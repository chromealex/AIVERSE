# AIVERSE Building Generator — Stage 16 authenticated command integration
Date: 2026-11-23
Branch: feature/aiv20261123_building-grammar
Draft PR: https://github.com/chromealex/AIVERSE/pull/114

## Delivered

Existing branch work was inspected before changes. Other agents had already
implemented the actual ECS lift actor bridge, native landing door collision,
authoritative scene publisher, revisioned server state snapshots and a
server-side lift command authority. Stage 16 does not duplicate these.

Files changed:
- engine/buildings/BuildingLiftCommandWire.h (new):
  fixed-size little-endian client command codec, version/magic/reserved
  validation, 44-byte bounded packet, nonzero scene/actor/sequence, floor
  and enum validation. This is a payload codec ONLY: no sockets, authentication,
  command execution or new network transport.
- engine/buildings/BuildingCirculationEcsScene.h (updated):
  persistent BuildingLiftAuthority<Body> tied to an existing ECS World and
  the existing circulation/bridge owners. New
  configureAuthenticatedCommands(OwnsActor,AtLanding) requires both trusted
  callbacks, handleAuthenticatedCommand(clientId,cmd) and
  handleAuthenticatedPacket(clientId,bytes). The authenticated client ID
  comes from the existing authorized transport, NEVER from a packet field.
  Existing authority checks actor owner identity, ECS entity existence,
  session/scene, strictly increasing accepted command sequence, lift state,
  landing access and physical boarding/disembarking.
  Direct ECS board/exit now require attached physical landing doors (fail
  closed rather than assuming missing door collisions imply an open portal).
  Authorization callbacks and nonce registry are discarded on detach.
- skills/world/aiverse-building-style/SKILL.md:
  documents secure command authority and no direct client state updates.
- docs/building-functional-recipes.md and
  examples/world/runtime-building/README.md:
  integration contract and command wire snippet.

## Integration contract

1. Authoritative server owner creates BuildingCirculationEcsScene<Body>
   bound to its existing ECS World, SpatialWorld and nav::Mesh.
2. Attach the building and its dynamic lift colliders, then attach the
   game-assigned landing door colliders.
3. Configure server-owned OwnsActor(clientId,Entity) and
   AtLanding(Entity,feature,floor) guards. Never accept client-supplied
   scene identity or entity ID without these checks.
4. Existing authenticated game transport delivers command bytes and an
   out-of-band authenticated client ID. Dispatch only on the exclusive
   world owner thread via handleAuthenticatedPacket(clientId,bytes).
5. Command frames contain sceneId, ECS actor packed ID, monotonic actor
   sequence, lift feature/floor and Call/Board/SelectFloor/Exit verb.
   Rejected frames do not advance lift controller state.
6. World owner advances ECS scene. Captured server state snapshots
   are published ONLY after successful platform collider AND actor
   position updates. Existing transport handles serialization/delivery.
7. Client consumes only authenticated server->client lift state snapshots;
   never route received bytes into direct ECS board/exit.

## Limits and verification status

The user explicitly deferred tests. No C++ build, execution, Studio/TS
typecheck, native GPU, multiplayer soak or real macOS ECS scene run was
performed for Stage 16. Source readback/connector commits are not proof of
runtime behavior.

Known integration boundaries: actor movement and actual boarding volume
are enforced by the owning ECS motor and BuildingLiftEcsBridge; existing
authority's AtLanding callback must be backed by collision-checked game
movement, not a client coordinate claim. Opening/closing physical landing
door poses are managed by BuildingLiftDoorOwner, which still needs a
real game scene and renderer to verify. Do not claim production-ready
network security until authenticated transport, entity lifecycle, replay
and snapshot integration tests run on server/client.

## Next agent step

Implement authoritative actor/landing command integration in one actual
game scene and synchronize the corresponding render-owned cabin/door
instances from committed snapshots. Verify sequence nonce lifetime,
reconnect/scene-id allocation, physical door obstruction and passenger
carry through real ECS commands. Then run focused C++/Metal/native Studio
and multiplayer tests when user allows; record exact outputs. Keep PR Draft
and do not merge main until evidence and branch reconciliation.
