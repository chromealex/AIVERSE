# W1.25a — asynchronous durable persistence

Area W1, branch `feature/aiv20261123_open_world_audit`, Draft PR #116. Base 5fdce0c6ec39f0abc4b46be2602a8e32fcc1f6dc.

## Implementation
- 4e35b09c: world roadmap in-progress entry.
- 9f1d2fb1, 7df84625: `engine/world/AsyncPersistenceIo.h` bounded native single-flight mailbox, queued/writing/durable/failed status, ordered receipt, byte admission and failure poisoning. Shutdown drains accepted operation before joining; WebAssembly start is unsupported.
- 7b7c5ccb, d53bbc56: `UnifiedWalSession.h` queueCommit/queueCheckpoint/pollIo, explicit busy result, encoded byte budget, owner-thread validated staged ECS state, worker-only append/flush and checkpoint disk publication/reopen, manifest and state publication after acknowledgement. Session denies concurrent state/backup/retention/fork operations.
- bf9f6e79: `NativeWorldFinality.h` WaitingForDurability state with one outstanding tick. Acknowledgement advances finalized tick; failure poisons authority.
- ff3b515a: `ActorPersistentSession.h` async opt-in and separate checkpoint barrier.
- 21b43a3f, 603077e9: `engine/net/authority/Session.h/.cpp` async option, finalizedTick, persistencePending, pollWorldCheckpoint; no extra input injection or snapshot publication before durable receipt. Transport update continues while gameplay reads pause.
- 7c1c6fac: `apps/fps/server.cpp` --world-async, finalized tick ceiling and final checkpoint acknowledgement before exit.
- d988a95f, 34cfc0db: source fixtures in `tests/fps/tests.cpp` and `tests/world/region_entity_ledger_test.cpp` for async server restart, checkpoint, pending/ack separation, bounded mailbox admission, failed receipt poisoning and shutdown drain.
- 46036599: persistent-world skill updated.

## Validation
Native compilation/CTest NOT RUN. Skill catalog audit NOT RUN. No main merge, no success devlog or performance claim. Roadmap remains in-progress.

## Contracts and limitations
One owner thread and one worker per enabled managed session, at most one outstanding operation including unconsumed receipt. No worker callback into live Simulation or ECS codecs. Only IO runs on worker; ECS copies, replay validation, encoding and checkpoint digest remain synchronous. The byte budget bounds queued encoded payload, not total working-set memory. No speculative multi-tick pipeline, group commit or concurrent checkpoint and append. Server must not expose uncommitted simulation state via external mutable access; transport updates continue, gameplay polling/admission can defer or reject while pending. A shutdown can drain a durable but unacknowledged operation; reopen must trust verified disk state rather than assume its absence. Static-map actor restriction and lifetime ledger growth remain. Source integration is experimental until native acceptance.

## Next
W1.25b: background recovery/loading with private immutable codec context, cancellation/progress and exclusive ownership handoff at the restore barrier. Never invoke live host validators on a worker; split offline validation and final owner-side content validation. Then immutable streamed-content provenance, scalable ledger/tombstone lifecycle, Studio/MCP and native acceptance. Preserve durable-before-publication and do not merge main before merged-tree native gate.
