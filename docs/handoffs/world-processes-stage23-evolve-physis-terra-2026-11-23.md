# AIVERSE World Processes — Stage 23: EVOLVE × PHYSIS × TerraWeave

Date: 2026-11-23. Repo chromealex/AIVERSE, branch `feature/world-processes-experimental`, draft PR #112. Main unchanged. User expressly requested to finish functions first, tests later.

## Implemented features

1. `libraries/include/aiverselib/world/EvolvePhysisBridge.h`
   - Commit `dbf97addc6e82385933bcd0125c12971a9435b3f` introduces the canonical, persistent `evolve::ClimateLink` component with a generation-safe EvolveRegion Entity reference, PHYSIS atmosphere/surface entity reference, bounded coefficients for temperature wear, rain fertility, drought stress, purification/pollution and warm/frost growth/regression.
   - `evolve::applyClimate(World&)` reads actual existing `physis::Atmosphere` and `physis::Surface` ECS components, quantizes Fixed raw ratios into integer permille and writes only the owning `evolve::Pressure` on each ClimateLink entity. Source atmosphere/weather remains owned by PHYSIS. Missing sources fail closed before state writes; stable Entity sorting and prevalidation preserve deterministic updates and rollback.
   - Commit `e2a8e5db4a0c4642b2e613b5e16acd7fd16d3d0f` hardens against invalid linked source pools and extreme Fixed numeric magnitudes before scaling.
   - `evolve::registerClimatePersistence(Simulation&)` installs schema `evolve.climate_link` v1; `evolve::installClimateBridge` schedules climate pressure updates **before** the normal `evolve.succession` evolution tick.
   - `WorldProcessesSuite.h` scheduling/persistence commit `173346fcc7f55f7f11f805672f782eb8d38511c6`.

2. `examples/world/world_processes_native/EvolvePhysisHost.h`
   - Commit `776d6849d24e8caf71b1b9a0840412f5998d9739`.
   - `attachEvolveClimate(World&,producer,region,physisCell,ClimateLink,error)` accepts actual pre-existing PHYSIS and EVOLVE entities, validates ownership, prevents duplicate link/pressure installation, and creates the real canonical `ClimateLink` and `Pressure` ECS components. The native game may opt in after both ECS owners have spawned their entities. No global PHYSIS connection or second weather field is instantiated.

3. `examples/world/world_processes_native/EvolveTerraEcologyBridge.h`
   - Commit `019eb664dd3bbf702b1438f0d9ec3562adbb9a7f`.
   - `EvolvePlantRegion` maps existing region Entity identity to a bounded, half-open world-centimetre rectangle; `projectEvolutionClimate(Region,ForestEcologyClimate)` translates EVOLVE fertility/succession/pollution to limiting nutrient and light permille without ever increasing external climate owners' values.
   - `makeEvolveForestClimateSampler(const World&,zones,baseSampler)` supplies the **actual** `TerraWeaveForestEcology.h` callback type `ForestEcologyClimateSampler`, which TerraWeave already consumes in `advanceForestEcology`. Project host supplies its current PHYSIS/NEREID baseline sampler, then wraps it with this EVOLVE-aware layer. Existing TerraWeave forest authority and procedural plant state remain canonical; no new forest graph.
   - This adapter must be executed synchronously while the ECS World snapshot is stable. Do not capture World into asynchronous workers that outlive it.

## Ownership and integration order

PHYSIS advances environmental ECS sources → `evolve.physis_climate` maps samples into Pressure → `evolve.succession` updates authoritative regional state → the native TerraWeave forest owner runs its established `advanceForestEcology` with the wrapped climate callback. All outputs are derived from canonical PHYSIS/EVOLVE/TerraWeave resources; only existing APIs mutate their owner data.

The helper is opt-in, NOT automatically injected into every TerraWeave/game module. Native projects choose region-to-PHYSIS mapping and plant-region bounds; changing authored region placement requires the host to update those mappings.

## Verification

**No tests or builds executed (user-requested deferral).** GitHub accepted source commits, but no C++ compilation, CTest, sanitizer, Mac Studio/PHYSIS scene, visual TerraWeave forest capture, save+rollback or cross-platform acceptance has verified Stage23. Older Stage05 green CI is unrelated.

## Limits and exact next step

No actual tree growth/harvest component is replaced: TerraWeave ecology owns those outputs. The EVOLVE callback adds environmental pressure and growth conditions, not direct terrain remeshing or separate tree simulation. Region selection is an explicit bounded rectangle, not an automatic 3D biome segmentation.
Next: SOCIETY native NPC knowledge/quest trigger authoring, then one complete opt-in project sample and consolidated tests after all feature integrations. Keep PR draft.
