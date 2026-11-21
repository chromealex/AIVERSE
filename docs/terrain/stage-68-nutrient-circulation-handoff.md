# TerraWeave Stage 68 — finite nutrients, PHYSIS climate, NEREID transport, and deadwood visual tiers

**Date:** 2026-11-19. **Branch:** `feature/terraweave`; Draft PR #24, base `main`.
**Work phase:** functions FIRST, independent QA/testing agent AFTER implementation.
**Primary goal:** close Stage67's nutrient accounting gap, model bounded
nutrient uptake and rain runoff without inventing water, consume real NEREID
flow to carry dissolved nutrient matter, accept PHYSIS weather snapshots for
decay, and tint shared Arbor fallen-tree prototypes as logs rot.

## Exact source commit log and changed paths

- `d819936f855337c8b3eddf30c4352883cc5831c2` —
  `engine/world/TerraWeaveForestDeadwood.h`: internal persisted ledger
  version **v2**, separate absorbed/runoff sinks, dissolved-water cells,
  leaching diagnostics, scheduled-cell cursors and v1 read upgrade.
- `2116f2bd9dbec2d3cdfd518985fb7012111989bc` — same:
  `ForestDeadwoodClimate` temperatureCentiC/moisturePermille,
  `ForestDeadwoodClimateSampler`, deterministic integer modifier of
  `advanceForestDeadwood` rate; no PHYSIS implementation dependency.
- `1a53b219572bbadf720ef10efc8e160ffdf516c4` — NEW
  `engine/world/TerraWeaveForestNutrientCycle.h` with
  `advanceForestNutrientCycle` and `exportForestDissolvedNutrients`.
  Per-cell plant uptake, rain/wetness leaching, same-NEREID-water-body
  cardinal advection from an immutable pre-step dissolved snapshot,
  bounded work, rollback-stable cursor and exact aggregate mass ledger.
- `109cb437a1526940c58140eb301c4eb081186990` —
  corrected local outlet cell alias and prevented overflow in transport
  by multiplying only the capped per-step source amount.
- `f598ad0f410cba004440868b175c09cff133a706` — NEW
  `engine/world/TerraWeaveForestNutrientEcs.h`: optional ECS system,
  32m soil/grass/root uptake demand, 1 completed-tick
  `WaterQueryIndex` over the authoritative terrain, native PHYSIS
  weather callback and COW resource publication.
- `df279e2df29c5e11a9afc67d2f64d62ac1d2e6ce` —
  `engine/world/TerraWeaveForestDeadwoodEcs.h`: exact
  `Simulation::registerPersistentResourceMigration` from schema 1 to
  schema 2, updated ECS decay callback for deterministic PHYSIS climate.
- `5773bc4a6306c53a69c85f0bc5bc1d4068addc76` —
  `TerraWeaveForestDeadwood.h`: original organic units per log,
  version-aware v1 record decoder without creating extra mass.
- `74524ba347b8d766ca8d08646ece79597653b372` — NEW
  `engine/world/TerraWeaveDeadwoodPresentation.h`: Fresh /
  Weathered / Rotten / Crumbling / Gone visual states from remaining
  versus initial saved wood, bounded colour tints.
- `a586fcfe6cc9f2133090d5df81242f35d268a012` —
  `TerraWeaveForestDeadwoodEcs.h`: mass baseline in large fallen
  tree intake, not only single natural mortality.
- `2c981eb3552c53e78edeaa7448687e39b4cc70ac` —
  `engine/world/TerraWeaveFallenPhysics.h`: optional immutable
  ForestDeadwoodResource pointer in existing `submit`; uses ordinary
  per-instance opaque `render::ModelDraw::tint`, shared Arbor meshes
  and the existing Box3D transform/ownership.
- `60768892ecaa1a1a75b2e27da61e28a749325a45` —
  `examples/world/terraweave_native/Integration.h`: Stage68 optional
  climate, nutrient and outcome callback configuration; registered
  after mortality and decomposition. No defaults changed.
- `c79170062b9081ea3525ed73ebbb725df8c1bc6d` —
  correct exact v1/v2 minimum record bytes (65/73) for Archive count.
- `54f43fc8b79bd47821056584da1c033193fa2edd` —
  never invent a cross-pool water-body ID for a NEREID flood-only sample.
- `e5d71eb0109da2eb9e41cef28279fb575cc64c5b` —
  direct `stdexcept` include in native integration.
- `651c4ea8c264693ab9d8208996d3d70934207a9c` —
  Stage68 v2 ledger stores `lastOutflowCommandId` for authoritative
  monotonic NEREID outlet receipts.
- `61c937eb3c07e5be4488a33a84c7ba05b87f0f99` —
  spent soil cells pruned, outlet export has monotonic nonzero command
  IDs; even zero-volume commands are recorded, duplicate/reversed
  sequences never draw future reservoir mass.
- `d3386114206ea27c7dc9be6fe530956e114b3469` —
  `TerraWeaveForestNutrientEcs.h`: `exportForestDissolvedNutrientsEcs`
  stages the outlet receipt and full organic ledger together in one World.
- `8829f6ecbb82e5fa89daacfe8ccca40b8af46f8f` —
  `examples/world/terraweave_native/README.md`: Stage68 public APIs,
  example PHYSIS callback signatures, shaderless shared fallen-tree
  tint, NEREID outlet integration and limitations.

## Exact authority model and units

**State ownership:** TerraWeave Earthworks owns terrain heights, NEREID
owns water bodies, flow, current modifiers, flood and sediment, PHYSIS
owns the weather/temperature/rainfall source, Box3D owns physical bodies,
and `ForestDeadwoodResource` owns only organic matter **in abstract
integer units** and its persistent movement through soil/dissolved pools.
No second water solver, no terrain rewrite, no GPU simulation, no
per-blade ECS records, no new special shader.

**Conservation:** `totalInitialUnits` must equal
`remainingWood + soilMineralUnits + dissolvedWaterUnits + totalRespiredUnits
+ totalExportedUnits [harvest] + totalAbsorbedUnits [plants]
+ totalRunoffExportUnits [authored river outlet]`.
`totalLeachedUnits` is **cumulative transfer telemetry**, not another
mass sink. `validateForestDeadwoodResource` rejects missing/created mass
and overflows; `deadwoodLedgerBalanced` is a nonthrowing bool wrapper.
Transactions stage the changes, validate complete mass, then replace
the authoritative ECS World/resource once.

**Archive format:** internal `ForestDeadwoodResource.version=2`;
`registerTerraWeaveForestDeadwoodPersistence` now registers schema v2
and a supported **v1→v2 persistent ECS migration** through
`Simulation::registerPersistentResourceMigration`. It decodes the
exact v1 record/soil layout, initializes new sinks/water/cursors empty,
and keeps old mass unchanged; v1 records did not store their original
wood mass, so the visual baseline migrates as `initial=remaining`.
v2 writes per-log original mass and reads it unchanged. All projects
must register migration BEFORE the registry seals. An old save with
*no deadwood resource registered at all* has a different persistent
ECS schema count; registering Stage68 in that existing game requires
a higher-level game save migration, not silently inventing schema
entries.

**Default budgets:** 65,536 deadwood IDs, 65,536 soil nutrient cells,
65,536 dissolved water cells; 32m world cells; Stage67 default decay
256 logs per 64 sim ticks. Stage68 nutrient processing defaults to
256 soil and 256 water cells per 256 sim ticks. Each soil cell can
uptake <=32 units and leach <=64 units per due step; each water cell
can advect <=128 units from its PREVIOUS dissolved snapshot. These
settings are validated. Native demand scans up to 65k interactive
rooted plants plus up to 4096 grass PATCHES once per nutrient period;
background procedural forest trees are not allocated per-agent.

**PHYSIS climate integration:** Pure `ForestDeadwoodClimate`
(`temperatureCentiC`, `moisturePermille`) adjusts decomposition rate
using capped integer arithmetic. Host `ForestNutrientWeatherSampler`
(`rainfallPermille`,`soilMoisturePermille`) controls mineral
leaching. Data must be supplied deterministically from a completed
authoritative PHYSIS/weather snapshot. This Stage68 source does NOT
include PHYSIS's active world module or silently read GPU rendering
weather; if the callbacks are omitted, neutral decay conditions
and no rainfall are used.

**NEREID transport:** On a scheduled `advanceForestNutrientCycleEcs`,
query the existing `WaterQueryIndex` at the actual TerraWeave ground
for each processed 32m nutrient cell. Use `WaterSample.body`,
`submerged` and `flow.x/.z`; the current is already altered by the
existing NEREID flow-obstruction/confluence/cascade stages. Transport
steps to one cardinal neighbouring cell, dominant current axis,
only when the destination is submerged and in the *same* canonical
NEREID water body. A dry destination, absent body or different
river body holds mass in the source. No fake global current and no
independent water/flood flow state are created. A new leach deposit
cannot advect again in the same step: water transport reads the
pre-leaching snapshot.

**Authored river outlets:** A native game can provide an explicit
NEREID outlet command via
`exportForestDissolvedNutrientsEcs(world,cellX,cellZ,amount,commandId)`
at an ECS structural/command barrier. This advances a monotonic
global command sequence and exports a bounded number of nutrients
from the dissolved reservoir into `totalRunoffExportUnits`. An
empty/duplicate command still has a deterministic recorded receipt.
The function does NOT verify NEREID water topology by itself: the
game must validate the outlet and assign ordered command IDs. It
does not mutate NEREID water volume, sediment, or permanent river
assets. Confluence transfer across distinct NEREID body IDs is
deliberately not automatic yet.

**Display/physics:** `deadwoodVisualState` reads O(log N) wood
records and derives four coarse RGB tints from
`remainingUnits/initialUnits`. `FallenPlantPhysics::submit`
accepts an optional `const ForestDeadwoodResource*` and uses the
existing Renderer3D `ModelDraw.tint` on a shared Arbor prototype.
No new shader, GPU texture, per-tree model, alpha blend pass or
separate Box3D geometry; fully spent bodies are hidden from the draw
and removable by the existing Stage67
`removeFullyDecayedFallenBodies` posttick hook.
A natural death with no fallen physics body is NOT automatically
converted to an animated log; that remains game-authoring policy.
Physics removal/obstruction/navmesh updates MUST remain on the native
Box3D owner/read barrier, never from pure ecology or water callbacks.

## Actual checks and evidence

**Performed:** Connected-GitHub source inspection of Stage67:
`TerraWeaveForestDeadwood.h`, `TerraWeaveForestDeadwoodEcs.h`,
ForestEcologyEcs, native integration, FallenPhysics, Stage64 grass
ecology; inspected NEREID's actual `WaterQueryIndex` and
`WaterSample` layout, `Archive` and native `Simulation`/
`PersistentEcsRegistry` migration APIs, source modifications and
GitHub commit receipts. Source readback of updated files is requested
at the end of the stage.

**NOT RUN:** CMake configure, AppleClang/GCC/MSVC compilation,
CTest/CI, binary migration roundtrip, 65k-cell stress, Metal/GL/Vulkan
GPU capture, Box3D step/cleanup, full PHYSIS/NEREID weather current
simulation, deterministic replay or network duplication fixtures.
No benchmark timings or passing tests are claimed. Do NOT merge PR #24
until a separate agent performs them and fixes failures.

## QA steps for a separate testing agent

From an isolated checked-out feature branch:

```bash
cd ~/Projects/AIVERSE-terraweave-qa
git fetch origin feature/terraweave
git switch feature/terraweave
git pull --ff-only origin feature/terraweave
cmake -S . -B build/terraweave-qa -DAIVERSE_BUILD_TESTS=ON
cmake --build build/terraweave-qa --parallel 4 --target aiverse_terraweave_forest_scene_test
ctest --test-dir build/terraweave-qa --output-on-failure -R '^terraweave_forest_scene$'
```

Add new dedicated Stage68 fixtures BEFORE claiming acceptance:
1. v1 old-sized record and resource roundtrip through the real
   `PersistentEcsRegistry` migration; reject corrupt payloads and
   ledger imbalance without partially replacing World.
2. Organics mass is exact at each intermediate tick for natural death,
   advection, rain leaching, uptake, harvest, authored outlet and
   rollback/replay, including 0-value exported receipt.
3. Simulate PHYSIS rain/temperature/moisture sequences with identical
   seeds and verify decay-factor bounds and same replay results.
4. NEREID river flow, obstruction deflection and same-body cell
   transport; no transport through dry, different-body or untagged
   flood-only destinations. Outflow events are monotonic and idempotent.
5. 65k nutrient cells, 1024 processing limit, negative world coordinates,
   fixed-point flow quantization, zero pool cells, overflow and corrupt
   migration. Document actual CPU cost/allocations (budgeted scanning
   is NOT O(1) in total rooted population).
6. Existing Arbor shared prototype/fallen physics shading, all four
   decay tiers, no per-plant mesh allocation, GPU draw stats, Metal/
   GL/Vulkan screenshots, Box3D collision removal and NavMesh dirty.
7. Run all earlier Stage44–67 deferred suites, editor + native world
   smoke targets and the PR's CI. Log exact CMake/CTest results and
   first compiler error rather than extrapolate old green tests.

## Known limitations and next source stage

- 32m water/soil cells are coarse: small creek hydraulics, vertical
  strata, sediment-bound nitrogen/phosphorus, microbial food webs,
  plant-root uptake distribution and cross-water-body confluences are
  outside Stage68. This is a **gameplay nutrient tracer**, not validated
  biogeochemistry.
- Water flow transfers abstract nutrient units, never water volume.
  NEREID owns the underlying velocity and flood routing.
- No actual native PHYSIS weather binding is available in this
  feature branch; host must supply deterministic callbacks.
- The fully-spent wood ID remains a stable tombstone to reject
  duplicate natural-death/fall events, subject to the 65k log cap.
  Background scatter million trees do not create deadwood records.
- Saved v2 field layout was developed during this Stage68 source
  stage; freeze and test the final wire layout BEFORE production
  adoption. Older games lacking the resource need game-level schema
  migration.
- Material tints are coarse opaque stages, not literal voxelized
  fragmentation of the trunk or physically shrinking its collider.

**Next precise implementation task:** optional cross-body dissolved
nutrient transfers ONLY at valid NEREID confluence/cascade links,
plus local plant-uptake distribution and PHYSIS weather-source
integration when those agents' main branches become compatible.
Keep handoff updated after every stage, with actual commits/QA state.

## Integration state observed just before final documentation

At last pre-handoff check: `feature/terraweave` SHA
`8829f6ecbb82e5fa89daacfe8ccca40b8af46f8f`, about 897 commits
ahead / 305 behind `main`; PR #24 was open, Draft, nonmergeable.
Several commits were added later in this stage; always fetch current
HEAD before comparing. No merge/rebase into `main` was performed.


## Final Stage68 review and boundary fixes

- `1aada4ebe2d7b3d86c4da8dc3953406cf94f8e03` —
  new `docs/terrain/CURRENT_HANDOFF.md` stable entry point for the
  next agent; links all Stage65–68 separate handoffs and explicit
  native QA obligations.
- `e9eeb2051b55acafc6dcd3b549b49bffd382a619` —
  `TerraWeaveForestNutrientEcs.h`: a valid first ECS water body can
  have `Entity(index=0,generation=0)` and `packed()==0`.
  The bridge now encodes valid body identity as `packed()+1`,
  reserving 0 exclusively for absent/flood-only water; prevents
  accidentally disabling nutrient transport in the first river.
- `f3f283f05dcb31170aef18f35c2d9b0b0a3534d7` —
  `TerraWeaveForestNutrientCycle.h`: reject nutrient completed ticks
  older than the already published deadwood decay tick, not just
  earlier nutrient-cycle ticks. Protects replay/order consistency.

**Source inspections completed:** fetched the current v2 archive/resource,
the final native `NereidQueryIndex` adapter, pure nutrient-cycle transport,
existing shared `FallenPlantPhysics` and README; verified file presence,
nonempty source and staging boundaries. No local compiler/CTest/GPU
execution occurred after these fixes.

**Mandatory extra QA:** include a real NEREID body with packed Entity ID
zero, same-body neighboring cells and fixed deterministic current. Verify
advection occurs in this case; a true `kNullEntity` body still prevents
fabricated flood-pool transport. Also assert reversed cross-resource
decay/nutrient timestamps reject cleanly without mutating ECS.
