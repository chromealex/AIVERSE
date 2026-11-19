# PHYSIS Stage 12 — numerical stability, native CI and agent skill (2026-11-18)

**Branch**: `feature/physis-nature-simulation`, draft PR #25 (base `feature/universal-entity-inspector-fps`).  
**Implementation commits**:
- `0a55829786eff6adac9d1403319eec763afc323c`: new discoverable `skills/world/aiverse-physis/SKILL.md`, documenting Box3D/NEREID/Terra ownership and native integration.
- `480b336263e87f400c0d80c194d054d771bab51e`: GitHub Actions `.github/workflows/physis.yml` focused Linux/macOS C++/CTest run including opt-in real Box3D Scene.
- `bfcbd8a595f636b50589e814989ee610ba9de46f`: `PhysisWind.h` now normalizes radial delta before squaring.
- `c0a106977d996c03bfcbb294c5200ac63c87e9ff`: `PhysisForces.h` clamps relative airflow velocity **before** computing squared Fixed magnitude.
- `3df63df981363ee38e16da2a9eaf9e90437b600a`: `PhysisStorm.h` normalizes planar radial distance before squaring.
- `6bf3512f2c0467f93ca8c367b1a4c0e8fa0da4cd`: `PhysisGravity.h` normalizes point gravity direction before squaring.
- `4cc2829c9a33d2348cfa0cb8e566c2cbaa1ebdfc`, `a51eb619f2673caa345efe92a9232879f7e13c97`, `e36f8df357f429bf33f895a6c5b0c7db92b45f77`, `7e95dcb6a9cf80c70f4164d37fc85ae4f063fbfd`: large-radius and high-velocity regression assertions in the existing PHYSIS CTests.

## Scope / compatibility
- No change to persisted ECS schemas, general gameplay/genre APIs, Box3D physics engine, NEREID or TerraWeave branches.
- The four authored field modules now avoid an avoidable Q47.16 overflow from large distances or adversarial velocities by normalizing spatial vectors or bounding velocity components before squared magnitude.
- The force cap remains 10,000,000 N/component, gravity cap 200 m/s²/component. This is a pragmatic gameplay policy, not a physics unit calibration, and requires domain-specific review.
- Radius+centre subtraction and unbounded sums across *extreme* numbers of overlapping ECS fields are not formal proof against every overflow case. Future profile/input validation should set max world radii and field counts.
- `skills/world/aiverse-physis/SKILL.md` tells agents to reuse the PHYSIS APIs and to keep Box3D optional, Water/NEREID/other Terrain responsibility independent.

## CI workflow and test status

The workflow is new and configured to compile **all ten PHYSIS CTest targets** on Ubuntu/macOS with `AIVERSE_ENABLE_PHYSICS=ON`: baseline 5 + wind, gravity, storm, forces, Box3D. Trigger: pushes to this feature branch affecting PHYSIS/test paths, compatible PRs, or manual workflow_dispatch. It runs targeted CMake builds and `ctest -R '^physis_'`.

**Actual execution: NOT RUN / no PASS evidence.** The connected GitHub commit workflow query did not yet show PR-triggered runs; direct local repository clone/build is blocked by DNS resolution for github.com. Therefore no compiler or real-time Box3D result can honestly be claimed.

To verify independently:
```sh
./run-tests.sh --filter '^physis_'
cmake -S . -B build/physis-physics -DCMAKE_BUILD_TYPE=Debug -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_ENABLE_PHYSICS=ON
cmake --build build/physis-physics --target aiverse_physis_box3d_test -j4
ctest --test-dir build/physis-physics -R '^physis_box3d$' --output-on-failure
```

## Next concrete steps
1. Check workflow runs/status in GitHub UI for the new `PHYSIS nature + Box3D integration checks` workflow; inspect first compiler error if failed. Fix and record exact PASS/FAIL logs in a later handoff.
2. Complete the optional consolidated `sampleEnvironment3D` read-only PHYSIS consumer API for independent future Terra/VFX/Physics adapters (do not depend on Terra/NEREID classes).
3. Add one cross-module opt-in adapter only after separate Water PR #23 and Terra PR #24 are reviewed and APIs verified, in a distinct integration branch. Keep canonical water authority as NEREID and PHYSIS weather/gravity as PHYSIS.
4. If adding more native physics coupling, test zero-G, no double gravity, tornado lift, real Box3D contacts, snapshots and host order; do not equate Box3D float solver to fixed deterministic lockstep.
5. Every new feature code commit must be followed by a separate agent handoff documenting SHAs, tests ACTUALLY RUN (or NOT RUN), exact next owner and limitations.
