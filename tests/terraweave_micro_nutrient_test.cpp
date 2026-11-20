// Stage70: source-level tests for the pure 4m nutrient ledger.
// These fixtures must be compiled/executed by independent native QA;
// authoring this file alone is NOT evidence that they pass.
#include "engine/world/TerraWeaveForestMicroNutrients.h"
#include "engine/core/Archive.h"
#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

using namespace aiverse::terraweave;

int main() {
    const auto soil=[](int64_t,int64_t) {
        return ForestMicroSoilSample{0,700,0};
    };

    // Old v2 saved dissolved 32m water cannot be silently assigned to one
    // of two neighboring NEREID streams. Archive read preserves 200 units.
    ForestDeadwoodResource old;
    old.version=2;
    old.totalInitialUnits=200;
    old.water={{0,0,200}};
    std::string wire;
    aiverse::Archive writer(&wire);
    io(writer,old);
    assert(writer.ok());
    aiverse::Archive reader(wire);
    ForestDeadwoodResource migrated;
    io(reader,migrated);
    assert(reader.ok() && reader.remaining()==0);
    assert(migrated.version==3 && migrated.water.size()==1);
    assert(migrated.microWater.empty() &&
           migrated.water.front().dissolvedUnits==200);
    assert(deadwoodLedgerBalanced(migrated));

    // An invalid partial split is rejected without modifying source.
    ForestLegacyMicroClaim bad;
    bad.coarseX=0;bad.coarseZ=0;
    bad.allocations={{{1,1,0},99},{{2,2,0},100}};
    bool rejected=false;
    try{
        claimForestLegacyWater(
            migrated,std::span<const ForestLegacyMicroClaim>(&bad,1),
            [](ForestMicroKey){return true;});
    }catch(const std::invalid_argument&){rejected=true;}
    assert(rejected && migrated.water.front().dissolvedUnits==200);

    ForestLegacyMicroClaim claim;
    claim.coarseX=0;claim.coarseZ=0;
    claim.allocations={{{1,1,0},100},{{2,2,0},100}};
    const uint64_t claimed=claimForestLegacyWater(
        migrated,std::span<const ForestLegacyMicroClaim>(&claim,1),
        [](ForestMicroKey k) {
            return (k.bodyKey==1 && k.x==1) ||
                   (k.bodyKey==2 && k.x==2);
        });
    assert(claimed==200 && migrated.water.empty());
    assert(migrated.microWater.size()==2);
    assert(deadwoodLedgerBalanced(migrated));

    // Two different water bodies can occupy the SAME 4m coordinate in
    // saved data without merging their mass; direct water sampling may
    // still conservatively reject an occluded secondary body.
    ForestDeadwoodResource overlap;
    overlap.totalInitialUnits=110;
    overlap.microWater={{1,1,0,50},{2,1,0,60}};
    assert(deadwoodLedgerBalanced(overlap));

    // One 4m cardinal NEREID step moves at most 64 units from the
    // immutable prior-tick snapshot. Next-hop movement is deferred.
    ForestDeadwoodResource current;
    current.totalInitialUnits=100;
    current.microWater={{1,0,0,100}};
    auto river=[](int64_t x,int64_t z,uint64_t expected) {
        if(z!=0 || (x!=0 && x!=1) ||
           (expected && expected!=1))
            return ForestMicroSample{};
        return ForestMicroSample{1,true,100,0};
    };
    const auto move=advanceForestMicroNutrients(
        current,256,soil,river);
    assert(move.due && move.advected==64);
    assert(current.microWater.size()==2);
    assert(current.microWater[0].dissolvedUnits==36);
    assert(current.microWater[1].dissolvedUnits==64);
    assert(deadwoodLedgerBalanced(current));

    // One valid NEREID confluence crosses different body IDs even
    // INSIDE the same old 32m tile, without carrying incoming
    // nutrients across the next junction in the SAME tick.
    ForestDeadwoodResource linked;
    linked.totalInitialUnits=100;
    linked.microWater={{1,0,0,100}};
    const auto linkedWater=[](int64_t x,int64_t z,uint64_t expected){
        if(z!=0 || x<0 || x>2)return ForestMicroSample{};
        const uint64_t body=x==0?1:(x==1?2:3);
        if(expected && expected!=body)return ForestMicroSample{};
        return ForestMicroSample{body,true,0,0};
    };
    const std::vector<ForestMicroJunction> junctions={
        {1,{1,0,0},{2,1,0},32},
        {2,{2,1,0},{3,2,0},32}
    };
    const auto crossed=advanceForestMicroNutrients(
        linked,256,soil,linkedWater,{},junctions);
    assert(crossed.junctionTransferred==32);
    assert(linked.microWater.size()==2);
    assert(linked.microWater[0].dissolvedUnits==68);
    assert(linked.microWater[1].dissolvedUnits==32);
    assert(deadwoodLedgerBalanced(linked));

    // Real authored outlet updates a shared monotonic receipt and can't
    // export the same units twice across duplicate network delivery.
    const uint64_t taken=exportForestMicroDissolvedNutrients(
        linked,{2,1,0},50,1);
    assert(taken==32);
    assert(exportForestMicroDissolvedNutrients(
        linked,{2,1,0},50,1)==0);
    assert(linked.totalRunoffExportUnits==32);
    assert(deadwoodLedgerBalanced(linked));

    // Regression: 32m/4m floor mapping must not truncate negative keys
    // toward zero. An invalid cross-cell claim is an atomic no-op.
    assert((microToCoarse({1,-1,-1})==
            std::pair<int64_t,int64_t>{-1,-1}));
    assert((microToCoarse({1,-8,-8})==
            std::pair<int64_t,int64_t>{-1,-1}));
    assert((microToCoarse({1,-9,-1})==
            std::pair<int64_t,int64_t>{-2,-1}));
    ForestDeadwoodResource negative;
    negative.totalInitialUnits=90;
    negative.water={{-1,-1,90}};
    ForestLegacyMicroClaim invalidNegative;
    invalidNegative.coarseX=-1;invalidNegative.coarseZ=-1;
    invalidNegative.allocations={
        {{1,-8,-8},40},{{2,0,-1},50}};
    rejected=false;
    try{
        claimForestLegacyWater(negative,
            std::span<const ForestLegacyMicroClaim>(&invalidNegative,1),
            [](ForestMicroKey){return true;});
    }catch(const std::invalid_argument&){rejected=true;}
    assert(rejected && negative.revision==0);
    assert(negative.microWater.empty() && negative.water.size()==1);
    assert(negative.water.front().dissolvedUnits==90);
    invalidNegative.allocations[1].key={2,-1,-1};
    rejected=false;
    try{
        claimForestLegacyWater(negative,
            std::span<const ForestLegacyMicroClaim>(&invalidNegative,1),
            [](ForestMicroKey){return false;});
    }catch(const std::invalid_argument&){rejected=true;}
    assert(rejected && negative.revision==0 && negative.microWater.empty());
    assert(claimForestLegacyWater(negative,
        std::span<const ForestLegacyMicroClaim>(&invalidNegative,1),
        [](ForestMicroKey){return true;})==90);
    assert(negative.water.empty() && negative.microWater.size()==2);
    assert(deadwoodLedgerBalanced(negative));
    std::string negativeWire;
    aiverse::Archive negativeWriter(&negativeWire);
    io(negativeWriter,negative);
    assert(negativeWriter.ok());
    aiverse::Archive negativeReader(negativeWire);
    ForestDeadwoodResource negativeRestored;
    io(negativeReader,negativeRestored);
    assert(negativeReader.ok() && negativeReader.remaining()==0);
    assert(negativeRestored.microWater==negative.microWater);
    assert(deadwoodLedgerBalanced(negativeRestored));

    // Regression: processing B,C from [A,B,C,D] and removing B must
    // resume at D, not A. The saved cursor indexes the compacted vector.
    ForestMicroSettings slices;
    slices.maxSoilPerStep=2;
    slices.maxWaterPerStep=2;
    std::vector<int64_t> soilVisited;
    auto slicedSoil=[&soilVisited](int64_t x,int64_t){
        soilVisited.push_back(x);
        return ForestMicroSoilSample{0,700,uint32_t(x==1?20:0)};
    };
    auto dry=[](int64_t,int64_t,uint64_t){
        return ForestMicroSample{};
    };
    ForestDeadwoodResource soilCursorState;
    soilCursorState.totalInitialUnits=50;
    soilCursorState.soil={
        {0,0,10},{1,0,20},{2,0,10},{3,0,10}};
    soilCursorState.soilCursor=1;
    const auto soilFirst=advanceForestMicroNutrients(
        soilCursorState,256,slicedSoil,dry,slices);
    assert(soilFirst.soilProcessed==2);
    assert(soilCursorState.soil.size()==3);
    assert(soilCursorState.soilCursor==2);
    const auto soilSecond=advanceForestMicroNutrients(
        soilCursorState,512,slicedSoil,dry,slices);
    assert(soilSecond.soilProcessed==2);
    assert((soilVisited==std::vector<int64_t>{1,2,3,0}));
    assert(deadwoodLedgerBalanced(soilCursorState));

    // Regression: outgoing nutrient flow removes x=1 while processing
    // x=1,2. Next saved water cursor must resume at x=3, not x=0.
    ForestDeadwoodResource waterCursorState;
    waterCursorState.totalInitialUnits=40;
    waterCursorState.microWater={
        {1,0,0,10},{1,1,0,10},{1,2,0,10},{1,3,0,10}};
    waterCursorState.microWaterCursor=1;
    slices.maxTransportPerWater=10;
    auto slicedRiver=[](int64_t x,int64_t z,uint64_t expected){
        if(z || x<0 || x>3 || (expected && expected!=1))
            return ForestMicroSample{};
        return ForestMicroSample{1,true,
            (x==1 || x==2)?100:0,0};
    };
    const auto waterFirst=advanceForestMicroNutrients(
        waterCursorState,256,soil,slicedRiver,slices);
    assert(waterFirst.waterProcessed==2);
    assert(waterFirst.advected==20);
    assert(waterCursorState.microWater.size()==3);
    assert(waterCursorState.microWaterCursor==2);
    assert(waterCursorState.microWater[2].x==3);
    assert(deadwoodLedgerBalanced(waterCursorState));

    // Full persisted 4m cap: process bounded slices over 65,536
    // distinct body-tagged cells without changing any organic mass.
    // This is a correctness/limit fixture, NOT a performance benchmark.
    ForestDeadwoodResource capacity;
    capacity.totalInitialUnits=ForestDeadwoodResource::kMaxMicroWaterCells;
    capacity.microWater.reserve(
        ForestDeadwoodResource::kMaxMicroWaterCells+1);
    for(int64_t x=0;
        x<int64_t(ForestDeadwoodResource::kMaxMicroWaterCells);++x)
        capacity.microWater.push_back({1,x,0,1});
    assert(deadwoodLedgerBalanced(capacity));
    ForestMicroSettings capacitySettings;
    capacitySettings.maxWaterPerStep=1024;
    const auto stationaryRiver=[](int64_t,int64_t,uint64_t expected){
        return expected==1?
            ForestMicroSample{1,true,0,0}:ForestMicroSample{};
    };
    const auto firstSlice=advanceForestMicroNutrients(
        capacity,256,soil,stationaryRiver,capacitySettings);
    assert(firstSlice.due && firstSlice.waterProcessed==1024);
    assert(capacity.microWaterCursor==1024);
    const auto secondSlice=advanceForestMicroNutrients(
        capacity,512,soil,stationaryRiver,capacitySettings);
    assert(secondSlice.due && secondSlice.waterProcessed==1024);
    assert(capacity.microWaterCursor==2048);
    assert(capacity.microWater.size()==
           ForestDeadwoodResource::kMaxMicroWaterCells);
    assert(capacity.totalInitialUnits==
           ForestDeadwoodResource::kMaxMicroWaterCells);
    assert(deadwoodLedgerBalanced(capacity));
    // An oversized saved reservoir is rejected BEFORE any processing.
    capacity.microWater.push_back({
        1,int64_t(ForestDeadwoodResource::kMaxMicroWaterCells),0,1});
    capacity.totalInitialUnits++;
    rejected=false;
    try{
        advanceForestMicroNutrients(
            capacity,768,soil,stationaryRiver,capacitySettings);
    }catch(const std::invalid_argument&){rejected=true;}
    assert(rejected && capacity.microWater.size()==
           ForestDeadwoodResource::kMaxMicroWaterCells+1);
    assert(capacity.lastNutrientTick==512);

    // Bad NEREID body link and old tick must fail without mutation.
    const std::vector<ForestMicroJunction> wrong={
        {1,{1,0,0},{1,1,0},32}
    };
    rejected=false;
    try{advanceForestMicroNutrients(linked,512,soil,linkedWater,{},wrong);}
    catch(const std::invalid_argument&){rejected=true;}
    assert(rejected && deadwoodLedgerBalanced(linked));
    rejected=false;
    try{advanceForestMicroNutrients(linked,128,soil,linkedWater);}
    catch(const std::invalid_argument&){rejected=true;}
    assert(rejected && deadwoodLedgerBalanced(linked));
}
