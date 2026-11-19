// TerraWeave: bounded grass patch RHI adapter. Thousands of blades become
// one draw per visible patch, not one ECS entity / draw call per blade.
// Works with existing Renderer3D::drawModel and gfx::Device on its owner thread.
#pragma once
#include "engine/world/TerraWeaveGrassCover.h"
#include "engine/render/Model.h"
#include "engine/render/Renderer3D.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::terraweave {

struct GrassRenderOriginCm {
    int64_t x=0,y=0,z=0;
};

struct GrassRenderQuality {
    uint32_t maxBladesPerPatch=1024; // 1..8192, stable bounded samples
    bool crossedBlades=true;         // 2 triangles instead of 1 per blade
    uint32_t maxGpuPatches=128;      // 1..256
    uint32_t maxUploadsPerFrame=2;   // 1..8
};

inline void validateGrassRenderQuality(const GrassRenderQuality& q) {
    if(!q.maxBladesPerPatch || q.maxBladesPerPatch>8192 ||
       !q.maxGpuPatches || q.maxGpuPatches>256 ||
       !q.maxUploadsPerFrame || q.maxUploadsPerFrame>8)
        throw std::invalid_argument("TerraWeave grass RHI quality");
}

inline bool sameGrassPresentation(const GrassPatch& a,const GrassPatch& b) {
    return a.key==b.key &&
        a.bounds.minX==b.bounds.minX && a.bounds.minZ==b.bounds.minZ &&
        a.bounds.maxX==b.bounds.maxX && a.bounds.maxZ==b.bounds.maxZ &&
        a.maximumBlades==b.maximumBlades &&
        a.coveragePermille==b.coveragePermille &&
        a.environment==b.environment && a.seed==b.seed;
}

using GrassSoilHeight=std::function<int32_t(int64_t,int64_t)>;

// Pure CPU model generation; only one or two opaque slender triangles per
// sampled blade. No per-patch alpha textures or unique shader variants.
// The sparse, stable stratified sample reacts to coverage/light/age without
// scanning a 262144-blade patch or allocating persistent blade arrays.
inline std::unique_ptr<render::Model> makeGrassPatchModel(
        const GrassPatch& patch,const GrassSoilHeight& ground,
        GrassRenderQuality quality={}) {
    validateGrassRenderQuality(quality);
    if(!ground)throw std::invalid_argument("TerraWeave grass ground sampler");
    const auto draw=prepareGrassPatch(patch);
    if(draw.bladeCount==0)return {};
    auto model=std::make_unique<render::Model>();
    model->path="terraweave/grass/patch-"+std::to_string(patch.key);
    model->batching.autoLod=false;
    model->batching.allowGeometryPool=false;

    render::Material grass;
    grass.name="TerraWeave Grass Patch";
    grass.baseColor={0.38f,0.64f,0.25f,1.0f};
    grass.roughness=0.95f;
    grass.doubleSided=true;
    grass.vertexColors=true;
    grass.castShadow=false; // avoid expensive per-blade shadow casters
    model->materials.push_back(grass);
    render::MeshDef mesh;
    mesh.name="Grass Batch";
    render::Primitive primitive;
    primitive.material=0;
    const auto appearance=plantAppearance(patch.environment);
    const uint32_t candidates=std::min(
        quality.maxBladesPerPatch,patch.maximumBlades);
    const uint32_t keep=uint32_t(
        uint64_t(patch.coveragePermille)*
        appearance.foliagePermille/1000u);
    if(candidates) {
        model->vertices.reserve(size_t(candidates)*
            (quality.crossedBlades?6u:3u));
        model->indices.reserve(size_t(candidates)*
            (quality.crossedBlades?6u:3u));
    }
    const int64_t spanX=patch.bounds.maxX-patch.bounds.minX;
    const int64_t spanZ=patch.bounds.maxZ-patch.bounds.minZ;
    const int32_t baseWidthCm=6;
    const int32_t baseHeightCm=26;
    const uint32_t light=patch.environment.lightPermille;
    const auto rgba=[](uint32_t r,uint32_t g,uint32_t b){
        return (0xffu<<24) | (b<<16) | (g<<8) | r;
    };
    const auto emitTriangle=[&](render::float3 a,render::float3 b,
                                render::float3 tip,uint32_t color){
        const auto normal=render::normalize(
            render::cross(b-a,tip-a));
        for(const auto& p:{a,b,tip}) {
            render::ModelVertex v;
            v.pos=p;
            v.nrm=normal;
            v.color=color;
            v.uv={p.y>std::max(a.y,b.y)?0.5f:0.0f,
                  p.y>std::max(a.y,b.y)?1.0f:0.0f};
            model->indices.push_back(
                static_cast<uint32_t>(model->vertices.size()));
            model->vertices.push_back(v);
        }
    };
    for(uint32_t slot=0;slot<candidates;++slot) {
        // Stable stratification of a large logical grass population:
        // changing coverage does not move the survivors.
        const uint32_t logicalIndex=uint32_t(
            uint64_t(slot)*patch.maximumBlades/candidates);
        const uint64_t h=detail::mix64(
            patch.seed^patch.key^
            (uint64_t(logicalIndex)*0x9e3779b97f4a7c15ULL));
        if(h%1000u>=keep)continue;
        const int64_t x=patch.bounds.minX+
            int64_t((h>>13)%uint64_t(spanX));
        const int64_t z=patch.bounds.minZ+
            int64_t((h>>35)%uint64_t(spanZ));
        const int32_t y=ground(x,z);
        if(y< -1000000 || y>1000000)
            throw std::out_of_range("TerraWeave grass soil height");
        const int32_t visualSize=std::max<int32_t>(
            1,int32_t(appearance.sizePermille)*
            (750+int32_t((h>>20)%501u))/1000);
        const float bladeHeight=float(baseHeightCm)*visualSize*0.00001f;
        const float bladeWidth=float(baseWidthCm)*
            (850+float((h>>46)%301u))/100000.f;
        const float mx=float(x-patch.bounds.minX)*0.01f;
        const float mz=float(z-patch.bounds.minZ)*0.01f;
        const float my=float(y)*0.01f;
        const float bias=(float(int32_t((h>>48)%101u)-50))*0.0003f;
        const uint32_t shade=uint32_t(std::clamp<int>(
            int(light*220u/1000u)+int((h>>29)%36u),70,255));
        const uint32_t color=rgba(shade,shade,
            uint32_t(std::max(1u,shade*3u/4u)));
        emitTriangle({mx-bladeWidth,my,mz},
                     {mx+bladeWidth,my,mz},
                     {mx+bias,my+bladeHeight,mz+bias},color);
        if(quality.crossedBlades)
            emitTriangle({mx,my,mz-bladeWidth},
                         {mx,my,mz+bladeWidth},
                         {mx-bias,my+bladeHeight,mz+bias},color);
    }
    primitive.firstVertex=0;
    primitive.vertexCount=static_cast<uint32_t>(model->vertices.size());
    primitive.firstIndex=0;
    primitive.indexCount=static_cast<uint32_t>(model->indices.size());
    if(!primitive.indexCount)return {}; // completely bare patch: no GPU object
    mesh.primitives.push_back(primitive);
    model->meshes.push_back(std::move(mesh));
    render::Node node;
    node.name="Grass Patch";
    node.mesh=0;
    model->nodes.push_back(std::move(node));
    model->finalize();
    return model;
}

// Device-owned residency/culling. Caller publishes new patch snapshots after
// one ECS tick and provides a terrain sampler valid on the current owner
// thread only. No worker captures mutable Earthworks or runs gameplay code.
class GrassPatchRenderer {
public:
    explicit GrassPatchRenderer(GrassRenderQuality quality={})
        :quality_(quality) {validateGrassRenderQuality(quality_);}
    size_t residentPatches()const{return resident_.size();}
    const GrassRenderQuality& quality()const{return quality_;}
    void setQuality(GrassRenderQuality quality) {
        validateGrassRenderQuality(quality);
        if(quality.maxBladesPerPatch!=quality_.maxBladesPerPatch ||
           quality.crossedBlades!=quality_.crossedBlades)
            for(const auto& [key,entry]:resident_){
                (void)entry;dirty_.insert(key);
            }
        quality_=quality;
    }

    // Mark only grass whose footprint intersects disturbed ground.
    // Native ECS rollback/save restore invalidates all retained render
    // geometry without touching authoritative grass coverage.
    void invalidateAll(){
        for(const auto& [id,patch]:resident_){
            (void)patch;
            dirty_.insert(id);
        }
    }

    void notifyTerrainEdited(const EarthworkChange& change){
        if(change.vertices.empty())return;
        for(const auto& [id,resident]:resident_)
            if(resident.patch.bounds.minX<change.dirty.maxX &&
               resident.patch.bounds.maxX>change.dirty.minX &&
               resident.patch.bounds.minZ<change.dirty.maxZ &&
               resident.patch.bounds.maxZ>change.dirty.minZ)
                dirty_.insert(id);
    }

    // Submit at most N new/rebuilt GPU patch meshes. Old models stay resident
    // during upload failure; unused patches retire only once required patches
    // are ready (streamer load-before-unload).
    size_t update(gfx::Device& device,
                  std::span<const GrassPatch> visible,
                  const GrassSoilHeight& ground) {
        if(!ground || visible.size()>quality_.maxGpuPatches)
            throw std::invalid_argument("TerraWeave grass visible patch budget");
        std::map<uint64_t,GrassPatch> requested;
        for(const auto& patch:visible) {
            (void)prepareGrassPatch(patch);
            if(!patch.key || !requested.emplace(patch.key,patch).second)
                throw std::invalid_argument("TerraWeave grass duplicate key");
        }
        size_t uploaded=0;
        for(const auto& [id,patch]:requested) {
            const auto old=resident_.find(id);
            if(old!=resident_.end() &&
               !dirty_.contains(id) &&
               sameGrassPresentation(patch,old->second.patch))
                continue;
            if(uploaded>=quality_.maxUploadsPerFrame)break;
            auto candidate=makeGrassPatchModel(patch,ground,quality_);
            if(candidate) {
                render::prepareModelUpload(*candidate);
                if(!render::uploadModel(device,*candidate) ||
                   !candidate->vb.valid() || !candidate->ib.valid()) {
                    render::releaseModel(device,*candidate);
                    break; // retain previously published model and retry
                }
            }
            Resident next{patch,std::move(candidate)};
            if(old!=resident_.end()){
                if(old->second.model)
                    render::releaseModel(device,*old->second.model);
                old->second=std::move(next);
            } else resident_.emplace(id,std::move(next));
            dirty_.erase(id);
            ++uploaded;
        }
        bool complete=true;
        for(const auto& [id,patch]:requested) {
            const auto found=resident_.find(id);
            if(found==resident_.end() ||
               dirty_.contains(id) ||
               !sameGrassPresentation(patch,found->second.patch)) {
                complete=false;
                break;
            }
        }
        if(complete)
            for(auto it=resident_.begin();it!=resident_.end();){
                if(!requested.contains(it->first)){
                    if(it->second.model)
                        render::releaseModel(device,*it->second.model);
                    dirty_.erase(it->first);
                    it=resident_.erase(it);
                }else ++it;
            }
        return uploaded;
    }

    void draw(render::Renderer3D& renderer,
              GrassRenderOriginCm origin={})const{
        for(const auto& [id,resident]:resident_){
            (void)id;
            if(!resident.model)continue;
            render::ModelDraw draw;
            draw.model=resident.model.get();
            draw.world=render::mat4::translate({
                float(resident.patch.bounds.minX-origin.x)*0.01f,
                float(-origin.y)*0.01f,
                float(resident.patch.bounds.minZ-origin.z)*0.01f
            });
            draw.flags=render::kDrawStatic|render::kDrawNoCastShadow;
            renderer.drawModel(draw);
        }
    }

    void shutdown(gfx::Device& device){
        for(auto& [id,entry]:resident_) {
            (void)id;
            if(entry.model)render::releaseModel(device,*entry.model);
        }
        resident_.clear();
        dirty_.clear();
    }
private:
    struct Resident {
        GrassPatch patch{};
        std::unique_ptr<render::Model> model;
    };
    GrassRenderQuality quality_;
    std::map<uint64_t,Resident> resident_;
    std::set<uint64_t> dirty_;
};

} // namespace aiverse::terraweave
