// Genre-independent ECS hitscan request -> ordered resolved-hit events.
// Instantaneous raycasts do NOT create flying projectiles. The host supplies
// candidate snapshots/rewind, target hitboxes and any obstruction cutoff.
#pragma once
#include "engine/mechanics/Namespace.h"
#include "engine/mechanics/EcsHitscanSystems.h"
#include "engine/sim/Simulation.h"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::lib::mechanics {

struct HitscanIntentComponent {
    Vec3 origin{}, delta{};
    Fixed maximumFraction=1_fx; // host can shorten this using world/vehicle occlusion
    uint32_t shotId=0;
    bool requested=false;
};

struct HitscanResolvedEvent {
    Entity shooter=kNullEntity, target=kNullEntity;
    uint32_t shotId=0;
    Vec3 point{};
    Fixed fraction{};
};

// A direct, synchronous policy shared by the ECS system below and genre
// adapters that must resolve a shot *inside* their existing gameplay tick.
// Unlike the scheduled system it preserves the selected immutable pose,
// which FPS uses for historical head/limb hit-zone calculation.
template<class Candidate>
struct HitscanResolvedCandidate {
    const Candidate* pose=nullptr;
    HitscanResolvedEvent event{};
    explicit operator bool()const {return pose!=nullptr;}
};

template<class CandidateRange,class Eligible,class Bounds,class TargetEntity>
auto resolveHitscanIntent(const HitscanIntentComponent& intent,Entity shooter,
                          const CandidateRange& targets,Eligible&& eligible,
                          Bounds&& bounds,TargetEntity&& entityOf) {
    using Candidate=std::remove_cv_t<std::remove_reference_t<
        decltype(*std::begin(targets))>>;
    HitscanResolvedCandidate<Candidate> result;
    if(!intent.requested || intent.maximumFraction<=Fixed{} ||
       intent.maximumFraction>1_fx ||
       intent.delta.lengthSq()<=Fixed{})return result;
    const auto selected=selectClosestHitscanCandidate(
        targets,intent.origin,intent.delta,intent.maximumFraction,
        std::forward<Eligible>(eligible),std::forward<Bounds>(bounds));
    if(!selected)return result;
    result.pose=selected.target;
    result.event={shooter,entityOf(*selected.target),intent.shotId,
                  intent.origin+intent.delta*selected.fraction,
                  selected.fraction};
    return result;
}

// Candidates: const range&(Simulation&,Entity,const HitscanIntentComponent&)
// Eligible: bool(Simulation&,Entity,const Candidate&)
// Bounds: world::Bounds(Simulation&,const Candidate&)
// TargetEntity: Entity(Simulation&,const Candidate&)
// HitSink: void(Simulation&,const HitscanResolvedEvent&).
//
// Each source entity owns one request. Consume requests exactly once, even
// when a ray is invalid, no target is available, or an obstruction wins.
// Hit callbacks run only AFTER stable ECS request traversal, not while the
// source component pool is being updated. Preserve source dense order.
// Host-specific rewind and instant-damage callback ordering must be validated
// before migrating any existing genre's authoritative hit logic.
template<class Candidates,class Eligible,class Bounds,
         class TargetEntity,class HitSink>
void addHitscanResolutionSystem(Simulation& sim,std::string name,
        Candidates candidates,Eligible eligible,Bounds bounds,
        TargetEntity entityOf,HitSink onHit) {
    sim.addSystem(std::move(name),
        [candidates=std::move(candidates),eligible=std::move(eligible),
         bounds=std::move(bounds),entityOf=std::move(entityOf),
         onHit=std::move(onHit)](Simulation& current) mutable {
            World& world=current.world();
            auto* requests=world.prepareExistingWrite<HitscanIntentComponent>();
            if(!requests)return;
            std::vector<HitscanResolvedEvent> events;
            const size_t count=requests->size();
            for(size_t i=0;i<count;++i) {
                const Entity shooter=requests->entityAt(i);
                if(!world.alive(shooter))continue;
                auto& request=requests->dataAt(i);
                if(!request.requested)continue;
                // Capture the accepted request before clearing the one-shot
                // flag; the pure policy can also run synchronously in FPS.
                const HitscanIntentComponent shot=request;
                request.requested=false;
                if(shot.maximumFraction<=Fixed{} ||
                   shot.maximumFraction>1_fx ||
                   shot.delta.lengthSq()<=Fixed{})continue;
                const auto& targets=candidates(current,shooter,shot);
                const auto resolved=resolveHitscanIntent(
                    shot,shooter,targets,
                    [&](const auto& candidate) {
                        return eligible(current,shooter,candidate);
                    },
                    [&](const auto& candidate) {return bounds(current,candidate);},
                    [&](const auto& candidate) {return entityOf(current,candidate);});
                if(!resolved || !world.alive(resolved.event.target))continue;
                events.push_back(resolved.event);
            }
            for(const auto& hit:events)onHit(current,hit);
        });
}

} // namespace aiverse::lib::mechanics
