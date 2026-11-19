# PHYSIS Stage 1 — authoritative ECS nature core (2026-11-18)

## Scope and status
- **Implementation commit:** `0ddc149271048a98a08e6b3149c447ae0ebffbd5`
- **Branch:** `feature/physis-nature-simulation` (branched from `feature/universal-entity-inspector-fps`).
- **Handoff commit:** run `git log -1 -- docs/agent-handoffs/physis-core-2026-11-18.md` after checking out.
- **Status:** code and standalone CTest registration committed. **Compilation and CTest NOT RUN** in the current connector-only environment. This is not a validated release.
- **Critical rule:** PHYSIS must NOT include or depend on FPS/RTS/RPG/genre modules. A genre is only a consumer, never an implementation branch.

## Changes
1. `engine/physis/Physis.h` — self-contained C++20 header (only `engine/core/ECS.h`, `Fixed.h`). Definitions:
   - `aiverse::physis::Cell` (grid coordinates and elevation, ECS component)
   - `Atmosphere` (temperature, humidity, pressure, x/z wind, rain, snow — ECS component)
   - `Surface` (wetness and snowpack — ECS component)
   - `Settings` (grid size/origin/cell size, seed, baseline climate)
   - `State` (World resource: settings, tick counter, row-major ECS cell entities)
   - `Sample` (value-only, read-only result)
2. `tests/physis_core_test.cpp` — deterministic tick repeatability, coordinate sampling, negative coordinates, snapshots/rollback/COW, custom non-genre gameplay component isolation, guards.
3. `CMakeLists.txt` — `aiverse_physis_core_test`, CTest `physis_core` with PHYSIS label.

## API example
```cpp
#include "engine/physis/Physis.h"
aiverse::World world;
aiverse::physis::Settings config;
config.seed = 42;
config.width = 128;
config.height = 128;
aiverse::physis::initialize(world, config);
for (uint64_t tick = 0; tick < 100; ++tick)
    aiverse::physis::advance(world); // exactly one call per committed fixed tick
aiverse::physis::Sample result;
if (aiverse::physis::sample(world, 10, 12, result)) {
    // Consume result.atmosphere.wind, result.atmosphere.rain, result.surface.wetness...
}
```

## Architecture and simulation contract
- PHYSIS owns ECS components on deterministic grid-cell entities and uses a snapshotted `State` resource, not a sidecar/renderer state. Coordinates are int32 cell indices in X/Z. Cell index order is Z-major.
- All authoritative update math uses the project's Q47.16 `aiverse::Fixed`; keyed hash uses portable fixed-width integers. No `std::hash`, random_device, wall clock, or floating point in gameplay state.
- `advance(World&)` is sync/serial in Stage 1, called by a host's fixed-step scheduler; do not invoke from render frames. No hidden global scheduler. All systems using PHYSIS must be independently composable.
- `cellAtPosition` maps world X/Z metres to grid cells, including negative coordinates. `read` and `sample` take `const World&`.
- World copies and copy-on-write rollback snapshots include these ECS pools and `State`; this does **not** automatically register PHYSIS with the custom `Simulation::serialize` file format.

## Tests
- **NOT RUN:** no AIVERSE checkout or C++ build was available during this GitHub connector commit. The test source and CTest target are written, but not executed and not asserted to pass.
- Commands for the next agent:
  ```sh
  cmake -S . -B build/physis-test -DAIVERSE_BUILD_TESTS=ON
  cmake --build build/physis-test --target aiverse_physis_core_test -j4
  ctest --test-dir build/physis-test --output-on-failure -R '^physis_core$'
  ```
- Also verify with Clang/GCC/MSVC and COW snapshots in debug/sanitizer builds. Fix any real errors and record exact outputs here.

## Known limitations
- Current seed-driven local forcing is deliberately a *baseline*, not meteorological weather-fronts, advection, Navier–Stokes, regional climates, or real snow-water conservation.
- No automatic connection to engine Simulation tick ordering, save/load serialization, scene editor, networking, view/render/VFX/foliage/water yet; these require explicit integration and tests.
- `initialize` is currently one-time per World; do not call twice. Grid is static/bounded at 1,048,576 cells. Dynamic streaming, level-of-detail, and unload ownership remain unimplemented.
- `advance` currently runs serially. Avoid implying it uses the ECS jobs infrastructure; design future range chunks with deterministic barriers and per-cell disjoint writes.
- Wind units and snowpack are preliminary, gameplay tuning remains uncalibrated.

## Concrete next step — Stage 2: Atmos & Climate
1. Introduce generic `ClimateZone` / `WeatherFront` value components and a configurable climate model driven by `State::tick`, with deterministic seasonal baseline / smooth spatial fields; independent of any genre and renderer.
2. Separate the weather update from surface update into independently callable ECS systems, preserving fixed tick order and snapshot reproducibility. Do not break existing `WeatherTimeline` which is view-only.
3. Add tests for seasons, freezing threshold, adjacent-cell continuity, seed repeatability, snapshot rollback, and custom native game use.
4. Commit code and CTest registration, then commit a **new Stage 2 handoff** including exact SHA, test results, modified files, limits and next action. Repeat per stage.
