#include "engine/render/graph/TextureGraphFusion.h"
#include "engine/render/graph/TextureGraphDagLowering.h"
#include "engine/render/graph/TextureGraphIncremental.h"
#include <cstdlib>
#include <iostream>
using namespace aiverse::render::texture_graph::gpu;
static void check(bool ok){if(!ok)std::abort();}
int main(){
    const std::vector<std::vector<size_t>> chain{{},{0},{1},{2},{3}};
    const std::vector<size_t> order{0,1,2,3,4};
    const auto all=planPointwiseFusion(chain,order,{4},std::vector<bool>(5,true));
    check(all.reachable==5&&all.passes==1&&all.regions[4].nodes==order&&all.regions[4].inputs.empty());
    const auto pin=planPointwiseFusion(chain,order,{1,4,1},std::vector<bool>(5,true));
    check(pin.passes==2&&pin.regions[1].nodes==std::vector<size_t>({0,1}));
    check(pin.regions[4].nodes==std::vector<size_t>({2,3,4})&&pin.edges[4]==std::vector<size_t>{1});
    const auto barrier=planPointwiseFusion(chain,order,{4},{true,true,false,true,true});
    check(barrier.passes==3&&barrier.regions[2].nodes==std::vector<size_t>{2});
    check(barrier.edges[2]==std::vector<size_t>{1}&&barrier.edges[4]==std::vector<size_t>{2});
    const auto diamond=planPointwiseFusion({{},{0},{0},{1,2},{}},order,{3},std::vector<bool>(5,true));
    check(diamond.reachable==4&&diamond.passes==2&&diamond.regions[0].nodes==std::vector<size_t>{0});
    check(diamond.regions[3].nodes==std::vector<size_t>({1,2,3})&&diamond.edges[3]==std::vector<size_t>{0});
    const auto dup=planPointwiseFusion({{},{0,0}},{0,1},{1},{true,true});
    check(dup.passes==1&&dup.regions[1].nodes.size()==2);
    const auto noFuse=planPointwiseFusion({{},{0,0}},{0,1},{1},{true,true},1);
    check(noFuse.edges[1]==std::vector<size_t>({0,0}));
    // Four pinned external inputs cannot be bound to a three-input kernel.
    const auto wide=planPointwiseFusion({{},{},{},{},{0,1,2},{4,3}},{0,1,2,3,4,5},{0,1,2,3,5},std::vector<bool>(6,true));
    check(wide.passes==6&&wide.regions[5].nodes==std::vector<size_t>{5});
    std::vector<std::vector<size_t>> longChain(32);std::vector<size_t> longOrder;
    for(size_t i=0;i<32;++i){longOrder.push_back(i);if(i)longChain[i]={i-1};}
    const auto bounded=planPointwiseFusion(longChain,longOrder,{31},std::vector<bool>(32,true));
    check(bounded.passes==4);
    for(const auto& group:bounded.regions)check(group.nodes.size()<=maxFusionNodes&&group.inputs.size()<=maxFusionInputs);
    // Run the exact canonical typed lowerer on both DAGs.
    const auto originalSlots=lowerDagSlots(chain,order,{4},std::vector<size_t>(5,256),4096);
    const auto fusedSlots=lowerDagSlots(all.edges,order,{4},std::vector<size_t>(5,256),4096);
    check(originalSlots.schedule.passes.size()==5&&originalSlots.allocatedBytes==512);
    check(fusedSlots.schedule.passes.size()==1&&fusedSlots.allocatedBytes==256);
    const auto pinnedSlots=lowerDagSlots(pin.edges,order,{1,4,1},std::vector<size_t>(5,256),4096);
    check(pinnedSlots.outputSlots[0]==pinnedSlots.outputSlots[2]&&pinnedSlots.outputSlots[0]!=pinnedSlots.outputSlots[1]);
    // Reduced DAG retains only region roots. Their keys incorporate the
    // entire generated kernel, including absorbed node parameters.
    const auto retained=lowerDagSlots(barrier.edges,order,{4},std::vector<size_t>(5,256),768,3,true);
    check(retained.schedule.passes.size()==3&&retained.allocatedBytes==768&&retained.peakLive==3);
    auto keys=incrementalKeys(retained.schedule,[](size_t node){return std::string("kernel")+std::to_string(node);});
    check(keys.size()==3&&incrementalDirty(keys,keys,3)==std::vector<bool>(3,false));
    auto changed=keys;changed[0].source+=" changed absorbed node";
    check(incrementalDirty(keys,changed,3)==std::vector<bool>(3,true));
    changed=keys;changed[2].source+=" changed leaf region";
    check(incrementalDirty(keys,changed,3)==std::vector<bool>({false,false,true}));
    bool invalid=false;
    try{planPointwiseFusion(chain,{1,0,2,3,4},{4},std::vector<bool>(5,true));}catch(const std::invalid_argument&){invalid=true;}
    check(invalid);
    std::cout<<"PASS: bounded fusion, pinned outputs, neighborhood barriers, shared nodes, duplicate ports, input cap, pruning, typed slots\n";
}
