# W1.25c — streamed world persistence

Area W1, branch `feature/aiv20261123_open_world_audit`, Draft PR #116. Base 2d022a7e74efac425e1ff0131e44a1dd228faa4f.

## Implementation
- 8b29b473: world roadmap in-progress entry.
- 7d2e042c, d3306b31: WorldAssets::verifyAll audits all declared assets (including nonresident chunks) one file at a time using existing SHA-256 verification; verifiedReader publishes output only on success. persistenceIdentity hashes length-delimited rules/map JSON including expected asset hashes and explicit portable schema tag. Static world identity remains unchanged.
- 2ab54407, e13f1ea9: CollisionStream records sticky read/hash failure across worker retries; ordinary nonpersistent stream behavior is unchanged.
- 20cdd45d, 6aa84454: ActorHost exposes integrity failure; restore invalidates derived navigation and admission caches.
- 7befaba5, 10504b8c: authority Server enables verified streamed persistence, performs declared asset audit before session create/reopen, binds SHA-256 identity, gates step/attach/poll/broadcast/checkpoint on restored collision/navigation admission. Runtime asset integrity, preparation, budget or navigation failure poisons persistent authority; no speculative publication.
- ae2b0e87: CLI waits for actual ActorHost admission before network listeners open.
- cb9a47ce: tests/fps/tests.cpp source fixture for streamed save/recovery, nonresident asset mismatch, changed manifest mismatch and resumed tick.
- 61d61720: persistent-world skill updated.

## Verification
Native compilation/CTest NOT RUN. Skill catalog audit NOT RUN. No main merge, no success devlog or measured performance. Roadmap remains in-progress.

## Constraints
Existing hashed WorldAssets manifests only, not arbitrary external state. Startup audit is synchronous, one file at a time, 32 MiB maximum per file subject to the reader contract. Background ECS recovery remains separate. This verifies declared bytes, not all possible future collision semantic errors/seams. Project files must be immutable during a session; already resident verified bytes may remain in memory, subsequent reads are verified. Initial readiness gate runs before peers/steps and CLI listener bind; recovered ECS may already be available privately on owner before content admission. The original server mutable Simulation API is still trusted-owner-only. Full native acceptance remains necessary.

## Next
W1.25d: scalable stable identity and tombstone lifecycle for long-lived worlds. Design bounded compaction without ID reuse; preserve monotonic allocator high-water mark, revision cursor semantics, WAL/checkpoint recovery and retired reference rejection. Then editor/MCP controls, diagnostics, skills and native acceptance. Do not merge main before merged-tree Mac gate.
