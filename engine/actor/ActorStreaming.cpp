#include "ActorModule.h"

namespace aiverse::actor {
void ActorHost::setContentReader(world::AssetReader reader) {
    streamedCollision_.reset();
    navigationCollisionRevision_=UINT64_MAX;
    navigationReadinessError_.clear();
    if(streamManifest_.getBool("async"))streamedCollision_=std::make_unique<world::CollisionStream>(streamManifest_,std::move(reader),collision_);
}
bool ActorHost::readyToStep(Simulation& sim) {
    // A new command may queue a region move after this preflight. Only
    // requests observed and admitted HERE may mutate positions this tick.
    admittedRegionMoves_.clear();
    if(!streamManifest_.getBool("async")) {
        if(const auto* moves=sim.world().resourceConst<world::RegionMoveRequests>())
            admittedRegionMoves_=moves->destinations;
        return true;
    }
    if(!streamedCollision_)return false;
    const Simulation& view=sim;const auto cars=vehicles(view);
    Fixed radius=64_fx;
    for(const auto& weapon:config_.weapons)radius=fxMax(radius,weapon.range+64_fx);
    radius=fxMax(radius,config_.systems.interactRange+64_fx);
    Fixed movement=config_.motor.sprintSpeed*sim.dt()+config_.motor.radius+8_fx;
    for(const auto& vehicle:cars)movement=fxMax(movement,vehicle.speed*sim.dt()+8_fx);
    radius+=movement;
    std::vector<world::Bounds> regions;
    auto region=[&](Vec3 p,Fixed r){Vec3 extent{r,r,r};regions.push_back({p-extent,p+extent});};
    // Includes respawn and round reset destinations before any actions can consume the tick.
    for(auto p:spawns_)region(p,radius);
    forEachCharacter(view,[&](ConstCharacterRef actor){region(actor.pos,radius);});
    for(const auto& vehicle:cars)region(vehicle.pos,radius);
    for(const auto& p:projectiles(view)){auto travel=fxMax(fxAbs(p.velocity.y),fxMax(fxAbs(p.velocity.x),fxAbs(p.velocity.z)))*sim.dt();
        if(p.program.enabled)travel=fxMax(travel,(p.programSpeed+fxAbs(p.program.verticalSpeed))*sim.dt()+p.program.radius);
        Fixed reach=fxMax(config_.systems.blastRadius,config_.systems.flashRadius);
        if(p.program.enabled)for(const auto& m:config_.weapons[p.weapon].flight.modifiers)
            if(m.kind==projectile::ProjectileModKind::Ricochet)reach=fxMax(reach,m.radius+p.program.radius);
        region(p.pos,travel+reach+16_fx);}
    // Authoritative staged moves extend the exact same collision residency
    // request. The queue is ECS state; this poll only prepares derived assets.
    // A refused step keeps tick, RNG and incoming commands unchanged.
    if(const auto* moves=sim.world().resourceConst<world::RegionMoveRequests>())
        for(const auto& [id,destination]:moves->destinations) {
            (void)id;
            region(destination,4_fx);
        }
    // Collision assets must be present before deriving the bot navigation
    // meshes. Waiting happens before the authoritative tick consumes any
    // command, RNG or gameplay state (Simulation::step/readyToStep).
    if(!streamedCollision_->prepareRegions(regions))return false;
    if(botHost_ && navigationCollisionRevision_!=streamedCollision_->revision()) {
        std::string error;
        if(!rebuildBotNavigation(&error)) {
            navigationReadinessError_=std::move(error);
            return false;
        }
        navigationReadinessError_.clear();
        navigationCollisionRevision_=streamedCollision_->revision();
    }
    if(const auto* moves=sim.world().resourceConst<world::RegionMoveRequests>())
        admittedRegionMoves_=moves->destinations;
    return true;
}
}
