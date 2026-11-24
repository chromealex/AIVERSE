// Canonical rooted forest -> shared native static source capsules.
#pragma once
#include "PlanetSourcePublisher.h"
#include "WorldProcessesPlanetForestPipeline.h"
#include "engine/world/TerraWeaveForestHarvestEcs.h"
namespace aiverse::lib::worldprocess {
using PlanetForestStaticSources=std::map<std::string,std::vector<PlanetSourceCollider>>;
inline std::optional<PlanetForestStaticSources> collectPlanetForestStaticSources(
        const World& world,const game::Surface& surface,const PlanetForestPipelineConfig& cfg){
    using namespace terraweave;
    const auto* roots=world.resourceConst<TerraWeaveAuthorityResource>();
    const auto* bindings=world.resourceConst<evolve::PlanetRootDeliveryResource>();
    const auto* wood=world.resourceConst<evolve::PlanetDeadwoodResource>();
    const auto* harvested=world.resourceConst<ForestHarvestResource>();
    if(!roots||!bindings||!wood||!surface.tab||roots->rooted.size()!=bindings->bindings.size()||
       roots->rooted.size()>evolve::kMaxPlanetRootBindings||!evolve::validPlanetDeadwood(*wood,surface))return {};
    PlanetForestStaticSources result;
    std::map<AnchoredPlantId,const evolve::PlanetRootBinding*> byId;
    for(const auto& b:bindings->bindings)if(!byId.emplace(b.id,&b).second)return {};
    std::set<AnchoredPlantId> seen;
    for(const auto& plant:roots->rooted){
        const AnchoredPlantId id{plant.anchor.speciesId,plant.anchor.plantKey};
        if(!id.speciesId||!id.key||!seen.insert(id).second||!byId.contains(id))return {};
        const auto recipe=cfg.naturalFallBodies.find(id.speciesId);
        if(recipe==cfg.naturalFallBodies.end()||(harvested&&isForestHarvested(*harvested,id)))continue;
        const auto& b=*byId.at(id);
        const PlanetSourceKey key{ConsequenceKind::Plant,id.key,id.speciesId};
        if(planetSourceRetired(world,key,b.bodyId))continue;
        auto shape=recipe->second;Fixed fraction=1_fx;
        const auto log=std::lower_bound(wood->ledger.logs.begin(),wood->ledger.logs.end(),id,
            [](const auto& r,auto key){return r.id<key;});
        if(log!=wood->ledger.logs.end()&&log->id==id){
            if(!log->remainingUnits)continue;
            if(!log->initialUnits||log->initialUnits>1000000000ull||log->remainingUnits>log->initialUnits)return {};
            fraction=fxMax(Fixed::fromRaw(1),Fixed::fromRatio(int64_t(log->remainingUnits),int64_t(log->initialUnits)));
        }
        if(const auto trunk=cfg.arborTrunks.find(id.speciesId);trunk!=cfg.arborTrunks.end()){
            const auto scale=cfg.floraScales.find(b.bodyId);
            if(scale==cfg.floraScales.end())return {};
            const auto derived=derivePlanetArborCapsule(trunk->second,plant,scale->second,fraction,shape);
            if(!derived)return {};
            shape=*derived;
        }
        if(!validPlanetForestCapsule(shape))return {};
        const auto frame=surfaceFrame(&surface,b.plantedPosition);
        if(!frame||surface.bodies[size_t(frame->body)].id!=b.bodyId)return {};
        // Pending receipt retains the previous standing collider until the
        // durable replacement/terminal acknowledgement is committed.
        const auto q=fxQuaternionLookRotation(frame->south,frame->up);
        if(!q)return {};
        physics::BodyDesc d;d.motion=physics::Motion::Static;d.shape=physics::ShapeKind::Capsule;
        d.radius=float(shape.radius.toDouble());d.halfHeight=float(shape.halfHeight.toDouble());
        d.friction=float(shape.friction.toDouble());d.density=float(shape.density.toDouble());
        d.position=physics::physisB3(b.plantedPosition-game::Surface::anchor(frame->body)+
            frame->up*(shape.radius+shape.halfHeight));
        d.rotation={float(q->x.toDouble()),float(q->y.toDouble()),float(q->z.toDouble()),float(q->w.toDouble())};
        result[b.bodyId].push_back({key,std::move(d)});
    }
    return result;
}
} // namespace aiverse::lib::worldprocess
