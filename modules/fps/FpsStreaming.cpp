#include "FpsModule.h"

namespace aiverse::fps {
void Module::setContentReader(world::AssetReader reader) {
    streamedCollision_.reset();
    if(streamManifest_.getBool("async"))streamedCollision_=std::make_unique<world::CollisionStream>(streamManifest_,std::move(reader),collision_);
}
bool Module::readyToStep(Simulation& sim) {
    if(!streamManifest_.getBool("async"))return true;
    if(!streamedCollision_)return false;
    const auto& s=state(sim);
    Fixed radius=64_fx;
    for(const auto& weapon:config_.weapons)radius=fxMax(radius,weapon.range+64_fx);
    radius=fxMax(radius,config_.systems.interactRange+64_fx);
    Fixed movement=config_.motor.sprintSpeed*sim.dt()+config_.motor.radius+8_fx;
    for(const auto& vehicle:s.vehicles)movement=fxMax(movement,vehicle.speed*sim.dt()+8_fx);
    radius+=movement;
    std::vector<world::Bounds> regions;
    auto region=[&](Vec3 p,Fixed r){Vec3 extent{r,r,r};regions.push_back({p-extent,p+extent});};
    // Includes respawn and round reset destinations before any actions can consume the tick.
    for(auto p:spawns_)region(p,radius);
    for(const auto& actor:s.actors)region(actor.pos,radius);
    for(const auto& vehicle:s.vehicles)region(vehicle.pos,radius);
    for(const auto& p:s.projectiles){auto travel=fxMax(fxAbs(p.velocity.x),fxAbs(p.velocity.z))*sim.dt();
        region(p.pos,travel+fxMax(config_.systems.blastRadius,config_.systems.flashRadius)+16_fx);}
    return streamedCollision_->prepareRegions(regions);
}
}
