"""Check partition equivalence by canonical RTL cell expressions, without SAT.

Combinational cells are opaque, typed bit-vector operators. Only identical
operators with identical parameters and recursively identical inputs match.
State cells keep their original identities; all state inputs, parameters and
initialization bits must match. This is sufficient, not complete equivalence.
"""
from __future__ import annotations
import hashlib
import json
import re
from pathlib import Path
from .hals_comparison import seq_cell

PURE_OPERATORS={'$add','$and','$div','$eq','$le','$mul','$mux','$ne','$not','$or',
                '$pmux','$reduce_or','$shl','$shr','$sub','$xor'}


def digest(value):
    return hashlib.sha256(json.dumps(value,sort_keys=True,separators=(',',':')).encode()).hexdigest()


class Expressions:
    def __init__(self,design,top):
        self.modules=design['modules'];self.parent={};self.cells=[];self.initializers=[];self.alias_records=[]
        self.memo={};self.base_memo={};self.visiting=set()
        module=self.modules[top];self.ports=module['ports']
        self.visit(top,('TOP',),{})
        self.pis={};self.drivers={};self.states={};self.initial={};self.aliases={};self.undriven={}
        for signal,label in self.alias_records:self.aliases.setdefault(self.root(signal),set()).add(label)
        for p,port in self.ports.items():
            if port['direction']!='input':continue
            for i,b in enumerate(port['bits']):
                signal=self.root(self.signal(('TOP',),b));tag=('pi',p,i)
                if signal in self.pis and self.pis[signal]!=tag:raise ValueError('Aliased primary input ports')
                self.pis[signal]=tag
        for signal,value in self.initializers:
            signal=self.root(signal)
            if signal in self.initial and self.initial[signal]!=value:raise ValueError('Conflicting initialization')
            self.initial[signal]=value
        for i,cell in enumerate(self.cells):
            for p,bits in cell['connections'].items():
                if cell['port_directions'][p]!='output':continue
                for j,b in enumerate(bits):
                    signal=self.root(b)
                    if signal[0]=='const':continue
                    if signal in self.drivers:raise ValueError('Multiple cell drivers')
                    self.drivers[signal]=(i,p,j)
            if seq_cell(cell):
                if cell['identity'] in self.states:raise ValueError('Duplicate state identity')
                self.states[cell['identity']]=i

    def root(self,x):
        self.parent.setdefault(x,x);path=[]
        while self.parent[x]!=x:path.append(x);x=self.parent[x]
        for p in path:self.parent[p]=x
        return x

    def union(self,a,b):
        a,b=self.root(a),self.root(b)
        if a==b:return
        if a[0]=='const' and b[0]=='const':raise ValueError('Conflicting constants')
        if a[0]=='const':self.parent[b]=a
        else:self.parent[a]=b

    def signal(self,path,bit):
        return ('wire',path,bit) if isinstance(bit,int) else ('const',bit)

    def visit(self,name,path,bindings):
        module=self.modules[name]
        if any(p['direction'] not in ('input','output') for p in module['ports'].values()):
            raise ValueError('Inout ports are outside this structural checker')
        for p,external in bindings.items():
            local=module['ports'][p]['bits']
            if len(local)!=len(external):raise ValueError('Hierarchical port width mismatch')
            for a,b in zip(local,external):self.union(self.signal(path,a),b)
        for netname,net in module.get('netnames',{}).items():
            self.alias_records += [(self.signal(path,b),(path,netname,i)) for i,b in enumerate(net['bits'])]
            if 'init' not in net.get('attributes',{}):continue
            bits=net['bits'];value=net['attributes']['init']
            value=format(value,f'0{len(bits)}b') if isinstance(value,int) else value
            if len(value)>len(bits) and any(c!='0' for c in value[:-len(bits)]):raise ValueError('Initialization overflow')
            value=value[-len(bits):].zfill(len(bits))
            self.initializers += [(self.signal(path,b),v) for b,v in zip(bits,reversed(value))]
        for name,cell in module.get('cells',{}).items():
            connections={p:[self.signal(path,b) for b in bits] for p,bits in cell['connections'].items()}
            kind=cell['type']
            if any(d not in ('input','output') for d in cell['port_directions'].values()):raise ValueError('Unsupported cell port direction')
            if kind in self.modules:
                if self.modules[kind].get('attributes',{}).get('blackbox') in ('1',1):raise ValueError('Blackbox in exact partition')
                self.visit(kind,path+(name,),connections)
            else:
                if not kind.startswith('$') or (kind not in PURE_OPERATORS and not seq_cell(cell)):
                    raise ValueError(f'Unsupported or nondeterministic primitive: {kind}')
                self.cells.append(dict(cell,connections=connections,identity=path+(name,)))

    def parameters(self,cell):
        result={}
        for name,value in cell.get('parameters',{}).items():
            numeric=(name.endswith(('_WIDTH','_SIGNED','_POLARITY')) or name in
                {'WIDTH','ABITS','SIZE','OFFSET','CE_OVER_SRST','CLK_ENABLE','ARST_VALUE','SRST_VALUE'})
            if numeric and isinstance(value,str) and re.fullmatch('[01]+',value):value=int(value,2)
            result[name]=value
        return result

    def shape(self,cell):
        return [(p,cell['port_directions'][p],len(bits)) for p,bits in sorted(cell['connections'].items())]

    def expression(self,signal):
        signal=self.root(signal)
        if signal in self.memo:return self.memo[signal]
        if signal[0]=='const':result=digest(signal)
        elif signal in self.pis:result=digest(self.pis[signal])
        elif signal not in self.drivers:
            # Memory-map mux leaves for out-of-range addresses can be undriven.
            # Preserve distinct, identically named symbols; never replace them
            # with zero or merge all unknown signals into one variable.
            names=sorted((name,i) for path,name,i in self.aliases.get(signal,()) if path==('TOP',))
            if not names:raise ValueError(f'Undriven signal without original top-level identity: {signal}')
            self.undriven[signal]=names;result=digest(('undriven',names))
        else:
            i,port,bit=self.drivers[signal];cell=self.cells[i]
            if seq_cell(cell):result=digest(('state',cell['identity'],port,bit))
            else:
                if i in self.visiting:raise ValueError('Combinational cycle')
                if i not in self.base_memo:
                    self.visiting.add(i)
                    inputs=[(p,[self.expression(b) for b in bits]) for p,bits in sorted(cell['connections'].items()) if cell['port_directions'][p]=='input']
                    self.base_memo[i]=digest((cell['type'],self.parameters(cell),self.shape(cell),inputs))
                    self.visiting.remove(i)
                result=digest(('cell-output',self.base_memo[i],port,bit))
        self.memo[signal]=result;return result

    def certificate(self):
        outputs={p:[self.expression(self.signal(('TOP',),b)) for b in port['bits']] for p,port in self.ports.items() if port['direction']=='output'}
        states={}
        for identity,i in sorted(self.states.items()):
            cell=self.cells[i]
            inputs=[(p,[self.expression(b) for b in bits]) for p,bits in sorted(cell['connections'].items()) if cell['port_directions'][p]=='input']
            init=[(p,[self.initial.get(self.root(b),'x') for b in bits]) for p,bits in sorted(cell['connections'].items()) if cell['port_directions'][p]=='output']
            states[json.dumps(identity)]=digest((cell['type'],self.parameters(cell),self.shape(cell),inputs,init))
        ports={p:(v['direction'],len(v['bits']),v.get('offset',0),v.get('upto',0)) for p,v in self.ports.items()}
        return dict(ports=ports,outputs=outputs,states=states)


def compare(original,partitioned,original_top=None):
    original_top=original_top or next(iter(original['modules']))
    gold=Expressions(original,original_top);gate=Expressions(partitioned,'study_top')
    a,b=gold.certificate(),gate.certificate()
    mismatches={category:[name for name in sorted(set(a[category])|set(b[category])) if a[category].get(name)!=b[category].get(name)] for category in a}
    return dict(status='identical' if not any(mismatches.values()) else 'not_structurally_identical',
        mismatches=mismatches,original_cells=len(gold.cells),partitioned_cells=len(gate.cells),
        primary_output_bits=sum(map(len,a['outputs'].values())),state_cells=len(a['states']),
        original_undriven_symbols=len(gold.undriven),partitioned_undriven_symbols=len(gate.undriven),
        proof='Identical typed RTL operator DAGs at every primary output and every state-cell input; state parameters, port shapes and initialization compared')


if __name__=='__main__':
    import argparse,time
    from .hals_comparison import BENCHMARKS,write_json
    p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);args=p.parse_args()
    rows=[];started=time.time();changed=False
    checker_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    for bench in BENCHMARKS:
        work=args.out/'structure'/bench;original=work/'flat.json';partitioned=work/'hals/clean.json';start=time.time()
        original_sha256=hashlib.sha256(original.read_bytes()).hexdigest();partitioned_sha256=hashlib.sha256(partitioned.read_bytes()).hexdigest()
        record=args.out/'structure_equivalence'/f'{bench}.json'
        if record.exists():
            cached=json.loads(record.read_text())
            if (cached.get('status')=='identical' and cached.get('checker_sha256')==checker_sha256
                    and cached['original_sha256']==original_sha256 and cached['partitioned_sha256']==partitioned_sha256):
                rows.append(cached);print(bench,'cached identical',flush=True);continue
        try:result=compare(json.loads(original.read_text()),json.loads(partitioned.read_text()))
        except Exception as e:result=dict(status='unsupported',error=str(e))
        result.update(benchmark=bench,elapsed_sec=time.time()-start,original=str(original.resolve()),partitioned=str(partitioned.resolve()),
            original_sha256=original_sha256,partitioned_sha256=partitioned_sha256,checker_sha256=checker_sha256)
        write_json(record,result)
        write_json(record.with_suffix('.timing.json'),dict(step='RTL_structural_equivalence',start_unix=start,elapsed_sec=result['elapsed_sec'],status=result['status']))
        changed=True;rows.append(result);print(bench,result['status'],result.get('mismatches',result.get('error')),flush=True)
    passed=all(r['status']=='identical' for r in rows)
    if changed or not (args.out/'structure_equivalence_status.json').exists():
        write_json(args.out/'structure_equivalence_status.json',dict(status='complete' if passed else 'needs_review',results=rows,elapsed_sec=time.time()-started,checker_sha256=checker_sha256))
    raise SystemExit(0 if passed else 1)
