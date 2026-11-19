# PHYSIS Stage 60 — exclude handoff-only changes from native CI triggers

Date: 2026-11-19
Repository: chromealex/AIVERSE
Branch: feature/physis-nature-simulation
PR #25: https://github.com/chromealex/AIVERSE/pull/25 (DRAFT)
Pre-stage branch HEAD: c888c6632139b21443c182d5a4ff0dd29dedb9ad
Code commit: 96b1efb210f9f4f9f050415e5e936b2707c1a500
Previous handoff: docs/agent-handoffs/physis-stage59-process-crash-recovery-2026-11-19.md
The latest handoff commit is the branch HEAD after committing this document.

## Problem found from live GitHub Actions

Stage 58 split journal standalone checks into their own workflow,
but `.github/workflows/physis.yml` continued to trigger for
ANY file in `engine/physis/**`, including `engine/physis/AGENTS.md`.
Our development policy requires an updated AGENTS.md after EVERY
stage. With `cancel-in-progress: true`, documentation-only
commits repeatedly triggered a newer full native PHYSIS run and
cancelled queued predecessor runs without reaching compilation.

At inspection, native PHYSIS run 44497562023 and separate journal run
44497359197 were queued. Their Ubuntu/macOS jobs had not begun,
and there were no completed successful build or CTest logs.

## Implementation and changed files

- `.github/workflows/physis.yml`:
  added `- '!engine/physis/AGENTS.md'` immediately after
  `- 'engine/physis/**'` in BOTH `push.paths` and
  `pull_request.paths`. A documentation-only AGENTS.md update
  no longer matches the full native workflow. C++ files beneath
  engine/physis and all listed tests, CMake and workflow config
  still match; the test matrix and build targets are unchanged.
  Exact GitHub blob SHA: 7a0b678c2545a666e16ad1ee61814b516c3e69f2.
- `engine/physis/AGENTS.md`:
  document reason, trigger policy, verification status.
- `skills/world/aiverse-physis/SKILL.md`:
  guide future agents not to reintroduce documentation-triggered
  cancellation.
- This dedicated Stage 60 handoff.
- No C++ production logic, ECS schema, transport protocol, GUI,
  water, terrain or Box3D code was modified this stage.

## Verification ACTUALLY performed

- GitHub source re-fetch confirmed the exact workflow blob SHA
  and the ordered negative path filter appeared twice.
  NOTE: merely inspecting YAML does not prove that GitHub runners
  have executed it. A future docs-only commit will be the real
  workflow-trigger check.
- Reconfigured and rebuilt the current exact standalone test
  sources in local Linux using the repository-independent CMake:
  `cmake -S /mnt/data/physis54_smoke/tests/physis_journal_standalone
     -B /mnt/data/physis54_smoke/build-staged -DCMAKE_BUILD_TYPE=Debug`
  `cmake --build /mnt/data/physis54_smoke/build-staged --parallel 3`
  `ctest --test-dir /mnt/data/physis54_smoke/build-staged --output-on-failure`
  ACTUAL output: `3/3 Passed, 0 Failed` for
  physis_journal_posix_io, physis_journal_locked_io and
  physis_journal_crash_recovery.
- Actual Clang 17 strict C++20 compile and run of all THREE
  separate tests: PASS.
- GCC 14.2 ASan+UBSan with leak detection and halt on UB ran the
  real SIGKILL crash recovery test: PASS.
- Exact source SHA verified in GitHub:
  JournalProcessLock.h 2512aefb07f612c808041b92a21da897df4f195b;
  PhysisJournalPosixIO.h ab24c98afebc3dbe45a05244b1a94e6eec977ccf;
  journal_posix_io_test.cpp d3ef56264ab8da3324375e7b8d76bf1a1ea64851;
  journal_locked_io_test.cpp db075dd14dd47f77a719fa47a86cce4adbac71ae;
  journal_crash_recovery_test.cpp 644671c632d0a2cc33372263681754cc6843bcb1;
  standalone CMakeLists.txt 5244076b034a04b16d3290d8c4a55139060b4d90.

## NOT verified and remaining work

- AIVERSE root CMake and full PHYSIS native matrix; no successful
  actual GitHub-hosted Ubuntu/macOS completed job logs.
- `aiverse_physis_admin_journal_test`,
  `aiverse_physis_remote_edit_test`,
  `aiverse_physis_field_runtime_test`, all PHYSIS CTests including
  Box3D, editor and Windows ACL/reparse checks.
- The container cannot resolve github.com for a full checkout.
  The isolated CMake suite cannot substitute for engine integration.
- PHYSIS branch was behind main by 0 commits at stage start.
  PR #25 targets a separately diverged feature branch and is
  marked dirty/merge-conflicted. Do not merge that unrelated branch
  unless explicitly requested.
- World ECS checkpoint + receipt journal are still not a single
  crash-atomic transaction, and POSIX advisory locks are not
  distributed fencing. Maintain Unknown/Uncertain semantics.

## Next concrete step for next agent

1. Inspect latest full PHYSIS native and separate standalone GitHub
   Actions runs. Verify that this AGENTS.md-only handoff update
   has NOT cancelled the native job. Capture actual completed logs.
2. If runners execute, fix the first real compiler/CTest error.
   Focus on physis_admin_journal and remote_edit integration,
   then field_runtime and full ^physis_ suite.
3. If runners remain queued, avoid speculative CI churn. Explore
   an authorized full-checkout path for actual native AIVERSE build,
   or provide the exact working commands and limitation.
4. Continue per-stage AGENTS.md + handoff with explicit checks
   actually run, branch SHA, changed files, architecture and next.
   Keep PR #25 DRAFT until both CI and target-base conflicts resolved.
