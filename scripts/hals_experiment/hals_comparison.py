"""Reproducible ICCAD HALS ablation: explicit RTL partitions and two-level models.

All generated artifacts are experiment-local.  The source RTL is frozen before
partition extraction.  HALS partitions are output cones terminating at register,
memory, or primary-input boundaries; shared ancestors may be duplicated.
"""
from __future__ import annotations

import argparse
import copy
import csv
import json
import os
import re
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BENCHMARKS = ('atax', 'bicg', 'cholesky', 'conv3x3', 'decimation', 'fft', 'gesummv', 'interp', 'syr2k')
BOUNDS = {'MAPE': [0.01, 0.05], 'NMSE': [0.001, 0.01]}


def write_json(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    # Yosys 0.9 writes small RTLIL constants as JSON integers, but read_json
    # recreates those at 32 bits.  Restore width-sensitive FF parameters.
    if isinstance(data,dict) and 'modules' in data:
        for mod in data['modules'].values():
            for cell in mod.get('cells',{}).values():
                if seq_cell(cell):
                    params=cell['parameters']
                    for key,value in list(params.items()):
                        if isinstance(value,int) and (key.endswith('_POLARITY') or key.endswith('_VALUE')):
                            width=1 if key.endswith('_POLARITY') else params.get('WIDTH',32)
                            if isinstance(width,str): width=int(width,2)
                            params[key]=format(value & ((1<<width)-1),f'0{width}b')
    with tempfile.NamedTemporaryFile('w', dir=path.parent, prefix=path.name+'.', suffix='.tmp', delete=False) as stream:
        stream.write(json.dumps(data, indent=2) + '\n')
        temporary = stream.name
    os.replace(temporary, path)


def run(command, work, label, cwd=None):
    work = Path(work)
    work.mkdir(parents=True, exist_ok=True)
    started = time.time()
    with (work / (label + '.log')).open('w') as stream:
        result = subprocess.run(command, cwd=cwd or ROOT, stdout=stream, stderr=subprocess.STDOUT)
    record = dict(step=label, command=command, start_unix=started,
                  elapsed_sec=time.time()-started, returncode=result.returncode)
    write_json(work / (label + '.timing.json'), record)
    if result.returncode:
        raise RuntimeError(f'{label}: exit {result.returncode}; {work / (label + ".log")}')
    return record


def seq_cell(cell):
    return bool(re.search(r'dff|latch|mem', cell['type'], re.I))


def flatten(bench, work):
    example = ROOT / 'examples' / bench
    wrapper = next(example.glob('*wrapper.v'))
    sources = [wrapper, *sorted((example / 'kernels').glob('*.v'))]
    stage = work / 'source'
    stage.mkdir(parents=True, exist_ok=True)
    for source in sources:
        body = re.sub(r'^\s*`include\s+"[^"\n]+\.v".*$', '', source.read_text(), flags=re.M)
        (stage / source.name).write_text(body)
    for source in (example / 'kernels').glob('*.vh'):
        (stage / source.name).write_bytes(source.read_bytes())
    top = re.search(r'\bmodule\s+(\w+)', wrapper.read_text()).group(1)
    cmd = f'read_verilog -sv -I{stage} ' + ' '.join(str(stage / p.name) for p in sources)
    cmd += f'; hierarchy -top {top}; proc; memory; opt_clean; write_json {work}/hierarchy.json; flatten; opt_clean; write_json {work}/flat.json'
    run(['yosys', '-p', cmd], work, 'flatten')
    return top, json.loads((work / 'flat.json').read_text())['modules'][top], json.loads((work / 'hierarchy.json').read_text())['modules']


def current_groups(bench, flat, hierarchy, top):
    active = json.loads((ROOT/'examples'/bench/(bench+'_model')/'active_metric.json').read_text())
    kernel_names = list(active['kernel_features'])
    found = {}
    def visit(mod, prefix=''):
        for cname, cell in hierarchy[mod]['cells'].items():
            typ = cell['type']
            path = prefix + cname
            if typ in kernel_names:
                child = hierarchy[typ]
                outputs = [path + '.' + name for name, p in child['ports'].items() if p['direction']=='output']
                inputs = [path + '.' + name for name, p in child['ports'].items() if p['direction']=='input']
                found[typ] = dict(name=typ, outputs=outputs, inputs=inputs, rule='existing module boundary')
            elif typ in hierarchy:
                visit(typ, path + '.')
    visit(top)
    assert set(found)==set(kernel_names), (bench, kernel_names, found)
    return [found[n] for n in kernel_names]


def hals_groups(bench, current, flat):
    def g(name, outputs, reason):
        return dict(name=name, outputs=outputs, rule=reason)
    if bench in ('atax', 'bicg', 'gesummv'):
        # Keep the original memory-update/output clusters.  Extract their full
        # cones, so an output cluster does not consume another cluster's error.
        return [dict(g(x['name'], x['outputs'], 'memory update/output cluster; full ancestor cone'),
                     preferred_inputs=x['inputs']) for x in current]
    if bench in ('conv3x3', 'syr2k'):
        names = [n for n,p in flat['ports'].items() if p['direction']=='output']
        return [g('hals_'+bench, names, 'single-output arithmetic DAG; no internal memory/branch cut')]
    if bench=='decimation':
        return [dict(g(x['name'], x['outputs'], 'FIR stage bounded by history registers'),
                     preferred_inputs=x['inputs']) for x in current[:4]] + [
            g('hals_decim_stage5', ['stage5_out'], 'entire stage 5, including partial-sum merge and Q8.8 output slice')]
    if bench=='interp':
        return [g('hals_interp_k'+str(i+1), ['interp_helper.odata'+str(i)],
                  'one full output cone; merge partial sums') for i in range(4)]
    if bench=='fft':
        # Adjacent same-component outputs of the final butterfly maximize
        # ancestor overlap. Real/imag outputs at one index can be disjoint.
        outputs = [n for n,p in flat['ports'].items() if p['direction']=='output']
        assert len(outputs)==32
        by_index={tuple(map(int,re.search(r'sample_out_(\d+)_(\d+)$',name).groups())):name for name in outputs}
        return [g('hals_fft_pair'+str(2*i+component),
                  [by_index[(2*i,component)],by_index[(2*i+1,component)]],
                  'same-component final butterfly output pair; maximal ancestor overlap; full cones')
                for i in range(8) for component in range(2)]
    if bench=='cholesky':
        return [
            g('hals_cholesky_div0', ['k0_l10','k0_l20'], 'two column-0 values stored at S_K0'),
            g('hals_cholesky_residual1', ['k1_diff1','k1_diff2'], 'S_K0 residual outputs; duplicate division ancestors'),
            g('hals_cholesky_column1', ['k1_l21','k2_diff'], 'S_K1 outputs; shared sequential sqrt remains exact'),
        ]
    raise ValueError(bench)


def extract(flat, group):
    cells = flat['cells']
    drivers = {}
    for name, cell in cells.items():
        for port, bits in cell['connections'].items():
            if cell['port_directions'][port]=='output':
                for bit in bits:
                    if isinstance(bit,int): drivers[bit]=name
    netnames = flat['netnames']
    outnets = [netnames[n] for n in group['outputs']]
    if 'inputs' in group:
        barriers = {b for n in group['inputs'] for b in netnames[n]['bits']}
    else:
        barriers = {b for p in flat['ports'].values() if p['direction']=='input' for b in p['bits']}
        barriers.update(group.get('memory_read_barrier_bits',[]))
        for c in cells.values():
            if seq_cell(c):
                barriers.update(b for p,bs in c['connections'].items() if c['port_directions'][p]=='output' for b in bs)
    chosen = set()
    leaves = set()
    def visit(bit):
        if not isinstance(bit,int): return
        if bit in barriers or bit not in drivers:
            leaves.add(bit); return
        cn = drivers[bit]
        if cn in chosen: return
        assert not seq_cell(cells[cn]), (group['name'],cn)
        chosen.add(cn)
        for p,bs in cells[cn]['connections'].items():
            if cells[cn]['port_directions'][p]=='input':
                for b in bs: visit(b)
    for net in outnets:
        for b in net['bits']: visit(b)
    # Preserve source input vector order when possible; remaining leaves are
    # scalar ports.  The generated IO manifest is authoritative for ALS.
    inputs=[]
    covered=set()
    for n in group.get('inputs', group.get('preferred_inputs', [])):
        bs = netnames[n]['bits']
        if all(isinstance(b,int) and b in leaves and b not in covered for b in bs) and len(set(bs))==len(bs):
            inputs.append((n,bs)); covered.update(bs)
    for n, net in netnames.items():
        if n.startswith('$'): continue
        bs=net['bits']
        if bs and all(isinstance(b,int) and b in leaves and b not in covered for b in bs) and len(set(bs))==len(bs):
            inputs.append((n,bs)); covered.update(bs)
    for b in sorted(leaves-covered): inputs.append(('bit'+str(b),[b]))
    ports={f'i{i}':dict(direction='input',bits=bs) for i,(n,bs) in enumerate(inputs)}
    ports.update({f'o{i}':dict(direction='output',bits=n['bits'],signed=n.get('signed',0)) for i,n in enumerate(outnets)})
    module=dict(attributes={}, ports=ports,cells={n:copy.deepcopy(cells[n]) for n in chosen},netnames={})
    module['netnames']={n:dict(hide_name=0,bits=p['bits'],attributes={},signed=p.get('signed',0)) for n,p in ports.items()}
    descriptor=dict(**group, input_sources=[n for n,bs in inputs], io={n:dict(direction=p['direction'],width=len(p['bits']),signed=p.get('signed',0)) for n,p in ports.items()},cell_count=len(chosen))
    return module, descriptor


def fft_partition_review(flat, groups):
    outputs=[n for n,p in flat['ports'].items() if p['direction']=='output']
    ancestors={name:set(extract(flat,dict(name='one',outputs=[name]))[0]['cells']) for name in outputs}
    def similarity(a,b):
        return len(ancestors[a]&ancestors[b])/max(len(ancestors[a]|ancestors[b]),1)
    chosen=[]
    for group in groups:
        a,b=group['outputs'];score=similarity(a,b)
        maxima=[max(similarity(n,x) for x in outputs if x!=n) for n in (a,b)]
        if any(score+1e-12<value for value in maxima):
            raise RuntimeError(f'FFT output pair requires structural review: {a}, {b}')
        chosen.append(dict(kernel=group['name'],outputs=[a,b],jaccard=score,
                           maximum_jaccard_per_output=maxima,union_operations=len(ancestors[a]|ancestors[b])))
    return dict(method='Manual same-component final-butterfly pairs; each pair attains mutual maximum ancestor Jaccard',
                selection_criterion='Structural ancestor overlap only, independent of measured approximation errors or areas',
                chosen_pairs=chosen,outputs=outputs,
                jaccard_matrix=[[similarity(a,b) for b in outputs] for a in outputs],
                superseded_real_imag_jaccard=[similarity(f'sample_out_{i}_0',f'sample_out_{i}_1') for i in range(16)])


def assemble(flat, groups, modules, injected=False):
    top=copy.deepcopy(flat)
    top['attributes']={}
    maxbit=max(b for n in top['netnames'].values() for b in n['bits'] if isinstance(b,int))+1
    # Disconnect old drivers, leaving consumers at their original bit IDs.
    targetbits={b for mod in modules.values() for p in mod['ports'].values() if p['direction']=='output' for b in p['bits'] if isinstance(b,int)}
    cutmap={b:maxbit+i for i,b in enumerate(sorted(targetbits))}; maxbit+=len(cutmap)
    for c in top['cells'].values():
        for p,bs in c['connections'].items():
            if c['port_directions'][p]=='output': c['connections'][p]=[cutmap.get(b,b) for b in bs]
    for g in groups:
        mod=modules[g['name']]
        top['cells']['study_'+g['name']]=dict(hide_name=0,type=g['name'],parameters={},attributes={},port_directions={n:p['direction'] for n,p in mod['ports'].items()},connections={n:p['bits'] for n,p in mod['ports'].items()})
    # Remove aliases of disconnected old drivers only through opt_clean.  The
    # skeleton retains the original register/control/memory implementation.
    return {'creator':'HALS comparison','modules':{'study_top':top,**modules}}


def prepare_one(bench, out):
    start=time.time()
    work=out/'structure'/bench
    work.mkdir(parents=True,exist_ok=True)
    top,flat,hierarchy=flatten(bench,work)
    current=current_groups(bench,flat,hierarchy,top)
    partitionings={'current':current,'hals':hals_groups(bench,current,flat)}
    if bench=='fft':
        write_json(work/'partition_review.json',fft_partition_review(flat,partitionings['hals']))
    if bench in ('atax','bicg','gesummv'):
        outputbits={b for g in current for n in g['outputs'] for b in flat['netnames'][n]['bits']}
        # A mapped memory read is a mux of registers.  Its output, rather than
        # every stored bit/address decoder, is the IR memory-access boundary.
        readbits={b for g in current for n in g['inputs'] for b in flat['netnames'][n]['bits'] if b not in outputbits}
        for g in partitionings['hals']:g['memory_read_barrier_bits']=sorted(b for b in readbits if isinstance(b,int))
    for label,groups in partitionings.items():
        dest=work/label; dest.mkdir(exist_ok=True)
        modules={}; descriptors=[]
        for group in groups:
            mod,descriptor=extract(flat,group)
            # Yosys 0.9 JSON omits port signedness. Arithmetic cell parameters
            # retain bit-exact behavior, while metric decoding needs this ABI.
            if bench in ('decimation','fft','interp'):
                for name,p in mod['ports'].items():
                    if p['direction']=='output':
                        p['signed']=1
                        mod['netnames'][name]['signed']=1
                        descriptor['io'][name]['signed']=1
            modules[group['name']]=mod; descriptors.append(descriptor)
            write_json(dest/(group['name']+'.json'), {'creator':'HALS comparison','modules':{group['name']:mod}})
        design=assemble(flat,groups,modules)
        write_json(dest/'design.json',design)
        run(['yosys','-p',f'read_json {dest}/design.json; hierarchy -top study_top; opt_clean; write_verilog -noattr {dest}/design.v; write_json {dest}/clean.json'],dest,'assemble')
        write_json(dest/'partitions.json',descriptors)
    write_json(work/'prepare.timing.json',dict(step='partition_prepare',elapsed_sec=time.time()-start))
    return dict(benchmark=bench,current=len(current),hals=len(partitionings['hals']))


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stage',choices=['prepare'])
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--benchmarks',nargs='+',default=list(BENCHMARKS),choices=BENCHMARKS)
    parser.add_argument('--jobs',type=int,default=8)
    args=parser.parse_args(argv)
    args.out=args.out.resolve(); args.out.mkdir(parents=True,exist_ok=True)
    write_json(args.out/'protocol.json',dict(benchmarks=args.benchmarks,bounds=BOUNDS,
        variants={'A':'current partition + truncation + HALS','B':'current partition + new operators + HALS','C':'HALS partition + truncation + HALS'},
        halstrunc='low-bit clearing; user-requested deviation from paper rounding',
        partition='manual output clusters, actual extracted arithmetic cones',
        error_model='sum alpha_i * epsilon_i ** beta_i + gamma; local then global',
        area_ratio='final flattened mapped approximate wrapper area / exact original wrapper area'))
    results=[]
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures={pool.submit(prepare_one,b,args.out):b for b in args.benchmarks}
        for f in as_completed(futures):
            try: row=f.result(); row['status']='ok'
            except Exception as e: row=dict(benchmark=futures[f],status='failed',error=str(e))
            results.append(row); print(json.dumps(row),flush=True)
    write_json(args.out/'prepare_summary.json',results)
    return int(any(r['status']!='ok' for r in results))


if __name__=='__main__':
    raise SystemExit(main())
