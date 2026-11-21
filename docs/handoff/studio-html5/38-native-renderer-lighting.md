# AIVERSE Studio HTML5 — Stage 38: authored lights in native Renderer3D preview

Date: 2026-11-20
Branch: feature/studio-html5
Parent: 6246bb6f2e7be8bce3cbff6c38732ed721131826
Stage commit: see branch history.

## Implemented
- Added backend-neutral read-only StudioAuthoredLights adapter, converting authored MapDoc lights or FPS JSON lights to the real render::PointLight data model.
- Projection supports x/z/height, range, intensity, linear color, halo, nightOnly and spot cone parameters, with finite/bounded input and max LightSystem::kMaxLights; reuses the standard Renderer3D LightSystem (Metal host today, reusable for Vulkan/GLES later).
- Added a small pure StudioLightPreviewMath.h to approximate sun intensity, sky ambience, cloud coverage and night factor for editor previews.
- StudioMetalScene now submits authored lights to renderer_.lights between beginScene and render; daylight/cloud parameters also update the renderer when a lighting block is present.
- Small wireframe light gizmos show light positions in the native view, without CPU frame readback.
- Registered native document-light projection and pure daylight math tests in CMake, Linux boundary checks and macOS CI.

## Files
apps/studio-native/StudioLightPreviewMath.h
apps/studio-native/StudioAuthoredLights.h
apps/studio-native/tests/StudioLightPreviewMathTests.cpp
apps/studio-native/tests/StudioAuthoredLightsTests.cpp
apps/studio-native/mac/StudioMetalScene.cpp
apps/studio-native/CMakeLists.txt
.github/workflows/studio-html5-web.yml
tools/studio/verify_mac.sh
docs/handoff/studio-html5/38-native-renderer-lighting.md

## Tests actually performed
- C++17 light preview math test compiled and run on Linux with -Wall -Wextra -Werror -pedantic: PASS.
- StudioAuthoredLights tests require full AIVERSE editor core; authored but NOT RUN locally.
- Native Metal/WebView graphical effects, macOS full build, Vulkan/GLES screenshots NOT RUN.
- GitHub Actions status not yet confirmed as passing.

## Limitations
- Approximate daytime/ambient/clouds; not full production RtsLights cycle, shadow/reflections/flicker/power/network lighting.
- No full material/terrain/VFX parity with legacy SceneWindow.
- Point lights are limited to LightSystem::kMaxLights and selected bounded authoring sources; GPU backend controls actual quality.
- In FPS, authored lights are saved in FPS map JSON (Stage 37), but runtime gameplay module support for these sources is a separate task.
- This is a native preview code path, not proven screenshot/render performance.

## Next
Run macOS CI and capture actual Metal frame with edited lights. Compare editor sky/spot/color/halo to production renderer, then extend scene extraction and port more Inspector panes. Keep Handoff per stage and do not remove ImGui yet.
