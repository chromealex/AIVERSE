# StructureGraph and exact-placement functional building recipes

## Goal

AIVERSE building functionality is compositional and genre-neutral. A recipe
describes large gates, small gates, personnel doors and an optional retractable
roof; style selects *only* visual materials. This stage adds deterministic
exact placement without breaking automatically packed old recipes.

## New schema

Every wall aperture supports:
- `side`: `front | back | left | right`.
- `position`: centre in **metres** along the wall; negative -1 means
  automatic. Front/back coordinate axis is local +X, left/right is local +Z.
- `count`: openings of the same kind; the requested `position` is the
  centre of the **whole group**.
- `spacing`: clear pier gap between repeats, minimum 0.35 m and maximum 8 m.
- `width` and `height`: clear aperture dimensions, not frame dimensions.
- `open`: 0 closed, 1 fully retracted. Partial states include door-leaf solids.

The sliding roof uses `position` (X) and `positionZ` (Z) as the
requested clear-aperture centre. Both default to -1 (centre automatically).
It cannot retract a panel outside the final building envelope.

Example configuration (see the sample under engine/assets/building-recipes):

```json
{
  "width":24,"height":12,"depth":18,"seed":7,
  "features":[
    {"kind":"large-gate","side":"front","count":1,
     "position":7,"width":5,"height":4.5,"open":1},
    {"kind":"small-gate","side":"back","count":2,
     "position":12,"spacing":1,"width":2.7,"height":2.9,"open":1},
    {"kind":"personnel-door","side":"front","count":1,
     "position":18,"width":1.2,"height":2.35,"open":1},
    {"kind":"sliding-roof","position":13,"positionZ":10,
     "width":5,"depth":5,"open":1}
  ]
}
```

## StructureGraph source-of-truth

`BuildingStructureGraph.h` introduces `BuildingStructureGraph` with:
- `solids`: exact structural volume pieces, labelled
  `Floor/Wall/Trim/DoorLeaf/Roof/RoofPanel` (local metres).
- `sockets`: named semantic connections
  `entrance/<feature-index>/<repeat-index>`, `roof/<feature-index>`.
- `error`: invalid positions, overlaps or lack of structural pier space.

`buildFunctionalStructureGraph(recipe)` is **pure CPU** and does not need
any style/material, Studio, renderer, filesystem or genre module. It first
reserves explicitly positioned apertures, sorts the wall's occupied spans,
then packs automatic apertures into available spans. Legacy recipes with
no exact positions retain evenly spaced layout. Invalid/overlapping requests
fail rather than shifting an explicitly positioned socket.

`generateFunctionalBuilding(style, recipe)` now consumes this *same graph*
to build both mesh and coarse collision. The graph is returned in local
building coordinates as `FunctionalBuildingResult::graph`, while
`anchors` and `building.gates` are transformed to world coordinates by
`origin/yaw`. `BuildingFeatureAnchor::socketId` remains stable even if
sorted geometry changes draw order. No hidden continuous hull exists behind
wall openings, because the physical wall solids are graph-produced segments.

This is the P0 **single-story functional shell foundation**. The structure
graph does not yet support arbitrary multi-floor room networks, structural
load simulation, or all ornamental massings. Do not claim GPU articulated
door animation: open fraction regenerates CPU geometry, while runtime
animation, physics scene updates and navmesh remain later integrations.

## Editor and persistence

Studio's Building Generator exposes exact centre, roof X/Z and grouped gap;
its native bridge persists these fields in a canonical GLB.recipe.json next
to the generated model. Setting/style choice does not modify the recipe's
functional layout. Existing legacy preset mode remains supported.

## QA

`tests/libraries/building_feature_tests.cpp` checks graph/mesh collision
counts, unique socket IDs, actual centres for the 24×12×18 recipe, mixed
manual/automatic placement, rejection of overlaps, and rejection of roofs
that cannot safely retract. `tests/studio/BuildingGenerationTests.cpp`
checks round-trip persistence of exact position/spacing.

To run locally on Mac:

```sh
cd ~/Projects/AIVERSE-buildings-qa
git fetch origin feature/aiv20261123_building-grammar
git status --short
git switch --detach origin/feature/aiv20261123_building-grammar
mkdir -p build/buildings-qa
clang++ -std=c++20 -O1 -Wno-misleading-indentation \
  -I. -Ilibraries/include \
  tests/libraries/building_feature_tests.cpp \
  engine/core/Fixed.cpp engine/data/Json.cpp \
  -o build/buildings-qa/building_feature_tests
./build/buildings-qa/building_feature_tests .
```

C++/Metal/macOS test results on this revision are not yet verified by
completed CI. A prior user-run of legacy 20 sized presets was 19/20:
factory 16×9×11 seed 3 failed; **not** proof of failure in the new graph path.


Stage 12: see docs/building-functional-recipes.md for multi-floor room grid, window aperture and collider semantics.


Stage 13: see docs/building-functional-recipes.md for stairs/elevator
shaft voids, vertical room edges and the runtime/navmesh integration limits.
