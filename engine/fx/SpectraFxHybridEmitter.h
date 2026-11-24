// SpectraFX opt-in hybrid presentation emitter.
// Selects exactly one owner (CPU ParticleSystem or GPU slot pool) for a batch.
// A backend failure is explicit: no silent dual-simulation or mid-frame
// migration of authoritative state. Call only on the device/render thread.
#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include "SpectraFxExecutionPolicy.h"
#include "SpectraFxGpuParticleSlots.h"
#include "SpectraFxGpuImpactFeedback.h"
#include "SpectraFxGpuBillboardRenderer.h"
#include "SpectraFxGpuCompaction.h"
#include "SpectraFxGpuSpawnScheduler.h"
#include "SpectraFxGpuLod.h"
#include "ParticleSystem.h"

namespace aiverse::fx {

class SpectraFxHybridEmitter {
public:
    struct Config {
        SpectraFxBackendRequest backend;
        std::string cpuEffect;
        // Optional authored CPU sub-effect on visual GPU ground impact.
        // Requires environment.reportImpacts and is rate-capped by playback.
        std::string impactEffect;
        SpectraFxGpuBillboardRenderer::Appearance appearance{};
        SpectraFxGpuEnvironment environment{};
        SpectraFxGpuSpawnSettings spawning{};
        SpectraFxGpuLodSettings lod{};
        FxQuality quality=FxQuality::High;
        bool autoSpawnGpu=true;
        // Experimental Metal-only GPU live-slot compaction + indirect draw.
        // False by default until GPU/Metal acceptance; legacy draw is fallback.
        bool gpuIndirectCompaction=false;
        // Explicit experimental opt-in for alpha effects that can tolerate
        // unsorted GPU drawing. Otherwise alpha requires CPU sorting.
        bool allowUnsortedAlpha=false;
        // Explicit Metal GPU back-to-front alpha sort. CPU remains default.
        bool gpuSortAlpha=false;
        // How the emitter runs when the GPU backend is not selected:
        // Twin   = the SAME asset on the CPU twin of the compute kernel,
        //          drawn by the same billboard renderer (default);
        // Effect = legacy: play the separately authored cpuEffect.
        enum class CpuFallback : uint8_t { Twin, Effect };
        CpuFallback cpuFallback=CpuFallback::Twin;
    };

    static SpectraFxBackendRequest effectiveRequest(const Config& config) {
        auto request=config.backend;
        // Alpha blending without back-to-front ordering is not generally
        // correct. The current GPU billboards/compaction are unsorted.
        if(!config.appearance.additive) {
            if(config.gpuSortAlpha)
                request.required=request.required|
                                 SpectraFxFeature::Sorting|
                                 SpectraFxFeature::GpuDepthSort;
            else if(!config.allowUnsortedAlpha)
                request.required=request.required|SpectraFxFeature::Sorting;
        }
        return request;
    }

    // Begin an emitter with no GPU particles yet. GPU emits are explicit
    // on safe buffer update points; CPU fallback plays an authored effect.
    bool begin(gfx::Device& device,ParticleSystem& cpu,Config config,
               float3 origin={0,0,0},float3 direction={0,1,0},
               SpectraFxGpuBillboardRenderer* sharedRenderer=nullptr,
               gfx::ComputeHandle sharedEvolution={},
               gfx::ComputeHandle sharedAlphaSort={},
               gfx::ComputeHandle sharedCompaction={}) {
        stop();
        // Configure before allocating GPU resources: failed settings do not
        // replace or partially initialize any backend.
        if(!scheduler_.configure(config.spawning) || !config.lod.validate() ||
           (config.gpuSortAlpha && (config.appearance.additive ||
                                    config.gpuIndirectCompaction))) {
            status_="Invalid SpectraFX GPU spawn settings";
            return false;
        }
        cpu_=&cpu;
        config_=std::move(config);
        origin_=origin;
        direction_=direction;
        decision_=chooseSpectraFxBackend(device,effectiveRequest(config_));
        if(!decision_.accepted) {
            status_="requested backend unavailable and CPU fallback disabled";
            cpu_=nullptr;
            return false;
        }
        if(decision_.backend==SpectraFxExecutionBackend::Gpu) {
            if(slots_.init(device,config_.backend.particles,
                           config_.environment.reportImpacts,
                           sharedEvolution) &&
               (sharedRenderer ? sharedRenderer->ready() : renderer_.init(device))) {
                rendererOwner_=sharedRenderer ? sharedRenderer : &renderer_;
                gpuDevice_=&device;
                if(config_.gpuSortAlpha &&
                   !alphaSort_.init(device,config_.backend.particles,
                                    sharedAlphaSort)) {
                    // Cannot silently draw unsorted alpha when authored sorted
                    // alpha is required. Release the GPU path and let the
                    // existing authored CPU fallback start below.
                    rendererOwner_=nullptr;
                    gpuDevice_=nullptr;
                    slots_.shutdown();
                    renderer_.release();
                } else {
                    if(config_.gpuIndirectCompaction) {
                        // Metal-only unsorted live-particle compaction;
                        // any failure retains regular GPU instanced draw.
                        if(!compaction_.init(device,config_.backend.particles,
                                             sharedCompaction))
                            ++compactionFallbacks_;
                    }
                    backend_=SpectraFxExecutionBackend::Gpu;
                    status_="GPU emitter initialized; awaiting spawn at buffer-safe point";
                    return true;
                }
            }
            slots_.shutdown();
            renderer_.release();
            if(!config_.backend.allowCpuFallback) {
                status_="GPU allocation/shader failed and fallback disabled";
                cpu_=nullptr;
                return false;
            }
        }
        if(config_.cpuFallback==Config::CpuFallback::Twin ||
           config_.cpuEffect.empty()) {
            // Same authored asset, CPU twin of the compute kernel.
            if(slots_.init(device,config_.backend.particles,
                           config_.environment.reportImpacts,{},true) &&
               (sharedRenderer && sharedRenderer->ready() ? true : renderer_.init(device))) {
                rendererOwner_=sharedRenderer && sharedRenderer->ready()
                    ? sharedRenderer : &renderer_;
                gpuDevice_=&device;
                twin_=true;
                backend_=SpectraFxExecutionBackend::Cpu;
                status_=std::string("CPU twin selected: ")+
                        (decision_.reason?decision_.reason:"CPU");
                return true;
            }
            status_="CPU twin unavailable (RGBA32F texture/pipeline): "+
                    slots_.evolution().lastError()+renderer_.lastError();
            slots_.shutdown();
            renderer_.release();
            cpu_=nullptr;
            return false;
        }
        cpuHandle_=cpu.play(config_.cpuEffect,origin,direction);
        if(!cpuHandle_.valid()) {
            status_="CPU ParticleSystem rejected effect";
            cpu_=nullptr;
            return false;
        }
        backend_=SpectraFxExecutionBackend::Cpu;
        status_="CPU ParticleSystem selected";
        return true;
    }

    // GPU only: emits into dormant slots, safe update point outside passes.
    // The CPU path has its own authored spawning logic in ParticleSystem.
    SpectraFxGpuParticleHandle emitGpu(const SpectraFxGpuSeed& seed) {
        if(!slotsActive() || suspended_) return {};
        return slots_.emit(seed);
    }
    bool killGpu(SpectraFxGpuParticleHandle handle) {
        return slotsActive() && slots_.kill(handle);
    }

    // GPU only, between beginFrame/endFrame and outside rendering passes.
    // For CPU selection, caller must update the shared ParticleSystem once
    // for the entire scene; no CPU update is performed by this adapter.
    bool updateGpu(float dt) {
        if(!slotsActive() || suspended_) return false;
        // A GPU error detected on an earlier completed frame is never
        // replayed into this frame. Select the authored CPU visual path
        // before touching GPU storage or issuing another compute command.
        if(!twin_ && gpuDevice_ && !gpuDevice_->gpuHealthy()) {
            if(switchToCpuFallback("RHI GPU unhealthy")) return true;
            status_="RHI GPU unhealthy; no CPU fallback";
            suspended_=true;
            return false;
        }
        if(config_.autoSpawnGpu && dt>0.0f) {
            const bool scheduled=scheduler_.tick(dt,origin_,direction_,
                spectraFxSpawnQualityScale(std::min(config_.quality,globalQuality_))*
                lodSpawnScale_*externalSpawnScale_,
                [this](render::float3 pos,render::float3 vel,float life) {
                    return slots_.emit({pos,vel,0,life}).valid();
                });
            if(!scheduled) {status_="Invalid GPU emission frame";return false;}
        }
        SpectraFxGpuEnvironment fieldEnvironment=config_.environment;
        fieldEnvironment.acceleration.x+=externalAcceleration_.x;
        fieldEnvironment.acceleration.y+=externalAcceleration_.y;
        fieldEnvironment.acceleration.z+=externalAcceleration_.z;
        if(twin_) {
            if(!config_.appearance.additive && sortCameraValid_)
                slots_.evolution().setTwinSortView(sortCameraEye_,sortCameraForward_);
            if(slots_.step(dt,fieldEnvironment)) {
                if(config_.environment.reportImpacts && gpuDevice_)
                    impactFeedback_.request(*gpuDevice_,slots_);
                return true;
            }
            status_="CPU twin step failed";
            suspended_=true;
            return false;
        }
        if(slots_.step(dt,fieldEnvironment)) {
            // A single visible alpha particle needs no ordering. Culling
            // also suppresses sort work while particle lifetime still
            // advances on the GPU.
            if(config_.gpuSortAlpha && lodVisible_ &&
               slots_.activeCount()>1) {
                if(!sortCameraValid_ ||
                   !alphaSort_.prepare(slots_.evolution(),
                                       sortCameraEye_,sortCameraForward_)) {
                    if(switchToCpuFallback("GPU alpha sort failed")) return true;
                    status_="GPU alpha sorting failed; fallback unavailable";
                    suspended_=true; // never draw an unsorted alpha frame
                    return false;
                }
                ++sortedFrames_;
            }
            if(compaction_.ready() && lodVisible_ && slots_.activeCount()>0) {
                if(compaction_.prepare(slots_.evolution())) ++compactionFrames_;
                else ++compactionFallbacks_;
            }
            if(config_.environment.reportImpacts && gpuDevice_)
                impactFeedback_.request(*gpuDevice_,slots_);
            return true;
        }
        if(switchToCpuFallback("GPU compute step failed")) return true;
        status_="GPU compute step failed; no CPU fallback";
        suspended_=true;
        return false;
    }

    void setSortView(render::float3 eye,render::float3 forward) {
        sortCameraEye_=eye;
        sortCameraForward_=forward;
        sortCameraValid_=true;
    }

    // Per-view preparation; MUST run after the once-per-frame GPU update
    // and BEFORE the target's render pass opens. No spawn/age/compute step.
    // Rebuilds depth ordering for each camera rather than reusing camera A's
    // sort texture in camera B's offscreen pass.
    bool prepareRenderView(render::float3 eye,render::float3 forward) {
        if(!slotsActive() || suspended_) return true;
        const auto finite=[](render::float3 value) {
            return std::isfinite(value.x)&&std::isfinite(value.y)&&
                   std::isfinite(value.z);
        };
        if(!finite(eye)||!finite(forward)||!gpuDevice_ ||
           !gpuDevice_->canUpdateSharedBuffers()) return false;
        updateLod(eye);
        if(twin_) {
            if(config_.appearance.additive || !lodVisible_) return true;
            const float tl=render::length(forward);
            if(!std::isfinite(tl)||tl<=1e-6f) return false;
            setSortView(eye,forward*(1.0f/tl));
            slots_.evolution().setTwinSortView(sortCameraEye_,sortCameraForward_);
            return slots_.evolution().repackTwin();
        }
        if(!config_.gpuSortAlpha || !lodVisible_ ||
           slots_.activeCount()<=1) return true;
        const float fLength=render::length(forward);
        if(!std::isfinite(fLength)||fLength<=1e-6f) return false;
        forward=forward*(1.0f/fLength);
        setSortView(eye,forward);
        if(!alphaSort_.prepare(slots_.evolution(),eye,forward)) {
            // Preserve the existing strict sorted-alpha fallback policy.
            // CPU takeover is attempted only from this outside-pass point.
            if(switchToCpuFallback("GPU camera alpha sort failed")) return true;
            status_="GPU camera alpha sorting failed; fallback unavailable";
            suspended_=true;
            return false;
        }
        ++sortedFrames_;
        return true;
    }

    // External GPU load governor only scales new visual spawns, never
    // existing particle state/lifetime, gameplay events or emitter transforms.
    void setGlobalQuality(FxQuality quality) {globalQuality_=quality;}
    FxQuality globalQuality() const {return globalQuality_;}
    void setExternalSpawnScale(float scale) {
        if(std::isfinite(scale))
            externalSpawnScale_=std::clamp(scale,0.0f,1.0f);
    }

    // Applied equally to the Metal compute kernel and its CPU twin.
    // Never changes the authored asset or authoritative simulation state.
    // One sample per emitter/frame (from a host-owned read-only field).
    bool setExternalAcceleration(float3 acceleration) {
        const auto valid=[](float v) {
            return std::isfinite(v) && std::abs(v)<=1000.f;
        };
        if(!valid(acceleration.x) || !valid(acceleration.y) ||
           !valid(acceleration.z))return false;
        externalAcceleration_=acceleration;
        return true;
    }
    float3 origin() const {return origin_;}
    float3 externalAcceleration() const {return externalAcceleration_;}

    // Update only presentation LOD; the particle state and its clock still
    // advance outside visibility to preserve safe expiry/recycling.
    void updateLod(render::float3 cameraEye) {
        const float distance=spectraFxDistance(cameraEye,origin_);
        lodSpawnScale_=config_.lod.spawnScale(distance);
        lodVisible_=config_.lod.visible(distance);
    }

    // Update attachment transform before the next GPU spawn tick.
    void moveOrigin(float3 pos,float3 direction={0,1,0}) {
        origin_=pos;
        direction_=direction;
        if(cpuActive()) cpu_->move(cpuHandle_,pos,direction);
    }
    const SpectraFxGpuSpawnStats& spawnStats() const {return scheduler_.stats();}
    // Delivers only presentation records, on the RHI owner thread. Discarded
    // generations never produce a callback after particle-slot reuse.
    template<class Callback>
    void drainGpuImpacts(Callback&& callback,size_t maxEvents=128) {
        if(slotsActive())
            impactFeedback_.drain(slots_,std::forward<Callback>(callback),maxEvents);
    }
    const SpectraFxGpuImpactFeedback::Stats& impactStats() const {
        return impactFeedback_.stats();
    }
    // On Stop/Kill/rollback, invalidate callbacks immediately while
    // retaining the underlying Metal storage for safe frame retirement.
    // Callback weak ownership + epoch prevents a delayed GPU buffer read
    // from publishing an impact after its emitter leaves the active scene.
    void invalidateGpuImpacts() { impactFeedback_.reset(); }

    // GPU only, inside the scene render pass after updateGpu completed.
    bool renderGpu(const render::mat4& viewProj,render::float3 cameraRight,
                   render::float3 cameraUp) {
        if(slotsActive() && !suspended_ &&
           (!lodVisible_ || slots_.activeCount()==0)) return true;
        return slotsActive() && !suspended_ && rendererOwner_ &&
               rendererOwner_->draw(slots_.evolution(),viewProj,cameraRight,
                                    cameraUp,config_.appearance,
                                    compaction_.ready()?&compaction_:nullptr,
                                    alphaSort_.ready()?&alphaSort_:nullptr);
    }

    // Stop emission and destroy owned GPU resources before the RHI device.
    // Does NOT stop or clear unrelated CPU effects.
    void stop(bool killCpu=true) {
        if(cpu_&&cpuHandle_.valid()) {
            if(killCpu) cpu_->kill(cpuHandle_);
            else cpu_->stop(cpuHandle_);
        }
        cpuHandle_={};
        impactFeedback_.reset();
        gpuDevice_=nullptr;
        deferredGpuRelease_=false;
        deferredGpuSince_=0;
        compaction_.release();
        alphaSort_.release();
        renderer_.release(); // only owned/local pipelines; shared lives in playback
        rendererOwner_=nullptr;
        slots_.shutdown();
        cpu_=nullptr;
        suspended_=false;
        scheduler_.reset();
        lodSpawnScale_=1;
        externalSpawnScale_=1;
        externalAcceleration_={0,0,0};
        lodVisible_=true;
        sortCameraValid_=false;
        twin_=false;
        backend_=SpectraFxExecutionBackend::Cpu;
    }

    // Slot path (GPU compute OR its CPU twin) owns the particles.
    bool slotsActive() const {
        return cpu_ && (backend_==SpectraFxExecutionBackend::Gpu || twin_) &&
               slots_.evolution().ready();
    }
    // Simulated by the GPU compute kernel.
    bool gpuActive() const {return slotsActive() && !twin_;}
    // Same asset simulated by the CPU twin of the kernel (no cpuEffect).
    bool cpuTwinActive() const {return slotsActive() && twin_;}
    bool cpuActive() const {
        return cpu_ && backend_==SpectraFxExecutionBackend::Cpu &&
               cpuHandle_.valid();
    }
    bool suspended() const {return suspended_;}
    SpectraFxExecutionBackend backend() const {return backend_;}
    const SpectraFxBackendDecision& decision() const {return decision_;}
    const Config& config() const {return config_;}
    // True even after a GPU->CPU transition until the old GPU buffers
    // have been safely retired. Include these slots in playback budgets.
    bool gpuResourcesHeld() const {return slots_.evolution().ready();}
    bool hasDeferredGpuRelease() const {return deferredGpuRelease_;}
    uint64_t deferredGpuSince() const {return deferredGpuSince_;}
    // Called at an outside-pass device safe point once each frame. On
    // Metal, beginFrame waits for the 3-frame slot semaphore; 3 newer
    // serials are required before resources last used at 'since' free.
    // Unknown/zero serial is retained until explicit scene shutdown.
    bool collectDeferredGpuRelease() {
        if(!deferredGpuRelease_ || !gpuDevice_ ||
           !gpuDevice_->canUpdateSharedBuffers()) return false;
        const uint64_t now=gpuDevice_->frameSerial();
        if(!now || !deferredGpuSince_ || now<deferredGpuSince_ ||
           now-deferredGpuSince_<3) return false;
        compaction_.release();
        alphaSort_.release();
        renderer_.release(); // only our local pipeline, not shared owner
        rendererOwner_=nullptr;
        slots_.shutdown();
        gpuDevice_=nullptr;
        deferredGpuRelease_=false;
        deferredGpuSince_=0;
        ++deferredGpuReleases_;
        return true;
    }
    // Stopping a CPU fallback must not force premature GPU cleanup.
    // The caller retains this emitter until the deferred retirement.
    void stopCpuOnly(bool killCpu=true) {
        if(cpu_ && cpuHandle_.valid()) {
            if(killCpu) cpu_->kill(cpuHandle_);
            else cpu_->stop(cpuHandle_);
        }
        cpuHandle_={};
    }
    uint64_t deferredGpuReleases() const {return deferredGpuReleases_;}
    uint64_t gpuRuntimeFallbacks() const {return runtimeFallbacks_;}
    uint64_t gpuSortedFrames() const {return sortedFrames_;}
    uint64_t gpuCompactedFrames() const {return compactionFrames_;}
    uint64_t gpuCompactionFallbacks() const {return compactionFallbacks_;}
    const std::string& status() const {return status_;}
    uint32_t gpuActiveParticles() const {return slots_.activeCount();}
    uint64_t nominalGpuBytes() const {
        if(!gpuResourcesHeld()) return 0;
        return slots_.evolution().nominalGpuBytes()+
               compaction_.nominalGpuBytes()+alphaSort_.nominalGpuBytes();
    }

private:
    bool switchToCpuFallback(const char* reason) {
        if(!cpu_ || !config_.backend.allowCpuFallback ||
           config_.cpuEffect.empty()) return false;
        // The new CPU effect is prepared first. There is no intervening GPU
        // update/render, so it never double-simulates the same frame.
        const FxHandle h=cpu_->play(config_.cpuEffect,origin_,direction_);
        if(!h.valid()) return false;
        // Stop publishing callbacks immediately, but the underlying GPU
        // textures and storage may still be referenced by command buffers
        // from this frame. Keep them alive until a completed frame slot.
        impactFeedback_.reset();
        deferredGpuRelease_=slots_.evolution().ready();
        deferredGpuSince_=gpuDevice_?gpuDevice_->frameSerial():0;
        backend_=SpectraFxExecutionBackend::Cpu;
        cpuHandle_=h;
        suspended_=false;
        ++runtimeFallbacks_;
        status_=std::string(reason)+": switched to authored CPU effect";
        return true;
    }
    ParticleSystem* cpu_=nullptr;
    FxHandle cpuHandle_{};
    Config config_{};
    SpectraFxBackendDecision decision_{};
    SpectraFxExecutionBackend backend_=SpectraFxExecutionBackend::Cpu;
    SpectraFxGpuParticleSlots slots_{};
    SpectraFxGpuBillboardRenderer renderer_{};
    SpectraFxGpuCompaction compaction_{};
    SpectraFxGpuAlphaSort alphaSort_{};
    SpectraFxGpuBillboardRenderer* rendererOwner_=nullptr;
    SpectraFxGpuSpawnScheduler scheduler_{};
    SpectraFxGpuImpactFeedback impactFeedback_{};
    gfx::Device* gpuDevice_=nullptr;
    float3 origin_{},direction_{0,1,0};
    std::string status_;
    bool suspended_=false;
    float lodSpawnScale_=1.0f;
    float externalSpawnScale_=1.0f;
    float3 externalAcceleration_{};
    FxQuality globalQuality_=FxQuality::High;
    bool lodVisible_=true;
    uint64_t sortedFrames_=0;
    uint64_t runtimeFallbacks_=0;
    bool twin_=false;
    bool deferredGpuRelease_=false;
    uint64_t deferredGpuSince_=0;
    uint64_t deferredGpuReleases_=0;
    bool sortCameraValid_=false;
    render::float3 sortCameraEye_{},sortCameraForward_{};
    uint64_t compactionFrames_=0;
    uint64_t compactionFallbacks_=0;
};
} // namespace aiverse::fx
