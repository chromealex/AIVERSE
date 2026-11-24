# W1.25b — background world recovery

Area W1, branch `feature/aiv20261123_open_world_audit`, Draft PR #116. Base 81ae0e9dc148ddae75c5f4e5a426899634f8fbfb.

## Implementation
- 9fa8b656: world roadmap in-progress entry.
- f23f14cc: `UnifiedCheckpointLoader.h` optional read progress callback and 1 MiB checkpoint read chunks, cancellation before publication.
- 7ec99b3b: `UnifiedWalSession.h` beginRecovery/recoveryProgress/requestRecoveryCancel/pollRecovery. Separate background mailbox; acquire world lifetime lock on owner and keep it through private checkpoint decode, WAL replay, writer reopen and final publication. Atomic progress counters; no repair/truncate. Owner consumes receipt, validates, publishes or discards. Close cancels/joins before releasing dependencies.
- ec4961f4: `ActorPersistentSession.h` splits setup, offline recovery and owner finish. Worker uses immutable metadata values and private codecs, not live host or Simulation. Final content/command validation and SimSnapshot publication on owner.
- 754da2ad, e3465872: `engine/net/authority/Session.h/.cpp` nonblocking recovery API, progress/cancel and loading gates on tick, network attach/poll/broadcast/checkpoint.
- ad0aa4a9: `apps/fps/server.cpp` --world-background-load, reports phase/checkpoint bytes/WAL frames and sequence before listeners bind. Combine with --world-async if desired.
- 80b2cafc: authority restart source fixture adds cancellation, concurrent lock refusal, unchanged live tick during recovery, final progress and resumed async tick.
- 64fd7072: persistent-world skill updated.

## Verification
Native compilation/CTest NOT RUN. Skill catalog audit NOT RUN. No main merge, no success devlog or measured loading performance. Roadmap remains in-progress.

## Contracts and limits
One owner and one loading worker, immutable worker-safe codecs/registry/factory/validators must outlive recovery. Worker structural/schema checks run at every replay frame; configured host content validation runs on owner against final recovered state (not every intermediate state). No live host callbacks on worker. Progress counters are independently sampled, not a transactional snapshot. Cancellation is cooperative at checkpoint read chunks, validation and WAL frame boundaries; one codec/digest/writer-open scan may complete before cancellation is noticed. No cancellation of in-progress OS IO. Full checkpoint and private ECS working set remain in memory. Cancelled/failed Server startup requires a new Server. No hot replacement of running authority or external streamed content yet. Do not treat a worker-ready result as a published live world.

## Next
W1.25c: immutable streamed-content provenance. Bind external asset manifest and verified asset digests to restart content identity, reuse existing collision/navmesh readiness gates, reject changed or missing assets, and enable supported streamed worlds only after explicit validation. Then scalable ledger/tombstone retention, Studio/MCP, skills and native acceptance. Do not merge main before merged-tree native gate.
