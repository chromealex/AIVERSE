// Stage70 QA-only bounded longevity/replay stress for the sparse nutrient
// ledger. Pure deterministic NEREID-like sampling; the separate native
// physis_terraweave_fall_bridge test owns real ECS/PHYSIS/NEREID/Box3D.
#include "engine/world/TerraWeaveForestMicroNutrients.h"
#include "engine/core/Archive.h"
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace tw = aiverse::terraweave;

static void require(bool value,const char* reason) {
    if(!value)throw std::runtime_error(reason);
}

static std::string snapshot(const tw::ForestDeadwoodResource& ledger) {
    auto copy=ledger;
    std::string bytes;
    aiverse::Archive writer(&bytes);
    tw::io(writer,copy);
    require(writer.ok(),"Stage70 stress archive write failed");
    return bytes;
}

static tw::ForestDeadwoodResource restore(const std::string& bytes) {
    tw::ForestDeadwoodResource ledger;
    aiverse::Archive reader(bytes);
    tw::io(reader,ledger);
    require(reader.ok() && reader.remaining()==0,
            "Stage70 stress v3 archive read failed");
    tw::validateForestDeadwoodResource(ledger);
    return ledger;
}

static tw::ForestDeadwoodResource initialState() {
    tw::ForestDeadwoodResource ledger;
    // 128 32m soil records and 2048 body-tagged 4m water records.
    // Body IDs and signed coordinates are ordered exactly as in the v3
    // archive. Never allocate a dense global water grid.
    for(int64_t x=-64;x<64;++x)
        ledger.soil.push_back({x,0,400});
    for(uint64_t body:{uint64_t(101),uint64_t(202)})
        for(int64_t x=-512;x<512;++x)
            ledger.microWater.push_back({body,x,body==101?0:1,10});
    ledger.totalInitialUnits=
        uint64_t(ledger.soil.size())*400+
        uint64_t(ledger.microWater.size())*10;
    tw::validateForestDeadwoodResource(ledger);
    return ledger;
}

static const tw::ForestMicroSettings kSettings=[]{
    tw::ForestMicroSettings settings;
    settings.intervalTicks=256;
    settings.maxSoilPerStep=32;
    settings.maxWaterPerStep=128;
    settings.maxJunctions=1;
    settings.maxUptakePerSoil=4;
    settings.maxLeachPerSoil=6;
    settings.maxTransportPerWater=6;
    return settings;
}();

// A bounded, static fixture, NOT a NEREID solver. The true ECS water
// priority/identity adapter is independently tested by the fall bridge.
static const auto waterAt=[](int64_t x,int64_t z,uint64_t expected) {
    const uint64_t body=z==0?101ull:(z==1?202ull:0ull);
    if(!body || x<-512 || x>=512 || (expected && body!=expected))
        return tw::ForestMicroSample{};
    return tw::ForestMicroSample{body,true,50,0};
};
static const auto soilAt=[](int64_t x,int64_t z) {
    const uint32_t demand=(z==0 && ((x+64)%4==0))?3u:2u;
    return tw::ForestMicroSoilSample{850,900,demand};
};
static const std::vector<tw::ForestMicroJunction> kLinks={
    {1,{101,-1,0},{202,-1,1},4}
};

struct Counters {
    uint64_t absorbed=0,leached=0,advected=0,junction=0;
};

static void advance(tw::ForestDeadwoodResource& state,
                    uint64_t iteration,Counters& totals) {
    const uint64_t tick=(iteration+1)*uint64_t(kSettings.intervalTicks);
    const auto result=tw::advanceForestMicroNutrients(
        state,tick,soilAt,waterAt,kSettings,kLinks);
    require(result.due && result.soilProcessed<=kSettings.maxSoilPerStep &&
            result.waterProcessed<=kSettings.maxWaterPerStep &&
            result.junctionsProcessed<=kSettings.maxJunctions,
            "Stage70 bounded scheduled slice violated");
    require(state.lastNutrientTick==tick && state.nutrientInitialized,
            "Stage70 tick checkpoint was not persisted");
    require(state.microWater.size()<=tw::ForestDeadwoodResource::kMaxMicroWaterCells &&
            state.soil.size()<=tw::ForestDeadwoodResource::kMaxSoilCells &&
            tw::deadwoodLedgerBalanced(state),
            "Stage70 organic conservation or memory bound failed");
    totals.absorbed+=result.absorbed;
    totals.leached+=result.leached;
    totals.advected+=result.advected;
    totals.junction+=result.junctionTransferred;
}

int main() {
    constexpr uint64_t kSteps=128; // 32,768 virtual simulation ticks
    tw::ForestDeadwoodResource state=initialState();
    const uint64_t originalUnits=state.totalInitialUnits;
    std::string checkpoint;
    Counters firstTotals{};
    for(uint64_t i=0;i<kSteps;++i) {
        advance(state,i,firstTotals);
        if(i==kSteps/2-1)
            checkpoint=snapshot(state);
        if(i==0) {
            // Calling before the next due tick must not reschedule or
            // mutate a previously committed authoritative transaction.
            const std::string before=snapshot(state);
            const auto early=tw::advanceForestMicroNutrients(
                state,257,soilAt,waterAt,kSettings,kLinks);
            require(!early.due && snapshot(state)==before,
                    "Stage70 sub-interval call mutated the ledger");
        }
    }
    require(!checkpoint.empty() && originalUnits==state.totalInitialUnits,
            "Stage70 checkpoint or initial mass lost");
    require(firstTotals.absorbed>0 && firstTotals.leached>0 &&
            firstTotals.advected>0 && firstTotals.junction>0,
            "Stage70 longevity fixture never exercised all transitions");
    const std::string finalBytes=snapshot(state);

    // Persisted midpoint replay must match every v3 field byte-for-byte:
    // water/soil cursors, relative ordering, receipts, tick, and mass.
    auto replay=restore(checkpoint);
    Counters replayTotals{};
    for(uint64_t i=kSteps/2;i<kSteps;++i)
        advance(replay,i,replayTotals);
    require(snapshot(replay)==finalBytes,
            "Stage70 sustained save/restore/replay diverged");

    // The source snapshot must be independent of later mutations.
    const auto restoredEnd=restore(finalBytes);
    require(restoredEnd.totalInitialUnits==originalUnits &&
            restoredEnd.microWater==state.microWater &&
            restoredEnd.microWaterCursor==state.microWaterCursor &&
            tw::deadwoodLedgerBalanced(restoredEnd),
            "Stage70 final v3 archive diverged");
    std::cout<<"PASS: Stage70 128 due ticks / 32768 virtual ticks; "
             <<"initialUnits="<<originalUnits
             <<" absorbed="<<firstTotals.absorbed
             <<" leached="<<firstTotals.leached
             <<" advected="<<firstTotals.advected
             <<" junction="<<firstTotals.junction
             <<" remainingMicroCells="<<state.microWater.size()
             <<" replay=identical\n";
}
