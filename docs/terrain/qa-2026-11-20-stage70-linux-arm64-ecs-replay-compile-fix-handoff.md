# TerraWeave Stage70 merged-main QA — Linux ARM64 ECS replay build fix

**Date:** 2026-11-20
**Branch:** `integration/terraweave-physis-stage70-20261120`
**Draft PR:** [#53](https://github.com/chromealex/AIVERSE/pull/53)
**Baseline tested by failing CI:** `d4303cafc8616a8ecec77cdb4d90bbac097124f0`

## Actual CI evidence: compile FAILED

GitHub Actions [CI run #44626522408](https://github.com/chromealex/AIVERSE/actions/runs/44626522408),
job `Linux arm64 gcc` / job ID `135921713533`:

- `actions/checkout`, dependencies, CMake configure: **success**.
- Build: **FAILED** at 901/1399 compile steps, in
  `tests/ecs_wave_replay_test.cpp`.
- Exact compiler mode (from job log): `g++ -O3 -DNDEBUG -std=c++20
  -Wall -Wextra -Wpedantic -Werror` (plus existing project warning flags).
- First errors:

  ```text
  tests/ecs_wave_replay_test.cpp:84:16: error: unused variable 'originalChecksum' [-Werror=unused-variable]
  tests/ecs_wave_replay_test.cpp:86:20: error: unused variable 'sequential' [-Werror=unused-variable]
  cc1plus: all warnings being treated as errors
  ninja: build stopped: subcommand failed
  ```

- The compiled test used `assert()` solely to consume these two values.
  Under `NDEBUG` the standard macro removes all four parity
  expressions, so not just the variables but the checks themselves
  disappeared. This is a preexisting generic experimental ECS test,
  **not** evidence that TerraWeave Stage70 computation failed.
- Other Linux/macOS/PHYSIS/TerraWeave jobs were queued at this
  inspection. No complete merged CTest pass exists on this SHA.

## Fix committed to isolated integration branch

- Code commit `451a3db84f756769c2889465aaa7ec7ede73909a`
- Changed file `tests/ecs_wave_replay_test.cpp` ONLY.
- Replaced four `assert(...)` with always-executed `CHECK(...)`,
  matching the repository's adjacent
  `tests/ecs_wave_executor_test.cpp` convention.
  Adds `<cstdlib>` and a small diagnostic CHECK macro that prints
  file/line/failed expression to `std::cerr` then calls
  `std::abort()` if a check fails.
- Original snapshot checksum and deterministic replay comparison
  across 0, 3 and 7 workers remain the same four actual checks,
  now active in both Debug and Release builds. Importantly, the
  three replay evaluations occur even under `-DNDEBUG`.
- Does **not** disable `-Werror`, `NDEBUG`, test targets,
  strict Place 5ms budget, concurrency, ECS policies or release flags.
- No change to ECS runtime, TerraWeave APIs, NEREID/PHYSIS/Box3D,
  GPU renderer, save format or replication wire protocol.

## Verification status — important distinction

**ACTUALLY DONE:**
- Read the full GitHub Actions failing job log (1,317 lines);
  located the first compiler error with its exact command.
- Read the file that failed and the adjacent existing
  `ecs_wave_executor_test.cpp` always-on `CHECK` precedent.
- Source fix committed through GitHub; fetched source back and
  verified the macro's two backslash continuations were intact.
- Terminal `git ls-remote` attempted in the authoring container;
  failed with `Could not resolve host: github.com`, so native
  repository checkout was not possible.

**NOT RUN AFTER FIX:** compiler/CMake, focused replay test,
  TerraWeave unit tests, unfiltered native CTest, PHYSIS/Box3D
  runtime integration, GPU Metal/Vulkan/GLES.
The old failing job does not validate the new commit. GitHub
Actions may cancel superseded runs after the source/handoff pushes.

## Mandatory next stage / acceptance criteria

1. Read the newest exact-head PR #53 jobs after this handoff commit.
   Find the first new failure from GCC/Clang/macOS; do not speculate.
2. Confirm `aiverse_ecs_wave_replay_test` compiles and executes
   its parity comparisons **with `-DNDEBUG -Werror`**.
3. Run all Stage70 `terraweave_micro_nutrient`,
   `terraweave_native_micro_compile`, physics smoke and full
   unfiltered CTest. Preserve Place's original 5ms gate.
4. Confirm PHYSIS/NEREID body-tagged 4m nutrient live world
   confluence, v1/v2->v3 save migration, rollback and conservation.
5. Validate Metal/Vulkan/GLES renderer paths and main divergence
   before promoting Draft PR #53. Every subsequent stage must
   commit its own independent implementation and handoff.

This is a **source repair in response to real failed CI**, not
a post-fix build pass.
