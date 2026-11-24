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


## Stage 23I — opt-in stepped collider motion and PBR texture resources

`BuildingApertureOwner::beginMotion(socket, targetOpen, domains)` and
`advanceMotion(spatial, router, stepPermille)` add opt-in incremental
physical translation of one conservative full-width sliding collider.
The game-provided obstruction probe MUST inspect the complete swept
volume for characters, vehicles and cargo; graph events are published
after every successfully committed SpatialWorld step. Failed collider
reinstallation attempts to restore the old collider, and failures
leave the current step uncommitted. If rollback also fails, the
aperture owner faults; the game must reconcile before resuming.

Choose the stepped path through
`BuildingApertureGameBridge::setSteppedMotion(true, 100)` (or through
`BuildingApertureFixedTickOwner`) before processing commands.
Pending graph acknowledgements block subsequent movement. A closing
motion from an initially fully open door creates its first moving
collider safely. Ordinary `setOpen` cannot race active motion.
Gameplay passage permission remains false until the 1000/1000 open
endpoint. Physical collider motion is FIXED-point and lives on the
authoritative world-owner phase, not the render thread.

**PBR/textures:** building style `StyleMaterial` currently contains
colour/metallic/roughness/emissive parameters but has NO texture paths.
The existing Renderer3D `render::Material` supports baseTex, mrTex,
normalTex, occlusionTex, emissiveTex, shader graph textures and
`Model::images`. Before GPU upload, the existing asset/resource owner
may resolve actual model-local PBR image data and call
`bridge.bindPbrTextures(socketId, render::Material,
std::vector<render::Image>)`, which validates all image indices
and installs them into the reusable CPU leaf Model. UVs are generated
by the existing building Mesher; GPU images, texture upload, resource
leases and async loading remain Renderer3D/asset owner responsibilities.
This does NOT automatically generate or load texture files, nor imply
building Style JSON now contains texture references. A later step may
add explicit texture asset GUID slots with migration/validation.
Do not call texture binding after GPU upload unless the render owner
performs its proper material resource invalidation and reupload.

**Physical scope:** a SINGLE translating barrier per aperture is an
intermediate solution, not two independently colliding leaves or a
hinged-door/roof motor. The visual side still draws two halves and is
updated on fully acknowledged fixed-tick frames; further work must
align actual leaf poses and intermediate physics updates exactly.
NavMesh only sees updated closed colliders after callback sync and
mesh.refresh; motion callbacks must use the exact impacted graph mask.

Tests remain deferred at user request; no compiler/runtime/texture
GPU evidence. See:
`docs/agent-handoffs/building-stepped-physics-textures-stage23i-2026-11-23.md`.


## Stage 23J — independent sliding leaves + style PBR GUIDs

For a newly spawned procedural building, opt into
`engine/buildings/BuildingDualLeafOwner.h` INSTEAD OF the legacy
`BuildingApertureOwner` for the same operable sockets. Provide validated
`BuildingAperturePose` metadata from the generator/scene and TWO unique
game-owned collider IDs (`BuildingDualLeafIds`) per socket; do not
attach the legacy and dual owners simultaneously.
`BuildingDualLeafOwner::attach` refuses IDs already registered in
SpatialWorld and installs two independently registered closed AABBs,
with complete registration rollback on failure.

`begin(socketId,targetOpen,domainMask)` starts a single motion; call
`advance(spatial,router,stepPermille)` in the authoritative fixed tick.
Each step builds a separate swept volume for EACH leaf and requires a
game/PHYSIS obstruction probe checking both against actors, vehicles,
cargo and other dynamic blockers. It replaces both existing
SpatialWorld colliders as a paired publication; on failure it attempts
to restore BOTH old bounds and faults the scene if rollback fails.
A physically opened endpoint has no collider; closing recreates both.
Each committed step publishes one revisioned `BuildingGraphChange`
covering both swept regions. Retry pending graph ACK with `retry`
before moving another step. Use
`buildingDualLeafNavInvalidator(dynamicSource,dualOwner,spatial,mesh)`
for Navigation, which validates both real physical AABBs before
dirtying current native nav tiles. Do not enable gameplay access on
a partly open aperture: the game motor/interaction owner must enforce
the `pose.open` endpoint gate, not rely solely on the shrinking physical
gap. This is sliding X/Z geometry, not hinges or arbitrarily shaped
collision meshes.

After both collision publication and all relevant graph notifications
are accepted, the existing Renderer3D presentation owner can call
`BuildingAperturePresentation::acceptPhysicalStep(
dualOwner.poses(),tick,dualOwner.revision())`, then interpolate/submit
its two instance transforms. This avoids frame interpolation drifting
from the physically committed substep; it is not a GPU uploader.

**PBR material GUIDs:** architectural style JSON
`materials.<name>.textures` now accepts optional `baseColor`,
`metallicRoughness`, `normal`, `occlusion`, `emissive` keys.
Values MUST be 32 lowercase hex GUIDs from the existing project
`content/*.meta` asset catalog, not filenames. Old styles remain
valid without `textures`. The portable
`BuildingApertureTextureResolver::bind` resolves these channels to
model-local `render::Image` indices with deduplicated GUIDs. The
`BuildingApertureGameBridge::bindStyleTextureGuids` convenience
method reads validated slots from `StyleMaterial` and supplies them
to the caller's existing asset loader callback BEFORE GPU upload.
Studio's `apps/studio-native/StudioBuildingTextureResolver.h`
adapts `StudioAssetCatalog::byGuid` and checks importer=`texture`,
then delegates actual safe loading/decode to the existing importer
provided by the host. Do NOT add a second GUID DB or decode/render
textures on ECS threads. The callback alone does not prove real
rendered texture upload; it must be supplied by an existing renderer/
import pipeline.

**Stage 23J limitations:** the previous Stage 23F/23H bridge still
owns the LEGACY aperture owner; the dual owner is an alternative
source-level pathway, not yet registered in a real game scheduler.
Both physical leaves use conservative translated AABBs and the motor
must enforce a full-open interlock. Add real game-world registration,
full PHYSIS sweep checks, cross-backend texture/UV verification,
and C++/CTest/GPU QA before declaring these systems production-ready.
Tests remain DEFERRED per user. Handoff:
`docs/agent-handoffs/building-dual-leaf-pbr-stage23j-2026-11-23.md`.


## Stage 23J.1 — materials and textures are ONE authored PBR resource

User explicitly requested to manage materials and textures together,
not in two disconnected workflows. Use the same
`StyleMaterial` in `BuildingStyle.materials` for scalar PBR factors,
emissive colour and the optional `textures` GUID group:
`baseColor`, `metallicRoughness`, `normal`, `occlusion`,
`emissive`. Each texture GUID is the 32-character lowercase ID from
the existing `content/*.meta` asset catalog; older style JSONs with
only numeric material parameters remain valid.

Prefer `engine/buildings/BuildingMaterialAssets.h` and
`bindBuildingStyleMaterials(render::Model&, vector<StyleMaterial>,
resolveImageByGuid)` for a single all-material assignment; it builds
complete Renderer3D PBR material factors AND their texture references
and deduplicates reused image assets across multiple materials in one
atomic operation. The runtime building-wide convenience is
`runtimeTexturedBuildingModel(styledStructure,resolve,name,&error)`,
for the complete generated building including shell/roof/interiors.
For doors, gates and roof leaves use
`BuildingApertureGameBridge::bindStyleMaterial(socket,style,
materialIndex,resolve)` or
`BuildingAperturePresentation::bindStyleMaterial(socket,
style.materials[index],resolve)`. This guarantees an authored material
and its texture channels are applied in the SAME operation, instead
of separately calling PBR material and texture setters.

`resolveImageByGuid` must use the real game or Studio asset importer
and return an existing `render::Image`, respecting meta importer
settings (sRGB on base colour, linear for data maps; platform
compression/mipmaps) and normal asset/GPU lease ownership.
`apps/studio-native/StudioBuildingTextureResolver.h` adapts
`StudioAssetCatalog::byGuid` to an existing safe texture loader.
These helpers never decode files, create a second GUID database,
or upload textures on the simulation thread. Bind before GPU upload;
if modifying an already uploaded Model, use the renderer's existing
material reupload/lifetime protocol.

Stage 23J.1 is committed source only, NOT tested. In particular the
game-side importer callback and actual Studio UI inspector/material
asset picker remain integration work. See
`docs/agent-handoffs/building-unified-materials-stage23j1-2026-11-23.md`.


## Stage 23K — HTML5 PBR material + texture editor (Studio)

The existing `apps/studio-web/src/panels/generated/BuildingGeneratorPanel.tsx`
now embeds `BuildingMaterialEditor.tsx`, using the shared
`AssetReferencePicker` for image GUID selection, search, and internal
Assets drag-and-drop. The editor uses `studio.materials` to load an
existing project `materials.json` entry and **one**
`studio.materialPatch` to save scalar PBR factors (metallic,
roughness, normalScale, occlusionStrength, emissiveStrength) together
with texture GUIDs (texture, normalMap, metallicRoughnessMap,
occlusionMap, emissiveMap). It checks dirty state before discarding
selection edits and does not pretend a browser-only session can write
native files. Styling is in `BuildingMaterialEditor.css`.

Important ownership distinction: `materials.json` is the existing
Studio shared material library; portable building architectural
`BuildingStyle.materials` uses separately authored style JSON.
The UI currently edits the *shared material library* with native
persistence. It does NOT yet connect a selected shared material name
to a specific procedural building style slot. Never claim generated
building GLB receives these changes automatically. Next implement a
validated native style-material reference/override API, persist it
using actual Studio project owner + conflict/undo rules, and wire the
combined `BuildingMaterialAssets.h` resolver to the generator's
real material/texture upload pipeline.

No browser build/typecheck/native importer/GLB rendering tests were
run at this stage (user asked to defer tests). Follow handoff at
`docs/agent-handoffs/building-studio-pbr-materials-stage23k-2026-11-23.md`.


## Stage 23L — bind shared project PBR materials to generated buildings

`apps/studio-native/StudioBuildingGeneration.h` is the real native
producer behind `studio.buildingCatalog`, `studio.buildingInspect` and
`studio.buildingGenerate`. The catalog now exposes each architectural
style's actual `materials` slot IDs; no guesswork from material part
indexes. `BuildingGeneratorPanel.tsx` has a selector per slot to bind
that ID to an existing project material from `studio.materials`.
The request has an optional `materialBindings` object of
`{ styleMaterialId: sharedProjectMaterialName }`; these are authoritative
names, NOT filesystem paths. Style changes reset bindings to prevent
applying slots from the previous style.

On Generate, Studio loads the real project `materials.json` through
its native project owner, validates numeric PBR properties and image
GUID channels (including `texture`, `normalMap`,
`metallicRoughnessMap`, `occlusionMap`, `emissiveMap`),
and applies them to the corresponding generated `StyledStructure`
material before GLB writing. The original style slot identity is
retained. Unknown slot/library names fail instead of silently dropping
an override. `Glb` now embeds PNG and JPEG image bytes resolved from
the real `StudioAssetCatalog::byGuid` through sandboxed
`project/content` paths, and writes glTF PBR texture indices for
base color, metallic/roughness, normal, AO and emission. Images are
deduplicated by GUID and limited to 16 MiB each. The generator
serializes the selected library names in the adjacent
`.glb.recipe.json` `materialBindings` map; no separate material
registry is created.

`studio.buildingInspect` is a geometry-only preview and does not
claim to display project PBR textures. The GLB exporter currently
accepts only uncompressed PNG/JPEG, not KTX2/DDS/TGA; those need the
existing importer to transcode them or a supported glTF extension.
GPU upload remains the normal existing Renderer3D model owner
responsibility, not the generator's C++/Studio thread. The runtime
native `runtimeTexturedBuildingModel` can also consume authored
`StyleMaterial` PBR GUIDs via an asset resolver but is a separate
runtime entry point, not a direct Studio RPC.

Compile, export validation, native asset integration, cross-backend
Metal/Vulkan/GLES previews and TypeScript tests are still DEFERRED
per user request. A new handoff records these limitations:
`docs/agent-handoffs/building-style-material-binding-stage23l-2026-11-23.md`.


## Stage 23M — UV transforms, importer metadata and material reimport tracking

`StyleMaterial` now owns optional `uvScale:[u,v]`,
`uvOffset:[u,v]`, `uvRotation` (radians), `normalScale`
and `occlusionStrength` alongside existing numeric PBR factors
and texture GUIDs. The portable building style parser strictly bounds
these parameters; older style JSON without them remains unchanged.
`engine/buildings/BuildingMaterialAssets.h` carries these values
into the existing Renderer3D `render::Material` UV, normal and AO
properties when generating fully textured CPU Models.
`engine/buildings/BuildingGlbWriter.h` exports glTF
`KHR_texture_transform` (in `extensionsUsed` when needed)
for every authored texture channel and standard `normalTexture.scale`
and `occlusionTexture.strength` properties.
`apps/studio-native/StudioBuildingGeneration.h` overlays shared
`materials.json` UV scale/offset and Normal/AO strength onto the
selected architecture style slots before GLB generation.

A native GLB generation records a sorted `textureDependencies` list
in adjacent `.recipe.json` with each embedded asset's original GUID,
importer, length, content fingerprint (FNV-1a 64-bit decimal string)
and imported `.meta` settings. These values are evidence of the
exact source texture versions used by the generated model; consumers
can use them to detect a later reimport and schedule regeneration.
This is not an installed file watcher, not a hot GPU reload handler,
and not a substitute for actual image-content validation.
`StudioAssetCatalog::byGuid` and the current project/content
sandbox remain the only asset ownership and safety mechanisms.

Current GLB writer embeds PNG/JPEG only. For KTX2/DDS/TGA/WebP/EXR
source assets the next integration must use the existing importer
to decode/transcode to a glTF-compatible format or a supported
compressed-texture extension. DO NOT simply label arbitrary bytes
as PNG/JPEG. A render-owner must perform actual GPU resource
reupload/retirement when texture or material generation changes;
this stage does not upload textures or patch an already-live Model.

The legacy AIVERSE path from `runtimeTexturedBuildingModel`
to `render::Model` remains the canonical CPU-side Renderer3D bridge;
no new graphics backend was created. Source-level support is not
proof of Metal/Vulkan/GLES GPU results. As requested, no CMake, CTest,
glTF validator or Studio web/native tests were run.
See `docs/agent-handoffs/building-uv-import-revisions-stage23m-2026-11-23.md`.


## Stage 23N — UV editing and source texture revision inspection

The existing `apps/studio-web/src/panels/generated/BuildingMaterialEditor.tsx`
now presents UV Scale and UV Offset (two comma-separated finite values),
alongside the shared numeric PBR and texture selectors. The authoritative
`apps/studio-native/StudioSession.cpp` bridge parses these strings with
`parseStudioUvPair` in `StudioMaterialValidation.h` and emits the
actual two-component JSON arrays accepted by the existing
`material_set` operation. `materialSchema.ts` includes these keys,
which survive native save/reload. The editable browser model resolves
the native material library's `content/` texture paths to the same
GUIDs used by the shared AssetReferencePicker before editing. Native
`StudioBuildingGeneration.h` also recognizes these canonical project
paths when binding material textures to building styles and resolves
them through `StudioAssetCatalog::byPath`. Do not assume the project
library stores GUID strings on disk: the native `studio.materialPatch`
trust boundary converts GUID picker input into content paths.

`apps/studio-native/StudioBuildingDependencies.h` checks the previously
generated `.glb.recipe.json` textureDependencies manifest against
current AssetCatalog GUID lookup, source image byte fingerprint and
importer settings. `studioBuildingStaleMaterials(project)` lists
outdated dependencies under `project/content/GeneratedBuildings`
without overwriting generated GLBs, changing GUID identity or
reuploading GPU resources. The existing `StudioSession.cpp` native
bridge exposes it as `studio.buildingStaleMaterials`, and
`BuildingGeneratorPanel.tsx` provides a manual check button and
list of outdated recipe/source reasons. On saving a shared material,
the BuildingGeneratorPanel refreshes its library choices.

A changed image can require regenerating the GLB and replacing/reloading
the Model through the EXISTING Renderer3D model lifetime and GPU
retirement owners. **The checker does not do this automatically.**
It is a safe source-level inspection API, not a filesystem watcher,
shader hot reload or GPU uploader. The GLB image writer only embeds PNG/JPEG interchange bytes.
`engine/buildings/BuildingPngEncoder.h` now provides a bounded
dependency-free true PNG RGBA8 encoder (zlib stored blocks, max
2048x2048). `StudioBuildingGeneration.h` invokes the existing
`render::decodeImage` CPU decoder on non-PNG/JPEG imported sources,
and encodes the returned RGBA as valid PNG for GLB. This supports only
those formats the engine really decodes into RGBA (e.g. supported
TGA/DDS/BLP and CPU-decodable KTX2 variants), not arbitrary compressed
ASTC/GPU-only KTX2, WebP or HDR/EXR data. Failed or oversize conversion
must abort export, never relabel source bytes with a fake MIME type.
The source `.meta` fingerprint still hashes ORIGINAL input bytes
before conversion; recipe dependency metadata records the resulting
embedded image type. The GLB embedded image budget is now 20 MiB
for valid converted PNG/JPEG bytes (original imported source cap is
16 MiB). Performance, compression and cross-backend quality remain
unverified until native tests are explicitly enabled.

No C++/TypeScript compilation, CTest, native Studio invocation, actual
import/decode, glTF parser, Metal/Vulkan/GLES graphics acceptance or
runtime hot-reload QA was executed, per user deferral. See handoff
`docs/agent-handoffs/building-material-reimport-stage23n-2026-11-23.md`.


## Stage 23O — safe material/texture revisions and controlled scene promotion

`apps/studio-native/StudioBuildingRegeneration.h` reconstructs
a validated building generation request from a preexisting
`GeneratedBuildings/*.glb.recipe.json` file. The path MUST be
project/content-relative, directly inside GeneratedBuildings, with
no `..`, symlinks or arbitrarily large input. It invokes the same
`studioBuildingGenerate` native owner to create a fresh GLB + recipe
with a fresh AssetCatalog GUID; it does NOT overwrite the old GLB,
recipe, sidecar or currently resident Model. The existing
`studio.buildingRegenerate({recipe})` bridge returns the new asset
and source recipe linkage. Never treat an arbitrary WebView-provided
path as a source or perform a game-world write from the renderer.

`StudioBuildingGeneration.h` records
`materialDependencies:[{name,fingerprint}]` for bound shared
`materials.json` PBR definitions, alongside earlier
`textureDependencies`. `StudioBuildingDependencies.h` now
compares those material definition fingerprints in addition to
original image bytes, GUIDs and importer settings. This also detects
a numeric PBR edit when no texture was assigned. Old recipes with
no tracked dependencies remain legacy/unknown (not proof that all
materials are current).

HTML5 `BuildingGeneratorPanel.tsx` subscribes to native Studio
asset change events and performs read-only stale scans. The operator
can choose **Create updated copy** per stale recipe, or opt in to
**Auto-create updated copies when Assets change**. Auto mode
deduplicates previously regenerated source recipes within this
mounted project session to prevent an infinite loop caused by an
immutable old recipe remaining stale. It is not a daemon or persistent
file watcher, and it processes a bounded one new copy per event scan.
The browser mock explicitly refuses regeneration.

An updated copy is NOT automatically installed over every existing
scene object or GPU resource. To promote a generated GLB, select an
authored scene entity and click **Use on Selected Object**. This calls
the already-supported `studio.setProperty` with
`componentId=MeshRenderer,key=asset,value=newGuid`, so
`StudioSession::setMeshRenderer` validates the real asset,
modifies the existing MapDoc with Undo, and the existing StudioScene
resource owner loads/updates the Model via its own
`render::uploadModel`, cache invalidation and
`render::releaseModel` lifecycle. NEVER upload/release GPU
resources on the simulation thread or while in-flight renderer
draw packets still borrow the old Model pointer.

This stage wires Studio-native create-only regeneration plus an
explicit scene promotion workflow. It does NOT perform hot-swapping
every live instance in a shipped game, promise identical old and new
GUIDs, or auto-retire GPU resources by itself. Shipping runtime
revisions need their own owner-bound GPU lease/fence and world-scene
publication. No compiler, browser TypeScript, native Studio,
glTF validator, GPU/PHYSIS or release tests were run (user deferred).
Handoff:
`docs/agent-handoffs/building-safe-reimport-stage23o-2026-11-23.md`.


## Stage 23P — two-leaf authoritative ECS fixed tick and render session

`engine/buildings/BuildingDualLeafGameSession.h` now connects
the opt-in two-collider `BuildingDualLeafOwner` to existing
`World`, `SpatialWorld` and `BuildingGraphChangeRouter` phases.
Exported via `BuildingRuntime.h`. It is one scene-owned entry point,
**not** a new engine scheduler, networking transport or GPU backend.
Attach with the EXACT same `sceneId` as the physical owner and with
mandatory live `Authorize(Entity,socket,targetOpen)` and
`InteractionRange(Entity,closedWorldBounds)` guards. The transport
must authenticate a peer and resolve the actor's real ECS generation
before calling `session.enqueue(ecs, BuildingApertureCommand)`.
A bounded queue rejects duplicate/reordered actor nonces, invalid
scene/masks and dead actors.

The authoritative owner invokes
`session.fixedTick(ecs,dualOwner,spatial,graphRouter,serverTick,stepPermille)`
AFTER movement and PHYSIS occupancy sensors update. It revalidates
actor liveness/access/range; permits only one motor at once; commits
one paired collider step on a tick, then sends revisioned graph
invalidations. Graph ACK failures stall any new commands. On successful
retry the prior committed revision is presented before the next motor
step. `frame.presentationAccepted` marks a read-only snapshot
synchronized to physics and graph consumers, not a finished NavMesh
refresh. Only `pose.open` on a FULLY OPEN endpoint grants permission
for game motors/actor traversal.

`session.submit(renderDtMs, SubmitLeaf)` on the actual Renderer3D owner
passes each borrowed `render::Model` and world-space
`BuildingApertureLeafPose` into the existing game's ModelDraw
submission / lease ownership. No GPU resources are uploaded in ECS.
`session.bindMaterial(socket,StyleMaterial,resolver)` assigns the
complete numeric PBR+GUID image set before GPU upload.
Presentation access from the world and renderer phases is
mutex-serialized to protect model/pose state; do not recursively call
session methods from within `SubmitLeaf`. Existing renderer must own
upload, retirement fences and cache invalidation.
`retireActor(entity)` is required before ECS generation reuse.
Reconcile the renderer's GPU leases before `detach()`, and detach
the physical owner separately.

This is a concrete reusable game scene integration API, **not** yet
a registered concrete game's main-loop invocation or real GPU test.
Source-only C++ changes are untested per user request. Next insert
it in an actual game host with real motor callbacks and transport
identity, run full CMake/CTest + GPU/PHYSIS acceptance, then resolve
the feature branch vs main before merging Draft PR #114. Follow
`docs/agent-handoffs/building-dual-leaf-game-session-stage23p-2026-11-23.md`.


## Architecture correction after Stage 23Q — NO legacy FPS/ActorHost coupling

The current engine is genre independent. The user explicitly rejected
integrating new procedural buildings into the legacy FPS module or
`ActorHost`. Stage 23Q attempted that and the coupling was reverted
in the branch. **Do not add building-system hooks to**
`apps/fps/`, `engine/actor/ActorModule.*`, or
`engine/net/authority/Session.h`. Do not use a genre-specific
network snapshot, collision copy, or game client as a building system
owner. Those are obsolete integrations for this task.

Canonical native building integration is through existing shared
ECS `World`, `Simulation` and world collision/graph and rendering
owners, independent of genre:
`engine/buildings/BuildingDualLeafOwner.h`,
`engine/buildings/BuildingDualLeafGameSession.h`,
`engine/buildings/BuildingDynamicNavSource.h`,
`engine/buildings/BuildingGraphChanges.h`, and
`engine/buildings/BuildingAperturePresentation.h`.
For packaged-game resource preparation, use
`BuildingMaterialAssets.h` + `RuntimeBuildingModel.h`.
Game-neutral systems register in a shared ECS simulation/lifecycle
owner, not any FPS/RTS-specific module. The connection layer maps an
authenticated actor to the existing ECS Entity using generic game
authority callbacks; do not invent a second character registry or
special FPS command protocol. Renderer3D owns all GPU/model leases.

The existing `BuildingDualLeafGameSession` already gives a
genre-neutral API for command queue, authoritative fixed tick,
paired collider mutation, graph acknowledgement, and CPU visual
publication. What remains is integration with a common ECS
simulation/world scene owner, including registration in real engine
tick/render stages; a header alone is not evidence of integration.

Stage 23Q game-specific source attempt was reversed in separate
GitHub commits before compiling or testing. Never reinstate it.
Correction handoff:
`docs/agent-handoffs/building-stage23q-architecture-correction-2026-11-23.md`.


## Stage 23Q (correct architecture) — common ECS Simulation adapter

`engine/buildings/BuildingEcsRuntime.h` is the **genre-independent**
integration with the current `Simulation` and its native ECS `World`,
not the legacy `engine/actor`/FPS/RTS module. The previously attempted
genre-specific Stage 23Q source changes were reverted and MUST NOT be
reintroduced.

The existing shared Simulation has a two-phase lifecycle:
`GameModule::setup` registers systems/persistence first,
`Simulation::setup` additionally auto-installs shared PHYSIS systems
and seals the persistence registry, and ordinary fixed ticks run
systems in registration order. Integration must respect this:

1. In common map/scene initialization during `GameModule::setup`,
   before the registry seals, call
   `building::registerBuildingEcsPersistence(sim, sceneId)`.
   The registered `BuildingEcsWorldState` ECS singleton contains
   the current authoritative per-socket committed physical progress,
   open/moving flags and revision, graph ACK and fixed-tick identity.
   It is copy-on-write snapshot and save compatible through the
   existing `PersistentEcsRegistry`; no GPU state or external
   collider pointers are serialized.
2. Use the game's existing scene collision ID namespace, one
   `SpatialWorld`, `BuildingDualLeafOwner` and
   `BuildingGraphChangeRouter`, plus the existing
   `BuildingDualLeafGameSession` with real ECS actor authorization,
   physical interaction range, and sweep/obstruction policies.
   Do NOT attach the old single-barrier owner to the same sockets.
3. AFTER `Simulation::setup()` and PHYSIS auto-registration,
   BEFORE the first `Simulation::step()`, call
   `BuildingEcsRuntime::attachAfterSetup(sim, session, physics,
   spatial, router, stepPermille)`. This appends the shared
   `building.dual_leaves` fixed-tick system after existing
   module movement and shared PHYSIS stages. A weak captured owner
   prevents a dangling callback if the scene is unloaded.
   Retain the returned shared_ptr in the common scene owner.
   Command transport resolves authenticated actor identity to a
   real Entity and calls `runtime->enqueue(command)`.
   No genre-specific action or network wire format is mandated.
4. Each accepted server tick calls existing dual-owner physics,
   graph ACK and CPU presentation; the EOS physical pose is
   recorded in the ECS singleton even when graph ACK is still
   pending, while visual acceptance remains gated on graph ACK.
   The real renderer consumes `BuildingDualLeafGameSession::submit`
   through its normal `Renderer3D::drawModel` and GPU owner.
5. `Simulation::addStateRestoredHook` suspends the adapter when
   `loadState` or persistent deserialization restores World,
   because external SpatialWorld colliders, the graph router,
   the pending command authority and GPU leases are NOT part of
   ECS COW snapshots. The game scene owner must tear down/rebuild
   or fully reconcile those derived external owners against the
   saved `BuildingEcsWorldState` before accepting new building
   commands. Never resume solely by clearing a boolean. When
   unloading, unregister/revoke commands and retire renderer
   model leases before removing the physics scene; call
   `retireActor` before Entity generation reuse.

This is a concrete shared Simulation registration helper, **not**
a default auto-installed feature for every game map. Games opt in
by calling the two setup phases and supplying their existing
world collision and render owners. No tests were run per user request.
Handoff:
`docs/agent-handoffs/building-common-ecs-stage23q-2026-11-23.md`.


## Stage 23R — genre-neutral ECS/PHYSIS sweep + deterministic rollback reconstruction

The old FPS/ActorHost engine is NOT a building integration target.

- `engine/buildings/BuildingEcsObstruction.h` is completely
  Box3D-independent. ECS entities with existing
  `aiverse::Transform` plus `BuildingPhysicalExtent` (positive
  authoritative half-size, `blocksBuildingMotion=true`) are checked
  in deterministic ECS iteration order against the swept AABB of EACH
  physical door leaf. Register `registerBuildingPhysicalExtentPersistence(sim)`
  inside common `GameModule::setup` before the persistent registry is
  sealed. This is opt-in: bodies without the extent component are not
  magically inferred, and gameplay must register extents for relevant
  actors, cargo, vehicles and movable obstacles.
- `engine/buildings/BuildingPhysisObstruction.h` is OPTIONAL for
  projects with real `physics::Scene`/Box3D. It calls the existing
  PHYSIS `Scene::overlapBox` on each leaf's swept AABB. PHYSIS query
  categories must exclude leaf bodies if they are ALSO registered in
  Box3D; the SpatialWorld leaf IDs are NOT PHYSIS BodyHandles.
  The caller guarantees ECS and PHYSIS are in one completed fixed-tick
  owner phase. It is not a second physics database.
- `BuildingDualLeafOwner::attachRestored(...)` restores the exact
  0..1000 physical leaf travel from the SAVED
  `BuildingEcsWorldState`, using two pre-reserved unique collider IDs
  per socket. Partial leaves are conservatively stationary/blocked
  pending a fresh authenticated command; no old network action is
  replayed. Owner can attach only into a fresh SpatialWorld slot, and
  never overwrite foreign colliders.
- `engine/buildings/BuildingEcsReconcile.h` is the transaction
  boundary after restoring a shared ECS World: it validates the source
  sockets and physical progress, builds the NEW physical owner,
  publishes an explicitly selected graph-domain invalidation on a
  NEW router using the union of complete leaf travel extents (not
  merely the initial aperture), requires ACK from ALL registered
  selected consumers, attaches a new presentation session, and
  invokes `BuildingEcsRuntime::rebindAfterRestore`.
  Invalid/missing domain owners or collision problems leave the
  runtime suspended; successful recovery never blindly reuses old
  graph router nonces or stale command queue entries. The owner must
  rebuild its native NavMesh source from the restored SpatialWorld
  and refresh dirty tiles in the normal nav owner phase after ACK.
- `rebindAfterRestore` checks actual collider ID presence/bounds,
  socket order, progress, revision and graph ACK against the
  restored authoritative ECS record before allowing the existing
  Simulation system callback to advance. It does not register a
  duplicate fixed-tick system. Resetting an internal suspended flag
  without this verified reconstruction is prohibited.
- Scene collision namespaces and leases remain owned by
  `SceneCollisionNamespaces`; allocate and retain distinct
  leaf collider IDs before any owner attach. The class does not
  invent a new allocation space.

IMPORTANT: a source-level generic ECS/PHYSIS obstacle adapter does
not prove all projects have authored their collidable extents, nor
does successful invalidation ACK prove a full NavMesh refresh.
External player/network authority, snapshot transport and GPU
retirement remain their existing owners. No compiler, CTest,
Box3D contact test, real nav refresh or GPU test has been run,
per user's feature-first testing deferral. Handoff:
`docs/agent-handoffs/building-ecs-physis-reconcile-stage23r-2026-11-23.md`.

## Stage 23S — Renderer3D ModelDraw + verified GPU resource retirement

Do NOT attach new building functionality to obsolete FPS/ActorHost or
any other genre module. The actual common graphics integration is:

- engine/buildings/BuildingRenderer3DScene.h: a renderer-thread GPU
  resource owner for one existing BuildingDualLeafGameSession. Call
  initialize(device,renderer,existingRenderDeviceLifetime.lease(),
  session,stableSceneInstanceId) AFTER all the authored PBR material
  factors and texture GUID images have been resolved into CPU models.
  Requires a live generation-safe device lease on its owning thread.
- The adapter walks session.forEachCpuModel, marks each leaf a dedicated
  non-rigid/non-autoLOD model, calls the real render::prepareModelUpload
  and render::uploadModel. It stages speculative images without
  publishing new entries in the shared texture cache until the owner
  proves residency and has an explicit retirement path.
- engine/world/WorldScene.h/.cpp has the optional
  setBuildingRenderOwner(shared_ptr<BuildingRenderer3DScene>) and
  setBuildingRenderElapsedMs(dtMs) composition seam. WorldScene keeps
  a WEAK reference to the graphics owner and calls owner->draw from
  its real draw(Renderer3D,Cell,cellSize) method. The game scene owns
  session, renderer owner and GPU lifetime; the WorldScene does NOT
  copy authoritative collision state or acquire raw ECS pointers.
- draw() uses BuildingDualLeafGameSession::submit to receive only
  graph-ACKed physical poses; for each visual panel it creates
  Renderer3D ModelDraw(model, persistentId, world transform) and
  calls the existing Renderer3D::drawModel. Position is made
  camera-relative using world::relativeTo. Unique per-scene-instance
  namespaces are required. It does not make physical access decisions
  from interpolation, upload meshes in the fixed tick, or re-upload
  textures on each draw. Static render packet reuse is disabled for
  moving leaves, with stable per-socket/leaf IDs for diagnostics.
- On GPU unload/update, call WorldScene::setBuildingRenderOwner({})
  followed by BuildingRenderer3DScene::shutdown at an owning renderer
  frame boundary OUTSIDE an active rendering pass. It verifies the
  device generation with RenderDeviceLifetime::Lease and calls the
  existing gfx::Device::waitForResourceIdle. ONLY after this succeeds
  can it invalidate Renderer3D model caches and releaseModel for each
  uploaded CPU Model. Failure preserves all session Models and GPU
  references and requires retry; NEVER detach the session, release
  its CPU models or retire the device epoch while cleanup is pending.
  Then game host may detach the authoritative session and physics.
  This conservative approach can stall the device at unload and is
  not yet a per-resource async-retirement replacement.
- WorldScene::buildingDrawError() exposes failures because the
  original WorldScene draw API returns void. The game host should
  inspect it and report device or session errors rather than assume
  every frame rendered.

This is REAL source wiring into the COMMON WorldScene render path,
not a registered game executable or verified GPU capture. Tests were
deferred by the user, so no native/GLTF/Metal/Vulkan/GLES result may
be claimed. Read the stage handoff:
docs/agent-handoffs/building-renderer3d-stage23s-2026-11-24.md.

## Twelve built-in architecture settings (Stage 23T.5)

The following visual setting IDs are available for the *same*
functional building / room / lift / door / ECS runtime: 
`medieval-fantasy`, `dark-fantasy`, `steampunk`,
`cyberpunk`, `scifi-space`, `alien-civilization`,
`post-apocalypse`, `modern`, `ancient-civilizations`,
`asian-historical`, `solarpunk` and `underwater`.
An original `scifi` style also remains for legacy users.
The Studio generator advertises these as independent named settings
rather than relying on a shortened first-word label.

Read `docs/building-settings.md` for each setting's materials,
massing/structure forms, facade and ornament grammar. Each setting
has its own `skills/world/aiverse-building-<id>/SKILL.md` and
registration in `skills/catalog.json`. For any style-specific task
read BOTH this parent skill and the selected setting skill.

Visual material schema is data-only. `materials.<name>` may now
contain `surfacePattern` (one of 16 values supported by
`BuildingSurfacePattern.h`) and optional `surfaceSeed`.
The portable deterministic CPU generator builds actual 128x128
albedo, metallic/roughness and normal images. The GLB writer
encodes those images as PNG; the runtime Model converter populates
its existing render::Image table. Existing imported GUID texture
slots *override* generated maps for each corresponding channel;
never invent GUIDs or treat a generated `surfacePattern` as a file
path. Explicit AO and emissive image channels still require
imported GUIDs, while numeric AO/emission factors remain valid.

All 12 settings have 3 authored palettes, 19 named PBR material
slots, four neutral structural silhouette IDs (`residence`,
`hall`, `workshop`, `watchtower`) and authored generic
massing/facade/prop grammar. These structure names describe
appearance only, not RTS/FPS/gameplay classes. The original
`modern` style ID was updated in place, not renamed.

Source changes have NOT been compiled or rendered. Do not claim
visual accuracy or successful GLB/Metal/Vulkan/GLES validation
until the deferred Stage 23U full QA. The next agents should
avoid adding hardcoded per-setting branches to ECS/PHYSIS/Renderer3D:
setting selection belongs exclusively to data and Studio UI.
Mandatory handoffs for this pack are
`docs/agent-handoffs/building-settings-core-stage23t0-2026-11-24.md`,
`building-settings-historic-stage23t1-2026-11-24.md`,
`building-settings-future-stage23t2-2026-11-24.md`,
`building-settings-civil-stage23t3-2026-11-24.md`,
`building-settings-nature-stage23t4-2026-11-24.md`, and
`building-settings-catalog-stage23t5-2026-11-24.md`
under the same `docs/agent-handoffs` directory.

## Stage 23T.6 — saved recipe authoring and authoritative layout inspection in HTML5 Studio

The existing panel apps/studio-web/src/panels/generated/BuildingGeneratorPanel.tsx
now browses previously generated building recipe sidecars using the
new native studio.buildingRecipes API, and reloads one through
studio.buildingReadRecipe. The native implementation in
apps/studio-native/StudioBuildingRegeneration.h permits only bounded
real files directly inside project/content/GeneratedBuildings, refuses
symbolic links and path traversal, checks recipe schema 1, loads the
existing style, and validates the restored request through the
existing buildingStudioDetail::request. The UI restores style/setting,
functional/preset mode, dimensions, floors, X/Z rooms, seed, features,
and PBR materialBindings. Next Generate always creates a NEW GLB and
recipe and retains the original file/GUID. Native Session.cpp forwards
both read/list methods; apps/studio-web/src/types.ts and mock.ts
declare them, and the browser mock never pretends to read project
files.

studio.buildingInspect now uses the actual
generateFunctionalBuilding(style,request.functionalRecipe) to expose
authoritative socketId, featureIndex, opening kind, side, width,
height, open fraction and outside position, along with number of
physical structure solids, graph connections and rooms/floors grid.
The new BuildingPlanPreview.tsx is an interactive TOP-DOWN
inspect/edit-navigation view, not fabricated 3D: selecting an actual
socket scrolls/focuses its corresponding functional property editor.
BuildingGeneratorPanel.css supplies responsive styling. The shared
native Studio assetThumbnail already renders a REAL model thumbnail,
and Place in Scene intentionally creates an actual scene entity that
can be explored with the existing interactive Scene viewport.

On GLB creation, StudioBuildingGeneration.h also records
physicalSockets for genuinely generated door/roof aperture features:
stable source socket IDs, featureIndex, kind, initialOpen fraction and
conservative LOCAL closedBoundsMin/Max. These local bounds match the
existing BuildingApertureOwner wall/roof formulas at origin/yaw 0.
The gameplay scene owner MUST still transform them for the actual
world instance, reserve unique real collider IDs via
SceneCollisionNamespaces, and instantiate through the shared ECS
runtime. Do not assign fake collider IDs in Studio, and never derive
physics from cosmetic panel geometry.

IMPORTANT: This stage DOES NOT implement a zero-write temporary
interactive native 3D viewport. The top-down plan is interactive,
and an actual 3D scene can be inspected only after a GLB is generated
and placed through the existing Studio scene APIs. Do not describe
native geometry metrics or a thumbnail as full interactive 3D preview.
The required additional zero-write native GPU preview pipeline must
be implemented by the established StudioScene/GPU owner if needed.
No TS/C++ build, GLB, native GPU or scene QA was run because the user
deferred tests. Handoff:
docs/agent-handoffs/building-studio-recipes-layout-stage23t6-2026-11-24.md.

## Stage 23T.7 — real ephemeral Studio Renderer3D building preview

Native Studio now has an opt-in ZERO-PROJECT-WRITE interactive 3D preview
for generated building requests, using the existing StudioScene and its
RenderDeviceLifetime / GPU Renderer3D. It does NOT create GLBs,
recipes, .meta files or asset GUIDs.

- apps/studio-native/StudioBuildingGeneration.h adds
  studioBuildingPreviewCpuModel(engine,project,params). It uses the same
  strict buildingStudioDetail::request/produce and applyProjectMaterials
  owners as generation; binds the existing portable runtime
  BuildingMaterialAssets generated 128x128 PBR albedo/MR/normal maps and
  resolves real project texture GUIDs from StudioAssetCatalog. It
  validates every path component, disallows symlinks and source files
  over 16 MiB, and decodes the image with render::decodeImage. No
  filesystem output is emitted.
- apps/studio-native/StudioHost.cpp answers studio.buildingPreview with
  a temporary CPU model on the host side, then moves it to StudioScene
  via the EXISTING onScene render-thread dispatch. It answers
  studio.buildingPreviewStop with setBuildingPreview({}). Commands
  require an open native Scene viewport and project, never a WebGL
  renderer or another native device.
- apps/studio-native/StudioScene.h/.cpp holds one preview
  unique_ptr<render::Model> with a separate GPU-residency flag. Its
  setBuildingPreview() runs ONLY on the graphics owner thread between
  frames. Before replacing/removing the old Model it requires the
  existing gfx::Device::waitForResourceIdle barrier, invalidates
  Renderer3D caches, and releases render::Model. A failed idle barrier
  retains all old pointers and returns an error; a partially uploaded
  new Model is NEVER presented as GPU-ready.
- StudioScene::frame takes an isolated native RenderDeviceLifetime-owned
  GPU path while preview is active, using its existing camera_
  controls (orbit/pan/zoom), renderer_.beginScene, renderer_.drawModel,
  lighting, FXAA and normal frame scope. The model is framed from real
  boundsMin/Max. This is a real GPU 3D preview, not a browser 3D
  approximation, a thumbnail or an SVG layout chart.
- apps/studio-web/src/types.ts and mock.ts declare the native-only
  commands, with explicit rejection from browser demo. The
  BuildingGeneratorPanel has Preview 3D in Scene and Close 3D preview
  buttons; it opens the common Scene panel BEFORE sending preview RPC
  and requests teardown when the project changes. Repeated button
  activation rebuilds the preview from current form values.

Architectural contract: generation and ECS/PHYSIS authority never
render directly. StudioScene owns GPU upload and safe deletion; project
GLB exporter remains a separate create-only workflow. Although the
code now routes an actual temporary Model through StudioScene, native
Metal/Vulkan/GLES view acceptance and PBR screenshots have NOT been
run. Deferred full Studio C++/TypeScript, GLB, image import and GPU
tests may uncover issues. Do not claim real executed verification.
Handoff: docs/agent-handoffs/building-studio-native-preview-stage23t7-2026-11-24.md.

## Stage 23U — verification coverage and known gates (2026-11-24)

The user has now explicitly authorized testing. First focused QA changes:

1. New tests/libraries/building_surface_pattern_tests.cpp checks ALL
   16 portable surface patterns, deterministic RGBA8, 128x128 dimensions,
   generated albedo + glTF packed metallic/roughness + normal channel
   contracts, changed seeds, and rejection of unknown patterns.
2. New tools/buildings/verify_setting_glb.py reads ACTUAL GLB2
   exporter output for all 12 styles, checks magic/size and JSON/BIN
   buffer chunks, 128x128 embedded PNG image bytes, image buffer view
   bounds and baseColor/metallicRoughness/normal texture bindings.
3. .github/workflows/building-style-focused.yml now compiles/runs
   the PBR C++ contract and generates all 12 GLB assets using the
   existing aiverse_building_export, then verifies their real PNG
   PBR images and publishes a CI artifact for renderer review.
4. tests/studio/BuildingGenerationTests.cpp checks native catalog
   discovery of all 12 styles, material/preset inventory, source
   recipe reading and list, exact generated physical socket bounds.
5. Static authored-source validation in the connected GitHub branch:
   parsed all 12 JSON files; checked 623 material keys, strict visual
   setting identity, 0..1 PBR ranges, supported surface patterns,
   architecture massing, major form kinds, valid material references
   and palette counts; no issues detected by those static checks.
6. Source bugs fixed in Stage 23U: optional studio preview no longer
   unconditionally rebuilds the SQLite Assets index; imported GUID
   resolution still uses the existing StudioAssetCatalog lazily.
   The generated GLB thumbnail URI must carry the content/ prefix
   required by StudioHost.assetThumbnail.

IMPORTANT: GitHub Actions workflow jobs were observed QUEUED,
not passed. This model's container has no cloned AIVERSE source
tree and cannot reach github.com via local git/network; do not
fabricate a local CMake/TypeScript/Metal run. Confirm actual job
conclusions and logs before saying C++/Vite/GLB/PHYSIS/GPU pass.
The original 12 authored JSON style static checks do NOT establish
native C++ parser compliance or visual quality. Do not merge the
Draft PR #114; its base is currently feature/aiv20261122_building_styles,
not main. The integrator must reconcile main and then run the full
platform and GPU acceptance before merging. Handoff:
docs/agent-handoffs/building-verification-stage23u-2026-11-24.md.

## Stage 23U.1 — rollback and authored physical state hardening

- `libraries/include/aiverselib/systems/BuildingStyle.h` now appends
  `surfacePattern`/`surfaceSeed` after all legacy StyleMaterial
  fields. This preserves the meaning of existing C++ positional
  aggregate material constructors and old style data.
- `engine/buildings/BuildingApertureOwner.h` maps a validated
  functional recipe's authored initial `open` fraction 0..1
  into 0..1000 authoritative `physicalPermille`, while retaining
  conservative collision and granting passage only at 1000.
- `engine/buildings/BuildingDualLeafOwner.h` initializes its two
  independent SpatialWorld colliders at that exact authored physical
  fraction and permits `attachRestored` from saved revision 0.
  Revision zero is valid BEFORE the first motion; the fresh
  BuildingEcsReconcile graph invalidation uses a nonzero revision
  before restarting authority.
- `engine/buildings/BuildingAperturePresentation.h` initializes
  CPU visual interpolation at saved `physicalPermille` rather
  than binary pose.open, so visually partial apertures are not
  presented as fully closed on their first frame.
- `tests/libraries/building_dual_leaf_restore_tests.cpp` now
  exercises real SpatialWorld paired leaf colliders at 500/1000,
  restoration at revision zero, one acked movement to 600 and
  refusal when a sweep obstruction exists, including teardown
  of both colliders. `.github/workflows/building-style-focused.yml`
  compiles and executes this test with the real SpatialWorld.cpp.
- `tests/render/runtime_building_model_compile.cpp` now includes
  shared BuildingEcsObstruction, BuildingEcsReconcile and
  BuildingRenderer3DScene headers with compile-time type contracts
  for their public API, catching missing types and invalid includes
  in the existing -fsyntax-only focused CI step.

Important: these fixes/tests were committed to the active PR branch,
but GH runners have remained queued and locally there is NO mounted
AIVERSE checkout; they have NOT yet completed C++ compilation or GPU
execution. The real final acceptance gate is successful Building Style
Focused, HTML5 Studio (TS/Vite + Mac/Windows native), CI, explicit
PHYSIS+rollback and at least one actual graphics capture. PR #114
is still Draft and based on feature/aiv20261122_building_styles, not
main. Do not claim release or merge readiness until the above passes.
Handoff:
`docs/agent-handoffs/building-stage23u1-final-hardening-2026-11-24.md`.
