// TerraWeave Stage 64 — deterministic post-flood shoreline ecology.
// A single record represents one GrassPatch, NOT one blade or ECS entity.
// Recovers eroded/drowned coverage using a seed bank, light and moisture,
// without changing NEREID water, Earthworks ground mass or fallen tree state.
#pragma once
#include "engine/world/TerraWeaveGrassCover.h"
#include "engine/core/Archive.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::terraweave {

struct ShoreRecoverySettings {
    uint32_t periodTicks=600;                  // 1..100000 simulation ticks
    uint32_t maxPatches=4096;                 // <=GrassCoverRegistry::kMaxPatches
    uint32_t floodDamagePermillePerPeriod=80; // 0..1000
    uint32_t regrowthPermillePerPeriod=90;    // 0..1000
    uint32_t seedGainPermillePerPeriod=12;    // 0..1000
    uint32_t soilCoverGainPermillePerPeriod=8;
    uint32_t soilCoverLossPermillePerPeriod=50;
    uint32_t maxPeriodsPerStep=32;            // catchup cap (1..100)
    uint32_t wetThresholdPermille=250;        // sampled 5-point flooded fraction
    uint32_t minLightPermille=180;
};

inline void validateShoreRecoverySettings(
        const ShoreRecoverySettings& s){
    if(!s.periodTicks || s.periodTicks>100000 ||
       !s.maxPatches || s.maxPatches>GrassCoverRegistry::kMaxPatches ||
       s.floodDamagePermillePerPeriod>1000 ||
       s.regrowthPermillePerPeriod>1000 ||
       s.seedGainPermillePerPeriod>1000 ||
       s.soilCoverGainPermillePerPeriod>1000 ||
       s.soilCoverLossPermillePerPeriod>1000 ||
       !s.maxPeriodsPerStep || s.maxPeriodsPerStep>100 ||
       s.wetThresholdPermille>1000 ||
       s.minLightPermille>1000)
        throw std::invalid_argument("TerraWeave invalid shore recovery settings");
}

struct ShoreRecoveryPatchState {
    uint64_t key=0;
    uint64_t wetTicks=0,dryTicks=0;           // saturated/cumulative dwell
    uint32_t seedBankPermille=750;           // persistent regrowth potential
    uint32_t soilCoverPermille=500;          // aggregate surface shield, NO grass roots
    constexpr bool operator==(const ShoreRecoveryPatchState&) const = default;
};

struct ShoreRecoveryResource {
    uint32_t version=1;
    uint64_t lastTick=0;
    bool initialized=false;
    uint64_t revision=0;
    std::vector<ShoreRecoveryPatchState> patches; // strictly sorted patch IDs
};

inline void validateShoreRecoveryResource(
        const ShoreRecoveryResource& state) {
    if(state.version!=1 ||
       state.patches.size()>GrassCoverRegistry::kMaxPatches)
        throw std::invalid_argument("TerraWeave invalid ecology resource");
    uint64_t last=0;
    for(const auto& item:state.patches) {
        if(!item.key || item.key<=last ||
           item.seedBankPermille>1000 ||
           item.soilCoverPermille>1000)
            throw std::invalid_argument("TerraWeave invalid ecology patch");
        last=item.key;
    }
}

struct ShorePatchClimate {
    uint32_t floodedPermille=0;              // 0..1000, NEREID water coverage
    uint32_t waterPermille=600;               // 0..1000, PHYSIS water availability
    uint32_t lightPermille=1000;              // 0..1000, canopy/shadow input
    uint32_t nutrientsPermille=1000;          // 0..1000, soil fertility
};
using ShorePatchClimateSampler=
    std::function<ShorePatchClimate(const GrassPatch&)>;

struct ShoreRecoveryChange {
    uint64_t key=0;
    uint32_t oldCoveragePermille=0,newCoveragePermille=0;
    uint32_t soilCoverPermille=0;
    PlantEnvironment environment{};
};

struct ShoreRecoveryResult {
    uint32_t evaluatedPatches=0;
    std::vector<ShoreRecoveryChange> changed;
    uint64_t revision=0;  // ecology revision, NOT Earthworks soil revision
};

inline void io(Archive& a,ShoreRecoveryPatchState& p){
    a(p.key,p.wetTicks,p.dryTicks,
      p.seedBankPermille,p.soilCoverPermille);
}
inline void io(Archive& a,ShoreRecoveryResource& state){
    a(state.version,state.lastTick,state.initialized,state.revision);
    const uint32_t count=a.count(state.patches.size(),32);
    if(!a.ok() || count>GrassCoverRegistry::kMaxPatches){
        a.fail();return;
    }
    if(!a.writing())state.patches.assign(count,{});
    for(auto& p:state.patches)if(a.ok())a(p);
    if(!a.writing() && a.ok()){
        try{validateShoreRecoveryResource(state);}
        catch(const std::exception&){a.fail();}
    }
}

// Caller passes a staging copy of canonical ECS GrassPatch vectors. This
// function stages BOTH resource and patches, and commits only after all
// validation, climate callbacks and allocations succeed. No per-blade loop.
inline ShoreRecoveryResult advanceShoreRecovery(
        ShoreRecoveryResource& recovery,
        std::vector<GrassPatch>& grass,
        uint64_t completedTick,
        const ShorePatchClimateSampler& climate,
        ShoreRecoverySettings settings={}) {
    validateShoreRecoverySettings(settings);
    validateShoreRecoveryResource(recovery);
    if(!climate || grass.size()>settings.maxPatches ||
       (recovery.initialized && completedTick<recovery.lastTick))
        throw std::invalid_argument("TerraWeave shore recovery tick/patch budget");
    std::vector<GrassPatch> nextGrass=grass;
    std::sort(nextGrass.begin(),nextGrass.end(),
        [](const GrassPatch& a,const GrassPatch& b){return a.key<b.key;});
    for(size_t i=0;i<nextGrass.size();++i){
        (void)prepareGrassPatch(nextGrass[i]);
        if(!nextGrass[i].key || (i && nextGrass[i-1].key==nextGrass[i].key))
            throw std::invalid_argument("TerraWeave duplicated ecology grass patch");
    }
    ShoreRecoveryResource next=recovery;
    if(!next.initialized) {
        next.initialized=true;
        next.lastTick=completedTick;
        next.patches.clear();
        next.patches.reserve(nextGrass.size());
        for(const auto& patch:nextGrass)
            next.patches.push_back({patch.key});
        recovery=std::move(next);
        return {};
    }
    const uint64_t delta=completedTick-next.lastTick;
    if(delta<settings.periodTicks)return {};
    const uint64_t periods=std::min<uint64_t>(
        settings.maxPeriodsPerStep,delta/settings.periodTicks);
    // Advance the clock only by processed time. Large tick gaps continue
    // catching up over consecutive owner phases without skipping ecology.
    const uint64_t elapsed=periods*settings.periodTicks;
    if(next.lastTick>UINT64_MAX-elapsed)
        throw std::overflow_error("TerraWeave ecology clock overflow");
    next.lastTick+=elapsed;
    const uint64_t ageLimit=1000000000000ull;

    std::map<uint64_t,ShoreRecoveryPatchState> old;
    for(const auto& patch:next.patches)old.emplace(patch.key,patch);
    next.patches.clear();
    next.patches.reserve(nextGrass.size());

    ShoreRecoveryResult result;
    result.evaluatedPatches=uint32_t(nextGrass.size());
    for(auto& patch:nextGrass) {
        ShoreRecoveryPatchState state;
        if(const auto it=old.find(patch.key);it!=old.end())
            state=it->second;
        else state.key=patch.key;
        const ShorePatchClimate sampled=climate(patch);
        if(sampled.floodedPermille>1000 ||
           sampled.waterPermille>1000 ||
           sampled.lightPermille>1000 ||
           sampled.nutrientsPermille>1000)
            throw std::invalid_argument("TerraWeave invalid sampled shore climate");
        const bool waterlogged=
            sampled.floodedPermille>=settings.wetThresholdPermille &&
            sampled.floodedPermille>0;
        const uint64_t maxTick=UINT64_MAX-elapsed;
        if(state.wetTicks>maxTick || state.dryTicks>maxTick)
            throw std::overflow_error("TerraWeave shore exposure clock overflow");
        if(waterlogged)state.wetTicks+=elapsed;
        else state.dryTicks+=elapsed;

        auto& env=patch.environment;
        const PlantEnvironment oldEnvironment=env;
        (void)plantAppearance(env);
        env.ageTicks=std::min<uint64_t>(
            ageLimit,env.ageTicks+std::min<uint64_t>(
                elapsed,ageLimit-env.ageTicks));
        env.waterPermille=sampled.waterPermille;
        env.lightPermille=sampled.lightPermille;
        env.nutrientsPermille=sampled.nutrientsPermille;

        const uint32_t before=patch.coveragePermille;
        if(waterlogged){
            // Drowning damages grass gradually. Wetland tolerance may be
            // supplied by native ecology material policy in later stages.
            const uint32_t strength=std::min<uint32_t>(
                1000,sampled.floodedPermille);
            const uint64_t loss=std::min<uint64_t>(
                1000,uint64_t(settings.floodDamagePermillePerPeriod)*
                periods*strength/1000);
            patch.coveragePermille=uint32_t(
                uint64_t(before)*(1000-loss)/1000);
            state.seedBankPermille=uint32_t(
                uint64_t(state.seedBankPermille)*
                (1000-std::min<uint64_t>(500,loss/2))/1000);
            state.soilCoverPermille=uint32_t(
                uint64_t(state.soilCoverPermille)*
                (1000-std::min<uint64_t>(
                    1000,uint64_t(settings.soilCoverLossPermillePerPeriod)*
                    periods))/1000);
        }else{
            // No growth on permanently barren, starved/dark/over-dry soil.
            const uint32_t condition=std::min({
                sampled.waterPermille,sampled.lightPermille,
                sampled.nutrientsPermille});
            if(condition>=settings.minLightPermille &&
               state.seedBankPermille>0){
                const uint32_t regrow=uint32_t(std::min<uint64_t>(
                    1000,uint64_t(settings.regrowthPermillePerPeriod)*
                    periods*condition/1000));
                const uint64_t gain=
                    uint64_t(1000-before)*regrow*
                    state.seedBankPermille/1000000ull;
                // Deterministic 1‰ sprout for slow recovery on viable
                // patches, but never spontaneously revive zero seed bank.
                const uint32_t applied=uint32_t(std::min<uint64_t>(
                    1000-before,std::max<uint64_t>(gain,1)));
                patch.coveragePermille=before+applied;
                state.seedBankPermille=uint32_t(std::min<uint64_t>(
                    1000,uint64_t(state.seedBankPermille)+
                    uint64_t(settings.seedGainPermillePerPeriod)*
                    periods*condition/1000));
                state.soilCoverPermille=uint32_t(std::min<uint64_t>(
                    1000,uint64_t(state.soilCoverPermille)+
                    uint64_t(settings.soilCoverGainPermillePerPeriod)*
                    periods*patch.coveragePermille/1000));
            }
        }
        if(patch.coveragePermille!=before || !(env==oldEnvironment))
            result.changed.push_back({
                patch.key,before,patch.coveragePermille,
                state.soilCoverPermille,env});
        next.patches.push_back(state);
    }
    if(next.revision==UINT64_MAX)
        throw std::overflow_error("TerraWeave ecology revision overflow");
    ++next.revision;
    result.revision=next.revision;
    recovery=std::move(next);
    grass=std::move(nextGrass);
    return result;
}


// A 64m bucket lookup makes it possible to apply recovery-derived topsoil
// shielding to <=16k auto shoreline ECS bank vertices without a 4096x16384
// grass-patch cross product. This is NOT root-binding and never creates
// per-blade ECS data. All input arrays are immutable snapshot values.
class ShoreRecoveryCoverIndex {
public:
    explicit ShoreRecoveryCoverIndex(
            const std::vector<GrassPatch>& grass,
            const ShoreRecoveryResource& recovery){
        validateShoreRecoveryResource(recovery);
        if(grass.size()>GrassCoverRegistry::kMaxPatches)
            throw std::length_error("TerraWeave cover index patch budget");
        std::map<uint64_t,ShoreRecoveryPatchState> recover;
        for(const auto& state:recovery.patches)
            recover.emplace(state.key,state);
        for(const auto& patch:grass){
            (void)prepareGrassPatch(patch);
            const auto it=recover.find(patch.key);
            if(it==recover.end())continue;
            const uint32_t shield=uint32_t(
                uint64_t(it->second.soilCoverPermille)*
                patch.coveragePermille/1000);
            if(!shield)continue;
            const size_t index=patches_.size();
            patches_.push_back({patch.bounds,shield});
            constexpr int64_t spacing=GrassCoverRegistry::kBucketCm;
            const int64_t x0=detail::floorDiv(patch.bounds.minX,spacing);
            const int64_t x1=detail::floorDiv(patch.bounds.maxX-1,spacing);
            const int64_t z0=detail::floorDiv(patch.bounds.minZ,spacing);
            const int64_t z1=detail::floorDiv(patch.bounds.maxZ-1,spacing);
            for(int64_t x=x0;x<=x1;++x)
                for(int64_t z=z0;z<=z1;++z)
                    buckets_[{x,z}].push_back(index);
        }
    }
    uint32_t shieldAt(int64_t x,int64_t z)const {
        constexpr int64_t spacing=GrassCoverRegistry::kBucketCm;
        const auto it=buckets_.find({
            detail::floorDiv(x,spacing),detail::floorDiv(z,spacing)});
        if(it==buckets_.end())return 0;
        uint32_t coverage=0;
        for(const size_t index:it->second){
            const auto& p=patches_[index];
            if(p.bounds.contains(x,z))
                coverage=std::max(coverage,p.shieldPermille);
        }
        return coverage;
    }
private:
    struct Entry {RectCm bounds{};uint32_t shieldPermille=0;};
    std::vector<Entry> patches_;
    std::map<std::pair<int64_t,int64_t>,std::vector<size_t>> buckets_;
};

} // namespace aiverse::terraweave
