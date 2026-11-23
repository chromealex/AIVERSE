#pragma once
// Engine-wide, genre-independent bridge from the EXISTING Simulation
// fixed-tick scheduler + rollback/persistent ECS World to dynamic building
// physics and render snapshots. No ActorHost, FPS/RTS, second ECS, or GPU.
#include "engine/sim/Simulation.h"
#include "engine/buildings/BuildingDualLeafGameSession.h"
#include <algorithm>
#include <cstdint>
#include <memory>
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
        auto fail=[&](const char* why){
            if(error)*error=why;
            return std::shared_ptr<BuildingEcsRuntime>{};
        };
        if(sim.tick()!=0||!session.attached()||!physics.attached()||
           physics.sceneId()==0||stepPermille==0||stepPermille>250)
            return fail("Build ECS registration needs attached owners before first tick");
        const auto* state=sim.world().resourceConst<BuildingEcsWorldState>();
        if(!state||state->sceneId!=physics.sceneId())
            return fail("Missing or mismatched persistent ECS building scene resource");
        auto runtime=std::shared_ptr<BuildingEcsRuntime>(
            new BuildingEcsRuntime(sim,session,physics,spatial,router,stepPermille));
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
        if(!frame.presentationAccepted || !frame.graphAcknowledged)return;
        auto* record=sim.world().resource<BuildingEcsWorldState>();
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
        record->graphAcknowledged=true;
        record->leaves.clear();
        for(const auto& pose:poses)
            record->leaves.push_back({pose.socketId,
                pose.physicalPermille,pose.revision,pose.open,pose.moving});
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
                       BuildingGraphChangeRouter& router,uint16_t step)
        :sim_(&sim),session_(&session),physics_(&physics),
         spatial_(&spatial),router_(&router),stepPermille_(step){}
    Simulation* sim_;
    BuildingDualLeafGameSession* session_;
    BuildingDualLeafOwner* physics_;
    world::SpatialWorld* spatial_;
    BuildingGraphChangeRouter* router_;
    uint16_t stepPermille_;
    bool suspended_=false;
    std::string lastError_;
};

} // namespace aiverse::building
