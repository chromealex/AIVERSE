# STRATA Stage 19.9 — follow-up Linux ARM64 GCC Release tests (2026-11-20)

## Branch and exact starting SHA
- Repo `chromealex/AIVERSE`, PR #70, integration `integration/strata-main-stage19`.
- Starting HEAD before this stage: `bb203014348e35dbd930fb6d8fc543acc234b9fe`; tree `9cf00a0cbf9aadbb0c2a5732a4528481accd9135`.
- PR remains Draft. Do not merge into main or force push. Previous source fix and validation: Stage 19.7 (Mac) and Stage 19.8 (first ARM64 CI failure).

## Actual CI evidence
- [Linux ARM64 GCC job #136254775218](https://github.com/chromealex/AIVERSE/actions/runs/44726387824/job/136254775218) on the Stage19.8 HEAD failed in **Build** at 2026-11-20T11:30Z. GCC `-O3 -DNDEBUG -Werror` reported `-Werror=unused-but-set-variable` in `tests/render/retirement_pressure.cpp`, and `-Werror=unused-variable` in `tests/render/model_resource_provider_contract.cpp`. Since `assert` disappears with `NDEBUG`, the tests also silently lose their validation and side effects. Not a demonstrated Vulkan/Metal/STRATA production runtime bug.

## Code changes
- Convert ordinary `assert(...)` into existing always-on `AIVERSE_TEST_CHECK(...)` in 17 CPU/native render regression sources. Replace `<cassert>` with `../AlwaysCheck.h`. Preserve `static_assert` unchanged and keep all other fixture source statements, ordering, and code paths unchanged.
- 245 checks now execute exactly once in both Debug and Release and throw a test failure on violation. This prevents both GCC unused-variables errors in these sources and false-positive Release tests.
- Individual changed sources:
- `tests/render/retirement_pressure.cpp`: 30 checks
- `tests/render/model_resource_provider_contract.cpp`: 10 checks
- `tests/render/retirement_status_replay.cpp`: 21 checks
- `tests/render/resource_owner_scope.cpp`: 14 checks
- `tests/render/gpu_program_retirement.cpp`: 19 checks
- `tests/render/model_estimate.cpp`: 5 checks
- `tests/render/model_owner_estimates.cpp`: 7 checks
- `tests/render/resource_owner_lifetime.cpp`: 12 checks
- `tests/render/resource_owner_group.cpp`: 16 checks
- `tests/render/async_model_cpu_queue.cpp`: 16 checks
- `tests/render/retirement_diagnostic_ring.cpp`: 14 checks
- `tests/render/model_priority.cpp`: 13 checks
- `tests/render/geometry_page_owners.cpp`: 4 checks
- `tests/render/model_parallel_jobs.cpp`: 19 checks
- `tests/render/persistent_model_pool.cpp`: 17 checks
- `tests/render/model_asset_provider_api.cpp`: 4 checks
- `tests/render/retirement_failed_token.cpp`: 24 checks

- No production C++, Metal/Vulkan/GLES, GPU parity gates, global `NDEBUG`, CMake compiler flags or `-Werror` changes.

## Validation truth / limits
- ACTUAL: read current GitHub sources; checked presence of a single `<cassert>` include and ordinary assertions; mechanically migrated only these expressions; created one GitHub tree and commit with guarded fast-forward update of PR source branch.
- **NOT RUN on new HEAD**: Linux ARM64 GCC build, full CI, Mac build, CTest, GPU, Vulkan Lavapipe and sanitizers. Do not call this fix CI-PASS until checks finish.
- This closes the *known failures and neighboring legacy assertions*, not necessarily every possible `assert`-only source in the entire repository.

## Next agent
1. On exact new PR #70 head, inspect check-runs and obtain full job logs for failing jobs. Confirm both Linux ARM64 test files now compile. If another test has disabled Release assertions, prefer `AIVERSE_TEST_CHECK` rather than suppressing warnings.
2. Run/review STRATA Vulkan Lavapipe CI and all platform workflows; retain native Vulkan support enabled for actual device acceptance later.
3. Check current main; existing PR #76 has two independent always-on test fixes already mirrored in Stage19.8. Reconcile only after CI and new main changes are reviewed, without force push.
4. Append precise passing/failing CI run IDs, SHA and known gaps in next independent handoff. Keep PR #70 Draft until acceptance.
