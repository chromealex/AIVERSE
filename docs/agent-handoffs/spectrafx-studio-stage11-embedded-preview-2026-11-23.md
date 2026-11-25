# SpectraStudio Stage 11 — real native FX preview inside HTML5 Graph dock

Date: 2026-11-23
Repository: chromealex/AIVERSE
Branch: feature/spectrafx-html5-data-interfaces-20261123
PR: #115 (draft, stacked on #113)
Code SHA before handoff: 2b7a8ff40cbc89b56e5a5d647392afa4df560df0
Main and all other agent branches remain untouched.

## User evidence and reason for this stage

Mac user provided a screenshot of the working SpectraFX node canvas (published
preview_fx, real native Graph v2 graph, Spawn → Initialize → SpriteOutput, Save/
Publish and existing timeline). They pointed out that rendering only in the
independent Scene tab was confusing: emitters were somewhere on a map and
preview should clearly be embedded in the SpectraFX Graph panel.

## Implemented source

- `apps/studio-native/StudioScene.h/.cpp`: `StudioScene::setSpectraDockMode`
  switches the **existing native GPU Scene renderer** into isolated FX stage
  mode, snapshots/restores the map camera, and renders GraphRuntime particles
  at a stable origin against a neutral dark preview grid. The render-thread
  owned gfx Device is reused; NO second Metal/Vulkan graphics context and NO
  JavaScript particle simulation. The isolated method steps particles before
  render passes, draws them into an active native pass, and resolves using the
  existing native postfx. `resetSpectraDockCamera` provides a reproducible
  camera origin independent of the map.
- `apps/studio-native/StudioHost.h/.cpp`: the existing
  `studio.viewportRect` contract accepts `id:"fx"` and moves the **same
  Scene native surface** over the FX panel's HTML bounding rect, with FX
  priority when Scene and FX are both visible. FX ownership is released when
  hidden; fallback Scene reports its DOM rect again. New Scene instances
  inherit the already requested FX mode before first GPU frame. Closing a
  floating native surface restores the map camera.
- `apps/studio-web/src/panels/SpectraDockViewport.tsx/.css`: a real embedded
  native GPU presentation rectangle. Uses ResizeObserver and scroll/overlay/
  drag/visibility changes, `viewportRectChanged`, and native IPC. On unmount
  hides the native surface and requests Scene viewport reflow. No HTML
  canvas or synthetic particles.
- `apps/studio-web/src/panels/spectraViewportModel.ts`:
  browser-geometry crop against the scrollable graph pane; hides native
  surface when scrolled too far out so it cannot cover unrelated HTML UI.
  DPR is clamped to 1..4.
- `apps/studio-web/src/panels/SpectraNativePreview.tsx/.css`,
  `SpectraDataInterfacesPanel.tsx`, and `StudioPanelRenderer.tsx`:
  replaces the previous Open Scene workflow with the embedded native GPU
  viewport and its Play/Pause/Step/Seek/Load/Stop controls.
- `apps/studio-web/src/components/ViewportPlaceholder.tsx`: listens for
  `studio:viewport-reflow`, restoring the ordinary Scene surface if the FX
  Graph tab closes while Scene is still visible.
- Native editor input isolation:
  `StudioHost::sceneMouse`, `sceneKey`, `viewportInput`,
  `placeAsset` and `placeType` cannot pick/select/delete/paint/drop objects
  into the game map while FX preview owns the native Scene layer. Pointer
  and keyboard camera controls remain enabled.

## Tests added or changed

- `apps/studio-web/tests/spectraViewportModel.test.mjs` — 4 new pure
  Node22 Graph-pane clipping/visibility/DPR guards, registered in
  `npm run test:graphs`.
- `apps/studio-native/tests/StudioSpectraPreviewTests.mm` — the actual
  Apple CAMetalLayer hardware CTest now enters native SpectraDockMode before
  playback and validates restoration of the map camera on exit. The same
  existing `studio_spectrafx_preview` target is used for GPU acceptance.

## Actual validation

- All code and test changes were committed through connected GitHub.
- **NO actual macOS AppleClang compile, WKWebView visual screenshot, or Metal
  CTest of THIS Stage 11 source has yet been seen.** GitHub PR #115 checks
  were queued and older commits cancel while the branch advances. Do not call
  this feature tested PASS merely because the earlier Stage 8 Metal CTest
  passed 2/2 with image codecs before these changes.
- GPU/CPU parity, background/alpha capture and Windows WebView2/Vulkan
  ownership tests remain unverified.
- Known limitation: this initial implementation moves the original Scene
  native surface into the FX editor; it does not support independent
  concurrent visible Scene and FX render outputs in two windows. FX has
  priority; when FX closes, Scene reflow restores the view.
  If true simultaneous rendering is later required, use a tested distinct
  native offscreen target with resource-fence retirement, never a browser
  GPU context or CPU screenshot streaming.

## Next step

1. On Mac checkout latest draft PR #115; build Node22 web bundle then
   aiverse_studio and aiverse_studio_spectrafx_preview_tests. Run
   `npm run typecheck && npm run test:graphs && npm run build`,
   `ctest --test-dir build/spectrafx-preview
   -R '^(studio_spectrafx_preview|studio_image_codecs)$'
   --output-on-failure`.
2. In actual WKWebView open SpectraFX Graph with a compiled v2 asset;
   confirm live Metal pixels appear INSIDE Graph pane, orbited camera
   works, map objects are never edited by clicks, hover and scroll hide
   the native layer correctly, tab close restores Scene.
3. Capture real screenshots, check source SHA and error logs. Fix compiler
   errors before accepting the stage.
4. Next stage: common native preview commands through `aiversemcp`
   and camera control, followed by render pixel-delta verification.

All following feature stages require separate dated handoff and status.
