# PHYSIS Stage 26 follow-up — directory durability fail-closed (2026-11-19)

Repository: chromealex/AIVERSE
Branch: feature/physis-nature-simulation
PR: #25 (DRAFT)
Implementation commit: 03ef1de540e181fc867b5ce16bfe413e24bc0033

## What changed
- engine/physis/PhysisRemoteJournal.h: after atomic POSIX rename, a failure to open the containing directory for fsync now returns false instead of silently reporting journal save success. Failed directory fsync or close also returns false.
- No public API, journal schema, gameplay component, or default configuration changed.
- No competing runtime registry, journal, or water/terrain solver introduced.

## Why
Successful file fsync + rename does not establish rename durability without a directory fsync. Previously an unsuccessful directory open was silently ignored, allowing the admin gateway to believe the receipt was durable. The writer now fails closed and callers can suspend privileged editing.

## Actual verification
- GitHub connector update_file succeeded and returned implementation commit 03ef1de540e181fc867b5ce16bfe413e24bc0033.
- NOT RUN: native compilation, CTest, OS failure injection, GitHub Actions, macOS editor or network QA. Do not mark tests PASS.
- The rename already happened when the directory sync fails. A failed save therefore signals **uncertain durability**, not a guaranteed rollback of disk state.

## Known limits
- Windows MoveFileEx durability semantics remain separate.
- Journal and authoritative ECS checkpoint are still not a coordinated transaction.
- Directory ownership, symlink/path hardening, single-process lock/fencing, and reliable failure-injection tests remain necessary.
- Keep PHYSIS genre-independent. NEREID owns water, TerraWeave terrain/foliage, Box3D physics.

## Next agent
1. Fetch this branch and run `cmake -S . -B build/physis26 -DAIVERSE_BUILD_TESTS=ON`, build `aiverse_physis_admin_journal_test`, then `ctest --test-dir build/physis26 --output-on-failure -R '^physis_(admin_journal|remote_edit|field_runtime)$'`.
2. Add injectable filesystem durability hooks or a scoped fake for open/fsync/close error paths, verifying fail-closed behavior and suspended admin submissions.
3. Implement exclusive journal ownership and avoid trusting mutable symlink paths; add crash/restart integration tests.
4. Reconcile duplicate FieldRuntimeState vs RuntimeFieldRegistry installation paths.
5. Update root/PHYSIS agent docs if contracts change, and record exact tested results and branch SHA after the next stage.
