# PHYSIS Stage 49 — standalone verified POSIX journal I/O (2026-11-19)

Repository: chromealex/AIVERSE
Branch: feature/physis-nature-simulation
PR #25 DRAFT: https://github.com/chromealex/AIVERSE/pull/25
Previous stage: docs/agent-handoffs/physis-stage48-pinned-journal-io-2026-11-19.md
Latest implementation/docs commit before this handoff: f0182275f2fcd12b8f4768701d6ee09124c5436b

## Motivation and architecture

The original `FileJournal::loadPinned/savePinned` methods implemented
their own POSIX `openat/read/write/fsync/renameat` logic inside
`PhysisRemoteJournal.h`, which includes ECS, GameModule,
network, serialization and crypto headers. Full repository checkout
is unavailable in the current environment because github.com DNS
does not resolve; the feature CI has mostly queued/cancelled runs.

To get a REAL native compiler/CTest result without duplicating the
engine's data model, raw fd-bound filesystem operations were factored
into an independent header with no ECS/genre/net/editor dependency.
The canonical `FileJournal` still owns the existing bounded v1
checksummed binary format, `ServerGateway::restoreJournal`, and
cryptographically random suffix generation. No save migration.

## Actual implementation commits and changed files

1. `5a26c8c8ef18861b6e5169251fa510c679d1092d`,
   `c3c80615deb69104a615b77fb6483881c8542554`
   - New `engine/physis/PhysisJournalPosixIO.h`.
   - `posix_journal::read(directoryFd,basename,min,max,bytes,missing,error)`
     opens with `openat(O_RDONLY|O_NONBLOCK|O_NOFOLLOW)`, validates
     file type, owner, single link, private mode and size. Missing
     target is not an error. Bad reads leave output unchanged.
   - `posix_journal::write(directoryFd,basename,bytes,randomSuffix,error)`
     validates owned/private parent, uses `openat(O_EXCL)`,
     exact write with EINTR retry, file fsync, `renameat`,
     directory fsync and `unlinkat` temp cleanup.
   - No new ECS component, persistent schema, network packet or
     platform-specific Windows behavior.
   - Verified blob SHA:
     `834a651f38b2d9c3ce75bc261b3ec731d3572e8a`.

2. `fda48ec65c73eb53bda9946fd0c2386bbafab3b7`
   - `engine/physis/PhysisRemoteJournal.h`
   - POSIX `FileJournal::loadPinned/savePinned` delegate only raw I/O
     to the independent helpers, keeping original encodeEntries/
     decodeEntries, randomKey, size limits and restoreJournal calls.
     Windows and direct legacy path-based load/save are unchanged.

3. `fb0ea7da5fce80a263e2a81b99ee1bc598d4b4cd`,
   `70c83172c14510a5087f6a0b9ec97ba837e106fe`
   - `tests/physis_journal_posix_io_test.cpp`
   - Standalone regression for absent/valid journal, exact byte
     roundtrip, invalid basename/suffix, size validation, file mode
     refusal, FIFO/symlink/hardlink denial, atomic replacement of a
     symlink without modifying its external target, parent chmod
     refusal and parent rename redirection defense.
   - Verified blob SHA:
     `d45e0dd9e5ab5d8d55b806e0b847fe49ced22775`.

4. `d8f1526b5a49fb28e327f6ffe7138be11c17df74`
   - `CMakeLists.txt`: new test target
     `aiverse_physis_journal_posix_io_test`, test name
     `physis_journal_posix_io` under
     `AIVERSE_BUILD_TESTS AND UNIX AND NOT EMSCRIPTEN`.
5. `de0ebe00170308db42624d546e8502f51b4be74f`
   - `.github/workflows/physis.yml`: add the test target to
     explicit Linux/macOS PHYSIS build list. Existing CTest '^physis_'
     pattern automatically includes the new test.
6. `5430c6d6843f85087edcaed15e8f13760e76df5b`
   - `engine/physis/AGENTS.md`: updated agent ownership,
     APIs and honest verification guidance.
7. `f0182275f2fcd12b8f4768701d6ee09124c5436b`
   - `skills/world/aiverse-physis/SKILL.md`: native test workflow.

## TESTS ACTUALLY EXECUTED (not just authored)

**PASS — isolated source-identical Linux module**

- Local directory: `/mnt/data/physis49_smoke`.
  Copied exactly the newly committed standalone header and test
  source into matching paths. Ran `git hash-object` on both;
  GitHub connector independently returned precisely matching blob
  SHA values as listed above.
- Compiler: Debian GCC 14.2.0 (C++20), warnings treated as errors.
  Native build:
  ```sh
  cmake -S /mnt/data/physis49_smoke -B /mnt/data/physis49_smoke/build -DCMAKE_BUILD_TYPE=Debug
  cmake --build /mnt/data/physis49_smoke/build --parallel 2
  ctest --test-dir /mnt/data/physis49_smoke/build --output-on-failure
  ```
  Result: `1/1 Test #1: physis_journal_posix_io ... Passed`,
  `100% tests passed, 0 tests failed out of 1`.
- Executed exact committed test with strict warning compile:
  `g++ -std=c++20 -O2 -Wall -Wextra -Werror -pedantic -I. ...`;
  result: `PHYSIS POSIX journal I/O: PASS`.
- Ran GCC AddressSanitizer + UndefinedBehaviorSanitizer:
  ```sh
  g++ -std=c++20 -g -O1 -Wall -Wextra -Werror -pedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer -I. \
    tests/physis_journal_posix_io_test.cpp -o journal_io_san
  ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 timeout 20 ./journal_io_san
  ```
  Result: `PHYSIS POSIX journal I/O: PASS`, exit code 0.
- Ran Clang 17.0.0 C++20 with strict warnings on the same source.
  Result: `PHYSIS POSIX journal I/O: PASS`, exit code 0.

**What is still NOT VERIFIED**

- Full AIVERSE CMake configure/build, actual
  `physis_admin_journal`, `physis_remote_edit`,
  `physis_field_runtime`, and whole PHYSIS CTest suite.
- Full `PhysisRemoteJournal.h`/Simulation integrated build (this
  isolation test verifies new raw POSIX helper only).
- macOS runtime, Windows I/O ACL/reparse parity, editor and real
  authenticated WebSocket after server restart.
- Live GitHub Actions jobs on the branch were queued or cancelled.
  No recent successful Ubuntu/macOS build/test logs available.
- Repo clone fails locally: `Could not resolve host: github.com`.
  The isolated CMake file uses the same target sources/includes
  but is NOT the complete AIVERSE CMake project.

## Important limitations and next tasks

- The helpers accept a directory fd and rely on the canonical
  `JournalProcessLock` for exclusive lifetime ownership.
  A same-UID process with directory write access can still race
  after the guard; journal and World checkpoint are not one
  crash-atomic transaction. No distributed fencing promised.
- The old path-based FileJournal::save/load/attach remain available
  for legacy non-privileged callers and are not automatically
  lock-owned. Canonical endpoint must use pinned methods.
- Once a full native checkout is available, build with existing
  `.github/workflows/physis.yml` flags and run
  `ctest --output-on-failure -R '^physis_'`.
  Repair actual compiler/CTest failures, including FileJournal
  delegator typing/includes and macOS POSIX compatibility.
- Extend failure injection and two-process crash tests for
  canonical authenticated endpoint, then test macOS, Windows
  and client Uncertain/rollback behavior.
- Keep the user-required per-stage handoff and PR #25 DRAFT until
  full native evidence is available.
- Project ownership: PHYSIS nature; NEREID water; TerraWeave
  terrain/foliage; Box3D physics. Genre-independent components
  and ECS systems remain required.
