# PHYSIS Stage 3 — environmental materials and universal responses (2026-11-18)

**Branch:** `feature/physis-nature-simulation`  
**Implementation commit:** `54f291991d318d9593aad32e42dbcd766347ca2a`  
**Handoff commit:** find with `git log -1 -- docs/agent-handoffs/physis-environment-2026-11-18.md`.  
**Verification:** **NOT RUN** — connector-backed writes only, no compiler, CTest, CI, headless runner, renderer or sanitizer run.

## Delivered changes
- `engine/physis/Physis.h`: added `Surface::ice` and `SurfaceParameters` (per-tick wetting, drying, snow retention, thawing/melting, freezing threshold). Introduced independent `advanceSurface(World&, Provider)` ECS pass. Atmosphere update and surface update are now separate operations composed by `advanceWith(World&, AtmosProvider, SurfaceProvider)`. The original `advanceWith(World&, AtmosProvider)` and `advance(World&)` stay source-compatible.
- `engine/physis/PhysisClimate.h`: added `advanceClimateWithSurface(World&, SurfaceProvider)`; previous `advanceClimate(World&)` remains a compatible default.
- `engine/physis/PhysisEnvironment.h` (new): `SurfaceMaterial` ECS component (surface simulation rates, permeability, dry traction); `EnvironmentalResponse` (temperature, wind, rain, snow, wetness, snowpack, ice, relative traction, runoff potential, fire suppression); `environmentAt` read-only per-cell query; `advanceEnvironment` combined climate + per-cell surface step.
- `tests/physis_environment_test.cpp`: material differences, wet/dry, snow/ice, read-only response, snapshot repeatability and native custom component isolation.
- `CMakeLists.txt`: added CTest `physis_environment` and `aiverse_physis_environment_test` target.

## Universal API
```cpp
#include "engine/physis/PhysisEnvironment.h"
aiverse::World world;
aiverse::physis::initialize(world);
auto terrainCell = aiverse::physis::cellAt(world, 2, 3);
aiverse::physis::SurfaceMaterial material;
material.permeability = aiverse::Fixed::fromRatio(1, 4);
world.add<aiverse::physis::SurfaceMaterial>(terrainCell, material);
// Called once for each authoritative, committed simulation tick:
aiverse::physis::advanceEnvironment(world);
aiverse::physis::EnvironmentalResponse response;
if (aiverse::physis::environmentAt(world, 2, 3, response)) {
    // Native game reads response.tractionMultiplier, response.wind,
    // response.runoffPotential, response.fireSuppression, etc.
}
```

## Contract and ordering
- **Exactly one** of `advance`, `advanceClimate`, `advanceEnvironment` per world per authoritative tick. They are alternatives; never call all three. `advanceSurface` is exposed for custom scheduling; do not call it after a combined step (double accumulation).
- Atmosphere, surface and material values are ECS components. Local world state is `State` resource. Presentation and external physics/terrain modules can sample the response but PHYSIS does not pull in genre-specific or renderer headers.
- `EnvironmentalResponse` values `runoffPotential` and `tractionMultiplier` are **normalized heuristics**, not an implementation of water transport or contact material friction. Game/physics code decides how to consume them.
- `SurfaceMaterial::simulation` overrides default response coefficients independently per cell. Without a material, neutral defaults preserve Stage-1 rain and snow rates.

## Tests and risk
```sh
cmake -S . -B build/physis-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/physis-test --target aiverse_physis_core_test aiverse_physis_climate_test aiverse_physis_environment_test -j4
ctest --test-dir build/physis-test --output-on-failure -R '^physis_(core|climate|environment)$'
```
- **NOT RUN**. Test sources exist, results are unknown.
- Climate forcing still costs O(cells x weather fronts) and all updates are serial. Static grid only, no streaming or simulation LOD. The `ice` field is a normalized coverage model, not a full thermodynamics/heat-transfer solver.
- No Terra/Water/Foliage/Physics/VFX plugin adapters wired into production, editor UX, standalone persistence file format or net replication. This stage provides **an interface for** those integrations, not proof they are connected.
- Need verify numerical clamping, COW snapshots, fixed-point safety across compilers and physics normalization before deploying game logic.

## Next bounded stage — disasters (PHYSIS Geo / Pyro / Hydro)
1. Add deterministic, optional ECS systems/components for fire sources, propagation, flooding and quake events; use `EnvironmentalResponse` or material data rather than genre-specific actors.
2. Define cross-cell propagation order and fixed-tick update barriers; read old state and commit once per tick, never allow traversal order to change results.
3. Add safe conservative budgets / event caps and large-world constraints. Do not claim full fluid/fire/volcano physics.
4. Write standalone tests for propagation, wind/rain suppression, seeded repeatability, snapshots and custom ECS consumer isolation; commit implementation and then a new handoff.
