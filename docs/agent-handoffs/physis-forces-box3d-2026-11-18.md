# PHYSIS Stage 11 — universal forces + optional Box3D Scene adapter (2026-11-18)

**Branch:** `feature/physis-nature-simulation`, draft PR #25.  
**Code/test commits:** `f9a6cbd0cfd73164ba8bd0889d48f5c4d7e05f3e`, `4aff85e8757ff17562501c86bea44fb45bbf63bb`, `4baaedd9d38adab2510d9e3a893af998086f5a14`, `5463327340d8f66265625b9665b0e9bc9ed5ca0c`, `7811b1b7bf21364762d2af709fde01acfa829359`, `a7aa67ed76dca8663c41ca78401f725d0c9191fc`, `9eb5c2be09b592c3ef6393325da0e48f5c190f48`, `9ebe9e35c16faa5718965f2a91a19c23aaa4317c`.  
**Verification:** NOT RUN: no native C++ compile, CTest, CI, Box3D scene execution, editor or FPS/RTS integration.

## What was implemented
- `engine/physis/PhysisForces.h`: genre-neutral Fixed ECS `ForceReceiver`, `ForceResult`, `ForceOutput`, `evaluateForces`, `advanceForces`. Inputs: position, velocity, body mass, air density, exposed area, coefficient, gravity scale. Read-only wind uses PHYSIS Wind + Storm; gravity uses PHYSIS Gravity. Outputs include full gravity and quadratic relative-airflow drag; `advanceForces` **only publishes ECS output** after a tick, never integrates velocity or steps Box3D.
- `engine/physics/PhysisBox3D.h`: optional physics-owner adapter `physics::applyPhysisToBody(physics::Scene&, World const&, BodyHandle, PhysisBodyMaterial)`. Reads actual Box3D position/velocity/body mass/gravityScale, converts at float↔Fixed boundary and applies force via existing `Scene::applyForce`. **Subtracts native `b3World_GetGravity` from PHYSIS `gravityAt` before multiplying by mass and body gravityScale**, avoiding double gravity. Body handles remain host-owned and generation-safe; invalid/static bodies rejected. No Box3D includes enter `engine/physis`.
- `engine/physis/PhysisIntegration.h`: version-1 persistent schema `physis.force_receiver`, `physis.force_output`, field-wise Archive I/O and optional `InstallOptions.enableForces` (defaults false). If true, schedules `physis.forces` **after** one `physis.environment` update, but does not advance external physics.
- `tests/physis_forces_test.cpp`: gravity/wind mathematical outputs, publish-only ECS system, COW rollback and tornado updraft.
- `tests/physis_box3d_test.cpp`: real `physics::Scene` fixture for baseline gravity parity, zero-G compensation, wind movement and tornado lift. Test only builds if `AIVERSE_ENABLE_PHYSICS=ON`.
- `CMakeLists.txt`: `physis_forces` always under `AIVERSE_BUILD_TESTS`; `physis_box3d` additionally behind `AIVERSE_ENABLE_PHYSICS`.

## Suggested native host ordering

```cpp
// GameModule::setup:
physis::InstallOptions opts;
opts.enableStorms = true;
opts.enableForces = true; // only if ECS ForceReceiver/ForceOutput policy is desired
opts.enableDisasters = true;
opts.floodMode = physis::FloodMode::ExternalAuthority; // NEREID owns water
physis::install(sim, opts);

// Host: exactly one authoritative Simulation step, then Box3D Scene owner
// samples current PHYSIS world, applies forces to each opted-in dynamic body,
// and calls Scene::step() once. The host owns body-to-ECS associations.
for (auto handle : myOptInBodyHandles)
    physics::applyPhysisToBody(box3dScene, sim.world(), handle, bodyMaterial);
box3dScene.step();
```

Do **not** apply `ForceResult::totalForce` directly to Box3D without subtracting its native gravity; the optional adapter already performs the correction. Do not apply both the ECS `ForceOutput` and `applyPhysisToBody` to the same body. Do not store Box3D float transforms as PHYSIS authoritative state without explicit host-controlled synchronization, save/rollback semantics and an authority choice.

## Verification commands (NOT RUN)

```sh
# New CPU-only components plus existing PHYSIS regressions:
./run-tests.sh --filter '^physis_'

# Separate optional Box3D build: root runner normally respects its configured flag:
cmake -S . -B build/physis-physics -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_ENABLE_PHYSICS=ON
cmake --build build/physis-physics --target aiverse_physis_box3d_test
ctest --test-dir build/physis-physics -R '^physis_box3d$' --output-on-failure
```

Run `physics_runtime`, `physics_scene` and legacy ECS replay/save tests as regression before merging. All results are **NOT RUN** until captured from an actual runner/CI.

## Limits and caveats
- Box3D adapter is opt-in and uses host thread; it **does not** register an automatic engine scene system, Character kinematic movement, platform-specific GPU code or collision damage/destruction. `physics::CharacterParams` has a scalar gravity; arbitrary 3D gravity for characters would require a separate locomotion API rather than hidden changes.
- Box3D native physics is float; preserving exact lockstep determinism across physics platforms is **not claimed**. PHYSIS ECS state uses fixed point and is independent of its presentation/physics consumer.
- Drag uses simplified isotropic projected-area policy (no aero lift coefficient, shape-specific pressure distribution, obstruction/collider occlusion or CFD). Wind, storm and gravity fields are O(active volumes) samples.
- Current force output has no real terrrain, water, ocean surge or vegetation destruction coupling. NEREID Water and TerraWeave remain separate agents/PRs; see `docs/physis-nereid-terraweave-integration.md`.
- Weather and storm/force settings must be consistently configured per native host/server; custom snapshots need to include any host-owned Box3D Scene state.
- There is no live compiled evidence in this PR: **must keep draft** until builds, tests and cross-platform checks pass.

## Next exact steps
1. On a local checkout run the commands above, fix compile, numerical and runtime problems and record *actual* results. Check fixed-point overflow guards for extreme storm/wind/force values.
2. Add a generic native project example with Box3D Scene single-thread integration and optional debug visualizer; keep all gameplay genre-independent.
3. Add follow-on performance features (spatial index/active-cell weather LOD, non-blocking jobs, streaming), editor widgets, VFX/Terra/NEREID host composition under dedicated integration PRs. Distinguish new proposals from implemented code.
4. Commit each implementation stage and a **separate** handoff doc with SHAs, status and next exact task.
