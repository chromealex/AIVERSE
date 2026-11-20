// Minimal native-project entry point for LumaWeave frame orchestration.
// Does NOT own Device::beginFrame()/endFrame(): those are platform-owned and
// may include multiple viewports. Does NOT mutate authoritative ECS.
// Surface drawer callback queues the actual models/terrain/sprites in the
// usual Renderer3D path after beginScene and before render.
// Host is responsible for correct screen pass and optional UI after resolve.
#pragma once
#include "LumaWeaveNativeCoordinator.h"
#include "GiGpuCascadeRuntime.h"
#include "GiDynamicRuntime.h"
#include "LumaWeaveBackendPolicy.h"
#include "LumaWeaveVulkanPostProcess.h"
#include <cstdint>
#include <span>
#include <utility>

namespace aiverse::render {

struct LumaNativeViewInput {
    RTSCamera camera{};
    uint32_t width=0,height=0;
    float visibleLightRadius=100;
    LumaWeaveQuality quality=LumaWeaveQuality::Balanced;
    // Ray BVH is optional for GLES/Vulkan/legacy raster lighting. When absent,
    // geometryRevision MUST be zero. The Metal direct path requires both.
    uint64_t geometryRevision=0;
    const GiGpuBvh* sceneGeometry=nullptr;  // immutable shared ray-query BVH

    // Optional INDIRECT lighting (separate bounced term). The host owns the
    // cascade runtime and MUST have published its own geometry revision before
    // this frame. This does not make direct light the GI probe source.
    GiGpuCascadeRuntime* indirectGi=nullptr;
    // Portable Vulkan, GLES3, GL33 and Metal CPU-traced L1 probe fallback.
    // Initialize it explicitly with RGBA32F support and publish immutable
    // GiTraceGeometryView externally. This is bounded, not full-world RT.
    GiDynamicRuntime* indirectCpuGi=nullptr;
    // Opt-in fail-closed world revision check for portable CPU-probe GI.
    // Pass the CURRENT immutable trace scene generation on Vulkan/GLES.
    // Zero retains older host behavior, but still rejects absent geometry.
    uint64_t expectedCpuGiGeometryRevision=0;
    size_t giProbeBudget=64;
    // Explicit opt-in: export only GI-eligible point lights (max 16) from
    // LumaWeave. False preserves externally authored GI lights unchanged.
    bool synchronizeGiPointSources=false;
    std::span<const LumaLight> authored{};
    std::span<const LumaFxSystemSnapshot> fxSystems{};
    std::span<const LumaEffectLight> effectLights{};
    std::span<const LumaAreaEmitter> areaLights{};
    bool allowMobileFallback=true;
    // Opt-in Vulkan GPU Direct with real Renderer3D material/surface captures.
    // Host must certify that all required opaque custom renderers supply
    // matching receiver passes, otherwise keep the raster LightSystem path.
    bool enableVulkanDirect=false;
    bool vulkanReceiverCoverageConfirmed=false;
    // Spatial filtering is enabled with Vulkan Direct. Temporal reuse needs
    // a NONZERO unique camera+world key. Bump the epoch on teleport/cut.
    uint64_t vulkanHistoryKey=0,vulkanHistoryEpoch=0;
    LumaVulkanDenoiseSettings vulkanDenoise{};
};

enum class LumaIndirectRoute : uint8_t {
    Disabled=0, MetalCascades=1, CpuProbes=2
};

struct LumaNativeViewResult {
    LumaNativeFrameStats sources{};
    LumaLightingBackendCaps backendCapabilities{};
    LumaIndirectRoute indirectRoute=LumaIndirectRoute::Disabled;
    size_t cpuGiProbeBudgetAdmitted=0;
    bool cpuGiBudgetClamped=false;
    GiRuntimeStats cpuGiFrame{};        // portable CPU probe trace/upload
    LumaWeaveScreenMetal::Completion priorGpuCompletion{};
    GiCascadeFrame giFrame{};           // submitted/dirty after point sync
    GiCascadeFrame giPollFrame{};       // previous GPU completions before sync
    LumaGiSyncStatus giSourceSync{};    // meaningful if sync was requested
    bool giRequested=false;
    bool giBound=false;
    bool giGeometryMismatch=false;     // missing or stale portable trace BVH
    bool giDeferred=false;             // no stale bounced light on failed sync
    bool submittedFrame=false;
    bool vulkanDirectRequested=false,vulkanDirectSelected=false;
    bool vulkanFrameComposited=false,vulkanPreviousFrameCompleted=false;
    bool vulkanMaterialMissing=false,vulkanSurfaceMissing=false;
    bool vulkanDenoiseEncoded=false,vulkanTemporalHistoryUsed=false;
    LumaVulkanHistoryReason vulkanHistoryReason=LumaVulkanHistoryReason::Disabled;
    LumaWeaveVulkanDirect::Submission vulkanSubmission{};
};

class LumaWeaveNativeDriver {
public:
    LumaWeaveNativeDriver(gfx::Device& device,Renderer3D& renderer,
                         PostProcess& existing)
        :device_(device),renderer_(renderer),existing_(existing),
         post_(existing,renderer.lights),
         coordinator_(post_,existing,renderer.lights),
         vulkanPost_(existing,renderer.lights,vulkanDirect_) {}
    LumaWeaveNativeDriver(const LumaWeaveNativeDriver&)=delete;
    LumaWeaveNativeDriver& operator=(const LumaWeaveNativeDriver&)=delete;

    bool initialize(){
        if(device_.info().backend==gfx::Backend::Vulkan) {
            // Optional producer failure does not disable the existing raster
            // direct + CPU GI fallback on a Vulkan GPU with weak features.
            if(vulkanDirect_.initialize(device_)) {
                if(!vulkanPost_.initialize(device_))
                    (void)vulkanDirect_.shutdown();
            }
            return true;
        }
        // On weak/mobile backends failure leaves old LightSystem available.
        return post_.initialize(device_);
    }
    bool shutdown(){
        if(!vulkanPost_.shutdown())return false;
        if(!vulkanDirect_.shutdown())return false;
        if(!post_.shutdown())return false;
        resetGiSourceSync();
        lastGiRuntime_=nullptr;
        lastCpuGiRuntime_=nullptr;
        lastGiSynchronization_=false;
        return true;
    }
    LumaWeavePostProcess& gpuPost(){return post_;}
    LumaWeaveVulkanDirect& vulkanDirect(){return vulkanDirect_;}
    LumaWeaveVulkanPostProcess& vulkanPost(){return vulkanPost_;}
    LumaWeaveNativeCoordinator& coordinator(){return coordinator_;}
    // Explicitly call when an external host replaces/reinitializes the same
    // GiGpuCascadeRuntime object at the SAME address.
    void resetGiSourceSync(){
        coordinator_.resetGiSourceSync();
        cpuGiSourceRevision_=0;
        cpuGiSourcesPublished_=false;
    }

    // Requires the host has ALREADY called device.beginFrame and completed
    // ECS/SpectraFX read barriers. One view at a time. The submitted callback
    // may render opaque/transparent native models and user terrain normally.
    template<class SubmitScene>
    LumaNativeViewResult frame(const LumaNativeViewInput& input,
                               const gfx::PassDesc& output,
                               SubmitScene&& submitScene) {
        if(!input.width || !input.height ||
           (input.sceneGeometry==nullptr)!=(input.geometryRevision==0) ||
           (input.synchronizeGiPointSources &&
            !input.indirectGi && !input.indirectCpuGi))
            throw std::invalid_argument(
                "LumaWeave frame requires nonzero view and coherent optional ray BVH revision");
        LumaNativeViewResult result;
        result.backendCapabilities=LumaWeaveBackendPolicy::inspect(device_);
        result.priorGpuCompletion=post_.pollGpu(); // callbacks from prior beginFrame
        result.vulkanDirectRequested=input.enableVulkanDirect;
        result.vulkanPreviousFrameCompleted=vulkanDirect_.poll();
        // The GI adapter caches the accepted registry revision. A new GI
        // runtime may have zero lights despite an identical source revision;
        // republish on runtime identity change or when sync is re-enabled.
        if(lastGiRuntime_!=input.indirectGi ||
           lastCpuGiRuntime_!=input.indirectCpuGi ||
           (input.synchronizeGiPointSources && !lastGiSynchronization_)) {
            resetGiSourceSync();
        }
        lastGiRuntime_=input.indirectGi;
        lastCpuGiRuntime_=input.indirectCpuGi;
        lastGiSynchronization_=input.synchronizeGiPointSources;
        coordinator_.collectScene(input.authored,input.fxSystems,
                                  input.effectLights,input.areaLights);
        renderer_.beginScene(input.camera,int(input.width),int(input.height));
        // beginScene() intentionally does not clear borrowed GI overlays:
        // reset them for EVERY view, including when this frame has no GI.
        // Also release borrowed pointers on exceptions and on frame exit.
        renderer_.clearDynamicGiOverlay();
        struct ScopedGiOverlay {
            Renderer3D& renderer;
            ~ScopedGiOverlay(){renderer.clearDynamicGiOverlay();}
        } scopedGi{renderer_};
        // Opt-in only: custom opaque material + normal receivers must match
        // the actual frontmost scene. Never assume terrain/FX are ModelDraw.
        bool vulkanChosen=false;
        if(input.enableVulkanDirect &&
           device_.info().backend==gfx::Backend::Vulkan)
            renderer_.lights.setLinearScene(existing_.linearScene());
        LumaScreenSettings vulkanLimits;
        vulkanLimits.maxPixels=LumaWeaveVulkanDirect::kMaxScreenPixels;
        vulkanLimits.maxSamples=LumaWeaveVulkanDirect::kMaxSamples;
        vulkanLimits.maxCandidateEvaluations=
            LumaWeaveVulkanDirect::kMaxScreenCandidateChecks;
        const auto decision=LumaWeaveQualityPolicy::choose(
            input.quality,input.width,input.height,
            coordinator_.registry().lights().size(),vulkanLimits);
        const bool tryVulkan=input.enableVulkanDirect &&
            input.vulkanReceiverCoverageConfirmed &&
            device_.info().backend==gfx::Backend::Vulkan &&
            input.sceneGeometry && input.geometryRevision &&
            !coordinator_.registry().lights().empty() &&
            vulkanPost_.ready() && vulkanDirect_.screenAvailable() &&
            !vulkanDirect_.busy() && existing_.active() &&
            existing_.linearScene() && decision.admitted &&
            renderer_.lights.count()==0 &&
            !renderer_.lights.staticPointLightsActive();
        result.sources=coordinator_.prepareFrame(
            input.sceneGeometry,input.geometryRevision,input.camera.target,
            input.visibleLightRadius,input.width,input.height,input.quality,
            input.allowMobileFallback && !tryVulkan);
        if(tryVulkan) {
            const bool sources=vulkanDirect_.setSources(coordinator_.registry());
            const bool geometry=sources && vulkanDirect_.setGeometry(
                *input.sceneGeometry,input.geometryRevision);
            if(sources && geometry) {
                vulkanPost_.setQuality(input.quality);
                vulkanPost_.setDenoiseSettings(input.vulkanDenoise);
                vulkanPost_.setHistoryIdentity(input.vulkanHistoryKey,
                                               input.vulkanHistoryEpoch);
                vulkanPost_.setEnabled(true);
                vulkanChosen=true;
                result.sources.route=LumaDirectRoute::VulkanGpu;
                result.sources.gpuExclusive=true;
                result.sources.backendUnavailable=false;
            } else if(input.allowMobileFallback) {
                // GPU publication may be deferred: never sample stale
                // lighting, and use exactly ONE legacy raster direct route.
                result.sources=coordinator_.prepareFrame(
                    input.sceneGeometry,input.geometryRevision,input.camera.target,
                    input.visibleLightRadius,input.width,input.height,input.quality,
                    true);
            }
        }
        if(!vulkanChosen) {
            // The Vulkan backend may have two frames in flight. It is safe
            // to use raster direct for THIS frame while the previous GPU
            // dispatch completes without invalidating that previous history
            // ONLY when the native view/world/source/BVH identities match.
            // A real route disable, camera cut or source/scene change resets it.
            const bool samePendingView=input.enableVulkanDirect &&
                input.vulkanReceiverCoverageConfirmed &&
                device_.info().backend==gfx::Backend::Vulkan &&
                vulkanDirect_.busy() && input.vulkanHistoryKey!=0 &&
                input.vulkanHistoryKey==vulkanPost_.historyKey() &&
                input.vulkanHistoryEpoch==vulkanPost_.historyEpoch() &&
                coordinator_.registry().revision()==vulkanDirect_.sourceRevision() &&
                input.sceneGeometry && input.geometryRevision &&
                input.geometryRevision==vulkanDirect_.geometryRevision();
            vulkanPost_.setEnabled(false,samePendingView);
        }
        result.vulkanDirectSelected=vulkanChosen;
        // Metal can run cascaded GPU GI. Vulkan now supports opt-in GPU
        // DIRECT via its compute RHI, but portable bounced GI remains an
        // independent bounded CPU L1-probe path. GLES3 has no compute ABI.
        result.giRequested=input.indirectGi || input.indirectCpuGi;
        const bool gpuGi=device_.info().backend==gfx::Backend::Metal &&
                         input.indirectGi && input.indirectGi->ready();
        const bool portableGiReady=input.indirectCpuGi &&
            result.backendCapabilities.portableCpuProbeAtlas &&
            input.indirectCpuGi->ready();
        const bool portableGeometryCurrent=portableGiReady &&
            LumaWeaveBackendPolicy::portableGiGeometryCurrent(
                input.indirectCpuGi->hasTraceGeometry(),
                input.indirectCpuGi->geometryRevision(),
                input.expectedCpuGiGeometryRevision);
        const bool cpuGi=!gpuGi && portableGeometryCurrent;
        // A stale/world-mismatched CPU atlas must NEVER be bound on this
        // viewport. Direct Metal/raster light selection remains unaffected.
        result.giGeometryMismatch=!gpuGi && portableGiReady &&
                                  !portableGeometryCurrent;
        if(gpuGi) {
            auto& gi=*input.indirectGi;
            result.indirectRoute=LumaIndirectRoute::MetalCascades;
            // Poll actual Metal completion callbacks BEFORE attempting to
            // republish moving bounce lights. No old-light GI is displayed
            // while a newer source revision is waiting for a GPU fence.
            if(input.synchronizeGiPointSources) {
                result.giPollFrame=gi.updateFrame(0,input.camera.target);
                result.giSourceSync=coordinator_.syncGi(gi);
                result.giDeferred=!result.giSourceSync.accepted;
            }
            if(!result.giDeferred) {
                result.giFrame=gi.updateFrame(input.giProbeBudget,
                                              input.camera.target);
                result.giBound=gi.bindCascades(renderer_);
            }
        } else if(cpuGi) {
            auto& gi=*input.indirectCpuGi;
            result.indirectRoute=LumaIndirectRoute::CpuProbes;
            if(input.synchronizeGiPointSources) {
                // The CPU transport has 64 moving-light capacity and accepts
                // source snapshots synchronously. Exceptions (spots/overflow)
                // fail closed BEFORE binding a previous atlas.
                const auto revision=coordinator_.registry().revision();
                const auto next=lumaGiPointSources(coordinator_.registry(),64);
                result.giSourceSync.bouncedSources=next.size();
                if(!cpuGiSourcesPublished_ || cpuGiSourceRevision_!=revision) {
                    gi.setLights(next);
                    cpuGiSourceRevision_=revision;
                    cpuGiSourcesPublished_=true;
                } else result.giSourceSync.unchanged=true;
                result.giSourceSync.accepted=true;
                result.giSourceSync.publishedSourceRevision=cpuGiSourceRevision_;
            }
            result.cpuGiProbeBudgetAdmitted=LumaWeaveBackendPolicy::cpuBudget(
                device_.info().backend,input.giProbeBudget);
            result.cpuGiBudgetClamped=
                result.cpuGiProbeBudgetAdmitted<input.giProbeBudget;
            result.cpuGiFrame=gi.update(result.cpuGiProbeBudgetAdmitted,
                                        input.camera.target);
            result.giBound=gi.bind(renderer_);
        } else if(result.giRequested) {
            result.giDeferred=true;
        }
        std::forward<SubmitScene>(submitScene)(renderer_);
        PostProcess* selected=vulkanChosen ?
            static_cast<PostProcess*>(&vulkanPost_) :
            &coordinator_.activePost();
        renderer_.render(output,selected);
        renderer_.resolvePostFx();
        if(vulkanChosen) {
            const auto& vk=vulkanPost_.stats();
            result.vulkanSubmission=vk.submission;
            result.vulkanFrameComposited=vk.composited;
            result.vulkanDenoiseEncoded=vk.denoiseEncoded;
            result.vulkanTemporalHistoryUsed=vk.temporalHistoryUsed;
            result.vulkanHistoryReason=vk.historyReason;
            result.vulkanMaterialMissing=!vk.materialAvailable;
            result.vulkanSurfaceMissing=!vk.surfaceAvailable;
            if(!vk.composited) {
                vulkanDirect_.invalidateHistory();
                result.sources.route=LumaDirectRoute::Disabled;
                result.sources.gpuExclusive=false;
            }
        }
        result.submittedFrame=true;
        return result;
    }

private:
    gfx::Device& device_;
    Renderer3D& renderer_;
    PostProcess& existing_;
    LumaWeavePostProcess post_;
    LumaWeaveNativeCoordinator coordinator_;
    LumaWeaveVulkanDirect vulkanDirect_;
    LumaWeaveVulkanPostProcess vulkanPost_;
    GiGpuCascadeRuntime* lastGiRuntime_=nullptr; // borrowed, identity only
    GiDynamicRuntime* lastCpuGiRuntime_=nullptr; // borrowed, identity only
    uint64_t cpuGiSourceRevision_=0;
    bool cpuGiSourcesPublished_=false;
    bool lastGiSynchronization_=false;
};

} // namespace aiverse::render
