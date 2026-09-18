"""Prove full mapping, including register transitions, with explicit state alignment."""
from __future__ import annotations
import fcntl
import hashlib
import json
import re
import time
from pathlib import Path
from .hals_comparison import ROOT,run,write_json
from .hals_fair_synthesis import LIB,ABC


class BooleanNet:
    def __init__(self,path):
        self.lines=Path(path).read_text().splitlines();self.parent={};self.registers=[];self.register_lines=set()
        def union(a,b):self.parent[self.root(a)]=self.root(b)
        for i,line in enumerate(self.lines):
            v=line.split()
            if not v:continue
            if v[0]=='.names':
                for n in v[1:]:self.root(n)
                if len(v)==3 and i+1<len(self.lines) and self.lines[i+1].strip()=='1 1':union(v[1],v[2])
            if v[0]=='.latch':
                if len(v)!=6 or v[3] not in ('re','fe'):raise ValueError(f'Unsupported latch: {line}')
                self.registers.append(dict(q=v[2],d=v[1],clock=v[4],edge=v[3],init=v[5],reset=None));self.register_lines.add(i)
            if v[0] in ('.subckt','.gate'):
                pins=dict(p.split('=',1) for p in v[2:])
                m=re.fullmatch(r'\$_DFF_([NP])([NP])([01])_',v[1])
                if not m:raise ValueError(f'Unexpanded or unsupported state cell: {line}')
                self.registers.append(dict(q=pins['Q'],d=pins['D'],clock=pins['C'],edge='re' if m[1]=='P' else 'fe',init='2',reset=pins['R'],reset_polarity=m[2],reset_value=m[3]));self.register_lines.add(i)
        for r in self.registers:self.root(r['q'])
        self.aliases={}
        for n in list(self.parent):self.aliases.setdefault(self.root(n),set()).add(n)
    def root(self,x):
        self.parent.setdefault(x,x)
        path=[];node=x
        while self.parent[node]!=node:
            path.append(node);node=self.parent[node]
        for item in path:self.parent[item]=node
        return node
    def cut(self,path,order):
        extra_inputs=[];extra_outputs=[];extra_logic=[]
        for index,ri in enumerate(order):
            r=self.registers[ri];state=f'__fair_state_{index}'
            extra_inputs.append(state);extra_logic += [f'.names {state} {r["q"]}','1 1']
            for pin,key in [('D','d'),('C','clock'),('R','reset')]:
                signal=r[key]
                if signal is None:continue
                output=f'__fair_next_{index}_{pin}';extra_outputs.append(output)
                polarity='0 1' if pin=='R' and r['reset_polarity']=='N' else '1 1'
                extra_logic += [f'.names {signal} {output}',polarity]
        result=[]
        for i,line in enumerate(self.lines):
            if i in self.register_lines:continue
            if line.startswith('.inputs'):line+=' '+' '.join(extra_inputs)
            if line.startswith('.outputs'):line+=' '+' '.join(extra_outputs)
            if line.strip()=='.end':result+=extra_logic
            result.append(line)
        Path(path).write_text('\n'.join(result)+'\n')


def align_and_cut(gold_path,gate_path,work):
    gold,gate=BooleanNet(gold_path),BooleanNet(gate_path)
    if len(gold.registers)!=len(gate.registers):raise RuntimeError(f'Mapping changed register count: {len(gold.registers)} / {len(gate.registers)}')
    names={}
    for i,r in enumerate(gold.registers):
        for n in gold.aliases[gold.root(r['q'])]:
            if n in names:raise RuntimeError('Multiple gold registers share one state wire')
            names[n]=i
    matched={};bindings=[]
    for j,r in enumerate(gate.registers):
        matches={names[n] for n in gate.aliases[gate.root(r['q'])] if n in names}
        if len(matches)!=1:raise RuntimeError(f'Cannot uniquely align state {r["q"]}: {matches}')
        i=matches.pop()
        if i in matched:raise RuntimeError('Non-bijective register mapping')
        matched[i]=j;g=gold.registers[i]
        if g['edge']!=r['edge'] or g['init']!=r['init'] or (g['reset'] is None)!=(r['reset'] is None) or g.get('reset_value')!=r.get('reset_value'):
            raise RuntimeError(f'Register edge, initialization, or reset semantics differ: {g} / {r}')
        bindings.append(dict(gold=g,gate=r))
    gold.cut(work/'gold_cut.blif',range(len(gold.registers)))
    gate.cut(work/'gate_cut.blif',[matched[i] for i in range(len(gold.registers))])
    write_json(work/'state_alignment.json',dict(register_count=len(bindings),bindings=bindings,
        proof_obligation='For every primary input and current-state assignment, all primary outputs, next-state D, clocks and normalized asynchronous resets agree. State bijection and reset values/edge polarities/initial values checked.'))
    return len(bindings)


def verify_mapping(mapped,out,*,reference=None):
    mapped=Path(mapped);generic=Path(reference) if reference is not None else mapped.parent.parent/'generic.blif'
    if not generic.exists():raise FileNotFoundError(f'Missing pre-mapping reference: {generic}')
    mapped_json=mapped.with_suffix('.json')
    if not mapped_json.exists():raise FileNotFoundError(mapped_json)
    sig=hashlib.sha256(generic.read_bytes()+b'\0'+mapped.read_bytes()+mapped_json.read_bytes()+LIB.read_bytes()).hexdigest()
    work=out/'mapping_verification'/sig;work.mkdir(parents=True,exist_ok=True)
    with (work/'verify.lock').open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        if (work/'result.json').exists():return json.loads((work/'result.json').read_text())
        start=time.time()
        for name,source in [('gold',generic),('gate',mapped)]:
            # The mapped JSON retains register initialization attributes that
            # BLIF's .subckt syntax cannot express. It is the authoritative
            # mapped design, and is also the artifact used for cell counting.
            read=(f'read_json {mapped_json}; read_liberty -overwrite -ignore_miss_func {LIB}'
                  if name=='gate' else f'read_liberty -ignore_miss_func {LIB}; read_blif {source}')
            command=f'{read}; hierarchy -top study_top; proc; flatten; opt_clean; techmap; opt_clean; abc -g AND; opt_clean; write_blif {work}/{name}.blif'
            run(['yosys','-p',command],work,name+'_expand')
        count=align_and_cut(work/'gold.blif',work/'gate.blif',work)
        command=[str(ABC),'-c',f'cec -T 300 {work}/gold_cut.blif {work}/gate_cut.blif']
        if reference is not None:command=['timeout','300']+command
        run(command,work,'cec')
        if 'Networks are equivalent' not in (work/'cec.log').read_text():raise RuntimeError(f'Full mapping proof failed: {work}')
        result=dict(status='ok',signature=sig,mapped=str(mapped),mapped_json=str(mapped_json),generic=str(generic),registers=count,
                    elapsed_sec=time.time()-start,proof='ABC CEC of primary outputs and normalized register transition functions under bijective state alignment')
        write_json(work/'result.json',result);return result


def watch(out,jobs=2):
    from concurrent.futures import ThreadPoolExecutor
    pending={};seen={};failures={};start=time.time()
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        while True:
            paths=list((out/'area/exact').glob('*/area.json'))+list((out/'area/C').glob('*/*/*/result.json'))
            for f,path in list(pending.items()):
                if not f.done():continue
                del pending[f]
                try:
                    proof=f.result();seen[str(path)]=proof
                    write_json(path.parent/'full_mapping_verification.json',proof)
                    print('mapping_verified',path,proof['registers'],proof['elapsed_sec'],flush=True)
                except Exception as e:
                    failures[str(path)]=str(e);print('mapping_failed',path,str(e),flush=True)
            scheduled={str(p) for p in pending.values()}
            for path in paths:
                if len(pending)>=jobs:break
                if str(path) in seen or str(path) in failures or str(path) in scheduled:continue
                result=json.loads(path.read_text())
                if path.name=='result.json':
                    marker=out/'workflow'/f'{result["benchmark"]}_{result["metric"]}.timing.json'
                    if (result.get('postprocess_revision')!=2 or not marker.exists()
                            or json.loads(marker.read_text()).get('postprocess_revision')!=2):continue
                net=result['best']['netlist'] if path.name=='area.json' else result['mapped_netlist']
                pending[pool.submit(verify_mapping,net,out)]=path
            complete=len(seen)==45 and not failures
            write_json(out/'mapping_verification_status.json',dict(status='complete' if complete else 'running',
                elapsed_sec=time.time()-start,verified=len(seen),pending=len(pending),failures=failures))
            if complete:break
            if failures and not pending and len(seen)+len(failures)==45:raise RuntimeError('Mapping proof failures remain')
            time.sleep(5)


if __name__=='__main__':
    import argparse
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);p.add_argument('--jobs',type=int,default=2)
    a=p.parse_args();watch(a.out.resolve(),a.jobs)
