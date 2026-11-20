// Genre-independent host-facing dynamic indirect lighting owner.
// Chooses bounded Vulkan/Metal compute only when the concrete RHI supports it;
// all other devices (including GLES3/WebGL2) use the CPU reference and sparse
// sampled RGBA32F atlas. A caller must still supply updated immutable geometry,
// stable light identities, and explicitly opt in before displaying this overlay.
// Never persists ECS state; never modifies direct point/sun light ownership.
#pragma once
#include "GiDynamicRuntime.h"
#include "GiGpuDynamicRuntime.h"
#include "GiLightBridge.h"
#include "GiEnvironmentBridge.h"
#include "GiGpuResidencyPlanner.h"
#include "GiBorrowedPresentation.h"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::render {

enum class GiTransportMode : uint8_t { Off, CpuProbes, GpuCompute };

struct GiTransportConfig {
    float3 origin{};
    float spacing=1;
    int nx=8,ny=4,nz=8;
    uint32_t raysPerProbe=16;
    float traceDistance=12;
    bool preferGpu=true;
    double cpuFrameBudgetMs=1.25;
    size_t cpuMaxProbes=4;
    // Conservative maximum range of GPU local point/spot secondary shadow
    // rays. A larger source is rejected rather than silently dropping
    // relevant occluder chunks beyond the resident GPU BVH volume.
    float maxGpuMovingLightRadius=64;
    // Explicit opt-in: when the bounded GPU BVH cannot admit ALL relevant
    // geometry, migrate the complete immutable source to bounded CPU GI.
    // This is a one-way quality fallback, not hidden GPU triangle truncation.
    bool cpuFallbackOnLocalGpuOverflow=false;
};

class GiLightingController {
public:
    struct StepStats {
        GiTransportMode mode=GiTransportMode::Off;
        GiRuntimeStats cpu{};
        GiGpuDynamicRuntime::FrameStats gpu{};
        size_t pending=0;
        bool canPresent=false;
    };

    GiLightingController()=default;
    GiLightingController(const GiLightingController&)=delete;
    GiLightingController& operator=(const GiLightingController&)=delete;

    // Resources may not be destroyed while a Vulkan/Metal GPU batch is pending.
    // If shutdown returns false, keep calling updateFrame() until the fence is
    // complete, unbind any Renderer3D, then shutdown again.
    bool shutdown() {
        // Renderer3D still owns a borrowed reference to the atlas. Never
        // destroy or replace the owner until the host unbinds every reader.
        if (!borrowers_.canRetire()) return false;
        if (mode_==GiTransportMode::GpuCompute && !gpu_.shutdown()) return false;
        if (mode_==GiTransportMode::CpuProbes) cpu_.shutdown();
        scene_.reset();
        mode_=GiTransportMode::Off;
        device_=nullptr;
        revision_=0;
        gpuResidency_={};
        pendingGeometry_=false;
        pendingLights_=false;
        pendingEnvironment_=false;
        return true;
    }

    bool initialize(gfx::Device& device, GiTransportConfig cfg) {
        if (!shutdown()) return false;
        if (cfg.cpuMaxProbes==0 || cfg.cpuMaxProbes>1024 ||
            cfg.raysPerProbe<8 || cfg.raysPerProbe>GiGpuSmallScene::kMaxSamples ||
            !std::isfinite(cfg.maxGpuMovingLightRadius) ||
            cfg.maxGpuMovingLightRadius<0 || cfg.maxGpuMovingLightRadius>1000)
            return false;
        // The same config/dimensions must be used when failing over to CPU,
        // preserving the geometric location of each probe.
        if (cfg.preferGpu &&
            (device.info().backend==gfx::Backend::Metal ||
             device.info().backend==gfx::Backend::Vulkan) &&
            gpu_.initialize(device,cfg.origin,cfg.spacing,cfg.nx,cfg.ny,cfg.nz,
                            cfg.raysPerProbe,cfg.traceDistance)) {
            device_=&device;
            config_=cfg;
            mode_=GiTransportMode::GpuCompute;
            return true;
        }
        // No compute support is a NORMAL condition on GLES3 / WebGL2.
        // CPU GI only requires sampled RGBA32F, not floating-point MRT/HDR.
        try {
            cpu_.cpuTraceBudget().configure(cfg.cpuFrameBudgetMs,cfg.cpuMaxProbes);
            if (!cpu_.initialize(device,cfg.origin,cfg.spacing,cfg.nx,cfg.ny,
                                 cfg.nz,int(cfg.raysPerProbe),cfg.traceDistance))
                return false;
        } catch (const std::invalid_argument&) { return false; }
        device_=&device;
        config_=cfg;
        mode_=GiTransportMode::CpuProbes;
        return true;
    }

    GiTransportMode mode() const { return mode_; }
    bool ready() const { return mode_!=GiTransportMode::Off; }
    bool gpuBusy() const {
        return mode_==GiTransportMode::GpuCompute && gpu_.busy();
    }
    size_t maxMovingLights() const {
        return mode_==GiTransportMode::GpuCompute ? GiGpuSmallScene::kMaxLights :
               mode_==GiTransportMode::CpuProbes ? 64 : 0;
    }
    const GiTransportConfig& config() const { return config_; }
    const GiGpuResidencyPlan& gpuResidency() const { return gpuResidency_; }
    bool presentationBlocked() const {
        return pendingGeometry_ || pendingLights_ || pendingEnvironment_;
    }
    size_t borrowedRendererCount() const { return borrowers_.readers(); }

    // Publisher owns a read-barrier-frozen immutable scene snapshot. The
    // controller retains its shared ownership across all CPU pointer borrows.
    // GPU publication may refuse while the previous dispatch is pending.
    bool setGeometry(std::shared_ptr<const GiChunkedGeometrySnapshot> next,
                     uint64_t revision) {
        if (!ready() || !next || revision==0 || revision<=revision_) return false;
        if (mode_==GiTransportMode::GpuCompute) {
            auto resident=planGpuResident(*next);
            if(!resident.admitted) {
                gpuResidency_=std::move(resident);
                if(gpuResidency_.exceededBudget &&
                   tryCpuOverflowFallback(next,revision))
                    return true;
                pendingGeometry_=true; // never sample old bounce on new world
                return false;
            }
            if(!gpu_.setGeometry(resident.selected,revision)) {
                pendingGeometry_=true; // e.g. old command still in flight
                return false;
            }
            gpuResidency_=std::move(resident);
        } else {
            cpu_.setTraceGeometry(next->traceView(),revision);
        }
        scene_=std::move(next);
        revision_=revision;
        pendingGeometry_=false;
        return true;
    }

    bool setGeometryRegion(std::shared_ptr<const GiChunkedGeometrySnapshot> next,
                           uint64_t revision,GiBounds changed,
                           float extraRadius=0) {
        if (!ready() || !next || !scene_ || revision==0 ||
            revision<=revision_) return false;
        if (mode_==GiTransportMode::GpuCompute) {
            auto resident=planGpuResident(*next);
            if(!resident.admitted) {
                gpuResidency_=std::move(resident);
                if(gpuResidency_.exceededBudget &&
                   tryCpuOverflowFallback(next,revision))
                    return true;
                pendingGeometry_=true;
                return false;
            }
            if (!gpu_.setGeometryRegion(resident.selected,revision,changed,
                                        extraRadius)) {
                pendingGeometry_=true;
                return false;
            }
            gpuResidency_=std::move(resident);
        } else {
            cpu_.setTraceGeometryRegion(next->traceView(),revision,
                                        changed,extraRadius);
        }
        scene_=std::move(next);
        revision_=revision;
        pendingGeometry_=false;
        return true;
    }

    // GPU supports 16 moving lights, CPU supports 64; never silently truncate
    // incoming authored sources and change the actual lighting design.
    bool setLights(std::vector<GiMovingLight> lights) {
        if (!ready())return false;
        // An updated direct source may already be visible in the scene.
        // Never render an older indirect delta when its publication fails.
        pendingLights_=true;
        if(lights.size()>maxMovingLights())return false;
        if(mode_==GiTransportMode::GpuCompute) {
            for(const auto& light:lights)
                if(!std::isfinite(light.radius) ||
                   light.radius>config_.maxGpuMovingLightRadius)
                    return false; // GPU resident bounds are not guaranteed
            if(!gpu_.setLights(std::move(lights)))return false;
        }else cpu_.setLights(std::move(lights));
        pendingLights_=false;
        return true;
    }

    // Environmental first bounce is separately opted in: the caller MUST
    // exclude these sources from its baked static GI base (or regenerate it).
    // Zero sun/sky intensity keeps the legacy static ambient path unchanged.
    bool setEnvironment(GiEnvironmentLight environment) {
        if(!ready())return false;
        pendingEnvironment_=true;
        if(!environment.valid())return false;
        if(mode_==GiTransportMode::GpuCompute) {
            if(!gpu_.setEnvironment(environment))return false;
        }else (void)cpu_.setEnvironment(environment);
        pendingEnvironment_=false;
        return true;
    }

    // Capture the ordinary per-scene sun/sky only when the host EXPLICITLY
    // authorizes indirect bounce. Passing both multipliers as zero is a safe
    // no-op on default/off source state and avoids counting baked sky twice.
    bool syncRendererEnvironment(const Renderer3D& renderer,
                                 float sunBounceMultiplier=0,
                                 float skyBounceMultiplier=0) {
        if(!ready())return false;
        pendingEnvironment_=true; // conversion itself may reject or throw
        return setEnvironment(giEnvironmentFromLighting(
            renderer.lighting,sunBounceMultiplier,skyBounceMultiplier));
    }

    // Use the same lights authored for the normal direct lighting path.
    // Only sources with indirectMultiplier>0 participate; static/baked lamps
    // stay excluded by default. Source namespace is stable per world/owner.
    bool setAuthoredLights(const std::vector<PointLight>& source,
                          uint32_t namespaceId,float3 focus={},
                          GiLightOverflowPolicy policy=GiLightOverflowPolicy::Reject,
                          GiLightBridge::Stats* stats=nullptr) {
        if(!ready())return false;
        // The authoring bridge can throw on duplicate/invalid stable IDs.
        // Mark the old atlas unsafe BEFORE converting the newer source set.
        pendingLights_=true;
        auto converted=GiLightBridge::collect(
            source,namespaceId,maxMovingLights(),focus,policy,stats);
        return setLights(std::move(converted));
    }

    // Called AFTER Renderer3D::lights.add() for the current scene and BEFORE
    // the GI updateFrame/render pass. This copies the stable source descriptors,
    // so no gameplay/ECS pointers escape into an asynchronous GPU submission.
    // Spot cone and opt-in indirectMultiplier remain the source of truth.
    bool syncRendererLights(const Renderer3D& renderer,uint32_t namespaceId,
                            float3 focus={},
                            GiLightOverflowPolicy policy=GiLightOverflowPolicy::Reject,
                            GiLightBridge::Stats* stats=nullptr) {
        return setAuthoredLights(renderer.lights.submittedLights(),
                                namespaceId,focus,policy,stats);
    }

    // Geometry, vegetation, material and weather publishers can use one
    // shared event accumulator. GPU invalidation preserves in-flight revision
    // safety; CPU invalidation additionally clears its old atlas coefficients.
    // For changed geometry/material reflectance, republish BVH/albedo first.
    GiWorldDirtyAccumulator::Stats applyWorldChanges(
            GiWorldDirtyAccumulator& changes) {
        if(mode_==GiTransportMode::GpuCompute)
            return gpu_.applyWorldChanges(changes);
        if(mode_==GiTransportMode::CpuProbes)
            return cpu_.applyWorldChanges(changes);
        return {};
    }

    void invalidate(GiBounds bounds,GiDirtyReason reason,
                    float influenceRadius=0) {
        if(mode_==GiTransportMode::GpuCompute)
            (void)gpu_.invalidate(bounds,reason,influenceRadius);
        else if(mode_==GiTransportMode::CpuProbes)
            cpu_.invalidate(bounds,reason,influenceRadius);
    }

    // Call after the per-world publication barrier, between RHI beginFrame and
    // draw passes for GPU. CPU mode is also safe before a render pass.
    StepStats updateFrame(float3 focus,size_t gpuProbeBudget=32) {
        StepStats s;
        s.mode=mode_;
        if (mode_==GiTransportMode::GpuCompute) {
            s.gpu=gpu_.updateFrame(gpuProbeBudget,focus);
            s.pending=s.gpu.dirty;
            s.canPresent=s.gpu.safeForPresentation &&
                         bool(scene_) && !presentationBlocked();
        } else if (mode_==GiTransportMode::CpuProbes) {
            s.cpu=cpu_.updateAdaptive(focus);
            s.pending=s.cpu.dirty;
            s.canPresent=bool(scene_) && !presentationBlocked();
        }
        return s;
    }

    // Renderer3D borrows the atlas. Unbind BEFORE shutdown/reinitialization
    // and always after a rejected bind. Do not claim a successful GI frame if
    // no scene or the GPU's actual completion fence has not been observed.
    bool bind(Renderer3D& renderer) const {
        if (!scene_ || !ready() || presentationBlocked()) {
            renderer.clearDynamicGiOverlay();
            borrowers_.release(&renderer);
            return false;
        }
        // Register before passing the raw atlas into Renderer3D: if set
        // allocation fails there must never be a successfully bound but
        // untracked borrower that shutdown() could destroy underneath.
        (void)borrowers_.acquire(&renderer);
        const bool ok=mode_==GiTransportMode::GpuCompute
            ? gpu_.bind(renderer) : cpu_.bind(renderer);
        if (!ok) {
            renderer.clearDynamicGiOverlay();
            borrowers_.release(&renderer);
            return false;
        }
        return true;
    }
    void unbind(Renderer3D& renderer) const {
        renderer.clearDynamicGiOverlay();
        borrowers_.release(&renderer);
    }

    // Only for external Metal completion sources where readBufferAsync is
    // unavailable. Vulkan ignores this: its own VkFence serial is mandatory.
    bool confirmExternalGpuCompletion(uint64_t batchSerial,bool successful) {
        return mode_==GiTransportMode::GpuCompute &&
               gpu_.confirmExternalCompletion(batchSerial,successful);
    }

    // CPU reference for diagnostics; null for GPU transport (no CPU readback).
    const GiDynamicRuntime* cpuRuntime() const {
        return mode_==GiTransportMode::CpuProbes ? &cpu_ : nullptr;
    }
    const GiGpuDynamicRuntime* gpuRuntime() const {
        return mode_==GiTransportMode::GpuCompute ? &gpu_ : nullptr;
    }

private:
    // One-way GPU->CPU transport fallback for an over-budget LOCAL scene.
    // No queue may be in flight and the underlying device must be healthy.
    // Full immutable BVH, accepted point/spot sources and optional sun/sky
    // are migrated; no relevant occluder is lost or a baked owner duplicated.
    // The caller must unbind the old atlas from Renderer3D before a source
    // publication that may shut down the GPU owner.
    bool tryCpuOverflowFallback(
            const std::shared_ptr<const GiChunkedGeometrySnapshot>& full,
            uint64_t revision) {
        if(!config_.cpuFallbackOnLocalGpuOverflow ||
           mode_!=GiTransportMode::GpuCompute || !device_ ||
           !device_->gpuHealthy() || gpu_.busy() ||
           !borrowers_.canRetire() || !full)
            return false;
        try {
            cpu_.cpuTraceBudget().configure(config_.cpuFrameBudgetMs,
                                            config_.cpuMaxProbes);
            if(!cpu_.initialize(*device_,config_.origin,config_.spacing,
                                config_.nx,config_.ny,config_.nz,
                                int(config_.raysPerProbe),config_.traceDistance))
                return false;
            cpu_.setTraceGeometry(full->traceView(),revision);
            cpu_.setLights(gpu_.lights());
            (void)cpu_.setEnvironment(gpu_.environment());
        } catch(...) {
            cpu_.shutdown();
            return false;
        }
        // No in-flight fence may reference the GPU scene at this point.
        if(!gpu_.shutdown()) {
            cpu_.shutdown();
            return false;
        }
        mode_=GiTransportMode::CpuProbes;
        scene_=full;
        revision_=revision;
        pendingGeometry_=false;
        // Keep independently rejected light/environment gates until their
        // real authoritative revisions have been accepted by the CPU path.
        return true;
    }

    GiGpuResidencyPlan planGpuResident(
            const GiChunkedGeometrySnapshot& scene) const {
        return GiGpuResidencyPlanner::plan(
            scene,config_.origin,config_.spacing,config_.nx,
            config_.ny,config_.nz,config_.traceDistance,
            config_.maxGpuMovingLightRadius);
    }

    gfx::Device* device_=nullptr;
    GiTransportMode mode_=GiTransportMode::Off;
    GiTransportConfig config_{};
    uint64_t revision_=0;
    std::shared_ptr<const GiChunkedGeometrySnapshot> scene_;
    GiDynamicRuntime cpu_;
    GiGpuDynamicRuntime gpu_;
    GiGpuResidencyPlan gpuResidency_;
    // GPU may reject new world collision until its old frame completes or
    // local BVH admission budget is reduced. The old atlas is then stale
    // relative to already-published ECS/terrain geometry: do not display it.
    bool pendingGeometry_=false;
    bool pendingLights_=false;
    bool pendingEnvironment_=false;
    GiBorrowedPresentation borrowers_{};
};

} // namespace aiverse::render
