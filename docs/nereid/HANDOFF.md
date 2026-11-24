# NEREID — living AI-agent handoff

**Repository**: `chromealex/AIVERSE`
**NEREID branch**: `feature/nereid-water-core`
**Created from**: `feature/universal-entity-inspector-fps` (working project
branch, may advance concurrently).
**Last implementation commit**: `5dc63de21772da931c68bed02954866de186d754`
**Native build/CTest status**: NOT RUN / UNVERIFIED for stages 1 and 2.

## Stage 1 — deterministic water queries (implementation commit 8099a884)

New/changed files in implementation commit:
- `libraries/include/aiverselib/water/NereidWater.h`: ECS types,
  pure footprint policy, field-wise Archive IO.
- `libraries/include/aiverselib/water/NereidWaterSystems.h`: pure
  `sampleWater`, transform adapter, ECS sampling registration, persistent IDs.
- `tests/ecs_nereid_water_test.cpp`: shooter/strategy simulation hosts,
  ocean/lake overlap, immersion, save/load, COW, rollback.
- `libraries/catalog.json`: header package `nereid-water`.
- `CMakeLists.txt`: target and CTest `ecs_nereid_water_test`.

**No existing FPS, RTS, render, editor, physics or map files were modified.**

## How to consume

```cpp
#include <aiverselib/water/NereidWaterSystems.h>
namespace nw = aiverse::lib::nereid;

// Inside native GameModule::setup(Simulation& sim,...)
nw::registerNereidWaterPersistence(sim);
// Attach WaterBodyComponent to ECS water source Entities.
// Attach WaterProbeComponent + WaterContactComponent to query Entities.
// Attach shared Transform OR write WaterProbeComponent::position yourself.
nw::addWaterProbeFromTransformSystem(sim, "nereid.probe"); // optional
nw::addWaterSamplingSystem(sim, "nereid.sample");
```

Use CMake `AIVERSE::lib_nereid_water`, and read
`docs/nereid/ARCHITECTURE.md` plus
`skills/gameplay/aiverse-nereid-water/SKILL.md`.

## Mandatory verification (not yet performed)

```sh
cmake -S . -B build/ecs-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/ecs-test --target aiverse_ecs_nereid_water_test -j 4
ctest --test-dir build/ecs-test --output-on-failure -R '^ecs_nereid_water_test$'
```

Also run legacy `water_*` fixtures and the relevant FPS/RTS ECS tests
before connecting NEREID to existing games. Compile/link a standalone native
game module with `AIVERSE::lib_nereid_water`.

## Known limits

- Currently supports infinite, axis-aligned box and disk water footprints;
  no river spline or irregular polygon.
- Static surface and flow only. No waves, buoyancy, hydraulic solver, VFX or
  under-water rendering has been implemented.
- Overlap is O(water-body count * probe count).
- Native water components are v1; Transform/motion components must be saved
  by their own owners. The library does not change old `waterNetwork` or
  the `~` tile bake.
- Existing `WaterKind` enum values represent semantic kinds, not feature
  completeness.
- If build/test failure occurs, fix it first and document exact command/result.

## Next agent action

1. Read `AGENTS.md`, `docs/ecs-mechanic-composition.md`,
   `docs/water.md` and `docs/nereid/ARCHITECTURE.md`.
2. Configure/build/run new NEREID test and fix any compile/runtime failure.
3. Implement next opt-in capability without altering the existing game's
   water source-of-truth or float presentation pipeline; favor shared
   deterministic ECS policy over per-genre implementations.
4. After EVERY implementation stage, append: exact commit SHA, modified
   files, API, test logs/result, limitations, next action.
5. Keep the implementation and documentation in separate commits, document
   the implementation immediately before proceeding to the next stage.

## Stage 2 — composable buoyancy and flow drag (implementation commit `5dc63de21772da931c68bed02954866de186d754`)

Files changed: `libraries/include/aiverselib/water/NereidBuoyancySystems.h`,
`tests/ecs_nereid_buoyancy_test.cpp`, `CMakeLists.txt`,
`libraries/catalog.json`.

Public package `AIVERSE::lib_nereid_buoyancy` depends on
`AIVERSE::lib_nereid_water`. Public API in `aiverse::lib::nereid`:
- `WaterBuoyancyComponent`: acceleration, linear drag and immersion height
  (data-only, fixed-point).
- `WaterVelocityInputComponent`: per-tick canonical-motion velocity input;
  not a second source of truth for the host's physical velocity.
- `WaterForceComponent`: computed acceleration, immersed fraction, sampledTick.
- `computeWaterForces(sample,params,velocity)`: pure shared CPU policy.
- `addWaterBuoyancySystem(sim,name)`: read same-tick sampled contact and
  velocity, publish force; zeroes invalid or stale input.
- `registerNereidBuoyancyPersistence(sim)`: native ECS schema v1
  (`nereid.buoyancy`, `nereid.velocity_input`, `nereid.force`).

Force result includes upward water acceleration and drag relative to the
water's flow; **gravity, collisions and velocity integration remain with the
host physics/motion owner**. This avoids a second mutable velocity or
genre-specific swim implementation. Use only one solver.

Suggested sequence in a host:
```cpp
nw::registerNereidWaterPersistence(sim);
nw::registerNereidBuoyancyPersistence(sim);
// After movement and host per-tick input:
nw::addWaterProbeFromTransformSystem(sim, "water.probe"); // optional
nw::addWaterSamplingSystem(sim, "water.sample");
nw::addWaterBuoyancySystem(sim, "water.buoyancy");
// Then host physics integrates WaterForceComponent::acceleration.
```

**Verification:** NOT RUN / UNVERIFIED. Git access from the available
terminal returned `Could not resolve host: github.com`; connected GitHub
writes were successful, but do not imply a C++ build. New test
`ecs_nereid_buoyancy_test` checks drag relative to river/ocean flow,
submersion, dry reset, two unrelated native hosts, v1 save/load, COW/replay.
Compile it on a checkout:
```sh
cmake --build build/ecs-test --target aiverse_ecs_nereid_buoyancy_test -j 4
ctest --test-dir build/ecs-test --output-on-failure -R '^ecs_nereid_(water|buoyancy)_test$'
```

**Limits**: this is a contact/force-output *policy*, not a dynamic fluid solver,
mesh, boat hull evaluator, wave field, ocean FFT, physics engine or gameplay
swimming controller. Host is responsible for keeping WaterVelocityInput
current from its canonical motion state each tick. Validate platform compile,
native physics coupling, save/rollback and existing RTS/FPS parity before
shipping.

**Next agent action**: First configure/build both NEREID tests and fix any
issues. Then create separate spline/polygon geometry adapter and renderer
stages. Do not move RTS waterNetwork ownership without existing
`water_*` bake parity and non-regression tests. After each code stage commit,
append another status section to this handoff in a documentation commit.


## Stage 3 — optional polygon and river-spline geometry

**Implementation commit**: `e00eb62c92ae1bdb5f932cf1d60f7fa4ce0a5e9c` on `feature/nereid-water-core`.

Files added: `libraries/include/aiverselib/water/NereidGeometry.h`,
`tests/ecs_nereid_geometry_test.cpp`.
Files changed: `libraries/include/aiverselib/water/NereidWaterSystems.h`,
`CMakeLists.txt`. Documentation update follows as a separate commit.

### API / architecture

- `WaterGeometryComponent` is an **optional ECS component on the same Entity**
  as `WaterBodyComponent`. It has `kind`, `points`, `enabled`.
- `WaterGeometryKind::Polygon` is a simple closed X/Z ring (implicit closing
  edge, boundary inclusive, no holes). Each `WaterGeometryPoint` stores
  `x`, `z`, `halfWidth`; polygon ignores `halfWidth`.
- `WaterGeometryKind::RiverSpline` represents a centerline polyline. Each
  endpoint's `halfWidth` is interpolated along segments; river width is
  tested against the nearest point on each segment.
- `coversWaterBody(body,optionalGeometry,point)` is a pure deterministic
  Fixed-point footprint policy; geometry overrides Box/Disc/Infinite when
  enabled. Disabled/missing geometry uses v1 footprint. Enabled invalid
  geometry yields no coverage (no accidental infinite fallback).
- `sampleWater(world,point)` automatically consults optional geometry and
  **retains** existing priority, surface, depth, body ID and flow selection.
- `registerNereidGeometryPersistence(sim)` registers
  `nereid.geometry` v1 separately from existing `nereid.water_body` v1,
  so older data schemas and callers stay unchanged.
- Bounded archive IO rejects more than 256 geometry nodes before allocation.
  The interface remains `AIVERSE::lib_nereid_water`; no FPS/RTS dependency.

Native usage:
```cpp
nw::registerNereidWaterPersistence(sim);
nw::registerNereidGeometryPersistence(sim); // opt-in for polygon/spline
// Attach WaterBodyComponent and WaterGeometryComponent to water entity.
// Existing addWaterSamplingSystem() reads both automatically.
```

### Verification and limitations

**Native C++ build, CTest, platform runtime: NOT RUN / UNVERIFIED.**
The current execution environment cannot resolve github.com for cloning.
The GitHub connector wrote the implementation but cannot compile C++.
The new test is **authored, not executed**. Its intended cases: polygon
interior/edge/exterior, width-varying spline, overlapping priority,
multiple independent native hosts, native save/load, COW and rollback.

```sh
cmake -S . -B build/ecs-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/ecs-test --target \
  aiverse_ecs_nereid_water_test \
  aiverse_ecs_nereid_buoyancy_test \
  aiverse_ecs_nereid_geometry_test -j 4
ctest --test-dir build/ecs-test --output-on-failure \
  -R '^ecs_nereid_(water|buoyancy|geometry)_test$'
```

**Limits:** simple polygons only (no holes/self-intersections), one
variable-width centerline per geometry component, up to 256 nodes. All
query math is deterministic Fixed, not fluid rendering or GPU waves.
Overlap remains O(water bodies × probes × geometry segments). This is a
gameplay footprint and cannot reconstruct auto-shores or editor bake.
Existing `waterNetwork` must not be replaced without parity tests.

**Next agent**: run the three NEREID tests and repair compile/runtime errors
before adding further production integration. Then design a cached,
rollback-safe spatial query accelerator and a read-only adapter from legacy
`waterNetwork`; keep the legacy source and bake unchanged. Update docs
after every implementation commit with actual test output, limits and SHA.


## Stage 4 — transient ECS water-query broadphase

**Implementation commit:** `9451e4a7ed682d3d9cecb87d205cc2a7865d617a`
on `feature/nereid-water-core`.

**Files added:** `libraries/include/aiverselib/water/NereidQueryIndex.h`,
`tests/ecs_nereid_query_index_test.cpp`.
**Files modified:** `libraries/include/aiverselib/water/NereidWaterSystems.h`,
`CMakeLists.txt`.

### Architecture / API

- `WaterQueryIndex(const World&)` creates a **transient read-phase**
  balanced AABB tree for finite enabled water bodies (Box/Disc, polygon,
  variable-width river spline); infinite oceans are kept in a separate list.
- `WaterQueryIndex::sample(Vec3)` uses conservative X/Z AABB checks followed
  by the unchanged exact Fixed-point `coversWaterBody` policy.
- `considerWaterCandidate` is now shared by both the old reference
  `sampleWater(world,point)` and indexed sampler; selection remains
  **priority > surface > generation-safe Entity ID**.
- `addIndexedWaterSamplingSystem(sim,name)` is an optional **replacement**
  for `addWaterSamplingSystem`. Register only one of the two.
  Both write the same `WaterContactComponent` and tick stamp.
- The index is **rebuilt every sampling tick** from ECS authoritative bodies.
  No index state is persisted in save/network/rollback. Do not retain a
  `WaterQueryIndex` after structural edits, geometry/position changes,
  `World` replacement or rollback; its bounds become stale.
- No new ECS schemas or catalog packages are required; link
  `AIVERSE::lib_nereid_water` and include
  `<aiverselib/water/NereidQueryIndex.h>`.

Native usage:
```cpp
nw::registerNereidWaterPersistence(sim);
nw::registerNereidGeometryPersistence(sim); // when geometry components exist
nw::addWaterProbeFromTransformSystem(sim,"water.probe");
nw::addIndexedWaterSamplingSystem(sim,"water.sample"); // NOT both samplers
```

This is a performance policy option for multiple water bodies/probes:
index construction is currently O(B log² B) due to repeated
median partitions (not amortized across ticks); query is typically
O(log B + nearby bodies), but worst-case O(B) with overlapping bodies.
It has not been benchmarked: **no performance gain is claimed yet**.

### Verification

**Code authored and pushed via GitHub connector. Native build/CTest NOT
RUN.** Previous CI runs were queued or cancelled due to newer pushes, and
no completed passing NEREID run was available while implementing stage 4.
Do not interpret an authored test as passing.

```sh
cmake -S . -B build/ecs-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/ecs-test --target \
  aiverse_ecs_nereid_water_test aiverse_ecs_nereid_buoyancy_test \
  aiverse_ecs_nereid_geometry_test aiverse_ecs_nereid_query_index_test -j 4
ctest --test-dir build/ecs-test --output-on-failure -R '^ecs_nereid_.*_test$'
```

The authored `ecs_nereid_query_index_test` exercises ~1,800 probe
positions per host against reference sampling, mixed oceans/discs/splines/
polygons, priority tie policies, invalid geometry, two unrelated native
hosts, native save/load, COW rollback and next-tick index rebuild after
water geometry changes. This test has **not run**.

### Limits / next action

- Spatial bounds are conservative; the canonical exact per-body geometry
  test still decides water contact. The index captures `World` by
  reference; it is valid only within its construction read phase.
- Geometry polygon holes/self-crossings remain unsupported; no change to
  RTS `waterNetwork`, tile baking, editor or water shaders.
- For very few bodies and probes, rebuilding per tick may be slower than
  unindexed scans; decide empirically after profiling.
- **Next agent:** compile all four NEREID tests, fix failures, then profile
  at small and large body/probe counts and add optional opt-in threshold.
  After that, implement read-only legacy spline geometry import with
  parity tests before any native/editor migration. Maintain a handoff
  section after every implementation commit with exact SHA, files, test
  commands/results, current limitations and next task.

## Stage 5 — legacy raster-to-ECS import (code committed; tests not run)

**Branch:** `feature/nereid-water-core`.
**Initial implementation:** `4447c5ddeb86de1e715eafd8b12c4190d7b200d0`.
**Follow-up correction:** `b55062501c78c26c9f9b5317b0ef9f2241895753`
(baked-tile API, wet-to-dry test, input validation).
**Test expectation correction:** `630367954294b22454f898d93e9aa1473c7e25b6`.

Changed files:
- Added `libraries/include/aiverselib/water/NereidRasterImport.h`: pure
  generic row-run rectangularisation, water-body conversion.
- Added `engine/view/game/NereidLegacyWaterAdapter.h`: optional read-only
  legacy map bridge, under the *legacy* module (core remains genre-free).
- Added `tests/ecs_nereid_legacy_raster_test.cpp`: cell-centre parity
  against the existing waterCells raster, baked-tile parity,
  indexed-vs-reference checks, two independent native hosts,
  persistence, COW and rollback.
- Updated `CMakeLists.txt`: `aiverse_ecs_nereid_legacy_raster_test`
  links the core engine and `GameWater.cpp` directly (no editor/runtime
  or rendering target).
- Updated `libraries/catalog.json` description to reflect stages 1–5.

### API

`aiverselib/water/NereidRasterImport.h` (generic):
- `WaterRasterImportConfig`: Fixed `cellMeters`, `surfaceY`,
  `bottomY`, common `kind`, `priority`, `maxRectangles`.
- `rectangleWaterCells(wet,width,height,maxRectangles)`: deterministic
  run-length compression into disjoint rectangular spans, merges
  same-width spans across adjacent rows.
- `waterBodiesFromRaster(wet,width,height,config)`: returns
  `std::vector<WaterBodyComponent>` Box shapes in world-metres X/Z,
  with Y surface/depth from config; caller creates ECS entities.
  Each wet grid cell centre is covered, each dry centre is excluded.
  Input cap 4,194,304 cells (4 MiB mask); rectangle cap defaults 262,144.

`engine/view/game/NereidLegacyWaterAdapter.h`:
- `game::water::importBakedWaterCellsToNereid(cells,w,h,cfg,'~')`
  reads **canonical simulation map tiles**, exact matching `~`.
  Prefer this path for games which edit baked maps after `waterNetwork`.
- `game::water::importWaterNetworkToNereid(network,w,h,legacyCm,cfg)`
  uses the **existing** `buildGeometry` and `waterCells` functions
  to reproduce the editor's network *source* wet mask before subsequent
  bridge/map editing. It validates matching legacy and Fixed cell metres.

Both are **explicit load-time converters**, not live systems. The returned
data can be attached to any native game's water-body ECS. They do NOT
touch `Network`, shore types, tiles, map saves, editor UI or old RTS
navigation. `WaterKind` is set by the importer config because baked
raster cells do **not** retain ocean/river identity; **never** infer the
legacy body type from the merged mask.

Example:
```cpp
#include "engine/view/game/NereidLegacyWaterAdapter.h"
namespace nw=aiverse::lib::nereid;
nw::registerNereidWaterPersistence(sim);
nw::WaterRasterImportConfig cfg;
cfg.cellMeters=4_fx;
cfg.surfaceY=8_fx;
cfg.bottomY=-4_fx;
for (const auto& body : aiverse::game::water::importBakedWaterCellsToNereid(
        legacyMapCells, width, height, cfg)) {
    const aiverse::Entity e=sim.world().create();
    sim.world().add<nw::WaterBodyComponent>(e,body);
}
nw::addIndexedWaterSamplingSystem(sim,"water.sample");
```

### Actual verification

- GitHub connector wrote code; **native C++ build/CTest NOT RUN**.
- The newest CI had queued/pending jobs, with older pushes cancelled;
  a cancelled workflow is **not** proof that tests pass.
- Authored parity tests check every cell centre in an 80x64 map,
  short exact-span fixtures, source-network compatibility, baked
  tile-map compatibility, two genres, persistence and rollback.
- The authored test was inspected and corrected before documentation:
  initial probe is wet, moved probe dry; expected rectangles for a
  two-column vertical merge is 2, not 3.

Run:
```sh
cmake -S . -B build/ecs-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/ecs-test --target \
  aiverse_ecs_nereid_water_test aiverse_ecs_nereid_buoyancy_test \
  aiverse_ecs_nereid_geometry_test aiverse_ecs_nereid_query_index_test \
  aiverse_ecs_nereid_legacy_raster_test -j 4
ctest --test-dir build/ecs-test --output-on-failure -R '^ecs_nereid_.*_test$'
```
Also run legacy `water_*` tests, FPS and RTS ECS tests.

### Limits and specific next step

- This is cell-based parity at **grid centres**, not sub-cell shoreline
  fidelity. For high-quality polygon/mesh rendering use legacy shore data
  or a separate presentation adapter.
- Large fragmented rasters can produce many rectangles and ECS entities.
  Measure memory, rebuild and broadphase cost before enabling by default.
- The raster is not a moving/hydraulic water solver. Regenerate bodies
  explicitly if a host changes its baked map during gameplay. Do not
  silently sync two canonical sources.
- Next agent: compile all five tests, fix any compiler or runtime
  failure, verify old `water_*` bake output unchanged, and benchmark
  rectangle counts/import time and indexed sampling. Then add a native
  opt-in migration path or independent render/shoreline adapter, with
  further per-stage handoff.

## Stage 6 — adaptive sampling, native setup, CI coverage

**Branch**: `feature/nereid-water-core` (base:
`feature/universal-entity-inspector-fps`). Implementation in successive
commits (all on the same branch):
- `4ecb353964c0eb01b0626375039731c291830f4d`: adaptive water sampler
- `83081b60e98e8ce484655b20f0db1900fa3e33db`: native-game setup helper
- `e58e814ebf8d56a21c5054270ccd3fc22aef9417`: independent-host test
- `505c0fcb0fc07fa613c2662f2fe314caac8e799e`: CTest target
- `bc867d1df4791e2e519f409c14c50af8bedf07e1`: CI invokes all six NEREID tests
- `2b48e15d68a1dd99fbb88517dce2ec58cde10390`: catalog note

### Files, API, architecture

- Added `libraries/include/aiverselib/water/NereidAdaptiveWater.h`.
  Public `AdaptiveWaterSamplingConfig{minWaterBodies=16,minProbes=8}`,
  pure `chooseIndexedWaterSampling(bodies,probes,config)`, and
  `addAdaptiveWaterSamplingSystem(sim,name,config)`. The decision uses only
  deterministic ECS counts, **never FPS, time or platform measurements**.
  It chooses reference scanning for small workloads and stage-4 BVH for
  larger ones, preserving the existing `WaterContactComponent` and
  priority selection. Install only ONE sampling system.
- Added `libraries/include/aiverselib/water/NereidNativeWater.h`.
  `NativeWaterPipelineConfig`: optional geometry persistence, optional
  buoyancy persistence+system, optional Transform-to-probe stage,
  adaptive thresholds. `registerNativeWaterPipeline(sim,prefix,config)`
  registers components and the stage order in any native GameModule.
  `spawnNativeWaterBodies(world, vector<WaterBodyComponent>)` creates
  water-only entities in canonical input order (call at setup or a
  structural-change barrier). The host keeps canonical physics and
  entity lifecycle ownership.
- Added `tests/ecs_nereid_native_pipeline_test.cpp`:
  two unrelated host tags, reference vs forced-indexed configs, same-tick
  samples, native persistence, checksum parity, rollback, COW.
- Updated `CMakeLists.txt`:
  `aiverse_ecs_nereid_native_pipeline_test`.
- Updated `tests/run_ecs_experiment_suite.sh`:
  all SIX NEREID tests are now compiled and executed in the ECS
  experimental GitHub Actions suite. Prior CI suite omitted them, so
  previous CI checks were not evidence about NEREID.
- Updated `libraries/catalog.json`; API remains public through the
  `AIVERSE::lib_nereid_water` and
  `AIVERSE::lib_nereid_buoyancy` targets.

### Minimal native consumption

```cpp
#include <aiverselib/water/NereidNativeWater.h>
namespace nw=aiverse::lib::nereid;
// Inside GameModule::setup -- register once before sealing persistence:
nw::NativeWaterPipelineConfig options;
options.enableGeometry=true;    // optional custom shore polygons
options.enableBuoyancy=true;    // optional force OUTPUT only
nw::registerNativeWaterPipeline(sim,"water",options);
// Attach WaterProbeComponent + WaterContactComponent to query entities.
// Supply Transform or write WaterProbeComponent::position yourself.
// For baked maps, import waterBodiesFromRaster() at LOAD TIME and:
auto ids=nw::spawnNativeWaterBodies(sim.world(),waterBodies);
```

### Verification / blockers

**C++ build, six-test CTest run, and shell CI execution: NOT RUN in this
execution environment.** Source and CI registrations were updated via the
GitHub connector; this does NOT establish successful compilation.
At start of stage 6, Actions jobs for stage 5 were queued, not green.
Check latest Actions for the current branch. Do NOT merge until actual CI
and FPS/RTS legacy regression results are available.

```sh
cmake -S . -B build/ecs-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/ecs-test --target aiverse_ecs_nereid_water_test \
 aiverse_ecs_nereid_buoyancy_test aiverse_ecs_nereid_geometry_test \
 aiverse_ecs_nereid_query_index_test aiverse_ecs_nereid_legacy_raster_test \
 aiverse_ecs_nereid_native_pipeline_test -j 4
ctest --test-dir build/ecs-test --output-on-failure -R '^ecs_nereid_.*_test$'
# Experimental CI-equivalent (runs other ECS tests too):
bash tests/run_ecs_experiment_suite.sh
```

### Limits and next step

Adaptive thresholds are heuristic, not measured; forced-index parity is
authored but unverified. Dynamic imports and reconfiguration must occur
through a deterministic structural barrier, not a renderer or parallel
sampling callback. The helper does not migrate existing RTS or FPS
runtime and adds no water render backend. Existing `waterNetwork` and
tile baking remain untouched.

**Next agent**: inspect the six NEREID results and fix compiler/runtime
failures first, then benchmark index rebuild and linear/indexed/adaptive
sampling for different body/probe counts. Verify native-project target
usage and old `water_*` baking parity; only then consider connecting
existing game examples or water presentation. After the next code stage
append exact commit/branch, files, tests actually run, limitations and
the following action to this document.

## Stage 7A — sampler performance diagnostic (authored, not executed)

**Implementation SHA:** `a8520bdcaefd0e54cf6f418b2c50b44c578c02db`, branch
`feature/nereid-water-core`, base `feature/universal-entity-inspector-fps`.

Files: added `tests/ecs_nereid_sampling_bench.cpp`, updated
`CMakeLists.txt`, `tests/run_ecs_experiment_suite.sh`.

Standalone diagnostic target:
```sh
cmake -S . -B build/ecs-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/ecs-test --target aiverse_ecs_nereid_sampling_bench -j 4
./build/ecs-test/aiverse_ecs_nereid_sampling_bench
# Or existing ECS tools + this benchmark:
bash tests/run_ecs_experiment_suite.sh --bench
```

This benchmark compares `sampleWater` (linear scan) against rebuilding
`WaterQueryIndex` once **per tick** and sampling the same input points.
Workload matrix: 16/64/256 bodies × 8/128/1024 probes, 12 repetitions;
reports average microseconds per tick, relative speedup and sample digest.
It warms and validates the selected body/depth/signatures before comparing.
No assertion on speedup (hardware, build flags and runner loads vary);
wrong gameplay parity causes a failure. It does not mutate simulation
state and wall-clock timing is **never** part of gameplay decisions.

**Actual verification:** code committed by GitHub connector; C++ bench
not built/run in this environment, so **no timing numbers are known**.
The latest GitHub workflow was queued when inspected, not verified.

**Limitations:** geometry in this fixture uses discs; profile polygon/spline
stress separately if adopting index by default. The measured ratio
includes build-per-tick cost, but not the contact-pool write and source
transform stages. Do not change adaptive defaults until real results
show a defensible break-even. Next: implement a universal presentation
mesh adapter; then obtain actual CI and benchmark output. Update this
handoff immediately after that stage with SHA/files/tests/limits.

## Stage 7B — backend-neutral water surface mesh

**Implementation commit:** `5957287763ca3879d58bb0507755aa5dbe7e26ff`,
branch `feature/nereid-water-core`. Files added:
`libraries/include/aiverselib/water/NereidWaterSurfaceMesh.h` and
`tests/ecs_nereid_surface_mesh_test.cpp`; files updated:
`CMakeLists.txt` and `tests/run_ecs_experiment_suite.sh`.

### Public API / ownership

`buildWaterSurfaceMesh(body,optionalGeometry,WaterSurfaceMeshOptions)`
returns an entirely **presentation-only** `WaterSurfaceMesh`, with
`WaterSurfaceVertex {x,y,z,u,v,shore}` and 32-bit triangle indices.
`WaterSurfaceLod` = Near / Medium / Far controls subdivisions and
disc polygonal resolution; `cameraX,Z` and `oceanPatchRadius` bound an
otherwise infinite ocean to a movable square patch. `foamWidth`
determines vertex proximity to shore segments; `uvScale` controls
texture-space units. `maxVertices` limits memory.

Shapes: box, disc, infinite camera patch, concave simple polygons with
ear clipping (+Y triangle winding for either polygon orientation), and
river ribbons with variable half-width and capped miter joins.
Self-intersecting and degenerate polygons return empty meshes, rather
than incorrect triangles. Source ECS water body and optional geometry
are read-only. Positions/Y come from fixed authoritative data, but
presentation coordinates/UVs/foam are floating-point and **must never
feed back into the deterministic simulation**.

`WaterSurfaceStyle` provides an opt-in material-input default
(color, foam tint/strength, alpha and specular) for the renderer
adapter to consume in a subsequent isolated stage. No new ECS schema,
shader binding, material persistence or scene draw path was added yet.
This code deliberately does **not** alter the existing RTS terrain water
or waterNetwork/bake, and does not introduce per-genre water mesh logic.

### Verification and next action

CMake executable / CTest `ecs_nereid_surface_mesh_test` tests box area,
+Y winding, near/far disc LOD, capped vertices, concave/reversed
polygons, invalid self-intersection, varying-width river, shore
factor and camera-relative ocean bounds. The ECS experimental shell
runner also builds/runs this fixture. **Actual C++ build and CTest: NOT
RUN / UNVERIFIED.** GitHub connector push succeeded; Actions status
needs separate confirmation.

```sh
cmake --build build/ecs-test --target aiverse_ecs_nereid_surface_mesh_test -j 4
ctest --test-dir build/ecs-test --output-on-failure -R '^ecs_nereid_surface_mesh_test$'
```

Limits: static surface sheet only; wave vertex displacement, depth
refraction, underwater treatment and native renderer integration are
not implemented. GPU vertex lifetime, camera-dependent patch rebuild,
CPU upload churn and platform-specific rendering need explicit
profiling and testing. **Next:** add a small RHI renderer bridge using
existing Renderer3D/MeshBuilder with a clean ownership/rebuild
contract. Keep shader effects as a separate change. First fix any
failed native CI tests before declaring this production-ready.

### Stage 7B immediate source review correction

**Fix commit:** `652ca2f3358f1384ee805cc2bdda72666910c449`.
In `NereidWaterSurfaceMesh.h`, the simple-polygon validator
incorrectly checked distance from vertex A to its **own A-B segment**
(which is always zero). That caused every polygon to be rejected
before ear clipping. Replaced it with the actual A-B segment-length
check, and explicitly included `<cstddef>` and `<iterator>`.
This was found by source review, **not by an executed C++ test**.
The authored concave/reversed polygon fixture must still be compiled
and run. All other stage-7B verification limitations remain.

## Stage 7C — real Renderer3D upload/draw bridge + explicit water shader flag

**Implementation commit:** `b96be812ee14cbb139ff09beae13e0fbc58a2bdb`
on `feature/nereid-water-core`. **Documentation commit:** this update.

Changed/added:
- New `engine/render/NereidWaterRenderer.h`.
- New `tests/ecs_nereid_renderer_contract_test.cpp`.
- Modified `engine/render/Renderer3D.h`, `Renderer3D.cpp`, `Shaders.h`.
- Registered CTest target in `CMakeLists.txt` and CI shell runner in
  `tests/run_ecs_experiment_suite.sh`.

### API, architecture and ownership

`render::uploadNereidWaterSurface(device,cpuMesh,style)` uploads
`WaterSurfaceMesh` from stage 7B into the *existing*
`Renderer3D::MeshBuilder` static VB/IB structure. It composes
per-vertex deep water tint with `WaterSurfaceVertex.shore` foam
in a normal vertex color; it does **not** invent a second RHI.

`render::drawNereidWaterSurface(renderer,mesh,style)` queues the
existing `Renderer3D::drawMesh` with alpha < 0.99, glossy specular,
`kDrawNoCastShadow`, and **new `kDrawWaterSurface=256` flag**.
`render::releaseNereidWaterSurface(device,mesh)` destroys both GPU
buffers and zeros the handle. **Caller owns** lifetime, static caching,
draw order, camera-relative infinite-patch refresh and device-loss
rebuild. No renderer automatically discovers NEREID ECS entities.

Renderer3D stores the opt-in flag via per-draw `u.params.x=-1`
while preserving `0` for lit and `1` for unlit. GLSL and Metal
now opt in to existing six-wave normal animation at *any* water
elevation when the flag is set. The unlit mix clamps this sentinel
before shading, so old lit/unlit/debug draw modes are preserved.
Trace-tier SSR can be applied to marked water even when Y is not near
zero. Pre-existing height-based terrain water behavior is unchanged.
No new textures or uniform block/layout/bindings are required.

`render::nereidWaterVertexColor` and
`render::validNereidWaterStyle` are pure CPU helpers; the latter
requires a translucent alpha and specular value 0.85–1.0 so the
existing reflection/render path recognizes water.

### Verification status and limitations

**Native C++ build, GLSL compile, Metal compile, real GPU screenshots,
and CTest are NOT RUN / UNVERIFIED**. New CPU contract test
`ecs_nereid_renderer_contract_test` checks foam vertex tint,
material validation, flag allocation and GLSL/MSL source sentinels.
This does NOT prove shader compilation or any GPU result. It is
also included in the ECS experiment shell runner. Check latest
GitHub Actions and fix failures before merging.

```sh
cmake -S . -B build/ecs-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/ecs-test --target   aiverse_ecs_nereid_surface_mesh_test   aiverse_ecs_nereid_renderer_contract_test -j 4
ctest --test-dir build/ecs-test --output-on-failure   -R '^ecs_nereid_(surface_mesh|renderer_contract)_test$'
# Also compile/launch the actual graphics editor/client on GL and Metal.
```

**Important limits:** the renderer bridge is opt-in, so FPS/RTS scenes
have not been migrated. Planar reflections are a single render
pass/plane chosen by existing `Renderer3DReflect.cpp`; multiple
water bodies at different Y levels are not yet reflected correctly.
SSR is available only when the existing trace tier has valid color
and depth inputs, and there are still no refractive depth lookups,
underwater/post effects, water collision waves or hydraulic solve.
The bridge tints foam by vertices (not per-pixel shoreline distance).
Explicit draw flag changes global lit shaders, so check regressions
on *both* GLSL and MSL before production.

**Next agent:** First obtain actual build/test results for all NEREID
targets (old and new) and old RTS water tests. Then validate the
renderer on actual GL and Metal, especially surface Y != 0 and
multi-plane reflection fallback. Add depth/refraction, per-pixel foam
or underwater view as another isolated renderer stage with a new
handoff after every implementation commit. Keep deterministic water
CPU authoritative and all GPU effects presentation-only.

### Stage 7C follow-up: coarse-LOD foam fix

**Implementation commit:** `a6fe7057b63485a8e9806e97ac4d979f23b826cf`.
After source review, the far surface geometry was discovered to contain
boundary-only vertices for some shapes. Per-vertex shoreline foam
therefore coloured the **entire far surface** as foam. The far LOD now
skips shoreline foam entirely (near/medium retain the distance-based
mask). The `ecs_nereid_surface_mesh_test` fixture asserts far box
vertex foam is zero. This is a visualization correctness fix; the
GPU/CTest pipeline is **still not verified**.

## Stage 8 — safe planar reflection fallback for multiple surface elevations

**Implementation SHA:** `5a6d804ca6a0869f1033ca4eb447329485a789e4`.
**Branch:** `feature/nereid-water-core`. **Base:** `feature/universal-entity-inspector-fps`.

**Files changed:**
- Added `engine/render/NereidWaterReflectionPolicy.h`, a pure renderer-only
  finite/tolerance test `render::sameWaterReflectionPlane(y,selectedY)`.
- `engine/render/Renderer3D.h`: transient per-frame `reflectionPlaneY_`
  and `reflectionPlaneValid_`.
- `engine/render/Renderer3DReflect.cpp`: only store the selected
  reflection plane when the existing `lights.beginReflection` succeeded.
- `engine/render/Renderer3D.cpp`: reset the selection at each
  `beginScene`; tagged NEREID water on a different Y uses U0.params.x=-2
  instead of -1. Unmarked meshes and debug draw modes remain unchanged.
- `engine/render/Shaders.h`: GLSL/MSL retain wave shading and SSR for
  the tagged secondary surface, but feed zero specular to
  `waterReflect` so its **wrong-plane planar texture** is not blended.
- `tests/ecs_nereid_renderer_contract_test.cpp`: tests same/different
  plane policy and GLSL/MSL sentinel fragments in addition to previous
  style/flag checks.

### Behavior / limits

The existing renderer has only **one** planar mirror target. This
stage guarantees an opt-in NEREID surface at a different elevation does
not consume a reflection rendered for another elevation. When
trace-tier SSR is available, secondary surfaces may still use it.
When not, they receive normal lit water, waves and environmental light,
but **not** planar reflections; this is not a multi-plane reflection
renderer. Same-height NEREID bodies may reuse the selected plane.
Tolerance is 0.025 world units. No new serialization, ECS state,
physics or legacy waterNetwork changes.

### Verification

**Actually executed:** compiled a standalone reproduction of
`sameWaterReflectionPlane` with Debian GCC 14.2.0 using
`-std=c++20 -Wall -Wextra -Werror` and ran a main that tested
near/equal, different elevation and NaN cases; exit 0. This isolated
smoke check does **not** link AIVERSE or compile the repository header
in a full checkout.

**NOT RUN / UNVERIFIED:** full CMake build, NEREID CTest, GLSL/MSL shader
compilation, GPU screenshot, CI regression suite. At inspection, GitHub
Actions for the previous head were queued and Linux arm64 was
in progress; no passing NEREID check was available. PR stays draft.

**Next:** build and run every existing/new NEREID CTest,
especially `ecs_nereid_renderer_contract_test` and visual
water-at-two-heights on OpenGL and Metal. Examine linked shader
compilation logs, test both trace enabled/disabled and a single
legacy water plane. Then implement isolated depth/refraction and
explicit shader material controls only after that baseline is green.

### Stage 8 follow-up — primary water plane selection

**Implementation SHA:** `782088418fe450762b555ebabd27ba55015e1b4c`.

`engine/render/Renderer3DReflect.cpp` now chooses an explicitly flagged
`kDrawWaterSurface` translucent glossy mesh before falling back to the
previous untagged glossy search. This prevents the first highly specular
glass/other non-water mesh in the queue from claiming NEREID's sole
planar reflection target. The old first-glossy behavior is unchanged
when no tagged NEREID surface exists. The pass remains **single-plane**;
stage-8 secondary water Y levels suppress invalid planar sampling.
Verification: source reviewed and committed only; **full C++ compile,
graphics runtime and CI remain NOT VERIFIED**. Next agent must include
mixed glass/NEREID scene in OpenGL/Metal validation.

## Stage 9 — transactional water surface GPU residency (feature implementation)

**Branch:** `feature/nereid-water-core`; **PR:** #23 (Draft).
**Implementation commits:** `1ca61ff311930b3c0ac5c1d69f2d5539b0c7f012`
(residency API), `4fd5d9f469d13a19a8ac7b1e9ba4a338c8953c62`
(contract fixture update).

**Changed files:**
- Added `engine/render/NereidSurfaceResident.h` — renderer-only,
  backend-neutral GPU-resident water mesh controller.
- Extended `tests/ecs_nereid_renderer_contract_test.cpp` with
  patch-grid, revision, camera and style invalidation cases.
- Updated this handoff separately after implementation.

### Public API and expected usage

`render::NereidSurfaceResident` owns one uploaded water VB/IB pair and
is deliberately noncopyable/nonmovable to prevent double buffer destruction.
`update(device,body,optionalGeometry,sourceRevision,meshOptions,style)`
returns `NereidSurfaceUpdate::{Unchanged,Uploaded,Failed}`.
`draw(renderer)` queues the existing flagged water draw;
`release(device)` explicitly releases VB/IB (also mandatory **before**
destroying or replacing the underlying RHI device).

`sourceRevision` is **supplied by the scene host**, never generated
from GPU or simulation state. Increment it whenever the authoritative
body or optional geometry changes, including when restoring a different
rollback snapshot. Do not pass a fixed revision for changing objects.
For infinite water with no enabled custom geometry, the patch center is
snapped to a camera-relative grid with cell width `oceanPatchRadius`;
within the same cell, camera motion alone does not re-upload a mesh.
Finite water ignores camera changes for cache invalidation.

Changed visual style, LOD, tessellation budget, foam width, UV scale
or ocean patch radius invalidates the mesh. The old GPU buffers remain
resident until a full replacement VB/IB is uploaded, then are released.
An invalid/empty new mesh or unsuccessful upload returns `Failed`
and retains the previous view. A disabled source reports `Failed`;
the host should call `release` to hide a retired water body.

Example native scene ownership:
```cpp
#include "engine/render/NereidSurfaceResident.h"
aiverse::render::NereidSurfaceResident waterVisual;
// During scene update, using a host-owned geometry revision:
waterVisual.update(device,body,geometry,geometryRevision,options,style);
// Between Renderer3D::beginScene and Renderer3D::render:
waterVisual.draw(renderer);
// During scene/device shutdown (before device teardown):
waterVisual.release(device);
```

**Key boundaries:** no new ECS schemas, gameplay state, dynamic wave
solver, renderer backend or genre dependency. One instance per visible
water body; caller chooses draw/discovery order, LOD and culling.
A changed geometry source with unchanged revision violates cache
invalidation, and a device replacement requires explicit release.
Cache still builds CPU triangulation synchronously on a valid invalidation;
no async generation, upload budget, dynamic geometry streaming, per-pixel
shore foam or true multi-plane reflection yet.

### Verification

**Actually performed:** connector wrote both commits, source examined;
test cases are **authored only**.
**NOT RUN / UNVERIFIED:** complete native C++ compilation, CTest,
GLSL and MSL compilation, GL/Metal runtime, device-loss recovery
and timing/memory benchmarks. The current environment has no local
AIVERSE checkout. This does **not** establish passing tests.

Run in an AIVERSE checkout:
```sh
cmake -S . -B build/ecs-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/ecs-test --target \
  aiverse_ecs_nereid_renderer_contract_test \
  aiverse_ecs_nereid_surface_mesh_test -j 4
ctest --test-dir build/ecs-test --output-on-failure -R '^ecs_nereid_.*_test$'
```
Also run the legacy `water_*` test suites, both graphics backends,
and a native-game smoke scene exercising 2 water elevations, rollback,
resizing, release/recreate and moving ocean camera at negative X/Z.

**Next agent:** first obtain C++ build and fixture results, fix failures.
Then integrate the resident into an opt-in native WorldScene or renderer
adapter with bounded per-frame rebuild scheduling and optional culling,
without changing RTS `waterNetwork` or its baked map. Add a new handoff
section after the implementation and record actual verification status.

## Stage 10 — main-only merge and opt-in WorldScene water presentation

**Merge main into NEREID branch:** `04860b76cdf3bd26d0e45dcfc71cbb7d02159c78`.
Merge parents: NEREID `9f88f47503911af881fd6249726c8055e224d591`,
main `9ee6ff6fbf390bd45363bc92c838f492dfe92eee`.
Only `main` was merged. Four overlapping files were reconciled:
`AGENTS.md`, `CMakeLists.txt`, `libraries/catalog.json`,
`skills/catalog.json`. Current main input/rules changes and existing
NEREID and SpectraFX registrations were preserved; no feature branch
was individually merged. Branch `feature/nereid-water-core` remains
the target; `main` was not changed.

**Feature commits:**
- `2aaa113cba6518977e9abe8f8ebf36c67fc93de5`: new
  `engine/world/NereidWaterScene.h`.
- `f3eacaaa443e36970d0556fc14a389414ca15aa3`:
  `engine/render/NereidWaterRenderer.h` supports a model matrix.
- `31700c2bb172f302461ddfde67ef9eb5a7b8b542`:
  `engine/render/NereidSurfaceResident.h` forwards optional draw transform.
- `1b879701af189441eeb2501c4e66021ac319794a`:
  `engine/world/WorldScene.h` public opt-in interface.
- `9ca4ce487de043b7535ab35dbf46826579039aa9`:
  `engine/world/WorldScene.cpp` update/draw/shutdown integration.
- `9fd68b786910d46c47552a60df340fb9cc1a6f08`:
  `tests/ecs_nereid_renderer_contract_test.cpp` bounds and snapshot
  contract assertions.

### API and lifecycle

A native project can create a read-only water snapshot and opt in:
```cpp
#include "engine/world/WorldScene.h"
std::vector<aiverse::world::NereidSceneSurface> snapshot;
aiverse::world::NereidSceneSurface water;
water.body = authoritativeBody;  // copy from current ECS read phase
water.useGeometry = false;       // set true + copy .geometry if needed
snapshot.push_back(water);
scene.setNereidSurfaces(std::move(snapshot));
// WorldScene::update(device,observers) uploads/refreshes optional water.
// WorldScene::draw(renderer,renderOrigin,cellSize) draws it relative to origin.
// WorldScene::shutdown(device) releases its GPU buffers.
```

`NereidSceneSurface` is a **copied** body/optional geometry and renderer
style/options, not an ECS entity pointer. A new snapshot increases
scene revision and invalidates cached GPU geometry; avoid calling the
setter each frame with an unchanged snapshot. Maximum 64 surfaces.
When the observer is present, camera-relative infinite ocean patches
use its canonical world-space X/Z. Finite water ignores camera movement.
Draw uses the `renderOrigin` and `cellSize` coordinates already passed
to `WorldScene::draw`, so water surface input points must be **global
world coordinates in metres** (not local chunk coordinates).

Calling `setNereidSurfaces` is a scene-host decision; there is no
automatic genre-specific runtime migration, ECS mutation or legacy
`waterNetwork` consumption. Call `WorldScene::update` following a
setter call to publish/release resources before rendering. Clearing
the snapshot retires current GPU handles on the next update. The
old rendered surface remains on upload failure until explicitly
retired, following Stage 9's transactional resource policy.
Water surfaces share the standard RHI and Renderer3D, no new backend.

### Verification, limits, next stage

**Actually performed:** GitHub connector tree merge, code and test
commits, source contract review. **Native C++ compilation, CTest,
actual OpenGL/Metal scenes, FPS/RTS and old water non-regression:
NOT RUN / UNVERIFIED** (no local AIVERSE checkout; GitHub DNS unavailable
to shell). Authored contract checks are not evidence of passing tests.

```sh
./run-tests.sh --filter '^ecs_nereid_'
# Also execute full ./run-tests.sh and visual tests on GL + Metal.
```

**Known limitations:** default config does not automatically import
water from chunk manifests, only explicit host snapshots. 64 body
objects upload synchronously; no water frustum culling or async mesh
generation. Global absolute water positions converted to float before
subtraction of render origin may lose precision at very large worlds;
use relative geometry rebaking/patch-based local origins for planetary
coordinates. No GPU device-loss auto recovery: call shutdown before
replacing Device. `WorldScene` destructor cannot release buffers
without a live Device; callers must invoke shutdown.

**Next agent:** FIRST run and fix the merged CMake/CTest suites,
including shader contract and GL/MSL builds, and ensure merge catalogs
and root `AGENTS.md` are consistent. Then add a manifest/asset adapter
or immutable ECS presentation bridge with bounded upload budgets and
camera-relative geometry for large worlds. Preserve the universal ECS
water owner and leave older RTS water tile bakes unchanged.

## Stage 11 — declarative water manifest and bounded GPU updates

**Branch:** `feature/nereid-water-core`, NEREID PR #23 (Draft).
**Baseline:** main-only merge `04860b76cdf3bd26d0e45dcfc71cbb7d02159c78`.

**Implementation commits:**
- `4d8ae573dbfa700cfb908401d7af1783f0b545ca`: new `engine/world/NereidWaterManifest.h`
- `ba302b10b2cbd4ed17f9b594ad8f2ceacd719f2c`: `NereidSurfaceResident::needsUpdate` (read-only)
- `1d3127243d88df4750b59bd5c90db4321dfecac6`: `NereidWaterScene` per-frame upload scheduler
- `a84470479ae57f7583d3af074492bb431d281adb`: stable round-robin scan fix
- `cdab63b9eca788e43479d1870f4e9c90dd2dd08f`: `WorldScene` constructor manifest opt-in
- `3479ebd4c44a683d49cac92c4c504b60a3c81139`: extended renderer contract fixture

### API and manifest

`world::parseNereidWaterManifest(const Json&)` parses an optional
top-level `nereidWater` block into `NereidManifest {surfaces,
maxUploadsPerFrame}`. It is separate from **authoritative ECS data**:
only world presentation is configured and no persistence schema changes.
If `nereidWater` is absent, older manifest paths behave as before.

Minimal example (coordinates and widths are in world metres, Y is up):
```json
{
  "nereidWater": {
    "maxUploadsPerFrame": 2,
    "surfaces": [
      {
        "kind": "ocean", "shape": "infinite",
        "surfaceY": 0, "bottomY": -500,
        "oceanPatchRadius": 256, "lod": "medium",
        "style": {"opacity": 0.73, "specular": 0.95}
      },
      {
        "kind": "lake", "shape": "polygon",
        "surfaceY": 7, "bottomY": -8,
        "points": [[0,0],[30,0],[40,20],[0,20]],
        "foamWidth": 2
      },
      {
        "kind": "river", "shape": "river",
        "surfaceY": 5, "bottomY": -2,
        "points": [[0,0,3],[25,10,5],[40,20,4]],
        "lod": "near"
      }
    ]
  }
}
```

Supported `kind` values: `ocean/lake/river/pool/puddle`.
Supported `shape` values: `infinite/box/disc/polygon/river`.
`box` requires `halfExtents:[x,z]`; `disc` requires positive
`radius`; `polygon` uses 2D X/Z nodes; `river` uses X/Z/halfWidth
nodes. Optional `center:[x,y,z]`, `flow:[x,y,z]`, `priority`,
`enabled`, `lod`, `uvScale`, `foamWidth`, `maxVertices`,
`oceanPatchRadius` and `style.deepColor/foamColor` RGB triples.
Positive extents, heights, color ranges and node counts are validated.
Up to **64** scene surfaces; geometry nodes <=256.
`maxUploadsPerFrame` range 1..16, default 2.

`WorldScene` reads config in its constructor. Native hosts can
override the initial water snapshot by calling
`WorldScene::setNereidSurfaces(...)` before `update`.
`NereidWaterScene::setMaxUploadsPerFrame` configures a fair
round-robin rebuild-attempt quota. It inspects resident keys using
`NereidSurfaceResident::needsUpdate` and only consumes a quota slot
when an upload/rebuild is needed (including failed attempts).
Disabled or removed surfaces release immediately on `update`.
GPU publication and eviction remain on the caller's device thread.

### Verification and limits

**Actually done:** source review, GitHub connector commits, authored
contract assertions for valid/invalid JSON and upload-quota bounds.
A local `git ls-remote` check failed due to
`Could not resolve host: github.com`; no checkout is available.

**NOT RUN / UNVERIFIED:** C++ CMake configuration, compilation, CTest,
OpenGL/Metal visual scene, asynchronous chunk streaming regression,
FPS/RTS parity, shader compilation and GPU upload/failure test.
Tests are source fixtures only until run on a checkout.
Use the unified runner:
```sh
./run-tests.sh --filter '^ecs_nereid_'
./run-tests.sh
```

**Limitations:** quota bounds *attempts per frame*, not CPU ms or bytes.
Triangulation/upload is still synchronous, so a single very large
surface may spike. Finite surfaces lack frustum culling, and
setSurfaces invalidates the whole list; avoid resubmitting unchanged
snapshots every frame. Water imported from legacy RTS maps remains
explicit opt-in. Rendering is still vertex-tinted foam, wave normals,
single-plane reflection fallback, no underwater/refraction pipeline.
Coordinates are absolute floats at mesh-build time, with known
far-origin precision limitations.

**Next agent:** run compiled NEREID and full regression suites first;
correct any failures, including CMake/catalog merge conflicts. Then add
per-body stable revision identities, bounded upload **byte** budget and
camera visibility selection; provide immutable ECS snapshot importer.
After each stage commit/update this handoff with actual results.

## Stage 12 — stable ECS identity, bounded water rendering, CPU culling

**Branch:** `feature/nereid-water-core`, draft PR #23; based on the
already merged `main` at merge commit
`04860b76cdf3bd26d0e45dcfc71cbb7d02159c78`.
No other feature branch was merged during this stage.

**Implementation commits (GitHub connector):**
- `2062c58567d779748756e0e3d16f99320157de79`:
  `engine/world/NereidWaterScene.h`, stable-ID GPU owner reuse, per-body
  revision change detection, distance culling, estimated byte admission.
- `cb24028f079cf601af6145bf11e3d41105c80955`:
  new `engine/world/NereidEcsPresentation.h`, read-only immutable ECS
  snapshot bridge.
- `e54afd5ba3758ae7317095eb4fe32f0a8058e260`:
  `engine/world/NereidWaterManifest.h`, explicit surface IDs,
  configurable distance and upload work budgets.
- `71d10f44d3595d1a9fb9b3f242a2c5c36f1c758f`:
  `engine/world/WorldScene.cpp`, apply new manifest settings.
- `8f60712e0b06113803800e95c5e43d382161f11c`:
  `NereidWaterScene.h`, disabled custom geometry/infinite ocean
  visibility and introspection fixes.
- `ce56f6cf51f44a10c44d3de52ed4f09574aef78c`:
  `tests/ecs_nereid_renderer_contract_test.cpp`, source-level
  contract tests (not executed).

### Architecture and native API

`NereidSceneSurface::id` is the stable GPU residency key.
Explicit nonzero IDs survive changes in ordering. Zero is the
legacy index-assigned fallback and **is not stable under reorder**.
`sameNereidSceneSurface` compares gameplay-snapshot data, geometry
and visual style field-by-field, ignoring camera coordinates. Calling
`setSurfaces` with unchanged content preserves its GPU revision.
Only a changed water body increments its individual revision.
Removed surfaces are queued for release by the next device-thread
`update` or `shutdown`; never destroy GPU resources in ECS/read phase.

`snapshotNereidEcsWater(const World&,NereidEcsPresentationOptions)`
reads all live `WaterBodyComponent` and optional
`WaterGeometryComponent`, copies data, assigns packed Entity+1 IDs
(which encode generation and avoid the zero sentinel), sorts by ID,
and returns at most 64 presentation surfaces. It does not add,
modify or remove any ECS component; simulation/rollback still own
authoritative bodies. Snapshot publication must happen at a
host-controlled read barrier after structural ECS changes:
```cpp
#include "engine/world/NereidEcsPresentation.h"
auto waterSnapshot=aiverse::world::snapshotNereidEcsWater(sim.world());
scene.setNereidSurfaces(std::move(waterSnapshot));
// scene.update(device,observers) on the device thread;
// scene.draw(renderer,origin,cellSize); scene.shutdown(device).
```

New `NereidWaterScene` controls:
- `setMaxUploadsPerFrame(1..16)`, default 2: attempts per frame.
- `setMaxEstimatedUploadBytes(1024..64MiB)`, default 8MiB:
  conservative CPU mesh budget estimate `maxVertices*(sizeof(MeshVertex)+
  6*sizeof(uint32_t))` for scheduling; **NOT** a measured GPU transfer
  byte cap. One oversized first request is admitted for progress.
- `setVisibilityRange(0..1000000)`, default 0 (off): conservative
  X/Z distance-to-bounds culling for finite water. Infinite oceans
  always qualify. This is **not frustum or occlusion culling**.
- `stats()`: `visible/resident/pending/uploadsAttempted/
  estimatedBytesAdmitted`; `surfaceRevision(id)` diagnostic API.

Manifest settings:
```json
{
  "nereidWater": {
    "maxUploadsPerFrame": 2,
    "maxEstimatedUploadBytes": 8388608,
    "visibilityRange": 500,
    "surfaces": [
      {"id": 17, "kind": "lake", "shape": "box",
       "surfaceY": 3, "bottomY": -5,
       "center": [0,0,0], "halfExtents": [40,30]}
    ]
  }
}
```
IDs are positive integers up to 2,147,483,647 in JSON; explicit duplicate
IDs cause an error. JSON with no `nereidWater` block keeps legacy behavior.
Host-provided snapshots replace manifest snapshots; do not wire both
at once. The GPU layer remains independent of RTS/FPS, waterNetwork,
PHYSIS, TerraWeave, Box3D, VFX and Prisma.

### Verification / known limitations / next step

**Actually done:** source inspected and connected GitHub commits written;
basic consistency/revision/budget/geometry/ECS copy tests authored.
**NOT RUN / UNVERIFIED:** C++ compilation, CTest, GL/Metal shaders and GPU,
native WorldScene runtime, FPS/RTS and water bake regression, upload
measurement and large-world precision. Do not treat test source as PASS.
No local checkout was available to execute the unified runner.

```sh
./run-tests.sh --filter '^ecs_nereid_'
./run-tests.sh
```

**Limitations:** 64 body cap; renderer does not automatically query the
live ECS, it consumes explicitly submitted snapshots. Estimated bytes
are a scheduler heuristic and uploads are still synchronous, not a true
byte/time quota. Repeated failed surfaces can retry each frame.
No camera frustum/HiZ occlusion, LRU eviction, deep-water refraction,
underwater volume, per-pixel foam, terrain bank blending, device-loss
automatic recovery or large-world relative-vertex rebake yet.
The ECS bridge assumes packed Entity IDs are unique for currently
alive entities and will reject overflow/snapshot population >64.
Source revision comparison is presentation-only, not an authoritative
network/rollback checksum.

**Next agent:** compile the added test and confirm that the default
`World` test fixture and renderer constructors compile. Fix any native
issues before shipping. Then implement true prepared CPU mesh upload
admission (exact buffer byte size), visibility-aware far-water LOD
and large-world origin rebasing, followed by GLSL/MSL scene screenshots
at multiple Y heights. Preserve a separate per-stage handoff.

**Stage 12 follow-up:** `AGENTS.md` ownership/status refresh commit `f6a4f6fdd0eb16a98a9b7f7dfd3ba91bf5310e5c`; `engine/world/NereidWaterScene.h` follow-up commit `b1b2f0cd645260730e24199f4f0cb3dae8e4d8b2` reserves retired GPU storage before moving residency owners in `setSurfaces`, avoiding allocation failure mid-transfer. Source changes committed; C++/CTest/graphics validation remains NOT RUN.

## Stage 13A — exact CPU mesh upload accounting and camera-distance LOD

**Branch:** `feature/nereid-water-core` (PR #23, Draft).
**Starting head:** `329e7c7f99b7d02e3b0eba76aa3f0206d2ba665b`.
**No additional branches merged.** The existing main merge remains the
Stage-10 merge `04860b76cdf3bd26d0e45dcfc71cbb7d02159c78`.

**Implementation commits:**
- `99a41b6d15d9e520e096403ec6bea6755ba18320` —
  `engine/render/NereidSurfaceResident.h`: split pure CPU preparation from
  RHI upload, add `NereidPreparedSurface` and exact `nereidMeshBufferBytes`.
- `a24d6acc301e2bbbf55be84f69ca7193d0852476` —
  `engine/world/NereidWaterScene.h`: prepared mesh cache by stable ID
  and render revision, byte admission and auto LOD.
- `9175bff5ba0b1691fd8e96e06deb466a98169a5b` —
  `engine/world/NereidWaterManifest.h`: opt-in LOD JSON fields.
- `18a3f94f63db0b575b36a22625e6957c75a675a8` —
  `engine/world/NereidWaterScene.h`: polygon/river distance uses
  geometry bounds, not uninitialized WaterBody center.
- `a57133bcec7dcb359446d663958f5fe4b66f7b7a` —
  `tests/ecs_nereid_renderer_contract_test.cpp`: exact byte and LOD
  regression cases (authored, **not executed**).

### Architecture/API

`render::NereidSurfaceResident::prepare(body,geometry,revision,options,style)`
is CPU-only and returns `std::optional<NereidPreparedSurface>` containing
the immutable mesh, its key, and exact **vertex + index array payload bytes**.
`uploadPrepared(device,prepared)` is the explicit GPU publication point,
retaining the previous resident surface on failed upload. Existing
`update(...)` remains available for native hosts as an immediate wrapper
around prepare + upload. `nereidMeshBufferBytes(mesh)` counts
`mesh.vertices.size()*sizeof(render::MeshVertex)` and
`mesh.indices.size()*sizeof(uint32_t)` with overflow checks.

`NereidWaterScene` caches a CPU prepared mesh per water ID and invalidates
it only when its source revision, camera ocean patch, style or selected LOD
changes. **Admission is strict**: the sum of admitted VB/IB bytes in the
current update does not exceed `maxEstimatedUploadBytes` (legacy public
name retained for compatibility). An individual upload larger than the
configured budget remains staged and deferred until the host increases the
budget or lowers LOD; this replaces Stage 12's "admit one oversized"
heuristic. `stats().estimatedBytesAdmitted` now reports exact CPU VB+IB
payload bytes; `stats().preparedBytes` and `stats().budgetDeferred`
are diagnostic counters. This does not include driver allocation overhead,
RHI padding, GPU texture memory or transient staging copies.

`NereidSceneSurface::autoLod` defaults to **false** so old scenes keep
their manually specified LOD. With opt-in, the observer selects
Near / Medium / Far using `mediumDistance` (default 120m) and
`farDistance` (default 400m). Finite box/disc bodies use their center;
polygon/river geometry uses its bounding rectangle (nearest AABB point);
infinite oceans retain the authored LOD. This is CPU presentation-only,
not GPU tessellation or an adaptive screen-space error metric.

Example in an existing `WorldScene` manifest:
```json
{
  "nereidWater": {
    "maxUploadsPerFrame": 2,
    "maxEstimatedUploadBytes": 8388608,
    "surfaces": [
      {
        "id": 7, "shape": "box", "kind": "lake",
        "center": [0, 0, 0], "halfExtents": [100, 50],
        "surfaceY": 3, "bottomY": -4,
        "autoLod": true, "mediumDistance": 120,
        "farDistance": 400
      }
    ]
  }
}
```

### Verification and follow-up

**Actually performed:** reviewed existing API and authored/committed C++
source, manifest handling, test assertions and this handoff through the
GitHub connector. **NOT RUN / UNVERIFIED:** native compilation, CTest,
OpenGL/Metal shader builds, visual GPU screenshots, real upload-byte
inspection, FPS/RTS parity, persistence and performance. No passing CI
status was observed for this stage.

Commands for the testing agent:
```sh
./run-tests.sh --filter '^ecs_nereid_'
./run-tests.sh
```

**Important limitations:** staged CPU water meshes are still computed
synchronously on the render/update thread; no async worker, cancellation
or total staging-memory budget across all resident surfaces. The byte cap
is exact for MeshBuilder arrays, NOT actual driver/transfer memory. No
underwater/refraction pass, shore depth attenuation, GPU displacement of
collision geometry, frustum/HiZ culling or large-world origin rebake.
Auto LOD currently has no hysteresis, so a moving camera near thresholds
may cause repeated updates. Follow-up should address async staged work,
hysteresis and precision, then implement separately validated depth/refraction
with GLSL and Metal parity. Every subsequent implementation stage must
append its own handoff with actual test evidence.

### Stage 13B — distance LOD hysteresis (incremental follow-up)

**Implementation commit:** `3871ad7cf188b5f5a5d75a4634c16464b10cdfe8`
(`engine/world/NereidWaterScene.h`).
**Manifest commit:** `372d93693035d073718138c99c67bf713aff3c45`
(`engine/world/NereidWaterManifest.h`).
**Authored test commit:** `47d658800947f4e51a42bc7b876875f262df8c98`
(`tests/ecs_nereid_renderer_contract_test.cpp`).

`NereidSceneSurface::lodHysteresis` defaults to 12 metres. With opt-in
`autoLod`, each stable water ID keeps its previous selected LOD across
snapshot reorder and uses threshold hysteresis to avoid switching detail
on camera jitter. `setSurfaces` resets the selected LOD only for that
changed surface. The manifest accepts optional `lodHysteresis` and
rejects impossible threshold/hysteresis combinations; it may be 0 to
disable hysteresis. Manual authored LOD and infinite oceans do not
use distance selection.

**Status:** Source/test commit written through GitHub; compiled C++,
CTest, Metal/GL visual verification and runtime performance tests were
**NOT RUN / UNVERIFIED**. No GPU output was inspected. Run
`./run-tests.sh --filter '^ecs_nereid_'` and the full runner; investigate
any failures before merging PR #23. Next: resolve real build/test findings,
then profile CPU mesh staging and implement optional background preparation
with bounded retained CPU memory. Depth/refraction shaders must be separately
validated on GLSL and MSL and coordinated with Prisma (postprocess owner).

## Stage 14 — opt-in asynchronous water CPU mesh preparation

**Current branch** `feature/nereid-water-core` (NEREID PR #23 remains Draft).
**Implementation commits:**
- `fa24703d9d12a92833285bb6825b3cc31e339314` —
  `engine/world/NereidWaterScene.h`: one in-flight background CPU
  water mesh via `std::async`/future (native builds), immutable copied
  body, optional geometry, style and options, nonblocking polling,
  result keyed by water ID + revision + mesh key.
- `20a2152f26f93e5719860f32e7c7dfe7a64aa391` —
  `engine/world/NereidWaterManifest.h`: `asyncPrepare` (default false)
  and `maxStagingBytes` (default 16 MiB, 1 KiB..64 MiB).
- `fcadd5b5b2f9e338650f5057e60d9655effc9365` —
  `engine/world/WorldScene.cpp`: opt-in manifest wiring.
- `202942f010a9e6d00731fc025363725bb97f33f9` —
  `engine/world/NereidWaterScene.h`: combined bounded retained CPU
  prepared surface bytes + conservative in-flight reservation.
- `51b135f2469960768cf91824d47e49bcf2fcee7d` —
  `tests/ecs_nereid_renderer_contract_test.cpp`: authored config tests.

### API, ownership and usage

`NereidWaterScene::setAsyncPreparation(bool)` is OFF by default.
When enabled on native platforms, at most **one** CPU preparation job
can be in flight, regardless of surface count. A worker owns copies
of every input and calls the existing pure CPU
`NereidSurfaceResident::prepare`; no worker accesses `World`,
`Simulation`, RHI, device or active mutable scene data.
`update` polls the future without waiting; completed results
are adopted only if their stable water ID + source revision + mesh
key still match. Removed entities and obsolete water/LOD/ocean changes
cannot publish stale GPU geometry. The actual buffer upload remains
on the scene's device/update thread, subject to the Stage 13 exact
VB+IB byte quota. `shutdown(device)` waits for any outstanding job
and then releases all GPU resources.

`setMaxWaterStagingBytes(1024..67108864)` limits the combined
prepared CPU buffer payload (VB+IB), plus conservative reservation
for the in-flight builder (based on maxVertices and an index topology
upper bound). A too-small budget defers preparation rather than silently
allocating an unbounded collection of CPU meshes. The existing
`maxEstimatedUploadBytes` and `maxUploadsPerFrame` still separately
limit GPU publication. The build performs CPU work asynchronously,
not GPU upload or OpenGL/Metal command recording.

New `NereidSceneStats` diagnostics:
`asyncPreparing`, `asyncDiscarded` and `stagingReservedBytes`.
Retained prepared surfaces may remain cached until a valid upload;
GPU-owned surfaces are preserved until a completed replacement.
On `__EMSCRIPTEN__`, default builds fall back to synchronous
preparation without requiring pthread support.

Example manifest:
```json
{
  "nereidWater": {
    "asyncPrepare": true,
    "maxStagingBytes": 16777216,
    "maxUploadsPerFrame": 2,
    "maxEstimatedUploadBytes": 8388608,
    "surfaces": []
  }
}
```

### Verification status, limits and next owner

**Actually performed**: read `AsyncChunkStream.h/.cpp` and NEREID
headers, reviewed existing scene lifecycle, wrote C++/JSON/tests via
GitHub connector. **NOT RUN / UNVERIFIED**: C++ build,
`./run-tests.sh`, CTest, thread sanitizer, mobile/Web, C++ runtime,
Metal/OpenGL scenes, performance and memory measurements. No user
hardware test reports are available in this stage.

The existing `AsyncChunkStream` is a cell-keyed world-chunk streamer;
NEREID currently uses a small dedicated one-worker future to avoid
changing its chunk lifecycle or introducing Cell-key collisions for
overlapping water bodies. The worker cannot cooperatively interrupt an
in-progress triangulation; stale results are discarded on completion,
not mid-calculation. Pending job futures can block at explicit
`shutdown`; worker currently executes at most one job in the background.
The staging cap is an upper bound for retained VB/IB arrays plus
conservative queued reservation, **not** a process-wide heap limit
or transient allocator watermark. Configure at least the expected
max water mesh size. High-distance worlds still have float position
precision issues; no new origin-rebasing or refraction was implemented
in this stage. The async feature should remain opt-in until compiler
and race checks pass.

**Next agent**: run and fix the stage-14 fixture using the unified
runner and audit future/std::async linkage on macOS, Linux, Windows and
Emscripten. Add an actual non-GPU async lifecycle test that forces a
stale job after changing a water ID/geometry/LOD and confirms it is
discarded; test exception paths, launch/shutdown and budget exhaustion.
Afterward implement origin-relative CPU mesh generation and explicit
depth/refraction on Renderer3D with shader parity. Respect Prisma's
postprocess ownership and do not alter legacy RTS waterNetwork.

## Stage 15A — precise local-origin water geometry for large worlds

**Branch**: `feature/nereid-water-core`, PR #23 (Draft).
**Stage baseline**: Stage 14 handoff `06e5c763e379a942418ed2d8d231069681767dcd`.
**Implementation commits:**
- `69e7ad014fa03ce0ddf132640afe64658464a376`:
  `libraries/include/aiverselib/water/NereidWaterSurfaceMesh.h` now
  supports double `WaterSurfaceMeshOptions::originX/originZ`; subtracts
  the origin BEFORE float conversion for all box/disc/polygon/river/ocean
  vertex positions, UV and shoreline foam calculations. Default zero
  preserves existing standalone geometry use.
- `2170dcc8dcba3cc9fb2b7f8d628ef73bae18e133`:
  `engine/render/NereidSurfaceResident.h` includes double-precision
  origin in GPU residency and prepared-mesh keys, so a change of basis
  always invalidates a cached mesh correctly.
- `db392e15083701e56e9456036b495aeb41ae6623`:
  `engine/world/NereidWaterScene.h` selects a stable body-local basis
  (box/disc center; first polygon/river point; camera ocean patch)
  and transforms each resident mesh using
  `float(meshOriginDouble - renderOriginDouble)` in `draw`.
  `WorldScene` continues to pass its existing render-origin cell and
  size; no second renderer or ECS authority was introduced.
- `f6258fca84b4054b1648b00a680e4b061c35e737`:
  `tests/ecs_nereid_renderer_contract_test.cpp` adds authored
  100,000,000-metre coordinate cases for local box/polygon vertices,
  origin-key invalidation and double-difference render origin.

### Native contract and units

The public `buildWaterSurfaceMesh(...,options)` accepts
`options.originX`/`originZ` as **world metres represented in double**.
Its resulting mesh positions X/Z are now relative to that basis, and
the renderer must place them at that world origin. With the default
0/0 options the old API still produces world coordinates. Stage 13
CPU/GPU cache keys and Stage 14 async generation preserve the origin
across preparation/publication. For the scene-level adapter, this is
automatic per water body; the per-surface render transform uses the
current `WorldScene::draw(renderer,renderOrigin,originCellSize)` values.
Changing render origin without changing the body does **not** require a
GPU mesh rebuild.

Shoreline foam tests compare all vertex X/Z against the shore edges in
the same local coordinate space. Resulting UV coordinates are local to
the mesh origin; this keeps their fractional detail but may change
the visual phase of water textures compared with world-space tiling.
Shaders should handle optional global phase separately if seamless
cross-body UVs become a requirement.

### Verification, limitations, next task

**Actual verification**: source reviewed and code/tests written to
GitHub through the connector. **NOT RUN / UNVERIFIED**: AIVERSE native
C++ compilation, CTest, full runner, GLSL/Metal rendering, framebuffer
screenshots, actual large-world precision on GPU, performance profiling.
No PASS has been claimed. Testing agent should run:

```sh
./run-tests.sh --filter '^ecs_nereid_'
./run-tests.sh
```

The 3D geometry still uses float Y; Y world origin rebasing is not yet
supported. Infinite ocean patch selection still derives from float
camera X/Z; on sufficiently large coordinates, it may jump in coarse
steps and cannot preserve sub-float camera motion. This stage is **not**
a full camera-relative RHI implementation for the entire world.
Further stages should address precise double ocean anchors and high-Y
origin/mesh transforms. Multi-plane refraction and underwater view
are not implemented; coordinate with Prisma for shared post effects,
while NEREID owns water geometry and material bindings. Retain
legacy RTS water and PHYSIS/Terra ownership boundaries.

### Stage 15B — far-world ocean patch precision

**Implementation commits:**
- `23f198137ef643efb7a679e1b7512a071ba1c4d3` —
  `libraries/include/aiverselib/water/NereidWaterSurfaceMesh.h`:
  optional `preciseCamera` and `cameraX64/cameraZ64` double fields,
  used for infinite ocean patch positions.
- `d92496014041d344edaeddf2c81206e798cbeabd` —
  `engine/render/NereidSurfaceResident.h`:
  `nereidPatchAnchor64(double,double)` and double-precision
  `NereidSurfaceKey::patchX/patchZ`; CPU preparation preserves
  the precise snapped patch rather than truncating it through float.
- `c825952a3df781ac6c749d82cb66fbc8e96a130f` —
  `engine/world/NereidWaterScene.h`:
  observer Fixed-point X/Z converted directly to double, precise camera
  mode enabled, and double-snapped ocean mesh origin.
- `3e1192365a47877c242f84ead288c7bed1a58024` —
  `tests/ecs_nereid_renderer_contract_test.cpp`:
  authored 100-million-metre ocean patch snapping, vertex-local
  positions, negative coordinates, and one-metre patch transition.

**Compatibility:** standalone callers using legacy float camera inputs
continue to work with `preciseCamera=false`; the universal scene
supplies double camera coordinates when an observer exists. The same
surface key includes snapped patch and origin; async preparation uses
the copied options. NEREID remains presentation-only, and changing
the render origin does not change authoritative ECS water coordinates.

**Verification:** GitHub connector writes completed; C++ build, CTest,
GPU screenshots, GLSL/MSL compilation, FPS/RTS regressions and timing
**NOT RUN / UNVERIFIED**. Execute `./run-tests.sh --filter
'^ecs_nereid_'` and the full runner in an AIVERSE checkout.
Precision at high world Y remains limited by the current Renderer3D
origin contract, and UV phase is surface-relative.

**Next functional owner:** implement high-Y visual origin support,
then isolate water refraction/depth inputs (and optional underwater
compositing via Prisma). Verify both Metal and GLSL and preserve the
single-reflection-plane fallback before merging PR #23.

## Stage 16 — high-Y water basis and opt-in trace-tier refraction

**Branch:** `feature/nereid-water-core` (draft NEREID PR #23).
**Starting head:** `1a10bad823d4712ffad876162d25f40ce7ce3abd`.
**No additional branches merged.**

### Implementation commits and changed files

- `f5fb1ff37e41e8a6ae961c6f82c7c4ca9420a0f3` —
  `libraries/include/aiverselib/water/NereidWaterSurfaceMesh.h`:
  `WaterSurfaceMeshOptions::originY` and subtraction of origin before
  converting water surface height to `float`.
- `6e6e29a5259a3a1d759e7a1754b8fbfaba3de5f6` —
  `engine/render/NereidSurfaceResident.h`: `originY` added to
  the GPU residency key to invalidate buffers when the vertical
  coordinate basis changes.
- `c39fd3822a6143a1ab38b41007efcc5261680c31` —
  `engine/world/NereidWaterScene.h`: uses the water surface height as
  local Y origin and offsets draws by `float(originY -
  renderOriginCellY*cellSize)`.
- `ea7aa973c6b2b5a393148c57c5239a4b11d8dd31`,
  `6f527548842ec573c844e96943f882eace2e933a`,
  `d4b7eb5125eda577ce592c5bed03b4e43fdc02d7`,
  `3078067d246d4107c3076f6f4a3dd08ebf46ffa0`,
  `53c8076caf9afb7c8c5b75721593189e55d4c389` —
  water style `refractionStrength` (defaults to zero; validated 0..1),
  passed to existing `Renderer3D::drawMesh(...,emissive)` parameter,
  cached in the material key, and configurable through the existing
  `nereidWater.surfaces[].style` manifest object.
- `6a450ca9b2c4f28ba3f45c9d15194dd8d9f181c9` —
  `engine/render/Shaders.h`: **both GLSL and Metal** now implement
  opt-in depth-gated refraction by sampling the existing scene capture
  from SSR slots 4/5. The NEREID water sentinel distinguishes the use
  of the existing draw uniform's fourth parameter as refraction strength
  instead of emissive glow; ordinary materials retain their previous
  emissive behavior. Refracted background is sampled only within screen
  UV bounds, with a valid captured opaque depth, and only if unprojected
  opaque geometry lies physically below the visible water surface.
  Water above another water plane still respects the established
  single-planar-reflection fallback.
- `25a8daa7cb4317d260f7642afe25ad4d2a6414ed` —
  `tests/ecs_nereid_renderer_contract_test.cpp`: authored high-Y
  mesh and cache-key checks, manifest invalid values, and GLSL/MSL
  depth-gate shader-source contracts.

### Usage and constraints

Example manifest material (only effective with the existing trace-tier
opaque scene colour/depth capture):
```json
{
  "nereidWater": {
    "surfaces": [
      {
        "id": 12,
        "kind": "lake", "shape": "box",
        "halfExtents": [50, 50],
        "surfaceY": 8, "bottomY": -20,
        "style": {"opacity": 0.73, "specular": 0.95,
                  "refractionStrength": 0.55}
      }
    ]
  }
}
```

`refractionStrength=0` preserves the previous water shader; the
sampling path requires the same `trace2.w` ready flag as the existing
SSR. This is a **simple screen-space opaque-background displacement
and depth-thickness attenuation**, not full Snell-ray tracing,
multi-layer transmission, caustics or a true underwater volume.
No additional RHI color/depth attachment or texture binding has been
introduced. The computed refraction contribution is clamped,
and invalid/background/above-water depth samples are rejected.

`originY` is independent of gameplay water height: the surface mesh
contains local float Y, while the scene renders it with a double
difference from the caller's render-origin Cell Y. The broader
Renderer3D camera/view-projection must still use a **compatible
floating origin** for worlds at very large altitudes; this module
cannot rebase arbitrary other world geometry on its own.
Scene renderer primitives remain on the standard RHI path.

### Verification and next action

**Actually performed:** inspected the existing Renderer3D trace capture,
SSR shader helpers, source API, wrote implementation and source-test
fixtures through the connected GitHub tool.

**NOT RUN / UNVERIFIED:** C++ compilation, unified runner/CTest,
GLSL shader compilation, Metal shader compilation, GLSL/Metal GPU
captures, actual scene depth accuracy, cross-platform build, editor
visual test, runtime performance and FPS/RTS regressions. Do not
interpret source-assertion tests as passing tests or PR readiness.

Testing agent commands:
```sh
./run-tests.sh --filter '^ecs_nereid_'
./run-tests.sh
```
In actual GL/Metal scenes additionally inspect water at multiple Y
heights, overlapping opaque bank geometry, high-Y floating-origin
movement, refraction off/on with trace tier disabled/enabled,
single-planar-reflection fallback, alpha-blend ordering and water
over the sky/background. Validate no sampling feedback hazards.

**Next:** fix compiler/shader/runtime failures first; then a dedicated
underwater volume compositor coordinated with Prisma and optional
per-pixel shoreline foam driven by depth. NEREID remains sole owner
of water geometry/material presentation and ECS sampling; do not
duplicate postprocess control, alter legacy RTS waterNetwork, or
modify PHYSIS and TerraWeave implementations.

## Stage 17 — depth optics, shoreline foam and underwater camera view

**Branch:** `feature/nereid-water-core` (draft NEREID PR #23).
**Starting head:** `b62416786d231124d641acc08e7372b97acbf2b8`.
**Other feature branches:** NOT merged or modified.
**Implementation commits:**
- `31e07517914855ff1774c5846c234b4552813cd3`:
  `libraries/include/aiverselib/water/NereidWaterSurfaceMesh.h`.
  Opt-in style parameters `absorption`, `shoreFoamDepth`, `depthTint`.
- `26682eb08f30cdb5f26a17ac72d248cf8863f969`:
  `engine/render/NereidWaterRenderer.h`. Validation and tagged water
  `drawMesh` material controls without adding uniform/texture slots.
- `97d615c6b05c09aa90ca9a8c00f24c7794cd9e65`:
  `engine/render/NereidSurfaceResident.h`. Cached visual key includes
  all new optical parameters.
- `05fb803976abf17b672fe68af9b51046ea6f02b9`:
  `engine/world/NereidWaterScene.h`. Per-body comparison includes optics.
- `2f8686c14e38a0ba00ac1f9a9b01c5baef87157e`:
  `engine/world/NereidWaterManifest.h`. JSON style parsing/bounds.
- `87f5c082afb63ecf3971a2b70912881d2eb6f249`:
  `engine/render/Shaders.h`. Depth optics and foam in GLSL and Metal.
- `8037893d76a1dac75bc57236ef767a7d163db632`:
  `tests/ecs_nereid_renderer_contract_test.cpp`. Optical source checks.
- `6125cf34ff2a77a1cca0ce69e4caa3b1ee639d2b`:
  new `engine/world/NereidUnderwaterView.h`: read-only camera optical
  state from an immutable collection of genre-neutral water surfaces.
- `b6b04d37de5045f61950208c6eadf937a02de5cc`:
  `engine/render/Shaders.h`. Depth optics composite AFTER SSR to avoid
  the reflection result overwriting shallow shoreline foam.
- `0421880f0122f89e17e0eee857d46b7dbfa4a0b8`:
  renderer contract test adds underwater selection, priority, exclusions
  and shader post-SSR ordering assertions.
- `1e1305e004b13dc6db5d6a196f16be14765ef0b4`:
  `engine/world/NereidWaterScene.h` introduces read-only per-source visitor.
- `ebeef4208371f5e23b5b43ba42fc159a89d2feec`:
  `engine/world/NereidUnderwaterView.h` supports direct scene sampling
  without copying meshes/geometry each frame.
- `7973ab79e00266e5d7f71acd3009604a37cdac3e`,
  `67cd3040fb81da8f63000898cc3c63cfbdada74e`:
  `engine/world/WorldScene.h/.cpp` exposes
  `scene.nereidUnderwaterAt(camera)` for the Prisma owner.
- `d41c3bad1195b89baa29195dfc5b7f8dd0904efa`:
  contract test of list-vs-scene camera sampling parity.

### Rendering, ownership and API

`WaterSurfaceStyle` optional params (all **zero by default**):
- `absorption` in [0,4] per metre: Beer–Lambert-like exponential
  transmission `exp(-thickness*absorption)`, where `thickness` is the
  vertical separation between the water plane and the depth-unprojected
  captured opaque surface.
- `depthTint` in [0,1]: contribution of the deep-water tint as the
  opaque background becomes attenuated.
- `shoreFoamDepth` in [0,20] metres: smooth, *screen-space* shallow
  band driven by scene depth, independent from vertex shoreline foam.
- `refractionStrength` (Stage 16) remains an independent [0,1] control.

GLSL and Metal share the **existing trace-tier SSR capture** on
texture slots 4/5 and the same water draw sentinel. To avoid a new
uniform block, NEREID tagged draws repurpose the existing per-draw
`u_color.rgb` as absorption, foam depth, tint strength; non-water
draws retain their original RGB tint semantics. `u_color.a` remains
opacity. `u_params.w` is the existing refraction control on marked
water, and remains emissive on unmarked materials. The water fragment
uses the per-vertex authored material tint, so the repurposed RGB
does not unintentionally darken the water's base colour. Depth-gated
optical effects apply after existing SSR. Invalid depth, background
samples and refracted objects above the surface leave existing water
rendering unchanged.

Example:
```json
{
  "nereidWater": {
    "surfaces": [{
      "kind": "lake", "shape": "box", "id": 17,
      "center": [0,0,0], "halfExtents": [60,40],
      "surfaceY": 3, "bottomY": -30,
      "style": {
        "refractionStrength": 0.45,
        "absorption": 0.20,
        "depthTint": 0.65,
        "shoreFoamDepth": 1.2
      }
    }]
  }
}
```

**Underwater camera view:** `sampleNereidUnderwater(surfaces,camera)`
and `sampleNereidUnderwater(scene,camera)` return
`NereidUnderwaterView` with `submerged`, `waterId`,
`waterSurfaceY`, `depthMetres`, `transmission`, `absorption`,
`deepR/G/B` and `turbidity`. Only enabled, containing water
volumes participate, with priority > height > ID tie breaks. These
queries do **not** read an active writable ECS pool, mutate simulation,
create a genre dependency or perform any GPU draw. Native scenes
consume `WorldScene::nereidUnderwaterAt(Vec3 camera)`.

**Prisma boundary:** this stage exposes a *read-only optical input*
for the separate Prisma postprocessing owner. A fullscreen camera
underwater compositor is **NOT yet wired** into Prisma or PostFx,
and must not be reported as implemented. Prisma should accept the
returned optical parameters through its own existing per-frame
parameter and quality API; water membership comes from NEREID and is
not recomputed by the postprocessor.

### Verification, risks and next action

**Actually performed:** GitHub connector source review and commits,
authored source-level tests for material bounds, cache invalidation,
GLSL/MSL shader optical fragments, underwater volume priority, missing
volumes, and scene-vs-snapshot parity.

**NOT RUN / UNVERIFIED:** native C++ compilation, CTest, full
`./run-tests.sh`, GLSL/Metal compiler, device render, framebuffer
capture, depth thickness accuracy, performance, FPS/RTS regressions,
and interoperability with Prisma. Authored fixture assertions are
NOT a passing execution result.

```sh
./run-tests.sh --filter '^ecs_nereid_'
./run-tests.sh
```

**Known limitations:** simplified screen-space vertical thickness, not
Snell refraction or physical light transport. Depth-driven foam uses
a constant pale tint, not custom per-pixel foamColor; existing authored
vertex foam still uses the configured material tint. Disabled trace
tier or absent opaque depth falls back to the older appearance. Thin
objects, steep banks, transparent objects and multiple water layers
may create visual approximations. Neither shaders nor the entire
Render3D/PostFx pipeline have been compiled or tested in this turn.

**Next agent:** run tests and fix compile/GLSL/MSL and RHI shader
integration issues first. Then implement the Prisma-owned fullscreen
underwater visual pass (camera fog/colour/exposure), using
`WorldScene::nereidUnderwaterAt` rather than adding duplicate water
detection to Prisma. Review underwater camera transitions, crossing
surface from both sides, foam near steep banks, shallow/high-opacity
blending, quality presets, and GL/Metal parity. Update this handoff
immediately after each implementation stage.

## Stage 18 — Prisma-owned underwater fullscreen composite (opt-in)

**Branch:** `feature/nereid-water-core` (PR #23, Draft).
**Starting head:** `9c9bdefe961b08b8844edb0da147bc8fc726a582`.
**No main/other feature branches merged in this stage.**

**Implementation commits:**
- `d6db9a29fd1d9487a0a6fbf1cec07163fbbf53c2` —
  `engine/render/PostFx.h`: data-only `PostFxUnderwater` and
  `PostFxUnderwaterState`, opt-in `PostFx::setUnderwater`.
- `cfd98effebf5de26597eb2546ff56c4fd0b9ecea` —
  `engine/render/PostFx.cpp`: `PF_UNDERWATER` composite bit,
  two extra packed vec4 uniform fields, per-frame material handling,
  no new fullscreen texture attachments or standalone GPU pass.
- `7dcf318894d19b456b7b14675b255d9e6b1de350` —
  `engine/render/PostFxShaders.h`: one common GLSL/MSL shader body
  for underwater screen tint, Beer–Lambert-style absorption and
  near-surface exposure decrease.
- `5a1cc2b2a9fc9d0be0f2fe69ff96d500043ca9eb` —
  `engine/world/NereidPrismaBridge.h`: explicit one-call adapter
  from WorldScene underwater camera optical snapshot to PostFx.
- `81c7837755a5b87c8690af3c7aed6c58295f4381` —
  `engine/render/PostFx.h`: GPU-free
  `advancePostFxUnderwater(state,target,deltaSeconds)`.
- `bede0492ee7f3e085c18808d8a7eb873a43b5b73` —
  `engine/render/PostFx.cpp`: runtime reuses pure fade policy.
- `f5569fcb7b49def83d9ed4e75d605696bea78805` —
  `tests/ecs_nereid_renderer_contract_test.cpp`: shader feature
  source contract, entrance/exit smoothing, invalid input clamping.
- `adce6b3704ec3c7447fa887a650ec2be2d9938d5` —
  `engine/render/PostFx.cpp`: no retained underwater effect after
  PostFx shutdown/device teardown.

### Runtime ownership and usage

The NEREID ECS/world surface definition remains authoritative.
`WorldScene::nereidUnderwaterAt(cameraWorld)` supplies a copied,
read-only camera snapshot (Stage 17); Prisma/PostFx owns only its
**visual state and fullscreen composite**.

```cpp
#include "engine/world/NereidPrismaBridge.h"
// Once per rendered frame BEFORE renderer.render(pass, &prisma):
aiverse::world::updateNereidPrismaUnderwater(
    worldScene, prisma, cameraWorldPosition, frameDeltaSeconds);
renderer.render(pass, &prisma);
// Render other scene overlays, then renderer.resolvePostFx() per existing host.
```

This call must run each frame, including when the camera exits the
water (the target then becomes inactive, and the current visual
strength fades to zero). The transition is time-based,
`alpha=1-exp(-6*clamp(dt,0,0.25))`. Any project with no underwater
input retains PostFx's original behavior; shader variants are not
selected unless the underwater state is active. Existing editor/client
PostFx effects and quality settings remain separate, and disabled
PostFx/quality Off disables underwater as expected.

Composite parameters: `underwater0={deepR,deepG,deepB,blend}`,
`underwater1={absorption,depthMetres,0,0}`; two additional vec4
fields on existing U0, no extra texture binding. A common shader
body compiled through the existing `PostFx::composite` builder
calculates `1-exp(-absorption*opticalDistance)`, fades toward water
tint and gently reduces light with depth. It is a **screen-wide
approximation**, not depth-per-pixel volumetric absorption, caustics,
physical scattering, distortion, or true view-ray intersection.
The current `PostFxStats::effects` counter includes the active
underwater composite feature. PostFx shutdown resets the visual state.

The opt-in bridge is header-only; it does not register a new ECS
component or depend on a specific FPS/RTS game. No automatic host
integration has been added: native projects must explicitly call the
bridge and render using the existing PostFx hook. This avoids the
NEREID WorldScene acquiring and mutating a Prisma owner.

### Verification and handoff

**Actual work:** inspected the PostFx composite pipeline and shared
GLSL/MSL shader generator through connected GitHub, wrote C++/shader
source, bridge, and unit-test source. Checked the matching C++
`CompositeU` vec4 fields and generator field list by source inspection.

**NOT RUN / UNVERIFIED:** complete C++ build, CTest,
`./run-tests.sh`, GLSL and MSL shader compilers, OpenGL/Metal
framebuffer captures, true camera transition appearance, mobile
performance, FPS/RTS regression. Source assertions and connector
commits do not imply passing native tests. Keep PR #23 Draft.

```sh
./run-tests.sh --filter '^ecs_nereid_'
./run-tests.sh
```

**Next agent:** compile and fix C++/MSL/GLSL errors, then test entering,
leaving and switching overlapping water volumes, HDR vs SDR,
FXAA/bloom/grade/fade order, inactive PostFx and render-target resize.
Do not merge until graphics and gameplay regressions are verified.
After baseline validation, consider smooth surface-crossing ripple,
per-pixel view depth integration and configurable mobile quality
profiles using the Prisma-owned composite path, not a new
full-screen water renderer.

## Stage 19 — underwater screen distortion, boundary impulse and scattering

**Branch:** `feature/nereid-water-core`, NEREID draft PR #23.
**Starting head:** `94d8add0f3f5c0da968dcb7209e3d9c91c41d602`.
**No `main` or unrelated feature-branch merges in this stage.**

### Implementation commits

- `36bee2ae0b743927ce87cb8c6a1feb662703273a`:
  `engine/render/PostFx.h`; `PostFxUnderwater` and
  `PostFxUnderwaterState` gain `distortion`, `scattering` and
  a decaying `crossing` impulse. The existing pure
  `advancePostFxUnderwater` performs frame-time smoothing and triggers
  a crossing pulse only when the submerged flag changes.
- `7df33c83d3ec9117380e05aa145b8a914caafeda`:
  `engine/render/PostFx.cpp`; packs the two optical strengths into
  unused `underwater1.zw` and crossing impulse into
  `graphFrame.y`, without adding uniform bindings/passes.
- `76ac50ff5598283813269c416d7e42f4d4bad9dd`:
  `engine/render/PostFxShaders.h`; a shared GLSL/MSL
  `PF_UNDERWATER` block applies bounded animated UV distortion,
  a decaying boundary ripple, and an approximate extinction-aware
  scattering contribution. The source sample comes from the already
  resolved scene texture; UV is clamped away from the screen edge.
- `57c90767ed159de5cc92e5db7941b3a95b29f471`:
  `libraries/include/aiverselib/water/NereidWaterSurfaceMesh.h`:
  opt-in `underwaterDistortion` / `underwaterScattering` style fields.
- `cd373ea4081af564a78ccc4fe8e6d95a68107b9d`:
  `engine/render/NereidWaterRenderer.h`: validate the new strengths.
- `107ea8f8ea260b47527da6894993461ff31ceb11`:
  `engine/render/NereidSurfaceResident.h`: optical fields invalidate
  the presentation material residency key.
- `12602dba3ac432ac3cfc057876999984e8deab43`:
  `engine/world/NereidWaterScene.h`: source change detection.
- `658e0f206487473fa5b8dc72b4ccd6c54d83ae99`:
  `engine/world/NereidWaterManifest.h`: JSON optical parameters.
- `dcc00501b17775b9ca5c8b9c8c584a17ca34894c`:
  `engine/world/NereidUnderwaterView.h`: read-only camera view output.
- `b66dd00ccb068adbd5f812f30e08f52a9e921a8c`:
  `engine/world/NereidPrismaBridge.h`: transmits optical parameters
  via existing WorldScene → Prisma per-frame bridge.
- `a837f694241d45141299197367929f8b1f4630a2`:
  `tests/ecs_nereid_renderer_contract_test.cpp`: authored optical
  style, cache/JSON, camera snapshot and crossing/shader source checks.

### API and behavior

Manifest example:
```json
{
  "nereidWater": {
    "surfaces": [{
      "id": 22, "kind": "lake", "shape": "box",
      "surfaceY": 4, "bottomY": -12,
      "halfExtents": [40, 30],
      "style": {
        "absorption": 0.20,
        "underwaterDistortion": 0.7,
        "underwaterScattering": 0.45
      }
    }]
  }
}
```

Both new strengths default to 0, are clamped/validated to [0,1],
and belong to the water **presentation style**, not authoritative ECS
hydrodynamics. NEREID selects the containing body and supplies
`NereidUnderwaterView::distortion/scattering` to Prisma.
`PostFx::setUnderwater` smooths optical parameters using
`1-exp(-6*dt)`; each transition of the submerged flag starts
a transient crossing pulse decaying as `exp(-8*dt)`.
Crossing adds a short-lived rippling distortion even for water with
`underwaterDistortion=0`; no-effect scenes do not call the underwater
bridge and stay unchanged.

Prisma performs all full-screen work in its **existing composite pass**.
`underwater1.zw` holds distortion/scattering; `graphFrame.y` holds
the transient crossing pulse. GLSL/MSL use the existing
`postfx::buildProgram` shared common subset. No new textures, RHI
backend, render passes, ECS components, gameplay effects, or duplicated
water-volume detection were introduced.

### Verification / limits / next agent

**Actually completed:** connected GitHub source inspection,
implementation commits, authored source and CPU state test assertions.
**NOT RUN / UNVERIFIED:** C++ compile, CTest, full `./run-tests.sh`,
GLSL/MSL compilation, GPU scene rendering, visual transition QA,
mobile performance, and FPS/RTS regression. Keep PR #23 Draft.

```sh
./run-tests.sh --filter '^ecs_nereid_'
./run-tests.sh
```

Known limitations: fake screen-space waves, 2D scene-colour
distortion, no refraction intersection ray marching or physically
measured scattering, and no depth per object in the Prisma compositor.
Optical distortion samples the original pre-composite scene, so combined
shader graph/grade ordering should be visually reviewed. Boundary
transitions are based on the water containment flag, not a physical
wave height or camera collision geometry. The shader's crossing
impulse is hard-coded and not yet configurable per water type.
PostFx quality `Off` / disabled remains an opt-out.

**Next agent:** obtain native compiler and real GL/Metal GPU evidence;
fix any source or shader errors. Then tune the crossing impulse with
separate mobile/desktop quality settings, guard shader graph behavior,
and consider GPU depth-aware underwater distance using Prisma-owned
resources without introducing a redundant water RHI. After any
implementation, append a new handoff section with exact SHA/files,
actual test results, limitations and next action.

## Stage 20 — mobile underwater quality profiles and soft boundary transition

**Branch:** `feature/nereid-water-core`, NEREID PR #23 (Draft).
**Starting head:** `bd24f391bebefe84e9ea06936a252ff13a263e11`.
**No unrelated feature branches or additional main merged.**

### Code commits

- `d396b3978d51a01fd0d107c98612da62fe00028f`:
  `engine/render/PostFx.h` adds `PostFxUnderwaterBudget` and
  `postFxUnderwaterBudget(PostFxQuality)`, mapping quality Off /
  Low / Medium / High to bounded wave-complexity 0/1/2/3 and
  distortion/scattering/crossing multipliers. Adds optional
  `PostFxUnderwater::surfaceBlend` (default -1 means legacy binary)
  and smooth target opacity to the existing temporal camera policy.
- `e6f209b14e810925d44e10bd0dc40fcbddb047aa`:
  `engine/world/NereidUnderwaterView.h` samples water volumes up to
  0.4 metres above their surface for *presentation only*, exposes
  `surfaceBlend` using `clamp(0.5 + signedDepth/0.8,0,1)`.
  `submerged` still means actual camera Y below surface.
- `754879f43f73b893e4043fd1b230eae84ec5e23c`:
  `engine/world/NereidPrismaBridge.h` forwards water proximity fade.
- `d3d2731089dfda87f3e840393bf31afee383b1f3`:
  `engine/render/PostFx.cpp` appends `underwater2` to the shared
  composite U0 vector set (18 vec4 fields total); the configured
  PostFx quality determines shader work while preserving existing
  PostFx pass count and texture binding layout.
- `083ec0c90a37499509b89732f0b9c9e44458be47`:
  `engine/render/PostFxShaders.h` conditionally evaluates 1, 2
  or 3 procedural distortion waves depending on
  `underwater2.z`; Low mode skips higher-cost wave expressions.
  Low/Medium/High also scale scattering and the entry/exit pulse.
  The shader stays a single shared GLSL/MSL body.
- `e52e75a2f2a0fd9e5ff55f0776493a66b6b24866`:
  `tests/ecs_nereid_renderer_contract_test.cpp` extends authored
  GPU-free contract assertions for quality budgets, above/under
  proximity, crossing state and shader tier branches.

### Public contract and expected behavior

A native host already using:
```cpp
aiverse::world::updateNereidPrismaUnderwater(
    worldScene, prisma, cameraWorldPosition, frameDeltaSeconds);
```
needs no new integration API. The existing `PostFx::setQuality` determines
underwater shader work. Quality Off disables it; Low (mobile) runs the
cheapest single directional wave, Medium two, High three. There is no
separate water shader, shader pipeline or GPU render target.

A water volume can now supply optical tint and fade while a camera is
within 40 centimetres **above** the water surface; full opacity is
reached about 40 centimetres below the surface. ECS gameplay contact,
buoyancy and body priority/persistence are unchanged. The
`submerged` boolean retains its actual geometric meaning. The
`surfaceBlend` result is input only for Prisma's visual transition.
Existing callers that construct `PostFxUnderwater` manually and leave
`surfaceBlend=-1` retain the previous hard target/soft temporal fade.

The display still uses `1-exp(-6*deltaSeconds)` smoothing, and
the surface-crossing ripple remains generated by the transition
of the actual submerged flag. Low-cost quality tiers reduce
distortion/scattering and crossing intensity, not the underlying
simulation.

### Verification and limitations

**Actually performed:** GitHub source inspection and commits,
authored contract fixtures. **NOT RUN / UNVERIFIED:** complete native
compilation, `./run-tests.sh`, CTest, GLSL/MSL shader compilation,
Metal/OpenGL framebuffer capture, mobile GPU timing, HDR/SDR/FXAA and
NEREID gameplay parity. PR #23 stays Draft.

```sh
./run-tests.sh --filter '^ecs_nereid_'
./run-tests.sh
```

The cost tiers currently reduce **procedural wave math**, but all tiers
still perform one underwater source texture sample for distortion when
active. No verified device-specific performance savings are claimed.

**Not implemented:** true per-object/per-pixel underwater camera depth,
additional scene-depth attachment sampling within Prisma, depth-aware
occlusion of background, real refraction ray marching, physical
subsurface scattering or postprocess wet-lens simulation. Reusing
PostFx scene depth safely across GL and Metal requires a separate
RHI texture sampling contract and device tests; do not confuse the
existing NEREID water-surface SSR depth path with a Prisma fullscreen
depth input. A near-above camera can enter the *visual* fade band while
`submerged=false`; higher-priority nearby overlapping bodies may
control that visual band. Test shoreline crossings and adjacent water
volumes visually before enabling universally.

**Next agent:** compile tests, fix new and prior shader/C++ errors,
measure low/medium/high on mobile Metal/GL, and only then add safe
PostFx full-screen depth sampling with an explicit scene-depth
resource and linear-view-distance reconstruction. Preserve Prisma's
postprocessing ownership and NEREID's geometry/ECS authority. Add the
next handoff section after every implementation stage.

## Stage 21 — Prisma per-pixel underwater scene-depth sampling

**Branch:** `feature/nereid-water-core`, Draft PR #23.
**Starting point:** Stage 20 handoff `35218a99b9c40cab107dbbfcb1247413f546b412`.
No unrelated feature branch or fresh main merge in this stage.

### Implementation commits/files

- `d2477d356afba19fe2185e898b9d8087d4af220d`:
  `engine/render/PostFx.h` distinguishes main scene depth from
  anti-alias/intermediate depth targets using a composite (size,role) key.
- `94143dce9fcf3b3bd136ef6eb004f3b2039755ca`:
  `engine/render/PostFx.cpp` extends the existing composite uniform
  block with four inverse-view-projection columns and camera position,
  and reserves an optional slot 5 for the scene depth texture.
- `4ef0ccf49af53e6e153bf3d890ba3ff6d3b5a453`:
  `engine/render/PostFx.cpp` binds completed scene depth only when
  the underwater effect is active, PostFx quality is Medium/High,
  there is no ShaderGraph texture-slot collision, depth attachment
  is valid and it differs from the destination attachment.
- `a68d469a8fe60a0b98630aec2328a8975b88c861`:
  `engine/render/PostFx.cpp` emits backend-specific
  `PF_DEPTH_SAMPLE` macros, using GLSL `sampler2D` versus Metal
  `depth2d<float>`.
- `006db2c6744759327d313672ed434e32bba27c76`:
  fixes escaping of shader macro line breaks so generated GLSL/MSL
  preprocessor directives end at actual newlines.
- `8cc84dd49fbd72e4f62c0794243aeca321791711`:
  `engine/render/PostFxShaders.h` uses optional per-pixel scene
  depth, clip-range-aware NDC Z, and inverse view-projection to
  reconstruct each opaque pixel's position and compute view distance.
- `3c11342b4713af30c79c04b76d1fe42e59d54ce0`:
  the full-screen compositor retries the existing non-depth underwater
  shader when the depth variant cannot be created on a backend.
- `36395ee5852fedf782e605fbfe384833d2ae9865`:
  `tests/ecs_nereid_renderer_contract_test.cpp` source-contract
  checks for GLSL/MSL-agnostic reconstruction, NDC clip ranges, sky
  fallback and quality-tier handling (authored, not run).
- `1877bc250bee88fe06dc097b1e90f5e6da74cb49`:
  intermediate PostFx effects use their own depth attachment (role 2),
  avoiding inadvertent clearing of the main scene depth.
- `de8f65bd56711dd935e80f6332373175d893a83a`:
  includes required `<utility>` for keyed depth resources.

### Contract / ownership

NEREID remains the sole source of water bodies and camera optical
parameters. Prisma owns the full-screen rendering and scene depth.
The existing `updateNereidPrismaUnderwater(worldScene,prisma,camera,dt)`
API and water JSON format are unchanged.

On Medium/High, when eligible, Prisma renders the opaque scene to its
existing depth texture, finishes that scene pass, then optionally binds
that depth texture at sampler slot 5 of the existing composite. No
second scene capture or second fullscreen render pass was introduced.
The two different clip conventions are handled by `underwaterCamera.w`
(0 = [-1,1] OpenGL clip depth; 1 = [0,1] Metal/other zero-to-one depth).
`inverseViewProj0..3` represent column-major inverse VP. World
reconstruction uses screen UV in Y-up coordinates and samples depth
with the renderer's `TUV` flip convention. Underwater optical distance
is the actual distance from the view camera to the reconstructed opaque
pixel, capped at 120 metres, instead of the earlier camera-water depth
heuristic. Background/sky depth near 1, invalid W and unavailable depth
fall back to Stage 20's heuristic.

**Feature gating:** Low tier retains the cheap heuristic. Medium/High
enable optional per-pixel depth only if the texture can be sampled
without feedback, no ShaderGraph texture slot conflict exists, and the
backend accepts a depth sampler shader. The shader compiler falls back
to the original non-depth underwater variant when needed. All normal
PostFx effects remain independently configurable and underwater
remains opt-in. Intermediate FXAA/bloom/fullscreen passes no longer
clear the main scene-depth texture; the final anti-alias pass has
another depth resource.

**Important limitations:** This is view-ray distance to the
depth-captured opaque geometry, NOT physically correct
per-pixel underwater segment length through arbitrary multiple
water surfaces. The water surface height/volume membership comes
from a single NEREID camera query. Transparent geometry, sky pixels,
thin geometry and mixed depths continue to use screen-space
approximations. Depth from overlapping NEREID water and custom
renderers requires real GPU tests. ShaderGraph compositing currently
uses the non-depth fallback to avoid sampler slot conflicts.
No changes were made to gameplay ECS, PHYSIS, RTS `waterNetwork` or Terra.

### Verification and follow-up

**Actually done:** reviewed Renderer3D/PostFx/RHI existing
API and current shader sources through GitHub, pushed implementation
commits and authored source-contract tests. **NOT RUN / UNVERIFIED:**
C++ build, CTest, `./run-tests.sh`, actual OpenGL or Metal shader
compilation, framebuffer screenshots, mobile GPU performance, visual
depth reconstruction and previous RTS/FPS regression. Source-level
contract assertions do not imply PASS. PR #23 must remain Draft.

```sh
./run-tests.sh --filter '^ecs_nereid_'
./run-tests.sh
```

**Next agent:** run/repair compiled tests and shader creation on real
Metal and GL. Compare depth-aware and fallback underwater views in
Low/Medium/High; test fullscreen passes, ShaderGraph, HDR/SDR,
repeated resize, clip ranges, and invalid depth, ensuring no
read/write attachment feedback. Then add **true underwater ray
segment clipping against NEREID water surface** for shallow scene
depths, with independent quality gates. Continue to append actual
handoff after every stage.

## Stage 21B — submerged segment length when view ray crosses water surface

**Branch:** `feature/nereid-water-core`, draft PR #23. This is a
follow-up to Stage 21's already-existing optional per-pixel scene
depth feature, not a duplicate feature branch or a merge.

**Implementation commits:**
- `ab6f4581192d9de69690316ed8824ddac02ab94c` —
  `engine/render/PostFx.h`: signed `surfaceOffset` on the per-frame
  and smoothed underwater structs, plus pure CPU
  `postFxUnderwaterRayFraction(startDepth,endDepth)` reference policy.
- `59b0cdbb08d80838fac7cd30a357c8dc568b1cdf` —
  `engine/world/NereidPrismaBridge.h`: computes signed
  `waterSurfaceY - cameraY` from NEREID's read-only camera view,
  clamped in double precision before converting to float.
- `fc4d7330040b51e54116950d18f28d8f8f5f876e` —
  `engine/render/PostFx.cpp`: passes signed camera-to-water plane
  offset in previously reserved `underwater2.w`. No new UBO fields,
  render passes or texture samplers.
- `6f068e37fbb997be98d14ababe9b194e34a027fc` —
  `engine/render/PostFxShaders.h`: existing `PF_UNDERWATER_DEPTH`
  shared GLSL/MSL shader variant now computes start and end signed
  distances to the water plane and multiplies the reconstructed
  camera-to-opaque ray distance by its immersed fraction. Fully
  airborne segments have zero water length, fully submerged segments
  retain their full length, and rays crossing the surface are split
  by the linear intersection fraction.
- `6cf3a35382f3d547aa7a425b36ab7b1f66a83921` —
  `tests/ecs_nereid_renderer_contract_test.cpp`: CPU-policy
  and shader-source contract tests for both crossing directions,
  full immersion, airborne rays, NaN, and signed offset.

### Integration behavior and limits

The original `WorldScene::nereidUnderwaterAt` and
`updateNereidPrismaUnderwater` API shape is unchanged. A camera
near/above the surface can now see submerged opaque geometry through
the water without applying underwater scattering to the part of the
view ray in air. This improves Stage 21's medium/high depth path.

The implementation uses **one horizontal water surface at the selected
camera volume**, so it is not a generalized ray-volume intersection
across multiple lakes, cave pools, waves, curved boundaries or
sloping banks. Water/Prisma ownership remains unchanged. The Low
quality or unavailable depth path retains the earlier no-depth
approximation.

**Actual verification:** GitHub source review and commits; CPU and
shader-source tests authored. **NOT RUN / UNVERIFIED:** native C++
compilation, CTest, GL/Metal shader compiler, GPU screenshots, mobile
performance and FPS/RTS regression. Keep PR Draft until test results
are recorded. Run the unified runner and render scenes that look
across the surface in both directions, including close geometry,
sky and intersecting volume priority cases.

**Next stage:** prioritize real native compiler/graphics validation;
then evaluate full camera-ray intersection with finite water
footprints, per-pixel fog scatter quality and mobile metrics, without
duplicating Prisma fullscreen passes. Append exact commits and
actual test outcomes after each stage.

## Stage 22 — deterministic universal water object interactions (features first)

**Branch:** `feature/nereid-water-core`, draft PR #23.
**Baseline:** Stage 21B head `3dccc0abbdcc0354e830b5e2726c7bc1ed53d5b6`.
**Instruction from owner:** ship **functional features before tests**;
do not add/run tests in this stage. Follow the same per-stage handoff.
No other branches merged; `main` has not been modified.

### Implementation commits and source files
- `17aa20659cf7cdbd4b2c95752bd5e3407abf473e`:
  NEW `libraries/include/aiverselib/water/NereidInteractions.h`;
  fixed-point per-actor interaction configuration/history/output,
  deterministic shared ECS system and versioned persistence.
- `515bc35237c83821c164e95da158d1553b35cc01`:
  corrected prepared ECS pool access (`entity.index`) and output
  clearing when the state pool is missing.
- `3f4fbbdaa3924a822f3a979fe8af1caa978b28d7`:
  NEW `libraries/include/aiverselib/water/NereidInteractionPresentation.h`;
  read-only, generation-safe, tick-filtered and bounded event snapshots,
  deterministically sorted by actor Entity packed ID.
- `fd652a9fc2815ed8759049499cbb54432fb90658`:
  `libraries/include/aiverselib/water/NereidNativeWater.h`:
  `NativeWaterPipelineConfig::enableInteractions` opt-in.
- `d9b2fd7252914d51b95151844fdde3a9277d3cd0`:
  `libraries/catalog.json`: package
  `AIVERSE::lib_nereid_interactions` for native game projects.
- `e9d6b6dcdb71201684ae07da83009799efada3a9`:
  interaction wake depth restriction and validated bounds.
- `1f0bbcb40b4a6232daad779ff2e2ac804804e9f8`:
  `skills/gameplay/aiverse-nereid-water/SKILL.md`: API and usage
  contract for future AI agents.

### Architecture, API and authoritative ownership

Existing `WaterBodyComponent`, `WaterContactComponent` and
`WaterProbeComponent` remain shared NEREID ECS policy. Nothing in
the new interaction system alters the water bodies, applies impulses
to host rigidbodies or depends on an RTS/FPS genre.

New actor ECS components:
- `WaterInteractionComponent`: `wakeSpacing`, `surfaceWakeDepth`,
  `wakeRadius`, `splashRadius`, `strength`, `emitSurfaceWakes`,
  `enabled` (fixed-point; bounded).
- `WaterInteractionStateComponent`: initialized flag,
  previous immersion/body/surface/position and wake anchor.
  **Persist this for save and rollback**, otherwise replay would
  spontaneously create entry/wake events.
- `WaterInteractionOutputComponent`: one bounded event per actor
  per tick, `kind/water/position/radius/strength/tick`.
  Event kinds: `None`, `Enter`, `Exit`, `SurfaceWake`,
  `SwitchBody`.

Native use:
```cpp
#include <aiverselib/water/NereidNativeWater.h>
#include <aiverselib/water/NereidInteractionPresentation.h>
aiverse::lib::nereid::NativeWaterPipelineConfig cfg;
cfg.enableGeometry=true;
cfg.enableBuoyancy=true;       // optional, independent
cfg.enableInteractions=true;   // NEW
aiverse::lib::nereid::registerNativeWaterPipeline(sim,"water",cfg);

// Each participating actor must have:
// WaterProbeComponent, WaterContactComponent,
// WaterInteractionComponent, WaterInteractionStateComponent,
// WaterInteractionOutputComponent.
// On a post-step presentation read barrier:
auto events=aiverse::lib::nereid::snapshotWaterInteractionEvents(
    sim.world(),sim.tick());
for(const auto& e:events) {
    // Host dispatches to SpectraFX/SFX/ripple presentation.
}
```
Schedule: native probe-from-Transform (if enabled) ->
adaptive/contact sampling -> buoyancy (if enabled) -> interaction
emission -> host presentation. Missing/stale current-tick sample
never produces a new event; initial submerged spawn initializes
history without a fake splash. Enter, Exit and switching bodies
emit explicit events. Surface wakes are distance-spaced and require
a near-surface contact; deep swimmers do not leave a surface wake.
Outputs reset each tick so consumers must match `output.tick`.
The helper returns a deterministically ordered snapshot, not an
authoritative mutable global event queue.

VFX spawning and ripple rendering remain **host integrations**, not
automatically performed by this ECS module. Configured event size
and strength allow different visuals per project without genre
dependencies. ECS state is serializable via the registered
`nereid.interaction`, `nereid.interaction_state` and
`nereid.interaction_output` schema identifiers, version 1.

### Verification, limits and next functional stage

**Actually performed:** inspected connected GitHub NEREID policies,
current ECS pool methods, authored source/library catalog/skill and
committed all changes. **Native compile, CTest, full run-tests, GPU
visual checks, FPS/RTS gameplay parity: NOT RUN / UNVERIFIED.**
Tests were deliberately deferred per owner instruction.

Known limits: no automatic VFX/audio playback or water surface
height deformation; contact sampling is a flat body surface and
does not reflect GPU displacement waves. Each actor outputs at most
one event per tick; wake density is distance-throttled. Physical
splash momentum, fluid momentum transfer, floating wake propagation
and waterfalls are future work. The host must add all relevant actor
components and own rigidbody integration.

**Next step (features before tests):** implement a renderer-neutral
transient ripple/wake field consuming these read-only events and
supporting radial propagation, decay and bounded memory. Bridge
to water shader via NEREID presentation data, with optional SpectraFX
spawn mapping. Then implement waterfalls/cascades and physical flow
interactions. Update this handoff after each stage. Run tests only
after functional backlog is done or owner changes priority.

## Stage 23 — expanding water ripple/wake presentation and VFX handoff

**Branch:** `feature/nereid-water-core`, Draft PR #23.
**Starting point:** Stage 22 head `326db9511494d2ecfa18332a454487f59bf99b62`.
**User priority:** features first; no new tests/run tests until functional
work is complete. No merge of other branches or changes to `main`.

### Actual commits and changed files

- `7f24f9a3f1d5b342e14d672826d8387fd1cba288` —
  `libraries/include/aiverselib/water/NereidInteractions.h`: cleans
  a redundant, misindented wake anchor assignment during initial
  contact history initialization.
- `75b91882bef25fb635fda8b3b0024430de28a1cd` —
  NEW `engine/world/NereidRipples.h`. A bounded, renderer-aware
  **presentation-only** expanding ripple field:
  `NereidRipple`, `NereidRippleField::ingest`, `advance`, `draw`,
  `clear`, `setLimit`, and `ripples`.
- `b770f18c754c4d6080a6a27801e8242009153026` —
  `engine/world/WorldScene.h`: opt-in water event ingestion, callback
  for SpectraFX/audio, render-time decay, read-only ripple accessor.
- `c7ddb743d09f06019c9775183ca1d94e556c9ee4` —
  `engine/world/WorldScene.cpp`: render expanding translucent ring
  triangles after NEREID scene water; clear the presentation field on
  world-scene shutdown.
- `75a533ecae7a7ab3f03db3194153e9789d60538e` —
  `skills/gameplay/aiverse-nereid-water/SKILL.md`: native usage and
  ownership rules for the next agent.

### Architecture / API

The canonical Stage-22 `WaterInteractionOutputComponent` stays
authoritative and is sampled once per completed tick using
`snapshotWaterInteractionEvents(world,completedTick)`. No changes to
the gameplay integrator, contact sampling, deterministic water body,
PHYSIS, Box3D, SpectraFX implementation or Prisma were required.

```cpp
#include <aiverselib/water/NereidInteractionPresentation.h>
// After the NEREID water interaction system has completed its tick:
auto events=lib::nereid::snapshotWaterInteractionEvents(sim.world(),sim.tick());
worldScene.ingestNereidInteractions(events,sim.tick(),
    [&](const lib::nereid::WaterPresentationEvent& e) {
        // The native host maps e.event.kind to SpectraFX/SFX; optional.
    });
// Once per presentation frame:
worldScene.advanceNereidRipples(deltaSeconds);
// Existing WorldScene::draw(renderer,renderOrigin,cellSize) renders rings.
```

`NereidRippleField` holds up to 256 active ripples, oldest-first
eviction and constant-bounded memory. Events of kinds `Enter`,
`Exit`, `SwitchBody` and `SurfaceWake` create pulses with distinct
propagation speed, initial radius and lifetime. Pulse age advances
using bounded non-negative frame time. Its ring mesh consists of
24-segment double-precision-origin-relative alpha triangles, emitted
through existing `Renderer3D::triangle` without persistent per-pulse
GPU buffers. The callback is explicitly a native project hook,
not an invented SpectraFX call or an automatic VFX installation.

Repeated ingestion of the same tick is ignored. Decreasing completed
tick (rollback) clears all old visual waves to avoid ghost history.
`NereidRippleField::setLimit` caps memory between 1 and 256.
Rendering is a presentation-only overlay at surface Y+0.035m;
no ECS state or physics force is affected, and simulated water
heights remain the source of truth.

### Verification and next feature

**Actually performed:** inspected existing NEREID interaction outputs,
WorldScene update/draw ownership and Renderer3D triangle API from the
connected GitHub repo; wrote the implementation, skill and handoff
using connected GitHub actions.

**NOT RUN / UNVERIFIED:** local C++ compiler, project build, CTest,
unit tests, Metal/GL shader rendering, visuals in real scenes,
FPS/RTS parity and automatic SpectraFX playback. No new tests
authored per current user instruction.

**Known limitations:** overlay rings are a visual approximation,
not a GPU water normal/displacement field or true wake simulation.
There is not yet an actual SpectraFX effect prefab mapping or
automatic rendering of particles/spray; the host gets a callback.
Visual rings are not clipped to shorelines and may extend past a
finite footprint; no depth-awareness or occlusion beyond the
existing overlay pipeline. Scene host must explicitly advance the
ripples per frame and pass the render origin; they do not age from
the deterministic ECS tick. Origin rebasing prevents XY precision
loss in ring placement but not all Renderer3D floating-world limits.

**Next functional stage (before tests):** add genre-neutral
water-surface shader perturbation driven by a bounded ring parameter
array or a renderer-provided ripple texture, plus real SpectraFX
mapping from event kinds. Prefer the existing RHI and NEREID
ownership boundaries. Then waterfall/cascade geometry and current
forces. Update this document after the next implementation stage.

## Stage 24 — ripple normals integrated into existing water shaders

**Branch**: `feature/nereid-water-core`, Draft PR #23.
**Baseline**: Stage 23 handoff at
`506ff8c62071f082a2d4568a33b3b6fa81b92cb4`.
**Instruction**: functional features first; **no tests authored or run**.
No merge of main or unrelated branches in this stage.

### Commits / changed files

- `27492a76c1d796b5b8ce529598f53fe7f07293f8` —
  `engine/render/Renderer3D.h`: `drawNereidRippleMesh` accepts four
  per-surface ripple float4 slots, and each DrawItem retains its own
  array so deferred rendering does not accidentally reuse parameters
  from another water body. The existing `drawMesh` API remains intact.
- `74eee4ba5d23a81e47f93fc879d070ba01b5db9f` —
  `engine/render/Renderer3D.cpp`: water draw entries capture ripple
  slots, and U0 uploads now include four vec4 ripple parameters.
- `28f193abced6cad793077b35803c771f79b05036` —
  `engine/render/Shaders.h`: enlarged matching GLSL/MSL U0 layout
  and ripple-gradient normal perturbation.
- `766f34f9e3c307ae068ee2933294f856f051e7cc`,
  `8cd860e85f44a75a3d80e59caf5b994322f37872` —
  `engine/render/Shaders.h`: moved ripple snippets out of the
  unrelated trace shader source macro into lit water fragment code,
  and after the existing procedural normal-wave calculation so it
  cannot be overwritten.
- `70728d3b959394cf9ca1c42326703575a8bbdb93` —
  `engine/render/Renderer3DReflect.cpp`: keep U0 upload length
  equal to the enlarged global shader U0 in reflection passes.
- `7946097a87eb200aef37f9e2127b798599a3cbe5`,
  `96097da6c78cd9d5f0600e8b71ca8d0edb7a234e` —
  `engine/world/NereidRipples.h`: select at most four nearby-origin
  ripple events per stable water ID. Avoid spatial rejection by
  origin distance on very large lakes/polygons; fragment shader
  evaluates distance per pixel.
- `4232e1a43bd3d6456fdfc92b1fa8b9d6b109de9a` —
  `engine/render/NereidWaterRenderer.h`: send ripple uniforms in
  the existing water draw; unchanged water draw API and style.
- `3ec0e9774d9d44890d7ca5d2cb14d2d8bbb47f81` —
  `engine/render/NereidSurfaceResident.h`: forward per-surface ripples
  while retaining the same GPU-resident mesh.
- `af514c39b81f40a7f08e0bce8c1ac8fe19686dbd` —
  `engine/world/NereidWaterScene.h`: binds each visible water slot's
  ripple set to its normal shader, with world-origin-relative X/Z
  from existing render-origin coordinates.
- `c028c4f4ef296e17994d1209414a35e9bbe67562` —
  `engine/world/WorldScene.cpp`: pass the ripple field into
  `NereidWaterScene::draw`, and stop drawing Stage 23 overlay rings
  automatically. The Stage-23 `NereidRippleField::draw` remains
  explicitly callable for debugging or alternate hosts.

### Behavior and ownership

The canonical ECS `WaterInteractionOutputComponent` is unchanged.
Visual history still comes from Stage 23's read-only, tick-filtered
events. Each NEREID water draw submits at most four float4 values:
`(renderRelativeX, renderRelativeZ, radiusMetres, fadedAmplitude)`.
Unknown/nonmatching water IDs contribute zero. Values are cached
inside the deferred draw item rather than mutable renderer state.
GLSL/MSL perturb the resulting procedural normal by the gradient of
a bounded radial Gaussian envelope centered on each expanding ring.
Existing lighting, Fresnel/specular, SSR, planar reflection, depth
optics and Prisma hooks consume the modified normal. No additional
texture, render target, GPU buffer, ECS component or native genre
dependency was introduced.

The water U0 block grew by four vec4 entries; this changes shared
Renderer3D/GLSL/MSL layouts, so ALL lit and reflection shader
variants must be compiled before production integration. The
`drawMesh` API and all its existing call sites remain source
compatible, and new arrays default to zero for non-water items.
The ring normal effect stays on NEREID-tagged water only; unrelated
shader effects do not sample these parameters.

**Known limitations:** four ripples per water surface are selected by
proximity to the mesh's stable origin, not per-tile/per-pixel adaptive
lists. Very large polygons/oceans with many simultaneous events may
omit waves away from the origin; the next stage should use per-tile
ripple selection or a bounded GPU ripple texture. This is only
visual normal perturbation, not geometry displacement or physical
wave-force propagation. Stage 23's overlay triangle renderer is
retained as an opt-in debug facility.

### Verification and next step

**Actually performed:** source API inspection and all GitHub
implementation/documentation commits. **NOT PERFORMED** (per user):
new tests, C++ compile, CTest, GLSL/Metal compilation, RHI runtime
render checks, FPS/RTS regressions. No testing claims should be
derived from the authored features. Keep PR Draft.

**Next feature stage:** shader-level amplitude/foam response for
impacts, per-tile multi-ripple field for large open water, and
auto-mapping interaction event kinds to SpectraFX effects through
an optional host adapter. Later add cascading water geometry.
Update handoff after each phase; test only when user requests.

## Stage 25 — observer-selected 16-wave normals, impact foam, SpectraFX playback

**Branch:** `feature/nereid-water-core`, PR #23 Draft.
**Starting head:** `d9c05c30ffd4edbfe6813ee15de040d5d24f31e0`.
**Owner direction:** implement features before tests. No tests created/run.
No merge of unrelated branches and no change to main.

### Committed code

- `dd5d9e0238767923cd4900236606f8f321a6a63a` —
  `engine/render/Renderer3D.h`: per-draw NEREID ripple slots 4→16.
- `53e91ee7dd56749d14f76df6ff9ff6004b689299` —
  `engine/render/Renderer3D.cpp`: matching expanded U0 payload.
- `9f2f9cd772bef2625b52946d497d2e0330cabeeb` —
  `engine/render/Renderer3DReflect.cpp`: expanded reflection
  U0 payload to match the main pass.
- `dad2931b8c1c1cf4507b87399d6d929d04c0ce5b` —
  `engine/render/Shaders.h`: GLSL/MSL U0 arrays and bounded
  16-slot per-fragment ripple normal loops.
- `cf4e58e90272ba34699068ca95aaf288a6c4635a` —
  `engine/render/NereidWaterRenderer.h` forwards 16 entries.
- `528647cedf7d6b23e752306e8c77b484f56055ac` —
  `engine/render/NereidSurfaceResident.h` forwards same data
  without recreating mesh residency.
- `d88a269ad6cedbe783de1f93032cf91a037ccf83`,
  `75af047d8df513b665c0268a14fff025e17afba1` —
  `engine/world/NereidWaterScene.h`: 16-slot per-surface array;
  use active observer's world X/Z to select nearby impulses rather
  than the mesh's first vertex/body origin.
- `fedd59d4323b7b28e68c3013c80bd5f97fcd509f` —
  `engine/world/NereidRipples.h`: up to 16 nearest ring centers
  per water ID, selected from up to 256 active scene ripples.
- `525733cc229ef7249a7eb0cb3ae20db319fbab6e` —
  `engine/render/Shaders.h`: per-pixel radial impact foam
  blended into the water base colour in both GLSL and Metal,
  after procedural normal synthesis.
- `f9b1f8c05d60caaf6383ceeabbdb6d5616e76a4d`,
  `aada509a188b6b2d134a8a81b688059f67edc193` —
  NEW `engine/world/NereidSpectraFxBridge.h`: real optional
  SpectraFX `ParticleSystem::play` adapter mapping event kinds
  to user-defined graph names and bounded tracked one-shot handles.
- `1f12a4a5ff55e2f2a40362d545336823359c2530` —
  clarify source comment for 16-slot selection.
- `35afeaf260b9e25ee2dd907f176284303c04fba6` —
  `skills/gameplay/aiverse-nereid-water/SKILL.md`: new native
  integration guidance for agents.

### API / ownership

No ECS schema changed. Stage 22 actor interaction output and Stage 23
read-only ripple ingestion remain authoritative sources; water mesh
and pulse visuals are presentation-only. For each visible water draw,
NEREID selects at most sixteen events by distance from the active
scene observer, filtered by packed water Entity ID. The renderer
uploads up to sixteen `float4` ripple parameters (position X/Z
relative to render origin, propagation radius, faded amplitude).
The shared GLSL/MSL water fragment adjusts normal and applies local
impact-foam tint on the radial ring. Existing shoreline foam,
refraction, SSR, reflection and postprocessing remain separate.
There are no extra RHI textures, buffers, full-screen passes or
new genre dependencies.

This extends the **shared Renderer3D U0 layout for all lit draws**,
not only water. C++ main/reflection U0 layouts and GLSL/MSL U0
declarations have all been updated. Other materials get zero-filled
ripple slots by default. Device shader compilation across every
supported RHI must be checked in the later testing phase.

`NereidSpectraFxBridge` is optional, constructed from the native
host's existing `spectra::Runtime&` (`fx::ParticleSystem`).
`setCue(kind,{graph,scale})` defines per-event one-shot graph names;
`emit(WaterPresentationEvent)` invokes the actual
`particles.play(graph,position,{0,1,0},scale)` API;
`reset()` kills the bridge's own tracked handles on rollback.
A maximum of 256 recent handles are tracked; older ones are
stopped rather than killing other SpectraFX users.
Empty graph names do nothing. The host still owns loading/defining
particle assets and calling ParticleSystem update/render.

Example:
```cpp
world::NereidSpectraFxBridge fxBridge(particleSystem);
fxBridge.setCue(lib::nereid::WaterInteractionKind::Enter,
                {"fx/water_splash",1.f}); // host-defined asset key
fxBridge.setCue(lib::nereid::WaterInteractionKind::SurfaceWake,
                {"fx/water_wake",0.5f}); // host-defined asset key
auto events=lib::nereid::snapshotWaterInteractionEvents(sim.world(),sim.tick());
worldScene.ingestNereidInteractions(events,sim.tick(),
    [&](const auto& e){ fxBridge.emit(e); });
```

### Verification / limitations / next feature

**Actually done:** inspected existing source and SpectraFX public API,
pushed implementation/skill/handoff via GitHub connector.
**NOT DONE (by user request):** C++ compile, CTest, shader compilation,
GPU screenshots, FPS/RTS regressions, runtime particle playback, and
any new tests. All runtime effects remain unverified. PR stays Draft.

Known limits: 16 shader pulses per water draw, 256 total visual
pulses; selection is per *surface and observer*, not per tile,
so waves far from the camera may be omitted on extremely large
oceans. SpectraFX graphs are supplied by the host and are not
created or automatically loaded in this stage. Impact foam uses
a constant pale tint, not a per-event colour or physical bubble
simulation. Material normal ripples do not alter the water ECS
surface height, Box3D forces or volumetric fluid simulation.

**Next features:** per-tile ripple selection or a bounded dynamic
normal/foam texture for enormous water surfaces, more configurable
foam styles, waterfall/cascade emitter geometry, and configurable
flow-wake response. Only after functional completion perform native
C++/GLSL/MSL and GPU visual verification. Always append another
handoff after the next feature phase.

## Stage 26 — static spatial water tiles with local ripple selection

**Branch** `feature/nereid-water-core`, Draft PR #23.
**Baseline:** Stage 25 commit `2ef7e99c728fa7121a59407d231f6e766c1d10a3`.
**Priority:** functional features first; no new tests or test runs.
No other feature branch was merged or main changed.

### Implementation commits / changed files
- `884fe1e8d13a8c9b84f2b3bec4c1251606cf16a3` —
  `libraries/include/aiverselib/water/NereidWaterSurfaceMesh.h`:
  `WaterSurfaceMeshOptions::rippleTileGrid` (1..8, default 1),
  `WaterTileRange`, CPU tessellation of a Box or Infinite water
  footprint into N×N disjoint indexed tiles. Tile vertices/indices
  are contiguous per tile and share the parent mesh's single VB/IB.
  Near/Medium/Far changes per-tile subdivision; only the outer
  boundary contributes geometric shoreline foam. Old shapes/modes
  retain the old mesh builder path.
- `766ea03f8f9c75db6fc37b92dcbc6cc8fb2c681a` —
  `engine/render/Renderer3D.h`: optional firstIndex/indexCount
  on deferred NEREID draws; other drawMesh clients unaffected.
- `5e2818cb6283d06840e9c442bb4443ec211986ae` —
  `engine/render/Renderer3D.cpp`: use Gfx::Device indexed subrange
  draw only when specified; profile actual submitted indices.
- `0156c7db38f369ef62127d11587ca2ee166c4df0` —
  `engine/render/NereidWaterRenderer.h`: forward indexed ranges.
- `4a6e16d6ae17b58bc41c1ce19af652f512ccc1d1` —
  `engine/render/NereidSurfaceResident.h`: cache per-tile ranges with
  uploaded mesh, expose `tiles()` and `drawTile()`; clear with GPU
  residency; add tile-grid to per-body GPU key.
- `6b217d6a9738bffb0d3a37de060ed17ac02b94a5` —
  `engine/world/NereidWaterScene.h`: invalidate residency on
  tile-grid changes.
- `49cb675837d95c0e9a76d50075f1272c453c8587` —
  `engine/world/NereidWaterManifest.h`: parse validated
  `rippleTileGrid` from the per-surface JSON.
- `17c8825199a9729853178fa506309daf8a6bfbaf` —
  `NereidWaterSurfaceMesh.h`: store precise X/Z AABBs per tile.
- `82693d9b80ce1afd04687e5a16671dcb9384ec7b` —
  `engine/world/NereidRipples.h`: new
  `shaderRipplesForTile(waterId,renderOriginX,renderOriginZ,
  minX,maxX,minZ,maxZ)`; filters by annular wavefront AABB
  intersection and selects up to sixteen closest affected impulses.
- `2ffe8e0a60807a6bfa27cd7ae93b7fbb463848c1` —
  `engine/world/NereidWaterScene.h`: when a resident has tiles,
  submit disjoint water index ranges with their own sixteen ripple
  parameters; legacy single draw remains when `rippleTileGrid=1`.

### API / example

```json
{
  "nereidWater": {
    "surfaces": [{
      "id": 12, "kind": "ocean", "shape": "infinite",
      "surfaceY": 3, "bottomY": -100,
      "oceanPatchRadius": 512,
      "rippleTileGrid": 4
    }]
  }
}
```

A grid size of 4 yields up to 16 indexed draw calls per resident
water body, each accessing 16 local ripple slots; 8 yields up to
64 draws. The CPU mesh and GPU VB/IB are still uploaded once per
water body (not once per tile). Active ripple source data remains
the Stage-22 deterministic ECS outputs copied to the Stage-23
presentation field, not a new simulation system. The tiling belongs
to render options; no change to authoritative water shape, physics,
networking or rollback. Tile bounds and rendered vertices use double
world origins before float conversion.

### Status, limitations, next features

**Actual work:** source inspection of Gfx::Device::drawIndexed and
NEREID renderer/mesh/scene, code commits and this handoff.
**NOT RUN / UNVERIFIED by user request:** native build, CTest,
shader compilation, Metal/OpenGL screen rendering, FPS/RTS
parity, GPU profiling. No test code authored this stage.
Keep PR Draft.

**Limitations:** tiled support in this stage applies to rectangular
Box and Infinite patches only. River splines, concave polygons and
discs remain single draws. Tile meshes duplicate vertices at seams;
this is expected, but per-surface VB/IB budgets still apply.
Quality setting `rippleTileGrid=1` keeps the old single-draw
path. Tiles are not frustum-culling independently; 8×8 means 64
draws even on mobile, so use 2×2 or 4×4 on weak GPUs. A chosen
ripple's normal/foam is still a presentation-only shader effect.
For larger world patches, precision is limited by global camera
view-projection and scene RHI rather than these tile-local bounds.

**Next functional work:** add opt-in waterfall/cascade presentation
geometry (vertical stream strips, variable widths and falling foam)
and simple flow-direction wave response, preserving ECS water body
authority. Per-stage handoff is mandatory; tests remain deferred.

## Stage 27 — descending waterfall/cascade ECS geometry and WorldScene rendering

**Branch:** `feature/nereid-water-core`, PR #23 remains Draft.
**Base:** completed Stage 26 spatial-tiled ocean work at
`a9acaaf75057673959450558bd989b50187738ed`.
**Owner priority:** ship functions first; do not write/run tests now.
No unrelated branch was merged and main was not modified.

### Implementation commits and files

- `ad154dd94a4500260fc8f449cbf7b4a00e9e3432`,
  `0860a97570741008baf8c1d677a8f07fb175b1e5` —
  NEW `libraries/include/aiverselib/water/NereidCascades.h`.
  Genre-neutral, fixed-point persistent `WaterCascadeComponent`,
  `WaterCascadePoint`, `validWaterCascade`,
  `registerNereidCascadePersistence`; pure CPU
  `buildWaterCascadeMesh` outputs two-sided descending ribbons with
  interpolated widths, subdivided steps, normals, and toe foam.
  Archive serializes the width axis explicitly as two Fixed fields.
- `159bf675c9330f1bcafdb34eb2d8b1abab4b87d1`,
  `4d08770c4fac8fc15de0af89645fe9cf9386d409` —
  NEW `engine/world/NereidCascadeScene.h`. Opt-in GPU owner:
  `setCascades`, `setMaxUploadsPerFrame`, `update`,
  `draw`, `shutdown`, `residentCount`. Stable ID matching
  avoids replacing unchanged GPU meshes; removed meshes release
  on the device thread. Invalid enabled sources are rejected before
  ownership transfer; failed uploads preserve the previous resident.
  Caps: 32 cascades and 1..8 attempted uploads/frame.
- `ca81d0a458bdda30a2b92205027fe7b9b5f61d14` —
  NEW `engine/world/NereidCascadeManifest.h`, strict optional
  `nereidCascades` scene configuration and 3D paths.
- `e3b2b5437b9c29df1799f75f40403138512f9c43`,
  `1442cc910b37d67c7087c48e0d79d9ede4590d55`,
  `e5d2a49d7e6578ae9abfb6c1d8484ff69be04182`,
  `02c6cc184cb482af5f7a9dae358899f6ffa652d8` —
  `engine/render/Renderer3D.h/.cpp`,
  `engine/render/Renderer3DReflect.cpp`,
  `engine/render/Shaders.h`: new `kDrawNereidCascade` tag
  marks waterfall sheets as water without incorrectly selecting a
  horizontal planar reflection plane; custom vertical-flow normal
  streaks in both GLSL and Metal.
- `274431a17dda8f269d433e89d6ec9363d70eb430`,
  `e35e9909d4d52cee00d124d6550331cb702e447d` —
  `engine/world/WorldScene.h/.cpp` opt-in constructor setup,
  water+waterfall draw, device update/shutdown, and
  `setNereidCascades` native snapshot override.
- `d2bd82abe52f6a1dfd387a6c248cb0c893325abf` —
  NEW `engine/world/NereidCascadeEcsPresentation.h`,
  read-only generation-safe ECS cascade snapshot adapter.
- `8887ef94d3f6cb3b70971382e4e2cc1afb582337` —
  `libraries/include/aiverselib/water/NereidNativeWater.h`
  adds optional `enableCascades` persistent ECS registration.
- `6dcd8331b5919d5d031d83661872bedc9acd8cfd` —
  `libraries/catalog.json`, native-linkable header-only
  `AIVERSE::lib_nereid_cascades` package.
- `dd22ea03ceee96c39686bd4bd128b4afb97e2a57` —
  updated `skills/gameplay/aiverse-nereid-water/SKILL.md`.

### Usage and architecture

`WaterCascadeComponent` is optional game/level geometry, not a
replacement for `WaterBodyComponent`. It stores a sequence of
3D Fixed points `{position,halfWidth}`, an X/Z ribbon width axis,
foam, speed, enabled flag. Each neighboring pair is descending or
horizontal; total path must descend. Each ribbon is subdivided on
the presentation CPU, double-sided, with width interpolation and
static toe foam. The body maintains high-precision position until
camera/render-origin-relative float conversion, and the host RHI
continues to own the GPU buffers.

Static authoring:
```json
{
  "nereidCascades": {
    "maxUploadsPerFrame": 2,
    "sources": [{
      "id": 3, "enabled": true,
      "widthAxis": [1,0], "foam": 0.6, "speed": 7,
      "stepsPerSegment": 4,
      "points": [[10,20,0,4], [12,9,0,5], [14,3,0,6]],
      "style": {
        "opacity": 0.75, "deepColor": [0.13,0.4,0.52],
        "foamStrength": 0.8
      }
    }]
  }
}
```
Native ECS projects:
```cpp
#include <aiverselib/water/NereidNativeWater.h>
#include "engine/world/NereidCascadeEcsPresentation.h"
lib::nereid::NativeWaterPipelineConfig config{};
config.enableCascades=true;
lib::nereid::registerNativeWaterPipeline(sim,"water",config);
// After the ECS structural/read barrier (never inside a write phase):
worldScene.setNereidCascades(
    world::snapshotNereidEcsCascades(sim.world()));
// Existing WorldScene update/draw/shutdown publishes and retires GPU data.
```

NEREID is responsible for fixed-point cascade data and rendering
geometry; physical flow, Box3D, PHYSIS, SpectraFX particles and Prisma
remain separately owned. `kDrawNereidCascade` preserves the normal
water material path while excluding vertical ribbons from the
single-plane planar reflection selection. The shared GLSL and Metal
shaders animate downward streak normals using the existing scene
time/light inputs; no additional RHI backend or postpass is introduced.

### Verification, limits and next functional stage

**Actually performed:** examined connector-accessible ECS/RHI source,
authored and committed all library, scene, shader, native integration,
manifest and handoff files through GitHub.

**NOT PERFORMED:** new tests, local C++ compile, CTest, shader
compilation, real GPU waterfall captures, FPS/RTS regression or
SpectraFX playback, as the user explicitly requested features first.
Never claim PASS; keep PR Draft.

**Limitations:** waterfalls are static/stepped visual sheets with
animated normal streaks, not liquid mass-conserving fluid simulation
or actual geometry displacement. Foam uses a vertex tint toward the
falling foot and at segment divisions; no automatic waterfall-spray
particle graph or river-to-waterfall fluid continuity yet.
Physically correct two-sided lighting, transparency sorting, high-Y
world-origin rebasing and multiple geometry intersections need actual
GPU checks later. The `speed` value is currently an authored
optical hint, not a physics velocity integrator or shader uniform.
Authoring cap: 128 points/cascade; resident cap: 32 cascades.

**Next features:** advect visual ripple centres with authoritative
NEREID flow without writing back to ECS, optionally derive cascade
waterfall spray events for SpectraFX, improve river->cascade
connections, and finally run the deferred native/GL/Metal suite.
Append a new handoff section after every feature.

## Stage 28 — visual ripple advection by ECS water current + continuous waterfall spray

**Branch:** `feature/nereid-water-core`, NEREID PR #23 Draft.
**Stage baseline:** Stage 27 handoff
`92c9e80c6933d1fb63c51a8c8450c51834a513ed`.
**User priority:** implement all missing features first; do not
write/run tests or change main. No branch merges in this stage.

### Commits / files

- `4c825b93636dcd33819ef3c00af2bcf1ed3997fa` —
  `engine/world/NereidRipples.h`: overload
  `advance(deltaSeconds, resolveCurrent)` with a fixed source
  ID-to-current lookup. Integrates X/Z visual ripple centres using
  copied water flow while retaining the original no-current overload
  for standalone/native callers.
- `02cb5da96387e9fcdb804149292c65a73eaafad7` —
  `engine/world/NereidWaterScene.h`:
  `currentForInteractionWater(packedEntityId)` looks up an enabled
  water body's copied Fixed flow from its stable scene ID
  (`packed+1`) without touching live ECS.
- `5b251c6e674b6af80a2e1b910dcb2fc4437fe68e` —
  `engine/world/WorldScene.h`: existing
  `advanceNereidRipples(deltaSeconds)` automatically resolves
  each visual pulse's current from the NEREID scene snapshot.
- `cfb574ce7edffd429155543d08e72dc9f6640cfd` —
  `engine/world/NereidCascadeScene.h`: read-only
  `forEachSource` for continuous waterfall spray presentation.
- `73c217bc947dfbaaed98214b9edf3df36ca60b24` —
  `engine/world/WorldScene.h`: read-only
  `forEachNereidCascade` bridge; native genres need no direct
  access to internal GPU residency slots.
- `345c0a206155f685f1e79d73b3b6b64690537b43` —
  NEW `engine/world/NereidCascadeSprayBridge.h`:
  opt-in continuous SpectraFX emitter per stable cascade ID, creates
  /moves active emitters and stops disappeared ones; `reset()`
  kills the adapter's own live handles on rollback/scene replacement.
- `093aa715de890f0601e67ea0631b6c3b0dd13037` —
  `skills/gameplay/aiverse-nereid-water/SKILL.md` appended native
  integration/ownership guidance.

### Architecture and native API

The Stage-22 water contacts, force sampling and water body flows
remain authoritative Fixed ECS data. The Stage-23/24 ripple field
exists **only in presentation**. At each render frame its position
is advanced by `clamp(body.flow.xz,-100,+100)*clamp(dt,0,0.25)`
in metres, preserving world-double coordinates until the RHI
relative-origin conversion. Unknown/removed water IDs default to
zero current. The source data comes from
`WorldScene::setNereidSurfaces` / static manifest, which still
must be refreshed by the host after authoritative changes.
No checksum, save state, physics forces, Box3D or PHYSIS data
is changed by advection.

`NereidCascadeSprayBridge` is constructed from an existing
`spectra::Runtime&` (`fx::ParticleSystem`), and a game explicitly
sets the name of a **previously loaded** particle graph. It uses
`WorldScene::forEachNereidCascade` and waterfall foot position,
width and downstream direction to call SpectraFX
`play(graph,position,direction,scale)`,
`move(handle,position,direction)`, `stop(handle)` and
`kill(handle)`. Identity is stable across reordered sources
and frame updates. A missing/disabled/removed cascade stops its
emitter; `reset()` kills all handles owned by this bridge.
ParticleSystem owns particles and GPU rendering; NEREID neither
simulates particles nor manages effects owned by other games.

```cpp
#include "engine/world/NereidCascadeSprayBridge.h"
world::NereidCascadeSprayBridge spray(particleSystem);
spray.setGraph("fx/waterfall_spray", 1.f); // host-provided asset
// Per presentation frame AFTER ECS snapshot publication:
worldScene.advanceNereidRipples(deltaSeconds);
spray.sync(worldScene);
// Before reuse of the view after rollback or new level:
spray.reset();
```

### Verification / limitations / next feature

**Actually performed:** reviewed connected GitHub source of NEREID
scene, event representation and real SpectraFX runtime API,
committed new C++ integration, skill and this handoff.
**NOT DONE:** test authoring, native C++ compilation, CTest, GLSL
or Metal validation, real GPU preview, FPS/RTS parity, full test
suite, SpectraFX graphs running on a device, all at user request.
Do not mark this stage verified; keep PR Draft.

**Limitations:** flow is a uniform vector per water body; no
spatial gradient, turbulence or shallow bank drag. It advects only
visual ripple centres, not authoritative geometry or physical forces.
Cascade spray graph assets must be supplied/loaded by each native
project; the bridge does not ship an art pack. It uses only the foot
of the waterfall ribbon, not an emitter per step. Effects are
continuous and not physically simulated spray momentum.
Waterfall ribbon shading and winding are not verified on devices.

**Next functional stage:** optional splash emitters along multiple
cascade steps, flow-aware wakes with directional anisotropy,
and a bounded per-water current field for rivers/rapids.
No tests until user requests feature completion, then run native
and GPU tests and record actual results.

**Stage 28 source review follow-up:** `d3e58576a6d0b14787a3b4c16cdd823922ecf0dd`
fixes `engine/world/NereidCascadeScene.h` so a source with a
pending/failed GPU rebuild keeps `dirty=true` after repeated identical
`setCascades` ECS snapshots. Without this, a native host publishing
one read-only snapshot per tick could suppress a required upload. This
was a source-inspection correction, **not** a compiled test or GPU
verification. Functional priority and Draft PR status are unchanged.

## Stage 29 — fixed-point spatial currents for rivers and rapids

**Branch:** `feature/nereid-water-core`, draft PR #23.
**Starting head:** `8527532cb1b94749afe97e6f85402885c9545335`.
**Owner priority:** finish features first; **no test authoring or runs**.
No merges from other feature branches or main in this stage.

### Implementation commits / modified files

- `ea806393704e8c22dbca9cc61f18fae9924cba33` —
  NEW `libraries/include/aiverselib/water/NereidCurrentField.h`:
  persisted `WaterCurrentNode {downstream,lateral,shear}`,
  `WaterCurrentFieldComponent {nodes,bankFade,enabled}`,
  `waterCurrentAt(body,geometry,field,position)`. Fixed-point
  nearest-segment projection, tangent speed, bank fade, cross-river
  velocity and deterministic tie handling; invalid field falls back to
  base `WaterBodyComponent.flow`.
- `f9d13b59a9e1add49e9fe635ed9d66ad17115726` —
  `libraries/include/aiverselib/water/NereidWaterSystems.h`:
  optional current parameter on shared candidate selection;
  reference water sampling now evaluates local current, and new
  `registerNereidCurrentPersistence` schema registration.
- `cf142f7af3f8808a9bf069220a06318a3b41005b` —
  `libraries/include/aiverselib/water/NereidQueryIndex.h`:
  indexed water sampling uses the same current field and returns the
  same `WaterSample.flow` semantics as the reference sampler.
- `7b7dc01f0893d9e66611cb1ee2d583897de0ad7e` —
  `libraries/include/aiverselib/water/NereidNativeWater.h`:
  opt-in `NativeWaterPipelineConfig::enableCurrentField`.
- `0909de423bdce60ccc2caa2f066b8d996b0ad998` —
  `engine/world/NereidWaterScene.h`:
  read-only current profile and `useCurrentField` in each scene
  source; `currentForInteractionWater(id,position)` queries local
  current without reading or changing authoritative ECS.
- `4b10b9fd0c78a2ac572cff26414d678167f0d81f` —
  `engine/world/NereidEcsPresentation.h`:
  copy optional water-current ECS component into presentation source.
- `8679c71c712e08fde36f9663775c8a5649ca0b8c`,
  `de258d20674268be0bf45fc20cff7a42c6b1b08b` —
  `engine/world/NereidRipples.h`,
  `engine/world/WorldScene.h`: visual ripple advection now invokes
  the per-position current query using precise double positions
  quantized only at the presentation read bridge.
- `f34af12259231d0b76ac2122955270cf05754898` —
  `engine/world/NereidWaterManifest.h`: optional strict
  `currentField` node triples per RiverSpline; one node per control
  point, bounded node speeds, and bankFade.
- `cd0e8e6af0efa2e4b576f1236cccccf5379cd4d3` —
  `libraries/catalog.json`: native reusable package
  `AIVERSE::lib_nereid_currents`.
- `47733fc78fb552cbd7afc2e18147435f3ede5a4e` —
  `skills/gameplay/aiverse-nereid-water/SKILL.md`: native use,
  ownership and feature guidance.

### Contracts and examples

Native ECS: attach a `WaterCurrentFieldComponent` on the SAME entity
as `WaterBodyComponent` and enabled `WaterGeometryComponent`
with kind `RiverSpline`. The number of profile nodes must match the
number of spline nodes, up to `kMaxWaterGeometryPoints`. Nodes are
interpolated along the closest spline segment; lateral direction is
defined by its tangent normal, shear varies smoothly across width,
and optional `bankFade` attenuates towards the bank. All simulation
math uses deterministic `Fixed`, not float/double. The existing
`WaterSample.flow` field now holds local velocity and is consumed
by the existing buoyancy/drag output, without adding a new physics
integrator. Existing water saves are unchanged unless the optional
new schema is explicitly registered.

Example scene `nereidWater.surfaces[]` entry:
```json
{
  "id": 30, "kind": "river", "shape": "river",
  "surfaceY": 10, "bottomY": -4,
  "points": [[0,0,4],[20,0,4],[35,10,6]],
  "flow": [0,0,0],
  "currentField": {
    "bankFade": 0.25,
    "nodes": [[2,0,0],[8,0.5,2],[4,-0.3,-1]]
  }
}
```

The same current field is copied in `snapshotNereidEcsWater`;
`WorldScene::advanceNereidRipples` advects visual pulse centres by
that local value. This is **read-only presentation sampling**, and
not an authoritative numerical fluid simulation. Flow profile changes
must be republished by the host after the ECS read barrier. A change
in flow alone should not require a water mesh re-upload.

### Verification / limits / next feature

**Actually done:** reviewed GitHub NEREID policies, authored and
committed implementation, package catalog, skill and this handoff.
**NOT DONE:** test authoring, native compiler, CTest, real shader
compilation, GPU captures, mobile perf, RTS/FPS regression, per
owner request. No test results are implied. Keep PR Draft.

Known limitations: the nearest-spline-segment approximation is
piecewise linear and may have discontinuities at sharp bends,
self-crossings and confluences; no dynamic turbulence solver or
pressure field. The local field is only defined for RiverSpline;
other water kinds retain uniform `body.flow`. BankFade is
deterministic and should not be used to infer collision with banks.

**Next stage:** add directional/asymmetric vessel wakes in the
existing NEREID ripple shader and ECS interaction event chain,
then step-based SpectraFX cascade spray, followed by a PHYSIS wind
presentation adapter. Update the handoff after *each* stage and
defer tests until the functional backlog is completed.

## Stage 30 — deterministic wake direction + stern-biased water shader

**Branch:** `feature/nereid-water-core`, draft PR #23.
**Baseline:** Stage 29 handoff `7c33f43e28734762572b8979710d9b05a4da09b3`.
**Owner priority:** functions first, no compilation or test suite yet.
No other branches merged; preserve existing ECS/Prisma/PHYSIS ownership.

### Commits, architecture and source files

- `a94f7bbeef20db8e09cf98050aa24ebd42890d71` —
  NEW `libraries/include/aiverselib/water/NereidWakeDirection.h`.
  A separate optional fixed-point `WaterWakeDirectionComponent`
  holds normalized actor X/Z motion, travelled metres and source tick.
  `waterWakeDirection(previous,now,tick)` is pure and deterministic.
- `7d6972f9185ff8844e86aae94339710c7832a14b` —
  `libraries/include/aiverselib/water/NereidInteractions.h`:
  `SurfaceWake` computes motion metadata from the old probe history
  before updating it; `registerNereidInteractionPersistence`
  registers new `nereid.wake_direction` schema v1 without changing
  the established `nereid.interaction_output` schema v1.
  Only entities carrying the optional direction pool get metadata.
- `3f7e46db962aa4b388b6c431d1d6249aaa7127d5` —
  `libraries/include/aiverselib/water/NereidInteractionPresentation.h`:
  appends `movementDirection` and `travelled` to read-only
  `WaterPresentationEvent`, only for a matching sampled tick.
- `5653012eb4c5f80c552ad1c14bc2da51fc3875cd` —
  `engine/world/NereidRipples.h`: keeps movement direction on
  `SurfaceWake` visuals; new `NereidRippleShaderPacket`
  holds matched `rings[16]` and `shapes[16]` arrays with identical
  ordering. The old ring-only shader queries remain wrappers for
  compatibility.
- `be6d10ae9193f16c313b701627e4652aa33a641d` —
  `engine/render/Renderer3D.h`: optional wakeShapes on the water
  submission API and deferred DrawItem state.
- `931681920ad1a621a94e0c8db44cec276f6fc2cd` —
  `engine/render/Renderer3D.cpp`: matching packed water wake shape
  uniform data, set only for NEREID-tagged submissions.
- `dbafb0f6e590acd4fc43edd7d5082f407b9972f4` —
  `engine/render/Renderer3DReflect.cpp`: equal-size reflection
  U0 block; zero-initialized for reflected opaque meshes.
- `f498519b7c5370107e7ccf43ecd91cbe7dee9daf` —
  `engine/render/NereidWaterRenderer.h`;
  `86f9da17c46904194fc037b75bd1cc5742653df7` —
  `engine/render/NereidSurfaceResident.h`;
  `2d353d29ff8985bf35579fb4fd87cf701c8d3469` —
  `engine/world/NereidWaterScene.h`: transfer packet shapes
  through both single-body and spatial-tiled index range draws.
- `5017f60540e38329dc45281f47c1355f570088a5` —
  `engine/render/Shaders.h`: GLSL and MSL U0 acquire 16 parallel
  `waterWakeShapes` vec4s. A valid directional marker distorts
  radial distance into the moving actor's heading/transverse basis,
  creates a stern-biased wake mask and propagates the resulting
  anisotropic gradient/foam into existing water normals and lighting.
  Old impacts remain round when shape marker is zero.
- `94f707254793d3ca72112e71d3bc4139d4af7e6f` —
  `skills/gameplay/aiverse-nereid-water/SKILL.md`: native
  implementation, persisted schema, shader packet and ownership rules.

### Usage

Attach `WaterWakeDirectionComponent` alongside the Stage-22
interaction components on actors with boats, swimming wakes or
surface locomotion. This component is optional; the absence of
motion metadata falls back to the original circular normal ripples.
The host still owns physical velocity and water contact integration.
The renderer owns only visual shader parameters; it never writes
to ECS or generates authoritative motion.

Directional wakes use the *existing* 16 ripple slots per water tile;
each has a paired shape slot. There are no new draw calls,
render textures or separate particle simulations. The **shared
lit shader U0 grew by 16 additional vec4s**; GLSL, Metal,
Renderer3D main and reflection C++ structures have matching fields,
but must all be compiled and device-validated later.

### Verification, limitations and next stage

**Actually done:** connected GitHub source review and implementation
commits; no tests authored. **NOT RUN / NOT VERIFIED:** C++ compiler,
CTest, shader compilers, Metal/GL visualization, FPS/RTS regressions
or performance; deferred per owner instruction. Draft PR retained.

The directional profile is deliberately approximate, not a Kelvin
wake solver, viscosity or fluid simulation. One direction is
captured per emitted SurfaceWake pulse. Camera-origin precision
still follows the existing visual field. Snapshots without the new
optional ECS component remain backward-compatible.

**Next functional stage:** attach additional SpectraFX emitters to
individual waterfall steps and waterfall foot, using stable
cascade/step identities, bounded handles, and correct update/stop/
rollback behavior. Then provide an optional PHYSIS wind presentation
adapter without linking NEREID to a particular weather engine
module. Always append handoff after each stage.

## Stage 31 — waterfall spray at intermediate cascade drops

**Branch:** `feature/nereid-water-core`, Draft PR #23.
**Starting head:** Stage 30 handoff
`afdc9de1307025d05922aaf5b450d5f15ea9a8f5`.
**Owner priority:** features first. No tests authored or run.
No unrelated branches or main merged.

### Changes / commits

- `68701c4e1f00ab8eafc152c3aa74e89f4dc7ba4d` —
  `engine/world/NereidCascadeSprayBridge.h`:
  adds independent `setStepGraph(graph,scale,minDrop)`, up to
  eight intermediate step emitters per cascade, stable
  `(cascade-id,point-index)` ownership keys, cached particle
  handles, and removal/reset cleanup. The existing
  `setGraph(graph,scale)` foot emitter remains supported and can
  be disabled independently. Changing the foot graph/scale resets
  only the foot handles; changing a step graph/threshold resets
  only the step handles. A step may emit even with no foot graph.
- `e7fa419d1618f538ca48578a636b4315f2acaaf4` —
  `skills/gameplay/aiverse-nereid-water/SKILL.md`:
  agent-facing usage, graph authoring and lifecycle instructions.

### Ownership and API

Existing `NereidCascadeScene` is the read-only authoritative
presentation snapshot for visual waterfall geometry. The host's
SpectraFX `ParticleSystem` owns particles. NEREID maps intermediate
descending segments (all but the final foot) to optional continuous
spray emitters only when segment vertical drop reaches a configurable
threshold; cap is eight qualifying intermediate steps per cascade.
Each step retains the same FxHandle across frames with the same
stable ID/point index; the real `play`, `move`, `stop`, `kill`
APIs are used. Removed cascades and steps stop emitting naturally;
`reset()` kills all handles on rollback/scene replacement. No new
ECS component, no new shaders/RHI, no genre dependencies or extra
physics simulation.

```cpp
world::NereidCascadeSprayBridge spray(particleSystem);
spray.setGraph("fx/cascade_foot",1.f);       // host-defined graph
spray.setStepGraph("fx/cascade_step",0.65f,0.75f);
spray.sync(worldScene);                     // each presentation frame
spray.reset();                              // scene replacement/rollback
```

**Actually done:** connected GitHub source modification, native
API review and agent handoff. **NOT RUN:** C++ build/CTest,
SpectraFX graph smoke, GPU/visual capture, gameplay regression,
all intentionally deferred. Keep PR Draft.

Known limitations: eight selected intermediate steps per cascade,
fixed first-eight eligible ordering (not camera/importance LOD),
one particle attachment per discrete spline endpoint, and
simple width/drop-based strength; no true fluid impact, spray
collisions or bubbles. The game must provide and load SpectraFX
graph assets and schedule ParticleSystem update/render.
No editor integration for step graph authoring yet.

**Next stage:** add an *optional*, genre-neutral visual wind input
for NEREID that a PHYSIS owner/adapter can supply, without NEREID
referencing genre or non-merged PHYSIS sources. Apply wind to
water shader normals and presentation ripple advection. Append
a new handoff when implemented; defer tests until owner requests.

## Stage 32 — optional PHYSIS wind input for NEREID visual water

**Branch:** `feature/nereid-water-core`, Draft PR #23.
**Baseline:** Stage 31 handoff `48e00baddb35ccc9f3d9909025b91adbc6ce0462`.
**Owner priority:** implement features before tests. No tests authored or
run this stage; no unrelated feature branches or main merged.

### Implementation commits / files

- `ab28a52af633eb18c39fd5af270a5a1aa9082717` —
  NEW `engine/world/NereidWindPresentation.h`:
  finite/bounded `NereidVisualWind {east,north,gust,coupling}`,
  `normalizeNereidWind` and presentation-only
  `nereidVisualWindFromSample(Vec3,Fixed,Fixed)`.
- `635bf8748bef4282310f5ba28c70d015a178f596` —
  `engine/render/Renderer3D.h`: per-draw wind float4.
- `c75cdb157eda2eb515d81b8682244d4982841c0f` —
  `engine/render/Renderer3D.cpp`: water wind copied to U0.
- `1c0ceb13dbbd0df055a733e608ee86ffbe74f227` —
  `engine/render/Renderer3DReflect.cpp`: reflected opaque U0 layout
  grows to exactly the same size, zero-initialized by default.
- `6b53012533766ca97650edefe4bf050ee608cc78` —
  `engine/render/NereidWaterRenderer.h`;
  `f12e4b535a7f3bdce3fa49dbd9ffe36b534950be` —
  `engine/render/NereidSurfaceResident.h`;
  `b8a23b7d668226942afa3a13fa778ac68e75e86a` —
  `engine/world/NereidWaterScene.h`: forward per-water wind for both
  single draw and spatial-tiled ripple index ranges.
- `d77c7c823c85b681d1c270a84e8012eeefeede74` —
  `engine/world/WorldScene.h`: public
  `setNereidVisualWind` / `nereidVisualWind`, presentation state.
- `ec71c2cf89038ecc37f63d7cad719ac7d7fc5f4f` —
  `engine/world/WorldScene.cpp`: forwards the wind to water draws.
- `06a3fa545bea04f027b573c8eee94df01fa9eaaa`,
  `b48af73433c403992129e316514d26d9dd34e6b1` —
  `engine/render/Shaders.h`: identical GLSL/MSL U0 extension and
  NEREID-only visual wave phase advection/normal tilt by wind and
  gust. The windless/default and untagged legacy-water paths retain
  old shader behavior. Steady winds also affect waves when gust=0.
- `124c047bb57584d4db5b8ecb019399b3f7d2d71e` —
  `engine/world/NereidRipples.h`: non-authoritative wind drift
  of existing visual pulse centres, with bounded frame time,
  velocity and coupling.
- `b954b7a42a01397906990507b2f44dfc49417c74` —
  `engine/world/WorldScene.h`: includes wind drift after spatial
  ECS flow advection.
- `d677f343358d7cee31fdc013312f7680e59fa148` —
  NEW `engine/world/NereidPhysisWindBridge.h`, explicit
  `applyNereidPhysisWindSample(WorldScene&, Vec3, Fixed, Fixed)`
  and `clearNereidPhysisWind`.
- `7f06f45f846f5eb2fa1e4cb839f64a3cdd89db3f` —
  `skills/gameplay/aiverse-nereid-water/SKILL.md`: AI handoff and
  native project integration documentation.

### Usage / ownership boundary

The weather simulation owner (PHYSIS or a game's custom weather)
provides a read-only per-frame sampled wind velocity (X/Z metres/sec)
and gust strength (0..1) **after its own ECS read barrier**. Example:

```cpp
#include "engine/world/NereidPhysisWindBridge.h"
// The host calls its own PHYSIS API to acquire sampledWind and gust:
world::applyNereidPhysisWindSample(
    worldScene,sampledWind,physisGust,1_fx);
// Existing WorldScene::advanceNereidRipples(dt) and draw() use it.
worldScene.advanceNereidRipples(deltaSeconds);
```

`NereidVisualWind` caps values and is presentation-only;
`WorldScene::setNereidVisualWind` is also available directly.
Shader waterWave phase/normal response, and optional downstream
ripple drift, use this data. WaterBodyComponent.flow continues
to own gameplay buoyancy and current-force semantics, and the
Stage-29 spatial river profile is unaffected by PHYSIS wind.
No PHYSIS ECS component or API contract was present in this branch
at review time, so the bridge accepts an **external sample** and
does not claim direct/automatic PHYSIS tick registration. PHYSIS
remains owner of wind, storms and weather systems; NEREID is only
the consuming visual subsystem. This allows a later PHYSIS agent
to call the adapter without pulling PHYSIS code into the reusable
water simulation library or genre modules.

Shared Renderer3D U0 adds one float4 and so native C++ draw
uniforms, reflection U0, GLSL and MSL all require later native
compiler/GPU verification. Legacy untagged water ignores the wind
path. No new GPU texture, render pass, RHI backend, or physics
integrator has been introduced.

### Verification / limitations / next feature

**Actually completed:** source review of existing NEREID, Renderer3D
and shader contracts, all GitHub implementation commits and per-stage
handoff. **NOT performed:** unit-test authoring, compiled C++ build,
CTest, GLSL/Metal compilation, GPU visuals, mobile profiling,
FPS/RTS regression and actual PHYSIS-host integration, per owner's
feature-first direction. PR #23 must remain Draft.

Limitations: wind uniform is global to the WorldScene camera and
is not sampled spatially per lake/river tile; gust is a time-sampled
parameter supplied by the host. Visual normal tilt and drift are
bounded approximations, not a spectral ocean or two-way coupling
with fluid pressure. Lack of a common PHYSIS header in this branch
prevents direct PHYSIS-system auto-wiring. Calling
`clearNereidPhysisWind` on scene reset is advised.

**Next functional stage:** add location-aware PHYSIS wind samples
per water tile (optional), configurable river wake vorticity and
seamless river-to-cascade-to-lake visual connections. Then, only
when the functional backlog is complete, begin the deferred C++,
GL/Metal and runtime test suite and write the factual results
to a new handoff section.

**Stage 32 source-review correction:** `7c5ae08bf779394ea048f5c0029399d6df8c76f9`
updates `engine/world/NereidRipples.h` to reject the invalid
`UINT64_MAX` packed-water ID before adding one for stable
presentation identity in the non-tiled shader path; the tiled path
already had this guard. Source review only — **no tests run**.

## Stage 33 — spatial PHYSIS-ready wind zones per NEREID water tile

**Branch:** `feature/nereid-water-core`, Draft PR #23.
**Previous head:** `9dcde885bcaee57e4efae197ae70926b36f3a25e`.
**Owner priority:** features first; no tests authored or executed.
**Commits / files:**
- `823941c971eff798db095df9bec7b3b24c16d6a9` new
  `engine/world/NereidWindField.h`: immutable-to-render
  `NereidWindZone{x,z,radius,wind,influence}`, bounded
  `NereidVisualWindField` (64 zones max), validation and weighted
  radial C1 blend over base wind with double world coordinates.
- `405809809915f87ad41532c761ba3bfad2b1ca3d` same file:
  explicit double→float final GPU sample conversion.
- `cd691eb700dcf6d8b8bc1901617a7e3fa98cb102`
  `engine/world/NereidWaterScene.h`: each indexed water tile receives
  a sample at its own world-space centre; untiled draw samples the
  observer or body basis. Zero/absent wind uses the existing uniform.
- `fdda474d7faa03bea2c8e1c8e04646672a190027`
  `engine/world/WorldScene.h`: `setNereidWindZones`,
  `nereidWindField`, existing `setNereidVisualWind` now sets
  base wind, `advanceNereidRipples` samples local wind at each pulse.
- `5b3f4ed07278384ed752e481c87b7a02ba73a93b`
  `engine/world/WorldScene.cpp`: supply field to existing water draw.
- `0184413a0cc7e910d6340cbc0b9b2602104d44db`
  `engine/world/NereidRipples.h`: generic callback sample overload
  for local winds; retain five-scalar legacy overload.
- `386d4050d2fc5a5fa33093fe7704d60530df92b8`
  `engine/world/NereidPhysisWindBridge.h`: external
  `applyNereidPhysisWindZones(scene,zones)`, clear resets zones/base.

**Usage:**
```cpp
worldScene.setNereidVisualWind({2.f,0.f,0.2f,1.f});
worldScene.setNereidWindZones({
   {1000.0,2000.0,500.0,{8.f,-3.f,0.8f,1.f},1.f}
});
// PHYSIS may publish these zones after its own read barrier, without
// linking the PHYSIS ECS module to NEREID.
```

**Ownership:** PHYSIS/host produces and owns weather, NEREID consumes
a presentation snapshot, per-body material shader receives a
`float4` wind sampled at the tile center; no ECS/gameplay mutation,
new render targets, PHYSIS header dependencies or genre coupling.
Zones are capped at 64, water surface tiles at 64. Far-world
interpolation is done in double before final GPU float conversion.
Base wind remains as a fallback outside all zones; zero zones
preserves Stage 32 behavior. Water surface normals are still
approximated per tile, not sampled every pixel.

**Actually performed:** connected GitHub source review and commits;
handoff appended. **NOT RUN:** builds, tests, GPU graphics, PHYSIS
integration, FPS/RTS regression (per owner direction). Keep PR Draft.

**Next:** Stage 34 optional deterministic current vortices tied to
water-body ECS, shared by reference/indexed sampler, buoyancy and
visual ripple flow. Add own handoff before proceeding to connections.

## Stage 34 — deterministic eddies and vortex currents in NEREID

**Branch:** `feature/nereid-water-core`, PR #23 Draft.
**Stage 33 head:** `ec98482c4e57c2c65c81f3e2a962079a02c924b1`.
**Owner instruction:** implement features before tests; no test
authoring/compilation/CTest/GPU validation in this stage.

**Commits / changed files:**
- `a385d8bf34b3dc7639b10cf54bc0606ee13846f7`: new
  `libraries/include/aiverselib/water/NereidVortices.h`,
  persistent `WaterVortex {center,radius,circulation,radial}`,
  `WaterVortexFieldComponent`, `validWaterVortexField`,
  pure deterministic `waterVortexVelocity` and
  `waterCurrentWithVortices` (up to 16 eddies per water body).
- `32cd340c88389000202757a6fe69958edfe19e4f`:
  `libraries/include/aiverselib/water/NereidWaterSystems.h`;
  optional vortex on `considerWaterCandidate`, ref sampler,
  `registerNereidVortexPersistence` schema `nereid.vortex_field` v1.
- `4f9cdc154773f27b63db7b51549ef48c8fc9e9b4`:
  `libraries/include/aiverselib/water/NereidQueryIndex.h`;
  indexed broadphase uses same local vortex current.
- `66ef99dff46fa11d02de755971f9e029e7c18721`:
  `libraries/include/aiverselib/water/NereidNativeWater.h`;
  `NativeWaterPipelineConfig::enableVortices` opt-in.
- `e833e7e15c397fa91f72e93b34dee4cae7b3b795`:
  `engine/world/NereidWaterScene.h`; read-only vortex snapshot
  and `currentForInteractionWater` local flow resolver.
- `2115f5917af7e796f89f0fb8b74fbb035d4e5442`:
  `engine/world/NereidEcsPresentation.h` copies the optional
  vortex component from the same water-body Entity.
- `1e9bb0cca252306d44276d725c9d846a2570ad21`:
  `engine/world/NereidWaterManifest.h`; strict optional
  `vortices` JSON array, bounded source values and lengths.
- `87cf38dc29d29474209f175894403cc6eabe1e0d`:
  `libraries/catalog.json` registers reusable
  `AIVERSE::lib_nereid_vortices` native package.

**Usage:**
```json
{
  "id": 7, "kind": "river", "shape": "river",
  "surfaceY": 4, "bottomY": -8,
  "points": [[0,0,3],[15,0,4],[24,8,6]],
  "vortices": [
    [15,0,6,3,-0.4], [22,6,4,-2,0.2]
  ]
}
```

The five numbers describe `[X,Z,radius,circulation,radial]`;
positive circulation rotates counterclockwise in X/Z and positive
radial moves outward. Vortex strength falls quadratically to
zero at radius, invalid fields fall back to base currents.
The new field can be attached to any NEREID water footprint, with
or without Stage 29 RiverSpline current profile. Both samplers
return shared `WaterSample.flow` already consumed by the existing
buoyancy/drag module; visual wave drift uses the copied field
through the same function. No new physics integrator, weather
ownership or genre-specific code was introduced.

**Actually performed:** GitHub source review and implementation
commits only; no tests were written/run. **NOT VERIFIED:** C++
compilation, native runtime, save/rollback execution, numerical
current matching, Box3D/FPS/RTS behaviors, GPU. PR stays Draft.

**Limits:** a sum of localized analytic swirls, not a pressure,
Navier–Stokes or collision-driven fluid simulation. Zero-radius and
invalid parameters are rejected, centre velocity is zero to avoid
division by zero, output X/Z current is capped at ±1000m/s.
Profile changes must be republished into WorldScene by the host.
Potential fixed-point overflow at enormous authored coordinates
requires verification during the later hardening phase.

**Next:** Stage 35 optional river/cascade/lake connector ribbons
using immutable NEREID surface levels and cascade geometry, with
bounded transition lengths and explicit water IDs. Update
handoff before starting any tests.

## Stage 35 — river → cascade → lake transition ribbons

**Branch:** `feature/nereid-water-core`, PR #23 Draft.
**Stage 34 baseline:** `59316bdab02a4b7b2a1c63cd8c5c2ec4e9acef26`.
**Owner priority:** finish functions first, tests/compilation deliberately
deferred. No unrelated branches merged or main modified.

### Commits and changed files

- `472488bf8b204b793747316d04f01daf3e78df42` —
  `libraries/include/aiverselib/water/NereidCascades.h`: new
  `WaterCascadeConnectionsComponent` with optional
  `inlet/outlet` generation-safe ECS Entities, `transitionLength`
  Fixed (metres) and `enabled`; independent v1 persistence ID
  `nereid.cascade_connections`, registered with the cascade package.
  Existing `WaterCascadeComponent` archive remains unchanged.
- `e667ea82fa68ef469cfd943074ca9620d33d078a` —
  `engine/world/NereidWaterScene.h`: read-only
  `surfaceLevelForId(stableWaterId)` resolves enabled water surface
  elevation without ECS mutation or GPU allocation.
- `a46f6447203b2298349c99909d66b54651e046f8` —
  `engine/world/NereidCascadeScene.h`: `NereidCascadeSource`
  gains optional `inletWaterId/outletWaterId/joinLength`.
  `connectedNereidCascade` creates a presentation copy of the
  descending ribbon with short entry/exit extensions at currently
  linked water levels, when their elevation ordering permits it.
  Joined surfaces use existing CPU mesh generation and the existing
  per-frame bounded GPU upload pipeline. Live inlet/outlet levels
  are cached in each resident slot and trigger a rebuild on change,
  including disappearance/appearance of linked water bodies.
- `c6a63c7ea549413da69b5b1cabc125e1caa88a88` —
  `engine/world/NereidCascadeManifest.h`: optional JSON fields
  `inletWaterId`, `outletWaterId`, `joinLength` with bounded values.
- `9d4965d7dee2e2e1e3cd372b0ff5b93646f0b31e` —
  `engine/world/NereidCascadeEcsPresentation.h`: read-only
  conversion of persistent connection component to stable
  `Entity::packed()+1` water scene IDs, validating link lifetime.
- `70c0d6fa12f9185191b7d460d6791eaffd8dbc9b` —
  `engine/world/WorldScene.cpp`: passes
  `&nereidWater_` to `cascades_.update(device,waterScene)`, so the
  cascade's GPU mesh is built after immutable water elevations
  have been published and can respond to live level changes.
- `716a6d73eae4bf6d03caf1a28357aa5a1661a35d` —
  `engine/world/NereidCascadeScene.h`: transition-only foam
  gradients on entry/exit joined segments mask water/cascade
  shading seams without altering the authoritative cascade points.
- `86c38ac456fc0385e8dfe135118a6500b485be40` —
  `skills/gameplay/aiverse-nereid-water/SKILL.md`: AI agent/native
  authoring documentation for Stages 33–35.

### Architecture, usage and ownership

Source water ECS remains authoritative for elevations, footprint and
priority. The existing Stage-27 cascade ECS data remains authoritative
for waterfall geometry. The optional new connection component names
the adjoining water bodies by generation-safe Entity IDs. Only the
presentation scene converts IDs to `uint64 stableId=packed+1`.
`WorldScene` publishes water source snapshots before the separate
`NereidCascadeScene` GPU update. The original cascade point
vector is never mutated; a transient copy receives an extended
ribbon aligned to the source's 3D path tangent. Purely vertical
paths fall back to the perpendicular of the authored width axis.
Entry Y matches upstream water surface if `upstreamY>=cascadeHeadY`;
exit Y matches downstream water if `downstreamY<=cascadeFootY`.
Mismatched/missing links leave the old geometry unchanged.
Updated elevations trigger dirty GPU residency, and retry
pending uploads within the existing cascade upload budget.

Sample scene fragment (surface IDs 10 and 11 should be valid,
already published water bodies):
```json
{
  "nereidCascades": {
    "sources": [{
      "id": 31,
      "inletWaterId": 10, "outletWaterId": 11,
      "joinLength": 3.0,
      "widthAxis": [1,0],
      "points": [[5,15,0,3],[7,5,0,4]],
      "style": {"opacity":0.75,"foamStrength":0.8}
    }]
  }
}
```
Native ECS projects use the optional
`WaterCascadeConnectionsComponent` attached to the same Entity as
`WaterCascadeComponent`; existing
`snapshotNereidEcsCascades(world)` picks it up automatically.
Join length is bounded to 0.1..50 metres, and the pure CPU cascade
vertex cap remains active. No new RHI texture, render pass, ECS
integrator, PHYSIS, FPS/RTS or terrain ownership is introduced.

### Verification and next agent

**Actually performed:** connected GitHub source reviews and code,
manifest, native ECS adapter, scene and handoff commits.
**Not performed (per owner):** new tests, native C++ compilation,
CTest, GL/Metal shader compilation, real GPU visual verification,
FPS/RTS/gameplay regression. Keep PR Draft until verified.

**Known limits:** these are geometric overlap strips, not
topologically guaranteed watertight connections. Each authored
cascade head/foot must be placed within or overlapping the
intended inlet/outlet water footprint and properly oriented by
`widthAxis`. The join cannot increase cascade points past its
existing 128-point validation cap (with two extra points, original
source must contain at most 126). Missing or elevation-incompatible
links fall back to the unconnected ribbon. Width at the junction
uses authored cascade halfWidth, not a fully resolved river bank
profile; foam masks color discontinuities but does not change
shaders' surface optics. This is not mass-conserving hydrodynamics,
automatic confluence detection, or current transfer across the
cascade. Rebuild after level changes is bounded by existing
upload budgets and may be delayed by device allocation.

**Next feature:** configurable source/destination width blending,
connection-surface footprint validation, current continuity through
waterfall links and optional whitewater turbulence at the seam.
After the functional backlog, run native C++ builds plus GLSL/Metal
GPU rendering and record actual results.

## Stage 36 — safe river-width cascade joins and shoreline footprint policy

**Branch:** `feature/nereid-water-core`, Draft PR #23.
**Before implementation:** checked GitHub `main` ref
`9ee6ff6fbf390bd45363bc92c838f492dfe92eee` versus NEREID
head `77fd209f286368e7cdcd50db212dae0664c81eeb`.
GitHub compare reported **0 commits behind main**; its exact
`main` commit was already an ancestor. Thus no redundant merge
or changes to main were made. If main advances during subsequent
work, refresh and recompare before concluding synchronization.

**Implementation commits / files:**
- `f6d30a7a979afd6fba55c72aa1d8e91147bfeca1` —
  `engine/world/NereidWaterScene.h`:
  `surfaceForId(stableId)` returns a synchronous, read-only
  scene-source pointer; never retain it across `setSurfaces`.
- `cd0d322c321fb31a06bb9deb2daf45f4cb2a1285` —
  `libraries/include/aiverselib/water/NereidCascades.h`:
  new optional `WaterCascadeJoinPolicyComponent` with
  `widthBlend` and `requireFootprint`; new separately named
  persistence schema `nereid.cascade_join_policy`, version 1.
  The existing `WaterCascadeConnectionsComponent` archive/version
  is unchanged.
- `d3caced80b5eab95255d94dcdc576d234f3501dc` —
  `engine/world/NereidCascadeScene.h`:
  authorable scene join policy with same-source comparison and
  bounded input validation.
- `93d81d33472be98869ecf5d414dbbbdec8c8465a` —
  `engine/world/NereidCascadeManifest.h`:
  optional JSON `joinWidthBlend` in [0,1] and strictly boolean
  `requireFootprint`.
- `e6d17269476e015b1fa99051bafeec7b21c13f42` —
  `engine/world/NereidCascadeEcsPresentation.h`:
  optional independent ECS join-policy component to read-only
  scene policy mapping.
- `d9eb9d83d13650998cecb27037fd1345ae256878` —
  `engine/world/NereidCascadeScene.h`:
  overload `connectedNereidCascade(source,inletSurface*,outletSurface*)`
  that creates a *presentation-only copy*. The exterior join end
  must be inside the linked `WaterBody` footprint when
  `requireFootprint=true` (default). For RiverSpline, interpolate
  the closest segment's source `halfWidth`, blend the authored
  cascade endpoint's halfWidth towards it by `joinWidthBlend`,
  then clip both exterior ribbon edges to valid water coverage
  by bounded fixed-point binary search. Missing/non-covering
  water bodies skip that join; prior standalone cascade remains.
  The GPU resident tracks inlet/outlet `surfaceRevision` in addition
  to level, rebuilding a changed bank/river geometry without
  forcing an ECS mutation or reuploading every unrelated mesh.
  The legacy level-only overload remains for compatibility.
- `aa8dce3005b2c7b1dd55c9cc38490990a0ff53fa` —
  `engine/world/NereidCascadeScene.h`: explicit `<limits>`
  for nearest river-segment distance sentinel.

**Public use:**
```json
{
  "inletWaterId": 10,
  "outletWaterId": 11,
  "joinLength": 3.0,
  "joinWidthBlend": 1.0,
  "requireFootprint": true
}
```
Native projects can attach `WaterCascadeJoinPolicyComponent`
to the same cascade entity, with optional
`WaterCascadeConnectionsComponent`.  The interpolation/clipping
is on presentation geometry only; ECS authoritative shape, body
elevation and collision are unchanged. The source pointer must
not be retained after the current draw/update transaction.

**Actual verification:** connected GitHub ref/compare inspection,
source review and the commits above. **Not performed:** local C++
build, CTest, native gameplay, shader compilation, GPU captures or
visual seam checks (user explicitly prioritizes features).
**Limits:** checks the *exterior* join cross-section against the
water footprint, not every interior cross-section. Complex concave
banks, narrow peninsulas and multiple intersecting rivers can still
need additional subdivision or authored correction. Missing or
incompatible inlet/outlet elevations suppress the join.
The width matching policy is a visual approximation, not proof of
watertight mesh topology or mass-conserving fluid dynamics.
Keep PR Draft.

**Next:** Stage 37 read-only/opt-in fixed-point current-transfer
policy for connected water bodies near cascade mouths, shared by
reference and indexed NEREID samplers, preserving the canonical
`WaterBody.flow` and body geometry.

## Stage 37 — optional current continuity through linked cascades

**Branch:** `feature/nereid-water-core`, Draft PR #23.
**Main sync:** prior Stage 36 confirmed main already an ancestor;
no merge needed. **Owner priority:** features first, no native build
or tests this stage.

**Commits / files:**
- `d26df5fb847b4cc135c3e2e296190d0d918a9da1` —
  `libraries/include/aiverselib/water/NereidCascades.h`:
  remove unnecessary dependency on WaterSystems, explicitly include
  Simulation; add independent persisted v1
  `WaterCascadeFlowPolicyComponent{radius,blend,speedLimit,enabled}`.
  Registration is opt-in via existing
  `registerNereidCascadePersistence`; no existing archives changed.
- `ab93122ee69093a4fc6aea7bf8e8d71b0f291f7a` —
  NEW `libraries/include/aiverselib/water/NereidCascadeFlow.h`:
  bounded deterministic reference ECS policy near a linked
  inlet/outlet. It steers WaterSample.flow toward the cascade mouth,
  and downstream combines authored cascade speed with the upstream
  river/eddy current speed, capped by `speedLimit`.
  Multiple contributions are resolved by sorted generation-safe
  cascade IDs (up to 64); no ECS body flow mutations.
- `50ae5f41d79f052eb3e5a3837c346da3affdcc8e`,
  `0ad6fa24c7a195a6f385901c4ae610b82f77aaa8` —
  `libraries/include/aiverselib/water/NereidWaterSystems.h` and
  `NereidQueryIndex.h` now apply exactly the same post-sampling
  `applyWaterCascadeFlow(world,position,sample)` for submerged
  contacts (normal and indexed broadphase).
- `2be4372785e9d4b393dcd83c3f24799e8d0a2b5f` —
  `NereidCascadeFlow.h` refactors the shared mouth contribution
  (`waterCascadeMouthContribution`) and stable reducer
  (`blendWaterCascadeContributions`) for ECS and presentation.
- `95ecb740307cc36cdfd9502abc7fdd4bd80a6dca` —
  `engine/world/NereidCascadeScene.h`:
  optional `NereidCascadeSource::flowPolicy` +
  `useFlowPolicy`; read-only `flowAtConnection` reuses same
  Fixed-point mouth kernel and current profiles in water sources.
- `4b434200b4851cb27529d507a5b042a48699e80c` —
  `engine/world/NereidCascadeManifest.h`:
  optional JSON `flowContinuity` fields.
- `294213069315c539d42dfa1e0ce4a2f448107a7b` —
  `engine/world/NereidCascadeEcsPresentation.h`:
  copy optional ECS flow-policy component into read-only scene source.
- `7bc06992ae2473d1d8fd451de00269bd0cb01c4b` —
  `engine/world/WorldScene.h`:
  presentation ripple advection combines existing local water
  profile/vortex current with cascade outlet/inlet blending,
  using exactly the shared Fixed-point contribution kernel.

**API / example:**
```json
{
  "id": 31, "inletWaterId": 10, "outletWaterId": 11,
  "joinLength": 3,
  "flowContinuity": {
    "enabled": true,
    "radius": 8,
    "blend": 0.75,
    "speedLimit": 30
  }
}
```

ECS-native games attach `WaterCascadeConnectionsComponent`,
`WaterCascadeComponent` and optional
`WaterCascadeFlowPolicyComponent` to the same cascade Entity.
Register cascade persistence via
`NativeWaterPipelineConfig::enableCascades=true`.
The shared water contact selects a single body as before;
its `WaterSample.flow` receives an additional **locally blended**
current around a connected cascade mouth, only while submerged.
Existing buoyancy/drag therefore observes continuity without
changing the authoritative `WaterBodyComponent.flow`.
This is *not mass transfer*: no conservation of fluid volume,
pressure equation, cascade geometry motion or integration of
rigid bodies was introduced. Optional scene JSON/immutable
ECS snapshots supply the same flow to visual ripple drift.

**Actually performed:** GitHub source review and implementation
commits. **NOT RUN:** compilation, CTest, per-platform shader
validation, GPU visuals, FPS/RTS/Box3D regression, save/replay
execution (features-first direction). PR stays Draft.

**Known limitations:** flow near mouths is an analytic radial blend,
not full Navier–Stokes dynamics; the authored cascade direction and
position must match its linked water footprint. Up to 64
generation-safe cascade contributions are considered; if more
are eligible, the lowest IDs win deterministically. The visual
side uses the immutable NEREID water snapshot; the host must
publish changed sources after an ECS read barrier. Scene flow
policy changes do not require a static mesh re-upload.

**Next stage:** Stage 38 optional rapid/whitewater surface response
based on local current and vortex magnitude, per-tile water shader
inputs; preserve NEREID GPU material ownership and default-off
behavior, then append per-stage handoff before test phase.

## Stage 38 — opt-in current-driven whitewater for fast rivers and cascade outlets

**Branch:** `feature/nereid-water-core`, Draft PR #23.
**Stage 37 handoff:** `009c6717c0e223429f96e781b329055af4d1b592`.
**Main synchronization:** before Stage 36, current main was already
in the branch ancestry (0 behind), no redundant merge required.
**Priority:** functions before tests. No tests written or executed.

**Implementation commits / files:**
- `fd85c36b8147687c073aa83f00e9b869dec916b0` —
  `libraries/include/aiverselib/water/NereidWaterSurfaceMesh.h`;
  three material parameters, all opt-in:
  `rapidFoamStrength=0`, `rapidFlowThreshold=3` m/s and
  `rapidNoiseScale=1`.
- `00b8ad59d7e33c751c4a0ec52f575184c13adf07` —
  `engine/render/NereidWaterRenderer.h`: style bounds and finite
  validation; `47a230446f03d4e1ac731864529b0dd1e3ecfe9f` —
  `engine/render/NereidSurfaceResident.h`: residency key
  invalidation on material changes;
  `b7a17c15ff9b6e9ace21cc94beaaddc35179969a` —
  `engine/world/NereidWaterScene.h`: scene style comparison;
  `cc5f120ecb0eef02a7cdd643544a5e82949079ee` —
  `engine/world/NereidWaterManifest.h`: JSON style controls.
- `ded57e64bd628a4cafea95aafc0ae64eaa05c36c` —
  `engine/render/Renderer3D.h`;
  `c448c9634587f1e32fa8600c7734df7698eefd65` —
  `engine/render/Renderer3D.cpp`;
  `6cd08c5d5b12a4631b23793d690b73207dedb4b4` —
  `engine/render/Renderer3DReflect.cpp`:
  one additional float4 `waterHydraulics` in the shared
  deferred draw U0. Other materials' fields default to zero.
- `1265bc46c049b1670177b51f27a59a668fc01cf6` —
  `engine/render/NereidWaterRenderer.h`;
  `30f6ff76a3005235dd605012bc59cf53a6912d6c` —
  `engine/render/NereidSurfaceResident.h`:
  carry the rapid current data through ordinary and indexed tile
  draw submissions.
- `402c2275efd89cbcd7a4e7bee8995a485e875e94` —
  `engine/render/Shaders.h`: matching GLSL/MSL shared U0 field;
  NEREID-only animated directional whitewater breakup, foam tint
  and local normal perturbation. The branch is inactive when
  `waterHydraulics.z` intensity is zero.
- `049fffec03d4ff21ddd557917360a5831076df04` —
  `engine/world/NereidWaterScene.h`: when enabled, samples local
  water CurrentField+VortexField at each tile centre/observer,
  optional callback applies Stage-37 cascade transfer current,
  then maps X/Z velocity and speed-above-threshold to a
  four-float GPU parameter vector. Does NOT change ECS bodies or
  re-upload the water geometry every frame.
- `c4540c86fd04f5c3e16827dfcf4f20be572e4a27` —
  `engine/world/WorldScene.cpp`: supplies the existing NEREID
  cascade transfer function to the water scene presentation draw.
- `8f899ae8062a1b6659d3fffb6f039908137c2bda` —
  `skills/gameplay/aiverse-nereid-water/SKILL.md`:
  agent guidance for Stages 36–38.

**API example:**
```json
{
  "nereidWater": {
    "surfaces": [{
      "id": 10, "kind": "river", "shape": "river",
      "surfaceY": 5, "bottomY": -2,
      "points": [[0,0,5],[20,0,5],[35,9,4]],
      "style": {
        "rapidFoamStrength": 0.9,
        "rapidFlowThreshold": 3,
        "rapidNoiseScale": 1.5
      }
    }]
  }
}
```

**Architecture:** Authoritative fixed ECS water body, local river flow
and analytical vortex components remain unchanged. The visual
scene samples them through the existing read-only NEREID snapshot.
If this source is linked to cascades with Stage-37
`flowContinuity`, the optional `WorldScene` callback adds mouth
flow; otherwise ordinary water current remains. The renderer
receives `waterHydraulics={currentX,currentZ,intensity,noiseScale}`
for each water tile/draw. Existing GLSL/MSL water fragments blend
animated whitewater breakup toward a pale foam shade and gently
perturb normals for lighting/refraction. Shader complexity is zero
extra work in the default opt-out branch but the shared U0 is larger
by another float4. No new render pass, texture, compute shader,
authoritative fluid integrator or genre coupling.

**Actually performed:** connected GitHub source review, implementation
commits and documentation. **NOT RUN / UNVERIFIED:** native C++
compile, full CTest, GLSL and Metal shader compilers, real scenes,
physical performance or FPS/RTS regression. User requests
functionality first. **Keep PR Draft**; do not claim completed
verification or GPU appearance.

**Known limitations:** fixed per-tile/observer flow sampling produces
an approximate uniform whitewater intensity per draw rather than
per-pixel current sampling or full navier-stokes whitewater.
Untiled very large rivers may underrepresent spatial variation.
The foam tint is currently constant and derived from the animated
water shader; independent per-body foam RGB tuning would require
extra material inputs. No actual bubble collision or physical
turbulence is simulated. Build+shader validation is mandatory
before merging these changes.

**Next agent:** continue remaining function backlog, including
optional rapid/turbulence detail quality tiers, confluence handling,
automatic bank-aware cascade width transitions, and exposing
configurable whitewater foam colour. Only once feature completion is
confirmed should native/build/GLSL/MSL/RTS/FPS tests be run and their
actual results recorded in a new handoff.


## Stage 39 — authorable per-water-surface rapid foam colour

**Implementation commit:** `1d7857e68c9783d528c591229a91058115cc1864` on `feature/nereid-water-core` (Draft PR #23; main ancestor 0 behind).
Full independent agent handoff: [`docs/nereid/stage-39-rapid-foam-color.md`](stage-39-rapid-foam-color.md).

New `WaterSurfaceStyle::rapidFoamR/G/B` and scene JSON `style.rapidFoamColor: [r,g,b]` provide 0..1 RGB for animated whitewater. The default `{0.84,0.93,0.94}` retains the prior GLSL/Metal look. Validated by `validNereidWaterStyle`; included in scene/resident cache comparisons. Added `waterRapidColor` float4 at the end of the shared C++/GLSL/MSL U0 draw layout and forwarded from water draw through Renderer3D. Water-body geometry, ECS flow, legacy genres, and PHYSIS/Box3D ownership unchanged.

**Verification performed:** reviewed source and authored code through connected GitHub; committed atomically to feature branch. **NOT RUN:** CMake, CTest, shader compilation, native sample, GPU inspection, regressions. **Limit:** shader-only color currently triggers material residency-key invalidation; U0 grows 16 bytes. **Next:** feature-first detail quality / confluence current; revisit residency split, then run actual verification before merge. See linked per-stage handoff.


## Stage 40 — whitewater detail tiers

**Branch** `feature/nereid-water-core`, Draft PR #23. **Code + docs:** same atomic commit. Detailed independent handoff: [Stage 40](stage-40-whitewater-quality.md).
Added `WaterRapidDetail {Low,Medium,High}` to presentation style (Medium default), scene JSON `style.rapidDetail`, validity and style-cache comparisons. Packed ordinal into existing `waterRapidColor.a` without new uniform bytes. GLSL and Metal use simpler Low, exact prior Medium, richer High. No changes to fixed ECS water, PHYSIS or genre modules. **Verified:** source review and remote commit only; **NOT RUN:** compilation, CTest, shader compiler, GPU visual checks or regressions. **Next:** linked river confluence flow, then style-only residency and verification. PR stays draft.


## Stage 41 — linked confluence current ECS policy

**Branch:** `feature/nereid-water-core`, Draft PR #23, code+handoff atomic commit. Full handoff: [Stage 41 confluence ECS](stage-41-confluence-ecs.md).
New `NereidConfluences.h` optional persistent v1 `WaterConfluenceComponent` (2–4 explicitly linked inlets, outlet, mouth, radius, blend and speed limit). `NereidConfluenceFlow.h` calculates deterministic Fixed X/Z current around junctions: all linked bodies must be alive, valid and cover the mouth, sorted Entity IDs prevent pool-order effects, up to 32 stable contributions. Both reference and indexed samplers call the same policy BEFORE cascade continuity; only the selected `WaterSample.flow` changes. `NativeWaterPipelineConfig::enableConfluences` registers it opt-in, false by default. No genre/Box3D/PHYSIS or ECS water-body changes. **Actual checks:** GitHub source review/commit only. **NOT run:** compile, tests, GPU, replay, regressions. **Next:** immutable ECS-to-scene confluence adapter (Stage 42); defer final test phase. Keep PR Draft.


### Stage 41 source correction (follow-up commit)

Corrected a source-level compile incompatibility: `Fixed` has no integer constructor; the inlet count now uses `Fixed::fromInt`. The contribution now carries its own `speedLimit`, and the shared reducer applies the strictest limit **only among the retained lowest-ID junctions**. Otherwise a discarded junction could silently constrain the result. The correction was made by GitHub connector source inspection; native build, tests and replay remain **not run**.


## Stage 42 — read-only confluence scene bridge

**Branch:** `feature/nereid-water-core`, Draft PR #23, code and per-stage handoff atomic. [Stage 42](stage-42-confluence-scene.md) contains files, API, limitations and next steps.
New `NereidConfluenceScene.h` stores immutable confluence snapshots by stable id, checks linked `NereidWaterScene` bodies, and computes confluence flow through the same Fixed-point Stage-41 kernel/reducer. New `NereidConfluenceEcsPresentation.h` snapshots live complete ECS junctions at read barrier using Entity packed()+1 water IDs. `WorldScene::setNereidConfluences` opts in; ripple advection and per-tile whitewater follow confluence then cascade in the same order as water sampling. No ECS writes or GPU geometry ownership changes. **Verified only by source inspection/remote commit**, not by compiler, CTest, GPU or native game. **Next:** split shader-only material updates from mesh residency; run deferred tests before merging.


## Stage 43 — draw-only water material updates without mesh upload

**Branch** `feature/nereid-water-core`, Draft PR #23. Code + documentation atomic commit. Independent handoff: [Stage 43](stage-43-material-residency.md).
Added `sameNereidWaterVertexStyle`, `sameNereidSurfaceUploadKey` and `NereidSurfaceResident::setDrawStyle`; full public key equality remains. Changed NereidWaterScene revision to only geometry/vertex-baked fields, while current, flow, optics and rapids are draw-only; preserve latest style after async mesh upload. Vertex baked deep/foam RGB/strength still rebuild meshes. **Performed:** GitHub source inspection and commit only. **NOT RUN:** C++ build, shaders, CTest, GPU metrics, scene parity or other genre regressions. **Next:** feature completeness check, then full validation and repair. Keep Draft PR.


## Stage 44 — author visual confluences in WorldScene JSON

**Branch:** `feature/nereid-water-core`, Draft PR #23; implementation and this handoff committed atomically. **Full agent handoff:** [Stage 44 confluence JSON](stage-44-confluence-manifest.md).

`nereidConfluences.sources` now accepts explicit scene IDs (`id`, `inletWaterIds` 2–4, `outletWaterId`, `mouth`, `radius`, `blend`, `speedLimit`, `enabled`) and links to explicit `nereidWater.surfaces[].id`. The new manifest parser validates numeric bounds, duplicates, self-links, unknown water IDs and all X/Z mouth footprints before constructing the scene. Disabled entries are validated but omitted. Changed ECS/scene validation split: `validWaterConfluenceFlowPolicy` checks Fixed flow settings independently; `validWaterConfluence` still validates canonical Entity links. Shared mouth math now uses numeric policy, allowing visual sources with no fake ECS handles. `WorldScene.cpp` parses and publishes to Stage 42 read-only scene; ECS water systems and persistence remain intact. **Actually performed:** source review + commit; **NOT RUN:** build, CTest, shaders, GPU, native simulation or regressions. **Next:** dedicated test/CI validation, fix failures. Keep Draft PR.


## Stage 45 — authored NEREID confluence regression contract

**Branch:** `feature/nereid-water-core`, Draft PR #23; test code, CMake and docs atomic commit. Full handoff: [Stage 45](stage-45-confluence-contract-tests.md).
Added CPU-only `tests/ecs_nereid_confluence_contract_test.cpp` to assert JSON validation, valid/disabled confluence, deterministic ECS reference vs indexed vs scene flow, and Stage 43 mesh-upload key semantics. Registered `aiverse_ecs_nereid_confluence_contract_test` in CMake/CTest and `tests/run_ecs_experiment_suite.sh`. **Actually performed:** reviewed code, authored test and remote commit. **NOT run:** C++ compilation, CTest, GitHub Actions passing job, GLSL/MSL shaders, GPU, save/replay, FPS/RTS/PHYSIS regressions. **Next:** execute test and fix all actual errors; then full validation. PR remains Draft.


### Stage 45.1 — source-level link dependency repair

Inspected exact definitions: `worldInteger` and `worldVector` are implemented (not inline) in `engine/world/WorldAssets.cpp`. Both renderer contract and confluence contract include NEREID manifest parsing and therefore need those symbols. The existing CMake targets linked only `aiverse_engine` plus the NEREID header-only package, missing `aiverse_world` (which owns `WorldAssets.cpp`). Updated BOTH CMake test link libraries to include `aiverse_world`. The standalone `tests/run_ecs_experiment_suite.sh` now compiles `engine/world/WorldAssets.cpp` and `engine/pack/Sha256.cpp` for both tests; the latter resolves references from `WorldAssets::verifiedReader` even though the fixture itself does not call it.

**Actually performed:** read CMake targets, `engine/actor/CMakeLists.txt` world library ownership and WorldAssets/Sha256 source via GitHub connector; applied guarded source patch/atomic commit. **Not performed:** native compile/link, CTest, shell execution or green CI. Git clone of repo from the local container failed (GitHub hostname DNS resolution unavailable); observed GitHub Actions for old SHAs remained queued/cancelled. Do not claim test PASS. **Next:** run the two CPU contracts on a checkout with network/source, resolve any compiler/logic failures, then full suite.


### Stage 45.2 — macOS Clang template parsing correction (reported by owner)

**Reported actual failure:** local macOS C++ build, `tests/ecs_nereid_native_pipeline_test.cpp` lines 61, 62, 77, 78, 80, 81, 86 and 87: Clang `use 'template' keyword to treat 'get' as a dependent template name`.

**Fix:** in templated `exercise<Tag>`, all eight `linear->world().get<T>()` and `indexed->world().get<T>()` expressions now use the required dependent-member disambiguator: `linear->world().template get<T>()` / `indexed->world().template get<T>()`. No water API, ECS semantics, runtime logic or other module changed. The source was verified to contain exactly eight such calls and all eight were replaced.

**Actually verified:** user-provided Clang diagnostic and connected GitHub source inspection, exact guarded transformation and committed branch update. **NOT verified:** a new compile/CTest run after the fix; no claim of PASS. Next: `cmake --build build/nereid-test --parallel 4`, then `ctest --test-dir build/nereid-test --output-on-failure --parallel 4`. If the build fails elsewhere, record first error and repair; PR #23 remains Draft.


## Stage 45.3 — runtime target missing NEREID include usage (macOS build error)

**Branch:** `feature/nereid-water-core`; PR #23 Draft. Full handoff: [Stage 45.3](stage-45-3-runtime-include-fix.md).

**Actual failure reported:** building `aiverse_runtime` / `engine/world/WorldScene.cpp` fails in `engine/render/NereidWaterRenderer.h` because public `aiverselib/water/NereidWaterSurfaceMesh.h` cannot be found. The header exists in `libraries/include`; `aiverse_runtime` lacked a direct PUBLIC target link to `AIVERSE::lib_nereid_water`, so the compiler did not inherit its INTERFACE include dirs. Added that runtime target dependency in root CMake; no gameplay or water source changes. **Performed:** inspection of GitHub CMake and source + commit. **Not run:** local macOS reconfigure/build, CTest, GPU, shaders or genre regressions. **Next:** reconfigure and rebuild on macOS, fix subsequent compiler errors, retain Draft PR.


## Stage 45.4 — SpectraFX upstream header compile fix selected for water integration

**Date:** 2026-11-19. **Branch:** `feature/nereid-water-core` (Draft PR #23, no merge to main). **Implementation commit:** `191b22e5948b502651ec3da53777e1b9873181f7`.

**User-reported build error:** Clang reports `unknown type name ParticleSystem` in `engine/fx/VfxGraphAssets.h`, then unknown `fx::ParticleSystem`/`fx::FxHandle` in `engine/fx/SpectraFX.h`, while compiling `tests/vfx_graph_compiler_test.cpp`. These are compile-time public-header dependency failures unrelated to NEREID simulation.

**Discovery:** there is no dedicated branch named SpectraFX. Active upstream VFX development is in `feature/universal-entity-inspector-fps` (Stage 40 on its head). Compared against NEREID branch: **diverged**, 520 upstream-ahead / 356 water-ahead commits at inspection; a full merge risks unresolved CMake/editor/render conflicts and changes outside NEREID scope. The exact upstream fix was already committed as `3aa17d390fae819458cc3f89127f25804e3b2877`, changing only `engine/fx/VfxGraphAssets.h` to include `"ParticleSystem.h"` after `"VfxGraph.h"`.

**Action taken:** applied the identical upstream one-line fix to the water branch without merging unrelated commits. Verified the resulting `VfxGraphAssets.h` content is byte-identical to the corresponding SpectraFX-upstream header. This supplies the complete ParticleSystem type for inline `ps_.define`, and transitively exposes `ParticleSystem`/`FxHandle` for aliases in `SpectraFX.h`. No gameplay, VFX behavior, ECS schema, GPU code or water code changed.

**Actually verified:** GitHub branch listing, branch divergence, upstream commit diff, source comparison, and remote commit/ref update.
**NOT run:** macOS Clang recompile, CTest, graph runtime checks, VFX GPU/Metal effects, full water/FPS/RTS regression. Compilation is not claimed successful. Keep Draft PR.

**Next command:** fetch/pull `feature/nereid-water-core`, reconfigure only if required by other changes, and rerun `cmake --build build/nereid-test --parallel 4`; if it succeeds, run `ctest --test-dir build/nereid-test --output-on-failure --parallel 4`. Report the first new compiler/runtime failure and continue in the same branch.


## Stage 45.5 — SpectraFX ECS presenter test missing snapshot vector (macOS compile)

**Date:** 2026-11-19. **Destination:** `feature/nereid-water-core`, PR #23 stays Draft. **Source:** owner-supplied Clang compiler failure at `tests/vfx_ecs_presentation_test.cpp:87-88` (`particles` undeclared).

**Cause:** The rollback-reset test added `ps.snapshot(particles)` and `particles.empty()`, but never declared the local collection. Checked both the water branch and current SpectraFX development branch (`feature/universal-entity-inspector-fps`): the same defect is present in both. This is a test-local type declaration, not a missing engine API or a water component issue.

**Fix:** Introduced `std::vector<fx::FxParticleInfo> particles;` immediately before `ps.snapshot(particles)` inside `scenario<Kind>()`. The source already includes `<vector>`, and `ParticleSystem::snapshot(std::vector<FxParticleInfo>&) const` is the public API. No runtime FX behavior, ECS state or NEREID game logic was changed.

**Verification performed:** compared the two branch files, inspected `ParticleSystem.h` public API and committed the one-line fix via the GitHub connector. **NOT RUN:** actual macOS build, CTest, GPU validation, full regression. Do not claim test PASS. **Next:** pull the NEREID branch, run `cmake --build build/nereid-test --parallel 4`; after a successful build, run `ctest --test-dir build/nereid-test --output-on-failure --parallel 4`. Share subsequent failures; keep PR Draft until build and tests pass. **Upstream note:** the originating SpectraFX branch has the same bug, so the SpectraFX agent should also take this patch.


## Stage 45.6 — Clang aggregate initializer warning in NEREID cascade slots

**Date:** 2026-11-19. **Branch:** `feature/nereid-water-core` (PR #23 Draft). **Reported macOS build:** `engine/world/NereidCascadeScene.h:286:55: warning: missing field 'inletLevel' initializer [-Wmissing-field-initializers]` while `aiverse_runtime` is compiling. The rest of the build was still in progress (77% reported); this is a warning rather than a build failure.

**Root cause:** `NereidCascadeScene::Slot` contains `source`, GPU mesh, flags, optional `inletLevel/outletLevel`, revisions and local origin. `Slot{std::move(source)}` value-initializes omitted aggregate fields but triggers Clang's missing-field warning, especially for the optional level member.

**Change:** construct `Slot slot{};` so all members are explicitly initialized, then `slot.source=std::move(source);`, followed by `next.emplace(id,std::move(slot))`. Keeps default flags, empty GPU handles/optional levels, 0 revisions/origin, map identity and duplicate error unchanged. No fixed ECS data, water physics, scene geometry or renderer algorithms changed.

**Actually verified:** source inspected through GitHub connector, offending initializer located exactly once, guarded patch and branch commit. **Not run:** macOS recompile, CTest or GPU verification; cannot claim warning fully gone until user reruns. **Next:** let the current build finish and report failures; after fetching this change rebuild `aiverse_runtime` or full target, and run CTest when compilation succeeds. Keep PR Draft.


## Stage 45.7 — SpectraFX test regression: unsigned underflow during gameplay event check

**Date:** 2026-11-19. **Branch:** `feature/nereid-water-core`, PR #23 Draft. **Actual failure:** on macOS, CTest `vfx_ecs_presentation` (109/118) aborted with `std::runtime_error: gameplay event did not play`.

**Root cause:** the test compared `size_t once >= before-20`, where `before` is an unsigned particle count from a low-rate continuous emitter. When `before<20`, subtraction wraps to an enormous `size_t` value and reports a failure even when gameplay-event emission works.

**Fix:** assert the actual `impact_flash` particle ID in a headless `ParticleSystem::snapshot`: the fixture's High-quality three-particle burst, with 0.5-second lifetime, must have exactly three impact particles after one 0.1-second update. Replaying the same tick must not add another impact burst, tested after a second 0.1-second update. Existing reset check still requires no attached or one-shot particles to remain. Added `<algorithm>` for `std::count_if`. No shipping VFX, ECS or water code changed.

**Performed:** GitHub source inspection of CPU emission and presenter, guarded edit, branch commit and handoff. **Not performed:** native C++ build/CTest rerun, GPU check or FPS/RTS/PHYSIS regressions. **Next:** pull, build `aiverse_vfx_ecs_tests`, run `ctest --test-dir build/nereid-test --output-on-failure -R '^vfx_ecs_presentation$'`; if successful, full CTest. Remain Draft.


## Stage 46 — fix two failures observed in the native NEREID CTest run

**Date:** 2026-11-19. **Branch:** `feature/nereid-water-core`, Draft PR #23. **Base:** `07b67ec8016d97afdb651a55ea7cf5bb9f6bccaf`. **Actual user-reported failures:** `ecs_nereid_surface_mesh_test` aborted with `shore factor missing`; `ecs_nereid_renderer_contract_test` aborted with `JsonError: JSON error at 5:5: expected ',' or '}'`. Separately, VFX failure from the same earlier run still reports the old `gameplay event did not play` text, which was removed in commit `07b67ec8`; rebuild that test binary after pulling.

**NEREID surface test fix:** `WaterSurfaceMesh.h` deliberately suppresses shoreline foam at Far LOD because far river meshes only have perimeter vertices (all would turn white). The test incorrectly required a nonzero river shore factor on Far LOD. It now checks that the Far river keeps zero shore and builds an independent Medium river to assert that shoreline contribution is nonzero, retaining the original Far topology/winding expectations.

**Renderer contract test fix:** Source inspection with JSON parsing of all raw string fixtures found exactly four objects with a missing final root `}`, at original source lines **263, 423, 460, 591** (LOD, refraction, depth optics, underwater visuals). The test expected these JSON examples to load. Added one closing brace to each and verified all extracted raw JSON literals parse using JavaScript `JSON.parse`. No shipping water mesh, renderer or gameplay code was changed.

**Actual verification:** reviewed generator branch, all test JSON strings scanned/parsed, code patch/commit via GitHub connector. **NOT executed:** native macOS C++ rebuild, CTest rerun, GPU shader compiler or genre regressions. The fixes are source-level and not yet proven passing. **Next:** resolve the separately reported duplicate NEREID skill ID/catalog regeneration; then rerun targeted CTest and full suite. Keep PR Draft.


## Stage 47 — repair duplicate NEREID skill IDs and regenerate the catalog

**Date:** 2026-11-19. **Branch:** `feature/nereid-water-core`, PR #23 Draft, parent `a65ad8ca8127ae7dae8f2d4b66cd04469e5b76bd`. **Actual macOS failures:** `skill_routing` fails with `ValueError: duplicate skill id` from `tools/skills/catalog.py validate`; `skill_catalog` reports `stale skill catalog; run generate`.

**Root cause:** six separately registered native NEREID package IDs (`nereid-water/buoyancy/interactions/cascades/currents/vortices`) all pointed at the same `skills/gameplay/aiverse-nereid-water/SKILL.md`. The generator creates one catalog entry **per package** and derives the entry ID from skill-directory name, producing the ID `aiverse-nereid-water` six times. The committed generated JSON was also stale (only the original `nereid-water` entry, and appended unsorted newer entries).

**Fix:** give each of the five specialized packages its own stable skill ID, frontmatter and `SKILL.md` while retaining the shared water skill and original library targets. Updated only those five `libraries/catalog.json` mappings. Add concise package-specific instructions, sources and CMake targets; include the full NEREID skill handoff as a common reference. Preserve package dependencies on the core skill. Explicit routing aliases for all six water skills are in `tools/skills/catalog.py`. Rebuilt `skills/catalog.json` from current catalog entries and all native package metadata, sorted by ID, with 219 unique IDs and original unmodified meta/workflow entries. This follows `generate`'s entry fields/ordering and JSON indentation.

**Source-level checks performed:** duplicate scan now finds zero; all 134 library packages have one skill ID and corresponding catalog entry; all dependencies resolve; five new SKILL frontmatter names match their catalog IDs; target names correspond to packages. **NOT RUN:** native `python3 tools/skills/catalog.py generate/check`, `tests/skills/catalog_test.py`, full skill audit or CTest (no runnable AIVERSE checkout); if generator reports differences, regenerate on a real checkout and commit its exact output. **Next:** pull branch, `python3 tools/skills/catalog.py check`, `python3 tests/skills/catalog_test.py`, CTest skill tests; then complete all pending NEREID/VFX regressions. Do not merge Draft PR until verified.


## Stage 48 — large-world ocean LOD fixture corrected (macOS CTest failure)

**Date:** 2026-11-19; branch `feature/nereid-water-core`; Draft PR #23; parent `caba95e19f5bc9d9b29dbe0f4393e2cfe511ed33`. **Actual report:** `ecs_nereid_renderer_contract_test` aborts with `large-world ocean patch vertices must be local and stable` during native CTest. Earlier JSON fixture errors are resolved far enough for the test to reach the large-world section.

**Root cause:** the assertion expected an unsubdivided four-vertex ocean patch; its `WaterSurfaceMeshOptions seaOptions` left `lod` at the default **Medium**. In `buildWaterSurfaceMesh`, Medium uses one round of triangle subdivision; only **Far** is the unsubdivided four-vertex path. The patch's precise double-precision coordinates, origins and GPU key are not implicated by this test's misleading message.

**Changes:** explicitly set `seaOptions.lod=WaterSurfaceLod::Far` before the four-vertex assertion. Added a separate regression case for `Medium`: CPU mesh must contain more than four vertices while retaining the expected -1 local X/Z and double-precision snapped key. No water mesh generation, ECS, RHI, shader, gameplay or resident-cache algorithms changed. Changed only `tests/ecs_nereid_renderer_contract_test.cpp`, this handoff and the NEREID documentation pointers.

**Verification actually performed:** inspected GitHub source of the test, `NereidSurfaceResident::prepare`, the shared water mesh LOD defaults, quad generation and `splitMesh`; guarded source edit and commit to connected GitHub. **NOT RUN:** native C++ compile, CTest rerun, Metal/GLSL shaders, GPU capture, performance or FPS/RTS/PHYSIS regressions. **Next:** pull the branch, rebuild `aiverse_ecs_nereid_renderer_contract_test`, execute targeted CTest, then full suite and investigate any subsequent failures. PR remains Draft.


## Stage 49 — reproducible NEREID stabilization gate and CI

**Date:** 2026-11-19. **Branch:** `feature/nereid-water-core`, Draft PR #23. **Implementation SHA:** `27dfb6291d4932fee75e88c1dd59762714a6553c`. **Handoff:** `docs/nereid/stage-49-validation-gate.md` (committed separately after implementation per `AGENTS.md`).

**Why:** native macOS CTest found independent renderer, surface, VFX, and skill catalog failures; previous isolated fixes were committed, but not all test executables had been rebuilt for each rerun. The owner's latest targeted `ecs_nereid_renderer_contract_test` result was **PASS (1/1)** following Stage 48. No complete green 118-test CTest has been shown.

**Implemented validation:** added `tools/nereid/validate.sh` (portable Bash) with `--focused` or `--full`. It captures current Git HEAD, runs `tools/skills/catalog.py check`, routing regression and six NEREID-skill audit, configures a separate CMake build, **builds targets before tests** (focused list or *all* targets for full mode), then executes NEREID ECS/geometry/renderer/confluence, native RTS integration, three VFX graph/presentation/assets checks, and three skill CTests. Full mode additionally executes **all CTest tests**. Each stage writes a log and exit status under `<build>/nereid-logs`. A failing command returns a nonzero exit and does not get masked by `tee`. `AIVERSE_NEREID_BUILD_DIR` and `AIVERSE_NEREID_JOBS` customize execution; default build is `build/nereid-validation`. It does **not** regenerate skills, change `main`, or silently skip failed tests.

**Automated CI:** new `.github/workflows/nereid-regression.yml` runs the same focused gate on `ubuntu-24.04` and `macos-15` for NEREID branch pushes/related PR changes and offers manual workflow dispatch. The workflow builds CPU contracts without requiring a graphics Device and uploads per-stage logs. These jobs are **not** GPU/Metal shader or visual quality proof.

**Documentation fix:** specialized `aiverse-nereid-currents` and `aiverse-nereid-vortices` skills now specify that optional persistence registration functions are declared in `<aiverselib/water/NereidWaterSystems.h>` and must not be registered twice when `registerNativeWaterPipeline` enables them. There was no water runtime or ECS API change.

**Actually verified:** inspected CMake target/test names, API declarations, the existing workflow conventions, CI branch/pull request status and created the implementation commit; checked published GitHub files and handoff. **NOT RUN:** this new Bash script, CMake or native CTest in current agent runtime; not yet observed new CI running or succeeding. Existing workflow runs on old branch SHAs were queued. Do not assert PASS for the gate.

**Immediate next action:** after pulling the branch on Mac, run `AIVERSE_NEREID_BUILD_DIR=build/nereid-test bash tools/nereid/validate.sh --full` for the complete suite (or `--focused` for faster triage). Report the logs from `build/nereid-test/nereid-logs`. Fix any remaining failures, then collect real Metal/GLSL visual and performance evidence before marking PR #23 Ready.


## Stage 50 — observed full macOS green run, and Release-test warning repair

**Date:** 2026-11-19. **Branch:** `feature/nereid-water-core`; PR #23 remains Draft. **Tested and passing SHA:** `65346f91c675b7a7bdd8313fcce03c829a336db2`. **Test-correctness implementation SHA:** `8f426376ecb095017cefdd313cca5dd6c2d70186` (requires rerun). See independent handoff `docs/nereid/stage-50-release-assertion-correctness.md`.

**ACTUAL user-provided macOS test evidence (on SHA `65346f91`):** `context.txt` confirms full-mode Release run on Mac Studio. `configure(5).log`: CMake configure/generate succeeded. `build-all.log`: entire build succeeded, ending `[100%] Built target aiverse_editor`. `ctest-focused.log`: **16/16 PASS**, including NEREID ECS, surface, renderer, confluence, SpectraFX and skills. `ctest-full.log`: **118/118 PASS, 0 failed, 268.32 sec**. `skill-catalog.log`: validated **219 skills**. `skill-routing.log`: passed. `skill-audit.log`: **0 errors / 0 warnings**. `summary(5).txt`: every one of seven stages returned exit=0. The logs were supplied by the owner; no need to infer CI success from pending GitHub jobs.

**Warning/NOTE review:** `build-all.log` contains exactly **18 Clang warnings** (`-Wunused-variable`, `-Wunused-parameter`, `-Wunused-but-set-variable`) and **3 Clang notes** (template/lambda instantiation context, not independent defects) across six **test** translation units: `ecs_scene_hierarchy_simulation_test.cpp`, `ecs_guarded_failure_test.cpp`, `ecs_simulation_bridge_test.cpp`, `ecs_cross_genre_mechanics_test.cpp`, `ecs_projectile_composition_test.cpp` and `ecs_cow_rollback_session_test.cpp`. Cause: these tests used standard `assert(...)`, which is compiled away under `NDEBUG`/Release; variables only inspected inside assertions then trigger unused warnings and important test contracts were **not actually checked in Release**. One projectile fixture was constructed inside `assert(collision_.add(...))` and also ceased to execute in Release. These diagnostics should not be hidden by suppressing compiler warnings.

**Correctness repair (SHA `8f426376ecb095017cefdd313cca5dd6c2d70186`):** added `tests/AlwaysCheck.h` with `AIVERSE_TEST_CHECK(expression)` that evaluates exactly once and throws a diagnostic `std::runtime_error` containing source file, line and failed expression, regardless of `NDEBUG`. Converted all **44** standard `assert()` calls in the six above-mentioned test files; eliminated each `<cassert>` include in those files. This now executes formerly-elided collision fixture creation and verifies cross-genre checksum, rollback and ECS invariants in real Release builds. **No engine, ECS, NEREID, VFX or shipping runtime code changed.**

**Actually run after code change:** an isolated smoke program reproducing the `AIVERSE_TEST_CHECK` helper was built and executed with GCC 14.2 `-std=c++20 -Wall -Wextra -Werror -pedantic` in both **`-DNDEBUG -O2` and `-O0`** modes: **both PASS**. This verifies the helper's basic Release/Debug semantics, **not** the six edited AIVERSE test files. Source review verified 44 conversions and their executable expressions. **NOT RUN after SHA `8f426376ecb095017cefdd313cca5dd6c2d70186`:** AIVERSE's macOS/CMake compilation, CTest, GPU shaders, real Metal quality/performance or platform matrix. The 118/118 run applies to the **previous** SHA, not this follow-up commit.

**Next:** pull the new branch SHA, run `AIVERSE_NEREID_BUILD_DIR=build/nereid-test AIVERSE_NEREID_JOBS=4 bash tools/nereid/validate.sh --full` again to prove that all formerly-disabled checks pass. Revisit any failures as genuine Release test regressions instead of weakening/removing checks. Once CPU suite is green on the new head, gather real Metal/GLSL/GPU evidence before PR Ready.


## Stage 51 — owner-confirmed 119-test green CTest and main integration preparation

**Date:** 2026-11-19. **Branch:** `feature/nereid-water-core`; **checked branch HEAD before this handoff:** `dcc3850f19d1e46a69280f6e317fcc60665dc0db`; **intended integration target:** `main` (owner expressly requested). **Last complete source-qualified run:** on `65346f91c675b7a7bdd8313fcce03c829a336db2`, owner provided build logs, 16/16 focused and 118/118 full CTest green. Stage 50 subsequently replaced 44 disabled Release `assert()` checks with always-on expressions at `8f426376ecb095017cefdd313cca5dd6c2d70186`.

**New owner-reported result:** full macOS `ctest-full` finished **119/119 PASS, 0 failed, 256.08 seconds**, including `vfx_graph_ops` PASS 2.44 s, `aiverse_tests` PASS 93.39 s, `skill_library_isolation` PASS 256.07 s. Validator output: `ctest-full exit=0` and `PASS: all requested NEREID validation stages`. This follow-up output was a terminal excerpt without accompanying `context.txt` or full build log, so **the exact source SHA under test and the absence of Clang warnings after Stage 50 are not independently established**. Do not attribute the 119th test's files to the remote branch without inspecting them.

**Integration source inspection:** `main` at `9ee6ff6fbf390bd45363bc92c838f492dfe92eee` was an ancestor of NEREID branch (407 commits ahead, zero behind at inspection). The branch diff is **113 files** and contains not only NEREID but shared ECS test infrastructure and initial SpectraFX graph/presentation sources. Pre-existing PR #23 targeted `feature/universal-entity-inspector-fps`, where it is marked conflicting/draft, so integration into `main` requires retargeting or a distinct main PR, not merging into the old feature base. **Do not silently merge the independent full SpectraFX/PHYSIS/TerraWeave branches.**

**Actual evidence:** user terminal output of 119 green tests, repository branch comparisons, PR metadata; no new engine runtime changes, native build, GPU captures or performance tests by this agent. **Remaining acceptance:** verify Metal and GLSL water visuals/performance including multi-height reflection, underwater optics, ocean origin rebasing, foam/rapid/cascade/confluence seams on target devices; finish or close parent PR bookkeeping after main integration.


## Stage 52 — NEREID shoreline erosion + TerraWeave soil bridge (source phase, 2026-11-19)

Merge \`a56ff765311a8f464a2e180d5dd23e7eacdcb5dd\` brought all NEREID
changes from \`main\` into \`feature/terraweave\` (no rewrite of main).
New soil mechanics on feature include \`NereidErosion.h\` with fixed/integer
flow/wave/wetness/soil resistance/sediment calculation; persistent
\`NereidShorelineSystems.h\` bank-cell ECS input/output and opt-in
\`NereidNativeWater.h\` stage after water contact sampling. Changes are
handoff to \`TerraWeaveEarthworks.h\` (sparse dirty heightfield), roots,
tree/shrub failure events and aggregated grass coverage. Source commits:
\`c085a48d\`, \`2968f177\`, \`78b5ab9b\`, \`6709cb9a\`, \`88ec8c01\`,
\`0a20fa96\`. Full design, API and ownership boundaries in
\`docs/nereid/TERRAIN_EROSION.md\`.

**Must not double-simulate:** ECS \`WaterBankSoilComponent\` is updated by
\`addNereidBankErosionSystem\`. At the ECS read barrier, the terrain host
uses \`applyEcsBankErosion\` (not \`erodeShore\` again). Separate native
Earthworks \`erodeTerrainVertex\` is for hosts that do not install the ECS
shoreline stage. Both paths reject source soil/terrain mismatch.
Falling trees require an external authoritative physics body and
save/replication; the new code emits a stable event rather than mutating
game state on the renderer thread. Grass is patch-aggregated, not per blade.

**QA:** source committed; no fresh CMake/CTest/performance/Metal test after
this stage. Earlier NEREID full test PASS applies only to preceding main
head. Keep source integrated but feature PR Draft until validation.


## 2026-11-20 native Terra landscape + NEREID host integration

Implementation b2c8c5d9235d697274239b17f29e64fbad621d0d on feature/aiv87_terrain_pbr, publication feature/terraweave. New TerraWeaveRiverbed authoring bridge carves native Earthworks from NEREID spline geometry with half-open bounded edits. Native landscape exporter uses TerrainSettings/Earthworks/terrain patches, existing scatter and rockPose; shared ground snapshot drives grass/trees/water masks. Shared ExportModel helper fixes a discovered dangling Json nested-member reference (adding material sibling keys invalidated pbr). No replacement water shader or project noise solver.

NereidManifestHost composes existing parser/residency/draw in renderbench and native editor Scene/RTS Game views. Missing water block clears previous scene. MapDoc extra owns presentation settings, not authoritative ECS water state. API additions preserve existing saves; offline assets are not live terrain/collision/ecology. Water uses renderer clock even with renderbench freeze-time. No new production rendering flags.

Executed: Release arm64 editor, renderbench, landscape/vegetation exporter and new landscape test builds PASS. CTest ^terraweave 15/15 PASS (0.72s), assertion checks enabled. Strict c++20 Wall/Wextra/Werror syntax landscape PASS; skills catalog 228 PASS; diff check PASS. Exported 48 terrain tiles, 12 tree prototypes and 202 grass patches; PBR binding audit PASS (192 terrain material bindings, 24 tree bindings, 202 constant dielectric grass materials). First generic PBR audit incorrectly required textures on constant-PBR grass and raised KeyError; corrected assertion explicitly checks grass dielectric factors. First landscape run aborted due dangling Json reference, fixed above and rerun successfully. Earlier build attempts lacked native manifest link library, then fixed a namespace ambiguity and RockInstance field name before successful build. Do not repeat those failures.

Real Metal renderbench capture /private/tmp/terra-nereid-final.png inspected: native river present, pipelineFailures=0, 61 pipelines. External project /Users/aleksandrfeer/Projects/AIVERSE-AlpineValley active rules use models/native-world.gltf; map NEREID points offset +24m X/Z to match asset entity. Actual editor Scene screenshot /private/tmp/terra-nereid-editor.png inspected and confirms water/terrain placement. Terrain/forest/mountains are authored by Terra, river by NEREID, sky uses existing PRISMA. The project has 935 trees and 200 rocks; counts are objects, not draw calls. Initial broad foam ribbon reduced with standard NEREID parameters, no shader fork.

Limits: current mountains are visibly round, crowns still lack requested species/age/winter diversity; this is integration evidence, NOT final art acceptance or SpeedTree parity. Editor lighting differs from renderbench; no Game-view capture, native gameplay buoyancy/contact test, GL/Vulkan/mobile, CI or full suite run. No performance improvement claimed. Main remains unchanged. Next: refine Terra mountain composition, canopy profiles and mixed species, season/age and near/far LOD acceptance; keep native ownership and capture scene-only results before main merge.


## 2026-11-20 NEREID animation clock stage

Implementation d3b7f872054f54449347c52e1ed9b2210baeab77 on feature/aiv87_terrain_pbr, publish feature/terraweave. Tagged NEREID water waves/cascade/whitewater use existing U1 camPos.w render clock, not cloud offsets. GLSL/MSL parity; untagged legacy water keeps cloud-clock behavior. Wave coordinates advect by configured local NEREID current, not wind direction. No uniform/save change or gameplay flow solver change.

Release editor/renderbench build PASS. Actual Metal sequences /private/tmp/nereid-flow-sequence and /private/tmp/nereid-advection-sequence captured16frames each at fixed camera/sky; zero pipeline failures. Initial frame change at800x500 mean4.84/255 (includes water/temporal effects, not quantitative flow velocity). First high foam tuning made regular bright blobs; reduced foam/refraction in external scene. Source clock repair is verified, not a substitute for editor flow controls.

Final Terra valley export /private/tmp/alpine-land-v3:48 native tiles, normal smoothness, atlas sampler/normal/dielectric audit PASS, max horizontal seam gradient ratio1.78<2. Newbank screenshot /private/tmp/nereid-bank-final.png inspected: sand transitions improved but grass still on sand, water rim too bright; user explicitly requires dense tufted grass and editor LOD verification. These remain next work. Current active externalproject assets updated locally but recipes/captures need commit once final. Main untouched.

New user requirement: flow must be editable in editor; automatic default from actual slope (example1degree), clocks only animate configured flow. Existing bodies are horizontal and current fields accept manual velocities; need additive native grade/profile contract shared by sampling, mesh and Terra bank, deterministic tests/persistence plus editor controls. Do not claim slope-based flow exists yet. Also verify LOD using editor node-level counters, not merely glTF presence. No full regression/CI/Vulkan/mobile testing.


## 2026-11-20 editable river grade native stage

Implementation f9b764f83e5b42c26585eeff0f099b933d52c449 on feature/aiv87_terrain_pbr, publication feature/terraweave. New optional WaterRiverGradeComponent schema1, opt-in native pipeline registration, deterministic inclined water plane, nearest-tangent automatic downhill velocity with configurable linear-drag response/cap, manual body.flow retained. Reference/indexed samples, contacts, ECS presentation, mesh vertices/normals/residency keys share grade. Parser accepts signed slopeDegrees and downhillHeadingDegrees or explicit risePerMetre. Typed editNereidFlow plus legacy editor agent operations validate before Undo transaction. Current velocity is supplied even when foam strength0. Terrain authoring can alignRiverGrade and carve bank from local water plane; grass mask excludes relative shore heights<270cm. PBR shoreline weights read same grade. No old component byte/version changes; optional new registration defaultsOFF to preserve old native catalog/checksums. Horizontal planar reflections and confluence/cascade reference-level connectors still approximate graded water and need dedicated future parity. Automatic current is friction-limited estimate, not hydraulic discharge conservation.

Executed: new standalone native CTest ecs_nereid_river_grade_test PASS (0.73s): reference/indexed parity, two independent hosts, native save/load/replay, COW rollback, manual/flat modes, mesh-vs-contact grade, invalid authoring. Earlier Terra landscape expanded test PASS. Release exporter/renderbench build PASS; legacy editor completed build before last minor header updates, final recompile pending. Initial direct test link missed worldVector/worldInteger; corrected WorldAssets/model dependencies and registered CMake target. New CMake target first invocation before regeneration failed no rule; explicit configure then target build PASS. No final graded GPU capture yet. Full NEREID suite/CI/cross-platform NOT RUN.

HTML5 UI is being implemented independently on feature/studio-html5 using generated Studio UI panel, typed bridge, native MapDoc Undo and native NEREID host; source copied only after baseline equality checks, no unrelated root changes overwritten. Its build/QA/handoff still pending; do not claim editor UI ready. Next finish Studio native tests/visual QA, rebuild external scene at1degree with matching Terra grade, dense tuft grass/no sand, and investigate editor LOD counters. Main untouched.


## 2026-11-20 explicit surface ownership and shoreline coverage

Implementation 8457e97bd7712ebcf187d964220aeaadef787b08 on feature/aiv87_terrain_pbr. TerrainLook.render defaulttrue for legacy projects; game/rules terrain.render=false suppresses legacy terrain/water and stale trace heightfield. New MapDoc.reset writes terrainPresentation=entities; old loaded maps missing key retain legacy behavior. GameView per-map enable flag and editor sync honor the document mode. Root HTML5 Studio is receiving the corresponding flag/empty-grid suppression separately. User explicitly confirmed green rectangular pieces were the unwanted standard plane and now requires no automatic terrain on blank maps, plus individually selectable entity/component terrain, rocks, trees and grass. Current packed single-valley entity must therefore be replaced, not relabeled as finished.

NEREID shader adds undistorted depth shoreline coverage and lower broken foam (GLSL/MSL), avoiding refraction moving the physical shoreline. Offline PBR baker darkens/roughness-adjusts wet sand by the same water plane. Executed: actual GLSL330 and GLES300 compile/link via glslangValidator PASS; native editor/renderbench build PASS for renderer/rules flag and shader. Later new-map default/per-map flag source additions need fresh build/test. Metal capture /private/tmp/nereid-final-shore.png has0pipelineFailures; visually white rim reduced but water looks excessively transparent, so final water optics acceptance is NOT claimed. No-optics debug /private/tmp/water-nooptics.ppm pending to isolate opacity vs contact. Wet atlas48tiles currently rebaking.

Next bounded stage: new blank-map tests and canonical authored entity components with stable IDs, native model transforms and shared selection/Inspector/Undo; replace packed external scene with individual native assets. Preserve old game compatibility and do not claim full authoritative RTS/ECS migration or physics ready just because editor entities exist. Main untouched; final visual capture and any corrective depth work pending.


## 2026-11-20 explicit authored environment entities

Implementation db2263ba1887a190415d5e0afa58cb14b5cc9f62 on feature/aiv87_terrain_pbr, publication feature/terraweave. Canonical MapDoc entities now support opt-in Transform Y/yaw/uniform scale plus StaticModel/provenance components; X/Z remain canonical pos, with existing LocalTransform affine presentation math. Shared transform adapter reads/edits actual authored Y and atomically validates Transform patches. Legacy planar objects still reject Y. Native editor model parentFrame consumes the authored transform. Duplicate retains components with a fresh document ID and fresh NEREID numeric surface ID. See docs/terrain/entity-authoring.md for schema/limits.

NEREID entity-owned local surface data compiles read-only to the existing native host; body heights/centre, river/polygon points, grade origin and vortices translate with the object. Rotation/non-unit scale explicitly unsupported. Flow edits write only the canonical component with MapDoc Undo; legacy top-level manifests continue to parse. No new native ECS registry/save/checksum contract; this is authored document identity, NOT completed gameplay ECS/physics migration. Scene/game screenshot requests now fail closed if viewport unavailable and do not create a notification over their own image.

External AlpineValley active map replaced packed valley entity with1391 stable IDs:48 TerraTerrain tiles,967 TerraTree,221 TerraRock,154 TerraGrassPatch and1 NereidWater. Shared assets under content/models, generated legacy rules bindings match canonical StaticModel refs. Generated assets~992MiB remain local/reproducible; native/install-entities.py and recipe/notes persist the conversion. Grass patches are individual document entities, NOT individual tuft/blade identities. User wants all environment objects as entities; per-tuft authoring remains a known unfinished part rather than an implied ECS conversion.

Executed: Release arm64 aiverse_editor and aiverse_editor_scene_adapter_tests builds PASS; final targeted CTest editor_scene_adapter PASS0.33s. Coverage includes new blank-map entity mode, authored XYZ/Y/scale/yaw, Undo/Redo, invalid atomic edit, duplicate identity, translated water and component-owned flow Undo. Initial build target AiverseEditor failed (wrong target name); corrected aiverse_editor. Two temporary Python editing scripts had indentation errors before any execution; corrected and reran. No current build failure. Actual final Metal editor via local API rendered1390 Terra model nodes plus native water; LOD chains1342, blocked0, shadowLimited0, camera levels48/834/508 at initial camera (counts are nodes, not draw calls/FPS). Clean scene capture /private/tmp/terra-entity-clean.png inspected: green rectangular standard ground absent. Actual map_new32x32 capture /private/tmp/terra-empty-entity-map.png inspected: empty dark viewport, no floor. Returned test editor to maps/valley.json afterward. Screenshot-only API never captures desktop; full-editor fallback prohibited for scene/game requests.

Studio HTML5 counterpart code f1aaffadab5b4212a3688955aa367d4622132248 on feature/studio-html5: stable entity:@ID, real model component transform/asset, Inspector Y/yaw/scale, masked/LOD rigid render support, no automatic plane/grid. Native app/tests built PASS; CTest editor_scene_adapter + studio_terrain_geometry + studio_session3/3 PASS6.19s. Its actual UI visual QA remains NOT VERIFIED; prior CUA session failed and desktop screenshot was auto-review rejected, no bypass. Root unrelated AGENTS/npm changes preserved.

CI/full Terra/NEREID regression/Vulkan/mobile/shadow pixel parity/runtime collision-buoyancy NOT RUN. New metadata defaults preserve old loaded-map rendering; no production MDI/ICB flags enabled. Current forest/mountain/water optics are still not accepted as final beauty art; river looks too transparent,10species/age/winter and per-tuft editing remain. Main untouched. Next: finish per-tuft entity/presentation strategy without unbounded meshes/DOM, precise masked/LOD/water picking and runtime ECS ownership; separately repair water optical visibility and continue final tree/mountain art before merge/gallery publication.


## 2026-11-21 graded underwater host integration

Code 1ba820ca4b34e9871439e6fb3e97797b7dc589d1, feature/aiv87_terrain_pbr. NereidUnderwaterView now resolves graded local surface/bottom before camera volume admission/priority/depth. New small NereidUnderwaterPrisma adapter maps snapshot to existing PrismaUnderwater; old WorldScene bridge delegates. Shared NereidManifestHost updates PRISMA after prepare in native editor Scene/Game, HTML5 Studio and renderbench. Studio always passes its HDR/PRISMA pipeline even with GI/direct lighting disabled; underwater no longer depends on lighting opt-in. No new water solver, authoritative simulation change or postprocess copy. Editor still respects its Scene postprocess toggle. Temporal underwater snapshot is currently one shared Prisma state; multi-camera smoothing isolation remains a limitation. Optical ray clipping uses local horizontal plane approximation of inclined water, not a full graded-plane ray equation.

Executed KINGSTON: ecs_nereid_renderer_contract_test build PASS and CTest1/1 PASS0.44s, includes graded above/below regression. Full native aiverse_editor build PASS; command then stopped on wrong target aiverse_studio_html5 (actual target aiverse_studio). Corrected renderbench+Studio build RUNNING underwater-host-build.log, not yet a PASS. No GPU underwater scene capture yet. Prepared above/crossing/submerged reference configs on KINGSTON. User video /Users/aleksandrfeer/Projects/video.mp4 decoded via AVFoundation18.033s,12frames; ffprobe unavailable, Swift extraction succeeds with deprecation warnings. Reference shows moving waterline and lingering lens drops after exit. User explicitly assigns screen effects to PRISMA and requires lens film to be separate DISABLABLE/CONFIGURABLE effect, independent of underwater absorption. Next stage add opt-in PRISMA lensWater catalog effect (amount/drop size/density/refraction/drain/dry time), per-view wetness, common GLSL/MSL compositor and real frames. Do not describe current host fix as full video parity. Water tiling/current/foam,1K/4m high-detail terrain, multiple sand variants/noise underlay and final tree/grass art remain pending. Main untouched.


## 2026-11-21 configurable PRISMA lensWater

Code 14ac558b7e1c4bdde5749d5e654aeedf8b19b046, feature/aiv87_terrain_pbr. Optional new PrismaType appended beforeCount; old numeric effect IDs preserved. Independent lensWater catalog entry, default absent/OFF, enabled/amount disable; amount/dropSize/density/distortion/flowSpeed/dryTime/film parameters share parser/serialization/volume/catalog UI. Native setUnderwater uses bounded64 per-view wetness histories; never-wet camera stays dry, submerged camera wets and exit starts complete bounded drying. retireView/shutdown clear lens ownership; disabled/zero/tier-off clears on next input update. Underwater absorption is separate and still works with lens effect absent/disabled. Screen compositor uses bounded9-neighbour jittered variable-size drops plus draining film and two source samples; no new image/target/pass or water/ECS solver. Internal composite U0 adds32bytes, GLSL/MSL field/feature count now array-derived. Output-pixel radius/distortion/speed preserve scale under input reconstruction. Renderbench preview uses real elapsed preview time for wetness, deterministic benchmark uses1/60; captured reports record actual effect execution and camera immersion after timing interval.

Actually executed on KINGSTON: Release lens/volume/NEREID contracts + renderbench build PASS; CTest prisma_lens_water + prisma_volume + ecs_nereid_renderer_contract_test3/3 PASS0.74s. Tests cover dry/no input, immersion/exit/dry interval, nonfinite delta, independent disabled parsing/roundtrip and grade. Final shader/bench source builds PASS lens-flow-build.log. Real Metal captures /Volumes/KINGSTON/AIVERSE-builds/terraweave-unified/underwater-reference: lens-on capturedPrismaEffects lensWater requested/encodedTRUE, lens-off requested/encodedFALSE reason effect disabled in camera profile, cameraUnderwaterFALSE and underwaterBlend0 in both; submerged without lensWater cameraUnderwaterTRUE, underwaterBlend0.999. Each completeTRUE/error empty, pipelineFailures0. Captures are diagnostic finite-terrain views with black horizon, not final forest/water beauty/reference acceptance.

Meaningful corrections: first implementation left hard-coded16feature/23uniform counts, so new pipeline key compiled without lens code. Actual pixel inspection found this; replaced with array-derived bounds, reran real GPU. First regular grid droplets looked artificial; new jitter/size+neighbour version inspected and more irregular, still screen approximation not video parity. Initial reference capture failed because glTF buffer URIs were absolute and importer prefixes directory; changed diagnostic relative paths using read-only ground symlink. System Python3.8 lacked is_relative_to; fixed diagnostic path script. ffprobe unavailable; supplied18.033s video read through AVFoundation12frames (Swift deprecation warnings only).

NOT RUN: latest full editor/Studio rebuild after adding LensWater enum, final editor UI toggle interaction, actual GL/Vulkan/GLES pixels, full regression/CI/performance. Previous underwater-only editor/Studio builds PASS; do not equate them with latest lens application build. PRISMA skill and docs/prisma-lens-water.md updated. No main merge or remote push this stage; full active-scene/tree/material acceptance remains pending. Requested1K/4m detail mapping, multiple sand PBR variants and grayscale noise-underlay material are not implemented yet. User now requests editor: silhouette-only selection contour, visual triangle/alpha picking instead of radius, no generic HP bars (health overlay only opting-in gameplay modules). Next bounded stage inspect/fix shared editor/HTML5 picking/outline and remove generic Scene HP HUD, preserve game module behavior and all dirty user files.


## 2026-11-21 finite NEREID shore masks restore native meadow cover

Code 23dde575c0ffd9530bf7d03c480989c79213b537, feature/aiv87_terrain_pbr. Added optional fixed-point waterFootprintDistance for finite box/disc/polygon/river bounds and Terra shoreCover authoring helper. River distance is a projected centreline/width approximation, not exact tapered SDF; existing water membership/contact and authoritative state unchanged. Ground v2 shares graded sand/wet masks between native PBR baker and grass exporter. Native mask replaces the erroneous additional infinite water-plane exclusion; v1 compatibility retained. Recipe exposes bank width, sand limit and grass maximum elevation. No live erosion/moisture/fall integration or save/wire change.

Executed on KINGSTON: geometry/landscape tests and both exporters built PASS (native-shore-mask-build.log); CTest ecs_nereid_geometry + terraweave_landscape 2/2 PASS 0.91s. terrain-shore-full-v1 export and four 1024/detail baker parts PASS; detail materials build PASS. Heights and waterHeight unchanged, all48 terrain.bin buffers byte-identical. Eligible ground vertices75954->107323. flora-shore-v1 exported40 prototypes/194 patches versus154. CPU audit1326768 High-LOD blade roots: terrain-height error<1.1cm, interpolated sand<=50.4875 for threshold50 with integer tolerance. Far-river tile sand channel mean43.2019->0. Python compile PASS after cache write rerun with external-drive permission; first sandbox cache write denied. diff --check PASS (CRLF warnings).

Review-project-v5 preserves1391 old entity IDs/transforms and adds40 grass entities;1431 total/mapdirtyfalse. Original dirty AlpineValley and user47324 unchanged. Owned editor47326 session60968. Actual Metal same-camera scene-only /private/tmp/grass-shore-before.png and grass-shore-after.png inspected: lowland grass restored, sand bank remains bare. Camera6366348 triangles, shadows554980,1433 draw calls, LOD492/218/40,729 chains,0 blocked. These are scene counters, not GPU speedup. Native grass still uniform green ribbons; water tiling/foam/flow, organic branch junctions, mountains, snow and full physics/reference/picking/site goals unfinished. No final-beauty claim, push, main merge or CI. Mouse Y/Undo NOT RUN: Mac locked. Next bounded stage improve shared native grass pigment/shape at unchanged bounded population, verify real close/mid/far and preserve tuft-root LOD correspondence.


## 2026-11-21 independent NEREID optics and transported irregular whitewater

Code ac2d3cdff66736ac884b778a7f18cf7e4529a407, feature/aiv87_terrain_pbr. Shared Renderer3D now captures opaque colour/depth for tagged NEREID optical coefficients/refraction regardless of traced-shadow quality. Existing trace2.w=.25 means optics-only;1/2 retain water/model SSR. Ray marching and opaque reflection replay remain trace-gated; legacy water/sprite helper overload preserves old behavior. No GPU uniform layout, ECS water velocity/save/wire schema change. Renderer3D appends explicit setPresentationTime (finite seconds, negative resumes steady clock), renderbench --freeze-time now fixes shader time too; C++ object layout requires rebuild. Current still comes from the existing authored/manual/grade-spline Fixed owner. Native wave/noise/whitewater detail uses worldXZ-flow*time; independently moving six-direction phases no longer override a moving current. Planar and SSR normal distortion uses the NEREID resolved normal, not cloud-offset sines; native planar Fresnel dielectric .02, legacy helper retains its old stylization.

Replaced rapid whitewater sine-product ellipses with transported, stretched domain-warped noise (1/2/3 layers Low/Medium/High). Undistorted contact-depth foam composites after refraction; vertical band no longer changes shore-coverage fade width. Screen-depth obstacle tail uses bounded4/8/16 upstream probes by rapidDetail, overlapping average and transverse taper/downstream fade. Zero shoreFoamDepth disables contact/tail, rapidFoamStrength remains independent. This is screen-space presentation, NOT hydrodynamics/erosion; offscreen/occluded obstacles absent, steep silhouettes/large-world/performance still need QA. Editor geometry_stats appends read-only cameraMeshTriangles/opaqueSceneCapture/ssrRaymarch/nereidConfiguredSurfaces; bench separates capture from SSR. Existing counters retained.

KINGSTON actual commands: final renderbench+nativeEditor+Studio+geometry contracts build PASS nereid-final-optics-build.log; CTest river_grade/surface_mesh/renderer_contract3/3 PASS0.58s. Follow-up nativeEditor diagnostic-only build PASS nereid-editor-capture-diagnostics-build.log. Earlier native-reflection-editors build also PASS but shader source changed during its later Studio dependency pass; explicit final rebuild of ALL targets above is the authoritative result. Full common Lit GLSL + real ColorSpace prefix compiles ES300/GL330 2/2 PASS with glslangValidator; actual GLES/Vulkan pixels NOT RUN. No source/temp/compiler products on internal build dirs.

Actual Metal fixture water-contact-v1 uses existing Terra terrain--1--1 and a native Terra rock. Final final0/final1/reverse/foamoff/low/manual-minus/full/no-water/repeat0 reports ALL completeTRUE/error empty and pipelineFailures0. hdr+Low capturesTRUE/SSRfalse, full captureTRUE/SSRtrue, no-water captureFALSE/SSRfalse. fixed time0 repeat PPM bytes exactly identical; time0->1 mean RGBdelta0.81607 (whole frame, NOT a flow-direction metric; sky clock also changes). Opposite manual1.8m/s vectors produce tail on opposite sides, PNGs inspected. Nine reports retained on KINGSTON/water-contact-v1; /private/tmp/water-final0.png, water-reverse.png, water-full.png, water-graph.png. Grid of white ellipses reproduced strongly with reverse1.8 before the rapid-noise fix, absent in final render. First4probe prototype made detached ghost-rock foam; next8max version made comb tips; rejected, replaced with overlapping4/8/16 average and taper. Source remains procedural with no new texture allocations.

Important NOT FINAL: full native forest still looks too bottom-dominant/brown with weak contact foam, despite actual water geometry2048triangles, captureTRUE/SSRtrue/configured1. ShaderGraph terrain bench reproduces stronger bottom appearance: graph.json/report completeTRUE, pipelineFailures0, shows correct scene-colour/detail involvement. This is a remaining optical/art/composition investigation, not missing surface or disabled capture. Need isolate actual depth/thickness and soil-detail colour before claiming reference-quality water. Review-project-v7 retains1431entities/all poses; only NEREID style .16m shore depth, absorption2, tint1, refraction.24 and existing autoLOD55/170m hysteresis8 tuned. Original user app47324, baseline47325 and dirty AlpineValley files untouched. Owned47326 session12108, screenshots forest-water-native-v7[-hdr|-diag].png. Transform Y+2.5 test through canonical entity:@water-1 succeeded; Undo restored position but MapDoc dirty flag stayedTRUE by its existing contract. Restarted from unchanged disk afterward; no map_save. Invalid initial unprefixed scene entity/component calls rejected with no mutation.

Corrected verification errors: initial GLSL dump omitted mandatory RHI ColorSpace prefix (missing rtsEncodeScene), fixed and reran; initial fixture used file instead of path and unplaced local tile, reportfailed despite native exit0; corrected path and original -64/-64 glTF root placement, all subsequent reports asserted. Existing string contracts expected refracted-depth foam; first CTest failed, updated existing contracts for undistorted foam and explicit non-npos post-SSR order, reran PASS. First reflection wrapper generation malformed before compilation, regenerated from unchanged HEAD source and GLSL verified. One non-escalated ctest -N failed external log write; reran with external-volume permission, discovered absent surface_mesh executable, built it explicitly. No failures counted as passes.

No CI/performance/GPU time or mobile parity claim. No main merge/push/final gallery publication. Mouse Y/Undo interaction still NOT RUN because Mac locked (API Transform/Undo above is not mouse parity). All original tree junction/LOD/art, snow/ground/sand/mountains, live erosion-root/fall host, PRISMA video parity, universal deformed/water picking and final scene/site goals remain active. Next bounded stage: isolate NEREID optical thickness in terrain-graph/full-editor path; source geometry has1.58m actual water-minus-bed at (-12,-32), so do not hide the issue by raising the whole river. Then shared parent-centreline branch attachment/junction anatomy, preserving foliage/LOD ownership.


## 2026-11-21 remove double riverbed compositing; inspect real optical inputs

Code f9d094e336afb9f93c02d733b18da98fc06e1de5, feature/aiv87_terrain_pbr. Resolved NEREID absorption/refraction already includes background colour; old85%-capped mix followed by ordinary opacity added unattenuated riverbed again. Now one absorbed-background/Fresnel surface composite, authored opacity scales optical density/refraction/Fresnel internally and final framebuffer alpha is contact coverage when optics resolved. Invalid depth/legacy untagged water retains old alpha fallback. Contact coverage fades only over3.5cm, independent of foam band, and foam strength scales with authored opacity. Existing style range .001.. .98 unchanged; this intentionally changes NEREID appearance, not save format/Fixed current/body or GPU uniform layout. LDR colour-space approximation/TAA/underwater/backfaces still need broader parity; do not call this full spectral/physical water BRDF.

Explicit read-only AIVERSE_NEREID_DIAGNOSTIC env selects depth,coefficients,basecolor,transmission,coverage shader variants in a separate owned process. Default0 compiler guards preserve ordinary GLSL/MSL; invalid value leaves normal rendering. Red depth/4m, negative green/unavailable blue; other modes inspect actual inputs/opacity after display postfx. No scene mutation, frontend UI/bridge or public struct ABI addition. Diagnostic images are NEVER final screenshots.

Investigation actual Metal water-contact-v1: depth-scene-v2 and depth-graph both centreRGB205,0,0, matching1.58m known ground snapshot; coefficient graph211,131,239; basecolor0,50,58; transmission46gray; coverage231gray. All7 inspection reports completeTRUE/error empty/pipelineFailures0. This excluded missing depth/parameters/source albedo and located excessive background mixing rather than moving river height. Final optical-new/minimum/no-water reports3/3 completeTRUE/error empty/pipelineFailures0, source terrain ShaderGraph13samples/4textures actually enabled. Minimum opacity.001 versus no water: final meanRGBdelta0.0631963, P99=1,max=1; ordinary.88 vs absent has a substantial colour difference (previous iteration47.5261mean), no fake screenshots. First test tried opacity0; existing manifest rejected it as expected, corrected to documented minimum. No widening of valid save/style input.

KINGSTON builds: nereid-optical-alpha-final-build.log all nativeEditor/Studio/renderbench/contracts PASS; contact-final-editors buildPASS with narrowercoverage, CTest river_grade/surface_mesh/renderer_contract3/3PASS0.34s. Last guard-only source build all four targets PASS nereid-default-diagnostic-guard-build.log; renderer_contract1/1PASS0.20s. Actual12 GLSL ES300/GL330 mode0..5 variants PASS before contact constant change; final RAW/default ES300/GL330 PASS with final guards. Raw ES initially FAILED because undefined macro in #if is illegal in GLES (the matrix had explicitly defined0); added #ifndef default0 to BOTH shader bodies, reran raw compile and all nativehosts. Commit attempt stopped on failure before Git mutation. No default-WebGL failure left. Other GPU pixels/CI/performance NOTRUN. Updating a header during an earlier build meant explicit final rebuilds, not relying on partial source snapshots.

Actual owned nativeEditor47326/session52132 review-project-v7: forest-water-optical-alpha.png and final forest-water-contact-final.png show water now blue/green and coast foam line visible with same terrain/poses; inspected Scene-only903x605. Final screenshot prior to guard-only recompilation, which changes default macro preprocessing only. MapdirtyFALSE1431entities. Same camera921748modeltris +2048water/meshtris,372drawcalls, captureTRUE, SSRtrue, configuredWater1; no FPS claim. Original user47324/baseline47325/dirtyAlpineValley untouched. /private/tmp/water-optical-contact-final.png actualMetal standalone diagnostic scene (black finite-ground horizon), NOT final beauty hero. Full forest still has synthetic foliage, repeated sand ripples and flat mountain/composition gaps. No main merge/push/gallery publication yet.

Next bounded work returns to native Arbor organic branch anatomy: current conifer mesh sags branches but source children start on STRAIGHT parent skeleton; the parent mesh/child/foliage must use one shared presentation centreline/attachment offset. Broadleaf twigs are straight1ring; add bounded opt-in curved scaffold/collars while keeping thin-twig budgets and exact leaf ownership/LOD support. Do not bend only woody mesh and leave leaves/children floating, and do not claim intersecting normal-blended collars are actual welded geometry. Shared PBR material currently has no true thin-leaf subsurface scattering (shadowDiffusion is shadow radius only); don't mistake it for SSS. All broad tree/season/ground/mountain/live-erosion/picking/video/site/main requirements remain active. Maclocked mouseY/Undo stillunverified. No new question/approval needed for existing authorized independent work.

## 2026-11-21 camera-independent local current grid — CPU stage only

Code87ea8c294, feature/aiv87_terrain_pbr. Newengine/world/NereidFlowMap.h snapshots EXISTING gradedWaterBody +waterCurrentWithVortices atlocalgraded surfaceY-.1m. NereidFlowMapSource borrowscanonicalbody/geometry/grade/current/vortexcomponents onlyduringbuild; outputownsfloattexels, noWorld/GPUhandles/RNG/save state. Finiteboundsindependentofcamera/meshLOD, default2mworldlattice, max256axis/16384texels, power-of-two spacing growthunderbudget. Infinitewaterrequiresboundedexplicitpatch; overlappingequal-scale patches sampleidenticalworldcentres. RGworldXZvelocitym/s,Blocalrapidintensity,Avalid. CPUclamp+bilinearsampler, rebasedrecthelper, invalidnonfinite/budget/boundsreject ordisabledsourceempty. Headeruses Fixed::fromRaw roundedcanonicalpositions (no nonexistentfromDouble); floatneverfeedsphysics. No canonicalwatermembershipreplacement.

KINGSTONflow-map-configure.log CMakePASS; flow-map-build/final-build/guards-build PASSnewtargetaiverse_nereid_flow_map_tests. Final CTestnereid_flow_map1/1PASS.53s. Testsmanualconstantflow/rapidintensity/clamp/nonfiniteinput, gradeprojectionfollows31degreebend(nearp0X0,p1XnegativewithXZratio.6), manualoverrideunchanged, additiveexistingcurrentnodes/vortexincludingcanonicaltexelcompare, copiedsnapshotunchangedaftersourcemutation, overlappinginfinitepatchsamplingidentical, boundedenlargedgrid, invalidbudget/NaN/disable. NoGPU/editorintegrationclaimed. CMakeonlytesttarget, nopackagedclientdependency/moduledefaultchange. Architecture explicitlystatesCPUreadinessandmissingrendererhook.

NEXT REQUIRED GPUstage: currentNereidWaterScene drawstillonehydraulicsvectorpertile/observer-dependentuntiledfallback; userbugNOTyetfixedinvisiblewater. UploadthisCPUfield asboundedlinearRGBA16F (reusehdr_detail::toHalf), texslot6freeinlitMeshshader(existing0shadow1lights2reflection3height4opaque5depth); increaseShaderDesc.textureSlots6->7andbindwhite6fornonwater/reflection. AddwaterFlowRectvec4 toDrawUniforms+DrawUniformsR andALLmatchingGLSL/MSLU0blocks; appendborrowedflowTexture/rect toCameraGeometryCommand+DrawItemandpreservecapture/replayinCameraModelSnapshot. SampleflowatfragmentWORLDXZ, useitforbasewaveadvection/rapidfoam/obstacletails. Keepmanual/grade controlscanonical, legacyuntaggedwaterunchanged. Avoidunboundedworld-flow*time shearingforvariableflow: boundeddual-phaseflowmapadvectionwithsmoothresetmayberequired. Fixed renderer.setPresentationTime /bench.freeze-time alreadyexists, doNOTinventanotherclock.

Ownership/gates: flowmapversionseparatefromstaticVB/IB(meshrevisiondoesNOTchangeoncurrentfield/styleonlychanges). Finitefieldmustnotrebakeoncamera/LOD. Preserveconfluence transferFlow throughprepare-timecanonicalresolver; currentdraw callbackisnotavailableatGPUprepare unlessnativehostpassesitthere. NoGPUallocationsinsideactiveencoder/drawcallback. Immutabletextureversion+CPUcaptureleaseandGpuResourceRetirementfencesneeded; capturedsecondarycameras mustpinoldfielduntilCPUreplaydoneandlastGPUfencecompleted. No borrowedowner destruction afterDevice lifetime; explicitshutdown/quarantineifcapturesremain. Bounduploads/retainedtexturebytes andreportdeferredratherthanunboundednewtextures. DoNOTstealripple/wakearrayentries, overloadnormalattributes, addglobalMeshVertexoverhead, orincreasewaterdrawspersegmentjusttofakebends.

GPUacceptance: nativeflowdebugcolouratSAMEworldpoints projectedintotwocamerasandacrossLOD; actualcurvedriverfourframes(time0/1,cameraA/B), manualreverse, foamoff, vortex/grade,movingcamera, Metal+GLshaderpixels completeTRUE/pipelineFailures0. UserlatestrockrequirementstillOPEN: supportlostunderrockmustsettle/tilt/fallviaactualPHYSIS/Box3DhostandupdatedTerra colliders, notinitialscatteronly/teleportedvisualY. Broadtree/LOD/liveecology/PRISMA/deformedpicking/GI/STRATA/VIVID/site/mainremainactive. CPUperffixc8c0c108c+d71cbc24falreadyPUSHEDfeature/terraweave, ABACPU~17.1->5.155->17.1SceneMs,440LENSchecksPASS; noFPS/GPUclaim. CurrentownQA47326STOPPED(freesGPUforuser), QAartifactbinaryAfromrepeat, nativebuiltbinaryBhasCPUfix/GPUread-onlyfields. UnlockedMacconfirmedbutmouseYUndoNOTRUNduetouserperformance/flowsteering. NoCI/mainmerge/finalacceptance.


## 2026-11-21 local river flow GPU integration and native bend validation

Code ff84b6648bd3825eea62471614611fced9f196fc; GPU-test follow-up
6402e0bbd28ceffa575a86ee283d4c1841322920, branch feature/aiv87_terrain_pbr.
This completes the renderer connection missing from CPU stage87ea8c294.
NereidWaterScene prepares a bounded linear signed RGBA16F world-current texture;
Renderer3D lit meshes sample texture6 at fragment world X/Z. U0 and reflection U0
append waterFlowRect (672B); Model/PBR U2 stays unchanged. Waves, rapid foam,
contact-tail orientation and contact-foam advection share the local field.
Four-second overlapping transport phases avoid unbounded current*time shear.
Untiled fallback current uses a stable source reference rather than the camera.
Finite fields ignore observer/selected LOD; source current/vortex edits invalidate
flow without remeshing. Manual/automatic grade/current/vortex ownership and all
Fixed/save/checksum schemas remain unchanged. Rebuild native callers for appended
renderer/capture ABI. No new draw per river segment, MeshVertex field or solver.

NereidFlowTextureCache bounds active+pending logical GPU payloads to32MiB and256
versions; exact signed-half payload and bounds/dimensions equality deduplicates
uploads. CPU capture leases pin replaced textures, followed by real GPU-fence
retirement. Shutdown revokes replay then drains after proven GPU idle, otherwise
quarantines handles. CameraModelSnapshot checks revocation before mesh dereference.
CPU half payload retention is additional memory, not included in GPU payload stat.
WorldScene supplies existing confluence/cascade/obstruction resolution only when
those sources exist. Nonzero resolver revision is an explicit immutable-input
promise; unversioned resolver refreshes bounded grids and deduplicates GPU bytes.
Many dynamic bodies may refresh over multiple frames; host CPU cost/latency still
requires measurement. Under budget failure, draw retains valid old field or uses
stable uniform fallback; flowPending reports deferral. No atomic mesh+field GPU
publication or new hydraulic simulation is claimed.

Native investigation found origin-amplified chevrons in rapid foam: rotating
absolute world position by a varying current tangent shifts noise phase by the
large world coordinate. Replaced with world-anchored noise and bounded directional
local averaging. Actual native automatic/zero-current A/B isolated the artifact;
zero-current temporary edit was Undo-restored, no save. Final fresh QA launch has
original disk state and the automatic inclined river. Do not rotate absolute
world X/Z into a per-fragment tangent frame again.

ACTUALLY EXECUTED on KINGSTON root terraweave-unified:
- flow-world-foam-build.log: aiverse_editor, aiverse_studio, aiverse_renderbench,
  aiverse_renderbench_gl, aiverse_ecs_nereid_renderer_contract_test PASS. Earlier
  flow-final-editors-build/final-relink also PASS; final source is world-foam build.
- ctest -R '^(nereid_flow_map|ecs_nereid_renderer_contract_test|ecs_nereid_river_grade_test|ecs_nereid_surface_mesh_test)$'
  4/4 PASS .26s. Includes signed half(+2/-1), capture lease before GPU fence,
  shutdown revocation,256-version cap, resolver preserve/dedupe, no finite camera/
  LOD rebuild, manual current edit with unchanged mesh revision.
- python3 tests/render/verify_nereid_flow.py <native/aiverse_renderbench> <flow-foam-final-metal>
  and <native/aiverse_renderbench_gl> <flow-foam-final-gl>:11actual GPU captures
  EACH PASS. World coordinates offset(256,128), two cameras x two times diagnostic
  and shaded frames, farLOD, manual reverse, foam-off. All reports completeTRUE,
  error empty; Metal pipelineFailures0, GL no API errors. Identical world points
  across cameras/times and near/far: RGB[128,103,50] straight,[116,109,43] turned;
  manual(-2,0,+1) RGB[77,153,114] both. Active whitewater and foam-off differ in real
  shaded pixels; animation changes while velocity diagnostics remain fixed.
  Earlier flow-world-final-* used rapid foam defaultOFF, so final enabled-foam
  rerun above is required evidence. Bench revision metadata predates commits:
  binaries were built from the working source now committed as ff84b6648.
- check-nereid-final-glsl.py PASS14linked GLSL330/GLES300 mode0..6 variants.
  Compilation/generated shader/temp/module artifacts all on KINGSTON.
- aiverse_tests buildPASS flow-lens-build.log; native/aiverse_tests lens_ PASS
  440checks0failed flow-lens-tests.log. No retained-camera Metal GPU test, Vulkan,
  WebGL device, mobile acceptance, full suite or CI executed. GLES compile is not
  WebGL pixel validation. No FPS/GPU speedup claim.

Failed/retried commands: initial cache used stats() without Device ->compile
failure, fixed exact API. Extended contract test initially lacked aiverse_models
MeshBuilder link ->fixed CMake test link. New resolver test assumed negativeY,
failed for default elevated body ->compares local surfaceY instead, rerunPASS.
First GL capture started before executable existed ->FileNotFound; rebuilt GL,
all final runs abovePASS. Initial clean native screenshot omitted window:scene
and was rejected without capture ->corrected. No failed command counted asPASS.

Owned unique AiverseTerraQA.app on47326 opened review-project-v24; actual clean
1280x720 scene captures /private/tmp/forest-local-current-final.png and
forest-local-current-close.png (also KINGSTON/flow-native-final). Final native
water bends smoothly without old foam chevrons; these are water-fix evidence,
NOT final art/website hero acceptance. Native source/entities/assets preserved;
all final scene state readbacks are native-flow-final-* JSON. User editor47325
and original dirty project untouched. GPU is also used by that user editor,
so whole-frame GPU readings are not isolated benchmark results. Startup graph
compilation dominates first120-frame average; do not report it as steady runtime.

Next: publish code+this separate handoff to feature/terraweave with ordinary
non-force push. Main readiness remains unmet. Under-eroded rock settling/fall is
STILL OPEN: authored rocks are static, initial nine-point support sampling is
not live physics. Need actual shared ECS rigid support + PHYSIS/Box3D body host
and changed Terra colliders, not visual Y teleport. The independent cheap shader
binding rejection CPU hotspot is still unimplemented. All broader tree/season/
live ecology, PRISMA video, deformed picking, native Y mouse/Undo, GI/STRATA/VIVID,
final gallery/site/main work remains active; do not infer completion from water.

Final publication checkpoint: code+handoff ordinary push to origin/feature/terraweave succeeded at427c9a097 (remote wasd71cbc24f, no force). Final map_info dirtyFALSE/1803entities. Owned QA47326 STOPPED after captures to release GPU; user47325 untouched and still loaded with its older running binary. Native built editor/QA artifacts now contain the local-flow fix; restart is needed to load it in an already-running editor. Code worktree clean at publication. Main not merged; preceding open requirements remain.


## 2026-11-21 main publication explicitly requested

User «Заливай в мейн» supersedes the prior defer-main condition for publication. Fresh remote main973b32970654cc5318fa6b6571f35d020501dcdd was an ancestor of TerraWeave ab5d360f4cb2f2f1f2e99b78b0262f596c48f8d9; normal push HEAD:main succeeded and ls-remote confirmed that exact SHA. No merge conflicts/new code. Repeated4NEREIDCTestPASS +440LENSchecks0failed. CI/fullGPU rerun NOTRUN this unchanged-code promotion; previous actual GPU evidence remains above. Details and still-open work: docs/agent-handoffs/terraweave-main-promotion-2026-11-21.md. Original dirty checkout/user editor untouched. This separate docs commit records the completed publication and is also published to main/TerraWeave; no claim that erosion-driven rocks or all broader art goals are done.

## Layering move (2026-11-21, branch feature/aiv20261121_arch-layers)
NEREID core headers moved from `libraries/include/aiverselib/water/` to `engine/world/nereid/` (same file names,
namespaces, CMake targets `AIVERSE::lib_nereid_*` and catalog ids). No compatibility headers (owner rule): every
user includes `engine/world/nereid/X.h`; `<aiverselib/water/X.h>` no longer exists. Reason: `engine/world` bridges
and `engine/render` include NEREID, and the core depends only on `engine/sim`/`engine/core`
(docs/architecture-layers.md, L1).


## Turbidity (`style.turbidity`, `style.siltColor`)

One authored value for suspended silt, 0..1 (default 0 = legacy optics).
`lib::nereid::resolvedWaterOptics(style)` derives the effective optics and is
the single source for both views:

- surface shader (outside): extinction `absorption + 1.6*t` (capped 4/m; t=1 sees ~2 m),
  deep colour milked toward `siltColor` by `0.75*t`, depth tint toward 1,
  refraction strength halved at t=1 - the bed fades sooner and the body
  turns milky;
- `NereidUnderwaterView` (diving): the same extinction, tint and
  `underwaterScattering >= 0.85*t` - visibility shrinks, the haze takes the
  silt colour. `view.silt` reports the authored value.

`siltColor` default `[0.42,0.43,0.31]` (linear). Both fields are parsed for
surfaces, entity water (`NereidWater` component) and cascades, validated,
and part of cache/change keys. No uniform or layout change. Contract test:
`ecs_nereid_renderer_contract_test` (turbidity block).

```json
"style": {"deepColor":[0.03,0.2,0.26], "absorption":0.5, "depthTint":0.85,
          "turbidity":0.22, "siltColor":[0.36,0.42,0.33]}
```


## 2026-11-22 large-world ocean LOD + legacy RTS water migration (branch agent-nereid-next)

Ocean: the infinite patch is now centred on its snap cell (before: anchored at the cell
corner, so the sea edge reached the camera right before a cell switch); opt-in
`"oceanLod": "rings"` builds stitched, crack/T-junction-free nested LOD rings out to
`oceanPatchRadius` (13 248 tris for 6 km with defaults). CTest `ecs_nereid_ocean_lod_test`
PASS, renderer_contract/surface_mesh/flow_map/river_grade PASS. Metal renderbench
`tests/render/verify_nereid_ocean_lod.py` PASS (coverage, triangle bound, no rebuild pop);
numbers and limits in `docs/nereid/ocean-lod.md`. GPU times shared with other agents: no
speed claim.

Legacy water: `TerrainRenderer::legacyWaterLayout` + `RtsWaterNereid.h` converter +
`terrain.water.renderer` switch (default legacy) + `RtsView` NEREID host. CTest
`rts_water_nereid_test` PASS; native editor before/after captures for 12 templates, ten
switched to NEREID (tiles2d no-op, lastcolony unverified). `aiverse_client` builds. Web,
Studio HTML5, LENS secondary cameras, played-match captures NOT RUN. Details:
`docs/nereid/legacy-water-migration.md`.
