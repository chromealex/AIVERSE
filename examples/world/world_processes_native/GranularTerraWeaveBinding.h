// GRANULAR -> real TerraWeave terrain sampling, Renderer3D geometry and
// optional Box3D static overlay. All geometry is derived from canonical ECS
// granular::Field plus the *existing* TerraWeave EarthworksLayer.
// No duplicate terrain height authority and no genre-specific game module.
#pragma once
#include "aiverselib/world/GranularEcs.h"
#include "engine/world/TerraWeaveEarthworks.h"
#include "engine/render/Renderer3D.h"
#include "engine/physics/Scene.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace aiverse::examples::world_processes_native {

struct GranularSurfaceSettings {
    // The canonical granular grid is projected onto X/Z. Positive row is
    // downhill along +Z (granular::settle's positive row direction).
    std::int64_t originXcm=0,originZcm=0;
    std::int32_t cellSizeCm=100;
    std::int32_t maximumDepthCm=200;
    std::uint32_t rgba=0xffaa9966u;
    std::size_t maxActiveCellsPerPatch=1024;
};
inline bool valid(const GranularSurfaceSettings& s){
    return s.cellSizeCm>=10&&s.cellSizeCm<=10000&&
           s.maximumDepthCm>=1&&s.maximumDepthCm<=20000&&
           s.originXcm>=-90000000&&s.originXcm<=90000000&&
           s.originZcm>=-90000000&&s.originZcm<=90000000&&
           s.maxActiveCellsPerPatch>=1&&s.maxActiveCellsPerPatch<=1024;
}

struct GranularSurfacePatch {
    Entity field=kNullEntity;
    std::uint16_t firstX=0,firstRow=0,columns=0,rows=0;
    std::uint32_t fieldTick=0;
    std::uint64_t soilRevision=0,geometrySignature=0;
    render::float3 originMetres{};
    // The **same height samples** determine rendered slabs and Box3D
    // walkable top surfaces. Each chunk has independent GPU/physics lifetime.
    render::MeshBuilder visual;
    std::vector<b3Vec3> collisionVertices;
    std::vector<std::int32_t> collisionIndices;
    std::size_t activeCells=0;
    bool empty()const{return activeCells==0;}
};
using GranularPatchKey=std::tuple<std::uint64_t,std::uint16_t,std::uint16_t>;

inline GranularPatchKey granularPatchKey(const GranularSurfacePatch& patch){
    return {patch.field.packed(),patch.firstX,patch.firstRow};
}
inline std::uint64_t granularSignatureMix(std::uint64_t h,std::uint64_t v){
    h^=v;return h*1099511628211ull;
}

// Bake on a content/terrain owner worker from an immutable ECS snapshot.
// Terrain is sampled via the ACTUAL EarthworksLayer. Caller must freeze that
// ground revision until this patch is published; reject stale revisions.
inline GranularSurfacePatch bakeGranularSurface(
        const World& ecs,Entity field,const terraweave::EarthworksLayer& soil,
        std::uint16_t firstX,std::uint16_t firstRow,
        std::uint16_t columns,std::uint16_t rows,
        GranularSurfaceSettings settings={}) {
    const auto* granules=ecs.tryGetConst<granular::Field>(field);
    if(!granules||!granules->enabled||!granular::valid(*granules)||
       !valid(settings)||!columns||!rows||columns>32||rows>32||
       firstX>=granules->width||firstRow>=granules->height||
       std::uint32_t(firstX)+columns>granules->width||
       std::uint32_t(firstRow)+rows>granules->height)
        throw std::invalid_argument("GRANULAR invalid terrain projection or chunk bounds");

    const auto worldXcm=settings.originXcm+
                        std::int64_t(firstX)*settings.cellSizeCm;
    const auto worldZcm=settings.originZcm+
                        std::int64_t(firstRow)*settings.cellSizeCm;
    const auto groundOriginCm=soil.sampleHeightCm(worldXcm,worldZcm);
    GranularSurfacePatch patch;
    patch.field=field;
    patch.firstX=firstX;patch.firstRow=firstRow;
    patch.columns=columns;patch.rows=rows;
    patch.fieldTick=granules->sampledTick;
    patch.soilRevision=soil.revision();
    patch.originMetres={float(double(worldXcm)/100.),
                         float(double(groundOriginCm)/100.),
                         float(double(worldZcm)/100.)};
    std::uint64_t signature=14695981039346656037ull;
    signature=granularSignatureMix(signature,field.packed());
    signature=granularSignatureMix(signature,patch.soilRevision);
    signature=granularSignatureMix(signature,std::uint64_t(settings.cellSizeCm));
    signature=granularSignatureMix(signature,std::uint64_t(settings.maximumDepthCm));
    signature=granularSignatureMix(signature,settings.rgba);
    signature=granularSignatureMix(signature,
        std::uint64_t(firstX)<<48 | std::uint64_t(firstRow)<<32 |
        std::uint64_t(columns)<<16 | rows);

    // Box3D mesh is a triangle soup of **top** surfaces. That matches
    // TerraWeave's own static-ground collider publishing convention, and
    // avoids physics bodies per grain/visual cube.
    for(std::uint16_t row=0;row<rows;++row){
        for(std::uint16_t col=0;col<columns;++col){
            const auto x=std::uint32_t(firstX)+col;
            const auto z=std::uint32_t(firstRow)+row;
            const auto index=std::size_t(z)*granules->width+x;
            const auto mass=granules->mass[index];
            signature=granularSignatureMix(signature,std::uint64_t(mass));
            signature=granularSignatureMix(signature,granules->solid[index]);
            if(!mass||granules->solid[index])continue;
            if(++patch.activeCells>settings.maxActiveCellsPerPatch)
                throw std::length_error("GRANULAR visible cell budget exceeded");

            // Quantized mass-to-height: no fractional raw ECS state enters
            // persistence. One grain minimum still produces a 1cm slab.
            const auto heightCm=std::max<std::int64_t>(1,
                (mass*settings.maximumDepthCm)/granules->capacity);
            const auto cxCm=settings.originXcm+
                std::int64_t(x)*settings.cellSizeCm+
                settings.cellSizeCm/2;
            const auto czCm=settings.originZcm+
                std::int64_t(z)*settings.cellSizeCm+
                settings.cellSizeCm/2;
            const auto baseCm=soil.sampleHeightCm(cxCm,czCm);
            signature=granularSignatureMix(signature,std::uint32_t(baseCm));
            signature=granularSignatureMix(signature,std::uint64_t(heightCm));

            const float size=float(settings.cellSizeCm)/100.f;
            const float lx=float(double(cxCm-worldXcm)/100.);
            const float lz=float(double(czCm-worldZcm)/100.);
            const float ly=float(double(baseCm-groundOriginCm+
                          heightCm/2)/100.);
            const float top=float(double(baseCm-groundOriginCm+heightCm)/100.);
            patch.visual.color(settings.rgba).box(
                {lx,ly,lz},{size,float(double(heightCm)/100.),size});

            const float left=lx-size/2,right=lx+size/2;
            const float north=lz-size/2,south=lz+size/2;
            const auto n=std::int32_t(patch.collisionVertices.size());
            patch.collisionVertices.push_back({left,top,north});  // a
            patch.collisionVertices.push_back({right,top,north}); // b
            patch.collisionVertices.push_back({right,top,south}); // c
            patch.collisionVertices.push_back({left,top,south});  // d
            patch.collisionIndices.insert(patch.collisionIndices.end(),
                {n,n+2,n+1,n,n+3,n+2}); // CCW from +Y, like TerraWeave
        }
    }
    patch.geometrySignature=signature;
    return patch;
}

inline physics::BodyDesc makeGranularBox3DSurface(
        const GranularSurfacePatch& patch,std::uint32_t userData=0){
    if(patch.empty()||patch.collisionVertices.empty()||
       patch.collisionIndices.size()!=patch.activeCells*6)
        throw std::invalid_argument("GRANULAR empty or incomplete collision patch");
    physics::BodyDesc body;
    body.motion=physics::Motion::Static;
    body.shape=physics::ShapeKind::Mesh;
    body.position={patch.originMetres.x,
                   patch.originMetres.y,patch.originMetres.z};
    body.vertices=patch.collisionVertices;
    body.indices=patch.collisionIndices;
    body.userData=userData;
    body.category=physics::kCategoryStatic;
    return body;
}

// Two publisher owners, because GPU and Box3D have different lifetime
// fences. Native host calls both at its respective completed stage.
// Do not mutate shared TerraWeave Earthworks or create extra physics worlds.
class GranularBox3DPublisher {
public:
    bool publish(physics::Scene& scene,const GranularSurfacePatch& patch,
                 std::uint32_t userData=0){
        const auto key=granularPatchKey(patch);
        const auto it=resident_.find(key);
        if(it!=resident_.end() && patch.fieldTick<it->second.tick)
            return false; // stale simulated field snapshot
        if(it!=resident_.end() && patch.geometrySignature==it->second.signature)
            return true;
        if(patch.empty()){
            unload(scene,key);return true;
        }
        auto desc=makeGranularBox3DSurface(patch,userData);
        const auto fresh=scene.create(desc);
        if(fresh==physics::kNoBody)return false; // keep old ground
        try{
            const Entry replacement{fresh,patch.fieldTick,patch.geometrySignature};
            const auto [next,inserted]=resident_.try_emplace(key,replacement);
            if(!inserted){
                const auto old=next->second.body;
                next->second=replacement;
                if(scene.valid(old))scene.destroy(old);
            }
        }catch(...){
            scene.destroy(fresh);throw;
        }
        return true;
    }
    void unload(physics::Scene& scene,GranularPatchKey key){
        const auto it=resident_.find(key);
        if(it==resident_.end())return;
        if(scene.valid(it->second.body))scene.destroy(it->second.body);
        resident_.erase(it);
    }
    void shutdown(physics::Scene& scene){
        for(const auto& [key,entry]:resident_){
            (void)key;
            if(scene.valid(entry.body))scene.destroy(entry.body);
        }
        resident_.clear();
    }
    std::size_t count()const{return resident_.size();}
private:
    struct Entry{
        physics::BodyHandle body=physics::kNoBody;
        std::uint32_t tick=0;
        std::uint64_t signature=0;
    };
    std::map<GranularPatchKey,Entry> resident_;
};

class GranularRenderPublisher {
public:
    // GPU owner thread only, outside an in-flight Renderer3D frame. A failed
    // upload does not replace a previous resident visual patch.
    bool publish(gfx::Device& device,const GranularSurfacePatch& patch){
        const auto key=granularPatchKey(patch);
        const auto it=resident_.find(key);
        if(it!=resident_.end()&&patch.fieldTick<it->second.tick)return false;
        if(it!=resident_.end()&&patch.geometrySignature==it->second.signature)
            return true;
        if(patch.empty()){
            return unload(device,key);
        }
        auto fresh=patch.visual.upload(device);
        if(!fresh.vb.valid()||!fresh.ib.valid()){
            if(fresh.vb.valid())device.destroyBuffer(fresh.vb);
            if(fresh.ib.valid())device.destroyBuffer(fresh.ib);
            return false;
        }
        // The same ModelDraw/mesh packets may still be in-flight on Metal,
        // Vulkan or GLES; do not retire old geometry before the device fence.
        if(!device.waitForResourceIdle()){
            device.destroyBuffer(fresh.vb);device.destroyBuffer(fresh.ib);
            return false;
        }
        try{
            Entry replacement{
                fresh,patch.originMetres,patch.fieldTick,
                patch.geometrySignature
            };
            const auto [next,inserted]=resident_.try_emplace(key,replacement);
            if(!inserted){
                auto old=next->second.mesh;
                next->second=replacement;
                if(old.vb.valid())device.destroyBuffer(old.vb);
                if(old.ib.valid())device.destroyBuffer(old.ib);
            }
        }catch(...){
            device.destroyBuffer(fresh.vb);device.destroyBuffer(fresh.ib);
            throw;
        }
        return true;
    }
    bool unload(gfx::Device& device,GranularPatchKey key){
        const auto it=resident_.find(key);
        if(it==resident_.end())return true;
        if(!device.waitForResourceIdle())return false;
        if(it->second.mesh.vb.valid())device.destroyBuffer(it->second.mesh.vb);
        if(it->second.mesh.ib.valid())device.destroyBuffer(it->second.mesh.ib);
        resident_.erase(it);
        return true;
    }
    // Submit only at the normal Renderer3D beginScene/drawMesh stage.
    // Renderer3D owns actual shading, shadows and platform backends.
    void draw(render::Renderer3D& renderer,render::float4 tint={1,1,1,1},
              std::uint32_t flags=0)const{
        for(const auto& [key,item]:resident_){
            (void)key;
            renderer.drawMesh(item.mesh,
                render::mat4::translate(item.origin),tint,flags);
        }
    }
    bool shutdownAfterIdle(gfx::Device& device){
        if(!device.waitForResourceIdle())return false;
        for(const auto& [key,item]:resident_){
            (void)key;
            if(item.mesh.vb.valid())device.destroyBuffer(item.mesh.vb);
            if(item.mesh.ib.valid())device.destroyBuffer(item.mesh.ib);
        }
        resident_.clear();
        return true;
    }
    std::size_t count()const{return resident_.size();}
private:
    struct Entry{
        render::Mesh mesh{};
        render::float3 origin{};
        std::uint32_t tick=0;
        std::uint64_t signature=0;
    };
    std::map<GranularPatchKey,Entry> resident_;
};

} // namespace aiverse::examples::world_processes_native
