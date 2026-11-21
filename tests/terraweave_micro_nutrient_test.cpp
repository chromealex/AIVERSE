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
