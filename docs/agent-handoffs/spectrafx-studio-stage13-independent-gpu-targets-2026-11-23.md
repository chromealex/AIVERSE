# SpectraStudio Stage 13 — independent native GPU Scene + FX render targets

Date: 2026-11-23
Repository: chromealex/AIVERSE
Branch: feature/spectrafx-html5-data-interfaces-20261123
Pull request: #115 (draft, stacked on parent draft #113)
Source HEAD immediately BEFORE this handoff: a10da76547a58c96db7ae23bb47c3939dfbb5bae
This handoff commit advances HEAD; always check exact branch SHA before asserting QA.
Main and other agents' branches untouched.

## Goal and user context

The user wants the SpectraFX Graph editor to show particle preview inside
its own dock and the normal Scene panel to be usable simultaneously.
Stage 11 moved the ONLY Scene GPU surface into the Graph's rectangle (with
FX priority); it could not show independent live images in both docks.
Stage 13 removes that shared-surface limitation.

## Implemented architecture

- `StudioSurface` enum now has `Scene`, `Game`, **`Fx`**.
  `StudioHostPlatform::createDevice`, `surfaceDrawable` and
  `placeSurface` are selected through this existing typed enum; no
  untrusted JS path or device pointer enters the browser.
- `StudioHost.h/.cpp` adds `fxPreview_` (second `StudioScene`), native
  `fxPlacement_`, and `tickFx()`. Both `preview_` (authored map Scene)
  and `fxPreview_` (isolated particle editor) have **independent gfx
  Devices, native drawables/swapchains, Renderer3D and cameras**.
  They run sequentially on the **same existing render-owner thread**,
  with one `StudioRenderThread` and no duplicate ECS simulation.
  Native `StudioScene::setSpectraDockMode(true)` turns the FX-owned Scene
  into an isolated dark-stage particle renderer before its first frame.
  `tickFx()` runs even if the authored Scene panel is hidden; the regular
  Scene independently renders when shown. Both can be visible at once.
  The FX Preview action bridge and MCP callback target `fxPreview_`;
  map Scene screenshot/selection/GI/other engine panels still target
  `preview_`. `shutdown` retires FX then Scene on their owner thread
  before the thread and GPU surfaces die. FX failure retires the FX Scene
  alone; Scene failure does not forcibly destroy FX.
- `apps/studio-native/mac/Main.mm`: adds `StudioFxView` (new
  CAMetalLayer, camera-only input) independent from `viewport_`
  (Scene) and `gameViewport_` (Game). `MacPlatform::setLayers` binds
  three native layer pointers; `createDevice(Fx)` instantiates a Metal
  device for the FX drawable. On visibility, resize and floating-window
  reparent the FX layer follows the Graph dock only; layer drawableSize
  is updated via existing render-thread boundary.
  ALL pointer release, key and drag/drop handlers on `StudioFxView` are
  isolated from authored map entity selection/deletion/placement.
- `apps/studio-native/win/Main.cpp`: Win32 creates a third native child
  surface; its `VulkanWindow` is independent. `index(Fx)==2`,
  floating-window detach loops all three, and WM_* commands in
  `fxInput` only orbit/zoom the FX camera, never route into Scene
  editing/asset drops/keyboard shortcuts.
- `apps/studio-web/src/panels/SpectraDockViewport.tsx/.css`,
  `SpectraNativePreview.tsx`: still uses the existing HTML5
  `studio.viewportRect {id:"fx"}` and Play/Seek/etc., but now that
  rectangle controls an **independent** native surface. Browser does
  not render simulated particles or transmit frame bitmaps.
- `apps/studio-native/StudioScene.cpp`: Stage 11's
  `frameSpectraDock()` already draws a separate neutral preview
  stage through real native `spectra::GraphRuntime`, using the
  appropriate device and renderer, and preserves its own preview camera.
  Stage 13 uses this on a dedicated `StudioScene` instance, so the
  authored map Scene never enters FX mode or lends its device.

## API and ownership

- Native UI: `studio.viewportRect {id:"fx",x,y,width,height,dpr,visible}`.
- Playback: `studio.spectraPreview` and the authenticated shared
  MCP `fx_graph_preview` operation (status/load/play/pause/step/seek/
  restart/parameter/stop/cameraReset/cameraOrbit/cameraZoom).
- Native graphics resources, GPU frame lifecycle and particle simulation
  belong solely to the Studio render thread. Browser only places the
  native surface and sends bounded commands. The two drawable devices
  use distinct native RHI resources; no image transfer between them.
- Scene and FX cameras/selection/history are separate. No authoritative
  gameplay/ECS write occurs from preview controls. No ImGui dependencies.

## Modified files in this stage

- `apps/studio-native/StudioHost.h`
- `apps/studio-native/StudioHost.cpp`
- `apps/studio-native/mac/Main.mm`
- `apps/studio-native/win/Main.cpp`
- `apps/studio-native/tests/StudioSpectraPreviewTests.mm`
- `apps/studio-web/src/panels/SpectraDockViewport.tsx`
- `apps/studio-web/src/panels/SpectraDockViewport.css`
- `apps/studio-web/src/panels/SpectraNativePreview.tsx`
- `.github/workflows/spectrafx-native-preview.yml`

## Tests / evidence — do not overstate

- Source changes committed through GitHub connector. Static review of
  platform surface dispatch, application window allocation and owner
  thread calls carried out. **Full AppleClang Studio shell build,
  actual two-device Metal CTest, Windows Vulkan build and WKWebView
  screenshot HAVE NOT BEEN EXECUTED/CONFIRMED for this Stage 13 SHA**.
- Enhanced `studio_spectrafx_preview` real Metal fixture now constructs
  TWO gfx::Devices on distinct CAMetalLayers, creates an authored map
  `StudioScene` and separate FX `StudioScene`, submits multiple frames
  to both, checks independent camera state, captures both GPU frames,
  and rejects identical images (>1000 differing pixels required). It
  retains earlier Play/Pause/Step/Seek/invalid graph/Stop assertions.
  This fixture is authored, not yet observed as PASS.
- `.github/workflows/spectrafx-native-preview.yml` now builds **the
  actual macOS `aiverse_studio` shell**, the Metal fixture,
  `aiverse_studio_image_codecs_tests` and
  `aiverse_vfx_ops_tests`; runs strict CTest for
  `studio_spectrafx_preview`, `studio_image_codecs` and
  `vfx_graph_ops`. GitHub workflow remained queued at handoff time.
- Historical Mac user test `studio_image_codecs` and earlier
  Scene-only `studio_spectrafx_preview` returned 2/2 PASS, **but this
  predates independent devices and does NOT establish Stage 13 PASS**.
- Potential risks to resolve through native QA: device/resource cache
  separation with two gfx::Devices on one owner thread; simultaneous
  swapchain visibility, multiple CAMetalLayers in the same window,
  Vulkan child resize/retirement, overlay scroll/clipping, separate
  framebuffer captures, CPU/GPU parity and cross-window focus.
- No performance measurement or memory budget claim on Stage 13.

## Next exact steps — macOS smoke and visual validation

```sh
cd ~/Projects/AIVERSE-spectrafx-test
git status --short
git fetch origin
git switch --detach origin/feature/spectrafx-html5-data-interfaces-20261123
cd apps/studio-web
npm run typecheck && npm run test:graphs && npm run build
cd ../..
cmake -S . -B build/spectrafx-preview \
    -DAIVERSE_BUILD_STUDIO_HTML5=ON \
    -DAIVERSE_BUILD_TESTS=ON \
    -DAIVERSE_BUILD_TOOLS=ON \
    -DAIVERSE_BUILD_EDITOR=OFF \
    -DAIVERSE_FETCH_TEMPLATES=OFF
cmake --build build/spectrafx-preview -j6 \
    --target aiverse_studio aiverse_studio_spectrafx_preview_tests \
             aiverse_studio_image_codecs_tests aiverse_vfx_ops_tests
ctest --test-dir build/spectrafx-preview \
    -R '^(studio_spectrafx_preview|studio_image_codecs|vfx_graph_ops)$' \
    --output-on-failure
```

Then open the built AiverseStudioHTML5.app; dock Scene and SpectraFX
Graph SIDE BY SIDE, Load Graph v2 and Play. Verify both remain visible
with their OWN cameras, captures, GPU backend labels and different
images. Drag within FX must not modify map; scroll the panel, float it,
switch projects and close the app. Screenshot both simultaneous views.
If compiler/CMake/Metal/WebView fails, collect the *first* error and
repair it at source. Avoid disabling diagnostics globally.

Windows native Vulkan hardware + WebView2 acceptance, independent
device loss, long soak, GPU/CPU parity and memory budget need future
tests. Maintain a separate dated handoff for every later feature.
