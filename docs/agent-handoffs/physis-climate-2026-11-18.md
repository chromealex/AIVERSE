# PHYSIS Stage 2 — climate zones and spatial weather fronts (2026-11-18)

**Branch:** `feature/physis-nature-simulation`  
**Implementation SHA:** `e874e518450109ebd689a8e66f0f2c58b73865b8`  
**Handoff SHA:** see `git log -1 -- docs/agent-handoffs/physis-climate-2026-11-18.md` after checkout.  
**Verification:** **NOT RUN** (GitHub connector-only changes; no local checkout, compiler/CTest results, CI or rendering validation).

## Completed implementation
- `engine/physis/PhysisClimate.h` (new): `ClimateZone` and `WeatherFront` ECS components; pure fixed-point `seasonSignal`; read-only spatial forcing `forcingAt`; authoritative `advanceClimate(World&)` which evaluates all cells against the same weather-front positions and moves fronts after their effects are applied.
- `engine/physis/Physis.h` (extended): new `Forcing` value type and `advanceWith(World&, Provider)` extension point. Original `advance(World&)` remains source-compatible as zero-forcing baseline. Fixed-point atmospheric temperature/humidity/pressure, wind, rain/snow and wet surface changes consume any forcing without a genre dependency.
- `tests/physis_climate_test.cpp` (new): seasonal triangle boundaries, region warming, freezing/rain-snow distinction, moving-front replay determinism, rollback and foreign custom ECS components.
- `CMakeLists.txt` (extended): `aiverse_physis_climate_test` / CTest `physis_climate`.

## Native C++ API example
```cpp
#include "engine/physis/PhysisClimate.h"
aiverse::World world;
aiverse::physis::initialize(world);
auto cell = aiverse::physis::cellAt(world, 4, 6);
world.add<aiverse::physis::ClimateZone>(cell, aiverse::physis::ClimateZone{});
auto front = world.create();
world.add<aiverse::physis::WeatherFront>(front, aiverse::physis::WeatherFront{});
aiverse::physis::advanceClimate(world); // one committed fixed tick
```

- Each `ClimateZone` applies to one spatial cell; projects are free to configure cells via biomes/terrain/environment rules outside PHYSIS. `yearTicks` is configurable, temperature variation uses integer triangle wave rather than platform-dependent sine. To disable seasons, set both season amplitudes to zero.
- `WeatherFront::center` and `velocity` are measured in *grid cells* and *grid cells per tick*. `radiusCells` uses Manhattan attenuation. Fronts are simply ECS entities; no scene or graphics-specific references.
- Gameplay logic can call `forcingAt` to inspect a cell or `advanceClimate` to perform atmosphere + surface evolution. A host must choose **either** baseline `advance` or `advanceClimate` for a tick, never both. Rendering is a consumer only.
- All authoritative math uses `Fixed` and serial ECS traversal; World COW snapshot/rollback includes fronts, zones, cell states and tick.

## Verification still required
```sh
cmake -S . -B build/physis-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/physis-test --target aiverse_physis_core_test aiverse_physis_climate_test -j4
ctest --test-dir build/physis-test --output-on-failure -R '^physis_(core|climate)$'
```
Tests are *written and registered*, not executed. Run Clang/GCC/MSVC builds and sanitizer tests; report genuine failures before claiming pass. Check that root CMake registration is correct on the latest branch.

## Remaining limitations and risks
- Current `forcingAt` scans all WeatherFront components for each cell: O(cells x fronts), intended baseline only. Must use spatial indexing for large worlds, SIMD/range jobs later.
- Seasonal cycle is a configurable triangle, not sun/orbit/weather physics; fronts use Manhattan falloff and linear movement, not wind/advection. No regional altitude/latitude forcing or conservation yet.
- No connection to engine `Simulation` fixed-step pipeline, save format, network replication, editor, Terra, Foliage, Water, VFX, Audio or Physics. No headless integration proof beyond standalone ECS test source.
- `advanceClimate` serially modifies atmospheric state and then front positions. Multithreading is **not** implemented.
- The old `WeatherTimeline` remains a view-only helper; do not have a game feed its float values back into PHYSIS.

## Next bounded stage — Environment Interactions
1. Add opt-in, genre-neutral `EnvironmentalMaterial`, `SurfaceState` and interaction interfaces: precipitation wetness, snowpack and frost/ice; keep mass/units documented and allow injection of terrain-dependent parameters.
2. Refactor surface changes into a separate ECS pass with deterministic ordering and no unconditional coupling to climate; avoid updating surface twice.
3. Provide data-only adapters for Terra, Water, Foliage and Physics: no hard link to renderer, no dependencies on FPS/RTS module headers.
4. Test wet/dry and accumulation, freezing/melting, disabled modules and custom native consumers. Commit implementation, then a **separate** Stage 3 handoff including actual tests and exact SHA.
