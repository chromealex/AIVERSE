
# AIVERSE Building StructureGraph Stage 12 — Windows, Rooms and Floors

Date: 2026-11-23
Branch: feature/aiv20261123_building-grammar
PR: https://github.com/chromealex/AIVERSE/pull/114 (Draft)

## Implemented

1. BuildingStructureGraph.h: window kind and floor/sill property fields.
   Functional recipe floors/roomsX/roomsZ with constrained size and count.
   BuildingGraphPart WindowPane/Partition, BuildingGraphRoom and
   BuildingGraphConnection. Real per-storey floor slabs and physical room
   partition segments are cut around interior door passages. Windows are
   actual apertures in the main exterior wall and use thin 0.05m glazing
   instead of full wall-thickness opaque cubes.
2. BuildingFeatureGenerator.h: parser accepts floors, roomsX/roomsZ, window,
   floor, sill; uses material-specific pane and partition solids, preserves
   real building height in the finished structure metadata and excludes
   windows from pedestrian gate anchors.
3. BuildingStructureGraph.h now has no visual style or engine dependency.
   Standalone C++20 test compiles the graph without Fixed.cpp/Json.cpp.
4. RuntimeBuildingGenerator.h: RuntimeBuildingResult.structureGraph exposes
   rooms, their horizontal adjacency and source graph solids to games.
5. Studio types/panel: Floors, Rooms X/Z, window kind, floor index and sill
   with validation. Native building generator writes those fields into
   canonical .glb.recipe.json beside the asset (GUID/.meta catalog).
6. Example two-floor-workshop.json: 24x14x18, two floors and two by two room
   grid with upper/lower windows and independent gate and retractable roof.
7. Tests: tests/libraries/building_feature_tests.cpp extends mesh/collision,
   cross-style identity, upper windows, floor slab, rooms, partition doorways
   and invalid floor indexes; new
   tests/libraries/building_structure_graph_tests.cpp is pure standalone
   C++20 validation. tests/studio/BuildingGenerationTests.cpp extends native
   GLB and recipe sidecar verification. CMake target
   library_building_structure_graph, focused workflow compiles/tests
   graph and produces scifi/modern two-floor GLBs.

## Actual verification

GitHub connector source commits and readback are confirmed. No passing
C++ compiler/test run, Studio TypeScript build, Mac GPU screenshot or
multi-platform release validation has been observed for Stage 12.
The prior user Mac legacy dimensioned GLB batch was 19/20 PASS; factory
16x9x11 seed3 failed. That is unrelated to this new functional room graph.
Latest focused CI was queued/pending and may need rerunning on Mac.

## Architectural limits / handoff next action

No stairs, elevator or vertical room connections: slabs are continuous
between floors. Do not claim navigable multi-floor buildings.
Horizontal rooms have 1.2m open internal passages, coarse physical AABBs
and room-to-room connection IDs; no published navmesh or gameplay ECS room
owner. Window panes collide and are NOT walkable doors. No real-time
window/door animation. Window placement shares wall horizontal intervals
across floors, preventing true vertical stacking for now.
No arbitrary-shaped rooms or structural engineering solver.

Next: validate C++ and TypeScript; inspect actual GLBs/Metal screenshots.
Then add vertical stairs/lifts and floor openings, vertical room graph edges,
per-storey two-dimensional aperture layout, editable arbitrary room geometry
and GPU node-based moving gates/panels. Preserve genre independence and
separate handoff after each stage. Keep PR draft until verified and
reconciled with main/parallel Studio work.
