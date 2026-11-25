# AIVERSE Building Stage 18 — ECS rider and physical door integration
Date: 2026-11-23
Branch: feature/aiv20261123_building-grammar
PR: https://github.com/chromealex/AIVERSE/pull/114 (Draft)

## Actual changes (this stage)
- engine/buildings/BuildingLiftEcsBridge.h: BuildingLiftPassenger component
  on EXISTING ECS Entity; template over an existing Body with Fixed Vec3 pos.
  Board checks the actor feet really stand within the cabin and the cabin
  is at an open landing. Exit now checks feet physically reached the
  outside landing volume on the actual target floor; no teleport/exit from
  an enclosed cabin. Generational Entity::packed IDs and per-actor revision
  prevent stale/double transfer. No new actor entity registry.
- engine/buildings/BuildingCirculationEcsScene.h: world-owner facade binding
  ECS World, BuildingCirculationScene, SpatialWorld and nav::Mesh. It owns
  a live ECS Body obstruction-probe callback for landing-door closure;
  non-copyable/non-movable while the probe captures this. Boarding is
  gated by physical door-open state. Only successfully published platform
  collider movements are applied to rider ECS positions; errors mark the
  scene/navmesh faulted. Authoritative unload clears this scene's passenger
  attachment markers without deleting foreign ECS entities.
- engine/buildings/BuildingLiftDoorOwner.h: physical paired landing-door
  colliders in the existing SpatialWorld. Only the reached/open landing
  removes closed leaves. Dirty native navmesh when door state changes.
- engine/buildings/BuildingCirculationScene.h: synchronized door collider
  publication and fail-closed world/physics update path, including fault()
  called when ECS publication fails.
- engine/buildings/BuildingLiftGameSession.h: alternative session facade
  requires an explicit live authoritative obstruction sensor when attaching
  doors (old sensorless overload fails EARLY); Exit now uses physical
  outside landing check.
- engine/buildings/BuildingLiftCommands.h: authenticated command Exit now
  calls the new spatially validated ECS disembark API.
- engine/buildings/BuildingLiftReplication.h: reuse the existing parallel
  agent's bounded, versioned server lift/rider snapshot codec, not a second
  wire protocol. BuildingCirculationEcsScene::captureServerSnapshot()
  refuses to serialize until physics AND rider ECS movements have committed.
- engine/buildings/BuildingRuntime.h: game native include facade exports
  both generic ECS alternatives, door owner and network codec.

## Ownership and API
Use ONE building scene facade: BuildingCirculationEcsScene<Body> OR the
alternative BuildingLiftGameSession<Body>; never both for one scene.
ECS World remains actor authority. SpatialWorld remains the sole collider
owner. nav::Mesh remains the path owner and needs its own Source refresh
after any dirty collision. Renderer3D owns persistent dynamic lift cabin
and door render models. Existing authenticated transport carries the wire
snapshot; this code neither opens sockets nor trusts client-authored state.
The ECS adapter currently checks live actors of ONE Body type. Projects
with vehicles, props and different Body pools must supply an additional
authoritative PHYSIS obstruction probe, not invent a client-side safety
sensor. The game actor motor retains responsibility for real walking,
stairs and safe landing approach; a room graph edge is never a teleport.

## Commits (selected)
dca584f0: initial generic ECS passenger adapter.
31a7cf58: live ECS door obstruction probe and unload marker cleanup.
083f693d: physical destination landing check on exit.
e248779a: scene passes building transform into exit verification.
9bd0c20d: server snapshot only after owner phase commit.
3c53ca1b: game session requires door safety sensor.
5c8c9e2e and 870035e3: adapt game session and authenticated command Exit.
d3bb9118: BuildingRuntime.h facade exports ECS/doors/network.

## Tests / limitations
The user requested "тесты потом". Therefore NO C++ compile, CTest,
HTML5 Studio build, native Mac/Metal render, end-to-end ECS motor or
network roundtrip was performed for this stage. GitHub source commits
are not a verified game build. A parallel agent also updated lift game
session, wire codec and render catalog; reconcile in one world-owner path.
Prior unrelated Mac legacy procedural building batch: 19 PASS / 1 FAIL,
factory 16x9x11 seed3. No claim that solver bug is fixed.

## Concrete next steps
1. When user authorizes tests, compile this integration against ECS,
   SpatialWorld, NavMesh, Renderer3D and real game startup.
2. Wire physical actor collision/motor and door render instances to
   authoritative owner update and verify no ghost actor carry.
3. Extend obstruction to cargo, vehicles, other Body pools and actor
   destruction/disconnect handling.
4. Register server snapshot and authenticated client commands in an
   actual game transport; test stale and reordered packets.
5. Validate GPU lift/doors on Metal, ensure physical/render positions
   agree, and maintain separate agent handoff each stage.
Do not merge Draft PR into main until focused QA and branch reconciliation.
