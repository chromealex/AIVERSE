# PHYSIS Stage 50 — post-read identity check and temp collision safety (2026-11-19)

Repository: chromealex/AIVERSE
Branch: feature/physis-nature-simulation
PR #25: https://github.com/chromealex/AIVERSE/pull/25 (DRAFT)
Previous handoff: docs/agent-handoffs/physis-stage49-posix-journal-test-2026-11-19.md
Latest implementation/agent-doc commit before this handoff: 1c56b4815a845d1de617ab58822097f31c0bc202

## Context and reason

Stage 49 extracted the pinned-directory POSIX filesystem operations
into `engine/physis/PhysisJournalPosixIO.h` with a standalone test.
A successful initial `fstat(fd)` did not prove that the visible
journal pathname continued to resolve to the same inode after the
read, nor did the write path recheck parent permissions after fsync.
This could expose stale bytes if a same-UID process replaced the
journal mid-read, or return a successful durability acknowledgment
after the directory became writable by other users.

The authoritative game state is still ECS FieldRuntimeState.
FileJournal is a separate v1 checksummed sidecar journal, NOT an
atomic transaction with the World checkpoint.

## Code and commits

1. `1dec096fe5a056aacf2aa6577e39c120a4f4082d`
   - `engine/physis/PhysisJournalPosixIO.h`
   - `posix_journal::read` keeps bytes in a local vector until
     it confirms the opened fd still has its original device/inode
     and size, the named file via `fstatat(...,AT_SYMLINK_NOFOLLOW)`
     still resolves to that same inode with one link and secure
     owner/mode, and the directory fd still names a server-owned
     non-group/world-writable directory.
   - Any failure closes fd and returns false WITHOUT mutating the
     caller's original output vector.
   - `posix_journal::write` checks `ownedDirectory(directory)`
     again after directory fsync. A write may already have renamed
     the journal when this fails; the caller must treat durability
     as uncertain, never falsely ACK success.
   - GitHub header blob:
     `fc1b07631c5191e8681992fb3743482029c7d2ea`.

2. `a3380adc4749306694b302c678144e7f7f260fe5`
   - `tests/physis_journal_posix_io_test.cpp`
   - Verify failed size validation does not publish partial data;
     O_EXCL collision on an already-existing temp filename refuses
     the write without modifying temp or original receipt contents;
     chmod 0777 parent rejects BOTH read and write and preserves
     the caller's output vector.
   - Covers previously introduced symlink, FIFO, hardlink, mode,
     atomic replace and parent rename semantics.
   - GitHub test blob:
     `f3500d80dc7c99c389b12e0d8a40721e202d8048`.

3. `6bff1cad3097a1cbd287dc0e0b0f9ef15f712646`
   - `engine/physis/AGENTS.md`: Stage 50 ownership/test contract.
4. `1c56b4815a845d1de617ab58822097f31c0bc202`
   - `skills/world/aiverse-physis/SKILL.md`: Stage 50 skill guidance.

Existing `FileJournal::loadPinned/savePinned` continue to delegate
to these helpers; no ECS component, binary protocol version,
persistence schema ID, water/terrain/physics integration or genre
dependency changed.

## ACTUAL executed verification

- Local working copy:
  `/mnt/data/physis49_smoke/engine/physis/PhysisJournalPosixIO.h`
  and `/mnt/data/physis49_smoke/tests/physis_journal_posix_io_test.cpp`.
  Both `git hash-object` results matched the above GitHub blob SHAs.
- Ran `cmake --build /mnt/data/physis49_smoke/build --parallel 2`
  then `ctest --test-dir /mnt/data/physis49_smoke/build --output-on-failure`.
  Actual result:
  `1/1 Test #1: physis_journal_posix_io ... Passed`,
  `100% tests passed, 0 tests failed out of 1`.
  This CMake directory is the isolated source-identical standalone
  project, NOT the full AIVERSE root CMake project.
- Debian GCC 14.2 C++20, strict warning flags:
  `g++ -std=c++20 -O2 -Wall -Wextra -Werror -pedantic -I. tests/physis_journal_posix_io_test.cpp -o journal_io_stage50`
  Output: `PHYSIS POSIX journal I/O: PASS`.
- Clang 17 C++20 with same warning flags: PASS.
- GCC AddressSanitizer + UndefinedBehaviorSanitizer:
  `-fsanitize=address,undefined -fno-omit-frame-pointer`,
  `ASAN_OPTIONS=detect_leaks=1:abort_on_error=1`,
  `UBSAN_OPTIONS=halt_on_error=1` — PASS, exit 0.
- GitHub PR #25 branch HEAD after code:
  `a3380adc4749306694b302c678144e7f7f260fe5` when checked,
  with main behind=0. Workflow run `44487184893` was QUEUED,
  older runs CANCELLED; no completed full-engine CI PASS was available.
- Clone attempt from local container still fails DNS for
  `github.com`.

## NOT verified / limitations

- Full AIVERSE root CMake configuration and build, canonical
  `physis_admin_journal` WebSocket/process CTest, `physis_remote_edit`,
  `physis_field_runtime`, full PHYSIS suite, macOS/Windows editor.
  All need native integration test logs.
- This checks the filename at one final point; a malicious same-UID
  actor in a writable journal directory can still race the pathname
  again after the check. Restrict directory access to server.
- There is no cross-file atomicity between receipt sidecar and
  authoritative World snapshot. Unknown/Uncertain after a crash or
  persistence error remains the correct fail-closed result.
- POSIX journal target/path pinned to trusted dirfd; Windows journal
  I/O still requires ACL/reparse validation and a native test.
- Do not conflate canonical editable `FieldRuntimeState` with legacy
  `RuntimeFieldRegistry/FieldCommandHost`. NEREID owns water,
  TerraWeave terrain/foliage, Box3D physics; PHYSIS remains
  genre-independent.

## Exact next step for next AI agent

1. Inspect latest completed
   https://github.com/chromealex/AIVERSE/actions/workflows/physis.yml
   Ubuntu/macOS job and collect `physis-build.log`, CTest output and
   job statuses. Do not count queued/cancelled jobs as passed.
2. On a full native checkout with this feature branch, build at least
   `aiverse_physis_admin_journal_test`,
   `aiverse_physis_remote_edit_test`,
   `aiverse_physis_field_runtime_test`,
   `aiverse_physis_journal_posix_io_test`; run:
   `ctest --test-dir <build> --output-on-failure -R '^physis_'`.
   Repair ACTUAL compilation/runtime failures, including macOS.
3. Add integration test proving a replaced/truncated receipt path
   during startup/restore fails closed without applying new edits;
   add actual fault injection for post-rename directory fsync errors.
4. Complete Windows handle/ACL/reparse equivalence or document why
   privileged admin journal is POSIX-only.
5. Record results, current branch HEAD, precise changed files, API,
   outstanding risks and follow-up in a new dedicated handoff.
   Keep PR #25 DRAFT until genuine full-engine validation succeeds.
