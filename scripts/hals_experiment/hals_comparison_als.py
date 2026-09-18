"""Run HALS local error models in ResubALS and greedy global-model DSE."""
from __future__ import annotations
import csv
import sys
import fcntl
import json
import os
import re
import time
from pathlib import Path

import numpy as np

from .hals_comparison import ROOT, BOUNDS, run, write_json
from .hals_comparison_models import predict,EXACT,configs_text


def check_cached_bounds(work, metric):
    config = json.loads((work / 'config.json').read_text())
    for key in ('errorBounds', 'designErrUppBounds'):
        if config.get(key) != BOUNDS[metric]:
            raise RuntimeError(f'Cached search constraint mismatch: {work}, {key}; use a new output directory')
    settings = next((p / 'search_settings.json' for p in work.parents
                     if (p / 'search_settings.json').exists()), None)
    if settings:
        for key, value in json.loads(settings.read_text()).items():
            if config.get(key) != value:
                raise RuntimeError(f'Cached search setting mismatch: {work}, {key}')


def launch_kernel(bench,variant,metric,gi,out,threads=4,max_round=0,max_cand=100000,smoke=False):
    partition='hals' if variant=='C' else 'current'
    group=json.loads((out/'structure'/bench/partition/'partitions.json').read_text())[gi]
    work=out/('als_smoke' if smoke else 'als')/variant/bench/metric/group['name']
    work.mkdir(parents=True,exist_ok=True)
    with (work/'run.lock').open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        # A scheduler may be restarted while a previous child is still finishing.
        # This also accommodates jobs launched before advisory locking was added.
        while True:
            status=work/'status.json'
            if status.exists() and json.loads(status.read_text()).get('status')=='ok':
                check_cached_bounds(work, metric)
                return json.loads(status.read_text())
            config=str(work/'config.json').encode()
            active=False
            for proc in Path('/proc').glob('[0-9]*/cmdline'):
                try:
                    argv=proc.read_bytes().split(b'\0')
                    if config in argv and any(a.endswith(b'/resubals.py') for a in argv):active=True;break
                except (FileNotFoundError,ProcessLookupError,PermissionError):pass
            if not active:break
            time.sleep(5)
        return _launch_kernel(bench,variant,metric,gi,out,threads,max_round,max_cand,smoke)


def _launch_kernel(bench,variant,metric,gi,out,threads=4,max_round=0,max_cand=100000,smoke=False):
    partition='hals' if variant=='C' else 'current'
    structure=out/'structure'/bench/partition
    groups=json.loads((structure/'partitions.json').read_text())
    group=groups[gi];name=group['name']
    work=out/('als_smoke' if smoke else 'als')/variant/bench/metric/name
    work.mkdir(parents=True,exist_ok=True)
    status=work/'status.json'
    if status.exists() and json.loads(status.read_text()).get('status')=='ok':
        check_cached_bounds(work, metric)
        return json.loads(status.read_text())
    widths=[p['width'] for p in group['io'].values() if p['direction']=='output']
    blif=structure/'als_exact'/name/'exact.blif'
    patterns=structure/'als_patterns'/(name+'.pattern')
    frames=min(4096,sum(1 for l in patterns.open() if l.strip()))
    frames=(frames//64)*64
    if frames<64:raise RuntimeError(f'insufficient ALS patterns: {patterns}')
    model=out/'models'/variant/bench/(metric+f'_local_{gi}.json')
    config=dict(module=name,IO=group['io'],blifPath=str(blif),patternFile=str(patterns),
        outputNum=len(widths),outputWidths=widths,isSigned=bench in ('fft','interp','decimation'),
        errorType='MAPE',errorBounds=BOUNDS[metric],errorBound=min(BOUNDS[metric]),
        designErrUppBounds=BOUNDS[metric],designErrUppBound=min(BOUNDS[metric]),
        designModelPath=str(model),featureIndices=list(range(len(widths))),
        featureMetrics=['ERROR_VECTOR_'+str(i) for i in range(len(widths))],
        initialFeatureVector=[0.]*len(widths),modelSafetyMargin=0.,scalarKernelFeature=False,
        nFrame=frames,distrType='SELF',seed=2027,maxRound=max_round,maxCandResub=max_cand,
        maxExactCandValidate=4,candidateValidationPolicy='size_gain',candidateFeatureSource='simulation',earlyExitPolicy='disabled')
    if len(groups)==1 and len(widths)==1:
        # ICCAD explicitly bypasses learned models for an indivisible scalar
        # design.  A linear unit conversion implements direct native NMSE.
        coefficient=1.
        if metric=='NMSE':
            info=json.loads((out/'data'/variant/bench/'dataset.json').read_text())
            ref=out/'references'/bench/f'patterns{info["patterns"]}_seed{info["input_seed"]}.bin'
            values=np.fromfile(ref,dtype=np.float64)
            norm=(1<<widths[0])-1
            coefficient=norm*norm/max(float(np.mean(values*values)),1e-30)
        model=work/'direct_metric.json'
        write_json(model,dict(backend='hals_power',prediction_semantics='clamped_raw',
            feature_cols=[metric],alpha=[coefficient],beta=[1.],input_scale=[1.],intercept=0.,
            note='Exact unit conversion; no learned error model for one scalar partition'))
        config.update(errorType=metric,designModelPath=str(model),featureMetrics=[metric])
    settings = out/'search_settings.json'
    if settings.exists():
        overrides = json.loads(settings.read_text())
        allowed = {'maxExactCandValidate', 'candidateValidationPolicy',
                   'candidateFeatureSource', 'maxCandResub', 'maxRound'}
        if set(overrides) - allowed:
            raise ValueError('Unsupported experiment search overrides')
        config.update(overrides)
    write_json(work/'config.json',config)
    write_json(status,dict(status='running',started_unix=time.time()))
    try:
        timing=run([sys.executable,str(ROOT/'scripts/resubals.py'),'--config',str(work/'config.json'),
                    '--working-dir',str(work),'--examples-dir',str(ROOT/'examples'),'--nthread',str(threads)],work,'resubals')
        # resubals.py writes its own terminal artifact next to the config.
        front=work/'als_results'/(name+'.trajectory.csv')
        if not front.exists():front=work/'als_results'/(name+'.csv')
        if not front.exists():front=None
        if front is None:raise RuntimeError(f'no ALS front in {work}')
        result=dict(status='ok',benchmark=bench,variant=variant,metric=metric,kernel=name,
                    kernel_index=gi,front=str(front),elapsed_sec=timing['elapsed_sec'],max_round=max_round,max_cand=max_cand,frames=frames)
        write_json(status,result);return result
    except Exception as e:
        write_json(status,dict(status='failed',error=str(e)));raise


def load_fronts(bench,variant,metric,out):
    partition='hals' if variant=='C' else 'current'
    groups=json.loads((out/'structure'/bench/partition/'partitions.json').read_text())
    fronts=[]
    for gi,g in enumerate(groups):
        path=out/'als'/variant/bench/metric/g['name']/'status.json'
        status=json.loads(path.read_text())
        if status['status']!='ok':raise RuntimeError('ALS incomplete: '+str(path))
        front_path=path.parent/'als_results'/(g['name']+'.trajectory.csv')
        if not front_path.exists():front_path=Path(status['front'])
        rows=list(csv.DictReader(front_path.open()))
        cfg=json.loads((path.parent/'config.json').read_text())
        local=json.loads(Path(cfg['designModelPath']).read_text())
        candidates=[]
        for row in rows:
            f=np.array([float(row['kernel_'+metric])],dtype=float) if len(groups)==1 else np.array(json.loads(row['error_vector']),dtype=float)
            estimate=float(predict(local,f)[0])
            # Sample-exact candidates carry zero true isolated error.
            if np.max(np.abs(f))==0:estimate=0.
            netlist=Path(row['aig_netlist'])
            if not netlist.is_absolute():netlist=ROOT/netlist
            if not netlist.is_file():raise FileNotFoundError(netlist)
            candidates.append(dict(area=float(row['area']),error=estimate,features=f.tolist(),netlist=str(netlist),round=int(row['round'])))
        from .hals_fair_recovery import recover_sample_exact
        candidates.extend(recover_sample_exact(path.parent,cfg,{c['netlist'] for c in candidates},len(local['feature_cols'])))
        exact=(out/'structure'/bench/partition/'als_exact'/g['name']/'exact.blif').resolve()
        # Preserve a guaranteed exact candidate even if the front dropped it.
        log=(path.parent/'als_results'/(g['name']+'.log')).read_text()
        match=re.search(r'current best: area = ([0-9.eE+-]+)',log)
        if not match:raise RuntimeError('missing initial exact mapped area: '+str(path))
        exact_candidate=dict(area=float(match.group(1)),error=0.,features=[0.]*len(local['feature_cols']),netlist=str(exact),round=-1)
        unique={c['netlist']:c for c in candidates}
        candidates=sorted(unique.values(),key=lambda c:(c['error'],c['area']))
        front=[exact_candidate];best=exact_candidate['area']
        for c in candidates:
            if c['area']<best-1e-9:front.append(c);best=c['area']
        fronts.append(front)
    return groups,fronts


def greedy(fronts,model,bound):
    # Choose the smallest zero-error point in each local Pareto front.
    state=[0]*len(fronts);history=[state.copy()]
    def error(indices):
        x=[fronts[i][j]['error'] for i,j in enumerate(indices)]
        return 0. if not any(x) else float(predict(model,x)[0])
    while True:
        previous=error(state);moves=[]
        for i,front in enumerate(fronts):
            for j in range(state[i]+1,len(front)):
                candidate=state.copy();candidate[i]=j;proposed=error(candidate)
                saving=front[state[i]]['area']-front[j]['area']
                if saving<=0 or proposed>bound:continue
                ratio=saving/max(proposed-previous,1e-15)
                moves.append((ratio,saving,-proposed,i,j,candidate))
        if not moves:break
        state=max(moves,key=lambda m:m[:5])[-1];history.append(state.copy())
    return history


def dse(bench,variant,metric,out,validation_patterns=65536,validation_seed=982451653):
    started=time.time();partition='hals' if variant=='C' else 'current'
    groups,fronts=load_fronts(bench,variant,metric,out)
    model=json.loads((out/'models'/variant/bench/f'{metric}_global.json').read_text())
    if len(groups)==1:
        model=dict(backend='hals_power',alpha=[1.],beta=[1.],input_scale=[1.],intercept=0.)
    work=out/'dse'/variant/bench/metric;work.mkdir(parents=True,exist_ok=True)
    histories={str(b):greedy(fronts,model,b) for b in BOUNDS[metric]}
    write_json(work/'fronts.json',fronts);write_json(work/'histories.json',histories)
    manifest=json.loads((out/'structure'/bench/partition/'simulator/manifest.json').read_text())
    # Validate the greedy result first and execute only required rollbacks.
    # Shared states across the two bounds are simulated once.
    bank=work/'candidates.txt'
    bank.write_text(''.join(f'{gi} {c["netlist"]}\n' for gi,f in enumerate(fronts) for c in f))
    ref=work/'reference.bin';structure=out/'structure'/bench
    run([str(structure/'reference/simulator/obj/Vstudy_top'),'unused','unused',str(validation_patterns),str(validation_seed),str(ref),''],work,'validation_reference')
    observed={};observations=[];batch=0;validation_elapsed=0.
    positions={str(b):len(histories[str(b)])-1 for b in BOUNDS[metric]}
    pending=set(positions)
    while pending:
        states=sorted({tuple(histories[b][positions[b]]) for b in pending}-set(observed))
        if states:
            from .hals_native_blif import compile_blif
            for state in states:
                for gi,j in enumerate(state):compile_blif(fronts[gi][j]['netlist'])
            configs=[]
            for state in states:
                cfg=[EXACT]*len(manifest['scalars'])
                for gi,j in enumerate(state):
                    for si in manifest['kernels'][gi]['scalar_indices']:cfg[si]=(10,j,1)
                configs.append(cfg)
            config_path=work/f'validate_{batch}.configs';result_path=work/f'validation_{batch}.csv'
            configs_text(config_path,configs)
            command=['env','HALS_CANDIDATE_BANK='+str(bank),str(structure/partition/'validation_simulator/obj/Vstudy_top'),
                     str(config_path),str(result_path),str(validation_patterns),str(validation_seed),str(ref),'']
            timing=run(command,work,f'gate_level_validation_{batch}');validation_elapsed+=timing['elapsed_sec']
            rows=list(csv.DictReader(result_path.open()))
            for state,row in zip(states,rows):
                observed[state]=float(row['overall_'+metric]);observations.append(dict(state=state,**row))
            batch+=1
        for b in list(pending):
            state=tuple(histories[b][positions[b]])
            if observed[state]<=float(b):pending.remove(b)
            elif positions[b]>0:positions[b]-=1
            else:raise RuntimeError('even exact fallback violates error bound')
    write_json(work/'observations.json',observations)
    selected=[]
    for bound in BOUNDS[metric]:
        history=histories[str(bound)];state=history[positions[str(bound)]]
        actual=observed[tuple(state)];rollbacks=len(history)-1-positions[str(bound)]
        selection=[fronts[i][j] for i,j in enumerate(state)]
        raw_prediction=float(predict(model,[c['error'] for c in selection])[0])
        selected.append(dict(benchmark=bench,variant=variant,metric=metric,bound=bound,
            actual_error=actual,predicted_error=raw_prediction if any(c['error'] for c in selection) else 0.,
            raw_model_prediction=raw_prediction,
            indices=state,rollbacks=rollbacks,candidates=selection,
            validated_states=len(observed),validation_patterns=validation_patterns,validation_seed=validation_seed))
    write_json(work/'selected.json',selected)
    write_json(work/'dse.timing.json',dict(step='dse_and_validation',elapsed_sec=time.time()-started,validation_elapsed_sec=validation_elapsed))
    return selected
