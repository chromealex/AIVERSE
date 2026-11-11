#pragma once
#include "engine/cloth/Cloth.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <string>
#include <stdexcept>
namespace clothdemo {
using namespace aiverse::cloth;
using Clock=std::chrono::steady_clock;
inline double ms(Clock::time_point a,Clock::time_point b){return std::chrono::duration<double,std::milli>(b-a).count();}
struct Workload {
    std::shared_ptr<Grid> grid;
    std::vector<Cloth> cloths;
    std::vector<Capsule> capsules;
    Environment env;
    Batch batch;
    int columns;
    explicit Workload(int count,int cells,int workers,int iterations=4,bool share=false,int windGroups=17):batch(workers,share){
        if(count<1||count>2048)throw std::invalid_argument("cloth count must be 1..2048");
        if(windGroups<1||windGroups>2048)throw std::invalid_argument("wind groups must be 1..2048");
        Settings settings;settings.cells=cells;settings.iterations=iterations;grid=std::make_shared<Grid>(settings);
        columns=int(std::ceil(std::sqrt(float(count))));cloths.reserve(size_t(count));
        for(int i=0;i<count;++i){cloths.emplace_back(grid,Vec{float(i%columns)*4,3.5f,float(i/columns)*4});cloths.back().windScale=windGroups==1?1.0f:.7f+.6f*float(i%windGroups)/float(windGroups-1);}
        capsules.push_back({{1.5f,-1.6f,.3f},{1.5f,-2.1f,.3f},.45f});env.capsules=capsules;
    }
    void step(int tick){env.wind={.6f*std::sin(float(tick)*.023f),0,3+2*std::sin(float(tick)*.017f)};batch.step(cloths,1.0f/60,env);}
    float stretch()const{float x=1;for(auto& c:cloths)x=std::max(x,c.maxStretch());return x;}
    bool healthy()const{for(auto& c:cloths)if(!c.healthy())return false;return true;}
};
struct Summary {double mean=0,p50=0,p95=0,p99=0;};
inline Summary summarize(std::vector<double> values){Summary s;if(values.empty())return s;for(double v:values)s.mean+=v;s.mean/=values.size();std::sort(values.begin(),values.end());auto q=[&](double f){return values[size_t(f*(values.size()-1))];};s.p50=q(.5);s.p95=q(.95);s.p99=q(.99);return s;}
inline void metric(std::ostream& o,const char* name,const std::vector<double>& values){auto s=summarize(values);o<<'"'<<name<<"\":{\"mean\":"<<s.mean<<",\"p50\":"<<s.p50<<",\"p95\":"<<s.p95<<",\"p99\":"<<s.p99<<'}';}
}
