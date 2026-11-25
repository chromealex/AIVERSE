// Frozen copy of the generator from origin/main before meadow grass
// (engine/render/TerraWeaveGrassRender.h, 2026-11-21). The current generator
// must reproduce it byte-for-byte when GrassRenderQuality::meadow is empty.
// Do not edit: it is the compatibility reference, not engine code.
#pragma once
#include "engine/render/TerraWeaveGrassRender.h"

namespace aiverse::terraweave::legacyref {
// Pure CPU model generation; bounded indexed opaque ribbons per
// sampled blade. No per-patch alpha textures or unique shader variants.
// The sparse, stable stratified sample reacts to coverage/light/age without
// scanning a 262144-blade patch or allocating persistent blade arrays.
inline std::unique_ptr<render::Model> legacyGrassPatchModel(
        const GrassPatch& patch,const GrassSoilHeight& ground,
        GrassRenderQuality quality={}, const GrassPlacementFilter& allows={}) {
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
                    model->vertices.push_back(vertex);
                }
                if(seg<quality.bladeSegments){
                    const uint32_t lo=base+seg*2, hi=lo+2;
                    model->indices.insert(model->indices.end(),{lo,lo+1,hi});
                    if(seg+1<quality.bladeSegments)model->indices.insert(model->indices.end(),{lo+1,hi+1,hi});
                }
            }
        }
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

// One shared authored density chain for both resident native patches and
// offline export. CPU generation happens only on patch upload/rebuild.
inline std::unique_ptr<render::Model> legacyGrassPatchLodModel(
        const GrassPatch& patch,const GrassSoilHeight& ground,
        GrassRenderQuality quality={},const GrassPlacementFilter& allows={}) {
    auto model=legacyGrassPatchModel(patch,ground,quality,allows);
    if(!model)return {};
    const auto maximum=std::min(quality.maxBladesPerPatch,patch.maximumBlades);
    for(uint32_t divisor:{3u,8u}) {
        auto tier=quality;
        tier.sampleStride=divisor;tier.sourceSamples=maximum;
        const auto tuftCount=(maximum+quality.bladesPerTuft-1)/quality.bladesPerTuft;
        tier.maxBladesPerPatch=std::min(maximum,tuftCount*((quality.bladesPerTuft+divisor-1)/divisor));
        tier.bladeSegments=std::max(1u,quality.bladeSegments/(divisor==3?2u:3u));
        tier.bladeHalfWidthCm=std::min(12u,uint32_t(std::ceil(quality.bladeHalfWidthCm*std::sqrt(float(divisor)))));
        auto lod=legacyGrassPatchModel(patch,ground,tier,allows);
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

} // namespace aiverse::terraweave::legacyref
