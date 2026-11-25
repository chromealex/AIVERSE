# AIVERSE Stage 16 — lift door safety and network authority
Date: 2026-11-23
Repo: chromealex/AIVERSE
Branch: feature/aiv20261123_building-grammar
PR: https://github.com/chromealex/AIVERSE/pull/114 (Draft)

## Source implementation

- libraries/include/aiverselib/systems/BuildingCirculationRuntime.h:
  interruptClosing() returns an obstructed cabin to the Open phase.
  canApplyReplicatedSnapshot() preflights incoming revision, lift/floor
  identity, position, occupancy and landing-state physical plausibility.
- engine/buildings/BuildingCirculationHost.h: exposes authoritative
  interruptLiftDoorClosing() and read-only riderBindings() for server
  snapshot collection.
- engine/buildings/BuildingCirculationScene.h:
  setDoorObstructionProbe() takes an authoritative game world callback
  (lift feature, floor). When physical doors are attached but the probe
  is absent, the lift owner refuses simulation ticks. An obstructed
  doorway interrupts Closing. The actual PHYSIS actor/object overlap
  sensor is supplied by the existing game/world owner.
- engine/buildings/BuildingLiftReplication.h:
  new compact, bounded little-endian server lift-state payload with
  magic/version, sceneId, serverTick, per-lift revision/floor/phase/mm
  position/passenger count, and rider actor IDs. Strict size, duplicate,
  reserved-field and version validation; stale server ticks rejected.
  Client replication preflights all lift states before any mutation.
  captureBuildingLiftServerFrame() copies authoritative host state.
- engine/buildings/BuildingLiftCommands.h:
  template BuildingLiftAuthority<Body> handles Call, Board, SelectFloor,
  Exit. Requires authenticated client ID FROM existing network server,
  an OwnsActor callback, existing ECS Entity/Body, scene identity,
  monotonic actor command sequence, and authoritative AtLanding callback.
  Boarding also checks physically open landing doors and actual cabin
  proximity via BuildingLiftEcsBridge; clients cannot send authoritative
  lift snapshots as gameplay commands.
- engine/buildings/BuildingRuntime.h includes these integrations.

## Ownership / limits

The engine's existing ECS owns actors, SpatialWorld owns colliders,
nav::Mesh owns navigation, Renderer3D owns model GPU lifetime and the
existing networking transport authenticates clients. No duplicate owner,
new socket service or new gameplay genre branch was created. This stage
is an owner-facing integration API, NOT proof that every FPS/RTS server
already dispatches lift actions or replicates this payload. Games must
explicitly register the snapshot in their authenticated server->client
messages and wire the command handler through existing permissions.

Door obstruction probe must inspect authoritative live ECS/physics objects;
client-provided callback results are forbidden. The live door model /
animation and network transport binding remain next tasks.

## Real verification

User explicitly requested 'тесты потом'. No C++ compile, CTest, native
Studio, Metal capture, multiplayer, ECS motor or transport test executed
for Stage 16; only GitHub commits were made. Do not claim production
readiness or successful network/door animation. Prior legacy 20-building
QA was 19/20 and an unrelated factory 16x9x11 size-fit failure remains.

## Next agent

1. Wire BuildingLiftAuthority to a concrete authoritative game network
   dispatcher, using the authenticated transport's client identity.
2. Wire AtLanding and setDoorObstructionProbe to existing world PHYSIS
   body/collision queries and movement zones; ensure callbacks cannot
   outlive ECS owner or access mutable world from render thread.
3. Bind BuildingLiftStateFrame to existing server snapshot channel;
   apply client visual interpolation without making client authoritative.
4. Implement GPU-transformed cabin + landing door models and door/interlock
   animation through the existing animation/Renderer3D lifecycle.
5. When user authorizes QA, compile with Clang/GCC, run native NavMesh/ECS
   route/lift/replication acceptance, Studio checks and real Mac rendering.
6. Update handoff with actually verified results, current git SHA and
   outstanding failures; do not merge Draft PR into main beforehand.

Source commits include 4c88c13b (wire), 81fd11c (host capture),
b56e3aee (door interrupt), 79296921 (scene sensor),
29d73513 (snapshot preflight), 495878fd (atomic client preflight),
64faa0dc (server command adapter), 0eae057c (umbrella header).
