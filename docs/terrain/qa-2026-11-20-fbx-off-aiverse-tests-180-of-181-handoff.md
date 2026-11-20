# TerraWeave Stage70 — full native CTest 180/181 and optional FBX-off test fix

**Date:** 2026-11-20.
**Branch:** `integration/terraweave-physis-stage70-20261120`.
**Draft PR:** [#53](https://github.com/chromealex/AIVERSE/pull/53).
**Code commit:** `92758e07a8edbb6d14f47787b3729f6cff1390a1`.
**Files changed:** `CMakeLists.txt` and `tests/tests.cpp`.
No runtime/ECS/PHYSIS/NEREID/Box3D/TerraWeave implementation changed.

## Native Mac Studio acceptance reported by user

The user supplied a complete concluding section of a Mac CTest log
from `~/Projects/AIVERSE-terraweave-stage70-local-qa`, showing:

```text
181/181 Test #110: place ........................ Passed 27.70 sec
99% tests passed, 1 tests failed out of 181
Total Test time (real) = 536.38 sec
The following tests FAILED:
  34 - aiverse_tests (Subprocess aborted)
```

All **dedicated** TerraWeave tests, including
`terraweave_micro_nutrient`, `terraweave_native_micro_compile`,
`terraweave_native_micro_physics_compile`, Arbor, Flora, Model,
Heightmap, Ops, Preview, Forest Scene, Horizon and Editor, passed.
PHYSIS, physics and Place CTests reported PASS as registered tests.
This is real user-provided CTest output, NOT a verified full PASS,
and it does NOT establish standalone Metal/Vulkan/GLES GPU parity or
comprehensive live cross-owner acceptance.
The user's excerpt does not include `git rev-parse HEAD` or
`CMakeCache.txt` provenance, so exact tested SHA/config cannot be
proven from the excerpt alone.

The **only CTest failure** was monolithic `aiverse_tests`.
The log includes an uncaught runtime exception:

```text
tests/data/models/cube.fbx: FBX import is disabled in this build
(AIVERSE_FBX=OFF); convert to .glb
```

Source inspection located the unconditional FBX fixture in
`tests/tests.cpp` at `TEST(models_fbx_skinned_cube)`. The existing
one-pass integration acceptance script intentionally sets
`-DAIVERSE_FBX=OFF`. `aiverse_models` correctly compiles without
FBX and issues the documented diagnostic, but `aiverse_tests`
did not receive the private `AIVERSE_NO_FBX` compile definition.
This mismatch meant the test attempted to decode `cube.fbx` anyway.

## Implementation and compatibility

- `CMakeLists.txt`: when `NOT AIVERSE_FBX`, define
  `AIVERSE_NO_FBX=1` only on `aiverse_tests`, matching the
  existing private feature flag of `aiverse_models`.
- `tests/tests.cpp`: FBX-on branches keep both existing full
  skin/material/animation regression fixtures
  (`models_fbx_skinned_cube` and `models_fbx_readable_errors`).
- In the FBX-off build, register instead
  `models_fbx_disabled_gives_actionable_error`. It attempts the
  same `cube.fbx` load and **requires** the model loader to throw a
  readable path-anchored error that contains `AIVERSE_FBX=OFF` and
  `convert to .glb`.
- No global skipping of `aiverse_tests`, no silencing exceptions, no
  disabling of `-Werror` and no weakening of Place's 5ms tick gate.
  Legitimate FBX-on importer geometry/animation assertions are preserved.
- Save/replay versions and all public engine APIs remain unchanged.

## Verification performed and pending

**Performed:** reviewed exact failing native CTest output, examined
runtime `Model.cpp` FBX-off diagnostic and CMake compile definitions;
committed CMake/test changes through connected GitHub API; independently
read back source around both changed locations.

**NOT RUN since code commit:** local CMake configure, C++ compile,
focused `aiverse_tests`, complete 181-test CTest, FBX-on test mode,
Metal/Vulkan/GLES hardware checks. A source commit is not test success.

## Next agent / local QA commands

On a clean isolated worktree, not other agents' working trees:

```bash
cd ~/Projects/AIVERSE-terraweave-stage70-local-qa
git status --short
git fetch origin integration/terraweave-physis-stage70-20261120
git switch --detach origin/integration/terraweave-physis-stage70-20261120
git rev-parse HEAD

cmake -S . -B build/terraweave-main-acceptance \
  -DAIVERSE_FBX=OFF
cmake --build build/terraweave-main-acceptance \
  --target aiverse_tests --parallel 4 &&
ctest --test-dir build/terraweave-main-acceptance \
  --output-on-failure -R '^aiverse_tests$'
```

The existing build directory must already have been configured by the
full `tools/terraweave-main-integration-acceptance.sh` script with
all other native options. If not, rerun the full script to configure
the integration build correctly. Do NOT reset/delete local changes.

After the focused test is green, rerun the original one-command
`bash tools/terraweave-main-integration-acceptance.sh`
and record the exact full CTest summary, git SHA and CMakeCache flags.
Then add an independent **FBX=ON** fixture test run (different build
folder) to validate the original skin/animation path. Report each
actual PASS/FAIL, not a predicted outcome.

This stage does not authorize merging Draft PR #53: coupled
PHYSIS/NEREID water nutrients, save/rollback, GPU backend parity and
main divergence still have separate acceptance gates.
