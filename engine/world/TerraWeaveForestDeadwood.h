// TerraWeave Stage67 — bounded deterministic deadwood and forest nutrient pool.
// Gameplay authority uses integer units; Box3D, soil geometry and renderer
// are external consumers and MUST NOT be called by this pure data kernel.
// A unit is an abstract organic-matter accounting unit, not kg or joules.
#pragma once
#include "engine/world/TerraWeaveForestEcology.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <compare>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <tuple>
#include <vector>

namespace aiverse::terraweave {

struct ForestDeadwoodRecord {
    AnchoredPlantId id{};
    int64_t xCm=0,zCm=0;
    uint64_t createdTick=0,lastEvaluatedTick=0;
    uint64_t remainingUnits=0; // zero is a persistent spent identity
    uint64_t initialUnits=0;   // Stage68 visual ratio (not a second ledger)
    uint32_t rateUnitsPer1000Ticks=40; // constant loss in full tick time
    uint32_t nutrientsPermille=300; // rest escapes to the non-soil sink
    uint16_t decayCarry=0,nutrientCarry=0; // integer fractional remainders
    bool fromFall=false;
};

struct ForestDeadwoodSoilCell {
    int64_t x=0,z=0; // 32m indexed world cell, NOT an Earthworks height vertex
    uint64_t availableUnits=0;
};

struct ForestDeadwoodWaterCell {
    int64_t x=0,z=0; // legacy 32m dissolved grid; unbound to NEREID body
    uint64_t dissolvedUnits=0;
};

// Stage70: persistent 4m water grid, with generation-safe NEREID body key.
// Unlike the old 32m untagged pool, overlapping rivers can own independent
// dissolved nutrient amounts at the SAME X/Z position.
struct ForestMicroWaterCell {
    uint64_t bodyKey=0; // NEREID Entity::packed()+1; 0 is invalid
    int64_t x=0,z=0;    // 4m world lattice indices
    uint64_t dissolvedUnits=0;
    constexpr auto operator<=>(const ForestMicroWaterCell&) const = default;
};

struct ForestDeadwoodResource {
    static constexpr size_t kMaxLogs=65536;
    static constexpr size_t kMaxSoilCells=65536;
    static constexpr size_t kMaxWaterCells=65536;
    static constexpr size_t kMaxMicroWaterCells=65536;
    static constexpr int64_t kNutrientCellCm=3200;
    static constexpr int64_t kMicroWaterCellCm=400;
    uint32_t version=3;
    uint64_t revision=0;
    uint64_t cursor=0;
    uint64_t lastStepTick=0;
    bool initialized=false;
    uint64_t totalInitialUnits=0,totalRespiredUnits=0,
             totalExportedUnits=0;
    // Stage68: v2 tracked sinks. Do not count transfer into the
    // dissolved water reservoir as exported; it remains world-owned.
    uint64_t totalAbsorbedUnits=0,totalRunoffExportUnits=0;
    uint64_t totalLeachedUnits=0; // cumulative diagnostic, NOT a sink
    uint64_t lastNutrientTick=0,soilCursor=0,waterCursor=0;
    uint64_t lastOutflowCommandId=0; // monotonic native outlet command
    bool nutrientInitialized=false;
    uint64_t microWaterCursor=0; // persistent bounded 4m water scheduler
    std::vector<ForestDeadwoodRecord> logs; // strictly by species/key
    std::vector<ForestDeadwoodSoilCell> soil; // strictly by x,z
    std::vector<ForestDeadwoodWaterCell> water; // legacy v2 coarse, untouched in micro mode
    std::vector<ForestMicroWaterCell> microWater; // sorted bodyKey,x,z
};

inline void validateForestDeadwoodResource(
        const ForestDeadwoodResource& state) {
    if(state.version!=3 || state.logs.size()>
       ForestDeadwoodResource::kMaxLogs ||
       state.soil.size()>ForestDeadwoodResource::kMaxSoilCells ||
       state.water.size()>ForestDeadwoodResource::kMaxWaterCells ||
       state.microWater.size()>ForestDeadwoodResource::kMaxMicroWaterCells)
        throw std::invalid_argument("TerraWeave invalid deadwood resource");
    AnchoredPlantId prevId{};
    bool first=true;
    for(const auto& log:state.logs) {
        if(!log.id.speciesId || !log.id.key ||
           (!first && !(prevId<log.id)) ||
           log.xCm<-kWorldLimitCm || log.xCm>kWorldLimitCm ||
           log.zCm<-kWorldLimitCm || log.zCm>kWorldLimitCm ||
           log.createdTick>log.lastEvaluatedTick ||
           log.initialUnits<log.remainingUnits ||
           log.rateUnitsPer1000Ticks>1000000 ||
           log.nutrientsPermille>1000 ||
           log.decayCarry>=1000 || log.nutrientCarry>=1000)
            throw std::invalid_argument("TerraWeave invalid deadwood log");
        prevId=log.id;first=false;
    }
    std::pair<int64_t,int64_t> prevCell{};
    first=true;
    for(const auto& cell:state.soil){
        const auto key=std::pair{cell.x,cell.z};
        if((!first && !(prevCell<key)) ||
           cell.x<-kWorldLimitCm/ForestDeadwoodResource::kNutrientCellCm-1 ||
           cell.x> kWorldLimitCm/ForestDeadwoodResource::kNutrientCellCm+1 ||
           cell.z<-kWorldLimitCm/ForestDeadwoodResource::kNutrientCellCm-1 ||
           cell.z> kWorldLimitCm/ForestDeadwoodResource::kNutrientCellCm+1)
            throw std::invalid_argument("TerraWeave invalid nutrient cell");
        prevCell=key;first=false;
    }
    prevCell={}; first=true;
    for(const auto& cell:state.water) {
        const auto key=std::pair{cell.x,cell.z};
        if((!first && !(prevCell<key)) || !cell.dissolvedUnits ||
           cell.x<-kWorldLimitCm/ForestDeadwoodResource::kNutrientCellCm-1 ||
           cell.x> kWorldLimitCm/ForestDeadwoodResource::kNutrientCellCm+1 ||
           cell.z<-kWorldLimitCm/ForestDeadwoodResource::kNutrientCellCm-1 ||
           cell.z> kWorldLimitCm/ForestDeadwoodResource::kNutrientCellCm+1)
            throw std::invalid_argument("TerraWeave invalid dissolved nutrient cell");
        prevCell=key;first=false;
    }
    uint64_t previousBody=0;
    std::pair<int64_t,int64_t> previousMicro{};
    first=true;
    const int64_t microLimit=
        kWorldLimitCm/ForestDeadwoodResource::kMicroWaterCellCm;
    for(const auto& cell:state.microWater){
        if(!cell.bodyKey || !cell.dissolvedUnits ||
           cell.x<=-microLimit || cell.x>=microLimit ||
           cell.z<=-microLimit || cell.z>=microLimit ||
           (!first && std::tuple{previousBody,previousMicro.first,
                                  previousMicro.second}>=
                      std::tuple{cell.bodyKey,cell.x,cell.z}))
            throw std::invalid_argument("TerraWeave invalid 4m nutrient cell");
        first=false;previousBody=cell.bodyKey;
        previousMicro={cell.x,cell.z};
    }
    // Reject impossible save/replay states BEFORE any resource publication:
    // no organic matter may silently appear or disappear in the ledger.
    uint64_t accounted=state.totalRespiredUnits;
    const auto add=[&accounted](uint64_t units) {
        if(accounted>UINT64_MAX-units)
            throw std::invalid_argument("TerraWeave deadwood ledger overflow");
        accounted+=units;
    };
    add(state.totalExportedUnits);
    add(state.totalAbsorbedUnits);
    add(state.totalRunoffExportUnits);
    for(const auto& log:state.logs)add(log.remainingUnits);
    for(const auto& cell:state.soil)add(cell.availableUnits);
    for(const auto& cell:state.water)add(cell.dissolvedUnits);
    for(const auto& cell:state.microWater)add(cell.dissolvedUnits);
    if(accounted!=state.totalInitialUnits)
        throw std::invalid_argument("TerraWeave deadwood mass not conserved");
}

inline void io(Archive& a,ForestDeadwoodRecord& r) {
    a(r.id.speciesId,r.id.key,r.xCm,r.zCm,
      r.createdTick,r.lastEvaluatedTick,r.remainingUnits,
      r.rateUnitsPer1000Ticks,r.nutrientsPermille,
      r.decayCarry,r.nutrientCarry,r.fromFall,r.initialUnits);
}
// Explicit version-1 decoder. The old record had NO initial mass; use
// surviving units as a conservative presentation baseline only.
inline void readForestDeadwoodRecordV1(Archive& a,
                                      ForestDeadwoodRecord& r) {
    a(r.id.speciesId,r.id.key,r.xCm,r.zCm,
      r.createdTick,r.lastEvaluatedTick,r.remainingUnits,
      r.rateUnitsPer1000Ticks,r.nutrientsPermille,
      r.decayCarry,r.nutrientCarry,r.fromFall);
    if(!a.writing())r.initialUnits=r.remainingUnits;
}
inline void io(Archive& a,ForestDeadwoodSoilCell& r) {
    a(r.x,r.z,r.availableUnits);
}
inline void io(Archive& a,ForestDeadwoodWaterCell& r) {
    a(r.x,r.z,r.dissolvedUnits);
}
inline void io(Archive& a,ForestMicroWaterCell& r) {
    a(r.bodyKey,r.x,r.z,r.dissolvedUnits);
}
inline void io(Archive& a,ForestDeadwoodResource& s) {
    a(s.version);
    if(!a.ok() || (s.version!=1u && s.version!=2u &&
                   s.version!=3u)){a.fail();return;}
    const bool v2=s.version>=2u;
    const bool v3=s.version==3u;
    a(s.revision,s.cursor,s.lastStepTick,s.initialized,
      s.totalInitialUnits,s.totalRespiredUnits,s.totalExportedUnits);
    if(v2)
        a(s.totalAbsorbedUnits,s.totalRunoffExportUnits,
          s.totalLeachedUnits,s.lastNutrientTick,s.soilCursor,
          s.waterCursor,s.lastOutflowCommandId,s.nutrientInitialized);
    uint32_t n=a.count(s.logs.size(),v2?73:65);
    if(!a.ok() || n>ForestDeadwoodResource::kMaxLogs){a.fail();return;}
    if(!a.writing())s.logs.assign(n,{});
    for(auto& v:s.logs)if(a.ok()) {
        if(v2)a(v);
        else readForestDeadwoodRecordV1(a,v);
    }
    n=a.count(s.soil.size(),24);
    if(!a.ok() || n>ForestDeadwoodResource::kMaxSoilCells){a.fail();return;}
    if(!a.writing())s.soil.assign(n,{});
    for(auto& v:s.soil)if(a.ok())a(v);
    if(v2){
        n=a.count(s.water.size(),24);
        if(!a.ok() || n>ForestDeadwoodResource::kMaxWaterCells){a.fail();return;}
        if(!a.writing())s.water.assign(n,{});
        for(auto& v:s.water)if(a.ok())a(v);
    }else if(!a.writing()){
        // Legacy v1 had neither dissolved reservoir nor uptake sinks.
        s.totalAbsorbedUnits=s.totalRunoffExportUnits=s.totalLeachedUnits=0;
        s.lastNutrientTick=s.soilCursor=s.waterCursor=0;
        s.lastOutflowCommandId=0;
        s.nutrientInitialized=false;
        s.water.clear();
    }
    if(v3){
        a(s.microWaterCursor);
        n=a.count(s.microWater.size(),32);
        if(!a.ok() || n>ForestDeadwoodResource::kMaxMicroWaterCells){
            a.fail();return;
        }
        if(!a.writing())s.microWater.assign(n,{});
        for(auto& v:s.microWater)if(a.ok())a(v);
    }else if(!a.writing()){
        // Preserve every 32m legacy water cell UNCHANGED. We do not
        // guess its owning NEREID body or split mass across 4m cells.
        s.microWater.clear();
        s.microWaterCursor=0;
        s.version=3;
    }
    if(!a.writing() && a.ok()){
        try{validateForestDeadwoodResource(s);}
        catch(const std::exception&){a.fail();}
    }
}

inline std::pair<int64_t,int64_t> deadwoodSoilKey(
        int64_t xCm,int64_t zCm) {
    if(xCm<-kWorldLimitCm || xCm>kWorldLimitCm ||
       zCm<-kWorldLimitCm || zCm>kWorldLimitCm)
        throw std::out_of_range("TerraWeave deadwood world extent");
    return {detail::floorDiv(
                xCm,ForestDeadwoodResource::kNutrientCellCm),
            detail::floorDiv(
                zCm,ForestDeadwoodResource::kNutrientCellCm)};
}

// Cheap, non-consuming fertility PROXY for up to 256 nearby rooted plants
// per ecology slice. A nutrient unit is still present in the soil pool:
// the 0..1000 health scalar is NOT itself a unit of added matter.
// PHYSIS/authoring establishes baseline fertility; TerraWeave adds local
// deadwood mineralisation in the same 32m cell, no global redistribution.
inline uint32_t deadwoodNutrientsPermille(
        const ForestDeadwoodResource& state,
        int64_t xCm,int64_t zCm,uint32_t baseline) {
    if(baseline>1000)
        throw std::invalid_argument("TerraWeave baseline nutrient range");
    const auto key=deadwoodSoilKey(xCm,zCm);
    const auto it=std::lower_bound(state.soil.begin(),state.soil.end(),key,
        [](const ForestDeadwoodSoilCell& cell,
           const std::pair<int64_t,int64_t>& k){
            return std::pair{cell.x,cell.z}<k;
        });
    if(it==state.soil.end() ||
       std::pair{it->x,it->z}!=key)return baseline;
    // Max benefit +250 permille, reached at 1000 accumulated units.
    const uint32_t added=uint32_t(
        std::min<uint64_t>(250,it->availableUnits/4));
    return std::min<uint32_t>(1000,baseline+added);
}

inline uint64_t deadwoodOrganicUnits(const AnchoredPlant& p) {
    validateRootProfile(p.anchor.roots);
    if(p.anchor.roots.habit==PlantHabit::Grass ||
       !p.anchor.plantKey || !p.anchor.speciesId)
        throw std::invalid_argument("TerraWeave deadwood rooted identity");
    // Configurable game policy can replace this cheap proxy explicitly.
    const uint64_t radius=p.anchor.roots.radiusCm;
    return std::clamp<uint64_t>(
        radius*(p.anchor.roots.habit==PlantHabit::Tree?24ull:5ull),
        100,200000);
}

// Returns false when this exact species/key was registered before,
// including fully decayed tombstones. Caller must invoke once at a
// structural ECS barrier, not from renderer/Box3D code.
inline bool seedForestDeadwood(
        ForestDeadwoodResource& state,
        const AnchoredPlant& plant,uint64_t completedTick,
        bool fromFall=false,uint64_t units=0,
        uint32_t rateUnitsPer1000Ticks=40,
        uint32_t nutrientsPermille=300) {
    validateForestDeadwoodResource(state);
    const AnchoredPlantId id{
        plant.anchor.speciesId,plant.anchor.plantKey};
    validateRootProfile(plant.anchor.roots);
    if(!id.speciesId || !id.key ||
       plant.anchor.roots.habit==PlantHabit::Grass ||
       plant.anchor.xCm<-kWorldLimitCm ||
       plant.anchor.xCm>kWorldLimitCm ||
       plant.anchor.zCm<-kWorldLimitCm ||
       plant.anchor.zCm>kWorldLimitCm ||
       !rateUnitsPer1000Ticks || rateUnitsPer1000Ticks>1000000 ||
       nutrientsPermille>1000 ||
       (state.initialized && completedTick<state.lastStepTick))
        throw std::invalid_argument("TerraWeave invalid deadwood seed");
    const auto it=std::lower_bound(state.logs.begin(),state.logs.end(),id,
        [](const ForestDeadwoodRecord& log,AnchoredPlantId key){
            return log.id<key;
        });
    if(it!=state.logs.end() && it->id==id)return false;
    if(state.logs.size()>=ForestDeadwoodResource::kMaxLogs)
        throw std::length_error("TerraWeave deadwood log budget");
    if(!units)units=deadwoodOrganicUnits(plant);
    if(units>1000000000ull ||
       state.totalInitialUnits>UINT64_MAX-units ||
       state.revision==UINT64_MAX)
        throw std::overflow_error("TerraWeave deadwood mass/revision overflow");
    ForestDeadwoodRecord log;
    log.id=id;
    log.xCm=plant.anchor.xCm;log.zCm=plant.anchor.zCm;
    log.createdTick=log.lastEvaluatedTick=completedTick;
    log.remainingUnits=units;
    log.initialUnits=units;
    log.rateUnitsPer1000Ticks=rateUnitsPer1000Ticks;
    log.nutrientsPermille=nutrientsPermille;
    log.fromFall=fromFall;
    state.logs.insert(it,log);
    state.totalInitialUnits+=units;
    ++state.revision;
    return true;
}

// PHYSIS can supply this immutable climate from the authoritative tick.
// No coupling to PHYSIS renderer/weather GPU buffers or wall clock.
struct ForestDeadwoodClimate {
    int32_t temperatureCentiC=2000; // 20 C neutral
    uint32_t moisturePermille=700;  // 700/1000 neutral
};
using ForestDeadwoodClimateSampler=std::function<
    ForestDeadwoodClimate(const ForestDeadwoodRecord&)>;

inline uint32_t forestDecayClimateFactorPermille(
        ForestDeadwoodClimate c) {
    if(c.temperatureCentiC<-8000 || c.temperatureCentiC>7000 ||
       c.moisturePermille>1000)
        throw std::invalid_argument("TerraWeave invalid deadwood climate");
    const int64_t warmth=std::clamp<int64_t>(
        1000+(int64_t(c.temperatureCentiC)-2000)/25,150,1800);
    const int64_t moisture=std::clamp<int64_t>(
        1000+(int64_t(c.moisturePermille)-700)/2,300,1200);
    return uint32_t(std::clamp<int64_t>(
        (warmth*moisture+500)/1000,50,2200));
}

struct ForestDeadwoodDecaySettings {
    uint32_t intervalTicks=64;
    uint32_t maxLogsPerStep=256;
    uint32_t maxElapsedTicksPerLog=100000000; // catch-up guard
};
inline void validateForestDeadwoodDecaySettings(
        const ForestDeadwoodDecaySettings& s) {
    if(!s.intervalTicks || s.intervalTicks>100000 ||
       !s.maxLogsPerStep || s.maxLogsPerStep>1024 ||
       !s.maxElapsedTicksPerLog || s.maxElapsedTicksPerLog>100000000)
        throw std::invalid_argument("TerraWeave deadwood decay settings");
}
struct ForestDeadwoodDecayOutcome {
    size_t processed=0;
    uint64_t decayedUnits=0,nutrientsReturned=0,respiredUnits=0;
    std::vector<AnchoredPlantId> fullyDecayed; // Box3D removal requests
    uint64_t revision=0;
};

// All-or-nothing on invalid settings, impossible timestamps, capacity,
// nutrient cell arithmetic or overflow. Update a bounded slice by persistent
// ID index; no per-grass-blade work and no real-time system clock dependency.
// Source record remains at 0 to reject duplicate birth/fall events after
// network resend or chunk streaming.
inline ForestDeadwoodDecayOutcome advanceForestDeadwood(
        ForestDeadwoodResource& resource,uint64_t completedTick,
        ForestDeadwoodDecaySettings settings={},
        ForestDeadwoodClimateSampler climate={}) {
    validateForestDeadwoodDecaySettings(settings);
    validateForestDeadwoodResource(resource);
    if(resource.initialized && completedTick<resource.lastStepTick)
        throw std::invalid_argument("TerraWeave deadwood reversed tick");
    if(resource.initialized &&
       completedTick-resource.lastStepTick<settings.intervalTicks)
        return {};
    ForestDeadwoodResource next=resource;
    next.initialized=true;
    next.lastStepTick=completedTick;
    ForestDeadwoodDecayOutcome result;
    const size_t total=next.logs.size();
    const size_t count=std::min<size_t>(settings.maxLogsPerStep,total);
    result.processed=count;
    const size_t start=total?size_t(next.cursor%total):0u;
    for(size_t offset=0;offset<count;++offset){
        ForestDeadwoodRecord& log=next.logs[(start+offset)%total];
        if(completedTick<log.lastEvaluatedTick)
            throw std::invalid_argument("TerraWeave deadwood future seed");
        const uint64_t dt=completedTick-log.lastEvaluatedTick;
        if(dt>settings.maxElapsedTicksPerLog)
            throw std::length_error("TerraWeave deadwood catch-up limit");
        log.lastEvaluatedTick=completedTick;
        if(!log.remainingUnits)continue;
        // Stage68: sampled PHYSIS temperature/moisture adjusts only the
        // rate, never the organic ledger. The pure integer callback sees
        // the saved record from this completed authoritative ECS tick.
        const uint32_t factor=forestDecayClimateFactorPermille(
            climate?climate(log):ForestDeadwoodClimate{});
        const uint64_t adjustedRate=
            (uint64_t(log.rateUnitsPer1000Ticks)*factor+500)/1000;
        const uint64_t scaled=dt*adjustedRate+log.decayCarry;
        const uint64_t lost=std::min<uint64_t>(
            log.remainingUnits,scaled/1000);
        log.decayCarry=uint16_t(scaled%1000);
        if(!lost)continue;
        log.remainingUnits-=lost;
        if(!log.remainingUnits) {
            log.decayCarry=0;
            result.fullyDecayed.push_back(log.id);
        }
        const uint64_t weighted=lost*log.nutrientsPermille+
                                log.nutrientCarry;
        const uint64_t nutrients=weighted/1000;
        // A completely spent log must account for fractional nutrient
        // carry: less than a unit is routed to the respired remainder.
        log.nutrientCarry=log.remainingUnits?
            uint16_t(weighted%1000):uint16_t(0);
        const uint64_t respired=lost-nutrients;
        if(next.totalRespiredUnits>UINT64_MAX-respired)
            throw std::overflow_error("TerraWeave deadwood respiration overflow");
        next.totalRespiredUnits+=respired;
        const auto key=deadwoodSoilKey(log.xCm,log.zCm);
        const auto it=std::lower_bound(next.soil.begin(),next.soil.end(),key,
            [](const ForestDeadwoodSoilCell& cell,
               const std::pair<int64_t,int64_t>& k){
                return std::pair{cell.x,cell.z}<k;
            });
        if(nutrients){
            if(it!=next.soil.end() && std::pair{it->x,it->z}==key){
                if(it->availableUnits>UINT64_MAX-nutrients)
                    throw std::overflow_error("TerraWeave soil nutrient overflow");
                it->availableUnits+=nutrients;
            }else{
                if(next.soil.size()>=ForestDeadwoodResource::kMaxSoilCells)
                    throw std::length_error("TerraWeave deadwood soil cell budget");
                next.soil.insert(it,{key.first,key.second,nutrients});
            }
        }
        result.decayedUnits+=lost;
        result.nutrientsReturned+=nutrients;
        result.respiredUnits+=respired;
    }
    next.cursor=total?(start+count)%total:0u;
    if(result.decayedUnits){
        if(next.revision==UINT64_MAX)
            throw std::overflow_error("TerraWeave deadwood revision overflow");
        ++next.revision;
    }
    result.revision=next.revision;
    validateForestDeadwoodResource(next);
    resource=std::move(next);
    return result;
}

// Remove the wood that a player actually carries away. Nutrients already
// deposited remain in soil; no accidental double reward on subsequent calls.
// Returns exported remainder, and retains a spent-ID tombstone.
inline uint64_t extractForestDeadwood(
        ForestDeadwoodResource& state,AnchoredPlantId id) {
    validateForestDeadwoodResource(state);
    const auto it=std::lower_bound(state.logs.begin(),state.logs.end(),id,
        [](const ForestDeadwoodRecord& log,AnchoredPlantId key){
            return log.id<key;
        });
    if(it==state.logs.end() || it->id!=id || !it->remainingUnits)
        return 0;
    const uint64_t units=it->remainingUnits;
    if(state.totalExportedUnits>UINT64_MAX-units ||
       state.revision==UINT64_MAX)
        throw std::overflow_error("TerraWeave extracted wood overflow");
    it->remainingUnits=0;
    it->decayCarry=it->nutrientCarry=0;
    state.totalExportedUnits+=units;
    ++state.revision;
    return units;
}

inline bool deadwoodLedgerBalanced(
        const ForestDeadwoodResource& state) {
    try {
        validateForestDeadwoodResource(state);
        return true;
    }catch(const std::exception&){
        return false;
    }
}

} // namespace aiverse::terraweave
