# W1.23y — safe unified backup retention and managed session

Area W1, branch `feature/aiv20261123_open_world_audit`, Draft PR #116. Base 58f04634e423303a10cc8ad10c7b44b6ebcd8b01.

## Implemented
- 84d3bf7d: roadmap item in progress.
- 6be310ac, 96a1ff2b, c0e92ed7: extract read-only recovery into `engine/world/UnifiedBackupRead.h`, reject nonregular checkpoint/WAL paths; keep existing `UnifiedBackupRecovery::recover/fork` surface; remove catalog/session include cycle.
- 6295281d: shared `RegionCheckpointRetention` eligibility predicate restricts removal candidates while retaining legacy default behavior and accounting for protected canonical regular files.
- 74a1eb1f, e70b7bff: catalog reports recovered final sequence and revision; `UnifiedCheckpointRetention::plan/cleanup` require active manifest match and successful full active recovery, and select only fully recoverable older same-world pairs whose final sequence/revision do not exceed the active checkpoint. Future, foreign, damaged, nonregular and orphan files are protected. Cleanup recalculates independently and rechecks the active manifest and file types.
- ff83e7ef: `UnifiedWalSession::managedReady/manifest/backups/retentionPlan/cleanupRetention/forkGeneration`. All managed operations require ready session and held lifetime lock. Fork rejects zero/same UUID and future generation, creates independent sequence zero. Cleanup is explicitly invoked, not automatic after every checkpoint.
- 644f737a: extended existing `tests/world/region_entity_ledger_test.cpp` with managed checkpoint rotation, positive catalog recovery, corrupt/foreign/future/orphan retention, stale manifest rejection, protected byte budget, independent fork and closed-session refusal. Fixture uses empty section adapters and unmaterialized ledger entity, not production component/resource codecs.
- 61d54db8: updated `skills/world/aiverse-persistent-world/SKILL.md` (frontmatter unchanged; existing catalog entry retained).

## Validation
Source changes committed; native build/CTest NOT RUN. Catalog generation/audit NOT RUN. No devlog success entry, no main merge, both roadmap items remain in-progress pending evidence.

## Boundaries and risks
Cooperating-process lifetime lock and quiescent finalized barrier required; no hostile concurrent filesystem changes supported. Full verification is expensive; checkpoint budget is per generation, not a WAL replay CPU/total disk cap. keepPrevious is the eligible backup count retained, maxBytes is soft. Partial unlink is reported by existing cleanup result; orphan files are protected on subsequent runs. No crash-atomic pair deletion claim. No resource WAL, distributed lock, production finality integration or async persistence. Windows/POSIX native behavior remains unverified.

## Next
W1.23z: durable resource codecs and transaction resource deltas. Reuse the existing resource section registry, define a bounded versioned resource WAL record stream, stage resource mutations alongside entity/component operations, preserve UWT1 compatibility with explicit envelope versioning, and test refusal/rollback before publishing. Then integrate the real native ECS finality owner and extend crash/recovery acceptance. Reconcile main in a dedicated pass before native W1 + Mac quick gate.
