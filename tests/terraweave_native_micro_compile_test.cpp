// TerraWeave Stages 65–70 — compile/link smoke for the complete native
// composition, NEREID micro water and optional PHYSIS bridge include path.
// This deliberately instantiates no real simulation: the independent
// runtime integration fixtures must prove ticks/save/rollback/Box3D.
// The primary benefit is detecting broken C++/header/API integration.
#include "examples/world/terraweave_native/Integration.h"
// PhysicsIntegration owns Box3D scene handles. The base native smoke
// must compile with AIVERSE_ENABLE_PHYSICS=OFF, while the separate
// physics-enabled smoke links AIVERSE::physics and its public include
// directories (including vendored <box3d/box3d.h>).
#if defined(AIVERSE_TERRAWEAVE_SMOKE_WITH_PHYSICS)
#include "examples/world/terraweave_native/PhysicsIntegration.h"
#endif
#include "engine/world/TerraWeaveNereidMicroNutrients.h"
#include "engine/world/TerraWeavePhysisNutrientBridge.h"
#include <cassert>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>

int main() {
    using namespace aiverse;
    using namespace aiverse::terraweave;
    static_assert(ForestDeadwoodResource::kMicroWaterCellCm==400);
    static_assert(ForestDeadwoodResource::kNutrientCellCm==3200);
    static_assert(std::is_copy_constructible_v<ForestDeadwoodResource>);
    static_assert(std::is_default_constructible_v<ForestMicroOutcome>);
    ForestDeadwoodResource saved;
    assert(saved.version==3 && saved.microWater.empty());
    assert(deadwoodLedgerBalanced(saved));
    const ForestMicroKey one{1,0,0};
    const std::pair<int64_t,int64_t> expected{0,0};
    assert(microToCoarse(one)==expected);
    // TerraWeave-only Linux build must compile without the PHYSIS
    // header from another agent's feature branch. Native opt-in checks
    // weather availability explicitly and never invents climate state.
    (void)terraweavePhysisWeatherAvailable();
    ForestMicroSettings cfg;
    validateForestMicroSettings(cfg);
    // Out-of-range NEREID mouth positions must fail BEFORE raw Fixed
    // -> centimetre scaling; no water query is permitted for bad input.
    const ForestMicroWaterSampler neverQuery=[](int64_t,int64_t,uint64_t)
        -> ForestMicroSample {
        throw std::logic_error("TerraWeave out-of-world mouth queried water");
    };
    ForestNereidMicroJunctionSettings mouthCfg;
    assert(!nearestNereidMicroCell(
        {Fixed::fromRaw(std::numeric_limits<int64_t>::max()),
         Fixed{},Fixed{}},1,neverQuery,mouthCfg));
    assert(!nearestNereidMicroCell(
        {Fixed::fromRaw(std::numeric_limits<int64_t>::min()),
         Fixed{},Fixed{}},1,neverQuery,mouthCfg));
    assert(!nearestNereidMicroCell(
        {Fixed{},Fixed{},Fixed::fromRaw(
            std::numeric_limits<int64_t>::max())},
        1,neverQuery,mouthCfg));
    // Exercise the other public entry point directly. Empty ECS has no
    // NEREID water bodies: extreme 4m indices must return dry *before*
    // Earthworks or the water index is sampled or integer-scaled.
    World emptyWorld;
    const EarthworksLayer terrain(400,
        [](int64_t,int64_t)->int32_t{return 0;});
    const lib::nereid::WaterQueryIndex waterIndex(emptyWorld);
    const auto emptyRiver=makeNereidMicroWaterSampler(terrain,waterIndex);
    assert(!emptyRiver(std::numeric_limits<int64_t>::max(),0,0).submerged);
    assert(!emptyRiver(std::numeric_limits<int64_t>::min(),0,0).submerged);
    assert(!emptyRiver(0,std::numeric_limits<int64_t>::max(),0).submerged);
    assert(!emptyRiver(0,std::numeric_limits<int64_t>::min(),0).submerged);
    assert(!emptyRiver(-1,-1,0).submerged);
    // Direct public helper calls must validate bounded search settings,
    // a nonzero body ID and a present sampler, even without the native
    // junction planner. Invalid requests must not invoke waterAt.
    const Vec3 origin{};
    bool rejectedSearch=false;
    ForestNereidMicroJunctionSettings badSearch=mouthCfg;
    badSearch.searchRadiusFineCells=std::numeric_limits<uint32_t>::max();
    try { (void)nearestNereidMicroCell(
        origin,1,neverQuery,badSearch); }
    catch(const std::invalid_argument&){rejectedSearch=true;}
    assert(rejectedSearch);
    badSearch=mouthCfg;
    badSearch.searchRadiusFineCells=0;
    rejectedSearch=false;
    try { (void)nearestNereidMicroCell(
        origin,1,neverQuery,badSearch); }
    catch(const std::invalid_argument&){rejectedSearch=true;}
    assert(rejectedSearch);
    rejectedSearch=false;
    try { (void)nearestNereidMicroCell(
        origin,0,neverQuery,mouthCfg); }
    catch(const std::invalid_argument&){rejectedSearch=true;}
    assert(rejectedSearch);
    rejectedSearch=false;
    try { (void)nearestNereidMicroCell(
        origin,1,ForestMicroWaterSampler{},mouthCfg); }
    catch(const std::invalid_argument&){rejectedSearch=true;}
    assert(rejectedSearch);
    // Normal negative coordinates must still discover the nearest
    // correct body, with deterministic signed lattice floor.
    const ForestMicroWaterSampler wetNegative=[](int64_t x,int64_t z,
                                                   uint64_t expected) {
        if(x==-1 && z==-1 && expected==7)
            return ForestMicroSample{7,true,0,0};
        return ForestMicroSample{};
    };
    const auto nearNegative=nearestNereidMicroCell(
        {Fixed::fromInt(-2),Fixed{},Fixed::fromInt(-2)},
        7,wetNegative,mouthCfg);
    assert(nearNegative.has_value());
    assert(nearNegative->bodyKey==7 &&
           nearNegative->x==-1 && nearNegative->z==-1);
#if defined(AIVERSE_TERRAWEAVE_SMOKE_WITH_PHYSICS)
    // A source-facing regression for the exact Clang precedence warning:
    // zero/zero is invalid, either nonzero budget is allowed, and
    // the default budget is valid. No physics Scene construction needed.
    FlowObstructionCapturePolicy policy;
    validateFlowObstructionCapture(policy);
    policy.maxFallenTrees=0;
    policy.maxSlopeDebris=0;
    bool rejected=false;
    try { validateFlowObstructionCapture(policy); }
    catch(const std::invalid_argument&) { rejected=true; }
    assert(rejected);
    policy.maxSlopeDebris=1;
    validateFlowObstructionCapture(policy);
    policy.maxFallenTrees=129;
    rejected=false;
    try { validateFlowObstructionCapture(policy); }
    catch(const std::invalid_argument&) { rejected=true; }
    assert(rejected);
#endif
    return 0;
}
