# Open World Framework — W1 stage 08: automatic ECS region reconciliation
Date: 2026-11-23
Repository: chromealex/AIVERSE
Branch: `feature/aiv20261123_open_world_audit`
PR: https://github.com/chromealex/AIVERSE/pull/116 (**Draft**)
Capability: W1 / OW-01, **partial**; not merged or production accepted
Previous: `docs/agent-handoffs/open-world-stage07-canonical-position-2026-11-23.md`
Status of main at start: feature branch was **39 commits behind main** (no merge attempted).

## Done — architecture and APIs

**Owner:** Existing world-space `ActorHost`, shared W1 ledger and shared
`VehicleDrive`/seat policies. All new code is genre-neutral except the
required thin adapter to ActorHost's authoritative positions; the simulation
has not been split into new genre modes.

1. `engine/world/RegionEntityLedger.h`
   - New `transferGroup(span<pair<RegionEntityId,Cell>>,Cell)`: validates each group member's **individual expected owner** in a stable ID order, permits boarding a vehicle from a neighbouring region, and changes all owners together. Wrong/duplicate/stale/dead ID rejects without partial mutation. Already-in-destination members are no-ops and consume no revision.
   - Read-only `records()` accessor for live/tombstone reconciliation.
2. `engine/world/RegionEcsBridge.h`
   - `transferGroupMixed(world,ledger,entities,destination,position,error)` uses the existing canonical `RegionPositionReader` and `RegionStableIdentity`, refuses dead/dormant/missing bindings, then calls the ledger's new mixed-owner transaction.
3. `engine/actor/ActorRegionOwnership.cpp` (new)
   - `ActorHost::synchronizeRegionOwnership(sim)` collects live CharacterBody, Vehicle and Loot entities, sorts by packed Entity handle, tags newly observed objects using the generation-aware ID `Entity::packed()+1`, migrates across fixed 32-unit region cells, and tombstones removed IDs.
   - Shared `VehicleDrive` cars use `vehicleRegionGroup` for seat actors and reverse links. The existing arcade `Vehicle` adapter uses its driver player slot and `CharacterBody.vehicle` backlink; no second car physics.
   - Group members from different old region owners travel atomically with the car. One-tick handled set prevents an occupant receiving a second solo migration after the grouped migration.
   - Refuses duplicate car IDs, orphaned vehicle references, stale driver/entity tags and dormant movement. Existing async `CollisionStream::covers` blocks committing a car ownership change into an unresident destination (this is a *post*-tick check; see limitations).
   - **Projectiles excluded** from persistent IDs. Their extremely short lifetimes would quickly exhaust the ledger's one-million-ID/tombstone ceiling. Durable `Loot` stays tracked.
4. `engine/actor/ActorVehicles.cpp`
   - Extended canonical position reader to `Loot.pos` and `Projectile.pos`. This does not change positions; Projectile is available for read-only queries, but not enrolled in the durable ledger.
5. `engine/actor/ActorModule.h/.cpp`
   - Exposed `synchronizeRegionOwnership` and, **only for** rules with `regionOwnership={}`, runs it after the rest of map setup (including race placement) and installs an `actor.regionOwnership` ECS system after the normal actor system.
   - Old games with no opt-in retain the same systems and save schema. The setup work moved from initial character/car registration to the single reconciliation owner so durable loot can be registered too.
6. `engine/actor/CMakeLists.txt`: compiled the new translation unit into existing `aiverse_fps` target (no duplicate system target).
7. `tests/world/actor_region_lifecycle_test.cpp` (new) + root `CMakeLists.txt`: CTest `actor_region_lifecycle_test`, target `aiverse_actor_region_lifecycle_test`, labels World/ECS/Vehicles.
8. `tests/world/region_entity_ledger_test.cpp`: new mixed-source atomicity assertions.
9. `tests/world/actor_region_position_test.cpp`: corrected earlier stage07 fixture to set `mechanics.enabled=true` so its map car is actually spawned.
10. `docs/region-entity-ledger.md`: Stage08 behaviour, API, exclusions and limitations.

## Test scenarios authored

- Real world-space GameCore and ActorHost, independent two-peer checksum parity, 1 fixed-tick automatic region update for an unoccupied car.
- 100 explicit consecutive car+driver+passenger authoritative region reconciliations with a passenger initially coming from another region, and matching peers at each step.
- Save to native ECS v10, independent new Simulation load, checksum, and COW rollback after another ownership transfer.
- Dynamic Loot entity appears after setup, migrates, is destroyed, is tombstoned; reused ECS slot generates a new generation-based ID, old tombstone remains deleted after save/reload.
- Orphaned CharacterBody.vehicle and duplicated Vehicle.id fail closed.
- Mixed-source ledger no-op / stale-owner / duplicate ID refusal.
- No-opt-in GameCore has no W1 ledger and remains legacy v9.
- Previous W1.1–W1.7 tests remain registered; stage07 car fixture now matches actual ActorHost mechanics requirements.

**Actual tests executed:** NONE. GitHub connector successfully committed files and the code was source-inspected, but no native target was compiled and no CTest binary was run on the owner's required Mac/ `/Volumes/KINGSTON` volume. Do not claim these authored assertions passed. No screenshot, performance or memory result was collected.

## Important remaining blockers

1. The auto-sync currently scans all live CharacterBody/Vehicle/Loot pools and every active/tombstoned ledger record on each tick. This is a functional correctness slice, **not** suitable for millions of open-world entities until indexed/event-driven changes and disk paging are available.
2. The sync happens after the actor gameplay systems. If a direct teleport or overspeed movement escapes the pre-tick collision preload envelope, the post-tick check throws and the world may already have advanced part of that tick. A **pre-commit staged destination admission/rollback** system is required. Do not claim fully transactional movement, teleport safety or guaranteed recoverability.
3. Existing ActorHost gameplay systems and shared vehicle seating code are not universally `RegionDormant`-aware. Real CharacterBody, VehicleDrive and related state is **not parked** by this stage. Hot/cold runtime on world-space ActorHost still requires explicit safe component owner integration.
4. Live non-projectile entity types other than CharacterBody/Vehicle/Loot (e.g. arbitrary game-native NPC, cargo and project-specific actors) still require an explicit shared ownership registration interface. Projectiles were deliberately excluded.
5. The stable ID policy `Entity::packed()+1` works for the current simulation allocator and survives generation recycling, but offscreen entity eviction / on-disk remapping and external reference migrations have not been designed.
6. Real car/occupant crosses are proven only by authored tests and source algorithm. Tests remain uncompiled; no merge/checksum acceptance should be asserted.
7. Branch was 39 commits behind main at stage start. **Resolve concurrent changes and run test suite on a merged Mac tree before main integration**, rather than blindly rewriting active `main`.

## Stage commits

- `84030fb8ff8361a2834d4cbece33d46b42f4ecc7` ledger mixed-owner group + records
- `ad42023a14136cd71bceee5403342905e4b8a642` mixed-owner ECS group bridge
- `1a5250882cf33d6a653b88f456fd237d1e7bca5d` ActorHost API declaration
- `6986a06558aba5571c99ed31005d5f9a11170c01` canonical loot/projectile readers
- `b0b65c8febe31575719cd7410b8cf3b8107bac5f` new automatic reconciliation implementation
- `41d261c392d586ae8f0340916d1d7c1de98520ba` actor build target
- `0635522b98947e9a140d5fcf72c0a7992ea2a637` opt-in setup and fixed-tick system
- `de22604b972d04f68f66437708274831361eba10` real ActorHost lifecycle test
- `54d0404fc7c7cdadd579f99f1c68645e867f1e09` CTest registration
- `dbb7d7d0032323ca5faca710aac4621de40fb45c` stage07 fixture mechanics fix
- `a7971e80c859e6e1eb1a3cb07b3f1a93045dfb67` mixed-origin ledger test
- `ed264013d1180ed24c5da571631f51fd3a82e718` orphaned vehicle reference validation
- `808704715e59549c6c1d0dce411cf9e3934a7491` orphan-link test
- `b023cae2ba96dbc01b0b8478408c5b2881965eda` documented automatic reconciliation
- `4d481b0b62c61fa53b04e21795006c641f1e783c` documented vehicle integrity refusal

## Next agent: acceptance then W1.9

1. Fetch latest main and audit conflicts with `feature/aiv20261123_open_world_audit`. On Mac with `/Volumes/KINGSTON`, build all nine W1 test targets and run CTest, especially the new `actor_region_lifecycle_test` and corrected `actor_region_position_test`; fix actual compiler errors and failed assertions first.
2. Run `./run-tests.sh --quick --build-dir /Volumes/KINGSTON/AIVERSE-builds/open-world-w1/Debug` on a merged test worktree, preserving the original main until acceptance. Record exact SHA and results.
3. Move movement-region admission **ahead** of authoritative gameplay state mutation so failed async collision/teleport is a pure refused tick (no command/RNG/checksum changes). Add real failure/retry and two-observer tests.
4. Replace per-tick full scans with world-owned indexed lifecycle events and bounded region journals; record deterministic background region work without a new genre host.
5. Add real safe hot/cold lifecycle for NPC, vehicles and occupants only after all ECS owners explicitly exclude dormant components and preserve cross-region references.
6. Disk region checkpoint/journal, crash recovery, replay and memory-budget proof; then screenshots and main merge if all tests pass.
7. Write another discrete stage handoff with real test evidence.

No release-ready W1, no merge to main, PR #116 remains Draft.
