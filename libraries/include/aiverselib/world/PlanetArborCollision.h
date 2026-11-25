// CPU-only structural trunk proxy for the same authored ARBOR species and seed.
// Capsule is a bounded approximation; no GPU mesh or collision solver dependency.
#pragma once
#include "EvolvePlanetNaturalFalls.h"
#include "engine/world/TerraWeaveForestSpeciesProfile.h"
namespace aiverse::lib::worldprocess {
struct PlanetArborTrunk {
    uint32_t species=0;
    int32_t heightCm=0,radiusCm=0;
};
inline PlanetArborTrunk planetArborTrunk(const world::ForestSpeciesProfile& source){
    const auto tree=terraweave::generateTree(source.profile,source.seed,terraweave::TreeLOD::High);
    if(tree.branches.empty())throw std::invalid_argument("ARBOR missing structural trunk");
    const auto& trunk=tree.branches.front();
    if(trunk.parent!=-1||trunk.x0||trunk.z0||trunk.x1||trunk.z1||trunk.y0||trunk.y1<=0)
        throw std::invalid_argument("ARBOR trunk axis");
    return {source.profile.speciesId,trunk.y1,std::max(trunk.startRadiusCm,trunk.endRadiusCm)};
}
inline std::optional<PlanetForestCapsuleRecipe> derivePlanetArborCapsule(
        const PlanetArborTrunk& trunk,const terraweave::AnchoredPlant& plant,
        Fixed floraScale,Fixed remainingFraction,PlanetForestCapsuleRecipe material={}){
    using namespace terraweave;
    if(trunk.species!=plant.anchor.speciesId||trunk.heightCm<=0||trunk.radiusCm<=0||
       floraScale<Fixed::fromRatio(1,1000)||floraScale>10_fx||
       remainingFraction<=Fixed{}||remainingFraction>1_fx||plant.anchor.roots.habit==PlantHabit::Grass)return {};
    try{
        auto scale=floraScale*Fixed::fromRatio(plantAppearance(plant.environment).sizePermille,1000);
        if(plant.anchor.roots.habit==PlantHabit::Shrub)scale=scale*Fixed::fromRatio(1,4);
        const auto half=Fixed::fromRatio(trunk.heightCm,200)*scale;
        const auto min=Fixed::fromRatio(1,50);
        material.radius=fxMax(min,Fixed::fromRatio(trunk.radiusCm,100)*scale*fxSqrt(remainingFraction));
        material.halfHeight=fxMax(min,half-material.radius);
        if(!validPlanetForestCapsule(material))return {};
        return material;
    }catch(const std::exception&){return {};}
}
inline std::optional<PlanetForestCapsuleRecipe> resolvePlanetArborFall(
        const World& world,const evolve::PlanetPlantFall& fall,const PlanetArborTrunk& trunk,
        Fixed floraScale,PlanetForestCapsuleRecipe material){
    using namespace terraweave;
    const auto* roots=world.resourceConst<TerraWeaveAuthorityResource>();
    const auto* wood=world.resourceConst<evolve::PlanetDeadwoodResource>();
    if(!roots||!wood)return {};
    const auto found=std::find_if(roots->rooted.begin(),roots->rooted.end(),[&](const auto& p){
        return AnchoredPlantId{p.anchor.speciesId,p.anchor.plantKey}==fall.event.id;
    });
    const auto log=std::lower_bound(wood->ledger.logs.begin(),wood->ledger.logs.end(),fall.event.id,
        [](const auto& r,auto id){return r.id<id;});
    if(found==roots->rooted.end()||!found->fallen||log==wood->ledger.logs.end()||
       log->id!=fall.event.id||!log->remainingUnits||!log->initialUnits||
       log->remainingUnits>log->initialUnits||log->initialUnits>1000000000ull)return {};
    const auto fraction=fxMax(Fixed::fromRaw(1),Fixed::fromRatio(int64_t(log->remainingUnits),int64_t(log->initialUnits)));
    return derivePlanetArborCapsule(trunk,*found,floraScale,fraction,material);
}
} // namespace aiverse::lib::worldprocess
