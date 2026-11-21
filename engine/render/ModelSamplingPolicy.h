// Shared, allocation-free model pipeline/binding resource admission.
// A graph that cannot fit the RHI uses the authored base PBR material rather
// than dropping geometry or binding out-of-range fragment texture slots.
#pragma once
#include <algorithm>
#include <cstddef>
namespace aiverse::render {
struct ModelSamplingPlan {
    bool graph=false,virtualTexture=false,alphaR8=false,graphBudgetFallback=false;
    int graphSlot=13,atlasSlot=13,tableSlot=14,dynamicGiSlot=13;
};

inline ModelSamplingPlan modelSamplingPlan(int slots,size_t sources,bool graph,
        bool graphUsesVirtual,bool availableVirtual,bool dynamicGi,
        bool availableAlphaR8,bool allowVirtual) {
    ModelSamplingPlan plan;plan.graph=graph&&sources<=4;
    plan.graphBudgetFallback=graph&&!plan.graph;
    for(int attempt=0;attempt<2;++attempt) {
        const int count=plan.graph?int(sources):0;
        plan.alphaR8=!plan.graph&&availableAlphaR8;
        plan.atlasSlot=plan.graph?13+count:13;plan.tableSlot=plan.atlasSlot+1;
        plan.virtualTexture=allowVirtual&&availableVirtual&&(!plan.graph||graphUsesVirtual)&&plan.tableSlot<slots;
        plan.dynamicGiSlot=plan.virtualTexture?std::max(plan.tableSlot+1,plan.alphaR8?16:0):13;
        if(dynamicGi&&plan.dynamicGiSlot>=slots) {
            plan.virtualTexture=false;plan.dynamicGiSlot=13;
        }
        plan.graphSlot=13+(dynamicGi&&!plan.virtualTexture?1:0);
        if(plan.graph&&(sources>4||plan.graphSlot+count>slots)) {
            plan.graph=false;plan.graphBudgetFallback=true;continue;
        }
        break;
    }
    return plan;
}
} // namespace aiverse::render
