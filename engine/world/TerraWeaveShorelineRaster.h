// TerraWeave Stage 57: automatically discover stable NEREID shoreline bank
// samples from the EXISTING ECS water bodies / polygon / river-spline
// coverage. One bank entity per terrain lattice vertex, never per grass blade.
// Pure discovery is read-only and deterministic; optional ECS publication
// must execute at a native structural-write barrier before water sampling.
#pragma once
#include "engine/world/TerraWeaveEcsBankBridge.h"
#include <aiverselib/water/NereidQueryIndex.h>
#include <aiverselib/water/NereidSedimentTransport.h>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::terraweave {

struct ShorelineRasterSettings {
    RectCm area{};                       // half-open world cm; <= 16384 vertices
    int32_t splashHeightCm=100;          // 0..500: waves reach above the surface
    int32_t maxWaterDepthCm=10000;       // 1..50000: bounded bank/bed depth
    bool includeWetBedCells=false;       // opt-in whole riverbed/estuary carrier
    int32_t minWetBedDepthCm=5;         // 0..5000cm for interior sediment
    uint32_t waveEnergyPermille=0;       // host PHYSIS/NEREID wave input
    uint32_t minContactPermille=50;      // 1..1000
    uint32_t maxBankCells=16384;
};

struct AutoShorelineBank {
    EarthVertex vertex{};
    Vec3 wetProbePosition{};            // water-side contact point
    lib::nereid::ShoreSoil substrate=lib::nereid::ShoreSoil::Loam;
    uint32_t wetContactPermille=0;
    uint32_t bankSlopePermille=0;
    uint32_t waveEnergyPermille=0;
    uint32_t rootBindingPermille=0;
    int32_t groundCm=0;
    int32_t bedrockCm=0;
};

using ShorelineSoilAt=std::function<lib::nereid::ShoreSoil(
    EarthVertex, int32_t)>;
using ShorelineRootsAt=std::function<uint32_t(
    EarthVertex)>;

// Explicit fallback by biome only when an authored soil paint/layer is not
// supplied. Project soil maps should override this for realistic erosion.
inline lib::nereid::ShoreSoil biomeShoreSubstrate(Biome biome){
    switch(biome){
        case Biome::Ocean:
        case Biome::Desert:return lib::nereid::ShoreSoil::Sand;
        case Biome::Wetland:return lib::nereid::ShoreSoil::Silt;
        case Biome::Tundra:return lib::nereid::ShoreSoil::Clay;
        case Biome::Alpine:return lib::nereid::ShoreSoil::Rock;
        case Biome::Grassland:
        case Biome::Forest:return lib::nereid::ShoreSoil::Loam;
    }
    throw std::invalid_argument("TerraWeave unrecognized shoreline biome");
}

namespace shore_detail {
inline int64_t cm(Fixed y){
    if(y.raw>std::numeric_limits<int64_t>::max()/100 ||
       y.raw<std::numeric_limits<int64_t>::min()/100)
        throw std::out_of_range("NEREID water elevation outside TerraWeave shoreline range");
    const int64_t scaled=y.raw*100;
    return scaled>=0 ? (scaled+Fixed::kOne/2)/Fixed::kOne
                     : (scaled-Fixed::kOne/2)/Fixed::kOne;
}
inline Vec3 position(int64_t x,int64_t z,int32_t groundCm){
    return {Fixed::fromRatio(x,100),
            Fixed::fromRatio(groundCm,100),
            Fixed::fromRatio(z,100)};
}
struct WetSample {
    bool wet=false;
    int32_t heightCm=0;
    lib::nereid::WaterSample sample{};
    Vec3 at{};
};
} // namespace shore_detail

// Cheap bounded rasterization only when the water/soil coastline topology
// changed; do NOT run a 16k-cell water broadphase on every graphics frame.
// WaterQueryIndex is built just once and retains the completed World read
// snapshot. The winner is NEREID's normal priority/geometry/current policy.
inline std::vector<AutoShorelineBank> discoverNereidShoreline(
        const World& waterWorld,const EarthworksLayer& earth,
        ShorelineRasterSettings cfg,
        ShorelineSoilAt soilAt={},
        ShorelineRootsAt rootsAt={}) {
    if(cfg.area.minX>=cfg.area.maxX ||
       cfg.area.minZ>=cfg.area.maxZ ||
       cfg.splashHeightCm<0 || cfg.splashHeightCm>500 ||
       cfg.maxWaterDepthCm<1 || cfg.maxWaterDepthCm>50000 ||
       cfg.minWetBedDepthCm<0 || cfg.minWetBedDepthCm>5000 ||
       (cfg.includeWetBedCells &&
        cfg.minWetBedDepthCm>cfg.maxWaterDepthCm) ||
       cfg.waveEnergyPermille>1000 ||
       cfg.minContactPermille==0 || cfg.minContactPermille>1000 ||
       cfg.maxBankCells==0 || cfg.maxBankCells>16384)
        throw std::invalid_argument("TerraWeave shoreline raster config");

    const int64_t spacing=earth.spacingCm();
    const int64_t margin=spacing*2;
    if(cfg.area.minX < -kWorldLimitCm+margin ||
       cfg.area.minZ < -kWorldLimitCm+margin ||
       cfg.area.maxX > kWorldLimitCm-margin ||
       cfg.area.maxZ > kWorldLimitCm-margin)
        throw std::out_of_range("TerraWeave shoreline grid near world edge");
    const int64_t minX=detail::floorDiv(cfg.area.minX+spacing-1,spacing);
    const int64_t minZ=detail::floorDiv(cfg.area.minZ+spacing-1,spacing);
    const int64_t maxX=detail::floorDiv(cfg.area.maxX-1,spacing);
    const int64_t maxZ=detail::floorDiv(cfg.area.maxZ-1,spacing);
    if(maxX<minX || maxZ<minZ)return {};
    const int64_t width=maxX-minX+1,depth=maxZ-minZ+1;
    if(width>cfg.maxBankCells || depth>cfg.maxBankCells ||
       width*depth>cfg.maxBankCells)
        throw std::length_error("TerraWeave shoreline raster candidate budget");

    const lib::nereid::WaterQueryIndex water(waterWorld);
    const auto sample=[&](EarthVertex vertex)->shore_detail::WetSample{
        const int64_t x=vertex.x*spacing,z=vertex.z*spacing;
        const int32_t height=earth.heightAt(vertex);
        const Vec3 at=shore_detail::position(x,z,height);
        const auto v=water.sample(at);
        bool wet=false;
        if(v.found){
            const int64_t surface=shore_detail::cm(v.surfaceY);
            const int64_t bottom=shore_detail::cm(v.bottomY);
            wet=int64_t(height)<=surface &&
                int64_t(height)>=bottom-cfg.maxWaterDepthCm;
        }
        return {wet,height,v,at};
    };
    const auto floorWet=[](const shore_detail::WetSample& wet)->bool{
        return wet.wet;
    };
    // Deterministic direction order and first matching wet-neighbour tie:
    // moving NEREID water geometry cannot change results by ECS pool order.
    static constexpr int delta[4][2]={{-1,0},{0,-1},{0,1},{1,0}};
    std::vector<AutoShorelineBank> result;
    result.reserve(std::min<int64_t>(width*depth,256));
    for(int64_t z=minZ;z<=maxZ;++z)for(int64_t x=minX;x<=maxX;++x){
        const EarthVertex key{x,z};
        const auto at=sample(key);
        std::optional<shore_detail::WetSample> matchedWet;
        int64_t steepest=0;
        for(const auto& d:delta){
            const auto adjacent=sample({x+d[0],z+d[1]});
            if(floorWet(adjacent)==floorWet(at))continue;
            const auto& wet=at.wet?at:adjacent;
            if(!wet.sample.found)continue;
            // The bank must lie within a real water level splash band,
            // never simply be below sea level elsewhere in the world.
            const int64_t waterLevel=shore_detail::cm(wet.sample.surfaceY);
            if(int64_t(at.heightCm)>waterLevel+cfg.splashHeightCm)
                continue;
            const int64_t grade=std::abs(
                int64_t(at.heightCm)-adjacent.heightCm);
            if(!matchedWet || grade>steepest){
                matchedWet=wet;
                steepest=grade;
            }
        }
        if(!matchedWet && cfg.includeWetBedCells && at.wet) {
            // Interior wet-bed cells carry NEREID sediment through broad
            // rivers and estuaries. Without them transport can only follow
            // the narrow outer shore and cannot form central sandbars.
            const int64_t depth=shore_detail::cm(
                at.sample.surfaceY)-at.heightCm;
            if(depth>=cfg.minWetBedDepthCm &&
               depth<=cfg.maxWaterDepthCm)
                matchedWet=at;
        }
        if(!matchedWet)continue;
        const int64_t level=shore_detail::cm(
            matchedWet->sample.surfaceY);
        const int64_t above=std::max<int64_t>(
            0,int64_t(at.heightCm)-level);
        const uint32_t contact=cfg.splashHeightCm==0 ?
            (above?0u:1000u) : uint32_t(
            1000u-std::min<int64_t>(
                1000,above*1000/cfg.splashHeightCm));
        if(contact<cfg.minContactPermille)continue;
        const auto substrate=soilAt?soilAt(key,at.heightCm) :
            lib::nereid::ShoreSoil::Loam;
        (void)lib::nereid::shoreCriticalSpeedCmPerSec(substrate);
        const uint32_t rootBinding=rootsAt?rootsAt(key):0;
        if(rootBinding>1000)
            throw std::invalid_argument("TerraWeave invalid bank root binding");
        if(result.size()>=cfg.maxBankCells)
            throw std::length_error("TerraWeave detected shoreline bank budget");
        result.push_back({
            key,matchedWet->at,substrate,contact,
            uint32_t(std::min<int64_t>(8000,
                steepest*1000/spacing)),
            cfg.waveEnergyPermille,rootBinding,
            at.heightCm,earth.bedrockCm()
        });
    }
    return result;
}

// Persistence tag differentiates generated banks from deliberately authored
// NEREID bank cells. Auto refresh NEVER modifies manually authored banks.
struct AutoShorelineBankComponent {
    uint32_t generatorVersion=1;
};
inline void io(Archive& ar,AutoShorelineBankComponent& c){
    ar(c.generatorVersion);
}
inline void registerAutoShorelineBankPersistence(Simulation& sim){
    sim.registerPersistentComponent<AutoShorelineBankComponent>(
        "terraweave.auto_bank",1);
}

struct ShorelineRefreshStats {
    uint32_t created=0,updated=0,disabled=0,manualSkipped=0;
};

// Native structural-write barrier (NOT inside WaterQueryIndex read phase).
// Existing cells retain sediment carry/suspension; missing cells become
// dormant instead of deleting sediment when the water boundary moves.
// Max 16k auto bank ECS entities. Other game systems own their lifecycle.
inline ShorelineRefreshStats refreshAutoShorelineBanks(
        World& world,const std::vector<AutoShorelineBank>& discovered) {
    if(discovered.size()>16384u)
        throw std::length_error("TerraWeave auto-bank discovery budget");
    std::map<EarthVertex,Entity> known;
    std::map<EarthVertex,Entity> occupied;
    world.eachExistingConst<BankEarthVertexComponent>(
        [&](Entity entity,const BankEarthVertexComponent& cell){
            if(!occupied.emplace(cell.vertex,entity).second)
                throw std::invalid_argument(
                    "TerraWeave duplicate manual/automatic bank vertex");
        });
    world.eachExistingConst<AutoShorelineBankComponent>(
        [&](Entity entity,const AutoShorelineBankComponent& mark){
            if(mark.generatorVersion!=1)
                throw std::invalid_argument("TerraWeave unknown auto-bank schema");
            const auto* vertex=world.tryGetConst<BankEarthVertexComponent>(entity);
            if(!vertex || !known.emplace(vertex->vertex,entity).second)
                throw std::invalid_argument(
                    "TerraWeave malformed duplicate automatic bank identity");
        });
    if(known.size()>16384u)
        throw std::length_error("TerraWeave saved automatic bank budget");

    std::map<EarthVertex,const AutoShorelineBank*> requested;
    for(const auto& bank:discovered){
        (void)lib::nereid::shoreCriticalSpeedCmPerSec(bank.substrate);
        if(bank.wetContactPermille>1000u ||
           bank.bankSlopePermille>8000u ||
           bank.waveEnergyPermille>1000u ||
           bank.rootBindingPermille>1000u ||
           bank.groundCm<bank.bedrockCm ||
           !requested.emplace(bank.vertex,&bank).second)
            throw std::invalid_argument("TerraWeave invalid/duplicate automatic bank");
    }
    if(known.size()+requested.size()>32768u)
        throw std::length_error("TerraWeave automatic bank capacity");

    // World::pool<T>() detaches shared COW pool ownership, while the
    // subsequent Pool::tryGet() detaches the individual payload page.
    // prepareExistingWrite<T>() ALONE does not detach pool ownership.
    auto* soilPool=&world.pool<
        lib::nereid::WaterBankSoilComponent>();
    auto* probePool=&world.pool<
        lib::nereid::WaterProbeComponent>();
    // Validate ALL existing bank data before any mutation; preserve both
    // native authored bank entities and previously carried sediment.
    for(const auto& [vertex,record]:requested){
        const auto it=known.find(vertex);
        if(it==known.end())continue;
        const Entity entity=it->second;
        const auto* soil=world.tryGetConst<
            lib::nereid::WaterBankSoilComponent>(entity);
        const auto* probe=world.tryGetConst<
            lib::nereid::WaterProbeComponent>(entity);
        if(!soil || !probe ||
           !world.has<lib::nereid::WaterContactComponent>(entity) ||
           !world.has<lib::nereid::WaterBankErosionOutputComponent>(entity) ||
           soil->soil.groundCm!=record->groundCm)
            throw std::invalid_argument(
                "TerraWeave auto-bank ECS stale/incomplete before refresh");
    }
    ShorelineRefreshStats stats;
    for(const auto& [vertex,entity]:known){
        if(requested.contains(vertex))continue;
        if(auto* soil=soilPool?soilPool->tryGet(entity.index):nullptr){
            soil->enabled=false;
            soil->wetContactPermille=0;
            ++stats.disabled;
        }
    }
    for(const auto& [vertex,record]:requested){
        auto existing=known.find(vertex);
        if(existing==known.end()) {
            // Never duplicate or modify a manually authored bank at this
            // vertex: it remains the authoritative soil/water owner.
            if(occupied.contains(vertex)){
                ++stats.manualSkipped;
                continue;
            }
            lib::nereid::WaterBankSoilComponent soil{};
            soil.soil.groundCm=record->groundCm;
            soil.soil.bedrockCm=record->bedrockCm;
            soil.soil.soil=record->substrate;
            soil.soil.rootBindingPermille=record->rootBindingPermille;
            soil.wetContactPermille=record->wetContactPermille;
            soil.waveEnergyPermille=record->waveEnergyPermille;
            soil.bankSlopePermille=record->bankSlopePermille;
            const Entity entity=world.create();
            world.add<BankEarthVertexComponent>(
                entity,BankEarthVertexComponent{vertex});
            world.add<AutoShorelineBankComponent>(
                entity,AutoShorelineBankComponent{});
            world.add<lib::nereid::WaterProbeComponent>(
                entity,lib::nereid::WaterProbeComponent{
                    record->wetProbePosition});
            world.add<lib::nereid::WaterContactComponent>(
                entity,lib::nereid::WaterContactComponent{});
            world.add<lib::nereid::WaterBankSoilComponent>(entity,soil);
            world.add<lib::nereid::WaterBankErosionOutputComponent>(
                entity,lib::nereid::WaterBankErosionOutputComponent{});
            world.add<lib::nereid::WaterBankSedimentTransportComponent>(
                entity,lib::nereid::WaterBankSedimentTransportComponent{});
            ++stats.created;
        } else {
            const Entity entity=existing->second;
            auto* soil=soilPool?soilPool->tryGet(entity.index):nullptr;
            auto* probe=probePool?probePool->tryGet(entity.index):nullptr;
            if(!soil || !probe ||
               !world.has<lib::nereid::WaterContactComponent>(entity) ||
               !world.has<lib::nereid::WaterBankErosionOutputComponent>(entity))
                throw std::invalid_argument("TerraWeave auto-bank ECS missing components");
            if(soil->soil.groundCm!=record->groundCm)
                throw std::invalid_argument("TerraWeave bank height differs from canonical soil");
            soil->soil.soil=record->substrate;
            soil->soil.bedrockCm=record->bedrockCm;
            soil->soil.rootBindingPermille=record->rootBindingPermille;
            soil->wetContactPermille=record->wetContactPermille;
            soil->waveEnergyPermille=record->waveEnergyPermille;
            soil->bankSlopePermille=record->bankSlopePermille;
            soil->enabled=true;
            probe->position=record->wetProbePosition;
            if(!world.has<lib::nereid::WaterBankSedimentTransportComponent>(entity))
                world.add<lib::nereid::WaterBankSedimentTransportComponent>(
                    entity,lib::nereid::WaterBankSedimentTransportComponent{});
            ++stats.updated;
        }
    }
    return stats;
}

} // namespace aiverse::terraweave
