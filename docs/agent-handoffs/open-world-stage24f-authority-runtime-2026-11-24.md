# W1.24f — opt-in authoritative server runtime persistence

Area W1, branch `feature/aiv20261123_open_world_audit`, Draft PR #116. Base 94d7d8ff8f974290de3f23720670209da33c5826.

## Implementation
- 567140d0: runtime-only ECS inventory of nonempty pools and active resources for storage coverage audits.
- e6e3c7fc, 22e3f986: ActorHost declares its derived resource types; invalidate cached admission after restore.
- d4189248: ActorHost::validatePortableWorld validates configured player count, character fields, optional gear/locomotion/bot state, rewind/royale state, object limits, projectile flight config, typed vehicles and race state.
- 684a71c2: `engine/actor/ActorPersistentSession.h` owns the explicit codec registry and UnifiedWalSession/NativeWorldFinality. Reject unknown active storage, orphan/unmaterialized entities, unsupported dormancy and pending deferred destroys. Create or reopen validated checkpoint/WAL before publishing a reconstructed SimSnapshot; use stable command remapping and explicit actor input policy. Checkpoint rotation on finalized barriers.
- 1e829e1e, 49d30379, 1c393e85, 22aa585d: actual `engine/net/authority/Session.h/.cpp` Server integration. Enable before peers attach; commit before broadcast; no extra queued input batch when readiness refuses a tick. Persistence failures close peers and block subsequent step/poll/broadcast. Persistent broadcast only allowed inside finalized step.
- 2f6db32a: `apps/fps/server.cpp` CLI supports --world-dir, --world-id (32 hex digits, nonzero UUID), --world-checkpoint-ticks. Creates empty directory or resumes existing manifest, before opening UDP/WebSocket listeners. Failure exits nonzero. No automatic WAL repair.
- 6a0ab982: `tests/fps/tests.cpp` source fixture for actual server creation, tick commits, checkpoint rotation, duplicate lock refusal, seed mismatch, state/RNG/tick recovery, unsupported resource refusal and reopen.
- d99511e7, aa6c48e0: roadmap and persistent-world skill updates.

## Verification
Native compile/CTest NOT RUN. Skill catalog audit NOT RUN. No main merge, no success devlog. This is experimental source integration, not production acceptance.

## Constraints
Opt-in regionOwnership, explicitly selected actor storage, static inline map content only. Async streamed external assets rejected until immutable content provenance is implemented. Rules/map content hash + explicit schema tag bind saved data; not a cryptographic external asset manifest. Existing clients and rollback remain unchanged. Network peer queues not yet consumed in a finalized tick are volatile; clients reconnect from durable acknowledgement. Do not mutate live state between commits via arbitrary external calls. WAL synchronous append may be expensive. Snapshot publication after commit is guarded; failed live tick is not automatically rewound, authority must stop. Additional game modules and unknown ECS resources intentionally refused. Existing ledger capacity/tombstone growth and portable checkpoint migration remain unresolved.

## Next
W1.25a: shared bounded asynchronous persistence pipeline with explicit queued/written/durable states, ordered acknowledgment, checkpoint barriers, backpressure and fault propagation. Preserve durable-before-publication contract; never report enqueue as committed. Then background restore, streamed-content provenance, long-running ledger capacity, editor/MCP, skills and native acceptance. Do not merge main before validation.
