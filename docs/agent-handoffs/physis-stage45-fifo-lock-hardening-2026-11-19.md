# PHYSIS Stage 45 — FIFO-safe POSIX journal lock opening (2026-11-19)

Repository: chromealex/AIVERSE
Branch: feature/physis-nature-simulation
PR #25: https://github.com/chromealex/AIVERSE/pull/25 (DRAFT)
Previous handoff: docs/agent-handoffs/physis-stage44-manual-retry-2026-11-19.md
Related concurrent branch work: docs/agent-handoffs/physis-stage40-lock-inode-security-2026-11-19.md

## Goal and issue

The existing POSIX `JournalProcessLock::acquire` called
`open(<journal>.lock, O_RDWR|O_CREAT, 0600)` before validating the
opened inode. Earlier work added `fstat` checks rejecting non-regular
inodes, wrong owner, group/other permissions and multiple hardlinks.
But on implementations where opening a FIFO read/write may wait for
another process, the server could block BEFORE reaching the guard.
A privileged admin endpoint must fail closed without waiting on an
untrusted lock-file object.

## Production implementation

- Commit: `be5769a00ebc3f0c7ca8099f6f9332b9ae3510eb`.
- File: `engine/physis/PhysisJournalProcessLock.h`.
- POSIX `open` now uses `O_RDWR|O_CREAT|O_NONBLOCK`, plus existing
  `O_CLOEXEC` and `O_NOFOLLOW` where supported. For a validated
  ordinary regular file, O_NONBLOCK has no material effect on flock.
  The existing `fstat`, single-link, effective UID, mode and
  `flock(LOCK_EX|LOCK_NB)` guards remain intact.
- No changes to Windows, serialized ECS resources, runtime protocols
  or the FileJournal binary format. No FPS/RTS, NEREID, TerraWeave,
  Box3D or editor-specific dependencies introduced.

## Test fixture implementation

- Commit: `104ee50a830c737634023464dcc9743fb5ea6a66`.
- File: `tests/physis_admin_journal_test.cpp`.
- Extended the existing POSIX-only `unsafeJournalLockInodesRejected`
  fixture: after checking dangerous mode/hardlinks, remove the normal
  lock, create a FIFO with `mkfifo(...,0600)`, then assert
  `JournalProcessLock::acquire` returns false without holding a lock.
- `#include <sys/stat.h>` is now explicit in this CTest source.
  Existing `main` registration and CMake `physis_admin_journal`
  target remain unchanged.

## Actual verification (be precise)

**PASS: isolated Linux smoke**

- Downloaded the current full `PhysisJournalProcessLock.h` contents
  through the GitHub connector and placed them in a separate local
  /mnt/data/physis45_smoke/ workspace.
- `git hash-object PhysisJournalProcessLock.h` printed
  `835cfb9630e66d7918ed91ef06f97f590c2007b5`, exactly matching
  the GitHub blob SHA for the production header after the code commit.
- Compiled against that exact header on Linux using
  `g++ (Debian 14.2.0-19)`:
  `g++ -std=c++20 -O2 -Wall -Wextra -Werror -pedantic smoke.cpp -o smoke`
- Executed `timeout 15 ./smoke`. Real output:
  `PASS: exact GitHub header (blob 835cfb9), FIFO rejected without stall, mode/hardlink checks, cross-process SIGKILL release`.
- The isolated harness covered safe first acquisition, competing
  acquisition denial, mode 0666 refusal, hardlink refusal, safe
  reacquisition, FIFO refusal without a stall, and exclusion across
  forked processes with reacquisition after SIGKILL.

**PASS: connector source changes only**

- GitHub update_file returned the two code/test commits and
  docs commits `ccd8e9caddb12ae663bca8955ece2780ef1fc375` and
  `347fa652ec0f931a1cda81d653b52099427118b3`.
- Re-fetched source and verified the updated O_NONBLOCK flag, FIFO
  fixture and its main registration.

**NOT RUN / unverified**

- Full AIVERSE CMake configure/build and the
  `aiverse_physis_admin_journal_test` CTest executable.
- Native macOS/Windows builds, actual editor start, WebSocket transport,
  sanitizer, integrated server crash/recovery, Windows ACL/reparse tests.
- The local container still cannot clone GitHub due to DNS:
  `git ls-remote https://github.com/chromealex/AIVERSE.git ...`
  returned `Could not resolve host: github.com`.
- Latest PHYSIS GitHub Actions jobs at inspection time were queued,
  and older attempts were cancelled. No new complete CI PASS.

## Limitations and next steps

- POSIX O_NOFOLLOW only rejects the final symlink component where
  supported; parent-directory traversal, mount changes and active
  inode replacement require further design/verification. Advisory
  flock is not a distributed multi-host fence, and it does not protect
  noncooperating direct FileJournal writers.
- Protect the journal directory as a private server-controlled path.
  Deleting/replacing a lock inode while any endpoint is active can
  undermine mutual exclusion.
- Stage 44 remote Unknown/Uncertain handling and journaling remain
  separate from this inode-opening hardening. Do not reimplement them.
- Run native CI/CTest on Ubuntu and macOS using the newest branch SHA,
  collect compiler and test logs, fix any failures, then add Windows
  process-level ACL/reparse and replacement-risk tests as appropriate.
- Example commands:
  ```sh
  cmake -S . -B build/physis45 -DCMAKE_BUILD_TYPE=Debug \
      -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_ENABLE_PHYSICS=ON \
      -DAIVERSE_BUILD_EDITOR=OFF -DAIVERSE_BUILD_TOOLS=OFF \
      -DAIVERSE_BUILD_CLIENT=OFF -DAIVERSE_BUILD_HEADLESS=OFF \
      -DAIVERSE_BUILD_SERVER=OFF -DAIVERSE_BUILD_PHYSICS_DEMO=OFF
  cmake --build build/physis45 --parallel 4 --target \
      aiverse_physis_admin_journal_test aiverse_physis_remote_edit_test \
      aiverse_physis_field_runtime_test
  ctest --test-dir build/physis45 --output-on-failure \
      -R '^physis_(admin_journal|remote_edit|field_runtime)$'
  ```
- Keep PR #25 DRAFT until full native evidence exists. Each next
  implementation stage must include exact changed paths, current
  branch/commit, API, honest test statuses and the next action.
