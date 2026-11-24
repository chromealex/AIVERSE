// TerraWeave Stage70 — native NEREID adaptor to 4m saved nutrient tiles.
// Read-only water/terrain query and explicit authoring topology. No competing
// water solver; no terrain/renderer/Box3D mutations from the ECS system.
#pragma once
#include "engine/world/TerraWeaveForestMicroNutrients.h"
#include "engine/world/TerraWeaveForestNutrientEcs.h"
#include "engine/world/nereid/NereidConfluences.h"
#include "engine/world/nereid/NereidCascades.h"
#include "engine/world/nereid/NereidQueryIndex.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace aiverse::terraweave {

struct ForestNereidMicroJunctionSettings {
    uint32_t maxJunctions=64;
    uint32_t searchRadiusFineCells=8; // within ~32m of NEREID mouth
};
inline void validateForestNereidMicroJunctionSettings(
        ForestNereidMicroJunctionSettings s){
    if(!s.maxJunctions || s.maxJunctions>256 ||
       !s.searchRadiusFineCells || s.searchRadiusFineCells>16)
        throw std::invalid_argument("TerraWeave invalid 4m NEREID junction search");
}

// The sampler is a pure read of the completed NEREID tick on current
// authoritative Earthworks height. Every saved 4m tile has a distinct
// generation-safe body identifier; two different rivers in the SAME old
// 32m tile never share an amount.
inline ForestMicroWaterSampler makeNereidMicroWaterSampler(
        const EarthworksLayer& terrain,
        const lib::nereid::WaterQueryIndex& water) {
    return [&terrain,&water](int64_t x4,int64_t z4,
                             uint64_t expectedBody)->ForestMicroSample {
        const int64_t cm=ForestDeadwoodResource::kMicroWaterCellCm;
        // Public native callers may supply any signed tile coordinate.
        // Reject it BEFORE multiplication to avoid signed overflow/UB.
        const int64_t limit=kWorldLimitCm/cm;
        if(x4<=-limit || x4>=limit || z4<=-limit || z4>=limit)
            return {};
        const int64_t px=x4*cm+cm/2;
        const int64_t pz=z4*cm+cm/2;
        const int64_t edge=kWorldLimitCm-terrain.spacingCm();
        if(px<=-edge || px>=edge || pz<=-edge || pz>=edge)
            return {};
        const auto sample=water.sample({
            Fixed::fromRatio(px,100),
            Fixed::fromRatio(terrain.sampleHeightCm(px,pz),100),
            Fixed::fromRatio(pz,100)});
        if(!sample.found || !sample.submerged ||
           !sample.body.valid())
            return {};
        const uint64_t key=sample.body.packed()+1ull;
        if(expectedBody && key!=expectedBody)return {};
        const auto quantize=[](Fixed speed)->int32_t{
            const int64_t cap=100ll*Fixed::kOne;
            return int32_t(
                std::clamp<int64_t>(speed.raw,-cap,cap)*100/Fixed::kOne);
        };
        return {key,true,quantize(sample.flow.x),
                          quantize(sample.flow.z)};
    };
}

inline std::optional<ForestMicroKey> nearestNereidMicroCell(
        Vec3 mouth,uint64_t bodyKey,
        const ForestMicroWaterSampler& waterAt,
        ForestNereidMicroJunctionSettings settings) {
    // This is also a public native helper. Enforce the bounded search
    // contract here, not only in buildNereidMicroJunctions: an external
    // caller could otherwise supply UINT32_MAX radius and stall a tick.
    validateForestNereidMicroJunctionSettings(settings);
    if(!bodyKey || !waterAt)
        throw std::invalid_argument("TerraWeave invalid micro body/sampler");
    // A malformed authored mouth can carry an arbitrary Fixed raw value.
    // Validate in raw units BEFORE multiplying by 100 (centimetres).
    constexpr int64_t mouthLimitRaw=
        (kWorldLimitCm-10000)*Fixed::kOne/100;
    if(mouth.x.raw<=-mouthLimitRaw ||
       mouth.x.raw>=mouthLimitRaw ||
       mouth.z.raw<=-mouthLimitRaw ||
       mouth.z.raw>=mouthLimitRaw)
        return std::nullopt;
    const int64_t mx=mouth.x.raw*100/Fixed::kOne;
    const int64_t mz=mouth.z.raw*100/Fixed::kOne;
    const int64_t cellCm=ForestDeadwoodResource::kMicroWaterCellCm;
    const int64_t centerX=detail::floorDiv(mx,cellCm);
    const int64_t centerZ=detail::floorDiv(mz,cellCm);
    std::optional<ForestMicroKey> best;
    uint64_t smallest=UINT64_MAX;
    const int64_t radius=int64_t(settings.searchRadiusFineCells);
    for(int64_t dz=-radius;dz<=radius;++dz)
        for(int64_t dx=-radius;dx<=radius;++dx){
            const int64_t x=centerX+dx,z=centerZ+dz;
            const int64_t px=x*cellCm+cellCm/2;
            const int64_t pz=z*cellCm+cellCm/2;
            const int64_t vx=px-mx,vz=pz-mz;
            const uint64_t distance=uint64_t(vx*vx+vz*vz);
            const auto sample=waterAt(x,z,bodyKey);
            validateForestMicroSample(sample);
            if(!sample.submerged || sample.bodyKey!=bodyKey)continue;
            const ForestMicroKey key{bodyKey,x,z};
            validateForestMicroKey(key);
            if(!best || distance<smallest ||
               (distance==smallest && key<*best)){
                best=key;smallest=distance;
            }
        }
    return best;
}

// Only NEREID-authored directed mouth topology may cross a body boundary.
// All link keys are sorted by stable ECS Entity+inlet-slot, not unordered
// hash iteration. With a small-stream mouth wholly inside one old 32m
// region, different 4m cells can now be linked and carry real separate
// NEREID body IDs without changing the soil 32m grid.
inline std::vector<ForestMicroJunction> buildNereidMicroJunctions(
        const World& world,const ForestMicroWaterSampler& waterAt,
        ForestNereidMicroJunctionSettings settings={}) {
    validateForestNereidMicroJunctionSettings(settings);
    using ID=std::pair<uint64_t,uint8_t>;
    std::vector<std::pair<ID,ForestMicroJunction>> links;
    const auto add=[&](Entity source,Entity dest,Vec3 inlet,Vec3 outlet,
                       ID identifier) {
        if(!world.alive(source) || !world.alive(dest) ||
           source==dest || source==kNullEntity || dest==kNullEntity)
            return;
        const auto* a=world.tryGetConst<lib::nereid::WaterBodyComponent>(source);
        const auto* b=world.tryGetConst<lib::nereid::WaterBodyComponent>(dest);
        if(!a || !b || !lib::nereid::validWaterBody(*a) ||
           !lib::nereid::validWaterBody(*b))
            return;
        const auto src=nearestNereidMicroCell(
            inlet,source.packed()+1ull,waterAt,settings);
        const auto dst=nearestNereidMicroCell(
            outlet,dest.packed()+1ull,waterAt,settings);
        if(!src || !dst)return;
        links.emplace_back(identifier,
            ForestMicroJunction{0,*src,*dst,32});
        if(links.size()>settings.maxJunctions)
            throw std::length_error("TerraWeave too many NEREID micro junctions");
    };
    world.eachExistingConst<lib::nereid::WaterConfluenceComponent>(
        [&](Entity e,const lib::nereid::WaterConfluenceComponent& c){
            if(!world.alive(e) || !lib::nereid::validWaterConfluence(c))
                return;
            for(uint8_t slot=0;slot<c.inletCount;++slot)
                add(c.inlets[slot],c.outlet,c.mouth,c.mouth,
                    {e.packed(),slot});
        });
    world.eachExistingConst<lib::nereid::WaterCascadeConnectionsComponent>(
        [&](Entity e,
            const lib::nereid::WaterCascadeConnectionsComponent& c){
            if(!world.alive(e) || !c.enabled)return;
            const auto* cascade=world.tryGetConst<
                lib::nereid::WaterCascadeComponent>(e);
            if(!cascade || !lib::nereid::validWaterCascade(*cascade))
                return;
            add(c.inlet,c.outlet,cascade->points.front().position,
                cascade->points.back().position,{e.packed(),uint8_t(255)});
        });
    std::sort(links.begin(),links.end(),
        [](const auto& a,const auto& b){return a.first<b.first;});
    std::vector<ForestMicroJunction> out;
    out.reserve(links.size());
    for(size_t i=0;i<links.size();++i){
        if(i && links[i-1].first==links[i].first)
            throw std::invalid_argument(
                "TerraWeave duplicate NEREID micro junction");
        links[i].second.linkKey=uint64_t(i+1);
        out.push_back(links[i].second);
    }
    return out;
}

// Explicit Stage70 native migration: the old coarse 32m water tile had
// NO water-body identity. Caller supplies an authored allocation plan;
// this verifies EVERY destination against real completed NEREID wetness
// and ground before atomically modifying the ECS World resource.
inline uint64_t claimForestLegacyNereidWaterEcs(
        World& ecs,const TerraWeaveEcsAuthorityConfig& cfg,
        std::span<const ForestLegacyMicroClaim> claims) {
    const auto* old=ecs.resourceConst<ForestDeadwoodResource>();
    const auto* soil=ecs.resourceConst<TerraWeaveAuthorityResource>();
    if(!old || !soil)
        throw std::logic_error("TerraWeave native legacy split missing authority");
    const auto ground=materializeEcsTerrain(*soil,cfg);
    const lib::nereid::WaterQueryIndex index(ecs);
    const auto waterAt=makeNereidMicroWaterSampler(ground,index);
    ForestDeadwoodResource next=*old;
    const uint64_t transferred=claimForestLegacyWater(
        next,claims,[&](ForestMicroKey k) {
            const auto sample=waterAt(k.x,k.z,k.bodyKey);
            return sample.submerged && sample.bodyKey==k.bodyKey;
        });
    if(!transferred)return 0;
    World staged=ecs;
    staged.setResource<ForestDeadwoodResource>(std::move(next));
    ecs=std::move(staged);
    return transferred;
}

// Game-authored NEREID outlet command for one body-tagged 4m location.
// Ownership, wetness and the actual saved terrain sample must agree at
// the completed ECS barrier. Topological OUTLET selection still belongs
// to the native host (there is no imaginary automatic ocean export).
inline uint64_t exportForestNereidMicroOutletEcs(
        World& ecs,const TerraWeaveEcsAuthorityConfig& cfg,
        ForestMicroKey key,uint64_t maximumUnits,uint64_t commandId) {
    validateForestMicroKey(key);
    const auto* old=ecs.resourceConst<ForestDeadwoodResource>();
    const auto* authority=ecs.resourceConst<TerraWeaveAuthorityResource>();
    if(!old || !authority)
        throw std::logic_error("TerraWeave 4m outlet missing authoritative state");
    if(!commandId)
        throw std::invalid_argument("TerraWeave invalid 4m outlet command");
    if(commandId<=old->lastOutflowCommandId)return 0;
    const auto ground=materializeEcsTerrain(*authority,cfg);
    const lib::nereid::WaterQueryIndex index(ecs);
    const auto waterAt=makeNereidMicroWaterSampler(ground,index);
    const auto wet=waterAt(key.x,key.z,key.bodyKey);
    if(!wet.submerged || wet.bodyKey!=key.bodyKey)
        throw std::invalid_argument("TerraWeave 4m outlet not current NEREID water");
    ForestDeadwoodResource next=*old;
    const uint64_t exported=exportForestMicroDissolvedNutrients(
        next,key,maximumUnits,commandId);
    World staged=ecs;
    staged.setResource<ForestDeadwoodResource>(std::move(next));
    ecs=std::move(staged);
    return exported;
}

// A separate opt-in system (NOT installed alongside Stage68 coarse
// system) uses the SAME canonical tick and World COW persistence.
inline ForestMicroOutcome advanceForestMicroNutrientsEcs(
        World& ecs,const TerraWeaveEcsAuthorityConfig& cfg,
        uint64_t completedTick,ForestMicroSettings settings={},
        ForestNutrientWeatherSampler weather={},
        ForestNereidMicroJunctionSettings junctionSettings={}) {
    validateForestMicroSettings(settings);
    validateForestNereidMicroJunctionSettings(junctionSettings);
    const auto* previous=ecs.resourceConst<ForestDeadwoodResource>();
    const auto* authority=ecs.resourceConst<TerraWeaveAuthorityResource>();
    if(!previous || !authority)
        throw std::logic_error("TerraWeave 4m nutrient system missing authority");
    if(previous->nutrientInitialized &&
       completedTick<previous->lastNutrientTick)
        throw std::invalid_argument("TerraWeave micro ECS reversed tick");
    if(previous->nutrientInitialized &&
       completedTick-previous->lastNutrientTick<settings.intervalTicks)
        return {};
    const auto ground=materializeEcsTerrain(*authority,cfg);
    const lib::nereid::WaterQueryIndex water(ecs);
    const auto waterAt=makeNereidMicroWaterSampler(ground,water);
    const auto* ecology=ecs.resourceConst<ForestEcologyResource>();
    const auto demand=prepareForestNutrientDemand(*authority,ecology);
    const auto soilAt=[&](int64_t x,int64_t z)->ForestMicroSoilSample{
        const int64_t cx=x*ForestDeadwoodResource::kNutrientCellCm+
            ForestDeadwoodResource::kNutrientCellCm/2;
        const int64_t cz=z*ForestDeadwoodResource::kNutrientCellCm+
            ForestDeadwoodResource::kNutrientCellCm/2;
        const ForestNutrientWeather c=weather?
            weather(cx,cz):ForestNutrientWeather{};
        const auto it=demand.find({x,z});
        return {c.rainfallPermille,c.soilMoisturePermille,
                it==demand.end()?0u:it->second};
    };
    const auto links=buildNereidMicroJunctions(
        ecs,waterAt,junctionSettings);
    if(links.size()>settings.maxJunctions)
        throw std::length_error("TerraWeave micro link step budget");
    ForestDeadwoodResource next=*previous;
    auto result=advanceForestMicroNutrients(
        next,completedTick,soilAt,waterAt,settings,links);
    if(!result.due)return result;
    World staged=ecs;
    staged.setResource<ForestDeadwoodResource>(std::move(next));
    ecs=std::move(staged);
    return result;
}

inline void addTerraWeaveForestMicroNutrientSystem(
        Simulation& sim,std::string name,TerraWeaveEcsAuthorityConfig cfg,
        ForestMicroSettings settings={},
        ForestNutrientWeatherSampler weather={},
        ForestNereidMicroJunctionSettings junctionSettings={},
        std::function<void(const ForestMicroOutcome&)> onChange={}) {
    validateEcsAuthorityConfig(cfg);
    validateForestMicroSettings(settings);
    validateForestNereidMicroJunctionSettings(junctionSettings);
    sim.addSystem(std::move(name),
        [cfg=std::move(cfg),settings,weather=std::move(weather),
         junctionSettings,onChange=std::move(onChange)]
        (Simulation& current) {
            const auto result=advanceForestMicroNutrientsEcs(
                current.world(),cfg,current.tick(),settings,weather,
                junctionSettings);
            if(onChange && (result.absorbed || result.leached ||
                            result.advected || result.junctionTransferred))
                onChange(result);
        });
}

} // namespace aiverse::terraweave
