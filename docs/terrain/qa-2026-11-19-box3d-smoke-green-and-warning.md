# TerraWeave QA — native Box3D smoke 3/3 green; obstacle precedence warning fix

Date: 2026-11-19. Branch `feature/terraweave`, Draft PR #24.
This is a QA correction on existing Stage70 source, not Stage71.

## What the Mac Studio actually proved

The user supplied an AppleClang build/CTest excerpt from
`~/Projects/AIVERSE-terraweave-qa-fix/build/terraweave-final-qa`.
It explicitly recorded:

```text
[100%] Built target aiverse_physics
[100%] Built target aiverse_terraweave_native_micro_physics_compile_test
[100%] Built target aiverse_terraweave_model_test
1/3 Test #118: terraweave_native_micro_compile ........... Passed 0.48 sec
2/3 Test #119: terraweave_native_micro_physics_compile ... Passed 0.27 sec
3/3 Test #122: terraweave_model .......................... Passed 0.73 sec
100% tests passed, 0 tests failed out of 3
Total Test time (real) = 1.49 sec
```

These are **real user-supplied local results**, showing optional
Box3D CMake header/link handling now works on that checkout and
all three focused tests passed. They do NOT prove the full 131-test
CTest, true physics body simulation, NEREID live topology, Metal
rendering, PHYSIS direct sampling, or all-platform integration.
The log did not include `git rev-parse HEAD`, so the exact tested
checkout SHA cannot be inferred; it clearly contains the two
optional base/physics smoke targets.

## Exact final source changes

- `16ff845ae77d6fb5d324c4b567c861fe1fe1b903` —
  `engine/world/TerraWeaveNereidObstacleBridge.h`:
  explicitly parenthesize
  `(!c.maxFallenTrees && !c.maxSlopeDebris)` inside the
  existing `||` condition in
  `validateFlowObstructionCapture`.
  Eliminates the AppleClang `-Wlogical-op-parentheses` warning
  from `TerraWeaveNereidObstacleBridge.h:35` without changing
  C++ boolean precedence or runtime logic.
- `e4b14d372d9ecdea6e44727cbe6eaa0defe9675a` —
  `tests/terraweave_native_micro_compile_test.cpp`:
  when `AIVERSE_TERRAWEAVE_SMOKE_WITH_PHYSICS` is defined,
  validate the default obstruction capture policy, verify
  `maxFallenTrees=0,maxSlopeDebris=0` rejects, verify
  `maxFallenTrees=0,maxSlopeDebris=1` accepts and
  `maxFallenTrees=129` rejects. No Box3D Scene step is
  required by the smoke.
- Both were committed via GitHub source edits and readback,
  but **the updated source was not yet built or tested** by
  the user after these two commits. The earlier 3/3 green
  result applies to the previous source without this new
  warning fix/regression check.

## What comes next

Use a clean isolated worktree with current `origin/feature/terraweave`.
Run the focused smoke test, then full build and CTest in
**build-first** order with `pipefail`, never letting
CTest start after a failed build:

```bash
cd ~/Projects/AIVERSE-terraweave-qa-fix
git status --short
git fetch origin feature/terraweave
git switch --detach origin/feature/terraweave
cmake -S . -B build/terraweave-final-qa \
  -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_ENABLE_PHYSICS=ON \
  -DAIVERSE_BUILD_PHYSICS_DEMO=OFF
set -euo pipefail
cmake --build build/terraweave-final-qa --parallel 4 \
  --target aiverse_terraweave_native_micro_compile_test \
           aiverse_terraweave_native_micro_physics_compile_test \
           aiverse_terraweave_model_test
ctest --test-dir build/terraweave-final-qa --output-on-failure \
  -R '^(terraweave_native_micro(_physics)?_compile|terraweave_model)$'
cmake --build build/terraweave-final-qa --parallel 4 \
  2>&1 | tee build/terraweave-final-qa/build-all.log
ctest --test-dir build/terraweave-final-qa --output-on-failure --parallel 2 \
  2>&1 | tee build/terraweave-final-qa/ctest-full.log
```

If `git status --short` reports uncommitted edits in that worktree,
DO NOT switch forcibly or overwrite user/agent work; create another
clean detached worktree from the fetched feature branch instead.

Native physics and graphics owner barriers, authoritative NEREID
erosion and Box3D falling-tree scene, PHYSIS climate adapter in an
integrated checkout, persistence/rollback and full-suite green QA
remain outstanding. Do not merge Draft PR #24 into diverged main
until their actual results are verified. Append new QA evidence
and exact SHA to a follow-up handoff after each correction.
