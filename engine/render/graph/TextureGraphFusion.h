// Conservative pointwise regions; no shader or device ownership.
#pragma once
#include "TextureGraphLiveness.h"

namespace aiverse::render::texture_graph::gpu {
struct FusionRegion {
    std::vector<size_t> nodes; // topological order, ending at the root
    std::vector<size_t> inputs; // external nodes in stable first-use order
};
struct FusionPlan {
    std::vector<FusionRegion> regions; // original root index, empty if absorbed/unreachable
    std::vector<std::vector<size_t>> edges; // reduced DAG, original node indices
    size_t reachable=0,passes=0;
};
inline constexpr size_t maxFusionNodes=8,maxFusionInputs=3;
inline FusionPlan planPointwiseFusion(const std::vector<std::vector<size_t>>& inputs,
        const std::vector<size_t>& order,const std::vector<size_t>& targets,
        const std::vector<bool>& pointwise,size_t nodeLimit=maxFusionNodes){
    const auto lifetime=planDagLifetime(inputs,order,targets);
    const auto n=inputs.size();
    if(pointwise.size()!=n||!nodeLimit||nodeLimit>maxFusionNodes)
        throw std::invalid_argument("invalid fusion policy");
    for(const auto& ports:inputs)if(ports.size()>maxFusionInputs)
        throw std::invalid_argument("fusion input arity exceeds ABI");
    std::vector<bool> needed(n),pinned(n),claimed(n);
    std::vector<std::vector<size_t>> consumers(n);
    for(const auto& step:lifetime.steps)needed[step.node]=true;
    for(auto t:targets)pinned[t]=true;
    for(auto node:order)if(needed[node])for(auto dep:inputs[node]){
        auto& list=consumers[dep];
        if(std::find(list.begin(),list.end(),node)==list.end())list.push_back(node);
    }
    FusionPlan plan;plan.regions.resize(n);plan.edges.resize(n);plan.reachable=lifetime.steps.size();
    // A candidate must have one reachable consumer and no named output of
    // its own. A shared or neighborhood value is always materialized.
    for(auto it=order.rbegin();it!=order.rend();++it){
        const auto root=*it;
        if(!needed[root]||claimed[root])continue;
        std::vector<bool> members(n);members[root]=true;
        auto external=[&]{
            std::vector<size_t> list;
            for(auto node:order)if(members[node])for(auto dep:inputs[node])
                if(!members[dep]&&std::find(list.begin(),list.end(),dep)==list.end())list.push_back(dep);
            return list;
        };
        size_t count=1;
        if(pointwise[root])while(count<nodeLimit){
            const auto frontier=external();bool grew=false;
            for(auto dep:frontier){
                if(claimed[dep]||pinned[dep]||!pointwise[dep]||consumers[dep].size()!=1||!members[consumers[dep][0]])continue;
                members[dep]=true;
                if(external().size()>maxFusionInputs){members[dep]=false;continue;}
                ++count;grew=true;break;
            }
            if(!grew)break;
        }
        auto& region=plan.regions[root];
        for(auto node:order)if(members[node]){region.nodes.push_back(node);claimed[node]=true;}
        // Preserve the existing authored port ABI for a singleton, including
        // duplicate edges. Fused regions bind each external value once.
        region.inputs=count==1?inputs[root]:external();
        plan.edges[root]=region.inputs;++plan.passes;
    }
    return plan;
}
} // namespace aiverse::render::texture_graph::gpu
