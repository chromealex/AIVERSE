// TerraWeave: bounded grass patch RHI adapter. Thousands of blades become
// one draw per visible patch, not one ECS entity / draw call per blade.
// Works with existing Renderer3D::drawModel and gfx::Device on its owner thread.
#pragma once
#include "engine/world/TerraWeaveGrassCover.h"
#include "engine/render/Model.h"
#include "engine/render/Renderer3D.h"
#include "engine/render/TerraWeaveVertexWind.h"
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
    uint32_t bladeSegments=1;       // 1..4; 2+ uses bent tapered ribbons
    uint32_t bladeHeightCm=26;      // 4..150
    uint32_t bladeHalfWidthCm=3;    // 1..12
    uint32_t bladesPerTuft=1;      // 1..32, stable shared centre
    uint32_t tuftRadiusCm=18;      // 1..100
    uint32_t sourceSamples=0;      // 0=own cap; LOD source sampling budget, <=8192
    uint32_t sampleStride=1;      // 1..8; LOD keeps seeded tuft centres
    uint32_t heightVariationPermille=250; // 0..600, symmetric size variation
    bool naturalBlades=false; // seeded pigment, narrow shoulder and varied sweep; no extra geometry
};

inline void validateGrassRenderQuality(const GrassRenderQuality& q) {
    if(!q.maxBladesPerPatch || q.maxBladesPerPatch>8192 ||
       !q.maxGpuPatches || q.maxGpuPatches>256 ||
       !q.maxUploadsPerFrame || q.maxUploadsPerFrame>8 ||
       q.bladeSegments<1 || q.bladeSegments>4 ||
       q.bladeHeightCm<4 || q.bladeHeightCm>150 ||
       q.bladeHalfWidthCm<1 || q.bladeHalfWidthCm>12 ||
       q.bladesPerTuft<1 || q.bladesPerTuft>32 || q.tuftRadiusCm<1 || q.tuftRadiusCm>100 ||
       q.sourceSamples>8192 || q.sampleStride<1 || q.sampleStride>8 || q.heightVariationPermille>600)
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
using GrassPlacementFilter=std::function<bool(int64_t,int64_t)>;

// Optional host-supplied presentation overlay (2026-11-21). TerraWeave keeps
// owning grass coverage; a host (e.g. engine/world/PhysisNatureGrass.h for
// PHYSIS wildfire) only tells the renderer how each blade LOOKS. Nothing here
// is written back to GrassPatch, Earthworks or any ECS.
struct GrassFireSample {
    uint16_t charPermille=0;   // 0 fresh .. 1000 burned to stubble/ash
    uint16_t firePermille=0;   // flames on this blade right now
};
using GrassFireSampler=std::function<GrassFireSample(int64_t xCm,int64_t zCm)>;
struct GrassPresentationOptions {
    GrassFireSampler fire;     // empty: unchanged legacy appearance
    bool windSway=false;       // root->tip vertex alpha weights + canopyWindGraph
};
// Burning blades glow through a second, emissive primitive. Bloom of that
// emission belongs to PRISMA; actual flames/smoke to SpectraFX.
inline constexpr uint16_t kGrassBurningFirePermille=140;

// Pure CPU model generation; bounded indexed opaque ribbons per
// sampled blade. No per-patch alpha textures or unique shader variants.
// The sparse, stable stratified sample reacts to coverage/light/age without
// scanning a 262144-blade patch or allocating persistent blade arrays.
inline std::unique_ptr<render::Model> makeGrassPatchModel(
        const GrassPatch& patch,const GrassSoilHeight& ground,
        GrassRenderQuality quality={}, const GrassPlacementFilter& allows={},
        const GrassPresentationOptions* presentation=nullptr) {
    validateGrassRenderQuality(quality);
    const bool sway=presentation && presentation->windSway;
    const GrassFireSampler* fireSampler=
        presentation && presentation->fire ? &presentation->fire : nullptr;
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
    if(sway)grass.shaderGraph=canopyWindGraph();
    model->materials.push_back(grass);
    if(fireSampler) {
        render::Material burning=grass;
        burning.name="TerraWeave Grass Burning";
        burning.baseColor={1.0f,1.0f,1.0f,1.0f};
        burning.emissive={1.0f,0.42f,0.08f};
        burning.emissiveStrength=3.0f;
        model->materials.push_back(burning);
    }
    std::vector<uint32_t> burningIndices;
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
            (quality.crossedBlades?2u:1u)*(2*quality.bladeSegments+1));
        model->indices.reserve(size_t(candidates)*
            (quality.crossedBlades?2u:1u)*3*(2*quality.bladeSegments-1));
    }
    const int64_t spanX=patch.bounds.maxX-patch.bounds.minX;
    const int64_t spanZ=patch.bounds.maxZ-patch.bounds.minZ;
    const int32_t baseWidthCm=int32_t(quality.bladeHalfWidthCm);
    const int32_t baseHeightCm=int32_t(quality.bladeHeightCm);
    const uint32_t light=patch.environment.lightPermille;
    const auto rgba=[](uint32_t r,uint32_t g,uint32_t b){
        return (0xffu<<24) | (b<<16) | (g<<8) | r;
    };
    for(uint32_t slot=0;slot<candidates;++slot) {
        // Stable stratification of a large logical grass population:
        // changing coverage does not move the survivors.
        const uint32_t perTuft=(quality.bladesPerTuft+quality.sampleStride-1)/quality.sampleStride;
        const uint32_t sampleIndex=quality.bladesPerTuft==1
            ? slot*quality.sampleStride
            : (slot/perTuft)*quality.bladesPerTuft+(slot%perTuft)*quality.sampleStride;
        const uint32_t sourceSamples=quality.sourceSamples?quality.sourceSamples:candidates;
        if(sampleIndex>=sourceSamples)continue;
        const uint32_t logicalIndex=uint32_t(uint64_t(sampleIndex)*patch.maximumBlades/sourceSamples);
        const uint64_t h=detail::mix64(
            patch.seed^patch.key^
            (uint64_t(logicalIndex)*0x9e3779b97f4a7c15ULL));
        if(h%1000u>=keep)continue;
        int64_t x=patch.bounds.minX+
            int64_t((h>>13)%uint64_t(spanX));
        int64_t z=patch.bounds.minZ+
            int64_t((h>>35)%uint64_t(spanZ));
        if(quality.bladesPerTuft>1){
            const auto centre=detail::mix64(patch.seed^patch.key^(uint64_t(sampleIndex/quality.bladesPerTuft)*0x9e3779b97f4a7c15ULL));
            const int64_t radius=std::min<int64_t>(quality.tuftRadiusCm,std::min(spanX,spanZ)/4);
            x=patch.bounds.minX+radius+int64_t((centre>>13)%uint64_t(std::max<int64_t>(1,spanX-2*radius)));
            z=patch.bounds.minZ+radius+int64_t((centre>>35)%uint64_t(std::max<int64_t>(1,spanZ-2*radius)));
            x+=int64_t((h>>20)%uint64_t(2*radius+1))-radius;
            z+=int64_t((h>>44)%uint64_t(2*radius+1))-radius;
        }
        if(allows && !allows(x,z))continue;
        const int32_t y=ground(x,z);
        if(y< -1000000 || y>1000000)
            throw std::out_of_range("TerraWeave grass soil height");
        const int32_t visualSize=std::max<int32_t>(
            1,int32_t(appearance.sizePermille)*
            (1000-int32_t(quality.heightVariationPermille)+int32_t((h>>20)%(2*quality.heightVariationPermille+1)))/1000);
        float bladeHeight=float(baseHeightCm)*visualSize*0.00001f;
        // Host burn overlay: charred blades shrink to ash stubble with a
        // ragged per-blade edge; burning blades move to the emissive primitive.
        uint32_t charred=0, fire=0;
        bool burningBlade=false;
        if(fireSampler) {
            const auto burn=(*fireSampler)(x,z);
            const uint32_t c=std::min<uint32_t>(burn.charPermille,1000u);
            const uint32_t edge=uint32_t((h>>40)%200u);
            charred=c>edge ? std::min<uint32_t>(1000u,(c-edge)*1000u/(1000u-edge)) : 0u;
            fire=std::min<uint32_t>(burn.firePermille,1000u);
            bladeHeight*=float(1000u-charred*82u/100u)/1000.0f;
            burningBlade=fire>=kGrassBurningFirePermille && charred<950u;
        }
        const uint32_t ash=uint32_t(26+(h>>51)%22u);
        const auto finishColor=[&](uint32_t base,float t)->uint32_t{
            uint32_t out=base|0xff000000u;
            if(fireSampler) {
                const auto mixc=[&](uint32_t from,uint32_t to){
                    return (from*(1000u-charred)+to*charred)/1000u;
                };
                const uint32_t r=out&255u,g=(out>>8)&255u,b=(out>>16)&255u;
                out=rgba(mixc(r,ash+6u),mixc(g,ash+2u),mixc(b,ash));
                if(burningBlade) {
                    const uint32_t hot=std::min<uint32_t>(255u,150u+fire*105u/1000u);
                    out=rgba(255u,hot*2u/3u,hot/5u);
                }
            }
            // Wind sway weight for canopyWindGraph: 0 at the root, 1 at the tip.
            if(sway)out=(out&0x00ffffffu)|(uint32_t(std::lround(std::clamp(t,0.f,1.f)*255.f))<<24);
            return out;
        };
        auto& targetIndices=burningBlade?burningIndices:model->indices;
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
        const auto pigmentSeed=detail::mix64(patch.seed^patch.key^
            (uint64_t(sampleIndex/quality.bladesPerTuft)*0xd6e8feb86659fd93ULL));
        const bool straw=quality.naturalBlades && pigmentSeed%100u<8u;
        const float pigment=.82f+float((pigmentSeed>>17)%301u)*.001f;
        const float widthVariation=quality.naturalBlades?.55f+float((h>>7)%551u)*.001f:1.f;
        const float sweep=quality.naturalBlades?.16f+float((h>>33)%501u)*.001f:.36f;
        const float yaw=float((h>>10)%62832)*.0001f;
        for(uint32_t plane=0;plane<(quality.crossedBlades?2u:1u);++plane){
            const float a=yaw+float(plane)*1.57079633f;
            const render::float3 side{std::cos(a)*bladeWidth*widthVariation,0,std::sin(a)*bladeWidth*widthVariation};
            const render::float3 bend{-std::sin(a)*bladeHeight*sweep,0,std::cos(a)*bladeHeight*sweep};
            const uint32_t base=uint32_t(model->vertices.size());
            for(uint32_t seg=0;seg<=quality.bladeSegments;++seg){
                const float t=float(seg)/quality.bladeSegments;
                const render::float3 root{mx,my,mz};
                const auto center=root+render::float3{bias*t,bladeHeight*t,0}+bend*(t*t);
                const auto direction=render::float3{bias,bladeHeight,0}+bend*(2*t);
                const auto normal=render::normalize(render::cross(side,direction));
                for(uint32_t edge=0;edge<(seg==quality.bladeSegments?1u:2u);++edge){
                    render::ModelVertex vertex;
                    // A narrow root opens into a shoulder, then tapers to a point.
                    // Folded edge normals catch light without doubling the mesh.
                    const float width=(1-t)*(quality.naturalBlades?(.60f+1.20f*t):1.f);
                    vertex.pos=center+side*((edge?1.f:-1.f)*width);vertex.nrm=normal;
                    vertex.uv={seg==quality.bladeSegments?.5f:float(edge),t};vertex.color=color;
                    if(quality.naturalBlades){
                        const auto across=render::normalize(side);
                        vertex.nrm=render::normalize(normal+across*(edge?.24f:-.24f));
                        const float brightness=float(shade)*pigment;
                        const auto channel=[&](float gain){return uint32_t(std::clamp(std::lround(brightness*gain),1l,255l));};
                        const float root=.42f+.58f*std::sqrt(t);
                        vertex.color=straw?rgba(channel(root*1.23f),channel(root*.91f),channel(root*.65f)):
                            rgba(channel(root*(.78f+.30f*t)),channel(root),channel(root*(.55f+.22f*t)));
                    }
                    if(fireSampler||sway)vertex.color=finishColor(vertex.color,t);
                    model->vertices.push_back(vertex);
                }
                if(seg<quality.bladeSegments){
                    const uint32_t lo=base+seg*2, hi=lo+2;
                    targetIndices.insert(targetIndices.end(),{lo,lo+1,hi});
                    if(seg+1<quality.bladeSegments)targetIndices.insert(targetIndices.end(),{lo+1,hi+1,hi});
                }
            }
        }
    }
    primitive.firstVertex=0;
    primitive.vertexCount=static_cast<uint32_t>(model->vertices.size());
    primitive.firstIndex=0;
    primitive.indexCount=static_cast<uint32_t>(model->indices.size());
    if(!primitive.indexCount && burningIndices.empty())
        return {}; // completely bare patch: no GPU object
    if(primitive.indexCount)mesh.primitives.push_back(primitive);
    if(!burningIndices.empty()) {
        render::Primitive glowing=primitive;
        glowing.material=1;
        glowing.firstIndex=static_cast<uint32_t>(model->indices.size());
        glowing.indexCount=static_cast<uint32_t>(burningIndices.size());
        model->indices.insert(model->indices.end(),
                              burningIndices.begin(),burningIndices.end());
        mesh.primitives.push_back(glowing);
    }
    model->meshes.push_back(std::move(mesh));
    render::Node node;
    node.name="Grass Patch";
    node.mesh=0;
    model->nodes.push_back(std::move(node));
    model->finalize();
    return model;
}

// One shared authored density chain for both resident native patches and
// offline export. CPU generation happens only on patch upload/rebuild.
inline std::unique_ptr<render::Model> makeGrassPatchLodModel(
        const GrassPatch& patch,const GrassSoilHeight& ground,
        GrassRenderQuality quality={},const GrassPlacementFilter& allows={},
        const GrassPresentationOptions* presentation=nullptr) {
    auto model=makeGrassPatchModel(patch,ground,quality,allows,presentation);
    if(!model)return {};
    const auto maximum=std::min(quality.maxBladesPerPatch,patch.maximumBlades);
    for(uint32_t divisor:{3u,8u}) {
        auto tier=quality;
        tier.sampleStride=divisor;tier.sourceSamples=maximum;
        const auto tuftCount=(maximum+quality.bladesPerTuft-1)/quality.bladesPerTuft;
        tier.maxBladesPerPatch=std::min(maximum,tuftCount*((quality.bladesPerTuft+divisor-1)/divisor));
        tier.bladeSegments=std::max(1u,quality.bladeSegments/(divisor==3?2u:3u));
        tier.bladeHalfWidthCm=std::min(12u,uint32_t(std::ceil(quality.bladeHalfWidthCm*std::sqrt(float(divisor)))));
        auto lod=makeGrassPatchModel(patch,ground,tier,allows,presentation);
        if(!lod)continue;
        const auto vo=uint32_t(model->vertices.size()),io=uint32_t(model->indices.size());
        model->vertices.insert(model->vertices.end(),lod->vertices.begin(),lod->vertices.end());
        for(auto index:lod->indices)model->indices.push_back(index+vo);
        auto mesh=lod->meshes[0];
        for(auto& primitive:mesh.primitives){primitive.firstVertex+=vo;primitive.firstIndex+=io;}
        model->meshes[0].lods.push_back({int(model->meshes.size()),divisor==3?220.f:90.f});
        model->meshes.push_back(std::move(mesh));
    }
    model->batching.independentShadowLod=true;
    model->finalize();return model;
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
           quality.crossedBlades!=quality_.crossedBlades ||
           quality.bladesPerTuft!=quality_.bladesPerTuft || quality.tuftRadiusCm!=quality_.tuftRadiusCm ||
           quality.sourceSamples!=quality_.sourceSamples || quality.sampleStride!=quality_.sampleStride || quality.heightVariationPermille!=quality_.heightVariationPermille || quality.naturalBlades!=quality_.naturalBlades ||
           quality.bladeSegments!=quality_.bladeSegments ||
           quality.bladeHeightCm!=quality_.bladeHeightCm ||
           quality.bladeHalfWidthCm!=quality_.bladeHalfWidthCm)
            for(const auto& [key,entry]:resident_){
                (void)entry;dirty_.insert(key);
            }
        quality_=quality;
    }

    // Optional presentation overlay (see GrassPresentationOptions). The
    // signature callback returns a cheap appearance hash per patch rectangle
    // (e.g. NaturePresentationFrame::signature); a patch is rebuilt only when
    // its hash changes, still within maxUploadsPerFrame.
    void setPresentation(GrassPresentationOptions options,
                         std::function<uint64_t(const RectCm&)> signature={}) {
        const bool layout=options.windSway!=presentation_.windSway ||
                          bool(options.fire)!=bool(presentation_.fire);
        presentation_=std::move(options);
        signature_=std::move(signature);
        if(layout)invalidateAll();
    }
    void clearPresentation(){setPresentation({}, {});}
    // Optional deferred GPU retirement (editor: Editor::retirePreviewModel).
    // Without it replaced models are released immediately, as before.
    void setModelRetirement(std::function<void(std::unique_ptr<render::Model>&)> retire){
        retire_=std::move(retire);
    }
    // Per-frame wind for windSway (world m/s, x/z), animation phase radians
    // and gust 0..1. Bounded tip lean 0.14 m at >= 16 m/s.
    void setWind(float xMetresPerSecond,float zMetresPerSecond,
                 float phaseRadians,float gust=0.0f) {
        const auto finite=[](float v){return std::isfinite(v)?v:0.0f;};
        const float x=finite(xMetresPerSecond),z=finite(zMetresPerSecond);
        const float speed=std::sqrt(x*x+z*z);
        const float lean=0.14f*std::clamp(speed/16.0f,0.0f,1.0f);
        const float k=speed>1e-4f?lean/speed:0.0f;
        wind_={x*k,z*k,finite(phaseRadians),std::clamp(finite(gust),0.0f,1.0f)};
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
    // Excludes blades (e.g. inside authored water). Applies to patches built
    // afterwards: call invalidateAll() when the filter changes.
    void setPlacementFilter(GrassPlacementFilter filter) {placement_=std::move(filter);}
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
            const uint64_t look=signature_?signature_(patch.bounds):0;
            if(old!=resident_.end() &&
               !dirty_.contains(id) &&
               sameGrassPresentation(patch,old->second.patch) &&
               old->second.signature==look)
                continue;
            if(uploaded>=quality_.maxUploadsPerFrame)break;
            const bool overlay=presentation_.windSway||bool(presentation_.fire);
            auto candidate=makeGrassPatchLodModel(patch,ground,quality_,placement_,
                                                  overlay?&presentation_:nullptr);
            if(candidate) {
                render::prepareModelUpload(*candidate);
                if(!render::uploadModel(device,*candidate) ||
                   !candidate->vb.valid() || !candidate->ib.valid()) {
                    render::releaseModel(device,*candidate);
                    break; // retain previously published model and retry
                }
            }
            Resident next{patch,std::move(candidate),look};
            if(old!=resident_.end()){
                retire(device,old->second.model);
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
                    retire(device,it->second.model);
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
            if(presentation_.windSway) {
                draw.useVertexParameters=true;
                draw.vertexParameters=wind_;
                draw.maxVertexOffsetMetres=0.25f;
            }
            renderer.drawModel(draw);
        }
    }

    void shutdown(gfx::Device& device){
        for(auto& [id,entry]:resident_) {
            (void)id;
            retire(device,entry.model);
        }
        resident_.clear();
        dirty_.clear();
    }
private:
    struct Resident {
        GrassPatch patch{};
        std::unique_ptr<render::Model> model;
        uint64_t signature=0;
    };
    GrassRenderQuality quality_;
    GrassPlacementFilter placement_;
    GrassPresentationOptions presentation_;
    std::function<uint64_t(const RectCm&)> signature_;
    std::function<void(std::unique_ptr<render::Model>&)> retire_;
    render::float4 wind_{0,0,0,0};
    void retire(gfx::Device& device,std::unique_ptr<render::Model>& model){
        if(!model)return;
        if(retire_)retire_(model);
        else render::releaseModel(device,*model);
    }
    std::map<uint64_t,Resident> resident_;
    std::set<uint64_t> dirty_;
};

} // namespace aiverse::terraweave
