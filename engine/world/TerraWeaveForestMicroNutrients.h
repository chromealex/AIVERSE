// TerraWeave Stage70 — authoritative sparse 4m NEREID-body nutrient grid.
// Pure deterministic fixed integer ledger; NO water/hydrology solver, no
// renderer/Box3D, and no ECS mutation inside the processing callbacks.
// The v2 32m untagged dissolved nutrient pool is never implicitly re-labeled.
#pragma once
#include "engine/world/TerraWeaveForestDeadwood.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <span>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace aiverse::terraweave {

struct ForestMicroKey {
    uint64_t bodyKey=0; // NEREID packed Entity + 1
    int64_t x=0,z=0;    // 4m signed lattice
    constexpr auto operator<=>(const ForestMicroKey&) const = default;
};
inline ForestMicroKey microKeyOf(const ForestMicroWaterCell& cell) {
    return {cell.bodyKey,cell.x,cell.z};
}
inline std::pair<int64_t,int64_t> microToCoarse(ForestMicroKey k) {
    return {detail::floorDiv(k.x,8),detail::floorDiv(k.z,8)};
}
inline void validateForestMicroKey(ForestMicroKey k) {
    const int64_t limit=kWorldLimitCm/ForestDeadwoodResource::kMicroWaterCellCm;
    if(!k.bodyKey || k.x<=-limit || k.x>=limit ||
       k.z<=-limit || k.z>=limit)
        throw std::invalid_argument("TerraWeave invalid micro water key");
}

struct ForestMicroSample {
    uint64_t bodyKey=0;
    bool submerged=false;
    int32_t currentXcmPerSec=0,currentZcmPerSec=0;
};
// When preferredBodyKey=0, the actual NEREID wet body at the point is
// returned. Otherwise it must be the requested generation-safe body;
// call-sites MUST check returned bodyKey to avoid cross-river transport.
using ForestMicroWaterSampler=std::function<ForestMicroSample(
    int64_t x4m,int64_t z4m,uint64_t preferredBodyKey)>;

struct ForestMicroSoilSample {
    uint32_t rainfallPermille=0,moisturePermille=700;
    uint32_t demandUnits=0;
};
using ForestMicroSoilSampler=std::function<ForestMicroSoilSample(
    int64_t coarseX,int64_t coarseZ)>;

struct ForestMicroJunction {
    uint64_t linkKey=0; // sorted, stable link ID from actual NEREID ECS
    ForestMicroKey source{},destination{};
    uint32_t maximumUnits=32;
};

struct ForestMicroSettings {
    uint32_t intervalTicks=256;
    uint32_t maxSoilPerStep=256;
    uint32_t maxWaterPerStep=256;
    uint32_t maxJunctions=64;
    uint32_t maxUptakePerSoil=32;
    uint32_t maxLeachPerSoil=64;
    uint32_t maxTransportPerWater=64;
    uint32_t minimumFlowCmPerSec=4;
};
inline void validateForestMicroSettings(ForestMicroSettings s) {
    if(!s.intervalTicks || s.intervalTicks>100000 ||
       !s.maxSoilPerStep || s.maxSoilPerStep>1024 ||
       !s.maxWaterPerStep || s.maxWaterPerStep>1024 ||
       !s.maxJunctions || s.maxJunctions>256 ||
       s.maxUptakePerSoil>100000 || s.maxLeachPerSoil>100000 ||
       s.maxTransportPerWater>100000 ||
       s.minimumFlowCmPerSec>10000)
        throw std::invalid_argument("TerraWeave invalid 4m nutrient budget");
}
inline void validateForestMicroSample(ForestMicroSample s) {
    if(s.currentXcmPerSec<-10000 || s.currentXcmPerSec>10000 ||
       s.currentZcmPerSec<-10000 || s.currentZcmPerSec>10000 ||
       (s.submerged && !s.bodyKey))
        throw std::invalid_argument("TerraWeave invalid NEREID 4m water sample");
}
inline void validateForestMicroJunctions(
        std::span<const ForestMicroJunction> links) {
    if(links.size()>256)
        throw std::length_error("TerraWeave 4m junction count");
    uint64_t previous=0;
    for(const auto& link:links) {
        validateForestMicroKey(link.source);
        validateForestMicroKey(link.destination);
        if(!link.linkKey || link.linkKey<=previous ||
           link.source==link.destination ||
           link.source.bodyKey==link.destination.bodyKey ||
           link.maximumUnits>100000)
            throw std::invalid_argument("TerraWeave invalid 4m junction");
        previous=link.linkKey;
    }
}

struct ForestMicroOutcome {
    bool due=false;
    uint32_t soilProcessed=0,waterProcessed=0,junctionsProcessed=0;
    uint64_t absorbed=0,leached=0,advected=0,junctionTransferred=0;
    uint64_t stateRevision=0;
};

// One bounded due tick. Old v2 coarse dissolved records remain frozen
// until an explicitly validated split is applied via claimLegacyWater.
// Stage68 coarse and Stage70 micro systems are mutually exclusive;
// they share nutrientInitialized, lastNutrientTick and soilCursor.
inline ForestMicroOutcome advanceForestMicroNutrients(
        ForestDeadwoodResource& resource,uint64_t completedTick,
        ForestMicroSoilSampler soilWeather,
        ForestMicroWaterSampler waterAt,
        ForestMicroSettings settings={},
        std::span<const ForestMicroJunction> links={}) {
    validateForestMicroSettings(settings);
    validateForestMicroJunctions(links);
    validateForestDeadwoodResource(resource);
    if(!soilWeather || !waterAt)
        throw std::invalid_argument("TerraWeave missing nutrient sampling callbacks");
    if(resource.initialized && completedTick<resource.lastStepTick)
        throw std::invalid_argument("TerraWeave nutrient before deadwood step");
    if(resource.nutrientInitialized &&
       completedTick<resource.lastNutrientTick)
        throw std::invalid_argument("TerraWeave reversed micro nutrient tick");
    if(resource.nutrientInitialized &&
       completedTick-resource.lastNutrientTick<settings.intervalTicks)
        return {};
    if(links.size()>settings.maxJunctions)
        throw std::length_error("TerraWeave micro junction step cap");

    ForestDeadwoodResource next=resource;
    next.nutrientInitialized=true;
    next.lastNutrientTick=completedTick;
    ForestMicroOutcome report;
    report.due=true;

    const auto checkedAdd=[](uint64_t& dest,uint64_t added) {
        if(dest>UINT64_MAX-added)
            throw std::overflow_error("TerraWeave 4m organic ledger overflow");
        dest+=added;
    };
    using Pool=std::map<ForestMicroKey,uint64_t>;
    Pool water;
    for(const auto& entry:next.microWater)
        water.emplace(microKeyOf(entry),entry.dissolvedUnits);
    const Pool original=water; // immutable pre-leaching flow/transfer snapshot
    Pool originalSpent;        // stable combined debit budget for all links

    // Plant demand comes only from explicit rooted plants and grass PATCHES,
    // computed by the caller, never from cosmetic instances.
    const size_t soilTotal=next.soil.size();
    const size_t soilStart=soilTotal?
        size_t(next.soilCursor%soilTotal):0;
    report.soilProcessed=uint32_t(std::min<size_t>(
        soilTotal,settings.maxSoilPerStep));
    for(uint32_t n=0;n<report.soilProcessed;++n){
        auto& patch=next.soil[(soilStart+n)%soilTotal];
        const ForestMicroSoilSample sample=
            soilWeather(patch.x,patch.z);
        if(sample.rainfallPermille>1000 ||
           sample.moisturePermille>1000 || sample.demandUnits>1000000)
            throw std::invalid_argument("TerraWeave invalid 4m soil weather");
        const uint64_t uptake=std::min({
            patch.availableUnits,uint64_t(sample.demandUnits),
            uint64_t(settings.maxUptakePerSoil)});
        patch.availableUnits-=uptake;
        checkedAdd(next.totalAbsorbedUnits,uptake);
        checkedAdd(report.absorbed,uptake);

        const uint32_t rain=uint32_t(
            (uint64_t(sample.rainfallPermille)*sample.moisturePermille+999)/1000);
        const uint64_t candidate=std::min(
            patch.availableUnits,
            uint64_t(settings.maxLeachPerSoil)*rain/1000);
        if(!candidate)continue;

        // Exactly 64 subcells in one 32m soil patch. Advance the search
        // start by a stable tick/cell hash to spread rain deposits, while
        // never inventing NEREID water where no wet sample exists.
        const uint64_t seed=detail::mix64(
            uint64_t(patch.x)^(
                uint64_t(patch.z)*0x9e3779b97f4a7c15ull)^completedTick);
        for(uint32_t j=0;j<64;++j){
            const uint32_t slot=uint32_t((seed+j)&63u);
            const int64_t fx=patch.x*8+int64_t(slot&7u);
            const int64_t fz=patch.z*8+int64_t(slot>>3);
            const auto wet=waterAt(fx,fz,0);
            validateForestMicroSample(wet);
            if(!wet.submerged)continue;
            const ForestMicroKey key{wet.bodyKey,fx,fz};
            validateForestMicroKey(key);
            checkedAdd(water[key],candidate);
            patch.availableUnits-=candidate;
            checkedAdd(next.totalLeachedUnits,candidate);
            checkedAdd(report.leached,candidate);
            break;
        }
    }
    next.soilCursor=soilTotal?
        (soilStart+report.soilProcessed)%soilTotal:0;
    next.soil.erase(std::remove_if(
        next.soil.begin(),next.soil.end(),
        [](const ForestDeadwoodSoilCell& cell) {
            return cell.availableUnits==0;
        }),next.soil.end());
    next.soilCursor=next.soil.empty()?0:
        next.soilCursor%next.soil.size();

    // One 4m cardinal step per due tick, restricted to current wet
    // NEREID body. A recipient gets its supply only NEXT tick.
    const size_t waterTotal=original.size();
    const size_t waterStart=waterTotal?
        size_t(next.microWaterCursor%waterTotal):0;
    report.waterProcessed=uint32_t(std::min<size_t>(
        waterTotal,settings.maxWaterPerStep));
    auto from=original.begin();
    if(waterTotal)std::advance(from,std::ptrdiff_t(waterStart));
    for(uint32_t i=0;i<report.waterProcessed;++i) {
        if(from==original.end())from=original.begin();
        const auto [key,initial]=*from++;
        const auto velocity=waterAt(key.x,key.z,key.bodyKey);
        validateForestMicroSample(velocity);
        if(!velocity.submerged || velocity.bodyKey!=key.bodyKey)
            continue;
        const int64_t ax=velocity.currentXcmPerSec;
        const int64_t az=velocity.currentZcmPerSec;
        const int64_t speed=std::max(ax<0?-ax:ax,az<0?-az:az);
        if(speed<int64_t(settings.minimumFlowCmPerSec))continue;
        const ForestMicroKey dst{
            key.bodyKey,
            key.x+((ax<0?-ax:ax)>=(az<0?-az:az)?(ax>=0?1:-1):0),
            key.z+((ax<0?-ax:ax)<(az<0?-az:az)?(az>=0?1:-1):0)};
        const int64_t limit=kWorldLimitCm/
            ForestDeadwoodResource::kMicroWaterCellCm;
        if(dst.x<=-limit || dst.x>=limit ||
           dst.z<=-limit || dst.z>=limit)continue;
        const auto target=waterAt(dst.x,dst.z,dst.bodyKey);
        validateForestMicroSample(target);
        if(!target.submerged || target.bodyKey!=dst.bodyKey)continue;
        const uint64_t cap=std::min<uint64_t>(
            initial,settings.maxTransportPerWater);
        const uint64_t transfer=cap*std::min<int64_t>(speed,100)/100;
        if(!transfer)continue;
        if(water[key]<transfer)
            throw std::logic_error("TerraWeave micro original water overspent");
        checkedAdd(water[dst],transfer);
        water[key]-=transfer;
        checkedAdd(originalSpent[key],transfer);
        checkedAdd(report.advected,transfer);
    }
    next.microWaterCursor=waterTotal?
        (waterStart+report.waterProcessed)%waterTotal:0;

    // Explicit true NEREID confluence/cascade edges. Link discovery belongs
    // to the ECS adapter. The pure kernel re-verifies the exact current
    // body identities and submerged states before any cross-body debit.
    for(const auto& edge:links){
        ++report.junctionsProcessed;
        const auto upstream=waterAt(
            edge.source.x,edge.source.z,edge.source.bodyKey);
        const auto downstream=waterAt(
            edge.destination.x,edge.destination.z,
            edge.destination.bodyKey);
        validateForestMicroSample(upstream);
        validateForestMicroSample(downstream);
        if(!upstream.submerged || !downstream.submerged ||
           upstream.bodyKey!=edge.source.bodyKey ||
           downstream.bodyKey!=edge.destination.bodyKey)continue;
        const auto old=original.find(edge.source);
        if(old==original.end())continue;
        const uint64_t spent=originalSpent[edge.source];
        if(spent>old->second)
            throw std::logic_error("TerraWeave micro link original overspent");
        const uint64_t transfer=std::min({
            old->second-spent,water[edge.source],
            uint64_t(edge.maximumUnits)});
        if(!transfer)continue;
        checkedAdd(water[edge.destination],transfer);
        water[edge.source]-=transfer;
        checkedAdd(originalSpent[edge.source],transfer);
        checkedAdd(report.junctionTransferred,transfer);
    }
    next.microWater.clear();
    next.microWater.reserve(water.size());
    for(const auto& [key,amount]:water)
        if(amount)next.microWater.push_back({
            key.bodyKey,key.x,key.z,amount});
    if(next.microWater.size()>ForestDeadwoodResource::kMaxMicroWaterCells)
        throw std::length_error("TerraWeave sparse 4m water cell budget");
    if(report.absorbed || report.leached || report.advected ||
       report.junctionTransferred){
        if(next.revision==UINT64_MAX)
            throw std::overflow_error("TerraWeave micro ledger revision");
        ++next.revision;
    }
    validateForestDeadwoodResource(next);
    report.stateRevision=next.revision;
    resource=std::move(next);
    return report;
}

// Explicit, reversible full-cell legacy migration. The OLD 32m v2 water
// record has no body ID, so it must NOT be assigned using a guessed
// dominant river. A game/level migration author chooses a complete set
// of valid 4m body-tagged allocations. This function checks conservation
// and spatial membership; the caller must also verify actual NEREID wet
// body IDs for every allocation at the authoritative ECS barrier.
struct ForestLegacyMicroAllocation {
    ForestMicroKey key{};
    uint64_t units=0;
};
struct ForestLegacyMicroClaim {
    int64_t coarseX=0,coarseZ=0;
    std::vector<ForestLegacyMicroAllocation> allocations;
};
inline uint64_t claimForestLegacyWater(
        ForestDeadwoodResource& resource,
        std::span<const ForestLegacyMicroClaim> claims,
        const std::function<bool(ForestMicroKey)>& isVerifiedWetBody) {
    validateForestDeadwoodResource(resource);
    if(!isVerifiedWetBody || claims.size()>256)
        throw std::invalid_argument("TerraWeave invalid legacy split plan");
    if(claims.empty())return 0;
    using OldKey=std::pair<int64_t,int64_t>;
    std::map<OldKey,uint64_t> coarse;
    for(const auto& cell:resource.water)
        coarse.emplace(OldKey{cell.x,cell.z},cell.dissolvedUnits);
    std::map<ForestMicroKey,uint64_t> micro;
    for(const auto& cell:resource.microWater)
        micro.emplace(microKeyOf(cell),cell.dissolvedUnits);
    std::set<OldKey> seen;
    uint64_t totalClaimed=0;
    const auto checkedAdd=[](uint64_t& a,uint64_t b) {
        if(a>UINT64_MAX-b)
            throw std::overflow_error("TerraWeave legacy claim units");
        a+=b;
    };
    for(const auto& claim:claims){
        const OldKey old{claim.coarseX,claim.coarseZ};
        if(!seen.insert(old).second || claim.allocations.empty() ||
           claim.allocations.size()>64)
            throw std::invalid_argument("TerraWeave duplicate/empty legacy claim");
        const auto prior=coarse.find(old);
        if(prior==coarse.end())
            throw std::invalid_argument("TerraWeave unknown legacy 32m water cell");
        uint64_t subtotal=0;
        std::set<ForestMicroKey> local;
        for(const auto& allocation:claim.allocations){
            validateForestMicroKey(allocation.key);
            if(!allocation.units || !local.insert(allocation.key).second ||
               microToCoarse(allocation.key)!=old ||
               !isVerifiedWetBody(allocation.key))
                throw std::invalid_argument(
                    "TerraWeave invalid/unverified 4m legacy allocation");
            checkedAdd(subtotal,allocation.units);
        }
        if(subtotal!=prior->second)
            throw std::invalid_argument(
                "TerraWeave legacy split must conserve entire coarse cell");
        for(const auto& allocation:claim.allocations)
            checkedAdd(micro[allocation.key],allocation.units);
        checkedAdd(totalClaimed,subtotal);
        coarse.erase(prior);
    }
    if(resource.revision==UINT64_MAX)
        throw std::overflow_error("TerraWeave legacy split revision");
    ForestDeadwoodResource next=resource;
    next.water.clear();
    for(const auto& [key,units]:coarse)
        next.water.push_back({key.first,key.second,units});
    next.microWater.clear();
    next.microWater.reserve(micro.size());
    for(const auto& [key,units]:micro)
        if(units)next.microWater.push_back({
            key.bodyKey,key.x,key.z,units});
    if(next.microWater.size()>ForestDeadwoodResource::kMaxMicroWaterCells)
        throw std::length_error("TerraWeave legacy 4m allocation budget");
    ++next.revision;
    validateForestDeadwoodResource(next);
    resource=std::move(next);
    return totalClaimed;
}

// An authored NEREID river outlet may remove dissolved nutrients from a
// SINGLE body-tagged 4m cell. Same monotonic replay command stream as
// Stage68 coarse export; a zero-volume command is still consumed.
inline uint64_t exportForestMicroDissolvedNutrients(
        ForestDeadwoodResource& resource,ForestMicroKey key,
        uint64_t maximumUnits,uint64_t commandId) {
    validateForestMicroKey(key);
    validateForestDeadwoodResource(resource);
    if(!commandId)
        throw std::invalid_argument("TerraWeave 4m outlet command ID");
    if(commandId<=resource.lastOutflowCommandId)return 0;
    ForestDeadwoodResource next=resource;
    auto pos=std::lower_bound(next.microWater.begin(),
        next.microWater.end(),key,
        [](const ForestMicroWaterCell& cell,ForestMicroKey rhs){
            return microKeyOf(cell)<rhs;
        });
    uint64_t exported=0;
    if(pos!=next.microWater.end() && microKeyOf(*pos)==key){
        exported=std::min(pos->dissolvedUnits,maximumUnits);
        if(next.totalRunoffExportUnits>UINT64_MAX-exported)
            throw std::overflow_error("TerraWeave 4m outlet mass overflow");
        pos->dissolvedUnits-=exported;
        if(!pos->dissolvedUnits)next.microWater.erase(pos);
        next.totalRunoffExportUnits+=exported;
    }
    if(next.revision==UINT64_MAX)
        throw std::overflow_error("TerraWeave 4m outlet revision overflow");
    next.lastOutflowCommandId=commandId;
    ++next.revision;
    validateForestDeadwoodResource(next);
    resource=std::move(next);
    return exported;
}

} // namespace aiverse::terraweave
