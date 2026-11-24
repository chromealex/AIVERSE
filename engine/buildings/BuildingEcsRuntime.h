#pragma once
// Engine-wide, genre-independent bridge from the EXISTING Simulation
// fixed-tick scheduler + rollback/persistent ECS World to dynamic building
// physics and render snapshots. No ActorHost, FPS/RTS, second ECS, or GPU.
#include "engine/sim/Simulation.h"
#include "engine/buildings/BuildingDualLeafGameSession.h"
#include <algorithm>
#include <cstdint>
#include <memory>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::building {

struct BuildingEcsLeafState {
    std::string socketId;
    uint16_t physicalPermille=0;
    uint64_t revision=0;
    bool open=false;
    bool moving=false;
};
inline void io(Archive& archive,BuildingEcsLeafState& record) {
    archive(record.socketId,record.physicalPermille,record.revision,
            record.open,record.moving);
}
struct BuildingEcsWorldState {
    uint64_t sceneId=0;
    uint64_t revision=0;
    uint32_t fixedTick=0;
    bool graphAcknowledged=false;
    std::vector<BuildingEcsLeafState> leaves;
};
inline void io(Archive& archive,BuildingEcsWorldState& state) {
    archive(state.sceneId,state.revision,state.fixedTick,
            state.graphAcknowledged,state.leaves);
    if(!archive.ok()||state.leaves.size()>64)archive.fail();
    for(const auto& leaf:state.leaves)
        if(leaf.socketId.empty()||leaf.socketId.size()>128||
           leaf.physicalPermille>1000)archive.fail();
}

// Multiple independently placed scenes share one persistent ECS resource.
struct BuildingEcsSceneCollection {
    uint64_t namespaceFingerprint=0;
    std::map<uint64_t,BuildingEcsWorldState> scenes;
    std::map<uint64_t,uint64_t> contentHashes;
};
inline void io(Archive& archive,BuildingEcsSceneCollection& collection){
    archive(collection.namespaceFingerprint,collection.scenes,collection.contentHashes);
    if(!archive.ok()||!collection.namespaceFingerprint||collection.scenes.size()>256||
       collection.scenes.size()!=collection.contentHashes.size())archive.fail();
    for(const auto& [id,state]:collection.scenes)
        if(!id||state.sceneId!=id||!collection.contentHashes.contains(id))
            archive.fail();
}
inline BuildingEcsWorldState* buildingEcsSceneState(World& world,uint64_t sceneId){
    if(auto* all=world.resource<BuildingEcsSceneCollection>()){
        const auto it=all->scenes.find(sceneId);
        return it==all->scenes.end()?nullptr:&it->second;
    }
    auto* state=world.resource<BuildingEcsWorldState>();
    return state&&state->sceneId==sceneId?state:nullptr;
}
inline void registerBuildingEcsSceneCollection(Simulation& sim,uint64_t fingerprint){
    if(!fingerprint)throw std::invalid_argument("Building namespace fingerprint required");
    sim.registerPersistentResource<BuildingEcsSceneCollection>("building.scenes",1);
    sim.world().setResource<BuildingEcsSceneCollection>().namespaceFingerprint=fingerprint;
}

// Stage A MUST run in the common GameModule::setup BEFORE the persistent
// ECS registry seals. Call only once for a Simulation. The resource is
// canonical for saves/checkpoints; never store GPU handles in it.
inline void registerBuildingEcsPersistence(Simulation& sim,
                                            uint64_t sceneId) {
    if(!sceneId)throw std::invalid_argument("ECS building scene ID required");
    sim.registerPersistentResource<BuildingEcsWorldState>(
        "building.dual_leaves",1);
    auto& resource=sim.world().setResource<BuildingEcsWorldState>();
    resource.sceneId=sceneId;
}

// Opt-in shared ECS scheduler adapter; install AFTER Simulation::setup()
// (and after PHYSIS map auto-install), BEFORE its first step. This places
// the door phase after existing movement/physics systems. Render owner
// reads BuildingDualLeafGameSession's committed snapshots separately.
class BuildingEcsRuntime : public std::enable_shared_from_this<BuildingEcsRuntime> {
public:
    static std::shared_ptr<BuildingEcsRuntime> attachAfterSetup(
        Simulation& sim,BuildingDualLeafGameSession& session,
        BuildingDualLeafOwner& physics,world::SpatialWorld& spatial,
        BuildingGraphChangeRouter& router,uint16_t stepPermille=100,
        std::string* error=nullptr) {
        if(sim.tick()!=0){
            if(error)*error="Legacy building registration must precede first tick";
            return {};
        }
        return attachManaged(sim,session,physics,spatial,router,stepPermille,error,true);
    }
    // The common world owner drives managed scenes in one stable-ID system.
    static std::shared_ptr<BuildingEcsRuntime> attachManaged(
        Simulation& sim,BuildingDualLeafGameSession& session,
        BuildingDualLeafOwner& physics,world::SpatialWorld& spatial,
        BuildingGraphChangeRouter& router,uint16_t stepPermille=100,
        std::string* error=nullptr,bool installCallbacks=false){
        auto fail=[&](const char* why){
            if(error)*error=why;
            return std::shared_ptr<BuildingEcsRuntime>{};
        };
        if(!session.attached()||!physics.attached()||
           physics.sceneId()==0||stepPermille==0||stepPermille>250)
            return fail("Building ECS registration needs attached owners");
        const auto* state=buildingEcsSceneState(sim.world(),physics.sceneId());
        if(!state||state->sceneId!=physics.sceneId())
            return fail("Missing or mismatched persistent ECS building scene resource");
        // Initial physical state is part of the persisted ECS resource,
        // including fully open leaves before the first moving command.
        {
            auto& current=*buildingEcsSceneState(sim.world(),physics.sceneId());
            const auto poses=physics.poses();
            if(poses.size()>64)return fail("Building scene aperture budget exceeded");
            current.leaves.clear();
            for(const auto& pose:poses)current.leaves.push_back({
                pose.socketId,pose.physicalPermille,pose.revision,
                pose.open,pose.moving});
            current.fixedTick=sim.tick();
            current.revision=physics.revision();
            current.graphAcknowledged=!physics.pendingGraphs();
        }
        auto runtime=std::shared_ptr<BuildingEcsRuntime>(
            new BuildingEcsRuntime(sim,session,physics,spatial,router,
                                   stepPermille,physics.sceneId()));
        if(!installCallbacks){if(error)error->clear();return runtime;}
        // Never capture a bare pointer into the global system registry.
        // The owner can be destroyed independently; the callback no-ops.
        const std::weak_ptr<BuildingEcsRuntime> weak=runtime;
        sim.addSystem("building.dual_leaves",[weak](Simulation& s){
            if(auto active=weak.lock())active->advance(s);
        });
        sim.addStateRestoredHook([weak](Simulation&){
            // World, RNG and persistent leaf record were rolled back, but
            // external SpatialWorld/graph callback acknowledgements and
            // renderer GPU leases cannot be restored by World COW.
            // Fail closed until the host reconciles/rebuilds the scene.
            if(auto active=weak.lock())active->suspended_=true;
        });
        if(error)error->clear();
        return runtime;
    }

    static std::shared_ptr<BuildingEcsRuntime> suspendedManaged(
        Simulation& sim,BuildingDualLeafGameSession& session,
        BuildingDualLeafOwner& physics,world::SpatialWorld& spatial,
        BuildingGraphChangeRouter& router,uint64_t sceneId,uint16_t stepPermille){
        if(!sceneId||!stepPermille||stepPermille>250||
           !buildingEcsSceneState(sim.world(),sceneId))return {};
        auto result=std::shared_ptr<BuildingEcsRuntime>(new BuildingEcsRuntime(
            sim,session,physics,spatial,router,stepPermille,sceneId));
        result->suspended_=true;return result;
    }
    uint64_t sceneId()const noexcept{return sceneId_;}
    bool enqueue(const BuildingApertureCommand& command,
                 std::string* error=nullptr) {
        if(suspended_||!sim_||!sim_->world().alive(command.actor)){
            if(error)*error="Building ECS authority suspended or actor retired";
            return false;
        }
        return session_->enqueue(sim_->world(),command,error);
    }

    // The source snapshots are authoritative only when both collision
    // commit and registered graph invalidation ACK have succeeded.
    void advance(Simulation& sim) {
        if(suspended_||&sim!=sim_||!physics_->attached()||
           physics_->faulted())return;
        auto frame=session_->fixedTick(
            sim.world(),*physics_,*spatial_,*router_,
            uint64_t(sim.tick())+1u,stepPermille_);
        lastError_=frame.error;
        // Physical commit can precede graph ACK; World must capture that
        // collider revision even while movement and presentation are held.
        // The graphAccepted flag stays false until the same event retries.
        auto* record=buildingEcsSceneState(sim.world(),sceneId_);
        if(!record||record->sceneId!=physics_->sceneId()){
            suspended_=true;
            lastError_="Missing or mismatched committed ECS building state";
            return;
        }
        auto poses=physics_->poses();
        if(poses.size()>64){
            suspended_=true;
            lastError_="Building ECS aperture budget exceeded";
            return;
        }
        record->fixedTick=sim.tick()+1;
        record->revision=physics_->revision();
        record->graphAcknowledged=frame.graphAcknowledged;
        record->leaves.clear();
        for(const auto& pose:poses)
            record->leaves.push_back({pose.socketId,
                pose.physicalPermille,pose.revision,pose.open,pose.moving});
    }

    // Rebind the existing Simulation registration after rollback only
    // after a new physical owner, graph router and presentation session
    // have been rebuilt from the restored ECS leaf record. The old
    // callbacks remain installed exactly once; their weak_ptr resolves
    // to this live owner and now sees the reconciled external state.
    bool rebindAfterRestore(BuildingDualLeafGameSession& session,
                            BuildingDualLeafOwner& physics,
                            world::SpatialWorld& spatial,
                            BuildingGraphChangeRouter& router,
                            std::string* error=nullptr){
        auto reject=[&](const char* reason){
            if(error)*error=reason;
            return false;
        };
        if(!suspended_||!sim_||!session.attached()||
           !physics.attached()||physics.faulted()||
           physics.pendingGraphs())
            return reject("Rebinding requires suspended runtime and fully rebuilt owners");
        auto* state=buildingEcsSceneState(sim_->world(),sceneId_);
        if(!state||!state->sceneId||physics.sceneId()!=state->sceneId||
           !state->graphAcknowledged)
            return reject("Restored scene/graph state not reconciled");
        const auto poses=physics.poses();
        if(poses.size()!=state->leaves.size()||
           physics.revision()!=state->revision)
            return reject("Restored physical scene revision or socket count differs");
        for(size_t i=0;i<poses.size();++i){
            const auto& expected=state->leaves[i];
            const auto& pose=poses[i];
            if(expected.socketId!=pose.socketId||
               expected.physicalPermille!=pose.physicalPermille||
               expected.open!=pose.open||pose.moving)
                return reject("Restored collider position or phase differs");
        }
        const auto owned=physics.ownedColliderIds();
        const auto closed=physics.closedColliderBounds();
        for(uint32_t id:owned){
            const auto a=spatial.colliders().find(id);
            const auto b=closed.find(id);
            if((a==spatial.colliders().end())!=(b==closed.end()))
                return reject("Restored physical collider presence differs");
            if(a!=spatial.colliders().end()&&
               (a->second.bounds.min.x!=b->second.min.x||
                a->second.bounds.min.y!=b->second.min.y||
                a->second.bounds.min.z!=b->second.min.z||
                a->second.bounds.max.x!=b->second.max.x||
                a->second.bounds.max.y!=b->second.max.y||
                a->second.bounds.max.z!=b->second.max.z))
                return reject("Restored real collider bounds differ");
        }
        session_=&session;physics_=&physics;
        spatial_=&spatial;router_=&router;
        lastError_.clear();suspended_=false;
        if(error)error->clear();
        return true;
    }

    // Called by the real ECS owner on authoritative actor retirement,
    // before Entity index/generation is reused. Never destroy actors
    // from inside the renderer or this adapter.
    void retireActor(Entity actor){session_->retireActor(actor);}
    bool suspended()const noexcept{return suspended_;}
    const std::string& lastError()const noexcept{return lastError_;}

    // Host may destroy this runtime AFTER all registered callbacks have
    // stopped; rebuild the shared SpatialWorld from saved World state,
    // reattach physics/graph/presentation owners and create a new
    // session before accepting commands after rollback.
    void suspend()noexcept{suspended_=true;}
private:
    BuildingEcsRuntime(Simulation& sim,BuildingDualLeafGameSession& session,
                       BuildingDualLeafOwner& physics,
                       world::SpatialWorld& spatial,
                       BuildingGraphChangeRouter& router,uint16_t step,uint64_t sceneId)
        :sim_(&sim),session_(&session),physics_(&physics),
         spatial_(&spatial),router_(&router),stepPermille_(step),sceneId_(sceneId){}
    Simulation* sim_;
    BuildingDualLeafGameSession* session_;
    BuildingDualLeafOwner* physics_;
    world::SpatialWorld* spatial_;
    BuildingGraphChangeRouter* router_;
    uint16_t stepPermille_;
    uint64_t sceneId_;
    bool suspended_=false;
    std::string lastError_;
};

} // namespace aiverse::building
