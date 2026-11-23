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

## Implementation

RegionWalFrame/Reader/Writer/Transaction/Commit/Finality provide bounded CRC frames, contiguous sequences, durable append and atomic ledger replay. RegionCheckpointManifest/Publish/Load/Archive bind generation and SHA256 to checkpoint and WAL prefix. RegionWorldSessionLock provides local cooperating-process exclusivity, not distributed consensus.

Native Archive and pack SHA256 are reused; link the existing SHA256 implementation. POSIX and Win32 paths are source implementations, not validated platform guarantees. WebGL is unsupported. Do not use pageSince or coalesced ECS dirty journals as a lossless WAL.

## Acceptance

PR #116 remains Draft. W1.21–22 native builds and crash-injection tests have not run. Full ECS component persistence, actual ActorHost finality integration, async writes, editor and MCP remain outstanding. Read stage21/22 handoffs. Keep per-stage handoffs and report actual validation only.
