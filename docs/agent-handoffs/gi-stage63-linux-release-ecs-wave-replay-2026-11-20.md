# AIVERSE portable GI — Stage 63: actual Release CI blocker in ECS wave replay (2026-11-20)

## Scope, branch and provenance

- Repository: `chromealex/AIVERSE`.
- Working branch: `feature/gi-bvh-precision-stage62-20261120` (an isolated child of `feature/gi-lighting-portable-backends-20261119`); GI Stage62 is already committed on this branch. Main and GI parent remain unchanged; PR #57 is DRAFT.
- Source fix commit: `33526ca1222651dd161083e6b63c1edc68fe073b`.
- Changed file: `tests/ecs_wave_replay_test.cpp` (ECS test harness only). **No production GI, renderer, ECS runtime or backend code was modified.**

## Actual CI evidence that caused the fix

- GitHub Actions **CI** run `44695330522`, **Linux arm64 gcc** job `136150631963`, on GI parent head `3e364b79f681feb4d159af5135b54b4b9326d609`.
- Verified result: **FAILURE in Build**, on object **892/1367**, `tests/ecs_wave_replay_test.cpp.o`; `g++ -O3 -DNDEBUG -std=c++20 -Wall -Wextra -Werror`.
- Real diagnostics: line 84 `originalChecksum` and line 86 `sequential` declared but unused under Release because the only consumers were standard `assert(expr)` calls; `-Werror=unused-variable`.
- All later steps, including CTest and Vulkan lavapipe, were **SKIPPED**, not failed GPU tests. This failure is unrelated to GI shader output.
- The previous Stage61 fix correctly converted standard asserts in four other ECS tests, and those four objects compiled in this run. The wave replay test was a NEW blocker encountered further along the same full build.
- Log evidence is available through GitHub Actions at `https://github.com/chromealex/AIVERSE/actions/runs/44695330522/job/136150631963`.

## Implementation

- Replace `#include <cassert>` with the repo's existing `#include "tests/AlwaysCheck.h"`.
- Convert all four checks (`fingerprint(original)==originalChecksum` plus three 0/3/7-worker deterministic replay comparisons) to `AIVERSE_TEST_CHECK`.
- This preserves verification AND evaluation of the comparisons in **Debug and Release**. Do not make tests compile by suppressing warnings or casting away unused values: assertions must execute under `NDEBUG`.
- Retain independent ECS-agent ownership of production systems. The repair changes test assertions only.

## Verification — do not inflate status

- **Observed FAIL:** real Linux arm64 GCC Release build above on the old GI-parent SHA.
- **Performed:** exact compiler log inspected; GitHub branch file read and update confirmed; new test source SHA committed.
- **NOT RUN:** rebuild on the fixed SHA, CTest `ecs_wave_replay_test`, GI CTest, real Vulkan compute/lavapipe, Metal/GLES shader checks or full suite. PR/CI status after this source commit must be checked separately; the previous failed CI is not a test of this fix.

## Required next action

1. Let native CI/owner build proceed from the fixed head; inspect **the next actual GCC/Clang failure** rather than assuming tests now pass. Determine exact registered `ecs_wave_replay_test` target from current CMake before invoking.
2. Once all compile blockers are resolved, run the user's one consolidated full Release+Debug CTest and GI targets (`gi_gpu_bvh_validation`, `gi_gpu_paged_bvh`, `gi_gpu_page_compaction`, `gi_gpu_dynamic_runtime`, `vulkan_backend` with lavapipe and validation).
3. Accept Stage62 float-ULP BVH fix only after compiled test, then handle real Metal/Vulkan/GLES pixel and resource lifetime validation; no premature merge into `main`.
4. Every further bounded stage requires its own separate handoff with real tested SHA/command/result/limitations. Keep other developers' concurrent branches untouched.
