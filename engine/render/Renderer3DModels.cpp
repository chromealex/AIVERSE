// Renderer3D: models (PBR materials, GPU skinning, alpha modes) — shader permutations, culling,
// sorting and the per-draw uniform blocks. See ModelShaders.h for the uniform layouts.
#include "Renderer3D.h"
#include "ModelMerge.h"
#include "GpuMaterialState.h"
#include "ModelShaders.h"
#include "Animation.h"
#include "ModelVisibility.h"
#include "ModelLodTransition.h"
#include "GpuIcbPolicy.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <chrono>
#include <unordered_map>
#include <tuple>
#include <functional>

namespace aiverse::render {

namespace {
struct MaterialUniforms { float base[4]; float emissive[4]; float pbr[4]; float misc[4]; float uv0[4]; float uv1[4]; float light[4]; float reveal[4]; float revealAxis[4]; };
constexpr size_t kJointFloats = size_t(kMaxPaletteJoints) * 12; // 3 rows of vec4 per joint
static_assert(kJointFloats * 4 <= 4096, "joint palette must fit a 4 KB uniform block");

constexpr uint32_t kKeyDrawCallView = 32768;
constexpr uint32_t kKeyMaterialInstancePalette = 1u << 27; // editor draw-call diagnostic shader permutation
constexpr uint32_t kKeySkinned = 1, kKeyNormalMap = 2, kKeyAlphaMask = 4, kKeyShadow = 8, kKeyBaked = 64, kKeyMorph = 128,
                   kKeyInstanced = 256, kKeyOutline = 512, kKeyLightmap = 2048, kKeyProbes = 4096, kKeyLodFade = 8192, kKeyMaterialPalette = 16384;   // GI keys: agent-lightbake
struct ProfileTimer {
    double* value;
    std::chrono::steady_clock::time_point start;
    explicit ProfileTimer(double* v):value(v) { if(value)start=std::chrono::steady_clock::now(); }
    ~ProfileTimer(){if(value)*value+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}
};

bool transparentItem(uint32_t flags) { return (flags & kDrawTransparent) != 0; }
// Model fragment slots 0..6, 9..12 are taken (12: SSR scene depth); up to four graph textures follow.
// Metal has sampler slots 0..15 only, so graph slots from 16 use a constexpr repeat/linear sampler.
constexpr int kGraphTextureSlot = 13;
std::string metalGraphSampler(int slot) {
    return slot < 16 ? ", sampler s"+std::to_string(slot)+" [[sampler("+std::to_string(slot)+")]]" : std::string();
}

} // namespace

MaterialQuality Renderer3D::defaultMaterialQualityFor(const gfx::Device& dev, bool mobile) {
    if (const char* q = std::getenv("AIVERSE_MATERIALS")) { // testing override: low|medium|high
        std::string v = q;
        return v == "low" ? MaterialQuality::Low : v == "medium" ? MaterialQuality::Medium : MaterialQuality::High;
    }
    const std::string& n = dev.info().name;
    if (mobile) {
        for (const char* weak : {"Mali-4", "Mali-T", "Adreno (TM) 3", "Adreno (TM) 4", "Adreno (TM) 50", "PowerVR SGX", "PowerVR Rogue GE"})
            if (n.find(weak) != std::string::npos) return MaterialQuality::Low;
        return MaterialQuality::Medium;
    }
    return MaterialQuality::High;
}

void Renderer3D::initModels() {
    auto tex1x1 = [&](uint32_t rgbaPx, const char* label) {
        gfx::TextureDesc td; td.width = td.height = 1; td.data = &rgbaPx; td.repeat = true; td.label = label;
        return dev_->createTexture(td);
    };
    white_ = tex1x1(0xffffffffu, "white");
    black_ = tex1x1(0xff000000u, "black");
    flatNormal_ = tex1x1(0xffff8080u, "flat normal");
    jointData_.reserve(kJointFloats * 64);
    setInstanceLimit(0);
}

void Renderer3D::shutdownModels() {
    if (indirectCommands_.valid()) dev_->destroyBuffer(indirectCommands_);
    indirectCommands_ = {};
    modelError_.clear();
    for (auto& [k, p] : modelPipes_) dev_->destroyPipeline(p);
    for (auto& [k, s] : modelShaders_) if (s.shader.valid()) dev_->destroyShader(s.shader);
    pendingModelPipes_.clear(); modelPipes_.clear(); modelShaders_.clear();
    for (auto t : {white_, black_, flatNormal_}) dev_->destroyTexture(t);
}

void Renderer3D::setInstanceLimit(uint32_t limit) {
    const auto maximum=dev_?uint32_t(std::clamp<size_t>(dev_->maxUniformBytes()/144,1,256)):32u;
    const auto next=limit?std::clamp(limit,1u,maximum):maximum;
    if(next==instanceLimit_)return;
    instanceLimit_=next;
    if(!dev_)return;
    for(auto& [key,p]:modelPipes_)dev_->destroyPipeline(p);
    for(auto& [key,s]:modelShaders_)if(s.shader.valid())dev_->destroyShader(s.shader);
    pendingModelPipes_.clear(); modelPipes_.clear();modelShaders_.clear();
}
void StaticModelCache::prepare(const Model& source,const mat4& transform) {
    model=&source;world=transform;transforms.clear();primitiveOffsets.clear();
    for(const auto& m:source.bindWorld)transforms.push_back(world*m);
    size_t offset=0;for(const auto& mesh:source.meshes){primitiveOffsets.push_back(offset);offset+=mesh.primitives.size();}
    displaced=std::any_of(source.materials.begin(),source.materials.end(),[](const Material& m){return m.shaderGraph&&m.shaderGraph->outputs.count("vertexOffset");});
    const auto c=(source.boundsMin+source.boundsMax)*.5f,e=(source.boundsMax-source.boundsMin)*.75f+float3{.1f,.1f,.1f};
    min={1e30f,1e30f,1e30f};max={-1e30f,-1e30f,-1e30f};
    for(int i=0;i<8;++i){auto p=world.transformPoint({c.x+(i&1?e.x:-e.x),c.y+(i&2?e.y:-e.y),c.z+(i&4?e.z:-e.z)});min=min3(min,p);max=max3(max,p);}
}
void Renderer3D::setMaterialQuality(MaterialQuality q) {
    if (q == matQuality_) return;
    matQuality_ = q;
    if (!dev_) return;
    for (auto& [k, p] : modelPipes_) dev_->destroyPipeline(p);
    for (auto& [k, s] : modelShaders_) if (s.shader.valid()) dev_->destroyShader(s.shader);
    pendingModelPipes_.clear(); modelPipes_.clear(); modelShaders_.clear();
}

gfx::PipelineHandle Renderer3D::modelPipeline(const ModelItem& it, int phase, bool instanced,
                                              bool gpuInstances, bool instancePalette) {
    const bool shadow = phase == 0, outline = phase == 3, depthAfter = phase == 4;
    const bool skinned = it.joints >= 0, baked = it.baked, morph = it.morph >= 0;
    const bool materialPalette = phase == 1 && (it.paletteMaterials || instancePalette);
    const bool lodFade = it.lodThreshold<1 || it.lodInvert;
    const bool mask = !outline && it.mat->alpha == AlphaMode::Mask;
    const bool normalMap = phase != 0 && !outline && it.mat->normalTex >= 0 && matQuality_ != MaterialQuality::Low;
    const bool blend = outline || (!shadow && !depthAfter && (it.mat->alpha == AlphaMode::Blend || it.tint.w < 0.99f || transparentItem(it.flags)));
    const bool twoSided = !outline && it.mat->doubleSided;
    const bool giPhase = phase == 1 || phase == 2;
    const bool lightmap = giPhase && it.gi == 1, probes = giPhase && it.gi == 2;
    const uint32_t gpuInstanceShaderBit = gpuInstances ? (1u << 26) : 0u;
    const uint32_t studioShaderBits = (geometryMainView_ && (phase == 1 || phase == 2) && studioDebugMode_ >= 2)
                                    ? uint32_t(studioDebugMode_) << 17 : 0u;
    const uint32_t shaderKey = gpuInstanceShaderBit | studioShaderBits |
                               (instancePalette ? kKeyMaterialInstancePalette : 0u) | (skinned ? kKeySkinned : 0u) | (normalMap ? kKeyNormalMap : 0u) | (mask ? kKeyAlphaMask : 0u) |
                               (shadow ? kKeyShadow : 0u) | (baked ? kKeyBaked : 0u) | (morph ? kKeyMorph : 0u) |
                               (instanced ? kKeyInstanced : 0u) | (outline ? kKeyOutline : 0u) |
                               ((drawCallDebug_ && geometryMainView_ && (phase == 1 || phase == 2)) ? kKeyDrawCallView : 0u) |
                               (lightmap ? kKeyLightmap : 0u) | (probes ? kKeyProbes : 0u) | (lodFade ? kKeyLodFade : 0u) | (materialPalette ? kKeyMaterialPalette : 0u);
    const bool fadeDepth = blend && !outline && it.mat->alpha != AlphaMode::Blend && (it.flags & kDrawFadeDepth) && !transparentItem(it.flags);
    const uint32_t pipeKey = shaderKey | (blend ? 16u : 0u) | (twoSided ? 32u : 0u) | (fadeDepth ? 64u : 0u) | (depthAfter ? 1024u : 0u);
    const auto selectedGraph=it.mat->shaderGraph && int(matQuality_)>=it.mat->shaderGraph->minQuality?it.mat->shaderGraph:nullptr;
    const auto* graph = selectedGraph.get();
    const ModelShaderKey skey{shaderKey, selectedGraph};
    const ModelShaderKey pkey{pipeKey, selectedGraph};
    for (auto& [k, p] : modelPipes_) if (k == pkey) return p;

    for (auto it=pendingModelPipes_.begin();it!=pendingModelPipes_.end();++it) if(it->first==pkey) {
        gfx::PipelineHandle result;
        if (!it->second(result)) { warmingPending_=true; return {}; }
        pendingModelPipes_.erase(it);
        if (!result.valid()) modelError_="model pipeline: "+dev_->lastError();
        modelPipes_.push_back({pkey,result}); return result;
    }
    ShaderEntry* entry = nullptr;
    for (auto& [k, s] : modelShaders_) if (k == skey) entry = &s;
    if (!entry) {
        if(std::getenv("AIVERSE_STREAM_TRACE"))std::fprintf(stderr,"[stream-pipeline] shader=%u graph=%d\n",shaderKey,graph?1:0);
        std::string defs;
        defs += "#define AIVERSE_INST " + std::to_string(instanced ? instanceLimit_ : 1) + "\n";
        if (gpuInstances && dev_->info().backend == gfx::Backend::Metal)
            defs += "#define GPU_INSTANCE_BUFFER 1\n";
        if (skinned) defs += "#define SKINNED 1\n";
        if (baked) defs += "#define BAKED 1\n";
        if (morph) defs += "#define MORPH 1\n";
        if (normalMap) defs += "#define NORMALMAP 1\n";
        if (mask) defs += "#define ALPHAMASK 1\n";
        if (materialPalette) defs += "#define MATERIAL_PALETTE 1\n";
        if (instancePalette) defs += "#define MATERIAL_INSTANCE_PALETTE 1\n";
        if (lodFade) defs += "#define LODFADE 1\n";
        if (outline) defs += "#define OUTLINE 1\n";
        if (shaderKey & kKeyDrawCallView) defs += "#define DRAW_CALL_VIEW 1\n";
        if (studioShaderBits) defs += "#define STUDIO_DEBUG_VIEW " + std::to_string(studioShaderBits >> 17) + "\n";
        if (lightmap) defs += "#define LIGHTMAP 1\n";
        if (probes) defs += "#define PROBES 1\n";
        if (lightmap || probes) defs += "#define AIVERSE_GIQ " + std::to_string(bakedQuality_ == BakedQuality::Low ? 1 : 2) + "\n";
        defs += "#define AIVERSE_Q " + std::to_string(int(matQuality_)) + "\n";
        std::string vs = defs + (shadow ? shaders::kModelShadowVS : shaders::kModelVS);
        std::string fs = defs + (shadow ? shaders::kModelShadowFS : shaders::kModelFS);
        std::string msl = defs + (shadow ? shaders::kModelShadowMSL : shaders::kModelMSL);
        if (graph) {
            auto replace = [](std::string& source,const std::string& marker,const std::string& text) {
                const auto at=source.find(marker);if(at!=std::string::npos)source.replace(at,marker.size(),text);
            };
            auto inject = [&](std::string& source,bool metal,bool vertexOnly) {
                const std::string v=metal?"float4":"vec4", v3=metal?"float3":"vec3";
                if(!vertexOnly) {
                    std::string declarations, args, macros;
                    for(size_t i=0;i<graph->textures.size();++i) {
                        const auto slot=std::to_string(kGraphTextureSlot+int(i)), index=std::to_string(i);
                        declarations+="uniform sampler2D tex"+slot+";\n";
                        args+=", texture2d<float> tex"+slot+" [[texture("+slot+")]]"+metalGraphSampler(kGraphTextureSlot+int(i));
                        macros+="#define SG_TEXTURE"+index+"(uv) "+(metal?"tex"+slot+".sample(s"+slot+",uv)":"texture(tex"+slot+",uv)")+"\n";
                        macros+="#define SG_TEXTURE_LOD"+index+"(uv,lod) "+(metal?"tex"+slot+".sample(s"+slot+",uv,level(lod))":"textureLod(tex"+slot+",uv,lod)")+"\n";
                    }
                    if(metal) {
                        const auto f=source.find("fragment ");const auto at=source.find(") {",f);
                        if(at!=std::string::npos)source.insert(at,args);
                    } else source=declarations+source;
                    const auto uv=metal?"in.uv":"v_uv", wp=metal?"in.wpos":"v_wpos", np=metal?"in.nrm":"v_nrm";
                    std::string code="\n#define SG_SAMPLE(uv) "+std::string(metal?"tex1.sample(s1,uv)":"texture(tex1,uv)")+"\n"+macros;
                    code+=std::string(metal?"float2":"vec2")+" sg_uv="+uv+"; "+v+" sg_input=base;\n";
                    code+=v3+" sg_world="+wp+", sg_object="+(metal?"in.opos":"v_opos")+", sg_normal="+np+", sg_view="+(metal?"f.camPos.xyz":"u_camPos.xyz")+"-"+wp+";\n";
                    code+=v+" sg_color="+(metal?"in.col":"v_col")+"; float sg_time="+(metal?"f.camPos.w":"u_camPos.w")+";\n";
                    // Projected world position gives the same normalized screen coordinates in colour/shadow passes.
                    code+=v+" sg_clip="+(metal?"f.viewProj":"u_viewProj")+"*"+v+"("+wp+",1.0); "+(metal?"float2":"vec2")+" sg_screen=sg_clip.xy/max(abs(sg_clip.w),0.00001)*0.5+0.5;\n";
                    code+=graph::statements(*graph,metal)+"base="+graph->outputs.at("color")+";\n#undef SG_SAMPLE\n";
                    replace(source,"// SG_COLOR",code);
                    std::string surface;
                    for(auto name:{"roughness","metallic","emission","normal"}) {
                        auto it=graph->outputs.find(name);if(it==graph->outputs.end())continue;
                        const std::string key=name;
                        if(key=="normal") surface+="n="+it->second+".xyz/max(length("+it->second+".xyz),0.00001);\n";
                        else surface+=(key=="roughness"?"rough":key=="emission"?"emis":"metallic")+std::string(" = ")+it->second+(key=="emission"?".rgb;\n":".r;\n");
                    }
                    replace(source,"// SG_SURFACE",surface);
                }
                if(vertexOnly || metal) {
                    const std::string pos=metal?"d.p":"p", normal=metal?"d.n":"n";
                    std::string code="{\n"+std::string(metal?"float2 sg_uv=uvTransform(m,in.uv);":"vec2 sg_uv=uvTransform(a_uv);");
                    code+=v3+" sg_object="+pos+".xyz, sg_world=(I.model*"+pos+").xyz;\n";
                    code+=v3+" sg_c0=I.model[0].xyz,sg_c1=I.model[1].xyz,sg_c2=I.model[2].xyz;\n";
                    code+=v3+" sg_normal="+(metal?"float3x3":"mat3")+"(cross(sg_c1,sg_c2),cross(sg_c2,sg_c0),cross(sg_c0,sg_c1))*"+normal+"*(dot(sg_c0,cross(sg_c1,sg_c2))<0.0?-1.0:1.0);\n";
                    code+=v3+" sg_view="+(metal?"f.camPos.xyz":"u_camPos.xyz")+"-sg_world; "+v+" sg_color="+(metal?"in.col":"a_col")+"; float sg_time="+(metal?"f.camPos.w":"u_camPos.w")+";\n";
                    std::string args,declarations,macros;
                    for(size_t i=0;i<graph->textures.size();++i) {
                        const auto slot=std::to_string(kGraphTextureSlot+int(i)),index=std::to_string(i);
                        declarations+="uniform sampler2D tex"+slot+";\n";
                        args+=", texture2d<float> tex"+slot+" [[texture("+slot+")]]"+metalGraphSampler(kGraphTextureSlot+int(i));
                        macros+="#define SG_TEXTURE_LOD"+index+"(uv,lod) "+(metal?"tex"+slot+".sample(s"+slot+",uv,level(lod))":"textureLod(tex"+slot+",uv,lod)")+"\n";
                    }
                    if(metal)replace(source,"vs_main(VERTEX_ARGS)","vs_main(VERTEX_ARGS"+args+")");else source=declarations+source;
                    code+=macros;
                    code+=metal?graph->vertexMsl:graph->vertexGlsl;
                    if(auto it=graph->outputs.find("vertexOffset");it!=graph->outputs.end())code+=pos+".xyz += "+it->second+".xyz;\n";
                    if(auto it=graph->outputs.find("vertexNormal");it!=graph->outputs.end())code+=normal+" = "+it->second+".xyz;\n";
                    code+="}\n";replace(source,"// SG_VERTEX",code);
                }
            };
            inject(vs,false,true);inject(fs,false,false);inject(msl,true,false);
            for(int slot=16;slot<kGraphTextureSlot+int(graph->textures.size());++slot) {
                const auto at=msl.find("using namespace metal;");
                if(at!=std::string::npos)msl.insert(at+22,"\nconstexpr sampler s"+std::to_string(slot)+"(coord::normalized, address::repeat, filter::linear, mip_filter::linear);");
            }
        }
        gfx::ShaderDesc sd;
        sd.glslVertex = vs.c_str(); sd.glslFragment = fs.c_str(); sd.msl = msl.c_str();
        // tex11/tex12: SSR scene colour/depth; graph textures follow at kGraphTextureSlot.
        sd.uniformSlots = shadow ? 5 : 6; sd.textureSlots = graph && !graph->textures.empty() ? kGraphTextureSlot+int(graph->textures.size()) : shadow ? 9 : 13; sd.label = shadow ? "model shadow" : "model"; // U5/tex9: lights, tex10: height field
        ShaderEntry e;
        if (warmingPipelines_) e.job=dev_->createShaderAsync(sd);
        else e.shader = dev_->createShader(sd);
        if (!e.job && !e.shader.valid()) {
            e.failed = true;
            modelError_ = "model shader (" + defs + "): " + dev_->lastError();
            std::fprintf(stderr, "%s\n", modelError_.c_str());
        }
        modelShaders_.push_back({skey, e});
        entry = &modelShaders_.back().second;
    }
    if (entry->job) {
        if (!entry->job(entry->shader)) { warmingPending_=true; return {}; }
        entry->job={};
        if (!entry->shader.valid()) { entry->failed=true; modelError_="model shader: "+dev_->lastError(); }
    }
    if(std::getenv("AIVERSE_STREAM_TRACE"))std::fprintf(stderr,"[stream-pipeline-state] key=%u phase=%d\n",pipeKey,phase);
    gfx::PipelineHandle ph;
    if (!entry->failed) {
        gfx::PipelineDesc pd;
        pd.shader = entry->shader;
        pd.attrs[0] = {0, gfx::VertexFormat::Float3, 0};
        pd.attrs[1] = {1, gfx::VertexFormat::Float3, 12};
        pd.attrs[2] = {2, gfx::VertexFormat::Float2, 24};
        pd.attrs[3] = {3, gfx::VertexFormat::Float4, 32};
        pd.attrs[4] = {4, gfx::VertexFormat::UByte4N, 48};
        pd.attrs[5] = {5, gfx::VertexFormat::UByte4, 52};
        pd.attrs[6] = {6, gfx::VertexFormat::UByte4N, 56};
        pd.attrCount = 7;
        pd.stride = sizeof(ModelVertex);
        pd.indexType = gfx::IndexType::UInt32;
        pd.cull = outline ? gfx::CullMode::Front : twoSided ? gfx::CullMode::None : gfx::CullMode::Back;
        if (shadow) {
            pd.depthOnly = true;
            pd.depthBiasConstant = 2.0f; pd.depthBiasSlope = 2.5f;
        } else if (blend) {
            pd.blend = gfx::BlendMode::Alpha;
            pd.depthWrite = fadeDepth;
        } else if (depthAfter) {
            pd.colorWrite = false;
        }
        pd.label = shadow ? "model shadow" : outline ? "model outline" : depthAfter ? "model depth" : "model";
        if (warmingPipelines_) {
            auto job=dev_->createPipelineAsync(pd);
            if (!job(ph)) { pendingModelPipes_.push_back({pkey,std::move(job)}); warmingPending_=true; return {}; }
        } else ph = dev_->createPipeline(pd);
        if (!ph.valid()) modelError_ = "model pipeline: " + dev_->lastError();
    }
    modelPipes_.push_back({pkey, ph});
    return ph;
}

bool Renderer3D::warmModelPipelines(const Model& model, const std::vector<Material>* materials) {
    warmingPipelines_=true; warmingPending_=false;
    auto warm=[&](const ModelItem& item,int phase,bool instanced) {
        const auto pipe=modelPipeline(item,phase,instanced);
        if (pipe.valid() && !dev_->warmPipelineColor(pipe,gfx::PixelFormat::RGBA16F)) warmingPending_=true;
    };
    const auto& mats = materials ? *materials : model.materials;
    bool lod = false, palette = false;
    for (const auto& mesh : model.meshes) {
        lod |= !mesh.lods.empty();
        palette |= !mesh.materialBatches.empty();
    }
    for (const auto& material : mats) {
        ModelItem item{}; item.model=&model; item.mat=&material; item.tint={1,1,1,1}; item.joints=model.skins.empty()?-1:0; item.morph=-1;
        for (int skin=0; skin<(model.baked.tex.valid()?2:1); ++skin) {
            item.baked=skin!=0; if(item.baked)item.joints=-1;
            for (int fade=0; fade<(lod?2:1); ++fade) {
                item.lodThreshold=fade?.5f:1.0f;
                for (bool instanced : {false,true}) for(int phase : {0,1}) {
                    item.paletteMaterials=nullptr; item.tint.w=1;
                    warm(item,phase,instanced);
                    // Fading passengers/roofs changes blend state, but should not compile during movement.
                    if(phase==1){
                        item.tint.w=.5f;warm(item,phase,instanced);
                        item.flags=kDrawFadeDepth;warm(item,phase,instanced);
                        item.tint.w=1;item.flags=0;
                        warm(item,3,instanced);warm(item,4,instanced);
                    }
                    if(phase==1 && palette){item.paletteMaterials=&mats;warm(item,phase,instanced);}
                }
            }
        }
    }
    warmingPipelines_=false; return !warmingPending_;
}

void Renderer3D::recordPersistentModel(const ModelDraw& d) {
    if (!persistentSceneEnabled_ || !d.persistentId || !d.model) return;
    GpuSceneRecord record{};
    std::memcpy(record.world, d.world.m, sizeof(record.world));
    const auto& lo = d.model->boundsMin;
    const auto& hi = d.model->boundsMax;
    record.boundsMin[0] = lo.x; record.boundsMin[1] = lo.y; record.boundsMin[2] = lo.z;
    record.boundsMax[0] = hi.x; record.boundsMax[1] = hi.y; record.boundsMax[2] = hi.z;
    record.geometry = d.model->vb.id;
    record.material = d.model->ib.id;
    record.flags = d.flags;
    auto it = persistentIds_.find(d.persistentId);
    if (it == persistentIds_.end()) {
        const auto handle = persistentScene_.insert(record);
        if (handle.valid())
            persistentIds_.emplace(d.persistentId, PersistentEntry{handle, persistentSceneEpoch_});
    } else {
        it->second.seen = persistentSceneEpoch_;
        persistentScene_.update(it->second.handle, record);
    }
}

void Renderer3D::syncPersistentModels() {
    if (!persistentSceneEnabled_ || !dev_) return;
    // Tolerate an entity being absent from an occasional Scene/Game viewport.
    // Stale handles are retired before its GPU slot is synchronized.
    for (auto it = persistentIds_.begin(); it != persistentIds_.end();) {
        if (persistentSceneEpoch_ > it->second.seen + 3) {
            persistentScene_.erase(it->second.handle);
            it = persistentIds_.erase(it);
        } else ++it;
    }
    persistentStats_.live = persistentScene_.liveCount();
    persistentStats_.ranges = persistentStats_.records = 0;
    persistentStats_.bytes = 0;
    persistentStats_.gpuFrustumDispatched = false;
    const uint64_t frame = dev_->frameSerial();
    if (!frame) return;
    if (!persistentUpload_)
        persistentUpload_ = std::make_unique<PersistentGpuSceneUpload>(*dev_);
    const auto changed = persistentUpload_->sync(persistentScene_, frame);
    persistentStats_.ranges = changed.ranges;
    persistentStats_.records = changed.records;
    persistentStats_.bytes = changed.bytes;
    static const bool gpuCull = [] {
        const char* setting = std::getenv("AIVERSE_GPU_SCENE_CULL");
        return setting && std::strcmp(setting, "1") == 0;
    }();
    if (gpuCull && persistentScene_.capacity() <= UINT32_MAX)
        persistentStats_.gpuFrustumDispatched = persistentCull_.dispatch(
            *dev_, persistentUpload_->buffer(frame),
            uint32_t(persistentScene_.capacity()), frame, viewProj_);
}

void Renderer3D::drawModel(const ModelDraw& d) {
    ProfileTimer timer(profiling_?&profile_.submitMs:nullptr);
    const Model* m = d.model;
    if (!m || !m->vb.valid() || m->nodes.empty()) return;
    recordPersistentModel(d);
    if (capture_ && (d.flags & kDrawStatic)) {   // light baker: the static scene, every node in world space
        const auto& pose = d.nodeWorld && d.nodeWorld->size() == m->nodes.size() ? *d.nodeWorld : m->bindWorld;
        std::vector<mat4> nodeWorld(pose.size());
        for (size_t i = 0; i < pose.size(); ++i) nodeWorld[i] = d.world * pose[i];
        capture_(d, nodeWorld);
    }
    const auto* cache=d.staticCache;
    if(cache&&(cache->model!=m||cache->transforms.size()!=m->nodes.size()||cache->primitiveOffsets.size()!=m->meshes.size()||std::memcmp(cache->world.m,d.world.m,sizeof d.world.m)!=0||d.nodeWorld||d.materials||d.nodeMaterial||d.pose||d.baked||!m->skins.empty()||!m->clips.empty()))cache=nullptr;
    if(cache)++profile_.cachedModels;
    // whole-model culling with the bind-pose bounds (padded: animation moves limbs outside them)
    float3 c = (m->boundsMin + m->boundsMax) * 0.5f, e = (m->boundsMax - m->boundsMin) * 0.75f + float3{0.1f, 0.1f, 0.1f};
    const auto& graphMaterials=d.materials && d.materials->size()>=m->materials.size()?*d.materials:m->materials;
    const bool displaced=cache?cache->displaced:std::any_of(graphMaterials.begin(),graphMaterials.end(),[this](const Material& mat){return mat.shaderGraph && int(matQuality_)>=mat.shaderGraph->minQuality && mat.shaderGraph->outputs.count("vertexOffset");});
    const bool inView = displaced || (cache?modelBoxVisible(viewProj_,cache->min,cache->max):modelBoxVisible(viewProj_ * d.world, c - e, c + e));
    const bool inShadow = quality_ != ShadowQuality::Off && !(d.flags & kDrawNoCastShadow) && (d.tint.w >= 0.99f || (d.flags & kDrawKeepShadow)) && d.dissolve <= 0 &&
                          (displaced || (cache?modelBoxVisible(lightViewProj_,cache->min,cache->max):modelBoxVisible(lightViewProj_ * d.world, c - e, c + e)));
    if (!inView && !inShadow) return;
    const std::vector<mat4>& world = d.nodeWorld && d.nodeWorld->size() == m->nodes.size() ? *d.nodeWorld : m->bindWorld;
    const std::vector<Material>& mats = d.materials && d.materials->size() >= m->materials.size() ? *d.materials : m->materials;
    const bool usePose = d.pose && d.pose->size() == m->nodes.size();
    float4 anim{0, 0, 0, 0};
    const bool canBake = d.baked && m->baked.tex.valid();
    if (canBake) {
        const BakedSample& b = *d.baked;
        anim = {float(bakedRow(*m, b.clipA, b.timeA, b.loopA)), float(bakedRow(*m, b.clipB, b.timeB, b.loopB)), std::clamp(b.mix, 0.0f, 1.0f), 0};
    }
    for (int n : m->order) {
        const Node& node = m->nodes[size_t(n)];
        if (node.mesh < 0) continue;
        ++modelStats_.nodes;
        const MeshDef& baseMesh = m->meshes[size_t(node.mesh)];
        const bool rigid = node.skin < 0 && !baseMesh.deformable && !displaced;
        const mat4 nodeWorld = cache ? cache->transforms[size_t(n)] : d.world * world[size_t(n)];
        bool nodeView = inView, nodeShadow = inShadow;
        if (rigid && modelCullingEnabled_) {
            nodeView = inView && modelBoxVisible(viewProj_ * nodeWorld, baseMesh.boundsMin, baseMesh.boundsMax);
            nodeShadow = inShadow && modelBoxVisible(lightViewProj_ * nodeWorld, baseMesh.boundsMin, baseMesh.boundsMax);
            if (!nodeView && !nodeShadow) ++modelStats_.culledNodes; // retained for later mirror/spot frusta
        }
        int meshIndex = node.mesh; size_t level = 0;
        ModelLodBlend lodBlend{node.mesh};
        // Palette poses provide conservative current skin bounds. Baked-only draws keep
        // LOD0 until they supply pose bounds; bind bounds can underestimate a bent limb.
        const bool skinLod = node.skin >= 0 && !displaced && usePose && !canBake && d.nodeWorld && d.nodeWorld->size() == m->nodes.size();
        if (modelLodEnabled_ && (rigid || skinLod) && !baseMesh.lods.empty() && d.variant < 0 && !d.nodeMaterial) {
            mat4 lodWorld = nodeWorld;
            float3 lodMin = baseMesh.boundsMin, lodMax = baseMesh.boundsMax;
            if (skinLod) {
                lodMin = {1e30f, 1e30f, 1e30f}; lodMax = {-1e30f, -1e30f, -1e30f};
                const Skin& skin = m->skins[size_t(node.skin)];
                for (const auto& prim : baseMesh.primitives) for (uint16_t joint : prim.jointPalette) {
                    if (size_t(joint) >= skin.joints.size()) continue;
                    const mat4 bone = world[size_t(skin.joints[joint])] * skin.inverseBind[joint];
                    for (int corner = 0; corner < 8; ++corner) {
                        const float3 v = bone.transformPoint({corner & 1 ? prim.boundsMax.x : prim.boundsMin.x, corner & 2 ? prim.boundsMax.y : prim.boundsMin.y, corner & 4 ? prim.boundsMax.z : prim.boundsMin.z});
                        lodMin = {std::min(lodMin.x, v.x), std::min(lodMin.y, v.y), std::min(lodMin.z, v.z)};
                        lodMax = {std::max(lodMax.x, v.x), std::max(lodMax.y, v.y), std::max(lodMax.z, v.z)};
                    }
                }
                lodWorld = d.world;
            }
            const float error = length(lodMax - lodMin);
            const bool zeroToOne = dev_->info().clipDepthZeroToOne;
            float pixels = nodeView ? geometryErrorPixels(viewProj_ * lodWorld, lodMin, lodMax, error, sceneW_, sceneH_, zeroToOne) : 0;
            if (nodeShadow) pixels = std::max(pixels, geometryErrorPixels(lightViewProj_ * lodWorld, lodMin, lodMax, error, shadowSize_, shadowSize_, zeroToOne));
            if (!nodeView && !nodeShadow) pixels = std::numeric_limits<float>::infinity(); // full detail for later mirror/spot frusta
            const bool opaque = d.tint.w>=.99f && d.dissolve<=0 && !transparentItem(d.flags) &&
                std::none_of(mats.begin(),mats.end(),[](const Material& mat){return mat.alpha==AlphaMode::Blend || mat.transparent;});
            const float fadeWidth = opaque && !m->geometry && matQuality_!=MaterialQuality::Low ? modelLodFadeWidth_ : 0;
            lodBlend=selectModelLod(node.mesh,baseMesh.lods,pixels,fadeWidth);
            meshIndex=lodBlend.mesh;level=lodBlend.level;
            if(lodBlend.incoming>=0)++profile_.lodTransitionNodes;
            if (meshIndex != node.mesh) ++profile_.lodNodes;
        }
        ++modelStats_.lodNodes[std::min<size_t>(level, 3)];
        // Shadow batch (mergeStaticPrimitives): one shadow draw covers the opaque parts of the mesh.
        // Material copies are common in game views; only shadow-incompatible overrides disable batching.
        for(int part=0;part<(lodBlend.incoming>=0?2:1);++part) {
        meshIndex=part?lodBlend.incoming:lodBlend.mesh;
        const MeshDef& drawMesh = m->meshes[size_t(meshIndex)];
        const bool shadowBatch = !m->geometry && !d.nodeMaterial && d.variant < 0 && shadowBatchCompatible(drawMesh, mats);
        std::vector<const MaterialBatch*> colourBatches;
        if (!m->geometry && !d.nodeMaterial && d.variant < 0 && d.tint.w >= .99f && !transparentItem(d.flags))
            for (const auto& batch : drawMesh.materialBatches)
                if (materialBatchCompatible(drawMesh, batch, mats)) colourBatches.push_back(&batch);
        auto addItem = [&](const Primitive& p, uint8_t shadowRole, uint8_t materialRole = 0) {
            if (p.indexCount == 0) return;
            int mi = p.material;
            if (d.variant >= 0 && d.variant < int(p.variantMaterial.size()) && p.variantMaterial[size_t(d.variant)] >= 0) mi = p.variantMaterial[size_t(d.variant)];
            if (d.nodeMaterial && size_t(n) < d.nodeMaterial->size() && (*d.nodeMaterial)[size_t(n)] >= 0 && &mats == d.materials) mi = (*d.nodeMaterial)[size_t(n)];
            ModelItem it;
            it.geometry = d.geometry;
            if(lodBlend.incoming>=0){it.lodThreshold=lodBlend.progress;it.lodInvert=(part==0);}
            it.firstIndex = p.firstIndex; it.indexCount = p.indexCount;
            // Restrict GPU LOD to a single compatible rigid primitive in each
            // of exactly two meshes. Complex materials and fading stay on CPU.
            static const bool gpuLod = [] {
                const char* value = std::getenv("AIVERSE_GPU_ICB_LOD");
                return value && std::strcmp(value, "1") == 0;
            }();
            if (gpuLod && modelLodEnabled_ && rigid && !m->geometry &&
                lodBlend.incoming < 0 && d.variant < 0 && !d.nodeMaterial &&
                !shadowBatch && materialRole == 0 && shadowRole == 0 &&
                baseMesh.lods.size() == 1 && baseMesh.primitives.size() == 1 &&
                baseMesh.materialBatches.empty() &&
                baseMesh.lods[0].mesh >= 0 &&
                size_t(baseMesh.lods[0].mesh) < m->meshes.size()) {
                const auto& nearPrim = baseMesh.primitives[0];
                const auto& lod = baseMesh.lods[0];
                const auto& farMesh = m->meshes[size_t(lod.mesh)];
                if (farMesh.primitives.size() == 1 && farMesh.materialBatches.empty()) {
                    const auto& farPrim = farMesh.primitives[0];
                    if (nearPrim.material == farPrim.material &&
                        nearPrim.indexCount && farPrim.indexCount &&
                        nearPrim.morphTargets == 0 && farPrim.morphTargets == 0 &&
                        nearPrim.jointPalette.empty() && farPrim.jointPalette.empty() &&
                        (&p == &nearPrim || &p == &farPrim) &&
                        lod.maxPixels > 0) {
                        it.gpuLodAvailable = true;
                        it.gpuLodNear = {nearPrim.indexCount, 1u,
                            nearPrim.firstIndex + m->gpuFirstIndex,
                            int32_t(m->gpuBaseVertex), 0};
                        it.gpuLodFar = {farPrim.indexCount, 1u,
                            farPrim.firstIndex + m->gpuFirstIndex,
                            int32_t(m->gpuBaseVertex), 0};
                        it.gpuLodMaxPixels = lod.maxPixels;
                        it.gpuLodError = length(baseMesh.boundsMax - baseMesh.boundsMin);
                        it.gpuLodBoundsMin = baseMesh.boundsMin;
                        it.gpuLodBoundsMax = baseMesh.boundsMax;
                    }
                }
            }
            size_t pi = cache?cache->primitiveOffsets[size_t(meshIndex)]:0;
            if(!cache)for (int mesh = 0; mesh < meshIndex; ++mesh) pi += m->meshes[size_t(mesh)].primitives.size();
            it.geometryPrimitive = shadowRole == 2 || materialRole == 2 ? 0 : pi + size_t(&p - m->meshes[size_t(meshIndex)].primitives.data());
            it.model = m; it.prim = &p; it.mat = &mats[size_t(std::clamp(mi, 0, int(mats.size()) - 1))];
            it.tint = d.tint; it.team = d.team; it.flash = d.flash; it.flags = d.flags | (mats[size_t(std::clamp(mi, 0, int(mats.size()) - 1))].transparent ? uint32_t(kDrawTransparent) : 0u);
            it.dissolve = d.dissolve; it.teamGlow = d.teamGlow; it.outline = d.outline;
            it.rigid = rigid;
            it.castShadow = !(d.flags & kDrawNoCastShadow) && (d.tint.w >= 0.99f || (d.flags & kDrawKeepShadow)) && d.dissolve <= 0 && it.mat->castShadow && it.mat->alpha != AlphaMode::Blend && !transparentItem(it.flags);
            it.inView = nodeView; it.inShadow = nodeShadow && it.castShadow;
            it.joints = -1; it.baked = false; it.anim = anim; it.morph = -1;
            const bool skinnedPrim = node.skin >= 0 && !p.jointPalette.empty();
            if (skinnedPrim && canBake && node.skin == m->baked.skin) {
                it.baked = true;
                it.world = d.world;
            } else if (skinnedPrim) {
                // joint matrices are in model space: the mesh node transform is ignored (glTF skinning rule)
                paletteMatrices(*m, m->skins[size_t(node.skin)], p, world, palette_);
                it.joints = int32_t(jointData_.size());
                jointData_.resize(jointData_.size() + kJointFloats, 0.0f);
                float* dst = jointData_.data() + it.joints;
                for (size_t j = 0; j < palette_.size() && j < size_t(kMaxPaletteJoints); ++j)
                    for (int r = 0; r < 3; ++r)
                        for (int col = 0; col < 4; ++col) dst[j * 12 + size_t(r) * 4 + size_t(col)] = palette_[j].at(r, col);
                it.world = d.world;
            } else {
                it.world = cache?cache->transforms[size_t(n)]:d.world * world[size_t(n)];
            }
            if (p.morphTargets > 0 && m->morphTex.valid()) {
                const float* w = usePose ? (*d.pose)[size_t(n)].w : node.weights;
                bool any = false;
                for (int k = 0; k < p.morphTargets; ++k) any |= w[k] != 0;
                if (any) {
                    it.morph = int32_t(morphData_.size());
                    morphData_.insert(morphData_.end(), w, w + kMaxMorphTargets);
                }
            }
            float3 wc = it.world.transformPoint((p.boundsMin + p.boundsMax) * 0.5f);
            // sort distance: ortho cameras along the view direction (Unity's TransparencySortMode.Default), else from the eye
            it.depth = cam_.ortho ? dot(wc - camPos_, normalize(cam_.target - camPos_)) : length(wc - camPos_);
            bakedItem(it, d, n);
            it.shadowRole = shadowRole;
            it.materialRole = materialRole;
            if (materialRole == 2) it.paletteMaterials = &mats;
            if (shadowRole == 2) it.inView = false;   // the batch exists only for shadow passes
            modelItems_.push_back(it);
        };
        for (size_t i = 0; i < drawMesh.primitives.size(); ++i) {
            const auto& p = drawMesh.primitives[i];
            bool covered = false;
            for (const auto* batch : colourBatches)
                covered |= std::find(batch->parts.begin(), batch->parts.end(), i) != batch->parts.end();
            addItem(p, shadowBatch && p.shadowBatched ? 1 : 0, covered ? 1 : 0);
        }
        for (const auto* batch : colourBatches) addItem(batch->primitive, 0, 2);
        if (shadowBatch) addItem(drawMesh.shadowBatch, 2);
        }
    }

}

void Renderer3D::packModelInstance(const ModelItem& x, int phase,
                                   const float4& callColour, float* d,
                                   uint32_t instanceMaterial) const {
    const bool colourDraw = drawCallDebug_ && geometryMainView_ && (phase == 1 || phase == 2);
    std::memcpy(d, x.world.m, 16 * sizeof(float));
    d[16] = colourDraw ? callColour.x : x.tint.x;
    d[17] = colourDraw ? callColour.y : x.tint.y;
    d[18] = colourDraw ? callColour.z : x.tint.z;
    d[19] = phase == 0 && (x.flags & kDrawKeepShadow) ? 1.f : x.tint.w;
    if (phase == 3) {
        d[20] = x.outline.x; d[21] = x.outline.y;
        d[22] = x.outline.z; d[23] = 0;
    } else {
        d[20] = x.team.x; d[21] = x.team.y;
        d[22] = x.team.z; d[23] = x.flash;
    }
    d[24] = (x.mat->receiveShadow && !(x.flags & kDrawNoReceiveShadow)) ? 1.0f : 0.0f;
    d[25] = x.dissolve;
    d[26] = x.outline.w;
    d[27] = phase == 3 ? 0.0f : x.teamGlow;
    d[28] = x.anim.x; d[29] = x.anim.y;
    d[30] = x.anim.z; d[31] = x.anim.w;
    d[32] = x.lodThreshold;
    d[33] = x.lodInvert ? 1.0f : 0.0f;
    // Two spare floats in the existing 144-byte Inst slot (vec4 lod).
    // Per-instance palette mode uses z; normal LOD and GI keep xy/anim.
    d[34] = float(instanceMaterial);
}

bool Renderer3D::drawModelBatch(const uint32_t* items, uint32_t count,
                                int phase, bool submit, bool gpuInstances,
                                bool instancePalette) {
    const bool tracePreparation = !submit && std::getenv("AIVERSE_ICB_PROFILE") != nullptr;
    const auto prepStart = tracePreparation ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    const ModelItem& it = modelItems_[items[0]];
    const bool instanced = count > 1;
    gfx::PipelineHandle ph = modelPipeline(it, phase, instanced, gpuInstances, instancePalette);
    if (!ph.valid()) return false;
    if (!(ph == currentPipe_)) {
        dev_->setPipeline(ph); currentPipe_ = ph;
        if (phase != 0) { lights.bind(5, 9); bindTrace(10); bindSceneTextures(11, 12); }   // U5 / tex9: dynamic lights; tex10: height field (traced terrain shadows)
    }
    const auto afterPipeBind = tracePreparation ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    // Upload the whole instance block declared by this backend's shader variant.
    const bool colourDraw = drawCallDebug_ && geometryMainView_ && (phase == 1 || phase == 2);
    const float4 callColour = colourDraw ? drawCallDebugColor(++debugMainDrawId_) : float4{1, 1, 1, 1};
    // Each indirect instance can index the PBR factors for its own material
    // without changing geometry, textures or the per-model node transform.
    // Kept separate from the existing per-vertex mesh material palettes.
    std::vector<const Material*> instanceMaterialSlots;
    if (instancePalette) instanceMaterialSlots.reserve(std::min<size_t>(count, kMaxMaterialPalette));
    if (!gpuInstances) {
        const uint32_t slots = instanced ? instanceLimit_ : 1u;
        instData_.assign(size_t(slots) * 36, 0.0f);
        for (uint32_t i = 0; i < count; ++i) {
            const auto& part = modelItems_[items[i]];
            uint32_t materialSlot = 0;
            if (instancePalette) {
                auto it = std::find(instanceMaterialSlots.begin(),
                                    instanceMaterialSlots.end(), part.mat);
                if (it == instanceMaterialSlots.end()) {
                    // Preflight in drawModelIndirectBatch enforces this.
                    if (instanceMaterialSlots.size() >= kMaxMaterialPalette) return false;
                    materialSlot = uint32_t(instanceMaterialSlots.size());
                    instanceMaterialSlots.push_back(part.mat);
                } else materialSlot = uint32_t(it - instanceMaterialSlots.begin());
            }
            packModelInstance(part, phase, callColour,
                              instData_.data() + size_t(i) * 36, materialSlot);
        }
    }
    const auto afterInstanceBuild = tracePreparation ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    if (!gpuInstances)
        dev_->setUniforms(0, instData_.data(), instData_.size() * sizeof(float));
    const auto afterInstanceUpload = tracePreparation ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    const Material& mt = *it.mat;
    // GI draws rebind tex7 / tex8 (lightmap, mask or probes): restore the model's own textures after them
    const bool newModel = !lastItem_ || lastItem_->model != it.model || (phase != 0 && lastItem_->gi != 0);
    // As in a state-oriented render batch, Material* identity does not decide
    // whether to upload U2. Distinct instances of the same GPU state share
    // shader parameters; geometry/model-specific state is handled separately.
    // GI can override sampled bindings, so the texture stage below still
    // refreshes those slots after a GI draw.
    const bool materialStateChanged = !lastItem_ ||
        ((lastItem_->mat != it.mat || lastItem_->model != it.model) &&
         !equivalentGpuMaterial(*lastItem_->model, *lastItem_->mat, *it.model, mt));
    const bool actorRevealChanged = lastItem_ &&
        ((lastItem_->flags ^ it.flags) & kDrawActorReveal) != 0;
    if (materialStateChanged || actorRevealChanged) {
        MaterialUniforms mu{};
        mu.base[0] = mt.baseColor.x; mu.base[1] = mt.baseColor.y; mu.base[2] = mt.baseColor.z; mu.base[3] = mt.baseColor.w;
        mu.emissive[0] = mt.emissive.x * mt.emissiveStrength; mu.emissive[1] = mt.emissive.y * mt.emissiveStrength; mu.emissive[2] = mt.emissive.z * mt.emissiveStrength;
        mu.pbr[0] = mt.metallic; mu.pbr[1] = mt.roughness; mu.pbr[2] = mt.normalScale; mu.pbr[3] = mt.occlusionStrength;
        mu.misc[0] = mt.alpha == AlphaMode::Mask ? mt.alphaCutoff : 0.0f;
        mu.misc[1] = float(int(mt.team)); mu.misc[2] = mt.teamStrength; mu.misc[3] = (mt.vertexColors ? 1.0f : 0.0f) + (mt.vertexOcclusion ? 2.0f : 0.0f) + (mt.vertexOcclusion && mt.backOcclusion ? 4.0f : 0.0f);
        mu.uv0[0] = mt.uvOffset.x; mu.uv0[1] = mt.uvOffset.y; mu.uv0[2] = mt.uvScale.x; mu.uv0[3] = mt.uvScale.y;
        mu.uv1[0] = mt.uvRotation; mu.uv1[1] = mt.shading == ShadingModel::Unlit ? 1.0f : 0.0f;
        mu.uv1[2] = mt.fixedShade.x; mu.uv1[3] = mt.fixedShade.y;
        mu.light[0] = mt.fixedLight.x; mu.light[1] = mt.fixedLight.y; mu.light[2] = mt.fixedLight.z; mu.light[3] = mt.fixedLight.w;
        if (phase != 0 && geometryMainView_ && (it.flags & kDrawActorReveal) && actorReveal.w > 0) {
            mu.reveal[0]=actorReveal.x;mu.reveal[1]=actorReveal.y;mu.reveal[2]=actorReveal.z;mu.reveal[3]=actorReveal.w;
            const float3 centre{actorReveal.x,actorReveal.y,actorReveal.z};
            const float3 axis=normalize(cam_.ortho ? cam_.eye()-cam_.target : cam_.eye()-centre);
            mu.revealAxis[0]=axis.x;mu.revealAxis[1]=axis.y;mu.revealAxis[2]=axis.z;
            mu.revealAxis[3]=cam_.ortho ? 0.f : 1.f/std::max(.01f,length(cam_.eye()-centre));
        }
        dev_->setUniforms(2, &mu, sizeof mu);
    }
    if (materialStateChanged || newModel) {
        auto tex = [&](int idx, gfx::TextureHandle def) {
            return idx >= 0 && idx < int(it.model->images.size()) && it.model->images[size_t(idx)].tex.valid() ? it.model->images[size_t(idx)].tex : def;
        };
        dev_->setTexture(1, tex(mt.baseTex, white_));
        if(mt.shaderGraph)for(size_t i=0;i<mt.shaderGraph->textures.size();++i)dev_->setTexture(kGraphTextureSlot+int(i),tex(mt.graphTextures[i],white_));
        if (phase != 0) {
            dev_->setTexture(2, tex(mt.mrTex, white_));
            dev_->setTexture(3, tex(mt.normalTex, flatNormal_));
            dev_->setTexture(4, tex(mt.occlusionTex, white_));
            dev_->setTexture(5, tex(mt.emissiveTex, white_));
            dev_->setTexture(6, tex(mt.teamTex, black_));
        }
    }
    if (newModel) {
        dev_->setTexture(7, it.model->baked.tex.valid() ? it.model->baked.tex : white_);
        dev_->setTexture(8, it.model->morphTex.valid() ? it.model->morphTex : white_);
    }
    if (it.gi && (phase == 1 || phase == 2)) bindBakedGI(it);
    if (instancePalette) {
        float palette[kMaxMaterialPalette * 8] = {};
        for (size_t i = 0; i < instanceMaterialSlots.size(); ++i) {
            const auto& material = *instanceMaterialSlots[i];
            float* p = palette + i * 8;
            p[0] = material.baseColor.x; p[1] = material.baseColor.y;
            p[2] = material.baseColor.z; p[3] = material.baseColor.w;
            p[4] = material.metallic; p[5] = material.roughness;
        }
        dev_->setUniforms(3, palette, sizeof palette);
    } else if (phase == 1 && it.paletteMaterials) {
        float palette[kMaxMaterialPalette * 8] = {};
        for (size_t i = 0; i < it.paletteMaterials->size(); ++i) {
            const auto& m = (*it.paletteMaterials)[i];
            float* p = palette + i * 8;
            p[0] = m.baseColor.x; p[1] = m.baseColor.y; p[2] = m.baseColor.z; p[3] = m.baseColor.w;
            p[4] = m.metallic; p[5] = m.roughness;
        }
        dev_->setUniforms(3, palette, sizeof palette);
    }
    if (it.joints >= 0) dev_->setUniforms(3, jointData_.data() + it.joints, kJointFloats * sizeof(float));
    else if (it.baked && (!lastItem_ || !lastItem_->baked || lastItem_->prim != it.prim)) {
        float pal[80] = {};
        for (size_t j = 0; j < it.prim->jointPalette.size() && j < 80; ++j) pal[j] = float(it.prim->jointPalette[j]);
        dev_->setUniforms(3, pal, sizeof pal);
    }
    if (it.morph >= 0) {
        float mo[16] = {};
        std::memcpy(mo, morphData_.data() + it.morph, 8 * sizeof(float));
        mo[8] = float(it.prim->morphTexel); mo[9] = float(it.prim->firstVertex); mo[10] = float(it.prim->vertexCount); mo[11] = float(it.prim->morphTargets);
        mo[12] = float(it.model->morphTexWidth);
        dev_->setUniforms(4, mo, sizeof mo);
    }
    dev_->setVertexBuffer(it.model->vb);
    dev_->setIndexBuffer(it.model->ib);
    if (!submit) {
        if (tracePreparation) {
            static uint32_t samples = 0;
            static double sums[4] = {};
            const auto ms = [](auto a, auto b) {
                return std::chrono::duration<double, std::milli>(b - a).count();
            };
            sums[0] += ms(prepStart, afterPipeBind);
            sums[1] += ms(afterPipeBind, afterInstanceBuild);
            sums[2] += ms(afterInstanceBuild, afterInstanceUpload);
            sums[3] += ms(afterInstanceUpload, std::chrono::steady_clock::now());
            if (++samples == 600) {
                std::fprintf(stderr,
                    "[model-prep] avg/600 pipeline=%.4f instance-build=%.4f instance-upload=%.4f material-bind=%.4f ms\n",
                    sums[0]/samples, sums[1]/samples, sums[2]/samples, sums[3]/samples);
                samples = 0;
                for (double& v : sums) v = 0.0;
            }
        }
        return true; // Bind shared state for one indirect multi-draw.
    }
    dev_->drawIndexed(it.indexCount, it.firstIndex + it.model->gpuFirstIndex,
                      int32_t(it.model->gpuBaseVertex), count);
    lastItem_ = &it;
    modelStats_.triangles += uint64_t(it.indexCount / 3) * count;
    if (count > 1) { ++modelStats_.instancedDraws; modelStats_.drawnInstances += count; }
    ++drawCalls_;
    ++profile_.draws[size_t(phase)];profile_.instances[size_t(phase)]+=count;
    profile_.triangles[size_t(phase)]+=it.indexCount/3*count;
    return true;
}

// Shared threshold for both the cheap preflight and actual indirect encoding.
// Tune with AIVERSE_METAL_MDI_MIN_COMMANDS=2..32; never let the two paths disagree.
// The 16-command default avoids Metal CPU-ICB overhead for undersized groups.
// macOS fixed-camera A/B (110 vs 68 calls) showed 8-command batches slower
// on both CPU and GPU; keep the threshold overridable for different devices.
static size_t indirectCommandThreshold() {
    static const size_t value = [] {
        const size_t fallback = 16u;
        const char* raw = std::getenv("AIVERSE_METAL_MDI_MIN_COMMANDS");
        if (!raw || !*raw) return fallback;
        char* end = nullptr;
        const long parsed = std::strtol(raw, &end, 10);
        return end != raw && *end == '\0' && parsed >= 2 && parsed <= 32
            ? size_t(parsed) : fallback;
    }();
    return value;
}

// A single command buffer submission for multiple rigid primitives that share
// ALL shader data, vertex/index buffers, transform and pipeline. The ordinary
// pipeline remains the automatic fallback on platforms lacking indirect draws.
bool Renderer3D::drawModelIndirectBatch(const uint32_t* items, uint32_t count, int phase,
                                        bool instancePalette) {
    if (count < 2 || !dev_->supportsMultiDrawIndirect()) return false;
    if (instancePalette) {
        // Shader U3 is fixed to 64 material entries. Never produce a batch
        // that would index beyond that uniform array.
        std::vector<const Material*> unique;
        unique.reserve(std::min<size_t>(count, kMaxMaterialPalette));
        for (uint32_t n = 0; n < count; ++n) {
            const Material* m = modelItems_[items[n]].mat;
            if (std::find(unique.begin(), unique.end(), m) != unique.end()) continue;
            if (unique.size() == kMaxMaterialPalette) return false;
            unique.push_back(m);
        }
    }
    // The Vulkan direct-draw path applies gpuBaseVertex as a vertex-buffer offset,
    // whereas indexed indirect applies vertexOffset to the fetched vertex index.
    // Until both paths use the same shader-visible vertex addressing convention,
    // indirect draws with nonzero base vertices would read the wrong geometry.
    // Preserve correctness by falling back to regular indexed draws for these models.
    if (dev_->info().backend == gfx::Backend::Vulkan) {
        for (uint32_t n = 0; n < count; ++n)
            if (modelItems_[items[n]].model->gpuBaseVertex != 0) return false;
    }
    const bool traceIndirect = std::getenv("AIVERSE_ICB_PROFILE") != nullptr;
    const auto indirectStart = std::chrono::steady_clock::now();
    // Exact-signature validation makes this safe across changing visibility,
    // model reloads and mixed geometry pages. Static scenes reuse layouts
    // almost every frame; avoid rebuilding the command list each time.
    // AIVERSE_MDI_LAYOUT_CACHE=off (or 0) restores the uncached path.
    const char* cacheMode = std::getenv("AIVERSE_MDI_LAYOUT_CACHE");
    const bool layoutCaching = !cacheMode ||
        (std::strcmp(cacheMode, "off") != 0 && std::strcmp(cacheMode, "0") != 0);
    auto& commands = indirectCommandScratch_;
    commands.clear();
    uint64_t triangles = 0;
    bool cacheHit = false;
    IndirectLayoutEntry* cacheEntry = nullptr;
    if (layoutCaching) {
        auto& entries = indirectLayoutCache_[size_t(phase)];
        const size_t cursor = indirectLayoutCursor_[size_t(phase)]++;
        if (cursor == entries.size()) entries.emplace_back();
        cacheEntry = &entries[cursor];
        // Compare exact geometry signatures, not hashes: stale model pointers,
        // mesh reloads and LOD changes must never reuse invalid commands.
        cacheHit = cacheEntry->signature.size() == count;
        if (cacheHit) for (uint32_t n = 0; n < count; ++n) {
            const auto& part = modelItems_[items[n]];
            const IndirectLayoutItem current{part.prim, part.indexCount, part.firstIndex,
                part.model->gpuFirstIndex, uint32_t(part.model->gpuBaseVertex)};
            if (!(cacheEntry->signature[n] == current)) {
                cacheHit = false;
                break;
            }
        }
        if (cacheHit) {
            commands.assign(cacheEntry->commands.begin(), cacheEntry->commands.end());
            triangles = cacheEntry->triangles;
        }
        if (traceIndirect) {
            static uint64_t cacheHits = 0, cacheMisses = 0;
            if (cacheHit) ++cacheHits; else ++cacheMisses;
            if (cacheHits + cacheMisses == 600)
                std::fprintf(stderr, "[mdi-layout] hits=%llu misses=%llu hit-rate=%.1f%% /600 submissions\n",
                    (unsigned long long)cacheHits, (unsigned long long)cacheMisses,
                    100.0 * double(cacheHits) / double(cacheHits + cacheMisses));
            if (cacheHits + cacheMisses == 600) cacheHits = cacheMisses = 0;
        }
    }
    if (!cacheHit) {
        if (commands.capacity() < count) commands.reserve(count);
        // Consecutive instances of one primitive form ONE indirect command.
        // firstInstance selects per-object transform data.
        for (uint32_t n = 0; n < count;) {
            const auto& part = modelItems_[items[n]];
            uint32_t end = n + 1;
            while (end < count && modelItems_[items[end]].prim == part.prim) ++end;
            commands.push_back({part.indexCount, end - n,
                                part.firstIndex + part.model->gpuFirstIndex,
                                int32_t(part.model->gpuBaseVertex), n});
            triangles += uint64_t(part.indexCount / 3) * (end - n);
            n = end;
        }
        if (cacheEntry) {
            cacheEntry->signature.clear();
            cacheEntry->signature.reserve(count);
            for (uint32_t n = 0; n < count; ++n) {
                const auto& part = modelItems_[items[n]];
                cacheEntry->signature.push_back({part.prim, part.indexCount, part.firstIndex,
                    part.model->gpuFirstIndex, uint32_t(part.model->gpuBaseVertex)});
            }
            cacheEntry->commands = commands;
            cacheEntry->triangles = triangles;
        }
    }
    // Building CPU ICB commands on Metal has a measurable setup cost.
    // Avoid switching to indirect for tiny groups where indexed draws win.
    const size_t minimumCommands = dev_->info().backend == gfx::Backend::Metal ? indirectCommandThreshold() : 4u;
    if (commands.size() < minimumCommands) return false;
    const auto afterGrouping = std::chrono::steady_clock::now();
    if (!drawModelBatch(items, count, phase, false, false, instancePalette))
        return true; // pipeline warming: no fallback draw
    const auto afterPipeline = std::chrono::steady_clock::now();
    if (!indirectCommands_.valid()) {
        gfx::BufferDesc d;
        d.type = gfx::BufferType::Indirect;
        d.usage = gfx::Usage::Dynamic;
        d.snapshotUpdates = true;
        d.size = commands.size() * sizeof(commands[0]);
        d.data = commands.data();
        d.label = "model indexed indirect commands";
        indirectCommands_ = dev_->createBuffer(d);
    } else dev_->updateBuffer(indirectCommands_, commands.data(), commands.size() * sizeof(commands[0]));
    const auto afterUpload = std::chrono::steady_clock::now();
    if (!indirectCommands_.valid() ||
        !dev_->drawIndexedIndirect(indirectCommands_, 0, uint32_t(commands.size()))) {
        for (uint32_t n = 0; n < count; ++n)
            drawModelBatch(items + n, 1, phase);
        return true;
    }
    const auto afterSubmit = std::chrono::steady_clock::now();
    if (traceIndirect) {
        static uint32_t samples = 0;
        static double sums[4] = {};
        const auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
        sums[0] += ms(indirectStart, afterGrouping);
        sums[1] += ms(afterGrouping, afterPipeline);
        sums[2] += ms(afterPipeline, afterUpload);
        sums[3] += ms(afterUpload, afterSubmit);
        if (++samples == 600) {
            std::fprintf(stderr,
                "[render-indirect] avg/600 grouping=%.4f pipeline=%.4f upload=%.4f backend=%.4f ms\n",
                sums[0]/samples, sums[1]/samples, sums[2]/samples, sums[3]/samples);
            samples = 0;
            for (double& v : sums) v = 0.0;
        }
    }
    // U2 / texture bindings come from items[0] for the whole indirect
    // submission. In per-instance palette mode the final command may use
    // different PBR factors, so pretending its state is still bound would
    // incorrectly skip the U2 upload for the next ordinary draw.
    lastItem_ = &modelItems_[items[instancePalette ? 0u : count - 1u]];
    modelStats_.triangles += triangles;
    for (const auto& c : commands) if (c.instanceCount > 1) {
        ++modelStats_.instancedDraws;
        modelStats_.drawnInstances += c.instanceCount;
    }
    ++modelStats_.multiDrawSubmissions;
    modelStats_.indirectCommands += commands.size();
    if (instancePalette) {
        ++modelStats_.mdiPaletteSubmissions;
        modelStats_.mdiPaletteCommands += commands.size();
    }
    ++drawCalls_;
    ++profile_.draws[size_t(phase)];
    profile_.instances[size_t(phase)] += count;
    profile_.triangles[size_t(phase)] += uint32_t(triangles);
    return true;
}

void Renderer3D::prepareGpuModelDraws() {
    gpuModelCommandsReady_ = false;
    gpuModelFrame_ = 0;
    gpuModelSubmission_ = 0;
    gpuModelEligible_ = gpuModelDrawn_ = gpuModelDynamicEligible_ = gpuModelLodEligible_ = 0;
    gpuEncodedDrawCalls_ = gpuEncodedCommands_ = 0;
    gpuIcbCandidates_ = gpuIcbPrepared_ = 0;
    gpuIcbRejected_ = gpuIcbExecuteFailed_ = 0;
    gpuEncodedBatches_.clear();
    gpuEncodedCovered_.clear();
    gpuAdaptiveInstancingFallback_ = false;
    gpuPreflightGroups_ = gpuBatchCacheHits_ = gpuBatchCacheMisses_ = 0;
    gpuModelSlots_.clear();
    gpuModelRecords_.clear();

    static const bool enabled = [] {
        const char* flag = std::getenv("AIVERSE_GPU_AUTHORED_DRAWS");
        return flag && std::strcmp(flag, "1") == 0;
    }();
    if (!enabled || !gpuAuthoredDrawEnabled_ || !modelMultiDrawEnabled_ || !dev_ ||
        dev_->info().backend != gfx::Backend::Metal ||
        !dev_->supportsCompute() || !dev_->frameSerial()) return;

    // The original static-only GPU route was useful for validating Metal
    // ICB but covered very few RTS objects. Rigid non-static primitives can
    // use the same code: draw records and uniforms are refreshed each frame.
    static const bool includeRigidDynamic = [] {
        const char* value = std::getenv("AIVERSE_GPU_ICB_RIGID");
        return value && std::strcmp(value, "1") == 0;
    }();
    std::vector<uint32_t> eligibleIndices;
    eligibleIndices.reserve(modelItems_.size() / 4);
    for (uint32_t i = 0; i < modelItems_.size(); ++i) {
        const ModelItem& it = modelItems_[i];
        // This path is deliberately conservative: only opaque rigid static
        // primitives with no GPU deformation, GI, fades or material batching.
        // Shadows/transparent/outline passes remain on the CPU renderer.
        if (!it.inView || !it.rigid || !it.prim || !it.model || !it.mat ||
            (!includeRigidDynamic && !(it.flags & kDrawStatic)) ||
            (it.flags & (kDrawTransparent | kDrawFadeDepth | kDrawActorReveal)) ||
            it.tint.w < .99f || it.mat->alpha != AlphaMode::Opaque ||
            it.mat->transparent || it.mat->shaderGraph ||
            it.joints >= 0 || it.morph >= 0 || it.baked ||
            it.gi != 0 || it.materialRole || it.shadowRole || it.paletteMaterials ||
            // Current GPU compute only does frustum culling; do not bypass
            // the CPU occlusion pipeline for geometry requiring its tests.
            it.geometry.occlusion ||
            it.model->geometry || it.lodThreshold < 1.0f || it.lodInvert ||
            !it.model->vb.valid() || !it.model->ib.valid() || !it.indexCount)
            continue;
        eligibleIndices.push_back(i);
        if (!(it.flags & kDrawStatic)) ++gpuModelDynamicEligible_;
        if (it.gpuLodAvailable) ++gpuModelLodEligible_;
    }
    gpuModelEligible_ = uint32_t(eligibleIndices.size());
    if (!gpuModelEligible_) return;
    static const bool gpuIcb = [] {
        const char* flag = std::getenv("AIVERSE_GPU_ENCODED_ICB");
        return flag && std::strcmp(flag, "1") == 0;
    }();
    static const bool adaptiveGpuIcb = [] {
        const char* flag = std::getenv("AIVERSE_GPU_ICB_ADAPTIVE");
        return flag && std::strcmp(flag, "1") == 0;
    }();
    static const uint32_t minGpuIcbCommands = [] {
        const char* raw = std::getenv("AIVERSE_GPU_ICB_MIN_COMMANDS");
        if (!raw || !*raw) return 2u;
        char* end = nullptr;
        const long value = std::strtol(raw, &end, 10);
        return end != raw && *end == '\0' && value >= 2 && value <= 256
            ? uint32_t(value) : 2u;
    }();
    const uint32_t groupCapacity = std::max(2u, instanceLimit_);
    const uint32_t effectiveMinimum = gpuIcbMinimumCommands(
        gpuModelEligible_, minGpuIcbCommands, adaptiveGpuIcb);
    if (adaptiveGpuIcb && gpuIcb && modelInstancingEnabled_ &&
        dev_->supportsGpuEncodedIcb()) {
        // First eliminate impossible groups using only resource handles and
        // draw flags. This avoids hashing material data when each compatible
        // geometry pool has fewer objects than the ICB threshold.
        std::map<std::tuple<uint32_t, uint32_t, uint32_t>, uint32_t> cheapCounts;
        for (uint32_t idx : eligibleIndices) {
            const auto& x = modelItems_[idx];
            auto& count = cheapCounts[{x.model->vb.id, x.model->ib.id, x.flags}];
            if (count < groupCapacity) ++count;
        }
        bool possible = false;
        for (const auto& [key, count] : cheapCounts)
            if (count >= effectiveMinimum) { possible = true; break; }
        if (!possible) {
            gpuAdaptiveInstancingFallback_ = true;
            return;
        }
        // GPU-state hashes are also only a necessary condition. Collisions
        // cause extra grouping, never unsafe rendering, because grouping
        // still calls equivalentGpuMaterial for exact compatibility.
        std::map<std::pair<const Model*, const Material*>, uint64_t> hashes;
        std::map<std::tuple<uint32_t, uint32_t, uint32_t, uint64_t>, uint32_t> counts;
        for (uint32_t i : eligibleIndices) {
            const auto& x = modelItems_[i];
            auto [it, fresh] = hashes.try_emplace(std::make_pair(x.model, x.mat), 0);
            if (fresh) it->second = gpuMaterialStateHash(*x.model, *x.mat);
            auto& count = counts[{x.model->vb.id, x.model->ib.id, x.flags, it->second}];
            if (count < groupCapacity) ++count;
        }
        for (const auto& [key, count] : counts)
            if (count >= effectiveMinimum) ++gpuPreflightGroups_;
        if (gpuIcbShouldFallbackToInstancing(adaptiveGpuIcb, gpuPreflightGroups_)) {
            gpuAdaptiveInstancingFallback_ = true;
            return;
        }
    }
    gpuModelSlots_.assign(modelItems_.size(), UINT32_MAX);
    gpuModelRecords_.reserve(eligibleIndices.size());
    for (uint32_t i : eligibleIndices) {
        const ModelItem& it = modelItems_[i];
        GpuPrimitiveDrawRecord record{};
        std::memcpy(record.world, it.world.m, sizeof(record.world));
        record.boundsMin[0] = it.prim->boundsMin.x;
        record.boundsMin[1] = it.prim->boundsMin.y;
        record.boundsMin[2] = it.prim->boundsMin.z;
        record.boundsMax[0] = it.prim->boundsMax.x;
        record.boundsMax[1] = it.prim->boundsMax.y;
        record.boundsMax[2] = it.prim->boundsMax.z;
        record.command.indexCount = it.indexCount;
        record.command.instanceCount = 1;
        record.command.firstIndex = it.firstIndex + it.model->gpuFirstIndex;
        record.command.baseVertex = int32_t(it.model->gpuBaseVertex);
        record.command.firstInstance = 0;
        if (it.gpuLodAvailable) {
            record.command = it.gpuLodNear;
            record.lodAlternate = it.gpuLodFar;
            record.lodMaxPixels = it.gpuLodMaxPixels;
            record.lodError = it.gpuLodError;
            record.lodEnabled = 1;
            // Culling and projected size must use the common base-mesh bounds,
            // not the primitive bounds of the CPU-selected LOD.
            record.boundsMin[0] = it.gpuLodBoundsMin.x;
            record.boundsMin[1] = it.gpuLodBoundsMin.y;
            record.boundsMin[2] = it.gpuLodBoundsMin.z;
            record.boundsMax[0] = it.gpuLodBoundsMax.x;
            record.boundsMax[1] = it.gpuLodBoundsMax.y;
            record.boundsMax[2] = it.gpuLodBoundsMax.z;
        }
        record.enabled = 1;
        gpuModelSlots_[i] = uint32_t(gpuModelRecords_.size());
        gpuModelRecords_.push_back(record);
    }
    auto prepareGpuCommands = [&]() {
        gpuModelFrame_ = dev_->frameSerial();
        gpuModelCommandsReady_ = gpuModelCommands_.prepare(
            *dev_, gpuModelRecords_, viewProj_, gpuModelFrame_,
            &gpuModelSubmission_, uint32_t(std::max(sceneW_, 0)),
            uint32_t(std::max(sceneH_, 0)));
        return gpuModelCommandsReady_;
    };
    // The original single-draw experiment can still be tested separately,
    // but adaptive mode must never pay its compute cost when no ICB group
    // is large enough to justify GPU execution.
    if (!gpuIcb) {
        if (!adaptiveGpuIcb) prepareGpuCommands();
        return;
    }
    // Requesting GPU ICB without the explicit safety gate falls back
    // directly to CPU instancing/MDI, not extra GPU single-draw compute.
    if (!dev_->supportsGpuEncodedIcb()) return;
    if (!modelInstancingEnabled_) return;

    // Pooling means different Model/Material objects can share the same GPU
    // vertex/index buffers and exact material state. Group by *GPU state*,
    // not pointer identity, otherwise static scene props never reach 2 draws.
    // Hash is only a prefilter: exact equivalence is required to avoid
    // mixing samplers, textures, material params, or shader variants.
    struct Group {
        uint64_t materialHash = 0;
        std::vector<uint32_t> items;
    };
    // The stable plan stores ONLY model-item indices and GPU-state signatures.
    // Transforms/instance payloads are never cached here. A new viewport,
    // reordered scene, asset reload or material mutation invalidates the plan.
    std::vector<GpuBatchPlanItem> signature;
    signature.reserve(eligibleIndices.size());
    std::map<std::pair<const Model*, const Material*>, uint64_t> cachedMaterialHashes;
    std::vector<uint64_t> hashes(modelItems_.size(), 0);
    for (uint32_t item : eligibleIndices) {
        const auto& m = modelItems_[item];
        const auto key = std::make_pair(m.model, m.mat);
        auto [cached, fresh] = cachedMaterialHashes.try_emplace(key, 0);
        if (fresh) cached->second = gpuMaterialStateHash(*m.model, *m.mat);
        const uint64_t hash = cached->second;
        hashes[item] = hash;
        signature.push_back({m.model, m.mat, item, m.flags,
            m.model->vb.id, m.model->ib.id,
            m.model->baked.tex.id, m.model->morphTex.id, hash});
    }
    std::vector<Group> groups;
    bool cacheValid = gpuBatchPlan_.signature == signature;
    if (cacheValid) {
        // Hashes are a prefilter, not a correctness guarantee. Revalidate the
        // material equivalence of every cached group before using it.
        size_t verified = 0;
        for (const auto& members : gpuBatchPlan_.groups) {
            if (members.empty()) { cacheValid = false; break; }
            const auto& first = modelItems_[members.front()];
            for (uint32_t member : members) {
                if (member >= modelItems_.size()) { cacheValid = false; break; }
                const auto& candidate = modelItems_[member];
                if (first.model->vb != candidate.model->vb ||
                    first.model->ib != candidate.model->ib ||
                    first.flags != candidate.flags ||
                    !equivalentGpuMaterial(*first.model, *first.mat,
                                           *candidate.model, *candidate.mat)) {
                    cacheValid = false;
                    break;
                }
                ++verified;
            }
            if (!cacheValid) break;
        }
        if (verified != eligibleIndices.size()) cacheValid = false;
    }
    if (cacheValid) {
        ++gpuBatchCacheHits_;
        groups.reserve(gpuBatchPlan_.groups.size());
        for (const auto& members : gpuBatchPlan_.groups)
            groups.push_back({hashes[members.front()], members});
    } else {
        ++gpuBatchCacheMisses_;
        groups.reserve(eligibleIndices.size() / 2 + 1);
        std::unordered_map<uint64_t, std::vector<size_t>> groupsByHash;
        groupsByHash.reserve(eligibleIndices.size() / 4 + 1);
        for (uint32_t item : eligibleIndices) {
            const auto& m = modelItems_[item];
            const uint64_t hash = hashes[item];
            bool found = false;
            auto& bucket = groupsByHash[hash];
            for (size_t groupIndex : bucket) {
                auto& group = groups[groupIndex];
                const auto& first = modelItems_[group.items.front()];
                if (first.model->vb != m.model->vb ||
                    first.model->ib != m.model->ib ||
                    first.model->baked.tex != m.model->baked.tex ||
                    first.model->morphTex != m.model->morphTex ||
                    first.flags != m.flags ||
                    !equivalentGpuMaterial(*first.model, *first.mat, *m.model, *m.mat))
                    continue;
                group.items.push_back(item);
                found = true;
                break;
            }
            if (!found) {
                bucket.push_back(groups.size());
                groups.push_back({hash, {item}});
            }
        }
        gpuBatchPlan_.signature = std::move(signature);
        gpuBatchPlan_.groups.clear();
        gpuBatchPlan_.groups.reserve(groups.size());
        for (const auto& group : groups)
            gpuBatchPlan_.groups.push_back(group.items);
    }
    gpuEncodedCovered_.assign(modelItems_.size(), 0);
    // GPU encoding has a compute-dispatch cost per batch, unlike CPU MDI.
    // Keep this threshold separately tunable; default 2 preserves the
    // previously validated path and permits a controlled 4/8/16 sweep.
    // Decide the entire GPU stage before uploading any draw-record buffers.
    // The previous implementation always ran compute even if adaptive policy
    // selected zero ICB groups, wasting work for the CPU fallback.
    static const bool groupAdaptive = [] {
        const char* flag = std::getenv("AIVERSE_ADAPTIVE_DRAW_GROUPS");
        return flag && std::strcmp(flag, "1") == 0;
    }();
    // Reuse a tiny CPU scratch array instead of allocating an unordered_set
    // for every batch. Stop as soon as the group reaches the ICB threshold.
    std::vector<const Primitive*> uniquePrimitives;
    uniquePrimitives.reserve(effectiveMinimum);
    auto gpuGroupRuns = [&](const std::vector<uint32_t>& indices,
                            size_t begin, size_t end) {
        uniquePrimitives.clear();
        for (size_t j = begin; j < end; ++j) {
            const Primitive* prim = modelItems_[indices[j]].prim;
            if (std::find(uniquePrimitives.begin(), uniquePrimitives.end(), prim) ==
                uniquePrimitives.end()) {
                uniquePrimitives.push_back(prim);
                if (uniquePrimitives.size() >= effectiveMinimum) break;
            }
        }
        return uint32_t(uniquePrimitives.size());
    };
    bool hasSuitableGroup = false;
    for (const auto& group : groups) {
        const size_t count = std::min(group.items.size(), size_t(groupCapacity));
        if (count < size_t(effectiveMinimum)) continue;
        if (groupAdaptive && !shouldUseGpuIcb(uint32_t(count),
                gpuGroupRuns(group.items, 0, count), effectiveMinimum)) continue;
        hasSuitableGroup = true;
        break;
    }
    if (gpuIcbShouldFallbackToInstancing(adaptiveGpuIcb, hasSuitableGroup ? 1u : 0u)) {
        gpuAdaptiveInstancingFallback_ = true;
        return;
    }
    if (!prepareGpuCommands()) return;
    for (auto& group : groups) {
        const auto& indices = group.items;
        for (size_t first = 0; first < indices.size();) {
            const size_t end = std::min(indices.size(), first + groupCapacity);
            // Do not spend a GPU compute dispatch for undersized groups.
            // Remaining items fall back to conventional instancing.
            if (end - first < effectiveMinimum) break;
            if (groupAdaptive && !shouldUseGpuIcb(uint32_t(end - first),
                    gpuGroupRuns(indices, first, end), effectiveMinimum)) {
                first = end; // leave these models for CPU instancing/MDI
                continue;
            }
            std::vector<uint32_t> sourceSlots;
            sourceSlots.reserve(end - first);
            for (size_t j = first; j < end; ++j)
                sourceSlots.push_back(gpuModelSlots_[indices[j]]);
            ++gpuIcbCandidates_;
            // One contiguous GPU Instance record per command. The Metal vertex
            // shader fetches it by baseInstance, so no per-batch U0 upload is
            // necessary when binding the GPU_INSTANCE_BUFFER permutation.
            std::vector<float> instanceRecords((end - first) * 36u, 0.0f);
            for (size_t j = first; j < end; ++j)
                packModelInstance(modelItems_[indices[j]], 1, {1, 1, 1, 1},
                                  instanceRecords.data() + (j - first) * 36u);
            const uint32_t token = dev_->prepareGpuEncodedIndexedIcb(
                gpuModelCommands_.output(gpuModelFrame_, gpuModelSubmission_),
                modelItems_[indices[first]].model->vb,
                modelItems_[indices[first]].model->ib,
                sourceSlots.data(), uint32_t(sourceSlots.size()),
                instanceRecords.data(), instanceRecords.size() * sizeof(float));
            if (token) {
                ++gpuIcbPrepared_;
                GpuEncodedBatch group;
                group.items.assign(indices.begin() + first, indices.begin() + end);
                group.token = token;
                gpuEncodedBatches_.push_back(std::move(group));
            } else ++gpuIcbRejected_;
            first = end;
        }
    }
    // A single command-encoder end creates the compute->render dependency
    // for all groups, instead of opening/closing one encoder per ICB batch.
    dev_->finishGpuEncodedIcbPreparation();
}

void Renderer3D::renderModels(int phase) {
    if (modelItems_.empty()) return;
    // A renderModels invocation is one independent visibility/ordering epoch.
    // Reset the per-phase cache cursor, but retain validated layouts across frames.
    indirectLayoutCursor_[size_t(phase)] = 0;
    const bool traceModelPass = std::getenv("AIVERSE_MODEL_BATCH_PROFILE") != nullptr;
    const auto passBegin = std::chrono::steady_clock::now();
    uint64_t mdiCandidates = 0, mdiAccepted = 0, mdiRejected = 0, mdiShort = 0;
    uint64_t submittedBatches = 0, submittedInstanced = 0, submittedItems = 0;
    double mdiScanMs = 0.0, mdiAttemptMs = 0.0, mdiAcceptedMs = 0.0, mdiRejectedMs = 0.0;
    uint64_t mdiBelowThreshold = 0;
    order_.clear();
    auto started=profiling_?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
    for (uint32_t i = 0; i < modelItems_.size(); ++i) {
        const ModelItem& it = modelItems_[i];
        const bool blended = it.mat->alpha == AlphaMode::Blend || it.tint.w < 0.99f || transparentItem(it.flags);
        bool visible = it.inView, shadow = it.inShadow;
        if (auxiliaryModelPass_ && it.rigid && modelCullingEnabled_) {
            visible = modelBoxVisible(viewProj_ * it.world, it.prim->boundsMin, it.prim->boundsMax);
            shadow = it.castShadow && modelBoxVisible(lightViewProj_ * it.world, it.prim->boundsMin, it.prim->boundsMax);
        }
        bool take = phase == 0 ? shadow : phase == 3 ? visible && it.outline.w > 0 : phase == 4 ? visible && transparentItem(it.flags) :
                    visible && (phase == 2) == blended;
        if (phase != 0 && it.tint.w <= 0.f) take = false;
        if (it.shadowRole == (phase == 0 ? 1 : 2)) take = false;   // batched parts cast through their shadow batch
        if(take&&phase==0&&!it.mat->shaderGraph&&it.joints<0&&!it.baked&&it.morph<0)
            take=geometryBoxVisible(lightViewProj_*it.world,it.prim->boundsMin,it.prim->boundsMax);
        // Reflection has its own view, never consume main-view occlusion there.
        if (phase == 1 && !geometryMainView_) take = it.shadowRole != 2 && !blended && (it.mat->shaderGraph || geometryBoxVisible(viewProj_ * it.world, it.prim->boundsMin, it.prim->boundsMax));
        // Kingston water: ground models must not cover the mirrored city.
        // This flat city puts the rainwater at y=.045. Fully submerged primitives
        // are omitted only from the reflection, retaining normal and shadow draws.
        if (take && phase == 1 && !geometryMainView_) {
            const auto center=(it.prim->boundsMin+it.prim->boundsMax)*.5f;
            const auto extent=(it.prim->boundsMax-it.prim->boundsMin)*.5f;
            const float top=it.world.transformPoint(center).y + std::abs(it.world.at(1,0))*extent.x + std::abs(it.world.at(1,1))*extent.y + std::abs(it.world.at(1,2))*extent.z;
            if(top<=.055f)take=false;
        }
        if ((it.materialRole == 1 && phase == 1) || (it.materialRole == 2 && phase != 1)) take = false;
        if (take && phase != 0 && geometryMainView_ && it.geometry.occlusion && !it.mat->shaderGraph && it.joints < 0 && !it.baked && it.morph < 0 && it.outline.w == 0 &&
            geometryOcclusion_.hidden(viewProj_ * it.world, it.prim->boundsMin, it.prim->boundsMax)) {
            if (phase == 1 || phase == 2) ++geometryStats_.occluded;
            take = false;
        }
        if (take) order_.push_back(i);
    }
    if(profiling_){auto now=std::chrono::steady_clock::now();profile_.selectMs[size_t(phase)]+=std::chrono::duration<double,std::milli>(now-started).count();started=now;}
    if (order_.empty()) return;
    const auto afterSelect = std::chrono::steady_clock::now();
    // Material identity is NOT a batching key. Editor/game code routinely
    // creates per-entity copies with identical GPU state. Each new material
    // must still pass the exact GPU-state comparison (hash is only a prefilter).
    //
    // The same immutable ModelItem material pointers are submitted to shadow,
    // opaque, reflection and outline passes within one beginScene()/render().
    // Resolve their equivalence once, then reuse group IDs across those passes.
    // This saves repeated GPU material hashes/string comparisons and map
    // allocation while retaining byte-identical grouping. Disable the cache
    // with AIVERSE_FRAME_MATERIAL_GROUP_CACHE=off for controlled A/B testing.
    static const bool frameCacheEnabled = [] {
        const char* value = std::getenv("AIVERSE_FRAME_MATERIAL_GROUP_CACHE");
        return !value || (std::strcmp(value, "off") != 0 &&
                          std::strcmp(value, "0") != 0);
    }();
    std::vector<uint32_t> localItemIds, localRepresentatives;
    std::unordered_map<MaterialStatePointer, uint32_t, MaterialStatePointerHash> localIdentities;
    std::unordered_map<uint64_t, std::vector<uint32_t>> localCandidates;
    auto& materialGroup = frameCacheEnabled ? materialItemIds_ : localItemIds;
    auto& representatives = frameCacheEnabled ? materialRepresentatives_ : localRepresentatives;
    auto& identities = frameCacheEnabled ? materialStateIds_ : localIdentities;
    auto& candidatesByHash = frameCacheEnabled ? materialHashCandidates_ : localCandidates;
    if (materialGroup.size() < modelItems_.size())
        materialGroup.resize(modelItems_.size(), UINT32_MAX);
    // retain hashtable capacity across phases and frames; reserve only
    // when the current visible set would exceed the existing bucket budget.
    const size_t identityBudget = std::min<size_t>(order_.size(), 4096);
    const size_t hashBudget = std::min<size_t>(order_.size(), 512);
    if (identities.bucket_count() < identityBudget) identities.reserve(identityBudget);
    if (candidatesByHash.bucket_count() < hashBudget) candidatesByHash.reserve(hashBudget);
    uint32_t reusedGroups = 0, resolvedGroups = 0;
    for (uint32_t i : order_) {
        if (materialGroup[i] != UINT32_MAX) {
            ++reusedGroups;
            continue; // Already resolved in an earlier pass of this scene.
        }
        const auto& item = modelItems_[i];
        const MaterialStatePointer materialKey{item.mat, item.model};
        const auto cached = identities.find(materialKey);
        if (cached != identities.end()) {
            materialGroup[i] = cached->second;
            ++reusedGroups;
            continue;
        }
        const uint64_t key = gpuMaterialStateHash(*item.model, *item.mat);
        auto& candidates = candidatesByHash[key];
        uint32_t id = uint32_t(representatives.size());
        for (uint32_t j : candidates) {
            const ModelItem& known = modelItems_[representatives[j]];
            if (equivalentGpuMaterial(*item.model, *item.mat,
                                      *known.model, *known.mat)) {
                id = j;
                break;
            }
        }
        if (id == representatives.size()) {
            representatives.push_back(i);
            candidates.push_back(id);
        }
        identities.emplace(materialKey, id);
        materialGroup[i] = id;
        ++resolvedGroups;
    }
    modelStats_.materialGroupsReused += reusedGroups;
    modelStats_.materialGroupsResolved += resolvedGroups;
    if (traceModelPass) {
        static uint64_t counts[2] = {};
        static uint32_t samples = 0;
        counts[0] += reusedGroups;
        counts[1] += resolvedGroups;
        if (++samples == 600) {
            std::fprintf(stderr,
                "[model-material-cache] enabled=%d avg/600 reused=%.1f resolved=%.1f unique-last=%zu\n",
                int(frameCacheEnabled), double(counts[0]) / samples,
                double(counts[1]) / samples, representatives.size());
            samples = 0;
            counts[0] = counts[1] = 0;
        }
    }
    // Opt-in vehicle experiment: separate node/primitive geometry stays
    // intact, while base colour + metallic + roughness are selected from
    // a shared per-command palette in the colour shader.
    // All other material state (including texture handles and emissive)
    // must still compare exactly.
    static const bool instancePaletteRequested = [] {
        const char* flag = std::getenv("AIVERSE_MDI_INSTANCE_PALETTE");
        return flag && std::strcmp(flag, "1") == 0;
    }();
    const char* paletteSafe = std::getenv("AIVERSE_RENDER_SAFE");
    const char* paletteIndirectOff = std::getenv("AIVERSE_MULTI_DRAW_INDIRECT");
    const bool paletteSort = instancePaletteRequested && phase == 1 &&
        modelMultiDrawEnabled_ && dev_->supportsMultiDrawIndirect() &&
        !gpuAdaptiveInstancingFallback_ &&
        !(paletteSafe && std::strcmp(paletteSafe, "1") == 0) &&
        !(paletteIndirectOff && std::strcmp(paletteIndirectOff, "off") == 0);
    std::vector<uint32_t> paletteGroup;
    if (paletteSort) {
        paletteGroup.resize(modelItems_.size(), UINT32_MAX);
        std::vector<uint32_t> byExactGroup(representatives.size(), UINT32_MAX);
        std::unordered_map<uint64_t, std::vector<uint32_t>> paletteCandidates;
        std::vector<uint32_t> paletteRepresentatives;
        for (uint32_t index : order_) {
            const uint32_t exact = materialGroup[index];
            uint32_t group = byExactGroup[exact];
            if (group == UINT32_MAX) {
                const ModelItem& item = modelItems_[index];
                const uint64_t hash = gpuPaletteStateHash(*item.model, *item.mat);
                auto& candidates = paletteCandidates[hash];
                group = uint32_t(paletteRepresentatives.size());
                for (uint32_t other : candidates) {
                    const ModelItem& known = modelItems_[paletteRepresentatives[other]];
                    if (equivalentGpuPaletteState(*item.model, *item.mat,
                                                  *known.model, *known.mat)) {
                        group = other;
                        break;
                    }
                }
                if (group == paletteRepresentatives.size()) {
                    paletteRepresentatives.push_back(index);
                    candidates.push_back(group);
                }
                byExactGroup[exact] = group;
            }
            paletteGroup[index] = group;
        }
    }
    // Sort by shared GPU buffers only when requested (or for the palette
    // experiment). It helps heterogeneous primitives on shared buffers.
    static const bool groupAdaptive = [] {
        const char* flag = std::getenv("AIVERSE_ADAPTIVE_DRAW_GROUPS");
        return flag && std::strcmp(flag, "1") == 0;
    }();
    const char* geometrySortSetting = std::getenv("AIVERSE_MDI_GEOMETRY_SORT");
    const bool geometrySortRequested = geometrySortSetting &&
        std::strcmp(geometrySortSetting, "1") == 0;
    const bool mdiGeometrySort = modelMultiDrawEnabled_ &&
        dev_->supportsMultiDrawIndirect() &&
        (!gpuAdaptiveInstancingFallback_ || groupAdaptive) &&
        phase <= 1 && (geometrySortRequested || paletteSort);
    if (phase == 2) { // back to front
        std::sort(order_.begin(), order_.end(), [&](uint32_t a, uint32_t b) { return modelItems_[a].depth > modelItems_[b].depth; });
    } else {          // group by state so identical primitives end up next to each other (instancing)
        std::sort(order_.begin(), order_.end(), [&](uint32_t a, uint32_t b) {
            const ModelItem &x = modelItems_[a], &y = modelItems_[b];
            if ((x.joints >= 0) != (y.joints >= 0)) return x.joints < y.joints;
            if (x.baked != y.baked) return x.baked < y.baked;
            if (paletteSort && paletteGroup[a] != paletteGroup[b])
                return paletteGroup[a] < paletteGroup[b];
            if (!paletteSort && materialGroup[a] != materialGroup[b])
                return materialGroup[a] < materialGroup[b];
            if (mdiGeometrySort) {
                if (x.model->vb.id != y.model->vb.id) return x.model->vb.id < y.model->vb.id;
                if (x.model->ib.id != y.model->ib.id) return x.model->ib.id < y.model->ib.id;
                // Keep non-MDI-eligible material variants in their own runs.
                if (x.flags != y.flags) return x.flags < y.flags;
                if (x.gi != y.gi) return x.gi < y.gi;
                if (x.lmPage != y.lmPage) return x.lmPage < y.lmPage;
            }
            // Keep identical primitives together for cheap conventional
            // instancing when an indirect group is too small.
            if (paletteSort && materialGroup[a] != materialGroup[b])
                return materialGroup[a] < materialGroup[b];
            if (x.prim != y.prim) return x.prim < y.prim;
            if (x.gi != y.gi) return x.gi < y.gi;
            if (x.lmPage != y.lmPage) return x.lmPage < y.lmPage;
            return a < b;
        });
    }
    if(profiling_)profile_.sortMs[size_t(phase)]+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    const auto afterSort = std::chrono::steady_clock::now();
    ProfileTimer timer(profiling_?&profile_.drawMs[size_t(phase)]:nullptr);
    currentPipe_ = {};
    lastItem_ = nullptr;
    auto batchable = [&](const ModelItem& x) { return modelInstancingEnabled_ && x.joints < 0 && x.morph < 0; };
    auto sameBatch = [&](uint32_t xIndex, uint32_t yIndex) {
        const ModelItem& x = modelItems_[xIndex];
        const ModelItem& y = modelItems_[yIndex];
        if((x.lodThreshold<1 || x.lodInvert)!=(y.lodThreshold<1 || y.lodInvert))return false;
        const bool bx = x.mat->alpha == AlphaMode::Blend || x.tint.w < 0.99f, by = y.mat->alpha == AlphaMode::Blend || y.tint.w < 0.99f;
        return x.paletteMaterials == y.paletteMaterials && x.prim == y.prim &&
               materialGroup[xIndex] == materialGroup[yIndex] && x.baked == y.baked && bx == by && x.gi == y.gi && x.lmPage == y.lmPage &&
               (x.flags & (kDrawFadeDepth | kDrawTransparent | kDrawActorReveal)) == (y.flags & (kDrawFadeDepth | kDrawTransparent | kDrawActorReveal));
    };
    // Invariant across all model batches in this pass. Avoid environment
    // queries and backend capability dispatch for every small group.
    const char* renderSafe = std::getenv("AIVERSE_RENDER_SAFE");
    const char* indirectToggle = std::getenv("AIVERSE_MULTI_DRAW_INDIRECT");
    // When the adaptive GPU plan cannot amortize ICB setup, choose ordinary
    // instancing for this frame (including shadow/opaque model submissions),
    // not the more expensive CPU MDI path. Existing A/B modes remain distinct.

    const bool allowIndirect = modelMultiDrawEnabled_ &&
        (!gpuAdaptiveInstancingFallback_ || groupAdaptive) &&
        !(renderSafe && std::strcmp(renderSafe, "1") == 0) &&
        !(indirectToggle && std::strcmp(indirectToggle, "off") == 0) &&
        dev_->supportsMultiDrawIndirect() && phase <= 1;
    const size_t minIndirectCommands = dev_->info().backend == gfx::Backend::Metal ? indirectCommandThreshold() : 4u;
    if (traceModelPass) {
        // candidates=0 is otherwise ambiguous: it could mean no GPU MDI
        // support, an explicit safety toggle, or an adaptive ICB fallback.
        // Report the gating conditions rather than treating a sorting A/B
        // comparison as evidence of improved multi-draw submission.
        static unsigned gateSamples[5] = {};
        if (++gateSamples[size_t(phase)] == 120) {
            std::fprintf(stderr,
                "[model-mdi-gate] phase=%d allowed=%d supported=%d enabled=%d adaptive-fallback=%d render-safe=%d indirect-off=%d debug-drawcalls=%d geometry-sort=%d min-commands=%zu order=%zu\n",
                phase, int(allowIndirect), int(dev_->supportsMultiDrawIndirect()),
                int(modelMultiDrawEnabled_), int(gpuAdaptiveInstancingFallback_),
                int(renderSafe && std::strcmp(renderSafe, "1") == 0),
                int(indirectToggle && std::strcmp(indirectToggle, "off") == 0),
                int(drawCallDebug_), int(mdiGeometrySort), minIndirectCommands, order_.size());
            gateSamples[size_t(phase)] = 0;
        }
    }
    if (phase == 1 && geometryMainView_ && !auxiliaryModelPass_ &&
        !drawCallDebug_ && gpuModelCommandsReady_) {
        for (auto& group : gpuEncodedBatches_) {
            if (group.items.size() < 2) continue;
            // Force all inherited parent state to be bound from this group.
            // Previous direct draws may have used transient setBytes blocks.
            currentPipe_ = {};
            lastItem_ = nullptr;
            dev_->setGpuIcbUniformBinding(true);
            const bool submitted = drawModelBatch(
                group.items.data(), uint32_t(group.items.size()), phase,
                false, true);
            const bool executed = submitted &&
                dev_->executeGpuEncodedIndexedIcb(group.token);
            dev_->setGpuIcbUniformBinding(false);
            if (!executed) {
                ++gpuIcbExecuteFailed_;
                continue;
            }
            lastItem_ = &modelItems_[group.items.back()];
            ++gpuEncodedDrawCalls_;
            gpuEncodedCommands_ += uint32_t(group.items.size());
            ++drawCalls_;
            ++profile_.draws[size_t(phase)];
            profile_.instances[size_t(phase)] += uint32_t(group.items.size());
            for (uint32_t idx : group.items) {
                gpuEncodedCovered_[idx] = 1;
                const auto& item = modelItems_[idx];
                const uint32_t triangles = item.indexCount / 3;
                modelStats_.triangles += triangles;
                profile_.triangles[size_t(phase)] += triangles;
            }
        }
    }
    for (size_t i = 0; i < order_.size();) {
        if (phase == 1 && !gpuEncodedCovered_.empty() &&
            gpuEncodedCovered_[order_[i]]) { ++i; continue; }
        // Keep one-draw-per-primitive GPU-authoring limited to the legacy
        // static path. Dynamic rigid models only use GPU ICB in batches of 2+;
        // otherwise conventional instancing remains cheaper and preserves
        // the original fallback behavior.
        const uint32_t modelIndex = order_[i];
        // Adaptive GPU ICB is a whole-path decision: never replace
        // undersized ICB groups with GPU single draws, otherwise the supposedly
        // disabled GPU path still incurs compute+per-draw API overhead.
        static const bool adaptiveGpuIcbFallback = [] {
            const char* flag = std::getenv("AIVERSE_GPU_ICB_ADAPTIVE");
            return flag && std::strcmp(flag, "1") == 0;
        }();
        if (!adaptiveGpuIcbFallback &&
            phase == 1 && modelMultiDrawEnabled_ && geometryMainView_ &&
            !auxiliaryModelPass_ && gpuModelCommandsReady_ && !drawCallDebug_ &&
            modelIndex < gpuModelSlots_.size() &&
            gpuModelSlots_[modelIndex] != UINT32_MAX &&
            (modelItems_[modelIndex].flags & kDrawStatic) &&
            drawModelBatch(&modelIndex, 1, phase, false)) {
            if (dev_->drawIndexedGpuIndirect(
                    gpuModelCommands_.output(gpuModelFrame_, gpuModelSubmission_),
                    size_t(gpuModelSlots_[modelIndex]) * sizeof(gfx::DrawIndexedIndirectCommand))) {
                const ModelItem& item = modelItems_[modelIndex];
                lastItem_ = &item;
                ++gpuModelDrawn_;
                ++drawCalls_;
                ++profile_.draws[size_t(phase)];
                ++profile_.instances[size_t(phase)];
                const uint32_t triangles = item.indexCount / 3;
                profile_.triangles[size_t(phase)] += triangles;
                modelStats_.triangles += triangles;
                ++i;
                continue;
            }
        }
        // Try MDI before conventional instancing, only when every command can
        // legally inherit the SAME uniforms/textures and rigid transform.
        // Draw-call colour mode uses one colour per indirect submission.
        if (allowIndirect && order_.size() - i >= minIndirectCommands) {
            ++modelStats_.mdiScans;
            const auto scanBegin = std::chrono::steady_clock::now();
            ++mdiCandidates;
            const uint32_t firstIndex = order_[i];
            const auto& first = modelItems_[firstIndex];
            const bool firstEligible = !first.model->geometry &&
                first.gi == 0 && !first.materialRole && !first.shadowRole &&
                !first.baked && first.joints < 0 && first.morph < 0 &&
                !first.paletteMaterials && first.tint.w >= .99f;
            auto compatible = [&](uint32_t candidateIndex) {
                const auto& y = modelItems_[candidateIndex];
                // Each indirect command selects its own world/tint/team/etc
                // through firstInstance into the shared instance uniform array.
                // Pipeline, geometry buffers, textures and material uniforms
                // must remain compatible across the entire command group.
                return first.model->vb == y.model->vb &&
                    first.model->ib == y.model->ib &&
                    !first.model->geometry && !y.model->geometry &&
                    first.gi == 0 && y.gi == 0 &&
                    !first.materialRole && !y.materialRole &&
                    !first.shadowRole && !y.shadowRole &&
                    !first.baked && !y.baked &&
                    first.joints < 0 && y.joints < 0 &&
                    first.morph < 0 && y.morph < 0 &&
                    !first.paletteMaterials && !y.paletteMaterials &&
                    (first.lodThreshold < 1 || first.lodInvert) ==
                       (y.lodThreshold < 1 || y.lodInvert) &&
                    first.flags == y.flags &&
                    first.tint.w >= .99f && y.tint.w >= .99f &&
                    (paletteSort ? paletteGroup[firstIndex] == paletteGroup[candidateIndex]
                                 : materialGroup[firstIndex] == materialGroup[candidateIndex]);
            };
            // Do not reorder translucent parts or geometry-cluster selections.
            if (firstEligible &&
                (phase == 0 || (first.mat->alpha == AlphaMode::Opaque &&
                                !transparentItem(first.flags)))) {
                size_t end = i + 1;
                while (end < order_.size() && end - i < size_t(instanceLimit_)) {
                    const uint32_t candidate = order_[end];
                    if (!compatible(candidate)) {
                        const auto& y = modelItems_[candidate];
                        if (first.model->vb != y.model->vb ||
                            first.model->ib != y.model->ib)
                            ++modelStats_.mdiGeometryBreaks;
                        else if (paletteSort
                            ? paletteGroup[firstIndex] != paletteGroup[candidate]
                            : materialGroup[firstIndex] != materialGroup[candidate])
                            ++modelStats_.mdiMaterialBreaks;
                        else ++modelStats_.mdiStateBreaks;
                        break;
                    }
                    ++end;
                }
                if (traceModelPass)
                    mdiScanMs += std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - scanBegin).count();
                if (end - i >= 2) {
                    // Count consecutive primitive runs before allocating a command
                    // vector or preparing a pipeline. Repeated instances of one
                    // primitive are only ONE indirect command.
                    size_t runs = 1;
                    for (size_t k = i + 1; k < end && runs < minIndirectCommands; ++k)
                        if (modelItems_[order_[k - 1]].prim != modelItems_[order_[k]].prim)
                            ++runs;
                    // With group-adaptive selection, reject MDI groups
                    // consisting mostly of repeated identical primitives.
                    // Instancing then avoids ICB setup and GPU command work.
                    if (runs >= minIndirectCommands &&
                        (!groupAdaptive || shouldUseCpuMultiDraw(
                            uint32_t(end - i), uint32_t(runs),
                            uint32_t(minIndirectCommands)))) {
                        const auto attemptBegin = std::chrono::steady_clock::now();
                        const bool accepted = drawModelIndirectBatch(
                            order_.data() + i, uint32_t(end - i), phase, paletteSort);
                        if (traceModelPass) {
                            const double elapsed = std::chrono::duration<double, std::milli>(
                                std::chrono::steady_clock::now() - attemptBegin).count();
                            mdiAttemptMs += elapsed;
                            if (accepted) mdiAcceptedMs += elapsed;
                            else mdiRejectedMs += elapsed;
                        }
                        if (accepted) {
                            ++mdiAccepted;
                            ++submittedBatches;
                            ++submittedInstanced;
                            submittedItems += end - i;
                            i = end; continue;
                        }
                        ++mdiRejected;
                        ++modelStats_.mdiFailed;
                    } else {
                        ++mdiBelowThreshold;
                        ++modelStats_.mdiBelowThreshold;
                    }
                } else {
                    ++mdiShort;
                    ++modelStats_.mdiShort;
                }
            } else {
                ++modelStats_.mdiIneligible;
                // Exclusive attribution for the first rejected candidate
                // in this run. These count scan attempts, not unique models.
                if (first.model->geometry) ++modelStats_.mdiIneligibleGeometry;
                else if (first.gi != 0 || first.baked) ++modelStats_.mdiIneligibleLighting;
                else if (first.materialRole || first.shadowRole || first.paletteMaterials)
                    ++modelStats_.mdiIneligibleMaterial;
                else if (first.joints >= 0 || first.morph >= 0)
                    ++modelStats_.mdiIneligibleDeform;
                else ++modelStats_.mdiIneligibleAlpha;
            }
        }
        size_t j = i + 1;
        if (batchable(modelItems_[order_[i]]))
            while (j < order_.size() && j - i < size_t(instanceLimit_) && batchable(modelItems_[order_[j]]) &&
                   sameBatch(order_[i], order_[j])) ++j;
        drawGeometryBatch(order_.data() + i, uint32_t(j - i), phase);
        ++submittedBatches;
        submittedInstanced += j - i > 1 ? 1u : 0u;
        submittedItems += j - i;
        i = j;
    }
    if (traceModelPass) {
        struct Totals {
            uint32_t passes = 0;
            double select = 0, sort = 0, draw = 0, scan = 0, attempt = 0, acceptedMs = 0, rejectedMs = 0;
            uint64_t candidates = 0, accepted = 0, rejected = 0, shortGroups = 0, belowThreshold = 0;
        };
        static Totals totals[2][5] = {};
        struct SubmissionTotals {
            uint64_t batches = 0, instanced = 0, items = 0;
        };
        static SubmissionTotals submissions[2][5] = {};
        const int mode = modelMultiDrawEnabled_ ? 1 : 0;
        auto& t = totals[mode][phase];
        auto& st = submissions[mode][phase];
        st.batches += submittedBatches;
        st.instanced += submittedInstanced;
        st.items += submittedItems;
        const auto ms = [](auto a, auto b) {
            return std::chrono::duration<double, std::milli>(b - a).count();
        };
        t.select += ms(passBegin, afterSelect);
        t.sort += ms(afterSelect, afterSort);
        t.draw += ms(afterSort, std::chrono::steady_clock::now());
        t.scan += mdiScanMs;
        t.attempt += mdiAttemptMs;
        t.acceptedMs += mdiAcceptedMs;
        t.rejectedMs += mdiRejectedMs;
        t.belowThreshold += mdiBelowThreshold;
        t.candidates += mdiCandidates;
        t.accepted += mdiAccepted;
        t.rejected += mdiRejected;
        t.shortGroups += mdiShort;
        if (++t.passes == 120) {
            std::fprintf(stderr,
                "[model-batch] mode=%s phase=%d avg/120 select=%.4f sort=%.4f draw=%.4f scan=%.4f attempt=%.4f acceptedMs=%.4f rejectedMs=%.4f ms candidates=%.1f accepted=%.1f rejected=%.1f belowThreshold=%.1f short=%.1f /pass\n",
                mode ? "ON" : "OFF", phase,
                t.select / t.passes, t.sort / t.passes, t.draw / t.passes,
                t.scan / t.passes, t.attempt / t.passes,
                t.acceptedMs / t.passes, t.rejectedMs / t.passes,
                double(t.candidates) / t.passes, double(t.accepted) / t.passes,
                double(t.rejected) / t.passes, double(t.belowThreshold) / t.passes,
                double(t.shortGroups) / t.passes);
            std::fprintf(stderr,
                "[model-batch-submissions] mode=%s phase=%d avg/120 batches=%.1f instanced=%.1f items=%.1f avg-items-per-batch=%.2f\n",
                mode ? "ON" : "OFF", phase,
                double(st.batches) / t.passes, double(st.instanced) / t.passes,
                double(st.items) / t.passes,
                st.batches ? double(st.items) / double(st.batches) : 0.0);
            st = {};
            t = {};
        }
    }
}

void Renderer3D::prepareOcclusion() {
    ProfileTimer timer(profiling_?&profile_.occlusionMs:nullptr);
    geometryOcclusion_.clear(dev_->info().clipDepthZeroToOne);
    uint32_t budget = 8192;
    for (const auto& it : modelItems_) {
        if (!budget) break;
        if (actorReveal.w > 0 && (it.flags & kDrawActorReveal)) continue; // cutaway cannot occlude the actor on CPU
        if (!it.inView || !it.geometry.occluder || !it.model->geometry || it.joints >= 0 || it.baked || it.morph >= 0 ||
            it.mat->alpha != AlphaMode::Opaque || it.mat->transparent || it.mat->shaderGraph ||
            it.tint.w < 1 || it.dissolve > 0 || transparentItem(it.flags)) continue;
        const auto clip=viewProj_*it.world;
        for (const auto& t : it.model->geometry->occluders[it.geometryPrimitive]) {
            geometryOcclusion_.triangle(clip, t, it.mat->doubleSided);
            ++geometryStats_.occluderTriangles;
            if (!--budget) break;
        }
    }
}
void Renderer3D::drawGeometryBatch(const uint32_t* items, uint32_t count, int phase) {
    auto& it = modelItems_[items[0]];
    const bool shadow=phase==0;
    const bool selected=shadow?it.geometry.shadowClusters:(phase==1&&geometryMainView_&&it.geometry.clusters);
    const bool eligible = count == 1 && selected && it.indexCount >= 6144 && !it.mat->shaderGraph && it.model->geometry &&
        it.joints < 0 && !it.baked && it.morph < 0 && it.outline.w == 0 && it.dissolve == 0;
    if (!eligible) { drawModelBatch(items,count,phase); return; }
    const auto& clusters = it.model->geometry->primitives[it.geometryPrimitive];
    if (clusters.empty() || clusters.size() > 8192 || geometryStats_.clustersTested + clusters.size() > 16384) { drawModelBatch(items,count,phase); return; }
    const mat4 clip = (shadow?lightViewProj_:viewProj_) * it.world;
    const auto first = it.firstIndex, indices = it.indexCount;
    auto& runs=geometryRuns_;runs.clear();
    uint32_t culled = 0, reduced = 0;
    for (const auto& c : clusters) {
        ++geometryStats_.clustersTested;
        if (!geometryBoxVisible(clip,c.min,c.max) || (!shadow && it.geometry.occlusion && geometryOcclusion_.hidden(clip,c.min,c.max))) {
            ++culled; continue;
        }
        // The occlusion buffer contains source triangles, so an occluder must render exact geometry.
        const bool coarse = (!it.geometry.occluder||shadow) && (!shadow||it.mat->alpha==AlphaMode::Opaque) && c.coarseCount <= c.indexCount &&
            geometryErrorPixels(clip,c.min,c.max,c.error,shadow?geometryShadowSize_:sceneW_,shadow?geometryShadowSize_:sceneH_,dev_->info().clipDepthZeroToOne) <= (shadow?it.geometry.shadowErrorPixels:it.geometry.errorPixels);
        if (coarse&&c.coarseCount<c.indexCount) ++reduced;
        const auto begin = coarse ? c.coarseFirst : c.firstIndex, length = coarse ? c.coarseCount : c.indexCount;
        if (!runs.empty() && runs.back().first + runs.back().second == begin) runs.back().second += length;
        else runs.push_back({begin,length});
    }
    // CPU-driven splits must save enough vertex work to pay for additional draws.
    uint64_t selectedIndices=0;for(auto run:runs)selectedIndices+=run.second;
    const uint64_t extraDrawCost=runs.empty()?0:uint64_t(runs.size()-1)*12288;
    if (runs.size()>4 || (!culled&&!reduced) || (!runs.empty()&&selectedIndices+extraDrawCost>=indices)) { drawModelBatch(items,count,phase); return; }
    geometryStats_.clustersCulled += culled; geometryStats_.coarseClusters += reduced;
    for (auto [begin,length] : runs) { it.firstIndex = begin; it.indexCount = length; drawModelBatch(items,count,phase); }
    it.firstIndex = first; it.indexCount = indices;
}
} // namespace aiverse::render
