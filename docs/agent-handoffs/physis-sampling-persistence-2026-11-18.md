# PHYSIS Stage 13 — consolidated nature sampling and persistent schema compatibility (2026-11-18)

**Branch:** `feature/physis-nature-simulation`, draft PR #25, base `feature/universal-entity-inspector-fps`.  
**Status:** source committed and tests registered; **NOT RUN**: native CMake build, CTest, CI result, real editor, Box3D Scene and cross-branch integration.  
**Related:** previous Stage 12 `docs/agent-handoffs/physis-numerical-ci-2026-11-18.md`.

## Exact implementation/test commits

- `3710de14ea28f3e06b9bbd33c775bc6fb8a7022f`: `engine/physis/PhysisPresentation.h` read-only `NatureFieldSample` and `sampleNatureField(World const&, Vec3, NatureFieldSample&)`.
- `e09d9bb4f3f3620a27cf639b394ddc78bfcc748e`: `tests/physis_presentation_test.cpp` tests empty world, negative coordinates, optional atmosphere/hazards, wind/storm/gravity combination and rollback.
- `c4062f5ae6d4e1f010434292404e1ee5f9111c62`: `CMakeLists.txt` registers `physis_presentation` CTest.
- `65056ead173d7410c5cad02413394cd8c777ef32`: add new target to `.github/workflows/physis.yml` focused Ubuntu/macOS and opt-in Box3D workflow.
- `832bc0f731065cc1732f99ee09f56a5882d3fe21`: fix `PhysisIntegration.h` to register optional wind/gravity/storm/force ECS save schemas **only** when opt-in flags are enabled.
- `16933ee9db6fec10ad84b8337e0a5e32a7464ab0`: `tests/physis_integration_test.cpp` asserts baseline register count 8 (with disasters), extended register count 14, save/load peers and reject mismatched schema manifests.
- `62ff263fefa6ec9ecaedf99e611d09623aa7be4d`: `docs/physis.md` updated with combined API, save contract and handoff links.
- `b71efc940c1c9e17b9f269133d6ee0120e3ad6ed`: `skills/world/aiverse-physis/SKILL.md` documents reuse and schema switches.
- `87fd9acbcd412feef4a00b3271429c22617a34d2`: root `AGENTS.md` source/test discovery.
- `42b15bbcce2d35c2b6e625b6ae6dbfbe8bcdc0ff`: `engine/physis/AGENTS.md` newest handoff pointer.

## Public API

```cpp
#include "engine/physis/PhysisPresentation.h"
physis::NatureFieldSample sample;
bool initialized = physis::sampleNatureField(world, queryPosition, sample);
if (initialized) {
    // sample.windVelocity: 3D m/s incl zones + storm vortex
    // sample.gravityAcceleration: 3D m/s²
    // sample.storms: local vortex/precipitation/pressure offset
    // sample.environment: surface wetness, snow, rain, temperature, etc.
    // sample.pressureHpa: PHYSIS sampled atmosphere pressure
    // sample.hazards: only when sample.hasHazards
}
```

Returns false without initialized PHYSIS state and does not mutate output. Returns true outside the climate grid (because global gravity/wind/storms can still exist); `hasAtmosphere` and `hasHazards` indicate whether grid-dependent data is valid. All state is copied out of ECS; no renderer or water/terrain engine dependency. Negative coordinates use existing `cellAtPosition` floor division.

For runtime read-only Terra/VFX consumers, use `NatureFieldSample`. Do not generate terrain water lines from PHYSIS `HazardCell::waterDepth` when NEREID is the water authority. Read NEREID separately via its own water/geometry API. This new sampler does NOT connect Terra or render an editor overlay automatically.

## Save compatibility fix

Investigation of `engine/sim/PersistentEcs.h` showed the serialization format persists a strict sorted schema ID count/versions. Before this stage, PHYSIS registered wind/gravity/storm/force schemas unconditionally and therefore **would change Stage 1–5 save manifests even for default hosts**.

`registerPersistence` signature now retains the existing first optional `disasters` arg and adds four optional flags, all default false:

```cpp
registerPersistence(simulation, disasters, windZones, gravityFields, storms, forces);
```

`InstallOptions` adds:
- `enableWindZones` (false): register `physis.wind_zone` and `physis.wind_shelter`
- `enableGravityFields` (false): register `physis.gravity_field` and `physis.gravity_settings`
- `enableStorms` (false): register `physis.storm_event` and enable storm climate tick
- `enableForces` (false): register `physis.force_receiver`, `physis.force_output` and enable force-output pass

Existing unchanged Stage 1–5 hosts set only `enableDisasters` (if needed), so the PHYSIS registry count and schema identifiers remain exactly the old set. **Do not enable or disable these flags when loading an existing save** without a formal migration. While the runtime source is committed, actual binary compatibility **has not been tested yet**. A native host that manually inserts optional components without setting their flags may use them during the current session, but they will NOT be persisted; therefore enabling flags is mandatory when long-lived custom field data is used.

## Verification steps and remaining risks

```sh
./run-tests.sh --filter '^physis_'
# Existing optional physics separate build:
cmake -S . -B build/physis-physics -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_ENABLE_PHYSICS=ON
cmake --build build/physis-physics --target aiverse_physis_box3d_test
ctest --test-dir build/physis-physics -R '^physis_box3d$' --output-on-failure
```

All listed commands are **NOT RUN** in connector-backed session. GitHub workflow `PHYSIS nature + Box3D integration checks` was created, but no completed PASS/FAIL logs were retrieved. On a developer checkout or completed CI run, record exact commit SHA, target, compiler errors and CTest results in follow-on handoff.

Known gaps: no compiled acceptance yet; no native NEREID/Terra adapter; no editor wind/pressure visualization, foliage sway, underwater storm surge or tornado collision/debris destruction. PHYSIS is a simplified finite-grid and vortex field approximation, not CFD/planetary solver. `NatureFieldSample` samples climate from X/Z grid while 3D wind uses world Y; vertical atmospheric stratification is not yet simulated. Large-world spatial streaming and per-sample spatial indexes remain unimplemented.

## Next agent actions

1. First retrieve CI build logs / run Linux or macOS native builds; fix compile errors and rerun every PHYSIS target including `physis_box3d`. Do not claim success until actual results exist.
2. Validate Stage 1–5 save/restore fixture and newly added opt-in save schema fixture. Ensure old saves deserialize correctly with default flags and cross-option loads fail with clear errors.
3. Coordinate with the separate TerraWeave (PR #24) and NEREID (PR #23) owners on independent read-only consumption: converted Terra cm terrain height/biome, PHYSIS nature field, NEREID waterline and currents. Create a separate optional adapter PR, not a hard cross-dependency in PHYSIS core.
4. Implement a bounded instrumentation/debug viewer or optional world-scene visualization after CI is green; avoid modifying the concurrent editor unification PR.
5. For **every** new step write code/tests commit(s) and a separate handoff doc commit, documenting actual validation and next action.
