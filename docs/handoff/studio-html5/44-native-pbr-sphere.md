# AIVERSE HTML5 Studio — Stage 44: actual native GPU PBR sphere preview

Date: 2026-11-20
Branch: feature/studio-html5
Parent commit: adb3a4099a8929efaf9a37d6a781cec21b06e67b
Stage commit: see HEAD.

## Implemented
- Extended native StudioMetalScene to lazily create a procedural sphere via existing AIVERSE MeshBuilder -> modelFromParts -> prepareModelUpload/uploadModel path from tests/render/HdrScene.h.
- The sphere is drawn using the native renderer's **ModelDraw / Renderer3D::drawModel** PBR path (not fake CSS/Canvas3D or CPU readback). It uses the validated resolved material factors selected by Stage 43.
- Preview mode replaces the Scene camera/terrain view with a lit sphere, one light and ground plane; orbit/zoom still use the normal native viewport input path.
- Changes to preview materials update the model material on a versioned revision; markMaterialChanged() invalidates renderer cached material signatures. GPU model resources released before Renderer3D shutdown.
- Changed model projection helper to use properly qualified render::float3.
- Added Preview in Scene / Exit PBR preview button to HTML5 Materials (disabled if there is a pending local draft, because unsaved edits must never be represented as saved GPU state).
- Native material.preview event raises Scene tab through the existing nested-dock API and refreshes snapshot; preview selection survives tabs and can be cleared.
- No new ImGui calls or renderer backend assumptions in the PBR material adapter; macOS is current host.

## Files
apps/studio-native/StudioPbrMaterialPreview.h
apps/studio-native/StudioSession.cpp
apps/studio-native/mac/StudioMetalScene.h
apps/studio-native/mac/StudioMetalScene.cpp
apps/studio-native/mac/Main.mm
apps/studio-web/src/types.ts
apps/studio-web/src/panels/MaterialsPanel.tsx
apps/studio-web/src/panels/StudioPanelRenderer.tsx
apps/studio-web/src/App.tsx
apps/studio-web/src/panels/MaterialsPanel.css
docs/handoff/studio-html5/44-native-pbr-sphere.md

## Verification
- Source changes committed to the separate feature branch.
- PBR CPU projection CTest from Stage 43 authored but has not completed in CI.
- Native Metal drawing and React/Vite bundle NOT BUILT/TESTED in the current Linux-only connector environment; a working GPU frame is **not** confirmed.
- Stage 43/44 should not be merged into main until a macOS compile, image parity smoke and input test pass.

## Limits
- Actual GPU PBR shader renders only scalar factors/base/emissive color/alpha; shader graphs, textures and image maps are intentionally excluded.
- The model preview is shown inside Scene View, not a second composited Materials Viewport. It temporarily replaces the Scene camera and its grid.
- No live sphere-surface material textures or PBR comparison metrics; no Vulkan/GLES platform host yet.
- Retaining a GPU model across project switches is intentional but stale revisions must be monitored by macOS QA.
- Normal Scene View remains the fallback; legacy ImGui editor is still needed for non-migrated functions.

## Next
- Run a full macOS CMake build, CTests and an actual screenshot of both Scene / PBR sphere. Fix compile/Metal errors.
- Expose a real native Materials dock viewport if separate dual-camera surfaces become needed.
- Expand safe material transforms/textures and Shader Graph editing; keep one handoff per stage.
