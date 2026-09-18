"""Symmetric full-design mapping with retained ALS standard-cell networks."""
from __future__ import annotations
import fcntl
import hashlib
import json
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from .hals_comparison import ROOT, run, write_json
from .hals_comparison_synthesis import LIB, liberty_areas
from .hals_blif_verilog import boolean_verilog
from scripts.date2027_logic_synthesis import ABC_VARIANTS

ABC = ROOT/'bin/abc'
VARIANTS = dict(ABC_VARIANTS, amap='st; amap; topo; stime',
    dch_amap='st; dch; amap; topo; stime',
    resyn2_amap='st; resyn2; amap; topo; stime',
    compress2rs_amap='st; compress2rs; amap; topo; stime')


def count_area(path, top):
    module=json.loads(path.read_text())['modules'][top]
    hist={}
    for cell in module['cells'].values():
        kind=cell['type'];hist[kind]=hist.get(kind,0)+1
    areas=liberty_areas()
    if set(hist)-set(areas):
        raise RuntimeError(f'Unmapped cells in {path}: {set(hist)-set(areas)}')
    return dict(area=sum(areas[k]*n for k,n in hist.items()),
                cell_count=sum(hist.values()),cell_histogram=hist)


def map_source(source, top, work, jobs=2, mapped_input=False):
    """No preliminary ABC rewrite: preserve the supplied graph until portfolio mapping."""
    record=work/'area.json'
    if record.exists(): return json.loads(record.read_text())
    start=time.time();work.mkdir(parents=True,exist_ok=True)
    generic=work/'generic.blif'
    lib=f'read_liberty -lib {LIB}; ' if mapped_input else ''
    command=f'{lib}read_verilog {source}; synth -top {top} -flatten -noabc; opt_clean; write_blif {generic}'
    run(['yosys','-p',command],work,'generic')
    def one(name,script_text):
        wd=work/name;wd.mkdir(exist_ok=True)
        script=wd/'abc.script';script.write_text(f'source {ROOT}/examples/abc.rc\n{script_text}\n')
        cmd=f'read_liberty -lib {LIB}; read_blif {generic}; hierarchy -top {top}; techmap; opt_clean; dfflibmap -liberty {LIB}; abc -exe {ABC} -liberty {LIB} -script {script}; clean; write_json {wd}/mapped.json; write_blif {wd}/mapped.blif'
        tm=run(['yosys','-p',cmd],wd,'mapping')
        return dict(variant=name,**count_area(wd/'mapped.json',top),elapsed_sec=tm['elapsed_sec'],netlist=str(wd/'mapped.blif'))
    # Existing library cells remain cells. Only wrapper logic still needs mapping.
    variants={'preserved_cells':VARIANTS['dch_amap']} if mapped_input else VARIANTS
    attempts=[]
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        futures={name:pool.submit(one,name,script) for name,script in variants.items()}
        for name,f in futures.items():
            try:attempts.append(f.result())
            except Exception as e:attempts.append(dict(variant=name,status='failed',error=str(e)))
    good=[a for a in attempts if 'area' in a]
    if not good:raise RuntimeError(f'All mapping routes failed: {work}: {attempts}')
    result=dict(best=min(good,key=lambda a:a['area']),attempts=attempts,elapsed_sec=time.time()-start,
        source=str(source),source_top=top,mapped_input=mapped_input,
        area_definition='sum of all Nangate45 cells, including flip-flops')
    write_json(record,result);return result


def retained_kernel(candidate, out):
    source=Path(candidate['netlist'])
    sidecar=Path(str(source)+'.mapped.blif')
    if sidecar.exists():return sidecar
    signature=hashlib.sha256(source.read_bytes()).hexdigest()
    wd=out/'kernel_mapping'/signature;wd.mkdir(parents=True,exist_ok=True)
    with (wd/'mapping.lock').open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        record=wd/'result.json'
        if record.exists():return Path(json.loads(record.read_text())['netlist'])
        attempts=[]
        # Also applies to each exact kernel: same mapping choices as approximate
        # checkpoints, without an ALS-specific delay limit on the baseline.
        for name,script in VARIANTS.items():
            target=wd/f'{name}.blif'
            cmd=f'source {ROOT}/examples/abc.rc; read_lib {LIB}; read_blif {source}; {script}; write_blif {target}; print_stats'
            tm=run([str(ABC),'-c',cmd],wd,name)
            if not target.exists():raise RuntimeError(f'ABC did not write {target}')
            hist={}
            for line in target.read_text().splitlines():
                if line.startswith('.gate '):
                    kind=line.split()[1];hist[kind]=hist.get(kind,0)+1
            areas=liberty_areas()
            areas.update({'_const0_':0., '_const1_':0.})
            if not hist:raise RuntimeError(f'Expected mapped gate BLIF: {target}')
            attempts.append(dict(variant=name,area=sum(areas[k]*v for k,v in hist.items()),netlist=str(target),elapsed_sec=tm['elapsed_sec']))
        best=min(attempts,key=lambda a:a['area'])
        write_json(record,dict(**best,attempts=attempts))
        return Path(best['netlist'])


def verify_conversion(original, fixed, name, out):
    sig=hashlib.sha256(Path(original).read_bytes()+b'\0'+fixed.read_bytes()+LIB.read_bytes()).hexdigest()
    wd=out/'verification'/sig;wd.mkdir(parents=True,exist_ok=True)
    with (wd/'verify.lock').open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        record=wd/'result.json'
        if record.exists():return json.loads(record.read_text())
        start=time.time()
        # Read functional cell models, then flatten them, to prove retained cell
        # conversion against the exact same Boolean candidate used in simulation.
        cmd=f'read_liberty -ignore_miss_func {LIB}; read_verilog {fixed}; hierarchy -top {name}; proc; flatten; opt; techmap; opt; abc -g AND; clean; write_blif {wd}/converted.blif'
        run(['yosys','-p',cmd],wd,'conversion')
        run([str(ABC),'-c',f'cec {original} {wd}/converted.blif'],wd,'cec')
        if 'Networks are equivalent' not in (wd/'cec.log').read_text():
            raise RuntimeError(f'Conversion failed CEC: {wd}')
        result=dict(status='ok',signature=sig,original=str(original),fixed=str(fixed),elapsed_sec=time.time()-start)
        write_json(record,result);return result


def assemble(bench,candidates,out,work):
    work.mkdir(parents=True,exist_ok=True)
    structure=out/'structure'/bench/'hals'
    groups=json.loads((structure/'partitions.json').read_text())
    shell=json.loads((structure/'clean.json').read_text())
    for name,mod in shell['modules'].items():
        if name!='study_top':mod.update(attributes={'blackbox':'1'},cells={})
    write_json(work/'shell.json',shell)
    run(['yosys','-p',f'read_json {work}/shell.json; write_verilog -noattr {work}/shell.v'],work,'shell')
    plain=[(work/'shell.v').read_text()];mapped=plain.copy();proofs=[]
    for gi,(g,c) in enumerate(zip(groups,candidates)):
        original=Path(c['netlist']); retained=retained_kernel(c,out)
        for route,blif,chunks,gates in [('boolean',original,plain,False),('retained',retained,mapped,True)]:
            fixed=work/f'{route}_kernel{gi}.v'
            fixed.write_text(boolean_verilog(blif,g['name'],g['io'],allow_gates=gates))
            chunks.append(fixed.read_text())
            proofs.append(verify_conversion(original,fixed,g['name'],out))
    (work/'boolean.v').write_text('\n'.join(plain))
    (work/'retained.v').write_text('\n'.join(mapped))
    write_json(work/'conversions.json',dict(status='ok',proofs=proofs))
    return work/'boolean.v',work/'retained.v'


def wrapper_portfolio(bench,candidates,out,work):
    start=time.time();record=work/'portfolio.json'
    if record.exists():return json.loads(record.read_text())
    plain,retained=assemble(bench,candidates,out,work/'assembly')
    routes=[map_source(plain,'study_top',work/'boolean_mapping'),
            map_source(retained,'study_top',work/'retained_mapping',mapped_input=True)]
    options=[dict(r['best'],source=r['source'],route='retained' if r['mapped_input'] else 'boolean') for r in routes]
    result=dict(best=min(options,key=lambda a:a['area']),routes=routes,elapsed_sec=time.time()-start)
    write_json(record,result);return result


def exact_baseline(bench,source,out):
    work=out/'area/exact'/bench;record=work/'area.json'
    if record.exists():return json.loads(record.read_text())
    start=time.time()
    old=json.loads((source/'area/exact'/bench/'area.json').read_text())
    rtl=map_source(Path(old['source']),'study_top',work/'original_rtl')
    groups=json.loads((out/'structure'/bench/'hals/partitions.json').read_text())
    exact=[dict(netlist=str(out/'structure'/bench/'hals/als_exact'/g['name']/'exact.blif'),round=-1) for g in groups]
    assembled=wrapper_portfolio(bench,exact,out,work/'exact_partitioned')
    options=[dict(old['best'],route='historical_original_exact'),dict(rtl['best'],route='enhanced_original_exact'),dict(assembled['best'],route='enhanced_exact_partitioned')]
    result=dict(best=min(options,key=lambda a:a['area']),attempts=options,elapsed_sec=time.time()-start,
        old_exact_area=old['best']['area'],source=old['source'],source_top='study_top')
    write_json(record,result);return result
