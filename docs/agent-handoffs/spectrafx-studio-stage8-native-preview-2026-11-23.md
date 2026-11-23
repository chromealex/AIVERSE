## Local Apple Metal acceptance reported by tester (2026-11-23)

Tester provided the full final CTest output from
`/Users/aleksandrfeer/Projects/AIVERSE-spectrafx-test/build/spectrafx-preview`:

```text
Start 451: studio_spectrafx_preview
1/1 Test #451: studio_spectrafx_preview ......... Passed 8.19 sec
100% tests passed, 0 tests failed out of 1
EDITOR = 8.19 sec*proc
GPU = 8.19 sec*proc
SpectraFX = 8.19 sec*proc
Total Test time (real) = 8.21 sec
```

This is **user-observed native macOS GPU CTest PASS (1/1)**, not a CI run.
The pasted result does not include `git rev-parse HEAD` or compiler identity,
so exact source SHA and device details are not independently established.
It confirms the fixture completed, including its playback/seek/capture/lifetime
assertions; it does not prove visually distinguishable particle pixels,
GPU-vs-CPU numerical parity, Vulkan support, or absence of nonfatal warnings.
Next request: launch `AiverseStudioHTML5.app`, test the actual Graph v2
panel and visible Scene rendering, then capture a screenshot; record SHA and
additional diagnostics before claiming full acceptance.

## Additional Stage 8 fixes (2026-11-23)

- `c54a39ab508fe695d3d02fa659be772dc3edaa91`: native Pause now also cancels outstanding bounded seek replay/single-step work, so Pause cannot advance visual time after returning.
- `bad66a01e8db7fa0f38aa633369af8bd1ebc8bb5`: real Metal fixture now verifies invalid Graph v2 load leaves the existing preview ready with diagnostic, and explicit Pause cancels a queued seek. Native fixture has not yet been observed executing on this new SHA.
- After these commits, **re-run the exact latest HEAD acceptance**; all prior source QA statuses are historical only. No evidence of completed latest native macOS/Windows/Metal tests yet.

# SpectraFX — Stage 8: native Studio Scene particle preview (2026-11-23)

## Provenance

- Repository: `chromealex/AIVERSE`.
- Branch: `feature/spectrafx-html5-data-interfaces-20261123`, draft stacked PR #115.
- Parent: `feature/spectrafx-significance-data-20261123`, draft PR #113 (must be accepted first).
- Stage implementation commits:
  `42fe6537` render-thread owner/timeline,
  `6d0bdf31` Scene ownership,
  `5e4fd1c4` real GPU/CPU draw before PRISMA,
  `8ac9bc6e` native host bridge,
  `9ececeaa` and `a1a1c53a` links on macOS/Windows,
  `2d180e66` typed web DTO,
  `55eedc56`, `e0451ef2` web controls and styles,
  `5d6eb809`, `efb53471` Graph tab integration and Scene navigation,
  `cd49ae59` Metal test link,
  `d369b422` real Metal acceptance test,
  `31c81431` CTest registration,
  `cfadbe41` macOS acceptance workflow,
  `9cc62e48`, `92e99c17`, `2e740414` camera-target burst and don't hijack Scene camera,
  `9d487b7a`, `16221bf5` bounded step/seek/error recovery.
- **Not yet merged to main.** Parent remains draft. Exact current tested SHA must be retrieved from PR #115 and matched against the CI run and captured images; the above is the source commit history, not proof of passing tests.

## What is implemented in source

1. **Native render owner and real particles.**
   `apps/studio-native/StudioSpectraPreview.h` owns a single `spectra::GraphRuntime` and bounded 60Hz clock. It validates the V2 graph with the established `fx::compileSpectraGraph` BEFORE replacing an active preview. `GraphRuntime` chooses strict eligible Metal compute or the original CPU effect fallback; it never invents a browser-side simulation. A scene source spawns near the current native Scene camera target at Y+1, without stealing the editor camera. The runtime is owned by the Scene render thread and its existing graphics device, not by ECS or the browser.
2. **Safe frame phases.**
   `StudioScene::frame` submits `GraphRuntime::step` only AFTER native `gfx::beginFrame` and BEFORE any render encoder. `GraphRuntime::draw` runs in the open `Renderer3D::render` HDR pass, BEFORE `resolvePrisma`. All resource creation/destruction, reset and parameter updates remain on the render thread. The Scene destructor waits for graphics resource idle then shuts down the VFX runtime before the device/renderer are released. Project switching clears a stale preview; unsupported/unavailable GPU paths take the existing full-effect CPU fallback.
3. **Strict Studio bridge.**
   `StudioHost::handleHostMethod` allowlists `studio.spectraPreview` and uses `onScene` (renderThread invoke between frames); a missing project, hidden/non-drawable native Scene or uninitialized device gets a clear error. No direct `studio.op` or browser path can obtain RHI handles, call gfx methods or bypass native asset validation. The graph payload is bounded to 512 KiB, v2 and native compilable.
4. **Preview commands.**
   `StudioSpectraPreview::command` handles `status|load|play|pause|restart|step|seek|parameter|stop`. Fixed 60Hz ticks, max 3600 (60 seconds), max 8 seek ticks/frame, explicit single-step overrides unfinished seeks. Time/targets, backend, GPU vs CPU, generation, error, and (GPU path) alive counter are returned as native status JSON. The preview is a single emitter instance, not a simulation for every graph output or genre.
5. **HTML5 authoring UI.**
   `apps/studio-web/src/panels/SpectraNativePreview.tsx/.css` adds Open Scene, Load unsaved Graph v2, Play/Pause, Step, Restart, Stop, slider seek and named runtime VM parameters. It polls native status only while active. This is a control panel: the real pixels appear in native Scene view. The web layer shows no fake particle picture; browser demo disables GPU controls. `StudioPanelRenderer` opens the existing Scene panel.
6. **Build and hardware contract.**
   `apps/studio-native/CMakeLists.txt`, `win/StudioWindows.cmake` explicitly link `aiverse_fx` to native Studio targets. `apps/studio-native/tests/StudioSpectraPreviewTests.mm` opens a real Apple Metal CAMetalLayer, loads a graph, advances Play/Pause/Step/Seek, captures native GPU frame, restarts and tears down without leaving a live preview. CTest `studio_spectrafx_preview` is labeled `EDITOR;SpectraFX;GPU`, time limit 90s. `.github/workflows/spectrafx-native-preview.yml` builds Node22 frontend, Metal test fixture and runs strict one-test CTest.

## Validation — executed vs not executed

- **Stage 8 source was committed to GitHub, but no new macOS/Windows native compiler or Metal hardware result has been observed in this chat at the time of writing.** Tests are authored but **NOT confirmed PASS**; do not claim feature or GPU image parity is verified.
- Previous parent PR #113 Ubuntu focused GraphRuntime/contract tests 4/4 PASS applies to SHA `e81a0c6f`, before this native Studio code. Previous UI Stage 5 Node22 workflow 14/14 PASS and Vite applies to an older Stage 5 SHA. Neither proves this stage.
- Full AIVERSE CI previously had independent failures in voxel architecture, GameCore override warnings and Android plugin staging. Do not mutate unrelated systems to hide these; report separately.
- Hardware test currently checks that a real Scene frame is captured with nonempty bytes after the live runtime steps, but **does not yet establish pixel-by-pixel isolation, appearance quality, Metal/CPU equivalence, or Vulkan correctness**. Add a paired no-effect reference capture and particle pixel deltas after smoke test.
- Only the Scene viewport gets real particle pixels. There is no separate offscreen texture shown *inside* the SpectraFX dock panel. That requires a separate native offscreen target and image/texture handshake; never copy the old ImGui viewport as if it were owned by Studio.
- Mandatory fixture commands on macOS:
  ```sh
  cd apps/studio-web && npm install --no-audit --no-fund
  npm run typecheck && npm run test:graphs && npm run build
  cd ../..
  cmake -S . -B build/spectrafx-studio -DCMAKE_BUILD_TESTS=ON -DAIVERSE_BUILD_STUDIO_HTML5=ON
  cmake --build build/spectrafx-studio --target aiverse_studio_spectrafx_preview_tests aiverse_studio -j4
  ctest --test-dir build/spectrafx-studio -R '^studio_spectrafx_preview$' --output-on-failure
  ```

## Known limitations and ownership

- `GraphRuntime` Metal eligibility is conservative and CPU fallback is valid; "Preview loaded" is not proof that the GPU path ran. UI displays the actual backend and GPU flag.
- Named parameters can be changed without recompiling a graph. Any required DataInterface provider missing from the host will reject a burst; Stage 8 does not yet automatically bind PHYSIS/TerraWeave/NEREID immutable snapshots to the GraphRuntime. Emitters depending on required host providers need an explicit next-stage adapter.
- Seek is recomputation from a reset in bounded 8-tick chunks. It is a visual authoring timeline only; it never changes server simulation or authoritative ECS data.
- No in-panel offscreen GPU preview; no alpha-compositing contract across all Vulkan/Metal post-processing targets is proven; no device-loss recovery, GPU fence leak/performance soak or visual quality QA on Stage 8 has been accepted.
- Do not call GPU resource methods inside browser JS, on the main UI thread, or while render/compute encoder is open. Device and `GraphRuntime` lifetimes belong solely to `StudioScene`.

## Next action

1. Read latest exact-SHA CI jobs from PR #115 and fix all actual compile errors, especially C++ StudioScene native linkage, TypeScript DTO and Metal GPU fixture. Do not merge while pending or failing.
2. Run manual macOS WKWebView Scene + SpectraFX side-by-side preview: load unsaved graph, play/pause/step/seek/parameter, switch project, resize, minimize/reopen, close. Capture real pixels to demonstrate visible effects, no fake visuals.
3. Add Vulkan Windows Studio build/renderer fallback checks and a GPU↔CPU parity fixture on the same graph.
4. Add optional native offscreen preview texture for rendering *inside* the SpectraFX panel after capture/retirement ownership is tested; this must use native GPU-safe frame outputs, not JS CPU simulations.
5. Maintain **a new handoff after each following stage** with changed files, APIs, actual test results, branch/SHA, limitations and next step. Preserve existing PRISMA, LENS, MotionWeave, ECS boundaries.
