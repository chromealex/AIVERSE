# W1.24c — stable actor ownership identities

Area W1, branch `feature/aiv20261123_open_world_audit`, Draft PR #116. Base 4ed573af206fe4ac07bb6cd0a0006e8684d48038.

## Changes
- dee60e9b: roadmap entry for stable identities.
- c57441d: `RegionEntityLedger::nextIdentity()` derives monotonic identity from highest existing key (including tombstones); zero signals exhaustion, no duplicate allocator state or archive format change.
- d5bc357, 74c080a: `ActorRegionOwnership.cpp` assigns new identities in semantic order (character slot, vehicle/loot object ID), retains existing tags and tracks a derived bidirectional handle-to-ID index. Full reconciliation resolves retirements by ID and never infers a ledger ID from a handle. Candidate-only loot/solo mode uses the prior index and requires a valid cache; restoration invalidates it. Runtime dirty notifications still use handles.
- 079d490: `RegionMoveRequests.h` schema v2 persists stable IDs and validates materialized live bindings. Explicit v1 migration preserves historic numeric tags (v1 packed+1 was the stored stable ID).
- 816cfb3: `ActorRegionAdmission.cpp` stages, checks overlap and applies moves using shared PersistentEntityReferences rebuilt from live stable tags and ledger.
- 780785b, 04e8d7e, fcfb527, de4b3e9: actor lifecycle/position/admission regression source now reads actual tags, distinguishes dirty observation keys, checks recycled handle non-reuse and stable-ID move gateway with deliberately different ID and retired old ID. RegionDirtyQueue comments clarify runtime-only handle semantics.
- 0b78d9a, 1117e91: update region-lifecycle and persistent-world skills.

## Validation
Native compile/CTest NOT RUN. Skill catalog audit NOT RUN. No main merge. Roadmap remains in-progress pending native acceptance.

## Contracts and limits
Stable IDs are preserved across old save migration; new allocations are monotonic and independent of runtime handles. The candidate-only optimization remains opt-in, requires complete journal coverage and does not support arbitrary same-tick rematerialization without cache invalidation. Full scan is the default; derived index copies are not an optimized incremental solution yet. DirtyQueue is a native snapshot observation queue, not a durable portable identity queue; drain it before portable restore. Move-request validation requires an authoritative ledger in the staged World. Production host materialization, CharacterIndex and vehicle handle-bearing component/resource codecs are not yet wired; no claim of complete actor restart.

## Next
W1.24d: audit ActorHost persistent component/resource schemas and register stable-reference codecs for CharacterIndex, vehicle driver/occupant links and any other transient references. Implement explicit private-world materializer for actor/vehicle/loot and rebuild host-derived state. Ensure durable replay's staged resource validation sees the authoritative ledger without a second source of truth. Only then connect production authority loop and restart publication. Continue async persistence, editor/MCP and native acceptance.
