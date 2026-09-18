"""Nangate45 area measurement, including mapped state flip-flops."""
from __future__ import annotations
import json
import copy
import re
import shutil
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from .hals_comparison import ROOT, run, write_json
from .hals_blif_verilog import boolean_verilog
from scripts.date2027_logic_synthesis import ABC_VARIANTS

LIB=ROOT/'examples/nangate_45nm_typ.lib'


def liberty_areas():
    return {name.strip():float(area) for name,area in re.findall(r'cell\s*\(\s*([^)]*)\)\s*\{.*?\barea\s*:\s*([0-9.eE+-]+)',LIB.read_text(),re.S)}


def map_area(source,top,work,variants=None,jobs=2):
    work.mkdir(parents=True,exist_ok=True);started=time.time()
    generic=work/'generic.blif'
    generic_strategy='standard_synth'
    try:
        run(['yosys','-p',f'read_verilog {source}; synth -top {top} -flatten; opt -full; write_blif {generic}'],work,'generic')
    except RuntimeError:
        if 'hash table exceeded maximum size' not in (work/'generic.log').read_text():raise
        # Yosys 0.9 can exhaust its hash table while merging a large set of
        # freshly bit-blasted cells. ABC first strashes the equivalent logic;
        # the same final optimization and eight technology maps still follow.
        generic_strategy='abc_before_post_techmap_merge'
        command=f'read_verilog {source}; synth -top {top} -flatten -run begin:fine; opt -fast -full; memory_map; opt -full; techmap; opt_expr; opt_clean; abc -fast; opt -fast; opt -full; write_blif {generic}'
        run(['yosys','-p',command],work,'generic_hash_fallback')
    areas=liberty_areas()
    def one(name):
        wd=work/name;wd.mkdir(exist_ok=True)
        script=wd/'abc.script';script.write_text(f'source {ROOT}/examples/abc.rc\n'+ABC_VARIANTS[name]+'\n')
        command=f'read_liberty -lib {LIB}; read_blif {generic}; hierarchy -top {top}; techmap; opt_clean; dfflibmap -liberty {LIB}; abc -liberty {LIB} -script {script}; clean; stat -liberty {LIB}; write_json {wd}/mapped.json; write_blif {wd}/mapped.blif'
        timing=run(['yosys','-p',command],wd,'map')
        net=json.loads((wd/'mapped.json').read_text())['modules'][top]
        counts={}
        for c in net['cells'].values():counts[c['type']]=counts.get(c['type'],0)+1
        unknown=set(counts)-set(areas)
        if unknown:raise RuntimeError(f'unmapped cell types {unknown}: {wd}')
        area=sum(areas[k]*v for k,v in counts.items())
        return dict(variant=name,area=area,cell_count=sum(counts.values()),cell_histogram=counts,
                    elapsed_sec=timing['elapsed_sec'],netlist=str(wd/'mapped.blif'))
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        futures={name:pool.submit(one,name) for name in list(variants or ABC_VARIANTS)}
        attempts=[]
        for name,future in futures.items():
            try: attempts.append(future.result())
            except Exception as e: attempts.append(dict(variant=name,status='failed',error=str(e)))
    successful=[r for r in attempts if 'area' in r]
    if not successful:
        write_json(work/'area.json',dict(status='failed',attempts=attempts))
        raise RuntimeError('all mapping variants failed: '+str(work))
    best=min(successful,key=lambda r:r['area'])
    result=dict(best=best,attempts=attempts,elapsed_sec=time.time()-started,
                generic_strategy=generic_strategy,
                area_definition='sum Nangate45 liberty cell areas, including mapped flip-flops',
                source=str(source),source_top=top)
    write_json(work/'area.json',result)
    return result


def kernel_blif(structure,partition,group):
    work=structure/partition/'als_exact'/group['name'];work.mkdir(parents=True,exist_ok=True)
    source=structure/partition/(group['name']+'.json')
    blif=work/'exact.blif'
    current=structure/'current'/source.name
    original=structure/'current/als_exact'/group['name']
    if partition=='hals' and current.exists() and (original/'exact.blif').exists():
        if json.loads(source.read_text())['modules']==json.loads(current.read_text())['modules']:
            started=time.time()
            for filename in ('exact.blif','exact.v'):
                if (original/filename).exists():shutil.copy2(original/filename,work/filename)
            write_json(work/'kernel_synthesis.timing.json',dict(step='kernel_synthesis',elapsed_sec=time.time()-started,
                       reused_from=str(original),reason='Identical partition must use the same AIG to avoid a synthesis-order confound'))
            return blif
    run(['yosys','-p',f'read_json {source}; synth -top {group["name"]} -flatten; abc -g AND; clean; write_blif {blif}; write_verilog {work}/exact.v'],work,'kernel_synthesis')
    # Make the bit order independent of Yosys' identifier sorting (i2/i10).
    text=blif.read_text();lines=text.splitlines();merged=[];buf=''
    for line in lines:
        buf+=line.rstrip('\\').strip()+' '
        if not line.endswith('\\'):merged.append(buf.strip());buf=''
    for direction,directive in [('input','.inputs'),('output','.outputs')]:
        old=next(l.split()[1:] for l in merged if l.startswith(directive+' '))
        wanted=[]
        for name,port in group['io'].items():
            if port['direction']!=direction:continue
            for i in range(port['width']):
                options=[f'{name}[{i}]',name] if port['width']==1 else [f'{name}[{i}]']
                wanted.append(next(x for x in options if x in old))
        assert set(wanted)==set(old)
        merged=[directive+' '+' '.join(wanted) if l.startswith(directive+' ') else l for l in merged]
    blif.write_text('\n'.join(merged)+'\n')
    return blif


def selected_wrapper(bench,variant,selection,out):
    partition='hals' if variant=='C' else 'current'
    structure=out/'structure'/bench/partition
    groups=json.loads((structure/'partitions.json').read_text())
    metric=selection['metric'];label=str(selection['bound']).replace('.','p')
    work=out/'area'/variant/bench/metric/label
    if (work/'result.json').exists():return json.loads((work/'result.json').read_text())
    work.mkdir(parents=True,exist_ok=True);start=time.time()
    original=json.loads((out/'area/exact'/bench/'area.json').read_text())
    if all(c['round']==-1 for c in selection['candidates']):
        result=dict(**selection,area=original['best']['area'],exact_area=original['best']['area'],
                    area_ratio=1.,area_source='original_exact_fallback',elapsed_sec=time.time()-start)
        write_json(work/'result.json',result);return result
    shell=json.loads((structure/'clean.json').read_text())
    for name,mod in shell['modules'].items():
        if name=='study_top':continue
        mod['attributes']={'blackbox':'1'};mod['cells']={}
    write_json(work/'shell.json',shell)
    run(['yosys','-p',f'read_json {work}/shell.json; write_verilog -noattr {work}/shell.v'],work,'wrapper_shell')
    chunks=[(work/'shell.v').read_text()]
    conversion_started=time.time()
    for gi,(g,candidate) in enumerate(zip(groups,selection['candidates'])):
        fixed=work/f'kernel{gi}.v'
        io=work/f'kernel{gi}.io.json';write_json(io,dict(module=g['name'],IO=g['io']))
        fixed.write_text(boolean_verilog(candidate['netlist'],g['name'],g['io']))
        chunks.append(fixed.read_text())
    write_json(work/'candidate_conversion.timing.json',dict(step='candidate_boolean_conversion',elapsed_sec=time.time()-conversion_started))
    source=work/'selected.v';source.write_text('\n'.join(chunks))
    mapped=map_area(source,'study_top',work/'mapping',jobs=2)
    area=mapped['best']['area'];exact_area=original['best']['area']
    result=dict(**selection,area=area,exact_area=exact_area,area_ratio=area/exact_area,
                area_source='selected_gate_level_wrapper',selected_source=str(source),
                abc_variant=mapped['best']['variant'],elapsed_sec=time.time()-start)
    if area>exact_area:
        # The original exact implementation is always a feasible alternative.
        result.update(rejected_larger_area=area,area=exact_area,area_ratio=1.,
                      rejected_actual_error=result['actual_error'],actual_error=0.,predicted_error=0.,
                      area_source='original_exact_area_fallback')
    write_json(work/'result.json',result)
    return result
