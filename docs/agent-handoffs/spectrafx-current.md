## 2026-11-23 — SpectraFX Studio Stages 15–17 (features only; tests postponed)

Added production-native preview-authoring controls to the independent
FX viewport and existing `aiversemcp` `fx_graph_preview` tool:

- **Stage 15**: `playback {speed:0.25|0.5|1|2|4,loop:boolean}`
  uses whole fixed 1/60s simulation ticks, bounded quarter-tick
  accumulator and native-frame restart at 3600 ticks. `stage`
  selects `background:dark|neutral|light|black` and boolean grid/axes;
  `cameraPreset` uses perspective/front/side/top with a separate FX
  camera. All supported through the HTML5 Graph pane and MCP.
- **Stage 16**: opt-in `Auto-reload graph` recompiles an immutable
  in-memory Graph v2 after 650ms debounce. Save Draft/Publish remain
  explicit; native compiler rejects invalid candidate graphs without
  replacing last-good state. Successful recompilation restores Play.
  Native backend initialization failure is a known separate hazard,
  not a proven atomic swap.
- **Stage 17**: `burst {count:1..8}` queues bounded native one-shot
  emitter events (max 32 queued), `clear` defers particle reset to
  the next native device frame. HTML5 Trigger Burst/Clear controls
  and real burst/queue counters; no gameplay/ECS writes.

Docs:
`docs/agent-handoffs/spectrafx-studio-stage15-playback-stage-2026-11-23.md`,
`docs/agent-handoffs/spectrafx-studio-stage16-auto-reload-2026-11-23.md`,
`docs/agent-handoffs/spectrafx-studio-stage17-burst-events-2026-11-23.md`.

**User deliberately deferred ALL testing until later.** No exact-SHA
TypeScript/Vite/AppleClang/Metal/Windows/Vulkan/interactive GUI PASS
has been observed for Stages 15–17. Do not describe the source
implementation as hardware-verified. Prior 2/2 Mac CTest PASS belongs
to an older Scene-only preview.

## 2026-11-23 — Stage 14: dual Scene/FX viewport QA hardening

The real native Scene and SpectraFX editor now use two separate GPU
drawables and independent cameras (Stage 13), both serialized on the
existing native render thread. Stage 14 extends
`StudioViewportLayoutTests.cpp` to verify two simultaneous independent
rectangles, DPR, FX hide/resize without mutating Scene geometry.
The focused macOS CI includes
`aiverse_studio_viewport_layout_tests` and CTest
`studio_viewport_layout` in addition to native Metal, codec and
Workspace/MCP tests. `SpectraDockViewport.tsx` no longer dispatches
the obsolete shared-surface reflow event when FX closes, preventing
unnecessary authored Scene redraws.
**Exact-SHA AppleClang/Metal, Node/Vite, Windows and dual live
WKWebView screenshot QA remain unverified.** Detailed handoff:
`docs/agent-handoffs/spectrafx-studio-stage14-dual-viewport-qa-2026-11-23.md`.

## 2026-11-23 — Stage 13: independent Scene and FX GPU swapchains

The SpectraFX Graph native GPU preview **NO LONGER BORROWS OR
REPOSITIONS the map Scene drawable**. StudioHost now uses three
`StudioSurface` values: `Scene`, `Game` and `Fx`. The native
`Fx` viewport owns its own `StudioScene`, renderer, camera, gfx Device
and native CAMetalLayer (macOS) / Win32 Vulkan child window (Windows).
Both map Scene and FX are framed on the **same render-owner thread**
using separate swapchains and can be displayed simultaneously.
`studio.viewportRect {id:"fx"}` places ONLY the FX surface. The
browser never draws fake particles or receives GPU handles.
`studio.spectraPreview` and `aiversemcp fx_graph_preview` remain
the shared playback/camera/parameter control surface, but target
`fxPreview_` rather than the authored Scene. No native map brush,
selection, asset drop, keyboard deletion or ECS mutation is permitted
from the dedicated FX window. Independent map Scene camera/history
are untouched. Both renderer/device lifetimes retire on the native
owner thread.

This supersedes Stage 11's **one shared native surface** limitation
without replacing the established Stage 11 HTML5 dock rectangle.
Metal CTest source now creates two real devices and captures different
GPU frames; macOS Studio CI also compiles the full native host.
**Full AppleClang/Metal, Windows Vulkan and WKWebView UI acceptance
not yet observed as PASS.** Required agent handoff:
`docs/agent-handoffs/spectrafx-studio-stage13-independent-gpu-targets-2026-11-23.md`.


## 2026-11-23 — SpectraStudio embedded native FX preview + MCP (Stages 11–12)

The latest HTML5 SpectraFX Graph panel includes a **real embedded native
Metal/Vulkan particle viewport**: `SpectraDockViewport.tsx` reports its
scroll-clipped CSS rect as `studio.viewportRect {id:"fx"}`. StudioHost
repositions its existing Scene GPU surface with FX priority and switches
StudioScene into a neutral grid stage with isolated camera, preserving the
authored map camera and blocking all Scene entity edits. The real
`spectra::GraphRuntime` executes on the render thread, then draws inside
native GPU render passes. Scene and FX share one surface; concurrent
independent rendering is NOT supported in this initial stage.

`fx_graph_preview` is now in the existing `aiversemcp` tools/list,
sharing a render-thread callback with `studio.spectraPreview`.
Supports status/load/play/pause/step/seek/restart/parameter/stop plus
cameraReset/cameraOrbit/cameraZoom. `load` accepts safe graphName,
`draft:true`, or inline Graph v2. All earlier `fx_graph_*` asset CRUD,
editor and draft MCP tools remain unchanged. Standalone MCP reports no
native GPU viewport and does not fake frames.
Node clipping tests, native Metal isolated dock tests and MCP discovery,
safe path/callback tests were AUTHORED. No exact-SHA AppleClang/Metal/
HTML5/WebView/Windows PASS has been observed for these latest changes;
the user's prior native CTest 2/2 PASS predates the embedded viewport.

Handoffs:
`docs/agent-handoffs/spectrafx-studio-stage11-embedded-preview-2026-11-23.md`
`docs/agent-handoffs/spectrafx-studio-stage12-mcp-camera-2026-11-23.md`

## 2026-11-23 — Stage 8 native SpectraFX Scene preview (source/QA pending)

On draft stacked PR #115, `StudioSpectraPreview.h` now wraps one real
`spectra::GraphRuntime` owned by StudioScene's native render thread.
Allowlisted `studio.spectraPreview` host operations load/validate an
unsaved Graph v2 and control Play/Pause/Restart/Step/Seek, finite named
VM parameters and actual backend status. Native GPU/CPU particle
simulation steps between beginFrame and the scene pass, then draws
inside the real open Renderer3D HDR pass before PRISMA resolves.
The HTML5 SpectraFX tab has real native Scene preview controls, not
fake DOM particles; open Scene to see the effects. No standalone
offscreen preview picture in the graph panel yet.

**Stage 8 exact-source native Studio build/Metal acceptance NOT confirmed**.
The macOS CAMetalLayer regression target and CI were authored, but
queued run is not a PASS. See mandatory handoff
`docs/agent-handoffs/spectrafx-studio-stage8-native-preview-2026-11-23.md`
for changed files, commits, test plan, risks and next step.
Parent PR #113 remains draft; main unchanged.

## 2026-11-23 — HTML5 SpectraStudio Graph authoring Stage 7 (draft PR #115)

The `fx_graph` dock tab now includes three new authoring surfaces:
(1) typed Data Interface param/provider inspector with native Assets picker,
(2) Graph v2 topology canvas with native Add/Delete/Connect/Disconnect, and
(3) Curve/Gradient key editor for existing v2 data nodes with normalized
times, scalar keys, RGBA opacity and static visual plots. These are
**editor authoring interfaces, NOT actual GPU particle preview frames**.
All graph Validate/Save Draft/Publish actions still route via whitelisted
Studio Workspace operations. No direct JS filesystem write or renderer
ownership. Source lives on `feature/spectrafx-html5-data-interfaces-20261123`,
stacked on parent draft PR #113. Main and `feature/studio-html5` unchanged.

Handoff chain:
- `docs/agent-handoffs/spectrafx-studio-stage5-2026-11-23.md`
- `docs/agent-handoffs/spectrafx-studio-stage6-graph-canvas-2026-11-23.md`
- `docs/agent-handoffs/spectrafx-studio-stage7-curve-gradient-2026-11-23.md`

**Acceptance:** prior Stage 5 early Ubuntu Node22 suite PASS 14/14 plus Vite;
that evidence predates Graph Canvas and Curve/Gradient. New Node/Vite
checks are queued; real macOS/Windows UI, hardware RHI and full SpectraFX
GPU preview remain unverified. Next: resolve exact-SHA CI first, then
design native render-owned preview mailbox/timeline with safe fences and
real image capture. Do not merge either draft PR until real validation.

## 2026-11-23 — Graph v2 Data Interfaces, Stage 4 (PR #113, QA pending)

New typed `dataInterfaces` bindings + runtime-only DataInterface node map
immutable host-provided scalar samples into existing VM `param0..param7`
at burst time. Editor model has shared node metadata and atomic binding edit
helpers; HTML5 Studio GUI integration remains outstanding. Source changes
and tests are on `feature/spectrafx-significance-data-20261123`, not main.
**No native PASS recorded for Stage 4 yet.** CI is queued. Must run focused
Linux/macOS compiler+runtime CTests, Metal parity and full acceptance before
marking done. Canonical handoff:
`docs/agent-handoffs/spectrafx-data-interfaces-stage4-2026-11-23.md`.
Previous stages:
`docs/agent-handoffs/spectrafx-significance-stage1-2026-11-23.md`,
`docs/agent-handoffs/spectrafx-significance-stage2-routes-2026-11-23.md`,
`docs/agent-handoffs/spectrafx-fields-stage3-2026-11-23.md`.

## 2026-11-20 — Inspector revalidated with lighting main

Combined implementation `4acfb482742d2bb1d615c3eaae60308af09ffa1b` preserves incoming main `d84e958b1`.
Editor build PASS; SpectraFX 20/20 PASS (16.78s), five real Metal checks with
no skips; actual PRISMA frame review 5/5 PASS. Two unused GraphWindow helpers
removed; cleanup build has zero warnings. Full repository/platform/manual UI
coverage remains unrun. Exact commands and integration decisions: `docs/agent-handoffs/inspector-spectrafx-lighting-sync-2026-11-20.md`.


## 2026-11-20 — Inspector graph-v2 integration with current main

Implementation `9db6d2014137e41cd67ff82dc187cf1136f73bb3`; branch `feature/aiv20261120_perf_inspector_integration`.
Editor build PASS; final SpectraFX 20/20 PASS including five hardware Metal tests;
actual PRISMA frame checks 5/5 PASS. Source-compatible view adapters and GPU
preview retirement corrections preserve newer main ownership. No experimental
defaults enabled. Full repo/Release/Vulkan/GLES/manual editor UI NOT RUN.
Exact commands, earlier failures, warnings, APIs and next steps:
`docs/agent-handoffs/inspector-spectrafx-main-2026-11-20.md`. Separate documentation commit follows implementation.


# SpectraFX — handoff for the next implementation agent

## 2026-11-21 packaged client build selection

On `feature/studio-html5`, implementation `7d92031a3` adds opt-in `fx` to
`packaging.engineModules`; omitting it from an explicit packaged-client list
excludes the client FX host and SpectraFX libraries. The editor-hosted client
and default legacy client retain full FX. macOS client builds with FX ON/OFF,
the hosted client build and the packager build passed; WebAssembly and GPU
visual parity were NOT RUN. See
`docs/agent-handoffs/spectrafx-stage121-client-build-pruning-2026-11-21.md`
for exact measurements and risks.

## Stage 120 — merged main + SpectraFX full native acceptance PASS (M3 Ultra)

**Validated merged code SHA:**
`70c9aa666f0bdaafc1f8169636b68fb7e4e437ba`
in a **clean dedicated Mac Studio worktree**. Full build and
full CTest stage exit 0; focused SpectraFX **15/15 PASS**,
real MetalGPU **4/4 PASS** with no skipped/missing cases,
seven-frame visual **5/5 PASS**; **all 10/10 stages exit 0**,
failed stages 0, skipped stages 0.
The user excerpt does **not** provide the total full CTest count,
so do not reuse old `128/128` from pre-merge.
RHI 512×256 stress PASS, `retire_remaining=0`,
p50=0.1006ms/p95=0.8632ms (whole small test GPU frame);
device-wide Metal allocation 1472/1728/1664 KiB.

The Stage 119 local CMake conflict was eliminated by checking out
the actual clean remote SHA; no engine source hotfix was needed.
Documentation handoff:
`docs/agent-handoffs/spectrafx-stage120-main-integration-native-pass-2026-11-20.md`.


## Stage 119 — local Mac CMake conflict is absent from remote integration (QA pending)

Mac reports `CMakeLists.txt:623 <<<<<<<` and whitespace
in `AGENTS.md` + Texture RHI files. Source inspection at integration
`5682c60360c29b9fc0484f4a19a9fac8d53407d5` shows
**no CMake conflict markers**; remote CMake line 623 is
`target_compile_options(native_app_glue PRIVATE -w)`.
`AGENTS.md` matches main byte-for-byte; named texture files
do not exist on either remote branch. This is a local checkout/ref
mismatch or uncommitted worktree contamination, NOT a verified source
error in this SpectraFX integration commit. Avoid destructive cleanup;
create a **new clean dedicated worktree**, verify exact SHA/blob
hashes, then run CMake and full acceptance. No engine source changed.
See
`docs/agent-handoffs/spectrafx-stage119-local-checkout-isolation-2026-11-20.md`.


## Stage 118 — integration Markdown trailing whitespace cleanup (Mac QA pending)

Integration QA found Git `diff --check` trailing whitespace
errors on SpectraFX handoff metadata lines. Stage 118 cleaned
**23 added Markdown handoffs (134 spaces)**; all code/RHI/CMake
and other agent files were preserved. Additional scans of newly
added SpectraFX sources and modified integration paths found
no other newly introduced whitespace. This is a docs-only fix
based on Stage 117's two-parent merge.
**Mac `git diff --check` and native acceptance must be rerun on
this updated integration commit.** Handoff:
`docs/agent-handoffs/spectrafx-stage118-whitespace-2026-11-20.md`.


## Stage 117 — main merged into isolated integration branch (NATIVE NOT RUN)

**Merge SHA** `7ef385060956b73e131821e66e3a8b515da6ac32`
on `integration/spectrafx-main-sync-20261120` has both
parents: SpectraFX `ba4b126fd533cdbcb8443f46a68c6d8afe76867f`
and main `e4129c5062a6ab47e7c70f3fdd483392fde1f679`.
Three-way merge preserved **77 new files, 5 unchanged-main edits,
6 concurrent edited files**, resolving one overlapping
`apps/fps/client.cpp::shutdown` line preserving TerraWeave timing
detach **and** SpectraFX shutdown.
Static tree review: expected 88 changed files vs main, no foreign
paths; two parents confirmed. **No native Mac build/CTest for merge**.
Read
`docs/agent-handoffs/spectrafx-stage117-main-merge-2026-11-20.md`
and run final QA in a NEW dedicated worktree before updating the
original feature or main.

## Stage 116 — pre-merge Apple M3 Ultra acceptance PASS

User native evidence at SHA
`ba4b126fd533cdbcb8443f46a68c6d8afe76867f`:
**128/128 full CTests, 13/13 SpectraFX, 4/4 real MetalGPU,
5/5 visual pixel assertions**, all 10 acceptance stages exit 0,
failed 0/skipped 0, Editor integration/full builds PASS.
The prior `dt` scope regression is fixed. These results apply
**only to the pre-main-merge SHA**.
Read `docs/agent-handoffs/spectrafx-stage116-editor-native-acceptance-2026-11-20.md`.


## Stage 115 — focused Editor/FPS/RTS native rebuild PASS (reported)

User reports successful Mac Studio focused build of
`aiverse_editor`, `aiverse_fps_client` and `aiverse_client`
after the `dt` scope fix `fc08834e1744a0a50d272043167515a635b70565`.
The complete compile log/SHA was not pasted.
**Full build and full CTest on the fixed source are still pending**;
last earlier full acceptance on the broken SHA failed.
Next: full strict `tools/spectrafx/acceptance.sh` with Metal
and capture enabled. See Stage 115 handoff.


## Stage 115 — Stage 114 Editor build regression: C++ dt scope fixed (NOT RETESTED)

User tested SHA `c5dd5914edac64f8071f4bd6a5ffd8eb5e6353e3`:
focused SpectraFX/4 MetalGPU/visual review PASS, **host integration
build exit 2, full build exit 2, full CTest SKIP** (failed stages 2,
skipped 1). Overall full acceptance therefore **FAILED**.

Source inspection found an unambiguous Editor compiler defect:
`drawPreview` declared `const float dt` inside a nested valid-JSON
branch but later called `previewHost_.prepareCpu(dt,view)` outside
its scope. Fixed in code commit
`fc08834e1744a0a50d272043167515a635b70565` by defining shared
`float dt=0` in the outer function scope and assigning it inside
the valid branch. The **exact compile log was not provided**; other
compiler errors remain possible. **No native recompilation has run on
the fix SHA**. First rebuild the Editor/FPS/RTS targets, then run
full acceptance if they pass.
Read `docs/agent-handoffs/spectrafx-stage115-editor-dt-build-fix-2026-11-20.md`.


## Stage 114 follow-up — exact-source Clang policy test PASS (Mac GUI NOT RUN)

Code `9c387ed9559b07196e40814ad753d212db248115` introduces the
Editor Preview policy, plus fixes
`1d21a12591012696740034011f3672b0efca4190` (invalid RT guard and
defer Step until successful dispatch) and
`04f775a97ed0f8b06dd47bed489e6d387b6a2fa4` (regression test).
**Pure CPU policy standalone Clang -Werror test PASS** in the local
container. Exact header/test file Git blob SHAs matched committed GitHub
source before execution. Acceptance core build now includes that target.
**Full native CMake/Metal and actual Editor interactions NOT RUN** for
Stage 114. Latest hardware-accepted code is still Stage 113's earlier SHA.
See `docs/agent-handoffs/spectrafx-stage114-editor-preview-controls-2026-11-20.md`.


## Stage 114 — Editor Preview state correctness and deterministic policy test (NOT RUN)

Pure new `SpectraFxGpuPreviewPolicy` controls Pause/Step, nonfinite
or long UI-clock deltas, revision caching, explicit Restart and
retry after failed emitter dispatch. Editor GPU Assets window no
longer marks failed GPU/CPU Play as successfully applied; switching
assets with identical authored JSON restarts preview. A new desktop
`spectrafx_preview_policy` CTest covers these state transitions.
No changes to genre-neutral `engine/fx`, RHI or PRISMA.
**Stage 114 source authored but not yet native CMake/CTest/UI tested**;
the previous 10/10 acceptance is tied to Stage 113's SHA.
Handoff:
`docs/agent-handoffs/spectrafx-stage114-editor-preview-controls-2026-11-20.md`.


## Stage 113 — actual screenshot + Metal timing inspection complete

User uploaded `spectrafx-visual-review.ppm` and `ctest_metal.log`.
The 532×400 contact sheet was **visually inspected**: camera A
blue box occludes the center of a red/orange FX ring, camera B shifts
box/FX projection, post-reset image has no residual particle, PRISMA
shows layered HDR/Bloom arcs. Tiny 128×128 fixtures are not proof of
production artistic quality.

From the actual MetalGPU log on Apple M3 Ultra: GPU timing
**requested=512 scheduled=512 valid=512 invalid=0**,
**p50=0.0849 ms p95=0.3166 ms** (whole small test frame).
`MTLDevice.currentAllocatedSize` **1472 → peak 1728 → after 1664 KiB**
(+192 KiB after vs baseline), **device-wide allocation only**,
not proof of GPU leak or physical residency. Stress **512×256 PASS**,
`peak_nominal_kib=36`, `retire_remaining=0 gpu_healthy=1`.
Four native MetalGPU tests PASS on validated engine SHA
`441d69e08836888604e0aee6ec71765a51fddddc`.
Source unchanged. Remaining: interactive Editor QA, memory stability and
perceptual alpha overlap.
Full handoff:
`docs/agent-handoffs/spectrafx-stage113-visual-gpu-profile-2026-11-20.md`.


## Stage 112 — Stage 111 strict native + visual review PASS (M3 Ultra)

User-provided acceptance at validated source SHA
`441d69e08836888604e0aee6ec71765a51fddddc`:
**10/10 stages exit 0**, failed 0, skipped 0;
MetalGPU **4/4 real PASS**, SKIP=0, MISSING=0, FAIL=0.
Seven PPMs were successfully analyzed; all **5/5 objective visual
checks PASS**, changed-pixel counts A/B/PRISMA **192/322/232**.
The excerpt does **not** include the full CTest test count, numeric
GPU p50/p95 or allocated Metal bytes; do not invent these values.
PPM/JSON artifacts are on the user's Mac, not visually inspected here.
See `docs/agent-handoffs/spectrafx-stage112-native-visual-acceptance-2026-11-20.md`.


## Stage 111 — GPU timing + Metal allocation telemetry and seven-frame visual QA (NOT RUN)

Code `3e56816e991f304bbe4ebe0fe1abc8505523e4d8`
adds GPU timestamp p50/p95 and device-wide Metal allocation tracking
to the real 512x256 churn fixture (no production RHI changes).
Code `65426ea8b551e3a33713109a43c17e990375cd1f` adds a
stdlib PPM contact sheet + JSON visual comparison tool and CTest.
Fix `e5aa28f843f7459afa8803a60e4c53d4b12cecc0` makes
the synthetic camera centroid fixture satisfy strict QA.
Acceptance now runs a separate `visual_review` stage when PPM capture
flag is supplied. **New Python/CTest/Metal code NOT yet executed**.
Last native green source is Stage 110 `b06c6d0f4dc820455d9c7b25dc98aa33397adca7`.
Read `docs/agent-handoffs/spectrafx-stage111-visual-perf-2026-11-20.md`.


## Stage 110 — full native Stage 106–109 acceptance PASS on M3 Ultra

**Validated source:** `b06c6d0f4dc820455d9c7b25dc98aa33397adca7`.
User-provided real Darwin full acceptance: **9/9 stages exit 0**,
**126/126 full CTest PASS**, **11/11 SpectraFX PASS**,
**4/4 MetalGPU PASS**, `SKIP=0 MISSING=0 FAIL=0`.
Native 512x256 RHI churn PASS, `retire_remaining=0 gpu_healthy=1`;
opposite-view alpha sorted index samples `51/229`;
PRISMA HDR/Bloom `prisma_hdr=1 passes=10 fx_pixels=232`,
real Renderer3D `depth_center_delta=0`.
Seven PPM files captured on user's Mac, **not yet visually inspected**.
No production source changes in this documentation-only stage.
See `docs/agent-handoffs/spectrafx-stage110-native-acceptance-2026-11-20.md`
for exact logs, limitations and the next steps.


## Stage 109 — 2048 production GPU-owner retirements + optional Metal stress (NOT RUN)

Code `a5bf5979a17019a2e8e4b4170922698e627d4a89` grows the
deterministic `spectrafx_retirement` fixture to 1024 repeated
Play→replacement→reset cycles (>=2048 GPU owners), records peak RHI
handle/slot budgets and guards against leakage or premature release.
The real `spectrafx_metal_rhi` accepts
`AIVERSE_SPECTRAFX_RHI_STRESS_FRAMES=512` and
`AIVERSE_SPECTRAFX_RHI_STRESS_CAPACITY=256` (default 24x1), with
outstanding nominal payload and GPU health telemetry. Additional
code `5310c44c0d500b5b2bb74e5299bd2b458f4f5ded`
allows realistic post-bloom light spill while retaining Stage 104
raw depth assertions. **New Stage 106–109 code NOT natively tested.**
Handoff:
`docs/agent-handoffs/spectrafx-stage109-stress-2026-11-20.md`.


## Stage 108 — real Metal GPU alpha sort between two cameras (NOT RUN)

Code `9468a46297abbdd846e8a98ccf1df371bba95f17` extends
`spectrafx_metal_rhi` with actual GPU bitonic sorting for two
opposite camera directions. A diagnostic RHI shader visualizes the
first sorted slot into separate RGBA8 RTs, read after frame end:
expected red values ~51 (index 0) and ~230 (index 1). The test
exercises per-view compute→draw ordering without CPU particle readback.
**Native acceptance not yet run**; last green Stage 105 SHA.
Handoff: `docs/agent-handoffs/spectrafx-stage108-multicamera-alpha-2026-11-20.md`.


## Stage 107 — PRISMA HDR/Bloom + SpectraFX integration fixture (NOT RUN)

Code `9ee2ffeb5a0ff3a0d3088e291b098a95279994dc`
extends production `spectrafx_metal_scene` to render baseline/FX
through `Renderer3D::render(pass,&PostFx)`, draw GPU particles
inside PRISMA's HDR scene **before** `resolvePostFx`, verify
depth+real post passes and compare output pixels. Optional two extra
HDR/Bloom PPM captures. **Hardware/native tests NOT RUN on Stage 107**.
Last confirmed source stays Stage 105. Handoff:
`docs/agent-handoffs/spectrafx-stage107-prisma-hdr-2026-11-20.md`.


## Stage 106 — Editor GPU preview camera/RT coherence (NOT RUN)

Code `f12afc07483523b34923f1fc1378c654cf40c9fb`:
`SpectraFxGpuAssetsWindow` derives the actual RT camera projection
and dimensions, updates GPU/CPU once, calls per-view GPU alpha sorting
outside the pass and renders with the same `SpectraFxView`.
Reset view control added. No genre dependence.
**Native/editor tests NOT RUN**; last validated SHA remains Stage 105.
Handoff `docs/agent-handoffs/spectrafx-stage106-editor-preview-camera-2026-11-20.md`.


## Stage 105 — full Renderer3D native acceptance **PASS** (M3 Ultra)

User-provided hardware run at validated SHA
`c4fc948535557f1cc1a00459d1a3cd48257ad6ca`:
**126/126 full CTests PASS**, **11/11 SpectraFX PASS**, **4/4 actual MetalGPU
PASS**, strict metal evidence `SKIP=0 MISSING=0 FAIL=0`.
All **9/9 full acceptance stages exit 0**; no failed/skipped stages.
Real RHI: A pixels left/right 229/0; B 0/228, 24 churn frames,
retire_remaining=0, gpu_healthy=1, impact_queued=1 discarded=1 delivered=0.
Renderer3D real scene: FX-delta pixels A=168 centroid=63.50,
B=304 centroid=43.07, depth_center_delta=0, failures=0.
Five PPM files were saved on user Mac but **not visually reviewed here**.
Full evidence and next limitations:
`docs/agent-handoffs/spectrafx-stage105-full-renderer3d-acceptance-2026-11-20.md`.


## Stage 104 — real Renderer3D two-camera Metal scene acceptance (NOT RUN)

Code commits `4e7539d298076a3944bf837e570e0cd420e26275`,
`4518f775451c32af36a8db8fdd21e95a2fbf433a`. New production
`Renderer3D` + `SpectraFxSceneHost` test `spectrafx_metal_scene`
renders baseline/FX in two independent camera RTs, verifies opaque
depth occlusion, checks camera-dependent screen displacement and
post-`resetScene()` removal + deferred GPU retirement.
Optional five PPM captures for real-device visual QA.
Strict Metal evidence now requires **4/4 hardware PASS**, not old 3/3.
**Native compilation, screenshots and GPU test results are NOT yet known
for Stage 104**. Last proven hardware acceptance is Stage 103's older SHA
`fceb8caef1bf26085063b55c224631a3c53a9407`.
Handoff: `docs/agent-handoffs/spectrafx-stage104-renderer3d-metal-2026-11-19.md`.


## Stage 103 — strict full native acceptance 9/9 PASS on Apple M3 Ultra

User supplied the acceptance report for validated SHA
`fceb8caef1bf26085063b55c224631a3c53a9407`:
**configure, SpectraFX core, architecture, host integration, full build,
full CTest, focused SpectraFX, Metal CTest and Metal evidence all exit 0**.
Failed stages 0, skipped stages 0. All 3 real Metal GPU tests PASS:
`spectrafx_metal_gpu`, `spectrafx_metal_billboard`,
`spectrafx_metal_rhi`; `PASS=3/3 SKIP=0 MISSING=0 FAIL=0`.
The exact full-CTest count is not in this excerpt.

The next work is Renderer3D/Editor live visual QA, real multi-RT camera parity
and high-volume resource/fault/performance validation, **not rerunning
the already-green Stage 98–102 acceptance without new changes**.

Handoff: `docs/agent-handoffs/spectrafx-stage103-full-native-acceptance-2026-11-19.md`.


## Stage 102 — distinguish 3/3 native Metal PASS from SKIPPED (parser PASS)

**Code:** `a6b8617d472b104e660a254d2ed228abc4c81905`,
`e711c2a955e54a12c6b3aa8136724744cc99726a`.
New `metal_evidence` acceptance stage parses verbose CTest for
`spectrafx_metal_gpu`, `spectrafx_metal_billboard` and
`spectrafx_metal_rhi`. `AIVERSE_ACCEPTANCE_REQUIRE_METAL=1`
fails the acceptance if any device test is SKIPPED, FAILED or MISSING.
Hermetic Python parser unit tests: **4/4 PASS in local container**.
**Not yet run:** Stage 98–102 native C++/Metal/CTest on the current SHA.
See `docs/agent-handoffs/spectrafx-stage102-metal-evidence-2026-11-19.md`.


## Stage 101 — deterministic production HybridPlayback retire/reset test (NOT RUN)

Code `50df70bd39cf8059c54e75389c81a1c6eb40a306` adds
`spectrafx_retirement` CPU-side test with a controlled Metal-capable
fake RHI exercising **production** keyed replacement, Stop,
`resetPresentation` in-encoder rejection, three-serial deferred GPU
release, 32 cycles of churn and exact resource-handle teardown.
Standalone real MetalGPU fixture remains `spectrafx_metal_rhi`.
**Stages 98–101 not yet compiled/native-tested**; last known green
full acceptance Stage 97 only.
Handoff: `docs/agent-handoffs/spectrafx-stage101-hybrid-retirement-2026-11-19.md`.


## Stage 100 — GPU impact callback lifetime (NOT RUN)

**Code:** `dcd0d38481561c5e5fc85cb1e7881893e5d9ba69` and
`a232c4535d221ff684ca81435bedeb97f35d1b36`.
Real Metal RHI fixture now checks async impact readback invalidation after
scene reset and rejects impacts for a recycled particle generation.
Production `HybridPlayback` calls
`SpectraFxHybridEmitter::invalidateGpuImpacts()` immediately when GPU
resources retire, while retaining the buffers for the 3-slot Metal
retirement policy. No gameplay or genre dependencies.
**Stage 98–100 native tests have NOT RUN**. Do not reuse the Stage 97
8/8 all-green result for these changes.
See `docs/agent-handoffs/spectrafx-stage100-impact-retirement-2026-11-19.md`.


## Stage 99 — multi-camera rendering without multiple simulation steps

Added `SpectraFxSceneHost::prepareRenderView(const SpectraFxView&)` and
corresponding HybridPlayback/Emitter methods: host updates GPU and CPU
**once per frame**, then prepares/sorts each camera independently
between offscreen render passes. No FPS/RTS/Editor dependency. Code
commit `8f9777c17df9c4b22d2104927ef949a925ede6b3`.
**NOT RUN:** Stage 98/99 native tests, Metal offscreen, full acceptance.
Read `docs/agent-handoffs/spectrafx-stage99-multiview-2026-11-19.md`
for API ordering and remaining limits.


## Stage 98 — real Metal RHI multi-view + retirement test authored (NOT RUN)

**Code SHAs:** `59605eea446f0c26533ea839b6074b7729d3d73f`,
`96954451dd1bd317079098bfe2ca02b823fa89ed`,
`fe530ceb33fd0edcb1c7c2b52113c838178bbce8`.
Added `spectrafx_metal_rhi` CTest with actual `GfxMetal.mm`,
offscreen RGBA8 readback, two independent camera views, compute→draw
and 24 frames of 3-serial GPU lifetime pressure. New optional
`gfx::Device::readColorRGBA8` is **diagnostic-only**; other backends
remain unchanged. **Stage 98 has NOT been compiled or tested on Mac**.
The Stage 97 all-green run was for the earlier SHA.
Next step: focused `aiverse_spectrafx_metal_rhi_test` build/CTest,
then one full `tools/spectrafx/acceptance.sh` and record logs.
Read `docs/agent-handoffs/spectrafx-stage98-metal-rhi-2026-11-19.md`.


## Stage 97 — full native acceptance PASS on Apple M3 Ultra

**Validated source:** `4e363db3a6fc0d1631a574165febb7d26e08c3b8`.
User-provided `acceptance.sh` summary: configure, core, architecture,
Editor/FPS/RTS hosts, full build, full CTest, focused SpectraFX and real
Metal GPU acceptance all exit **0**; **failed stages 0, skipped stages 0**.
Real Metal compute and billboard tests are **2/2 PASS**, including
alpha direct/remap 163/163 and additive red 76/152. Full CTest count is
not in the excerpt. End-to-end Renderer3D / Editor viewport visuals and
GPU resource-lifetime stress still need validation.

Full evidence/next stage:
`docs/agent-handoffs/spectrafx-stage97-native-acceptance-2026-11-19.md`.


## Stage 96 — Metal half4 compile failure: focused 1/1 PASS

User's Mac Studio full CTest previously reported `spectrafx_metal_billboard` FAILED
(`112/122`, test #113) with `half4(float3,float)` MSL constructor error.
Code commit `7f77b26fa7e6092ed970d3cf8d2d08c764527761` explicitly
narrows each billboard vertex color component to `half`. This is the
runtime shader used by the exact Metal offscreen raster test.
**Post-fix user-reported result:** `spectrafx_metal_billboard` PASSED 1/1,
0 failed, 1.47 sec. The excerpt does not identify the exact tested HEAD SHA;
the full 122-test suite and visual RHI/Editor acceptance are not yet confirmed.
Full details and revalidation commands:
`docs/agent-handoffs/spectrafx-stage96-metal-half4-2026-11-19.md`.


## Stage 95 — rolled/overhead camera basis (unverified)

`ParticleSystem` now derives billboards from the caller's `SpectraFxView.up`
rather than a forced world-Y vector. `spectrafx_contract` includes a
genre-neutral vertical-camera and invalid-view fixture. The code is committed
on this branch; native compilation/CTest/GPU visual validation for this SHA
**HAVE NOT RUN**. See
`docs/agent-handoffs/spectrafx-stage95-arbitrary-camera-2026-11-19.md`.
The final one-command acceptance is still required.


## Stage 94 — no genre/editor camera dependency

The current branch now contains `SpectraFxView`: an explicit, camera-independent
eye/basis/projection/viewport packet for the FX core. `ParticleSystem` and
`SpectraFxSceneHost` no longer explicitly mention `RTSCamera`.
Previous camera-shaped APIs are preserved as templates.
Core compilation, host integration and full-build acceptance are distinct stages.
A new `spectrafx_architecture` CTest rejects reintroduced direct genre/Editor
source dependencies. **Native tests for Stage 94 have NOT RUN**.

See `docs/agent-handoffs/spectrafx-stage94-genre-neutral-camera-2026-11-19.md`
for exact implementation SHAs, API, test limitations, and next steps.


Updated: 2026-11-19. Branch: `feature/spectrafx-generic-cue-queue`.
Repository: `chromealex/AIVERSE`.
Status: **native validation in progress; CPU/header/host gates confirmed,
Metal GPU work pending**. User-provided Mac Studio logs confirm:
- `aiverse_spectrafx_header_smoke`, `aiverse_editor`,
  `aiverse_fps_client` and `aiverse_client` **built/linked** successfully
- `ctest -L SpectraFX` **4/4 PASS**, 0 failures, 1.51 seconds:
  `vfx_graph_compiler`, `vfx_ecs_presentation`,
  `spectrafx_contract`, `vfx_graph_assets`
- Metal Evolution, Alpha Sorting and Compaction compute **PASS** on
  actual Apple M3 Ultra; Editor GPU Preview, real GPU billboard draw,
  GPU resource lifetime and visual fallback have **not** been verified.

A new macOS-only **real Metal GPU compute test**
`aiverse_spectrafx_metal_gpu_test` was added in stage 90.
It compiles and executes the exact runtime evolution, alpha sorting
and compaction MSL shaders and verifies the GPU-generated data.
It **PASSED 1/1 on an Apple M3 Ultra** (user-provided Mac Studio log:
CMake configured, C++/Objective-C++ compiled, Metal libraries compiled,
compute dispatched, GPU results verified, 1.63 seconds test, 0 failures).
Together with the 4/4 earlier CPU/header contracts, this confirms five
separate native tests; the five have not yet been re-run as one suite
on the latest commit.
The new GitHub Actions run `44578278295` was queued at last observation:
https://github.com/chromealex/AIVERSE/actions/runs/44578278295

Important cross-agent warning: `main` is now >300 commits ahead of
the feature base while other agents work. Keep the validated detached
worktree isolated; do not merge `main` into it or into this feature
branch until runtime acceptance and conflict review.
Do not claim release readiness or merge to `main` without validation.
## One-shot acceptance gate (Stage 93)

Use `bash tools/spectrafx/acceptance.sh build/spectrafx-validation`
from the project's existing detached Mac Studio SpectraFX validation
worktree. It runs **configure, all required target builds, full native
build, complete CTest suite, focused SpectraFX CTests and real Metal
compute/billboard CTests in one invocation**, with per-stage logs and
`build/spectrafx-validation/spectrafx-acceptance/summary.md`.

The same script is now invoked by `.github/workflows/spectrafx-validation.yml`
on Linux and macOS, with errors-as-warnings and acceptance artifacts
uploaded from every run. The first full GitHub run was **44582216785**
(queued when last checked):
https://github.com/chromealex/AIVERSE/actions/runs/44582216785

**Do not claim full acceptance PASSED** without the new run's actual
results. Hosted macOS may skip Metal if no GPU is available. This
does not replace a live Editor Preview/FPS screenshot and frame-retire
stress on the user's Apple M3 Ultra.

## Latest GPU rendering gate (Stage 92)

The separate macOS `aiverse_spectrafx_metal_billboard_test` was added
with CMake and CI in Stage 92. It uses the actual SpectraFX MSL
billboard shaders and direct offscreen Metal rasterization to verify
alpha, additive blending, and sorted-index remapping. **NOT YET RUN**,
so only the earlier `spectrafx_metal_gpu` compute test is a proven
real GPU PASS. On the user's Apple M3 Ultra validation worktree:

```bash
cd ~/Projects/AIVERSE-spectrafx-validation
git status --short
git fetch origin
git switch --detach origin/feature/spectrafx-generic-cue-queue
cmake -S . -B build/spectrafx-validation -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=ON
cmake --build build/spectrafx-validation --parallel 4 --target aiverse_spectrafx_metal_gpu_test aiverse_spectrafx_metal_billboard_test
ctest --test-dir build/spectrafx-validation -L MetalGPU -V --output-on-failure
```

Expected registered MetalGPU test count after the refresh: **2**.
Still remaining after raw Metal billboard pass: actual RHI Renderer3D
integration, Editor Preview on-device, GPU callback/lifetime/stress.
Do not merge to main without those checks.

## Where to start

1. `docs/agent-handoffs/spectrafx-2026-11-19.md` is the canonical,
   per-stage chronological handoff (very long). The latest heading is the
   most recent stage. Append another stage **after every implementation
   commit**, with the actual commit SHA, branch, modified files, APIs,
   functional behavior, limitations, tests actually performed, and next
   actionable step.
2. `docs/spectrafx-gpu-runtime.md` is the project-facing guide to
   `fx/gpu/*.gpu.json` and `fx/gpu/routes.json`, current runtime
   limitations, genre host frame ordering and Editor preview.
3. For native source:
   - `engine/fx/SpectraFxHybridPlayback.h`: manages routed keyed and
     one-shot instances, CPU fallback, Metal GPU pools, total 65536 GPU
     slot budget, retained resources and presentation diagnostics.
   - `engine/fx/SpectraFxHybridEmitter.h`: CPU/GPU backend selection,
     quality/LOD/spawn scheduler, GPU recovery and retained buffer lifetime.
   - `engine/fx/SpectraFxGpuSharedArena.h`: one homogeneous additive
     material batch, up to 64 sources sharing GPU particle state.
   - `engine/fx/SpectraFxGpuEvolution.h` +
     `SpectraFxComputeShaders.h`: persistent GPU particle state,
     optional world-Y ground collision and impact feedback.
   - `SpectraFxGpuBillboardRenderer.h`, `SpectraFxGpuAlphaSort.h`,
     `SpectraFxGpuCompaction.h`: GPU billboards, Metal-only sorted alpha,
     Metal-only GPU-author-written indexed indirect draw. Alpha sorting
     and compaction both use three frame scratch slots. They cannot be
     used simultaneously on one emitter.
   - `SpectraFxGpuImpactFeedback.h`: Metal-only asynchronous GPU
     impact readback (<=4096 particle records per emitter);
     weak callback ownership, epoch invalidation and generation checks.
   - `SpectraFxGpuEmitterAsset.h`, `SpectraFxGpuEmitterLibrary.h`,
     `SpectraFxGpuRoutes.h`: strict project-owned JSON assets/routes.
   - `SpectraFxCueInbox.h`, `SpectraFxCueBridge.h`,
     `SpectraFxSceneHost.h`: genre-independent presentation API.
   - `apps/client/ClientFx.cpp`: existing opt-in RTS bridge.
   - `apps/fps/client.cpp`: opt-in local muzzle-fire cue ID 1, with
     floating-origin reset. `engine/actor/CMakeLists.txt` links
     `aiverse_fx` into FPS client only.
   - `apps/editor/windows/SpectraFxGpuAssetsWindow.cpp`: GPU route and
     asset visual authoring, safe save/backups, unsaved-JSON offscreen
     preview with pause/step/backend diagnostics.
   - `apps/editor/windows/SpectraFxGraphWindow.cpp`: separately owns
     existing canonical CPU graph editing; don't duplicate its solver.

## Must-preserve boundaries

- The name **SpectraFX** covers game-genre-neutral presentation only.
  PRISMA owns postprocessing. PHYSIS owns environmental phenomena;
  NEREID/TerraWeave own water/terrain. Do not write gameplay authority,
  networking or ECS state from GPU particle callbacks.
- All GPU commands and state-mutating buffer operations must be encoded
  on the RHI owner thread and outside a render/compute encoder;
  renderGpu must run in an already open matching scene pass.
- `gfx::Device` is borrowed; destroy all SpectraFX resources before
  Device teardown. Live scene/floating-origin reset must call
  `resetPresentation`, not teardown-only `reset`. Retired GPU
  allocations continue charging the per-playback slot budget.
- Metal is the only currently **enabled** GPU backend. The existing
  AIVERSE Vulkan RHI lacks implemented compute methods; Vulkan/GL
  deliberately use authored CPU fallback. Do not claim Vulkan GPU FX
  support or use `frameSerial` as a true portable GPU fence.
- A project without `fx/gpu/routes.json` must behave exactly as
  before. Default alpha requires CPU; GPU alpha sorting is opt-in
  (`gpuSortAlpha`, <=4096, Metal). Homogeneous pools require additive
  appearance, matching environment and explicit `sharedPool`.
- `FxQuality::Off` suppresses new GPU spawn but existing particles
  still expire. The adaptive governor uses **whole-frame** GPU time,
  not SpectraFX-exclusive GPU time.
- Source-only review has identified strict backend fallback semantics:
  Auto+allowCpuFallback=false must reject CPU (fixed in stage 82).
  Shared billboard init failure may use a private GPU renderer
  (fixed in stage 83).

Latest source correction: stage 85 prevents repeated Editor preview resets on invalid JSON; see the chronological handoff for its commit and limitations.

## Next implementation/validation

The next source review should prioritize remaining metal readback
callback/resource completion assumptions, the editor viewport frame
sequence and compatibility with current `gfx::Device` API.

**Hardware-only follow-up if CI lacks Metal:** now that the real M3 Ultra compute
kernel acceptance has PASSED, build and run the separate offscreen Metal
billboard raster/blend acceptance added after Stage 91.
Use `~/Projects/AIVERSE-spectrafx-validation` (detached worktree),
`git fetch origin && git switch --detach origin/feature/spectrafx-generic-cue-queue`,
`cmake -S . -B build/spectrafx-validation -DAIVERSE_BUILD_TESTS=ON`,
`cmake --build build/spectrafx-validation --parallel 4 --target aiverse_spectrafx_metal_gpu_test`,
then `ctest --test-dir build/spectrafx-validation -L MetalGPU -V --output-on-failure`.
The prior compute test has already passed. A future render test may report PASS,
FAIL with Metal compiler/GPU details, or SKIPPED (exit 77, no Metal device). This is a raw Metal compute acceptance
test **not** an integration test of Renderer3D/RHI draw or 3-frame
retirement. Save full logs and fix any errors before merging.

After this first validated native contract, continue with:
- CMake configure/build including `aiverse_editor`,
  `aiverse_fps_client`, and RTS native client
- C++ header smoke for all new `engine/fx/SpectraFx*.h`
- focused config/route/queue/pool/quality/strict fallback CTests
- Metal compile/actual render for GPU billboard, sort, compaction,
  collision, async feedback and preview
- CPU fallback on Vulkan/OpenGL, strict Auto and GPU policy
- FPS floating-origin transitions and RTS match reset
- delayed resource retirement and three frames in flight
- no unintended diff or duplicate feature ownership with
  PHYSIS/NEREID/TerraWeave/PRISMA
- warnings/ASan where available, and focused CPU/GPU perf measurements

Record real command outputs and failures in the chronological handoff;
do not merely state that tests were scheduled.

## Sync source

```bash
cd /Users/aleksandrfeer/Projects/AIVERSE
git fetch origin
git switch feature/spectrafx-generic-cue-queue
git pull --ff-only origin feature/spectrafx-generic-cue-queue
git fetch origin main
git merge origin/main
```

If your working tree is rebasing or has unmerged files, do not run
blind `git pull`: inspect `git status` and use a new worktree.


## Lighting integration compatibility checkpoint

2026-11-20; branch integration/lighting-main-stage1-20261120.
Implementation: 6ce5b19c061ec16a450f60bc475e0e9f6e6674fe.

Restored two merged legacy SpectraFX preview calls to the existing shared
ViewportGpuRetirement API (GPU Assets and Graph windows). No VFX behavior or
new ImGui UI. Repaired Studio UI skill frontmatter, escaped Markdown and editor
workflow/category registration; regenerated skills/catalog.json. Added opt-in
Release arm64 OFF/ON/OFF lighting acceptance runner and updated lighting docs
and AGENTS checkpoint. No save-format or public rendering API changes.

All builds, dependencies, temporary data and logs reside at
/Volumes/KINGSTON/AIVERSE/Builds/.temp/lighting-completion-20261120.
Actual checks:
- Release arm64 aiverse_renderbench, aiverse_hdr_scene, aiverse_studio,
  aiverse_studio_metal_lighting_tests and aiverse_client builds PASS.
- Release CTest lighting GPU/host/ICB/animated selection: 10/10 PASS (27.87s),
  logs/stage29-release-gpu.log and .xml.
- Release studio_metal_lighting: 1/1 PASS (4.95s), stage29-release-studio.log/xml.
- Debug full build including AiverseEditor PASS after fixing the two calls.
- Full ./run-tests.sh --quick --build-dir <root>/build --jobs 12 --ctest-jobs 2
  still RUNNING at handoff, report 20261120-155447-18337, session 24647.
  Main aiverse_tests passed in 840.79s. Three catalog/routing/audit tests failed
  before the metadata fix; rerun through CTest after completion is required.
- catalog.py generate/check, tests/skills/catalog_test.py, tools/skills/audit.py
  PASS after fix (227 skills, zero audit errors/warnings). Python syntax check
  of acceptance runner PASS; performance runner NOT EXECUTED yet.
- Earlier own low-parallel build processes were terminated and resumed at 12
  jobs; first resumed Debug build failed on the two Viewport calls, then fixed.
  Interrupted/failed attempts are not passes. Logs stage29-quick*.log and
  stage29-release*.log preserve evidence.
- git diff --check PASS. CI N/A, no push.

Locked Mac still blocks interactive HTML/native screenshot QA. User confirms no
Android/Vulkan hardware stand presently: hardware acceptance NOT RUN. Native
Metal capture tests work independently. Experimental ICB stays OFF by default;
Studio geometry remains terrain/proxy preview, not full game asset parity.
Next: fix FPS preview/GI bounds and camera using actual geometry rather than
MapDoc default RTS dimensions, verify actual Metal; then consolidate full-suite
results and run isolated Release Sponza OFF/ON/OFF acceptance.
