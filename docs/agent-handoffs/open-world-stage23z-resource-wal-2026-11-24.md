# W1.23z — unified resource WAL

Area W1; branch `feature/aiv20261123_open_world_audit`; Draft PR #116. Base: 8d731a6291b4a638dcdc2ea555b9e7841ec3714d.

## Implementation
- e8699a6e: roadmap in-progress item.
- 8d6810c5: `engine/world/DurableResourceTransaction.h`: bounded versioned resource stream; stable ASCII schema IDs, canonical sorted unique records, native PersistentEcsRegistry resource codecs, explicit selected schema policy, captureChanges compares before/after serialized values including resource absence. This is per-resource replacement, not field-level deltas.
- d46a6bc1: `UnifiedWorldTransaction.h` preserves version 1 bytes for resource-free frames, adds v2 section for resource records, permits resource-only frames and bounds all sections by the outer WAL frame budget.
- c04009aa: `UnifiedWorldReplay.h` stages ledger, materialization, components, resources and validation before publishing the private state; missing policy fails closed for resource-bearing frames.
- bce7cf98, fb2c062f, 80cedab9, eddb83fe, 839812fa: resource policy forwarded through session commit/open/replay, backup recovery and fork, catalog verification and retention. Existing resource-free callers retain defaults.
- 7e2f85fc: `UnifiedResourceSection::adapter(policy,budget)` shares resource schema selection with WAL.
- 1a69d1ac, 6b4b6bb8: `tests/world/region_entity_ledger_test.cpp` source fixtures for canonical codec rejection, combined ledger/resource replay, validator refusal, removal, unchanged resources, v1 compatibility, disk reopen, catalog and fork with a real registered fixture resource.
- 924e018d: existing persistent-world skill updated.

## Verification
Native build and CTest NOT RUN. Skill catalog generation/audit NOT RUN. No main merge or devlog success entry. Roadmap remains in-progress pending acceptance.

## Contracts and limits
Pass the same explicit resource policy to all replay-capable calls; registry lifetime must cover the synchronous operations and checkpoint adapter. Selection must match checkpoint selection; do not encode duplicate authoritative ledger resources or raw runtime Entity handles. The registry's migration and validation semantics are retained. Resources are opt-in, not automatically intercepted. The caller must capture every finalized selected-resource change, and must not publish side effects from replay callbacks. Old binaries reject v2; new readers support v1. Outer frame CRC, ordering, durable write and fail-closed recovery remain unchanged. No native platform or crash-injection evidence yet.

## Next
W1.24a: integrate unified capture and replay at the real native ECS finalized-tick owner. Establish explicit schema selection and canonical resource/ledger ownership, capture from finalized before/after states, wire transaction production, restore materialization, lifecycle barriers and refusal handling; do not derive lossless WAL from coalesced dirty journals. Avoid genre splits. Then async save IO, editor/MCP and native acceptance.
