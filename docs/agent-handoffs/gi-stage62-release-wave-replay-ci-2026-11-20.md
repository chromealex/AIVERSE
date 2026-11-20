# AIVERSE GI — Stage 62: unblock Release CI wave-replay test (2026-11-20)

## Branch / evidence

- Repository: `chromealex/AIVERSE`.
- Integration branch: `feature/gi-lighting-portable-backends-20261119` (PR #47, DRAFT targeting `feature/hybrid-dynamic-gi`, **not main**).
- Examined actual GitHub Actions CI run [44695330522](https://github.com/chromealex/AIVERSE/actions/runs/44695330522), Linux arm64 gcc job `136150631963`, tested SHA `3e364b79f681feb4d159af5135b54b4b9326d609`.
- **Observed FAIL:** Release build stopped at `[892/1367]` on `tests/ecs_wave_replay_test.cpp`: `originalChecksum` (line 84) and `sequential` (line 86) were unused under `-O3 -DNDEBUG -Werror`. Its four `assert(...)` expressions were compiled out. CTest, Vulkan lavapipe, screenshot steps were **SKIPPED** because Build failed.
- Source-fix commit: `01cafe0c522a74d6a14971eb7279277cd9f12b1d` — `tests/ecs_wave_replay_test.cpp`. Replace `<cassert>` with the existing `tests/AlwaysCheck.h`; replace four assertions with `AIVERSE_TEST_CHECK`.
- No production renderer/GI/Vulkan/Metal/GLES code, ECS runtime, gameplay or persistence schema was modified. The regression now evaluates its replay comparisons exactly once in both Debug and Release and throws on failure rather than reporting a false PASS when `NDEBUG` removes checks.

## Verification truth

- **PERFORMED:** connected GitHub read of the complete failing job log, inspection of the exact test and existing `AlwaysCheck.h`, successful GitHub source update acknowledgement.
- **NOT RUN:** native C++ compile or execution after the fix, CTest (including `ecs_wave_replay`), shader compilation, Vulkan lavapipe, Metal or GLES render/pixel tests, performance benchmarks or full AIVERSE acceptance.
- The failure is a cross-cutting test harness blocker surfaced by GI's branch CI. It does **not** establish that the GI implementation itself passed or failed GPU tests.

## Next action

1. Watch the new head's Linux arm64 GCC Release job. Confirm `ecs_wave_replay_test.cpp` builds cleanly under `-DNDEBUG -Werror`; inspect logs for any further harness/production compiler errors and address each with a separate source and handoff commit.
2. Once Build passes, execute the narrow `ecs_wave_replay` and GI `gi_gpu_bvh_validation|gi_gpu_paged_bvh|gi_gpu_page_compaction|gi_gpu_dynamic_runtime|gi_gpu_ticket_queue|gi_gpu_sparse_patch` CTests; run `vulkan_backend` on lavapipe with validation if available.
3. Reconcile GI (still diverged from current `main`) and separately developed LumaWeave, PHYSIS and TerraWeave changes in a controlled integration worktree **without merging untested code into main**. Preserve direct-light vs bounced-light ownership, fence-safe atlas borrows, CPU/GLES fallback and GI disabled-by-default behavior.
4. Run the final native Metal / Vulkan / GLES compile and GPU visual/performance acceptance against one exact SHA, document every actual command/result, only then promote to main.
