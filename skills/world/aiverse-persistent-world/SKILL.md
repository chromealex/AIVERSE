---
name: aiverse-persistent-world
description: Use AIVERSE persistent world sessions, durable ledger WAL, checkpoints, recovery, retention and independent backup forks.
---

# Persistent world

Use engine/world/PersistentWorldSession.h for genre-independent durable region ownership. The current implementation persists RegionEntityLedger only, not arbitrary ECS components or full gameplay state. Never claim a complete game save from this API.

## Workflow

- Create an existing private empty directory and supply a unique nonzero 16-byte world UUID. Call create(dir,uuid,initialLedger) or open(dir,uuid,maxCheckpointBytes,repairIncompleteTail).
- Check every result. Hold the session for the entire authoritative lifetime; do not unlink lock files or bypass the session using low-level writers.
- Call transactFinalized(callback) only at an authoritative finality barrier, never on speculative rollback ticks. Callback uses transaction methods only, returns false on refusal, and must not reenter or publish external side effects.
- On ambiguous write or rotation failure, stop simulation, close and recover. Never blindly retry a transaction.
- checkpoint(unusedIncreasingGeneration) rotates the WAL and retains older generations.
- backups(maxBytes,validate) lists metadata or performs expensive full verification. Metadata alone is not proof of recoverability.
- retentionPlan(policy) previews deletion; cleanupRetention(policy) removes eligible older pairs. keepPrevious is a minimum; maxBytes is a soft budget. Active and orphan files are protected.
- forkGeneration(generation,destination,newUuid,maxBytes) copies recovered ledger into an existing empty private directory. Original remains unchanged.

## Unified ECS checkpoint and WAL (experimental)

Use `engine/world/UnifiedWalSession.h` for the managed UWT1 session, not the ledger-only session above. Supply a private existing empty directory, unique nonzero UUID, checkpoint budget, three explicit section adapters (entities, components, resources), a private-state factory, durable component codecs and a materializer. Register stable entity IDs and contextual codecs; never persist raw runtime Entity handles. The factory, readers, materializer and validator must not publish side effects.

- `create` starts a new world at generation 1 and sequence 0; `openCheckpoint` restores the active checkpoint and replays WAL. Check every return code. Use `commit` only at finalized barriers, then `checkpoint` with an unused increasing generation.
- `backups(budget, adapters, factory, codecs, materializer, validator, verify)` lists UWT1 generations. MetadataOnly means no full replay was performed; Recoverable means checkpoint plus complete WAL restored privately. Listing future or orphan candidates is not authorization to activate them.
- `retentionPlan(policy, budget, adapters, factory, codecs, materializer, validator)` previews deletion; `cleanupRetention` revalidates independently before removal. Invoke explicitly at a quiescent barrier after a successful checkpoint; never clean on an ambiguous result. Only fully recoverable older same-world pairs with sequence/revision no newer than the active checkpoint are eligible. Active, foreign, damaged, future, symlink and orphan files stay protected. keepPrevious is the retained eligible backup count; maxBytes is a soft target and protected files may exceed it. Byte accounting is for canonical regular checkpoint/WAL files, not total directory disk usage.
- `forkGeneration` recovers a selected current or older generation and creates an independent world in a separate private empty directory with a new UUID. The new WAL starts at sequence 0. It does not mutate the original.
- For resource WAL use `DurableResourceTransaction::Policy{&registry, selectedResourceIds}` with the same explicitly selected durable resource schemas as `UnifiedResourceSection::adapter(policy, budget)`. Registry outlives all adapters and calls. Use `captureChanges(registry, policy.selected, beforeWorld, afterWorld, records)` at the finalized barrier; only changed schemas are emitted as full resource values (including absence), not field-level deltas. Never select a resource that duplicates authoritative ledger state or embeds raw runtime Entity handles.
- Encode resource records with `UnifiedWorldTransaction::encode(tx, componentRecords, resourceRecords, sequence, frame)`. Resource-only frames are supported. Version 1 remains byte-identical for resource-free frames; version 2 adds a bounded resource section. Pass the same policy as the final argument to commit, open/openCheckpoint, backups (when verifying), retentionPlan, cleanupRetention and forkGeneration. Resource-bearing replay without a matching policy fails closed. Older binaries cannot replay version 2.
- Production ActorHost finality wiring, crash injection and native acceptance remain outstanding. No assertion that arbitrary game state is durable.

## Native finalized tick boundary (experimental)

Use `engine/world/NativeWorldFinality.h` only from an explicitly authoritative non-rollback native tick owner. Construct it with the live Simulation, managed UnifiedWalSession, selected durable component codecs, resource policy from the same Simulation registry, and a private-state materializer. The initial durable projection must match the live world. Call its `step()` instead of the owner's ordinary Simulation::step(). Never install it as a gameplay system or use it for predicted/replayed ticks.

- Committed: selected changes are durably appended. NoChanges: tick completed without selected changes; no WAL frame. WaitingForContent: native readiness refused the tick without selected changes. NeedsRecovery: stop the owner; never blindly step again or publish the tick. Live Simulation is not rewound automatically.
- RegionEntityLedger::MutationCapture observes chronological mutators, including repeated changes to one ID; aborted ledger transactions remove their captured suffix. The capture is thread-confined, bounded, host-only, and not copied or serialized. Missing capture due to resource replacement/COW/worker mutation is rejected by private replay and exact ledger comparison.
- The coordinator compares stable entity materialization and selected component/resource bytes before and after replay. It is a correctness-first full projection, not a dirty-only performance solution. Cold/wake changes with no corresponding replayable operation fail closed. Resource selection must exclude the region ledger itself.
- This does not save Simulation tick, RNG, pending commands, unselected components or arbitrary module state. A production host must define those policies and its restore/republication barrier before enabling durable sessions. The generic native API is available; existing server/client loops are not automatically wired.

## Implementation

RegionWalFrame/Reader/Writer/Transaction/Commit/Finality provide bounded CRC frames, contiguous sequences, durable append and atomic ledger replay. RegionCheckpointManifest/Publish/Load/Archive bind generation and SHA256 to checkpoint and WAL prefix. RegionWorldSessionLock provides local cooperating-process exclusivity, not distributed consensus.

Native Archive and pack SHA256 are reused; link the existing SHA256 implementation. POSIX and Win32 paths are source implementations, not validated platform guarantees. WebGL is unsupported. Do not use pageSince or coalesced ECS dirty journals as a lossless WAL.

## Acceptance

PR #116 remains Draft. W1.21–22 native builds and crash-injection tests have not run. Unified ECS checkpoint/component WAL source exists but native compilation and verification are outstanding, as are actual ActorHost finality integration, async writes, editor and MCP. Read stage21–23 handoffs. Keep per-stage handoffs and report actual validation only.
