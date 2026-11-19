# PHYSIS Stage 4 — deterministic disasters: fire, flood and quake (2026-11-18)

**Branch:** `feature/physis-nature-simulation`  
**Implementation SHA:** `2ea0c8c23d2a6302011245d4fe58f0e49e854ea9`  
**Handoff commit:** `git log -1 -- docs/agent-handoffs/physis-disasters-2026-11-18.md`  
**Verification:** **NOT RUN**: GitHub connector writes only, no actual compiler, CTest, sanitizer, CI or live game execution.

## Delivered files
1. `engine/physis/PhysisDisasters.h`: optional `HazardCell` ECS component (fuel, fire intensity, shallow water depth, quake intensity); `DisasterConfig` per-tick coefficients; snapshotted `DisasterState` resource; generic `QuakeEvent` ECS component; APIs `initializeDisasters`, `ignite`, `hazardAt`, `advanceDisasters`, `advanceEnvironmentWithDisasters`.
2. `tests/physis_disasters_test.cpp`: wildfire spread, fuel loss, repeatable seeded calculations, rollback, dry-land water transport conservation, quake attenuation/duration and double-tick guard.
3. `CMakeLists.txt`: `aiverse_physis_disasters_test`, CTest `physis_disasters`, label PHYSIS.

## Native API
```cpp
#include "engine/physis/PhysisDisasters.h"
aiverse::World world;
aiverse::physis::initialize(world);
aiverse::physis::initializeDisasters(world); // opt in once
aiverse::physis::ignite(world, 6, 9, aiverse::Fixed::fromRatio(4, 5));
auto quake = world.create();
aiverse::physis::QuakeEvent event;
event.centerX = 6;
event.centerZ = 9;
event.startTick = 1;
world.add<aiverse::physis::QuakeEvent>(quake, event);
aiverse::physis::advanceEnvironmentWithDisasters(world); // one authoritative tick
aiverse::physis::HazardCell hazards;
if (aiverse::physis::hazardAt(world, 6, 9, hazards)) {
    // Project-specific systems can read hazards.fire / waterDepth / quake.
}
```

## Architecture and determinism
- Disaster module is **optional** and genre-independent. It imports only PHYSIS Environment and common ECS/Fixed; zero FPS/RTS/genre/renderer headers.
- The disaster step requires `State::tick == DisasterState::lastProcessedTick + 1`; it must follow **one** atmosphere/surface tick. The convenience `advanceEnvironmentWithDisasters` performs that order. Do not also invoke baseline advance or disaster pass in the same tick.
- Fire uses previous-tick neighbor fire, seeded perturbation, wind bias, fuel consumption and rain/wetness suppression. All cells first read old immutable ECS state into a temporary buffer, then compute, then commit. No scan-order-dependent propagation.
- Shallow flooding routes capped per-source outflow among four orthogonal neighbors according to differences in elevation+depth; raw integer deltas transfer identical negative/positive amounts for mass conservation absent rain, evaporation and final depth clamping. No full CFD solver.
- Quake is timed, seeded only by its event parameters, and currently an intensity field; structural damage, physics impulses, camera shake and VFX are **consumers** not hard dependencies.
- Disaster state is in World resource, hazard state in ECS components, so World cloning/COW snapshots apply. `Simulation::serialize` full engine persistence integration is **not** automatic.

## Verification required
```sh
cmake -S . -B build/physis-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/physis-test --target aiverse_physis_core_test aiverse_physis_climate_test aiverse_physis_environment_test aiverse_physis_disasters_test -j4
ctest --test-dir build/physis-test --output-on-failure -R '^physis_'
```
Run release/debug/Clang/GCC/MSVC and sanitizers before integration, record exact results. The code/test files are committed, **no tests have been executed** by this agent.

## Known limitations / review risks
- This is a simplified deterministic **fire/flood/quake** model, not all disasters from the roadmap. No tornado, hurricane solver, volcanic eruption, landslides, avalanches or reactive geometry in this stage.
- Static full-grid iteration, all hazard cells allocated eagerly; O(cells x quake events) quake pass and O(cells) temporary buffers. Large-world streaming, sparse active hazards, worker jobs and simulation LOD are not implemented.
- Rain and melt flows are simplified; surface ice/snow mass and flooded waterways are not physically conserved together. At water depth clamp to 100 m, conservation can be lost by design.
- No game-specific propagation of hazard damage, foliage burn meshes, renderer particle events, water mesh updates or navmesh changes yet. Add opt-in adapters only; do not introduce genre dependencies inside PHYSIS.
- Fire neighbor-spread uses a simple 4-neighbor grid, with x/z wind components; no ignition heat-transfer or combustion chemistry. Numeric overflow/edge-case stress tests still needed.
- No editor inspection, multiplayer replication or save-file format integration. PR currently draft and unvalidated.

## Concrete next stage — Stage 5 integration
Build **read-only presentation/interaction adapters** for Terra/Foliage, Water, VFX, Physics, Audio and a PHYSIS editor/debug inspector without importing genre modules into PHYSIS. Favor one-directional data-contract interfaces; consumers may opt in, PHYSIS must compile and run headless. Add CTest/headless integration tests for adapter isolation and compile-time API consumption, then a Stage 5 implementation commit and separate handoff. Stage 6: spatial indexing, streaming/Simulation LOD, deterministic job partitions, persistence/networking.
