
# Building Generator Stage 13 — Vertical circulation

Date: 2026-11-23
Repo: chromealex/AIVERSE
Branch: feature/aiv20261123_building-grammar
PR: https://github.com/chromealex/AIVERSE/pull/114 (Draft)

## Architecture and files changed

- libraries/include/aiverselib/systems/BuildingStructureGraph.h:
  extends the genre-neutral recipe with BuildingFeatureKind::Stairs/Elevator;
  new graph parts StairTread, StairLanding, ElevatorPlatform, ElevatorGuide;
  BuildingConnectionKind distinguishes Passage/Stair/Elevator. Vertical core
  planning checks geometry and room cell support, disallows overlapping cores,
  and cuts all upper floor slabs into solid rectangles around each shaft.
  Opposed flights and turning landing use stair tread boxes <=0.18m riser,
  >=0.24m horizontal tread. Lift has 4 guide rails and one initial platform.
- libraries/include/aiverselib/systems/BuildingFeatureGenerator.h:
  parser and style-material mapping for the two new feature kinds, continuing
  one graph source for model mesh and physics colliders.
- apps/studio-web/src/types.ts / panels/generated/BuildingGeneratorPanel.tsx:
  add Stair/Lift choices and validated X/Z centre + shaft W/D fields.
  Existing floors and room grid options are reused.
- engine/assets/building-recipes/vertical-circulation.json:
  two-floor workshop with separate staircase/lift cores, accessible front
  entrances and upper windows, compatible with sci-fi/modern styles.
- tests/libraries/building_structure_graph_tests.cpp:
  verifies two-storey shaft holes, bounded stair step pitch and landings,
  vertical room links, rejection of overlapping cores and insufficient run,
  three-storey geometry and a single initial lift platform.
- tests/libraries/building_feature_tests.cpp:
  verifies new GLB construction source, style-invariant geometry/collision,
  semantic stair/lift anchors and vertical links.
- tests/studio/BuildingGenerationTests.cpp:
  checks GLB and canonical recipe persistence for shaft dimensions and
  positions plus cross-style generated triangle equality.
- .github/workflows/building-style-focused.yml:
  adds export of vertical-circulation GLB for scifi and modern styles,
  existing building graph and functional tests remain enabled.

## Tests actually performed

- Source written through GitHub connector; no local Mac source clone is
  exposed to this tool session, so Stage-13 C++ compilation/runtime/Metal
  screenshots have NOT been verified.
- Previous user Mac batch (legacy presets) was 19/20 PASS; factory
  16x9x11 seed3 failed. This is an unrelated known legacy sizing issue.
- Latest focused CI status must be read before claiming success.
- No actual lift animation, passenger transport or navmesh stitching has
  been implemented. Vertical graph links are semantic; collision and shaft
  openings are genuine boxes but not yet gameplay routing.

## Important ownership and limitations

The portable StructureGraph remains pure C++20, without the renderer,
Studio, file IO, PHYSIS or gameplay dependencies. The game/physics/navmesh
owners must attach stair links, moving lift state, actor pathfinding and
published collision updates. The model bridge still converts CPU geometry
and should upload/retire only through Renderer3D's existing device owner.
No gameplay-genre branches.

## Follow-up for next agent

1. Run standalone StructureGraph C++20 tests and full feature C++ tests on
   macOS Clang; verify current GitHub focused CI and Studio TS/native tests.
2. ModelView screenshots and normals/collision audit for two/three storeys.
3. Bind vertical graph edges to existing navmesh/off-mesh link APIs and
   accessible room path queries, preserving authoritative simulation.
4. Lift actor/door control, platform motion with collision updates, safety
   interlocks and network state; avoid rebuilding procedural meshes per tick.
5. Connect stairwell room corridors and support multi-shaft floor plans
   beyond two static cores. Retain exact recipe and scene handles.
6. After each implementation stage record actual tests, branch SHA, files,
   ownership and next step; do not merge Draft PR into main prematurely.
