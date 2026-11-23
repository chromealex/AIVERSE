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
