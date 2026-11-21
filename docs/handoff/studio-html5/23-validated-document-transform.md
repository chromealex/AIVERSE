# AIVERSE HTML5 Studio — Stage 23: validated document transform authoring

Date: 2026-11-20
Branch: feature/studio-html5
Parent commit: de7ba888af55c258e8186d8ebcbddf8278d8f3af
Stage commit: see branch history.

## Implemented
- Inspector x/y/z for non-map authoring nodes are now editable.
- StudioSession parses the *selected* DocumentEntityRef using the existing editor::enumerateDocumentSceneNodes source adapter. No genre checks are performed in React.
- Non-map position edit occurs in a TEMPORARY MapDoc via editor::setDocumentPosition, preserving collider shapes and spawn formats.
- A candidate FPS document is validated and committed via the existing Workspace::call("fps_set"), which validates the entire candidate simulation before taking one undo snapshot.
- Invalid/unsupported coordinates and no-op edits are rejected or returned without changing history.
- MapEntity type, owner, x/z continue using Workspace::entity_update.
- Expanded scene node contract tests: transformed FPS spawn, AABB collider midpoint and min/max preservation.
- Added studio_scene_nodes to macOS CI build, CTest and tools/studio/verify_mac.sh.

## Changed files
apps/studio-native/StudioSession.cpp
apps/studio-native/tests/StudioSceneNodesTests.cpp
.github/workflows/studio-html5-web.yml
tools/studio/verify_mac.sh
docs/handoff/studio-html5/23-validated-document-transform.md

## Tests and verification
GitHub commit authored and branch updated. TypeScript build, native Mac compilation, CTest and visual tests NOT RUN in this runtime.
macOS CI run must be inspected before reporting validation success. Rebuilding full gameplay scene on each property edit may be costly; benchmark interaction latency.

## Limitations
- Inspector only supports translation for collider/spawn sources, not oriented rotation, scale, bounds, collections, or ECS component editing.
- If fps_set validation rejects a candidate, existing MapDoc remains unchanged and no undo snapshot is added.
- FPS preview cannot yet render these document sources: separate generic scene renderable extraction is needed.
- Nonmap entity IDs still use index ordinals. Stabilize before concurrent edit sessions or reordering.
- Renderer and HTML composition are experimental; old ImGui fallback remains.

## Next
Run macOS CI and fix compiler problems. Then add a generic preview renderable extraction from EditorSceneNodes to show collider/spawn proxies in Renderer3D and pick them via typed source keys. Continue migration of remaining editor windows; handoff per stage.
