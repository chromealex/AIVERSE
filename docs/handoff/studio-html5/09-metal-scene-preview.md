# AIVERSE Studio HTML5 — Stage 9: native Metal scene preview

Date: 2026-11-20
Branch: feature/studio-html5
Parent: 1b98130c9dacb1e5a4b470ddaa742cddc29f6059
Commit: see this stage's branch HEAD.

## Implemented
- Native NSView using CAMetalLayer placed as a sibling OVER WKWebView, limited to DOM viewport bounds from Stage 8.
- AIVERSE's own gfx::createMetalDevice + Renderer3D render directly to that layer (no CPU readback, no JS image copies).
- Renders a simplified real MapDoc visualization: ground plane, grid and up to 2,000 placed entities at real x/z positions, colored by owner.
- Orbit drag and scroll zoom feed Renderer3D camera; 60Hz timer on the main runloop with frame culling while hidden/minimized.
- Renderer and session are both main-thread owned; GPU resources released before layer teardown.
- Added pure viewportPlacement C++ unit test target and linked the new app with existing aiverse_runtime object sources.

## Files
apps/studio-native/CMakeLists.txt
apps/studio-native/StudioViewportLayout.h
apps/studio-native/mac/Main.mm
apps/studio-native/mac/StudioMetalScene.h
apps/studio-native/mac/StudioMetalScene.cpp
docs/handoff/studio-html5/09-metal-scene-preview.md

## Validation
GitHub source commit verified. No Apple SDK/Mac binary in tool runtime; macOS CMake build, on-device layer composite and image parity NOT TESTED.
UI typecheck/Vite build NOT TESTED.
Run the Stage 3 macOS commands, and:
    cmake --build build/studio-html5 --target aiverse_studio_viewport_layout_tests --parallel 4
    ctest --test-dir build/studio-html5 -R 'studio_(session|viewport_layout)' --output-on-failure

## Critical limitations
- A **working native renderer is IMPLEMENTED IN SOURCE but not confirmed by a macOS run**. Do not call this feature complete until build, visual and input tests pass.
- This is NOT full SceneWindow parity: no native world scene/lights/terrain/water/VFX/models/materials/animation, no scene entity picking/gizmos, no multi-camera or Game viewport.
- The metal view sits ABOVE the webview (sibling), so HTML popovers overlapping the scene are occluded. A proper compositor or transparency strategy must be designed and tested.
- Scene preview draws the MapDoc primitive representation only, capped at 2,000 objects; flat maps only. It is NOT a performance benchmark of the main renderer nor evidence for other backends.
- The experimental Mac host includes aiverse_runtime (which transitively contains ImGui object code), although no ImGui is called by the new Studio files. Full removal is later.
- WebView2/Vulkan and WebKitGTK/GLES hosts are not implemented.
- Window backing scale changes after moving between monitors require explicit remeasure or viewDidChangeBackingProperties handling.

## Next stage
- Run macOS build and fix warnings/errors; add actual Scene runtime data adapter instead of primitive map proxy.
- Complete event routing and proper DOM/native overlay composition.
- Integrate selected entity identity, picking and gizmos.
- Add controls to enable/downgrade native renderer; test on Mac.
- Transfer a new handoff after every stage. Only remove production ImGui after parity.
