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
};

class LumaWeaveNativeDriver {
public:
    LumaWeaveNativeDriver(gfx::Device& device,Renderer3D& renderer,
                         PostProcess& existing)
        :device_(device),renderer_(renderer),existing_(existing),
         post_(existing,renderer.lights),
         coordinator_(post_,existing,renderer.lights) {}
    LumaWeaveNativeDriver(const LumaWeaveNativeDriver&)=delete;
    LumaWeaveNativeDriver& operator=(const LumaWeaveNativeDriver&)=delete;

    bool initialize(){
        // On weak/mobile backends this fails cleanly and the existing
        // LightSystem/lighting path remains fully available.
        return post_.initialize(device_);
    }
    bool shutdown(){
        if(!post_.shutdown())return false;
        resetGiSourceSync();
        lastGiRuntime_=nullptr;
        lastCpuGiRuntime_=nullptr;
        lastGiSynchronization_=false;
        return true;
    }
    LumaWeavePostProcess& gpuPost(){return post_;}
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
        result.sources=coordinator_.prepareFrame(
            input.sceneGeometry,input.geometryRevision,input.camera.target,
            input.visibleLightRadius,input.width,input.height,input.quality,
            input.allowMobileFallback);
        // Prefer Metal GPU cascades when actually supported, otherwise use
        // the explicitly initialized CPU-traced L1 probe overlay. Vulkan and
        // GLES3 do not implement the gfx::Device compute ABI today; their
        // normal path is raster direct + CPU GI, not a fake GPU dispatch.
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
        renderer_.render(output,&coordinator_.activePost());
        renderer_.resolvePostFx();
        result.submittedFrame=true;
        return result;
    }

private:
    gfx::Device& device_;
    Renderer3D& renderer_;
    PostProcess& existing_;
    LumaWeavePostProcess post_;
    LumaWeaveNativeCoordinator coordinator_;
    GiGpuCascadeRuntime* lastGiRuntime_=nullptr; // borrowed, identity only
    GiDynamicRuntime* lastCpuGiRuntime_=nullptr; // borrowed, identity only
    uint64_t cpuGiSourceRevision_=0;
    bool cpuGiSourcesPublished_=false;
    bool lastGiSynchronization_=false;
};

} // namespace aiverse::render
