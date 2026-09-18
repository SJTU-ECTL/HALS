"""Recover saved sample-exact ALS checkpoints omitted by the model export guard.

The frozen engine already accepts these points using its sample-exact exception,
but Eval's export guard applies the uncorrected model intercept a second time.
Re-evaluate every output bit on the original SELF ensemble before recovery.
The original search, model, log and trajectory files remain unchanged.
"""
from __future__ import annotations
import hashlib
import json
import re
import time
from pathlib import Path
from .hals_comparison import write_json
from .hals_comparison_synthesis import LIB,liberty_areas


def recovery_is_covered(out,bench,metric):
    records=list((out/'als/C'/bench/metric).glob('*/sample_exact_recovery.json'))
    if not records:return True
    path=out/'dse/C'/bench/metric/'fronts.json'
    if not path.exists():return False
    groups=json.loads((out/'structure'/bench/'hals/partitions.json').read_text())
    fronts=json.loads(path.read_text());indices={g['name']:i for i,g in enumerate(groups)}
    for record in records:
        front=fronts[indices[record.parent.name]]
        for candidate in json.loads(record.read_text())['candidates']:
            if not any(c['error']<=0 and c['area']<=candidate['area']+1e-9 for c in front):return False
    return True


def simulate_words(path,inputs,mask):
    lines=[];buffer=''
    for line in Path(path).read_text().splitlines():
        if line.endswith('\\'):buffer+=line[:-1]+' ';continue
        lines.append(buffer+line);buffer=''
    values={};outputs=[];pending=[];i=0
    while i<len(lines):
        words=lines[i].split();i+=1
        if not words:continue
        if words[0]=='.inputs':
            if len(words)-1!=len(inputs):raise ValueError('Recovery input width mismatch')
            values.update(zip(words[1:],inputs))
        elif words[0]=='.outputs':outputs=words[1:]
        elif words[0]=='.names':
            fanins=words[1:-1];target=words[-1];cubes=[];polarity=True
            while i<len(lines) and not lines[i].startswith('.'):
                truth=lines[i].split();i+=1
                if not truth or truth[0].startswith('#'):continue
                cube,bit=(truth[0],truth[1]) if fanins else ('',truth[0])
                if len(cube)!=len(fanins) or any(c not in '01-' for c in cube):raise ValueError('Invalid recovery cube')
                if cubes and polarity!=(bit=='1'):raise ValueError('Mixed BLIF cover polarity')
                cubes.append(cube);polarity=bit=='1'
            pending.append((fanins,target,cubes,polarity))
        elif words[0] in ('.latch','.gate','.subckt'):raise ValueError('Recovery expects Boolean combinational BLIF')
    while pending:
        waiting=[]
        for fanins,target,cubes,polarity in pending:
            if any(n not in values for n in fanins):waiting.append((fanins,target,cubes,polarity));continue
            result=0
            for cube in cubes:
                term=mask
                for n,c in zip(fanins,cube):
                    if c!='-':term&=values[n] if c=='1' else mask^values[n]
                result|=term
            values[target]=result if polarity or not cubes else mask^result
        if len(waiting)==len(pending):raise ValueError('Cyclic or undriven recovery BLIF')
        pending=waiting
    return [values[n] for n in outputs]


def recover_sample_exact(work,config,known,feature_count):
    if config.get('candidateValidationPolicy')!='complete_size_gain':return []
    name=config['module'];paths=[]
    for p in (work/'als_results'/name).glob('*_aig.blif'):
        if p.name.startswith('0_') or str(p) in known:continue
        if re.search(r'_(?:MAPE|NMSE)_0_eval_',p.name):paths.append(p)
    if not paths:return []
    paths.sort(key=lambda p:int(p.name.split('_',1)[0]))
    start=time.time();exact=Path(config['blifPath']);pattern=Path(config['patternFile'])
    digest=hashlib.sha256(exact.read_bytes()+pattern.read_bytes()+LIB.read_bytes()+str(config['nFrame']).encode())
    for p in paths:digest.update(str(p).encode()+p.read_bytes()+Path(str(p)+'.mapped.blif').read_bytes())
    signature=digest.hexdigest();record=work/'sample_exact_recovery.json'
    if record.exists():
        cached=json.loads(record.read_text())
        if cached['signature']==signature:return cached['candidates']
    n=config['nFrame'];bits=sum(v['width'] for v in config['IO'].values() if v['direction']=='input')
    patterns=pattern.read_text().splitlines()[:n]
    if len(patterns)!=n or any(len(p)<bits or set(p[:bits])-set('01') for p in patterns):raise ValueError('Invalid SELF patterns')
    inputs=[sum((p[i]=='1')<<j for j,p in enumerate(patterns)) for i in range(bits)];mask=(1<<n)-1
    reference=simulate_words(exact,inputs,mask);areas=liberty_areas();areas.update({'_const0_':0.,'_const1_':0.})
    candidates=[]
    for p in paths:
        if simulate_words(p,inputs,mask)!=reference:raise RuntimeError(f'Saved zero-error checkpoint is not sample-exact: {p}')
        mapped=Path(str(p)+'.mapped.blif')
        area=sum(areas[line.split()[1]] for line in mapped.read_text().splitlines() if line.startswith('.gate '))
        candidates.append(dict(area=area,error=0.,features=[0.]*feature_count,netlist=str(p),round=int(p.name.split('_',1)[0]),
            recovered_sample_exact=True,local_area_source='actual_saved_mapped_cell_sum'))
    write_json(record,dict(status='ok',signature=signature,patterns=n,exact=str(exact),candidates=candidates,
        note='Sample-exact is not a claim of functional equivalence; every selected full design still requires both validation ensembles.',elapsed_sec=time.time()-start))
    write_json(work/'sample_exact_recovery.timing.json',dict(step='recover_and_revalidate_omitted_sample_exact_checkpoints',start_unix=start,elapsed_sec=time.time()-start))
    return candidates
