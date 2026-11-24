// Dependency-only execution plan for the shared texture graph evaluator.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::render::texture_graph {
struct DagLifetimeStep {
    size_t node=0;
    std::vector<size_t> releaseAfter;
};
struct DagLifetimePlan {
    std::vector<DagLifetimeStep> steps;
    size_t peakLive=0;
    size_t retainedOutputs=0;
};
// Input edges are consumer -> producer. Targets are pinned until packing;
// duplicate target or input edges must not prematurely release storage.
inline DagLifetimePlan planDagLifetime(const std::vector<std::vector<size_t>>& inputs,
        const std::vector<size_t>& order,const std::vector<size_t>& targets) {
    const size_t n=inputs.size();
    if(!n||n>32||order.size()!=n||targets.empty()||targets.size()>9)
        throw std::invalid_argument("invalid texture DAG shape");
    std::vector<bool> seen(n),needed(n),pinned(n);
    for(auto node:order){
        if(node>=n||seen[node])throw std::invalid_argument("invalid texture DAG order");
        for(auto dep:inputs[node])
            if(dep>=n||!seen[dep])throw std::invalid_argument("invalid texture DAG dependency order");
        seen[node]=true;
    }
    DagLifetimePlan plan;
    for(auto t:targets){
        if(t>=n)throw std::invalid_argument("invalid texture DAG target");
        needed[t]=true;
        if(!pinned[t]){pinned[t]=true;++plan.retainedOutputs;}
    }
    for(auto it=order.rbegin();it!=order.rend();++it)
        if(needed[*it])for(auto dep:inputs[*it])needed[dep]=true;
    std::vector<size_t> remaining(n);
    for(auto node:order)if(needed[node])
        for(auto dep:inputs[node])++remaining[dep];
    size_t live=0;
    for(auto node:order)if(needed[node]){
        DagLifetimeStep step;step.node=node;
        plan.peakLive=std::max(plan.peakLive,++live);
        for(auto dep:inputs[node])if(--remaining[dep]==0&&!pinned[dep]){
            step.releaseAfter.push_back(dep);--live;
        }
        plan.steps.push_back(std::move(step));
    }
    if(live!=plan.retainedOutputs)throw std::logic_error("texture DAG lifetime mismatch");
    return plan;
}
} // namespace aiverse::render::texture_graph
