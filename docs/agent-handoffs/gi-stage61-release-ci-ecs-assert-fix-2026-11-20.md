# AIVERSE GI integration — Stage 61: actual Linux ARM64 Release CI failure and test assertion repair (2026-11-20)

## Branch / observed CI / commits

- Repo `chromealex/AIVERSE`; branch `feature/gi-lighting-portable-backends-20261119`. PR #47 remains DRAFT against `feature/hybrid-dynamic-gi`, NOT current main.
- **REAL CI FAILURE CONFIRMED**, unlike earlier source-only handoffs. GitHub Actions run `44693379411`, job `136144283268` (`Linux arm64 gcc`), tested head `d71411613d47fd26304edb6b0559515543806c1c`.
- Steps **Set up, checkout, dependencies and CMake Configure** returned SUCCESS. **Build** (`cmake --build build/linux-release`, `CMAKE_BUILD_TYPE=Release`, GCC 13, `-O3 -DNDEBUG -Werror`) returned **FAILURE, exit code 1**, around operation 890/1367. Subsequent Tests, Vulkan lavapipe, screenshot and artifact upload steps were SKIPPED. Do NOT say CTest failed or passed — it was never reached.
- Four specific build failures from real compiler log:
  1. `tests/ecs_cow_snapshot_test.cpp:12`: five unused parameters in helper `check` (`-Werror=unused-parameter`).
  2. `tests/ecs_cow_exception_test.cpp:37,51,96`: `rejected`, `caught` used only in removed Debug `assert`, now unused in Release (`-Werror=unused-but-set-variable`).
  3. `tests/ecs_cow_parallel_test.cpp:34,59,60`: `original`, `expected`, `workers` unused (`-Werror=unused-variable`).
  4. `tests/ecs_guarded_parallel_test.cpp:52,102`: `caught`, `expected` unused (`-Werror=unused-but-set-variable/unused-variable`).
- Repair commits on **the isolated GI integration branch only**, no direct main changes:
  - `e5c66fc302be945c144181e598360283ee4519ae`: `ecs_cow_snapshot_test.cpp`, 28 checks converted.
  - `d245bf4ff04d720afefaa843ed9bba45474ecea1`: `ecs_cow_exception_test.cpp`, 19 checks converted.
  - `6591166d9b4573cda58f9eaa80ae251a40233321`: `ecs_cow_parallel_test.cpp`, 5 checks converted.
  - `2cf0f6bf364dbb29f8a6bed6dd4c82463df63068`: `ecs_guarded_parallel_test.cpp`, 7 checks converted.
- Total **59 standard `assert(expr)` expressions replaced with `AIVERSE_TEST_CHECK(expr)`**, using the existing repo `tests/AlwaysCheck.h`. No production ECS logic changed.

## Why this correction is required, not a warning suppression

Normal `assert(expr)` is entirely compiled out under `NDEBUG`. It eliminates the execution of `expr`, potentially skipping side-effectful test actions (e.g. `assert(exercise(workers)==expected)`) and disabling invariant checking in shipped-style Release CI. GCC `-Werror` then finds variables used solely by those removed assertions. The existing `AIVERSE_TEST_CHECK` always evaluates its expression **exactly once** and throws `std::runtime_error` on failed invariants, both Debug and Release. Replacing it rather than adding `(void)` casts or suppressing warnings repairs both compile and actual test coverage. Keep these four tests genre-independent and respect other ECS agents' ongoing work.

## What was actually verified

- **PASS as observed:** CI checkout/dependencies/configure steps on the prior SHA.
- **FAIL as observed:** Release Linux ARM64 GCC full-build target on prior SHA, exact compiler errors above.
- **FIXED IN SOURCE BUT NOT YET VERIFIED:** the four ECS Release test files, commit SHAs above.
- **NOT RUN:** new post-fix CI build/CTest, GI BVH regression execution, Vulkan GLSL/SPIR-V/Metal/GLES shader compilation and GPU pixel/device tests. No successful all-tests pass is established.
- This failure is an ECS **test harness** issue, not evidence that GI BVH or Vulkan shaders passed or failed. The build failed before executing CTest. If a later GCC/Clang build fails elsewhere, inspect its logs and make a new stage-specific handoff with the exact evidence.

## Next action for next agent

1. Monitor fresh GitHub Actions CI for the **new branch tip**, inspect `Linux arm64 gcc` job logs. Confirm that the four object files compile with Release `NDEBUG -Werror`. If any other test file fails due to Debug-only `assert`, convert it to the existing `AIVERSE_TEST_CHECK` macro rather than hiding diagnostics.
2. Run the narrow GI CTest targets once compiled: `gi_gpu_bvh_validation|gi_gpu_paged_bvh|gi_gpu_page_compaction|gi_gpu_dynamic_runtime|gi_gpu_ticket_queue|gi_gpu_sparse_patch`, plus `vulkan_backend` with lavapipe + validation layers if the runner supports it.
3. Finish remaining feature integration, then one user-requested final full AIVERSE CTest+Metal/Vulkan/GLES acceptance and fix failures with tested SHA. Never claim source-authored tests PASSED.
4. Reconcile the GI feature with current `main` and ongoing PHYSIS/TerraWeave/renderer agents only after actual test evidence, preserving all sources. Keep PR #47 DRAFT.
