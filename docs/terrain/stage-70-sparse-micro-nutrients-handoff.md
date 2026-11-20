# TerraWeave Stage70 — sparse 4m NEREID-body nutrients + lossless v1/v2 migration

**Date:** 2026-11-19. **Branch:** `feature/terraweave` (Draft PR #24).
**Workflow:** features-first development, then one separate integrated QA pass.
The previous stage's [Stage69 handoff](stage-69-nereid-physis-nutrient-handoff.md)
and [Stage68 handoff](stage-68-nutrient-circulation-handoff.md) are prerequisites.

## Goal and architectural decision

The v2 `ForestDeadwoodResource.water` represented **one** dissolved
amount per 32m tile without any water-body identifier. Two NEREID rivers
within a 32m cell could not be distinguished, so Stage69 intentionally
rejected some real confluences. Stage70 adds a **sparse 4m persistent
water subgrid** with the actual NEREID generation-safe water-body ID,
without changing the existing authoritative 32m **soil** grid.
This is fine-grained *nutrient transport*, not a second water solver.

- New saved `ForestMicroWaterCell{bodyKey,x,z,dissolvedUnits}`; `x,z`
  are signed **4m** tile coordinates, and `bodyKey` is encoded from the
  generation-safe NEREID ECS `Entity::packed()+1` (0 is invalid).
- `ForestDeadwoodResource` now has `version=3`,
  `microWater`, `microWaterCursor`, `kMicroWaterCellCm=400` and
  `kMaxMicroWaterCells=65536`. Sorted order is
  `(bodyKey,x,z)`; separate bodies at identical 4m XZ may have
  distinct records. Existing `soil` remains **3200cm / 32m**.
- Version 1 and version 2 old dissolved water records remain in
  the **legacy coarse water array** after migration; no mass
  reclassification or auto-split. All saved coarse units continue
  participating in the exact global conservation ledger but are
  *frozen* in Stage70 micro mode until explicitly allocated.
- **No** per-grass-blade ECS entities or per-river standalone water
  simulations. NEREID remains authoritative for body geometry,
  flood/water levels and current. PHYSIS still owns weather.

## Exact source commits and changed files

1. `3cf2617b6c5ce3082299a6bceca2f9f8db593e0f` —
   `engine/world/TerraWeaveForestDeadwood.h`: append new body-tagged
   4m water records and cursor to v3 persistent archive. Every old
   v1 or v2 record is decoded with its historic wire layout, then
   promoted to v3 while preserving all 32m water units unassigned.
   The conservation validator now counts both coarse legacy and
   fine body-tagged water, with strict bounds/ordering and 65k caps.
2. `cb07c929afcaefb9d7e23c0aaac67ee34aecb142` —
   `engine/world/TerraWeaveForestDeadwoodEcs.h`: native
   `Simulation::registerPersistentResource` is now schema **3**;
   registers exact native resource migrations **1→3 and 2→3**
   before registry sealing. Existing Stage67/68 APIs continue to
   use this one authoritative resource.
3. `9b2315ad283d93262ca30443515f6b83587e8715` —
   C++20 `<compare>` include for the saved fine-cell ID.
4. `5b8c1e5249cbdce4156ef9ac0e8619b894f5ce94` —
   NEW `engine/world/TerraWeaveForestMicroNutrients.h`:
   `ForestMicroKey`, `ForestMicroSettings`,
   `advanceForestMicroNutrients`,
   `claimForestLegacyWater` and
   `ForestMicroOutcome`. The pure fixed-tick kernel samples
   32m soil plant demand, bounded rain/mineral leaching into
   actually wet 4m NEREID-tagged cells, cardinal 4m current advection
   and directed cross-body confluence/cascade links. It processes
   bounded CPU slices and publishes one copy/swap ledger transaction.
5. `2e30e73308fefd71b7bdde5a15d894c896bbfdc9` —
   NEW `engine/world/TerraWeaveNereidMicroNutrients.h`:
   read-only NEREID/Earthworks 4m water sampler, authentic
   `WaterConfluenceComponent` + `WaterCascadeConnectionsComponent`
   topology, near-mouth fine tile selection, explicit verified
   legacy-coarse claim, and opt-in ECS system with a single World COW
   authoritative publication per due tick.
6. `5122254baac96afe3245445f1afabb82ee42dd76` —
   `examples/world/terraweave_native/Integration.h`:
   new trailing opt-ins `enableFourMetreNutrients=false`,
   `ForestMicroSettings`, `ForestNereidMicroJunctionSettings`
   and `onMicroNutrients`. When enabled, install ONLY the Stage70
   micro system; DO NOT simultaneously install Stage68 coarse one.
   Old worlds retain coarse mode by default. Both use one persistent
   resource and the same PHYSIS weather callback.
7. `3213531050e82149e9b346f88e9912d61d0c0c2a` —
   pure native-independent body-tagged 4m outlet
   `exportForestMicroDissolvedNutrients`; monotonic
   `lastOutflowCommandId` shares the Stage68 coarse outlet receipt,
   so a duplicated network message cannot export twice.
   Also explicit standard `<iterator>` include.
8. `819202e7c6694ae236bc43a9d4a631b99e092cfd` —
   `TerraWeaveNereidMicroNutrients.h`:
   `exportForestNereidMicroOutletEcs` verifies live body ID and
   wetness at the saved 4m point, then stages the receipt and source
   debit through World COW. Authoring of a *real outlet* remains the
   game's responsibility, not inferred from a map edge.
9. `92fefce9538805888011e4c6b95f712fa1cb152d` —
   NEW `tests/terraweave_micro_nutrient_test.cpp`:
   test fixtures covering legacy v2 archive read to v3, invalid split
   rollback, two distinct body IDs within one old coarse cell,
   fine advection, no same-tick multi-hop, authored outlet replay,
   ledger equality and rejection of invalid links/ticks.
10. `aec46b27d6362258eec89668424cc3faabc1326f` —
    `CMakeLists.txt`: register new headless CTest target
    `aiverse_terraweave_micro_nutrient_test` /
    `terraweave_micro_nutrient` with C++ assertions enabled.

**Important:** The ten records above document COMMITTED SOURCE only.
There has been NO observed successful compiler, CTest, integration
runtime, save migration or real GPU performance result.

## Public API and exact units

- `ForestMicroKey{bodyKey,x,z}`: signed **4m** XZ tile indices.
- `microToCoarse(key)`: floor signed `x/8, z/8`, covering one
  32m soil/legacy water tile even at negative world coordinates.
- `ForestMicroWaterSampler(x4,z4,expectedBodyKey)` returns actual
  NEREID identity, submerged state and Fixed-quantized current in
  **cm/second**, computed at the saved Earthworks ground.
  Expected body 0 means discover the actual NEREID body at that tile.
  The native adapter rejects absent or different body samples.
- `ForestMicroSoilSampler(coarseX,coarseZ)`: integer PHYSIS rain
  intensity, soil moisture and bounded rooted+grass PATCH demand.
  The native host uses Stage68's authoritative
  `prepareForestNutrientDemand`, not cosmetic forest instances.
- `advanceForestMicroNutrients(resource,tick,soilFn,waterFn,
  settings,links)`: one scheduled, deterministic pure CPU pass.
  Default 256 32m soil cells + 256 4m dissolved cells, once each
  256 simulation ticks, with <=64 actual NEREID junction links.
  Micro nutrient records are sparse; only wet deposits/transported
  amounts get a saved fine tile. No full 4m dense world grid.
- Soil uptake removes real nutrient units to persisted
  `totalAbsorbedUnits`; rain leaching debits soil and credits a
  NEREID-verified 4m cell; transport uses ONE cardinal 4m hop and
  optional NEREID junction edge. The original pre-step dissolved
  snapshot is the maximum per-tile send budget: newly received
  nutrients cannot hop again until the next due tick. Every move is
  exactly conserved in the existing global organic ledger.
- NEREID links are directed from real confluence inlet to outlet
  or cascade inlet to outlet. Each body is checked against live
  `WaterQueryIndex` / the current Earthworks height at tile
  center. Fail closed if no valid wet sample, disabled body, stale
  entity generation or missing linked ECS components.
- `claimForestLegacyWater` accepts explicit full-cell
  `ForestLegacyMicroClaim{coarseX,coarseZ,allocations}`.
  All allocations must have unique nonzero body-tagged 4m keys
  within the original **same** coarse cell, and sum to the
  ENTIRE coarse saved amount. A native verifier checks every
  allocation against live NEREID wetness and body ID.
  Claims are atomic, limited to 256 coarse cells/command and 64
  fine allocations per coarse tile. Ambiguous old records are kept
  coarse rather than guessing how the two rivers divided the amount.
- `claimForestLegacyNereidWaterEcs` performs this check and atomic
  native World update.
- `exportForestNereidMicroOutletEcs` is a native authoritative
  outlet command. Its global monotonic receipt prevents duplicate
  replay and preserves the existing `totalRunoffExportUnits` sink.

## Determinism, limits and compatibility

Persistent `ForestDeadwoodResource` remains the sole organic matter
authority. `validateForestDeadwoodResource` verifies:

```
input organic units == remaining logs + soil + coarse legacy water
                       + micro water + absorbed + respired
                       + harvested/exported + runoff export
```

`totalLeachedUnits` remains telemetry only, NOT a second sink.
Stage70 never credits both 32m coarse and 4m micro water for one
rain-leached amount. Micro mode does not touch old coarse water.
The old coarse scheduled ECS system is never installed beside the
new micro system in the native composition helper. The renderer
uses the existing Arbor and Box3D paths unchanged.

**Caveats:** A `WaterQueryIndex` sample selects an authoritative
covering water body; overlapping water bodies within the **same
4m tile** may not both be independently sampled, although v3 has
storage for different per-body identities there. This implementation
supports two streams in separate 4m tiles even if both reside within
the same old 32m tile. Small creeks narrower than a 4m center sample
can still be missed. NEREID river mass is not transported by this
module; only the separately accounted nutrient tracer is moved.
The soil grid remains 32m and root/grass demand is aggregated by
the host, avoiding one ECS entity per grass blade.

**Other limitations:** 4m saved water tiles cannot represent vertical
water strata, sub-cell mixing, sediment chemistry or fractional
hydraulic parcel tracking. A 65k record cap still applies; each
due step builds a sorted map from resident micro entries (O(N)
setup) and then processes bounded 256 source tiles; this is NOT
an O(1) full-grid update nor a measured performance claim.
A project moving from a previously *absent* persistent deadwood
resource to opt-in v3 needs game-level schema-count migration;
the v1/v2→v3 resource migrations only cover worlds that already
registered the same resource ID.

## Actual QA evidence and next agent acceptance

**Actually done:** GitHub source reads of Stage68/69 water, ecology,
archive, NEREID adapter and native example; connected GitHub source
commits for v3, new pure/native 4m kernels and regression test
fixtures; CMake target registration. No local checkout was available
in this runtime.

**NOT RUN:** CMake configure, AppleClang/GCC/MSVC builds, CTest,
the new Stage70 unit test, save/rollback/real persistent registry
migration, NEREID real confluence and cascade ECS simulation,
PHYSIS+TerraWeave integrated build, Box3D, full editor/game launch,
GPU profiling, or Metal/GL/Vulkan screenshots. The test SOURCE
exists but its pass status is **unknown**.

### Required independent Mac Studio QA commands

```bash
cd ~/Projects/AIVERSE-terraweave-qa
git fetch origin feature/terraweave
git switch feature/terraweave
git pull --ff-only origin feature/terraweave
cmake -S . -B build/terraweave-qa -DAIVERSE_BUILD_TESTS=ON
cmake --build build/terraweave-qa --parallel 4 \
  --target aiverse_terraweave_micro_nutrient_test
ctest --test-dir build/terraweave-qa --output-on-failure \
  -R '^terraweave_micro_nutrient$'
```

Then add native ECS integration fixture using real
`WaterBodyComponent`, two bodies within one 32m cell and a valid
confluence or cascade mouth; test reversible World COW rollback,
duplicate outlet command, invalid legacy claim rollback, exact
v1/v2→v3 native resource migration, 65k memory/cursor performance,
negative coordinates and partial dying shoreline.
Verify Stage68 coarse compatibility and Stage44–69 previously
deferred tests before allowing PR #24 to merge.
If the first compiler or CTest error occurs, record exact
diagnostics and the repaired commit in a NEW QA handoff.

## Next feature stage

Stage71 candidate: true per-body sub-cell wet fraction/flow weighting,
better multi-inlet mass diffusion and native editor visualization of
sparse 4m dissolved nutrient cells. Only then consider 1m grid
for specific waterfalls/streams; do not introduce another global
dense 1m world allocation or change the existing NEREID/PHYSIS
ownership boundaries.

**Mandatory per-stage rule:** new commits, changed files, exact API,
actual tests/not run, limitations, branch HEAD and next step must be
recorded in another standalone handoff.


## Final stage source update and branch status

- `76ae95aacf67f90767c6cc164a97d8721789622d` —
  `examples/world/terraweave_native/README.md` documents opt-in
  `enableFourMetreNutrients`, the v3 resource migration contract,
  explicit NEREID-vetted full-cell legacy distribution, native
  micro outflow, and the existing authoritative PHYSIS/NEREID boundaries.
- `d4e07fa1d426d63afda61bc1a0e3e4502c30c2e7` —
  `docs/terrain/CURRENT_HANDOFF.md` now points first to Stage70
  and distinguishes authored test fixtures from executed validation.
- GitHub readback successfully fetched nonempty current source for:
  v3 `TerraWeaveForestDeadwood.h`,
  `TerraWeaveForestDeadwoodEcs.h`,
  `TerraWeaveForestMicroNutrients.h`,
  `TerraWeaveNereidMicroNutrients.h`,
  native `Integration.h`,
  the Stage70 regression fixture,
  `CURRENT_HANDOFF.md` and this handoff.
  This verifies files were written, not that C++ builds.
- Last observed remote feature head before this appended document:
  `d4e07fa1d426d63afda61bc1a0e3e4502c30c2e7`.
  `feature/terraweave` is **929 ahead / 305 behind** `main`
  (diverged); GitHub PR #24 remained open, Draft, nonmergeable.
  No merge/rebase was performed.
- Actual CMake configure, native compile, CTest execution, v1/v2-v3
  live ECS migration, PHYSIS/Box3D runtime and GPU QA are STILL NOT RUN.
  The first independent QA action remains the exact Stage70 test
  command above, followed by native NEREID and full-suite checks.

**Risk to prioritize in QA:** The 4m NEREID bridge uses the body's
highest-priority `WaterQueryIndex` surface at each fine-cell center.
Its v3 archive is capable of storing separate overlapping body keys,
but a secondary water body fully concealed by another at the
same exact 4m center is conservatively not advanced; test this
deliberately and do not call the feature a volumetric water chemistry
solution. Source implementation is a bounded sparse gameplay tracer.
