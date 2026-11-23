# W1.24e — portable active projectiles and race sessions

Area W1, branch `feature/aiv20261123_open_world_audit`, Draft PR #116. Base 792615adb378af43c92a336341ebdf12cc27c29e.

## Implementation
- fbd7f137: world roadmap in-progress entry.
- e0c47c6f: PersistentEcsRegistry::registerContextualResource<T, Transfer> supports canonical World-aware resource codecs, without altering ordinary native schemas.
- c2aa12ca: PersistentEntityReferences::rebuildTagged for private projections with externally held ledger; callers must validate authoritative ledger separately.
- b9183258: `engine/vehicles/DurableRaceState.h` shared race resource codec preserves grid, ordinal racer order, session/bot/standings data and remaps vehicle handles through stable IDs. Reject duplicate/missing racer bindings.
- 1903a078: ActorRegionOwnership tracks projectile structural changes and includes live projectile components in full region reconciliation; opt-in candidate-only path falls back when projectiles are present or changed.
- 2a75a006: `engine/actor/ActorDurableProjectile.h` bounded codec for base projectile plus full programmed flight state, modifiers, hit history, ballistic state and target keys. ActorHost keys encode slot/life or vehicle object identity, not runtime handles; historical hit keys are preserved rather than resolved against live allocator.
- bc9f3fb0, 6332c1d0: actor projection registers projectile component and shared race resource; rebuild checks ownership and object-ID uniqueness, race validation runs after staged resource replay.
- 0b3d412a: expanded actor-region-position source fixture with active projectile, shifted runtime entity handles, racer reference remapping, checkpoint progress, identical durable resource projection and projectile retirement tombstone.
- 1a03b012: update persistent-world skill.

## Validation
Native compile/CTest NOT RUN. Skill catalog audit NOT RUN. No main merge; no success devlog or claim of production readiness. Roadmap remains in-progress.

## Limits and compatibility
Previous experimental W1.24d checkpoint resource selection excludes race state and does not directly load using the new four-resource policy. No migration or native regression run has been performed. Ordinary native save format unchanged; opt-in region checksums change for active projectile ownership. Projectile tombstones consume existing bounded ledger capacity: high-churn long-running worlds require a deliberate retention strategy, not identity reuse. Immutable race course/weapon definitions and host-specific state still require content identity validation. Arbitrary additional gameplay schemas and runtime publication remain outside this projection.

## Next
W1.24f: production host integration audit. Identify actual authoritative simulation entry points and restore/publication barriers; enforce content identity and explicit resource/component policies before wiring NativeWorldFinality into the server runtime. Preserve rollback/finality semantics and refuse unsupported hosts. Then asynchronous durable IO, background loading, Studio/MCP exposure, feature skills and native acceptance. Do not merge main before validation.
