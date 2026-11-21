# AIVERSE Studio HTML5 — Stage 10: Scene selection to Inspector

Date: 2026-11-20
Branch: feature/studio-html5
Parent commit: ce7d9a109830ce98ac75ee08daa169314c7ea75e
Stage commit: see branch history.

## Implemented
- Fixed Stage 9 compile blocker: StudioSession now exposes const Workspace accessor (view state only) and validated selectedEntityIndex().
- Native metal preview now highlights selected MapDoc primitive entities using yellow bounds overlay.
- Native viewport click casts a screen ray through Renderer3D's RTSCamera and reuses existing genre-independent pickSceneRenderable slab-test from apps/editor/SceneRenderable.h.
- Emits selection.changed to WKWebView after successful StudioSession studio.select command. React already refreshes the snapshot when this event arrives.
- Orbit drag and scroll zoom continue to operate without writing project data.
- Added headless selectedEntityIndex empty-state assertion.

## Files
apps/studio-native/StudioSession.h
apps/studio-native/StudioSession.cpp
apps/studio-native/mac/StudioMetalScene.h
apps/studio-native/mac/StudioMetalScene.cpp
apps/studio-native/mac/Main.mm
apps/studio-native/tests/StudioSessionTests.cpp
docs/handoff/studio-html5/10-native-selection.md

## Tests
GitHub source changes committed. No C++/macOS/TypeScript tests run in this connector environment. A build on macOS is mandatory before asserting the native app works.
Use Stage 3 CMake build commands plus the studio_session and studio_viewport_layout CTests.

## Current limitations
- Picking uses simplified 1.1x1.2x1.1 proxy boxes; detailed mesh triangle picking and transforms not present.
- No rectangle/multi-selection or gizmo.
- Legacy RTSCamera type is still used inside Renderer3D; new API remains genre-neutral, but camera naming cleanup will require coordination with camera agent.
- Draw limit 2,000 map entities, flat maps only; no true world ECS hierarchy, assets or visual parity yet.
- NSView overlay still occludes HTML popovers overlapping the native viewport; needs compositor policy.
- Stable entity IDs not yet implemented; entity:index can change after deletion.
- New Studio files do not call ImGui, but linking aiverse_runtime still pulls shared ImGui support transitively.
- Stage 9 and 10 must compile/test on actual macOS before production.

## Next
- Verify CMake compilation and TypeScript build with CI or local Mac; fix any code/link failures.
- Split scene adapters from legacy editor into common C++ genre-independent library and connect all ECS/prefabs/assets.
- Implement native camera transform/gizmos and GPU picking across multiple viewports.
- Introduce a compositing policy with HTML overlays and native scene views.
- Handoff after each step.
