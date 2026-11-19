# PHYSIS Stage 57 — independent journal CMake and CI jobs (2026-11-19)

Repository: chromealex/AIVERSE
Branch: feature/physis-nature-simulation
PR: https://github.com/chromealex/AIVERSE/pull/25 (DRAFT)
Previous handoff: docs/agent-handoffs/physis-stage56-bounded-posix-journal-2026-11-19.md
Code/CI commits: b458fa85d5c6c60b06f3a79bd727eede189f6412, 339c0da9045f27e071e1c6729b32ae6871e71a87, 974b9dfc79290380ba57485f35bcc9540f7b855d
Current branch HEAD immediately before this handoff commit: 974b9dfc79290380ba57485f35bcc9540f7b855d.

## Why

Full AIVERSE GitHub Actions jobs have been queued or cancelled
rather than producing actionable Ubuntu/macOS compiler logs.
The model's local container cannot clone github.com due DNS,
so full engine native integration remains unverified.
The real POSIX journal lock and I/O sources/tests can, however,
be built independently from the engine, giving a reproducible
first-pass native regression check.

## Architecture and changed files

- tests/physis_journal_standalone/CMakeLists.txt (NEW)
  - Independent POSIX-only CMake project, C++20, extension-free,
    -Wall -Wextra -Werror -pedantic with GCC/Clang/AppleClang.
  - Uses the actual project-root production headers via root
    include directory, not surrogate implementations.
  - Builds tests/physis_journal_posix_io_test.cpp and
    tests/physis_journal_locked_io_test.cpp, registers tests
    physis_journal_posix_io and physis_journal_locked_io.
  - Does not configure/link ECS, engine, net, editor or Box3D.
  - Git blob SHA: f4e60d18a2db0e8b40fb3d0f996a1cb31145d629.
- .github/workflows/physis.yml
  - A separate physis-journal-standalone matrix job for
    ubuntu-latest and macos-latest (checkout, configure, build,
    CTest and log artifacts) alongside existing physis-native
    engine integration matrix. It does NOT replace the full run.
  - Added 'tests/physis_journal_standalone/**' to push and
    pull_request path filters so CMake changes retrigger CI.
- engine/physis/AGENTS.md and
  skills/world/aiverse-physis/SKILL.md: exact invocation,
  ownership boundaries, evidence and next-step guidance.
- No changes to PHYSIS simulation, network/receipt protocol,
  FileJournal serialization, Box3D, NEREID, TerraWeave or genres.

## Tests ACTUALLY executed

On Linux, created the exact new CMake file under
/mnt/data/physis54_smoke/tests/physis_journal_standalone/.
git hash-object returned
f4e60d18a2db0e8b40fb3d0f996a1cb31145d629,
which the connected GitHub fetch independently confirmed.

The four actual production/test source files in that local
workspace also matched their current GitHub blob IDs:
- engine/physis/PhysisJournalProcessLock.h:
  2512aefb07f612c808041b92a21da897df4f195b
- engine/physis/PhysisJournalPosixIO.h:
  ab24c98afebc3dbe45a05244b1a94e6eec977ccf
- tests/physis_journal_locked_io_test.cpp:
  db075dd14dd47f77a719fa47a86cce4adbac71ae
- tests/physis_journal_posix_io_test.cpp:
  d3ef56264ab8da3324375e7b8d76bf1a1ea64851

Commands and actual output:
```sh
cmake -S /mnt/data/physis54_smoke/tests/physis_journal_standalone \
  -B /mnt/data/physis54_smoke/build-staged -DCMAKE_BUILD_TYPE=Debug
cmake --build /mnt/data/physis54_smoke/build-staged --parallel 2
ctest --test-dir /mnt/data/physis54_smoke/build-staged --output-on-failure

# 1/2 Test #1: physis_journal_posix_io .......... Passed
# 2/2 Test #2: physis_journal_locked_io ......... Passed
# 100% tests passed, 0 tests failed out of 2
```

Compiler: GNU C++ 14.2.0, C++20, with strict warnings treated as errors.

## What is NOT verified

- Full AIVERSE root CMake, native admin journal and remote edit
  networking/ECS tests, Box3D, real editor and complete PHYSIS suite.
- Actual Ubuntu/macOS GitHub Actions runner completion for these new
  standalone jobs: the overall Actions queue was still backed up.
  Do not claim CI green merely because it was configured.
- Native Windows owner/reparse/ACL security or cross-platform file
  semantics.
- No crash-atomic transaction between World checkpoint and journal.

## Integration status and PR warning

- GitHub compare base=main, head=feature/physis-nature-simulation
  returned behind_by=0. Main already included.
- PR #25 targets feature/universal-entity-inspector-fps, not main,
  and GitHub reports mergeable_state=dirty (merge conflicts).
  That base has diverged separately. The user's earlier instruction
  was explicitly to bring only main into the PHYSIS branch; do not
  merge the unrelated target feature branch without new approval.
- Keep PR #25 DRAFT until full native QA and base conflicts resolved.

## Concrete next step

1. Check GitHub Actions run status after queues clear; inspect BOTH
   separate journal-standalone and full physis-native jobs on Ubuntu
   and macOS, including artifact logs.
2. In a full checkout, run the standalone CMake commands above,
   then root AIVERSE CMake and ctest --output-on-failure -R '^physis_'.
3. Fix real errors in physis_admin_journal, remote_edit, field_runtime
   and Box3D integration first; preserve fail-closed receipt semantics.
4. Resolve PR base diverged/dirty status only when the user selects
   whether to integrate the other feature branch or retarget PR.
5. Produce another per-stage handoff with exact SHA, changed files,
   actual executed tests, limitations and next step.
