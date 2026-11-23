---
name: aiverse-building-style
description: Generate AIVERSE buildings in a setting from a style file - whole armoured RTS-style structures (sci-fi barracks, command centre, factory, depot) or dressed enterable building-gen houses (modern city); add or tune a style, export to .glb and check in the model viewer.
---

# building-style

Use when generated buildings must look like a setting rather than grey boxes, or when adding a new setting.
The setting is data: `engine/assets/building-styles/<id>.json`. The code (`BuildingStyle.h`) knows only
generic elements, so a new style never needs new C++ unless it needs a new kind of element.

Two generators share one style file: `dressStructure` (`BuildingStructure.h`) - one armoured hull per building,
the sci-fi look (kinds in the style's `structures` block) - and `dressBuilding` (`BuildingStyle.h`) - facades on
enterable building-gen houses. For an RTS / StarCraft-like base use structures; floors with windows always read
as city buildings.

0. Structure: `dressStructure(style, "barracks", seed, origin, yaw)` → `mesh`, `boxes` (collision), `gates`
   (ground points in front of the gates). Export: `aiverse_building_export --style <style> --out b.glb --structure barracks --seed 3 --ground`.
   New kind = new entry in `structures` (size, height, taper, chamfer, bands, modules, tower, door, legs, thrusters, props).
1. Read `libraries/include/aiverselib/systems/BuildingStyle.h` (namespace `aiverse::lib::systems`) and the
   reference style `engine/assets/building-styles/scifi.json`.
2. Load: `readBuildingStyle(json, "path")` - errors name the path (`style.panels.material: unknown material 'x'`).
3. Definition: build the `BuildingDef` as for building-gen (agent radius / height / step from the owner's
   motor), then `applyBuildingStyle(def, style)` writes the style's proportions (window / door size,
   floor height) when they still fit; otherwise the definition stays unchanged.
4. Mesh: `dressBuilding(def, style, seed, origin, yaw)` with the same seed / origin / yaw as
   `generateBuilding` - the mesh covers exactly that collision. It returns vertices (position, normal, UV in
   metres), one index list per used material and the materials after the seeded palette. Empty mesh = the
   building does not fit.
5. Look at it: `aiverse_building_export --style <style.json> --out street.glb --street 10 --seed 3 --ground`
   (or `--building def.json --seed 7` for one house), then `aiverse_modelview street.glb` (screenshots with
   `AIVERSE_SCREENSHOT`, see AGENTS.md section 4).
6. Check: `aiverse_library_building_style_tests <repo>` (CTest `library_building_style`): the style file loads,
   door corridor free, window holes hold only glass, determinism, seeds and palettes vary, yaw, budget,
   JSON errors. Add every new style file to the test's list of shipped styles.

Style file elements (all optional except `materials`, `shell`, `interior`, `roof`):
`materials` (linear colour, metallic, roughness, emissive + strength), `palettes` (seeded colour sets),
`proportions`, `panels` (cladding grid with accent chance and depth range), `base`, `band` (+ `light`),
`corners`, `window` (frame, glass, lit glass chance, sill `strip`), `door` (frame, `light`, canopy),
`parapet`, `crown` (slanted visor with mitred corners), `ribs` (fins between window columns), `sign`
(vertical light sign), `skirt` (sloped armour), `buttresses`, `pipes`, `hazard` stripes, chamfered `corners`,
window `hood`, `roofBlocks` (clad machine rooms), `props` (`box`, `mast`, `tank`, `stack`, `dish`, with `tip` lights).
Shipped: `scifi.json` (structures: barracks, command, factory, depot; plus a military facade set), `modern.json` (glass-and-panel city).

Adding a style: copy the closest shipped style, change materials, palettes, proportions and which elements exist
(a medieval style drops skirt / pipes / props and uses small windows and timber bands). Keep emissive
materials for real light sources. Check triangle count (the export prints it) against the target devices.

Not implied: kit models (GLB pieces) in styles, textures, non-rectangular footprints, a map block that builds
the Model at load (debt: hosts call the library; the actor map `buildings` block still draws its collision boxes),
LOD / merged far meshes, interiors furniture.

Build selection: `AIVERSE::lib_building_style` (depends on `building-gen`); native OBJECT modules call
`aiverse_native_use_libraries(target building-style)`.


Stage 2: `structures.<kind>.massing` accepts `bastion`, `spire`, `twin-bay`,
`terraced` (generic shape grammars). Pass `StructureSize{width,height,depth}` as
the sixth argument to `dressStructure` or export with `--structure factory --size 24 12 18`.
All three dimensions are required for a sized request; they include the complete visible
envelope, and generated colliders and gate points are transformed with the mesh.
Omit the size to retain legacy output. The editor should implement Setting → Style →
Function → W/H/D → Seed → Generate, saving a generated model as a project Asset;
that UI and asset integration have NOT been implemented in this stage.


Stage 3: `structures.<kind>.forms` adds data-authored major volumes:
`pod` (armoured wing), `silo` (cylindrical tank/reactor),
`pylon` (tapered tower) and `gantry` (raised open portal).
Schema per entry: `kind`, `material`, `at:[x,z]`, `size:[width,height,depth]`,
optional `level`, `taper`, `chance`, `repeat` and `step:[dx,dz]`.
All positions/dimensions are normalized to the building's dimensions.
Parser caps entries, repeats, positions, dimensions and probabilities. Both
mesh and coarse collision reflect the authored shapes. Reserve roof footprints
before roof prop placement; do not claim a gantry pierces the closed main hull.


Stage 4: `forms[].kind` additionally accepts `arch`, `dome` and
`swept-hull`. The arch has segmented curved beam collision with an open
middle; the other two forms are closed and receive conservative AABB collision.
All forms are derived from the same dimensions and style data. Explicit
`StructureSize` validates front gate visual clearance (minimum 1.6 m width,
2.1 m height after final fit); a failure returns an empty structure. This does
not make the whole sci-fi hull enterable or preserve every small detail thickness.
Use the `Building Style Focused` GitHub workflow for dedicated C++/GLB tests;
its status must be inspected before claiming a test pass.


Stage 5 constructive sizing: `dressStructure(..., StructureSize{W,H,D})` now
iteratively rebuilds the structure at candidate body sizes; it never applies a
nonuniform final scale to vertices. `StyledStructure::constructionSize` stores
the chosen raw body dimensions. Regenerate with `dressStructureGeometry`
(using that body size, the same style, kind, seed and yaw) to compare raw
vertex normals/UVs. Existing unsized calls remain single-pass and compatible.
Impossible envelopes fail instead of shrinking fixed-dimension gates below
1.6 × 2.1 m. Solver is capped at 32 attempts; the editor should distinguish
unsatisfied bounds from unknown function or malformed input in future stages.

Stage 6 Studio authoring: panel id `building_generator`, native methods
`studio.buildingCatalog`, `studio.buildingInspect` and `studio.buildingGenerate`.
Select setting/style/function/W/H/D/seed. The CPU evaluates three candidate
seeds; choosing Generate creates an Asset GLB under
`content/GeneratedBuildings/`, with GUID/.meta via existing StudioAssetCatalog.
Never write files from browser JS or fake a 3D preview. Native tests:
`tests/studio/BuildingGenerationTests.cpp` in Building Style Focused CI.


Stage 10 functional recipes: BuildingFeatureGenerator.h defines setting-
independent properties large-gate, small-gate, personnel-door and sliding-roof.
Style controls appearance, recipe controls actual geometry holes and collisions.
Use RuntimeBuildingGenerator::generateRecipe in packaged games; Studio
BuildingGeneratorPanel defaults to functional properties, native bridge saves
GLB + canonical .glb.recipe.json. CLI exports with --recipe JSON. Tests:
library_building_features, native BuildingGenerationTests, focused CI. Do not
claim verified Metal or playable hinge animation without actual testing.


Stage 11: use BuildingStructureGraph.h as source of truth for exact
functional opening layout; position/positionZ default -1 = automatic,
spacing min 0.35m. Graph solids drive BOTH CPU mesh and collision; graph
sockets have stable entrance/<feature>/<copy> and roof/<feature> IDs.
Studio writes positions to canonical recipe sidecar. Tests and Mac QA
must be run; no claim of verified GPU-actuated doors or multi-floor rooms.


Stage 12: floors, roomsX, roomsZ, window, sill and floor in BuildingStructureGraph.
The graph itself compiles standalone C++20 and emits rooms/connections. Window
panes are thin and collidable. No vertical navmesh, stairs or lifts. Handoff:
docs/agent-handoffs/building-floors-rooms-windows-2026-11-23.md.


Stage 13: stairs and elevator properties with centre X/Z + width/depth,
room-cell containment and real slab apertures in BuildingStructureGraph.
Vertical links are graph metadata, NOT automatically published navmesh.
Static lift rails and starting platform only; runtime motion later.
Handoff: docs/agent-handoffs/building-vertical-circulation-2026-11-23.md.


Stage 14 circulation: portable BuildingCirculationRuntime.h plans room
routes and owns revisioned integer-mm lift state; native game adapters are
engine/buildings/BuildingNavMeshBridge.h, BuildingCirculationHost.h,
BuildingLiftColliderOwner.h and BuildingLiftPlatformModel.h. Existing
nav::Mesh provides horizontal A*; vertical links are an off-mesh overlay
with genuine nav polygon anchors, not fake portals. Existing SpatialWorld
owns dynamic cabin colliders and dirty navmesh tile publication. Dynamic
platforms are omitted from STATIC runtime meshes. Tests explicitly deferred
by user; never claim Mac/Metal or gameplay path pass.


Stages 14-15: BuildingCirculationRuntime.h and BuildingRouteExecutor.h own
portable lift/room execution; BuildingNavMeshBridge.h uses EXISTING
engine/nav/NavMesh.h, cannot insert offmesh portals. BuildingCirculationScene.h
publishes only successful platform collider moves and corresponding rider
deltas through SpatialWorld. Game ECS must acknowledge actual actor movement,
boarding and exit; no teleport. Do not regenerate meshes each tick. No tests
run by user request. Handoff: docs/agent-handoffs/building-route-executor-2026-11-23.md.


Stage 17: BuildingLiftGameSession<Body> in BuildingRuntime.h is the
approved native ECS actor integration. Require a nonempty game actor
authorization callback and physical lift doors before boarding. Publish
network frames only from complete authoritative ticks; normal unload via
detachIfVacant and safe rider releaseAtLanding. Do not claim production
verified until compiler/physics/Metal/multiplayer tests are run. Handoff:
docs/agent-handoffs/building-authoritative-game-session-2026-11-23.md.


## Stage 16: lift network + door safety

The generated building's ECS passenger and world collision systems are
owned by existing AIVERSE runtime modules, not by this style generator.
engine/buildings/BuildingLiftReplication.h defines a bounded server-origin
lift snapshot codec, monotonically ordered by scene ID/server tick/lift
revision; BuildingLiftCommands.h validates client actions using existing
ECS actor ownership, authorized caller ID, nonces and actual landing zones.
Never accept client-authored server snapshots.

When physical landing doors are attached, the world owner MUST provide
BuildingCirculationScene::setDoorObstructionProbe using live PHYSIS/ECS
collision information. Without a sensor, advancing lift simulation
refuses to proceed; obstructed doors interrupt Closing. Only publish
ECS riders and dynamic cabin transforms after successful collider commit.
Dynamic model should be uploaded once, never regenerated every tick.

Handoff: docs/agent-handoffs/building-lift-safety-network-2026-11-23.md.
No C++/TS/GPU/network tests were run (explicitly deferred by user).


Stage 18 ECS physical integration: use BuildingCirculationEcsScene<Body> with the
existing ECS World/Entity and BuildingLiftPassenger marker, or the alternative
BuildingLiftGameSession<Body>, never both for the same lift. Boarding checks
physical feet inside the cabin, exiting checks the real target landing.
Door closure requires an authoritative obstruction probe; the generic ECS
scene supplies one for its Body pool. Add PHYSIS sensors for other object
kinds. Apply only publishedRiders after successful SpatialWorld platform
collider publication, then send the EXISTING BuildingLiftReplication.h server
frame. No tests run at user request; do not claim verified gameplay.
Handoff: docs/agent-handoffs/building-ecs-physical-doors-2026-11-23.md.


Stage 17: BuildingLiftPresentation.h owns render::Model CPU construction
of reusable cabin/door leaf meshes and authoritative transform commands.
Do not rebuild geometry per tick, upload on ECS thread, or merge dynamic
models with static scenery. After successful BuildingLiftGameSession
authoritative world/collider/ECS commit, use visualFrame(). Only current
landing doors animate. On fault visualFrame is invalid. Game renderer
owner applies GPU instance transforms and retires resources via existing
fences; visual animation never grants boarding permission.
Handoff: docs/agent-handoffs/building-lift-presentation-2026-11-23.md.
No new tests were run (user defers tests).


Stage 16: authenticating lift commands is server-owned. See
engine/buildings/BuildingLiftCommandWire.h for 44-byte bounded command
payload and BuildingCirculationEcsScene::configureAuthenticatedCommands,
handleAuthenticatedPacket. Caller MUST pass authenticated client identity
from existing transport, verify ECS actor owner and physical landing.
Never let browser/client packets mutate lift snapshots or bypass physical
doors. No tests run by user request; inspect handoff under
docs/agent-handoffs/building-authenticated-commands-2026-11-23.md.


## Stage 19 — ECS actor retirement and scene streaming guard (2026-11-23)

For the existing `BuildingCirculationEcsScene<Body>`, call
`prepareActorRetirement(actor, &error)` on the authoritative ECS owner
**before** removing an actor or reusing its entity generation. If the actor
rides a moving cabin or the doors are not physically open at the real
landing, this returns false: defer actual actor destruction until the lift
arrives and releases the passenger. On a valid landing it calls the
existing `BuildingLiftEcsBridge::releaseAtLanding` and clears the
authenticated command nonce. Never decrement rider counts by deleting ECS
markers or inventing a new physics owner.

For ordinary building streaming, use `detachIfVacant(spatial, navmesh,
&error)`. `canUnload()` checks the authoritative host rider bindings;
ordinary unload rejects occupied/faulted scenes. Existing `detach()`
is forced teardown only for world destruction and requires external
passenger reconciliation. An actor-pending-destruction queue is the game's
responsibility; no automatic despawn or background ticking is added.

This is a source-only stage: tests remain deferred on user instruction.
Handoff: `docs/agent-handoffs/building-ecs-actor-lifecycle-stage19-2026-11-23.md`.


## Stage 20 — deterministic ECS retirement fixed-tick queue

Use `engine/buildings/BuildingActorLifecycleTick.h`, available through
`BuildingRuntime.h`. Own one `BuildingActorLifecycleTickOwner<Body>`
per existing `BuildingCirculationEcsScene<Body>`. On authenticated
disconnect, owner-authorized despawn or scene transfer, revoke commands
in the existing game transport/session and enqueue
`requestRetirement(ecs, scene, actor, reason)` BEFORE any ECS destroy.

In the exclusive authoritative owner tick call
`advanceFixedTick(ecs,scene,spatial,navmesh,dtMs)`. The adapter invokes
the real circulation scene's collision-and-passenger publish first;
only after a successful publish does it attempt pending retirements.
Actors in transit remain alive and are retried after an actual open
landing. The successful path calls `prepareActorRetirement`, then
`ecs.destroyLater(actor)`. Game ECS still owns the structural-change
barrier `ecs.flush()`; invoke it only when all ECS iterations have ended.
Do not directly call `ecs.destroy` on an attached lift passenger,
and do not create a second physics, entity, or network registry.

If using manually integrated `scene.advance`, call
`afterCommittedTick(ecs, scene)` only when its `Tick.complete` is true.
Never run both entrypoints in one simulation tick. Drain and reset the
owner only during explicitly reconciled total world teardown.
`BuildingLiftGameSession` is an alternative owner, not to be composed
with `BuildingCirculationEcsScene` for the same lift.

Tests remain DEFERRED per user instruction. This is not a verified
running game scene. Handoff:
`docs/agent-handoffs/building-ecs-fixed-tick-stage20-2026-11-23.md`.


## Stage 21 — physical stair traversal with existing ECS motor ownership

`engine/buildings/BuildingStairMotor.h` supplies a genre-neutral
`BuildingStairMotor<Body>`, exported by `BuildingRuntime.h`.
Use `BuildingCirculationHost::route(mesh, fromRoom, toRoom)` to obtain
`BuildingNavRoute` with resolved actual NavMesh endpoints and a stair
corridor generated from physical StairTread/StairLanding solids.
Use `BuildingRouteExecutor` for route phases. For a
`BuildingNavRouteStep` with `ClimbStairs`, initialize the stair motor
with `begin(entity, step, mesh, sweepCallback)`. The callback MUST invoke
the existing PHYSIS/game actor motor to sweep the actor's real complete
collision shape, check obstacle clearance and grounded support, and
authorize the waypoint; false means no movement. Do not return true
unconditionally in production. Run `advance(ecs,mesh)` in the exclusive
authoritative world-owner phase; do not call on a rendering thread.
Only when the final waypoint is reached may
`acknowledgeLanding(routeExecutor)` advance its stair route action.

If NavMesh is dirty, its revision changes, the ECS Body disappears, or
collision blocks a waypoint, stop movement and replan/reconcile through
the owning game systems. No direct PHYSIS implementation or generalized
character step motor is fabricated in this module; it is an adapter over
the game's trusted sweep callback. It does not create navmesh polygons,
insert off-mesh links, or teleport through stairs.

Tests remain deferred on user request. Stage 21 is source-only, without
a compiled native game/PHYSIS integration pass.
Handoff: `docs/agent-handoffs/building-stair-motor-stage21-2026-11-23.md`.


## Stage 22 — building solids to existing native NavMesh

Use `engine/buildings/BuildingNavSource.h` (exported from
`BuildingRuntime.h`) to construct a `nav::Source` from an existing
`BuildingStructureGraph` and its transformed building width/depth,
origin and quadrant yaw. This is not an alternate navmesh engine. Call
`source.initialize(graph, width, depth, origin, yaw, options, &error)`,
then let the existing navigation owner call
`nav::Mesh::build(source, source.config(), &error)`, or refresh dirty
tiles against the same long-lived source. A successful mesh build can
be published to `BuildingCirculationScene::rebuildNavigation(mesh)` to
resolve actual room polygons and vertical landing endpoints.

`BuildingNavSource` rasterizes static `Floor` solids into stacked
walkable span columns and tests standing headroom against non-floor
solids at each sample. This respects inter-storey slab holes cut by the
generator, structural partitions, door leaf solids and roof headroom.
Only adjacent columns with supported compatible heights and no wall at
their shared midpoint are linked. Native `nav::Mesh` continues to
own rectangles, portals, A*, dirty tiles and revision tracking.
Vertical stairs/lifts intentionally remain explicit off-mesh actions
via `BuildingNavMeshBridge`, `BuildingStairMotor` and the lift runtime.
Do not claim the mesh alone provides stair climbs.

The sampling uses a column centre and a link midpoint, not full actor
capsule erosion or collision-world baking. Generated `Floor` spans do
not cover floating objects, dynamic cabin platforms, arbitrary terrain,
or external colliders. The game owner must compose other world geometry
with this source using its existing nav ownership and invalidate tiles
when doors or world obstacles move. Use a suitably fine cell size and
recheck with the real PHYSIS motor sweep before moving an actor.
Never use this as the sole runtime collision gate.

Tests are deferred. Stage 22 is source-only and not validated by
compiler, native NavMesh tests, Mac/Metal/GL33 or in-game navigation.
Handoff:
`docs/agent-handoffs/building-native-nav-source-stage22-2026-11-23.md`.


## Stage 23A — all relevant building-dependent graphs, not only NavMesh

Use `engine/buildings/BuildingGraphChanges.h` (exported via
`BuildingRuntime.h`) when interactive construction changes after an
authoritative world/collider commit. `BuildingGraphChangeRouter` dispatches
revisioned notifications across eight orthogonal domains:
Navigation (native nav::Mesh), Spatial (building room/region connectivity),
Hierarchy (MapGrid A*, HPA* and flow fields), Transport (RoadGraph/rail),
Gameplay (ZoneGraph/quest/ability logic), Presentation (shader/material/VFX
or scene dependencies), Audio (acoustics) and Simulation (power, utilities,
damage/environment networks). These are **independent consumers**, not a
claim that their graph formats are interchangeable or automatically rebuilt.

Register each relevant owner with `registerConsumer(domain, callback)`.
A callback MUST invalidate/recompute its real graph or enqueue an
authoritative rebuild and return true only once the change is accepted;
a failed/unregistered domain remains pending. For nav::Mesh use
`buildingNavInvalidator(mesh)`, which calls the already existing
`markBuildingNavCollisionChanged`. For all other domains, bind callbacks
to the existing owner APIs; never instantiate duplicate pathfinding,
network or renderer graphs. Call `publish(change)` with a monotonic
scene revision and a conservative world-space bounds **after** the
physical aperture state is successfully committed. Retry the same change
while `pendingDomains(sceneId,revision)` is nonzero; never publish a newer
revision before earlier domains are acknowledged. Choose a precise mask
for the feature: DO NOT set `kBuildingGraphAll` unless every domain has a
registered owner, or the event will deliberately remain pending.

A change notification is not itself a physical door/gate/roof controller.
Authoritative PHYSIS/SpatialWorld owns blocking geometry and collision,
`BuildingStructureGraph` owns its physical opening topology,
`BuildingCirculationScene` owns lift doors; presentation graphs remain
read-only visual consumers. No client may use an event to bypass a door
interlock. An absent graph subsystem does not need to be notified.
Any changes to ZoneGraph connectivity or Roads require the appropriate
existing game system to validate its semantics before commit.

Tests are deferred per user instruction; this source-level graph router is
not evidence of verified all-graph live integration.
Handoff:
`docs/agent-handoffs/building-multigraph-stage23a-2026-11-23.md`.


## Stage 23B — authoritative interactive doors, gates and sliding roofs

Use `engine/buildings/BuildingApertureOwner.h` via
`BuildingRuntime.h` for runtime binary access of `PersonnelDoor`,
`SmallGate`, `LargeGate` and `SlidingRoof`. The runtime
`RuntimeBuildingGenerator::generateRecipe` now omits movable
`DoorLeaf` and `RoofPanel` from STATIC mesh/collision, while Studio
GLB preview still bakes the initial visual leaves. Game renderer must
construct its own reusable moving door/roof instances; no GPU updates
occur in the simulation.

In the authoritative world-owner phase, call
`owner.attach(generated,spatial,sceneId,origin,yaw,colliderIds,
obstructionProbe)` with unique game-allocated IDs per operable socket.
The obstruction probe MUST check live characters, cargo and vehicles,
not only `SpatialWorld::blocked` against the door itself. Static
geometry and doors must never share collider IDs. Full-width
conservative closed barriers are inserted as dynamic SpatialWorld
colliders. For authorized opening/closing call
`setOpen(socketId,open,spatial,router,domainMask)`. Closing is rejected
if obstructed. The authoritative barrier is added/removed first;
only then is `BuildingGraphChange` published with a revision and
world-space bounds. Binary physically open/closed state alone controls
game access; visual intermediate animation grants no permission.
If a graph consumer refuses an event, physical state remains committed,
subsequent changes are blocked, and the exact event must be retried
via `retryGraphPublication(router)`.

Register only real graph owners from `BuildingGraphChanges.h`; native
NavMesh uses `buildingNavInvalidator`. For every other affected domain
(HPA*/flow, room/zone, transport, render/VFX, audio, simulation),
register a consumer adapter implemented by its existing owner.
Unregistered masks remain pending; they are not treated as success.
A nav::Mesh markDirty is ONLY an invalidation: callers must regenerate
its Source using updated dynamic collision/door state and call refresh
before pathfinding. The current `BuildingNavSource` uses generated
static graph solids; it is NOT yet a collision-world composite source
that dynamically reflects these doors. Never claim closed doors are
NavMesh-blocked solely from this notification.

`BuildingApertureOwner` currently supports instantaneous binary
physical commits and conservative full-width slabs, not partial
collision hinges, swept animations, custom leaf render meshes,
PHYSIS actor impulses, network authorization transports or automatic
multi-graph adapter registration. Avoid duplicate lift door ownership:
`BuildingLiftDoorOwner` remains authoritative for elevators.

Stage 23B source committed without build/tests per user preference.
Handoff: `docs/agent-handoffs/building-aperture-owner-stage23b-2026-11-23.md`.


## Stage 23C — dynamic collision-backed navigation overlay

Use `engine/buildings/BuildingDynamicNavSource.h` through
`BuildingRuntime.h` to combine static `BuildingNavSource` floors
with authoritative `BuildingApertureOwner` closed door/gate/roof
colliders in the existing `SpatialWorld`. Do not bake movable
`DoorLeaf`/`RoofPanel` graph solids into static NavMesh blocking;
`BuildingNavSource::blocked` excludes them, while the overlay reads
their real committed collision presence.

Lifecycle: create a long-lived static `BuildingNavSource`, then a
`BuildingDynamicNavSource dynamic(staticSource)`; attach the aperture
owner to existing SpatialWorld and call
`dynamic.synchronize(apertures,spatial,&error)`. Build
`nav::Mesh` using `dynamic` and `dynamic.config()`. Both source
objects must remain alive while the mesh refreshes. Register
`buildingDynamicNavInvalidator(dynamic,apertures,spatial,mesh)`
for `BuildingGraphDomain::Navigation` in the existing
`BuildingGraphChangeRouter` BEFORE opening or closing an aperture.
The callback re-snapshots owned closed colliders, validates their
exact SpatialWorld AABB and marks only affected nav tiles dirty.
The native navigation owner must then call `mesh.refresh(dynamic)`
on its simulation/graph update phase before allowing path queries
and re-resolve the building's room/off-mesh landing links.

The source filters standing surfaces under closed apertures, checks
dynamic obstruction at horizontal crossings and correctly translates
filtered multi-storey span IDs back to static source IDs. An open
aperture has no blocking dynamic collider. No fake portal is added
across a shaft or a disconnected outside floor. The primitive is
specific to generated aperture colliders; compose it with other
collision/terrain owners at the game's existing nav source level.

Every other applicable graph domain (Spatial, Hierarchy, Transport,
Gameplay, Presentation, Audio, Simulation) must register its own
`BuildingGraphChangeRouter` consumer, with its actual subsystem's
dirty/rebuild function. The presence of a router does not prove these
other owners have been integrated. Use a precise per-event mask:
unregistered domains deliberately hold the event pending.
A callback ACK means the owner has safely *accepted* the invalidation,
not that its expensive graph rebuild is already completed.

A dirty NavMesh is intentionally not queryable; do not claim traversable
doors until mesh.refresh succeeds and current actor collision checks
allow traversal. Source is conservative and cell-sampled; real PHYSIS
movement remains authoritative for body volume, and other dynamic
obstacles, terrain and actual renderer animation are not included.

Tests deferred by user request. Handoff:
`docs/agent-handoffs/building-dynamic-multigraph-stage23c-2026-11-23.md`.


## Stage 23D — concrete MapGrid path-cache and room connectivity adapters

`engine/buildings/BuildingGraphOwnerAdapters.h`, included by
`BuildingRuntime.h`, provides two concrete adapters for existing
AIVERSE graph owners. Do not duplicate A*, HPA*, flow field, or room
topology algorithms.

For flat game maps, register the existing `game::MapGrid` owner
under `BuildingGraphDomain::Hierarchy` using
`BuildingGridGraphAdapter::consumer(grid, committedProjection)`.
The callback's `committedProjection(change)` must only return true
AFTER the game's original `MapGrid.flags/blocked` data has been
authoritatively projected from the building change. It is NEVER
sufficient to call `cellChanged` while leaving source obstacles
unchanged. This adapter invokes the real `MapGrid::cellChanged`
over affected cells, which bumps flow stamps and invalidates local
HPA*/NavMesh/clearance. For changes exceeding the bounded local budget
it uses the existing `invalidatePathing()` plus `flowDropAll()`.
Reject sphere maps unless their separate topology-aware owner
adapter is provided. The normal grid remains 2D; it cannot encode
stacked building floors and must not be used for upper-storey routing.

For room connectivity, initialize `BuildingRoomGraphCache` from
the original `BuildingStructureGraph` and provide a live
`CanTraverse(const BuildingGraphConnection&)` callback rooted in
actual committed door, stair, lift and gameplay access policy.
Register `roomCache.consumer()` under
`BuildingGraphDomain::Spatial`. Each accepted event reevaluates
existing connection accessibility in deterministic graph order,
atomically replaces cached undirected adjacency and makes
`reachable(fromRoom,toRoom)` available to gameplay code.
This does not modify room-graph topology or grant external doors
connections they never had. For one-way permissions, use the
game's own directed logic graph instead of this cache.

The stage complements the already implemented Navigation-domain
`BuildingDynamicNavSource`. Graph change masks MUST contain only
actual registered/impacted domains. Acknowledged invalidation does
not imply every derived path is already computed: expensive graph
searches are performed by their original owners when queried.
No direct callbacks for RoadGraph, ZoneGraph, shader/VFX, audio,
utilities or other optional graph owners have been wired yet.

Tests were deferred per user instruction; no native compilation or
end-to-end graph update was verified. Handoff:
`docs/agent-handoffs/building-graph-owners-stage23d-2026-11-23.md`.


## Stage 23E — RoadGraph, directed ZoneGraph and optional rail owner

Use `engine/buildings/BuildingTransportZoneGraphAdapters.h` from the
`BuildingRuntime.h` umbrella for existing road and game-zone systems.

`BuildingRoadGraphView::initialize(const game::RoadGraph&,CanTraverse)`
produces a filtered ROAD GRAPH of existing ECS road nodes/adjacency,
with the game-provided `CanTraverse(uint16_t from,uint16_t to)`
grounded in current authoritative world/collider/permissions.
The source RoadGraph remains owned by the game, and the existing
RoadGraph::path(World&,from,to) still solves routes on the filtered
copy. Register `roadView.consumer()` under
`BuildingGraphDomain::Transport` when roads depend on building
gate access. Never convert world-space building sockets into arbitrary
new road nodes. The view preserves original node indices and is
recomputed from the actual source on committed graph events.

`BuildingZoneGraphView` wraps the existing
`aiverselib/strategy/ZoneGraph.h`. The game supplies a live
`CanTraverse(const ZoneDoor&)` predicate; derived data may REMOVE
directed door edges blocked by physics, but NEVER adds permissions,
abilities, checkpoints or fake reverse links. Register its consumer
under `BuildingGraphDomain::Gameplay`. Existing ZoneState remains
the game-owned mutable actor state; routing/checkpoints and
`moveZone` still require actual authoritative contact and sequence
checks. The derived view is not itself a live ZoneState migration or
proof of absence of softlocks.

The rail mechanics `libraries/include/aiverselib/mechanics/Railways.h`
was identified in the default branch search, but was NOT fetchable
from the current building feature branch; do not add a broken mandatory
include. Pass the existing rail owner's callback to
`buildingRailOwnerInvalidator`, checking active track blocks and
train reservations before acknowledging. To register both road and rail
under the single Transport domain, use
`buildingAllGraphOwners({roadView.consumer(), railCallback})`.
All callbacks must be retry-safe; no new rail occupancy/physics
implementation is created. Avoid claiming a full integrated railway
update merely because the optional callback interface exists.

Changes flow from physically committed gate/roof events, through
`BuildingGraphChangeRouter`, to these derived graph owners.
The caller registers only the domains it genuinely uses. Source
snapshot changes require the game's exclusive owner phase; the
views reference source graphs, which must outlive the adapters.
No tests were run per user instruction; this code is NOT verified
as compiling or running in a live game.
Handoff: `docs/agent-handoffs/building-transport-zone-stage23e-2026-11-23.md`.


## Stage 23F — authoritative fixed-tick interactive building commands

`engine/buildings/BuildingApertureFixedTick.h` defines
`BuildingApertureFixedTickOwner` and is exported via `BuildingRuntime.h`.
One instance belongs to the existing authoritative game/world-owner scene.
Call `attach(sceneId, authorize, reachable)` with mandatory callbacks:
`authorize(actor,socketId,open)` checks current server-side actor access,
and `reachable(actor,worldAABB)` checks actual game actor contact/range
against a physically committed interaction volume. Both are invoked in
the fixed tick, not trusted from client-provided coordinates. Authenticate
packets in the EXISTING transport, map them to the existing ECS Entity
with a generation, and `enqueue(ecs, BuildingApertureCommand)`.
The command contains scene, actor, monotonically increasing actor nonce,
socket ID, binary target and the exact affected graph-domain mask.

Call `advance(ecs,apertures,spatial,router,serverTick)` on the exclusive
server simulation owner phase after movement/obstruction information is
current. It retries a pending committed graph event before new commands.
Up to 64 queued commands per tick are processed in arrival order. Each
is revalidated for ECS liveness/nonce, current permission and actual
interaction position. Existing `BuildingApertureOwner::setOpen` commits
real SpatialWorld collision first; its graph notification is processed
before the frame exposes `committedPoses` as authoritative read-only
data for the game's existing renderer/network snapshot owner. A graph
failure keeps physical state committed and stalls new aperture changes
until graph consumers acknowledge; **do not replay the original action**
because its nonce has already been accepted.

The renderer owns movable meshes, animation and GPU upload; consume
`frame.committedPoses` after `frame.complete`. This adapter does not
create shader/VFX graphs or animate leaf geometry. The game must still
wire its existing network authenticated dispatch, entity retirement
(`retireActor` before ECS entity index reuse), nav::Mesh refresh,
and any separate authoritative snapshot serialization. Do not claim
this is already in an example game main loop.

Do not use `detach()` until game scene teardown has reconciled
pending graphics/graph/actor owners. Revision/nonce state resets only
at a new unique scene lifetime. An ACK from graph consumers means they
accepted an invalidation, not that all graphics/navigation caches are
already refreshed. User defers tests: no compile, native runtime,
Metal/GL or multiplayer verification performed.
Handoff: `docs/agent-handoffs/building-aperture-fixed-tick-stage23f-2026-11-23.md`.


## Stage 23G — movable aperture CPU models and committed visual handoff

`engine/buildings/BuildingAperturePresentation.h` is exported through
`BuildingRuntime.h`. One `BuildingAperturePresentation` instance belongs
to the existing Renderer3D presentation owner of each active building.
Create it once with `initialize(apertures.poses(),style,&error)`.
It creates one reusable CPU `render::Model` per independently
controlled door, gate or sliding-roof socket and reuses the same half-
panel geometry for both visual leaves. No static-mesh regeneration
or per-frame GPU uploads are permitted. Existing renderer owner uploads
`model(socketId)` once, leases the two instance transforms, and
retires resources through its normal GPU fences at scene teardown.

The existing network/ECS authority must first enqueue commands into
`BuildingApertureFixedTickOwner` only after authenticated connection
ownership is established. In the world-owner fixed update, call
`advance(ecs,apertures,spatial,graphRouter,serverTick)` after actor
movement, collision and obstruction sensors are current. Only if
the resulting `BuildingApertureFixedTickFrame.complete` is true should
the game transfer it to presentation using
`presentation.accept(frame,&error)`. On the separate render-owner
phase, `presentation.interpolate(renderDtMs,animationMs)` produces
two world-space instance positions per socket and an informational
visual fraction, which the existing Model/Renderer3D owner submits.
Visual fraction NEVER controls collision, navigation permissions or
network authority. No sprite, shader graph or second GPU cache is made.

The model is deliberately a simple static cuboid leaf based on the
authoritative closed world AABB, split along its widest horizontal
axis. It is not a final hinge model or physically swept animation;
door, roof and gate functionality is binary at the authoritative
SpatialWorld boundary. The renderer must manage leases, model upload,
render-node IDs and resource retirement. This stage does not yet
insert the bridge into a specific example game application's scheduler,
and it does not replace its transport protocol. For production-ready
swept motion/interlocks, an actual PHYSIS moving-leaf owner and
gameplay integration are still required.

Tests stay deferred at user request. No Clang/CMake, actual renderer,
in-game gameplay or GPU capture has been executed for this stage.
Handoff:
`docs/agent-handoffs/building-aperture-presentation-stage23g-2026-11-23.md`.


## Stage 23H — concrete native command/tick/render owner bridge

`engine/buildings/BuildingApertureGameBridge.h`, exported via
`BuildingRuntime.h`, composes the existing
`BuildingApertureFixedTickOwner` and
`BuildingAperturePresentation` into one scene-facing API.
Game owner first creates the generated runtime building and attaches
`BuildingApertureOwner` with actual SpatialWorld collider IDs,
a live obstruction sensor and registered graph consumers. Then call
`bridge.attach(sceneId, authorize, reachable, apertures, style)`.
Use the game's existing authenticated command transport to map
requester identity onto an ECS Entity and call
`bridge.enqueue(ecs,command)`. No network packet is trusted without
the real owner policy.

During the *existing* authoritative fixed-tick phase call
`bridge.fixedTick(ecs,apertures,spatial,router,serverTick)` only
after actor movement and obstruction information have been refreshed.
It commits real collision, publishes graph events and only forwards
completed frames to the CPU presentation owner. Rendering happens
separately: the existing Renderer3D owner calls
`bridge.render(dtMs,submitLeaf)`; the supplied callback receives a
const `render::Model&` plus one `BuildingApertureLeafPose`, maps
these into its usual `ModelDraw` submission/instance-transforms and
owns all GPU upload, graphics lifetime and retirement fences.
Keep CPU Models alive through graphics retirement; release GPU leases
before `bridge.detach()`. Call `bridge.retireActor(entity)` from
the game's existing authoritative actor lifecycle handler before ECS
generation recycling.

The bridge DOES NOT register itself into an application main loop
or renderer, create a new scheduler, write a transport or upload GPU
models. The source-level callback is an explicit integration seam,
not proof that all game examples invoke it. Existing gate/roof
collision remains instantaneous and binary while visual leaf animation
is interpolated, so this is NOT a physically swept moving-leaf
implementation. The next stage must add substep collider movement,
swept actor/vehicle obstruction, rollback and synchronized nav
invalidation before calling the physical animation complete.
No tests were run per user instruction. Handoff:
`docs/agent-handoffs/building-aperture-game-bridge-stage23h-2026-11-23.md`.
