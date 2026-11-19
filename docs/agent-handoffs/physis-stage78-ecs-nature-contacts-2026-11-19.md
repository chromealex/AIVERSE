# PHYSIS Stage 78 — ECS Nature Contacts and opt-in NEREID composition

Date: 2026-11-19
Branch: `feature/physis-nature-simulation`
PR: https://github.com/chromealex/AIVERSE/pull/25 (DRAFT)
Implementation commits:
- `354e0689fffcfc4ec9853307fc3a06ba32c86212`
- `a286022cb733c524f30b795501c957ee1ae3f9ec`
Documentation commit: inspect `git log -1 -- docs/agent-handoffs/physis-stage78-ecs-nature-contacts-2026-11-19.md`.
Previous documented implementation: Stage 76, `docs/agent-handoffs/physis-stage76-build-monitor-diagnostics-2026-11-19.md`.

## What was built

- `engine/physis/PhysisNatureSampling.h`:
  - `NatureProbeComponent {Vec3 position; bool enabled;}`;
  - `NatureContactComponent` with sampled position and read-only
    derived wind, gravity, temperature/humidity/pressure, rain/snow,
    wetness/snowpack/ice, runoff potential, fire/quake and
    `climateTick` / `sampledSimulationTick` / valid flags;
  - `io(Archive&,...)` for optional ECS component persistence;
  - `advanceNatureContacts(World&,uint32_t)`: iterates existing
    `NatureContact` entities; reads `NatureProbe` and canonical
    PHYSIS fields, clears invalid/disabled probes and publishes
    snapshots without advancing the climate tick or creating
    entities.
- `engine/physis/PhysisIntegration.h`: opt-in
  `InstallOptions::enableNatureContacts=false` by default,
  registers optional persistence schema IDs
  `physis.nature_probe` / `physis.nature_contact` (v1)
  and schedules `physis.nature_contacts` AFTER
  `physis.environment`, preserving current legacy save
  manifests when disabled.
- `engine/world/PhysisNereidNatureBridge.h`: optional
  read-only `NatureWaterContactView` and
  `readNatureWaterContact(world,entity,view)`; rejects
  disabled/invalid nature probe, mismatched sample tick and
  stale nature or NEREID probe positions. Does not read or
  mutate authoritative water bodies and does not install
  either scheduler. Host remains responsible for NEREID
  water contact sampling and `FloodMode::ExternalAuthority`.
- `tests/physis_nereid_nature_contacts_test.cpp`:
  same-World `Simulation` host with PHYSIS and NEREID,
  negative world coordinates, one lake, a windy local volume,
  external flood authority, contact validation, movement
  without resampling, save/load, rollback/replay and COW modes.
- `CMakeLists.txt` registers
  `aiverse_physis_nereid_contacts_test` and
  CTest `physis_nereid_contacts` with PHYSIS/NEREID/ECS labels.

No gameplay genre, renderer/GPU, NEREID body or solver,
TerraWeave surface, Box3D or existing network schema changed.

## Verification status — important

- GitHub source and CMake test registration were inspected
  after both commits; new test source and public API exist.
- **NOT RUN:** compilation of Stage78 changes, new native
  `physis_nereid_contacts` CTest, entire PHYSIS/NEREID
  filtered or full AIVERSE suite, real GUI or Metal QA.
- Earlier user-reported PHYSIS/NEREID/SpectraFX filtered 40/40
  pass was on older code. No claim of Stage78 native PASS.
- User later confirmed universal Build Monitor `tool_aiverse_progress`
  CTest 1/1 and a monitored macOS AIVERSE build with zero
  warning/error lines, both PRIOR to Stage78.
- Branch PR remains DRAFT and `main` was not modified.

## Next commands / action

```sh
cd ~/Projects/AIVERSE &&
git fetch origin &&
git switch feature/physis-nature-simulation &&
git pull --ff-only origin feature/physis-nature-simulation &&
cmake -S . -B build/physis-nereid-validation \
  -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_ENABLE_PHYSICS=ON &&
cmake --build build/physis-nereid-validation \
  --target aiverse_physis_nereid_contacts_test --parallel 4 &&
ctest --test-dir build/physis-nereid-validation \
  --output-on-failure -R '^physis_nereid_contacts$'
```

If compilation fails, inspect `NatureContactComponent` Archive
serialization and CMake include target transitivity first.
If runtime fails, inspect sample tick order, seeded climate wind
and snapshot mutations; do not weaken assertions to fit code.
After green, rerun `^(physis_|ecs_nereid_)`, full suite and
the live editor, capture real logs and document a new Stage79.

## Remaining environment capabilities

Full physical meteorology/CFD, water volume response to calibrated
precipitation, erosion/vegetation/terrain coupling, true real-time
damage from wind/tornados to TerraWeave trees and structures,
native production WSS/TLS and live GUI/GPU performance are NOT
finished/accepted in this stage. Keep subsystem ownership clear.
