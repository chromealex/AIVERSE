# Building circulation runtime — Stage 14

This API is genre-independent. It uses the existing BuildingStructureGraph
for room connectivity, the existing engine/nav/NavMesh.h for normal
horizontal paths, and SpatialWorld for movable lift colliders. There is
NO second pathfinding engine or standalone scene physics simulator.

## Native headers

- libraries/include/aiverselib/systems/BuildingCirculationRuntime.h:
  deterministic room BFS and authoritative lift controller.
- engine/buildings/BuildingNavMeshBridge.h: resolves room floor positions
  and vertical edge endpoints onto nav::Mesh polygon IDs. All horizontal
  passages use nav::Mesh::findPath; stair segments return actual tread
  waypoint chains; lift segments return a RequestLift action.
- engine/buildings/BuildingCirculationHost.h: owner of the graph,
  revisioned nav-link resolution and per-feature lift controllers.
- engine/buildings/BuildingLiftColliderOwner.h: publishes a separate
  moving lift collider into the EXISTING SpatialWorld and marks changed
  NavMesh cells dirty. The navigation owner then refreshes the same
  navmesh from its existing Source. Until refreshed, navigation queries
  refuse stale data.
- engine/buildings/BuildingLiftPlatformModel.h: creates one movable
  platform Model at zero local height, built only ONCE. Its owner updates
  the instance transform on subsequent lift position changes.
- engine/buildings/BuildingRuntime.h: umbrella include for native
  client games. Dedicated servers may include the portable generator
  and circulation headers without linking Renderer3D.

## API flow

1. Register style bytes from packaged assets with
   RuntimeBuildingGenerator::registerStyleAsset().
2. Generate an independent functional recipe via generateRecipe().
   For gameplay models, the lift cabin is EXCLUDED from the STATIC
   building Model and static collision; the graph still contains its
   original ElevatorPlatform solid for initial pose discovery.
3. Initialize BuildingCirculationHost from the generated result.
4. For every lift controller create one runtimeLiftPlatformModel()
   and register a unique dynamic collider ID with
   BuildingLiftColliderOwner::registerPlatform() on the world owner
   phase. Do not add its initial platform as a second static collider.
5. Build/refresh the existing navmesh after publishing collision. Call
   host.rebuildNavigation(mesh) to resolve vertical off-mesh anchors.
6. host.route(mesh, fromRoom, toRoom) returns a list of actions:
   WalkThrough with the native A* corridor, ClimbStairs with a chain
   of generated physical tread waypoints, RequestLift with two room
   landings that require an actual elevator journey.
7. Lift journey: host.callLift(feature, pickupFloor); once readyAt(pickup),
   host.boardLift(feature,pickupFloor), then
   host.requestLiftDestination(feature,destinationFloor).
   Actors disembark with host.disembarkLift(...). Boarding and
   disembarking are only permitted with open doors at the current floor.
   Occupied lifts cannot be redirected by a different external call.
8. Call host.advance(deltaMs) in the authoritative simulation owner
   phase. Consume returned BuildingLiftPlatformMotion commands and
   call colliderOwner.applyMovement(spatial,&navmesh,command) from the
   PHYSIS/SpatialWorld owner. Update the render INSTANCE transform
   from the same platform position; don't regenerate the mesh each tick.
9. Refresh dirty navmesh tiles with the existing nav::Source, then
   re-run host.rebuildNavigation(mesh). Off-mesh registrations carry
   navmesh revision and are REJECTED if the live mesh was refreshed.
10. Replicate BuildingLiftSnapshot (floor/target, phase, y in integer mm,
    occupancy and monotonic revision) using the game's existing
    network model. A client may call applyReplicatedLiftState; the
    authoritative server must never accept arbitrary client snapshots.

## Limits and ownership

- The present engine/nav/NavMesh.h has no built-in off-mesh portal
  insertion. The new bridge therefore keeps its cross-floor links in
  an external registry, **not** as imaginary native mesh portals.
  A game motor must execute stair segment waypoints with its existing
  step/sweep checks, and explicitly board/carry riders in an elevator.
  A successful room-graph route is not itself proof that any actor
  traversed a staircase.
- Cabin collision is updated by the existing SpatialWorld owner, not
  the lift controller. Dynamic collider IDs belong to that owner.
  Rebuilding navmesh after collision movement can be expensive if done
  every frame; batch refreshes according to the game's navigation policy.
- Lift time is advanced in integer milliseconds and position in integer
  millimetres; mesh generation uses floats for presentation, never
  authoritative gameplay lockstep calculations.
- The lift controller is single-cabin, with capacity 8, simple open/
  close/move phases and no elaborate queue arbitration. Stairs are
  switchback physical boxes but need motor clearance checks.
- No Unity/UE scripting, no genre-specific role names, no engine-editor
  dependencies in the portable circulation controller.
- Motor-step traversal, passenger parenting, collision owner integration,
  navmesh rasterization, visual animation and network behavior have
  NOT been validated in a running game or Metal screenshot yet.
- User explicitly deferred tests for this stage. No test binary
  or CI result is claimed as passing here.

## Next

Implement multi-cabin requests and ECS actor boarding attachment on the
existing gameplay owner; validate real step-following across the generated
staircase, navmesh source sampling and moving collider synchronization.
Then run C++20/Clang+GCC, existing native Studio checks, Mac/Metal,
Vulkan/GLES and network/state tests as a separate requested QA phase.


## Stage 16 — authoritative lift interactions and safety

Game clients never send a complete lift state to authority. An existing
authenticated server handler calls BuildingLiftAuthority<Body>::execute()
from engine/buildings/BuildingLiftCommands.h, passing the client ID that
the existing connection authenticated. The handler checks actor ownership,
scene ID, per-actor increasing request sequence and authoritative physical
landing access before Call, Board, SelectFloor or Exit. Board requires real
cabin-volume validation from the existing BuildingLiftEcsBridge.

When landing doors are attached, call
BuildingCirculationScene::setDoorObstructionProbe(worldOwnerCallback).
The callback returns true for a physically blocked landing/cabin opening.
Blocking during Closing reopens doors. Without a sensor, lift simulation
will not advance with physical doors enabled. Only the existing
PHYSIS/ECS owner should query live actor/object collision volumes.

Server state publication uses BuildingLiftReplication.h:
captureBuildingLiftServerFrame(host, sceneId, serverTick) copies the
authoritative state *after* world collision and actor carry commits;
encodeBuildingLiftStateFrame writes a bounded little-endian, versioned
payload. The existing authenticated server->client snapshot channel
transports those bytes. Clients call decodeBuildingLiftStateFrame, then
BuildingLiftReplica::acceptServerFrame and applyToClient.
The client replica preflights all lift states, rejects foreign scenes,
old server ticks/revisions and implausible landing poses. No new socket
or general network transport is created.

**Scope**: Owner-facing APIs have been written to GitHub; this is not
yet a finished in-game lift interaction or an actual multiplayer run.
The network/game owners must register the new snapshot field and command
dispatcher, supply physical door sensors and route actor motor events.
Tests and native GPU evidence are explicitly deferred by the user.
Agent handoff: docs/agent-handoffs/building-lift-safety-network-2026-11-23.md.
