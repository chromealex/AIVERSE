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
#include "StaticModelCachePolicy.h"
#include "StaticCrossModelBatchPolicy.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <chrono>
#include <unordered_map>
#include <tuple>
#include <functional>
#include <limits>

namespace aiverse::render {

namespace {
struct MaterialUniforms { float base[4]; float emissive[4]; float pbr[4]; float misc[4]; float uv0[4]; float uv1[4]; float light[4]; float reveal[4]; float revealAxis[4]; };
constexpr size_t kJointFloats = size_t(kMaxPaletteJoints) * 12; // 3 rows of vec4 per joint
static_assert(kJointFloats * 4 <= 4096, "joint palette must fit a 4 KB uniform block");

constexpr uint32_t kKeyDrawCallView = 32768;
constexpr uint32_t kKeyMaterialInstancePalette = 1u << 27;
constexpr uint32_t kKeyLumaMaterial=1u<<28,kKeyLumaSurface=1u<<30; // editor draw-call diagnostic shader permutation
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

std::array<double, 8> cpuMdiStageValues(
    const ModelRenderStats::CpuMdiStageProfile& p) {
    return {p.totalMs, p.selectionMs, p.groupingSortMs, p.candidateScanMs,
            p.commandPackingMs, p.pipelineSetupMs, p.bufferUploadMs,
            p.backendSubmitMs};
}

} // namespace

void Renderer3D::recordCpuMdiStageSample(
    size_t phase, const ModelRenderStats::CpuMdiStageProfile& before) {
    if (phase >= cpuMdiRolling_.size() || !profiling_) return;
    auto& history = cpuMdiRolling_[phase];
    const auto start = cpuMdiStageValues(before);
    const auto finish = cpuMdiStageValues(modelStats_.mdiCpuStages[phase]);
    auto& target = history.samples[history.next];
    for (size_t i = 0; i < target.size(); ++i)
        target[i] = std::max(0.0, finish[i] - start[i]);
    history.next = (history.next + 1) % history.samples.size();
    history.count = std::min(history.count + 1, history.samples.size());
}

Renderer3D::CpuMdiRollingSummary Renderer3D::cpuMdiRollingSummary(
    size_t phase) const {
    CpuMdiRollingSummary result{};
    if (phase >= cpuMdiRolling_.size()) return result;
    const auto& history = cpuMdiRolling_[phase];
    result.samples = history.count;
    if (!result.samples) return result;
    // Only the read-only UI/query path sorts a fixed stack scratch array.
    // Nothing allocates, sorts or scans the ring while rendering a pass.
    std::array<double, kCpuMdiRollingWindow> sorted{};
    const size_t rank = ((95 * result.samples + 99) / 100) - 1; // nearest-rank P95
    const size_t medianRank = ((50 * result.samples + 99) / 100) - 1; // nearest-rank P50
    for (size_t metric = 0; metric < result.meanMs.size(); ++metric) {
        double sum = 0.0;
        for (size_t i = 0; i < result.samples; ++i) {
            const double value = history.samples[i][metric];
            sum += value;
            sorted[i] = value;
        }
        std::sort(sorted.begin(), sorted.begin() + result.samples);
        result.meanMs[metric] = sum / double(result.samples);
        result.p50Ms[metric] = sorted[medianRank];
        result.p95Ms[metric] = sorted[rank];
    }
    // Variance diagnosis runs on the same explicit summary/UI read as
    // Stage 31's bounded sorting, never in the renderer hot loop. A high
    // P95 only indicates intermittent CPU latency, not a regression.
    result.hasBaseline = result.samples >= kCpuMdiSpikeMinSamples;
    if (result.hasBaseline) {
        for (size_t metric = 0; metric < result.spike.size(); ++metric) {
            const double excess = result.p95Ms[metric] - result.meanMs[metric];
            result.spike[metric] =
                result.p95Ms[metric] >= kCpuMdiSpikeMinP95Ms &&
                excess >= kCpuMdiSpikeMinExcessMs &&
                result.p95Ms[metric] >=
                    result.meanMs[metric] * kCpuMdiSpikeP95ToMeanRatio;
            if (metric > 0 && result.spike[metric]) {
                ++result.highlightedStages;
                if (excess > result.dominantExcessMs) {
                    result.dominantStage = int(metric);
                    result.dominantExcessMs = excess;
                }
            }
            // Independently identify stages that are consistently costly:
            // P50 protects against isolated outliers lifting the mean.
            // Index 0 is a whole-pass total and never attributed as a
            // specific CPU MDI stage. Stage time share is only diagnostic;
            // individual stages can overlap and must not be summed.
            if (metric > 0 && result.meanMs[0] > 0.0) {
                const double share = result.meanMs[metric] / result.meanMs[0];
                result.sustained[metric] =
                    result.p50Ms[metric] >= kCpuMdiSustainedMinMedianMs &&
                    share >= kCpuMdiSustainedMinMeanShare;
                if (result.sustained[metric]) {
                    ++result.sustainedStages;
                    if (result.p50Ms[metric] > result.dominantSustainedP50Ms) {
                        result.dominantSustainedStage = int(metric);
                        result.dominantSustainedP50Ms = result.p50Ms[metric];
                        result.dominantSustainedShare = share;
                    }
                }
            }
        }
    }
    return result;
}

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


void Renderer3D::clearIndirectLayoutCaches() {
    // Call between render passes, never while drawModelIndirectBatch borrows
    // entry.commands for its synchronous GPU upload/submission.
    for (size_t phase = 0; phase < 5; ++phase) {
        indirectLayoutsRetired_ += indirectLayoutCache_[phase].size();
        std::vector<IndirectLayoutEntry>().swap(indirectLayoutCache_[phase]);
        indirectLayoutCursor_[phase] = 0;
    }
}

void Renderer3D::retireIndirectLayoutsFor(const Model* model) {
    if (!model) return;
    // Compare borrowed pointer identity only: never inspect an unloaded Model.
    // A mixed-model indirect layout must be retired if ANY source unloads.
    for (size_t phase = 0; phase < 5; ++phase) {
        auto& entries = indirectLayoutCache_[phase];
        const size_t before = entries.size();
        entries.erase(std::remove_if(entries.begin(), entries.end(),
            [model](const IndirectLayoutEntry& entry) {
                return std::any_of(entry.signature.begin(), entry.signature.end(),
                    [model](const IndirectLayoutItem& item) {
                        return item.model == model;
                    });
            }), entries.end());
        indirectLayoutsRetired_ += before - entries.size();
        if (entries.size() != before) {
            indirectLayoutCursor_[phase] = 0;
            if (entries.size() * 2 < entries.capacity())
                entries.shrink_to_fit();
        }
    }
}

void Renderer3D::maintainIndirectLayoutCaches() {
    // No renderModels call is needed to age out a scene's old layouts:
    // empty scenes and changed viewports also release inactive entries.
    if (++indirectLayoutSceneEpoch_ == 0) {
        clearIndirectLayoutCaches();
        indirectLayoutSceneEpoch_ = 1;
    }
    if (indirectLayoutSceneEpoch_ <= kMdiLayoutIdleScenes) return;
    const uint64_t cutoff = indirectLayoutSceneEpoch_ - kMdiLayoutIdleScenes;
    for (size_t phase = 0; phase < 5; ++phase) {
        auto& entries = indirectLayoutCache_[phase];
        const size_t before = entries.size();
        entries.erase(std::remove_if(entries.begin(), entries.end(),
            [cutoff](const IndirectLayoutEntry& entry) {
                return entry.lastUsedScene < cutoff;
            }), entries.end());
        indirectLayoutsRetired_ += before - entries.size();
        if (entries.size() != before) {
            indirectLayoutCursor_[phase] = 0;
            if (entries.size() * 2 < entries.capacity())
                entries.shrink_to_fit();
        }
    }
}

Renderer3D::IndirectLayoutCacheStats Renderer3D::indirectLayoutCacheStats() const {
    IndirectLayoutCacheStats stats{};
    stats.retired = indirectLayoutsRetired_;
    for (const auto& entries : indirectLayoutCache_) {
        stats.entries += entries.size();
        stats.retainedBytes += entries.capacity() * sizeof(IndirectLayoutEntry);
        for (const auto& entry : entries) {
            stats.retainedBytes += entry.signature.capacity() * sizeof(IndirectLayoutItem);
            stats.retainedBytes += entry.commands.capacity() *
                sizeof(gfx::DrawIndexedIndirectCommand);
        }
    }
    return stats;
}

void Renderer3D::shutdownModels() {
    clearCpuMdiRollingHistory();
    clearIndirectLayoutCaches();
    std::vector<gfx::DrawIndexedIndirectCommand>().swap(indirectCommandScratch_);
    if (indirectCommands_.valid()) dev_->destroyBuffer(indirectCommands_);
    indirectCommands_ = {};
    modelError_.clear();
    for (auto& [k, p] : modelPipes_) dev_->destroyPipeline(p);
    for (auto& [k, s] : modelShaders_) if (s.shader.valid()) dev_->destroyShader(s.shader);
    pendingModelPipes_.clear(); pendingColorWarms_.clear(); failedColorWarms_.clear();
    modelPipes_.clear(); modelShaders_.clear();
    for (auto t : {white_, black_, flatNormal_})
        if(t.valid()) dev_->destroyTexture(t);
    white_=black_=flatNormal_={};
}

void Renderer3D::retireModelPrograms() {
    if(!dev_)return;
    checkGpuDevice(*dev_);
    if(!programRetirement_)
        throw std::logic_error("Renderer3D shader retirement owner unavailable");
    // Reserve BEFORE detaching any renderer pipeline/shader handles. An
    // allocation failure leaves the previous material cache usable.
    GpuProgramBatch resources;
    resources.pipelines.reserve(modelPipes_.size());
    resources.shaders.reserve(modelShaders_.size());
    for(const auto& [key,pipeline]:modelPipes_) {
        (void)key;
        if(pipeline.valid())resources.pipelines.push_back(pipeline);
    }
    for(const auto& [key,shader]:modelShaders_) {
        (void)key;
        if(shader.shader.valid())resources.shaders.push_back(shader.shader);
    }
    // Pending native compiler jobs own their own cancellation semantics.
    // They are never mistaken for completed shader handles.
    pendingModelPipes_.clear();
    pendingColorWarms_.clear(); // invalidated pipeline handles must not be polled
    failedColorWarms_.clear(); // failures belong to this retired pipeline generation
    modelPipes_.clear();
    modelShaders_.clear();
    programRetirement_->retire(std::move(resources));
}

void Renderer3D::setInstanceLimit(uint32_t limit) {
    const auto maximum=dev_?uint32_t(std::clamp<size_t>(dev_->maxUniformBytes()/144,1,256)):32u;
    const auto next=limit?std::clamp(limit,1u,maximum):maximum;
    if(next==instanceLimit_)return;
    if(dev_)retireModelPrograms();
    instanceLimit_=next;
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
    if(q==matQuality_)return;
    if(dev_)retireModelPrograms();
    matQuality_=q;
}

gfx::PipelineHandle Renderer3D::modelPipeline(const ModelItem& it, int phase, bool instanced,
                                              bool gpuInstances, bool instancePalette) {
    const bool shadow = phase == 0, outline = phase == 3, depthAfter = phase == 4;
    const bool maskPass = outline && selectionMaskPass_;
    const bool skinned = it.joints >= 0, baked = it.baked, morph = it.morph >= 0;
    const bool materialPalette = phase == 1 && (it.paletteMaterials || instancePalette);
    const bool lodFade = it.lodThreshold<1 || it.lodInvert;
    // The screen-space selection pass must honour cutout materials; the
    // legacy inflated-hull pass intentionally remains a solid silhouette.
    const bool mask = (!outline || maskPass) && it.mat->alpha == AlphaMode::Mask;
    const bool normalMap = phase != 0 && !outline && it.mat->normalTex >= 0 && matQuality_ != MaterialQuality::Low;
    const bool blend = (outline && !maskPass) || (!outline && !shadow && !depthAfter &&
        (it.mat->alpha == AlphaMode::Blend || it.tint.w < 0.99f || transparentItem(it.flags)));
    const bool twoSided = !outline && it.mat->doubleSided;
    const bool giPhase = phase == 1 || phase == 2;
    const bool surfaceCapture=lumaSurfaceCapture_ && phase==1;
    const bool materialCapture=(lumaMaterialCapture_||surfaceCapture) && phase==1;
    const bool lightmap = giPhase && !materialCapture && it.gi == 1;
    const bool probes = giPhase && !materialCapture && it.gi == 2;
    const uint32_t gpuInstanceShaderBit = gpuInstances ? (1u << 26) : 0u;
    const uint32_t studioShaderBits = (!materialCapture && geometryMainView_ && (phase == 1 || phase == 2) && studioDebugMode_ >= 2)
                                    ? uint32_t(studioDebugMode_) << 17 : 0u;
    const uint32_t shaderKey = gpuInstanceShaderBit | studioShaderBits |
                               (materialCapture?kKeyLumaMaterial:0u) |
                               (surfaceCapture?kKeyLumaSurface:0u) |
                               (instancePalette ? kKeyMaterialInstancePalette : 0u) | (skinned ? kKeySkinned : 0u) | (normalMap ? kKeyNormalMap : 0u) | (mask ? kKeyAlphaMask : 0u) |
                               (shadow ? kKeyShadow : 0u) | (baked ? kKeyBaked : 0u) | (morph ? kKeyMorph : 0u) |
                               (instanced ? kKeyInstanced : 0u) | (outline ? kKeyOutline : 0u) |
                               ((drawCallDebug_ && geometryMainView_ && (phase == 1 || phase == 2)) ? kKeyDrawCallView : 0u) |
                               (lightmap ? kKeyLightmap : 0u) | (probes ? kKeyProbes : 0u) | (lodFade ? kKeyLodFade : 0u) | (materialPalette ? kKeyMaterialPalette : 0u);
    const bool fadeDepth = blend && !outline && it.mat->alpha != AlphaMode::Blend && (it.flags & kDrawFadeDepth) && !transparentItem(it.flags);
    const uint32_t pipeKey = shaderKey | (blend ? 16u : 0u) | (twoSided ? 32u : 0u) |
        (fadeDepth ? 64u : 0u) | (depthAfter ? 1024u : 0u) | (maskPass ? (1u << 29) : 0u);
    const auto selectedGraph=it.mat->shaderGraph && int(matQuality_)>=it.mat->shaderGraph->minQuality?it.mat->shaderGraph:nullptr;
    const auto* graph = selectedGraph.get();
    const ModelShaderKey skey{shaderKey, selectedGraph};
    const ModelShaderKey pkey{pipeKey, selectedGraph};
    for (auto& [k, p] : modelPipes_) if (k == pkey) {
        if(warmingPipelines_ && !p.valid())warmingFailed_=true;
        return p;
    }

    for (auto it=pendingModelPipes_.begin();it!=pendingModelPipes_.end();++it) if(it->first==pkey) {
        gfx::PipelineHandle result;
        if (!it->second(result)) { warmingPending_=true; return {}; }
        pendingModelPipes_.erase(it);
        if (!result.valid()) {
            modelError_="model pipeline: "+dev_->lastError();
            if(warmingPipelines_)warmingFailed_=true;
        }
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
        if(materialCapture && !surfaceCapture)defs+="#define LUMAWEAVE_MATERIAL 1\n";
        if(surfaceCapture)defs+="#define LUMAWEAVE_SURFACE 1\n";
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
            if(warmingPipelines_)warmingFailed_=true;
            modelError_ = "model shader (" + defs + "): " + dev_->lastError();
            std::fprintf(stderr, "%s\n", modelError_.c_str());
        }
        modelShaders_.push_back({skey, e});
        entry = &modelShaders_.back().second;
    }
    if (entry->job) {
        if (!entry->job(entry->shader)) { warmingPending_=true; return {}; }
        entry->job={};
        if (!entry->shader.valid()) {
            entry->failed=true;
            if(warmingPipelines_)warmingFailed_=true;
            modelError_="model shader: "+dev_->lastError();
        }
    }
    if(warmingPipelines_ && entry->failed)warmingFailed_=true;
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
        pd.cull = maskPass ? (it.mat->doubleSided ? gfx::CullMode::None : gfx::CullMode::Back) :
                  outline ? gfx::CullMode::Front : twoSided ? gfx::CullMode::None : gfx::CullMode::Back;
        if (maskPass) pd.depthWrite = false;
        if (shadow) {
            pd.depthOnly = true;
            pd.depthBiasConstant = 2.0f; pd.depthBiasSlope = 2.5f;
        } else if (blend) {
            pd.blend = gfx::BlendMode::Alpha;
            pd.depthWrite = fadeDepth;
        } else if (depthAfter) {
            pd.colorWrite = false;
        }
        pd.label = shadow ? "model shadow" : surfaceCapture ? "LumaWeave normal/roughness" :
                   materialCapture ? "LumaWeave material/metallic" :
                   outline ? "model outline" : depthAfter ? "model depth" : "model";
        if (warmingPipelines_) {
            auto job=dev_->createPipelineAsync(pd);
            if (!job(ph)) { pendingModelPipes_.push_back({pkey,std::move(job)}); warmingPending_=true; return {}; }
        } else ph = dev_->createPipeline(pd);
        if (!ph.valid()) {
            modelError_ = "model pipeline: " + dev_->lastError();
            if(warmingPipelines_)warmingFailed_=true;
        }
    }
    modelPipes_.push_back({pkey, ph});
    return ph;
}

Renderer3D::PipelineWarmStatus
Renderer3D::warmModelPipelinesChecked(
        const Model& model,const std::vector<Material>* materials) {
    if(dev_)checkGpuDevice(*dev_);
    warmingPipelines_=true;
    // Shader/pipeline jobs or user-authored graph generation may throw;
    // never let an exception leak the temporary warming mode into the
    // regular frame renderer.
    struct WarmingScope {
        bool& enabled;
        ~WarmingScope(){enabled=false;}
    } scope{warmingPipelines_};
    warmingPending_=warmingFailed_=false;
    auto warm=[&](const ModelItem& item,int phase,bool instanced) {
        const auto pipe=modelPipeline(item,phase,instanced);
        if(!pipe.valid())return;
        // A pending colour variant has no backend ShaderJob to poll.
        // Record its stable handle exactly once; all later owner frames
        // advance it without regenerating the authored scratch Model.
        if(std::find(failedColorWarms_.begin(),failedColorWarms_.end(),
                     pipe)!=failedColorWarms_.end()) {
            warmingFailed_=true;
        }else if(std::find(pendingColorWarms_.begin(),pendingColorWarms_.end(),
                            pipe)!=pendingColorWarms_.end()) {
            warmingPending_=true;
        }else {
            switch(dev_->warmPipelineColorStatus(pipe,gfx::PixelFormat::RGBA16F)) {
            case gfx::Device::PipelineColorWarmStatus::Ready:
                break;
            case gfx::Device::PipelineColorWarmStatus::Pending:
                warmingPending_=true;
                pendingColorWarms_.push_back(pipe);
                break;
            case gfx::Device::PipelineColorWarmStatus::Failed:
                warmingFailed_=true;
                failedColorWarms_.push_back(pipe);
                ++colorWarmFailed_;
                modelError_="model HDR pipeline: "+dev_->lastError();
                break;
            }
        }
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
    return warmingFailed_ ? PipelineWarmStatus::Failed :
           warmingPending_ ? PipelineWarmStatus::Pending :
                             PipelineWarmStatus::Ready;
}

bool Renderer3D::warmModelPipelines(
        const Model& model,const std::vector<Material>* materials) {
    return warmModelPipelinesChecked(model,materials)==PipelineWarmStatus::Ready;
}

Renderer3D::GpuWarmStats Renderer3D::gpuWarmStats() const noexcept {
    GpuWarmStats out;
    out.pipelineJobs=pendingModelPipes_.size();
    out.colorVariants=pendingColorWarms_.size();
    out.colorPollAttempts=colorWarmPollAttempts_;
    out.colorVariantsReady=colorWarmReady_;
    out.colorVariantsFailed=colorWarmFailed_;
    for(const auto& [key,shader]:modelShaders_) {
        (void)key;
        if(shader.job)++out.shaderJobs;
    }
    return out;
}

bool Renderer3D::hasPendingModelPipelineJobs() const noexcept {
    if(!pendingModelPipes_.empty() || !pendingColorWarms_.empty())
        return true;
    for(const auto& [key,entry]:modelShaders_) {
        (void)key;
        if(entry.job)return true;
    }
    return false;
}

bool Renderer3D::pollPendingModelPipelineJobs() {
    if(!dev_)return false;
    checkGpuDevice(*dev_);
    // Do not allocate or upload a Model, texture, or material merely to
    // advance GPU compiler jobs that have already been submitted.
    for(auto it=pendingColorWarms_.begin();it!=pendingColorWarms_.end();) {
        ++colorWarmPollAttempts_;
        switch(dev_->warmPipelineColorStatus(*it,gfx::PixelFormat::RGBA16F)) {
        case gfx::Device::PipelineColorWarmStatus::Ready:
            ++colorWarmReady_;
            it=pendingColorWarms_.erase(it);
            break;
        case gfx::Device::PipelineColorWarmStatus::Failed:
            failedColorWarms_.push_back(*it);
            ++colorWarmFailed_;
            modelError_="model HDR pipeline: "+dev_->lastError();
            it=pendingColorWarms_.erase(it);
            break;
        case gfx::Device::PipelineColorWarmStatus::Pending:
            ++it;
            break;
        }
    }
    for(auto& [key,entry]:modelShaders_) {
        (void)key;
        if(!entry.job)continue;
        if(!entry.job(entry.shader))continue;
        entry.job={};
        if(!entry.shader.valid()) {
            entry.failed=true;
            modelError_="model shader: "+dev_->lastError();
        }
    }
    // Reserve room for all completed programs before polling a backend job:
    // if vector allocation fails, the job is still in its original slot.
    modelPipes_.reserve(modelPipes_.size()+pendingModelPipes_.size());
    for(auto it=pendingModelPipes_.begin();it!=pendingModelPipes_.end();) {
        gfx::PipelineHandle pipeline{};
        if(!it->second(pipeline)) {++it;continue;}
        if(!pipeline.valid())
            modelError_="model pipeline: "+dev_->lastError();
        modelPipes_.push_back({it->first,pipeline});
        it=pendingModelPipes_.erase(it);
    }
    return hasPendingModelPipelineJobs();
}

void Renderer3D::recordPersistentModel(const ModelDraw& d) {
    if (!persistentSceneEnabled_ || !d.persistentId || !d.model) return;
    GpuSceneRecord record{};
    std::memcpy(record.world, d.world.m, sizeof(record.world));
    const auto& lo = d.model->boundsMin;
    const auto& hi = d.model->boundsMax;
    const float offset=std::isfinite(d.maxVertexOffsetMetres) ?
        std::clamp(d.maxVertexOffsetMetres,0.0f,10.0f) : 0.0f;
    record.boundsMin[0] = lo.x-offset; record.boundsMin[1] = lo.y-offset; record.boundsMin[2] = lo.z-offset;
    record.boundsMax[0] = hi.x+offset; record.boundsMax[1] = hi.y+offset; record.boundsMax[2] = hi.z+offset;
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

const Renderer3D::SharedMaterialEntry* Renderer3D::cachedSharedMaterialState(
    const Model& model, const ModelDraw& draw,
    const std::vector<Material>& materials) {
    if (!draw.materialOwnerKey || !draw.materialOwnerRevision ||
        !draw.staticPacketRevision || !draw.persistentId ||
        !(draw.flags & kDrawStatic) || materials.empty())
        return nullptr;
    // Large unique/streamed material sets use exact per-packet validation.
    // Cap owner count and the actual retained signature payload separately.
    constexpr size_t kMaxMaterialEntries = 4096;
    constexpr size_t kMaxMaterialBytes = 8u * 1024u * 1024u;
    constexpr size_t kMaxMaterialsPerOwner = 512;
    if (materials.size() > kMaxMaterialsPerOwner) return nullptr;
    const MaterialOwnerKey key{&model, draw.materialOwnerKey};
    auto it = staticMaterialOwners_.find(key);
    if (it != staticMaterialOwners_.end()) {
        SharedMaterialEntry& state = it->second;
        if (state.ownerRevision == draw.materialOwnerRevision &&
            state.modelMaterialRevision == model.materialRevision &&
            state.geometryRevision == model.geometryRevision &&
            state.count == materials.size()) {
            state.seen = staticMaterialOwnerEpoch_;
            ++sharedMaterialStateStats_.hits;
            return &state;
        }
    }
    ++sharedMaterialStateStats_.misses;
    const size_t newBytes = materials.size() * sizeof(uint64_t);
    const size_t replacedBytes = it == staticMaterialOwners_.end()
        ? 0 : it->second.hashes.size() * sizeof(uint64_t);
    if ((it == staticMaterialOwners_.end() &&
         staticMaterialOwners_.size() >= kMaxMaterialEntries) ||
        newBytes > kMaxMaterialBytes ||
        newBytes > kMaxMaterialBytes - (staticMaterialOwnerBytes_ - replacedBytes))
        return nullptr;
    // Every new owner revision receives a different serial, so packets
    // referencing an earlier signature can never accidentally hot-replay.
    // The cache is effectively bounded far below the 64-bit wrap interval.
    SharedMaterialEntry& state = staticMaterialOwners_[key];
    state.hashes = staticPacketMaterialHashes(model, materials);
    state.minVertexOffsetQuality = std::numeric_limits<int>::max();
    for (const Material& material : materials)
        if (material.shaderGraph &&
            material.shaderGraph->outputs.count("vertexOffset"))
            state.minVertexOffsetQuality = std::min(
                state.minVertexOffsetQuality, int(material.shaderGraph->minQuality));
    state.ownerRevision = draw.materialOwnerRevision;
    state.modelMaterialRevision = model.materialRevision;
    state.geometryRevision = model.geometryRevision;
    state.count = materials.size();
    state.seen = staticMaterialOwnerEpoch_;
    // A wrap can only occur after 2^64 signature changes; treat it as
    // a global packet invalidation rather than reusing an old serial.
    if (++staticMaterialOwnerSerial_ == 0) {
        staticRenderPackets_.clear();
        staticRenderPacketBytes_ = 0;
        staticMaterialOwnerSerial_ = 1;
    }
    state.serial = staticMaterialOwnerSerial_;
    staticMaterialOwnerBytes_ = staticMaterialOwnerBytes_ - replacedBytes + newBytes;
    return &state;
}

Renderer3D::CpuMdiPageEntry* Renderer3D::cpuMdiPageEntry(const Model& model) {
    const auto page = model.geometryPoolPage;
    if (!page || !model.vb.valid() || !model.ib.valid()) return nullptr;
    const CpuMdiPageKey key{
        page.get(), model.vb.id, model.ib.id, cpuMdiAdaptiveScope_};
    auto it = cpuMdiPages_.find(key);
    if (it != cpuMdiPages_.end()) {
        // An allocator can recycle the raw pointer and even GPU handle IDs.
        // A weak reference to the actual shared arena detects that case.
        const auto retained = it->second.lifetime.lock();
        if (retained == page) {
            ++cpuMdiPageStats_.reused;
            it->second.seen = cpuMdiPageEpoch_;
            return &it->second;
        }
        cpuMdiPages_.erase(it);
        ++cpuMdiPageStats_.evicted;
    }
    constexpr size_t kMaxTrackedPages = 256;
    if (cpuMdiPages_.size() >= kMaxTrackedPages) {
        // No tracked feedback means "allow original CPU MDI", not an unsafe
        // inferred decision or a new unbounded allocation.
        ++cpuMdiPageStats_.untracked;
        return nullptr;
    }
    CpuMdiPageEntry entry{};
    entry.lifetime = page;
    entry.seen = cpuMdiPageEpoch_;
    ++cpuMdiPageStats_.created;
    return &cpuMdiPages_.emplace(key, std::move(entry)).first->second;
}

ModelStaticEligibility Renderer3D::cachedStaticModelEligibility(const Model& source) {
    // Model* is a lookup key, not proof of identity or unchanged content.
    // Revision, GPU allocation, public cardinalities and geometry attachment
    // are checked on every hit; asset owners must invalidate before destruction.
    auto it = staticModelEligibility_.find(&source);
    if (it != staticModelEligibility_.end()) {
        const auto& entry = it->second;
        if (entry.geometryRevision == source.geometryRevision &&
            entry.vb == source.vb.id && entry.ib == source.ib.id &&
            entry.nodes == source.nodes.size() &&
            entry.meshes == source.meshes.size() &&
            entry.bindNodes == source.bindWorld.size() &&
            entry.skins == source.skins.size() &&
            entry.materials == source.materials.size() &&
            entry.geometry == source.geometry.get()) {
            it->second.seen = staticEligibilityEpoch_;
            ++staticEligibilityStats_.hits;
            return entry.eligibility;
        }
    }
    ++staticEligibilityStats_.misses;
    const ModelStaticEligibility result = classifyModelStaticEligibility(source);
    // Retain the hottest assets without unbounded growth from procedural or
    // streamed one-shot models. At capacity, unmatched models are evaluated
    // directly until the next epoch sweep or an explicit asset invalidation.
    constexpr size_t kMaximumModelEntries = 8192;
    if (it != staticModelEligibility_.end() ||
        staticModelEligibility_.size() < kMaximumModelEntries) {
        StaticEligibilityEntry entry{};
        entry.eligibility = result;
        entry.geometryRevision = source.geometryRevision;
        entry.seen = staticEligibilityEpoch_;
        entry.vb = source.vb.id;
        entry.ib = source.ib.id;
        entry.nodes = source.nodes.size();
        entry.meshes = source.meshes.size();
        entry.bindNodes = source.bindWorld.size();
        entry.skins = source.skins.size();
        entry.materials = source.materials.size();
        entry.geometry = source.geometry.get();
        staticModelEligibility_[&source] = entry;
    }
    return result;
}

void Renderer3D::drawModel(const ModelDraw& d) {
    ProfileTimer timer(profiling_?&profile_.submitMs:nullptr);
    const Model* m = d.model;
    if (!m || !m->vb.valid() || m->nodes.empty()) return;
    recordPersistentModel(d);
    if (capture_ && (d.flags & kDrawStatic) && d.primitiveMaterialFilter < 0) {   // light baker: the static scene, every node in world space
        const auto& pose = d.nodeWorld && d.nodeWorld->size() == m->nodes.size() ? *d.nodeWorld : m->bindWorld;
        std::vector<mat4> nodeWorld(pose.size());
        for (size_t i = 0; i < pose.size(); ++i) nodeWorld[i] = d.world * pose[i];
        capture_(d, nodeWorld);
    }
    const auto& graphMaterials = d.materials && d.materials->size() >= m->materials.size()
        ? *d.materials : m->materials;
    // This lookup is entirely optional. Without explicit owner identity and
    // owner revision, the original full material fingerprint path is used.
    const SharedMaterialEntry* sharedMaterials =
        staticRenderPacketsEnabled_ && d.model && d.persistentId
            ? cachedSharedMaterialState(*m, d, graphMaterials) : nullptr;
    const bool activeDisplacement = sharedMaterials
        ? int(matQuality_) >= sharedMaterials->minVertexOffsetQuality
        : std::any_of(
            graphMaterials.begin(), graphMaterials.end(), [this](const Material& mat) {
                return mat.shaderGraph && int(matQuality_) >= mat.shaderGraph->minQuality &&
                       mat.shaderGraph->outputs.count("vertexOffset");
            });
    // Only the geometry-revision-dependent asset traits are memoized.
    // Current draw flags, pose and displacement remain live per instance.
    ModelStaticEligibility eligibility{};
    if ((staticModelCacheEnabled_ || staticRenderPacketsEnabled_) &&
        d.persistentId && (d.flags & kDrawStatic))
        eligibility = cachedStaticModelEligibility(*m);
    const StaticModelCache* cache = d.staticCache;
    if (cache && !staticModelCacheGeometryCompatible(*cache, d, activeDisplacement))
        cache = nullptr;
    if (!cache && staticModelCacheEnabled_) {
        if (staticModelCacheDrawCandidate(d, activeDisplacement, eligibility)) {
            auto& entry = staticModelCaches_[d.persistentId];
            const bool unchanged =
                entry.vertexBuffer == m->vb.id &&
                entry.indexBuffer == m->ib.id &&
                entry.geometryRevision == m->geometryRevision &&
                staticModelCacheGeometryCompatible(entry.cache, d, false);
            if (!unchanged) {
                entry.cache.prepare(*m, d.world);
                entry.vertexBuffer = m->vb.id;
                entry.indexBuffer = m->ib.id;
                entry.geometryRevision = m->geometryRevision;
                ++staticModelCacheStats_.rebuilds;
            } else {
                ++staticModelCacheStats_.hits;
            }
            entry.seen = staticModelCacheEpoch_;
            cache = &entry.cache;
        } else {
            ++staticModelCacheStats_.declined;
        }
    }
    if (cache) ++profile_.cachedModels;
    // Whole-model culling continues to use the current camera / shadow frusta.
    float3 c = (m->boundsMin + m->boundsMax) * 0.5f,
           e = (m->boundsMax - m->boundsMin) * 0.75f + float3{0.1f, 0.1f, 0.1f};
    const bool displaced = cache ? cache->displaced : activeDisplacement;
    // Bounded vertex graphs retain ordinary node frustum tests and LOD.
    // Unknown vertexOffset graphs remain conservatively uncullable.
    const float offset=(displaced && std::isfinite(d.maxVertexOffsetMetres) &&
                        d.maxVertexOffsetMetres>0.0f &&
                        d.maxVertexOffsetMetres<=10.0f) ?
                        d.maxVertexOffsetMetres : 0.0f;
    const bool unboundedDisplacement=displaced && offset==0.0f;
    const float3 margin{offset,offset,offset};
    const bool inView = unboundedDisplacement || (cache?modelBoxVisible(viewProj_,cache->min,cache->max):modelBoxVisible(viewProj_ * d.world, c - e - margin, c + e + margin));
    const bool inShadow = quality_ != ShadowQuality::Off && !(d.flags & kDrawNoCastShadow) && (d.tint.w >= 0.99f || (d.flags & kDrawKeepShadow)) && d.dissolve <= 0 &&
                          (unboundedDisplacement || (cache?modelBoxVisible(lightViewProj_,cache->min,cache->max):modelBoxVisible(lightViewProj_ * d.world, c - e - margin, c + e + margin)));
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
    // Revision-aware immutable CPU packet replay. Material copies are
    // checked by resolved GPU state and remapped by index, never by stale
    // vector element pointers. Camera, sun shadow, LOD and depth remain live.
    static const bool gpuPacketLod = [] {
        const char* flag = std::getenv("AIVERSE_GPU_ICB_LOD");
        return flag && std::strcmp(flag, "1") == 0;
    }();
    const bool packetEligible = staticRenderPacketsEnabled_ && !gpuPacketLod &&
        staticRenderPacketDrawCandidate(d, activeDisplacement, bakedActive(),
                                        lodDiagnosticsEnabled_, eligibility);
    const size_t packetStart = modelItems_.size();
    std::vector<int> packetSelectedMeshes;
    bool packetHasFade = false;
    // The selected-mesh vector is needed only if a packet is missing or
    // invalidated. Never allocate it on a successful hot replay.
    if (packetEligible) {
        auto found = staticRenderPackets_.find(d.persistentId);
        if (found != staticRenderPackets_.end()) {
            StaticPacketEntry& packet = found->second;
            const bool sameSource = packet.geometryRevision == m->geometryRevision &&
                packet.materialRevision == m->materialRevision &&
                packet.vertexBuffer == m->vb.id && packet.indexBuffer == m->ib.id &&
                packet.selectedMeshes.size() == m->nodes.size() &&
                packet.hasNodeMaterialOverride == (d.nodeMaterial != nullptr) &&
                (!d.nodeMaterial || packet.nodeMaterialSnapshot == *d.nodeMaterial) &&
                equivalentStaticRenderPacketDraw(packet.draw, d) &&
                (sharedMaterials
                    ? packet.sharedMaterialSerial == sharedMaterials->serial
                    : (packet.sharedMaterialSerial == 0 &&
                       staticPacketMaterialStateMatches(
                           *m, mats, packet.materialsSnapshot, packet.materialHashes))) &&
                std::all_of(packet.items.begin(), packet.items.end(),
                    [m, &mats](const ModelItem& item) {
                        return item.model == m && item.prim &&
                            item.sourceNode >= 0 &&
                            size_t(item.sourceNode) < m->nodes.size() &&
                            item.materialIndex >= 0 &&
                            size_t(item.materialIndex) < mats.size();
                    });
            if (sameSource) {
                // Reuse a renderer-owned buffer instead of allocating on
                // each cache hit across thousands of static entities.
                auto& nodeStates = staticPacketNodeScratch_;
                nodeStates.resize(m->nodes.size());
                bool sameLods = true;
                const bool opaque = d.tint.w >= .99f && d.dissolve <= 0 &&
                    !transparentItem(d.flags) &&
                    std::none_of(mats.begin(), mats.end(), [](const Material& mat) {
                        return mat.alpha == AlphaMode::Blend || mat.transparent;
                    });
                const float fadeWidth = opaque && !m->geometry &&
                    matQuality_ != MaterialQuality::Low ? modelLodFadeWidth_ : 0.0f;
                for (int n : m->order) {
                    const Node& node = m->nodes[size_t(n)];
                    if (node.mesh < 0) continue;
                    const MeshDef& mesh = m->meshes[size_t(node.mesh)];
                    const mat4 xf = cache ? cache->transforms[size_t(n)]
                                          : d.world * world[size_t(n)];
                    auto& st = nodeStates[size_t(n)];
                    st.inView = inView;
                    st.inShadow = inShadow;
                    if (modelCullingEnabled_) {
                        st.inView = inView && modelBoxVisible(viewProj_ * xf,
                                                              mesh.boundsMin, mesh.boundsMax);
                        st.inShadow = inShadow && modelBoxVisible(lightViewProj_ * xf,
                                                                   mesh.boundsMin, mesh.boundsMax);
                    }
                    ModelLodBlend lod{node.mesh};
                    // Explicit variant/node-material selection disables normal
                    // CPU LOD selection in the baseline model draw path too.
                    if (modelLodEnabled_ && d.variant < 0 && !d.nodeMaterial &&
                        !mesh.lods.empty()) {
                        const float error = length(mesh.boundsMax - mesh.boundsMin);
                        const bool z01 = dev_->info().clipDepthZeroToOne;
                        const float cameraPixels = st.inView
                            ? geometryErrorPixels(viewProj_ * xf, mesh.boundsMin,
                                                  mesh.boundsMax, error,
                                                  sceneW_, sceneH_, z01) : 0.0f;
                        float pixels = cameraPixels;
                        if (st.inShadow)
                            pixels = std::max(pixels, geometryErrorPixels(
                                lightViewProj_ * xf, mesh.boundsMin, mesh.boundsMax,
                                error, shadowSize_, shadowSize_, z01));
                        if (!st.inView && !st.inShadow)
                            pixels = std::numeric_limits<float>::infinity();
                        lod = selectModelLod(node.mesh, mesh.lods, pixels, fadeWidth);
                    }
                    st.mesh = lod.mesh;
                    st.level = lod.level;
                    st.transitioning = lod.incoming >= 0;
                    if (st.transitioning ||
                        st.mesh != packet.selectedMeshes[size_t(n)]) {
                        sameLods = false;
                        break;
                    }
                }
                if (sameLods) {
                    packet.seen = staticRenderPacketEpoch_;
                    ++staticRenderPacketStats_.hits;
                    size_t itemIndex = 0;
                    for (int n : m->order) {
                        const Node& node = m->nodes[size_t(n)];
                        if (node.mesh < 0) continue;
                        const auto& st = nodeStates[size_t(n)];
                        ++modelStats_.nodes;
                        ++modelStats_.lodNodes[std::min<size_t>(st.level, 3)];
                        if (st.mesh != node.mesh) ++profile_.lodNodes;
                        if (!st.inView && !st.inShadow && modelCullingEnabled_)
                            ++modelStats_.culledNodes;
                        while (itemIndex < packet.items.size() &&
                               packet.items[itemIndex].sourceNode == n) {
                            ModelItem item = packet.items[itemIndex++];
                            // Index validation is completed before replay,
                            // so no individual source draw can disappear here.
                            item.mat = &mats[size_t(item.materialIndex)];
                            item.paletteMaterials = item.materialRole == 2 ? &mats : nullptr;
                            item.inView = item.shadowRole != 2 && st.inView;
                            item.inShadow = st.inShadow && item.castShadow;
                            const float3 center = item.world.transformPoint(
                                (item.prim->boundsMin + item.prim->boundsMax) * 0.5f);
                            item.depth = cam_.ortho
                                ? dot(center - camPos_, normalize(cam_.target - camPos_))
                                : length(center - camPos_);
                            modelItems_.push_back(std::move(item));
                        }
                    }
                    return;
                }
            }
        }
    } else if (staticRenderPacketsEnabled_) {
        ++staticRenderPacketStats_.declined;
    }
    if (packetEligible)
        packetSelectedMeshes.assign(m->nodes.size(), -1);
    for (int n : m->order) {
        const Node& node = m->nodes[size_t(n)];
        if (node.mesh < 0) continue;
        ++modelStats_.nodes;
        const MeshDef& baseMesh = m->meshes[size_t(node.mesh)];
        const bool rigid = node.skin < 0 && !baseMesh.deformable && !unboundedDisplacement;
        const mat4 nodeWorld = cache ? cache->transforms[size_t(n)] : d.world * world[size_t(n)];
        bool nodeView = inView, nodeShadow = inShadow;
        if (rigid && modelCullingEnabled_) {
            nodeView = inView && modelBoxVisible(viewProj_ * nodeWorld, baseMesh.boundsMin-margin, baseMesh.boundsMax+margin);
            nodeShadow = inShadow && modelBoxVisible(lightViewProj_ * nodeWorld, baseMesh.boundsMin-margin, baseMesh.boundsMax+margin);
            if (!nodeView && !nodeShadow) ++modelStats_.culledNodes; // retained for later mirror/spot frusta
        }
        int meshIndex = node.mesh; size_t level = 0;
        ModelLodBlend lodBlend{node.mesh};
        // Palette poses provide conservative current skin bounds. Baked-only draws keep
        // LOD0 until they supply pose bounds; bind bounds can underestimate a bent limb.
        const bool skinLod = node.skin >= 0 && !unboundedDisplacement && usePose && !canBake && d.nodeWorld && d.nodeWorld->size() == m->nodes.size();
        const bool lodEligible = (rigid || skinLod) && !baseMesh.lods.empty() &&
                                 d.variant < 0 && !d.nodeMaterial;
        if (modelLodEnabled_ && lodEligible) {
            mat4 lodWorld = nodeWorld;
            float3 lodMin = baseMesh.boundsMin-margin, lodMax = baseMesh.boundsMax+margin;
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
            const float cameraPixels = nodeView
                ? geometryErrorPixels(viewProj_ * lodWorld, lodMin, lodMax, error,
                                       sceneW_, sceneH_, zeroToOne) : 0.0f;
            float pixels = cameraPixels;
            if (nodeShadow) pixels = std::max(pixels, geometryErrorPixels(
                lightViewProj_ * lodWorld, lodMin, lodMax, error,
                shadowSize_, shadowSize_, zeroToOne));
            if (!nodeView && !nodeShadow) pixels = std::numeric_limits<float>::infinity(); // full detail for later mirror/spot frusta
            const bool opaque = d.tint.w>=.99f && d.dissolve<=0 && !transparentItem(d.flags) &&
                std::none_of(mats.begin(),mats.end(),[](const Material& mat){return mat.alpha==AlphaMode::Blend || mat.transparent;});
            const float fadeWidth = opaque && !m->geometry && matQuality_!=MaterialQuality::Low ? modelLodFadeWidth_ : 0;
            lodBlend=selectModelLod(node.mesh,baseMesh.lods,pixels,fadeWidth);
            meshIndex=lodBlend.mesh;level=lodBlend.level;
            // This extra camera-only LOD selection is a diagnostic, not
            // part of rendering. Avoid doing it on every frame when the
            // LOD inspector is closed, especially in dense cities.
            if (lodDiagnosticsEnabled_ && nodeView && nodeShadow && pixels > cameraPixels &&
                selectModelLod(node.mesh, baseMesh.lods, cameraPixels, fadeWidth).level > level) {
                ++modelStats_.cameraShadowLimitedLod;
                if (lodDebugTarget_ && d.persistentId == lodDebugTarget_)
                    ++lodDebugSample_.shadowLimited;
            }
            if(lodBlend.incoming>=0)++profile_.lodTransitionNodes;
            if (meshIndex != node.mesh) ++profile_.lodNodes;
        }
        if (packetEligible) {
            packetSelectedMeshes[size_t(n)] = meshIndex;
            packetHasFade |= lodBlend.incoming >= 0;
        }
        ++modelStats_.lodNodes[std::min<size_t>(level, 3)];
        if (lodDiagnosticsEnabled_ && nodeView) {
            ++modelStats_.cameraLodNodes[std::min<size_t>(level, 3)];
            if (baseMesh.lods.empty()) ++modelStats_.cameraNoLodChains;
            else {
                ++modelStats_.cameraLodChains;
                if (!lodEligible) ++modelStats_.cameraLodBlocked;
            }
            // Editor only: the selected map entity provides a stable ID from
            // SceneObject::persistentKey, so we can inspect its actual LOD
            // without storing per-entity traces or changing draw grouping.
            if (lodDebugTarget_ && d.persistentId == lodDebugTarget_) {
                lodDebugSample_.matched = true;
                ++lodDebugSample_.visibleNodes;
                ++lodDebugSample_.levels[std::min<size_t>(level, 3)];
                if (baseMesh.lods.empty()) ++lodDebugSample_.withoutChain;
                else ++lodDebugSample_.withChain;
            }
        }
        // Shadow batch (mergeStaticPrimitives): one shadow draw covers the opaque parts of the mesh.
        // Material copies are common in game views; only shadow-incompatible overrides disable batching.
        for(int part=0;part<(lodBlend.incoming>=0?2:1);++part) {
        meshIndex=part?lodBlend.incoming:lodBlend.mesh;
        const MeshDef& drawMesh = m->meshes[size_t(meshIndex)];
        const bool shadowBatch = d.primitiveMaterialFilter<0 && !m->geometry && !d.nodeMaterial && d.variant < 0 && shadowBatchCompatible(drawMesh, mats);
        std::vector<const MaterialBatch*> colourBatches;
        if (d.primitiveMaterialFilter<0 && !m->geometry && !d.nodeMaterial && d.variant < 0 && d.tint.w >= .99f && !transparentItem(d.flags))
            for (const auto& batch : drawMesh.materialBatches)
                if (materialBatchCompatible(drawMesh, batch, mats)) colourBatches.push_back(&batch);
        auto addItem = [&](const Primitive& p, uint8_t shadowRole, uint8_t materialRole = 0) {
            if (p.indexCount == 0 ||
                (d.primitiveMaterialFilter >= 0 &&
                 p.material != d.primitiveMaterialFilter)) return;
            int mi = p.material;
            if (d.variant >= 0 && d.variant < int(p.variantMaterial.size()) && p.variantMaterial[size_t(d.variant)] >= 0) mi = p.variantMaterial[size_t(d.variant)];
            if (d.nodeMaterial && size_t(n) < d.nodeMaterial->size() && (*d.nodeMaterial)[size_t(n)] >= 0 && &mats == d.materials) mi = (*d.nodeMaterial)[size_t(n)];
            ModelItem it;
            it.sourceNode = n;
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
            const int materialIndex = std::clamp(mi, 0, int(mats.size()) - 1);
            it.model = m; it.prim = &p; it.materialIndex = materialIndex;
            it.mat = &mats[size_t(materialIndex)];
            it.tint = d.tint; it.team = d.team; it.flash = d.flash; it.flags = d.flags | (mats[size_t(std::clamp(mi, 0, int(mats.size()) - 1))].transparent ? uint32_t(kDrawTransparent) : 0u);
            it.dissolve = d.dissolve; it.teamGlow = d.teamGlow; it.outline = d.outline;
            it.rigid = rigid;
            it.motionPersistentId = d.persistentId;
            it.motionNodeIndex = n;
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
            // Rigid material ShaderGraph vertices can use the already
            // allocated U0 Inst.anim vec4, which is otherwise reserved for
            // baked skin clips or baked lightmap UVs. Never overwrite those.
            if(d.useVertexParameters && it.joints<0 && !it.baked &&
               it.morph<0 && it.gi!=1 && it.mat->shaderGraph)
                it.anim=d.vertexParameters;
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
    if (packetEligible) {
        // Crossfade blends depend continuously on camera pixels, so never
        // replay a stored packet across a transition. Frame falls back to
        // ordinary drawModel even if the last stable LOD had a packet.
        constexpr size_t kMaxPacketItems = 128;
        constexpr size_t kMaxPacketBytes = 64u * 1024u * 1024u;
        const size_t count = modelItems_.size() - packetStart;
        const size_t newBytes = count * sizeof(ModelItem) +
            (sharedMaterials ? 0 : mats.size() * (sizeof(Material) + sizeof(uint64_t))) +
            packetSelectedMeshes.size() * sizeof(int) +
            (d.nodeMaterial ? d.nodeMaterial->size() * sizeof(int) : 0);
        auto old = staticRenderPackets_.find(d.persistentId);
        const size_t replacedBytes = old == staticRenderPackets_.end()
            ? 0 : staticPacketBytes(old->second);
        if (!packetHasFade && count <= kMaxPacketItems &&
            newBytes <= kMaxPacketBytes &&
            newBytes <= kMaxPacketBytes - (staticRenderPacketBytes_ - replacedBytes)) {
            StaticPacketEntry& packet = staticRenderPackets_[d.persistentId];
            packet.draw = d;
            packet.draw.staticCache = nullptr;
            packet.draw.materials = nullptr; // don't retain caller-owned vector
            packet.draw.nodeMaterial = nullptr;
            packet.hasNodeMaterialOverride = d.nodeMaterial != nullptr;
            if (d.nodeMaterial)
                packet.nodeMaterialSnapshot = *d.nodeMaterial;
            else
                packet.nodeMaterialSnapshot.clear();
            packet.geometryRevision = m->geometryRevision;
            packet.materialRevision = m->materialRevision;
            packet.sharedMaterialSerial = sharedMaterials ? sharedMaterials->serial : 0;
            packet.vertexBuffer = m->vb.id;
            packet.indexBuffer = m->ib.id;
            packet.seen = staticRenderPacketEpoch_;
            packet.items.assign(modelItems_.begin() + packetStart, modelItems_.end());
            packet.selectedMeshes = std::move(packetSelectedMeshes);
            if (sharedMaterials) {
                // Fingerprints are shared across all objects with this owner.
                // Free prior unversioned snapshot allocations on migration;
                // clear() alone would retain capacity outside the 64 MiB cap.
                std::vector<uint64_t>().swap(packet.materialHashes);
                std::vector<Material>().swap(packet.materialsSnapshot);
            } else {
                packet.materialHashes = staticPacketMaterialHashes(*m, mats);
                packet.materialsSnapshot = mats;
            }
            staticRenderPacketBytes_ = staticRenderPacketBytes_ - replacedBytes +
                                       staticPacketBytes(packet);
            ++staticRenderPacketStats_.rebuilds;
        } else {
            ++staticRenderPacketStats_.declined;
            if (old != staticRenderPackets_.end()) {
                staticRenderPacketBytes_ -= replacedBytes;
                staticRenderPackets_.erase(old);
            }
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
    d[26] = selectionMaskPass_ && phase == 3 ? 0.0f : x.outline.w;
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
    // Palette has a fixed shader ABI of 64 entries. Stack storage avoids
    // allocating and freeing one vector per indirect submission.
    std::array<const Material*, kMaxMaterialPalette> instanceMaterialSlots{};
    size_t instanceMaterialSlotCount = 0;
    if (!gpuInstances) {
        const uint32_t slots = instanced ? instanceLimit_ : 1u;
        instData_.assign(size_t(slots) * 36, 0.0f);
        for (uint32_t i = 0; i < count; ++i) {
            const auto& part = modelItems_[items[i]];
            uint32_t materialSlot = 0;
            if (instancePalette) {
                const auto last = instanceMaterialSlots.begin() + instanceMaterialSlotCount;
                const auto found = std::find(instanceMaterialSlots.begin(), last, part.mat);
                if (found == last) {
                    // The independent preflight in drawModelIndirectBatch
                    // already checked the same fixed 64-material limit.
                    if (instanceMaterialSlotCount == kMaxMaterialPalette) return false;
                    materialSlot = uint32_t(instanceMaterialSlotCount);
                    instanceMaterialSlots[instanceMaterialSlotCount++] = part.mat;
                } else {
                    materialSlot = uint32_t(found - instanceMaterialSlots.begin());
                }
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
        for (size_t i = 0; i < instanceMaterialSlotCount; ++i) {
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
    // Only immutable, static pooled rigid Models use Vulkan-standard indexed
    // vertexOffset semantics, matching the indirect command path. Other
    // imported/posed/morphed RHI draws keep their legacy vertex-ID behavior.
    const bool canonicalVulkan = dev_->info().backend == gfx::Backend::Vulkan &&
        vulkanPooledMdiAddressingEnabled() && it.model->geometryPoolPage &&
        (it.flags & kDrawStatic) && it.rigid && it.joints < 0 &&
        it.morph < 0 && !it.baked && !it.mat->shaderGraph;
    if (canonicalVulkan)
        dev_->drawIndexedCanonical(it.indexCount,
            it.firstIndex + it.model->gpuFirstIndex,
            int32_t(it.model->gpuBaseVertex), count);
    else
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
    const bool profileMdiCpu = profiling_ && !auxiliaryModelPass_ &&
        (phase == 0 || (phase == 1 && geometryMainView_));
    auto* cpuStages = profileMdiCpu ? &modelStats_.mdiCpuStages[size_t(phase)] : nullptr;
    if (instancePalette) {
        // Fixed 64-entry shader palette: avoid transient heap allocations in
        // this preflight, and preserve its exact material-pointer semantics.
        std::array<const Material*, kMaxMaterialPalette> unique{};
        size_t uniqueCount = 0;
        for (uint32_t n = 0; n < count; ++n) {
            const Material* m = modelItems_[items[n]].mat;
            if (std::find(unique.begin(), unique.begin() + uniqueCount, m) !=
                unique.begin() + uniqueCount) continue;
            if (uniqueCount == kMaxMaterialPalette) return false;
            unique[uniqueCount++] = m;
        }
    }
    // Vulkan's ordinary direct indexed draw uses a legacy VB binding offset
    // and cannot match a nonzero indirect vertexOffset. For explicitly opted-in
    // static pooled rigid items, drawModelBatch selects drawIndexedCanonical,
    // giving both paths the same Vulkan vertex-index semantics. Other models
    // keep the old safe fallback, including all nonpooled/deformed callers.
    if (dev_->info().backend == gfx::Backend::Vulkan) {
        for (uint32_t n = 0; n < count; ++n) {
            const auto& part = modelItems_[items[n]];
            if (!part.model->gpuBaseVertex) continue;
            if (!vulkanPooledMdiAddressingEnabled() ||
                !part.model->geometryPoolPage || !part.rigid ||
                !(part.flags & kDrawStatic) || part.joints >= 0 ||
                part.morph >= 0 || part.baked || !part.mat ||
                part.mat->shaderGraph)
                return false;
        }
    }
    // Start after preflight rejection checks: pack covers exact cache
    // signature validation and building the ordered CPU indexed commands.
    const auto packStart = cpuStages
        ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    if (cpuStages) ++cpuStages->indirectAttempts;
    const bool traceIndirect = std::getenv("AIVERSE_ICB_PROFILE") != nullptr;
    const auto indirectStart = std::chrono::steady_clock::now();
    // Exact-signature validation makes this safe across changing visibility,
    // model reloads and mixed geometry pages. Static scenes reuse layouts
    // almost every frame; avoid rebuilding the command list each time.
    // Per-view policy was resolved at beginScene. Disable caching without
    // disabling CPU MDI submission or mutating previously cached layouts.
    const bool layoutCaching = mdiLayoutCacheEnabled_;
    // A signature hit can use the already retained immutable command vector
    // directly until this synchronous drawIndexedIndirect returns. It does not
    // need a second CPU-to-CPU copy into indirectCommandScratch_.
    uint64_t triangles = 0;
    bool cacheHit = false;
    IndirectLayoutEntry* cacheEntry = nullptr;
    if (layoutCaching && count <= kMaxMdiCachedItems) {
        auto& entries = indirectLayoutCache_[size_t(phase)];
        const size_t cursor = indirectLayoutCursor_[size_t(phase)]++;
        if (cursor < kMaxMdiLayoutsPerPhase) {
            if (cursor == entries.size()) entries.emplace_back();
            cacheEntry = &entries[cursor];
            cacheEntry->lastUsedScene = indirectLayoutSceneEpoch_;
        }
    }
    if (layoutCaching && !cacheEntry)
        ++modelStats_.mdiLayoutCacheBypassed;
    if (cacheEntry) {
        // Compare exact model, revision, GPU handles and primitive geometry.
        // Unloaded assets are explicitly retired before pointers can recycle.
        cacheHit = cacheEntry->signature.size() == count;
        if (cacheHit) for (uint32_t n = 0; n < count; ++n) {
            const auto& part = modelItems_[items[n]];
            const IndirectLayoutItem current{
                part.model, part.model->geometryRevision,
                part.model->vb.id, part.model->ib.id,
                part.prim, part.indexCount, part.firstIndex,
                part.model->gpuFirstIndex, uint32_t(part.model->gpuBaseVertex)};
            if (!(cacheEntry->signature[n] == current)) {
                cacheHit = false;
                break;
            }
        }
        if (cacheHit) {
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
        auto& scratch = indirectCommandScratch_;
        scratch.clear();
        if (scratch.capacity() < count) scratch.reserve(count);
        // Consecutive instances of one primitive form ONE indirect command.
        // firstInstance selects per-object transform data.
        for (uint32_t n = 0; n < count;) {
            const auto& part = modelItems_[items[n]];
            uint32_t end = n + 1;
            while (end < count && modelItems_[items[end]].prim == part.prim) ++end;
            scratch.push_back({part.indexCount, end - n,
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
                cacheEntry->signature.push_back({
                    part.model, part.model->geometryRevision,
                    part.model->vb.id, part.model->ib.id,
                    part.prim, part.indexCount, part.firstIndex,
                    part.model->gpuFirstIndex, uint32_t(part.model->gpuBaseVertex)});
            }
            cacheEntry->commands = scratch;
            cacheEntry->triangles = triangles;
        }
    }
    // Cache hits own a stable per-entry command vector; misses build only in
    // reusable scratch. Do not mutate either source during GPU submission.
    const auto& commands = cacheHit
        ? cacheEntry->commands : indirectCommandScratch_;
    // Building CPU ICB commands on Metal has a measurable setup cost.
    // Avoid switching to indirect for tiny groups where indexed draws win.
    const size_t minimumCommands = dev_->info().backend == gfx::Backend::Metal ? indirectCommandThreshold() : 4u;
    if (commands.size() < minimumCommands) {
        if (cpuStages) {
            cpuStages->commandPackingMs += std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - packStart).count();
            ++cpuStages->underThreshold;
        }
        return false;
    }
    const auto afterGrouping = std::chrono::steady_clock::now();
    if (cpuStages) {
        cpuStages->commandPackingMs += std::chrono::duration<double, std::milli>(
            afterGrouping - packStart).count();
    }
    if (!drawModelBatch(items, count, phase, false, false, instancePalette)) {
        if (cpuStages) {
            cpuStages->pipelineSetupMs += std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - afterGrouping).count();
            ++cpuStages->pipelineWarmups;
        }
        return true; // pipeline warming: no fallback draw
    }
    const auto afterPipeline = std::chrono::steady_clock::now();
    if (cpuStages) {
        cpuStages->pipelineSetupMs += std::chrono::duration<double, std::milli>(
            afterPipeline - afterGrouping).count();
    }
    const bool existingIndirectBuffer = indirectCommands_.valid();
    if (!existingIndirectBuffer) {
        gfx::BufferDesc d;
        d.type = gfx::BufferType::Indirect;
        d.usage = gfx::Usage::Dynamic;
        d.snapshotUpdates = true;
        // Every call writes a complete ordered command prefix and executes
        // exactly commands.size() draws. No draw reads the previous tail.
        // Backends still create isolated in-flight snapshots/versions.
        d.discardOnUpdate = true;
        d.size = commands.size() * sizeof(commands[0]);
        d.data = commands.data();
        d.label = "model indexed indirect commands";
        indirectCommands_ = dev_->createBuffer(d);
    } else dev_->updateBuffer(indirectCommands_, commands.data(), commands.size() * sizeof(commands[0]));
    const auto afterUpload = std::chrono::steady_clock::now();
    if (cpuStages) {
        cpuStages->bufferUploadMs += std::chrono::duration<double, std::milli>(
            afterUpload - afterPipeline).count();
    }
    const bool indirectSubmitted = indirectCommands_.valid() &&
        dev_->drawIndexedIndirect(indirectCommands_, 0, uint32_t(commands.size()));
    if (!indirectSubmitted) {
        if (cpuStages) {
            cpuStages->backendSubmitMs += std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - afterUpload).count();
            ++cpuStages->backendFallbacks;
        }
        for (uint32_t n = 0; n < count; ++n)
            drawModelBatch(items + n, 1, phase);
        return true;
    }
    const auto afterSubmit = std::chrono::steady_clock::now();
    if (cpuStages) {
        cpuStages->backendSubmitMs += std::chrono::duration<double, std::milli>(
            afterSubmit - afterUpload).count();
        ++cpuStages->submitted;
    }
    modelStats_.mdiIndirectCommandBytes +=
        commands.size() * sizeof(gfx::DrawIndexedIndirectCommand);
    if (existingIndirectBuffer) ++modelStats_.mdiIndirectPrefixOverwrites;
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
    if (cacheHit) {
        // Only count genuinely submitted draws. The old warm path copied
        // this complete command vector into scratch before each upload.
        ++modelStats_.mdiLayoutZeroCopySubmissions;
        modelStats_.mdiLayoutCopyAvoidedBytes +=
            commands.size() * sizeof(gfx::DrawIndexedIndirectCommand);
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
    // Count only *successful* mixed-Model CPU MDI submissions. All models
    // must share the same GPU geometry page and retain their per-instance
    // source geometry ranges; a same-model instanced batch is not cross-model.
    if (staticCrossModelBatchesEnabled_ && phase <= 1 && count > 1) {
        const Model* anchor = modelItems_[items[0]].model;
        bool differentModels = false, compatiblePage = anchor != nullptr;
        for (uint32_t n = 0; n < count && compatiblePage; ++n) {
            const auto& part = modelItems_[items[n]];
            if (!part.model || !part.rigid ||
                !(part.flags & kDrawStatic) || !part.model->geometryPoolPage ||
                part.joints >= 0 || part.morph >= 0 || part.baked ||
                part.gi != 0 || part.materialRole || part.shadowRole ||
                part.paletteMaterials || part.lodThreshold < 1.0f ||
                part.lodInvert) {
                compatiblePage = false;
                break;
            }
            if (part.model != anchor) {
                differentModels = true;
                if (!staticCrossModelPageCompatible(*anchor, *part.model))
                    compatiblePage = false;
            }
        }
        if (differentModels && compatiblePage) {
            ++modelStats_.staticCrossModelSubmissions;
            modelStats_.staticCrossModelCommands += commands.size();
            modelStats_.staticCrossModelItems += count;
        }
    }
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

    // Feedback from the previous completed pass, sampled before counters reset.
    // This is CPU submission feedback, not measured GPU busy-time.
    static const bool autoBatch = [] {
        const char* flag = std::getenv("AIVERSE_BATCH_AUTO");
        return flag && std::strcmp(flag, "1") == 0;
    }();
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
    }() || autoBatch;
    static const uint32_t minGpuIcbCommands = [] {
        const char* raw = std::getenv("AIVERSE_GPU_ICB_MIN_COMMANDS");
        if (!raw || !*raw) return 2u;
        char* end = nullptr;
        const long value = std::strtol(raw, &end, 10);
        return end != raw && *end == '\0' && value >= 2 && value <= 256
            ? uint32_t(value) : 2u;
    }();
    const uint32_t groupCapacity = std::max(2u, instanceLimit_);
    const uint32_t effectiveMinimum = autoBatch
        ? std::max(minGpuIcbCommands, gpuIcbAutoMinimum_)
        : gpuIcbMinimumCommands(
            gpuModelEligible_, minGpuIcbCommands, adaptiveGpuIcb);
    // In auto mode avoid costly grouping/compute for small eligible workloads.
    // Existing CPU MDI / instancing still handles every item.
    if (autoBatch && gpuIcb && gpuModelEligible_ < effectiveMinimum) return;
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
            gpuIcbSourceSlotsScratch_.clear();
            gpuIcbSourceSlotsScratch_.reserve(end - first);
            for (size_t j = first; j < end; ++j)
                gpuIcbSourceSlotsScratch_.push_back(gpuModelSlots_[indices[j]]);
            ++gpuIcbCandidates_;
            // One contiguous GPU Instance record per command. The Metal vertex
            // shader fetches it by baseInstance, so no per-batch U0 upload is
            // necessary when binding the GPU_INSTANCE_BUFFER permutation.
            gpuIcbInstancesScratch_.resize((end - first) * 36u);
            std::fill(gpuIcbInstancesScratch_.begin(), gpuIcbInstancesScratch_.end(), 0.0f);
            for (size_t j = first; j < end; ++j)
                packModelInstance(modelItems_[indices[j]], 1, {1, 1, 1, 1},
                                  gpuIcbInstancesScratch_.data() + (j - first) * 36u);
            const uint32_t token = dev_->prepareGpuEncodedIndexedIcb(
                gpuModelCommands_.output(gpuModelFrame_, gpuModelSubmission_),
                modelItems_[indices[first]].model->vb,
                modelItems_[indices[first]].model->ib,
                gpuIcbSourceSlotsScratch_.data(), uint32_t(gpuIcbSourceSlotsScratch_.size()),
                gpuIcbInstancesScratch_.data(), gpuIcbInstancesScratch_.size() * sizeof(float));
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
    if (autoBatch) {
        // Hysteresis: favour the CPU for sustained failed prepares and only
        // probe GPU encoding again after workload grows. Never disable
        // ordinary draw fallback or bypass material/geometry validation.
        if (gpuIcbPrepared_ && !gpuIcbRejected_) {
            gpuIcbAutoMisses_ = 0;
            if (gpuIcbAutoMinimum_ > 64)
                gpuIcbAutoMinimum_ = std::max(64u, gpuIcbAutoMinimum_ / 2u);
        } else if (gpuIcbCandidates_) {
            if (++gpuIcbAutoMisses_ >= 3) {
                gpuIcbAutoMinimum_ = std::min(256u, gpuIcbAutoMinimum_ * 2u);
                gpuIcbAutoMisses_ = 0;
            }
        }
    }
}

void Renderer3D::renderModels(int phase) {
    if (modelItems_.empty()) return;
    // Read the same dynamic indirect-buffer handle at both boundaries.
    // Reflection and other auxiliary view replays are intentionally excluded
    // from the main-camera opaque metric; phases 2..4 are not shadow/opaque.
    // A buffer first created *during* this pass has no previous counters,
    // so its initial versions and CPU creation-time copies belong here.
    const bool sampleMdiTransfers = !auxiliaryModelPass_ &&
        (phase == 0 || (phase == 1 && geometryMainView_));
    const gfx::BufferHandle transferHandleBefore = sampleMdiTransfers
        ? indirectCommands_ : gfx::BufferHandle{};
    const gfx::BufferUpdateStats transferBefore = sampleMdiTransfers
        ? indirectBufferUpdateStats() : gfx::BufferUpdateStats{};
    // A renderModels invocation is one independent visibility/ordering epoch.
    // Reset the per-phase cache cursor, but retain validated layouts across frames.
    indirectLayoutCursor_[size_t(phase)] = 0;
    const bool traceModelPass = std::getenv("AIVERSE_MODEL_BATCH_PROFILE") != nullptr;
    const bool profileMdiCpu = profiling_ && sampleMdiTransfers;
    auto* cpuStages = profileMdiCpu ? &modelStats_.mdiCpuStages[size_t(phase)] : nullptr;
    // Stage 30 aggregates within beginScene. Snapshot before incrementing
    // pass counters so each Stage 31 ring sample is an independent invocation.
    const ModelRenderStats::CpuMdiStageProfile cpuBefore =
        cpuStages ? *cpuStages : ModelRenderStats::CpuMdiStageProfile{};
    const auto passBegin = std::chrono::steady_clock::now();
    if (cpuStages) ++cpuStages->passes;
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
    const auto afterSelect = std::chrono::steady_clock::now();
    if (cpuStages) {
        cpuStages->selectionMs += std::chrono::duration<double, std::milli>(
            afterSelect - passBegin).count();
    }
    if (order_.empty()) {
        if (cpuStages) {
            cpuStages->totalMs += std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - passBegin).count();
            recordCpuMdiStageSample(size_t(phase), cpuBefore);
        }
        return;
    }
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
    // Keep the independent material-palette sort rules. A renderer caller
    // may opt into geometry sorting without touching process env flags.
    const bool geometrySortRequested = mdiGeometrySortRequested_;
    const bool mdiGeometrySort = modelMultiDrawEnabled_ &&
        dev_->supportsMultiDrawIndirect() &&
        (!gpuAdaptiveInstancingFallback_ || groupAdaptive) &&
        phase <= 1 && (geometrySortRequested || paletteSort);
    // Opt-in specifically for opaque/shadow static rigid primitives from
    // different source models resident in ONE GPU geometry page. Visibility,
    // sun-shadow culling and per-node LOD were already calculated before the
    // pass order is built. This only changes the safe opaque grouping order.
    const char* crossSafe = std::getenv("AIVERSE_RENDER_SAFE");
    const char* crossIndirect = std::getenv("AIVERSE_MULTI_DRAW_INDIRECT");
    bool staticCrossModelSort = staticCrossModelBatchesEnabled_ &&
        modelMultiDrawEnabled_ && dev_->supportsMultiDrawIndirect() &&
        !gpuAdaptiveInstancingFallback_ && !gpuModelCommandsReady_ &&
        geometryMainView_ && !auxiliaryModelPass_ && !drawCallDebug_ &&
        phase <= 1 && !paletteSort &&
        !(crossSafe && std::strcmp(crossSafe, "1") == 0) &&
        !(crossIndirect && std::strcmp(crossIndirect, "off") == 0);
    const auto eligibleStaticPage = [&](const ModelItem& item) {
        return item.model && item.prim && item.mat &&
            item.model->geometryPoolPage && !item.model->geometry &&
            item.model->vb.valid() && item.model->ib.valid() &&
            (item.flags & kDrawStatic) && item.rigid &&
            !(item.flags & (kDrawTransparent | kDrawFadeDepth | kDrawActorReveal)) &&
            item.tint.w >= .99f && item.dissolve <= 0 &&
            !item.baked && item.joints < 0 && item.morph < 0 &&
            item.gi == 0 && !item.materialRole && !item.shadowRole &&
            !item.paletteMaterials && !item.gpuLodAvailable &&
            item.lodThreshold >= 1.0f && !item.lodInvert &&
            item.mat->alpha == AlphaMode::Opaque && !item.mat->transparent &&
            !item.mat->shaderGraph &&
            // The optional canonical direct indexed path matches Vulkan's
            // nonzero indirect vertexOffset for immutable rigid pooled models.
            // With the opt-in OFF, retain the exact old direct fallback.
            (dev_->info().backend != gfx::Backend::Vulkan ||
             item.model->gpuBaseVertex == 0 ||
             vulkanPooledMdiAddressingEnabled());
    };
    // Page-aware preflight operates on *already visible* ModelItems. Only
    // groups with distinct source Models AND enough unique primitives to
    // meet the backend's indirect-command threshold trigger special sorting.
    // Everything else retains the original material/primitive sort order.
    // GPU-state materialGroup IDs were resolved with exact comparisons above.
    // Workspace memory and group slots persist across shadow/main passes,
    // but a beginPass epoch prevents leaking any visibility or stale pointer
    // decisions from the preceding camera or model asset revision.
    staticCrossPreflight_.clearModes();
    if (staticCrossModelSort) {
        const size_t threshold = dev_->info().backend == gfx::Backend::Metal
            ? indirectCommandThreshold() : size_t{4};
        if (order_.size() < threshold || size_t(instanceLimit_) < threshold) {
            staticCrossModelSort = false;
        } else {
            auto& workspace = staticCrossPreflight_;
            workspace.beginPass(modelItems_.size());
            // A fixed open-addressed directory and bounded vector of groups
            // replace the per-pass unordered_map/node allocations. The
            // item->group index is retained only for this render pass, so
            // the promotion scan does not repeat a material/page hash lookup.
            bool budgetExceeded = false;
            for (uint32_t itemIndex : order_) {
                const ModelItem& item = modelItems_[itemIndex];
                if (!eligibleStaticPage(item)) continue;
                const StaticCrossModelPreflightKey key{
                    item.model->geometryPoolPage.get(),
                    materialGroup[itemIndex], item.flags,
                    item.model->vb.id, item.model->ib.id};
                const uint16_t groupIndex = workspace.findOrCreate(key);
                if (groupIndex == StaticCrossModelPreflightWorkspace::kInvalidGroup) {
                    budgetExceeded = true;
                    break;
                }
                workspace.groups()[groupIndex].group.observe(
                    item.model, item.prim, threshold);
                workspace.rememberItem(itemIndex, groupIndex);
            }
            modelStats_.staticCrossModelPreflightGroups += workspace.groups().size();
            size_t readyGroupsThisPass = 0;
            if (!budgetExceeded) for (auto& record : workspace.groups()) {
                auto& group = record.group;
                group.ready = group.opportunity(threshold, instanceLimit_);
                if (group.ready) {
                    ++readyGroupsThisPass;
                    ++modelStats_.staticCrossModelPreflightReady;
                } else {
                    ++modelStats_.staticCrossModelPreflightRejected;
                }
            }
            if (budgetExceeded) {
                modelStats_.staticCrossModelPreflightRejected += workspace.groups().size();
                staticCrossModelSort = false;
            } else if (readyGroupsThisPass == 0) {
                staticCrossModelSort = false;
            } else {
                // Each live GPU page has its own shadow/opaque CPU cost.
                // One decision per page, scene epoch and pass, regardless
                // of how many material groups on that page qualified.
                size_t permittedGroups = 0;
                bool skippedThisPass = false, probedThisPass = false;
                for (auto& record : workspace.groups()) {
                    auto& group = record.group;
                    if (!group.ready) continue;
                    if (cpuMdiAdaptiveEnabled_) {
                        CpuMdiPageEntry* entry = cpuMdiPageEntry(*group.firstModel);
                        if (entry) {
                            const size_t pass = size_t(phase);
                            if (entry->decisionEpoch[pass] != cpuMdiPageEpoch_) {
                                auto& feedback = entry->byPass[pass];
                                const bool overBudget = feedback.samples >= 8 &&
                                    feedback.avgUsPerCommand >
                                        cpuMdiAdaptiveBudgetUs_;
                                entry->permitted[pass] =
                                    feedback.permit(cpuMdiAdaptiveBudgetUs_);
                                entry->decisionEpoch[pass] = cpuMdiPageEpoch_;
                                if (overBudget && entry->permitted[pass]) {
                                    ++modelStats_.staticCrossModelAdaptiveProbeGroups;
                                    probedThisPass = true;
                                }
                            }
                            group.permitted = entry->permitted[pass];
                        }
                    }
                    if (group.permitted) {
                        ++permittedGroups;
                    } else {
                        ++modelStats_.staticCrossModelAdaptiveSkippedGroups;
                        skippedThisPass = true;
                    }
                }
                if (skippedThisPass)
                    ++modelStats_.staticCrossModelAdaptiveSkippedPasses;
                if (probedThisPass)
                    ++modelStats_.staticCrossModelAdaptiveProbePasses;
                // The group index was captured during this same visibility
                // epoch. No second GPU-material/page hash lookup or redundant
                // eligibleStaticPage call is needed when assigning draw modes.
                workspace.activateModes(modelItems_.size());
                for (uint32_t itemIndex : order_) {
                    const uint16_t groupIndex = workspace.groupForItem(itemIndex);
                    if (groupIndex == StaticCrossModelPreflightWorkspace::kInvalidGroup)
                        continue;
                    const auto& group = workspace.groups()[groupIndex].group;
                    if (!group.ready) continue;
                    const uint8_t mode = group.permitted ? 1u : 2u;
                    workspace.rememberMode(itemIndex, mode);
                    if (mode == 1u)
                        ++modelStats_.staticCrossModelPreflightItems;
                }
                staticCrossModelSort = permittedGroups > 0;
                if (staticCrossModelSort)
                    modelStats_.staticCrossModelRankedGroups +=
                        workspace.prepareSortRanks();
            }
        }
    }
    if (phase == 2) { // transparent: preserve original back-to-front order
        std::sort(order_.begin(), order_.end(), [&](uint32_t a, uint32_t b) {
            return modelItems_[a].depth > modelItems_[b].depth;
        });
    } else {
        // The existing static cross-model comparator puts all admitted items
        // before ordinary items. Partition that same set once; then avoid
        // comparing a candidate to every ordinary item during a global sort.
        // mode 2 (adaptive fallback) always follows the legacy sorter.
        auto ordinaryBegin = order_.begin();
        if (staticCrossModelSort) {
            ordinaryBegin = std::partition(order_.begin(), order_.end(),
                [&](uint32_t index) {
                    return staticCrossPreflight_.modeOf(index) == 1u;
                });
            const size_t permittedCount = size_t(ordinaryBegin - order_.begin());
            const auto withinGroupLess = [&](uint32_t a, uint32_t b) {
                const ModelItem &x = modelItems_[a], &y = modelItems_[b];
                if (x.lmPage != y.lmPage) return x.lmPage < y.lmPage;
                if (x.prim != y.prim)
                    return std::less<const Primitive*>{}(x.prim, y.prim);
                return a < b;
            };
            // For small candidate sets the counting pass and bucket scratch
            // can cost more than one comparison sort; use a conservative
            // CPU-only threshold without changing GPU MDI eligibility.
            if (permittedCount >= 64u &&
                staticCrossPreflight_.bucketizePermittedPrefix(
                    order_, permittedCount, withinGroupLess)) {
                modelStats_.staticCrossModelBucketedItems += permittedCount;
                modelStats_.staticCrossModelBucketedGroups +=
                    staticCrossPreflight_.rankedGroupCount();
            } else {
                // The earlier ranked comparator is the safe fallback for
                // tiny groups or unexpected stale/missing rank records.
                std::sort(order_.begin(), ordinaryBegin,
                    [&](uint32_t a, uint32_t b) {
                        const uint16_t ra =
                            staticCrossPreflight_.sortRankForPermittedItem(a);
                        const uint16_t rb =
                            staticCrossPreflight_.sortRankForPermittedItem(b);
                        if (ra != rb) return ra < rb;
                        return withinGroupLess(a, b);
                    });
            }
        }
        // Non-admitted objects retain the exact legacy order: this includes
        // per-page adaptive instancing, nonstatic, nonpooled and GI items.
        std::sort(ordinaryBegin, order_.end(), [&](uint32_t a, uint32_t b) {
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
    modelStats_.staticCrossModelPreflightWorkspaceBytes = std::max(
        modelStats_.staticCrossModelPreflightWorkspaceBytes,
        staticCrossPreflight_.retainedBytes());
    if(profiling_)profile_.sortMs[size_t(phase)]+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    const auto afterSort = std::chrono::steady_clock::now();
    if (cpuStages) {
        // Material-group resolution and preflight are included here, not
        // just the final std::sort/order_ operation.
        cpuStages->groupingSortMs += std::chrono::duration<double, std::milli>(
            afterSort - afterSelect).count();
    }
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

    // AUTO is a three-path policy: when neither indirect encoder can
    // amortize setup, use the ordinary direct/instancing renderer. This
    // conservative admission gate is per pass, after visibility/culling,
    // and never affects explicit manual batching modes.
    static const bool autoBatchCpu = [] {
        const char* flag = std::getenv("AIVERSE_BATCH_AUTO");
        return flag && std::strcmp(flag, "1") == 0;
    }();
    static const size_t autoCpuMdiMinimum = [] {
        const char* raw = std::getenv("AIVERSE_BATCH_AUTO_CPU_MIN_ITEMS");
        if (!raw || !*raw) return size_t(64);
        char* end = nullptr;
        const long n = std::strtol(raw, &end, 10);
        return end != raw && *end == '\0' && n >= 16 && n <= 4096
            ? size_t(n) : size_t(64);
    }();
    // Learn only from unambiguous CPU-only passes. A successful GPU ICB
    // in the opaque pass would contaminate a Direct-vs-MDI CPU comparison.
    // Workloads below the conservative threshold keep the guaranteed direct
    // path; larger buckets independently explore both CPU alternatives.
    const bool autoCpuLearn = autoBatchCpu && phase <= 1 &&
        !auxiliaryModelPass_ && geometryMainView_ &&
        // GPU ICB covers opaque only; shadow CPU samples remain valid.
        (phase == 0 || gpuEncodedBatches_.empty()) &&
        modelMultiDrawEnabled_ &&
        order_.size() >= autoCpuMdiMinimum;
    const bool autoCpuPermit = !autoBatchCpu ||
        (autoCpuLearn && autoCpuBatch_.useMdi(size_t(phase), order_.size()));
    const uint64_t cpuMdiBefore = modelStats_.multiDrawSubmissions;
    const bool allowIndirect = autoCpuPermit && modelMultiDrawEnabled_ &&
        (!gpuAdaptiveInstancingFallback_ || groupAdaptive) &&
        !(renderSafe && std::strcmp(renderSafe, "1") == 0) &&
        !(indirectToggle && std::strcmp(indirectToggle, "off") == 0) &&
        dev_->supportsMultiDrawIndirect() && phase <= 1;
    const size_t minIndirectCommands = dev_->info().backend == gfx::Backend::Metal ? indirectCommandThreshold() : 4u;
    // Opt-in short diagnostic. Trace MDI training probes, not every object.
    // A missing candidate scan means one of the gates below rejected the
    // whole pass; no amount of command-packing optimization can fix that.
    static const bool traceAutoCpu = [] {
        const char* value = std::getenv("AIVERSE_BATCH_AUTO_TRACE");
        return value && std::strcmp(value, "1") == 0;
    }();
    if (traceAutoCpu && autoBatchCpu && autoCpuLearn && phase <= 1) {
        static uint32_t probes[2]{};
        const size_t lane = size_t(phase);
        if (autoCpuPermit && probes[lane]++ < 12) {
            std::fprintf(stderr,
                "[auto-cpu-gate] phase=%d items=%zu permit=%d allowed=%d "
                "multiDraw=%d adaptiveGpuFallback=%d groupAdaptive=%d "
                "renderSafe=%d indirectOff=%d supported=%d "
                "minCommands=%zu instanceLimit=%u gpuGroups=%zu\n",
                phase, order_.size(), int(autoCpuPermit), int(allowIndirect),
                int(modelMultiDrawEnabled_), int(gpuAdaptiveInstancingFallback_),
                int(groupAdaptive),
                int(renderSafe && std::strcmp(renderSafe, "1") == 0),
                int(indirectToggle && std::strcmp(indirectToggle, "off") == 0),
                int(dev_->supportsMultiDrawIndirect()), minIndirectCommands,
                instanceLimit_, gpuEncodedBatches_.size());
        }
    }
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
            const char* autoFlag = std::getenv("AIVERSE_BATCH_AUTO");
            return (flag && std::strcmp(flag, "1") == 0) ||
                   (autoFlag && std::strcmp(autoFlag, "1") == 0);
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
        // Adaptive fallback only changes the static pooled subgroup. Other
        // ordinary MDI candidates (including dynamic and nonpooled models)
        // retain their existing selection logic.
        const bool preferInstancing = cpuMdiAdaptiveEnabled_ &&
            staticCrossPreflight_.modeOf(order_[i]) == 2u;
        if (allowIndirect && !preferInstancing &&
            order_.size() - i >= minIndirectCommands) {
            ++modelStats_.mdiScans;
            const auto scanBegin = std::chrono::steady_clock::now();
            bool cpuScanRecorded = false;
            if (cpuStages) ++cpuStages->candidateScans;
            ++mdiCandidates;
            const uint32_t firstIndex = order_[i];
            const auto& first = modelItems_[firstIndex];
            const bool firstEligible = !first.model->geometry &&
                first.gi == 0 && !first.materialRole && !first.shadowRole &&
                !first.baked && first.joints < 0 && first.morph < 0 &&
                !first.paletteMaterials && first.tint.w >= .99f;
            const bool pooledStatic = staticCrossModelSort &&
                staticCrossPreflight_.modeOf(firstIndex) == 1u;
            auto compatible = [&](uint32_t candidateIndex) {
                const auto& y = modelItems_[candidateIndex];
                // Each indirect command selects its own world/tint/team/etc
                // through firstInstance into the shared instance uniform array.
                // Pipeline, geometry buffers, textures and material uniforms
                // must remain compatible across the entire command group.
                return (!pooledStatic ||
                    (staticCrossPreflight_.modeOf(candidateIndex) == 1u &&
                     eligibleStaticPage(y) &&
                     (first.model == y.model ||
                      staticCrossModelPageCompatible(*first.model, *y.model)))) &&
                    first.model->vb == y.model->vb &&
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
                    const bool measureAdaptive = cpuMdiAdaptiveEnabled_ && pooledStatic;
                    size_t runs = 1;
                    for (size_t k = i + 1;
                         k < end && (measureAdaptive || runs < minIndirectCommands);
                         ++k)
                        if (modelItems_[order_[k - 1]].prim != modelItems_[order_[k]].prim)
                            ++runs;
                    // With group-adaptive selection, reject MDI groups
                    // consisting mostly of repeated identical primitives.
                    // Instancing then avoids ICB setup and GPU command work.
                    if (runs >= minIndirectCommands &&
                        (!groupAdaptive || shouldUseCpuMultiDraw(
                            uint32_t(end - i), uint32_t(runs),
                            uint32_t(minIndirectCommands)))) {
                        // The run search and threshold decision are selection
                        // work, never part of indirect command packing time.
                        if (cpuStages) {
                            cpuStages->candidateScanMs += std::chrono::duration<double, std::milli>(
                                std::chrono::steady_clock::now() - scanBegin).count();
                            cpuScanRecorded = true;
                        }
                        const auto attemptBegin = std::chrono::steady_clock::now();
                        const uint64_t submissionsBefore =
                            measureAdaptive ? modelStats_.multiDrawSubmissions : 0;
                        const bool accepted = drawModelIndirectBatch(
                            order_.data() + i, uint32_t(end - i), phase, paletteSort);
                        if (measureAdaptive) {
                            // CPU wall-clock includes compatible-run scan,
                            // command packing, pipeline bindings and the GPU
                            // submission API, not actual GPU elapsed time.
                            // Failed submissions still teach the policy that
                            // costly attempts should be avoided on most frames.
                            const auto elapsedUs =
                                std::chrono::duration<double, std::micro>(
                                    std::chrono::steady_clock::now() - scanBegin).count();
                            const bool submitted =
                                modelStats_.multiDrawSubmissions > submissionsBefore;
                            // The pass-wide aggregate is diagnostic only.
                            // Admission is controlled by the source page's
                            // own shadow/opaque EMA, never another page.
                            cpuMdiAdaptive_[size_t(phase)].observe(
                                elapsedUs, uint32_t(runs), submitted);
                            if (CpuMdiPageEntry* entry =
                                    cpuMdiPageEntry(*first.model))
                                entry->byPass[size_t(phase)].observe(
                                    elapsedUs, uint32_t(runs), submitted);
                            ++modelStats_.staticCrossModelAdaptiveMeasuredAttempts;
                        }
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
            if (cpuStages && !cpuScanRecorded) {
                cpuStages->candidateScanMs += std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - scanBegin).count();
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
    if (autoCpuLearn) {
        // One sample per completed phase. Only credit MDI if the backend
        // actually submitted it; otherwise fallbacks must not teach the
        // scheduler that indirect encoding was the measured path.
        const bool mdiSubmitted = modelStats_.multiDrawSubmissions > cpuMdiBefore;
        if (autoCpuPermit && !mdiSubmitted) {
            autoCpuBatch_.recordNoSubmission(size_t(phase), order_.size());
        } else {
            const double cpuMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - passBegin).count();
            autoCpuBatch_.observe(size_t(phase), order_.size(),
                                  autoCpuPermit, cpuMs);
        }
    }
    if (cpuStages) {
        cpuStages->totalMs += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - passBegin).count();
        recordCpuMdiStageSample(size_t(phase), cpuBefore);
    }
    if (sampleMdiTransfers) {
        const size_t pass = size_t(phase); // only 0 (shadow) or 1 (main opaque)
        const gfx::BufferUpdateStats transferAfter = indirectBufferUpdateStats();
        // A replaced buffer cannot be compared with the previous handle's
        // lifetime stats. No GPU readback is needed for this identity guard.
        if ((!transferHandleBefore.valid() ||
             transferHandleBefore == indirectCommands_) &&
            transferAfter.available) {
            const auto delta = gfx::bufferUpdateCounterDelta(
                transferBefore, transferAfter);
            if (delta.available) {
                gfx::accumulateBufferUpdateCounters(
                    modelStats_.mdiBackendPassTransfers[pass], delta);
                ++modelStats_.mdiBackendPassSamples[pass];
            }
        }
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
