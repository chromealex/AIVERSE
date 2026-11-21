# AIVERSE Stage 114 — restore Release assertions in retirement tests

Date: 2026-11-20
Repository: chromealex/AIVERSE
Target branch: `fix/postmerge-release-retirement-checks-stage114-20261120`
Base main SHA: `d6c8fa4646cf6aa205eea5f6a0b7c0c34ac479cb`
Earlier handoff: `docs/handoffs/aiverse-stage-112-gcc-arm64-range-loop.md`

## Confirmed CI failure
GitHub Actions main CI #44712475094:
https://github.com/chromealex/AIVERSE/actions/runs/44712475094
Linux arm64 GCC job 136207365060; CMake Configure PASS, compile
progress `[945/1570]` before first fatal diagnostics.
GCC `-Werror=unused-but-set-variable` in:
- `tests/render/resource_retirement.cpp:16-17`: fence, dispose.
- `tests/render/async_resource_retirement.cpp:18-24`: begin, done,
  close, wait, dispose.

## Root cause
Both regression tests used standard `assert(expr)` around the tested
queue operations. Under Release `-DNDEBUG`, `assert` elides the
expression entirely. This invalidated test execution and made lambdas
appear unused. The correct existing project-wide helper is
`tests/AlwaysCheck.h`, where `AIVERSE_TEST_CHECK(expr)` evaluates
its argument exactly once in Debug and Release, throwing on failure.

## Changed files and behavior
- `tests/render/resource_retirement.cpp`: replace all `assert(...)`
  with always-on `AIVERSE_TEST_CHECK(...)`, include `../AlwaysCheck.h`
  instead of `<cassert>`. No production code modified.
- `tests/render/async_resource_retirement.cpp`: identical conversion.
- All queue operations (collect/drain/dispose/fence) now actually
  execute during Release CI, rather than being compiled out.

## Verified vs not verified
- Confirmed exact compiler diagnostics from completed Linux ARM64 job.
- Inspected source test files and shared `AlwaysCheck.h` contract.
- Source transformation guarded to replace only `assert` calls and
  one include per file; retained all assertions and operation order.
- Real full native compile / Release CTest / Metal / Vulkan / GLES:
  **NOT RUN on this new revision at handoff creation**. CI must
  validate; do not claim PASS until it finishes.
- Old full postmerge workflow run #44709805590 targets
  `fc42156b41129f15b48c8b9e8626579eb2c70db0`, not the fixed SHA.
  It must not be considered final acceptance on current main.

## Next steps
Create PR for this focused fix and merge only after checking
head/base and mergeability. Then inspect the next main CI run; if
another compile issue appears, address that first. Request full
`AIVERSE post-merge full validation` via workflow_dispatch on the
current corrected main only after compilation is unblocked. Add
another handoff for each subsequent stage, preserving all other
agents' parallel worktrees and feature branches.
