#include "Renderer3D.h"
#include "Shaders.h"
#include "SelectionOutlineShaders.h"
#include "Renderer3DMotionShaders.h"
#include "Renderer3DReactiveShaders.h"
#include "NereidWaterReflectionPolicy.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <set>
#include <tuple>
#include <stdexcept>

namespace aiverse::render {

namespace {
constexpr float kPi = 3.14159265358979f;

struct DrawUniforms { float model[16]; float color[4]; float params[4]; float waterRipples[64]; float waterWakeShapes[64]; float waterWind[4]; float waterHydraulics[4]; float waterRapidColor[4]; };
struct FrameUniforms {
    float viewProj[16];
    float lightViewProj[16];
    float invViewProj[16];
    float sunDir[4], sunColor[4], skyColor[4], groundColor[4], fog[4], camPos[4], shadow[4], quality[4];
    float trace0[4], trace1[4], trace2[4], trace3[4];   // Shaders.h: traced terrain shadows + SSR
};
void put3(float* d, float3 v, float w) { d[0] = v.x; d[1] = v.y; d[2] = v.z; d[3] = w; }
} // namespace

// ============================== Renderer3D ==============================

ShadowQuality Renderer3D::defaultQualityFor(const gfx::Device& dev, bool mobile) {
    if (const char* q = std::getenv("AIVERSE_SHADOWS")) { // testing override: off|low|medium|high
        std::string v = q;
        return v == "off" ? ShadowQuality::Off : v == "low" ? ShadowQuality::Low : v == "high" ? ShadowQuality::High : ShadowQuality::Medium;
    }
    const std::string& n = dev.info().name;
    if (n.find("llvmpipe") != std::string::npos || n.find("SwiftShader") != std::string::npos) return ShadowQuality::Low;
    if (mobile) {
        // Old/weak mobile GPUs get the cheapest shadows; everything else medium.
        for (const char* weak : {"Mali-4", "Mali-T", "Adreno (TM) 3", "Adreno (TM) 4", "Adreno (TM) 50", "PowerVR SGX", "PowerVR Rogue GE"})
            if (n.find(weak) != std::string::npos) return ShadowQuality::Low;
        return ShadowQuality::Medium;
    }
    return ShadowQuality::High;
}

bool Renderer3D::init(gfx::Device& dev) {
    checkGpuDevice(dev);
    // Reinitializing the same instance without shutdown would overwrite live
    // GPU handles and the retirement owners borrowed by LightSystem.
    if(dev_)throw std::logic_error("Renderer3D init: already initialized; call shutdown first");
    if(programRetirement_ && programRetirement_->pendingBatches()!=0)
        throw std::logic_error("Renderer3D cannot reinitialize with pending GPU programs");
    if(targetRetirement_ && targetRetirement_->pendingPairs()!=0)
        throw std::logic_error("Renderer3D cannot reinitialize with pending GPU targets");
    programRetirement_=std::make_unique<GpuProgramRetirement>(
        dev,gpuDeviceLease_);
    targetRetirement_=std::make_unique<OffscreenTargetRetirement>(
        dev,gpuDeviceLease_);
    lights.setTargetRetirement(targetRetirement_.get());
    dev_ = &dev;

    // Allocate as a single transaction. An unsuccessful shader, buffer,
    // texture, LightSystem or trace setup must not leave a half-initialized
    // renderer that a host can mistakenly render or initialize again.
    // Rollback requires a real GPU idle barrier, even for async backends.
    bool initialized = false;
    try {
        initialized = [&]() -> bool {
            gfx::ShaderDesc ls; ls.glslVertex = shaders::kLitVS; ls.glslFragment = shaders::kLitFS; ls.msl = shaders::kLitMSL;
            ls.uniformSlots = 3; ls.textureSlots = 6; ls.label = "lit"; // U2/tex1/tex2: Lights (tex2 water reflection); tex3 height field, tex4/tex5 scene (trace)
            litShader_ = dev.createShader(ls);
            gfx::ShaderDesc ss; ss.glslVertex = shaders::kShadowVS; ss.glslFragment = shaders::kShadowFS; ss.msl = shaders::kShadowMSL;
            ss.uniformSlots = 2; ss.label = "shadow";
            shadowShader_ = dev.createShader(ss);
            gfx::ShaderDesc cs; cs.glslVertex = shaders::kColorVS; cs.glslFragment = shaders::kColorFS; cs.msl = shaders::kColorMSL;
            cs.uniformSlots = 2; cs.label = "color";
            colorShader_ = dev.createShader(cs);
            if (!litShader_.valid() || !shadowShader_.valid() || !colorShader_.valid()) return false;

            gfx::PipelineDesc mp;
            mp.shader = litShader_;
            mp.attrs[0] = {0, gfx::VertexFormat::Float3, 0};
            mp.attrs[1] = {1, gfx::VertexFormat::Float3, 12};
            mp.attrs[2] = {2, gfx::VertexFormat::UByte4N, 24};
            mp.attrCount = 3; mp.stride = sizeof(MeshVertex);
            mp.indexType = gfx::IndexType::UInt32;
            litPipe_ = dev.createPipeline(mp);
            mp.blend = gfx::BlendMode::Alpha; mp.depthWrite = false;
            litBlendPipe_ = dev.createPipeline(mp);

            gfx::PipelineDesc sp;
            sp.shader = shadowShader_;
            sp.attrs[0] = {0, gfx::VertexFormat::Float3, 0};
            sp.attrCount = 1; sp.stride = sizeof(MeshVertex);
            sp.indexType = gfx::IndexType::UInt32;
            sp.depthOnly = true;
            sp.cull = gfx::CullMode::Back;   // models are closed meshes; back faces would self-shadow less but cost more
            sp.depthBiasConstant = 2.0f; sp.depthBiasSlope = 2.5f;
            shadowPipe_ = dev.createPipeline(sp);

            gfx::PipelineDesc lp;
            lp.shader = colorShader_;
            lp.attrs[0] = {0, gfx::VertexFormat::Float3, 0};
            lp.attrs[1] = {1, gfx::VertexFormat::UByte4N, 12};
            lp.attrCount = 2; lp.stride = sizeof(ColorVertex);
            lp.indexType = gfx::IndexType::None;
            lp.primitive = gfx::Primitive::Lines;
            lp.cull = gfx::CullMode::None;
            lp.blend = gfx::BlendMode::Alpha;
            lp.depthWrite = false;
            linePipe_ = dev.createPipeline(lp);
            lp.primitive = gfx::Primitive::Triangles;
            triPipe_ = dev.createPipeline(lp);

            gfx::BufferDesc vb; vb.usage = gfx::Usage::Dynamic; vb.size = 64 * 1024;
            lineVB_ = dev.createBuffer(vb);
            triVB_ = dev.createBuffer(vb);
            setShadowQuality(quality_);
            initModels();
            if (!lights.init(dev)) return false;
            if (!initTrace()) return false;
            initSelectionOutline();  // optional editor effect: failure must not break the game renderer
            return litPipe_.valid() && litBlendPipe_.valid() &&
                   shadowPipe_.valid() && linePipe_.valid() && triPipe_.valid() &&
                   lineVB_.valid() && triVB_.valid() && white_.valid() &&
                   black_.valid() && flatNormal_.valid() &&
                   (quality_ == ShadowQuality::Off || shadowMap_.valid());
        }();
    } catch (...) {
        // If idle/cleanup is impossible, shutdown throws and preserves the
        // live owner state instead of freeing potentially in-flight handles.
        shutdown();
        throw;
    }
    if(!initialized)shutdown();
    return initialized;
}

void Renderer3D::retireRenderTargets(gfx::TextureHandle color,
                                     gfx::TextureHandle depth) {
    if(!color.valid() && !depth.valid())return;
    if(targetRetirement_) {
        targetRetirement_->retire(color,depth);
        return;
    }
    // The shared target owner exists throughout normal Renderer3D use.
    // The fallback is for exceptional setup/teardown only. Never destroy
    // GPU-visible resources without a real idle proof, even with no lease.
    if(!dev_) {
        ++unleasedTargetOrphans_;
        return;
    }
    try {
        if(!dev_->waitForResourceIdle()) {
            ++unleasedTargetIdleFailures_;
            ++unleasedTargetOrphans_;
            return;
        }
        if(color.valid())dev_->destroyTexture(color);
        if(depth.valid())dev_->destroyTexture(depth);
    }catch(...) {
        ++unleasedTargetOrphans_;
    }
}

void Renderer3D::setShadowQuality(ShadowQuality q) {
    if(!dev_) {quality_=q;return;}
    checkGpuDevice(*dev_);
    int size=q==ShadowQuality::High?highShadowSize_:
             q==ShadowQuality::Medium?1536:1024;
    if(q==ShadowQuality::Off)size=0;
    if(size==shadowSize_ && shadowMap_.valid()) {
        quality_=q;return;
    }
    gfx::TextureDesc td;
    td.width=td.height=std::max(size,16);
    td.format=gfx::PixelFormat::Depth;
    td.renderTarget=true;
    td.compare=false;
    td.filter=gfx::Filter::Nearest;
    auto replacement=dev_->createTexture(td);
    if(!replacement.valid())return; // preserve previous valid shadow target
    retireRenderTargets(shadowMap_);
    shadowMap_=replacement;
    shadowSize_=size;
    quality_=q;
}

void Renderer3D::shutdown() {
    if (!dev_) return;
    checkGpuDevice(*dev_);
    // Live renderer shaders and buffers also require a completed GPU frame.
    if(!dev_->waitForResourceIdle())
        throw std::runtime_error("Renderer3D shutdown: GPU idle barrier failed");
    // Successful full-device idle is required when shader cache generations
    // are still pending. Failed completion cannot silently discard them.
    if(programRetirement_ && !programRetirement_->drain())
        throw std::runtime_error(
            "Renderer3D shutdown: pending GPU shader/pipeline completion");
    gpuModelCommands_.release(*dev_);
    persistentCull_.release(*dev_);
    if (persistentUpload_) {
        persistentUpload_->release();
        persistentUpload_.reset();
    }
    persistentIds_.clear();
    persistentScene_ = {};
    staticModelCaches_.clear();
    staticRenderPackets_.clear();
    staticModelEligibility_.clear();
    staticMaterialOwners_.clear();
    staticMaterialOwnerBytes_ = 0;
    staticMaterialOwnerSerial_ = 0;
    staticMaterialOwnerEpoch_ = 0;
    staticRenderPacketBytes_ = 0;
    staticModelCacheEpoch_ = 0;
    staticRenderPacketEpoch_ = 0;
    staticEligibilityEpoch_ = 0;
    cpuMdiAdaptive_ = {};
    cpuMdiPages_.clear();
    cpuMdiPageEpoch_ = 0;
    cpuMdiPageStats_ = {};
    staticCrossPreflight_.release();
    for (auto& [id, history] : objectMotionViews_) {
        if (history.target.valid()) dev_->destroyTexture(history.target);
        if (history.depth.valid()) dev_->destroyTexture(history.depth);
        if (history.reactive.valid()) dev_->destroyTexture(history.reactive);
    }
    objectMotionViews_.clear();
    // Renderer shutdown must occur at the existing host's confirmed
    // GPU-idle teardown point. Quarantined textures are not discarded.
    drainObjectMotionAfterGpuIdle();
    motionLastSubmittedFence_ = 0;
    if (objectMotionPipeline_.valid()) dev_->destroyPipeline(objectMotionPipeline_);
    if (objectMotionShader_.valid()) dev_->destroyShader(objectMotionShader_);
    if (objectMotionInstancedPipeline_.valid()) dev_->destroyPipeline(objectMotionInstancedPipeline_);
    if (objectMotionInstancedShader_.valid()) dev_->destroyShader(objectMotionInstancedShader_);
    if (objectMotionDeformedPipeline_.valid()) dev_->destroyPipeline(objectMotionDeformedPipeline_);
    if (objectMotionDeformedShader_.valid()) dev_->destroyShader(objectMotionDeformedShader_);
    if (objectMotionBakedPipeline_.valid()) dev_->destroyPipeline(objectMotionBakedPipeline_);
    if (objectMotionBakedShader_.valid()) dev_->destroyShader(objectMotionBakedShader_);
    if (objectMotionMaskedPipeline_.valid()) dev_->destroyPipeline(objectMotionMaskedPipeline_);
    if (objectMotionMaskedShader_.valid()) dev_->destroyShader(objectMotionMaskedShader_);
    if (objectMotionReactivePipeline_.valid()) dev_->destroyPipeline(objectMotionReactivePipeline_);
    if (objectMotionReactiveMeshPipeline_.valid()) dev_->destroyPipeline(objectMotionReactiveMeshPipeline_);
    if (objectMotionReactiveCutoutPipeline_.valid()) dev_->destroyPipeline(objectMotionReactiveCutoutPipeline_);
    if (objectMotionReactiveCutoutShader_.valid()) dev_->destroyShader(objectMotionReactiveCutoutShader_);
    if (objectMotionReactiveShader_.valid()) dev_->destroyShader(objectMotionReactiveShader_);
    objectMotionPipeline_ = {}; objectMotionShader_ = {};
    objectMotionInstancedPipeline_ = {}; objectMotionInstancedShader_ = {};
    objectMotionDeformedPipeline_ = {}; objectMotionDeformedShader_ = {};
    objectMotionBakedPipeline_ = {}; objectMotionBakedShader_ = {};
    objectMotionMaskedPipeline_ = {}; objectMotionMaskedShader_ = {};
    objectMotionReactivePipeline_ = {}; objectMotionReactiveShader_ = {};
    objectMotionReactiveCutoutPipeline_ = {}; objectMotionReactiveCutoutShader_ = {};
    objectMotionReactiveMeshPipeline_ = {};
    if (objectMotionReactiveSkinPipeline_.valid()) dev_->destroyPipeline(objectMotionReactiveSkinPipeline_);
    if (objectMotionReactiveSkinShader_.valid()) dev_->destroyShader(objectMotionReactiveSkinShader_);
    if (objectMotionReactiveBakedPipeline_.valid()) dev_->destroyPipeline(objectMotionReactiveBakedPipeline_);
    if (objectMotionReactiveBakedShader_.valid()) dev_->destroyShader(objectMotionReactiveBakedShader_);
    objectMotionReactiveSkinPipeline_ = {}; objectMotionReactiveSkinShader_ = {};
    objectMotionReactiveBakedPipeline_ = {}; objectMotionReactiveBakedShader_ = {};
    motionReactiveDraws_.clear();
    shutdownModels();
    lights.shutdown();
    lights.setTargetRetirement(nullptr);
    shutdownTrace();
    retireRenderTargets(shadowMap_);
    shadowMap_={};
    if(lineVB_.valid())dev_->destroyBuffer(lineVB_);
    if(triVB_.valid())dev_->destroyBuffer(triVB_);
    lineVB_=triVB_={};
    retireRenderTargets(sceneSelectionMask_);
    sceneSelectionMask_={};
    if (sceneSelectionTriangle_.valid()) dev_->destroyBuffer(sceneSelectionTriangle_);
    sceneSelectionTriangle_={};
    if (sceneSelectionPrimitiveMaskPipe_.valid())
        dev_->destroyPipeline(sceneSelectionPrimitiveMaskPipe_);
    sceneSelectionPrimitiveMaskPipe_={};
    for (auto p : {sceneSelectionEdgePipe8_, sceneSelectionEdgePipeHdr_})
        if (p.valid()) dev_->destroyPipeline(p);
    sceneSelectionEdgePipe8_=sceneSelectionEdgePipeHdr_={};
    if (sceneSelectionEdgeShader_.valid())
        dev_->destroyShader(sceneSelectionEdgeShader_);
    sceneSelectionEdgeShader_={};
    for (auto p : {litPipe_, litBlendPipe_, shadowPipe_, linePipe_, triPipe_})
        if(p.valid())dev_->destroyPipeline(p);
    for (auto s : {litShader_, shadowShader_, colorShader_})
        if(s.valid())dev_->destroyShader(s);
    litPipe_=litBlendPipe_=shadowPipe_=linePipe_=triPipe_={};
    litShader_=shadowShader_=colorShader_={};
    shadowSize_=0;
    if(targetRetirement_ && !targetRetirement_->drain())
        throw std::runtime_error(
            "Renderer3D shutdown: pending GPU render target completion");
    targetRetirement_.reset();
    programRetirement_.reset();
    dev_ = nullptr;
}

// Static cache entries hold borrowed Model/Primitive pointers only. Remove
// them before the owner unloads an asset or reuses a persistent draw identity.
// These methods must be called on the renderer's submission thread, between
// drawModel calls; they do not cancel items already queued this frame.
void Renderer3D::invalidateStaticModelCache(uint64_t persistentId) {
    if (!persistentId) return;
    if (staticModelCaches_.erase(persistentId))
        ++staticModelCacheStats_.evicted;
    auto it = staticRenderPackets_.find(persistentId);
    if (it != staticRenderPackets_.end()) {
        staticRenderPacketBytes_ -= staticPacketBytes(it->second);
        staticRenderPackets_.erase(it);
        ++staticRenderPacketStats_.evicted;
    }
}

void Renderer3D::invalidateSharedMaterialState(
    const Model* model, uint64_t materialOwnerKey) {
    if (!model || !materialOwnerKey) return;
    auto it = staticMaterialOwners_.find(MaterialOwnerKey{model, materialOwnerKey});
    if (it == staticMaterialOwners_.end()) return;
    staticMaterialOwnerBytes_ -= it->second.hashes.size() * sizeof(uint64_t);
    staticMaterialOwners_.erase(it);
    ++sharedMaterialStateStats_.evicted;
}

void Renderer3D::invalidateModelCachesFor(const Model* model) {
    if (!model) return;
    for (auto it = staticMaterialOwners_.begin(); it != staticMaterialOwners_.end();) {
        if (it->first.model == model) {
            staticMaterialOwnerBytes_ -= it->second.hashes.size() * sizeof(uint64_t);
            it = staticMaterialOwners_.erase(it);
            ++sharedMaterialStateStats_.evicted;
        } else ++it;
    }
    if (staticModelEligibility_.erase(model))
        ++staticEligibilityStats_.evicted;
    // Compare pointer identity only, never dereference a model being released.
    for (auto it = staticModelCaches_.begin(); it != staticModelCaches_.end();) {
        if (it->second.cache.model == model) {
            it = staticModelCaches_.erase(it);
            ++staticModelCacheStats_.evicted;
        } else ++it;
    }
    for (auto it = staticRenderPackets_.begin(); it != staticRenderPackets_.end();) {
        if (it->second.draw.model == model) {
            staticRenderPacketBytes_ -= staticPacketBytes(it->second);
            it = staticRenderPackets_.erase(it);
            ++staticRenderPacketStats_.evicted;
        } else ++it;
    }
    // A mixed-model command layout borrows every source Model pointer.
    // Asset unload must not leave any of those pointers in a layout cache.
    retireIndirectLayoutsFor(model);
}

void Renderer3D::beginScene(const RTSCamera& cam, int fbW, int fbH) {
    if(dev_)checkGpuDevice(*dev_);
    // Query backend-confirmed GPU completion before allocating new targets;
    // this is nonblocking and never guesses an N-frame CPU delay.
    if (dev_) collectCompletedObjectMotion(dev_->gpuCompletedFence());
    static const bool enablePersistentScene = [] {
        const char* value = std::getenv("AIVERSE_PERSISTENT_GPU_SCENE");
        return value && std::strcmp(value, "1") == 0;
    }();
    persistentSceneEnabled_ = enablePersistentScene;
    if (persistentSceneEnabled_) ++persistentSceneEpoch_;
    static const bool enableCpuStaticCache = [] {
        const char* value = std::getenv("AIVERSE_PERSISTENT_CPU_MODEL_CACHE");
        return value && std::strcmp(value, "1") == 0;
    }();
    staticModelCacheEnabled_ = enableCpuStaticCache;
    staticModelCacheStats_ = {};
    static const bool enablePackets = [] {
        const char* value = std::getenv("AIVERSE_PERSISTENT_CPU_PACKETS");
        return value && std::strcmp(value, "1") == 0;
    }();
    staticRenderPacketsEnabled_ = enablePackets;
    // A separate opt-in for pooled static geometry sorting. It uses existing
    // CPU MDI submission and honors the global MDI disable/safety flags.
    static const bool enableStaticCrossModelBatches = [] {
        const char* value = std::getenv("AIVERSE_STATIC_CROSS_MODEL_BATCHES");
        return value && std::strcmp(value, "1") == 0;
    }();
    // Studio may explicitly opt a single Scene viewport in/out without
    // changing the process env or the Game view. Re-evaluate at each
    // beginScene (the view boundary), not during individual draw calls.
    staticCrossModelBatchesEnabled_ =
        staticCrossModelBatchMode_ == StaticCrossModelBatchMode::Enable ||
        (staticCrossModelBatchMode_ == StaticCrossModelBatchMode::Environment &&
         enableStaticCrossModelBatches);
    // Every policy is resolved at the view/scene boundary, not from the
    // inner draw loop. The defaults exactly preserve the old env semantics.
    static const bool envLayoutCacheEnabled = [] {
        const char* flag = std::getenv("AIVERSE_MDI_LAYOUT_CACHE");
        return !flag || (std::strcmp(flag, "off") != 0 &&
                         std::strcmp(flag, "0") != 0);
    }();
    static const bool envGeometrySortEnabled = [] {
        const char* flag = std::getenv("AIVERSE_MDI_GEOMETRY_SORT");
        return flag && std::strcmp(flag, "1") == 0;
    }();
    mdiLayoutCacheEnabled_ =
        mdiLayoutCacheMode_ == CpuMdiPolicyMode::Enable ||
        (mdiLayoutCacheMode_ == CpuMdiPolicyMode::Environment && envLayoutCacheEnabled);
    mdiGeometrySortRequested_ =
        mdiGeometrySortMode_ == CpuMdiPolicyMode::Enable ||
        (mdiGeometrySortMode_ == CpuMdiPolicyMode::Environment &&
         envGeometrySortEnabled);
    // Adaptive feedback affects only the Stage 17 static pooled sort, not
    // ordinary CPU MDI or the separate GPU-authored ICB path.
    static const bool enableCpuMdiAdaptive = [] {
        const char* value = std::getenv("AIVERSE_CPU_MDI_ADAPTIVE");
        return value && std::strcmp(value, "1") == 0;
    }();
    static const double cpuMdiMaxUsPerCommand = [] {
        const char* value = std::getenv("AIVERSE_CPU_MDI_MAX_US_PER_COMMAND");
        if (!value || !*value) return 25.0; // conservative, tunable experiment
        char* end = nullptr;
        const double parsed = std::strtod(value, &end);
        return end != value && *end == '\0' &&
            std::isfinite(parsed) && parsed >= 1.0 && parsed <= 1000.0
            ? parsed : 25.0;
    }();
    cpuMdiAdaptiveEnabled_ = staticCrossModelBatchesEnabled_ &&
        (cpuMdiAdaptiveMode_ == CpuMdiPolicyMode::Enable ||
         (cpuMdiAdaptiveMode_ == CpuMdiPolicyMode::Environment &&
          enableCpuMdiAdaptive));
    cpuMdiAdaptiveBudgetUs_ = cpuMdiAdaptiveBudgetOverrideUs_ > 0.0
        ? cpuMdiAdaptiveBudgetOverrideUs_ : cpuMdiMaxUsPerCommand;
    cpuMdiPageStats_ = {};
    if (cpuMdiAdaptiveEnabled_) {
        ++cpuMdiPageEpoch_;
        // Track shared pages only while their allocation is live/recent.
        // Weak lifetime prevents address-reuse from inheriting old costs.
        if (cpuMdiPageEpoch_ % 64u == 0u) {
            for (auto it = cpuMdiPages_.begin(); it != cpuMdiPages_.end();) {
                if (it->second.lifetime.expired() ||
                    (cpuMdiPageEpoch_ > it->second.seen &&
                     cpuMdiPageEpoch_ - it->second.seen > 128u)) {
                    it = cpuMdiPages_.erase(it);
                    ++cpuMdiPageStats_.evicted;
                } else ++it;
            }
        }
    }
    staticRenderPacketStats_ = {};
    staticEligibilityStats_ = {};
    sharedMaterialStateStats_ = {};
    if (staticRenderPacketsEnabled_) {
        ++staticMaterialOwnerEpoch_;
        if ((staticMaterialOwnerEpoch_ % 64u) == 0u) {
            for (auto it = staticMaterialOwners_.begin();
                 it != staticMaterialOwners_.end();) {
                if (staticMaterialOwnerEpoch_ > it->second.seen + 128u) {
                    staticMaterialOwnerBytes_ -= it->second.hashes.size() * sizeof(uint64_t);
                    it = staticMaterialOwners_.erase(it);
                    ++sharedMaterialStateStats_.evicted;
                } else ++it;
            }
        }
    }
    if (staticModelCacheEnabled_ || staticRenderPacketsEnabled_) {
        ++staticEligibilityEpoch_;
        // Memo is model-scoped, not per visible entity; keep entries alive
        // across transient Scene/Game viewports and prune abandoned assets.
        if ((staticEligibilityEpoch_ % 64u) == 0u) {
            for (auto it = staticModelEligibility_.begin();
                 it != staticModelEligibility_.end();) {
                if (staticEligibilityEpoch_ > it->second.seen + 128u) {
                    it = staticModelEligibility_.erase(it);
                    ++staticEligibilityStats_.evicted;
                } else ++it;
            }
        }
    }
    if (staticRenderPacketsEnabled_) {
        ++staticRenderPacketEpoch_;
        // Bound packet templates by retiring long-unseen object identities.
        // The grace period protects alternating Scene/Game viewports.
        if ((staticRenderPacketEpoch_ % 64u) == 0u) {
            for (auto it = staticRenderPackets_.begin(); it != staticRenderPackets_.end();) {
                if (it->second.seen + 4 < staticRenderPacketEpoch_) {
                    staticRenderPacketBytes_ -= staticPacketBytes(it->second);
                    it = staticRenderPackets_.erase(it);
                    ++staticRenderPacketStats_.evicted;
                } else ++it;
            }
        }
    }
    if (staticModelCacheEnabled_) {
        ++staticModelCacheEpoch_;
        // Bounded persistent memory. Delayed removal tolerates alternating
        // Scene/Game viewports and temporary invisibility without rebuilding.
        if ((staticModelCacheEpoch_ % 64u) == 0u) {
            for (auto it = staticModelCaches_.begin(); it != staticModelCaches_.end();) {
                if (it->second.seen + 4 < staticModelCacheEpoch_) {
                    it = staticModelCaches_.erase(it);
                    ++staticModelCacheStats_.evicted;
                } else ++it;
            }
        }
    }
    bakedLightmapped_ = bakedCountLm_; bakedProbed_ = bakedCountPr_; bakedCountLm_ = bakedCountPr_ = 0;   // agent-lightbake
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if(graphEpoch_ < 0) graphEpoch_ = now;
    graphTime_ = float(now - graphEpoch_);
    sceneW_ = fbW; sceneH_ = fbH; // viewport of the main pass (screen or an offscreen target of this size)
    float aspect = fbH > 0 ? float(fbW) / float(fbH) : 1.0f;
    viewProj_ = cam.proj(aspect, dev_->info().clipDepthZeroToOne) * cam.view();
    invViewProj_ = viewProj_.inverse();
    camPos_ = cam.eye();
    camFwd_ = normalize(cam.target - camPos_);
    cam_ = cam;
    ssrThisFrame_ = false;
    reflectionPlaneValid_ = false;
    reflectionPlaneY_ = 0.f;
    items_.clear(); lines_.clear(); tris_.clear(); custom_.clear(); customOpaque_.clear(); customShadow_.clear();
    customMaterialReceiver_.clear();customSurfaceReceiver_.clear();
    lumaMaterialCapture_=lumaSurfaceCapture_=false;
    motionReactiveDraws_.clear(); // per-scene extensions register after beginScene
    modelItems_.clear(); jointData_.clear(); morphData_.clear(); modelStats_ = {};
    maintainIndirectLayoutCaches(); // prune unused layouts even in empty scenes
    lodDebugSample_ = {}; // selected instance is tracked per viewport beginScene
    // ModelItem pointers are valid only within this scene; never reuse their
    // material identity across frames, asset reloads or another viewport.
    materialStateIds_.clear();
    materialHashCandidates_.clear();
    materialRepresentatives_.clear();
    materialItemIds_.clear();
    geometryStats_ = {}; profile_ = {}; geometryMainView_ = true;
    RTSCamera shadowCamera = cam; shadowCamera.jitter = {};
    computeShadowMatrix(shadowCamera, aspect);
    geometryShadowSize_=shadowSize_;
    lights.begin(cam, aspect);
}

void Renderer3D::computeShadowMatrix(const RTSCamera& cam, float aspect) {
    // Fit an orthographic light frustum around the part of the ground the camera sees.
    float3 pts[8];
    int n = 0;
    const float w = 1000.0f, h = w / aspect;
    const float sx[4] = {0, w, 0, w}, sy[4] = {0, 0, h, h};
    for (int i = 0; i < 4; ++i) {
        float3 g;
        if (!cam.screenToGround(sx[i], sy[i], w, h, g) || length(g - cam.target) > cam.distance * 3.0f) {
            float3 o, d;
            cam.screenRay(sx[i], sy[i], w, h, o, d);
            float3 flatD = normalize(float3{d.x, 0, d.z});
            g = cam.target + flatD * (cam.distance * 1.6f);
        }
        pts[n++] = g;
        pts[n++] = g + float3{0, 5.0f, 0}; // tall objects
    }
    if (hasShadowBounds_) {
        n=0;
        for(int z=0;z<2;++z)for(int y=0;y<2;++y)for(int x=0;x<2;++x)
            pts[n++]={x?shadowBoundsMax_.x:shadowBoundsMin_.x,y?shadowBoundsMax_.y:shadowBoundsMin_.y,z?shadowBoundsMax_.z:shadowBoundsMin_.z};
    }
    const float3 center=hasShadowBounds_?(shadowBoundsMin_+shadowBoundsMax_)*.5f:cam.target;
    float3 L = normalize(lighting.sunDir);
    float3 up = std::fabs(L.y) > 0.99f ? float3{0, 0, 1} : float3{0, 1, 0};
    mat4 view = mat4::lookAt(center + L * 60.0f, center, up);
    float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f, minZ = 1e9f, maxZ = -1e9f;
    for (int i = 0; i < n; ++i) {
        float3 p = view.transformPoint(pts[i]);
        minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
        minZ = std::min(minZ, p.z); maxZ = std::max(maxZ, p.z);
    }
    // texel snapping keeps shadow edges stable while the camera pans
    float size = std::max(maxX - minX, maxY - minY) + 2.0f;
    float texel = size / float(std::max(shadowSize_, 16));
    shadowTexelWorld_ = texel;
    shadowExtent_ = size;
    float cx = std::floor((minX + maxX) * 0.5f / texel) * texel, cy = std::floor((minY + maxY) * 0.5f / texel) * texel;
    mat4 proj = mat4::ortho(cx - size * 0.5f, cx + size * 0.5f, cy - size * 0.5f, cy + size * 0.5f,
                            -maxZ - 40.0f, -minZ + 10.0f, dev_->info().clipDepthZeroToOne);
    lightViewProj_ = proj * view;
}

void Renderer3D::drawMesh(const Mesh& m, const mat4& model, float4 color, uint32_t flags, float specular, float emissive) {
    items_.push_back({&m, model, color, flags, specular, emissive});
}
void Renderer3D::drawNereidRippleMesh(const Mesh& mesh,const mat4& model,
        float4 color,float specular,float refraction,
        const std::array<float4,16>& ripples,
        uint32_t firstIndex,uint32_t indexCount,
        const std::array<float4,16>& wakeShapes,float4 wind,
        float4 hydraulics,float4 rapidColor) {
    drawMesh(mesh,model,color,kDrawNoCastShadow|kDrawWaterSurface,
             specular,refraction);
    if(!items_.empty()) {
        items_.back().waterRipples=ripples;
        items_.back().waterWakeShapes=wakeShapes;
        items_.back().waterWind=wind;
        items_.back().waterHydraulics=hydraulics;
        items_.back().waterRapidColor=rapidColor;
        items_.back().waterFirstIndex=firstIndex;
        items_.back().waterIndexCount=indexCount;
    }
}

void Renderer3D::drawSelectionMesh(const Mesh& mesh, const mat4& model,
                                   float4 tint, float4 selection) {
    drawMesh(mesh, model, tint);
    if (!items_.empty()) items_.back().selection = selection;
}


void Renderer3D::line(float3 a, float3 b, uint32_t c) { lines_.push_back({a, c}); lines_.push_back({b, c}); }
void Renderer3D::triangle(float3 a, float3 b, float3 c, uint32_t col) { tris_.push_back({a, col}); tris_.push_back({b, col}); tris_.push_back({c, col}); }

void Renderer3D::quadXZ(float3 a, float3 b, float w, uint32_t c, float y) {
    float3 d = b - a; d.y = 0;
    float len = length(d);
    if (len < 1e-5f) return;
    float3 side = float3{-d.z, 0, d.x} * (w * 0.5f / len);
    a.y = b.y = y;
    triangle(a - side, b - side, b + side, c);
    triangle(a - side, b + side, a + side, c);
}

void Renderer3D::circleXZ(float3 c, float r, uint32_t col, int seg, float y) {
    for (int i = 0; i < seg; ++i) {
        float a0 = 2 * kPi * i / seg, a1 = 2 * kPi * (i + 1) / seg;
        line({c.x + std::cos(a0) * r, y, c.z + std::sin(a0) * r}, {c.x + std::cos(a1) * r, y, c.z + std::sin(a1) * r}, col);
    }
}

void Renderer3D::uploadFrameUniforms(bool forShadow) {
    FrameUniforms f{};
    std::memcpy(f.viewProj, viewProj_.m, sizeof f.viewProj);
    std::memcpy(f.lightViewProj, lightViewProj_.m, sizeof f.lightViewProj);
    std::memcpy(f.invViewProj, invViewProj_.m, sizeof f.invViewProj);
    put3(f.sunDir, normalize(lighting.sunDir), lighting.sunIntensity);
    // trace tier: terrain shadows march the height field; SSR when the scene targets are bound this frame
    if (traceActive() && !forShadow) {
        const HeightField& h = heightField_;
        f.trace0[0] = h.originX; f.trace0[1] = h.originZ;
        f.trace0[2] = 1.0f / std::max(h.extentX, 1e-3f); f.trace0[3] = 1.0f / std::max(h.extentZ, 1e-3f);
        f.trace1[0] = h.heightMin; f.trace1[1] = h.heightRange;
        f.trace1[2] = std::max(lighting.terrainShadowDistance, 2.0f);
        f.trace1[3] = std::max(lighting.terrainShadowSoftness, 0.005f);
    }
    put3(f.trace2, camFwd_, ssrThisFrame_ && !forShadow ? (modelReflections_ ? 2.0f : 1.0f) : 0.0f);
    f.trace3[0] = 48.0f;    // SSR march distance (world)
    f.trace3[1] = 0.6f;     // thickness: a ray passing further behind a surface is a miss (thin objects)
    f.trace3[2] = 16.0f;    // steps (+4 refinement)
    f.trace3[3] = 0.08f;    // terrain shadow ray bias above the receiver (avoids self-shadowing on slopes)
    put3(f.sunColor, lighting.sunColor, 1);
    put3(f.skyColor, lighting.skyColor, lighting.tonemap ? 1.0f : 0.0f);   // w: tonemap switch of the lit shaders
    put3(f.groundColor, lighting.groundColor, lighting.srgbModels ? 2.0f : 1.0f);   // w: model output encoding (2 = sRGB)
    put3(f.fog, lighting.fogColor, lighting.fogDensity);
    put3(f.camPos, camPos_, graphTime_);
    const bool zeroToOne = dev_->info().clipDepthZeroToOne;
    f.shadow[0] = dev_->info().renderTargetFlipY ? 1.0f : 0.0f;
    f.shadow[1] = zeroToOne ? 1.0f : 0.5f;
    f.shadow[2] = zeroToOne ? 0.0f : 0.5f;
    f.shadow[3] = 1.0f / float(std::max(shadowSize_, 16));
    // More PCF samples preserve fine shadow silhouettes in wide penumbrae.
    // Low stays at four taps; medium/high retain the nine-sample blocker search.
    f.quality[0] = quality_ == ShadowQuality::High ? 24.0f : quality_ == ShadowQuality::Medium ? 12.0f : 4.0f;
    {
        // Maximum search/filter radius; PCSS can narrow contacts to half a texel.
        float radiusWorld = std::max(lighting.shadowSoftness, shadowTexelWorld_ * 1.5f);
        // Low (4 fixed taps): a wide radius would show 4 offset copies, keep it around two texels
        if (quality_ == ShadowQuality::Low) radiusWorld = std::min(radiusWorld, shadowTexelWorld_ * 2.0f);
        f.quality[2] = radiusWorld / std::max(shadowExtent_, 1.0f);
        // Keep receiver displacement tied to resolution, not penumbra width: thin folds must not detach.
        f.quality[3] = shadowTexelWorld_ * 1.5f;
    }
    // Fractional tap count encodes temporal PCSS sampling without changing U1 layout.
    // This flag is active only for the HDR ScreenLighting TAA path; static PCF stays stable elsewhere.
    if (!forShadow && quality_ >= ShadowQuality::Medium && post_ && post_->temporalShadowResolve())
        f.quality[0] += 0.5f;
    f.quality[1] = quality_ == ShadowQuality::Off || forShadow ? 0.0f : lighting.shadowContactHardening ? 2.0f : 1.0f;
    dev_->setUniforms(1, &f, sizeof f);
}

void Renderer3D::bindLitFrame() {
    uploadFrameUniforms(false);
    dev_->setTexture(0, shadowMap_);
    lights.bind();
}

void Renderer3D::bindTraceTextures() {
    bindTrace(3);
    bindSceneTextures(4, 5);
}

Renderer3D::ResidentMemoryStats Renderer3D::residentMemoryStats() const {
    ResidentMemoryStats out;
    if (!dev_) return out;
    std::set<uint32_t> seenTextures, seenBuffers;
    auto bytesOfTexture=[](const gfx::TextureInfo& info) -> uint64_t {
        if (info.width<=0 || info.height<=0) return 0;
        uint64_t bpp=0;
        switch (info.format) {
        case gfx::PixelFormat::RGBA16F: bpp=8; break;
        case gfx::PixelFormat::RGBA32F: bpp=16; break;
        case gfx::PixelFormat::R8: bpp=1; break;
        case gfx::PixelFormat::RGBA8:
        case gfx::PixelFormat::RGBA8_sRGB:
        case gfx::PixelFormat::Depth: bpp=4; break;
        default: break; // block-compressed formats need exact mip metadata
        }
        return uint64_t(info.width)*uint64_t(info.height)*bpp;
    };
    auto addTexture=[&](gfx::TextureHandle h,uint64_t* category) {
        if (!h.valid() || !seenTextures.insert(h.id).second) return;
        ++out.liveTextures;
        gfx::TextureInfo info{};
        if (!dev_->textureInfo(h,info) || info.width<=0 || info.height<=0) {
            ++out.unknownTextures; return;
        }
        const uint64_t bytes=bytesOfTexture(info);
        if (!bytes) { ++out.unknownTextures;return; }
        out.liveTextureKnownBytes+=bytes;
        if (category) *category+=bytes;
    };
    auto addBuffer=[&](gfx::BufferHandle h) {
        if (!h.valid() || !seenBuffers.insert(h.id).second) return;
        ++out.liveBuffers;
        gfx::BufferInfo info{};
        if (!dev_->bufferInfo(h,info) || !info.logicalBytes) {
            ++out.unknownBuffers;return;
        }
        out.liveBufferKnownBytes+=uint64_t(info.logicalBytes);
        if (info.versions>1) ++out.multiVersionBuffers;
    };
    // Renderer-owned scene/render targets (NOT shared by Prisma).
    addTexture(shadowMap_,nullptr);
    addTexture(sceneSelectionMask_,nullptr);
    addTexture(dummyDepth_,nullptr);
    addTexture(dummyHeight_,nullptr);
    addTexture(white_,nullptr);
    addTexture(black_,nullptr);
    addTexture(flatNormal_,nullptr);
    for (const auto& size : sceneTargets_) {
        addTexture(size.color,&out.sceneCaptureKnownBytes);
        addTexture(size.depth,&out.sceneCaptureKnownBytes);
    }
    // Aliases of sceneTargets_ are intentionally NOT counted again:
    // sceneColor_ and sceneDepth_ reference handles in that cache.
    // Motion targets and reactive masks are private to one view.
    for (const auto& [viewId, view] : objectMotionViews_) {
        (void)viewId;
        addTexture(view.target,&out.objectMotionKnownBytes);
        addTexture(view.depth,&out.objectMotionKnownBytes);
        addTexture(view.reactive,&out.objectMotionKnownBytes);
    }
    addBuffer(lineVB_);
    addBuffer(triVB_);
    addBuffer(sceneSelectionTriangle_);
    addBuffer(blitVB_);
    addBuffer(indirectCommands_);
    out.pendingMotionKnownBytes=motionRetirementStats().knownPendingBytes;
    // Add each mutually exclusive owner exactly once. These owners allocate
    // RHI handles directly; they never include external Model/Material
    // textures/buffers and do not borrow Renderer3D's core handles.
    const auto addOwner=[&](const GpuOwnedMemoryStats& owned,
                            uint64_t& category) {
        category=owned.knownBytes();
        out.liveTextureKnownBytes=GpuOwnedMemoryCounter::saturatingAdd(
            out.liveTextureKnownBytes,owned.textureKnownBytes);
        out.liveBufferKnownBytes=GpuOwnedMemoryCounter::saturatingAdd(
            out.liveBufferKnownBytes,owned.bufferKnownBytes);
        out.liveTextures+=owned.textures;
        out.liveBuffers+=owned.buffers;
        out.unknownTextures+=owned.unknownTextures;
        out.unknownBuffers+=owned.unknownBuffers;
        out.multiVersionBuffers+=owned.multiVersionBuffers;
    };
    addOwner(lights.ownedGpuMemoryStats(),out.lightSystemKnownBytes);
    if (persistentUpload_)
        addOwner(persistentUpload_->ownedGpuMemoryStats(),
                 out.persistentUploadKnownBytes);
    addOwner(persistentCull_.ownedGpuMemoryStats(*dev_),
             out.persistentCullKnownBytes);
    addOwner(gpuModelCommands_.ownedGpuMemoryStats(*dev_),
             out.gpuAuthoredKnownBytes);
    // Exclusions: external asset Model meshes/textures, geometry upload
    // pools not directly owned by this renderer, transient Metal/GL
    // driver allocations, pipelines/shaders, and PRISMA-owned targets.
    // RHI metadata omits mip/physical alignment: this is a LOWER BOUND.
    return out;
}

Renderer3D::MotionRetirementStats Renderer3D::motionRetirementStats() const {
    MotionRetirementStats stats;
    stats.liveViews = objectMotionViews_.size();
    stats.fencedTextures = motionRetired_.pendingCount();
    stats.unfencedTextures = motionUnfenced_.size();
    stats.lastGpuCompletedFence = dev_ ? dev_->gpuCompletedFence() : 0;
    auto bytesOf = [this](gfx::TextureHandle handle) -> uint64_t {
        if (!dev_ || !handle.valid()) return 0;
        gfx::TextureInfo t{};
        if (!dev_->textureInfo(handle,t) || t.width <= 0 || t.height <= 0)
            return 0;
        const uint64_t bpp =
            t.format == gfx::PixelFormat::RGBA16F ? 8 :
            t.format == gfx::PixelFormat::RGBA32F ? 16 :
            t.format == gfx::PixelFormat::R8 ? 1 : 4;
        return uint64_t(t.width) * uint64_t(t.height) * bpp;
    };
    for (const auto& p : motionRetired_.pending())
        stats.knownPendingBytes += bytesOf(p.handle);
    for (const auto& h : motionUnfenced_)
        stats.knownPendingBytes += bytesOf(h);
    return stats; // lower bound when RHI cannot describe an allocation
}

void Renderer3D::queueObjectMotionTexture(
    gfx::TextureHandle& texture, uint64_t lastSubmittedFence) {
    if (!texture.valid()) return;
    // Prefer a token from the live backend's own CURRENT GPU submission.
    // This frame's fence covers all previous submissions on its graphics
    // queue, including work referencing the stale texture.
    const uint64_t currentGpuFence = dev_ ? dev_->gpuSubmissionFence() : 0;
    if (currentGpuFence) lastSubmittedFence = currentGpuFence;
    if (lastSubmittedFence)
        motionRetired_.retire(texture,lastSubmittedFence);
    else
        motionUnfenced_.push_back(texture); // not safe to release yet
    texture = {};
}

void Renderer3D::retireObjectMotionView(
    uint64_t viewId, uint64_t lastSubmittedFence) {
    auto it = objectMotionViews_.find(viewId);
    if (it == objectMotionViews_.end()) return;
    // The caller must provide a backend-submitted fence token that covers
    // ALL work referencing this view. A zero token quarantines until GPU idle.
    queueObjectMotionTexture(it->second.target,lastSubmittedFence);
    queueObjectMotionTexture(it->second.depth,lastSubmittedFence);
    queueObjectMotionTexture(it->second.reactive,lastSubmittedFence);
    objectMotionViews_.erase(it);
    if (objectMotionStats_.viewId == viewId) objectMotionStats_ = {};
}

size_t Renderer3D::retireUnregisteredObjectMotionViews(
    const std::vector<uint64_t>& liveViewIds) {
    // Explicit per-host roster; do not infer view liveness from dimensions
    // or a CPU frame timestamp. Copy IDs before erasing map entries.
    const std::set<uint64_t> live(liveViewIds.begin(), liveViewIds.end());
    std::vector<uint64_t> orphanIds;
    orphanIds.reserve(objectMotionViews_.size());
    for (const auto& [id, history] : objectMotionViews_) {
        (void)history;
        if (!live.contains(id)) orphanIds.push_back(id);
    }
    const uint64_t submission = dev_ ? dev_->gpuSubmissionFence() : 0;
    for (const uint64_t id : orphanIds)
        retireObjectMotionView(id, submission);
    return orphanIds.size();
}

size_t Renderer3D::collectCompletedObjectMotion(
    uint64_t completedGpuFence) {
    if (!dev_ || !completedGpuFence) return 0;
    // completedGpuFence MUST come from an actual signaled backend fence.
    return motionRetired_.collect(completedGpuFence,
        [this](gfx::TextureHandle h) { dev_->destroyTexture(h); });
}

size_t Renderer3D::drainObjectMotionAfterGpuIdle() {
    if (!dev_) return 0;
    size_t removed = motionRetired_.drainAfterGpuIdle(
        [this](gfx::TextureHandle h) { dev_->destroyTexture(h); });
    for (auto h : motionUnfenced_)
        if (h.valid()) { dev_->destroyTexture(h); ++removed; }
    motionUnfenced_.clear();
    return removed;
}

void Renderer3D::resetObjectMotionHistory(uint64_t viewId) {
    auto it = objectMotionViews_.find(viewId);
    if (it == objectMotionViews_.end()) return;
    it->second.previous.clear();
    it->second.previousDeformed.clear();
    it->second.valid = false;
}
void Renderer3D::resetAllObjectMotionHistory() {
    for (auto& [id, view] : objectMotionViews_) {
        view.previous.clear();
        view.previousDeformed.clear();
        view.valid = false;
    }
}

void Renderer3D::releaseObjectMotionViewAfterGpuIdle(uint64_t viewId) {
    // The caller must have waited for all submissions referencing the view.
    // We cannot infer this from CPU frameSerial() or viewport close events.
    auto it = objectMotionViews_.find(viewId);
    if (it == objectMotionViews_.end()) return;
    if (dev_) {
        if (it->second.target.valid()) dev_->destroyTexture(it->second.target);
        if (it->second.depth.valid()) dev_->destroyTexture(it->second.depth);
        if (it->second.reactive.valid()) dev_->destroyTexture(it->second.reactive);
    }
    objectMotionViews_.erase(it);
    if (objectMotionStats_.viewId == viewId) objectMotionStats_ = {};
}

// Current-pose coverage is independent of previous transforms and RGBA16F
// velocity support. In particular, it must still run after a camera cut,
// initial frame, or viewport resize, provided the scene depth is readable.
void Renderer3D::renderCurrentFrameReactiveMask(
    PostProcess& post, ObjectMotionHistory& history,
    gfx::TextureHandle sceneDepth, int w, int h) {
    if (!dev_ || !sceneDepth.valid()) return;
    struct MotionU { float current[16], previous[16], params[4]; };
    // Conservative translucent coverage: surfaces whose true motion
    // cannot be reconstructed suppress blur locally via a reactive mask.
    // No previous-pose approximation, no readback, no ECS state changes.
    auto reactiveEligible = [&](const ModelItem& it) {
        if (!it.inView || !it.model || !it.prim || !it.mat ||
            !it.model->vb.valid() || !it.model->ib.valid() ||
            it.indexCount == 0 || !it.rigid || it.joints >= 0 ||
            it.baked || it.morph >= 0 || it.model->geometry ||
            it.mat->shaderGraph || it.materialRole == 1 ||
            it.shadowRole == 2) return false;
        const bool translucent = it.tint.w < 0.99f ||
            it.mat->transparent || it.mat->alpha == AlphaMode::Blend ||
            (it.flags & kDrawTransparent) != 0;
        // Fade parts are skipped by the velocity overlay until their
        // coverage is reproduced. Mark only actual dithered fragments
        // reactive so these LOD boundaries cannot smear the background.
        const bool fade = (it.lodThreshold < 1.0f || it.lodInvert) &&
            it.mat->alpha == AlphaMode::Opaque;
        // Alpha-mask silhouettes (foliage/grilles) require precise material
        // texture cutout; dissolving objects require object-space noise.
        const bool cutout = it.mat->alpha == AlphaMode::Mask;
        const bool reveal = geometryMainView_ && actorReveal.w > 0.0f &&
            (it.flags & kDrawActorReveal);
        return translucent || fade || cutout || it.dissolve > 0.0f || reveal;
    };
    auto reactiveAnimatedEligible = [&](const ModelItem& it) {
        if (!it.inView || !it.model || !it.prim || !it.mat ||
            !it.model->vb.valid() || !it.model->ib.valid() ||
            !it.indexCount || it.model->geometry ||
            it.materialRole == 1 || it.shadowRole == 2 ||
            it.mat->shaderGraph) return false;
        const bool deform = it.joints >= 0 || it.baked ||
            (it.prim->morphTargets > 0 && it.model->morphTex.valid());
        const bool translucent = it.tint.w < 0.99f ||
            it.mat->transparent || it.mat->alpha == AlphaMode::Blend ||
            (it.flags & kDrawTransparent) != 0;
        // Current-pose reactive silhouettes also protect animated LOD
        // fades and dissolving characters. LOD dither is reproduced in
        // the reactive shader; dissolve is conservative for animated
        // vertex deformation (the exact noise lives in ModelShaders).
        const bool fade = (it.lodThreshold < 1.0f || it.lodInvert) &&
            it.mat->alpha == AlphaMode::Opaque;
        const bool dissolve = it.dissolve > 0.0f && it.dissolve < 1.0f;
        const bool reveal = geometryMainView_ && actorReveal.w > 0.0f &&
            (it.flags & kDrawActorReveal);
        const bool cutout = it.mat->alpha == AlphaMode::Mask;
        return deform && (translucent || fade || dissolve || reveal || cutout) &&
            (!it.baked || it.model->baked.tex.valid());
    };
    // Water sheets and other immediate MeshVertex draws use DrawItem
    // rather than ModelItem. Their opacity is part of DrawItem::color;
    // the reactive pass reuses those same GPU buffers and transforms.
    const auto meshReactiveEligible = [](const DrawItem& item) {
        return item.mesh && item.mesh->vb.valid() &&
            item.mesh->ib.valid() && item.mesh->indexCount &&
            item.color.w > 0.001f && item.color.w < 0.99f;
    };
    bool meshReactiveNeeded = false;
    for (const auto& item : items_)
        meshReactiveNeeded |= meshReactiveEligible(item);
    bool rigidReactiveNeeded = false, animatedReactiveNeeded = false;
    bool cutoutReactiveNeeded = false, simpleRigidReactiveNeeded = false;
    bool animatedBakedNeeded = false, animatedSkinNeeded = false;
    for (const auto& it : modelItems_) {
        const bool rigid = reactiveEligible(it);
        rigidReactiveNeeded |= rigid;
        const bool detailed = rigid &&
            (it.mat->alpha == AlphaMode::Mask || it.dissolve > 0.0f ||
             (geometryMainView_ && actorReveal.w > 0.0f &&
              (it.flags & kDrawActorReveal)));
        cutoutReactiveNeeded |= detailed;
        simpleRigidReactiveNeeded |= rigid && !detailed;
        const bool animated = reactiveAnimatedEligible(it);
        animatedReactiveNeeded |= animated;
        if (animated) {
            animatedBakedNeeded |= it.baked;
            animatedSkinNeeded |= !it.baked;
        }
    }
    const bool reactiveNeeded = rigidReactiveNeeded ||
        animatedReactiveNeeded || meshReactiveNeeded ||
        !motionReactiveDraws_.empty();
    // A reactive-only first frame/cut needs its own private scratch
    // depth even when no RGBA16F velocity target can be prepared.
    if (reactiveNeeded && !history.depth.valid()) {
        gfx::TextureDesc scratch{};
        scratch.width = w; scratch.height = h;
        scratch.renderTarget = true;
        scratch.format = gfx::PixelFormat::Depth;
        scratch.filter = gfx::Filter::Nearest;
        scratch.label = "prisma reactive scratch depth";
        history.depth = dev_->createTexture(scratch);
    }
    if (reactiveNeeded && !history.reactive.valid()) {
        gfx::TextureDesc target;
        target.width = w; target.height = h;
        target.renderTarget = true;
        target.format = gfx::PixelFormat::RGBA8;
        target.filter = gfx::Filter::Nearest;
        target.label = "prisma motion reactive mask";
        history.reactive = dev_->createTexture(target);
    }
    if ((rigidReactiveNeeded || meshReactiveNeeded) && !objectMotionReactiveShader_.valid()) {
        gfx::ShaderDesc shader;
        shader.glslVertex = reactive_shaders::kVS;
        shader.glslFragment = reactive_shaders::kFS;
        shader.msl = reactive_shaders::kMSL;
        shader.textureSlots = 1;
        shader.uniformSlots = 1;
        shader.label = "prisma motion reactive coverage";
        objectMotionReactiveShader_ = dev_->createShader(shader);
        if (objectMotionReactiveShader_.valid()) {
            gfx::PipelineDesc pipeline;
            pipeline.shader = objectMotionReactiveShader_;
            pipeline.attrs[0] = {0,gfx::VertexFormat::Float3,0};
            pipeline.attrCount = 1;
            pipeline.stride = sizeof(ModelVertex);
            pipeline.indexType = gfx::IndexType::UInt32;
            pipeline.cull = gfx::CullMode::Back;
            pipeline.depthTest = false;
            pipeline.depthWrite = false;
            pipeline.colorFormat = gfx::PixelFormat::RGBA8;
            pipeline.label = "prisma motion reactive coverage";
            objectMotionReactivePipeline_ = dev_->createPipeline(pipeline);
        }
    }
    // Dedicated rigid pass recreates the SAME material alpha cutout and
    // object-space dissolve discard as normal ModelShaders. The simpler
    // untextured shader remains for water and ordinary blended materials.
    if (cutoutReactiveNeeded && !objectMotionReactiveCutoutShader_.valid()) {
        gfx::ShaderDesc shader{};
        shader.glslVertex = reactive_shaders::kCutoutVS;
        shader.glslFragment = reactive_shaders::kCutoutFS;
        shader.msl = reactive_shaders::kCutoutMSL;
        shader.textureSlots = 2; // TEX0 scene depth, TEX1 model base alpha
        shader.uniformSlots = 1;
        shader.label = "prisma rigid alpha/dissolve reactive coverage";
        objectMotionReactiveCutoutShader_ = dev_->createShader(shader);
        if (objectMotionReactiveCutoutShader_.valid()) {
            gfx::PipelineDesc pd{};
            pd.shader = objectMotionReactiveCutoutShader_;
            pd.attrs[0] = {0,gfx::VertexFormat::Float3,0};
            pd.attrs[1] = {2,gfx::VertexFormat::Float2,
                           uint32_t(offsetof(ModelVertex,uv))};
            pd.attrs[2] = {4,gfx::VertexFormat::UByte4N,
                           uint32_t(offsetof(ModelVertex,color))};
            pd.attrCount = 3;
            pd.stride = sizeof(ModelVertex);
            pd.indexType = gfx::IndexType::UInt32;
            pd.cull = gfx::CullMode::None; // alpha-cutout foliage may be two-sided
            pd.depthTest = false; pd.depthWrite = false;
            pd.blend = gfx::BlendMode::Screen;
            pd.colorFormat = gfx::PixelFormat::RGBA8;
            pd.label = shader.label;
            objectMotionReactiveCutoutPipeline_ = dev_->createPipeline(pd);
        }
    }
    if (meshReactiveNeeded && !objectMotionReactiveMeshPipeline_.valid() &&
        objectMotionReactiveShader_.valid()) {
        gfx::PipelineDesc pipeline;
        pipeline.shader = objectMotionReactiveShader_;
        pipeline.attrs[0] = {0,gfx::VertexFormat::Float3,0};
        pipeline.attrCount = 1;
        pipeline.stride = sizeof(MeshVertex);
        pipeline.indexType = gfx::IndexType::UInt32;
        pipeline.cull = gfx::CullMode::None; // water sheets are two-sided
        pipeline.depthTest = false;
        pipeline.depthWrite = false;
        pipeline.blend = gfx::BlendMode::Screen;
        pipeline.colorFormat = gfx::PixelFormat::RGBA8;
        pipeline.label = "prisma mesh water reactive coverage";
        objectMotionReactiveMeshPipeline_ = dev_->createPipeline(pipeline);
    }
    // Deformable translucent models use the same GPU current-pose
    // vertex deformation as their motion shaders, but a coverage-only
    // fragment. The Metal fragment is swapped at shader creation time.
    auto prepareAnimatedReactive = [&](bool baked) {
        auto& shaderHandle = baked ? objectMotionReactiveBakedShader_
                                   : objectMotionReactiveSkinShader_;
        auto& pipelineHandle = baked ? objectMotionReactiveBakedPipeline_
                                     : objectMotionReactiveSkinPipeline_;
        if (shaderHandle.valid()) return;
        const char* vertex = baked ? motion_shaders::kBakedVS :
                                       motion_shaders::kDeformedVS;
        const char* vertexMsl = baked ? motion_shaders::kBakedMSL :
                                          motion_shaders::kDeformedMSL;
        const std::string glsl = reactive_shaders::animatedVertexGlsl(vertex,baked);
        const std::string msl = reactive_shaders::animatedMsl(vertexMsl,baked);
        if (glsl.empty() || msl.empty()) return; // avoid stale/unbound varyings
        gfx::ShaderDesc shader;
        shader.glslVertex = glsl.c_str();
        shader.glslFragment = reactive_shaders::kAnimatedFS;
        shader.msl = msl.c_str();
        shader.textureSlots = 9; // scene depth 0 + morph atlas 8 / baked 7
        shader.uniformSlots = 6;
        shader.label = baked ? "prisma animated baked reactive" :
                               "prisma animated skin reactive";
        shaderHandle = dev_->createShader(shader);
        if (!shaderHandle.valid()) return;
        gfx::PipelineDesc pipeline;
        pipeline.shader = shaderHandle;
        pipeline.attrs[0] = {0,gfx::VertexFormat::Float3,0};
        pipeline.attrs[1] = {2,gfx::VertexFormat::Float2,
                             uint32_t(offsetof(ModelVertex,uv))};
        pipeline.attrs[2] = {4,gfx::VertexFormat::UByte4N,
                             uint32_t(offsetof(ModelVertex,color))};
        pipeline.attrs[3] = {5,gfx::VertexFormat::UByte4,
                             uint32_t(offsetof(ModelVertex,joints))};
        pipeline.attrs[4] = {6,gfx::VertexFormat::UByte4N,
                             uint32_t(offsetof(ModelVertex,weights))};
        pipeline.attrCount = 5;
        pipeline.stride = sizeof(ModelVertex);
        pipeline.indexType = gfx::IndexType::UInt32;
        pipeline.cull = gfx::CullMode::Back;
        pipeline.depthTest = false;
        pipeline.depthWrite = false;
        pipeline.blend = gfx::BlendMode::Screen;
        pipeline.colorFormat = gfx::PixelFormat::RGBA8;
        pipeline.label = shader.label;
        pipelineHandle = dev_->createPipeline(pipeline);
    };
    if (animatedReactiveNeeded) {
        if (animatedBakedNeeded) prepareAnimatedReactive(true);
        if (animatedSkinNeeded) prepareAnimatedReactive(false);
    }
    // Check the pipeline for the ACTUAL provider type. A failed cutout
    // shader must not publish an empty mask merely because the simple
    // water/blend pipeline happened to be prepared.
    const bool anyReactiveProviderReady =
        (simpleRigidReactiveNeeded && objectMotionReactivePipeline_.valid()) ||
        (cutoutReactiveNeeded && objectMotionReactiveCutoutPipeline_.valid()) ||
        (animatedSkinNeeded && objectMotionReactiveSkinPipeline_.valid()) ||
        (animatedBakedNeeded && objectMotionReactiveBakedPipeline_.valid()) ||
        (meshReactiveNeeded && objectMotionReactiveMeshPipeline_.valid()) ||
        !motionReactiveDraws_.empty();
    if (reactiveNeeded && history.reactive.valid() &&
        history.depth.valid() && sceneDepth.valid() && anyReactiveProviderReady) {
        struct ReactiveUniforms {
            float mvp[16], params[4], lod[4];
        };
        static_assert(sizeof(ReactiveUniforms) == 96);
        struct ReactiveCutoutUniforms {
            float mvp[16], params[4], lod[4], uv0[4], uv1[4], maskInfo[4];
            float world[16], reveal[4], revealAxis[4];
        };
        static_assert(sizeof(ReactiveCutoutUniforms) == 240);
        // U1 for the current animated draw; U0 and U3/U4/U5 remain the
        // existing skin/morph/baked deformation ABI. No CPU mesh readback.
        struct ReactiveAnimatedU1 {
            float lod[4], uv0[4], uv1[4], maskInfo[4];
            float world[16], reveal[4], revealAxis[4];
        };
        static_assert(sizeof(ReactiveAnimatedU1) == 160);
        const auto putActorReveal = [&](const ModelItem& it,
                float* world, float* reveal, float* axis) {
            std::memcpy(world,it.world.m,sizeof it.world.m);
            if (!(geometryMainView_ && actorReveal.w > 0.0f &&
                  (it.flags & kDrawActorReveal))) return;
            reveal[0]=actorReveal.x;reveal[1]=actorReveal.y;
            reveal[2]=actorReveal.z;reveal[3]=actorReveal.w;
            const float3 centre{actorReveal.x,actorReveal.y,actorReveal.z};
            const float3 direction=normalize(cam_.ortho ?
                cam_.eye()-cam_.target : cam_.eye()-centre);
            axis[0]=direction.x;axis[1]=direction.y;axis[2]=direction.z;
            axis[3]=cam_.ortho ? 0.0f :
                1.0f/std::max(0.01f,length(cam_.eye()-centre));
        };
        struct ReactiveMorphU {
            float currentW[8] = {}, previousW[8] = {};
            float info[4] = {}, info2[4] = {};
        };
        gfx::PassDesc maskPass;
        maskPass.colorTarget = history.reactive;
        maskPass.depthTarget = history.depth;
        maskPass.clearColor[0] = maskPass.clearColor[1] =
            maskPass.clearColor[2] = maskPass.clearColor[3] = 0.0f;
        maskPass.clearColorEnabled = true;
        maskPass.clearDepthEnabled = true;
        dev_->beginPass(maskPass);
        dev_->setViewport({0,0,w,h});
        dev_->setTexture(0,sceneDepth);
        for (const auto& it : modelItems_) {
            if (!reactiveEligible(it)) continue;
            const bool cutout = it.mat->alpha == AlphaMode::Mask;
            const bool dissolve = it.dissolve > 0.0f;
            const bool reveal = geometryMainView_ && actorReveal.w > 0.0f &&
                (it.flags & kDrawActorReveal);
            const bool detailed = cutout || dissolve || reveal;
            const auto pipeline = detailed ? objectMotionReactiveCutoutPipeline_ :
                                             objectMotionReactivePipeline_;
            if (!pipeline.valid()) { ++objectMotionStats_.rejected; continue; }
            dev_->setPipeline(pipeline);
            const mat4 mvp = viewProj_ * it.world;
            const bool fade = it.lodThreshold < 1.0f || it.lodInvert;
            if (fade) ++objectMotionStats_.lodReactiveDraws;
            if (detailed) {
                ReactiveCutoutUniforms u{};
                std::memcpy(u.mvp,mvp.m,sizeof u.mvp);
                u.params[0] = float(w); u.params[1] = float(h);
                u.params[2] = 1.0f;
                u.lod[0] = it.lodThreshold;
                u.lod[1] = it.lodInvert ? 1.0f : 0.0f;
                u.lod[2] = fade ? 1.0f : 0.0f;
                u.uv0[0] = it.mat->uvOffset.x;
                u.uv0[1] = it.mat->uvOffset.y;
                u.uv0[2] = it.mat->uvScale.x;
                u.uv0[3] = it.mat->uvScale.y;
                u.uv1[0] = it.mat->uvRotation;
                u.maskInfo[0] = it.mat->baseColor.w;
                // TeamMode::BaseAlpha / VertexAlpha override base.a in
                // ModelShaders; a vertex color alpha must NOT be multiplied
                // a second time for their ALPHAMASK discard decision.
                const bool overrideAlpha = it.mat->team == TeamMode::BaseAlpha ||
                                           it.mat->team == TeamMode::VertexAlpha;
                u.maskInfo[1] = it.mat->vertexColors &&
                    !it.mat->vertexOcclusion && !overrideAlpha ? 1.0f : 0.0f;
                u.maskInfo[2] = cutout ? it.mat->alphaCutoff : -1.0f;
                u.maskInfo[3] = dissolve ? std::clamp(it.dissolve,0.0f,1.0f) : 0.0f;
                putActorReveal(it,u.world,u.reveal,u.revealAxis);
                if (reveal) ++objectMotionStats_.actorRevealReactiveDraws;
                gfx::TextureHandle base = white_;
                // BaseAlpha explicitly ignores the source texture's alpha
                // in the normal material shader (only its team tint uses it).
                if (it.mat->team != TeamMode::BaseAlpha &&
                    it.mat->baseTex >= 0 &&
                    size_t(it.mat->baseTex) < it.model->images.size()) {
                    const auto t=it.model->images[size_t(it.mat->baseTex)].tex;
                    if (t.valid()) base=t;
                }
                dev_->setUniforms(0,&u,sizeof u);
                dev_->setTexture(1,base);
                ++objectMotionStats_.cutoutReactiveDraws;
                if (dissolve) ++objectMotionStats_.dissolveReactiveDraws;
            } else {
                ReactiveUniforms u{};
                std::memcpy(u.mvp,mvp.m,sizeof u.mvp);
                u.params[0] = float(w); u.params[1] = float(h);
                u.params[2] = 1.0f; // conservative coverage, not alpha parity
                u.lod[0] = it.lodThreshold;
                u.lod[1] = it.lodInvert ? 1.0f : 0.0f;
                u.lod[2] = fade ? 1.0f : 0.0f;
                dev_->setUniforms(0,&u,sizeof u);
            }
            dev_->setVertexBuffer(it.model->vb);
            dev_->setIndexBuffer(it.model->ib);
            dev_->drawIndexed(it.indexCount,
                it.firstIndex + it.model->gpuFirstIndex,
                int32_t(it.model->gpuBaseVertex));
            ++objectMotionStats_.reactiveDraws;
        }

        // Actual water/transparent immediate mesh submissions. This
        // includes RTS TerrainRenderer water surfaces, which queue
        // Renderer3D::drawMesh(water_, ..., waterColor) before render.
        // We reuse exactly the submitted mesh GPU geometry and matrix.
        if (objectMotionReactiveMeshPipeline_.valid()) {
            dev_->setPipeline(objectMotionReactiveMeshPipeline_);
            dev_->setTexture(0,sceneDepth);
            for (const DrawItem& item : items_) {
                if (!meshReactiveEligible(item)) continue;
                ReactiveUniforms uniforms{};
                const mat4 mvp = viewProj_ * item.model;
                std::memcpy(uniforms.mvp,mvp.m,sizeof uniforms.mvp);
                uniforms.params[0] = float(w);
                uniforms.params[1] = float(h);
                uniforms.params[2] = std::clamp(item.color.w,0.0f,1.0f);
                dev_->setUniforms(0,&uniforms,sizeof uniforms);
                dev_->setVertexBuffer(item.mesh->vb);
                dev_->setIndexBuffer(item.mesh->ib);
                dev_->drawIndexed(item.mesh->indexCount);
                ++objectMotionStats_.reactiveMeshDraws;
            }
        }
        // Animated translucent current-pose silhouettes. We intentionally
        // use the current pose for both vertex parameters: the fragment
        // writes coverage, NOT velocity. Thus this works without an ID.
        for (const auto& it : modelItems_) {
            if (!reactiveAnimatedEligible(it)) continue;
            const bool baked = it.baked;
            const auto pipeline = baked ? objectMotionReactiveBakedPipeline_
                                         : objectMotionReactiveSkinPipeline_;
            if (!pipeline.valid()) continue;
            const bool morph = it.prim->morphTargets > 0 &&
                               it.model->morphTex.valid();
            const bool skin = it.joints >= 0;
            if (skin &&
                (it.joints < 0 ||
                 size_t(it.joints) + size_t(kMaxPaletteJoints) * 12 >
                     jointData_.size())) {
                ++objectMotionStats_.rejected;
                continue;
            }
            if (morph && it.morph >= 0 &&
                size_t(it.morph) + size_t(kMaxMorphTargets) >
                    morphData_.size()) {
                ++objectMotionStats_.rejected;
                continue;
            }
            MotionU position{};
            const mat4 mvp = viewProj_ * it.world;
            std::memcpy(position.current,mvp.m,sizeof position.current);
            std::memcpy(position.previous,mvp.m,sizeof position.previous);
            position.params[0] = float(w);
            position.params[1] = float(h);
            position.params[2] = baked ? (morph ? 1.0f : 0.0f) :
                                         (skin ? 1.0f : 0.0f);
            position.params[3] = !baked && morph ? 1.0f : 0.0f;

            ReactiveMorphU mo{};
            if (morph) {
                if (it.morph >= 0) {
                    std::memcpy(mo.currentW,morphData_.data()+it.morph,
                                sizeof mo.currentW);
                    std::memcpy(mo.previousW,mo.currentW,sizeof mo.previousW);
                }
                mo.info[0] = float(it.prim->morphTexel);
                mo.info[1] = float(it.prim->firstVertex);
                mo.info[2] = float(it.prim->vertexCount);
                mo.info[3] = float(it.prim->morphTargets);
                mo.info2[0] = float(it.model->morphTexWidth);
            }

            dev_->setPipeline(pipeline);
            dev_->setUniforms(0,&position,sizeof position);
            const bool fade = it.lodThreshold < 1.0f || it.lodInvert;
            const bool cutout = it.mat->alpha == AlphaMode::Mask;
            const bool dissolve = it.dissolve > 0.0f;
            const bool reveal = geometryMainView_ && actorReveal.w > 0.0f &&
                (it.flags & kDrawActorReveal);
            ReactiveAnimatedU1 coverage{};
            coverage.lod[0] = it.lodThreshold;
            coverage.lod[1] = it.lodInvert ? 1.0f : 0.0f;
            coverage.lod[2] = fade ? 1.0f : 0.0f;
            coverage.uv0[0] = it.mat->uvOffset.x;
            coverage.uv0[1] = it.mat->uvOffset.y;
            coverage.uv0[2] = it.mat->uvScale.x;
            coverage.uv0[3] = it.mat->uvScale.y;
            coverage.uv1[0] = it.mat->uvRotation;
            coverage.maskInfo[0] = it.mat->baseColor.w;
            const bool overrideAlpha = it.mat->team == TeamMode::BaseAlpha ||
                                       it.mat->team == TeamMode::VertexAlpha;
            coverage.maskInfo[1] = it.mat->vertexColors &&
                !it.mat->vertexOcclusion && !overrideAlpha ? 1.0f : 0.0f;
            coverage.maskInfo[2] = cutout ? it.mat->alphaCutoff : -1.0f;
            coverage.maskInfo[3] = dissolve ?
                std::clamp(it.dissolve,0.0f,1.0f) : 0.0f;
            putActorReveal(it,coverage.world,coverage.reveal,coverage.revealAxis);
            dev_->setUniforms(1,&coverage,sizeof coverage);
            if (fade) ++objectMotionStats_.lodReactiveDraws;
            if (dissolve) ++objectMotionStats_.dissolveReactiveDraws;
            if (cutout) ++objectMotionStats_.cutoutReactiveDraws;
            if (reveal) ++objectMotionStats_.actorRevealReactiveDraws;
            dev_->setTexture(0,sceneDepth);
            gfx::TextureHandle base=white_;
            if (it.mat->team != TeamMode::BaseAlpha &&
                it.mat->baseTex >= 0 &&
                size_t(it.mat->baseTex) < it.model->images.size()) {
                const auto t=it.model->images[size_t(it.mat->baseTex)].tex;
                if (t.valid()) base=t;
            }
            dev_->setTexture(1,base);
            if (baked) {
                float palette[80] = {};
                for (size_t j=0;j<it.prim->jointPalette.size() && j<80;++j)
                    palette[j] = float(it.prim->jointPalette[j]);
                float animation[8] = {
                    it.anim.x,it.anim.y,it.anim.z,it.anim.w,
                    it.anim.x,it.anim.y,it.anim.z,it.anim.w
                };
                dev_->setUniforms(3,palette,sizeof palette);
                dev_->setUniforms(4,animation,sizeof animation);
                dev_->setUniforms(5,&mo,sizeof mo);
                dev_->setTexture(7,it.model->baked.tex);
            } else {
                float joints[size_t(kMaxPaletteJoints)*12] = {};
                if (skin)
                    std::memcpy(joints,jointData_.data()+it.joints,
                                sizeof joints);
                dev_->setUniforms(3,joints,sizeof joints);
                dev_->setUniforms(4,joints,sizeof joints);
                dev_->setUniforms(5,&mo,sizeof mo);
            }
            dev_->setTexture(8,morph ? it.model->morphTex : white_);
            dev_->setVertexBuffer(it.model->vb);
            dev_->setIndexBuffer(it.model->ib);
            dev_->drawIndexed(it.indexCount,
                it.firstIndex + it.model->gpuFirstIndex,
                int32_t(it.model->gpuBaseVertex));
            ++objectMotionStats_.reactiveAnimatedDraws;
        }

        // Optional water/particles/custom extensions. The GPU pass and
        // viewport are already active. Clients must blend coverage over
        // previous layers (Screen union) and must not end the pass.
        for (auto& renderReactive : motionReactiveDraws_) {
            renderReactive(*dev_,sceneDepth,w,h);
            ++objectMotionStats_.reactiveCallbacks;
        }
        dev_->endPass();
        post.acceptMotionReactiveMask(MotionTextureInput::declared(
            history.reactive,w,h,gfx::PixelFormat::RGBA8));
        objectMotionStats_.reactiveSubmitted = true;
    } else {
        // Callbacks are frame-scoped and discarded at resolvePrisma().
        // Expose why an authored callback could not be invoked rather
        // than reusing it in another camera's subsequent scene.
        objectMotionStats_.reactiveCallbacksDropped +=
            static_cast<uint32_t>(motionReactiveDraws_.size());
    }

}

void Renderer3D::renderObjectMotionVectors(PostProcess& post) {
    objectMotionStats_ = {};
    if (!post.wantsObjectMotionVectors() || !dev_) {
        objectMotionStats_.reactiveCallbacksDropped =
            static_cast<uint32_t>(motionReactiveDraws_.size());
        return;
    }
    uint64_t viewId = 0;
    mat4 previousCamera;
    const bool previousCameraValid =
        post.objectMotionContext(viewId, previousCamera);
    auto& history = objectMotionViews_[viewId];
    objectMotionStats_.viewId = viewId;
    const uint64_t epoch = post.objectMotionEpoch(viewId);
    if (history.epoch != epoch) {
        history.previous.clear();
        history.previousDeformed.clear();
        history.valid = false;
        history.epoch = epoch;
    }

    // Collect current rigid-node transforms once, before any optional GPU pass.
    // Stable (entity,node) pairs are the identity: repeated primitives and
    // instancing batches cannot accidentally advance history multiple times.
    using Key = std::pair<uint64_t, int>;
    std::map<Key, MotionRigidPose> current;
    auto eligible = [](const ModelItem& it) {
        return it.motionPersistentId != 0 && it.motionNodeIndex >= 0 &&
            it.rigid && it.inView && it.tint.w >= 0.99f &&
            it.dissolve <= 0.0f && it.joints < 0 && !it.baked &&
            it.morph < 0 && (!it.prim || it.prim->morphTargets == 0 ||
            !it.model || !it.model->morphTex.valid()) &&
            it.materialRole != 1 && it.shadowRole != 2 &&
            it.lodThreshold >= 1.0f && !it.lodInvert &&
            it.mat && (it.mat->alpha == AlphaMode::Opaque ||
             it.mat->alpha == AlphaMode::Mask) && !it.mat->shaderGraph &&
            it.mat->team == TeamMode::None && !it.mat->transparent && !(it.flags & kDrawTransparent) &&
            it.model && !it.model->geometry && it.model->vb.valid() &&
            it.model->ib.valid() && it.indexCount > 0;
    };
    for (const auto& it : modelItems_)
        if (eligible(it))
            current.try_emplace(Key{it.motionPersistentId,it.motionNodeIndex},
                                MotionRigidPose{it.model,it.world});

    using DeformKey = std::tuple<uint64_t,int,size_t>;
    std::map<DeformKey, MotionDeformPose> currentDeformed;
    auto deformEligible = [&](const ModelItem& it) {
        if (!it.motionPersistentId || it.motionNodeIndex < 0 ||
            !it.inView || !it.model || !it.prim || !it.mat ||
            !it.model->vb.valid() || !it.model->ib.valid() ||
            it.model->geometry || it.indexCount == 0 ||
            it.mat->shaderGraph)
            return false;
        const bool hasMorph = it.prim->morphTargets > 0 &&
                              it.model->morphTex.valid();
        const bool hasSkin = it.joints >= 0;
        // Mixed baked-clip + morph uses the dedicated Stage 29 shader:
        // morph deltas are applied before texture-atlas skinning.
        const bool bakedEligible = it.baked && it.model->baked.tex.valid() &&
            it.prim->jointPalette.size() <= 80 &&
            (!hasMorph || it.prim->morphTargets <= kMaxMorphTargets);
        return (hasSkin || hasMorph || bakedEligible) &&
            !(it.baked && !bakedEligible) && it.tint.w >= 0.99f &&
            it.dissolve <= 0.0f && it.materialRole != 1 &&
            it.shadowRole != 2 && it.lodThreshold >= 1.0f &&
            !it.lodInvert && it.mat->alpha == AlphaMode::Opaque &&
            !it.mat->transparent && !(it.flags & kDrawTransparent);
    };
    for (const auto& it : modelItems_) {
        if (!deformEligible(it)) continue;
        MotionDeformPose pose;
        pose.world = it.world;
        pose.sourceModel = it.model;
        pose.hasSkin = it.joints >= 0;
        pose.baked = it.baked;
        pose.animation = it.anim;
        if (pose.hasSkin &&
            size_t(it.joints) + pose.joints.size() <= jointData_.size())
            std::memcpy(pose.joints.data(), jointData_.data() + it.joints,
                        pose.joints.size()*sizeof(float));
        else if (pose.hasSkin) continue;
        if (it.morph >= 0 &&
            size_t(it.morph) + pose.weights.size() <= morphData_.size())
            std::memcpy(pose.weights.data(),morphData_.data()+it.morph,
                        pose.weights.size()*sizeof(float));
        currentDeformed[{it.motionPersistentId,it.motionNodeIndex,
                         it.geometryPrimitive}] = std::move(pose);
    }

    // Velocity requires a previously admitted camera and transform epoch;
    // current-frame reactive coverage does not. Never submit a fake zero
    // velocity buffer on an initial frame/cut/resize.
    const int w = std::max(sceneW_,1), h = std::max(sceneH_,1);
    const bool resize = (history.width != 0 && history.height != 0) &&
                        (history.width != w || history.height != h);
    const bool velocityHistoryReady =
        previousCameraValid && history.valid && !resize;
    objectMotionStats_.velocitySeedOnly = !velocityHistoryReady;
    if (resize) {
        queueObjectMotionTexture(history.target,motionLastSubmittedFence_);
        queueObjectMotionTexture(history.depth,motionLastSubmittedFence_);
        queueObjectMotionTexture(history.reactive,motionLastSubmittedFence_);
    }
    history.width = w; history.height = h;
    history.valid = true;
    const auto sceneDepth = post.objectMotionSceneDepth();
    if (!sceneDepth.valid()) {
        objectMotionStats_.reactiveCallbacksDropped =
            static_cast<uint32_t>(motionReactiveDraws_.size());
        history.previous = std::move(current);
        history.previousDeformed = std::move(currentDeformed);
        return; // neither velocity nor the reactive mask can sample depth
    }

    // Only the velocity producer needs signed RGBA16F and previous poses.
    // The RGBA8 reactive producer below remains available without either.
    if (velocityHistoryReady && dev_->supports(gfx::PixelFormat::RGBA16F)) {
    if (!history.target.valid()) {
        gfx::TextureDesc desc;
        desc.width = w; desc.height = h;
        desc.renderTarget = true;
        desc.format = gfx::PixelFormat::RGBA16F;
        desc.filter = gfx::Filter::Nearest;
        desc.label = "prisma rigid motion velocity";
        history.target = dev_->createTexture(desc);
    }
    if (!history.depth.valid()) {
        gfx::TextureDesc desc;
        desc.width = w; desc.height = h;
        desc.renderTarget = true;
        desc.format = gfx::PixelFormat::Depth;
        desc.filter = gfx::Filter::Nearest;
        desc.label = "prisma rigid motion scratch depth";
        history.depth = dev_->createTexture(desc);
    }
    if (!objectMotionPipeline_.valid() && !objectMotionShader_.valid()) {
        gfx::ShaderDesc shader;
        shader.glslVertex = motion_shaders::kVS;
        shader.glslFragment = motion_shaders::kFS;
        shader.msl = motion_shaders::kMSL;
        shader.textureSlots = 1; shader.uniformSlots = 1;
        shader.label = "prisma rigid motion overlay";
        objectMotionShader_ = dev_->createShader(shader);
        if (objectMotionShader_.valid()) {
            gfx::PipelineDesc pipeline;
            pipeline.shader = objectMotionShader_;
            pipeline.attrs[0] = {0, gfx::VertexFormat::Float3, 0};
            pipeline.attrCount = 1;
            pipeline.stride = sizeof(ModelVertex);
            pipeline.indexType = gfx::IndexType::UInt32;
            pipeline.cull = gfx::CullMode::Back;
            pipeline.depthTest = false;
            pipeline.depthWrite = false;
            pipeline.colorFormat = gfx::PixelFormat::RGBA16F;
            pipeline.label = "prisma rigid motion overlay";
            objectMotionPipeline_ = dev_->createPipeline(pipeline);
        }
    }
    if (!objectMotionInstancedShader_.valid()) {
        gfx::ShaderDesc shader;
        shader.glslVertex = motion_shaders::kInstancedVS;
        shader.glslFragment = motion_shaders::kInstancedFS;
        shader.msl = motion_shaders::kInstancedMSL;
        shader.textureSlots = 1;
        shader.uniformSlots = 1;
        shader.label = "prisma batched rigid velocity";
        objectMotionInstancedShader_ = dev_->createShader(shader);
        if (objectMotionInstancedShader_.valid()) {
            gfx::PipelineDesc pipeline;
            pipeline.shader = objectMotionInstancedShader_;
            pipeline.attrs[0] = {0, gfx::VertexFormat::Float3, 0};
            pipeline.attrCount = 1;
            pipeline.stride = sizeof(ModelVertex);
            pipeline.indexType = gfx::IndexType::UInt32;
            pipeline.cull = gfx::CullMode::Back;
            pipeline.depthTest = false;
            pipeline.depthWrite = false;
            pipeline.colorFormat = gfx::PixelFormat::RGBA16F;
            pipeline.label = "prisma batched rigid velocity";
            objectMotionInstancedPipeline_ = dev_->createPipeline(pipeline);
        }
    }
    if (!objectMotionDeformedShader_.valid()) {
        gfx::ShaderDesc shader;
        shader.glslVertex = motion_shaders::kDeformedVS;
        shader.glslFragment = motion_shaders::kFS;
        shader.msl = motion_shaders::kDeformedMSL;
        shader.textureSlots = 9; // original morph delta texture at slot 8
        shader.uniformSlots = 6; // U0 plus current/previous palette U3/U4 and morph U5
        shader.label = "prisma skinned morph velocity";
        objectMotionDeformedShader_ = dev_->createShader(shader);
        if (objectMotionDeformedShader_.valid()) {
            gfx::PipelineDesc pipeline;
            pipeline.shader = objectMotionDeformedShader_;
            pipeline.attrs[0] = {0, gfx::VertexFormat::Float3, 0};
            pipeline.attrs[1] = {5, gfx::VertexFormat::UByte4,
                                 uint32_t(offsetof(ModelVertex,joints))};
            pipeline.attrs[2] = {6, gfx::VertexFormat::UByte4N,
                                 uint32_t(offsetof(ModelVertex,weights))};
            pipeline.attrCount = 3;
            pipeline.stride = sizeof(ModelVertex);
            pipeline.indexType = gfx::IndexType::UInt32;
            pipeline.cull = gfx::CullMode::Back;
            pipeline.depthTest = false;
            pipeline.depthWrite = false;
            pipeline.colorFormat = gfx::PixelFormat::RGBA16F;
            pipeline.label = "prisma skinned morph velocity";
            objectMotionDeformedPipeline_ = dev_->createPipeline(pipeline);
        }
    }
    if (!objectMotionBakedShader_.valid()) {
        gfx::ShaderDesc shader;
        shader.glslVertex = motion_shaders::kBakedVS;
        shader.glslFragment = motion_shaders::kFS;
        shader.msl = motion_shaders::kBakedMSL;
        shader.textureSlots = 9; // texture 0 scene depth, 7 baked atlas, 8 morph deltas
        shader.uniformSlots = 6; // U0, U3 palette, U4 animation, U5 prior/current morph
        shader.label = "prisma baked animation velocity";
        objectMotionBakedShader_ = dev_->createShader(shader);
        if (objectMotionBakedShader_.valid()) {
            gfx::PipelineDesc pipeline;
            pipeline.shader = objectMotionBakedShader_;
            pipeline.attrs[0] = {0, gfx::VertexFormat::Float3, 0};
            pipeline.attrs[1] = {5, gfx::VertexFormat::UByte4,
                                 uint32_t(offsetof(ModelVertex,joints))};
            pipeline.attrs[2] = {6, gfx::VertexFormat::UByte4N,
                                 uint32_t(offsetof(ModelVertex,weights))};
            pipeline.attrCount = 3;
            pipeline.stride = sizeof(ModelVertex);
            pipeline.indexType = gfx::IndexType::UInt32;
            pipeline.cull = gfx::CullMode::Back;
            pipeline.depthTest = false;
            pipeline.depthWrite = false;
            pipeline.colorFormat = gfx::PixelFormat::RGBA16F;
            pipeline.label = "prisma baked animation velocity";
            objectMotionBakedPipeline_ = dev_->createPipeline(pipeline);
        }
    }
    if (!objectMotionMaskedShader_.valid()) {
        gfx::ShaderDesc shader;
        shader.glslVertex = motion_shaders::kMaskedVS;
        shader.glslFragment = motion_shaders::kMaskedFS;
        shader.msl = motion_shaders::kMaskedMSL;
        shader.textureSlots = 2; // sampled scene depth + alpha base texture
        shader.uniformSlots = 1;
        shader.label = "prisma alpha-cutout velocity";
        objectMotionMaskedShader_ = dev_->createShader(shader);
        if (objectMotionMaskedShader_.valid()) {
            gfx::PipelineDesc pipeline;
            pipeline.shader = objectMotionMaskedShader_;
            pipeline.attrs[0] = {0, gfx::VertexFormat::Float3, 0};
            pipeline.attrs[1] = {2, gfx::VertexFormat::Float2,
                                 uint32_t(offsetof(ModelVertex,uv))};
            pipeline.attrs[2] = {4, gfx::VertexFormat::UByte4N,
                                 uint32_t(offsetof(ModelVertex,color))};
            pipeline.attrCount = 3;
            pipeline.stride = sizeof(ModelVertex);
            pipeline.indexType = gfx::IndexType::UInt32;
            pipeline.cull = gfx::CullMode::Back;
            pipeline.depthTest = false;
            pipeline.depthWrite = false;
            pipeline.colorFormat = gfx::PixelFormat::RGBA16F;
            pipeline.label = "prisma alpha-cutout velocity";
            objectMotionMaskedPipeline_ = dev_->createPipeline(pipeline);
        }
    }
    if (history.target.valid() && history.depth.valid()) {
        struct MotionU {
            float current[16], previous[16], params[4];
        };
        // The reactive pass is independent from object-velocity shaders:
        // transparent water/particles providers must still run if a rigid
        // motion pipeline is unavailable on a particular backend.
        if (objectMotionPipeline_.valid()) {
        gfx::PassDesc pass;
        pass.colorTarget = history.target;
        pass.depthTarget = history.depth; // NEVER sample and attach scene depth
        pass.clearColor[0] = pass.clearColor[1] = 0.0f;
        pass.clearColor[2] = pass.clearColor[3] = 0.0f; // B=0: no object
        pass.clearColorEnabled = true;
        pass.clearDepthEnabled = true;
        dev_->beginPass(pass);
        dev_->setViewport({0,0,w,h});
        dev_->setPipeline(objectMotionPipeline_);
        dev_->setTexture(0, sceneDepth);
        // Geometry-equivalent rigid draws can share one instanced velocity
        // submission even when their ordinary material states differ.
        // The source Model pointer is part of the key: model buffer IDs,
        // index offsets and base vertex must be identical in one batch.
        using BatchKey = std::tuple<const Model*, uint32_t, uint32_t>;
        std::map<BatchKey, std::vector<const ModelItem*>> batches;
        for (const auto& it : modelItems_) {
            if (!eligible(it) || it.mat->alpha == AlphaMode::Mask) continue;
            batches[{it.model,it.firstIndex,it.indexCount}].push_back(&it);
        }
        struct BatchU {
            float current[16][16];
            float previous[16][16];
            float params[4];
        };
        static_assert(sizeof(BatchU) <= 4096);
        const auto lastModel = [&](const ModelItem& it) {
            const Key key{it.motionPersistentId,it.motionNodeIndex};
            const auto previous = history.previous.find(key);
            return previous == history.previous.end() ||
                   previous->second.sourceModel != it.model
                ? it.world : previous->second.world;
        };
        for (const auto& [key, items] : batches) {
            for (size_t first = 0; first < items.size();) {
                const size_t n = objectMotionInstancedPipeline_.valid()
                    ? std::min<size_t>(16,items.size()-first) : 1;
                const ModelItem& sample = *items[first];
                if (n > 1) {
                    BatchU uniforms{};
                    uniforms.params[0] = float(w);
                    uniforms.params[1] = float(h);
                    uniforms.params[2] = dev_->info().renderTargetFlipY ? 1.0f : 0.0f;
                    for (size_t i=0; i<n; ++i) {
                        const ModelItem& it = *items[first+i];
                        const mat4 currentMvp = viewProj_ * it.world;
                        const mat4 previousMvp = previousCamera * lastModel(it);
                        std::memcpy(uniforms.current[i], currentMvp.m, sizeof currentMvp.m);
                        std::memcpy(uniforms.previous[i], previousMvp.m, sizeof previousMvp.m);
                    }
                    dev_->setPipeline(objectMotionInstancedPipeline_);
                    dev_->setUniforms(0,&uniforms,sizeof uniforms);
                } else {
                    const mat4 currentMvp = viewProj_ * sample.world;
                    const mat4 previousMvp = previousCamera * lastModel(sample);
                    MotionU uniforms{};
                    std::memcpy(uniforms.current,currentMvp.m,sizeof uniforms.current);
                    std::memcpy(uniforms.previous,previousMvp.m,sizeof uniforms.previous);
                    uniforms.params[0] = float(w);
                    uniforms.params[1] = float(h);
                    uniforms.params[2] = dev_->info().renderTargetFlipY ? 1.0f : 0.0f;
                    dev_->setPipeline(objectMotionPipeline_);
                    dev_->setUniforms(0,&uniforms,sizeof uniforms);
                }
                dev_->setVertexBuffer(sample.model->vb);
                dev_->setIndexBuffer(sample.model->ib);
                dev_->drawIndexed(sample.indexCount,
                    sample.firstIndex + sample.model->gpuFirstIndex,
                    int32_t(sample.model->gpuBaseVertex),
                    static_cast<uint32_t>(n));
                objectMotionStats_.rigidDraws++;
                objectMotionStats_.rigidInstances += static_cast<uint32_t>(n);
                first += n;
            }
        }
        // Alpha-mask replay must happen after rigid batches, before the
        // deformable pass, so cut-out fragments never mark empty pixels
        // with a bogus object velocity.
        if (objectMotionMaskedPipeline_.valid()) {
            struct MaskU {
                float current[16], previous[16], params[4];
                float uvTransform0[4], uvTransform1[4];
            };
            dev_->setPipeline(objectMotionMaskedPipeline_);
            for (const auto& it : modelItems_) {
                if (!eligible(it) || it.mat->alpha != AlphaMode::Mask)
                    continue;
                const Key key{it.motionPersistentId,it.motionNodeIndex};
                const auto previous = history.previous.find(key);
                if (previous == history.previous.end() ||
                    previous->second.sourceModel != it.model) continue;
                const mat4 currentMvp = viewProj_ * it.world;
                const mat4 previousMvp = previousCamera * previous->second.world;
                MaskU u{};
                std::memcpy(u.current,currentMvp.m,sizeof u.current);
                std::memcpy(u.previous,previousMvp.m,sizeof u.previous);
                u.params[0]=float(w); u.params[1]=float(h);
                u.uvTransform0[0]=it.mat->uvOffset.x;
                u.uvTransform0[1]=it.mat->uvOffset.y;
                u.uvTransform0[2]=it.mat->uvScale.x;
                u.uvTransform0[3]=it.mat->uvScale.y;
                u.uvTransform1[0]=it.mat->uvRotation;
                u.uvTransform1[1]=it.mat->alphaCutoff;
                u.uvTransform1[2]=it.mat->baseColor.w;
                u.uvTransform1[3]=it.mat->vertexColors ? 1.0f : 0.0f;
                dev_->setUniforms(0,&u,sizeof u);
                gfx::TextureHandle base=white_;
                if (it.mat->baseTex >= 0 &&
                    size_t(it.mat->baseTex) < it.model->images.size()) {
                    auto tex=it.model->images[size_t(it.mat->baseTex)].tex;
                    if (tex.valid()) base=tex;
                }
                dev_->setTexture(1,base);
                dev_->setVertexBuffer(it.model->vb);
                dev_->setIndexBuffer(it.model->ib);
                dev_->drawIndexed(it.indexCount,
                    it.firstIndex + it.model->gpuFirstIndex,
                    int32_t(it.model->gpuBaseVertex));
                objectMotionStats_.cutoutDraws++;
            }
        }
        // Re-evaluate vertex deformation for BOTH poses. Current and prior
        // palette buffers are independent U3/U4 blocks (each <= 4KB).
        // Morph deltas are read from the same immutable model texture, with
        // current and previous weights in U5. No CPU mesh readback.
        if (objectMotionDeformedPipeline_.valid()) {
            struct MorphU {
                float currentW[8] = {};
                float previousW[8] = {};
                float info[4] = {};
                float info2[4] = {};
            };
            static_assert(sizeof(MorphU) <= 4096);
            dev_->setPipeline(objectMotionDeformedPipeline_);
            for (const auto& it : modelItems_) {
                if (!deformEligible(it)) continue;
                const DeformKey key{it.motionPersistentId,it.motionNodeIndex,
                                    it.geometryPrimitive};
                const auto prior = history.previousDeformed.find(key);
                const auto now = currentDeformed.find(key);
                if (prior == history.previousDeformed.end() ||
                    now == currentDeformed.end() ||
                    prior->second.hasSkin != now->second.hasSkin ||
                    prior->second.baked != now->second.baked ||
                    prior->second.sourceModel != now->second.sourceModel ||
                    it.baked) { ++objectMotionStats_.rejected; continue; }
                const auto& cur = now->second;
                const auto& prev = prior->second;
                const mat4 currentMvp = viewProj_ * cur.world;
                const mat4 previousMvp = previousCamera * prev.world;
                MotionU u{};
                std::memcpy(u.current,currentMvp.m,sizeof u.current);
                std::memcpy(u.previous,previousMvp.m,sizeof u.previous);
                u.params[0] = float(w);
                u.params[1] = float(h);
                u.params[2] = cur.hasSkin ? 1.0f : 0.0f;
                const bool morph = it.prim->morphTargets > 0 &&
                                   it.model->morphTex.valid();
                u.params[3] = morph ? 1.0f : 0.0f;
                MorphU mo{};
                std::memcpy(mo.currentW,cur.weights.data(),sizeof mo.currentW);
                std::memcpy(mo.previousW,prev.weights.data(),sizeof mo.previousW);
                if (morph) {
                    mo.info[0] = float(it.prim->morphTexel);
                    mo.info[1] = float(it.prim->firstVertex);
                    mo.info[2] = float(it.prim->vertexCount);
                    mo.info[3] = float(it.prim->morphTargets);
                    mo.info2[0] = float(it.model->morphTexWidth);
                }
                dev_->setUniforms(0,&u,sizeof u);
                dev_->setUniforms(3,cur.joints.data(),
                                  cur.joints.size()*sizeof(float));
                dev_->setUniforms(4,prev.joints.data(),
                                  prev.joints.size()*sizeof(float));
                dev_->setUniforms(5,&mo,sizeof mo);
                if (morph) dev_->setTexture(8,it.model->morphTex);
                dev_->setVertexBuffer(it.model->vb);
                dev_->setIndexBuffer(it.model->ib);
                dev_->drawIndexed(it.indexCount,
                    it.firstIndex + it.model->gpuFirstIndex,
                    int32_t(it.model->gpuBaseVertex));
                objectMotionStats_.skinnedDraws++;
            }
        }
        // Texture-baked animation: rows A/B and blend are independently
        // sampled for current and previous pose. Keep this separate from
        // CPU-palette skinning; no animation state is written back to ECS.
        if (objectMotionBakedPipeline_.valid()) {
            struct BakedMotionU {
                float current[16], previous[16], params[4];
            };
            struct BakedAnimU {
                float current[4], previous[4];
            };
            struct BakedMorphU {
                float current[8] = {};
                float previous[8] = {};
                float info[4] = {};
                float info2[4] = {};
            };
            static_assert(sizeof(BakedMorphU) <= 4096);
            dev_->setPipeline(objectMotionBakedPipeline_);
            for (const auto& it : modelItems_) {
                if (!it.baked || !deformEligible(it)) continue;
                const DeformKey key{it.motionPersistentId,it.motionNodeIndex,
                                     it.geometryPrimitive};
                const auto before = history.previousDeformed.find(key);
                const auto now = currentDeformed.find(key);
                if (before == history.previousDeformed.end() ||
                    now == currentDeformed.end() ||
                    before->second.sourceModel != now->second.sourceModel ||
                    !before->second.baked) continue;
                const mat4 currentMvp = viewProj_ * now->second.world;
                const mat4 previousMvp = previousCamera * before->second.world;
                BakedMotionU u{};
                std::memcpy(u.current,currentMvp.m,sizeof u.current);
                std::memcpy(u.previous,previousMvp.m,sizeof u.previous);
                u.params[0] = float(w); u.params[1] = float(h);
                const bool hasMorph = it.prim->morphTargets > 0 &&
                                      it.model->morphTex.valid();
                u.params[2] = hasMorph ? 1.0f : 0.0f;
                float palette[80] = {};
                for (size_t i=0;i<it.prim->jointPalette.size() && i<80;++i)
                    palette[i] = float(it.prim->jointPalette[i]);
                BakedAnimU anim{};
                const auto& a = now->second.animation, b = before->second.animation;
                anim.current[0]=a.x; anim.current[1]=a.y;
                anim.current[2]=a.z; anim.current[3]=a.w;
                anim.previous[0]=b.x; anim.previous[1]=b.y;
                anim.previous[2]=b.z; anim.previous[3]=b.w;
                BakedMorphU morph{};
                if (hasMorph) {
                    std::memcpy(morph.current,now->second.weights.data(),
                                sizeof morph.current);
                    std::memcpy(morph.previous,before->second.weights.data(),
                                sizeof morph.previous);
                    morph.info[0] = float(it.prim->morphTexel);
                    morph.info[1] = float(it.prim->firstVertex);
                    morph.info[2] = float(it.prim->vertexCount);
                    morph.info[3] = float(it.prim->morphTargets);
                    morph.info2[0] = float(it.model->morphTexWidth);
                }
                dev_->setUniforms(0,&u,sizeof u);
                dev_->setUniforms(3,palette,sizeof palette);
                dev_->setUniforms(4,&anim,sizeof anim);
                dev_->setUniforms(5,&morph,sizeof morph);
                dev_->setTexture(7,it.model->baked.tex);
                dev_->setTexture(8,hasMorph ? it.model->morphTex : white_);
                dev_->setVertexBuffer(it.model->vb);
                dev_->setIndexBuffer(it.model->ib);
                dev_->drawIndexed(it.indexCount,
                    it.firstIndex + it.model->gpuFirstIndex,
                    int32_t(it.model->gpuBaseVertex));
                objectMotionStats_.bakedDraws++;
            }
        }
        dev_->endPass();
        objectMotionStats_.submitted = true;
        post.acceptObjectMotionVectors(MotionTextureInput::declared(
            history.target,w,h,gfx::PixelFormat::RGBA16F));
        } // optional object-velocity rendering

    } // existing velocity target/depth gate
    } // previous camera velocity eligibility
    // ALWAYS produce current-frame coverage after the velocity phase,
    // including its history-seeding and RGBA16F-unavailable cases.
    renderCurrentFrameReactiveMask(post,history,sceneDepth,w,h);
    history.previous = std::move(current);
    history.previousDeformed = std::move(currentDeformed);
}

void Renderer3D::resolvePrisma() {
    if(dev_)checkGpuDevice(*dev_);
    if (!post_) {
        objectMotionStats_ = {};
        objectMotionStats_.reactiveCallbacksDropped =
            static_cast<uint32_t>(motionReactiveDraws_.size());
        motionReactiveDraws_.clear();
        return;
    }
    PostProcess* post = post_;
    post_ = nullptr;
    dev_->endPass();
    renderObjectMotionVectors(*post);
    PostFrame f{viewProj_, camPos_, lighting,
                sceneW_ > 0 ? sceneW_ : dev_->framebufferWidth(),
                sceneH_ > 0 ? sceneH_ : dev_->framebufferHeight(),
                quality_ != ShadowQuality::Off ? shadowMap_ : gfx::TextureHandle{},
                lightViewProj_,
                cam_.ortho ? -200.0f : cam_.zNear,
                cam_.ortho ? 1000.0f : cam_.zFar, cam_.ortho};
    post->resolve(*dev_, postOut_, f);
    motionReactiveDraws_.clear();
    hdrFrame_ = false;
}

// Distinct, stable high-contrast colour for each draw call. Instanced draws
// intentionally consume one ID, not one ID per instance.
float4 Renderer3D::drawCallDebugColor(uint32_t drawId) {
    const float h = std::fmod(float(drawId) * 0.61803398875f, 1.0f) * 6.0f;
    const float x = 0.76f * (1.0f - std::fabs(std::fmod(h, 2.0f) - 1.0f));
    float r = 0, g = 0, b = 0;
    if (h < 1) { r = .76f; g = x; }
    else if (h < 2) { r = x; g = .76f; }
    else if (h < 3) { g = .76f; b = x; }
    else if (h < 4) { g = x; b = .76f; }
    else if (h < 5) { r = x; b = .76f; }
    else { r = .76f; b = x; }
    return {r + .17f, g + .17f, b + .17f, 1.0f};
}

void Renderer3D::render(const gfx::PassDesc& screenPass, PostProcess* post) {
    if(dev_)checkGpuDevice(*dev_);
    drawCalls_ = 0;
    // Synchronize persistent scene staging before any rendering passes.
    // Model shader bindings and GPU culling remain on the legacy path.
    syncPersistentModels();
    // GPU command generation must complete encoding before render passes.
    // Metal guarantees command order between compute and render encoders.
    prepareGpuModelDraws();
    prepareOcclusion();
    // ---- shadow pass
    if (quality_ != ShadowQuality::Off && shadowMap_.valid()) {
        gfx::PassDesc sp;
        sp.depthTarget = shadowMap_;
        sp.clearColorEnabled = false;
        sp.clearDepthEnabled = true;
        dev_->beginPass(sp);
        dev_->setViewport({0, 0, shadowSize_, shadowSize_});
        dev_->setPipeline(shadowPipe_);
        uploadFrameUniforms(true);
        for (auto& it : items_) {
            if (it.flags & (kDrawNoCastShadow | kDrawUnlit) || it.color.w < 0.99f) continue;
            DrawUniforms u{};
            std::memcpy(u.model, it.model.m, sizeof u.model);
            dev_->setUniforms(0, &u, sizeof u);
            dev_->setVertexBuffer(it.mesh->vb);
            dev_->setIndexBuffer(it.mesh->ib);
            dev_->drawIndexed(it.mesh->indexCount);
            ++drawCalls_;
        }
        renderModels(0);
        for (auto& fn : customShadow_) fn(*dev_);
        dev_->endPass();
    }

    renderSpotShadow();   // High: the main spot light's shadow map (Renderer3DReflect.cpp)
    lights.renderMap();


    // ---- main pass (left open for the caller's UI; with a post hook: the hook's scene target)
    post_ = post && post->active() ? post : nullptr;
    postOut_ = screenPass;
    const int sw = sceneW_ > 0 ? sceneW_ : dev_->framebufferWidth(), sh = sceneH_ > 0 ? sceneH_ : dev_->framebufferHeight();
    const gfx::PassDesc mainPass = post_ ? post_->scenePass(screenPass, sw, sh) : screenPass;
    hdrFrame_ = post_ && post_->linearScene();
    lights.setLinearScene(hdrFrame_);
    renderReflection();
    struct LumaReceiverScope {
        bool& capture;bool& auxiliary;bool oldAuxiliary;
        LumaReceiverScope(bool& c,bool& a):capture(c),auxiliary(a),oldAuxiliary(a){
            capture=true;auxiliary=true; // disable unrelated GPU ICB/MDI
        }
        ~LumaReceiverScope(){capture=false;auxiliary=oldAuxiliary;}
    };
    // PBR side-channel captures are opt-in PostProcess only. They sample the
    // SAME current/animated ModelItems and palette/batched material state.
    // Each receiver has independent depth; never reuse HDR depth as a
    // writable attachment (it will be sampled by Vulkan compute later).
    if(post_ && hdrFrame_ && post_->wantsMaterialReceiverPass()) {
        auto receiver=post_->materialReceiverPass(sw,sh);
        if(receiver.colorTarget.valid() && receiver.depthTarget.valid()) {
            dev_->beginPass(receiver);
            dev_->setViewport({0,0,sw,sh});
            {
                LumaReceiverScope captureScope(lumaMaterialCapture_,auxiliaryModelPass_);
                uploadFrameUniforms(false);
                renderModels(1);
                for(auto& fn:customMaterialReceiver_)fn(*dev_);
            }
            dev_->endPass();
            post_->materialReceiverPublished(receiver.colorTarget,sw,sh);
        }
    }
    if(post_ && hdrFrame_ && post_->wantsSurfaceReceiverPass()) {
        auto surface=post_->surfaceReceiverPass(sw,sh);
        if(surface.colorTarget.valid() && surface.depthTarget.valid()) {
            dev_->beginPass(surface);
            dev_->setViewport({0,0,sw,sh});
            {
                LumaReceiverScope captureScope(lumaSurfaceCapture_,auxiliaryModelPass_);
                uploadFrameUniforms(false);
                renderModels(1);
                for(auto& fn:customSurfaceReceiver_)fn(*dev_);
            }
            dev_->endPass();
            post_->surfaceReceiverPublished(surface.colorTarget,sw,sh);
        }
    }
    // Exclude shadow, probe and reflection passes from the visible-colour ID range.
    debugMainDrawId_ = 0;
    // trace tier + water: the opaque scene goes into colour + depth textures first (SSR reads them),
    // then it is blitted (with depth) into the main pass and the rest is drawn on top
    bool capture = modelReflections_ && hdrFrame_ && traceActive();
    if (traceActive()) {
        for (const auto& it : items_)
            if (it.color.w < 0.99f && it.specular >= 0.85f && !(it.flags & kDrawUnlit)) { capture = true; break; }
        if (capture) capture = beginSceneCapture(sw, sh);
    }
    if (!capture) dev_->beginPass(mainPass);
    dev_->setViewport({0, 0, sw, sh});
    for (auto& fn : customOpaque_) fn(*dev_);
    dev_->setPipeline(litPipe_);
    uploadFrameUniforms(false);
    dev_->setTexture(0, shadowMap_);
    lights.bind(2, 1, 2);
    bindTraceTextures();
    auto drawItem = [&](const DrawItem& it) {
        DrawUniforms u{};
        std::memcpy(u.model, it.model.m, sizeof u.model);
        u.color[0] = it.color.x; u.color[1] = it.color.y; u.color[2] = it.color.z; u.color[3] = it.color.w;
        // Shader -1: reflective NEREID water. -2: NEREID water at a
        // different elevation than the single planar reflection texture.
        // Keep waves and SSR on secondary surfaces, but skip the wrong plane.
        const bool markedWater=(it.flags & kDrawWaterSurface)!=0;
        const bool otherPlane=markedWater && reflectionPlaneValid_ &&
            !sameWaterReflectionPlane(
                it.model.transformPoint((it.mesh->boundsMin+
                                         it.mesh->boundsMax)*0.5f).y,
                reflectionPlaneY_);
        u.params[0] = markedWater ?
                      ((it.flags & kDrawNereidCascade) ? -3.f :
                       (otherPlane ? -2.f : -1.f)) :
                      ((it.flags & kDrawUnlit) ? 1.f : 0.f);
        u.params[1] = it.specular;
        u.params[2] = (it.flags & kDrawNoReceiveShadow) ? 0.f : 1.f;
        u.params[3] = it.emissive;
        std::memcpy(u.waterRipples,it.waterRipples.data(),sizeof u.waterRipples);
        std::memcpy(u.waterWakeShapes,it.waterWakeShapes.data(),sizeof u.waterWakeShapes);
        std::memcpy(u.waterWind,&it.waterWind,sizeof u.waterWind);
        std::memcpy(u.waterHydraulics,&it.waterHydraulics,
                    sizeof u.waterHydraulics);
        std::memcpy(u.waterRapidColor,&it.waterRapidColor,
                    sizeof u.waterRapidColor);
        if (studioDebugMode_ == 2 || studioDebugMode_ == 4 || studioDebugMode_ == 9)
            u.params[0] = 10.0f + float(studioDebugMode_);
        if (drawCallDebug_) {
            const float4 c = drawCallDebugColor(++debugMainDrawId_);
            u.color[0] = c.x; u.color[1] = c.y; u.color[2] = c.z; u.color[3] = 1.0f;
            u.params[0] = 2.0f; // solid diagnostic output in GLSL and Metal
        }
        dev_->setUniforms(0, &u, sizeof u);
        dev_->setVertexBuffer(it.mesh->vb);
        dev_->setIndexBuffer(it.mesh->ib);
        const uint32_t drawCount=it.waterIndexCount?it.waterIndexCount:it.mesh->indexCount;
        dev_->drawIndexed(drawCount,it.waterFirstIndex);
        ++drawCalls_;
        // drawItem is only used by the current camera's colour pass.
        profile_.cameraMeshTriangles += uint64_t(it.waterIndexCount?it.waterIndexCount:it.mesh->indexCount) / 3;
    };
    for (auto& it : items_) if (it.color.w >= 0.99f) drawItem(it);
    // The per-phase profile also includes reflection/SSR replay. Snapshot
    // only the *first* main-camera opaque pass so a triangle is not counted
    // twice when the same visible surface is drawn for another target.
    const uint32_t opaqueStart = profile_.triangles[1];
    renderModels(1);
    profile_.cameraModelTriangles += uint32_t(profile_.triangles[1] - opaqueStart);
    if (!sceneSelectionOutline_) renderModels(3); // legacy game outline; editor uses a depth-tested mask
    if (capture) {
        endSceneCapture(mainPass, sw, sh); dev_->setViewport({0, 0, sw, sh});
        // The blit binds scene color at slot 0; model shaders expect the sun depth map there.
        dev_->setTexture(0, shadowMap_);
        // Read the first opaque pass while shading into a different target (no feedback).
        if (modelReflections_ && hdrFrame_) { uploadFrameUniforms(false); renderModels(1); }
    }
    // translucent meshes (water, ghosts) before overlays so that overlays (fog of war, markers) stay on top
    bool any = false;
    for (auto& it : items_) {
        if (it.color.w >= 0.99f) continue;
        if (!any) { dev_->setPipeline(litBlendPipe_); uploadFrameUniforms(false); dev_->setTexture(0, shadowMap_); lights.bind(2, 1, 2); bindTraceTextures(); any = true; }
        drawItem(it);
    }
    const uint32_t blendedStart = profile_.triangles[2];
    renderModels(2);
    profile_.cameraModelTriangles += uint32_t(profile_.triangles[2] - blendedStart);
    renderModels(4);   // transparent objects (kDrawTransparent) write their depth now: later ground layers stay under them
    lights.drawHalos(viewProj_);   // additive glows, depth-tested against the scene
    // custom renderers (sprites) after translucent meshes: they don't write depth, so water drawn
    // later would tint them; overlays (fog of war, markers) still come on top
    for (auto& fn : custom_) fn(*dev_);
    if (!tris_.empty()) {
        dev_->updateBuffer(triVB_, tris_.data(), tris_.size() * sizeof(ColorVertex));
        dev_->setPipeline(triPipe_);
        uploadFrameUniforms(false);
        dev_->setVertexBuffer(triVB_);
        dev_->draw(uint32_t(tris_.size()));
        ++drawCalls_;
        profile_.cameraOverlayTriangles += uint64_t(tris_.size()) / 3;
    }
    if (!lines_.empty()) {
        dev_->updateBuffer(lineVB_, lines_.data(), lines_.size() * sizeof(ColorVertex));
        dev_->setPipeline(linePipe_);
        uploadFrameUniforms(false);
        dev_->setVertexBuffer(lineVB_);
        dev_->draw(uint32_t(lines_.size()));
        ++drawCalls_;
    }
    if (sceneSelectionOutline_) renderSelectionOutline(mainPass, sw, sh);
}


bool Renderer3D::initSelectionOutline() {
    if (!dev_) return false;
    gfx::ShaderDesc shader;
    shader.glslVertex = selection_outline::kVertex;
    shader.glslFragment = selection_outline::kFragment;
    shader.msl = selection_outline::kMetal;
    shader.textureSlots = 1;
    shader.uniformSlots = 1;
    shader.label = "scene selection edge";
    sceneSelectionEdgeShader_ = dev_->createShader(shader);
    if (!sceneSelectionEdgeShader_.valid()) return false;
    gfx::PipelineDesc pd;
    pd.shader = sceneSelectionEdgeShader_;
    pd.attrs[0] = {0, gfx::VertexFormat::Float2, 0};
    pd.attrCount = 1;
    pd.stride = 2 * sizeof(float);
    pd.indexType = gfx::IndexType::None;
    pd.cull = gfx::CullMode::None;
    pd.blend = gfx::BlendMode::Alpha;
    pd.depthTest = false;
    pd.depthWrite = false;
    pd.colorFormat = gfx::PixelFormat::RGBA8;
    pd.label = "scene selection edge RGBA8";
    sceneSelectionEdgePipe8_ = dev_->createPipeline(pd);
    pd.colorFormat = gfx::PixelFormat::RGBA16F;
    pd.label = "scene selection edge HDR";
    sceneSelectionEdgePipeHdr_ = dev_->createPipeline(pd);
    // Mask primitives with their REAL geometry, but never modify the scene
    // depth. The same depth buffer is reused to hide occluded silhouettes.
    gfx::PipelineDesc primitiveMask;
    primitiveMask.shader = litShader_;
    primitiveMask.attrs[0] = {0, gfx::VertexFormat::Float3, 0};
    primitiveMask.attrs[1] = {1, gfx::VertexFormat::Float3, 12};
    primitiveMask.attrs[2] = {2, gfx::VertexFormat::UByte4N, 24};
    primitiveMask.attrCount = 3;
    primitiveMask.stride = sizeof(MeshVertex);
    primitiveMask.indexType = gfx::IndexType::UInt32;
    primitiveMask.depthTest = true;
    primitiveMask.depthWrite = false;
    primitiveMask.colorFormat = gfx::PixelFormat::RGBA8;
    primitiveMask.label = "selection primitive mask depth read-only";
    sceneSelectionPrimitiveMaskPipe_ = dev_->createPipeline(primitiveMask);
    const float triangle[6] = {-1, -1, 3, -1, -1, 3};
    gfx::BufferDesc bd;
    bd.data = triangle;
    bd.size = sizeof triangle;
    bd.label = "scene selection edge fullscreen triangle";
    sceneSelectionTriangle_ = dev_->createBuffer(bd);
    return sceneSelectionEdgePipe8_.valid() && sceneSelectionTriangle_.valid();
}

void Renderer3D::renderSelectionOutline(const gfx::PassDesc& mainPass,
                                        int width, int height) {
    if (!dev_ || !mainPass.colorTarget.valid() || !mainPass.depthTarget.valid() ||
        !sceneSelectionTriangle_.valid() || width <= 0 || height <= 0)
        return;
    const gfx::PipelineHandle edgePipeline = hdrFrame_
        ? sceneSelectionEdgePipeHdr_ : sceneSelectionEdgePipe8_;
    if (!edgePipeline.valid()) return;
    bool any = false;
    for (const auto& item : modelItems_) if (item.outline.w > 0 && item.inView) {
        any = true;
        break;
    }
    if (!any) for (const auto& item : items_) if (item.selection.w > 0) {
        any = true;
        break;
    }
    if (!any) return;

    if(!sceneSelectionMask_.valid() ||
       sceneSelectionWidth_!=width || sceneSelectionHeight_!=height) {
        gfx::TextureDesc td;
        td.width=width;td.height=height;
        td.format=gfx::PixelFormat::RGBA8;
        td.renderTarget=true;
        td.filter=gfx::Filter::Nearest;
        td.label="Scene selected-object depth-tested mask";
        const auto replacement=dev_->createTexture(td);
        if(!replacement.valid())return; // keep old mask for its in-flight work
        retireRenderTargets(sceneSelectionMask_);
        sceneSelectionMask_=replacement;
        sceneSelectionWidth_=width;
        sceneSelectionHeight_=height;
    }
    if (!sceneSelectionMask_.valid()) return;

    // Renderer3D::render normally leaves the main pass open. Temporarily
    // suspend it, draw visible selected geometry using the same scene depth,
    // then resume the colour pass WITHOUT clearing its contents or depth.
    dev_->endPass();
    gfx::PassDesc mask;
    mask.colorTarget = sceneSelectionMask_;
    mask.depthTarget = mainPass.depthTarget;
    mask.clearColor[0] = mask.clearColor[1] = mask.clearColor[2] =
        mask.clearColor[3] = 0.0f;
    mask.clearDepthEnabled = false;
    dev_->beginPass(mask);
    dev_->setViewport({0, 0, width, height});

    // Existing model skin/morph/LOD pipelines draw a second pass with zero
    // normal displacement. Cull backfaces and do not write the scene depth.
    selectionMaskPass_ = true;
    uploadFrameUniforms(false);
    renderModels(3);
    selectionMaskPass_ = false;

    // Primitive previews use their original geometry, not bounding-box lines.
    bool bound = false;
    for (const auto& item : items_) {
        if (item.selection.w <= 0 || !item.mesh || !item.mesh->vb.valid())
            continue;
        if (!bound) {
            if (!sceneSelectionPrimitiveMaskPipe_.valid()) break;
            dev_->setPipeline(sceneSelectionPrimitiveMaskPipe_);
            uploadFrameUniforms(false);
            bound = true;
        }
        DrawUniforms u{};
        std::memcpy(u.model, item.model.m, sizeof u.model);
        u.color[0] = item.selection.x;
        u.color[1] = item.selection.y;
        u.color[2] = item.selection.z;
        u.color[3] = 1.0f;
        u.params[0] = 2.0f;  // existing solid unlit/diagnostic branch
        dev_->setUniforms(0, &u, sizeof u);
        dev_->setVertexBuffer(item.mesh->vb);
        dev_->setIndexBuffer(item.mesh->ib);
        dev_->drawIndexed(item.mesh->indexCount);
        ++drawCalls_;
    }
    dev_->endPass();

    gfx::PassDesc resume = mainPass;
    resume.clearColorEnabled = false;
    resume.clearDepthEnabled = false;
    dev_->beginPass(resume);
    dev_->setViewport({0, 0, width, height});

    dev_->setPipeline(edgePipeline);
    dev_->setVertexBuffer(sceneSelectionTriangle_);
    const float params[4] = {
        1.0f / float(width), 1.0f / float(height),
        dev_->info().renderTargetFlipY ? 1.0f : 0.0f, 0.0f
    };
    dev_->setUniforms(0, params, sizeof params);
    dev_->setTexture(0, sceneSelectionMask_);
    dev_->draw(3);
    ++drawCalls_;
}

} // namespace aiverse::render
