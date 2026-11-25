# W1.25d — checkpoint-bound ledger compaction

Area W1, branch `feature/aiv20261123_open_world_audit`, Draft PR #116. Base 50ca6ef758112793d8a3b9fa0b0c813bec150f1f.

## Implementation
- 73a42b55: roadmap item, in-progress.
- 1756c20c, 31d81464: RegionEntityLedger v2 persists sealed retired ID prefix and compacted history revision floor. Bounded removal of old tombstones at a quiescent private checkpoint barrier; no ID reuse, no gameplay revision increment. nextIdentity derives high-water mark from prefix and remaining records. Reject compaction with active transaction/capture on owner lane. Live-only activeIn traversal; stale page/reset and since refusal. Native schema migration from v1, direct archive v1 reader.
- 95c61672, 01a46775, 63217925: exact ledger comparisons include watermarks; reference classification recognizes retired prefix.
- 6e8ea6d2: UnifiedWalSession synchronous and asynchronous checkpoint rotation can compact a private candidate, publish it only on successful durable acknowledgement.
- e3131a4a: ActorPersistentSession adopts identical compacted ledger on acknowledged barrier, refuses divergent live ledger; default recent window 65536 revisions.
- 6c85a82d, af03fee8, 61ac4b4d: Server checkpoint API and CLI --world-retain-revisions N.
- b4da057f, e6cb3365: source fixtures for stale cursor bootstrap, sealed identity gaps, legacy archive, WAL replay after compaction, synchronous/asynchronous checkpoint rotation, backup fork.
- d7d4ea4c, 0d213a4a: persistent-world and region-lifecycle skills updated.

## Verification
Native compile/CTest NOT RUN. Skill catalog audit NOT RUN. No main merge, no success devlog or scalability benchmark. Roadmap remains in-progress.

## Contracts / limits
Compaction permanently seals missing IDs at or below greatest removed ID, including unused gaps; existing live IDs remain valid. Use monotonic allocation thereafter. Cutoff and removal limit are explicit. History before removed revisions requires full observer replacement, not cursor skipping; legacy since throws. Direct external page consumers must honor resetRequired; audit remaining editor/MCP consumers in next stage. Archive v2 readable by current code; v1 reads/migration source provided, no native migration acceptance yet. Old binaries reject v2. The record cap still applies between checkpoints; configure sufficiently frequent rotations. Removal limit bounds removals, not total index scan. Live population, full snapshot working set and total disk usage are not bounded by compaction. Existing backup retention is separate explicit maintenance. No automatic deletion of backup generations. No compaction WAL frame; checkpoint manifest atomic rotation binds compacted bytes and unchanged sequence/revision. Failure/ambiguity stops authority and requires recovery. No cross-world ID equivalence for forks.

## Next
W1.26a: Studio/MCP integration for world status, checkpoint/backup/retention and recovery controls, progress and cursor bootstrap; then native compilation and full acceptance. Audit existing tool consumers of region change pages. Add optional pressure-triggered checkpoint/retention policy as a separate explicit operational feature if needed. Preserve genre-neutral APIs and main gate.
