"""Verilator simulator for the HALS comparison's frozen RTL output cones."""
from __future__ import annotations
import copy
import json
from pathlib import Path

from .hals_comparison import ROOT, run, write_json


def instrument(design, groups):
    design=copy.deepcopy(design)
    top=design['modules']['study_top']
    bit=max(b for n in top['netnames'].values() for b in n['bits'] if isinstance(b,int))+1
    def alloc(width):
        nonlocal bit
        bs=list(range(bit,bit+width)); bit+=width; return bs
    def port(name,direction,width=None,bits=None):
        bs=alloc(width) if bits is None else bits
        top['ports'][name]=dict(direction=direction,bits=bs)
        top['netnames'][name]=dict(hide_name=0,bits=bs,attributes={})
        return bs
    scalar=[]; kernels=[]
    for gi,g in enumerate(groups):
        cell=top['cells']['study_'+g['name']]
        input_ports=[name for name,p in g['io'].items() if p['direction']=='input']
        inputs=[b for name in input_ports for b in cell['connections'][name]]
        port('pi'+str(gi),'output',bits=inputs)
        kernel=dict(name=g['name'],input_width=len(inputs),input_ports=input_ports,scalar_indices=[])
        for name,bs in list(cell['connections'].items()):
            if cell['port_directions'][name]!='output': continue
            idx=len(scalar); width=len(bs)
            raw=alloc(width); cell['connections'][name]=raw
            port('trace'+str(idx),'output',bits=raw)
            m=port('mode'+str(idx),'input',4)
            a=port('arg'+str(idx),'input',64)
            b=port('argb'+str(idx),'input',64)
            top['cells']['inject'+str(idx)]=dict(hide_name=0,type='study_inject'+str(width),parameters={},attributes={},
                port_directions=dict(x='input',mode='input',a='input',b='input',y='output'),
                connections=dict(x=raw,mode=m,a=a,b=b,y=bs))
            scalar.append(dict(kernel=gi,port=name,width=width,signed=g['io'][name].get('signed',0)))
            kernel['scalar_indices'].append(idx)
        kernels.append(kernel)
    return design,scalar,kernels


def injection_rtl(width):
    return f'''module study_inject{width}(input [{width-1}:0] x, input [3:0] mode,
        input [63:0] a,b, output reg [{width-1}:0] y);
      always @* begin
        case(mode)
          0: y=x;
          1: y=x & ~a;
          2: y=x | a;
          3: y=x ^ a;
          4: y=x & a;
          5: y=x + a;
          6: y=x >> a;
          7: y=(x*a)/(b == 0 ? 64'd1 : b);
          8: y=0;
          9: y=(x & ~a) + b;
          default: y=x;
        endcase
      end
    endmodule\n'''


def stimuli_cpp(bench, inputs):
    names=[n for n,p in inputs.items() if n not in ('clk','rst','start')]
    lines=['std::vector<Stim> stimuli; std::mt19937 rng(seed);',
           'auto randv=[&](int a,int b){return std::uniform_int_distribution<int>(a,b)(rng);};',
           'auto emit=[&](std::initializer_list<uint64_t> v,bool reset=false,bool start=false){ Stim s; s.x=v; s.reset=reset;s.start=start;stimuli.push_back(s);};']
    def emit(values,extra=''):
        return 'emit({'+','.join(str(values.get(n,0)) for n in names)+'}'+extra+');'
    if 'clk' in inputs: lines.append(emit({},',true,false'))
    if bench=='atax':
        lines += ['for(int c=0;c<std::max(1,npatterns/3234);++c){',emit({},',true,false'),
                  'uint64_t x[42]; for(auto &v:x)v=randv(128,512);',
                  'for(int i=0;i<38;++i){for(int j=0;j<42;++j){',emit(dict(Aij='randv(0,320)',xj='x[j]')),
                  '}for(int j=0;j<42;++j){',emit({}),'}}for(int j=0;j<42;++j){',emit({}),'}}']
    elif bench=='bicg':
        lines += ['for(int c=0;c<std::max(1,npatterns/1634);++c){',emit({},',true,false'),
                  'uint64_t p[38];for(auto &v:p)v=randv(128,512);',
                  'for(int i=0;i<42;++i){uint64_t r=randv(128,512);for(int j=0;j<38;++j){',
                  emit(dict(Aij='randv(0,384)',pj='p[j]',ri='r')),'}}for(int j=0;j<38;++j){',emit({}),'}}']
    elif bench=='cholesky':
        lines += ['for(int c=0;c<npatterns;++c){',
            'uint64_t d0=randv(256,1024),d1=randv(256,1024),d2=randv(256,1024),l10=randv(0,512),l20=randv(0,512),l21=randv(0,512);',
            'for(int k=0;k<8;++k){',emit(dict(a00='(d0*d0)>>8',a10='(l10*d0)>>8',a20='(l20*d0)>>8',
                a11='((l10*l10)>>8)+((d1*d1)>>8)',a21='((l20*l10)>>8)+((l21*d1)>>8)',a22='((l20*l20)>>8)+((l21*l21)>>8)+((d2*d2)>>8)'),',false,k==0'),'}}']
    else:
        values={}
        for n in names:
            if bench in ('fft','interp','decimation'): values[n]='uint16_t(randv(-16384,16383))'
            elif bench=='conv3x3': values[n]='randv(0,255)'
            elif bench=='gesummv': values[n]=384 if n=='alpha' else 307 if n=='beta' else 'randv(0,512)'
            elif bench=='syr2k': values[n]=384 if n=='alpha' else 307 if n=='beta' else 'randv(0,512)'
        lines += ['for(int p=0;p<npatterns;++p){',emit(values),'}']
    return '\n'.join(lines),names


def harness(bench,ports,scalar,kernels,instrumented):
    inputs={n:p for n,p in ports.items() if p['direction']=='input'}
    outputs={n:p for n,p in ports.items() if p['direction']=='output' and n not in ('valid','valid_out')}
    stim,names=stimuli_cpp(bench,inputs)
    feed='\n'.join(f'dut.{n}=s.x[{i}];' for i,n in enumerate(names))
    if 'rst' in inputs: feed+='\ndut.rst=s.reset;'
    if 'start' in inputs: feed+='\ndut.start=s.start;'
    dataout=' '.join(f'out.push_back(signvalue(dut.{n},{len(p["bits"])},{p.get("signed",0)}));' for n,p in outputs.items())
    valid='!s.reset'
    if 'valid' in ports: valid+=' && dut.valid'
    if 'valid_out' in ports: valid+=' && dut.valid_out'
    configs='\n'.join(f'dut.mode{i}=cfg[{i}].mode;dut.arg{i}=cfg[{i}].a;dut.argb{i}=cfg[{i}].b;' for i in range(len(scalar))) if instrumented else ''
    traces='\n'.join(f'if(capture) traces[{i}].push_back(uint64_t(dut.trace{i}));' for i in range(len(scalar))) if instrumented else ''
    dump=[]
    if instrumented:
        for i,k in enumerate(kernels):
            width=k['input_width']
            access=f'((dut.pi{i} >> j)&1)' if width<=64 else f'((dut.pi{i}[j/32] >> (j%32))&1)'
            dump.append(f'if(capture && !pattern_dir.empty() && pattern_count<4096){{for(int j=0;j<{width};++j) pattern_files[{i}]<<char(\'0\'+{access});pattern_files[{i}]<<"\\n";}}')
    preclock=traces+'\n'+'\n'.join(dump)+'\n++pattern_count;'
    evaluation= ('dut.clk=0;dut.eval();if(!s.reset){'+preclock+'}dut.clk=1;dut.eval();' if 'clk' in inputs else 'dut.eval();'+preclock)
    declarations='\n'.join(f'pattern_files.emplace_back(pattern_dir+"/{k["name"]}.pattern");' for k in kernels)
    widths=','.join(str(s['width']) for s in scalar) or '1'
    signs=','.join(str(s['signed']) for s in scalar) or '0'
    return f'''#include "Vstudy_top.h"
#include "verilated.h"
#include <vector>
#include <array>
#include <random>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <chrono>
#include <stdexcept>
#include <memory>
struct Stim {{std::vector<uint64_t>x;bool reset=false,start=false;}};
struct Op {{int mode=0;uint64_t a=0,b=1;}};
static constexpr int NS={len(scalar)};
static constexpr int widths[]={{{widths}}}, signs[]={{{signs}}};
double signvalue(uint64_t x,int width,bool sign){{
 if(width<64){{uint64_t mask=(uint64_t(1)<<width)-1;x &= mask;if(sign && (x & (uint64_t(1)<<(width-1))))return double(int64_t(x|~mask));}}
 return sign ? double(int64_t(x)):double(x);
}}
uint64_t apply(uint64_t x,Op p,int width){{
 uint64_t y=x;switch(p.mode){{case 1:y=x&~p.a;break;case 2:y=x|p.a;break;case 3:y=x^p.a;break;
 case 4:y=x&p.a;break;case 5:y=x+p.a;break;case 6:y=x>>p.a;break;case 7:y=(x*p.a)/(p.b?p.b:1);break;case 8:y=0;break;case 9:y=(x&~p.a)+p.b;break;}}
 return width==64?y:y&((uint64_t(1)<<width)-1);
}}
std::array<double,3> metrics(const std::vector<double>&a,const std::vector<double>&e){{
 if(a.size()!=e.size()||a.empty())throw std::runtime_error("output length mismatch or empty");
 double mape=0,sse=0,energy=0;for(size_t i=0;i<a.size();++i){{double d=a[i]-e[i];sse+=d*d;energy+=e[i]*e[i];mape+=e[i]==0?(d==0?0:1):std::abs(d/e[i]);}}
 return {{mape/e.size(),sse/std::max(energy,1.0),sse}};
}}
int main(int argc,char**argv){{try{{
 if(argc<7)throw std::runtime_error("usage: simulator configs output patterns seed reference pattern_dir");
 std::string configs_path=argv[1],output_path=argv[2],reference=argv[5],pattern_dir=argv[6];
 int npatterns=std::stoi(argv[3]);uint32_t seed=std::stoul(argv[4]);
 Verilated::randReset(0);
 {stim}
 std::vector<std::vector<uint64_t>> traces(NS);
 std::vector<std::ofstream> pattern_files;
 if(!pattern_dir.empty()){{{declarations if instrumented else ''}}}
 auto simulate=[&](const std::vector<Op>&cfg,bool capture){{
  Vstudy_top dut;{configs}
  std::vector<double> out;size_t cycle=0,pattern_count=0;
  for(const auto&s:stimuli){{{feed}
   {evaluation}
   if({valid}){{{dataout}}}++cycle;
  }}dut.final();return out;
 }};
 std::vector<Op> exact(NS);auto gold=simulate(exact,true);
 if({str(not instrumented).lower()}){{std::ofstream f(reference,std::ios::binary);f.write((char*)gold.data(),gold.size()*sizeof(double));std::cout<<gold.size()<<" exact outputs\\n";return 0;}}
 std::ifstream ref(reference,std::ios::binary|std::ios::ate);if(!ref)throw std::runtime_error("missing reference");
 auto bytes=ref.tellg();ref.seekg(0);std::vector<double> orig(size_t(bytes)/sizeof(double));ref.read((char*)orig.data(),bytes);
 auto parity=metrics(gold,orig);if(parity[2]!=0)throw std::runtime_error("partition/reference exact parity failed");
 std::ifstream cfgstream(configs_path);if(!cfgstream)throw std::runtime_error("missing configs");
 std::ofstream output(output_path);output<<std::setprecision(17)<<"config_id,overall_MAPE,overall_NMSE,runtime_sec";
 for(int j=0;j<NS;++j)output<<",s"<<j<<"_MAPE,s"<<j<<"_NMSE";output<<"\\n";
 std::string line;int id=0;
 while(std::getline(cfgstream,line)){{if(line.empty())continue;std::istringstream row(line);std::vector<Op> cfg(NS);
  for(auto&p:cfg)if(!(row>>p.mode>>p.a>>p.b))throw std::runtime_error("invalid config row");
  auto t0=std::chrono::steady_clock::now();auto actual=simulate(cfg,false);auto mm=metrics(actual,gold);
  output<<id<<","<<mm[0]<<","<<mm[1]<<","<<std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count();
  for(int j=0;j<NS;++j){{double m=0,sse=0,energy=0;for(uint64_t e:traces[j]){{double ev=signvalue(e,widths[j],signs[j]);double av=signvalue(apply(e,cfg[j],widths[j]),widths[j],signs[j]);double d=av-ev;m+=ev==0?(d==0?0:1):std::abs(d/ev);sse+=d*d;energy+=ev*ev;}}
    output<<","<<m/std::max(size_t(1),traces[j].size())<<","<<sse/std::max(energy,1.0);}}
  output<<"\\n";output.flush();++id;
 }}
 std::cout<<"configs="<<id<<" cycles="<<stimuli.size()<<" outputs="<<gold.size()<<" parity_sse="<<parity[2]<<"\\n";
 return 0;
 }}catch(const std::exception&e){{std::cerr<<e.what()<<"\\n";return 1;}}}}
'''


def add_candidates(design,scalar,kernels):
    top=design['modules']['study_top']
    bit=max(b for n in top['netnames'].values() for b in n['bits'] if isinstance(b,int))+1
    maxpi=max(k['input_width'] for k in kernels)
    maxpo=max(sum(scalar[i]['width'] for i in k['scalar_indices']) for k in kernels)
    rtl=[]
    for gi,k in enumerate(kernels):
        cell=top['cells']['study_'+k['name']]
        inp=[b for n in k['input_ports'] for b in cell['connections'][n]]
        exact=[b for n,bs in cell['connections'].items() if cell['port_directions'][n]=='output' for b in bs]
        approx=list(range(bit,bit+len(exact)));bit+=len(exact)
        first=k['scalar_indices'][0]
        con=dict(bits=inp,x=exact,y=approx,mode=top['ports']['mode'+str(first)]['bits'],sel=top['ports']['arg'+str(first)]['bits'])
        top['cells']['candidate'+str(gi)]=dict(hide_name=0,type='candidate_mux'+str(gi),parameters={},attributes={},
            port_directions=dict(bits='input',x='input',y='output',mode='input',sel='input'),connections=con)
        offset=0
        for i in k['scalar_indices']:
            width=scalar[i]['width'];top['cells']['inject'+str(i)]['connections']['x']=approx[offset:offset+width];offset+=width
        rtl.append(f'''module candidate_mux{gi}(input [{len(inp)-1}:0] bits,input [{len(exact)-1}:0] x,
            input [3:0] mode,input [63:0] sel,output reg [{len(exact)-1}:0] y);
            import "DPI-C" function void hals_candidate(input int kernel,input int candidate,
                input bit [{maxpi-1}:0] values,output bit [{maxpo-1}:0] results);
            reg [{maxpo-1}:0] tmp;
            always @* begin
                tmp=0;
                if(mode==10)begin hals_candidate({gi},sel[31:0],{{{{{maxpi-len(inp)}{{1'b0}}}},bits}},tmp);y=tmp[{len(exact)-1}:0];end
                else y=x;
            end
        endmodule\n''')
    return ''.join(rtl)


def build_simulator(bench,structure,partition=None,jobs=2,candidates=False):
    work=structure/(partition or 'reference')/('validation_simulator' if candidates else 'simulator')
    work.mkdir(parents=True,exist_ok=True)
    flat=json.loads((structure/'flat.json').read_text())
    original=next(iter(flat['modules'].values()))
    original['attributes']={}
    if bench in ('decimation','fft','interp'):
        for p in original['ports'].values():
            if p['direction']=='output':p['signed']=1
    if partition:
        groups=json.loads((structure/partition/'partitions.json').read_text())
        design=json.loads((structure/partition/'clean.json').read_text())
        design,scalar,kernels=instrument(design,groups)
        inject=''.join(injection_rtl(w) for w in sorted({x['width'] for x in scalar}))
        (work/'inject.v').write_text(inject)
        prefix=f'read_verilog -sv {work}/inject.v; '
    else:
        design={'creator':'HALS reference','modules':{'study_top':original}}
        scalar=[];kernels=[];prefix=''
    write_json(work/'sim.json',design)
    run(['yosys','-p',prefix+f'read_json {work}/sim.json; hierarchy -top study_top; opt_clean; write_verilog -noattr {work}/sim.v'],work,'rtl')
    cpp=harness(bench,original['ports'],scalar,kernels,bool(partition))
    if candidates:
        # The DPI wrappers are added after Yosys export, since DPI is a
        # simulation feature. Export the shell with blackbox declarations.
        rtl=add_candidates(design,scalar,kernels)
        blackboxes=[]
        import re
        for module in re.findall(r'module candidate_mux\d+.*?\);',rtl,re.S):
            blackboxes.append('(* blackbox *) '+module+' endmodule\n')
        (work/'candidate_blackboxes.v').write_text(''.join(blackboxes))
        write_json(work/'sim_candidates.json',design)
        run(['yosys','-p',prefix+f'read_verilog {work}/candidate_blackboxes.v; read_json {work}/sim_candidates.json; hierarchy -top study_top; opt_clean; write_verilog -noattr {work}/sim.v'],work,'candidate_rtl')
        with (work/'sim.v').open('a') as f:f.write(rtl)
        cpp='#include "'+str(ROOT/'scripts/hals_experiment/hals_candidate_runtime.h')+'"\n'+cpp
    (work/'sim.cpp').write_text(cpp)
    run(['verilator','--cc','--exe','--build','-j',str(jobs),'-Wno-fatal','--top-module','study_top',
         '--Mdir',str(work/'obj'),'-CFLAGS','-O3',str(work/'sim.v'),str(work/'sim.cpp')],work,'compile')
    write_json(work/'manifest.json',dict(benchmark=bench,partition=partition,scalars=scalar,kernels=kernels))
    return work/'obj'/'Vstudy_top'
