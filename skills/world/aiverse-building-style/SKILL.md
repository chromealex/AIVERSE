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
