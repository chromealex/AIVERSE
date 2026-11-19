# PHYSIS Stage 10 — hurricane and tornado ECS storm fields (2026-11-18)

**Branch:** `feature/physis-nature-simulation`, draft PR #25.  
**Code/test commits:** `d8e3ccf2df5db8607ff174a4a908803eaa1ac216`, `856a5848fce47a954bdcdf079af3012c1c719aa1`, `e06b166d28db923dd898c27d3ed5068c02e0fe9c`, `b13b0a6e8010d7fc47a5e3398c34cd4b346e7303`, `c2f3a185d7f405963db012be6d419c038868c00c`, `90fabffa00659af6843ecde73923e95d7d907566`, `01910ef60e247b0018f38c753a7679a1060112dd`, `6a732754a4e39955b76b8cdf71b583501d81ba07`.  
**Verification:** NOT RUN — no CMake configure/build, CTest, CI, production rendering, or physics scene execution.

## Implementation
- `engine/physis/PhysisStorm.h`: `StormEvent` is an ECS component with `StormKind::{Hurricane,Tornado}`, center, drift/tick, eye/outer radius, vertical extent, swirl/inflow/updraft velocity, rain intensity and pressure drop, timing and rotation. `stormContribution`, `stormsAt`, `windWithStormsAt` are read-only Fixed-point samplers. `advanceEnvironmentWithStorms` / `advanceEnvironmentWithStormsAndDisasters` advance **one climate tick** plus storms and optional disasters, with a nonduplicating `FloodMode` policy.
- `engine/physis/PhysisClimate.h`: add `advanceClimateWithSurfaceAndForcing` general extra-forcing hook. Original `advanceClimateWithSurface` delegates with zero extra forcing; preexisting climate API remains.
- `engine/physis/PhysisIntegration.h`: persistent ECS schema `physis.storm_event` v1, field-wise Archive I/O, new `InstallOptions.enableStorms` opt-in; `install` dispatches one weather system appropriate for storms/disasters. Existing hosts default to storms off.
- `tests/physis_storm_test.cpp`: vortex handedness, opposite sides, updraft, pressure and precipitation forcing, deterministic track movement, peer World and rollback, external-water-authority hazard composition. `CMakeLists.txt` registers `physis_storm` test.
- No dependencies on Box3D, water agents, TerraWeave, genres, renderer, wall clock, or floating point in PHYSIS.

## Usage
```cpp
#include "engine/physis/PhysisIntegration.h"
physis::InstallOptions opts;
opts.enableStorms = true;
opts.enableDisasters = true;
opts.floodMode = physis::FloodMode::ExternalAuthority; // if NEREID owns water
physis::install(simulation, opts);

physis::StormEvent cyclone;
cyclone.kind = physis::StormKind::Hurricane;
cyclone.center = {100_fx, 0_fx, 200_fx};
cyclone.outerRadiusMeters = 3000_fx;
simulation.world().add<physis::StormEvent>(simulation.world().create(), cyclone);
// Once per host authoritative tick via simulation.step():
const Vec3 wind = physis::windWithStormsAt(simulation.world(), position);
```

## Dynamics and limitations
- Hurricane/tornado profiles are authored analytic radial vortex fields, **not physical atmospheric Navier–Stokes**. No cloud/particle/shader/debris destruction, terrain obstruction, water surge, flooding bridge, or visual cockpit integration is implemented.
- Storm pressure/rain are fed to atmospheric Forcing. Storm physical wind is sampled through `windWithStormsAt` to avoid double wind contribution. Storm tracks move after climate consumed the previous center, then post-tick physics/display samples next center; a one-step lag between local rain and the current track is intentional and must be checked for visual alignment.
- `StormEvent` persists on ECS; `StormEvent::driftPerTick` is metres per committed PHYSIS tick, not metres/second. Event duration 0 means persistent. No jobs, spatial index, large-world streaming, volumetric CFD, destruction or turbulence-based pressure gradients.
- PHYSIS precipitation is normalized intensity, not physical water volume. Water agent/NEREID owns any rainfall-to-water simulation coupling.
- Storm activation in `install` is opt-in via `enableStorms`; storm component existence alone does not automatically update climate/track.

## Tests/validation
```sh
./run-tests.sh --filter '^physis_'
```
**NOT RUN** in connector session. Fix compile/test errors before merging; run existing climate/disaster regression and persistence tests too.

## Next stage
Implement common `PHYSIS Forces` data-only drag/gravity policy, and a **separate optional Box3D Scene adapter** under `engine/physics`, using `Scene::applyForce`, `BodyHandle`, `b3Body_GetMass`, and an explicit Box3D baseline gravity subtraction. Bind to Box3D only in adapter and its optional `AIVERSE_ENABLE_PHYSICS` CMake test; no Box3D includes in `engine/physis`. Verify by live Box3D physics scene with correct real motion; handoff separately after code. Do not merge other agents' Water/Terra branches.
