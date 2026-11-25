// Spherical adapter of TerraWeave's existing bounded nutrient cycle.
// Soil/dissolved addresses encode canonical Surface cells, not planar
// metres. One opted-in NEREID sea per planetary body; no water solver.
#pragma once
#include "EvolvePlanetDeadwood.h"
#include "engine/world/TerraWeaveForestNutrientCycle.h"
#include "engine/world/TerraWeaveForestHarvestEcs.h"

namespace aiverse::evolve {
struct PlanetNutrientWeather {
    uint32_t rainfallPermille=0,soilMoisturePermille=700;
};
using PlanetNutrientWeatherSampler=std::function<PlanetNutrientWeather(std::string_view,Vec3)>;
inline std::optional<terraweave::ForestNutrientCycleOutcome> advancePlanetNutrients(
        World& world,const game::Surface& surface,uint32_t tick,
        terraweave::ForestNutrientCycleSettings settings={},
        PlanetNutrientWeatherSampler weather={}){
    using namespace terraweave;
    using namespace lib::nereid;
    const auto* state=world.resourceConst<PlanetDeadwoodResource>();
    const auto* roots=world.resourceConst<TerraWeaveAuthorityResource>();
    const auto* delivery=world.resourceConst<PlanetRootDeliveryResource>();
    if(!state||!roots||!delivery||tick==UINT32_MAX||
       roots->rooted.size()!=delivery->bindings.size()||
       roots->rooted.size()>kMaxPlanetRootBindings||
       !validPlanetDeadwood(*state,surface)||world.resourceConst<ForestDeadwoodResource>())return {};
    auto next=*state;
    // Coarse dissolved ledger is spatial, not multi-water-body keyed.
    // Reject ambiguous sea ownership, even if one competing shell is dry.
    std::map<int,Entity> seas;
    bool valid=true;
    world.eachExistingConst<WaterSphereComponent>([&](Entity e,const WaterSphereComponent& sea){
        if(!world.alive(e)||!validWaterSphere(sea))return;
        for(size_t b=0;b<surface.bodies.size();++b){
            if(!surface.bodies[b].hasSurface()||sea.center!=game::Surface::anchor(int(b)))continue;
            if(!seas.emplace(int(b),e).second)valid=false;
        }
    });
    if(!valid)return {};
    std::map<AnchoredPlantId,const PlanetRootBinding*> bindings;
    for(const auto& b:delivery->bindings)if(!bindings.emplace(b.id,&b).second)return {};
    std::set<AnchoredPlantId> dead,seen;
    if(const auto* forest=world.resourceConst<ForestEcologyResource>())
        for(const auto& p:forest->plants)if(p.dead)dead.insert(p.id);
    const auto* harvested=world.resourceConst<ForestHarvestResource>();
    std::map<int,uint32_t> demands;
    for(const auto& p:roots->rooted){
        const AnchoredPlantId id{p.anchor.speciesId,p.anchor.plantKey};
        const auto it=bindings.find(id);
        if(it==bindings.end()||!seen.insert(id).second)return {};
        const auto frame=lib::worldprocess::surfaceFrame(&surface,it->second->plantedPosition);
        if(!frame||surface.bodies[size_t(frame->body)].id!=it->second->bodyId)return {};
        if(!p.fallen&&!dead.contains(id)&&(!harvested||!isForestHarvested(*harvested,id)))
            demands[frame->cell]+=p.anchor.roots.habit==PlantHabit::Shrub?1u:2u;
    }
    struct Sample {
        int cell=-1,body=-1;
        Vec3 ground{},flow{};
        ForestNutrientCellClimate climate{};
    };
    std::map<int,Sample> cache;
    const auto sample=[&](int64_t x,int64_t z)->const Sample&{
        if(x<0||x>=surface.W||z<0||z>=surface.H)
            throw std::invalid_argument("planet nutrient address");
        const int cell=int(z*surface.W+x);
        if(const auto it=cache.find(cell);it!=cache.end())return it->second;
        int b=-1,f=-1,i=-1,j=-1;
        if(!surface.cellFace(cell,b,f,i,j))throw std::invalid_argument("planet nutrient void cell");
        Sample result;result.cell=cell;result.body=b;result.ground=surface.cellCenter(cell);
        const auto met=weather?weather(surface.bodies[size_t(b)].id,result.ground):PlanetNutrientWeather{};
        result.climate.rainfallPermille=met.rainfallPermille;
        result.climate.soilMoisturePermille=met.soilMoisturePermille;
        if(const auto d=demands.find(cell);d!=demands.end())result.climate.demandUnits=d->second;
        const auto shore=game::samplePlanetShore(world,surface,result.ground,Fixed{});
        if(!shore)throw std::invalid_argument("planet nutrient shore");
        if(shore->water.submerged){
            const auto it=seas.find(b);
            if(it==seas.end()||it->second!=shore->water.body)throw std::invalid_argument("planet nutrient sea identity");
            result.climate.submergedPermille=1000;
            result.climate.waterBodyKey=shore->water.body.packed()+1ull;
            if(!result.climate.waterBodyKey)throw std::invalid_argument("planet nutrient sea key overflow");
            result.flow=shore->water.flow;
        }
        validateForestNutrientCellClimate(result.climate);
        return cache.emplace(cell,result).first->second;
    };
    const auto route=[&](int64_t x,int64_t z,const ForestNutrientCellClimate& c)
            ->std::optional<ForestNutrientDownstream>{
        const auto& source=sample(x,z);
        if(!c.submergedPermille||!c.waterBodyKey)return {};
        const auto radial=surface.cellDir(source.cell);
        int best=-1;Fixed speed{};
        for(int k=0;k<4;++k){
            const int cell=surface.neighbor(source.cell,k);
            int b=-1,f=-1,i=-1,j=-1;
            if(cell<0||!surface.cellFace(cell,b,f,i,j)||b!=source.body)continue;
            const auto delta=surface.cellDir(cell)-radial;
            const auto tangent=delta-radial*delta.dot(radial);
            if(tangent.lengthSq().raw<=0)continue;
            const auto along=source.flow.dot(fxNormalize(tangent));
            if(along>speed||(along==speed&&along>Fixed{}&&cell<best)){
                speed=along;best=cell;
            }
        }
        if(best<0)return {};
        const auto cm=uint32_t(std::clamp<int64_t>((speed*100).toInt(),0,10000));
        return ForestNutrientDownstream{best%surface.W,best/surface.W,cm};
    };
    try{
        const auto result=advanceForestNutrientCycle(next.ledger,tick,
            [&](int64_t x,int64_t z){return sample(x,z).climate;},settings,{},route);
        if(!validPlanetDeadwood(next,surface))return {};
        world.setResource<PlanetDeadwoodResource>(std::move(next));return result;
    }catch(const std::exception&){return {};}
}
} // namespace aiverse::evolve
