#!/usr/bin/env python3
"""Portable entry point for a self-contained HALS C or DATE release."""
from __future__ import annotations
import argparse, copy, csv, hashlib, json, os, re, shutil, subprocess, sys, tarfile, time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT))
sys.dont_write_bytecode=True

def read(p):return json.loads(Path(p).read_text())
def write(p,d):
 p=Path(p);p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(d,indent=2)+'\n')
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def expand(x,work):
 if isinstance(x,dict):return {k:expand(v,work) for k,v in x.items()}
 if isinstance(x,list):return [expand(v,work) for v in x]
 if isinstance(x,str):return x.replace('@ROOT@',str(ROOT)).replace('@ASSETS@',str(work/'assets'))
 return x

def command(argv,work,label):
 from scripts.hals_experiment.hals_comparison import run
 return run([str(x) for x in argv],work,label)

def verify(args):
 from scripts.hals_experiment.hals_fair_synthesis import count_area
 rows=expand(read(ROOT/'results/final.json'),args.work);areas={}
 for row in rows:
  if row['actual_error']>row['bound']:raise RuntimeError('Error bound violation')
  p=Path(row['mapped_json'])
  if p not in areas:areas[p]=count_area(p,'study_top')['area']
  if abs(areas[p]-row['area'])>1e-6:raise RuntimeError('Mapped cell area mismatch')
  if abs(row['area']/row['common_exact_area']-row['area_ratio'])>1e-12:raise RuntimeError('Area ratio mismatch')
  if row['ensembles']:
   actual=max(float(e['overall_'+row['metric']]) for e in row['ensembles'])
   if actual!=row['actual_error']:raise RuntimeError('Recorded ensemble mismatch')
 if len(rows)!=36:raise RuntimeError('Expected 36 final experimental results')
 print(json.dumps(dict(status='ok',method=read(ROOT/'release.json')['method'],final_results=len(rows),unique_circuits=len(areas))))

def unpack(work):
 target=work/'assets';archive=ROOT/'assets/candidates.tar.gz';marker=target/'unpacked.json'
 info=dict(size=archive.stat().st_size,mtime_ns=archive.stat().st_mtime_ns)
 if marker.exists() and read(marker)==info:return
 target.mkdir(parents=True,exist_ok=True)
 with tarfile.open(archive,'r|gz') as stream:
  for member in stream:
   path=target/member.name
   if not member.isfile() or not path.resolve().is_relative_to(target.resolve()):raise RuntimeError('Invalid archive member')
   path.parent.mkdir(parents=True,exist_ok=True)
   with stream.extractfile(member) as src,path.open('wb') as dst:shutil.copyfileobj(src,dst)
 write(marker,info)

def prepare(work,benchmarks,need_assets=True):
 info=read(ROOT/'release.json');partition='hals' if info['method']=='C' else 'current'
 work.mkdir(parents=True,exist_ok=True)
 if need_assets:unpack(work)
 for b in benchmarks:
  example=ROOT/'examples'/b;structure=work/'structure'/b
  if not (structure/partition).exists():shutil.copytree(example/'partition',structure/partition)
  for typ,name in [('reference','reference'),('training',partition),('validation',partition)]:
   kind='validation_simulator' if typ=='validation' else 'simulator';target=structure/name/kind
   if not target.exists():shutil.copytree(example/'simulators'/typ,target)
  model=(work/'models/C'/b if info['method']=='C' else work/'models'/b)
  if not model.exists():shutil.copytree(example/f'{b}_model',model)
  if info['method']=='C' and not (work/'data/C'/b).exists():shutil.copytree(example/'data',work/'data/C'/b)
  if need_assets:
   exact=expand(read(ROOT/'results/exact'/f'{b}.json'),work)
   write(work/'area/exact'/b/'area.json',dict(best=dict(area=exact['area'],netlist=exact['mapped_netlist'],source_group='released_common_exact',route='released_common_exact')))
 return partition

def simulator(work,b,typ,jobs=2):
 partition='hals' if read(ROOT/'release.json')['method']=='C' else 'current'
 kind='validation_simulator' if typ=='validation' else 'simulator'
 path=work/'structure'/b/('reference' if typ=='reference' else partition)/kind
 binary=path/'obj/Vstudy_top'
 if binary.exists():return binary
 flags='-O3 -I'+str(ROOT/'scripts/hals_experiment')
 command(['verilator','--cc','--exe','--build','-j',str(jobs),'-Wno-fatal','--top-module','study_top',
  '--Mdir',str(path/'obj'),'-CFLAGS',flags,'-LDFLAGS','-ldl',str(path/'sim.v'),str(path/'sim.cpp')],path,'build')
 return binary

def selected_rows(args):
 return [r for r in expand(read(ROOT/'results/final.json'),args.work) if r['benchmark'] in args.benchmarks
         and (args.profile=='all' or r['profile']==args.profile)]

def validate(args):
 from scripts.hals_experiment.hals_comparison_models import EXACT,configs_text
 from scripts.hals_experiment.hals_native_blif import compile_blif
 prepare(args.work,args.benchmarks)
 rows=selected_rows(args);results=[]
 def one_benchmark(b):
  reference=simulator(args.work,b,'reference',args.jobs);binary=simulator(args.work,b,'validation',args.jobs)
  part='hals' if read(ROOT/'release.json')['method']=='C' else 'current'
  manifest=read(args.work/'structure'/b/part/'validation_simulator/manifest.json')
  for row in [r for r in rows if r['benchmark']==b]:
   wd=args.work/'validation'/b/row['profile'];wd.mkdir(parents=True,exist_ok=True)
   cfg=[EXACT]*len(manifest['scalars']);lines=[]
   for i,c in enumerate(row['candidates']):
    if c.get('round')==-1:continue
    compile_blif(c['netlist']);lines.append(f'{i} {c["netlist"]}\n')
    for si in manifest['kernels'][i]['scalar_indices']:cfg[si]=(10,0,1)
   bank=wd/'candidates.txt';bank.write_text(''.join(lines));configs_text(wd/'configs.txt',[cfg]);measurements=[]
   for n,seed in read(ROOT/'release.json')['ensembles']:
    ref=args.work/'references'/b/f'{n}_{seed}.bin'
    if not ref.exists():command([reference,'unused','unused',n,seed,ref,''],ref.parent,f'reference_{seed}')
    output=wd/f'{seed}.csv'
    command(['env','HALS_CANDIDATE_BANK='+str(bank),binary,wd/'configs.txt',output,n,seed,ref,''],wd,f'validate_{seed}')
    measurements.append(next(csv.DictReader(output.open())))
   actual=max(float(r['overall_'+row['metric']]) for r in measurements)
   if abs(actual-row['actual_error'])>max(1e-12,abs(row['actual_error'])*1e-9):raise RuntimeError(f'Measured error changed: {b} {row["profile"]}: {actual} != {row["actual_error"]}')
   if actual>row['bound']:raise RuntimeError('Measured constraint violation')
   results.append(dict(benchmark=b,profile=row['profile'],actual_error=actual,bound=row['bound'],status='ok',ensembles=measurements))
   print('validated',b,row['profile'],actual,flush=True)
 with ThreadPoolExecutor(max_workers=min(args.jobs,len(args.benchmarks))) as pool:list(pool.map(one_benchmark,args.benchmarks))
 results.sort(key=lambda r:(r['benchmark'],r['profile']))
 write(args.work/'validation_summary.json',results)

def prove(args):
 from scripts.hals_experiment.hals_fair_verify import verify_mapping
 rows=selected_rows(args);unique={r['mapped_netlist']:r for r in rows}
 def one(row):
  result=verify_mapping(row['mapped_netlist'],args.work,reference=row['generic'])
  print('proved',row['benchmark'],row['profile'],flush=True);return result
 with ThreadPoolExecutor(max_workers=args.jobs) as pool:proofs=list(pool.map(one,unique.values()))
 write(args.work/'proof_summary.json',dict(status='ok',rows=len(rows),circuits=len(proofs),proofs=proofs))


def partition_check(args):
 from scripts.hals_experiment.hals_fair_structure import compare
 results=[]
 def one(b):
  source=ROOT/'examples'/b/'source';wrapper=next(source.glob('*wrapper.v'))
  files=[wrapper,*sorted((source/'kernels').glob('*.v'))]
  work=args.work/'partition_check'/b;stage=work/'source';stage.mkdir(parents=True,exist_ok=True)
  for f in files:
   text=re.sub(r'^\s*`include\s+"[^"\n]+\.v".*$', '', f.read_text(), flags=re.M)
   (stage/f.name).write_text(text)
  for f in (source/'kernels').glob('*.vh'):shutil.copy2(f,stage/f.name)
  top=re.search(r'\bmodule\s+(\w+)',wrapper.read_text()).group(1)
  script=f'read_verilog -sv -I{stage} '+ ' '.join(str(stage/f.name) for f in files)
  script+=f'; hierarchy -top {top}; proc; memory; opt_clean; flatten; opt_clean; write_json {work}/flat.json'
  command(['yosys','-p',script],work,'flatten')
  result=compare(read(work/'flat.json'),read(ROOT/'examples'/b/'partition/clean.json'),top)
  if result['status']!='identical':raise RuntimeError(f'Partition equivalence failed: {b}: {result}')
  result['benchmark']=b;write(work/'equivalence.json',result);print('partition_identical',b,flush=True);return result
 with ThreadPoolExecutor(max_workers=args.jobs) as pool:results=list(pool.map(one,args.benchmarks))
 write(args.work/'partition_summary.json',dict(status='ok',results=results))


def model_check(args):
 import numpy as np
 work=args.work/'model_check';work.mkdir(parents=True,exist_ok=True)
 binary=work/'predict'
 command(['g++','-O2','-std=c++17','-I'+str(ROOT/'resubals/src'),ROOT/'tools/model_probe.cc',ROOT/'resubals/src/design_error_model.cc','-o',binary],work,'compile')
 method=read(ROOT/'release.json')['method'];paths=[];results=[]
 if method=='DATE':
  os.environ.setdefault('HALS_RELEASE_WORK',str(args.work));os.environ.setdefault('HALS_RELEASE_BOUNDS',json.dumps({'MAPE':.05,'NMSE':.001}))
  from release_pipeline.campaign import Model
 else:
  from scripts.hals_experiment.hals_comparison_models import predict
 for b in args.benchmarks:
  models=ROOT/'examples'/b/f'{b}_model'
  if method=='DATE':paths += [f for m in ['MAPE','NMSE'] for f in sorted((models/m).glob('*.json')) if f.name!='original.json']
  else:paths += [models/f'{m}_global.json' for m in ['MAPE','NMSE']]+sorted((models/'als').glob('*/*.json'))
 for i,path in enumerate(paths):
  payload=read(path);rng=np.random.default_rng(2027);width=len(payload['feature_cols'])
  if method=='DATE':
   model=Model(payload);x=rng.uniform(model.lower,model.upper,size=(18,width));x[0]=0.;x[1]=model.initial
   expected=model.predict(x,guard=False)
  else:
   x=rng.uniform(0.,.3,size=(18,width));x[0]=0.;expected=predict(payload,x)
  data=work/f'{i}.txt';np.savetxt(data,x,fmt='%.17g');command([binary,path,data],work,f'predict_{i}')
  lines=[line.split()[1:] for line in (work/f'predict_{i}.log').read_text().splitlines() if line.startswith('PRED ')]
  actual=np.array([float(v[0]) for v in lines])
  if not np.allclose(actual,expected,atol=1e-10,rtol=1e-10):raise RuntimeError('C++/Python model mismatch: '+str(path))
  if method=='DATE' and not np.array_equal(np.array([v[1]=='1' for v in lines]),model.in_range(x)):raise RuntimeError('Range guard mismatch')
  results.append(dict(path=str(path.relative_to(ROOT)),rows=len(x),max_abs_diff=float(np.max(abs(actual-expected))),status='ok'))
 write(args.work/'model_parity_summary.json',dict(status='ok',models=len(results),results=results))
 print('model_parity_ok',len(results),flush=True)


def build(args):
 command(['cmake','-S',ROOT,'-B',args.work/'build','-DCMAKE_BUILD_TYPE=Release',
  '-DCMAKE_RUNTIME_OUTPUT_DIRECTORY='+str(args.work/'bin'),'-DHALS_ENABLE_TORCH=OFF'],args.work,'configure')
 command(['cmake','--build',args.work/'build','--parallel',str(args.jobs)],args.work,'compile')
 (ROOT/'bin').mkdir(exist_ok=True)
 for name in ['resubals.out','abc']:
  if not (ROOT/'bin'/name).exists():shutil.copy2(args.work/'bin'/name,ROOT/'bin'/name)
 print('Built ALS:',args.work/'bin/resubals.out')
 print('Frozen published binaries remain unchanged. Set RESUBALS_BIN to use the rebuilt engine.')

def als(args):
 method=read(ROOT/'release.json')['method'];prepare(args.work,args.benchmarks,False)
 profile=args.profile
 if profile=='all':keys=['MAPE','NMSE'] if method=='C' else [r['key'] for r in read(ROOT/'release.json')['profiles']]
 else:keys=[profile.split('_')[0] if method=='C' else profile]
 tasks=[]
 for b in args.benchmarks:
  for key in keys:
   for path in sorted((ROOT/'examples'/b/'als_configs'/key).glob('*.json')):tasks.append((b,key,path))
 os.environ.setdefault('RESUBALS_BIN',str(ROOT/'bin/resubals.out'))
 def one(task):
  b,key,path=task;cfg=expand(read(path),args.work)
  work=(args.work/'als/C'/b/key/path.stem if method=='C' else args.work/'als'/b/key.split('_')[0]/path.stem)
  # Separate DATE profiles must have separate work directories.
  if method=='DATE' and len(keys)>1:work=args.work/'als_profiles'/key/b/path.stem
  work.mkdir(parents=True,exist_ok=True)
  if (work/'status.json').exists() and read(work/'status.json').get('status')=='ok':
   status=read(work/'status.json')
   if status.get('smoke',False)!=args.smoke or read(work/'config.json')!=cfg:raise RuntimeError('ALS work directory has different parameters; choose a new work directory')
   return
  write(work/'config.json',cfg)
  cmd=[sys.executable,ROOT/'scripts/resubals.py','--config',work/'config.json','--working-dir',work,
    '--examples-dir',ROOT/'examples','--nthread','2']
  if args.smoke:cmd+=['--max-round','1','--max-cand-resub','1000']
  timer=command(cmd,work,'resubals')
  front=work/'als_results'/(cfg['module']+'.trajectory.csv')
  if not front.exists():front=work/'als_results'/(cfg['module']+'.csv')
  if not front.exists():raise RuntimeError('No ALS trajectory')
  write(work/'status.json',dict(status='ok',front=str(front),elapsed_sec=timer['elapsed_sec'],smoke=args.smoke))
  print('als_complete',b,key,path.stem,flush=True)
 with ThreadPoolExecutor(max_workers=args.jobs) as pool:list(pool.map(one,tasks))

def train(args):
 if read(ROOT/'release.json')['method']!='C':
  raise RuntimeError('The supplied DATE training CSVs are unavailable. See docs/TRAINING.md for the included generator/trainer and new-data training commands; frozen inference is reproducible.')
 from scripts.hals_experiment.hals_comparison_models import generate,train as fit
 prepare(args.work,args.benchmarks,False)
 for b in args.benchmarks:
  if args.regenerate:
   simulator(args.work,b,'reference',args.jobs);simulator(args.work,b,'training',args.jobs)
   cfg=read(ROOT/'examples'/b/'data/dataset.json')
   generate(b,'C',args.work,patterns=cfg['patterns'],seed=cfg['input_seed'],joint_samples=2048,local_samples=128)
  fit(b,'C',args.work);print('trained',b,flush=True)

def date_replay(args):
 if args.profile=='all':raise ValueError('Use one DATE profile per work directory; see README for the four-profile loop.')
 metric,boundkey=args.profile.split('_',1);bound=float(boundkey.replace('p','.'))
 os.environ['HALS_RELEASE_WORK']=str(args.work);os.environ['HALS_RELEASE_BOUNDS']=json.dumps({'MAPE':bound if metric=='MAPE' else .05,'NMSE':bound if metric=='NMSE' else .001})
 prepare(args.work,args.benchmarks)
 from release_pipeline import dse,polish,deep
 for b in args.benchmarks:
  simulator(args.work,b,'reference',args.jobs);simulator(args.work,b,'validation',args.jobs)
  work=args.work/'dse'/b/metric;bank=expand(read(ROOT/'examples'/b/'candidate_banks'/(args.profile+'.json')),args.work)
  if args.fresh_als:
   for i,group in enumerate(bank['groups']):
    search=args.work/'als'/b/metric/group['name'];status=read(search/'status.json')
    if status.get('smoke'):raise RuntimeError('Smoke trajectories cannot be used as full experimental results')
    # Original historical trajectories remain valid inputs. Replace this profile's
    # original new trajectory with the freshly generated one.
    old=[c for c in bank['banks'][i] if c.get('origin')!='new']
    for r in csv.DictReader(Path(status['front']).open()):
     if int(r['round'])==0:continue
     net=Path(r['aig_netlist']);net=net if net.is_absolute() else ROOT/net
     old.append(dict(netlist=str(net),area=float(r['area']),features=dse.local_features(r),round=int(r['round']),origin='new'))
    distinct={}
    for c in old[1:]:
     signature=(sha(c['netlist']),tuple(round(x,12) for x in c['features']))
     if signature not in distinct or c['area']<distinct[signature]['area']:distinct[signature]=c
    bank['banks'][i]=[old[0],*sorted(distinct.values(),key=lambda c:(-c['area'],c['netlist']))]
  write(work/'bank.json',bank)
  dse.finish_case(b,metric);polish.polish(b,metric);deep.refine(b,metric)
  print('date_replay_complete',b,args.profile,flush=True)

def c_replay(args):
 from scripts.hals_experiment import hals_fair_dse as dse,hals_comparison_als as local
 prepare(args.work,args.benchmarks)
 metrics=['MAPE','NMSE'] if args.profile=='all' else [args.profile.split('_')[0]]
 source=args.work/'historical';cache={}
 original_load=local.load_fronts
 def load_fronts(b,variant,m,out):
  bank=cache[(b,m)]
  if args.fresh_als and out==args.work:return original_load(b,variant,m,out)
  return bank['groups'],copy.deepcopy(bank['fronts'])
 dse.load_fronts=load_fronts
 for b in args.benchmarks:
  simulator(args.work,b,'reference',args.jobs);simulator(args.work,b,'validation',args.jobs)
  for m in metrics:
   bank=expand(read(ROOT/'examples'/b/'candidate_banks'/(m+'.json')),args.work);cache[(b,m)]=bank
   write(source/'dse/C'/b/m/'selected.json',bank['incumbents'])
   dse.finish(b,m,args.work,source);print('hals_replay_complete',b,m,flush=True)

def replay(args):
 if args.smoke:raise ValueError('Replay cannot use reduced smoke budgets')
 method=read(ROOT/'release.json')['method']
 if method=='DATE' and args.profile=='all':raise ValueError('DATE replay needs one profile and a separate work directory')
 binding=dict(profile=args.profile if method=='DATE' else args.profile.split('_')[0],fresh_als=args.fresh_als)
 record=args.work/'replay_binding.json'
 if record.exists() and read(record)!=binding:raise RuntimeError('Replay parameters changed; choose a new work directory')
 write(record,binding)
 if args.fresh_als:als(args)
 if method=='C':c_replay(args)
 else:date_replay(args)

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('action',choices=['verify','build','validate','prove','partition-check','model-check','als','train','replay'])
 p.add_argument('--benchmarks',default='all');p.add_argument('--profile',default='all',choices=['all','MAPE_0p01','MAPE_0p05','NMSE_0p001','NMSE_0p01'])
 p.add_argument('--work',type=Path,default=ROOT/'work');p.add_argument('--jobs',type=int,default=4)
 p.add_argument('--smoke',action='store_true',help='ALS only: 1 round and 1000 generated candidates; never a final result')
 p.add_argument('--fresh-als',action='store_true',help='Replay: regenerate this profile ALS before DSE, retaining documented historical candidates')
 p.add_argument('--regenerate',action='store_true',help='HALS training: regenerate truncation CSVs first')
 args=p.parse_args();args.work=args.work.resolve();info=read(ROOT/'release.json');names=[b['benchmark'] for b in info['benchmarks']]
 args.benchmarks=names if args.benchmarks=='all' else args.benchmarks.split(',')
 if not set(args.benchmarks)<=set(names):p.error('Unknown benchmark')
 if args.jobs<1:p.error('--jobs must be positive')
 if args.smoke and args.action!='als':p.error('--smoke is only allowed for ALS')
 from threadpoolctl import threadpool_limits
 with threadpool_limits(1):globals()[args.action.replace('-','_')](args)

if __name__=='__main__':main()
