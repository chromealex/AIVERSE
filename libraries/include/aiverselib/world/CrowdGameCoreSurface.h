// Native host bridge: only issue normal movement orders. GameCore alone
// owns Transform, Mobile route, slope speed, separation and projection.
#pragma once
#include "CrowdSurfaceSlots.h"
#include "engine/game/GameCore.h"

namespace aiverse::lib::worldprocess {
inline bool surfaceCrowdEligible(const game::GameCore& host,const World& read,Entity e){
    const auto* unit=read.tryGetConst<game::Unit>(e);
    if(!unit||unit->hp.raw<=0||unit->type>=host.rules().types.size()||
       (unit->flags&(game::kUnitOffMap|game::kUnitInTransit|game::kUnitPossessed|
                    game::kUnitHold|game::kUnitUnderConstruction))||
       read.has<game::Transit>(e)||!read.has<game::Mobile>(e)||
       !read.has<game::Orders>(e))return false;
    const auto& type=host.rules().types[unit->type];
    return type.speed.raw>0&&!(type.layer&(game::kLayerAir|game::kLayerOrbit));
}
// Run after GameCore movement. Orders are consumed at the next owner tick;
// never overwrite player/AI commands, current paths or combat targets.
// Legacy Agent x/z/speed/radius are not a second position/motion authority.
inline bool advanceGameCoreSurfaceCrowd(game::GameCore& host,Simulation& sim){
    const auto* surface=host.surface(sim);
    if(!surface)return false;
    auto& world=sim.world();
    const World& read=world;
    const auto& grid=host.state(sim).grid;
    auto plan=crowd::planSurfaceSlots(read,*surface,sim.tick(),
        [&](Entity e){return surfaceCrowdEligible(host,read,e);},
        [&](Entity e,const SurfaceFrame& frame){
            const auto* unit=read.tryGetConst<game::Unit>(e);
            const auto& type=host.rules().types[unit->type];
            return grid.passableAt(frame.origin,type.pathLayer);
        });
    if(!plan)return false;
    std::vector<crowd::SurfaceSlotIntent> orders;
    for(const auto& intent:plan->intents){
        const auto* mobile=read.tryGetConst<game::Mobile>(intent.agent);
        const auto* queue=read.tryGetConst<game::Orders>(intent.agent);
        const auto* tr=read.tryGetConst<Transform>(intent.agent);
        const auto* combat=read.tryGetConst<game::Combat>(intent.agent);
        if(!mobile||!queue||!tr||mobile->moving||!queue->queue.empty()||
           (combat&&read.alive(combat->target)))continue;
        // The existing Move order uses a minimum goal radius of 0.7 units
        // for one mover. Match that tolerance to avoid repeat arrival orders.
        if(surface->groundDist(tr->pos,intent.target)<=Fixed::fromRatio(7,10))continue;
        orders.push_back(intent);
    }
    if(!crowd::commitSurfaceSlots(world,*plan))return false;
    for(const auto& intent:orders){
        game::Order order;
        order.type=game::OrderType::Move;order.point=intent.target;order.param=1;
        host.issue(sim,intent.agent,order,true);
    }
    return true;
}
inline void installGameCoreSurfaceCrowd(Simulation& sim){
    crowd::registerPersistence(sim);
    sim.addSystem("crowd.surface_slots",[](Simulation& current){
        auto* host=dynamic_cast<game::GameCore*>(&current.module());
        if(host)(void)advanceGameCoreSurfaceCrowd(*host,current);
    });
}
} // namespace aiverse::lib::worldprocess
