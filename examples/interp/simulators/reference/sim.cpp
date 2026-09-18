#include "Vstudy_top.h"
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
struct Stim {std::vector<uint64_t>x;bool reset=false,start=false;};
struct Op {int mode=0;uint64_t a=0,b=1;};
static constexpr int NS=0;
static constexpr int widths[]={1}, signs[]={0};
double signvalue(uint64_t x,int width,bool sign){
 if(width<64){uint64_t mask=(uint64_t(1)<<width)-1;x &= mask;if(sign && (x & (uint64_t(1)<<(width-1))))return double(int64_t(x|~mask));}
 return sign ? double(int64_t(x)):double(x);
}
uint64_t apply(uint64_t x,Op p,int width){
 uint64_t y=x;switch(p.mode){case 1:y=x&~p.a;break;case 2:y=x|p.a;break;case 3:y=x^p.a;break;
 case 4:y=x&p.a;break;case 5:y=x+p.a;break;case 6:y=x>>p.a;break;case 7:y=(x*p.a)/(p.b?p.b:1);break;case 8:y=0;break;case 9:y=(x&~p.a)+p.b;break;}
 return width==64?y:y&((uint64_t(1)<<width)-1);
}
std::array<double,3> metrics(const std::vector<double>&a,const std::vector<double>&e){
 if(a.size()!=e.size()||a.empty())throw std::runtime_error("output length mismatch or empty");
 double mape=0,sse=0,energy=0;for(size_t i=0;i<a.size();++i){double d=a[i]-e[i];sse+=d*d;energy+=e[i]*e[i];mape+=e[i]==0?(d==0?0:1):std::abs(d/e[i]);}
 return {mape/e.size(),sse/std::max(energy,1.0),sse};
}
int main(int argc,char**argv){try{
 if(argc<7)throw std::runtime_error("usage: simulator configs output patterns seed reference pattern_dir");
 std::string configs_path=argv[1],output_path=argv[2],reference=argv[5],pattern_dir=argv[6];
 int npatterns=std::stoi(argv[3]);uint32_t seed=std::stoul(argv[4]);
 Verilated::randReset(0);
 std::vector<Stim> stimuli; std::mt19937 rng(seed);
auto randv=[&](int a,int b){return std::uniform_int_distribution<int>(a,b)(rng);};
auto emit=[&](std::initializer_list<uint64_t> v,bool reset=false,bool start=false){ Stim s; s.x=v; s.reset=reset;s.start=start;stimuli.push_back(s);};
for(int p=0;p<npatterns;++p){
emit({uint16_t(randv(-16384,16383)),uint16_t(randv(-16384,16383)),uint16_t(randv(-16384,16383)),uint16_t(randv(-16384,16383)),uint16_t(randv(-16384,16383)),uint16_t(randv(-16384,16383)),uint16_t(randv(-16384,16383)),uint16_t(randv(-16384,16383))});
}
 std::vector<std::vector<uint64_t>> traces(NS);
 std::vector<std::ofstream> pattern_files;
 if(!pattern_dir.empty()){}
 auto simulate=[&](const std::vector<Op>&cfg,bool capture){
  Vstudy_top dut;
  std::vector<double> out;size_t cycle=0;
  for(const auto&s:stimuli){dut.idata_0=s.x[0];
dut.idata_1=s.x[1];
dut.idata_2=s.x[2];
dut.idata_3=s.x[3];
dut.idata_4=s.x[4];
dut.idata_5=s.x[5];
dut.idata_6=s.x[6];
dut.idata_7=s.x[7];
   dut.eval();

   if(!s.reset){out.push_back(signvalue(dut.odata_0,64,1)); out.push_back(signvalue(dut.odata_1,64,1)); out.push_back(signvalue(dut.odata_2,64,1)); out.push_back(signvalue(dut.odata_3,64,1));}++cycle;
  }dut.final();return out;
 };
 std::vector<Op> exact(NS);auto gold=simulate(exact,true);
 if(true){std::ofstream f(reference,std::ios::binary);f.write((char*)gold.data(),gold.size()*sizeof(double));std::cout<<gold.size()<<" exact outputs\n";return 0;}
 std::ifstream ref(reference,std::ios::binary|std::ios::ate);if(!ref)throw std::runtime_error("missing reference");
 auto bytes=ref.tellg();ref.seekg(0);std::vector<double> orig(size_t(bytes)/sizeof(double));ref.read((char*)orig.data(),bytes);
 auto parity=metrics(gold,orig);if(parity[2]!=0)throw std::runtime_error("partition/reference exact parity failed");
 std::ifstream cfgstream(configs_path);if(!cfgstream)throw std::runtime_error("missing configs");
 std::ofstream output(output_path);output<<std::setprecision(17)<<"config_id,overall_MAPE,overall_NMSE,runtime_sec";
 for(int j=0;j<NS;++j)output<<",s"<<j<<"_MAPE,s"<<j<<"_NMSE";output<<"\n";
 std::string line;int id=0;
 while(std::getline(cfgstream,line)){if(line.empty())continue;std::istringstream row(line);std::vector<Op> cfg(NS);
  for(auto&p:cfg)if(!(row>>p.mode>>p.a>>p.b))throw std::runtime_error("invalid config row");
  auto t0=std::chrono::steady_clock::now();auto actual=simulate(cfg,false);auto mm=metrics(actual,gold);
  output<<id<<","<<mm[0]<<","<<mm[1]<<","<<std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count();
  for(int j=0;j<NS;++j){double m=0,sse=0,energy=0;for(uint64_t e:traces[j]){double ev=signvalue(e,widths[j],signs[j]);double av=signvalue(apply(e,cfg[j],widths[j]),widths[j],signs[j]);double d=av-ev;m+=ev==0?(d==0?0:1):std::abs(d/ev);sse+=d*d;energy+=ev*ev;}
    output<<","<<m/std::max(size_t(1),traces[j].size())<<","<<sse/std::max(energy,1.0);}
  output<<"\n";output.flush();++id;
 }
 std::cout<<"configs="<<id<<" cycles="<<stimuli.size()<<" outputs="<<gold.size()<<" parity_sse="<<parity[2]<<"\n";
 return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
