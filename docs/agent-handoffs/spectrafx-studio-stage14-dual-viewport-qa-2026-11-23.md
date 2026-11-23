# SpectraStudio Stage 14 — independent Scene/FX viewport QA and lifecycle hardening

Date: 2026-11-23
Repo: chromealex/AIVERSE
Branch: feature/spectrafx-html5-data-interfaces-20261123
PR: #115, draft stacked on parent PR #113
Source HEAD before this handoff: 5f80677af4feb44b151dc2db9652be3dd382288b
Do not modify main or other agents' feature branches.

## User goal

Continue the SpectraFX Graph-native preview work. The previous stage gave
Scene and FX independent CAMetalLayer/Vulkan swapchains and independent
native StudioScene/Renderer3D/GraphRuntime instances, both serialized on
the existing render thread. The user should be able to show the authored
map and particle editor side by side without moving one native view.

## Implemented in this stage

1. `apps/studio-native/tests/StudioViewportLayoutTests.cpp` extends
   the existing platform-neutral test with two simultaneous native
   viewport rectangles: Scene 700×610 CSS at (0,40), FX 620×400 at
   (720,160), device scale 2, and different GPU pixel extents. Hiding
   or resizing FX must not change Scene geometry/visibility.
2. `.github/workflows/spectrafx-native-preview.yml` now compiles
   `aiverse_studio_viewport_layout_tests` together with the actual
   `aiverse_studio` shell, Metal fixture, PNG/JPEG codecs and
   `aiverse_vfx_ops_tests`. The strict CTest filter includes
   `studio_viewport_layout` alongside `studio_spectrafx_preview`,
   `studio_image_codecs`, and `vfx_graph_ops`.
3. `apps/studio-web/src/panels/SpectraDockViewport.tsx` removes the
   obsolete `studio:viewport-reflow` event emitted on FX-panel unmount.
   Scene has its own separate native GPU surface as of Stage 13 and
   must not be unnecessarily repositioned/repainted when FX closes.

## Architecture and ownership

- AIVERSE Studio HTML5 browser owns the panel rectangle and authoring.
  Native shells own separate `StudioSurface::Scene`,
  `StudioSurface::Game` and `StudioSurface::Fx` drawables.
- `StudioHost::tickFx` creates and renders a separate FX-only
  `StudioScene` using the existing native `StudioRenderThread`.
  `StudioHost::runSpectraPreview` is used by BOTH
  `studio.spectraPreview` (web) and `fx_graph_preview` (authenticated
  MCP). No second browser GPU engine, no CPU fake raster, no gameplay
  entity mutation.
- Mac has an independent `StudioFxView` and CAMetalLayer. Windows
  has a third Vulkan-capable child HWND. FX camera events cannot
  select or place map objects. Renderer resources retire on the
  native owner thread at app shutdown.
- Distinct native RHI devices/swapchains are heavyweight: do not
  assume zero extra memory, numerical GPU parity or lifetime safety
  across device loss until measured. No offscreen GPU readback for
  every browser frame is performed.

## Actual tests and status

- **Committed test changes and source review**, but no full AIVERSE
  compiler/build or hardware test run is confirmed for the Stage 14
  SHA. All currently seen GitHub workflows remain queued/pending.
- Previous user-reported macOS CTest 2/2 PASS (Studio image codecs +
  Stage 8 Scene-only SpectraFX preview) is historical and does not
  establish Stage 11/12/13/14 success.
- Hardware Stage 13 fixture already constructs two distinct Metal
  graphics devices, captures Scene and FX frames, asserts >1000
  differing RGB pixels, independent camera and GraphRuntime
  Play/Pause/Step/Seek. It is authored but NOT yet observed PASS.
- The new `studio_viewport_layout` is a platform-neutral geometry
  contract; it does not simulate AppKit/WebView2 actual docking or
  hardware-swapchain failure.

## Next precise work

1. Obtain latest exact HEAD and run on Mac:
   ```sh
   cd ~/Projects/AIVERSE-spectrafx-test
   git status --short
   git fetch origin
   git switch --detach origin/feature/spectrafx-html5-data-interfaces-20261123
   cd apps/studio-web
   npm run typecheck && npm run test:graphs && npm run build
   cd ../..
   cmake -S . -B build/spectrafx-preview \
     -DAIVERSE_BUILD_STUDIO_HTML5=ON -DAIVERSE_BUILD_TESTS=ON \
     -DAIVERSE_BUILD_TOOLS=ON -DAIVERSE_BUILD_EDITOR=OFF \
     -DAIVERSE_FETCH_TEMPLATES=OFF
   cmake --build build/spectrafx-preview -j6 --target \
     aiverse_studio aiverse_studio_spectrafx_preview_tests \
     aiverse_studio_image_codecs_tests \
     aiverse_studio_viewport_layout_tests aiverse_vfx_ops_tests
   ctest --test-dir build/spectrafx-preview \
     -R '^(studio_spectrafx_preview|studio_image_codecs|studio_viewport_layout|vfx_graph_ops)$' \
     --output-on-failure
   ```
2. Run actual AiverseStudioHTML5.app on Mac; open Scene and SpectraFX
   Graph SIDE BY SIDE, Load valid Graph v2 and Play. Capture screenshots
   of simultaneous **distinct** native rendered frames. Verify
   both remain visible when resizing, scrolling, floating FX tab,
   switching projects, focusing, closing the FX tab, and quitting.
3. Fix exact compiler/Metal/Windows WebView2 failures; record actual
   SHA, compile logs and hardware frame captures before QA acceptance.
4. Next functional stage after acceptance: native, bounded particle
   frame capture for MCP/UI diagnostics and GPU-vs-CPU pixel delta
   investigation, without continuous untrusted image streaming.
5. Append a dated handoff for every subsequent stage, documenting
   changed files, API, actual tests/results, current SHA, known gaps
   and next agent step.

