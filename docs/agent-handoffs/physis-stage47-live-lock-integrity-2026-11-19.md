# PHYSIS Stage 47 — live admin journal lock integrity and fail-closed loss handling (2026-11-19)

Repository: chromealex/AIVERSE
Branch: feature/physis-nature-simulation
PR #25: https://github.com/chromealex/AIVERSE/pull/25 (DRAFT)
Previous handoff: docs/agent-handoffs/physis-stage46-lock-parent-integrity-2026-11-19.md
Last commit before this handoff: a2feb991554a370af26f1e9db0af6b0fe71f5254

## Goal / discovered failure mode

An advisory POSIX flock protects the **open inode**. Even after Stage
46's initial integrity checks, another process running as the same
user could rename/unlink the published `<journal>.lock` file, create
a different inode at the original name, and acquire a separate flock.
The still-running endpoint would otherwise continue to accept and
journal remote admin edits under an obsolete lock. Parent chmod
after acquisition can likewise invalidate directory trust.

## Production architecture and committed changes

1. `ccf31b0bbb7fd4d11b084a8b0a4c4dacf48a198d`
   - `engine/physis/PhysisJournalProcessLock.h`
   - New `bool valid() const` checks that the still-published POSIX
     lock pathname resolves to the same dev/inode as the held fd,
     file is regular/single-link/current UID/private, and the
     parent path still has its original device/inode and current
     owner/private permissions.
   - Store parent path/lock path + parent dev/ino on successful
     acquire; clear metadata on release.
   - On Windows `valid()` currently equals `held()`, explicitly
     NOT equivalent to POSIX identity/ACL/reparse verification.
   - GitHub blob SHA for this exact header:
     `040fedb623d491428057542eee2f8fae6bcc2a28`.

2. `cfcf14e0cd2f13e166840daaf0b5988451e390f7`
   - `engine/physis/PhysisRemoteServer.h`
   - Add `ServerGateway::suspendJournal()`, a sticky fail-closed
     journal health transition. Once unhealthy, new submissions
     are rejected and uncertain positive reply statuses are masked
     as Unknown by the existing Stage 42 implementation.

3. `e28ca0ab7c3700b1599eaee3477e4ee3b1c7a4ec`
   - `engine/physis/PhysisAdminEndpoint.h`
   - `LoopbackAdminEndpoint::poll` checks lock validity BEFORE
     processing any new or existing privileged request; a bad path
     suspends gateway journal health.
   - `afterTick` checks before ECS receipt reconciliation.
   - Override gateway's journal writer with a callback that checks
     valid() immediately before every `FileJournal::save` call.
   - New `journalHealthy() const` reflects both lock identity and
     persistent gateway health. Journal-loss recovery requires a
     new safely initialized endpoint; merely repairing a filename
     or chmod does not clear sticky suspension.
   - No new ECS schema, network packet, receipt enum, or persistence
     manifest was introduced.

4. `27b0fcdb3b224b2b11ef2e4e04e80879ce28fe03`
   - `tests/physis_admin_journal_test.cpp`
   - POSIX `replacedLiveLockSuspendsAdmin`: start a real endpoint,
     rename its still-locked inode, acquire a separate replacement
     lock, assert endpoint health loss, no journal write, fresh
     authenticated command Rejected, ECS revision remains 0.
   - POSIX `liveParentPermissionChangeSuspendsAdmin`: chmod
     the live journal parent 0777, assert path invalid, new writes
     fail closed; restoring 0700 does NOT unsuspend the gateway.
   - Both registered in existing `physis_admin_journal` CTest
     target, with no CMake target or genre dependencies added.

5. `1e59805125d629117280afc128bbab503d2a34ad`
   - `engine/physis/AGENTS.md`: subsystem policies and limitations.
6. `a2feb991554a370af26f1e9db0af6b0fe71f5254`
   - `skills/world/aiverse-physis/SKILL.md`: AI agent guidance.

## Tests actually performed — do not overstate

- **PASS: isolated native Linux header smoke.** Local workspace
  `/mnt/data/physis47_smoke` contains the exact current header.
  `git hash-object PhysisJournalProcessLock.h` =
  `040fedb623d491428057542eee2f8fae6bcc2a28`
  matching the connected GitHub source blob SHA.
- Executed with Debian GCC 14.2:
  `g++ -std=c++20 -O2 -Wall -Wextra -Werror -pedantic smoke.cpp -o smoke`
  and `timeout 15 ./smoke`. Exit code **0**. Printed:
  `PASS: exact GitHub header Stage47, live rename drift and chmod detection plus Stage46 process/FIFO/inode coverage`.
- Isolated smoke tested POSIX regular/single-link/private inode,
  FIFO nonblocking denial, hardlink/mode denial, untrusted parent
  mode and symlink denial, two-process flock exclusion, SIGKILL
  release/reacquire, live lock rename and live parent chmod.
- GitHub source/test/skill commits were created successfully;
  re-fetched the authoritative branch to confirm `valid()`,
  endpoint writer guard, and test registration.
- **NOT RUN / NOT VERIFIED:** Full AIVERSE native CMake configure and
  compilation, `physis_admin_journal` CTest integration, authenticated
  WebSocket runtime, Ubuntu/macOS/Windows test jobs, sanitizer and
  actual editor. A local git fetch from github.com still failed DNS
  in this environment. The PHYSIS Actions runs inspected were
  queued or cancelled, not completed build/CTest PASS.

## Security and functional limitations

- This is best-effort identity validation **at ingress, tick and
  write boundaries**, not a capability against races BETWEEN checks
  and filesystem operations. A malicious same-UID process can still
  race a pathname; protecting the containing directory against
  untrusted writes is a mandatory host policy.
- The FileJournal sidecar and authoritative Simulation World
  checkpoint remain separate persistence domains and do not offer
  crash-atomic exactly-once status.
- Direct FileJournal callers can bypass endpoint guards; they must
  coordinate ownership or be refactored to a hardened handle-based
  journal API if stronger guarantees are needed.
- Windows `JournalProcessLock::valid()` only tests open handle
  presence; native Windows ACL, reparse, inode identity and
  independent-process tests remain open.
- Canonical editable `FieldRuntimeState` cannot coexist with legacy
  `RuntimeFieldRegistry/FieldCommandHost`. Genre-independent PHYSIS
  owns nature, NEREID water, TerraWeave terrain/foliage, Box3D physics.

## Exact next steps for another agent

1. Check latest workflow run on
   https://github.com/chromealex/AIVERSE/actions/workflows/physis.yml.
   Collect **completed** macOS/Ubuntu build+CTest logs before calling
   anything PASS.
2. In a local checkout of this exact branch:
   ```sh
   cmake -S . -B build/physis47 -DCMAKE_BUILD_TYPE=Debug \
       -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_ENABLE_PHYSICS=ON \
       -DAIVERSE_BUILD_EDITOR=OFF -DAIVERSE_BUILD_TOOLS=OFF \
       -DAIVERSE_BUILD_CLIENT=OFF -DAIVERSE_BUILD_HEADLESS=OFF \
       -DAIVERSE_BUILD_SERVER=OFF -DAIVERSE_BUILD_PHYSICS_DEMO=OFF
   cmake --build build/physis47 --parallel 4 --target \
       aiverse_physis_admin_journal_test \
       aiverse_physis_remote_edit_test \
       aiverse_physis_field_runtime_test
   ctest --test-dir build/physis47 --output-on-failure \
       -R '^physis_(admin_journal|remote_edit|field_runtime)$'
   ```
3. Fix the first actual compiler/CTest error; then run all
   `^physis_` tests including optional real Box3D and editor.
4. Consider moving FileJournal operations to an already-verified
   dirfd/handle to close the residual check-to-save path race.
   Add Windows `GetFileInformationByHandle` identity/ACL/reparse
   checks and independent process tests.
5. Publish a fresh per-stage handoff with exact changed files,
   branch SHA, actual test logs, limitations and concrete next step.
   Keep PR #25 DRAFT until genuinely validated.
