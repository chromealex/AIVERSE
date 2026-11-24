// Coordinate adapter for the existing TerraWeave forest lifecycle.
// Only rooted, explicitly mapped trees/shrubs; no cosmetic grass ECS.
#pragma once
#include "EvolvePlanetRootSupport.h"
#include "EvolvePlanetRootClimate.h"
#include "engine/world/TerraWeaveForestEcology.h"
#include "engine/world/TerraWeaveForestHarvestEcs.h"
#include "engine/world/TerraWeaveForestDeadwood.h"
#include <map>
#include <set>
namespace aiverse::evolve {
struct PlanetForestEcologyOutcome {
    terraweave::ForestEcologyResult ecology;
    std::vector<PlanetRootBinding> born;
    uint64_t authorityRevision=0;
};
inline constexpr size_t kMaxPlanetForestComparisons=4194304;
inline void registerPlanetForestEcologyPersistence(Simulation& sim){
    sim.registerPersistentResource<terraweave::ForestEcologyResource>("terraweave.forest_ecology",1);
    registerPlanetRootDeliveryPersistence(sim);
}
// Call at the deterministic world barrier, not from the renderer.
// The owner supplies PHYSIS mappings for this immutable snapshot. Existing
// planar forest and deadwood stages must not run over these same records.
inline std::optional<PlanetForestEcologyOutcome> advancePlanetForestEcology(
        World& world,const game::Surface& surface,uint32_t tick,
        const std::vector<terraweave::ForestEcologySpecies>& species,
        terraweave::ForestEcologySettings cfg={},
        std::span<const PlanetWeatherMapping> weather={},uint64_t climateTick=0){
    using namespace terraweave;
    namespace wp=lib::worldprocess;
    const auto* authority=world.resourceConst<TerraWeaveAuthorityResource>();
    const auto* delivery=world.resourceConst<PlanetRootDeliveryResource>();
    if(!authority||!delivery||!surface.tab||tick==UINT32_MAX||
       authority->rooted.size()!=delivery->bindings.size()||
       authority->rooted.size()>kMaxPlanetRootBindings||
       world.resourceConst<ForestDeadwoodResource>())return {};
    cfg.maxTrackedPlants=std::min<uint32_t>(cfg.maxTrackedPlants,kMaxPlanetRootBindings);
    validateForestEcologySettings(cfg);
    const auto* prior=world.resourceConst<ForestEcologyResource>();
    ForestEcologyResource ecology=prior?*prior:ForestEcologyResource{};
    if(ecology.initialized&&tick<ecology.lastStepTick)return {};
    if(ecology.initialized&&tick-ecology.lastStepTick<cfg.intervalTicks)
        return PlanetForestEcologyOutcome{{},{},authority->stateRevision};
    std::map<AnchoredPlantId,PlanetRootBinding> bindings;
    for(const auto& b:delivery->bindings)if(!bindings.emplace(b.id,b).second)return {};
    std::map<AnchoredPlantId,Vec3> ground;
    std::map<AnchoredPlantId,int> bodies;
    std::map<AnchoredPlantId,ForestEcologyClimate> climates;
    std::set<AnchoredPlantId> dead,retired;
    for(const auto& history:ecology.plants)if(history.dead)dead.insert(history.id);
    for(const auto& event:delivery->events)retired.insert(event.fall.event.id);
    std::map<uint32_t,ForestEcologySpecies> traits;
    for(const auto& policy:species){
        validateForestEcologySpecies(policy);
        if(!traits.emplace(policy.speciesId,policy).second)return {};
    }
    if(traits.empty()||traits.size()>64)return {};
    std::vector<PlanetRootClimateSite> sites;
    std::vector<PlanetEcologyRequest> requests;
    std::vector<AnchoredPlantId> ids;
    const auto* harvested=world.resourceConst<ForestHarvestResource>();
    for(const auto& plant:authority->rooted){
        const AnchoredPlantId id{plant.anchor.speciesId,plant.anchor.plantKey};
        const auto it=bindings.find(id);
        if(it==bindings.end()||ground.contains(id)||(harvested&&isForestHarvested(*harvested,id)))return {};
        const auto frame=wp::surfaceFrame(&surface,it->second.plantedPosition);
        if(!frame||surface.bodies[size_t(frame->body)].id!=it->second.bodyId)return {};
        const auto pos=surface.project(it->second.plantedPosition);
        ground.emplace(id,pos);bodies.emplace(id,frame->body);
        ForestEcologyClimate climate{1000,0,plant.environment.waterPermille,plant.environment.nutrientsPermille};
        sites.push_back({it->second.bodyId,pos,climate,false,true});
        requests.push_back({it->second.bodyId,pos,climate,{}});
        ids.push_back(id);
    }
    std::vector<PlanetEcologySample> samples;
    if(weather.empty()){
        auto sampled=samplePlanetEcology(world,surface,requests);
        if(!sampled)return {};samples=std::move(*sampled);
    }else{
        auto sampled=samplePlanetRootClimate(world,surface,weather,sites,climateTick);
        if(!sampled)return {};samples=std::move(sampled->sites);
    }
    for(size_t i=0;i<ids.size();++i)climates.emplace(ids[i],samples[i].climate);
    struct Crown {AnchoredPlantId id;int body;Vec3 pos;int64_t radius,top;PlantHabit habit;};
    std::vector<Crown> crowns;
    for(const auto& plant:authority->rooted){
        const AnchoredPlantId id{plant.anchor.speciesId,plant.anchor.plantKey};
        const auto trait=traits.find(id.speciesId);
        if(plant.fallen||dead.contains(id)||trait==traits.end())continue;
        const auto maturity=plantAppearance(plant.environment).sizePermille;
        const int64_t radius=std::clamp<int64_t>(int64_t(plant.anchor.roots.radiusCm)*
            trait->second.crownRadiusFactorPermille*maturity/1000000ll,60,2000);
        const int64_t height=std::clamp<int64_t>(int64_t(plant.anchor.roots.radiusCm)*
            (plant.anchor.roots.habit==PlantHabit::Tree?8ll:3ll)*maturity/1000ll,
            plant.anchor.roots.habit==PlantHabit::Tree?150ll:40ll,30000ll);
        crowns.push_back({id,bodies.at(id),ground.at(id),radius,int64_t(plant.anchor.plantedHeightCm)+height,plant.anchor.roots.habit});
    }
    // Bounded exact same-body chord checks. No planar canopy buckets or
    // cross-body neighbor leakage. Later stages may use Surface buckets.
    size_t comparisons=0;
    const auto consume=[&](){if(++comparisons>kMaxPlanetForestComparisons)
        throw std::length_error("planet forest geometry budget");};
    std::vector<PlanetRootBinding> born;
    ForestEcologyGeometry geometry;
    geometry.light=[&](const AnchoredPlant& plant,uint32_t sun){
        const AnchoredPlantId id{plant.anchor.speciesId,plant.anchor.plantKey};
        uint32_t blocked=0;
        for(const auto& crown:crowns){
            if(crown.id==id||crown.body!=bodies.at(id))continue;
            consume();
            const auto d=surface.groundDist(ground.at(id),crown.pos);
            if(d>=Fixed::fromRatio(crown.radius,100))continue;
            const auto cm=(d*100).toInt();
            const uint64_t r2=uint64_t(crown.radius)*crown.radius;
            const auto closeness=uint32_t((r2-uint64_t(cm*cm))*1000/r2);
            const auto top=int64_t(plant.anchor.plantedHeightCm)+int64_t(plant.anchor.roots.radiusCm)*
                (plant.anchor.roots.habit==PlantHabit::Tree?6ll:2ll);
            const uint32_t strength=crown.habit==PlantHabit::Tree?650u:300u;
            blocked=std::min<uint32_t>(900,blocked+uint32_t(uint64_t(closeness)*strength*
                (crown.top>=top?1000u:300u)/1000000ull));
        }
        return uint32_t(uint64_t(sun)*(1000-blocked)/1000);
    };
    geometry.recruit=[&](const AnchoredPlant& parent,AnchoredPlantId child,int64_t dx,int64_t dz)->std::optional<RootAnchor>{
        const AnchoredPlantId id{parent.anchor.speciesId,parent.anchor.plantKey};
        if(retired.contains(child)||(harvested&&isForestHarvested(*harvested,child)))return {};
        const auto frame=wp::surfaceFrame(&surface,ground.at(id));
        if(!frame)return {};
        const auto radius=surface.bodies[size_t(frame->body)].radius;
        const auto tangent=frame->east*Fixed::fromRatio(dx,100)+frame->south*Fixed::fromRatio(dz,100);
        const auto length=tangent.length();
        if(length<=Fixed{}||radius<=Fixed{}||length>=radius*game::kFxTwoPi/4)return {};
        const auto turns=length/(radius*game::kFxTwoPi);
        const auto dir=fxNormalize(frame->up*fxCosTurns(turns)+(tangent/length)*fxSinTurns(turns));
        const auto point=surface.project(game::Surface::anchor(frame->body)+dir*radius);
        const auto resolved=wp::surfaceFrame(&surface,point);
        if(!resolved||resolved->body!=frame->body)return {};
        const auto spacing=Fixed::fromRatio(120,100);
        for(const auto& [other,p]:ground){
            if(bodies.at(other)!=frame->body)continue;
            consume();
            if(surface.groundDist(point,p)<spacing)return {};
        }
        for(const auto& recruit:born)if(recruit.bodyId==bindings.at(id).bodyId&&
            surface.groundDist(point,recruit.plantedPosition)<spacing)return {};
        const auto water=game::samplePlanetShore(world,surface,point,Fixed{});
        if(!water||water->water.submerged)return {};
        const auto height=surface.altitude(point);
        const auto raw=height.raw*100;
        auto anchor=parent.anchor;anchor.plantKey=child.key;
        // Planar coordinates intentionally unused for spherical records.
        anchor.xCm=anchor.zCm=0;
        anchor.plantedHeightCm=int32_t(raw>=0?(raw+Fixed::kOne/2)/Fixed::kOne:-((-raw+Fixed::kOne/2)/Fixed::kOne));
        born.push_back({child,bindings.at(id).bodyId,point});return anchor;
    };
    auto rooted=authority->rooted;
    PlanetForestEcologyOutcome out;
    out.authorityRevision=authority->stateRevision;
    try{
        out.ecology=advanceForestEcology(ecology,rooted,tick,species,{},
            [&](const AnchoredPlant& plant){return climates.at({plant.anchor.speciesId,plant.anchor.plantKey});},cfg,&geometry);
    }catch(const std::exception&){return {};}
    if(out.ecology.born.size()!=born.size())return {};
    auto updated=*authority;auto rootDelivery=*delivery;
    if(!out.ecology.changed.empty()||!out.ecology.born.empty()||!out.ecology.died.empty()){
        if(updated.stateRevision==UINT64_MAX)return {};
        updated.rooted=std::move(rooted);out.authorityRevision=++updated.stateRevision;
    }
    for(const auto& b:born)rootDelivery.bindings.push_back(b);
    std::sort(rootDelivery.bindings.begin(),rootDelivery.bindings.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    out.born=std::move(born);
    World staged=world;
    staged.setResource<ForestEcologyResource>(std::move(ecology));
    staged.setResource<TerraWeaveAuthorityResource>(std::move(updated));
    staged.setResource<PlanetRootDeliveryResource>(std::move(rootDelivery));
    world=std::move(staged);return out;
}
} // namespace aiverse::evolve
