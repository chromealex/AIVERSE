// TerraWeave <-> NEREID: explicit owner-thread handoff for earth erosion.
// Authoritative water ECS state comes from NEREID. This optional native
// adapter does not duplicate a water solver and never runs on the GPU thread.
#pragma once
#include "engine/world/TerraWeaveEarthworks.h"
#include "engine/world/TerraWeaveVegetationSoil.h"
#include <aiverselib/water/NereidErosion.h>
#include <aiverselib/water/NereidShorelineSystems.h>
#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

namespace aiverse::terraweave {

struct BankErosionCommit {
    lib::nereid::ShoreErosionCell soil{};
    lib::nereid::ShoreErosionResult erosion{};
    EarthworkChange terrain{};
};

// NEREID and the terrain owner must agree on the old ground height, or
// an out-of-order tick would modify the wrong terrain generation.
// Water height is authoritative in NEREID; Earthworks stores sparse terrain
// elevation. The host should persist both snapshots in a single game tick.
inline BankErosionCommit erodeTerrainVertex(
        EarthworksLayer& layer,
        EarthVertex vertex,
        const lib::nereid::ShoreErosionCell& soil,
        const lib::nereid::ShoreErosionForcing& forcing,
        uint32_t deltaMilliseconds,
        lib::nereid::ShoreErosionSettings settings={}) {
    const int32_t before=layer.heightAt(vertex);
    if(before!=soil.groundCm)
        throw std::invalid_argument(
            "TerraWeave NEREID erosion source terrain revision mismatch");
    const auto eroded=lib::nereid::erodeShore(
        soil,forcing,deltaMilliseconds,settings);
    BankErosionCommit result;
    result.erosion=eroded;
    if(eroded.changed) {
        // Changes terrain only after the pure water calculation succeeded.
        // Host invalidates collision, navmesh, near vegetation and Horizon HLOD.
        result.terrain=layer.setHeight(vertex,eroded.cell.groundCm);
    }
    result.soil=eroded.cell;
    return result;
}

// ECS WaterBankErosionOutputComponent is authoritative and already applied
// to the ECS soil component. Do NOT call erodeShore again at the render barrier.
// Instead apply precisely its one computed ground delta after verifying
// that the terrain is still at the corresponding pre-tick elevation.
inline EarthworkChange applyEcsBankErosion(
        EarthworksLayer& layer,EarthVertex vertex,
        const lib::nereid::WaterBankErosionOutputComponent& output) {
    if(!output.changed)return {};
    if(output.removedCm && output.depositedCm)
        throw std::invalid_argument("NEREID bank output cannot cut and deposit together");
    const int64_t delta=int64_t(output.afterGroundCm)-output.beforeGroundCm;
    if(delta!=int64_t(output.depositedCm)-output.removedCm)
        throw std::invalid_argument("NEREID bank output sediment delta mismatch");
    if(layer.heightAt(vertex)!=output.beforeGroundCm)
        throw std::invalid_argument("NEREID bank output stale terrain revision");
    return layer.setHeight(vertex,output.afterGroundCm);
}

struct PlantGroundResponse {
    RootState support{};
    std::vector<ExposedRootSegment> visibleRoots;
    uint64_t fallEventId=0; // nonzero only when support crosses threshold
};

// Called by the scene/authority once per changed dirt region. The previous
// RootState is caller-owned/saved, so a fallen tree emits ONE stable
// event regardless of camera, worker scheduling, or repeated frame updates.
// The physics system consumes fallEventId and controls trunk motion,
// collisions, harvesting and network/replay. Tree roots are cosmetic output.
inline PlantGroundResponse evaluateBankPlant(
        const RootAnchor& anchor,
        const SoilHeightSampler& surface,
        std::optional<RootState> previous=std::nullopt) {
    PlantGroundResponse result;
    result.support=evaluateRootSupport(anchor,surface);
    result.visibleRoots=generateExposedRoots(anchor,result.support);
    if(result.support.uprooted &&
       (!previous || !previous->uprooted)) {
        result.fallEventId=detail::mix64(
            anchor.plantKey ^ (uint64_t(anchor.speciesId)<<32) ^
            0xfad1e110ba5e73d5ULL);
        if(result.fallEventId==0)result.fallEventId=1;
    }
    return result;
}

// No per-blade updates: a dirty ground area reduces grass coverage at most
// once per grass patch. The renderer sees one updated instance-group
// descriptor and procedurally re-derives ages/positions of survivors.
// Caller supplies the fraction of the patch whose soil was lost.
inline void disturbBankGrass(GrassPatch& patch,
                             uint32_t areaLostPermille) {
    erodeGrassCoverage(patch,areaLostPermille);
}

} // namespace aiverse::terraweave
