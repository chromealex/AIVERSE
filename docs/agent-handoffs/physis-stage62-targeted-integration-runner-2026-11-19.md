# PHYSIS Stage 62 — focused integration test runner

Date: 2026-11-19
Repository: chromealex/AIVERSE
Branch: feature/physis-nature-simulation
PR #25 Draft: https://github.com/chromealex/AIVERSE/pull/25
Implementation commit: 9fe0b27a28177c5e4126f9ccf3a839c4fc49793d
Prior handoff: docs/agent-handoffs/physis-stage61-native-validation-runner-2026-11-19.md
Handoff commit: see HEAD after this file is committed.

## Motivation

The full engine and Ubuntu/macOS CI jobs remain queued without
compiler logs. The user wants real admin journal, remote editing and
field runtime integration checked before spending more time on broad
nature features. Stage 61 added a runner for full PHYSIS and an
independent standalone POSIX journal mode. The full mode builds
over twenty targets and Box3D, so it is unnecessarily large for
debugging the key ECS/network/persistence paths.

## Implementation / changed files

- tools/physis-validate-native.sh
  - New --integration mode: configures the ROOT AIVERSE CMake as a
    focused headless Debug build, then builds exactly these targets:
      aiverse_physis_admin_journal_test
      aiverse_physis_remote_edit_test
      aiverse_physis_field_runtime_test
      aiverse_physis_journal_posix_io_test
      aiverse_physis_journal_locked_io_test
      aiverse_physis_journal_crash_recovery_test
  - Runs CTest with an anchored regex matching only those six
    tests. Keeps the same failure-propagating stage helper and
    per-run configure/build/ctest/summary logs.
  - --no-physics can be combined with --integration to avoid
    optional physics dependencies; --full and --standalone keep
    their prior behavior.
  - Git blob SHA: 9125e955165a664e0759b0bdab556e23157783ff
  - No changes to ECS, wire protocol, journal binary format or
    physics/foliage/water ownership.
- engine/physis/AGENTS.md and
  skills/world/aiverse-physis/SKILL.md updated with precise
  commands, scope and test evidence.
- This dedicated per-stage handoff.

## ACTUAL verification

1. Source script syntax checked with bash -n, no errors.
2. Exact new script run on the existing Linux project containing
   REAL PHYSIS journal lock and POSIX I/O headers and tests:
     bash tools/physis-validate-native.sh --standalone --jobs 3
   CMake/CTest: physis_journal_posix_io, physis_journal_locked_io,
   physis_journal_crash_recovery all PASSED; 3/3.
   Report path: /mnt/data/physis54_smoke/build/stage62/physis-reports/.
3. Independently tested --integration runner **control flow** against
   a temporary CMake fixture defining six stub test targets with
   the same names (not the real PHYSIS implementations):
   - All 6 mock CTests passed; script exit 0 and logs created.
   - Deliberately failed all mock CTests using an environment
     variable; script exited 8, printed FAIL ctest, and preserved
     ctest.log plus summary. This confirms that failure statuses
     propagate instead of producing a false PASS.
   - A first malformed mock CMake target name was repaired before
     the successful mock test; no engine source was involved.
4. Source audit via the connected GitHub repository confirmed
   all six selected executable targets and registered test names
   exist in the actual root CMakeLists.txt.
5. Connected GitHub blob SHA matched the exact locally tested
   new script: 9125e955165a664e0759b0bdab556e23157783ff.

## NOT verified / limitations

- The --integration mode HAS NOT run against the real AIVERSE root
  project, Simulation/ECS, journal serializer, admin WebSocket or
  Box3D. Mock CTest 6/6 is NOT equivalent to real CTest success.
- Real AIVERSE full native CMake configure/build/CTest, all headless
  PHYSIS, macOS or Windows still not run. GitHub clone from the
  container failed DNS resolution for github.com.
- GitHub Actions full PHYSIS and standalone journal workflow
  Ubuntu/macOS jobs remained queued; no completed build/CTest logs.
- The full mode excludes editor-core-only authoring fixtures; use
  an editor-enabled build for those.
- PHYSIS already includes current main (0 commits behind). PR #25
  remains Draft and is conflicted with its unrelated target feature
  branch. Do not merge that branch without explicit user approval.
- Canonical PHYSIS FieldRuntimeState/edit commands remain genre
  independent. NEREID owns water, TerraWeave terrain/foliage,
  Box3D physics. Journal and World checkpoint are not one
  atomic transaction; continue Unknown/Uncertain fail-closed rules.

## Next agent concrete step

1. On a full macOS/Linux checkout of feature/physis-nature-simulation:
     bash tools/physis-validate-native.sh --integration --no-physics --jobs 4
   It will print the report directory and save configure.log,
   build.log, ctest.log and summary.txt. Use the first actual
   compiler error or test failure in those files to fix real code.
2. After targeted integration is green:
     bash tools/physis-validate-native.sh --full --jobs 4
   Then separately run editor-enabled PHYSIS authoring CTests.
3. Check completed Ubuntu/macOS Actions jobs and log artifacts
   for both independent journal and full PHYSIS workflows.
4. Publish the next stage handoff with real test results and SHA.
   Keep PR #25 Draft until native integration and PR conflicts
   are resolved.
