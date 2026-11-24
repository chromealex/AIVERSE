// Minimal opt-in native game composition of existing NEREID ECS water,
// TerraWeave authoritative terrain/vegetation and the shared WorldScene.
// This is a reusable example header (not an engine-level genre module).
#pragma once
#include "engine/world/nereid/NereidNativeWater.h"
#include "engine/world/TerraWeaveNativeDriver.h"
#include "engine/world/TerraWeaveShorelineRaster.h"
#include "engine/world/TerraWeaveSedimentEcs.h"
#include "engine/world/TerraWeaveNereidFloodBridge.h"
#include "engine/world/TerraWeaveFloodShorelineSystem.h"
#include "engine/world/TerraWeaveShoreRecoveryEcs.h"
#include "engine/world/TerraWeaveForestEcologyEcs.h"
#include "engine/world/TerraWeaveForestHarvestEcs.h"
#include "engine/world/TerraWeaveForestDeadwoodEcs.h"
#include "engine/world/TerraWeaveForestNutrientEcs.h"
#include "engine/world/TerraWeaveNereidMicroNutrients.h"
#include "engine/world/TerraWeavePhysisNutrientBridge.h"
#include "engine/world/TerraWeaveFlowObstructionEcs.h"
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::examples::terraweave_native {

// Invoke once from GameModule::setup, before the Simulation registry seals.
// Native water owns ALL WaterContact/current/bank flow calculations.
// TerraWeave owns Earthworks soil, rooted plants and sparse grass coverage,
// persisted through the one rollback-safe ECS authority resource.
inline void registerSystems(
        Simulation& sim,
        terraweave::TerraWeaveEcsAuthorityConfig cfg,
        std::function<void(
            const terraweave::EcsBankTerrainCommit&)> onEroded={},
        bool enableSlopeFailures=true,
        terraweave::SlopeFailureSettings slopeSettings={},
        std::optional<terraweave::ShorelineRasterSettings>
            autoFloodShoreline=std::nullopt,
        bool enableEcologyRecovery=true,
        terraweave::ShoreRecoverySettings recoverySettings={},
        std::vector<terraweave::ForestEcologySpecies>
            rootedForestSpecies={},
        terraweave::ForestEcologySettings rootedForestSettings={},
        std::function<void(const terraweave::ForestEcologyEcsOutcome&)>
            onForestEcology={},
        bool enableForestHarvest=false,
        bool enableForestDeadwood=false,
        terraweave::ForestDeadwoodDecaySettings deadwoodSettings={},
        std::function<void(const terraweave::ForestDeadwoodDecayOutcome&)>
            onDeadwoodDecay={},
        terraweave::ForestDeadwoodClimateSampler deadwoodClimate={},
        bool enableForestNutrientCycle=false,
        terraweave::ForestNutrientCycleSettings nutrientSettings={},
        terraweave::ForestNutrientWeatherSampler nutrientWeather={},
        std::function<void(const terraweave::ForestNutrientCycleOutcome&)>
            onNutrientCycle={},
        terraweave::NereidNutrientJunctionSettings junctionSettings={},
        bool useNativePhysisWeather=false,
        bool enableFourMetreNutrients=false,
        terraweave::ForestMicroSettings microSettings={},
        terraweave::ForestNereidMicroJunctionSettings microJunctionSettings={},
        std::function<void(const terraweave::ForestMicroOutcome&)>
            onMicroNutrients={}) {
    // Stage68 cannot run without the versioned organic ledger. Reject
    // invalid setup BEFORE registering any ECS system or persistence type.
    if(enableForestNutrientCycle && !enableForestDeadwood)
        throw std::invalid_argument(
            "TerraWeave nutrient cycle requires forest deadwood");
    if(enableFourMetreNutrients && !enableForestNutrientCycle)
        throw std::invalid_argument(
            "TerraWeave 4m nutrient residency requires nutrient cycle");
    if(enableFourMetreNutrients){
        terraweave::validateForestMicroSettings(microSettings);
        terraweave::validateForestNereidMicroJunctionSettings(
            microJunctionSettings);
    }
    if(useNativePhysisWeather){
        if(!enableForestDeadwood)
            throw std::invalid_argument(
                "TerraWeave PHYSIS ecology needs forest deadwood enabled");
        if(!terraweave::terraweavePhysisWeatherAvailable())
            throw std::logic_error(
                "TerraWeave PHYSIS nature-sampling code is not in this build");
        // PHYSIS state is read from the SAME Simulation World at each
        // authoritative tick; it is not copied at registration time.
        // Callers can still override either specialized sampler.
        if(!deadwoodClimate)
            deadwoodClimate=terraweave::makeTerraWeavePhysisDecayClimate(
                sim.world());
        if(!nutrientWeather && enableForestNutrientCycle)
            nutrientWeather=terraweave::makeTerraWeavePhysisNutrientWeather(
                sim.world());
    }
    lib::nereid::NativeWaterPipelineConfig water{};
    water.enableBankErosion=true;
    // Geometry and current state belong to NEREID, including polygon/
    // river-spline shoreline outlines used by automatic bank detection.
    water.enableGeometry=true;
    water.enableCurrentField=true;
    // Authoritative mouth topology is needed for directed sediment transfer
    // between river/lake bodies; absent components add no runtime junctions.
    water.enableConfluences=true;
    water.enableCascades=true;
    water.enableFlowObstructions=true;
    water.enableFloodRouting=true;
    water.floodTickMs=water.bankErosionTickMs;
    // This example gives bank cells explicit WaterProbe positions. A native
    // game that uses Transform-driven probes may choose true instead.
    water.probeFromTransform=false;
    lib::nereid::registerNativeWaterPipeline(sim,"nereid",water);
    terraweave::registerTerraWeaveBankVertexPersistence(sim);
    terraweave::registerAutoShorelineBankPersistence(sim);
    if(autoFloodShoreline)
        terraweave::registerAutomaticShorelineRevisionPersistence(sim);
    terraweave::registerTerraWeaveObstructionPersistence(sim);
    terraweave::registerNereidSedimentEcsPersistence(sim);
    terraweave::registerTerraWeaveAuthorityResource(sim,cfg);
    if(enableEcologyRecovery)
        terraweave::registerShoreRecoveryPersistence(sim);
    // The rooted-plant ecology is opt-in PER SPECIES and tracks only
    // the interactable subset of trees/shrubs, never the entire million-
    // instance procedural forest or individual grass blades.
    if(!rootedForestSpecies.empty())
        terraweave::registerTerraWeaveForestEcologyPersistence(sim);
    // Persistent harvest is separately opt-in. With it enabled, native
    // games call harvestNativeForestTree at a structural/command barrier
    // and present the resulting tombstones through the existing forest.
    if(enableForestHarvest)
        terraweave::registerTerraWeaveForestHarvestPersistence(sim);
    if(enableForestDeadwood)
        terraweave::registerTerraWeaveForestDeadwoodPersistence(sim);
    // NEREID calculates local bank erosion; transport then conserves
    // suspended sediment between same-water-body neighbouring banks.
    // TerraWeave applies already computed soil changes LAST.
    terraweave::addNereidSedimentTransportSystem(
        sim,"nereid.sediment_transport",cfg.soilGridCm,
        water.bankErosionTickMs);
    terraweave::addTerraWeaveAuthoritativeBankSystem(
        sim,"terraweave.bank_commit",cfg,
        [after=std::move(onEroded)](
            const terraweave::EcsBankTerrainCommit& change) {
            // A real server may append fall events to a host-managed
            // presentation journal. NEVER step Box3D/GPU inside this ECS
            // callback or mutate ECS again. A missing sink is permitted:
            // the canonical ECS resource still contains the new fallen flags.
            if(after)after(change);
        },enableSlopeFailures,slopeSettings);
    // Refresh water-column ground heights ONLY when canonical ECS soil
    // revision changes, AFTER the bank erosion+slides commit. The next
    // NEREID flood tick sees the new bottom while preserving water volume.
    terraweave::addTerraWeaveFloodGroundSyncSystem(
        sim,"terraweave.flood_ground",cfg);
    // Only when the game supplies an explicit bounded raster region.
    // Installed last, after both NEREID and TerraWeave finished this tick.
    if(autoFloodShoreline)
        terraweave::addAutomaticNereidShorelineRefreshSystem(
            sim,"terraweave.auto_shoreline",cfg,*autoFloodShoreline);
    // A lower-frequency, deterministic ecological update, after the
    // NEREID flood and bank authoring. This never creates grass-blade ECS
    // entities and does not raise/revive fallen trees automatically.
    if(enableEcologyRecovery)
        terraweave::addTerraWeaveShoreEcologySystem(
            sim,"terraweave.shore_ecology",cfg,recoverySettings);
    // Rooted-plant mortality and recruitment are independent of grass
    // restoration; create this fixed-period ECS stage only when species
    // policies are explicitly authored by the game.
    if(!rootedForestSpecies.empty())
        terraweave::addTerraWeaveForestEcologySystem(
            sim,"terraweave.forest_ecology",cfg,
            std::move(rootedForestSpecies),rootedForestSettings,{},
            std::move(onForestEcology));
    // Stage67: natural deaths from Stage65 are seeded first, then this
    // lower-frequency bounded decomposer returns nutrients and asks the
    // external physics owner to remove fully rotted fallen bodies.
    if(enableForestDeadwood)
        terraweave::addTerraWeaveForestDeadwoodSystem(
            sim,"terraweave.forest_deadwood",deadwoodSettings,
            std::move(onDeadwoodDecay),std::move(deadwoodClimate));
    // PHYSIS weather snapshots and NEREID current are sampled only by
    // the opt-in nutrient cycle after soil/wood/ecology authority commits.
    if(enableForestNutrientCycle && enableFourMetreNutrients){
        // Exclusive with Stage68 coarse system: only ONE nutrient
        // scheduler can mutate the persistent World per simulation tick.
        terraweave::addTerraWeaveForestMicroNutrientSystem(
            sim,"terraweave.forest_nutrients_4m",cfg,
            microSettings,std::move(nutrientWeather),
            microJunctionSettings,std::move(onMicroNutrients));
    }else if(enableForestNutrientCycle){
        terraweave::addTerraWeaveForestNutrientSystem(
            sim,"terraweave.forest_nutrients",cfg,nutrientSettings,
            std::move(nutrientWeather),std::move(onNutrientCycle),
            junctionSettings);
    }
}

inline Entity addWaterBody(
        World& ecs,lib::nereid::WaterBodyComponent source) {
    if(!lib::nereid::validWaterBody(source))
        throw std::invalid_argument(
            "TerraWeave native sample requires a valid NEREID water body");
    const Entity id=ecs.create();
    ecs.add<lib::nereid::WaterBodyComponent>(id,source);
    return id;
}

// Creates one authoritative shoreline cell (NOT one entity per grass blade).
// The X/Z bank probe may be on the water side while its EarthVertex is the
// adjacent ground surface whose soil is being eroded.
inline Entity addShoreCell(
        World& ecs,terraweave::EarthVertex groundVertex,
        int32_t groundHeightCm,Vec3 waterProbePosition,
        lib::nereid::WaterBankSoilComponent source) {
    source.soil.groundCm=groundHeightCm;
    if(source.wetContactPermille>1000u ||
       source.waveEnergyPermille>1000u ||
       source.bankSlopePermille>8000u)
        throw std::invalid_argument("TerraWeave invalid shoreline contact");
    lib::nereid::validateShoreErosion(
        source.soil,{},source.settings);
    const Entity id=ecs.create();
    ecs.add<lib::nereid::WaterProbeComponent>(
        id,lib::nereid::WaterProbeComponent{waterProbePosition});
    ecs.add<lib::nereid::WaterContactComponent>(
        id,lib::nereid::WaterContactComponent{});
    ecs.add<lib::nereid::WaterBankSoilComponent>(id,source);
    ecs.add<lib::nereid::WaterBankErosionOutputComponent>(
        id,lib::nereid::WaterBankErosionOutputComponent{});
    ecs.add<lib::nereid::WaterBankSedimentTransportComponent>(
        id,lib::nereid::WaterBankSedimentTransportComponent{});
    ecs.add<terraweave::BankEarthVertexComponent>(
        id,terraweave::BankEarthVertexComponent{groundVertex});
    return id;
}

// Call only at a native structural ECS barrier, e.g. map start or after
// water-body/terrain geometry topology changes. Reads current NEREID ECS
// bodies/flows (polygon and river-spline aware), then creates/updates the
// generated bank-cell entities. Manual banks are left untouched.
inline terraweave::ShorelineRefreshStats refreshDetectedShoreline(
        World& ecs,
        const terraweave::TerraWeaveEcsAuthorityConfig& cfg,
        terraweave::ShorelineRasterSettings settings,
        terraweave::ShorelineSoilAt soilAt={},
        terraweave::ShorelineRootsAt rootsAt={}) {
    const auto* state=ecs.resourceConst<
        terraweave::TerraWeaveAuthorityResource>();
    if(!state)throw std::logic_error(
        "TerraWeave auto-shoreline requires canonical ECS terrain");
    const auto ground=terraweave::materializeEcsTerrain(*state,cfg);
    auto banks=terraweave::discoverNereidShoreline(
        ecs,ground,settings,std::move(soilAt),std::move(rootsAt));
    return terraweave::refreshAutoShorelineBanks(ecs,banks);
}

// Structural ECS barrier after authored NEREID WaterBodyComponent creation,
// shoreline detection and canonical TerraWeaveAuthorityResource setup.
// Call ONCE for the pinned map window; do not repeatedly reseed a live flood
// reservoir because that would create/destroy water rather than conserve it.
inline void seedFloodReservoir(
        World& ecs,
        const terraweave::TerraWeaveEcsAuthorityConfig& cfg,
        terraweave::NereidFloodGridRegion region){
    const auto* state=ecs.resourceConst<
        terraweave::TerraWeaveAuthorityResource>();
    if(!state)throw std::logic_error(
        "NEREID flood seed requires canonical TerraWeave soil");
    auto ground=terraweave::materializeEcsTerrain(*state,cfg);
    terraweave::initializeNereidFloodGrid(ecs,ground,region);
}

// River inlet/outlet volume is counted in the same NEREID flood ledger as
// on-grid water. Set only after seedFloodReservoir and before the first tick.
inline void configureFloodRiverPorts(
        World& ecs,
        std::vector<lib::nereid::WaterFloodPort> ports,
        uint64_t finiteExternalReserveCm){
    terraweave::configureNereidFloodBoundaryPorts(
        ecs,std::move(ports),finiteExternalReserveCm);
}

inline terraweave::NativeTerrainPublishResult publishFrame(
        const Simulation& sim,
        terraweave::NativeTerraWeaveWorldBinding& binding,
        terraweave::TerrainMutationCoordinator& terrain,
        world::WorldScene& streamedScene,gfx::Device& device,
        world::SpatialWorld& collision,nav::Mesh& navigation,
        const terraweave::EarthworksNavSource& navSource,
        std::span<const world::Cell> pinnedCollisionTiles,
        terraweave::RectCm grassArea,
        const terraweave::TerrainMutationCoordinator::ColliderIdForCell&
            collisionIds,std::string* navError=nullptr,
        bool afterRollback=false) {
    return terraweave::publishNativeTerraWeaveFrame(
        sim.world(),binding,terrain,streamedScene,device,collision,
        navigation,navSource,pinnedCollisionTiles,grassArea,collisionIds,
        navError,2u,afterRollback);
}

} // namespace aiverse::examples::terraweave_native
