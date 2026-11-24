#pragma once
// Render-owner-thread bridge. Uses the existing model cache, retirement
// queue and Renderer3D instancing. No secondary GPU resource owner.
// Caller must supply a globally unique persistent ID per submitted shell
// and keep the returned frame object alive through renderer consumption.
#include "engine/systems/BuildingInstances.h"
#include "engine/buildings/BuildingMaterialAssets.h"
#include "engine/render/ModelResourceCache.h"
#include "engine/render/Renderer3D.h"
#include "engine/world/SpatialWorld.h"
#include <span>
#include <set>
#include <map>

namespace aiverse::building {
using lib::systems::BuildingInstanceTemplate;
using lib::systems::BuildingPlacedInstance;
struct BuildingInstanceDrawStats {size_t instances=0,uniqueModels=0,uploadedModels=0;};
struct BuildingInstanceRenderFrame {
    render::ModelCacheReadScope scope;
    std::vector<render::ModelDraw> draws;
    BuildingInstanceDrawStats stats;
    // Host must not retire or replace cached models while this is alive.
    bool submit(render::Renderer3D& renderer)const{
        if(!scope)return false;
        for(const auto& d:draws)renderer.drawModel(d);
        return true;
    }
};
inline bool prepareBuildingInstancePointerFrame(gfx::Device& device,
    const render::RenderDeviceLifetime::Lease& deviceLease,render::ModelResourceCache& cache,
    std::span<const BuildingPlacedInstance* const> instances,world::Cell renderOrigin,int cellSize,
    const BuildingImageByGuid& resolve,BuildingInstanceRenderFrame& out,
    std::string* error=nullptr,lib::systems::BuildingSurfaceCache* surfaces=nullptr){
    auto fail=[&](const std::string& why){if(error)*error=why;return false;};
    if(out.scope||!deviceLease.validFor(device)||cellSize<=0||instances.size()>65536)
        return fail("Invalid building instance frame or previous frame still held");
    // Check whole batch before any GPU operation. A duplicate ID is not a
    // second object: it would corrupt Renderer3D's persistent packet state.
    std::set<uint64_t> ids;std::map<std::string,const BuildingInstanceTemplate*> unique;
    for(const auto* ptr:instances){
        if(!ptr)return fail("Null building instance");
        const auto& i=*ptr;
        if(!i.resource||!lib::systems::validBuildingInstancePlacement(i.placement)||
           !ids.insert(i.placement.id).second)return fail("Invalid or duplicate building instance");
        unique.try_emplace(i.resource.modelKey,&i.resource);
    }
    render::ModelResourceCache::ModelMap staged;
    auto retire=[&](){for(auto& [key,model]:staged){(void)key;cache.retireUnpublished(device,model);}staged.clear();};
    try{
        cache.bindResourceDevice(device,deviceLease);
        if(unique.size()>4096)return fail("Building unique model budget exceeded");
        for(const auto& [key,resource]:unique){
            if(cache.contains(key)){
                const auto* resident=cache.find(key);
                if(!resident||!resident->vb.valid()||!resident->ib.valid()){retire();return fail("Building model cache entry not resident");}
                continue;
            }
            // Insert CPU scratch before upload so all partially allocated
            // handles remain owned even if an upload throws.
            std::string issue;
            auto model=runtimeTexturedBuildingModel(resource->local->building,resolve,key,&issue,surfaces);
            if(!model){retire();return fail(issue.empty()?"Building model preparation failed":issue);}
            model->batching.autoLod=false; // authored semantic levels win
            model->batching.mergeRigid=false;
            model->batching.geometryPoolPolicy=render::Model::Batching::GeometryPoolPolicy::AllowRigidLods;
            const auto it=staged.emplace(key,std::move(model)).first;
            if(!render::uploadModel(device,*it->second,false,false,cache.retainPickingGeometry())){
                retire();return fail("Building GPU model upload rejected");
            }
        }
        const size_t uploaded=staged.size();
        cache.publishStagedModels(device,staged);
        BuildingInstanceRenderFrame next;
        next.scope=cache.acquireFrameReadScope();
        next.draws.reserve(instances.size());
        for(const auto* ptr:instances){
            const auto& i=*ptr;
            const auto* model=cache.find(i.resource.modelKey);
            if(!model)return fail("Building model missing after publication");
            const auto& b=i.resource.local->building;
            const auto w=lib::systems::buildingInstanceDimension(b.constructionSize.width);
            const auto d=lib::systems::buildingInstanceDimension(b.constructionSize.depth);
            const auto offset=lib::systems::buildingInstanceRotationOffset(w,d,i.placement.yaw);
            const auto relative=world::relativeTo(i.placement.origin+offset,renderOrigin,cellSize);
            render::ModelDraw draw{};
            draw.model=model;draw.persistentId=i.placement.id;
            // Keep normal renderer state equality checks; sharing the same
            // Model is sufficient for compatible primitive instancing.
            draw.staticPacketRevision=0;
            draw.world=render::mat4::translate({float(relative.x.toDouble()),float(relative.y.toDouble()),float(relative.z.toDouble())})*
                render::mat4::rotateY(-float(i.placement.yaw)*1.57079632679f);
            next.draws.push_back(std::move(draw));
        }
        next.stats={instances.size(),unique.size(),uploaded};
        out=std::move(next);if(error)error->clear();return true;
    }catch(const std::exception& e){retire();return fail(e.what());}
    catch(...){retire();return fail("Building instance publication failed");}
}
// Compatibility entry point for contiguous instance storage.
inline bool prepareBuildingInstanceFrame(gfx::Device& device,
    const render::RenderDeviceLifetime::Lease& deviceLease,render::ModelResourceCache& cache,
    std::span<const BuildingPlacedInstance> instances,world::Cell renderOrigin,int cellSize,
    const BuildingImageByGuid& resolve,BuildingInstanceRenderFrame& out,
    std::string* error=nullptr,lib::systems::BuildingSurfaceCache* surfaces=nullptr){
    if(instances.size()>65536){if(error)*error="Building instance budget exceeded";return false;}
    std::vector<const BuildingPlacedInstance*> refs;refs.reserve(instances.size());
    for(const auto& i:instances)refs.push_back(&i);
    return prepareBuildingInstancePointerFrame(device,deviceLease,cache,refs,renderOrigin,cellSize,resolve,out,error,surfaces);
}
} // namespace aiverse::building
