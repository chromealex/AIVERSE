# PHYSIS Stage 27 — exclusive admin journal process ownership (2026-11-19)

Repository: chromealex/AIVERSE
Branch: feature/physis-nature-simulation
PR #25 remains DRAFT.
Code commits: 607d68a0032c5ebe632fb739ccfbf6aef2a477e8, 9f7260260a5a9c81c5f982c05c43ed6d869ba86e.

## Changes
- New engine/physis/PhysisJournalProcessLock.h: noncopyable RAII exclusive advisory lock on <journal>.lock; POSIX flock(LOCK_EX|LOCK_NB), Windows LockFileEx(LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY). Keeps OS handle open for lifetime; unlocks on destruction. POSIX O_NOFOLLOW when supported, O_CLOEXEC when supported.
- engine/physis/PhysisAdminEndpoint.h: LoopbackAdminEndpoint::open acquires lock BEFORE journal attach and listener bind. Failure to attach or bind releases lock; successful endpoint holds it through object lifetime.
- No PHYSIS save schema, genre dependencies, gameplay ECS or NEREID/Terra ownership changes.

## Security and limitations
- Advisory locking only: legacy/direct FileJournal::save users must cooperate; this does not lock arbitrary external file writes.
- Lock file must be in a server-controlled directory; no full path traversal / parent symlink defense, no cross-machine distributed fencing.
- Do not unlink the lock file during normal operation: replacing the inode could permit a second lock.
- Do not call open twice on one endpoint without lifecycle review. No explicit close/reopen API added.
- FileJournal and Simulation checkpoint remain separate persistence domains.
- Two parallel authored field registry paths remain unresolved. Canonical admin path is FieldRuntimeState/installEditableFromMap.

## Verification
- PASS: GitHub create_file and update_file returned commit SHAs.
- NOT RUN: C++ compile, CTest, macOS/Linux/Windows cross-process lock tests, real websocket endpoint, CI, sanitizer and security QA. No assertion of runtime correctness.

## Next agent
1. Compile PHYSIS on macOS/Linux/Windows; ensure Windows headers and POSIX flock behavior compile.
2. Add native two-process test: first endpoint acquires, second fails, first closes/dies, second succeeds; also invalid parent, journal load failure, listener bind failure, and symlink lock file.
3. Add a host-controlled lifecycle close/reopen policy if required; do not release the lock while listener/peers remain live.
4. Audit all FileJournal::save direct callers and ensure they cannot bypass process ownership in production.
5. Reconcile FieldRuntimeState versus RuntimeFieldRegistry API and test authoritative replay/rollback.
6. Update AGENTS/skill and per-stage handoff with actual tested evidence, exact SHA and next step. Keep PR draft.
