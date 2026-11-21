# AIVERSE Studio HTML5 — Stage 18: terrain from real MapDoc

Date: 2026-11-20
Branch: feature/studio-html5
Parent: c67685322266c77d1bc7c22b1cbb43c091bc1b18
Commit: see branch HEAD.

## Implemented
- New standalone StudioTerrainGeometry.h builds CPU triangles from MapDoc grid, terrain corners, fine heights, and levels, with source terrain-kind colors and upward normals. Samples real project data, no fake screenshot or JS bitmap streaming.
- CPU mesh has a 256-by-256 sample cap per dimension and refuses planet mode; unsupported maps keep a visible fallback plane.
- StudioMetalScene caches a GPU terrain mesh per MapDoc revision and project levelHeight, only re-uploads when source changes; frees old geometry during replacement and shutdown.
- Scene entities and picking use the same height interpolation as the preview terrain, instead of the previous mismatched hardcoded 1.0 level height.
- New C++ contract tests cover corner heights, bilinear interpolation, color categories, normals, planet skip, levelHeight and large-map sampling bound.
- New CTest target studio_terrain_geometry.

## Changed files
apps/studio-native/StudioTerrainGeometry.h
apps/studio-native/mac/StudioMetalScene.h
apps/studio-native/mac/StudioMetalScene.cpp
apps/studio-native/mac/Main.mm
apps/studio-native/CMakeLists.txt
apps/studio-native/tests/StudioTerrainGeometryTests.cpp
docs/handoff/studio-html5/18-map-terrain-preview.md

## Validation
Source changes committed. CI and native target compilation still NOT RUN to completion; no Metal image validation.
Run on Mac: bash tools/studio/verify_mac.sh, then build aiverse_studio_terrain_tests and run ctest -R 'studio_terrain_geometry'.

## Known limitations
- This is a colored approximate authoring-surface mesh, not the complete legacy terrain renderer with splat/detail surfaces, decals, assets, plant distributions and water shader.
- Fallback height sampling without corners does not model ramp interpolation perfectly.
- Planet map surfaces are not yet visualized; fallback plane is shown instead.
- Mesh re-upload occurs on any MapDoc revision including entity edits (could optimize with terrain-specific revisions).
- Existing SceneRenderer and UI remain separated; the full scene still requires a genre-neutral scene extraction layer.
- GPU preview and native WKWebView must pass real macOS compile and interactive QA before describing this stage as working.

## Next
- First build native target and fix discovered compilation/link/runtime bugs.
- Reuse SceneRenderable/WorldScene geometry for full lighting and models; expose generic scene entities and real camera service.
- Add automatic incremental terrain updates and visual parity tests.
- Continue one handoff per stage; only remove ImGui when the editor is feature-complete.
