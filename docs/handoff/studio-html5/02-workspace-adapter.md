# AIVERSE Studio HTML5 — Stage 2: ImGui-free Workspace adapter

Date: 2026-11-20
Branch: feature/studio-html5
Parent commit: 6b2b1bffb89ed72ecb054543cf49fb3107f590f2
Stage commit: see current branch history.

## Implemented
- StudioSession lives in apps/studio-native/; compiles without ImGui headers.
- studio.snapshot reads actual open Workspace MapDoc entity list, project, dirty flag and last 100 activity logs.
- studio.select validates temporary entity:N index and returns the refreshed snapshot.
- studio.setProperty accepts only type, x, z, owner; routes updates through Workspace::call("entity_update") with existing undo/history/validation and propagates errors.
- No generic arbitrary Workspace/MCP execution exposed through the UI bridge.
- Headless contract tests authored in apps/studio-native/tests/StudioSessionTests.cpp.

## Changed files
apps/studio-native/StudioSession.h
apps/studio-native/StudioSession.cpp
apps/studio-native/tests/StudioSessionTests.cpp
docs/handoff/studio-html5/02-workspace-adapter.md

## Tests
Headless C++ tests: authored, NOT RUN (full AIVERSE source tree/compilation is not available in the connector runtime).
Native macOS app: NOT BUILT; integrating native WKWebView and CMake is the next stage.
Existing ImGui editor: untouched in this stage.

## Limits
- Only authoring MapDoc entities are visible; no live ECS World, prefab hierarchy or camera viewport yet.
- entity:N IDs are transient and can refer to a different entity after delete/reorder. Introduce stable identifiers before multi-window workflows.
- Saving, undo/redo commands, subscriptions and project picker pending.
- This adapter must be invoked on the owner/UI thread, not arbitrary WebKit worker threads.

## Next stage
Add opt-in WKWebView host + CMake target to serve built apps/studio-web/dist files, validate main-frame origins and postMessage methods, connect StudioSession, create a test target, run build/tests on macOS. Keep the existing editor as fallback.
