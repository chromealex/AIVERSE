# W1.24a — exact native finalized tick capture

Area W1, branch `feature/aiv20261123_open_world_audit`, Draft PR #116. Base 097385dfbfa28bcc0e686168a3e476dfe806a922.

## Changes
- 14f28235: roadmap in-progress entry.
- 7691127a: `engine/world/RegionEntityLedger.h` adds scoped, thread-confined, bounded `MutationCapture` recording chronological mutator output (spawn, transfer, mixed transfer, despawn). Aborted transactions truncate captured suffix; overflow/allocation failure poisons capture. No extra serialized or copied ledger fields.
- 04da86df, 2794980e: `RegionWalTransaction::encodeCaptured` and `UnifiedWorldTransaction::encodeCaptured` accept exact revision-contiguous mutations; preserve existing on-disk v1/v2 formats.
- 7ab8ddc0: `engine/world/NativeWorldFinality.h` opt-in native Simulation step coordinator. Compare selected durable/live baseline, detach COW ledger resource, capture exact mutator stream, execute one real Simulation::step(), compute component/resource differences, validate private replay against final stable identity/ledger/component/resource projection before WAL append. Result is Committed, NoChanges, WaitingForContent or NeedsRecovery. Unexpected step, capture gaps, projection mismatch or durable append error poison the coordinator. No changes to normal Simulation::step(), rollback or network loops.
- ae2f9eed: source fixtures in `tests/world/region_entity_ledger_test.cpp`: multiple moves of same ID, transaction rollback suffix, spawn/despawn, capture overflow, actual native Simulation tick with stable entity and typed durable component, no-change tick, unowned-step refusal, disk restore.
- b5e8be84: persistent-world skill describes native boundary usage and limitations.

## Validation
Native compile/CTest NOT RUN; skill catalog generation/audit NOT RUN. No main merge or success devlog; roadmap remains in-progress.

## Contracts / limits
Host must hold exclusive quiescent tick boundary with no active transaction, no speculative step, no external mutation, and no reentrant session operations. Capture is thread-local and pointer-scoped; COW replacement or other-lane writes may escape it, so final exact ledger comparison is mandatory. On NeedsRecovery stop publication and further simulation; live state is not automatically rolled back. WAL session projection is an offline persistence representation, not a new gameplay authority. The ledger is separately authoritative, excluded from resource policy. This implementation is full projection/capture correctness-first, not optimized dirty-only; oversized deltas fail closed. Materialization-only changes require a replayable lifecycle policy and otherwise fail comparison. No persistence of native tick/RNG/pending commands or arbitrary unselected state; no automatic production server/ActorHost enablement yet.

## Next
W1.24b: define production native save policy and real stable-ID materializer for actor/vehicle/loot and host resources, include explicit tick/RNG/restart metadata without raw transient handles, and wire the actual authoritative host loop to NativeWorldFinality (not rollback/prediction). Establish restore barrier and fail-closed publication behavior. Then async IO, editor/MCP, native acceptance and main reconciliation.
