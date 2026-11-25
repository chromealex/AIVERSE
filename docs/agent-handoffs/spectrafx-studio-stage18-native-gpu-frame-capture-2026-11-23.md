# SpectraStudio Stage 18 — native bounded one-shot VFX frame capture

Date: 2026-11-23
Repository: chromealex/AIVERSE
Branch: feature/spectrafx-html5-data-interfaces-20261123
Pull request: #115 (draft; stacked on parent draft #113)
Source HEAD **before** this handoff: e412a65e5bd79eb18a66233d99a40278c8ee0364
The handoff commit advances HEAD; always retrieve exact test SHA.
Main and other agents' worktrees/branches unchanged.

## Goal

Add a real one-shot snapshot of the SpectraFX Graph native GPU viewport.
The user wants further features before batch validation; **no tests or builds
are run in this stage**. Captures must be real RHI output, not browser
particle emulation or unrestricted streaming of desktop pixels.

## Implemented and ownership

- `apps/studio-native/StudioHost.h/.cpp`:
  `StudioHost::captureSpectraFrame` is a limited async GPU readback
  over the **dedicated FX** `StudioScene` and its `gfx::Device`.
  The command `{"action":"capture","maxSide":512}` schedules
  `fxPreview_->captureNextFrame` on the Studio render-owner thread.
  The RHI completion callback may occur on a different thread; it owns
  a `std::shared_ptr<FxShot>` and writes JPEG + error/size under
  `std::mutex`. StudioHost polls completed capture via
  `{"action":"captureStatus"}` on the UI thread. No IPC call
  waits for a GPU completion while holding the render thread; no
  React code owns GPU handles.
- The capture is explicitly **one-shot**. Source dimensions are bounded
  to ≤8192×8192 and ≤64 MiB of RGBA. The snapshot is resampled to
  a maximum dimension of 128–768 px (default 512), then encoded using
  the already-established Studio stb JPEG codec at quality 80.
  Returned JPEG bytes are capped at 600 KiB before base64 encoding,
  leaving room under the existing Studio 1 MiB JSON bridge limit.
  The `captureStatus` response has a `capture` envelope
  `{pending,ready,width,height,mime:"image/jpeg",image?,error?}`
  plus the normal live playback state.
- A second capture while one is pending is rejected; a pending
  callback is considered timed out after 10s. The callback retains
  only its own shared state and cannot dereference a destroyed host.
  Starting to open a different project, detaching the FX viewport,
  or shutting down Studio detaches any prior image. The image is
  **never** saved into the project or filesystem by the host.
- `apps/editor/core/SpectraGraphOps.cpp`: existing authenticated
  `aiversemcp` `fx_graph_preview` tool now exposes
  `capture {maxSide:128..768}` and `captureStatus`; it uses the same
  `StudioHost::runSpectraPreview` callback as the Graph UI.
  Headless MCP without a native GPU viewport continues to report
  explicit unavailability rather than faking an image.
- `apps/studio-web/src/types.ts`: extends the actual
  `StudioSpectraPreviewState` with optional `capture` status and
  the typed capture operations.
- `apps/studio-web/src/panels/SpectraNativePreview.tsx/.css`:
  adds a Snapshot size selector (256/512/768px), Capture Frame
  button, bounded 350ms polling only while a capture is pending,
  inline genuine JPEG thumbnail and browser `download` action.
  Displays native capture errors, not synthetic fallback images.
  Auto-reload waits while capture is pending.
- All output remains presentation-only: never changes authored
  Graph v2 files, server ECS, Scene selection, gameplay rules,
  game-map camera or the independent map Scene swapchain.

## Known limitations and security

- This is a single JPEG **screenshot of the native FX drawable**.
  JPEG intentionally discards alpha and is lossy. It does not prove
  FX image quality, CPU/GPU parity, alpha coverage or HDR precision.
  Those require separate real hardware measurements and lossless
  capture artifacts. No continuous frame streaming is added.
- GPU completion callback is async and can finish after the UI
  cancels; `shared_ptr<FxShot>` prevents writing through a
  dangling `StudioHost`. No untrusted output path is accepted.
- Frame capture needs an initialized, visible, drawable FX viewport,
  not the original map Scene viewport. One capture at a time.
- HTML5 download is based solely on an actual native JPEG data URI;
  playback and authoring remain separate.

## Tests executed and results

**NONE on the new Stage 18 source**, per the user's instruction to
implement features now and test later. No AppleClang, Windows Vulkan,
WKWebView, UI TypeScript/Vite, CTest, device loss, GPU readback
success or exact rendered pixels have been verified.

Historical user-reported Mac CTest `studio_image_codecs` and the
earlier Scene-only `studio_spectrafx_preview` passed 2/2 before the
new independent GPU view and this capture protocol; **they do not
validate Stage 18**.

## Mandatory next agent steps

1. Preserve the Stage 18 code and prevent duplicate capture systems.
2. Continue feature development if the user still wants tests postponed.
   Natural next feature: A/B snapshot comparison with explicit
   before/after user capture, difference ratio (CPU-side inspection
   of *actual* captured pixels), and export. Avoid falsely calling
   it GPU/CPU parity unless two real native backends were captured.
3. When user requests QA, compile latest SHA:
   `npm run typecheck && npm run test:graphs && npm run build`,
   `cmake --build build/spectrafx-preview -j6 --target aiverse_studio
   aiverse_studio_spectrafx_preview_tests aiverse_vfx_ops_tests`,
   `ctest --test-dir build/spectrafx-preview
   -R '^(studio_spectrafx_preview|vfx_graph_ops|studio_image_codecs|studio_viewport_layout)$'
   --output-on-failure`.
4. Add actual Mac Metal capture lifecycle tests: frame requested,
   callback completes, dimensions/data URI within bounds, timeout,
   concurrent request rejection, callback after tab closes and after
   project switches. Check PNG/JPEG alpha expectations. Windows
   WebView2/Vulkan and dual Scene/FX screenshot QA follow.
5. Every further stage requires another handoff with exact source SHA,
   changed files, **tests actually run** and explicit limitations.
