// Authoritative, explicit region activation requests for a real Simulation host.
// The renderer/ChunkStreamer must never issue these gameplay decisions.
#pragma once
#include "engine/world/RegionColdStore.h"
#include "engine/sim/Simulation.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <span>
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
    // Immutable setup-time configuration, not another persisted resource.
    // Admission reads authoritative Simulation state at the selected tick
    // barrier. No external side effects, wall clock or async asset callbacks.
    using Admission=std::function<bool(const Simulation&,Cell,std::span<const Entity>)>;
    struct GuardedPolicy {
        RegionDormancyController::ColdAdvancePlan plan;
        Admission mayPark;
        Admission mayWake;
    };

private:
    static void installResources(Simulation& sim,const RegionDormancyController& controller,
                                 uint32_t maxTransitionsPerTick) {
        if(!maxTransitionsPerTick || maxTransitionsPerTick>16)
            throw std::invalid_argument("regionLifecycle.maxTransitionsPerTick must be 1..16");
        if(sim.world().resourceConst<RegionActivationQueue>() ||
           sim.world().resourceConst<RegionEntityLedger>() ||
           sim.world().resourceConst<RegionColdStore>())
            throw std::logic_error("region lifecycle already installed or region resources pre-owned");
        registerRegionEntityLedger(sim);
        registerRegionStableIdentity(sim);
        registerRegionColdState(sim,controller.positionReader());
        sim.registerPersistentResource<RegionActivationQueue>(
            "aiverse.world.region_activation_queue",RegionActivationQueue::kSchema);
        auto& world=sim.world();
        world.setResource<RegionEntityLedger>(RegionEntityLedger{});
        world.setResource<RegionColdStore>(RegionColdStore{});
        world.setResource<RegionActivationQueue>(RegionActivationQueue{});
    }

    // Single queue owner and drain implementation. Canonical head-of-line
    // blocking preserves replay order; a failed transition is not ACKed.
    // Callbacks may not mutate the queue or its resources.
    template<class Transition>
    static size_t drain(Simulation& sim,uint32_t budget,Transition transition,
                        std::string* blockedReason) {
        if(blockedReason)blockedReason->clear();
        auto& world=sim.world();
        const auto* queued=world.resourceConst<RegionActivationQueue>();
        if(!budget || !queued || queued->pending.empty())return 0;
        const auto* ledger=world.resourceConst<RegionEntityLedger>();
        if(!ledger || !world.resourceConst<RegionColdStore>()) {
            if(blockedReason)*blockedReason="region lifecycle resources unavailable";
            return 0;
        }
        // Acquire writable resources before the structural operation. No
        // detachment or cold-store copy in the ordinary no-request tick.
        auto* queue=world.resource<RegionActivationQueue>();
        auto* cold=world.resource<RegionColdStore>();
        size_t finished=0;
        while(!queue->pending.empty() && finished<budget) {
            const auto item=*queue->pending.begin();
            if(queue->applied==UINT64_MAX || item.second>1) {
                if(blockedReason)*blockedReason="invalid region activation queue state";
                break;
            }
            std::string why;
            if(!transition(world,*ledger,*cold,item.first,item.second!=0,&why)) {
                if(blockedReason)*blockedReason=std::move(why);
                break;
            }
            queue->pending.erase(item.first);
            ++queue->applied;
            ++finished;
        }
        return finished;
    }

public:
    // Legacy pause-only installation. Choose this OR installGuarded, not
    // both. The owning host must register the system at a structural barrier.
    static void install(Simulation& sim,RegionDormancyController controller,
                        uint32_t maxTransitionsPerTick=2) {
        installResources(sim,controller,maxTransitionsPerTick);
        auto policy=std::make_shared<const RegionDormancyController>(std::move(controller));
        sim.addSystem("world.regionLifecycle",
            [policy,maxTransitionsPerTick](Simulation& current){
                RegionLifecycleRuntime::step(current,*policy,maxTransitionsPerTick);
            });
    }

    // Register the guarded system at the host's chosen authoritative phase,
    // after its clocked live components reach sim.tick() and before readers
    // need restored state. The domain owner must arrange system ordering,
    // complete references and actual readiness; this is not a solver.
    static void installGuarded(Simulation& sim,RegionDormancyController controller,
                               GuardedPolicy guarded,uint32_t maxTransitionsPerTick=2) {
        std::string error;
        if(!guarded.mayPark || !guarded.mayWake ||
           !controller.validateCompletePlan(guarded.plan,&error))
            throw std::invalid_argument("region lifecycle guarded policy invalid: "+error);
        auto policy=std::make_shared<const RegionDormancyController>(std::move(controller));
        auto guards=std::make_shared<const GuardedPolicy>(std::move(guarded));
        installResources(sim,*policy,maxTransitionsPerTick);
        sim.addSystem("world.regionLifecycle",
            [policy,guards,maxTransitionsPerTick](Simulation& current){
                RegionLifecycleRuntime::stepGuarded(current,*policy,*guards,maxTransitionsPerTick);
            });
    }

    // Only validated gameplay/native commands and authoritative simulation
    // rules may queue a request. Never call from view/client/render tasks.
    // Repeating the request is idempotent until it has been processed.
    static bool request(Simulation& sim,Cell region,bool active){
        const auto* current=sim.world().resourceConst<RegionActivationQueue>();
        if(!current)return false;
        const auto it=current->pending.find(region);
        const uint8_t target=active?1:0;
        if(it!=current->pending.end() && it->second==target)return true;
        if(it==current->pending.end() &&
           current->pending.size()>=RegionActivationQueue::kMaxPending)return false;
        auto* queue=sim.world().resource<RegionActivationQueue>();
        queue->pending[region]=target;
        return true;
    }

    // Explicit owner-authorized cancellation unblocks a refused head.
    // No transition occurred, so applied is not incremented.
    static bool cancel(Simulation& sim,Cell region) {
        const auto* current=sim.world().resourceConst<RegionActivationQueue>();
        if(!current)return false;
        if(!current->pending.contains(region))return true;
        sim.world().resource<RegionActivationQueue>()->pending.erase(region);
        return true;
    }

    static size_t step(Simulation& sim,const RegionDormancyController& policy,
                       uint32_t budget){
        return drain(sim,budget,[&](World& world,const RegionEntityLedger& ledger,
                                    RegionColdStore& cold,Cell region,bool active,std::string* why){
            return policy.setRegionActive(world,ledger,cold,region,active,why);
        },nullptr);
    }

    // Native guarded consumers must use this path, never the legacy step
    // to bypass their configured policy. Refused head remains persisted;
    // retry naturally uses the then-current authoritative simulation tick.
    // Optional reason is a transient caller output, not gameplay state.
    static size_t stepGuarded(Simulation& sim,const RegionDormancyController& policy,
                              const GuardedPolicy& guarded,uint32_t budget,
                              std::string* blockedReason=nullptr) {
        if(!guarded.mayPark || !guarded.mayWake) {
            if(blockedReason)*blockedReason="missing region lifecycle admission predicate";
            return 0;
        }
        return drain(sim,std::min<uint32_t>(budget,16),
            [&](World& world,const RegionEntityLedger& ledger,RegionColdStore& cold,
                Cell region,bool active,std::string* why){
                const auto admitPark=[&](const World&,Cell cell,std::span<const Entity> members){
                    return guarded.mayPark(std::as_const(sim),cell,members);
                };
                const auto admitWake=[&](const World&,Cell cell,std::span<const Entity> members){
                    return guarded.mayWake(std::as_const(sim),cell,members);
                };
                return policy.setRegionActiveGuarded(world,ledger,cold,region,active,
                           sim.tick(),guarded.plan,admitPark,admitWake,why);
            },blockedReason);
    }
};

} // namespace aiverse::world
