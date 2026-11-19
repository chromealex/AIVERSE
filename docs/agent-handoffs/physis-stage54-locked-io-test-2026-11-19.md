# PHYSIS Stage 54 — integrated low-level journal lock + pinned I/O native regression (2026-11-19)

Repository: chromealex/AIVERSE
Branch: feature/physis-nature-simulation
PR #25: https://github.com/chromealex/AIVERSE/pull/25 (DRAFT)
Previous stage: docs/agent-handoffs/physis-stage53-published-inode-2026-11-19.md
Branch HEAD before this handoff: 08358e301428c0a1c3fd3922e031e75b3ea4d836

## Scope and design

The canonical POSIX PHYSIS privileged journal uses two production
headers together:
- `engine/physis/PhysisJournalProcessLock.h`: advisory exclusive flock,
  trusted parent directory/inode validation, pinned directory lifetime,
  `valid()` and `directoryFd()` fail-closed after path replacement.
- `engine/physis/PhysisJournalPosixIO.h`: bounded, secure raw
  `openat`/`read`/`write`/`renameat`/`fsync` relative to that
  pinned directory.

Prior tests covered each header separately or relied on the complete
`physis_admin_journal` CTest, which could not be built without a full
repository checkout. Stage 54 adds a **native cross-module regression**
using BOTH actual production headers together. This is not a mock and
does not alter PHYSIS ECS, the journal V1 serialization, admin network
protocol or persistence resource IDs.

## Commits and changed files

1. `a960306f5f15d8eb612f4db328f9f4a87b34addd`:
   new `tests/physis_journal_locked_io_test.cpp`.
   - Uses a private `mkdtemp` directory.
   - Acquires a real `JournalProcessLock`, borrows its valid
     directoryFd, writes/loads receipt bytes using the actual POSIX I/O
     helper and confirms equality.
   - Confirms a competing lock instance is denied.
   - Renames the live `receipts.lock` inode, confirms the original
     owner's `valid()` is false and `directoryFd()` returns -1,
     while a new owner may lock the replacement basename. This
     demonstrates the need for the canonical endpoint's fail-closed
     ownership checks; **the test never makes an old-owner authorized
     write after invalidation**.
   - Checks independent new-owner pinned I/O, release and restart.
   - Git blob SHA:
     `db075dd14dd47f77a719fa47a86cce4adbac71ae`.

2. `94c64d8e1fe942c977282fc97812c7e1e7e26bba`:
   `CMakeLists.txt`: new native CTest
   `physis_journal_locked_io`, target
   `aiverse_physis_journal_locked_io_test` under existing
   `AIVERSE_BUILD_TESTS AND UNIX AND NOT EMSCRIPTEN`.
   - Include project root; compile as C++20.
   - Labels `PHYSIS;PERSISTENCE;SECURITY`.
   - No dependencies on `aiverse_engine`, `aiverse_net`,
     editor, Box3D or genre modules.

3. `55ad9c603776fefe14544a4352bc6897db7b5114`:
   `.github/workflows/physis.yml`: adds the new test executable
   to the explicit Ubuntu/macOS PHYSIS build target list. Existing
   CTest `-R '^physis_'` includes it automatically.

4. `d684bbc1c77597a2e2e790b90c1741522ccad130`,
   `77ec0714609331a1b72e2dfe1e81daaed7995b5b`:
   `engine/physis/AGENTS.md` handoff/ownership/testing instructions.

5. `08358e301428c0a1c3fd3922e031e75b3ea4d836`:
   `skills/world/aiverse-physis/SKILL.md` reuse instructions.

## Tests actually executed — do not confuse with full-engine PASS

Local native Linux workspace: `/mnt/data/physis54_smoke`.
Production header files were copied from the existing checked exact
source snapshots and verified against the CURRENT GitHub blobs:
- Lock header: `2512aefb07f612c808041b92a21da897df4f195b`.
- POSIX I/O header: `2dd114731b498c4156da305915201f97c4848647`.
- New CTest source: `db075dd14dd47f77a719fa47a86cce4adbac71ae`.
- Existing raw I/O CTest source (Stage 53):
  `7c6cd0ef9e330be5b61bf7935bb8d8b0f025a907`.

**PASS: native standalone GCC/CMake/CTest.**
```sh
cmake -S /mnt/data/physis54_smoke -B /mnt/data/physis54_smoke/build \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build /mnt/data/physis54_smoke/build --parallel 2
ctest --test-dir /mnt/data/physis54_smoke/build --output-on-failure
```
Actual output:
```text
Start 1: physis_journal_locked_io
1/2 Test #1: physis_journal_locked_io ... Passed
Start 2: physis_journal_posix_io
2/2 Test #2: physis_journal_posix_io ... Passed
100% tests passed, 0 tests failed out of 2
```
The isolated CMake harness contains the same two test sources/headers,
but it is **NOT** the root AIVERSE CMake project.

**PASS: standalone Clang 17 C++20 strict warning build/run**
(`-O2 -Wall -Wextra -Werror -pedantic`) of the new combined test;
printed `PHYSIS locked journal I/O: PASS`.

**PASS: standalone GCC 14.2 ASan+UBSan build/run** of the new test
(`-fsanitize=address,undefined -fno-omit-frame-pointer`,
`ASAN_OPTIONS=detect_leaks=1:abort_on_error=1`,
`UBSAN_OPTIONS=halt_on_error=1`); same PASS output and exit 0.

**PASS: GitHub connector checks** confirmed the new test blob exactly
matched the locally tested file, and CMake/PHYSIS workflow register
the correct target.

## What was NOT verified

- Complete AIVERSE CMake configure/build and native `physis_admin_journal`,
  `physis_remote_edit`, `physis_field_runtime`, entire PHYSIS suite,
  editor/runtime and Box3D integration.
- macOS, Windows, native WebSocket authentication, disk crash fuzz and
  the canonical `LoopbackAdminEndpoint` exercising the full FileJournal
  serialization.
- The local container still cannot resolve github.com for
  `git clone/ls-remote`. At inspection GitHub Actions for the branch
  had queued runs and earlier cancelled runs, not a completed PHYSIS
  native test PASS.
- No claim of crash-atomic exactly-once delivery: receipt journal and
  authoritative ECS checkpoint are separate stores.
- No distributed fencing: POSIX `flock` is advisory and same-UID
  pathname replacement still needs canonical endpoint validity checks.

## Ownership and next step for another agent

PHYSIS remains genre-independent and owns natural phenomena. NEREID
owns water, TerraWeave terrain and foliage, Box3D physics. The
canonical `FieldRuntimeState`/Editable admin route must not coexist
with legacy `RuntimeFieldRegistry/FieldCommandHost`.

1. Obtain a full checkout or a **completed** Ubuntu/macOS PHYSIS run
   from https://github.com/chromealex/AIVERSE/actions/workflows/physis.yml.
2. Compile the newly registered `aiverse_physis_journal_locked_io_test`
   and existing `aiverse_physis_journal_posix_io_test` using ROOT
   AIVERSE CMake; run both through root CTest.
3. Compile/run `aiverse_physis_admin_journal_test`,
   `aiverse_physis_remote_edit_test`,
   `aiverse_physis_field_runtime_test`; fix real failures using logs.
   Then run every `^physis_` CTest including optional Box3D.
4. Verify endpoint lock loss after a successful edit against real
   authenticated WebSocket and disk journal; confirm no unpersisted
   Applied ACK. Add Windows process/ACL/reparse coverage separately.
5. Publish next per-stage handoff with exact HEAD SHA, file changes,
   actual native test results, limitations, and next action. Keep
   PR #25 DRAFT until confirmed full-engine tests pass.
