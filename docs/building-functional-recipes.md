# Functional building recipes — setting-independent properties

## Model

Visual appearance and functional capability are separate dimensions. Setting/style
selects materials and colours. A functional recipe supplies actual wall openings,
roof apertures and movable panels. It does NOT select gameplay role factory,
barracks, depot or command. Existing legacy preset calls remain compatible.

Current kinds:
- large-gate: 1..8 vehicle openings, metres-based clear width/height, wall side, 0..1 open state
- small-gate: 1..8 small-gate openings, same parameter types
- personnel-door: 1..8 personnel entrances
- sliding-roof: one real roof aperture with two sliding panels, width/depth in metres, 0..1 open state

Four sides: front, back, left, right. Geometry validates non-overlapping space
per wall, building bounds and panel sizes before generating a mesh. Walls are
constructed as solid segments around openings and share collision shapes; the
roof ring excludes its true aperture, and retracted roof panels remain visible
on the solid side deck. Style-derived material slots affect appearance only.

## Example

File: engine/assets/building-recipes/multiuse-open.json

    {
      "width":24,"height":12,"depth":18,"seed":7,
      "features":[
        {"kind":"large-gate","side":"front","count":1,"width":5,"height":4.5,"open":1},
        {"kind":"small-gate","side":"back","count":2,"width":2.7,"height":2.9,"open":1},
        {"kind":"personnel-door","side":"front","count":1,"width":1.2,"height":2.35,"open":1},
        {"kind":"sliding-roof","count":1,"width":5,"depth":5,"open":1}
      ]
    }

Change only the --style input (scifi vs modern): the structure functionality
and actual aperture/collision geometry remain identical. The appearance changes.

    ./build/buildings-qa/aiverse_building_export \
      --style engine/assets/building-styles/scifi.json \
      --recipe engine/assets/building-recipes/multiuse-open.json \
      --out build/buildings-qa/scifi-multiuse.glb

    ./build/buildings-qa/aiverse_building_export \
      --style engine/assets/building-styles/modern.json \
      --recipe engine/assets/building-recipes/multiuse-open.json \
      --out build/buildings-qa/modern-multiuse.glb

## Native game API

CPU implementation: libraries/include/aiverselib/systems/BuildingFeatureGenerator.h.
Runtime API: libraries/include/aiverselib/systems/RuntimeBuildingGenerator.h.
Use generateFunctionalBuilding(style,recipe,origin,yaw) directly, or register
a style with RuntimeBuildingGenerator and call generateRecipe(styleId,recipe).
The latter returns geometry, conservative collision solids, gate anchors and
featureAnchors for doors and the sliding roof. Convert geometry using
engine/buildings/RuntimeBuildingModel.h to Renderer3D Model; GPU upload and
retirement belong to the game render owner.

An independent game build needs neither Studio nor the editor, and styles
can be loaded from packaged asset bytes. The recipe is independent of genres.

## Studio

Building Generator now defaults to Functional properties, with add/remove
controls for kind, wall side, count, aperture width/height/depth and open state.
Legacy silhouette presets remain selectable in the alternative mode. Native
Studio creates the generated .glb and a canonical .glb.recipe.json next to it
under project/content/GeneratedBuildings, then updates the existing Asset
Catalog/.meta/GUID. Browser JS never writes project files directly.

## Testing on macOS

    clang++ -std=c++20 -O1 -Wno-misleading-indentation -I. -Ilibraries/include \
      tests/libraries/building_feature_tests.cpp engine/core/Fixed.cpp \
      engine/data/Json.cpp -o build/buildings-qa/building_feature_tests
    ./build/buildings-qa/building_feature_tests .

The test covers wall collision holes, shutter open/closed, roof open/closed,
cross-style identical geometry, yaw, error validation and runtime recipe API.
Native Studio acceptance also checks GLB export and the saved JSON recipe.
Actual latest C++/TypeScript/Metal verification has NOT yet completed.

## Boundaries / follow-up

This new functional shell is a simple modular building, not yet a full
fusion with the earlier sci-fi armour/curved massing grammar. The old
armoured building presets still have visual doors on closed hulls; do not
claim those are enterable. Open fractions currently generate a new CPU
mesh/collision state; smooth real-time animation needs moving GPU-owned
subnodes and safe physics/navmesh publication rather than regeneration
every frame. Next add windows, loading ramps, hinged/roller shutters,
roof landing pads, service hatches and positional placement per face.


## Exact location and StructureGraph

See docs/building-structure-graph.md. Wall property `position` (metres along wall; -1 auto),
`spacing` (clear gap for repeated opening count) and roof `position` (X) /
`positionZ` (Z) are authoritative. Graph computes structural solids,
collision and stable named sockets from the same layout.


## Stage 12: Windows, room grid and multiple floors

The core FunctionalBuildingRecipe now supports floors (1..8), roomsX and roomsZ
(1..6 per axis, with at most 128 total cells). A floor requires at least 2.8m
nominal vertical clearance and each room cell at least 2.5m width/depth.

New feature kind window is a real exterior wall aperture with a separate 5cm
glazing solid, not a texture on a full wall. Specify feature.floor as the
zero-indexed storey and feature.sill in metres relative to that storey.
Windows have a clear width and height, count, side and exact position.
Glazing gets its own physical collider; windows are not walkable entrances.

BuildingStructureGraph includes room rectangles and horizontal connections.
Room IDs follow room/<floor>/<column>/<row>. Horizontal internal passage IDs
follow passage/x/... and passage/z/...; partition geometry and collision
contain true 1.2m wide door cutouts. The graph is plain C++20 with no
material/renderer/game/editor dependency. RuntimeBuildingResult.structureGraph
exposes this topology from a packaged game build.

Example: engine/assets/building-recipes/two-floor-workshop.json:
24x14x18m, 2 floors, 2x2 rooms per floor, upper- and lower-floor windows,
a vehicle gate, personnel door and sliding roof. The same recipe can be
exported with scifi or modern material style.

Run in the isolated Mac worktree after fetch/switch:

    mkdir -p build/buildings-qa
    clang++ -std=c++20 -Wall -Wextra -Werror -Ilibraries/include \
      tests/libraries/building_structure_graph_tests.cpp \
      -o build/buildings-qa/building_structure_graph_tests
    ./build/buildings-qa/building_structure_graph_tests
    clang++ -std=c++20 -O1 -Wno-misleading-indentation -I. -Ilibraries/include \
      tests/libraries/building_feature_tests.cpp \
      engine/core/Fixed.cpp engine/data/Json.cpp \
      -o build/buildings-qa/building_feature_tests
    ./build/buildings-qa/building_feature_tests .
    python3 tools/buildings/qa_batch_20.py --compile --allow-failures
    ./build/buildings-qa/aiverse_building_export \
      --style engine/assets/building-styles/scifi.json \
      --recipe engine/assets/building-recipes/two-floor-workshop.json \
      --out build/buildings-qa/two-floor-workshop.glb

LIMITS: Floors remain DISCONNECTED vertically. No stairs, elevators, floor-slab
openings, vertically connected navmesh or live physics updates exist yet.
Horizontal window wall-span reservation is shared across floor elevations, so
windows cannot stack at the same wall coordinate on different floors yet.
The room layout is a rectangular grid, not arbitrary floor-plan geometry.
Window glass is static. No engine GPU render screenshots have been verified
for this stage. GitHub focused CI was queued at implementation time.
