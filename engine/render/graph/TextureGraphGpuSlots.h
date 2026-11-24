// Pure allocation plan: no RHI handles, no implied GPU completion.
#pragma once
#include "TextureGraphLiveness.h"
#include <limits>

namespace aiverse::render::texture_graph::gpu {
inline constexpr size_t maxDagSlotBytes=64u*1024u*1024u;
inline constexpr size_t maxDagSlots=8;
inline constexpr size_t noDagSlot=std::numeric_limits<size_t>::max();
struct DagSlotStep {
    size_t node=0,outputSlot=noDagSlot;
    std::vector<size_t> inputSlots; // in authored port order, duplicates preserved
    std::vector<size_t> packOutputIndices; // pack once, fan out to these output positions
    std::vector<size_t> releaseSlotsAfter; // after compute AND any pack/readback complete
};
struct DagSlotPlan {
    bool fits=false;
    size_t slotCount=0,slotBytes=0,packedBytes=0,requiredBytes=0;
    std::vector<DagSlotStep> steps;
};
// Fixed float4 intermediate ABI (16 bytes/pixel), plus a single RGBA8
// output buffer. A serial executor must finish each step's GPU work and
// optional pack/readback before releasing slots or reusing the packed buffer.
// This is intentionally not a command-buffer aliasing or fence implementation.
inline DagSlotPlan planDagSlots(const std::vector<std::vector<size_t>>& inputs,
        const std::vector<size_t>& order,const std::vector<size_t>& targets,
        int resolution,size_t budget=maxDagSlotBytes,size_t slotLimit=maxDagSlots){
    if(resolution<8||resolution>1024||budget>maxDagSlotBytes||
       slotLimit>maxDagSlots||slotLimit==0)
        throw std::invalid_argument("invalid GPU DAG slot budget");
    const auto life=planDagLifetime(inputs,order,targets);
    const size_t n=inputs.size(),pixels=size_t(resolution)*size_t(resolution);
    DagSlotPlan plan;
    plan.slotBytes=pixels*16;plan.packedBytes=pixels*4;
    const size_t available=budget>=plan.packedBytes?
        std::min(slotLimit,(budget-plan.packedBytes)/plan.slotBytes):0;
    std::vector<size_t> remaining(n),nodeSlot(n,noDagSlot);
    std::vector<std::vector<size_t>> outputs(n);
    for(size_t i=0;i<targets.size();++i)outputs[targets[i]].push_back(i);
    for(const auto& step:life.steps)for(auto dep:inputs[step.node])++remaining[dep];
    std::vector<bool> used;
    for(const auto& lifetime:life.steps){
        DagSlotStep step;step.node=lifetime.node;
        for(auto dep:inputs[step.node]){
            if(nodeSlot[dep]==noDagSlot)throw std::logic_error("GPU DAG input not resident");
            step.inputSlots.push_back(nodeSlot[dep]);
        }
        size_t slot=0;
        while(slot<used.size()&&used[slot])++slot;
        if(slot==used.size()){
            if(slot>=available){
                // No partial executable plan. Caller retains the fused path.
                return {};
            }
            used.push_back(false);
        }
        used[slot]=true;nodeSlot[step.node]=slot;step.outputSlot=slot;
        step.packOutputIndices=outputs[step.node];
        auto release=[&](size_t node){
            const auto s=nodeSlot[node];
            if(s==noDagSlot||!used[s])throw std::logic_error("GPU DAG slot double release");
            step.releaseSlotsAfter.push_back(s);
            used[s]=false;nodeSlot[node]=noDagSlot;
        };
        for(auto dep:inputs[step.node])if(--remaining[dep]==0)release(dep);
        // Unlike the CPU plan, pack named targets at their producer step,
        // so their float slots need not remain pinned after the last consumer.
        if(remaining[step.node]==0)release(step.node);
        plan.steps.push_back(std::move(step));
    }
    if(std::any_of(used.begin(),used.end(),[](bool v){return v;}))
        throw std::logic_error("GPU DAG unreleased slot");
    plan.slotCount=used.size();plan.requiredBytes=plan.slotCount*plan.slotBytes+plan.packedBytes;
    plan.fits=true;return plan;
}
} // namespace aiverse::render::texture_graph::gpu
