#pragma once
#include "engine/systems/BuildingStreamPolicy.h"
#include "engine/buildings/BuildingInstanceRenderer.h"

namespace aiverse::building {
struct BuildingStreamState {
    std::set<uint64_t> selectedIds;
    std::set<std::string> selectedModels;
    size_t outside=0,deferred=0,newModels=0;
};
// On the existing render owner thread, before submission. Preserve the
// last successful selection on failure. The frame scope must survive
// renderer consumption; no second model cache or GPU resource owner.
inline bool prepareBuildingStreamFrame(gfx::Device& device,
    const render::RenderDeviceLifetime::Lease& lease,render::ModelResourceCache& cache,
    std::span<const BuildingPlacedInstance> candidates,Vec3 eye,
    lib::systems::BuildingStreamPolicy policy,world::Cell renderOrigin,int cellSize,
    const BuildingImageByGuid& resolve,BuildingStreamState& state,BuildingInstanceRenderFrame& out,
    std::string* error=nullptr,lib::systems::BuildingSurfaceCache* surfaces=nullptr){
    if(out.scope||!lease.validFor(device)||cellSize<=0||!lib::systems::validBuildingStreamPolicy(policy)){
        if(error)*error="Invalid building stream frame or policy";
        return false;
    }
    try{
        cache.bindResourceDevice(device,lease);
        if(cache.publicationBackpressured())policy.maxNewModels=0;
        lib::systems::BuildingStreamPlan plan;
        if(!lib::systems::planBuildingStream(candidates,eye,policy,state.selectedIds,
            [&](const std::string& key){
                const auto* model=cache.find(key);
                return model&&model->vb.valid()&&model->ib.valid();
            },plan,error))return false;
        if(!prepareBuildingInstancePointerFrame(device,lease,cache,plan.instances,
            renderOrigin,cellSize,resolve,out,error,surfaces))return false;
        state.selectedIds=std::move(plan.selectedIds);
        state.selectedModels=std::move(plan.selectedModels);
        state.outside=plan.outside;state.deferred=plan.deferred;state.newModels=plan.newModels;
        return true;
    }catch(const std::exception& e){if(error)*error=e.what();return false;}
}
// Only keys the host explicitly certifies as unused by ALL other views and
// owners may be retired. No global trim of unrelated cache resources.
// Call after frame readers are released and renderer submission is done.
template<class OnDetach>
size_t retireBuildingStreamModels(gfx::Device& device,render::ModelResourceCache& cache,
    const BuildingStreamState& state,std::span<const std::string> exclusivelyUnused,
    size_t maxRetire,OnDetach&& detach){
    if(!maxRetire||cache.activeModelReaders()||cache.publicationBackpressured())return 0;
    constexpr std::string_view prefix="generated/buildings/";
    std::set<std::string> keys;
    for(const auto& key:exclusivelyUnused){
        if(!key.starts_with(prefix)||key.size()==prefix.size()||
           (key.size()-prefix.size())%2||
           key.find_first_not_of("0123456789abcdef",prefix.size())!=std::string::npos)
            throw std::invalid_argument("Invalid generated building cache key");
        if(!state.selectedModels.contains(key)&&cache.contains(key))keys.insert(key);
    }
    std::vector<std::string> sources;
    for(const auto& key:keys){if(sources.size()>=maxRetire)break;sources.push_back(key);}
    return cache.invalidateSources(device,sources,std::forward<OnDetach>(detach));
}
} // namespace aiverse::building
