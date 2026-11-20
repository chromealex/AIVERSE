// TerraWeave Stage68: deterministic bounded nutrient uptake, rain leaching
// and water-current-driven dissolved transport. This is a pure gameplay policy:
// PHYSIS supplies weather samples; NEREID supplies water ownership and flow.
// TerraWeave does NOT solve water, change Earthworks ground or allocate
// per-blade/per-foliage ECS entities.
#pragma once
#include "engine/world/TerraWeaveForestDeadwood.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::terraweave {

struct ForestNutrientCellClimate {
    uint32_t rainfallPermille=0;       // from PHYSIS, 0..1000
    uint32_t soilMoisturePermille=700;  // from PHYSIS, 0..1000
    uint32_t submergedPermille=0;      // from NEREID, 0..1000
    int32_t currentXcmPerSec=0;        // authoritative NEREID current
    int32_t currentZcmPerSec=0;
    uint64_t waterBodyKey=0;           // 0 = no active NEREID water
    uint32_t demandUnits=0;           // rooted shrubs/trees + grass patches
};
using ForestNutrientCellSampler=std::function<
    ForestNutrientCellClimate(int64_t cellX,int64_t cellZ)>;

struct ForestNutrientCycleSettings {
    uint32_t intervalTicks=256;
    uint32_t maxSoilCellsPerStep=256;  // bounded O(N) map setup once/due tick
    uint32_t maxWaterCellsPerStep=256;
    uint32_t maxUptakeUnitsPerCell=32;
    uint32_t maxLeachUnitsPerCell=64;
    uint32_t maxAdvectionUnitsPerCell=128;
    uint32_t minFlowCmPerSec=4;
    uint32_t waterSamplesPerAxis=8;       // 4m probes in 32m water cell
    uint32_t maxJunctionTransfersPerStep=64; // NEREID-authored edges only
};
inline void validateForestNutrientCycleSettings(
        const ForestNutrientCycleSettings& s) {
    if(!s.intervalTicks || s.intervalTicks>100000 ||
       !s.maxSoilCellsPerStep || s.maxSoilCellsPerStep>1024 ||
       !s.maxWaterCellsPerStep || s.maxWaterCellsPerStep>1024 ||
       s.maxUptakeUnitsPerCell>100000 ||
       s.maxLeachUnitsPerCell>100000 ||
       s.maxAdvectionUnitsPerCell>100000 ||
       s.minFlowCmPerSec>10000 ||
       (s.waterSamplesPerAxis!=1 && s.waterSamplesPerAxis!=2 &&
        s.waterSamplesPerAxis!=4 && s.waterSamplesPerAxis!=8) ||
       !s.maxJunctionTransfersPerStep ||
       s.maxJunctionTransfersPerStep>256)
        throw std::invalid_argument("TerraWeave invalid nutrient cycle settings");
}
inline void validateForestNutrientCellClimate(
        const ForestNutrientCellClimate& c) {
    if(c.rainfallPermille>1000 ||
       c.soilMoisturePermille>1000 || c.submergedPermille>1000 ||
       c.currentXcmPerSec<-10000 || c.currentXcmPerSec>10000 ||
       c.currentZcmPerSec<-10000 || c.currentZcmPerSec>10000 ||
       c.demandUnits>1000000)
        throw std::invalid_argument("TerraWeave invalid nutrient weather/water");
}

// One directed, verified topological link between distinct NEREID bodies.
// Producer MUST validate its water-body ECS handles and wetted geometry at
// the same completed ECS read barrier. 32m cells cannot disambiguate two
// different water bodies IN THE SAME CELL; such links are omitted.
struct ForestNutrientJunctionLink {
    uint64_t linkKey=0; // unique, stable ECS junction/cascade ID + inlet slot
    int64_t sourceX=0,sourceZ=0,targetX=0,targetZ=0;
    uint64_t sourceBodyKey=0,targetBodyKey=0; // (packed Entity + 1)
    uint32_t maxTransferUnits=32;
};

struct ForestNutrientCycleOutcome {
    size_t soilCellsProcessed=0,waterCellsProcessed=0;
    uint64_t absorbedUnits=0,leachedUnits=0,advectedUnits=0;
    uint64_t junctionTransferredUnits=0;
    uint32_t junctionLinksProcessed=0;
    uint64_t revision=0;
};

inline ForestNutrientCycleOutcome advanceForestNutrientCycle(
        ForestDeadwoodResource& resource,uint64_t completedTick,
        ForestNutrientCellSampler climate,
        ForestNutrientCycleSettings settings={},
        std::span<const ForestNutrientJunctionLink> junctions={}) {
    validateForestNutrientCycleSettings(settings);
    if(!climate)
        throw std::invalid_argument("TerraWeave nutrient cycle needs climate/water");
    validateForestDeadwoodResource(resource);
    if(resource.initialized && completedTick<resource.lastStepTick)
        throw std::invalid_argument(
            "TerraWeave nutrient tick precedes saved decay");
    if(resource.nutrientInitialized &&
       completedTick<resource.lastNutrientTick)
        throw std::invalid_argument("TerraWeave reversed nutrient tick");
    if(resource.nutrientInitialized &&
       completedTick-resource.lastNutrientTick<settings.intervalTicks)
        return {};

    ForestDeadwoodResource next=resource;
    next.nutrientInitialized=true;
    next.lastNutrientTick=completedTick;
    ForestNutrientCycleOutcome result;

    using Cell=std::pair<int64_t,int64_t>;
    std::map<Cell,uint64_t> water;
    for(const auto& old:next.water)
        water.emplace(Cell{old.x,old.z},old.dissolvedUnits);
    // Snapshot BEFORE leaching: runoff released this tick cannot traverse
    // multiple grid cells in the same tick or depend on iteration order.
    const auto previousWater=next.water;
    if(junctions.size()>256)
        throw std::length_error("TerraWeave nutrient junction plan budget");
    uint64_t lastLink=0;
    for(const auto& link:junctions) {
        const int64_t limit=kWorldLimitCm/
            ForestDeadwoodResource::kNutrientCellCm;
        if(!link.linkKey || link.linkKey<=lastLink ||
           !link.sourceBodyKey || !link.targetBodyKey ||
           link.sourceBodyKey==link.targetBodyKey ||
           (link.sourceX==link.targetX && link.sourceZ==link.targetZ) ||
           link.maxTransferUnits>100000 ||
           link.sourceX<=-limit || link.sourceX>=limit ||
           link.sourceZ<=-limit || link.sourceZ>=limit ||
           link.targetX<=-limit || link.targetX>=limit ||
           link.targetZ<=-limit || link.targetZ>=limit)
            throw std::invalid_argument("TerraWeave invalid junction plan");
        lastLink=link.linkKey;
    }

    const size_t soilCount=next.soil.size();
    const size_t soilStart=soilCount?size_t(next.soilCursor%soilCount):0;
    result.soilCellsProcessed=
        std::min<size_t>(soilCount,settings.maxSoilCellsPerStep);
    const auto checkedAdd=[](uint64_t& target,uint64_t added) {
        if(target>UINT64_MAX-added)
            throw std::overflow_error("TerraWeave nutrient ledger overflow");
        target+=added;
    };

    for(size_t i=0;i<result.soilCellsProcessed;++i) {
        auto& cell=next.soil[(soilStart+i)%soilCount];
        const ForestNutrientCellClimate c=climate(cell.x,cell.z);
        validateForestNutrientCellClimate(c);
        // Only plants actually represented in the opted-in rooted/grass
        // subset can claim the bounded uptake; neither GI nor canopy
        // presentation creates extra organic matter.
        const uint64_t uptake=std::min({
            cell.availableUnits,
            uint64_t(settings.maxUptakeUnitsPerCell),
            uint64_t(c.demandUnits)});
        checkedAdd(next.totalAbsorbedUnits,uptake);
        checkedAdd(result.absorbedUnits,uptake);
        cell.availableUnits-=uptake;

        // Rainfall and actual wetness make mineral nutrients mobile.
        // Drought suppresses runoff; submerged soil is maximally wet.
        // No second wave/current generation is performed here.
        const uint32_t moisture=std::max(
            c.soilMoisturePermille,c.submergedPermille);
        const uint32_t exposure=uint32_t(
            (uint64_t(c.rainfallPermille)*moisture+999)/1000);
        const uint64_t leach=std::min(
            cell.availableUnits,
            uint64_t(settings.maxLeachUnitsPerCell)*exposure/1000);
        if(!leach)continue;
        auto& dissolved=water[{cell.x,cell.z}];
        checkedAdd(dissolved,leach);
        checkedAdd(next.totalLeachedUnits,leach); // transfer diagnostic
        checkedAdd(result.leachedUnits,leach);
        cell.availableUnits-=leach;
    }
    next.soilCursor=soilCount?
        (soilStart+result.soilCellsProcessed)%soilCount:0;
    // Once depleted, keep no empty permanent soil cell. Released organic
    // matter can re-create the same stable grid cell later.
    next.soil.erase(std::remove_if(next.soil.begin(),next.soil.end(),
        [](const ForestDeadwoodSoilCell& cell){
            return cell.availableUnits==0;
        }),next.soil.end());
    next.soilCursor=next.soil.empty()?0:
        next.soilCursor%next.soil.size();

    std::map<Cell,uint64_t> originalByCell;
    for(const auto& cell:previousWater)
        originalByCell.emplace(Cell{cell.x,cell.z},cell.dissolvedUnits);
    std::map<Cell,uint64_t> debitedFromOriginal;
    const size_t waterCount=previousWater.size();
    const size_t waterStart=waterCount?size_t(next.waterCursor%waterCount):0;
    result.waterCellsProcessed=
        std::min<size_t>(waterCount,settings.maxWaterCellsPerStep);
    for(size_t i=0;i<result.waterCellsProcessed;++i){
        const auto& source=previousWater[(waterStart+i)%waterCount];
        const auto sampled=climate(source.x,source.z);
        validateForestNutrientCellClimate(sampled);
        if(!sampled.waterBodyKey || !sampled.submergedPermille ||
           !source.dissolvedUnits)
            continue;
        const int64_t fx=sampled.currentXcmPerSec;
        const int64_t fz=sampled.currentZcmPerSec;
        const int64_t ax=fx<0?-fx:fx;
        const int64_t az=fz<0?-fz:fz;
        const int64_t speed=std::max(ax,az);
        if(speed<int64_t(settings.minFlowCmPerSec))continue;
        const bool xPrimary=ax>=az;
        const int64_t sx=xPrimary?(fx>0?1:-1):0;
        const int64_t sz=xPrimary?0:(fz>0?1:-1);
        const Cell dest{source.x+sx,source.z+sz};
        const int64_t limit=kWorldLimitCm/
            ForestDeadwoodResource::kNutrientCellCm;
        if(dest.first<=-limit || dest.first>=limit ||
           dest.second<=-limit || dest.second>=limit)
            continue; // hold until an explicit game-authored outlet
        const auto target=climate(dest.first,dest.second);
        validateForestNutrientCellClimate(target);
        // Never spontaneously move dissolved nutrients across distinct
        // NEREID water bodies or through an absent/dry destination.
        if(!target.submergedPermille ||
           target.waterBodyKey!=sampled.waterBodyKey)continue;
        // Scale only the capped per-step transfer, not potentially huge
        // historical reservoir totals (avoid uint64 overflow).
        const uint64_t capped=std::min<uint64_t>(
            source.dissolvedUnits,settings.maxAdvectionUnitsPerCell);
        const uint64_t move=capped*
            std::min<uint64_t>(uint64_t(speed),100)/100;
        if(!move)continue;
        auto& src=water[{source.x,source.z}];
        if(src<move)
            throw std::logic_error("TerraWeave nutrient snapshot mismatch");
        auto& dst=water[dest];
        checkedAdd(dst,move);
        src-=move;
        checkedAdd(debitedFromOriginal[{source.x,source.z}],move);
        checkedAdd(result.advectedUnits,move);
    }
    next.waterCursor=waterCount?
        (waterStart+result.waterCellsProcessed)%waterCount:0;

    // NEREID explicitly directed confluences/cascades: use ONLY original
    // prior-tick dissolved mass not already spent by cardinal advection.
    // All links read from this immutable snapshot and debit from the
    // same per-cell budget; an upstream->junction->downstream chain cannot
    // forward the same units twice during one simulation tick.
    const size_t linksToRun=std::min<size_t>(
        junctions.size(),settings.maxJunctionTransfersPerStep);
    for(size_t i=0;i<linksToRun;++i) {
        const auto& link=junctions[i];
        ++result.junctionLinksProcessed;
        const Cell source{link.sourceX,link.sourceZ};
        const Cell target{link.targetX,link.targetZ};
        const auto available=originalByCell.find(source);
        if(available==originalByCell.end())continue;
        const uint64_t spent=debitedFromOriginal[source];
        if(spent>available->second)
            throw std::logic_error("TerraWeave original nutrient overspent");
        const uint64_t originalRemaining=available->second-spent;
        const uint64_t moved=std::min({
            originalRemaining,water[source],
            uint64_t(link.maxTransferUnits)});
        if(!moved)continue;
        checkedAdd(water[target],moved);
        water[source]-=moved;
        checkedAdd(debitedFromOriginal[source],moved);
        checkedAdd(result.junctionTransferredUnits,moved);
        checkedAdd(result.advectedUnits,moved);
    }

    next.water.clear();
    next.water.reserve(water.size());
    for(const auto& [key,amount]:water){
        if(amount)
            next.water.push_back({key.first,key.second,amount});
    }
    if(result.absorbedUnits || result.leachedUnits ||
       result.advectedUnits){
        if(next.revision==UINT64_MAX)
            throw std::overflow_error("TerraWeave nutrient revision overflow");
        ++next.revision;
    }
    validateForestDeadwoodResource(next); // including exact full ledger
    result.revision=next.revision;
    resource=std::move(next); // all-or-none
    return result;
}

// A game-authored outflow (river exits simulation region) can export
// dissolved nutrient units; boundary water cannot do so implicitly.
// Calling this from a canonical command/event barrier is the host's
// responsibility. The resulting explicit sink is persisted and counted.
inline uint64_t exportForestDissolvedNutrients(
        ForestDeadwoodResource& resource,int64_t x,int64_t z,
        uint64_t maximumUnits,uint64_t commandId) {
    validateForestDeadwoodResource(resource);
    if(!commandId)
        throw std::invalid_argument(
            "TerraWeave nutrient outflow needs nonzero command sequence");
    // Monotonic native sequence suppresses duplicated/out-of-order
    // events across network delivery and rollback replay. A zero-result
    // command is also recorded so it cannot export matter later.
    if(commandId<=resource.lastOutflowCommandId)return 0;
    using Cell=std::pair<int64_t,int64_t>;
    ForestDeadwoodResource copy=resource;
    const auto it=std::lower_bound(
        copy.water.begin(),copy.water.end(),Cell{x,z},
        [](const ForestDeadwoodWaterCell& cell,const Cell& key){
            return Cell{cell.x,cell.z}<key;
        });
    uint64_t exported=0;
    if(it!=copy.water.end() && Cell{it->x,it->z}==Cell{x,z}){
        exported=std::min(it->dissolvedUnits,maximumUnits);
        if(copy.totalRunoffExportUnits>UINT64_MAX-exported)
            throw std::overflow_error("TerraWeave runoff export overflow");
        it->dissolvedUnits-=exported;
        if(!it->dissolvedUnits)copy.water.erase(it);
        copy.totalRunoffExportUnits+=exported;
    }
    if(copy.revision==UINT64_MAX)
        throw std::overflow_error("TerraWeave runoff command revision overflow");
    copy.lastOutflowCommandId=commandId;
    ++copy.revision;
    validateForestDeadwoodResource(copy);
    resource=std::move(copy);
    return exported;
}

} // namespace aiverse::terraweave
