// Authoritative, explicit region activation requests for a real Simulation host.
// The renderer/ChunkStreamer must never issue these gameplay decisions.
#pragma once
#include "engine/world/RegionColdStore.h"
#include "engine/sim/Simulation.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace aiverse::world {

struct RegionActivationQueue {
    static constexpr uint32_t kSchema=1;
    static constexpr size_t kMaxPending=1024;
    // 0 = cold; 1 = hot. Sorted by region Cell, independent of insertion order.
    std::map<Cell,uint8_t> pending;
    uint64_t applied=0;
};

inline void io(Archive& archive,RegionActivationQueue& queue) {
    uint32_t version=RegionActivationQueue::kSchema;
    archive(version);
    if(!archive.ok() || version!=RegionActivationQueue::kSchema){
        archive.fail();return;
    }
    uint64_t applied=queue.applied;
    archive(applied);
    uint32_t count=0;
    if(archive.writing()) {
        if(queue.pending.size()>RegionActivationQueue::kMaxPending){
            archive.fail();return;
        }
        count=uint32_t(queue.pending.size());
    }
    archive(count);
    if(!archive.ok() || count>RegionActivationQueue::kMaxPending){
        archive.fail();return;
    }
    if(archive.writing()){
        for(const auto& [cell,flag]:queue.pending) {
            auto x=cell.x,y=cell.y,z=cell.z;
            uint8_t hot=flag;
            archive(x,y,z,hot);
            if(hot>1){archive.fail();return;}
        }
    }else{
        std::map<Cell,uint8_t> decoded;
        for(uint32_t i=0;i<count && archive.ok();++i){
            Cell cell{};
            uint8_t hot=0;
            archive(cell.x,cell.y,cell.z,hot);
            if(!archive.ok() || hot>1 || !decoded.emplace(cell,hot).second){
                archive.fail();return;
            }
        }
        if(archive.ok()){
            queue.applied=applied;
            queue.pending.swap(decoded);
        }
    }
}

class RegionLifecycleRuntime {
public:
    // Install from a real GameCore native module or another gameplay host's
    // setup. No automatic registration for games without this capability.
    // Every registered parked component must be safe to pause in that host.
    static void install(Simulation& sim,RegionDormancyController controller,
                        uint32_t maxTransitionsPerTick=2) {
        if(!maxTransitionsPerTick || maxTransitionsPerTick>16)
            throw std::invalid_argument("regionLifecycle.maxTransitionsPerTick must be 1..16");
        if(sim.world().resourceConst<RegionActivationQueue>())
            throw std::logic_error("region lifecycle already installed");
        registerRegionEntityLedger(sim);
        registerRegionStableIdentity(sim);
        registerRegionColdState(sim);
        sim.registerPersistentResource<RegionActivationQueue>(
            "aiverse.world.region_activation_queue",RegionActivationQueue::kSchema);
        auto& world=sim.world();
        world.setResource<RegionEntityLedger>(RegionEntityLedger{});
        world.setResource<RegionColdStore>(RegionColdStore{});
        world.setResource<RegionActivationQueue>(RegionActivationQueue{});
        auto policy=std::make_shared<const RegionDormancyController>(std::move(controller));
        sim.addSystem("world.regionLifecycle",
            [policy,maxTransitionsPerTick](Simulation& current){
                RegionLifecycleRuntime::step(current,*policy,maxTransitionsPerTick);
            });
    }

    // Only validated gameplay/native commands and authoritative simulation
    // rules may queue a request. Never call from view/client/render tasks.
    // Repeating the request is idempotent until it has been processed.
    static bool request(Simulation& sim,Cell region,bool active){
        auto* queue=sim.world().resource<RegionActivationQueue>();
        if(!queue)return false;
        auto it=queue->pending.find(region);
        if(it==queue->pending.end() &&
           queue->pending.size()>=RegionActivationQueue::kMaxPending)return false;
        queue->pending[region]=active?1:0;
        return true;
    }

    // Each successful transition consumes exactly one pending request.
    // A failed head stays queued without changing the affected ECS entities
    // or proceeding to later regions. A future command may cancel/replace it.
    // This scheduler is deterministic but is NOT a collision/navmesh gate:
    // real hosts must validate destination readiness before locomotion.
    static size_t step(Simulation& sim,const RegionDormancyController& policy,
                       uint32_t budget){
        auto& world=sim.world();
        auto* queue=world.resource<RegionActivationQueue>();
        const auto* ledger=world.resourceConst<RegionEntityLedger>();
        auto* cold=world.resource<RegionColdStore>();
        if(!queue || !ledger || !cold || !budget)return 0;
        size_t finished=0;
        while(!queue->pending.empty() && finished<budget) {
            const auto item=*queue->pending.begin();
            if(queue->applied==UINT64_MAX)break;
            std::string why;
            if(!policy.setRegionActive(world,*ledger,*cold,
                                       item.first,item.second!=0,&why))
                break;
            queue->pending.erase(item.first);
            ++queue->applied;
            ++finished;
        }
        return finished;
    }
};

} // namespace aiverse::world
