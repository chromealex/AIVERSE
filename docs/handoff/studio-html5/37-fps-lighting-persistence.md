# AIVERSE HTML5 Studio — Stage 37: FPS Lighting persistence parity

Date: 2026-11-20
Branch: feature/studio-html5
Parent: 5fd6115a6c9a6aeaf53c11955d086b2da3eb7ccf
Stage commit: see branch HEAD.

## Root cause
The existing LightOps mutates MapDoc.data.extra, but FPS maps serialize MapDoc.data.fps via MapDoc::toJson(). Editing lights in the new HTML5 panel on an FPS project would therefore not persist after Save. This stage fixes the Studio path without changing shared LightOps.

## Implemented
- New StudioFpsLightingDocument.h builds modified copies of the FPS JSON document, reads all authored lights/lighting and updates allowlisted fields.
- Stage 35 StudioSession Lighting methods now branch for FPS maps. They create a candidate FPS document and commit it only through existing Workspace::call("fps_set"), which validates the complete gameplay setup and records one MapDoc Undo snapshot. On validation failure, no partial mutation is committed.
- Existing authored spawns, colliders and other unknown FPS JSON keys remain unchanged by lighting operations.
- Strict integer index validation is now applied to both FPS and non-FPS updates/deletions (no truncating 0.5 to index 0).
- Added pure authorization contract and document round-trip tests, registered in CMake/CTest and Linux/macOS CI. The pure authorization C++17 tests were built and run on Linux.

## Changed files
apps/studio-native/StudioFpsLightingContract.h
apps/studio-native/StudioFpsLightingDocument.h
apps/studio-native/tests/StudioFpsLightingContractTests.cpp
apps/studio-native/tests/StudioFpsLightingDocumentTests.cpp
apps/studio-native/StudioSession.cpp
apps/studio-native/CMakeLists.txt
.github/workflows/studio-html5-web.yml
tools/studio/verify_mac.sh
docs/handoff/studio-html5/37-fps-lighting-persistence.md

## Tests actually run
- Pure StudioFpsLightingContractTests compiled with C++17 -Wall -Wextra -Werror -pedantic on Linux: PASS.
- Full FPS JSON round-trip test requires AIVERSE editor core linking; authored but NOT RUN locally.
- Mac native WebView/Metal and full TypeScript app build: NOT RUN; GitHub Actions status pending.

## Limitations
- fps_set has a full gameplay setup validation; frequent property edits may be slow. Benchmark latency before production.
- The FPS gameplay module may not yet consume the stored Lighting block, although it survives the FPS map document round-trip. Rendering parity is a separate task.
- Native real-time GPU lighting preview and materials are still incomplete.
- Preserve legacy ImGui editor until complete feature/visual parity.

## Next
Run macOS CI including fps_lighting_document; fix compile/load failure. Then reflect authored lights in native Renderer3D's LightSystem and confirm updated scene appearance, FPS persistence and Undo with loaded projects. Handoff per stage.
