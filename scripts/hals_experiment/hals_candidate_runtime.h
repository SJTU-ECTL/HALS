// Runtime evaluation of actual ResubALS BLIFs in the complete sequential RTL.
// This is validation only; synthesized area comes from the selected netlists.
#pragma once
#include "svdpi.h"
#include <algorithm>
#include <cstdlib>
#include <dlfcn.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace hals_validation {
struct Gate {
    std::vector<int> inputs;
    int output;
    std::vector<std::string> cubes;
    bool on=true;
};
struct Circuit {
    using Native=void(*)(const uint32_t*,uint32_t*);
    Native native=nullptr;
    std::vector<int> inputs,outputs;
    std::vector<Gate> gates;
    std::vector<unsigned char> values;
    explicit Circuit(const std::string&path) {
        void*library=dlopen((path+".so").c_str(),RTLD_NOW|RTLD_LOCAL);
        if(library)native=reinterpret_cast<Native>(dlsym(library,"hals_eval"));
        std::ifstream file(path);if(!file)throw std::runtime_error("missing candidate "+path);
        std::vector<std::string> lines;std::string line,buffer;
        while(std::getline(file,line)) {
            if(!line.empty()&&line.back()=='\\'){line.pop_back();buffer+=line+' ';continue;}
            lines.push_back(buffer+line);buffer.clear();
        }
        std::unordered_map<std::string,int> ids;
        auto id=[&](const std::string&n){auto p=ids.emplace(n,int(ids.size()));return p.first->second;};
        std::vector<Gate> raw;
        for(size_t i=0;i<lines.size();++i) {
            std::istringstream row(lines[i]);std::string op;row>>op;
            std::vector<std::string> names;std::string n;while(row>>n)names.push_back(n);
            if(op==".inputs")for(auto&n:names)inputs.push_back(id(n));
            else if(op==".outputs")for(auto&n:names)outputs.push_back(id(n));
            else if(op==".names") {
                if(names.empty())throw std::runtime_error("empty .names");
                Gate g;g.output=id(names.back());names.pop_back();for(auto&n:names)g.inputs.push_back(id(n));
                bool first=true;
                while(i+1<lines.size()&&!lines[i+1].empty()&&lines[i+1][0]!='.') {
                    std::istringstream truth(lines[++i]);std::string cube,bit;
                    if(!(truth>>cube)||cube[0]=='#')continue;
                    if(g.inputs.empty()){bit=cube;cube="";}else if(!(truth>>bit))throw std::runtime_error("bad truth table");
                    bool on=bit=="1";
                    if(!first&&on!=g.on)throw std::runtime_error("mixed-polarity BLIF cover");
                    g.on=on;first=false;g.cubes.push_back(cube);
                }
                raw.push_back(std::move(g));
            }
            else if(op==".latch"||op==".subckt"||op==".gate")throw std::runtime_error("candidate must be a combinational .names BLIF");
        }
        values.resize(ids.size());std::vector<bool> ready(ids.size(),false),used(raw.size(),false);
        for(int i:inputs)ready[i]=true;
        while(gates.size()<raw.size()) {
            size_t before=gates.size();
            for(size_t i=0;i<raw.size();++i)if(!used[i]) {
                bool ok=true;for(int j:raw[i].inputs)ok=ok&&ready[j];
                if(ok){used[i]=true;ready[raw[i].output]=true;gates.push_back(raw[i]);}
            }
            if(gates.size()==before)throw std::runtime_error("cyclic or undriven candidate BLIF");
        }
        for(int o:outputs)if(!ready[o])throw std::runtime_error("undriven candidate output");
    }
    void eval(const svBitVecVal*in,svBitVecVal*out) {
        if(native){native(in,out);return;}
        for(size_t i=0;i<inputs.size();++i)values[inputs[i]]=(in[i/32]>>(i%32))&1;
        for(const auto&g:gates) {
            bool match=false;
            for(const auto&cube:g.cubes) {
                bool yes=true;
                for(size_t i=0;i<g.inputs.size();++i)if(cube[i]!='-'&&values[g.inputs[i]]!=(cube[i]=='1')){yes=false;break;}
                if(yes){match=true;break;}
            }
            values[g.output]=g.cubes.empty()?false:(match?g.on:!g.on);
        }
        for(size_t i=0;i<(outputs.size()+31)/32;++i)out[i]=0;
        for(size_t i=0;i<outputs.size();++i)out[i/32]|=svBitVecVal(values[outputs[i]])<<(i%32);
    }
};
inline std::vector<std::vector<Circuit>>& bank() {
    static std::vector<std::vector<Circuit>> circuits;
    static bool loaded=false;
    if(!loaded){loaded=true;const char*path=std::getenv("HALS_CANDIDATE_BANK");
        if(path){std::ifstream f(path);if(!f)throw std::runtime_error("missing candidate bank");int k;std::string file;
            while(f>>k>>file){if(int(circuits.size())<=k)circuits.resize(k+1);circuits[k].emplace_back(file);}
            int native=0,total=0;for(const auto&group:circuits)for(const auto&c:group){++total;if(c.native)++native;}
            std::cerr<<"BLIF bank circuits="<<total<<" native="<<native<<"\n";}}
    return circuits;
}
}
extern "C" void hals_candidate(int kernel,int candidate,const svBitVecVal*in,svBitVecVal*out) {
    auto&b=hals_validation::bank();
    if(kernel<0||size_t(kernel)>=b.size()||candidate<0||size_t(candidate)>=b[kernel].size())
        throw std::runtime_error("invalid candidate bank selection");
    b[kernel][candidate].eval(in,out);
}
