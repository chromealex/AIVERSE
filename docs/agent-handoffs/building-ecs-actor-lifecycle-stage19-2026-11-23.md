# AIVERSE Building Generator — Stage 19 ECS actor lifecycle and safe scene streaming

Date: 2026-11-23
Repository: chromealex/AIVERSE
Branch: feature/aiv20261123_building-grammar
PR: https://github.com/chromealex/AIVERSE/pull/114 (Draft)

## Why

Existing BuildingCirculationEcsScene supported real ECS rider carry, landing door collision, authenticated commands, owner-phase authoritative publication and snapshot encoding. However, its detach() is forced and unconditionally invalidated ECS passenger markers even while actors remained in the cabin. Deleting an ECS actor directly while riding also left an orphaned packed actor identity in the circulation host and subsequently faulted rider publication. Stage 19 introduces a fail-closed lifecycle contract rather than inventing a second ECS/physics system.

## Implemented

- `engine/buildings/BuildingCirculationEcsScene.h`
  - `prepareActorRetirement(Entity,std::string*)`: call before authoritative ECS destruction/logout. Reject missing/dead actors, scene faults, and riders without actual physically open landing doors. At a valid landing, verify that the existing ECS passenger marker belongs to this scene and lift, then use the existing bridge's authoritative `releaseAtLanding` to reduce host occupancy and unlink the marker. Clear the actor's authenticated command sequence on success. The caller may destroy the actor only after true is returned.
  - `canUnload() const`: reject missing, faulted and currently occupied scenes.
  - `detachIfVacant(SpatialWorld&,nav::Mesh*,std::string*)`: safe ordinary streaming detach, refuses an occupied scene. Existing `detach()` remains forced world-teardown API, NOT for streaming.
- `skills/world/aiverse-building-style/SKILL.md`: added Stage 19 integration instructions; registered skill retains its existing catalog entry.

## Ownership and API rules

Call `prepareActorRetirement` only from the exclusive authoritative world-owner phase, and before ECS generation reuse. If it rejects an in-transit rider, keep its ECS entity alive or preserve a valid server-owned pending-retirement state until landing. Do NOT silently delete `BuildingLiftPassenger` or decrement lift counts independently. Existing `SpatialWorld`, nav::Mesh, ECS World/Entity, BuildingLiftEcsBridge and authenticated transport retain their owners. Door obstruction remains governed by live ECS/PHYSIS guards. This API does not teleport riders or advance the lift until it arrives.

For streaming, `detachIfVacant` is mandatory; `detach` is the destructive override and must be preceded by caller-owned rider reconciliation. Using BuildingLiftGameSession and BuildingCirculationEcsScene for the same lift is unsupported. This stage does not add genre-specific FPS/RTS code.

## Exact provenance and validation

- `9be80afdea20b5e2db8d183ed16f2088a4ff9a17` — lifecycle gate and safe unload API.
- `236ab85572c7f6c635a218f95f8c662a5a705144` — skill instructions.
- Handoff publication is a separate documentation commit after these commits.

User requested tests later. NO compiler, static analysis, CTest, live gameplay actor simulation, Mac GPU screenshot or multiplayer verification was executed for this stage. GitHub source update is not build evidence. The branch is substantially divergent from current main and must stay Draft until integration and verification.

## Remaining work / next stage

1. Hook game character/controller authoritative destroy/logout queues to `prepareActorRetirement`, including disconnect-in-motion, and guarantee deferred entity retirement on the actual landing tick.
2. Add idempotent rider cleanup at the actual ECS actor lifetime boundary, plus scene ID generation/reconnect protection.
3. Add per-actor motor collision sweeps for stair traversal, boarding/exit and lift carry; confirm physical bodies cannot pass closed door collision.
4. Bind building static solids and floor elevations to native nav::Source and NavMesh generation; preserve existing physics/nav owners.
5. Later run C++20 compilation, focused tests, all CTest, Studio TypeScript and Metal/GL33 capture QA; reconcile concurrent main changes without overwriting them. Add a new handoff after the next stage.
