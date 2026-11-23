#pragma once
// Server/world-owner ECS fixed-tick adapter for procedural building riders.
// This layer is intentionally genre neutral. It does not own network peers,
// motor movement, physics, or the global ECS World destroy queue.
#include "engine/buildings/BuildingCirculationEcsScene.h"
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::building {

enum class BuildingActorRetirementReason : uint8_t {
    Destroyed, Disconnected, SceneTransfer
};

struct BuildingActorRetirement {
    Entity actor{};
    uint64_t sceneId=0;
    BuildingActorRetirementReason reason=BuildingActorRetirementReason::Destroyed;
};

struct BuildingActorLifecycleTick {
    bool complete=false;
    std::string error;
    std::vector<Entity> queuedForDestruction;
    std::vector<Entity> awaitingLanding;
    std::vector<Entity> stale;
};

template<BuildingPositionBody Body>
struct BuildingActorFixedTickResult {
    typename BuildingCirculationEcsScene<Body>::Tick circulation;
    BuildingActorLifecycleTick retirement;
    bool complete=false;
};

// One owner-phase queue per authoritative ECS scene. A disconnect must first
// revoke command/interaction authority in the EXISTING session/transport;
// the ECS entity and Body remain live while the cabin moves.
template<BuildingPositionBody Body>
class BuildingActorLifecycleTickOwner {
public:
    using Scene=BuildingCirculationEcsScene<Body>;

    bool requestRetirement(const World& ecs,const Scene& scene,Entity actor,
                           BuildingActorRetirementReason reason,
                           std::string* error=nullptr) {
        if(!scene.sceneId()||!actor.valid()||!ecs.alive(actor)){
            if(error)*error="Retirement requires an attached scene and live entity";
            return false;
        }
        const auto* passenger=ecs.tryGet<BuildingLiftPassenger>(actor);
        if(passenger&&passenger->attached&&passenger->sceneId!=scene.sceneId()){
            if(error)*error="Actor belongs to a different lift scene";
            return false;
        }
        const auto it=scene.scene().host().riderBindings().find(actor.packed());
        if(it==scene.scene().host().riderBindings().end()&&
           (!passenger||!passenger->attached)){
            // Nonriders can also enter the same deterministic retirement queue.
        }else if(it==scene.scene().host().riderBindings().end()){
            if(error)*error="Passenger marker has no authoritative rider binding";
            return false;
        }
        const auto key=actor.packed();
        auto inserted=pending_.emplace(key,BuildingActorRetirement{actor,scene.sceneId(),reason});
        // Idempotent: repeated network disconnect/despawn notices must not
        // enqueue the same ECS generation more than once.
        if(!inserted.second&&inserted.first->second.sceneId!=scene.sceneId()){
            if(error)*error="Retirement already belongs to another scene";
            return false;
        }
        if(error)error->clear();
        return true;
    }

    // One-entry-point order for the authoritative fixed tick: publish
    // platform colliders, publish ECS rider movement, then retire only
    // riders that have reached a physical landing. Caller owns ecs.flush().
    BuildingActorFixedTickResult<Body> advanceFixedTick(
        World& ecs,Scene& scene,world::SpatialWorld& spatial,
        nav::Mesh* navmesh,uint32_t elapsedMs) {
        BuildingActorFixedTickResult<Body> result;
        result.circulation=scene.advance(spatial,navmesh,elapsedMs);
        if(!result.circulation.complete)return result;
        result.retirement=afterCommittedTick(ecs,scene);
        result.complete=result.retirement.complete;
        return result;
    }

    // Call once after a SUCCESSFUL scene.advance() from the existing
    // authoritative fixed tick. The gameplay owner must call ecs.flush()
    // at its normal structural-change barrier, never during ECS iteration.
    // No direct deletion is performed by this adapter.
    BuildingActorLifecycleTick afterCommittedTick(World& ecs,Scene& scene) {
        BuildingActorLifecycleTick result;
        if(!scene.sceneId()){
            result.error="Cannot drain retirement queue without attached scene";
            return result;
        }
        // Refuse cross-scene processing: if a scene was reattached with a
        // new ID, stale pending messages cannot retire actors in that scene.
        for(auto it=pending_.begin();it!=pending_.end();) {
            const auto item=it->second;
            if(item.sceneId!=scene.sceneId()){
                result.stale.push_back(item.actor);
                it=pending_.erase(it);
                continue;
            }
            if(!ecs.alive(item.actor)){
                // External ECS destruction bypassed the required owner gate.
                // Preserve a visible failure instead of silently claiming
                // that the lift host's passenger count was reconciled.
                result.error="Actor destroyed outside building lifecycle owner";
                return result;
            }
            auto* marker=ecs.tryGet<BuildingLiftPassenger>(item.actor);
            if(marker&&marker->attached&&marker->sceneId!=item.sceneId){
                result.error="Actor passenger marker changed scene during retirement";
                return result;
            }
            const auto binding=scene.scene().host().riderBindings().find(item.actor.packed());
            if(marker&&marker->attached&&binding==scene.scene().host().riderBindings().end()){
                result.error="Passenger marker has no host rider binding";
                return result;
            }
            std::string why;
            if(!scene.prepareActorRetirement(item.actor,&why)){
                // Never make this destructive action on a moving passenger.
                result.awaitingLanding.push_back(item.actor);
                ++it;
                continue;
            }
            // ECS owner remains authoritative: this is the standard ECS
            // structural queue, not a custom actor registry/destructor.
            ecs.destroyLater(item.actor);
            result.queuedForDestruction.push_back(item.actor);
            it=pending_.erase(it);
        }
        result.complete=true;
        return result;
    }

    // Prevent a pending actor from being reenabled by a reconnect/login.
    bool pending(Entity actor)const noexcept {
        return pending_.find(actor.packed())!=pending_.end();
    }
    size_t pendingCount()const noexcept { return pending_.size(); }
    // Only after forced world destruction or an explicit owner reconciliation.
    void resetAfterWorldDestruction() { pending_.clear(); }
private:
    std::map<uint64_t,BuildingActorRetirement> pending_;
};

} // namespace aiverse::building
