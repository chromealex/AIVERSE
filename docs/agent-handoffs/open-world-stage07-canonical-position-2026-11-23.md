# AIVERSE Open World — W1 stage 07: canonical position ownership for real world-space actors

Date: 2026-11-23
Repository: `chromealex/AIVERSE`
Branch: `feature/aiv20261123_open_world_audit`
Draft PR: https://github.com/chromealex/AIVERSE/pull/116
W1 / OW-01 status: **partial**, NOT production accepted
Preceding handoff: `docs/agent-handoffs/open-world-stage06-vehicle-groups-2026-11-23.md`

## What was actually changed

The existing world-space `ActorHost` owns its characters' positions in
`actor::CharacterBody.pos` and its cars' positions in `actor::Vehicle.pos`,
not in `Transform`. Stage 06 correctly refused to move a car with passengers
in the shared W1 ledger because the shared region bridge only accepted
`Transform`. Stage 07 introduces an explicit **read-only canonical position
adapter**, preserving that existing authoritative owner.

- `engine/world/RegionEcsBridge.h`: new `RegionPositionReader` callback
  (`std::optional<Vec3>(const World&,Entity)`), `regionTransformPosition`
  default, additional overloads for `bindNew`, `bindExisting`,
  `transferGroup`, `rebuild`, and `crossings`. All old API signatures
  continue to use `Transform`; no new position ECS pool is allocated.
- `engine/world/VehicleRegionTransfer.h`: overload for canonical position
  reader; existing Transfer signature still delegates to `Transform`.
- `engine/actor/ActorModule.h`, `engine/actor/ActorVehicles.cpp`:
  `ActorHost::regionPositionReader()` returns `CharacterBody.pos`,
  `Vehicle.pos` or real `Transform.pos` in that order. New
  `ActorHost::transferVehicleRegion` verifies the owner's streamed collision
  / navigation readiness and destination car bounds, requires that the actual
  car position is in the destination W1 cell, validates the complete shared
  driver/passenger group and atomically updates ledger ownership.
- `engine/actor/ActorModule.cpp`: opt-in root `rules.regionOwnership={}`
  registers `RegionEntityLedger` and `RegionStableIdentity` via the
  existing native ECS persistence system during setup. Initial real characters
  and map-spawned vehicles receive stable IDs (packed Entity handle + 1)
  and owner regions computed from the canonical positions in 32-unit cells.
  No rules block means no W1 schema registration, no changed legacy save.
- `engine/world/RegionColdStore.h`: allow controller position reader and
  pass it to the transactional cross-schema load validator. Existing
  Transform-based controller remains the default; no production actor/vehicle
  core components are parked implicitly.
- `engine/world/RegionLifecycleRuntime.h`: pass configured position reader
  to `registerRegionColdState`, so opt-in runtime activation can use the
  same authoritative host-position owner.
- `tests/world/actor_region_position_test.cpp`: new actual world-space
  `game::GameCore` fixture with two characters and one map vehicle,
  `regionOwnership={}` rules, manually attached test seat bindings,
  canonical crossing detection, occupied car atomic ownership transfer,
  optional synthetic parked component, full ECS v10 save/reload and COW
  rollback. The artificial typed `VehicleDrive` is removed before full
  untyped map vehicle serialization to avoid changing the host's production
  vehicle definition. Explicit no-opt-in v9 format assertion.
- `CMakeLists.txt`: registered `actor_region_position_test` /
  `aiverse_actor_region_position_test`, linked `aiverse_game aiverse_fps`.
- `docs/region-entity-ledger.md`: setup, API, ownership boundaries,
  existing-map compatibility, current limitations, next tasks.
- This handoff.

## Commits (in order)

1. `b712c81b1589df00c4a6e2b5eb6d338ea61a4930` — generic canonical position reader API.
2. `440a06a0699d627d71899ade80cb0d90be096023` — shared vehicle transfer reader overload.
3. `02f60b86380e919dbe1dfef18d03e5aeb245cea4` — ActorHost method declarations.
4. `d076692e7dae2a167013b1ada32561ee626fe896` — world-space position reader/guarded vehicle transfer implementation.
5. `295beb5e972572541ccc8c2bbe4bef3b4e4a2edb` — opt-in actor+vehicle region identity registration during setup.
6. `6c4d94bf30be6ef2898ad6c10a8296259eb6c0ba` — real world-space position/save fixture.
7. `f887ea8ed4af0d2d2c09cf4dee82fce6e5ef265e` — correct assertion macro with Cell initializer.
8. `1a2e9234542bc7fa970f9370323c162a19bf26b4` — CMake CTest registration.
9. `1c3ba0ca4f9221b71098c26d626e62e551c9a3a7` — cold store canonical position support.
10. `f9b2450cae7fabd539b3e8cb6b9212a3ebac6a25` — runtime passes configured reader to load validator.
11. `3d3659823975b7c430ce2fb3a994b3a23b418359` — test opt-in cold park/wake of auxiliary component.
12. `554b0a1d2f66ded31fd3237816052308b9304473` — W1.7 public documentation.

## Architecture decisions

- No alternate ActorHost Transform or position mirroring. Only one source
  of truth for movement (CharacterBody/Vehicle, or the generic Transform
  for hosts which already own one). Collision and navigation admission
  are queried using the existing ActorHost/CollisionStream mechanisms.
- The new W1 system may transfer ownership **after** authoritative movement
  has placed a vehicle in the destination cell. It does not move an actor.
- A seated driver and passengers remain in one atomic group from the
  stage06 `VehicleRegionTransfer`; stale seats, missing IDs, or absent
  destination collision fail without changing ownership.
- The root rules block `regionOwnership` is data-presence opt-in and
  genre-neutral. Existing world-space content without the block has no
  additional W1 component or persistent resource.
- Only initial map actors/cars are automatically tagged so far. Dynamic
  spawning, respawns, despawns and moving unoccupied NPCs still need
  hooks from their owning ECS systems. Don't silently mark them handled.
- `RegionDormancyController::setPositionReader` enables only explicit,
  codec-registered supplemental components to be parked; it does **not**
  make removing ActorHost gameplay pools safe. The world-space
  `rules.regionOwnership` setup does not install cold-storage runtime
  or an automatic interest scheduler.
- A generic cold-store load validator with custom reader is installed
  by `RegionLifecycleRuntime::install` only when that runtime is opted in
  explicitly by a suitable gameplay owner.

## Tests: written vs executed

**Written**:
- `actor_region_position_test`: no duplicate Transform, stable identity
  ownership, crossing diagnostics, occupied car region transition, strict
  previous-owner refusal, custom component park/wake, native ECS v10
  disk-state checksum/restore and COW rollback, no-opt-in legacy v9.
- Existing five W1.1–W1.5 suites plus W1.6 grouped vehicle suites remain
  registered and must still be executed for real.
- The source was inspected through GitHub after commits.

**Actually run**: No compiled C++ or CTest on the user's Mac/KINGSTON
volume. Current runtime has no mounted `/Volumes/KINGSTON` and cannot
execute the required macOS test environment. Do not claim these written
assertions passed. No performance, frame-capture or replays were run.

**Integration warning:** At this stage's start `main` was **39 commits
ahead** of `feature/aiv20261123_open_world_audit`; do not assume
this source automatically merges cleanly. Rebase/merge and resolve conflicts
after inspecting concurrent work before running the full acceptance suite.
PR #116 is intentionally Draft.

## Remaining W1

1. **Mac acceptance first**: on latest merged branch and writable
   `/Volumes/KINGSTON/AIVERSE-builds/.../`, configure/build all eight W1
   test targets and run them; capture exact errors/pass counts. In
   particular check interaction of v10 persistent ECS and the ActorHost
   module's own v10 save with `regionOwnership` enabled.
2. Add safe hooks at all actual gameplay spawn, death, respawn, seat switch
   and vehicle move boundaries; use stable IDs to keep every dependent
   occupant and vehicle ownership synchronized. Validate 100 completed
   world-space crossings on two independent simulations.
3. Introduce the authoritative deterministic active-region policy and
   opt-in hot/cold runtime for the world-space host only after deciding
   which actual ActorHost/Vehicle components are safe to park.
4. Integrate dynamic collision/ NavMesh readiness for teleports and
   destination admission, including async failure, retries and
   non-advancement of tick/RNG/commands.
5. Finish disk-backed region checkpoints, eviction and offscreen
   progression without retaining an unlimited live Entity count.
6. Validate p95/peak memory/performance, macOS quick suite,
   cross-platform replay parity, and visual screenshots before marking
   W1 done or merging to main.

Stage status: W1 partial, docs and code source committed, no release
or main merge.
