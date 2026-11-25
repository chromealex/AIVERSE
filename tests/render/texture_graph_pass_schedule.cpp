#include "engine/render/graph/TextureGraphPassSchedule.h"
#include <cstdlib>
#include <iostream>
using namespace aiverse::render::texture_graph::gpu;
static void check(bool ok){if(!ok)std::abort();}
int main(){
    PassSchedule s{{64,64,64},{},{{0,0,{}},{1,1,{0}},{2,2,{0,1}},{3,0,{2}}},0};
    std::vector<size_t> calls;
    auto encode=[&](const ScheduledPass& p){calls.push_back(p.pipeline);return true;};
    auto r=encodePassSchedule(s,192,encode);
    check(r.complete&&r.encoded==4&&(calls==std::vector<size_t>{0,1,2,3}));
    calls.clear();r=encodePassSchedule(s,191,encode);check(!r.complete&&r.encoded==0&&calls.empty());
    auto bad=s;bad.passes[0].inputs={1};r=encodePassSchedule(bad,192,encode);check(!r.complete&&calls.empty());
    bad=s;bad.passes[2].inputs={2};check(!validPassSchedule(bad,192));
    bad=s;bad.passes[3].output=3;check(!validPassSchedule(bad,192));
    bad=s;bad.slotBytes[0]=0;check(!validPassSchedule(bad,192));
    bad=s;bad.externalReady={1,1};check(!validPassSchedule(bad,192));
    PassSchedule external{{64,64},{1},{{1,0,{1}}},0};
    check(validPassSchedule(external,128));external.externalReady.clear();check(!validPassSchedule(external,128));
    r=encodePassSchedule(s,192,[&](const ScheduledPass& p){calls.push_back(p.pipeline);return p.pipeline!=2;});
    check(!r.complete&&r.encoded==2&&(calls==std::vector<size_t>{0,1,2}));
    std::cout<<"PASS: ordered passes, slot reuse, external readiness, bounds, dependency rejection, partial encode\n";
}
