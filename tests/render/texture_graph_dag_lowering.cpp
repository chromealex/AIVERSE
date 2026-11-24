#include "engine/render/graph/TextureGraphDagLowering.h"
#include "engine/render/graph/TextureGraphIncremental.h"
#include <cstdlib>
#include <iostream>
using namespace aiverse::render::texture_graph::gpu;
static void check(bool ok){if(!ok)std::abort();}
int main(){
    const std::vector<std::vector<size_t>> edges={{},{0},{0,0},{1,2}, {3}, {}};
    const std::vector<size_t> order={0,1,2,3,4,5},bytes={64,64,64,256,64,4096};
    auto p=lowerDagSlots(edges,order,{4},bytes,384);
    check(p.schedule.passes.size()==5&&p.allocatedBytes==384&&p.peakLive==3);
    check((p.schedule.passes[2].inputs==std::vector<size_t>{0,0}));
    check((p.schedule.passes[3].inputs==std::vector<size_t>{1,2}));
    check(p.schedule.passes[3].output==0&&p.schedule.slotBytes[0]==256);
    check(p.outputSlots==std::vector<size_t>{1});
    p=lowerDagSlots(edges,order,{0,4,0},bytes,448);
    check(p.outputSlots[0]==p.outputSlots[2]&&p.outputSlots[0]!=p.outputSlots[1]);
    check(p.peakLive==4&&p.allocatedBytes==448);
    bool bad=false;try{(void)lowerDagSlots(edges,order,{4},bytes,383);}catch(const std::invalid_argument&){bad=true;}check(bad);
    bad=false;try{(void)lowerDagSlots(edges,order,{4},bytes,384,1);}catch(const std::invalid_argument&){bad=true;}check(bad);
    bad=false;try{(void)lowerDagSlots(edges,order,{6},bytes,384);}catch(const std::invalid_argument&){bad=true;}check(bad);
    std::vector<std::vector<size_t>> chain(32);std::vector<size_t> seq,sizes(32,64);
    for(size_t i=0;i<32;++i){seq.push_back(i);if(i)chain[i]={i-1};}
    p=lowerDagSlots(chain,seq,{31},sizes,128);
    check(p.schedule.passes.size()==32&&p.schedule.slotBytes.size()==2&&p.allocatedBytes==128);
    for(size_t i=0;i<32;++i)check(p.schedule.passes[i].output==i%2);
    p=lowerDagSlots(edges,order,{4},bytes,512,3,true);
    check(p.allocatedBytes==512&&p.peakLive==5&&p.schedule.slotBytes.size()==5);
    for(size_t i=0;i<5;++i)check(p.schedule.passes[i].output==i);
    auto keys=incrementalKeys(p.schedule,[](size_t)->std::string_view{return "shader";});
    check(keys.size()==5);auto changed=keys;changed[2].source="new shader";
    check(incrementalDirty(keys,changed,5)==std::vector<bool>({false,false,true,true,true}));
    check(incrementalDirty(keys,keys,5)==std::vector<bool>(5,false));
    check(incrementalDirty({},keys,5)==std::vector<bool>(5,true));
    changed=keys;changed[3].inputs={2,1};check(incrementalDirty(keys,changed,5)==std::vector<bool>({false,false,false,true,true}));
    auto aliased=lowerDagSlots(edges,order,{4},bytes,384);
    check(incrementalKeys(aliased.schedule,[](size_t)->std::string_view{return "shader";}).empty());
    bad=false;try{(void)lowerDagSlots(edges,order,{4},bytes,511,3,true);}catch(const std::invalid_argument&){bad=true;}check(bad);
    std::cout<<"PASS: typed capacity, multi-input order, duplicate ports/targets, output pinning, pruning, budget, binding limit, slot reuse\n";
}
