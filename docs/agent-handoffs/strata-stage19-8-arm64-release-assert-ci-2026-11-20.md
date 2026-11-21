# STRATA Stage 19.8 — Linux ARM64 Release assertion CI fix (2026-11-20)

## Ownership / context
- Repo: `chromealex/AIVERSE`
- Integration branch: `integration/strata-main-stage19`, PR #70 (Draft).
- Previously tested HEAD: `32c6a31f495595f6614780bb9b00c65b262d980e`. Owner Mac native build, 276/276 CTests, six actual Metal fixtures and six assert-enabled retirement fixtures PASS (evidence in Stage 19.7 handoff).
- First-party `main` advanced two commits via PR #76, to `ad175f624ef6f25e7c6dc19cabe199cb30623201`. This repairs precisely the same two Release regression sources. No direct `main` write is performed from this STRATA stage.

## Verified failure from GitHub Actions
- Failing workflow: [CI run #44720131837](https://github.com/chromealex/AIVERSE/actions/runs/44720131837), exact failing job [Linux arm64 gcc #136233460496](https://github.com/chromealex/AIVERSE/actions/runs/44720131837/job/136233460496), started 2026-11-20T10:33Z; failed in **Build** around 10:46Z; CMake configure and dependency steps passed, Tests were skipped.
- Build command uses `/usr/bin/g++ -O3 -DNDEBUG ... -Werror`. Because `<cassert>` `assert(...)` is compiled out, local test callbacks `fence`, `dispose`, `begin`, `done`, `close`, `wait` become unused, triggering GCC `-Werror=unused-but-set-variable`.
- Failure occurs in `tests/render/resource_retirement.cpp` and `tests/render/async_resource_retirement.cpp`; not evidence of production RHI/Vulkan incompatibility.

## Actual code changes and commits
- `fcb7ec5c84e97c959c52fcbc5884522f65145b91`: `tests/render/resource_retirement.cpp`, replace include `<cassert>` with `../AlwaysCheck.h`, replace 14 `assert(...)` with `AIVERSE_TEST_CHECK(...)`.
- `7969e370b7f52a43922d49329126db5376b60e51`: `tests/render/async_resource_retirement.cpp`, replace include `<cassert>` with `../AlwaysCheck.h`, replace 24 `assert(...)` with `AIVERSE_TEST_CHECK(...)`.
- The shared `tests/AlwaysCheck.h` already existed on integration and evaluates the expression exactly once via `test_support::require`, throwing on failed assertion with expression/file/line.
- Both source updates were byte-for-byte checked against the independently merged `main` PR #76 versions, with optimistic blob-SHA writes. Do **not** weaken `-Werror`, change global `NDEBUG`, or disable Vulkan to clear this.
- GPU production sources, STRATA gates, Metal, Vulkan and GLES source unchanged in this stage.

## Test evidence and limitations
- **Actual:** GitHub Actions original ARM64 raw job log inspected; failure lines identified; both corrected source files compared with `main` and committed via GitHub.
- **NOT RUN on these new commits:** Linux ARM64 GCC native build, GitHub CI result, CTest, Vulkan Lavapipe. CI re-runs on branch update have to finish before claiming this failure resolved at runtime.
- Mac Studio hardware acceptance from Stage19.7 was on the earlier integrated tree; it cannot prove this new HEAD. This update changes only two test sources.

## Next agent
1. Inspect GitHub Actions of the latest PR #70 head; specifically Linux ARM64 GCC and STRATA Vulkan Lavapipe. If ARM64 fails again, obtain the **full job log** with GitHub workflow job logs action before deciding the fix.
2. Acknowledge the latest `main` PR #76 commits; reconcile ancestry before final merge, without force pushing or discarding overlapping peer work.
3. Confirm GitHub Actions on the exact new HEAD, record compilation/CTest and any Vulkan software-ICD result in a new dated handoff.
4. Keep PR #70 Draft until CI and safety gates are reviewed; no automated main merge.
