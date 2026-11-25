# W1.24b — restart-aware native finalized ticks

Area W1; branch `feature/aiv20261123_open_world_audit`; Draft PR #116. Base 492bd3f3585bf57b67bf34c42cad9f4bafd0ae47.

## Changes
- b25d2b62: roadmap item for simulation restart metadata.
- 69059a48: read-only Simulation accessors for pending commands, sequence counters, players and RNG.
- 63c5582b, 4b44e9b8: `engine/world/NativeRestartState.h` bounded canonical restart encoding, content identity, tick/rate/seed, PCG state/increment, players, command counters and pending queue. Explicit command policy approves opaque payloads; unit/target handles converted to stable ledger IDs, resolved against the rebuilt World. Stage a private SimSnapshot, do not publish from the codec.
- ffbd870e, 6e622a6d: `UnifiedWorldTransaction.h` version 3 WAL and `UnifiedWorldReplay.h` restart metadata continuity, one tick per frame, stable reference validation. Older resource-free/metadata-free encodings remain unchanged.
- b4f4c143, 7737ec0f: checkpoint format v2 with fifth restart section; legacy four-section v1 retained. Checkpoint capture/restore validate metadata and references.
- 8811a0ef: `NativeWorldFinality.h` opt-in restart mode verifies initial durable metadata, captures completed tick and queue, emits a frame even for metadata-only tick, refuses inconsistent/blocked ticks. Queued external input becomes durable at next successful finalized tick.
- 73a54052: `tests/world/region_entity_ledger_test.cpp` extends native fixture with restart-aware WAL and checkpoint, pending command stable ID remapping to different Entity handle, RNG, tick, command sequence, content mismatch and truncated codec refusal. Existing non-restart fixture retained.
- 42b1d974: update persistent-world skill.

## Verification
Native compilation/CTest NOT RUN. Skill catalog generation/audit NOT RUN. No merge into main. Source-only, roadmap in-progress.

## Limits
Host must provide a content identity binding actual rules/map/schema compatibility, select complete durable component/resource policy and stage a fully rebuilt World before restore publication. Opaque command param/args require explicit host certification; queued commands referring to missing entities fail closed. Executed history excluded. Older binaries reject v3 WAL and v2 checkpoint. Uncommitted external inputs may be lost. No automatic host loop wiring, production actor materialization or complete arbitrary gameplay restore yet.

## Blocking ActorHost audit and next
`engine/actor/ActorRegionOwnership.cpp` currently uses entity.packed()+1 as stable identity (including candidate retire and live-ID assertions). Runtime handles change on reconstructed worlds. Do not enable restart for this production host until identity allocation/mapping is independent of runtime handles. W1.24c: migrate stable IDs and reverse ownership lookup, audit ActorHost components/resources including vehicle seats and native references, then implement production materializer and restart policy; wire only authoritative loop with restore/cache barrier. Continue async IO, editor/MCP and native acceptance afterwards.
