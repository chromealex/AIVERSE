#include "engine/render/graph/TextureGraphGpuSlots.h"
#include <cstdlib>
#include <iostream>
using namespace aiverse::render::texture_graph::gpu;
static void check(bool ok){if(!ok)std::abort();}
int main(){
    const size_t pixels=256u*256u,slot=pixels*16,packed=pixels*4;
    // Two branches consume the same source, including a repeated edge.
    const std::vector<std::vector<size_t>> edges{{},{0},{0,0},{1,2},{}};
    const std::vector<size_t> order{0,1,2,3,4};
    const auto plan=planDagSlots(edges,order,{3,0,3},256);
    check(plan.fits&&plan.slotCount==3&&plan.requiredBytes==3*slot+packed);
    check(plan.steps.size()==4); // unreachable node pruned
    check(plan.steps[0].packOutputIndices==std::vector<size_t>{1});
    check(plan.steps[0].releaseSlotsAfter.empty()); // source still needed
    check(plan.steps[2].inputSlots==std::vector<size_t>({0,0}));
    check(plan.steps[2].releaseSlotsAfter==std::vector<size_t>{0});
    check(plan.steps[3].outputSlot==0&&plan.steps[3].inputSlots==std::vector<size_t>({1,2}));
    check(plan.steps[3].packOutputIndices==std::vector<size_t>({0,2}));
    check(plan.steps[3].releaseSlotsAfter.size()==3);
    check(planDagSlots(edges,order,{3,0,3},256,3*slot+packed).fits);
    auto rejected=planDagSlots(edges,order,{3,0,3},256,3*slot+packed-1);
    check(!rejected.fits&&rejected.steps.empty());
    check(!planDagSlots(edges,order,{3},256,maxDagSlotBytes,2).fits);
    std::vector<std::vector<size_t>> chain(32);
    std::vector<size_t> linear;
    for(size_t i=0;i<32;++i){linear.push_back(i);if(i)chain[i].push_back(i-1);}
    auto longPlan=planDagSlots(chain,linear,{0,5,10,15,20,25,31},1024);
    check(longPlan.fits&&longPlan.slotCount==2&&longPlan.requiredBytes==36u*1024u*1024u);
    for(const auto& step:longPlan.steps)for(auto s:step.inputSlots)check(s!=step.outputSlot);
    auto single=planDagSlots({{}},{0},{0,0},8);
    check(single.fits&&single.slotCount==1&&single.steps[0].packOutputIndices.size()==2);
    check(!planDagSlots({{}},{0},{0},8,0).fits);
    bool invalid=false;
    try{planDagSlots(edges,{1,0,2,3,4},{3},256);}catch(const std::invalid_argument&){invalid=true;}
    check(invalid);invalid=false;
    try{planDagSlots(edges,order,{3},1025);}catch(const std::invalid_argument&){invalid=true;}
    check(invalid);
    std::cout<<"PASS: GPU slot liveness, output fanout, budget rejection, no input alias\n";
}
