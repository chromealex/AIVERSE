# PHYSIS Stage 61 — focused native validation runner with persistent logs

Date: 2026-11-19
Repository: chromealex/AIVERSE
Branch: feature/physis-nature-simulation
PR #25 (Draft): https://github.com/chromealex/AIVERSE/pull/25
Implementation commit: 4df41161778cb21e6fd555450be18961eb001791
Previous handoff: docs/agent-handoffs/physis-stage60-ci-doc-filter-2026-11-19.md

## Goal and blocker

User wants completion of real native full PHYSIS build/CTest on their
macOS engine checkout. The current container cannot clone github.com
(Could not resolve host: github.com) and does not contain the full
AIVERSE repository. GitHub Actions PHYSIS full-native Ubuntu/macOS
jobs and separate journal jobs remain queued, so no integrated
compiler or CTest logs are available. Do not claim PHYSIS is fully
tested.

To enable reproducible native results on a machine that HAS the
repository, this stage adds one command to run exactly the focused
PHYSIS targets and preserve diagnostic artifacts, avoiding the
existing general-purpose run-tests.sh full-world build.

## Changed files, API and commit

- tools/physis-validate-native.sh (NEW, mode 100755)
  Git blob SHA: 9880c7aefc0ddf4791e69d729abf289729a3a96b
  - Bash 3.2-compatible; can be launched from any directory
    with 'bash tools/physis-validate-native.sh'.
  - --standalone: CMake configure/build and 3 CTests of the
    engine-independent native journal project.
  - --full (default): root AIVERSE Debug CMake, tests ON,
    physics ON by default, editor/tools/client/headless/server/
    physics demo OFF, build every headless PHYSIS CTest target
    as enumerated in the CI workflow, then
    'ctest --no-tests=error --output-on-failure -R ^physis_'.
  - --jobs N, --build-dir DIR, --no-physics (full mode only).
    --no-physics omits optional Box3D.
  - Per-stage configure/build/ctest logs, summary with git HEAD
    SHA, failure status and report path under
    <build-dir>/physis-reports/<UTC timestamp>-<PID>/.
  - Does not remove existing build directories or modify
    untracked project files other than its chosen build output.
- engine/physis/AGENTS.md and
  skills/world/aiverse-physis/SKILL.md: commands and test policy.
- docs/agent-handoffs/physis-stage61-native-validation-runner-2026-11-19.md:
  this mandatory per-stage handoff.

No PHYSIS runtime/C++ API, ECS component/schema, FileJournal V1 codec,
network format, Box3D, TerraWeave or NEREID code changed.

## What was ACTUALLY tested

Used current isolated Linux workspace:
  /mnt/data/physis54_smoke
It has the committed standalone journal sources and CMake files
but NOT full AIVERSE. The exact script source was staged under
  /mnt/data/physis54_smoke/tools/physis-validate-native.sh
and its git hash-object matched GitHub blob
9880c7aefc0ddf4791e69d729abf289729a3a96b.

Commands:
  bash -n /mnt/data/physis54_smoke/tools/physis-validate-native.sh
  cd /
  bash /mnt/data/physis54_smoke/tools/physis-validate-native.sh \
    --standalone --jobs 2 --build-dir build/scriptcheck

Actual test output:
  1/3 physis_journal_posix_io       Passed
  2/3 physis_journal_locked_io      Passed
  3/3 physis_journal_crash_recovery Passed
  100% tests passed, 0 tests failed out of 3.
  PHYSIS validation PASS.
  Report created under:
  /mnt/data/physis54_smoke/build/scriptcheck/physis-reports/

These real test results cover ONLY the standalone POSIX
journal lock and I/O tests. Full --full script mode has not been
run; it cannot be run in this container without the repository.

## Known limits

- Full mode is focused headless testing; CMake targets dependent
  on a built editor core (physis_authoring,
  physis_field_authoring) are intentionally not configured.
  Further editor-enabled CMake/CTest runs are required.
- Full CMake could still fail on actual macOS/Ubuntu toolchains,
  other engine dependencies, files or tests. We will fix failures
  when native logs exist; do not assume success.
- The workflow PR targets diverged
  feature/universal-entity-inspector-fps and remains merge-conflicted;
  user previously asked to include only main, which is already an
  ancestor. Do not merge the unrelated feature base implicitly.
- The journal and World checkpoint are still not crash atomic.
  Genre-neutral PHYSIS subsystem ownership unchanged.

## Concrete next step

1. On a Mac with AIVERSE repository, checkout/pull the PHYSIS
   feature branch and run:
     bash tools/physis-validate-native.sh --full --jobs 4
   Read the printed report directory (configure/build/ctest logs
   and summary.txt). If build fails, use the first true compiler
   error from build.log, not just the final error.
2. Once CTest is green, repeat with editor-enabled configuration
   to cover physis_authoring and physis_field_authoring, and
   run full all-modules regressions.
3. Independently inspect completed Ubuntu/macOS Actions jobs when
   available; queued or cancelled runs are NOT test results.
4. Make code fixes in the PHYSIS branch and record actual test
   results, changed files, next step and SHA in a Stage62 handoff.
   Keep PR #25 Draft pending full native QA and resolution of
   its target-feature-branch conflicts.
