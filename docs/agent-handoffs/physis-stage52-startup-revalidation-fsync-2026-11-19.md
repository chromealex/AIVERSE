# PHYSIS Stage 52 — startup lock revalidation and post-rename fsync fault (2026-11-19)

Repository: chromealex/AIVERSE
Branch: feature/physis-nature-simulation
PR #25: https://github.com/chromealex/AIVERSE/pull/25 (DRAFT)
Previous stage: docs/agent-handoffs/physis-stage51-pinned-startup-2026-11-19.md
Feature branch SHA just before handoff: edf2fe1ec86190d4c8a818440f1fc93f4fb78f7d

## Goal and observed state

- User approved continuing native PHYSIS test work.
- Checked PR #25 and the GitHub Actions branch runs: newest inspected
  run `44488377481` was pending and all completed runs on the first
  page were CANCELLED, not successful completed CMake/CTest builds.
  Do not call these PASS. Feature branch remained 0 commits behind main.
- Attempted local `git ls-remote https://github.com/chromealex/AIVERSE.git HEAD`,
  but the container cannot resolve github.com, so full AIVERSE
  checkout and native engine compilation could not proceed.
- Re-ran existing isolated source-identical
  `/mnt/data/physis49_smoke` CMake/CTest before implementing changes:
  `1/1 physis_journal_posix_io ... Passed`.

## Code and tests added

1. Commit `59b35a35dc17ad9aa0d4496ab62285779fea8f90`:
   - `engine/physis/PhysisAdminEndpoint.h`.
   - In canonical `LoopbackAdminEndpoint::open`, after
     `FileJournal::loadPinned` or legacy Windows attach and BEFORE
     calling `WsListener::open`, revalidate
     `JournalProcessLock::valid()`.
   - If lock/parent identity changed during reading: call
     `ServerGateway::suspendJournal()`, release lock, return false
     with a diagnostic. This prevents binding an admin listener
     after a successful decode from a no-longer-owned journal.
   - Existing authenticated ACL, v1 receipt protocol and ECS
     serialization are unchanged; Windows `valid()` still means
     held handle only, not a full ACL/reparse identity check.

2. Commit `f26813f2f68758601177f49f7d9f4a8a8105c106`:
   - `tests/physis_journal_posix_io_test.cpp`.
   - Add Linux-only `#ifdef O_PATH` deterministic fault injection
     using a directory descriptor that resolves `openat` and
     `renameat` but cannot `fsync(dirfd)` (EBADF).
   - Assert `posix_journal::write` returns false with an fsync
     diagnostic even though the new file is visible. This tests the
     distinction between atomic pathname replacement and confirmed
     crash durability.
   - No extra CMake test target. Existing
     `physis_journal_posix_io` is already in CMake and workflow.
   - Source blob SHA:
     `c9c7063481b15d9e3d96fc3c8b022e86a32f30ff`.

3. Commit `b73298a28f20ffef33881ca604bc1d534e73bc67`:
   - `engine/physis/AGENTS.md`: record lock revalidation and honest
     CTest status for agents.
4. Commit `edf2fe1ec86190d4c8a818440f1fc93f4fb78f7d`:
   - `skills/world/aiverse-physis/SKILL.md`: Stage 52 API and
     test guidance.

## Tests ACTUALLY executed (Linux native, isolated)

The /mnt/data/physis49_smoke workspace contained the prior
source-identical standalone header and test. Confirmed the current
header blob, and applied the exact test source change so
`git hash-object tests/physis_journal_posix_io_test.cpp` produced
`c9c7063481b15d9e3d96fc3c8b022e86a32f30ff`,
matching the connected GitHub source blob SHA.

```sh
cmake --build /mnt/data/physis49_smoke/build -j2
ctest --test-dir /mnt/data/physis49_smoke/build --output-on-failure
```

ACTUAL output:
`1/1 Test #1: physis_journal_posix_io ... Passed`,
`100% tests passed, 0 tests failed out of 1`.

Additional exact-source tests:
- Clang 17.0.0, C++20, `-O2 -Wall -Wextra -Werror -pedantic`:
  `PHYSIS POSIX journal I/O: PASS`.
- GCC 14.2 C++20 `-fsanitize=address,undefined`,
  `-fno-omit-frame-pointer`, ASAN leak check and UBSAN halt:
  `PHYSIS POSIX journal I/O: PASS`, exit 0.

**NOT RUN/NOT VERIFIED:** full AIVERSE root CMake, integrated
`physis_admin_journal` CTest including the new post-load guard,
`physis_remote_edit`, `physis_field_runtime`, complete PHYSIS
suite, macOS and Windows native tests, production editor/WebSocket,
network security or end-to-end crash consistency. Isolated
`physis_journal_posix_io` CTest is NOT a full engine PASS.

## Ownership and API boundaries

- The canonical privileged host still acquires the single-owner
  journal lock, loads a checksummed V1 receipt journal via pinned
  dirfd, and must keep the lock valid before listener bind and
  at each protected write/ingress/tick.
- FileJournal + Simulation World checkpoints remain distinct
  persistence domains, not a crash-atomic transaction. An IO/fsync
  failure can leave a visible file but requires Unknown/Uncertain
  receipt semantics; never claim exactly-once durable delivery.
- Direct FileJournal::save/load/attach are legacy and do not
  automatically enforce canonical endpoint lock ownership.
- Canonical FieldRuntimeState/Editable cannot coexist with legacy
  RuntimeFieldRegistry/FieldCommandHost. PHYSIS nature is
  genre-independent. NEREID owns water, TerraWeave terrain/foliage,
  Box3D physics.

## Next concrete step

1. Inspect latest completed Ubuntu/macOS runs at
   https://github.com/chromealex/AIVERSE/actions/workflows/physis.yml
   and read actual build/CTest artifact logs.
2. On an AIVERSE native checkout build
   `aiverse_physis_admin_journal_test`,
   `aiverse_physis_remote_edit_test`,
   `aiverse_physis_field_runtime_test` and
   `aiverse_physis_journal_posix_io_test`, then run
   `ctest --test-dir <build> --output-on-failure -R '^physis_'`.
3. Fix real compiler or runtime errors. In particular validate
   the newly added `LoopbackAdminEndpoint::open` second lock check
   under macOS/Linux and fault-injection for journal fsync failures.
4. Consider a deterministic high-level fault hook for a lock inode
   replacement between loadPinned and listener bind, or test it
   through a controlled concurrent process. Do not add flaky sleeps.
5. Create the next per-stage handoff with current branch SHA,
   changed files, actual test outcomes and limitations.
   Keep PR #25 DRAFT until full native validation succeeds.
