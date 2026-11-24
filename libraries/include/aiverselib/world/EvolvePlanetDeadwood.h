// Planet-specific address adapter for the existing TerraWeave deadwood
// ledger. Its x/z keys encode stacked Surface grid column/row, NOT metres.
// Do not pass this ledger to planar nutrient/water/harvest systems.
#pragma once
#include "EvolvePlanetRootSupport.h"
#include "engine/game/PlanetTerrainWater.h"
#include "engine/world/TerraWeaveForestDeadwood.h"
#include "engine/core/Hash.h"
#include <map>

namespace aiverse::evolve {
struct PlanetDeadwoodResource {
    uint64_t topology=0;
    uint32_t baselineNutrientsPermille=500;
    terraweave::ForestDeadwoodResource ledger;
};
inline uint64_t planetDeadwoodTopology(const game::Surface& s){
    Hasher hash;hash.i32(s.W);hash.i32(s.H);hash.u32(uint32_t(s.bodies.size()));
    for(const auto& b:s.bodies){hash.str(b.id);hash.i32(b.faceSize);hash.fx(b.radius);hash.i32(b.rowBase);}
    return hash.value();
}
inline void io(Archive& a,PlanetDeadwoodResource& r){
    a(r.topology,r.baselineNutrientsPermille,r.ledger);
    if(!r.topology||r.baselineNutrientsPermille>1000||!r.ledger.water.empty()||
       !r.ledger.microWater.empty())a.fail();
}
inline bool initializePlanetDeadwood(World& world,const game::Surface& surface,uint32_t baseline=500){
    if(!surface.tab||baseline>1000||world.resourceConst<PlanetDeadwoodResource>()||
       world.resourceConst<terraweave::ForestDeadwoodResource>())return false;
    PlanetDeadwoodResource r;r.topology=planetDeadwoodTopology(surface);r.baselineNutrientsPermille=baseline;
    world.setResource<PlanetDeadwoodResource>(std::move(r));return true;
}
inline void registerPlanetDeadwoodPersistence(Simulation& sim){
    sim.registerPersistentResource<PlanetDeadwoodResource>("evolve.planet_deadwood",1);
}
inline bool validPlanetDeadwood(const PlanetDeadwoodResource& r,const game::Surface& s){
    using namespace terraweave;
    if(r.topology!=planetDeadwoodTopology(s)||r.baselineNutrientsPermille>1000||
       !r.ledger.water.empty()||!r.ledger.microWater.empty()||!deadwoodLedgerBalanced(r.ledger))return false;
    const auto validCell=[&](int64_t x,int64_t z){
        int b=-1,f=-1,i=-1,j=-1;
        return x>=0&&x<s.W&&z>=0&&z<s.H&&s.cellFace(int(z*s.W+x),b,f,i,j);
    };
    for(const auto& log:r.ledger.logs){
        constexpr auto stride=ForestDeadwoodResource::kNutrientCellCm;
        if(log.xCm%stride||log.zCm%stride||!validCell(log.xCm/stride,log.zCm/stride))return false;
    }
    for(const auto& c:r.ledger.soil)if(!validCell(c.x,c.z))return false;
    return true;
}
inline std::optional<std::pair<int64_t,int64_t>> planetDeadwoodAddress(
        const game::Surface& surface,const PlanetRootBinding& binding){
    const auto frame=lib::worldprocess::surfaceFrame(&surface,binding.plantedPosition);
    if(!frame||surface.bodies[size_t(frame->body)].id!=binding.bodyId)return {};
    constexpr auto stride=terraweave::ForestDeadwoodResource::kNutrientCellCm;
    const int64_t x=int64_t(frame->cell%surface.W)*stride,z=int64_t(frame->cell/surface.W)*stride;
    if(x<0||x>terraweave::kWorldLimitCm||z<0||z>terraweave::kWorldLimitCm)return {};
    return std::pair{x,z};
}
// Death and fall share species/key, so a later fall cannot seed the same
// organic matter twice. Use historic planting cell until a native owner
// supplies a different transport/deposition policy.
inline bool seedPlanetDeadwood(PlanetDeadwoodResource& resource,const game::Surface& surface,
        const terraweave::AnchoredPlant& plant,const PlanetRootBinding& binding,uint32_t tick){
    if(binding.id!=terraweave::AnchoredPlantId{plant.anchor.speciesId,plant.anchor.plantKey})return false;
    const auto address=planetDeadwoodAddress(surface,binding);
    if(!address)return false;
    auto copy=plant;copy.anchor.xCm=address->first;copy.anchor.zCm=address->second;
    try{
        (void)terraweave::seedForestDeadwood(resource.ledger,copy,tick,plant.fallen);
        return true;
    }catch(const std::exception&){return false;}
}
inline uint32_t planetDeadwoodFertility(const PlanetDeadwoodResource& r,const game::Surface& s,
        const PlanetRootBinding& binding,uint32_t baseline){
    const auto at=planetDeadwoodAddress(s,binding);
    if(!at)return baseline;
    return terraweave::deadwoodNutrientsPermille(r.ledger,at->first,at->second,baseline);
}
using PlanetDeadwoodClimateSampler=std::function<terraweave::ForestDeadwoodClimate(
    std::string_view body,Vec3 ground)>;
inline std::optional<terraweave::ForestDeadwoodDecayOutcome> advancePlanetDeadwood(
        World& world,const game::Surface& surface,uint32_t tick,
        terraweave::ForestDeadwoodDecaySettings settings={},PlanetDeadwoodClimateSampler climate={}){
    using namespace terraweave;
    const auto* source=world.resourceConst<PlanetDeadwoodResource>();
    const auto* roots=world.resourceConst<TerraWeaveAuthorityResource>();
    const auto* bindings=world.resourceConst<PlanetRootDeliveryResource>();
    if(!source||!roots||!bindings||tick==UINT32_MAX||
       roots->rooted.size()!=bindings->bindings.size()||!validPlanetDeadwood(*source,surface)||
       world.resourceConst<ForestDeadwoodResource>())return {};
    auto next=*source;
    std::map<AnchoredPlantId,const PlanetRootBinding*> byId;
    for(const auto& b:bindings->bindings)if(!byId.emplace(b.id,&b).second)return {};
    std::set<AnchoredPlantId> dead;
    if(const auto* forest=world.resourceConst<ForestEcologyResource>())
        for(const auto& item:forest->plants)if(item.dead)dead.insert(item.id);
    try{
        for(const auto& plant:roots->rooted){
            const AnchoredPlantId id{plant.anchor.speciesId,plant.anchor.plantKey};
            const auto binding=byId.find(id);
            if(binding==byId.end())return {};
            if((plant.fallen||dead.contains(id))&&!seedPlanetDeadwood(next,surface,plant,*binding->second,tick))return {};
        }
        const auto result=advanceForestDeadwood(next.ledger,tick,settings,[&](const ForestDeadwoodRecord& log){
            constexpr auto stride=ForestDeadwoodResource::kNutrientCellCm;
            const int cell=int((log.zCm/stride)*surface.W+log.xCm/stride);
            int b=-1,f=-1,x=-1,y=-1;
            if(!surface.cellFace(cell,b,f,x,y))throw std::invalid_argument("planet deadwood cell");
            const auto ground=surface.cellCenter(cell);
            auto c=climate?climate(surface.bodies[size_t(b)].id,ground):ForestDeadwoodClimate{};
            const auto shore=game::samplePlanetShore(world,surface,ground,Fixed{});
            if(shore&&shore->water.submerged)c.moisturePermille=1000;
            return c;
        });
        if(!validPlanetDeadwood(next,surface))return {};
        world.setResource<PlanetDeadwoodResource>(std::move(next));return result;
    }catch(const std::exception&){return {};}
}
} // namespace aiverse::evolve
