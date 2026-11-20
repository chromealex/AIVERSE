# TerraWeave QA — fix optional Box3D native header smoke (2026-11-19)

**Branch:** `feature/terraweave`; **PR #24:** Draft against `main`.
**Scope:** QA/blocker fixes only, no Stage71 gameplay changes.

## Exact reproduced external diagnostic (Mac Studio user log)

The user ran a full build under
`~/Projects/AIVERSE-terraweave-qa-fix/build/terraweave-final-qa`
and reported the first build blocker:

```text
In file included from tests/terraweave_native_micro_compile_test.cpp:7:
In file included from examples/world/terraweave_native/PhysicsIntegration.h:6:
In file included from engine/world/TerraWeaveNereidObstacleBridge.h:7:
In file included from engine/world/TerraWeaveFallenPhysics.h:8:
engine/physics/Scene.h:5:10: fatal error: 'box3d/box3d.h' file not found
```

Followed by make exit code 2. Another nonfatal diagnostic:

```text
tests/terraweave_model_test.cpp:114:27: warning:
comparison of integers of different signs: 'const int' and 'size_t'
assert(p.material == layer);
```

The compile error originates from a **new test** that unconditionally
included the optional Box3D physics adapter while linking only
`aiverse_models`, `aiverse_world`, `aiverse_zstd_dec`.
Top-level CMake declares
`option(AIVERSE_ENABLE_PHYSICS "... Box3D ..." OFF)`. Only when enabled,
`engine/physics/CMakeLists.txt` adds vendored `third_party/box3d`
and exports `AIVERSE::physics` with a PUBLIC link to `box3d::box3d`.
That target is the owner of public `<box3d/...>` includes.
The engine physics module and Box3D library were **not** defective
based on this evidence; the smoke-test CMake/header boundary was.

## Committed source changes

1. `c6d9d5d77869f82a220614ba395c31888db4f6b5` —
   `tests/terraweave_native_micro_compile_test.cpp` now imports
   `examples/world/terraweave_native/PhysicsIntegration.h`
   only with `AIVERSE_TERRAWEAVE_SMOKE_WITH_PHYSICS` defined.
   The base test still compiles the native ECS, NEREID, Stage70 micro
   nutrient and optional PHYSIS weather bridge headers, but does not
   require Box3D in normal engine builds with physics OFF.
2. `e061226d7e1687c1eeee16cdfc5737c305f4fdff` —
   `CMakeLists.txt` retains the base
   `aiverse_terraweave_native_micro_compile_test` and registers
   a separate `aiverse_terraweave_native_micro_physics_compile_test`
   **only when `TARGET aiverse_physics` exists**.
   The latter defines `AIVERSE_TERRAWEAVE_SMOKE_WITH_PHYSICS=1`
   and links `AIVERSE::physics`, receiving the correct vendored
   Box3D include paths and linked implementation through its PUBLIC
   CMake dependencies. Its CTest name is
   `terraweave_native_micro_physics_compile`.
   No hardcoded third_party include path or fake Box3D shim.
3. `9919b56f1ee1339fd80f8c9f217f4b3d9dca3fa5` —
   `tests/terraweave_model_test.cpp`: safely compare
   `p.material` to `static_cast<int>(layer)`, because
   `layer` is bounded to 0..1. Removes a Clang
   `-Wsign-compare` warning without changing expected behavior.
4. `fbab34116618ac88c3a90c12c90a6cc598f2bfc1` —
   `.github/workflows/terraweave-full-qa.yml`: enable
   `AIVERSE_ENABLE_PHYSICS=ON` and disable the optional
   interactive physics demo for the full Linux/macOS CI build.
   It now compiles/runs both the base and the physics-enabled smoke
   as part of the complete CTest inventory.
   The existing isolated GCC/Clang workflow keeps physics OFF,
   specifically validating the optional-dependency boundary.

## What was actually verified

- Inspected the exact CMake option and
  `engine/physics/CMakeLists.txt`: target
  `AIVERSE::physics` transitively links `box3d::box3d`.
- Inspected `engine/physics/Scene.h`: it directly includes
  `<box3d/box3d.h>`, explaining the compiler diagnostic.
- Inspected the vendored `third_party/box3d` directory:
  includes `CMakeLists.txt`, `include`, `src`.
- GitHub accepted all four commits and verified the full QA
  workflow contains the two needed CMake flags.
- GitHub Actions queued an isolated GCC/Clang push run
  [#44600630153](https://github.com/chromealex/AIVERSE/actions/runs/44600630153)
  at SHA `9919b56f1ee1339fd80f8c9f217f4b3d9dca3fa5`.
- GitHub Actions queued a new full Linux/macOS native run
  [#44600667456](https://github.com/chromealex/AIVERSE/actions/runs/44600667456)
  at SHA `fbab34116618ac88c3a90c12c90a6cc598f2bfc1`.
- **No post-fix Clang/GCC compilation, CTest, Box3D step,
  runtime scene validation, PHYSIS+NEREID integration, Metal GPU
  capture or performance result has been observed in this turn.**
  A queued Actions run is not a passing run. Do not claim otherwise.

## Mac Studio commands: fetch before every build, no checkout contamination

Use the existing isolated QA worktree named in the user's error:

```bash
cd ~/Projects/AIVERSE-terraweave-qa-fix
git status --short
git fetch origin feature/terraweave
# If detached QA worktree, explicitly update to newest remote head:
git switch --detach origin/feature/terraweave

cmake -S . -B build/terraweave-final-qa \
  -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_ENABLE_PHYSICS=ON \
  -DAIVERSE_BUILD_PHYSICS_DEMO=OFF

# Fast isolated first error and with Box3D public dependency:
cmake --build build/terraweave-final-qa --parallel 4 \
  --target aiverse_terraweave_native_micro_compile_test \
           aiverse_terraweave_native_micro_physics_compile_test \
           aiverse_terraweave_model_test

ctest --test-dir build/terraweave-final-qa --output-on-failure \
  -R '^(terraweave_native_micro_compile|terraweave_native_micro_physics_compile|terraweave_model)$'

# Only after the smoke/physics/link passes:
set -euo pipefail
cmake --build build/terraweave-final-qa --parallel 4 \
  2>&1 | tee build/terraweave-final-qa/build-all.log
ctest --test-dir build/terraweave-final-qa --output-on-failure --parallel 2 \
  2>&1 | tee build/terraweave-final-qa/ctest-full.log
```

**Important:** `git switch --detach` is appropriate only for a clean
isolated worktree. If `git status --short` shows uncommitted user/agent
changes, do NOT switch or overwrite; create a fresh clean worktree at
the fetched remote SHA instead. An existing unrelated
`AIVERSE-static-scene` checkout must not be changed.

Alternatively, to verify the **physics-off** header dependency:

```bash
cmake -S . -B build/terraweave-no-physics \
  -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_ENABLE_PHYSICS=OFF
cmake --build build/terraweave-no-physics --parallel 4 \
  --target aiverse_terraweave_native_micro_compile_test
ctest --test-dir build/terraweave-no-physics --output-on-failure \
  -R '^terraweave_native_micro_compile$'
```

## Remaining integration and QA gates

1. Confirm both ON and OFF configurations compile and link on Mac
   and CI; if other header/API errors emerge, fix the **first actual
   compiler error** and record its source and commit.
2. Verify full CTest only after ALL registered native targets
   build; otherwise dozens of `Not Run` results from missing
   executables obscure the first actionable failure.
3. Run real `physics::Scene` Box3D vegetation/terrain collision and
   NEREID erosion/root-fall scenes at the physics owner barrier.
4. Exercise Stage70 saved 4m nutrients, v1/v2→v3 migration, rollback,
   PHYSIS direct climate integration in compatible branch, Renderer3D
   and Arbor/Flora near/far quality/Metal captures.
5. PR #24 remains Draft. Reconcile diverged `main` only after
   real CI, native, physics and graphics QA.

**Next agent:** read this QA handoff first; use actual CI job logs or
the Mac Studio post-fix compile command above. Never convert a
source-level diagnosis or a queued check into a passing test claim.
