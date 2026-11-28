# Review fix: shared planar minimap raster — handoff (2026-11-28)

Branch `review/minimap-shared-raster`, based on `studio-only-editor` e158b5a45, code commit `ea51f08ec`.
Area: client HUD minimap (`apps/client/Minimap.cpp`), Studio overlay preview (`apps/studio-native/StudioOverlayPlan.h`),
shared header `engine/view/game/PlanarMinimap.h`.

## Problem (found in review of `studio-only-editor`)

`PlanarMinimap.h` was copied from an older client minimap. Against the current client it:
- ignored `minimap.bounds` (camera / rectangle) and `cameraFrame`, so the Studio preview showed the whole map;
- indexed `rules.types[o.type]` without the client's range guard;
- ignored `view.minimap: false` on a type;
- read `grid.index(-1, …)` for a 0×0 map.

## Change

`PlanarMinimap.h` is the single owner: `resolveLayout(map, manifest, w, h)`, `build(grid, layout, rgba, iw, ih)`,
`rasterize(raster, grid, rules, scene, vision, marks, viewQuad, pixels)`. The client `Minimap` keeps its GPU texture
and planet path and delegates the planar raster; Studio uses the same calls with `map.extra` and the project manifest.

## Verification (actually run)

- g++ 13 `-fsyntax-only`: `apps/client/Minimap.cpp`, `MinimapPlanet.cpp`, `ClientUi.cpp`, a TU including
  `StudioOverlayPlan.h`, `tests/tests.cpp`.
- Standalone build of the new `planar_minimap_shared_bounds_and_scene_guards` checks with the real `Json`,
  `MapGrid`, `RoutesData`: pass.
- NOT RUN: CMake build, `./run-tests.sh --quick` (merged-tree Mac gate), `apps/studio-native/tests/overlay-plan`.

## Next

Owner of `studio-only-editor`: merge or cherry-pick `ea51f08ec`, run the Mac quick gate and the overlay-plan test.
