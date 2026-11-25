# AIVERSE Open World — W1 stage 06: shared vehicle/occupant region groups
Date: 2026-11-23
Repository: chromealex/AIVERSE
Branch: `feature/aiv20261123_open_world_audit`
Draft PR: https://github.com/chromealex/AIVERSE/pull/116
Area: W1 / OW-01; **partial**. World roadmap remains in-progress.
Previous handoff: `docs/agent-handoffs/open-world-stage05-worldspace-readiness-2026-11-23.md`

## Intent and actual scope

W1.6 begins the hardest ownership integration: transferring one shared
`vehicles::VehicleDrive` car with its driver and passengers across region
boundaries without losing occupant links or inventing a second transport
controller. This stage implements **read-only, validated group discovery** and
an atomic ledger-transfer policy when all participants are registered; it
does **not** yet enable global world-space hot/cold activation for all cars.

Key discovery: the production world-space `actor::ActorHost` stores seat
actors as integer player slots and the active character position in
`actor::CharacterBody`. The shared `RegionEcsBridge` currently expects
`Transform` and `RegionStableIdentity` for every member. A vehicle or
passenger group in an unmodified world-space scene therefore MUST refuse an
ownership transfer. A cosmetic `Transform` is not a legal workaround.

## Changed files and commits

1. `engine/world/VehicleRegionTransfer.h` — new generic, header-only
   `VehicleRegionTransfer::gather` and `transfer`. Commit
   `6dd9deeb58531da67f78533f9051118f77cbf2d6`.
2. `tests/world/vehicle_region_group_test.cpp` — shared ECS fixture using
   actual `VehicleDrive`, `VehicleOccupant`, ledger stable IDs,
   grouped transfers, invalid seats, missing IDs, dormant members,
   stale passengers and reverse occupant links. Added in
   `2da23d3dc5a501c4926057528e246e59be0ce4ae`, corrected in
   `eac6f06507933216a8c34759a7edb35bafa2d1f8`.
3. `engine/actor/ActorModule.h` — read-only public
   `vehicleRegionGroup(const Simulation&,Entity,VehicleRegionGroup&,string*)`
   declaration and generic header include; commit
   `05c8b3109ffc3a5b6f3fdd1a0f450ad1ad72d5c0`.
4. `engine/actor/ActorVehicles.cpp` — thin world-space host adapter,
   translating integer seat actor slots to real `CharacterIndex` entities
   without casting slots into Entity indices; commit
   `3ab61788a393899205491a1b38733f3aeb6768ef`.
5. `tests/world/actor_vehicle_region_group_test.cpp` — real
   `GameCore`/`ActorHost` fixture that resolves seats and
   explicitly rejects ledger transfer before stable actor identity +
   position support is installed; commit
   `44097e0d131400d87acea5e0b7908a84afefa625`.
6. `CMakeLists.txt` — registered CTests `vehicle_region_group_test`
   and `actor_vehicle_region_group_test`, labels World/ECS/Vehicles;
   commit `85e091f3d264c61eb7c35edb673bebce3a6d64f7`.
7. `docs/region-entity-ledger.md` — usage and limitation section;
   commit `c97f108eaf3322824da70490eeb7d9431cea5008`.
8. This separate stage handoff.

## Public shared API

`world::SeatEntityResolver = std::function<Entity(int64_t)>`: the host
converts its existing seat actor identity into an ECS entity.

`world::VehicleRegionGroup {Entity vehicle; vector<Entity> members;}`:
read-only immutable result for a successful gathering operation, sorted by
packed Entity handles.

`world::VehicleRegionTransfer::gather(world,car,resolver,result,error)`:
- Requires live `vehicles::VehicleDrive` car.
- Includes car, driver, resolved seat manifest occupants and all reverse
  `vehicles::VehicleOccupant` backlinks to that car, without duplicates.
- Refuses missing/stale seat actors, duplicate seat assignments,
  conflicting occupant-to-car backrefs and missing resolver when seats exist.
- Does not mutate ECS state, vehicle physics, seat manifests, RNG or ledger.
- Does not infer arbitrary external passenger / cargo components; a host with
  additional relations must extend the authoritative dependency policy.

`world::VehicleRegionTransfer::transfer(world,ledger,car,resolver,from,to,error)`:
- First gathers group.
- Delegates to `RegionEcsBridge::transferGroup`: all members must be live,
  `Transform`-bearing, non-dormant, stable-ID tagged and owned by `from`.
- No ledger updates happen if validation fails; transport movement/seat changes
  remain the job of shared `engine/vehicles` and the host's gameplay logic.
- This method does NOT check destination collision/NavMesh readiness. Caller
  must do this with host readiness **before** movement/transfer.

`actor::ActorHost::vehicleRegionGroup`:
- Maps integer player-seat IDs through `ActorHost::characterEntity` /
  CharacterIndex, then reuses `VehicleRegionTransfer::gather`.
- Does not add stable identity, `Transform`, or alter the car/occupants.
- The function is a thin host adapter, not a world-space-only copy of the
  generic vehicle physics or region-transfer policy.

## Tests actually run

**Not run:** neither newly authored CTest target has been compiled or executed
in a real AIVERSE checkout; no Mac/KINGSTON build, quick suite, replay/hash
baseline, GPU screenshot or throughput benchmark. Test source and registration
were created through GitHub, not validated by running. Previous W1.0–W1.5
native tests also still require actual Mac acceptance before this branch
can merge. Do not claim PASS or W1 core.

**Inspected:** current GitHub source of `engine/vehicles/Vehicles.h/.cpp`,
`engine/mechanics/VehicleSeats.h`, `engine/actor/ActorVehicles.cpp`,
`ActorComponents.h`, `ActorModule.h`, W1 cold store/ledger/bridge and
current branch ancestry. The existing world-space character position source
was confirmed as `CharacterBody`, not an automatically persistent
`Transform`.

## Outstanding architecture and acceptance debt

- World-space characters require an authoritative generic world-position
  binding for RegionEcsBridge. Do not simply duplicate position into a second
  state representation. A neutral position-adapter API or extraction of the
  canonical position owner is needed; stable-ID and component save contract
  must be explicit.
- Seats/driver relations and `VehicleOccupant` are not always identical in
  all hosts. This stage gathers both, requiring resolvers to be consistent.
  It is NOT a car fleet or general cargo/hierarchy transfer planner yet.
- W1.5's streamed collision/NavMesh gate must be checked when transferring
  a car/occupants before a gameplay tick, not only when running renderer
  streaming.
- Production `ActorHost` still does not invoke
  `RegionLifecycleRuntime::install`. Grid GameCore does through a native
  module fixture; real world-space registration is missing.
- Parking actual `VehicleDrive`/seat/CharacterBody components will require
  all step systems to explicitly skip dormants. Don't park these now.
- Further work: disk-backed region persistence with crash recovery, offscreen
  progression, bounded entity hot/cold memory, full seat reparenting and
  collision/navmesh blocking, stress/performance acceptance and native tests.

## Next agent instructions — W1.7

1. On the user's Mac, require writable `/Volumes/KINGSTON`. Build and run
   `aiverse_vehicle_region_group_test` and
   `aiverse_actor_vehicle_region_group_test` plus the five prior W1 tests
   on latest merged tree. Correct actual compilation/runtime failures first.
2. Create a generic, authoritative position/region adapter for world-space
   `CharacterBody` / host actors without copying positions into
   non-authoritative `Transform` components, preserving ECS persistence,
   checksum and COW rollback. Prove a real vehicle + driver + passenger
   crossing after integration.
3. Connect world-space opt-in hot/cold registration, with component ownership
   and awake/dormant guards in *all* relevant actor + vehicle systems. No
   genre flags or mode-specific branches.
4. Connect destination collision/navmesh ready gates and test denied load,
   teleport, 100 real crossings, failure retries, fresh save/restore and
   peer checksums.
5. Finish disk-backed region journal and entity memory eviction only after
   stable identity/position correctness. Build and screenshot actual
   gameplay before claiming open world finished.
6. Write another separate handoff after the stage with exact SHA, build,
   tests, failures and next steps. Don't merge draft PR #116 into main
   until the Mac quick suite on the merged tree passes.

Release status: not ready, no W1 core claim or public demo.
