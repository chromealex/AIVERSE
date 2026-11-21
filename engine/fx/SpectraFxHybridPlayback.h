// SpectraFX project GPU/CPU emitter playback, render-thread only.
// Project assets and routes are presentation resources. ECS SimEvent remains
// authoritative for the occurrence of events, never for effect particles.
#pragma once
#include <algorithm>

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <chrono>
#include <memory>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "SpectraFxGpuEmitterLibrary.h"
#include "SpectraFxGpuSharedArena.h"
#include "SpectraFxGpuBudgetGovernor.h"
#include "SpectraFxEmitterEvents.h"
#include "SpectraFxCueBridge.h"

namespace aiverse::fx {

class SpectraFxHybridPlayback {
public:
    static constexpr size_t kMaxEmitters=64;
    static constexpr size_t kMaxGpuPools=8;
    static constexpr uint32_t kMaxTotalPooledParticles=65536;
    // Upper bound on all GPU particle slots owned by one playback instance,
    // including dedicated emitters and retained homogeneous pool capacity.
    static constexpr uint32_t kMaxPlaybackGpuSlots=65536;
    static constexpr float kUnusedPoolRetireSeconds=5.0f;
    static constexpr uint64_t kMetalRetireFrameSlots=3;
    struct Route {
        std::string asset;
        bool enabled=true;
        // One-shot presentation ownership, seconds (hard retire and release).
        // The playing emitter may spawn particles until this deadline.
        float oneShotSeconds=3.0f;
        // Opt-in homogeneous GPU arena across routes; empty => existing
        // dedicated hybrid emitter behavior. No change to CPU fallback.
        std::string sharedPool;
        uint32_t sharedPoolCapacity=8192;
    };
    struct Stats {
        uint64_t played=0,moved=0,stopped=0,killed=0;
        uint64_t missing=0,refused=0,failed=0,oneShots=0,retired=0;
        uint64_t gpuImpacts=0,impactDropped=0,impactEffects=0;
        uint64_t pooledPlays=0,pooledFallbacks=0,pooledRetired=0,pooledFailures=0;
        uint64_t gpuBudgetCpuFallbacks=0,gpuBudgetRefusals=0;
        uint64_t unusedPoolsReleased=0;
        uint64_t sharedComputePrograms=0,sharedComputeFailures=0;
        uint64_t sharedAlphaPrograms=0,sharedCompactPrograms=0;
        uint64_t sharedMetalProgramFailures=0;
        uint64_t gpuRuntimeCpuFallbacks=0,gpuDeferredReleaseCompleted=0;
        uint64_t pooledCpuRecoveries=0,pooledRecoveryDropped=0;
        uint64_t pooledRenderFaults=0;
        uint64_t pooledResourceRetired=0,pooledResourceReleased=0;
        uint64_t dedicatedGpuRetired=0,dedicatedGpuReleased=0;
        uint64_t cpuUpdateSamples=0,cpuDrawSamples=0;
        uint64_t cpuUpdateEncodeUs=0,cpuDrawEncodeUs=0;
        uint64_t cpuUpdatePeakUs=0,cpuDrawPeakUs=0;
    };

    SpectraFxHybridPlayback(gfx::Device& device,ParticleSystem& cpu,
                            SpectraFxGpuEmitterLibrary& library)
        : device_(device),cpu_(cpu),library_(library) {}

    // Destruction should happen before the RHI Device and CPU ParticleSystem.
    ~SpectraFxHybridPlayback() {
        reset(); // destroys every borrower before either shared RHI handle
        sharedRenderer_.release();
        if(sharedCompaction_.valid()) device_.destroyCompute(sharedCompaction_);
        if(sharedAlphaSort_.valid()) device_.destroyCompute(sharedAlphaSort_);
        if(sharedEvolution_.valid()) device_.destroyCompute(sharedEvolution_);
        sharedCompaction_={};
        sharedAlphaSort_={};
        sharedEvolution_={};
    }

    void route(uint32_t cueId,Route value) {routes_[cueId]=std::move(value);}
    void unroute(uint32_t cueId) {routes_.erase(cueId);}
    void clearRoutes() {routes_.clear();}
    // Presentation tier caps per-asset authored quality. Off prevents NEW
    // GPU spawns while old GPU particles continue aging to completion;
    // the host owns CPU ParticleSystem::setQuality for a shared CPU backend.
    void setGlobalQuality(FxQuality quality) {
        globalQuality_=quality;
        for(auto& entry:active_) entry.second->setGlobalQuality(quality);
        for(auto& shot:oneShots_) shot.emitter->setGlobalQuality(quality);
        for(auto& pool:pools_) pool.second.arena->setGlobalQuality(quality);
    }
    FxQuality globalQuality() const {return globalQuality_;}
    // Disabled unless explicitly requested by a host/debugger.
    // CPU wall time spent issuing commands, NOT asynchronous GPU time.
    void setCpuEncodeProfiling(bool enabled) {cpuEncodeProfiling_=enabled;}
    bool cpuEncodeProfiling() const {return cpuEncodeProfiling_;}

    // Project/host opt-in. Uses device's WHOLE-FRAME GPU time only as a
    // conservative spawn throttle, never as a claim about SpectraFX cost.
    bool configureGpuBudget(const SpectraFxGpuBudgetGovernor::Config& cfg) {
        return gpuGovernor_.configure(cfg);
    }
    const SpectraFxGpuBudgetGovernor::Stats& gpuBudgetStats() const {
        return gpuGovernor_.stats();
    }

    bool dispatch(const SpectraFxCue& cue) {
        if(cue.action!=SpectraFxCue::Action::Play) {
            if(!cue.key) {++stats_.refused;return false;}
            auto pooled=pooledActive_.find(cue.key);
            if(pooled!=pooledActive_.end()) {
                auto group=pools_.find(pooled->second.name);
                if(group==pools_.end()) {++stats_.failed;return false;}
                const bool unhealthy=group->second.failurePending ||
                                     !device_.gpuHealthy();
                // Failed pools must never receive new GPU buffer updates.
                // Preserve pose-only changes for the upcoming CPU recovery.
                // A Stop/Kill removes the presentation key immediately, so
                // that source is NOT later resurrected by recovery.
                if(unhealthy) {
                    if(cue.action==SpectraFxCue::Action::Move) {
                        const auto finite=[](render::float3 p) {
                            return std::isfinite(p.x)&&std::isfinite(p.y)&&
                                   std::isfinite(p.z);
                        };
                        if(!finite(cue.position)||!finite(cue.direction))
                            return false;
                        pooled->second.position=cue.position;
                        pooled->second.direction=cue.direction;
                        ++stats_.moved;
                    } else {
                        pooledActive_.erase(pooled);
                        if(cue.action==SpectraFxCue::Action::Kill) ++stats_.killed;
                        else ++stats_.stopped;
                    }
                    return true;
                }
                auto* arena=group->second.arena.get();
                if(cue.action==SpectraFxCue::Action::Move) {
                    if(!arena->moveEmitter(pooled->second.ticket,
                                           cue.position,cue.direction)) {
                        ++stats_.failed;return false;
                    }
                    pooled->second.position=cue.position;
                    pooled->second.direction=cue.direction;
                    ++stats_.moved;return true;
                }
                if(!arena->removeEmitter(pooled->second.ticket,
                                         cue.action==SpectraFxCue::Action::Kill)) {
                    ++stats_.failed;return false;
                }
                pooledActive_.erase(pooled);
                if(cue.action==SpectraFxCue::Action::Kill) ++stats_.killed;
                else ++stats_.stopped;
                return true;
            }
            auto it=active_.find(cue.key);
            if(it==active_.end()) {++stats_.missing;return false;}
            if(cue.action==SpectraFxCue::Action::Move) {
                it->second->moveOrigin(cue.position,cue.direction);
                ++stats_.moved;return true;
            }
            auto retired=std::move(it->second);
            active_.erase(it);
            retireDedicatedEmitter(std::move(retired),
                                   cue.action==SpectraFxCue::Action::Kill);
            if(cue.action==SpectraFxCue::Action::Kill) ++stats_.killed;
            else ++stats_.stopped;
            return true;
        }
        const auto route=routes_.find(cue.id);
        if(route==routes_.end() || !route->second.enabled ||
           !SpectraFxGpuEmitterLibrary::validName(route->second.asset)) {
            ++stats_.missing;return false;
        }
        const bool oneShot=cue.key==0;
        if(oneShot && (!std::isfinite(route->second.oneShotSeconds) ||
                       route->second.oneShotSeconds<=0 ||
                       route->second.oneShotSeconds>60)) {
            ++stats_.refused;return false;
        }
        if(active_.size()+oneShots_.size()+pooledActive_.size()+
           pooledShots_.size()>=kMaxEmitters &&
           (oneShot || (active_.find(cue.key)==active_.end() &&
                        pooledActive_.find(cue.key)==pooledActive_.end()))) {
            ++stats_.refused;return false;
        }
        const auto load=library_.load(route->second.asset);
        const auto* entry=library_.find(route->second.asset);
        if(!load.valid || !entry) {++stats_.failed;return false;}
        // Shared pools are explicit project opt-ins. Unsupported devices,
        // heterogeneous settings, or an exhausted pool use the existing
        // per-emitter HybridEmitter path with the authored CPU fallback.
        if(!route->second.sharedPool.empty()) {
            PooledRef pooled;
            if(tryStartPooled(route->second,entry->config,cue,pooled)) {
                if(!oneShot && !retireKey(cue.key)) {
                    auto* arena=poolArena(pooled.name);
                    if(arena) arena->removeEmitter(pooled.ticket,true);
                    ++stats_.failed;
                    return false;
                }
                if(oneShot) {
                    pooledShots_.push_back({std::move(pooled),0,
                                            route->second.oneShotSeconds});
                    ++stats_.oneShots;
                } else pooledActive_[cue.key]=std::move(pooled);
                ++stats_.played;
                ++stats_.pooledPlays;
                return true;
            }
            ++stats_.pooledFallbacks;
        }
        // Account for all GPU slots reserved by this playback, even if pool
        // particles are currently dormant. Exceeding the cap falls back to
        // the authored CPU effect only if the route explicitly allows it.
        auto selected=entry->config; // never mutate the cached authored asset
        // If a prior frame marked this group failed, new sources must not
        // re-enter it or create more GPU work until the safe-point recovery.
        if(!route->second.sharedPool.empty()) {
            const auto group=pools_.find(route->second.sharedPool);
            if(group!=pools_.end() && group->second.failurePending) {
                if(!selected.backend.allowCpuFallback ||
                   selected.cpuEffect.empty()) {
                    ++stats_.gpuBudgetRefusals;
                    return false;
                }
                selected.backend.preference=SpectraFxExecutionPreference::Cpu;
            }
        }
        auto preference=chooseSpectraFxBackend(device_,SpectraFxHybridEmitter::effectiveRequest(selected));
        bool gpuRequested=preference.accepted &&
                          preference.backend==SpectraFxExecutionBackend::Gpu;
        if(gpuRequested &&
           usedGpuSlots()+selected.backend.particles>kMaxPlaybackGpuSlots) {
            if(!selected.backend.allowCpuFallback || selected.cpuEffect.empty()) {
                ++stats_.gpuBudgetRefusals;
                return false;
            }
            selected.backend.preference=SpectraFxExecutionPreference::Cpu;
            ++stats_.gpuBudgetCpuFallbacks;
            gpuRequested=false;
        }
        if(gpuRequested && !sharedRendererAttempted_) {
            sharedRendererAttempted_=true;
            sharedRenderer_.init(device_);
        }
        // The evolution pipeline is independently shared with every
        // dedicated emitter and homogeneous arena. A failed creation
        // leaves the historical owned-per-emitter compute path intact.
        const gfx::ComputeHandle sharedCompute=gpuRequested?
            ensureSharedEvolution():gfx::ComputeHandle{};
        const gfx::ComputeHandle sharedSort=
            gpuRequested && selected.gpuSortAlpha?
                ensureSharedAlphaSort():gfx::ComputeHandle{};
        const gfx::ComputeHandle sharedCompact=
            gpuRequested && selected.gpuIndirectCompaction?
                ensureSharedCompaction():gfx::ComputeHandle{};
        auto next=std::make_unique<SpectraFxHybridEmitter>();
        // A failed shared billboard pipeline must not prevent the legacy
        // per-emitter renderer path from being attempted. Only lend a
        // resource that actually initialized successfully.
        if(!next->begin(device_,cpu_,std::move(selected),cue.position,cue.direction,
                        gpuRequested && sharedRenderer_.ready()
                            ? &sharedRenderer_ : nullptr,
                        sharedCompute,sharedSort,sharedCompact)) {
            ++stats_.failed;return false;
        }
        next->setGlobalQuality(globalQuality_);
        if(oneShot) {
            oneShots_.push_back({std::move(next),0,route->second.oneShotSeconds});
            ++stats_.oneShots;
        } else {
            if(!retireKey(cue.key)) {
                next->stop();
                ++stats_.failed;
                return false;
            }
            auto it=active_.find(cue.key);
            if(it!=active_.end()) {
                it->second->stop();
                it->second=std::move(next);
            } else active_.emplace(cue.key,std::move(next));
        }
        ++stats_.played;
        return true;
    }

    template<class Range>
    void dispatchAll(const Range& cues) {
        for(const auto& cue:cues) dispatch(cue);
    }

    // Call inside RHI beginFrame/endFrame, outside all render passes.
    // The host separately updates the shared CPU ParticleSystem ONCE/frame.
    void updateGpu(float dt) {
        if(!std::isfinite(dt)||dt<0||dt>0.1f) {++stats_.failed;return;}
        const auto encodeStart=cpuEncodeProfiling_?Clock::now():Clock::time_point{};
        // Metrics may be unavailable on mobile / Vulkan; the governor then
        // keeps its last known safe scale (1 by default).
        gpuGovernor_.observe(float(device_.gpuFrameMs()),dt);
        const float spawnScale=gpuGovernor_.spawnScale();
        for(auto& pair:active_) {
            if(pair.second->collectDeferredGpuRelease())
                ++stats_.gpuDeferredReleaseCompleted;
            pair.second->setExternalSpawnScale(spawnScale);
            advanceGpu(*pair.second,dt);
        }
        for(size_t i=0;i<oneShots_.size();) {
            auto& shot=oneShots_[i];
            // Retire exactly once; GPU and CPU resources are bounded by the
            // authored deadline, independent of whether particles remain.
            if(shot.elapsed+dt>=shot.duration) {
                auto retired=std::move(shot.emitter);
                oneShots_.erase(oneShots_.begin()+std::ptrdiff_t(i));
                retireDedicatedEmitter(std::move(retired),true);
                ++stats_.retired;
                continue;
            }
            if(shot.emitter->collectDeferredGpuRelease())
                ++stats_.gpuDeferredReleaseCompleted;
            shot.emitter->setExternalSpawnScale(spawnScale);
            advanceGpu(*shot.emitter,dt);
            shot.elapsed+=dt;
            ++i;
        }
        // Retire pooled one-shots before the arena's next compute pass.
        // Both public and private pooled sources count toward the 64 limit.
        for(size_t i=0;i<pooledShots_.size();) {
            PooledShot& shot=pooledShots_[i];
            if(shot.elapsed+dt>=shot.duration) {
                auto* arena=poolArena(shot.source.name);
                if(!arena || !arena->removeEmitter(shot.source.ticket,true)) {
                    ++stats_.pooledFailures;
                    ++i; // keep ownership and retry next safe frame
                    continue;
                }
                pooledShots_.erase(pooledShots_.begin()+std::ptrdiff_t(i));
                ++stats_.retired;
                ++stats_.pooledRetired;
                continue;
            }
            shot.elapsed+=dt;
            ++i;
        }
        // RHI command buffers can still refer to resources submitted in
        // earlier frames. Reclaim only after Metal's completed slot has
        // safely cycled; unknown frame serial => retain until reset().
        collectRetiredPools();
        for(auto it=pools_.begin();it!=pools_.end();) {
            PoolGroup& group=it->second;
            group.arena->setExternalSpawnScale(spawnScale);
            if(group.failurePending || !device_.gpuHealthy()) {
                recoverPooledSources(it->first);
                retirePoolResources(group);
                it=pools_.erase(it);
                continue;
            }
            // No GPU submission for wholly empty groups. Retirement remains
            // deferred to a completed frame slot rather than immediate
            // destruction of resources still referenced by earlier draws.
            if(group.arena->emitterCount()==0 &&
               group.arena->aliveParticles()==0) {
                group.drawReady=false;
                group.idleSeconds+=dt;
                if(group.idleSeconds>=kUnusedPoolRetireSeconds) {
                    retirePoolResources(group);
                    it=pools_.erase(it);
                    ++stats_.unusedPoolsReleased;
                    continue;
                }
            } else {
                group.idleSeconds=0;
                group.drawReady=group.arena->update(dt,lastCameraEye_);
                if(!group.drawReady) {
                    ++stats_.pooledFailures;
                    recoverPooledSources(it->first);
                    retirePoolResources(group);
                    it=pools_.erase(it);
                    continue;
                }
            }
            ++it;
        }
        if(cpuEncodeProfiling_) {
            const uint64_t us=elapsedMicros(encodeStart);
            ++stats_.cpuUpdateSamples;
            stats_.cpuUpdateEncodeUs+=us;
            stats_.cpuUpdatePeakUs=std::max(stats_.cpuUpdatePeakUs,us);
        }
    }

    // Camera-aware update used by native clients. The no-camera overload
    // remains for standalone hosts and preserves LOD settings from last view.
    void updateGpu(float dt,render::float3 cameraEye) {
        updateGpu(dt,cameraEye,{0,0,0});
    }
    // Sorted alpha requires both camera position AND forward direction.
    // The two-argument legacy overload supplies no direction; such effects
    // safely switch to their authored CPU fallback rather than guess depth.
    void updateGpu(float dt,render::float3 cameraEye,
                   render::float3 cameraForward) {
        lastCameraEye_=cameraEye;
        for(auto& pair:active_) {
            pair.second->updateLod(cameraEye);
            if(render::length(cameraForward)>1e-6f)
                pair.second->setSortView(cameraEye,cameraForward);
        }
        for(auto& shot:oneShots_) {
            shot.emitter->updateLod(cameraEye);
            if(render::length(cameraForward)>1e-6f)
                shot.emitter->setSortView(cameraEye,cameraForward);
        }
        updateGpu(dt);
    }

    // Reprepare view-dependent LOD and alpha sorting WITHOUT advancing
    // simulation time. For multiple cameras: updateGpu(dt,firstEye,forward)
    // once, then prepareRenderView for each offscreen pass separately.
    // Invoke only outside any render/compute encoder, after prior pass ends.
    bool prepareRenderView(render::float3 eye,render::float3 forward) {
        bool ok=true;
        for(auto& pair:active_) {
            if(!pair.second->prepareRenderView(eye,forward)) {
                ++stats_.failed;ok=false;
            }
        }
        for(auto& shot:oneShots_) {
            if(!shot.emitter->prepareRenderView(eye,forward)) {
                ++stats_.failed;ok=false;
            }
        }
        // Shared arenas are additive-only: their once-per-frame compute,
        // compacted indirect buffers and output storage are reusable across
        // cameras, with independent billboard matrix and axes at render().
        return ok;
    }

    // Call during a compatible open render pass after the compute stage.
    // This submits only GPU-owned emitters, never duplicates CPU draws.
    void renderGpu(const render::mat4& viewProj,render::float3 cameraRight,
                   render::float3 cameraUp) {
        const auto encodeStart=cpuEncodeProfiling_?Clock::now():Clock::time_point{};
        for(auto& pair:active_)
            drawGpu(*pair.second,viewProj,cameraRight,cameraUp);
        for(auto& shot:oneShots_)
            drawGpu(*shot.emitter,viewProj,cameraRight,cameraUp);
        // One billboard draw per homogeneous pool regardless of source count.
        for(auto& pair:pools_) {
            PoolGroup& group=pair.second;
            if(!group.drawReady || group.failurePending ||
               group.arena->aliveParticles()==0) continue;
            if(!group.arena->render(viewProj,cameraRight,cameraUp)) {
                // Do NOT free any buffers here: we are inside a render
                // encoder. Next updateGpu (outside passes) recovers sources
                // and queues old resources for deferred retirement.
                group.drawReady=false;
                group.failurePending=true;
                ++stats_.pooledRenderFaults;
                ++stats_.pooledFailures;
            }
        }
        if(cpuEncodeProfiling_) {
            const uint64_t us=elapsedMicros(encodeStart);
            ++stats_.cpuDrawSamples;
            stats_.cpuDrawEncodeUs+=us;
            stats_.cpuDrawPeakUs=std::max(stats_.cpuDrawPeakUs,us);
        }
    }

    // Called on the GPU owner/presentation thread after async readback
    // callbacks have been delivered (typically a subsequent beginFrame).
    // Emits no gameplay events and never modifies Simulation or ECS.
    void publishGpuImpacts(SpectraFxEmitterEventRouter& router,
                           size_t maxEvents=128) {
        size_t remaining=maxEvents;
        auto forward=[&](uint64_t key,SpectraFxHybridEmitter& emitter) {
            if(!remaining) return;
            emitter.drainGpuImpacts([&](const SpectraFxGpuImpact& impact) {
                SpectraFxEmitterEventRecord record;
                record.emitterKey=key;
                record.type=SpectraFxEmitterEvent::Impact;
                record.marker=impact.particle.index;
                record.position=impact.position;
                if(router.publish(record)) ++stats_.gpuImpacts;
                else ++stats_.impactDropped;
                --remaining;
            },remaining);
        };
        for(auto& pair:active_) forward(pair.first,*pair.second);
        for(auto& oneShot:oneShots_) forward(0,*oneShot.emitter);
    }

    // Optional authored on-impact CPU sub-effects. Call once after
    // ParticleSystem::setView and before ParticleSystem::update, on the
    // presentation thread. Effects are bounded and NEVER gameplay hits.
    void playGpuImpactEffects(size_t maxEffects=32) {
        size_t remaining=maxEffects;
        auto emit=[&](SpectraFxHybridEmitter& emitter) {
            const auto& name=emitter.config().impactEffect;
            if(name.empty() || !remaining) return;
            emitter.drainGpuImpacts([&](const SpectraFxGpuImpact& impact) {
                const auto h=cpu_.play(name,impact.position);
                if(h.valid()) ++stats_.impactEffects;
                else ++stats_.impactDropped;
                --remaining;
            },remaining);
        };
        for(auto& pair:active_) emit(*pair.second);
        for(auto& shot:oneShots_) emit(*shot.emitter);
    }

    // Non-destructive presentation reset for a live running frame, e.g.
    // FPS floating-origin change or rollback. No old GPU effect is drawn
    // after this call. GPU buffers are released after completed frame
    // slots, not immediately. Call ONLY outside render/compute encoders.
    // Returns false with NO state changes if the device is not safe.
    bool resetPresentation() {
        if(!device_.canUpdateSharedBuffers()) return false;
        for(auto& item:active_)
            retireDedicatedEmitter(std::move(item.second),true);
        active_.clear();
        for(auto& item:oneShots_)
            retireDedicatedEmitter(std::move(item.emitter),true);
        oneShots_.clear();
        pooledActive_.clear();
        pooledShots_.clear();
        for(auto& item:pools_)
            retirePoolResources(item.second);
        pools_.clear();
        return true;
    }

    // Host teardown only: device is no longer submitting draws and the
    // RHI owner guarantees it is safe to destroy all resources now.
    void reset() {
        for(auto& pair:active_) pair.second->stop();
        active_.clear();
        for(auto& shot:oneShots_) shot.emitter->stop();
        oneShots_.clear();
        pooledActive_.clear();
        pooledShots_.clear();
        for(auto& pair:pools_) pair.second.arena->shutdown();
        pools_.clear();
        for(auto& pool:retiringPools_) pool.arena->shutdown();
        retiringPools_.clear();
        for(auto& retired:retiringDedicated_)
            retired.emitter->stop();
        retiringDedicated_.clear();
        retiringGpuSlots_=0;
        totalPoolCapacity_=0;
    }

    size_t attached() const {return active_.size()+pooledActive_.size();}
    size_t oneShotCount() const {return oneShots_.size()+pooledShots_.size();}
    struct BackendSnapshot {
        size_t dedicatedGpu=0, dedicatedCpu=0, suspendedGpu=0;
        size_t pooledSources=0, pooledGroups=0;
        uint64_t activeGpuSlots=0, retiredGpuSlots=0;
        uint64_t nominalGpuBytes=0;
    };
    // Read-only presentation diagnostics. Counts reflect live ownership,
    // not confirmed GPU draw submissions or particle instance FPS.
    BackendSnapshot backendSnapshot() const {
        BackendSnapshot out;
        auto classify=[&](const SpectraFxHybridEmitter& emitter) {
            if(emitter.gpuActive()) {
                ++out.dedicatedGpu;
                if(emitter.suspended()) ++out.suspendedGpu;
            } else if(emitter.cpuActive()) ++out.dedicatedCpu;
            if(emitter.gpuResourcesHeld())
                out.activeGpuSlots+=emitter.config().backend.particles;
        };
        for(const auto& entry:active_) classify(*entry.second);
        for(const auto& shot:oneShots_) classify(*shot.emitter);
        out.pooledSources=pooledActive_.size()+pooledShots_.size();
        out.pooledGroups=pools_.size();
        out.activeGpuSlots+=totalPoolCapacity_;
        out.retiredGpuSlots=retiringGpuSlots_;
        out.nominalGpuBytes=nominalGpuBytes();
        return out;
    }
    size_t sharedPoolCount() const {return pools_.size();}
    uint32_t sharedPoolCapacity() const {return totalPoolCapacity_;}
    // Includes both pools and dedicated emitters held for in-flight frames.
    uint64_t retiringGpuSlots() const {return retiringGpuSlots_;}
    size_t retiringGpuResourceCount() const {
        return retiringPools_.size()+retiringDedicated_.size();
    }
    // Approximate native RHI payload only. Driver overhead, pipeline state,
    // transient staging and texture memory alignment are not accounted for.
    uint64_t nominalGpuBytes() const {
        uint64_t bytes=0;
        for(const auto& entry:active_) bytes+=entry.second->nominalGpuBytes();
        for(const auto& entry:oneShots_)
            bytes+=entry.emitter->nominalGpuBytes();
        for(const auto& entry:pools_)
            bytes+=entry.second.arena->nominalGpuBytes();
        for(const auto& entry:retiringPools_)
            bytes+=entry.arena->nominalGpuBytes();
        for(const auto& entry:retiringDedicated_)
            bytes+=entry.emitter->nominalGpuBytes();
        return bytes;
    }

    uint64_t usedGpuSlots() const {
        uint64_t total=uint64_t(totalPoolCapacity_)+retiringGpuSlots_;
        for(const auto& e:active_)
            if(e.second->gpuResourcesHeld())
                total+=e.second->config().backend.particles;
        for(const auto& e:oneShots_)
            if(e.emitter->gpuResourcesHeld())
                total+=e.emitter->config().backend.particles;
        return total;
    }
    const Stats& stats() const {return stats_;}
    void resetStats() {stats_={};}

private:
    using Clock=std::chrono::steady_clock;
    static uint64_t elapsedMicros(Clock::time_point started) {
        const auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(
            Clock::now()-started).count();
        return elapsed>0 ? uint64_t(elapsed) : 0;
    }
    struct OneShot {
        std::unique_ptr<SpectraFxHybridEmitter> emitter;
        float elapsed=0,duration=3;
    };
    struct PooledRef {
        std::string name;
        uint64_t ticket=0;
        SpectraFxHybridEmitter::Config config{};
        render::float3 position{},direction{0,1,0};
    };
    struct PooledShot {
        PooledRef source;
        float elapsed=0,duration=3;
    };
    struct PoolGroup {
        SpectraFxGpuSharedArena::Config config;
        std::unique_ptr<SpectraFxGpuSharedArena> arena;
        float idleSeconds=0;
        bool drawReady=false;
        bool failurePending=false;
    };
    struct RetiringPool {
        std::unique_ptr<SpectraFxGpuSharedArena> arena;
        uint32_t reservedSlots=0;
        uint64_t lastSubmittedSerial=0;
    };
    struct RetiringEmitter {
        std::unique_ptr<SpectraFxHybridEmitter> emitter;
        uint32_t reservedSlots=0;
        uint64_t lastSubmittedSerial=0;
    };
    void retireDedicatedEmitter(
            std::unique_ptr<SpectraFxHybridEmitter> emitter,
            bool killCpu) {
        if(!emitter) return;
        // CPU effects with no retained GPU state stop immediately. A
        // CPU fallback may still hold buffers from a prior GPU frame;
        // stop only its CPU handle and retire the buffers with the same
        // completed-frame serial policy as any other GPU owner.
        if(!emitter->gpuResourcesHeld()) {
            emitter->stop(killCpu);
            return;
        }
        // Retired emitters cannot emit callbacks into a new presentation
        // epoch even while their GPU storage survives in-flight frames.
        emitter->invalidateGpuImpacts();
        if(emitter->cpuActive()) emitter->stopCpuOnly(killCpu);
        const auto slots=emitter->config().backend.particles;
        retiringGpuSlots_+=slots;
        retiringDedicated_.push_back({std::move(emitter),slots,
                                      device_.frameSerial()});
        ++stats_.dedicatedGpuRetired;
    }

    std::unique_ptr<SpectraFxHybridEmitter> makePooledCpuFallback(
            const PooledRef& source) {
        if(!source.config.backend.allowCpuFallback ||
           source.config.cpuEffect.empty()) return {};
        auto config=source.config;
        config.backend.preference=SpectraFxExecutionPreference::Cpu;
        auto cpu=std::make_unique<SpectraFxHybridEmitter>();
        if(!cpu->begin(device_,cpu_,std::move(config),
                       source.position,source.direction)) return {};
        return cpu;
    }

    // Presentational replacement only. A pooled GPU source may have been
    // advanced in an earlier encoded frame; no readback/state migration is
    // attempted. CPU effects start at each source's latest known pose.
    void recoverPooledSources(const std::string& name) {
        for(auto it=pooledActive_.begin();it!=pooledActive_.end();) {
            if(it->second.name!=name) {++it;continue;}
            auto cpu=makePooledCpuFallback(it->second);
            if(cpu) {
                active_[it->first]=std::move(cpu);
                ++stats_.pooledCpuRecoveries;
            } else ++stats_.pooledRecoveryDropped;
            it=pooledActive_.erase(it);
        }
        for(size_t i=0;i<pooledShots_.size();) {
            auto& shot=pooledShots_[i];
            if(shot.source.name!=name) {++i;continue;}
            auto cpu=makePooledCpuFallback(shot.source);
            if(cpu) {
                oneShots_.push_back({std::move(cpu),shot.elapsed,shot.duration});
                ++stats_.pooledCpuRecoveries;
            } else ++stats_.pooledRecoveryDropped;
            pooledShots_.erase(pooledShots_.begin()+std::ptrdiff_t(i));
        }
    }
    // Called ONLY after GPU compute ended and outside all render passes.
    // Keep old resources alive through Metal's in-flight command slots.
    // Vulkan/unknown frame-serial uses conservative retention until reset.
    void retirePoolResources(PoolGroup& group) {
        if(!group.arena) return;
        const uint32_t capacity=group.config.particles;
        retiringGpuSlots_+=capacity;
        totalPoolCapacity_-=capacity;
        retiringPools_.push_back({std::move(group.arena),capacity,
                                  device_.frameSerial()});
        ++stats_.pooledResourceRetired;
    }
    // Gfx.h retirement contract: a resource retired at frameSerial() S is
    // released only once completedFrameSerial() (GPU-completed, not merely
    // the CPU frame counter) is nonzero and >= S. Never by elapsed frames.
    static bool retirementCompleted(uint64_t retiredAt,uint64_t completed) {
        return retiredAt!=0 && completed!=0 && completed>=retiredAt;
    }
    void collectRetiredPools() {
        if(!device_.canUpdateSharedBuffers()) return;
        const uint64_t completed=device_.completedFrameSerial();
        if(!completed) return; // unknown RHI completion: never guess safe
        for(size_t i=0;i<retiringPools_.size();) {
            auto& retired=retiringPools_[i];
            if(!retirementCompleted(retired.lastSubmittedSerial,completed)) {
                ++i;continue;
            }
            retired.arena->shutdown();
            retiringGpuSlots_-=retired.reservedSlots;
            retiringPools_.erase(retiringPools_.begin()+std::ptrdiff_t(i));
            ++stats_.pooledResourceReleased;
        }
        for(size_t i=0;i<retiringDedicated_.size();) {
            auto& retired=retiringDedicated_[i];
            if(!retirementCompleted(retired.lastSubmittedSerial,completed)) {
                ++i;continue;
            }
            retired.emitter->stop();
            retiringGpuSlots_-=retired.reservedSlots;
            retiringDedicated_.erase(
                retiringDedicated_.begin()+std::ptrdiff_t(i));
            ++stats_.dedicatedGpuReleased;
        }
    }

    static bool same(render::float3 a,render::float3 b) {
        return a.x==b.x && a.y==b.y && a.z==b.z;
    }
    static bool same(const SpectraFxGpuEnvironment& a,
                     const SpectraFxGpuEnvironment& b) {
        return same(a.acceleration,b.acceleration) &&
            a.damping==b.damping && a.collideGround==b.collideGround &&
            a.reportImpacts==b.reportImpacts &&
            a.groundHeight==b.groundHeight &&
            a.restitution==b.restitution &&
            a.tangentRetention==b.tangentRetention;
    }
    static bool same(const SpectraFxGpuBillboardRenderer::Appearance& a,
                     const SpectraFxGpuBillboardRenderer::Appearance& b) {
        return same(a.color,b.color) && a.radius==b.radius &&
               a.additive==b.additive;
    }

    gfx::ComputeHandle ensureSharedEvolution() {
        if(sharedEvolutionAttempted_) return sharedEvolution_;
        sharedEvolutionAttempted_=true;
        sharedEvolution_=device_.createCompute({
            kSpectraFxComputeMetal,kSpectraFxComputeGLSL,
            "SpectraFX shared GPU evolution"});
        if(sharedEvolution_.valid()) ++stats_.sharedComputePrograms;
        else ++stats_.sharedComputeFailures;
        return sharedEvolution_;
    }
    gfx::ComputeHandle ensureSharedAlphaSort() {
        if(device_.info().backend!=gfx::Backend::Metal) return {};
        if(sharedAlphaSortAttempted_) return sharedAlphaSort_;
        sharedAlphaSortAttempted_=true;
        sharedAlphaSort_=device_.createCompute({
            kSpectraFxAlphaSortMetal,nullptr,"SpectraFX shared GPU alpha sort"});
        if(sharedAlphaSort_.valid()) ++stats_.sharedAlphaPrograms;
        else ++stats_.sharedMetalProgramFailures;
        return sharedAlphaSort_;
    }
    gfx::ComputeHandle ensureSharedCompaction() {
        if(device_.info().backend!=gfx::Backend::Metal) return {};
        if(sharedCompactionAttempted_) return sharedCompaction_;
        sharedCompactionAttempted_=true;
        sharedCompaction_=device_.createCompute({
            kSpectraFxCompactMetal,nullptr,"SpectraFX shared live-slot compact"});
        if(sharedCompaction_.valid()) ++stats_.sharedCompactPrograms;
        else ++stats_.sharedMetalProgramFailures;
        return sharedCompaction_;
    }


    SpectraFxGpuSharedArena* poolArena(const std::string& name) {
        auto it=pools_.find(name);
        return it==pools_.end()?nullptr:it->second.arena.get();
    }
    // Replaces an existing keyed instance only after a new one has started.
    // Called at GPU-buffer-safe presentation points before compute passes.
    bool retireKey(uint64_t key) {
        auto pooled=pooledActive_.find(key);
        if(pooled!=pooledActive_.end()) {
            auto* arena=poolArena(pooled->second.name);
            if(!arena || !arena->removeEmitter(pooled->second.ticket,true))
                return false;
            pooledActive_.erase(pooled);
        }
        auto direct=active_.find(key);
        if(direct!=active_.end()) {
            auto retired=std::move(direct->second);
            active_.erase(direct);
            retireDedicatedEmitter(std::move(retired),true);
        }
        return true;
    }
    bool tryStartPooled(const Route& route,
                        const SpectraFxHybridEmitter::Config& effect,
                        const SpectraFxCue& cue,PooledRef& out) {
        if(!SpectraFxGpuEmitterLibrary::validName(route.sharedPool) ||
           route.sharedPoolCapacity==0 ||
           route.sharedPoolCapacity>kMaxTotalPooledParticles ||
           !effect.autoSpawnGpu || !effect.impactEffect.empty() ||
           !effect.appearance.additive ||
           effect.environment.reportImpacts) return false;
        // CPU policy and unsupported GPU features retain dedicated fallback.
        const auto choice=chooseSpectraFxBackend(device_,SpectraFxHybridEmitter::effectiveRequest(effect));
        if(!choice.accepted || choice.backend!=SpectraFxExecutionBackend::Gpu)
            return false;

        auto it=pools_.find(route.sharedPool);
        bool newlyCreated=false;
        if(it!=pools_.end()) {
            if(it->second.failurePending) return false;
            const auto& cfg=it->second.config;
            if(cfg.particles!=route.sharedPoolCapacity ||
               cfg.gpuIndirectCompaction!=effect.gpuIndirectCompaction ||
               !same(cfg.appearance,effect.appearance) ||
               !same(cfg.environment,effect.environment))
                return false; // heterogeneous group: separate HybridEmitter
        } else {
            if(pools_.size()>=kMaxGpuPools ||
               route.sharedPoolCapacity>kMaxTotalPooledParticles-totalPoolCapacity_ ||
               usedGpuSlots()+route.sharedPoolCapacity>kMaxPlaybackGpuSlots)
                return false;
            auto arena=std::make_unique<SpectraFxGpuSharedArena>();
            SpectraFxGpuSharedArena::Config cfg;
            cfg.particles=route.sharedPoolCapacity;
            cfg.appearance=effect.appearance;
            cfg.environment=effect.environment;
            cfg.gpuIndirectCompaction=effect.gpuIndirectCompaction;
            if(!arena->init(device_,cfg,ensureSharedEvolution(),
                            cfg.gpuIndirectCompaction?
                                ensureSharedCompaction():gfx::ComputeHandle{})) {
                ++stats_.pooledFailures;
                return false;
            }
            arena->setGlobalQuality(globalQuality_);
            it=pools_.emplace(route.sharedPool,
                              PoolGroup{cfg,std::move(arena)}).first;
            totalPoolCapacity_+=route.sharedPoolCapacity;
            newlyCreated=true;
        }
        SpectraFxGpuSharedArena::EmitterConfig source;
        source.spawn=effect.spawning;
        source.lod=effect.lod;
        source.quality=effect.quality;
        if(++nextPoolTicket_==0) ++nextPoolTicket_;
        const uint64_t ticket=nextPoolTicket_;
        if(!it->second.arena->addEmitter(ticket,std::move(source),
                                         cue.position,cue.direction)) {
            ++stats_.pooledFailures;
            if(newlyCreated) {
                it->second.arena->shutdown();
                totalPoolCapacity_-=it->second.config.particles;
                pools_.erase(it);
            }
            return false;
        }
        out={route.sharedPool,ticket,effect,cue.position,cue.direction};
        return true;
    }
    void advanceGpu(SpectraFxHybridEmitter& emitter,float dt) {
        if(!emitter.gpuActive() || emitter.suspended()) return;
        const bool ok=emitter.updateGpu(dt);
        if(emitter.cpuActive()) ++stats_.gpuRuntimeCpuFallbacks;
        if(!ok) ++stats_.failed;
    }
    void drawGpu(SpectraFxHybridEmitter& emitter,
                 const render::mat4& viewProj,render::float3 right,
                 render::float3 up) {
        if(emitter.gpuActive()&&!emitter.suspended()&&
           !emitter.renderGpu(viewProj,right,up)) ++stats_.failed;
    }
    gfx::Device& device_;
    ParticleSystem& cpu_;
    SpectraFxGpuEmitterLibrary& library_;
    SpectraFxGpuBillboardRenderer sharedRenderer_{};
    bool sharedRendererAttempted_=false;
    gfx::ComputeHandle sharedEvolution_{};
    gfx::ComputeHandle sharedAlphaSort_{};
    gfx::ComputeHandle sharedCompaction_{};
    bool sharedEvolutionAttempted_=false;
    bool sharedAlphaSortAttempted_=false;
    bool sharedCompactionAttempted_=false;
    std::unordered_map<uint32_t,Route> routes_;
    std::unordered_map<uint64_t,std::unique_ptr<SpectraFxHybridEmitter>> active_;
    std::vector<OneShot> oneShots_;
    std::map<std::string,PoolGroup> pools_;
    std::vector<RetiringPool> retiringPools_;
    std::vector<RetiringEmitter> retiringDedicated_;
    uint64_t retiringGpuSlots_=0;
    std::unordered_map<uint64_t,PooledRef> pooledActive_;
    std::vector<PooledShot> pooledShots_;
    uint32_t totalPoolCapacity_=0;
    uint64_t nextPoolTicket_=0;
    render::float3 lastCameraEye_{};
    bool cpuEncodeProfiling_=false;
    SpectraFxGpuBudgetGovernor gpuGovernor_{};
    FxQuality globalQuality_=FxQuality::High;
    Stats stats_{};
};

} // namespace aiverse::fx
