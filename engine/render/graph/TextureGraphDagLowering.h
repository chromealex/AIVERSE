// Logical typed-buffer lowering; no allocation, dispatch, or GPU ownership.
#pragma once
#include "TextureGraphLiveness.h"
#include "TextureGraphPassSchedule.h"
#include <limits>

namespace aiverse::render::texture_graph::gpu {
struct MaterializedDagPlan {
    PassSchedule schedule;
    std::vector<size_t> outputSlots; // caller target order, including duplicates
    size_t allocatedBytes=0;
    size_t peakLive=0;
};
// Pipeline IDs are source node indices. Input slots preserve authored port
// order. Slot capacity is sized for all sequential occupants; outputs stay
// pinned until the caller has packed/read them. Reuse assumes GPU barriers.
inline MaterializedDagPlan lowerDagSlots(
        const std::vector<std::vector<size_t>>& inputs,
        const std::vector<size_t>& order,const std::vector<size_t>& targets,
        const std::vector<size_t>& nodeBytes,size_t budget,size_t maxInputs=3){
    if(nodeBytes.size()!=inputs.size()||maxInputs>3)
        throw std::invalid_argument("invalid materialized DAG shape or binding limit");
    for(size_t i=0;i<inputs.size();++i)
        if(!nodeBytes[i]||inputs[i].size()>maxInputs)
            throw std::invalid_argument("invalid materialized node size or arity");
    const auto lifetime=planDagLifetime(inputs,order,targets);
    constexpr size_t none=std::numeric_limits<size_t>::max();
    std::vector<size_t> nodeSlot(inputs.size(),none);
    std::vector<bool> occupied;
    MaterializedDagPlan result;
    result.peakLive=lifetime.peakLive;
    auto& schedule=result.schedule;
    for(const auto& step:lifetime.steps){
        const auto node=step.node,bytes=nodeBytes[node];
        size_t slot=none,growth=bytes;
        // Prefer minimum growth; stable slot order breaks ties.
        for(size_t i=0;i<occupied.size();++i)if(!occupied[i]){
            const auto capacity=schedule.slotBytes[i];
            const auto extra=bytes>capacity?bytes-capacity:0;
            if(slot==none||extra<growth){slot=i;growth=extra;}
        }
        if(growth>budget-result.allocatedBytes)
            throw std::invalid_argument("materialized DAG exceeds slot budget");
        if(slot==none){
            slot=occupied.size();occupied.push_back(false);
            schedule.slotBytes.push_back(bytes);
        }else schedule.slotBytes[slot]+=growth;
        result.allocatedBytes+=growth;
        ScheduledPass pass;pass.pipeline=node;pass.output=slot;
        for(auto dep:inputs[node]){
            if(nodeSlot[dep]==none||!occupied[nodeSlot[dep]]||nodeSlot[dep]==slot)
                throw std::logic_error("invalid materialized input lifetime");
            pass.inputs.push_back(nodeSlot[dep]);
        }
        occupied[slot]=true;nodeSlot[node]=slot;
        schedule.passes.push_back(std::move(pass));
        for(auto dep:step.releaseAfter){occupied[nodeSlot[dep]]=false;nodeSlot[dep]=none;}
    }
    for(auto target:targets){
        if(nodeSlot[target]==none||!occupied[nodeSlot[target]])
            throw std::logic_error("materialized output not pinned");
        result.outputSlots.push_back(nodeSlot[target]);
    }
    schedule.resultSlot=result.outputSlots.back();
    if(!validPassSchedule(schedule,budget))throw std::logic_error("invalid lowered pass schedule");
    return result;
}
} // namespace aiverse::render::texture_graph::gpu
