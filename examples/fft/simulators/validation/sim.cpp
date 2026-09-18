#include "hals_candidate_runtime.h"
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
static constexpr int NS=32;
static constexpr int widths[]={16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16}, signs[]={1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1};
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
emit({uint16_t(randv(-16384,16383)),uint16_t(randv(-16384,16383)),uint16_t(randv(-16384,16383)),uint16_t(randv(-16384,16383)),uint16_t(randv(-16384,16383)),uint16_t(randv(-16384,16383)),uint16_t(randv(-16384,16383)),uint16_t(randv(-16384,16383)),uint16_t(randv(-16384,16383)),uint16_t(randv(-16384,16383)),uint16_t(randv(-16384,16383)),uint16_t(randv(-16384,16383)),uint16_t(randv(-16384,16383)),uint16_t(randv(-16384,16383)),uint16_t(randv(-16384,16383)),uint16_t(randv(-16384,16383)),uint16_t(randv(-16384,16383)),uint16_t(randv(-16384,16383)),uint16_t(randv(-16384,16383)),uint16_t(randv(-16384,16383)),uint16_t(randv(-16384,16383)),uint16_t(randv(-16384,16383)),uint16_t(randv(-16384,16383)),uint16_t(randv(-16384,16383)),uint16_t(randv(-16384,16383)),uint16_t(randv(-16384,16383)),uint16_t(randv(-16384,16383)),uint16_t(randv(-16384,16383)),uint16_t(randv(-16384,16383)),uint16_t(randv(-16384,16383)),uint16_t(randv(-16384,16383)),uint16_t(randv(-16384,16383))});
}
 std::vector<std::vector<uint64_t>> traces(NS);
 std::vector<std::ofstream> pattern_files;
 if(!pattern_dir.empty()){pattern_files.emplace_back(pattern_dir+"/hals_fft_pair0.pattern");
pattern_files.emplace_back(pattern_dir+"/hals_fft_pair1.pattern");
pattern_files.emplace_back(pattern_dir+"/hals_fft_pair2.pattern");
pattern_files.emplace_back(pattern_dir+"/hals_fft_pair3.pattern");
pattern_files.emplace_back(pattern_dir+"/hals_fft_pair4.pattern");
pattern_files.emplace_back(pattern_dir+"/hals_fft_pair5.pattern");
pattern_files.emplace_back(pattern_dir+"/hals_fft_pair6.pattern");
pattern_files.emplace_back(pattern_dir+"/hals_fft_pair7.pattern");
pattern_files.emplace_back(pattern_dir+"/hals_fft_pair8.pattern");
pattern_files.emplace_back(pattern_dir+"/hals_fft_pair9.pattern");
pattern_files.emplace_back(pattern_dir+"/hals_fft_pair10.pattern");
pattern_files.emplace_back(pattern_dir+"/hals_fft_pair11.pattern");
pattern_files.emplace_back(pattern_dir+"/hals_fft_pair12.pattern");
pattern_files.emplace_back(pattern_dir+"/hals_fft_pair13.pattern");
pattern_files.emplace_back(pattern_dir+"/hals_fft_pair14.pattern");
pattern_files.emplace_back(pattern_dir+"/hals_fft_pair15.pattern");}
 auto simulate=[&](const std::vector<Op>&cfg,bool capture){
  Vstudy_top dut;dut.mode0=cfg[0].mode;dut.arg0=cfg[0].a;dut.argb0=cfg[0].b;
dut.mode1=cfg[1].mode;dut.arg1=cfg[1].a;dut.argb1=cfg[1].b;
dut.mode2=cfg[2].mode;dut.arg2=cfg[2].a;dut.argb2=cfg[2].b;
dut.mode3=cfg[3].mode;dut.arg3=cfg[3].a;dut.argb3=cfg[3].b;
dut.mode4=cfg[4].mode;dut.arg4=cfg[4].a;dut.argb4=cfg[4].b;
dut.mode5=cfg[5].mode;dut.arg5=cfg[5].a;dut.argb5=cfg[5].b;
dut.mode6=cfg[6].mode;dut.arg6=cfg[6].a;dut.argb6=cfg[6].b;
dut.mode7=cfg[7].mode;dut.arg7=cfg[7].a;dut.argb7=cfg[7].b;
dut.mode8=cfg[8].mode;dut.arg8=cfg[8].a;dut.argb8=cfg[8].b;
dut.mode9=cfg[9].mode;dut.arg9=cfg[9].a;dut.argb9=cfg[9].b;
dut.mode10=cfg[10].mode;dut.arg10=cfg[10].a;dut.argb10=cfg[10].b;
dut.mode11=cfg[11].mode;dut.arg11=cfg[11].a;dut.argb11=cfg[11].b;
dut.mode12=cfg[12].mode;dut.arg12=cfg[12].a;dut.argb12=cfg[12].b;
dut.mode13=cfg[13].mode;dut.arg13=cfg[13].a;dut.argb13=cfg[13].b;
dut.mode14=cfg[14].mode;dut.arg14=cfg[14].a;dut.argb14=cfg[14].b;
dut.mode15=cfg[15].mode;dut.arg15=cfg[15].a;dut.argb15=cfg[15].b;
dut.mode16=cfg[16].mode;dut.arg16=cfg[16].a;dut.argb16=cfg[16].b;
dut.mode17=cfg[17].mode;dut.arg17=cfg[17].a;dut.argb17=cfg[17].b;
dut.mode18=cfg[18].mode;dut.arg18=cfg[18].a;dut.argb18=cfg[18].b;
dut.mode19=cfg[19].mode;dut.arg19=cfg[19].a;dut.argb19=cfg[19].b;
dut.mode20=cfg[20].mode;dut.arg20=cfg[20].a;dut.argb20=cfg[20].b;
dut.mode21=cfg[21].mode;dut.arg21=cfg[21].a;dut.argb21=cfg[21].b;
dut.mode22=cfg[22].mode;dut.arg22=cfg[22].a;dut.argb22=cfg[22].b;
dut.mode23=cfg[23].mode;dut.arg23=cfg[23].a;dut.argb23=cfg[23].b;
dut.mode24=cfg[24].mode;dut.arg24=cfg[24].a;dut.argb24=cfg[24].b;
dut.mode25=cfg[25].mode;dut.arg25=cfg[25].a;dut.argb25=cfg[25].b;
dut.mode26=cfg[26].mode;dut.arg26=cfg[26].a;dut.argb26=cfg[26].b;
dut.mode27=cfg[27].mode;dut.arg27=cfg[27].a;dut.argb27=cfg[27].b;
dut.mode28=cfg[28].mode;dut.arg28=cfg[28].a;dut.argb28=cfg[28].b;
dut.mode29=cfg[29].mode;dut.arg29=cfg[29].a;dut.argb29=cfg[29].b;
dut.mode30=cfg[30].mode;dut.arg30=cfg[30].a;dut.argb30=cfg[30].b;
dut.mode31=cfg[31].mode;dut.arg31=cfg[31].a;dut.argb31=cfg[31].b;
  std::vector<double> out;size_t cycle=0,pattern_count=0;
  for(const auto&s:stimuli){dut.sample_0_0=s.x[0];
dut.sample_0_1=s.x[1];
dut.sample_1_0=s.x[2];
dut.sample_1_1=s.x[3];
dut.sample_2_0=s.x[4];
dut.sample_2_1=s.x[5];
dut.sample_3_0=s.x[6];
dut.sample_3_1=s.x[7];
dut.sample_4_0=s.x[8];
dut.sample_4_1=s.x[9];
dut.sample_5_0=s.x[10];
dut.sample_5_1=s.x[11];
dut.sample_6_0=s.x[12];
dut.sample_6_1=s.x[13];
dut.sample_7_0=s.x[14];
dut.sample_7_1=s.x[15];
dut.sample_8_0=s.x[16];
dut.sample_8_1=s.x[17];
dut.sample_9_0=s.x[18];
dut.sample_9_1=s.x[19];
dut.sample_10_0=s.x[20];
dut.sample_10_1=s.x[21];
dut.sample_11_0=s.x[22];
dut.sample_11_1=s.x[23];
dut.sample_12_0=s.x[24];
dut.sample_12_1=s.x[25];
dut.sample_13_0=s.x[26];
dut.sample_13_1=s.x[27];
dut.sample_14_0=s.x[28];
dut.sample_14_1=s.x[29];
dut.sample_15_0=s.x[30];
dut.sample_15_1=s.x[31];
   dut.eval();if(capture) traces[0].push_back(uint64_t(dut.trace0));
if(capture) traces[1].push_back(uint64_t(dut.trace1));
if(capture) traces[2].push_back(uint64_t(dut.trace2));
if(capture) traces[3].push_back(uint64_t(dut.trace3));
if(capture) traces[4].push_back(uint64_t(dut.trace4));
if(capture) traces[5].push_back(uint64_t(dut.trace5));
if(capture) traces[6].push_back(uint64_t(dut.trace6));
if(capture) traces[7].push_back(uint64_t(dut.trace7));
if(capture) traces[8].push_back(uint64_t(dut.trace8));
if(capture) traces[9].push_back(uint64_t(dut.trace9));
if(capture) traces[10].push_back(uint64_t(dut.trace10));
if(capture) traces[11].push_back(uint64_t(dut.trace11));
if(capture) traces[12].push_back(uint64_t(dut.trace12));
if(capture) traces[13].push_back(uint64_t(dut.trace13));
if(capture) traces[14].push_back(uint64_t(dut.trace14));
if(capture) traces[15].push_back(uint64_t(dut.trace15));
if(capture) traces[16].push_back(uint64_t(dut.trace16));
if(capture) traces[17].push_back(uint64_t(dut.trace17));
if(capture) traces[18].push_back(uint64_t(dut.trace18));
if(capture) traces[19].push_back(uint64_t(dut.trace19));
if(capture) traces[20].push_back(uint64_t(dut.trace20));
if(capture) traces[21].push_back(uint64_t(dut.trace21));
if(capture) traces[22].push_back(uint64_t(dut.trace22));
if(capture) traces[23].push_back(uint64_t(dut.trace23));
if(capture) traces[24].push_back(uint64_t(dut.trace24));
if(capture) traces[25].push_back(uint64_t(dut.trace25));
if(capture) traces[26].push_back(uint64_t(dut.trace26));
if(capture) traces[27].push_back(uint64_t(dut.trace27));
if(capture) traces[28].push_back(uint64_t(dut.trace28));
if(capture) traces[29].push_back(uint64_t(dut.trace29));
if(capture) traces[30].push_back(uint64_t(dut.trace30));
if(capture) traces[31].push_back(uint64_t(dut.trace31));
if(capture && !pattern_dir.empty() && pattern_count<4096){for(int j=0;j<256;++j) pattern_files[0]<<char('0'+((dut.pi0[j/32] >> (j%32))&1));pattern_files[0]<<"\n";}
if(capture && !pattern_dir.empty() && pattern_count<4096){for(int j=0;j<256;++j) pattern_files[1]<<char('0'+((dut.pi1[j/32] >> (j%32))&1));pattern_files[1]<<"\n";}
if(capture && !pattern_dir.empty() && pattern_count<4096){for(int j=0;j<384;++j) pattern_files[2]<<char('0'+((dut.pi2[j/32] >> (j%32))&1));pattern_files[2]<<"\n";}
if(capture && !pattern_dir.empty() && pattern_count<4096){for(int j=0;j<384;++j) pattern_files[3]<<char('0'+((dut.pi3[j/32] >> (j%32))&1));pattern_files[3]<<"\n";}
if(capture && !pattern_dir.empty() && pattern_count<4096){for(int j=0;j<448;++j) pattern_files[4]<<char('0'+((dut.pi4[j/32] >> (j%32))&1));pattern_files[4]<<"\n";}
if(capture && !pattern_dir.empty() && pattern_count<4096){for(int j=0;j<448;++j) pattern_files[5]<<char('0'+((dut.pi5[j/32] >> (j%32))&1));pattern_files[5]<<"\n";}
if(capture && !pattern_dir.empty() && pattern_count<4096){for(int j=0;j<448;++j) pattern_files[6]<<char('0'+((dut.pi6[j/32] >> (j%32))&1));pattern_files[6]<<"\n";}
if(capture && !pattern_dir.empty() && pattern_count<4096){for(int j=0;j<448;++j) pattern_files[7]<<char('0'+((dut.pi7[j/32] >> (j%32))&1));pattern_files[7]<<"\n";}
if(capture && !pattern_dir.empty() && pattern_count<4096){for(int j=0;j<480;++j) pattern_files[8]<<char('0'+((dut.pi8[j/32] >> (j%32))&1));pattern_files[8]<<"\n";}
if(capture && !pattern_dir.empty() && pattern_count<4096){for(int j=0;j<480;++j) pattern_files[9]<<char('0'+((dut.pi9[j/32] >> (j%32))&1));pattern_files[9]<<"\n";}
if(capture && !pattern_dir.empty() && pattern_count<4096){for(int j=0;j<480;++j) pattern_files[10]<<char('0'+((dut.pi10[j/32] >> (j%32))&1));pattern_files[10]<<"\n";}
if(capture && !pattern_dir.empty() && pattern_count<4096){for(int j=0;j<480;++j) pattern_files[11]<<char('0'+((dut.pi11[j/32] >> (j%32))&1));pattern_files[11]<<"\n";}
if(capture && !pattern_dir.empty() && pattern_count<4096){for(int j=0;j<480;++j) pattern_files[12]<<char('0'+((dut.pi12[j/32] >> (j%32))&1));pattern_files[12]<<"\n";}
if(capture && !pattern_dir.empty() && pattern_count<4096){for(int j=0;j<480;++j) pattern_files[13]<<char('0'+((dut.pi13[j/32] >> (j%32))&1));pattern_files[13]<<"\n";}
if(capture && !pattern_dir.empty() && pattern_count<4096){for(int j=0;j<480;++j) pattern_files[14]<<char('0'+((dut.pi14[j/32] >> (j%32))&1));pattern_files[14]<<"\n";}
if(capture && !pattern_dir.empty() && pattern_count<4096){for(int j=0;j<480;++j) pattern_files[15]<<char('0'+((dut.pi15[j/32] >> (j%32))&1));pattern_files[15]<<"\n";}
++pattern_count;
   if(!s.reset){out.push_back(signvalue(dut.sample_out_0_0,16,1)); out.push_back(signvalue(dut.sample_out_0_1,16,1)); out.push_back(signvalue(dut.sample_out_1_0,16,1)); out.push_back(signvalue(dut.sample_out_1_1,16,1)); out.push_back(signvalue(dut.sample_out_2_0,16,1)); out.push_back(signvalue(dut.sample_out_2_1,16,1)); out.push_back(signvalue(dut.sample_out_3_0,16,1)); out.push_back(signvalue(dut.sample_out_3_1,16,1)); out.push_back(signvalue(dut.sample_out_4_0,16,1)); out.push_back(signvalue(dut.sample_out_4_1,16,1)); out.push_back(signvalue(dut.sample_out_5_0,16,1)); out.push_back(signvalue(dut.sample_out_5_1,16,1)); out.push_back(signvalue(dut.sample_out_6_0,16,1)); out.push_back(signvalue(dut.sample_out_6_1,16,1)); out.push_back(signvalue(dut.sample_out_7_0,16,1)); out.push_back(signvalue(dut.sample_out_7_1,16,1)); out.push_back(signvalue(dut.sample_out_8_0,16,1)); out.push_back(signvalue(dut.sample_out_8_1,16,1)); out.push_back(signvalue(dut.sample_out_9_0,16,1)); out.push_back(signvalue(dut.sample_out_9_1,16,1)); out.push_back(signvalue(dut.sample_out_10_0,16,1)); out.push_back(signvalue(dut.sample_out_10_1,16,1)); out.push_back(signvalue(dut.sample_out_11_0,16,1)); out.push_back(signvalue(dut.sample_out_11_1,16,1)); out.push_back(signvalue(dut.sample_out_12_0,16,1)); out.push_back(signvalue(dut.sample_out_12_1,16,1)); out.push_back(signvalue(dut.sample_out_13_0,16,1)); out.push_back(signvalue(dut.sample_out_13_1,16,1)); out.push_back(signvalue(dut.sample_out_14_0,16,1)); out.push_back(signvalue(dut.sample_out_14_1,16,1)); out.push_back(signvalue(dut.sample_out_15_0,16,1)); out.push_back(signvalue(dut.sample_out_15_1,16,1));}++cycle;
  }dut.final();return out;
 };
 std::vector<Op> exact(NS);auto gold=simulate(exact,true);
 if(false){std::ofstream f(reference,std::ios::binary);f.write((char*)gold.data(),gold.size()*sizeof(double));std::cout<<gold.size()<<" exact outputs\n";return 0;}
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
