# AIVERSE Studio HTML5 — Stage 35: validated native Lighting bridge

Date: 2026-11-20
Branch: feature/studio-html5
Parent commit: 5abd40098f0e68d8544b8bd2cbb9d5897fd02663
Stage commit: see current branch HEAD

## Implemented
- Added allowlisted, validated native lighting operations in StudioSession: studio.lighting, studio.lightingSet, studio.lightAdd, studio.lightUpdate and studio.lightDelete.
- All mutations call existing Workspace operations light_list, lighting_set, light_add, light_update and light_delete, preserving MapDoc beginEdit/Undo and native data validation.
- Browser cannot supply arbitrary operation names or filesystem paths. Read/update/delete require an open project; index bounds are checked before light updates.
- Pure StudioLightingValidation.h enforces finite coordinates, scalar bounds, supported string enumerations and boolean fields. This is a C++ authorization boundary, not JS-only validation.
- New lights start at map center in flat maps or origin for FPS. The browser does not pick arbitrary paths or execute generic Workspace tools.
- Registered headless Linux and macOS CMake/CTest Lighting validation checks, plus negative no-project StudioSession scenarios.

## Files
apps/studio-native/StudioLightingValidation.h
apps/studio-native/tests/StudioLightingValidationTests.cpp
apps/studio-native/StudioSession.cpp
apps/studio-native/tests/StudioSessionTests.cpp
apps/studio-native/CMakeLists.txt
.github/workflows/studio-html5-web.yml
tools/studio/verify_mac.sh
docs/handoff/studio-html5/35-native-lighting-bridge.md

## Tests actually performed
- Compiled StudioLightingValidationTests.cpp locally on Linux with C++17, -Wall -Wextra -Werror -pedantic: **PASS**.
- Full native macOS aiverse_studio build / CTest: NOT RUN. GitHub Actions status pending.
- UI LightingPanel is next stage; no claim of end-to-end functionality yet.

## Limitations
- These APIs operate on the open MapDoc's authoring lights, not live ECS lighting data or the full Scene Renderer; real-time preview still requires integration.
- UI supports no arbitrary C++ operation dispatch, and the operation set intentionally excludes file editing.
- Native LightOps verifies several properties itself but allows other schema options; the Studio bridge deliberately exposes only a strict subset.
- Light-list size and synchronous map scanning may affect large projects; monitor performance.
- Stage 36: implement React Lighting panel on top of these typed methods; add project dirty refresh notification, tests and another handoff.
