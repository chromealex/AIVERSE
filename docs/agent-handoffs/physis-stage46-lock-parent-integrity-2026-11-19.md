# PHYSIS Stage 46 — anchored trusted journal parent and post-flock inode verification (2026-11-19)

Repository: chromealex/AIVERSE
Branch: feature/physis-nature-simulation
PR #25: https://github.com/chromealex/AIVERSE/pull/25 (DRAFT)
Previous handoff: docs/agent-handoffs/physis-stage45-fifo-lock-hardening-2026-11-19.md
Latest code/docs commit before this handoff: c396e75861e2bd055681c2e0fe1531206ee95348

## Goals and security boundary

Stage 45 rejected unsafe POSIX lock inodes and FIFO startup hangs, but
the parent path used to be verified only by
`std::filesystem::is_directory(parent)` (which may follow a symlink and
does not enforce ownership/permissions). A second issue was that the
locked descriptor could differ from the still-visible basename if a
replacement happened during lock acquisition.

The journal is privileged local server state; its DIRECT parent must
be a real effective-UID-owned directory that is not group/world
writable. Path identity needs a second check after acquiring flock.

## Actual code, tests and commits

1. `3cb2231cdcca1ba8ab675baaf18a701163fa0fe2`
   - `engine/physis/PhysisJournalProcessLock.h`.
   - POSIX: open parent as a directory descriptor with O_NOFOLLOW,
     O_DIRECTORY and O_CLOEXEC where available; `fstat` must confirm
     S_ISDIR, same effective UID and `st_mode & 0022 == 0`.
   - Use `openat(parentFd, basename, O_RDWR|O_CREAT|O_NONBLOCK
     [plus optional O_NOFOLLOW/O_CLOEXEC], 0600)` for lock creation.
   - Preserve existing regular-file, single hardlink, same effective
     owner, no group/other access checks and flock(LOCK_EX|LOCK_NB).
   - After flock, use fstatat(AT_SYMLINK_NOFOLLOW) and lstat(parent)
     to require visible lock dev/ino and direct parent dev/ino to match
     opened descriptors. Close all descriptors and fail closed on
     mismatches or any earlier failure.
   - No ECS schema, network protocol, FileJournal format or Windows
     behavior changed.

2. `969d51cd82a69a24d21acb68ba22fd56f3d26892`
   - `tests/physis_admin_journal_test.cpp`.
   - New `unsafeJournalLockParentsRejected` POSIX fixture checks:
     world-writable direct parent denies acquisition; changing the
     parent to mode 0700 allows it; a symlinked direct parent is denied.
   - Added to existing `physis_admin_journal` main under
     `!_WIN32 && !__EMSCRIPTEN__`, no CMake target change.

3. `5bfe9c13fa327d31d3c283548424416deb7c1e8b`
   - `engine/physis/AGENTS.md`: document anchored directory policy
     and limits.
4. `c396e75861e2bd055681c2e0fe1531206ee95348`
   - `skills/world/aiverse-physis/SKILL.md`: update agent instructions.

## Actual verification

PASS (isolated C++ harness, NOT full engine):
- A local copy of the exact new repository header in
  `/mnt/data/physis45_smoke_stage46/PhysisJournalProcessLock.h` had
  `git hash-object` = `3fcd9f029bc7cc71f939c4471828acfbc358b843`.
  This matches the GitHub connector's blob SHA returned by the code update.
- Debian GCC C++20 compilation:
  `g++ -std=c++20 -O2 -Wall -Wextra -Werror -pedantic smoke.cpp -o smoke`
- Execution: `timeout 15 ./smoke`
- Actual printed result:
  `PASS: exact GitHub header (blob 3fcd9f0), trusted parent checks, symlink denied, FIFO safe, inode modes/hardlinks and SIGKILL replay`.
- This harness tested a safe effective-UID-owned parent, group/world
  writable parent rejection, direct-parent symlink rejection, FIFO
  rejection, lock mode 0666 rejection, hardlink rejection, competing
  processes, crash (SIGKILL) and post-crash reacquisition.
- Source commits and test presence were confirmed through GitHub.

NOT RUN / unverified:
- Full AIVERSE CMake/CTest, `physis_admin_journal` integrated test,
  macOS/Windows, editor and production server.
- Attempting native git checkout from the local container:
  `git ls-remote https://github.com/chromealex/AIVERSE.git HEAD`
  failed: `Could not resolve host: github.com`.
- At the start of this stage GitHub Actions had 174 PHYSIS runs;
  page one contained 77 queued, 23 completed (cancelled) and no PASS.
  The latest at inspection was queued. These are NOT build errors or
  successful test evidence.

## Limits / dependencies

- Acquisition-time check does not continuously detect a same-user
  attacker replacing or unlinking the lock after successful acquire.
  Guarantee requires private directory ownership for whole lifetime.
- Direct parent is checked; intermediate directory symlink/mount
  traversal is not independently pinned. If the application allows
  untrusted ancestors, further path-hardening is required.
- `AT_SYMLINK_NOFOLLOW`, `openat`, `fstatat` are used for native POSIX
  targets. macOS compatibility must be compiler-verified; Windows
  reparse points and DACL/owner checks remain unimplemented.
- Advisory flock alone does not prevent noncooperating direct writes
  or distributed multi-host split-brain; separate World/journal files
  do not form one crash-atomic transaction.
- Maintain genre-independent PHYSIS. NEREID owns canonical water,
  TerraWeave owns terrain/foliage, Box3D owns physics.
- Canonical editable `FieldRuntimeState` cannot coexist with legacy
  `RuntimeFieldRegistry/FieldCommandHost`.

## Concrete next step

1. Obtain actual CI jobs/logs for newest branch HEAD; do not infer
   CTest success from queued/cancelled workflows.
2. Run at least:
   ```sh
   cmake -S . -B build/physis46 -DCMAKE_BUILD_TYPE=Debug \
      -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_ENABLE_PHYSICS=ON \
      -DAIVERSE_BUILD_EDITOR=OFF -DAIVERSE_BUILD_TOOLS=OFF \
      -DAIVERSE_BUILD_CLIENT=OFF -DAIVERSE_BUILD_HEADLESS=OFF \
      -DAIVERSE_BUILD_SERVER=OFF -DAIVERSE_BUILD_PHYSICS_DEMO=OFF
   cmake --build build/physis46 --parallel 4 --target \
      aiverse_physis_admin_journal_test aiverse_physis_remote_edit_test \
      aiverse_physis_field_runtime_test
   ctest --test-dir build/physis46 --output-on-failure \
      -R '^physis_(admin_journal|remote_edit|field_runtime)$'
   ```
3. Audit continuous lock-path integrity at polling / before writes
   (same-user inode replacement after acquisition), with a clear
   fail-closed policy. Add test for a replaced live lock inode.
4. Add Windows-specific secure directory owner/reparse enforcement
   and native Windows process-level test, subject to platform tools.
5. Run all PHYSIS CTests, record logs and commit SHA, and keep PR #25
   DRAFT until genuine native test evidence is available.

Per-stage handoff is mandatory and must distinguish authored tests
from tests that actually ran.
