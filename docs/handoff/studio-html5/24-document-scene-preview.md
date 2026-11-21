# AIVERSE HTML5 Studio — Stage 24: generic document scene visualization

Date: 2026-11-20
Branch: feature/studio-html5
Parent: 7268f9f2482dbc9dcfc0d2407ebb38d17ad7bd71
Stage commit: see branch history.

## Implemented
- Shared StudioDocumentIds.h provides the ID convention for MapEntity/FpsCollider/FpsSpawn. Reused by StudioSession and renderer to avoid divergent UI and picking IDs.
- StudioDocumentVisuals.h is a generic authoring-document -> visual proxy adapter, based on existing EditorSceneNodes/DocumentEntityRef; no ImGui or fabricated runtime ECS entities.
- Metal Renderer3D preview can now draw all listed MapDoc nodes with primitive geometry: placed objects, FPS AABB/rotated colliders, FPS spawns.
- Ray picking uses the same proxy bounds as rendering and returns a typed node ID; native Scene pick updates StudioSession selection and the HTML5 Inspector.
- Oriented colliders use center/half/yaw; conservative rotated AABBs for picking; no GPU readback.
- Single consistent source of identity for native renderer + HTML5 hierarchy, and bounds/translation tests for generated visual proxies.
- Added studio_document_visuals test target and CI one-pass inclusion.

## Files
apps/studio-native/StudioDocumentIds.h
apps/studio-native/StudioDocumentVisuals.h
apps/studio-native/mac/StudioMetalScene.h
apps/studio-native/mac/StudioMetalScene.cpp
apps/studio-native/mac/Main.mm
apps/studio-native/StudioSession.h
apps/studio-native/StudioSession.cpp
apps/studio-native/CMakeLists.txt
apps/studio-native/tests/StudioDocumentVisualsTests.cpp
.github/workflows/studio-html5-web.yml
tools/studio/verify_mac.sh
docs/handoff/studio-html5/24-document-scene-preview.md

## Verification
GitHub source commit created. Native CMake/CTest, Apple Metal screenshot, UI browser build NOT EXECUTED in this environment. CI status must be inspected; no PASS claim.
On Mac: bash tools/studio/verify_mac.sh, then verify picking AABB/rotated colliders, selection, and Inspector coordinates in project UI.

## Known limitations
- These are document authoring proxies, not fully-featured ECS WorldScene. Real model assets, materials/animations, water, planet geometry, and VFX remain to port.
- Up to 2000 proxies currently drawn; index source IDs may change after deletion/reorder.
- Rotated picking uses conservative bounds (may select outside exact collider volume).
- Native Scene camera still uses the current Renderer3D RTSCamera type; generic camera service migration is separate.
- WebView composition experimental, actual macOS validation missing.
- The full old ImGui editor is still needed; do NOT remove until parity.

## Next
Run macOS CMake/CTest and fix compiler errors. Add stable source IDs, generic ECS WorldScene subscription, actual model/material rendering, and finish HTML5 dockable panels.
