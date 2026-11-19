# PHYSIS Stage 59 — real SIGKILL recovery of pinned receipt journal

Date: 2026-11-19
Repository: chromealex/AIVERSE
Branch: feature/physis-nature-simulation
PR #25 (DRAFT): https://github.com/chromealex/AIVERSE/pull/25
Code commit: 8e29db13ba60afeb105993d790f9f988c93f4b21
Previous handoff: docs/agent-handoffs/physis-stage58-independent-ci-2026-11-19.md
This handoff is in the subsequent documentation-only commit; use the
branch HEAD displayed in GitHub for its final exact SHA.

## Problem and architecture

POSIX FileJournal stores the last confirmed V1 receipt file through
openat, fsync, renameat and directory fsync. A process killed AFTER
writing and fsyncing an unpredictable temporary file but BEFORE
renameat must not replay that orphan as committed status. Only the
canonical journal path is a recoverable receipt history.

Stage 55 had covered an abandoned temporary file in one process.
Stage 59 uses a REAL independent child process, advisory flock,
SIGKILL and waitpid. This is lower-level crash consistency rather
than the complete engine/ECS checkpoint recovery.

## Changed files and APIs

- tests/physis_journal_crash_recovery_test.cpp (NEW).
  Uses true production JournalProcessLock and posix_journal IO
  classes. Child acquires lock, writes committed canonical bytes,
  stages and fsyncs .next-uncommitted, signals ready on pipe and
  blocks. Parent verifies competing acquisition is refused, kills
  child with SIGKILL, waits for signal termination, reacquires
  lock, reads prior committed bytes, safely publishes a replacement
  journal and checks the orphan is preserved.
  Readiness is synchronized with a pipe and bounded poll, not sleeps.
  Source blob SHA: 644671c632d0a2cc33372263681754cc6843bcb1.
- tests/physis_journal_standalone/CMakeLists.txt.
  Adds third standalone CTest named physis_journal_crash_recovery,
  without ECS/network/editor dependencies. Exact checked blob:
  5244076b034a04b16d3290d8c4a55139060b4d90.
- CMakeLists.txt.
  Registers aiverse_physis_journal_crash_recovery_test in the native
  POSIX PHYSIS CMake test block, C++20, and CTest name
  physis_journal_crash_recovery.
- .github/workflows/physis.yml.
  Full native target list includes new crash recovery test.
- .github/workflows/physis-journal.yml.
  Its push and pull_request filters include the new source.
  Existing standalone CMake build automatically includes test.
- engine/physis/AGENTS.md and
  skills/world/aiverse-physis/SKILL.md.
  Updated with this stage's ownership, test statuses and handoff.

No production C++ runtime code changed, and no PHYSIS ECS schema,
network wire protocol, physics/terrain/water module or genre-specific
code was altered.

## Tests ACTUALLY executed

In local Linux workspace /mnt/data/physis54_smoke, verified the test
source and standalone CMake SHA with git hash-object, matching
the actual GitHub create_blob values above.

Ran full standalone CMake configure, native build and CTest:
  cmake -S tests/physis_journal_standalone -B build-staged -DCMAKE_BUILD_TYPE=Debug
  cmake --build build-staged --parallel 3
  ctest --test-dir build-staged --output-on-failure

Output:
  physis_journal_posix_io       Passed
  physis_journal_locked_io      Passed
  physis_journal_crash_recovery Passed
  100% tests passed; 0 failed out of 3.

Also:
- Clang 17, C++20 -O2 -Wall -Wextra -Werror -pedantic:
  PHYSIS journal cross-process crash: PASS.
- GCC 14.2, C++20, AddressSanitizer + UndefinedBehaviorSanitizer,
  leak detection and halt on undefined behavior: PASS, exit code 0.

This is not a full AIVERSE engine run. The standalone project
links neither Simulation/ECS nor WebSocket, and does not parse
FileJournal V1 receipt bytes.

## Not verified / limitations

- Full root AIVERSE CMake/build, physis_admin_journal,
  physis_remote_edit, physis_field_runtime, complete PHYSIS CTests,
  Box3D, editor, native macOS/Windows.
- GitHub Actions journal-standalone and full PHYSIS jobs were queued
  in previous inspection, with no completed Ubuntu/macOS pass.
- Container git clone still fails DNS for github.com.
- Actual cross-file World + receipt sidecar crash atomicity is not
  provided. An Applied response after a crash must be reconciled
  against authoritative World data and persisted receipt status.
- POSIX advisory locks are local cooperative locks, not distributed
  fencing. Same-UID path races and Windows ACL/reparse handling
  remain separate security concerns.
- PR #25 has conflicts with its divergent target feature branch.
  PHYSIS is 0 commits behind main; do not merge the unrelated
  target branch without user approval.

## Next step for the next agent

1. Check the completed Ubuntu/macOS workflow runs and collect logs
   from both separate PHYSIS CI workflows. Jobs still queued are
   not tests run.
2. Obtain full native AIVERSE checkout; compile and run
   aiverse_physis_admin_journal_test, physis_remote_edit and
   physis_field_runtime, then the full CTest ^physis_ suite.
3. Fix actual integration compiler or runtime errors, including
   canonical FileJournal loadPinned/savePinned and admin endpoint.
4. Verify PHYSIS reconciliation under real authenticated restart,
   journal corruption, rollback and crash paths.
5. Maintain a separate per-stage handoff documenting commits,
   touched APIs, actual native tests, limitations and next action.
   Keep PR Draft until full engine verification and target-branch
   conflicts are resolved.
