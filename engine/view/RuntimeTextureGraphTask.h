// Shared presentation-only task policy. No GPU ownership or simulation state.
#pragma once
#include "LiveAppearanceMaterialTarget.h"
#include <string>
#include <string_view>

namespace aiverse::view {
struct RuntimeTextureGraphTask {
    std::string request;
    size_t type=0,choice=0;
    LiveAppearanceMaterialTarget target;
};
enum class RuntimeTextureGraphDecision { Wait, Apply, Cancel, Failed };
inline RuntimeTextureGraphDecision decideRuntimeTextureGraph(
        const RuntimeTextureGraphTask& task,std::string_view actualRequest,
        std::string_view state,bool busy,bool residentReady,bool hostActive)noexcept{
    if(task.request.empty()||actualRequest!=task.request||
       state=="failed"||state=="cancelled"||state=="idle")
        return RuntimeTextureGraphDecision::Failed;
    if(!hostActive)return RuntimeTextureGraphDecision::Cancel;
    if(busy||state=="queued"||state=="pending")return RuntimeTextureGraphDecision::Wait;
    return state=="ready"&&residentReady?RuntimeTextureGraphDecision::Apply:RuntimeTextureGraphDecision::Failed;
}
} // namespace aiverse::view
