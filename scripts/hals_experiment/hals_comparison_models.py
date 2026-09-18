"""Configuration-disjoint data and ICCAD local/global power regression."""
from __future__ import annotations
import hashlib
import json
import time
import warnings
from pathlib import Path

import numpy as np
import pandas as pd
from scipy.optimize import least_squares
from sklearn.metrics import r2_score
from sklearn.neural_network import MLPRegressor

from .hals_comparison import BOUNDS, ROOT, run, write_json

EXACT=(0,0,1)


def semantic_op(op,width):
    mode,a,b=map(int,op);mask=(1<<width)-1
    if mode==0:return EXACT
    if mode in (1,2,3):
        a &= mask
        if a==0:return EXACT
        return (mode,a,1)
    if mode==4:
        a &= mask
        return EXACT if a==mask else (1,mask^a,1)
    if mode==5:
        a &= mask
        return EXACT if a==0 else (5,a,1)
    if mode==6:return EXACT if a==0 else (6,a,1)
    if mode==7:
        if a==0:return (1,mask,1)
        if a==b:return EXACT
        import math
        divisor=math.gcd(a,b);return (7,a//divisor,b//divisor)
    if mode==8:return (1,mask,1)
    if mode==9:
        b &= mask
        return semantic_op((1,a,1),width) if b==0 else (9,a&mask,b)
    return (mode,a,b)


def configs_text(path,configs):
    path.write_text(''.join(' '.join(str(x) for op in c for x in op)+'\n' for c in configs))


def new_op(rng,bench,width):
    maxbits=min(width,16 if width<=20 else 32)
    bits=int(rng.integers(0,maxbits+1))
    if bench in ('fft','cholesky','decimation','interp'):
        return (int(rng.integers(1,3)),(1<<bits)-1,1) if bits else EXACT
    if bench in ('atax','bicg'):
        # The current generator combines low-bit truncation with additive bias.
        # Opcode 9 has the same bit-exact wrap semantics.
        bits=int(rng.integers(0,9))
        bias=int(rng.integers(-8,9)) if bits else 0
        return (9,(1<<bits)-1,bias & ((1<<64)-1)) if bits else EXACT
    from scripts.polybench_error_model import random_unsigned_approx_config
    cfg=random_unsigned_approx_config(rng,width,maxbits)
    mode=cfg['mode']
    if mode=='trunc': return (1,(1<<cfg['bits'])-1,1) if cfg['bits'] else EXACT
    if mode=='zero': return (8,0,1)
    if mode=='shift_down': return (6,cfg['shift'],1)
    if mode=='xor_mask': return (3,cfg['mask'],1)
    if mode=='and_mask': return (4,cfg['mask'],1)
    if mode=='offset': return (5,cfg['offset'] & ((1<<64)-1),1)
    if mode=='scale_num': return (7,cfg['num'],cfg['den'])
    raise ValueError(mode)


def local_pools(manifest,bench,new=False,seed=2027,local_samples=128):
    rng=np.random.default_rng(seed)
    pools=[]
    for k in manifest['kernels']:
        indices=k['scalar_indices']; widths=[manifest['scalars'][i]['width'] for i in indices]
        pool={tuple([EXACT]*len(indices))}
        for j,width in enumerate(widths):
            for bits in range(1,width+1):
                for mode in ([1,2] if new and bench in ('fft','cholesky','decimation','interp') else [1]):
                    cfg=[EXACT]*len(indices);cfg[j]=(mode,(1<<bits)-1,1);pool.add(tuple(cfg))
        if len(indices)>1 or new:
            for attempt in range(local_samples*20):
                if len(pool)>=max(local_samples,2*len(indices)*16): break
                cfg=tuple(new_op(rng,bench,w) if new else ((1,(1<<int(rng.integers(0,w+1)))-1,1)) for w in widths)
                cfg=tuple(EXACT if op[0]==1 and op[1]==0 else op for op in cfg)
                pool.add(cfg)
        pools.append(sorted(pool))
    return pools


def generate(bench,variant,out,patterns=10000,seed=2027,joint_samples=2048,local_samples=128):
    started=time.time()
    partition='hals' if variant=='C' else 'current'
    struct=out/'structure'/bench
    manifest=json.loads((struct/partition/'simulator/manifest.json').read_text())
    work=out/'data'/variant/bench;work.mkdir(parents=True,exist_ok=True)
    refdir=out/'references'/bench;refdir.mkdir(parents=True,exist_ok=True)
    ref=refdir/f'patterns{patterns}_seed{seed}.bin'
    # Different variants share this exact frozen input stream.  Atomic locking
    # is performed by the caller scheduling one benchmark's variants serially.
    if not ref.exists():
        run([str(struct/'reference/simulator/obj/Vstudy_top'),'unused','unused',str(patterns),str(seed),str(ref),''],refdir,f'reference_{patterns}_{seed}')
    ns=len(manifest['scalars']);zero=tuple([EXACT]*ns)
    pools=local_pools(manifest,bench,variant=='B',seed,local_samples)
    configs=[zero];poolrows=[]
    for kernel,pool in zip(manifest['kernels'],pools):
        rows=[]
        for cfg in pool:
            if all(op==EXACT for op in cfg): rows.append(0);continue
            row=list(zero)
            for i,op in zip(kernel['scalar_indices'],cfg): row[i]=op
            rows.append(len(configs));configs.append(tuple(row))
        poolrows.append(rows)
    configs_text(work/'local.configs',configs)
    pattern_dir=work/'als_patterns';pattern_dir.mkdir(exist_ok=True)
    binary=struct/partition/'simulator/obj/Vstudy_top'
    run([str(binary),str(work/'local.configs'),str(work/'local.csv'),str(patterns),str(seed),str(ref),str(pattern_dir)],work,'local_simulation')
    local=pd.read_csv(work/'local.csv')
    admissible=[]
    for k,rows in zip(manifest['kernels'],poolrows):
        features=[f's{i}_MAPE' for i in k['scalar_indices']]
        # HALS paper identifies local MAPE <= 30% as its modeling regime.
        keep=[j for j,r in enumerate(rows) if float(local.loc[r,features].max())<=0.3]
        admissible.append(keep or [0])
    rng=np.random.default_rng(seed+777)
    joint={tuple([0]*len(pools))}
    maxproduct=int(np.prod([len(x) for x in admissible],dtype=object))
    target=min(joint_samples,maxproduct)
    for attempt in range(max(10000,target*100)):
        if len(joint)>=target:break
        ids=[int(rng.choice(a)) for a in admissible]
        # Mix sparse and dense combinations; all selections are real local
        # configurations with already measured isolation-system errors.
        if attempt%2==0:
            active=set(rng.choice(len(pools),size=int(rng.integers(1,len(pools)+1)),replace=False))
            ids=[v if i in active else pools[i].index(tuple([EXACT]*len(manifest['kernels'][i]['scalar_indices']))) for i,v in enumerate(ids)]
        joint.add(tuple(ids))
    selections=sorted(joint)
    jointcfg=[]
    for ids in selections:
        row=list(zero)
        for k,pool,j in zip(manifest['kernels'],pools,ids):
            for i,op in zip(k['scalar_indices'],pool[j]):row[i]=op
        jointcfg.append(tuple(row))
    configs_text(work/'global.configs',jointcfg)
    run([str(binary),str(work/'global.configs'),str(work/'global.csv'),str(patterns),str(seed),str(ref),''],work,'global_simulation')
    info=dict(benchmark=bench,variant=variant,partition=partition,patterns=patterns,input_seed=seed,
              local_metric='MAPE',target_metrics=list(BOUNDS),manifest=manifest,pools=pools,poolrows=poolrows,
              joint_selections=selections,local_configs=len(configs),joint_configs=len(jointcfg),
              semantic_joint_capacity=maxproduct,local_regime_mape=0.3,
              elapsed_sec=time.time()-started)
    write_json(work/'dataset.json',info)
    return info


def predict(model,x):
    x=np.atleast_2d(x)
    if model['backend']=='hals_power':
        y=np.full(len(x),model['intercept'])
        y+=np.sum(np.asarray(model['alpha'])*np.maximum(x/np.asarray(model['input_scale']),0)**np.asarray(model['beta']),axis=1)
    elif model['backend']=='mlp_relu':
        a=(x-np.asarray(model['input_scaler']['mean']))/np.asarray(model['input_scaler']['scale'])
        for layer in model['layers']:
            a=a@np.asarray(layer['weights'])+np.asarray(layer['bias'])
            if layer['activation']=='relu':a=np.maximum(a,0)
        y=model['output_scaler']['mean']+model['output_scaler']['scale']*a[:,0]
    else:raise ValueError(model['backend'])
    return np.maximum(y,0)


def score(y,p):
    y=np.asarray(y);p=np.asarray(p)
    return dict(n=len(y),r2=float(r2_score(y,p)) if len(y)>1 and np.var(y)>1e-30 else None,
                rmse=float(np.sqrt(np.mean((p-y)**2))),mae=float(np.mean(abs(p-y))))


def split_indices(keys):
    # Semantic configurations, never shuffled CSV positions, determine split.
    strings=[json.dumps(k,sort_keys=True) for k in keys]
    unique=sorted(set(strings),key=lambda k:hashlib.sha256(k.encode()).hexdigest())
    labels={k:0 if int(hashlib.sha256(k.encode()).hexdigest()[:8],16)/2**32<.65 else 1 if int(hashlib.sha256(k.encode()).hexdigest()[:8],16)/2**32<.8 else 2 for k in unique}
    if len(unique)>=5 and len(set(labels.values()))<3:
        a=max(1,int(.65*len(unique)));b=max(a+1,int(.8*len(unique)))
        labels={k:0 if i<a else 1 if i<b else 2 for i,k in enumerate(unique)}
    membership=np.array([labels[k] for k in strings])
    train,val,test=[np.flatnonzero(membership==i) for i in range(3)]
    return train,val,test


def fit_model(x,y,keys,feature_cols,allow_fallback=True):
    t0=time.time();x=np.asarray(x);y=np.asarray(y)
    tr,va,te=split_indices(keys)
    if not len(tr):tr=np.arange(len(y))
    if not len(va):va=tr
    n=x.shape[1]
    scale=np.maximum(np.max(x[tr],axis=0),1e-15)
    sy=max(float(np.max(y[tr])),1e-12)
    z=x[tr]/scale;t=y[tr]/sy
    def fun(p):return p[-1]+(p[:n]*np.maximum(z,0)**p[n:2*n]).sum(axis=1)-t
    def jac(p):
        xp=np.maximum(z,0)**p[n:2*n]
        lg=np.log(np.maximum(z,1e-300))
        return np.column_stack([xp,xp*lg*p[:n],np.ones(len(z))])
    fits=[]
    for beta in (0.5,1.,2.):
        p0=np.r_[np.full(n,1/max(n,1)),np.full(n,beta),0.]
        fit=least_squares(fun,p0,jac=jac,bounds=(np.r_[np.zeros(n),np.full(n,.05),-1.],np.r_[np.full(n,100.),np.full(n,5.),1.]),max_nfev=1500)
        fits.append(fit)
    best=min(fits,key=lambda f:np.mean(fun(f.x)**2));p=best.x
    power=dict(backend='hals_power',prediction_semantics='clamped_raw',feature_cols=feature_cols,
               alpha=(p[:n]*sy).tolist(),beta=p[n:2*n].tolist(),input_scale=scale.tolist(),intercept=float(p[-1]*sy))
    chosen=power
    pv=score(y[va],predict(power,x[va]));fallback=None
    reliable=(pv['r2'] is None and pv['rmse']<1e-10) or (pv['r2'] is not None and pv['r2']>.85 and abs(power['intercept'])<.05*sy)
    # The paper recommends a neural-network fallback.  Selection uses only the
    # validation configurations; final test labels never choose the backend.
    if allow_fallback and not reliable and len(tr)>=8 and np.std(y[tr])>1e-15:
        mean=x[tr].mean(axis=0);xs=np.maximum(x[tr].std(axis=0),1e-12)
        ym=float(y[tr].mean());ys=max(float(y[tr].std()),1e-12)
        mlp=MLPRegressor(hidden_layer_sizes=(64,32),solver='lbfgs',random_state=2027,max_iter=600,max_fun=30000,alpha=1e-3,tol=1e-8)
        with warnings.catch_warnings():
            warnings.simplefilter('ignore');mlp.fit((x[tr]-mean)/xs,(y[tr]-ym)/ys)
        fallback=dict(backend='mlp_relu',prediction_semantics='clamped_raw',feature_cols=feature_cols,
                      input_scaler=dict(mean=mean.tolist(),scale=xs.tolist()),output_scaler=dict(mean=ym,scale=ys),
                      layers=[dict(weights=w.tolist(),bias=b.tolist(),activation='relu' if i<len(mlp.coefs_)-1 else 'linear') for i,(w,b) in enumerate(zip(mlp.coefs_,mlp.intercepts_))])
        if score(y[va],predict(fallback,x[va]))['rmse'] < pv['rmse']:chosen=fallback
    report=dict(train_n=len(tr),validation_n=len(va),test_n=len(te),
                local_or_global_input_dimensions=n,selected_backend=chosen['backend'],
                power_validation=pv,power_test=score(y[te],predict(power,x[te])) if len(te) else None,
                selected_validation=score(y[va],predict(chosen,x[va])),
                selected_test=score(y[te],predict(chosen,x[te])) if len(te) else None,
                power_reliable=reliable,power_gamma=power['intercept'],
                elapsed_sec=time.time()-t0,split=dict(train=tr.tolist(),validation=va.tolist(),test=te.tolist()))
    return chosen,power,fallback,report


def train(bench,variant,out):
    started=time.time();data=out/'data'/variant/bench;work=out/'models'/variant/bench;work.mkdir(parents=True,exist_ok=True)
    info=json.loads((data/'dataset.json').read_text());local=pd.read_csv(data/'local.csv');joint=pd.read_csv(data/'global.csv')
    summaries=[]
    for metric in BOUNDS:
        models=[];reports=[];isolated=[];predicted=[]
        for gi,(k,rows,pool) in enumerate(zip(info['manifest']['kernels'],info['poolrows'],info['pools'])):
            cols=[f's{i}_MAPE' for i in k['scalar_indices']]
            x=local.loc[rows,cols].to_numpy();y=local.loc[rows,'overall_'+metric].to_numpy()
            good=np.flatnonzero(x.max(axis=1)<=.3)
            widths=[info['manifest']['scalars'][i]['width'] for i in k['scalar_indices']]
            canonical=[tuple(semantic_op(op,w) for op,w in zip(c,widths)) for c in pool]
            model,power,mlp,report=fit_model(x[good],y[good],[canonical[i] for i in good],cols)
            report.update(kernel=k['name'],metric=metric,regime_rows=len(good),all_rows=len(rows))
            write_json(work/f'{metric}_local_{gi}.json',model)
            write_json(work/f'{metric}_local_{gi}_power.json',power)
            write_json(work/f'{metric}_local_{gi}_report.json',report)
            models.append(model);reports.append(report)
            choices=[s[gi] for s in info['joint_selections']]
            isolated.append(y[choices]);predicted.append(predict(model,x[choices]))
        x=np.stack(isolated,axis=1);xp=np.stack(predicted,axis=1);y=joint['overall_'+metric].to_numpy()
        keys=[]
        for selection in info['joint_selections']:
            key=[]
            for k,pool,j in zip(info['manifest']['kernels'],info['pools'],selection):
                widths=[info['manifest']['scalars'][i]['width'] for i in k['scalar_indices']]
                key.append([semantic_op(op,w) for op,w in zip(pool[j],widths)])
            keys.append(key)
        gm,gpower,gmlp,greport=fit_model(x,y,keys,[k['name'] for k in info['manifest']['kernels']])
        write_json(work/f'{metric}_global.json',gm);write_json(work/f'{metric}_global_power.json',gpower)
        te=greport['split']['test'];gp=predict(gm,x);ep=predict(gm,xp)
        endscore=score(y[te],ep[te]) if te else None
        audit={}
        for bound in BOUNDS[metric]:
            yt=y[te];pt=ep[te];accepted=pt<=bound
            audit[str(bound)]=dict(test_n=len(te),actual_feasible=int(np.sum(yt<=bound)),
                predicted_feasible=int(np.sum(accepted)),false_accept=int(np.sum(accepted & (yt>bound))),
                false_reject=int(np.sum((~accepted)&(yt<=bound))))
        greport.update(benchmark=bench,variant=variant,metric=metric,end_to_end_test=endscore,bound_audit=audit,local_models=reports)
        write_json(work/f'{metric}_report.json',greport)
        pd.DataFrame(dict(true=y,global_with_measured_local=gp,end_to_end=ep,split=['test' if i in te else 'development' for i in range(len(y))])).to_csv(work/f'{metric}_predictions.csv',index=False)
        summaries.append(greport)
    write_json(work/'training.timing.json',dict(step='model_training',elapsed_sec=time.time()-started))
    return summaries
