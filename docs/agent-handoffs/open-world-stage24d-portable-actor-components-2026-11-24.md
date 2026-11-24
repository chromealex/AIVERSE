# W1.24d — portable actor object projection and reference codecs

Area W1, branch `feature/aiv20261123_open_world_audit`, Draft PR #116. Base abb2a25b47dd332ead25c208c7561fa8250fa21b.

## Implementation
- d9192633: world roadmap in-progress entry.
- 98d63860, 7942ab4f, dd4a9f28: shared DurableComponentCodecs::addContextual/addRebuilder/addCaptureValidator; checkpoint and WAL rebuild derived indices after complete component restore.
- 21a458f4: `engine/vehicles/DurableVehicleComponents.h` shared stable driver and occupant reference codecs; reject pending seat requests, preserve existing native archive layout. Numeric seat actor IDs must already be host-stable (ActorHost player slots).
- 5e0c4910, cff755d4, 11426103, acddf2b3: durable resource restoration supplies temporary authoritative ledger context for native validators and erases it before publication; ledger cannot be selected as a duplicate resource; selected registry validation also runs for empty resource deltas.
- 10418ed1, b4ace107, 129a428a, bee0a5d6, 9e866ca7: `engine/actor/ActorDurableComponents.h`: eight core character component codecs, optional gear/bot/locomotion, vehicle, loot, stable-reference links; shell materializer for all live region actor IDs; CharacterIndex rebuilt from restored slots, consistency/seat/ownership checks and capture preflight. Explicit actor resource registry/policy uses canonical World values only.
- adac75e4, aa35a4e0: `ActorModule.h/.cpp` exposes handle-free canonical match State codec (round/winner/nextObject/match, rewind frames and royale), without changing native interleaved serializer.
- 2ebaa28e: NativeWorldFinality allows explicitly supplied host-owned resource codec registry in addition to native registry; no duplicate gameplay state. 159eb4cc: extract RegionMoveRequests validator for both registries.
- b11639fe, 0e51fc90: existing `tests/world/actor_region_position_test.cpp` fixture captures and reconstructs actor projection with deliberately shifted runtime handles, driver/vehicle backlinks, character health, match round and stable queued move; missing stable reference refused.
- ed9d0fde: updated existing persistent-world skill.

## Validation
Native compilation/CTest NOT RUN. Skill catalog generation/audit NOT RUN. No main merge, no success devlog. Roadmap remains in-progress.

## Limits
This is an opt-in projection, not a complete production save. Active projectiles and race resources are explicitly refused, runtime dirty queue must be drained. Arbitrary unselected components/resources, transient host services and content-specific configuration validation are not covered. Full materialization policy is only for worlds with all live ledger actors materialized. Shared vehicle codec assumes host-stable numeric seat occupant IDs. CharacterIndex is derived, not separately persisted. Resource registry metadata is independent of native save schema; actual resource values remain in canonical ECS. The fixture uses synthetic typed drive on a real ActorHost map, does not establish production typed configuration or runtime parity. Host publication and server loop remain unwired.

## Next
W1.24e: extend portable actor policy to active projectiles (program state, host-specific hit identity and target reference semantics) and race resource (stable racer vehicle references), then audit content-specific validation and host-derived services. Wire actual authoritative server tick loop and restore publication only after complete selected policy is validated; then async persistence, editor/MCP and native acceptance. Preserve genre independence and shared vehicle ownership.
