// SpectraFX Stage 35 — persistent, genre-independent read-only ECS bindings
// for the pooled GPU/CPU visual presenter. Never place PoolSourceId, Metal
// handles, particle state, or render timing in authoritative ECS World.
#pragma once
#include "engine/mechanics/Namespace.h"

#include "EcsVfxPresentation.h"
#include "engine/fx/SpectraGpuPoolWorld.h"
#include <cstdint>
#include <cmath>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::spectra {

// Host owns this adapter AND the GpuPoolWorld. The pool must outlive it.
// Use an isolated rollback scope: resetForRollback resets the WHOLE pool.
// sync() is called AFTER pool.beginPresentationFrame(), BEFORE pool.step().
// World reads are const; each Entity::packed() includes its generation.
class GpuEcsAttachmentPresenter final {
public:
    using ResolvePose = lib::mechanics::SpectraPresentationSystem::ResolvePose;

    explicit GpuEcsAttachmentPresenter(GpuPoolWorld& pool) : pool_(pool) {}
    GpuEcsAttachmentPresenter(const GpuEcsAttachmentPresenter&) = delete;
    GpuEcsAttachmentPresenter& operator=(const GpuEcsAttachmentPresenter&) = delete;

    // Pooled continuous effects MUST be explicitly configured by native game
    // presentation code. No genre-specific ECS fields or implicit emitters.
    bool bindEffect(std::string effect,PoolContinuousEmission emission) {
        if(effect.empty() || !std::isfinite(emission.burstsPerSecond) ||
           !std::isfinite(emission.metresPerBurst) ||
           !std::isfinite(emission.lifetimeSeconds) ||
           emission.burstsPerSecond<0.f ||
           emission.burstsPerSecond>120.f ||
           emission.metresPerBurst<0.f || emission.metresPerBurst>1.e5f ||
           emission.lifetimeSeconds<0.f ||
           emission.lifetimeSeconds>86400.f ||
           emission.maxBurstsPerFrame==0 ||
           emission.maxBurstsPerFrame>64u)return false;
        const uint64_t revision=nextPolicyRevision_++;
        bindings_[std::move(effect)]={emission,revision};
        return true;
    }

    // A temporary pose failure should NOT silently kill existing effects.
    // The affected source is paused and can resume when its pose returns.
    // Disappearing/disabled components are detached using killOnDetach.
    bool sync(const World& world,const ResolvePose& resolver) {
        reason_.clear();
        if(!pool_.ready() || !resolver) {
            reason_="SpectraFX ECS attachments need initialized pool and pose resolver";
            return false;
        }
        struct Wanted {
            std::string effect;
            fx::float3 position{},direction{0,1,0};
            uint32_t revision=0;
            int64_t scaleRaw=0;
            bool killOnDetach=false, hasPose=false;
        };
        std::map<uint64_t,Wanted> desired;
        world.eachExistingConst<lib::mechanics::SpectraSourceComponent>(
            [&](Entity entity,const lib::mechanics::SpectraSourceComponent& component) {
                if(!world.alive(entity) || !component.enabled ||
                   component.effect.empty())return;
                Wanted w;
                w.effect=component.effect;
                w.revision=component.revision;
                w.scaleRaw=component.scale.raw;
                w.killOnDetach=component.killOnDetach;
                // Keep an entry even if rendering pose is temporarily missing.
                w.hasPose=resolver(world,entity,w.position,w.direction);
                desired[entity.packed()]=std::move(w);
            });

        // Validate all incoming declarations BEFORE detaching any previously
        // presented sources. Bad ECS authoring must not cause partial teardown.
        for(const auto& [key,want]:desired) {
            (void)key;
            if(bindings_.find(want.effect)==bindings_.end()) {
                reason_="SpectraFX ECS effect has no bound continuous policy: "+want.effect;
                return false;
            }
            if(want.scaleRaw!=(1_fx).raw) {
                reason_="SpectraFX pooled ECS attachment requires unit scale: "+want.effect;
                return false;
            }
            if(want.hasPose &&
                (!std::isfinite(want.position.x) ||
                 !std::isfinite(want.position.y) ||
                 !std::isfinite(want.position.z) ||
                 !std::isfinite(want.direction.x) ||
                 !std::isfinite(want.direction.y) ||
                 !std::isfinite(want.direction.z))) {
                reason_="SpectraFX ECS pose contains nonfinite coordinates: "+want.effect;
                return false;
            }
        }
        // Stage replacement sources BEFORE touching any old attachment. If
        // capacity is exhausted, old effects continue unaffected; the caller
        // can increase PoolWorldBudgets::maxSources for overlap headroom.
        auto equivalent=[&](uint64_t key,const Wanted& want) {
            auto old=attached_.find(key);
            if(old==attached_.end())return false;
            auto policy=bindings_.find(want.effect);
            return policy!=bindings_.end() &&
                   old->second.effect==want.effect &&
                   old->second.revision==want.revision &&
                   old->second.scaleRaw==want.scaleRaw &&
                   old->second.policyRevision==policy->second.revision &&
                   pool_.sourceBindingCurrent(old->second.handle);
        };
        std::map<uint64_t,Attached> staged;
        const auto abandonStaged=[&]() {
            // Staged emitters have not been submitted yet, so no GPU kill is
            // required. Invalidating their handles leaves original live.
            for(auto& [key,state]:staged) {
                (void)key;
                (void)pool_.retireSource(state.handle);
            }
            staged.clear();
        };
        for(const auto& [key,want]:desired) {
            if(equivalent(key,want))continue;
            const auto policy=bindings_.find(want.effect);
            const PoolSourceId id=pool_.createSource(want.effect);
            if(!id.valid()) {
                const std::string why=pool_.reason();
                abandonStaged();
                reason_="SpectraFX ECS staged admission failed (old sources kept): "+why;
                return false;
            }
            Attached entry;
            entry.handle=id;entry.effect=want.effect;
            entry.revision=want.revision;
            entry.scaleRaw=want.scaleRaw;
            entry.policyRevision=policy->second.revision;
            entry.killOnDetach=want.killOnDetach;
            staged.emplace(key,entry);
            bool ok=pool_.configureContinuous(id,policy->second.emission);
            if(ok && want.hasPose)
                ok=pool_.setSourceTransform(id,want.position,want.direction);
            if(ok && !want.hasPose)
                ok=pool_.pauseSource(id);
            if(!ok) {
                const std::string why=pool_.reason();
                abandonStaged();
                reason_="SpectraFX ECS staged source configuration failed: "+why;
                return false;
            }
            staged[key].pausedForMissingPose=!want.hasPose;
        }

        // Check expected GPU selective-kill preconditions before applying any
        // detach. Hardware health can still change after the check.
        for(const auto& [key,current]:attached_) {
            auto want=desired.find(key);
            if(want!=desired.end() && equivalent(key,want->second))continue;
            if(!pool_.canStopSource(current.handle,current.killOnDetach)) {
                abandonStaged();
                reason_="SpectraFX ECS detach preflight rejected; old sources kept";
                return false;
            }
        }

        // Commit stage: retire obsolete sources. An actual RHI failure here
        // can be partial (checked GPU command failed after another detach).
        // The retained map allows retry; it is not falsely called atomic.
        for(auto it=attached_.begin();it!=attached_.end();) {
            auto want=desired.find(it->first);
            if(want!=desired.end() && equivalent(it->first,want->second)) {
                ++it;continue;
            }
            if(!pool_.stopSource(it->second.handle,it->second.killOnDetach) ||
               !pool_.retireSource(it->second.handle)) {
                const std::string why=pool_.reason();
                abandonStaged();
                reason_="SpectraFX ECS detach failed; retry presentation sync: "+why;
                return false;
            }
            it=attached_.erase(it);
        }

        // Staged handles now become owned by the presentation attachment map.
        for(auto& [key,state]:staged)
            attached_.emplace(key,std::move(state));
        staged.clear();

        // Update existing sources only after successful staging/retirement.
        // Missing poses pause rather than retire. When the pose returns,
        // attach it and resume without invalidating source generations.
        for(const auto& [key,want]:desired) {
            auto it=attached_.find(key);
            if(it==attached_.end())continue; // retained for defensive safety
            it->second.killOnDetach=want.killOnDetach;
            if(!want.hasPose) {
                if(!pool_.pauseSource(it->second.handle)) {
                    reason_="SpectraFX ECS source pause rejected";
                    return false;
                }
                it->second.pausedForMissingPose=true;
                continue;
            }
            if(!pool_.setSourceTransform(it->second.handle,
                                         want.position,want.direction)) {
                reason_="SpectraFX ECS source transform rejected";
                return false;
            }
            if(it->second.pausedForMissingPose &&
               !pool_.resumeSource(it->second.handle)) {
                reason_="SpectraFX ECS source resume rejected";
                return false;
            }
            it->second.pausedForMissingPose=false;
        }
        return true;
    }

    // Call at the presentation boundary after a COMMITTED simulation tick.
    // Repeated frames at the same tick may update interpolated render poses,
    // but a lower tick is an explicit rollback requiring resetForRollback().
    bool syncCommitted(uint64_t tick,const World& world,
                       const ResolvePose& resolver) {
        if(haveCommittedTick_ && tick<lastCommittedTick_) {
            reason_="SpectraFX ECS tick moved backward; resetForRollback required";
            return false;
        }
        if(!sync(world,resolver))return false;
        lastCommittedTick_=tick;
        haveCommittedTick_=true;
        return true;
    }
    uint64_t lastCommittedTick() const {return lastCommittedTick_;}
    // When the graphics Device was permanently lost, the native host shuts
    // down/reconstructs the entire presentation pool. Renderer source IDs are
    // no longer valid. Drop ONLY this presenter's local tracking, preserving
    // effect policies; DO NOT access the old GPU pool or authoritative ECS.
    void discardTrackingAfterPoolRecreation() {
        attached_.clear();
        lastCommittedTick_=0;
        haveCommittedTick_=false;
        reason_.clear();
    }

    // Call outside any active render/compute pass and before replaying ticks.
    // Rejects without clearing tracking if GPU pool cannot reset safely.
    bool resetForRollback() {
        if(!pool_.resetForRollback()) {
            reason_="SpectraFX ECS rollback reset: "+pool_.reason();
            return false;
        }
        attached_.clear();
        lastCommittedTick_=0;
        haveCommittedTick_=false;
        reason_.clear();
        return true;
    }

    // Use when presenter stops but the pool remains alive. Existing particles
    // may age naturally (killLiving=false) or be explicitly invalidated.
    // Must be called at a safe graphics phase for GPU selective kills.
    bool detachAll(bool killLiving=false) {
        for(auto it=attached_.begin();it!=attached_.end();) {
            if(!pool_.stopSource(it->second.handle,killLiving) ||
               !pool_.retireSource(it->second.handle)) {
                reason_="SpectraFX ECS detachAll failed: "+pool_.reason();
                return false;
            }
            it=attached_.erase(it);
        }
        lastCommittedTick_=0;
        haveCommittedTick_=false;
        reason_.clear();
        return true;
    }

    size_t activeAttachments() const {return attached_.size();}
    const std::string& reason() const {return reason_;}

private:
    struct Policy {
        PoolContinuousEmission emission{};
        uint64_t revision=0;
    };
    struct Attached {
        PoolSourceId handle{};
        std::string effect;
        uint64_t policyRevision=0;
        uint32_t revision=0;
        int64_t scaleRaw=0;
        bool killOnDetach=false, pausedForMissingPose=false;
    };

    GpuPoolWorld& pool_;
    std::map<std::string,Policy> bindings_;
    std::map<uint64_t,Attached> attached_;
    uint64_t nextPolicyRevision_=1,lastCommittedTick_=0;
    bool haveCommittedTick_=false;
    std::string reason_;
};

} // namespace aiverse::spectra
