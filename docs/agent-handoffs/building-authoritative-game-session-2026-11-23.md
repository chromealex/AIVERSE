# AIVERSE Stage 17 — authoritative ECS lift game session

Date: 2026-11-23
Branch: feature/aiv20261123_building-grammar
PR: https://github.com/chromealex/AIVERSE/pull/114 (Draft)

## Code implemented

- engine/buildings/BuildingLiftGameSession.h
  provides BuildingLiftGameSession<Body>: a native authoritative game facade
  composing existing ECS World/Entity, SpatialWorld, nav::Mesh,
  BuildingCirculationScene, BuildingLiftEcsBridge and existing lift snapshot codec.
- engine/buildings/BuildingRuntime.h exposes this facade to native games.
- attach(generated, spatial, nav, platformColliderIds, sceneId, origin, yaw,
  authorize) requires a caller-supplied ECS actor permission callback.
- attachDoors(spatial, nav, landingDoorIds) publishes physical door leaves via
  existing BuildingLiftDoorOwner. Player boarding and exit are gated by real
  open door state; no boarded actor may be taken from arbitrary client inputs.
- board and exit confirm actual ECS actor identity, owner permission and
  physically located feet in the lift platform volume, then use the existing
  BuildingLiftEcsBridge for actual passenger marker ownership.
- requestDestination checks authorization and onboard rider registration.
  call checks authority for an ECS actor, not arbitrary client feature IDs.
- advance(ecs, spatial, nav, elapsedMs, serverTick):
  first commits physical landing/cabin collision changes, then publishes
  rider ECS motion, then captures and validates a server state snapshot.
  It exposes BuildingLiftStateFrame ONLY if all these owner phase mutations
  succeeded. Failure faults the scene and dirties navmesh.
- rebuildNavigation defers to the authoritative existing native nav::Mesh
  refresh owner. detach removes dynamic lift-owned colliders, not game actors.

## Reused systems (avoid duplicate implementations)

engine/buildings/BuildingLiftEcsBridge.h
engine/buildings/BuildingLiftDoorOwner.h
engine/buildings/BuildingLiftReplication.h
engine/buildings/BuildingCirculationHost.h
engine/buildings/BuildingCirculationScene.h
engine/buildings/BuildingLiftColliderOwner.h
engine/buildings/BuildingNavMeshBridge.h
libraries/include/aiverselib/systems/BuildingRouteExecutor.h

Game ECS remains the single authoritative source for actor state. Existing
SpatialWorld is still the only collision owner, native nav::Mesh remains the
navigation source, existing authenticated network transport sends snapshots.
No genre dependencies were added.

## Commits and actual verification

- 32865c533e394c73e2c571de4e6262db290b1ed1: game session.
- cc004d674ef62a5b965a6f1496e76ab2e0244940: require door and actor
  permission gate before boarding/exiting.
- 50af0f0f510d076b2ec2d6762e43d98735645d39: include facade.

User said tests later. Source writes/readback only; NO new clang/GCC,
CTest, Studio TypeScript, Mac/Metal GPU or gameplay end-to-end test has run.
Do not claim verified multiplayer or integrated gameplay from these files.
Older 19/20 Mac batch tested legacy building size exports, not this session.

## Constraints and next concrete step

The game must pass a valid authorization callback from its existing network
or actor-ownership system. attachDoors must be called after attach and before
boarding. Caller must run advance only on exclusive world-owner phase,
and must send captured snapshots over its existing authenticated transport.
The library does not automatically attach Body render proxies or ECS game
entity lifetime. It does not own actor destruction, physics motor placement,
platform mesh GPU retirement, navmesh refresh Source or remote client state.

Next stage: integrate with the game's real ECS fixed tick and transport,
bind actor destroy / logout cleanup, guarantee no riders on scene detach,
door obstruction interlocks and moving render-node transforms; later
compile/test and capture real GPU screenshots on Mac. Do not merge to main
until verified and reconciled with concurrent Studio and parent changes.
