# PHYSIS Stage 9 — gravity fields as environment acceleration policy (2026-11-18)

**Branch:** `feature/physis-nature-simulation` (draft PR #25).  
**Code/test commits:** `b9d94e1dc91761a539ebec2085d2de1b98d0b6a5`, `f8d09f5f21335bc858dd38129e9cb65fda1b1c06`, `fdfd0a1818f6ec465fe3e266b4e6f96eba3bff1e`, `3c69dbf05cf3675ba45754e3cd65f2a08a92945b`.  
**Verification:** NOT RUN (C++ build, CTest, native Box3D scene and CI all unverified).

## Scope / public API
- `engine/physis/PhysisGravity.h`: ECS `GravityField` (Uniform, Attract, Repel; optional sphere radius; priority; `overrideBaseline`), World resource `GravitySettings` (default 0, -9.81, 0 m/s²; cap); pure fixed-point `gravityAt(World const&, Vec3 position)` with deterministic tie-break (higher priority, lower packed entity).
- Uniform field with radius 0 is global. With positive radius, effect fades linearly to 0. Radial fields are bounded author-defined point attractors/repulsors with normalized inward/outward vector, no inverse-square N-body physics.
- Additive fields superpose. Override winner interpolates authored acceleration over baseline using spherical weight; additive fields remain additive. Zero-g available via zero baseline resource or overriding zone.
- `engine/physis/PhysisIntegration.h`: portable field-wise Archive I/O for settings and field; new version-1 persistent IDs `physis.gravity_settings` (resource) and `physis.gravity_field` (component), no existing PHYSIS schema layouts changed.
- `tests/physis_gravity_test.cpp` and `CMakeLists.txt`: `physis_gravity` CTest for baseline, additive/override, radial attraction/repulsion, World copy/restore.

## Usage
```cpp
#include "engine/physis/PhysisGravity.h"
aiverse::physis::GravitySettings settings;
settings.baseline = {Fixed{}, Fixed{}, Fixed{}}; // zero-G world
world.setResource<aiverse::physis::GravitySettings>(settings);
aiverse::physis::GravityField moon;
moon.mode = aiverse::physis::GravityMode::Attract;
moon.center = {0_fx, 0_fx, 0_fx};
moon.radiusMeters = 500_fx;
moon.radialMagnitude = 2_fx;
world.add<aiverse::physis::GravityField>(world.create(), moon);
const auto acceleration = aiverse::physis::gravityAt(world, queryPosition);
```

## Important Box3D contract
This stage **DOES NOT** change Box3D world.gravity or apply forces. Box3D already applies its own gravity for dynamic bodies. The later adapter must compute **delta gravity** = PHYSIS `gravityAt` minus the Box3D configured world baseline, multiply by body mass and apply one `physics::Scene::applyForce` per fixed Box3D step; otherwise it would double gravity. Do not copy Box3D float positions into lockstep authoritative ECS without explicit host-owned synchronization and snapshot/replay semantics. Keep Box3D optional (not linked by PHYSIS core).

## Verification
```sh
./run-tests.sh --filter '^physis_'
```
**NOT RUN**; no compile, test, performance, persistence or platform results recorded.

## Limitations / next
- No spherical planet interior, relativistic gravity, inverse-square celestial N-body calculation, arbitrary SDF volumes or gravity-driven deformation. `radialWeight` spheres require sane input radius and world-coordinate bounds, spatial indexing pending.
- Need storm/tornado ECS volumes and climate forcing, stage handoff; then a separated optional Box3D physics adaptor with a live Scene test proving no double gravity and real force application.
- Document all stage SHAs, tests and limits. Keep NEREID Water / TerraWeave ownership separate.
