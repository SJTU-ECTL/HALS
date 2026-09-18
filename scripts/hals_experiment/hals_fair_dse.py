"""Frozen HALS greedy search, two validation ensembles, and retained-cell area."""
from __future__ import annotations
import csv
import fcntl
import json
import time
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor

from .hals_comparison import ROOT, BOUNDS, run, write_json
from .hals_comparison_als import load_fronts, greedy
from .hals_comparison_models import predict, EXACT, configs_text
from .hals_native_blif import compile_blif
from .hals_fair_synthesis import wrapper_portfolio
from .hals_fair_recovery import recovery_is_covered

ENSEMBLES=[(65536,982451653),(65536,32452843)]
POSTPROCESS_REVISION=2


def expand_history(history):
    """Keep every intermediate Pareto point along the same HALS greedy path.

    A greedy jump can skip hundreds of points. Validation rollback must not
    discard those otherwise eligible designs when the jump endpoint fails.
    """
    expanded=[history[0].copy()]
    for target in history[1:]:
        state=expanded[-1].copy()
        changed=[i for i,(a,b) in enumerate(zip(state,target)) if a!=b]
        if len(changed)!=1:raise ValueError('Expected one HALS greedy coordinate move')
        gi=changed[0]
        for j in range(state[gi]+1,target[gi]+1):
            point=state.copy();point[gi]=j;expanded.append(point)
    return expanded


def select(bench,metric,out,source):
    work=out/'dse/C'/bench/metric;work.mkdir(parents=True,exist_ok=True)
    if (work/'selected.json').exists():
        cached=json.loads((work/'selected.json').read_text())
        if all(r.get('postprocess_revision')==POSTPROCESS_REVISION for r in cached) and recovery_is_covered(out,bench,metric):return cached
        archive=out/f'postprocess_v{cached[0].get("postprocess_revision",1)}'
        for relative in (Path('dse/C')/bench/metric,Path('area/C')/bench/metric):
            old=out/relative;target=archive/relative
            if old.exists():
                target.parent.mkdir(parents=True,exist_ok=True)
                if target.exists():raise RuntimeError(f'Existing revision archive: {target}')
                old.rename(target)
        work.mkdir(parents=True,exist_ok=True)
    start=time.time();groups,fronts=load_fronts(bench,'C',metric,out)
    _,old_fronts=load_fronts(bench,'C',metric,source)
    for gi,(new,old) in enumerate(zip(fronts,old_fronts)):
        exact=new[0];merged={c['netlist']:c for c in new[1:]+old[1:]}
        best=exact['area'];fronts[gi]=[exact]
        for c in sorted(merged.values(),key=lambda c:(c['error'],c['area'])):
            if c['area']<best-1e-9:fronts[gi].append(c);best=c['area']
    model=json.loads((out/'models/C'/bench/f'{metric}_global.json').read_text())
    if len(groups)==1:model=dict(backend='hals_power',alpha=[1.],beta=[1.],input_scale=[1.],intercept=0.)
    sparse={str(b):greedy(fronts,model,b) for b in BOUNDS[metric]}
    histories={b:expand_history(h) for b,h in sparse.items()}
    write_json(work/'greedy_jump_histories.json',sparse)
    # Prior validated designs are retained as explicit incumbents. They receive
    # the same fresh error validation and mapping as new greedy results.
    old_states=[]
    for selection in json.loads((source/'dse/C'/bench/metric/'selected.json').read_text()):
        state=[]
        for gi,c in enumerate(selection['candidates']):
            found=next((j for j,x in enumerate(fronts[gi]) if x['netlist']==c['netlist']),None)
            if found is None:found=len(fronts[gi]);fronts[gi].append(c)
            state.append(found)
        old_states.append(tuple(state))
    write_json(work/'fronts.json',fronts);write_json(work/'histories.json',histories)
    bank=work/'candidates.txt'
    bank.write_text(''.join(f'{i} {c["netlist"]}\n' for i,f in enumerate(fronts) for c in f))
    manifest=json.loads((out/'structure'/bench/'hals/simulator/manifest.json').read_text())
    references=[]
    for n,seed in ENSEMBLES:
        ref=work/f'reference_{seed}.bin'
        run([str(out/'structure'/bench/'reference/simulator/obj/Vstudy_top'),'unused','unused',str(n),str(seed),str(ref),''],work,f'reference_{seed}')
        references.append(ref)
    observed={};observations=[];batch=0
    def evaluate(states):
        nonlocal batch
        states=sorted(set(tuple(s) for s in states)-set(observed))
        if not states:return
        paths={fronts[i][j]['netlist'] for state in states for i,j in enumerate(state)}
        with ThreadPoolExecutor(max_workers=4) as pool:list(pool.map(compile_blif,sorted(paths)))
        configs=[]
        for state in states:
            cfg=[EXACT]*len(manifest['scalars'])
            for gi,j in enumerate(state):
                for si in manifest['kernels'][gi]['scalar_indices']:cfg[si]=(10,j,1)
            configs.append(cfg)
        config_path=work/f'validate_{batch}.configs';configs_text(config_path,configs)
        measurements=[[] for _ in states]
        def ensemble(item):
            (n,seed),ref=item
            result_path=work/f'validation_{batch}_{seed}.csv'
            cmd=['env','HALS_CANDIDATE_BANK='+str(bank),str(out/'structure'/bench/'hals/validation_simulator/obj/Vstudy_top'),str(config_path),str(result_path),str(n),str(seed),str(ref),'']
            run(cmd,work,f'validation_{batch}_{seed}')
            rows=list(csv.DictReader(result_path.open()))
            if len(rows)!=len(states):raise RuntimeError('Validation row count mismatch')
            return n,seed,rows
        with ThreadPoolExecutor(max_workers=len(ENSEMBLES)) as pool:
            ensemble_rows=list(pool.map(ensemble,zip(ENSEMBLES,references)))
        for n,seed,rows in ensemble_rows:
            for i,row in enumerate(rows):measurements[i].append(dict(seed=seed,patterns=n,**row))
        for state,rows in zip(states,measurements):
            actual=max(float(r['overall_'+metric]) for r in rows)
            observed[state]=actual;observations.append(dict(state=state,actual_error=actual,ensembles=rows))
        write_json(work/'observations.json',observations);batch+=1
    positions={str(b):len(histories[str(b)])-1 for b in BOUNDS[metric]};pending=set(positions)
    evaluate(old_states+[tuple(0 for _ in fronts)])
    while pending:
        evaluate([histories[b][positions[b]] for b in pending])
        for b in list(pending):
            state=tuple(histories[b][positions[b]])
            if observed[state]<=float(b):pending.remove(b)
            elif positions[b]>0:positions[b]-=1
            else:raise RuntimeError('Exact state violates bound')
    results=[]
    for bound in BOUNDS[metric]:
        feasible=[s for s,e in observed.items() if e<=bound]
        state=min(feasible,key=lambda s:sum(fronts[i][j]['area'] for i,j in enumerate(s)))
        candidates=[fronts[i][j] for i,j in enumerate(state)]
        features=[c['error'] for c in candidates]
        results.append(dict(benchmark=bench,variant='C',metric=metric,bound=bound,
            postprocess_revision=POSTPROCESS_REVISION,
            candidates=candidates,indices=state,actual_error=observed[state],
            predicted_error=float(predict(model,features)[0]) if any(features) else 0.,
            ensembles=next(r['ensembles'] for r in observations if tuple(r['state'])==state),
            validation_ensembles=ENSEMBLES,validated_states=len(observed),
            greedy_rollbacks=len(histories[str(bound)])-1-positions[str(bound)],
            from_prior_incumbent=state in old_states))
    write_json(work/'selected.json',results)
    write_json(work/'dse.timing.json',dict(step='HALS_greedy_and_two_ensemble_validation',start_unix=start,elapsed_sec=time.time()-start))
    return results


def finish(bench,metric,out,source):
    locks=out/'locks';locks.mkdir(exist_ok=True)
    with (locks/f'{bench}_{metric}_postprocess.lock').open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        return _finish(bench,metric,out,source)


def _finish(bench,metric,out,source):
    marker=out/'workflow'/f'{bench}_{metric}.timing.json'
    paths=[out/'area/C'/bench/metric/str(b).replace('.','p')/'result.json' for b in BOUNDS[metric]]
    if marker.exists() and json.loads(marker.read_text()).get('postprocess_revision')==POSTPROCESS_REVISION and all(p.exists() for p in paths):
        cached=[json.loads(p.read_text()) for p in paths]
        if all(r.get('postprocess_revision')==POSTPROCESS_REVISION for r in cached) and recovery_is_covered(out,bench,metric):return cached
    start=time.time();selections=select(bench,metric,out,source)
    exact=json.loads((out/'area/exact'/bench/'area.json').read_text())['best']
    results=[]
    for s in selections:
        work=out/'area/C'/bench/metric/str(s['bound']).replace('.','p')
        record=work/'result.json'
        if record.exists():results.append(json.loads(record.read_text()));continue
        ts=time.time()
        if all(c['round']==-1 for c in s['candidates']):
            best=exact;area_source='optimized_exact'
        else:
            portfolio=wrapper_portfolio(bench,s['candidates'],out,work/'portfolio')
            best=portfolio['best'];area_source='optimized_approximate'
        r=dict(s,area=best['area'],exact_area=exact['area'],area_ratio=best['area']/exact['area'],
            mapped_netlist=best['netlist'],area_source=area_source,mapping_route=best['route'],elapsed_sec=time.time()-ts)
        if r['area']>=exact['area']:
            r.update(area=exact['area'],area_ratio=1.,rejected_actual_error=r['actual_error'],actual_error=0.,predicted_error=0.,
                mapped_netlist=exact['netlist'],area_source='optimized_exact',mapping_route=exact['route'])
        write_json(record,r);results.append(r)
    # A relaxed bound includes every validated design at the tighter bound.
    for i in range(1,len(results)):
        if results[i-1]['area']<results[i]['area']:
            bound=results[i]['bound'];elapsed=results[i]['elapsed_sec']
            results[i]=dict(results[i-1],bound=bound,elapsed_sec=elapsed,reused_tighter_bound=results[i-1]['bound'])
            write_json(out/'area/C'/bench/metric/str(bound).replace('.','p')/'result.json',results[i])
    write_json(out/'workflow'/f'{bench}_{metric}.timing.json',dict(step='postprocess_case',postprocess_revision=POSTPROCESS_REVISION,start_unix=start,elapsed_sec=time.time()-start))
    return results
