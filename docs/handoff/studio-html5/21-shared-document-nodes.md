# AIVERSE HTML5 Studio — Stage 21: shared authoring scene nodes

Date: 2026-11-20
Branch: feature/studio-html5
Parent commit: 4a443b37840c1723c9451338a8f183e4a97203a0
Stage commit: see branch history.

## Implemented
- HTML5 native Workspace snapshot now reuses the existing editor::enumerateDocumentSceneNodes() adapter in apps/editor/EditorSceneNodes.h, rather than inventing a separate map-only hierarchy.
- Generic scene IDs: entity:N, collider:N, spawn:N; labels and positions come from the existing document adapter.
- FPS collider and spawn coordinates appear in the Inspector as explicitly **read-only** fields. No unvalidated FPS writes or fake ECS components.
- Selection validates membership in the adapter's enumerated source IDs.
- MapDoc entity properties continue to use Workspace::call("entity_update") for authoring/Undo.
- Added headless unit test for shared document source enumeration (map entity, FPS collider/spawn), registered as studio_scene_nodes CTest.
- React disables read-only Inspector inputs via optional StudioProperty.editable.

## Changed files
apps/studio-native/StudioSession.cpp
apps/studio-web/src/types.ts
apps/studio-web/src/App.tsx
apps/studio-native/CMakeLists.txt
apps/studio-native/tests/StudioSceneNodesTests.cpp
docs/handoff/studio-html5/21-shared-document-nodes.md

## Validation
GitHub commit created. C++/TypeScript builds and CTest NOT RUN; macOS Metal window NOT VERIFIED.
Compile on Mac via tools/studio/verify_mac.sh and add new studio_scene_nodes target to acceptance runs.

## Important limitations
- These are authored *document* objects, not the live ECS World/Simulation hierarchy; do not infer runtime entity identity or component presence.
- Cross-genre document sources are supported by the native adapter; HTML5 presentation remains genre-neutral.
- FPS collider/spawn editing is intentionally disabled pending a validated, undoable native Workspace operation.
- Native preview rendering still supports map entities, not full FPS/planet geometry.
- IDs using N indices are not stable across removal/reordering; next stages must introduce stable refs.
- Existing ImGui editor remains the production fallback until verified parity.

## Next
1. Run native CI and fix errors. Include studio_scene_nodes in scripted tests.
2. Add validated FPS document writes using the existing document position adapter with correct undo/redo.
3. Bring real ECS WorldScene and asset/model extraction into the native Metal viewport.
4. Keep handoff per stage, no unverified PASS claims.
