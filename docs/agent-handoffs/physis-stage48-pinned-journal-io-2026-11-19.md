# PHYSIS Stage 48 — pinned-dirfd receipt journal I/O after main ancestry check (2026-11-19)

Repository: chromealex/AIVERSE
Branch: feature/physis-nature-simulation
PR #25 DRAFT: https://github.com/chromealex/AIVERSE/pull/25
Previous handoff: docs/agent-handoffs/physis-stage47-live-lock-integrity-2026-11-19.md
Latest code/docs commit BEFORE this handoff: 8693ef74940fb1b06860715e9745faa157e57e41

## User requirement — integrate main FIRST

Checked GitHub `main` SHA `9ee6ff6fbf390bd45363bc92c838f492dfe92eee`.
GitHub compare `base=main,head=feature/physis-nature-simulation`
returned `behind_by=0` and merge base exactly that main SHA:
the feature branch ALREADY contains all of main. A redundant merge
commit was not created and `main` itself was not changed.
The PHYSIS feature branch remains the sole write destination.

## Problem and API changes

Stage 47 validated the currently visible lock inode before journal
writes, but the POSIX FileJournal save path still resolved
`file_.string()` and its parent separately for creation, rename and
directory fsync. Parent renames between the ownership check and
filesystem operations could redirect writes into a different folder.

This stage binds privileged journal I/O to the directory descriptor
validated and opened during the original JournalProcessLock::acquire.
On the canonical admin endpoint (POSIX only) it also uses pinned
no-symlink startup reads. The existing journal byte layout and
Simulation save schema are unchanged.

Changes and commits:
- `1a1c924cc674a82fd7e0eff5a8c311be3e185599`:
  `engine/physis/PhysisJournalProcessLock.h`
  - Retain `parentFd_` until release, rather than closing immediately
    after successful flock.
  - `valid()` verifies both the pinned directory descriptor and
    visible lock/parent identities, owner, link count and modes.
  - Add POSIX `int directoryFd() const`: returns pinned directory fd
    only while `valid()`; -1 means lost ownership. Windows is
    intentionally unchanged.
  - Blob SHA: `2512aefb07f612c808041b92a21da897df4f195b`.

- `7ac207f29f7ca1873f5efc8f66fdc097af7fc986`:
  `engine/physis/PhysisRemoteJournal.h`
  - `FileJournal::savePinned(int parentFd, entries, error)` on POSIX:
    serializes exact legacy V1 journal bytes; creates a random temp
    filename with `openat(parentFd,...O_CREAT|O_EXCL...)`, writes
    exact bytes, fsyncs/closes file, `renameat` of temp to target in
    SAME directory fd, then `fsync(parentFd)`. Errors fail closed
    and cleanup temp filename through `unlinkat`.
  - The old path-based FileJournal::save remains for legacy callers.

- `dfa2ab6d8f77d4c08c30d3c8b4b6ee7baa0c5883`:
  `engine/physis/PhysisAdminEndpoint.h`
  - Privileged POSIX writer checks lock validity, resolves pinned fd,
    calls `savePinned`, and rechecks lock validity before admitting
    durability success. Windows remains on path-based FileJournal.

- `2e37f6d10d271639573fd1c757789250ec75b620`:
  `tests/physis_admin_journal_test.cpp`
  - `pinnedJournalSaveDoesNotFollowMovedParent` fixture. Direct
    method-level test writes to a pinned fd after renaming its parent,
    proves no file appears in replacement parent, and that the
    canonical lock reports the published path as invalid.
    Direct test of savePinned, NOT a permission for endpoints to
    continue I/O after lock loss.

- `6cd752d106a1a40d62aa3662361898e117193c17`:
  `engine/physis/PhysisRemoteJournal.h`
  - POSIX `FileJournal::loadPinned(int parentFd,gateway,error)`
    resolves startup journal using `openat`, O_NONBLOCK,
    O_NOFOLLOW when available, fstat owner/mode/regular/single-link
    and bounded size, reads with EINTR retry, validates existing
    checksum and restores original field-edit receipts.
    Missing file is a fresh empty journal.

- `c9defb2ee8da2f1a7819997a5c7eeabd7bf69f2f`:
  `engine/physis/PhysisAdminEndpoint.h`
  - Canonical POSIX startup uses loadPinned from verified lock dirfd,
    instead of path-based `FileJournal::attach`; installs its
    protected callback for subsequent writes as before.
    Windows retains legacy attach path pending ACL/reparse hardening.

- `2c81242095e156d9db2dd1fa46a73d63e92f3882`:
  `tests/physis_admin_journal_test.cpp`
  - `pinnedJournalLoadRejectsUnsafeInodes` tests absent/valid
    journal, unsafe mode (0644), symlink and FIFO refusal.
  - Both new POSIX fixtures registered in existing
    `physis_admin_journal` executable; no extra CMake target.

- `401059f771a2bc0f405f8e4ad0fa6aa3180a9df3`:
  `engine/physis/AGENTS.md` stage48 ownership and test policy.
- `8693ef74940fb1b06860715e9745faa157e57e41`:
  `skills/world/aiverse-physis/SKILL.md` new agent API guidance.

## Actual verification (distinguish source from runtime)

- **PASS isolated Linux GCC 14.2 C++20 lock smoke**:
  local `/mnt/data/physis47_smoke/PhysisJournalProcessLock.h`
  reconstructed via explicit source edits from the prior exact
  repository header; `git hash-object` =
  `2512aefb07f612c808041b92a21da897df4f195b`,
  identical to GitHub's new lock header blob SHA.
  `g++ -std=c++20 -O2 -Wall -Wextra -Werror -pedantic smoke.cpp -o smoke48`;
  `timeout 15 ./smoke48` exited 0, running existing
  acquisition, identity, FIFO, hardlink, chmod and SIGKILL cases.
- **PASS isolated Linux pinned-directory primitive smoke**:
  `g++ -std=c++20 -Wall -Wextra -Werror -pedantic pinned_smoke.cpp -o pinned_smoke`,
  `timeout 15 ./pinned_smoke` exited 0 and printed:
  `PASS: native pinned-dirfd save uses original directory despite parent rename, and lock refuses further privileged access`.
  This uses the exact lock header and a small stand-alone openat/
  renameat harness, **not** the whole FileJournal implementation.
- **PASS GitHub connector writes/inspection**: all source/test/docs
  commits returned SHAs. Compared `main` ancestry before and after
  changes, behind 0.
- **NOT RUN**: Full native AIVERSE CMake compile, integrated
  FileJournal::loadPinned/savePinned CTest, WebSocket admin, full PHYSIS
  CTest suite, macOS, Windows, editor, sanitizer. Local container
  `git ls-remote` still failed due to github.com DNS resolution.
  PHYSIS GitHub Actions checks were queued or superseded/cancelled,
  with no recent completed native PASS.

## Remaining risks and boundaries

- Pinned directory fd closes the parent path redirection problem,
  but a malicious SAME-UID writer permitted to mutate the trusted
  directory may still race the target basename/lock between checks.
  This is not a distributed fencing token. Private server-only
  directory ownership is a strict operational requirement.
- The canonical endpoint rechecks journalLock.valid before/after
  protected save; a failed post-write ownership check leaves
  receipt durability uncertain and gateway suspends new edits.
- `FileJournal::save/load/attach` remain public legacy path-based
  APIs and do not automatically acquire a JournalProcessLock.
  Keep privileged consumers on canonical LoopbackAdminEndpoint.
- Windows path-based journal I/O is unchanged and needs native
  handle-based directory/file identity and ACL/reparse checks.
- The sidecar and authoritative World checkpoint remain separate
  non-atomic persistence domains. Unknown/Conflict on crash
  ambiguity remains correct; do not claim exactly-once delivery.
- PHYSIS remains genre-independent. NEREID water, TerraWeave
  terrain/foliage and Box3D physics have their own owners; do not
  introduce cross-genre ECS coupling.
- Canonical editable FieldRuntimeState cannot be installed with
  parallel legacy RuntimeFieldRegistry/FieldCommandHost.

## Exact next steps for next agent

1. Run Ubuntu/macOS `physis.yml` CI until a completed job provides
   compiler + CTest logs (do not call queued/cancelled a pass).
2. On a checkout of this branch, run:
   ```sh
   cmake -S . -B build/physis48 -DCMAKE_BUILD_TYPE=Debug \
      -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_ENABLE_PHYSICS=ON \
      -DAIVERSE_BUILD_EDITOR=OFF -DAIVERSE_BUILD_TOOLS=OFF \
      -DAIVERSE_BUILD_CLIENT=OFF -DAIVERSE_BUILD_HEADLESS=OFF \
      -DAIVERSE_BUILD_SERVER=OFF -DAIVERSE_BUILD_PHYSICS_DEMO=OFF
   cmake --build build/physis48 --parallel 4 --target \
      aiverse_physis_admin_journal_test \
      aiverse_physis_remote_edit_test \
      aiverse_physis_field_runtime_test
   ctest --test-dir build/physis48 --output-on-failure \
      -R '^physis_(admin_journal|remote_edit|field_runtime)$'
   ```
3. Fix any compiler issues from POSIX `openat/renameat` and tests;
   examine macOS `O_NOFOLLOW` and directory fsync behavior.
4. Add admin endpoint restart test with real FileJournal::loadPinned,
   verify corrupted/symlink receipt path fails closed and no false
   Applied ACK. Run all PHYSIS CTests and real native editor smoke.
5. Document actual tests, exact branch SHA, current limitations and
   next step in a new dedicated handoff; PR #25 remains DRAFT.
