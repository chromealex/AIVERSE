# Stage 100 — CROWD × PHYSIS ground-support eligibility
Date: 2026-11-24. Branch: feature/world-processes-stage31-bidirectional-handles.
Base cb2fe43b; plan 0a3f55cd; implementation 1cf7562c0176923cdcd05650da7d7f7a62c464aa.

- SurfaceGravitySupport.h: Fixed-point classification using PHYSIS gravityAt and Surface faceNormal, clearance and body/cell coordinates. No gravity/physics solver or state.
- CrowdGameCoreSurface.h: optional support predicate on agent and slot, existing path-layer passability and same-body filtering preserved. Existing movement orders and authoritative Transform untouched.
- WorldProcessesSuite.h, WorldProcessesMapHydration.h and WorldProcessesNativeModule.h: explicit crowdGravitySupport boolean, installer validation, same system name and persistence, no duplicate registration. Planet demo rules enable this option.
- tests/world_processes_gamecore_native_test.cpp: pure classification, six face sampling, body isolation, reverse gravity and COW restoration; authored native demo disables/re-enables PHYSIS fields and asserts reservation release/reacquisition.
- Committed source readback confirmed new option and test calls. Tests, compilation, platform and performance checks NOT RUN.

Limit: GameCore::moveSphere is a kinematic projection owner. This stage does not suspend active movement, integrate velocity, detach unsupported characters, simulate zero-g flight or resolve collisions. Future owner integration must consume the shared support policy and PHYSIS acceleration in the canonical locomotion/physics system, with no competing Transform writers. No new schema or editor operation. Planar behavior remains unchanged. Stage98/99 TerraWeave owner dependencies remain open.

Next Stage101: inspect existing render/physics integration and implement a bounded owner-compatible adapter, explicitly identify blocked acceptance. No main sync/merge.
