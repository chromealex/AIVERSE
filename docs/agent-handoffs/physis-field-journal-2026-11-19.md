# PHYSIS Stage 26 — atomic per-tick field commands and replay journal

Repository: chromealex/AIVERSE
Branch: feature/physis-nature-simulation
PR #25 DRAFT
Implementation: 14d8aed34b1a00721a32127f66783dc93f830dc4, 9d24e8b5e33e6a312e84d359868b51ae3c0f98a9

## Changed files
- engine/physis/PhysisFieldCommands.h: applyAtTick now stages ALL commands at the same tick in one World copy, then publishes once. If a later command throws, neither original World nor pending queue is modified. Successful batch advances committed sequence and removes all applied commands.
- engine/physis/PhysisFieldJournal.h: host-owned FieldCommandJournal, append validation for monotonic sequence/tick, pendingAfter(lastCommittedSequence), replayWindow(lastCommittedSequence, throughTick). Does not serialize journal into ECS state.

## Contracts
Authoritative host owns journal and command sequence, stores it durably or replicates it, and restores matching World snapshot before replay. lastCommittedSequence is a separate checkpoint supplied by host, not inferred from PHYSIS tick. Queue and journal accept full desired field sets, not patches. Authoring must call parseFieldAssets before submission; journal checks only shallow queue constraints. Host must call applyAtTick at tick boundary before PHYSIS systems. Do not use a transient queue as the source of truth across rollback.

## Actual verification
GitHub writes succeeded. Native compile NOT RUN. CTest NOT RUN. Snapshot/replay parity NOT RUN. Network integration NOT RUN. No CI PASS observed. PR must stay Draft.

## Limitations
- No editor wiring or host tick registration; APIs must be invoked by a native host.
- Journal is in-memory only, no wire/disk format or command acknowledgements.
- Host must supply correct checkpoint sequence and restore a matching World; wrong checkpoint causes divergence.
- Multiple same-tick commands are transactional but are still applied sequentially within staged World, so last complete replacement wins.
- Large World snapshots per tick-batch may be expensive; profile COW before production use.
- Stage 23/24 runtime field API and Archive serialization still need native compilation/CTest validation.

## Next
Add dedicated PHYSIS CTest for atomic rollback when second command fails, replay parity, journal order, and entity identity. Integrate journal/checkpoint with Simulation host authoritative command lifecycle; then editor Apply button should enqueue commands instead of touching live ECS. Keep PHYSIS independent of FPS/RTS, NEREID water, TerraWeave foliage and Box3D physics.
