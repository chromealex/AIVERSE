## 2026-11-23 — Stage 18: real native SpectraFX frame capture (tests deferred)

`apps/studio-native/StudioHost::captureSpectraFrame` now adds bounded
asynchronous one-shot readback of the dedicated native FX drawable,
NOT a browser-generated screenshot. The existing `gfx::Device`
`captureNextFrame` is scheduled on the native render owner and
completed via an immutable shared callback state guarded by a mutex.
`studio.spectraPreview` and authenticated `aiversemcp`
`fx_graph_preview` share new actions
`{"action":"capture","maxSide":512}` and
`{"action":"captureStatus"}`. Capture status includes
`capture:{pending,ready,width,height,mime,image?,error?}` in the
normal preview status. Max capture side 128–768, default 512;
JPEG ≤600 KiB, at most one outstanding request, 10s timeout;
project switch, FX detach and shutdown discard stale results.
Graph UI exposes Capture Frame, genuine JPEG thumbnail and Save
captured JPEG. No gameplay/ECS writes, fake particles, filesystem
output path or continuous video streaming. **No current-source
AppleClang/Metal, Node/Vite, CTest, Mac UI or Windows Vulkan tests
were run (user requested features first).**
Handoff:
`docs/agent-handoffs/spectrafx-studio-stage18-native-gpu-frame-capture-2026-11-23.md`.

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


## 2026-11-23 — Stage 11/12 HTML5 native embedded FX preview + MCP

Native `StudioScene` now draws an isolated particle stage in the
SpectraFX Graph HTML5 dock by moving the original native GPU surface,
NOT by allocating another Device or delivering a fake browser bitmap.
FX camera is independent, resettable and restored to map camera after
Graph closes. Scene editing/picking/asset placement is forbidden while
FX owns the viewport. In headless MCP all graph authoring operations
remain supported; `fx_graph_preview` is additionally discoverable but
requires a running Studio GPU owner for status/load/play/pause/step/
seek/restart/stop/parameter/cameraReset/cameraOrbit/cameraZoom.
MCP `load` supports inline graph or secure by-name source/draft lookup.
Authenticated Studio agents and HTML5 UI use one
`StudioHost::runSpectraPreview` method.
**Do not assert new Apple Metal build/GUI passes yet.**
Handoff docs: `docs/agent-handoffs/spectrafx-studio-stage11-embedded-preview-2026-11-23.md`,
`docs/agent-handoffs/spectrafx-studio-stage12-mcp-camera-2026-11-23.md`.


## Stage 9 — SpectraFX Graph first-use UX (2026-11-23)

User screenshot showed the `New Graph` form hidden below a scrolling asset list
in the narrow ~420px HTML5 Graph dock. Creation now appears as always-visible
**Starter FX** and **Blank V2** actions, above Graph Assets and in the no-graph
welcome state. Starter passes through the canonical native
`fx_graph_edit {action:"new"}` result (which already has
Spawn→Initialize→SpriteOutput) and invokes native `fx_graph_validate`
before presenting it as runnable. Blank V2 explicitly removes nodes/links
without mutating the native source. Duplicate graph names are rejected;
source controls and tests live in `SpectraDataInterfacesPanel.tsx`,
`spectraStarterModel.ts`, `SpectraDataInterfacesPanel.css` and
`tests/spectraStarterModel.test.mjs`. **Exact HEAD UI/Node/Vite checks
not yet confirmed PASS.** Handoff:
`docs/agent-handoffs/spectrafx-studio-stage9-first-use-2026-11-23.md`.

# Agent rules: SpectraFX universal VFX: status, APIs, stage log

## Stage 10 — macOS Studio image codec warnings (2026-11-23)

Tester Mac build succeeded but reported 54 diagnostics:
11 `-Wmissing-field-initializers` (3 repetitions of `StudioGameLaunch`
plus 8 stb writer initializers), 42 `-Wunused-function` generated by the
private static stb implementation in `StudioHost.cpp`, and 1 deprecated
`sprintf` in the vendored HDR writer. Source cleanup:
`StudioGameLaunch.h` fully initializes the optional camera;
`third_party/stb_image_write/stb_image_write.h` completes 8 portable
C initializers and uses bounded `snprintf`;
`StudioStbCodecs.h` retains private stb linkage but annotates its
optional unused entry points `[[maybe_unused]]` without muting any
first-party warnings; `StudioHost.cpp` includes this wrapper.
`studio_image_codecs` is a real PNG/JPEG regression target compiled
with `-Werror=unused-function`, `-Werror=missing-field-initializers`
and `-Werror=deprecated-declarations`. CI also fixes Studio's
`AIVERSE_BUILD_TOOLS=ON` prerequisite.

**Mac exact-HEAD build and codec CTest still pending**.
Small Clang/GCC language contract checks passed in a Linux sandbox;
they are not a full project compile. Handoff:
`docs/agent-handoffs/spectrafx-studio-stage10-warning-cleanup-2026-11-23.md`.


## 2026-11-23 — SpectraFX native Scene GPU preview (Stage 8; hardware acceptance pending)

Mandatory handoff: `docs/agent-handoffs/spectrafx-studio-stage8-native-preview-2026-11-23.md`.
`apps/studio-native/StudioSpectraPreview.h` owns a single real
`spectra::GraphRuntime` on Studio's existing Scene render thread and
gfx::Device. `StudioScene::frame` performs actual GPU/CPU particle step
at a device-safe point BEFORE render passes and draws into the open HDR
Renderer3D scene pass BEFORE PRISMA resolve. `StudioHost` allowlists
`studio.spectraPreview` commands and marshals them via `onScene`.
HTML5 `SpectraNativePreview.tsx` controls Play/Pause/Step/Seek (60Hz,
max 3600 ticks), Restart/Stop, actual backend status and named
runtime scalar parameters. Click Open Scene to see real native pixels.
This DOES NOT render an offscreen preview *within the Graph panel*;
it does not draw fake HTML particles. A new macOS Metal CAMetalLayer
acceptance test and CI are authored. **Current revision not yet
build/Metal/UI verified**; the parent PR #113 must be accepted first.
Do not route RHI handles, GPU simulation or non-immutable ECS data into
React or the main StudioSession/UI thread.


## 2026-11-23 — SpectraFX Stage 7: typed Curve and Gradient authoring (QA pending)

Mandatory handoff: `docs/agent-handoffs/spectrafx-studio-stage7-curve-gradient-2026-11-23.md`.
`apps/studio-web/src/panels/SpectraCurveEditor.tsx` adds bounded
visual key editing for the existing Graph v2 `Curve` and `Gradient`
value nodes. `spectraCurveModel.ts` validates 1..32 unique normalized
keys, finite curve numbers, safe six/eight-digit RGBA hex and immutable
changes. Alpha, add/remove key, time/parameter edits and static chart/
color-strip preview are authoring only. Existing `fx_graph_validate`
and `fx_graph_set` remain authoritative for Publish. Never describe the
static chart as GPU particles or use browser code to own RHI handles.
`npm run test:graphs` now includes `spectraCurveModel.test.mjs`.
**Current Stage 7 source has NOT passed confirmed CI or GUI hardware QA.**
This feature is on stacked draft PR #115, parent PR #113 remains a draft.


## 2026-11-23 — SpectraFX HTML5 graph canvas (Stage 6; QA pending)

Read `docs/agent-handoffs/spectrafx-studio-stage6-graph-canvas-2026-11-23.md`.
The existing `fx_graph` dock tab now includes a 2D node/link canvas implemented
by `SpectraGraphCanvas.tsx` and `spectraGraphCanvasModel.ts`:
node drag/layout, zoom, display typed/exec links, native catalog-driven
Add/Delete and native `fx_graph_edit` Connect/Disconnect.
File saves still use the parent Stage 5 Data Interfaces pane's
`fx_graph_validate` / `fx_graph_set` / `fx_graph_draft_set`, not browser IO.
`npm run test:graphs` includes new canvas-model tests.
**Source committed, CI queued; not hardware/UI-verified.**
This is an authoring topology canvas, NOT a particle/GPU preview.
Never represent it as a real frame or change RHI simulation based on it.


## 2026-11-23 — SpectraFX Graph v2 Data Interfaces (Stage 5, draft PR #115)

Read `docs/agent-handoffs/spectrafx-studio-stage5-2026-11-23.md` before
changing this area. `apps/studio-web/src/panels/SpectraDataInterfacesPanel.tsx`
now renders the existing `fx_graph` tab using the shared Studio UI kit.
It accesses native project graph assets ONLY through `studio.op` with
`fx_graph_list/get/validate/set/draft_*`, and uses `studio.assets` to
choose real SpriteOutput image references from `content/`. Do not write
the project's files directly from React. The pure
`spectraDataInterfaceModel.ts` owns atomic Graph v2 parameter/binding
edits, safe provider IDs, node refs and image path validation; tests live
in `apps/studio-web/tests/spectraDataInterfaces.test.mjs`.
Provider names refer to host callbacks, not file paths. Browser-only
validation does not substitute native `fx_graph_validate`.
Live GPU preview/full wiring are NOT finished; never fake the renderer.

This stage is stacked on `feature/spectrafx-significance-data-20261123`
(parent draft PR #113); `main` and `feature/studio-html5` are unchanged.
Native Studio WebView and new CI tests have not yet passed as of this
handoff. Read the exact test logs and preserve per-stage handoffs.


## 2026-11-23 — Stage 4: Graph v2 typed Data Interfaces (native QA pending)

Required handoff:
`docs/agent-handoffs/spectrafx-data-interfaces-stage4-2026-11-23.md`.
Graph-v2 root `dataInterfaces` maps named `instanceParameters` (slot 0..7)
to `{provider,channel,scale,offset,fallback,required}`. A typed
`DataInterface` graph node lowers `{"$runtime":"nodeId"}` to an existing
ParticleProgram VM `paramN` read. `DataInterfaceRegistry` injects up to
16 read-only, nonthrowing, owner-thread providers. `GraphRuntime` samples
them once per burst at its world position, not per particle/per tick.
Required missing data rejects the burst; optional missing data uses a
validated fallback. This reuses existing CPU/shared BatchWorld and opt-in
GPU VM selection; it is not a new GPU execution backend. The editor's
shared node catalog, typed `name` field and atomic
`setDataInterfaceBinding` / `removeDataInterfaceBinding` helpers are
available. Full HTML5 Studio UI, PHYSIS-specific adapter and hardware
parity are not yet provided.

**New revision's tests are committed but have not been observed passing.**
The workflow now builds graph/compiler/runtime CTests on Ubuntu and macOS.
Keep the PR draft and do not merge without actual test logs and graphics QA.
`examples/spectrafx/data_interface_weather_v2.vfx.json` demonstrates the
format. Use `docs/spectrafx-modernization-roadmap-2026-11-23.md` for what
remains. Preserve strict presentation-only ECS ownership and CPU fallback.


## 2026-11-23 — Stage 3: read-only visual acceleration fields (native QA pending)

Handoff: `docs/agent-handoffs/spectrafx-fields-stage3-2026-11-23.md`.
`SpectraFxAccelerationFields` is a bounded, injected position→acceleration
provider registry (max 16, weighted, validates finite outputs). Hosts may bind
PHYSIS wind/gravity/turbulence presentation snapshots via
`SpectraFxSceneHost::bindAccelerationField` after init. Dedicated Metal GPU
and same-asset CPU twin emitters receive an identical per-frame additive
acceleration correction without modifying the authored emitter asset, ECS,
RHI ownership or source generation. With fields attached, new effects bypass
homogeneous GPU shared pooling; existing pooled emitters are not migrated.
Legacy `cpuEffect` fallback is not dynamically field-driven, and there is NO
PHYSIS-specific adapter or HTML5 authoring UI in this stage. Native build,
CTest and Metal parity **NOT YET VERIFIED**. The next agent must run focused
CI and real hardware tests, then implement field data interfaces and graph
integration without changing game authority or PRISMA/LENS ownership.


## 2026-11-23 — Opt-in camera significance for SpectraFX cues (UNVERIFIED native)

Read the two mandatory handoffs:
- `docs/agent-handoffs/spectrafx-significance-stage1-2026-11-23.md`
- `docs/agent-handoffs/spectrafx-significance-stage2-routes-2026-11-23.md`

The new `SpectraFxSignificance` (camera/event score) and `SpectraFxCueInbox::drainPrioritized`
prioritize bounded presentation cues while preserving per-key Play/Move/Stop/Kill
ordering, callback snapshot semantics, visibility check and default FIFO behavior.
Enable explicitly from `SpectraFxSceneHost::configureCueSignificance` or by
`fx/gpu/routes.json` `cuePriority` with per-route `importance`. This orders
only the scene cue inbox; it is NOT particle culling or whole-system budgeting.
Native/Metal/mobile tests have NOT RUN here. Next step is clean build, CTest,
full SpectraFX acceptance and real scene captures. Broader modernization plan:
`docs/spectrafx-modernization-roadmap-2026-11-23.md`. Preserve all existing
GPU fences, ECS ownership and PRISMA/LENS boundaries.


Moved verbatim from AGENTS.md on 2026-11-21 (AGENTS.md keeps the cross-cutting rules; this file is
mandatory reading before working in this area). Newest entries are at the end.

## AIVERSE SpectraFX (universal VFX, in progress)

**Required handoff:** [docs/agent-handoffs/universal-niagara-vfx-2026-11-18.md](../../docs/agent-handoffs/universal-niagara-vfx-2026-11-18.md).
**Official AI skill:** [skills/visuals/aiverse-spectrafx/SKILL.md](../../skills/visuals/aiverse-spectrafx/SKILL.md).
**System documentation:** [docs/spectrafx.md](../../docs/spectrafx.md).

**Effects in data + one CPU/GPU path (2026-11-22):** [docs/spectrafx-entity-effects.md](../../docs/spectrafx-entity-effects.md).
Entity effects are declared in rules/map/type/weapon/ability `"spectraFx"` data and presented by the shared
`engine/fx/SpectraFxEntityEffects.h` in every host (RTS client, FPS client); never add a host-specific copy. A
`fx/gpu/*.gpu.json` asset runs on the Metal kernel or its CPU twin (`SpectraFxEvolutionKernel.h`, keep in lockstep
with `SpectraFxComputeShaders.h`); `spectrafx_cpu_gpu_parity` must stay green.

<!-- Historical incoming Inspector/SpectraFX branch contracts; current integration gates still apply. -->
**Latest implementation handoff (Stage 44, actual standalone tests RUN):**
[docs/agent-handoffs/spectrafx-stage-44-offline-diagnostics-tested-2026-11-19.md](../../docs/agent-handoffs/spectrafx-stage-44-offline-diagnostics-tested-2026-11-19.md).
New `tests/vfx_spfx_diagnostics_test.cpp` and
`aiverse_vfx_spfx_diagnostics_test` (CTest:
`vfx_spfx_diagnostics`, label `SpectraFX`) cover
raw SPFX V2 checkpoint CRC, bad magic/version/size,
redacted/opted-in JSON, fixed 3-entry ZIP STORE
structure and CRC. The offline CLI and test were
compiled and actually executed on GCC 14.2 Linux,
using byte-exact GitHub source mirrors (six
`git hash-object` comparisons passed), with minimal
`render::float4`/checkpoint ABI stubs. C++20
`-Wall -Wextra -Werror`, real Python `zipfile`
extraction, CLI valid/corrupt/missing-file exit
codes, standalone CMake/CTest **1/1 PASS**, and
ASAN+UBSAN of portable diagnostics also PASS.
A separate Linux/macOS workflow is now available:
`.github/workflows/spectrafx-diagnostics.yml`,
and `.github/workflows/spectrafx-validation.yml`
builds the new focused test target.

**NOT VERIFIED:** full AIVERSE/CMake project
compilation, editor/Metal/Objective-C++ shaders,
GPU checkpoint readback and recovery, live device
testing or CPU/GPU parity. GitHub Actions checks
were still queued/pending when inspected. Do not
claim those tests passed. Do not merge `main` or
change other agents' branches without instruction.
Keep the separate Stage 44 handoff and all existing
genre-neutral ECS presentation ownership rules.


**Latest implementation handoff (Stage 43, source committed; actual native tests deferred):**
[docs/agent-handoffs/spectrafx-stage-43-safe-bundle-mailbox-recovery-2026-11-19.md](../../docs/agent-handoffs/spectrafx-stage-43-safe-bundle-mailbox-recovery-2026-11-19.md).
The offline CLI/editor now create one **explicit local SAFE ZIP**
diagnostic bundle, at most 128 KiB, with fixed manifest,
checkpoint summary and sanitized preview timeline JSON.
Never add raw `.spfx` GPU particle/VM state, source filenames,
world bounds, exact RHI identifiers or unrelated editor history
to this safe ZIP. Raw SPFX remains a separate, sensitive export.
Safe filenames exclude absolute GPU frame serials; sensitive
report opt-ins reset on group/pool change.

The public native `GpuCaptureRequestMailbox` is a same-owner-thread
non-owning editor/native request slot. Editor UI can QUEUE a
selected GPU group and request release, but it never calls GPU
readback/compute/reset directly. The native render host claims the
request before the scene pass and uses the existing one-at-a-time
`GpuPoolCaptureHost`; Stage 32 shared readback budgets,
Stage 35 completed-fence quarantine, Stage 40 checked buffer
staging and Stage 42 current revision checks remain mandatory.
Detach Editor's telemetry/capture/recovery pointers BEFORE the
native pool, mailbox or Device is destroyed. Never store graphics
handles or visual timelines in authoritative ECS, and never
add genre-specific VFX code.

`GpuPoolRecoveryHost` now supports a bounded GPU reset retry
pump at the native gfx Device pre-pass phase, no background
work or busy GPU loop: exponential host-frame cooldown up to
32 frames. Events expose retry failure counts and earliest
owner frame; editor can display them and queue only reset
intent. Failed/lost graphics Device still requires an explicit
NEW platform GPU Device; live particles are not recovered.
Stage 43 C++/ObjC++/MSL builds, CTest, ZIP extraction,
CLI execution, editor UI and Metal device tests have
NOT RUN. Keep separate dated handoffs, preserve other
agents' changes and do not merge to main without request.


**Latest implementation handoff (Stage 42; committed source, tests deferred):**
[docs/agent-handoffs/spectrafx-stage-42-redacted-diagnostics-group-routing-2026-11-19.md](../../docs/agent-handoffs/spectrafx-stage-42-redacted-diagnostics-group-routing-2026-11-19.md).
`SpectraDiagnosticReport.h` now generates **privacy-redacted**
V2 `.spfx` JSON by default: NO raw particle/VM data,
world-coordinate bounds, exact GPU frame/revision/CRC identifiers
or original file path. Only explicit `includeWorldBounds` /
`includeGpuIdentifiers` options reveal their respective
aggregate fields. The offline inspector CLI defaults to
redaction and writes local JSON only with `--report`; it
never auto-uploads. Raw `.spfx` itself remains SENSITIVE.
Editor has two separate opt-in actions: Save safe JSON report
and Save RAW GPU checkpoint (sensitive), both scoped to the
selected GPU group with fresh V2 snapshot eligibility.

`GpuPoolWorld::gpuGroupCount/gpuGroupSummary` provide safe
per-group status, and native `GpuPoolCaptureHost::requestNext`
selects ONE capture by RoundRobin, largest prior particle
estimate, or missing/stalest checkpoint. Continue to enforce
Stage 32 shared readback limits, current-revision capture
freshness and Stage 35 completed-GPU-fence slot quarantine.
Only the editor's OWN group-0 preview may be scheduled by
its UI; external render-host pools are non-owning telemetry.

`GpuPoolRecoveryHost` now supports a queued
`requestPresentationReset` with explicit ResetRequested phase,
distinct ResetRetryRequired/DeviceRecreationRequired status,
stable monotonic transition sequences and visible reset/rebuild
attempt counters. It does not allocate a Device, operate
asynchronously or restore live GPU particles; the native host
must execute reset in an active graphics frame outside encoders
or recreate a distinct healthy Device, preserving the old
Device until old resources are released. Renderer/ECS source
handles remain entirely presentation-only and genre-neutral.
Stage 42 C++/Metal compilation, CTest, CLI execution, editor
screenshots, GPU multi-group capture and hardware recovery
have NOT RUN. Keep mandatory handoffs; do not merge to main
or overwrite parallel agents' work without instruction.


**Latest implementation handoff (Stage 41, source-only; native tests deferred):**
[docs/agent-handoffs/spectrafx-stage-41-offline-inspector-native-capture-2026-11-19.md](../../docs/agent-handoffs/spectrafx-stage-41-offline-inspector-native-capture-2026-11-19.md).
Use `engine/fx/SpectraCheckpointInspector.h` and
`tools/spectrafx/inspect_spfx.cpp` for offline **V2 only**
`.spfx` diagnostics. The bounded native reader distinguishes
magic, header, version, payload length, truncation/trailing
bytes and CRC errors, then reports finite/live/dead GPU
particle properties without ever opening a graphics Device.
This is NOT a portable savegame or GPU state importer.

The native presentation layer now exports
`GpuPoolCaptureHost` through `<aiverselib/mechanics/SpectraFX.h>`.
It holds ONE capture request, submits async readback ONLY after
pool.step and before the render pass, and polls on the Device
owner thread at the next beginFrame before any new particle
mutation. It rejects stale or incorrect pool-lifetime epochs
and request IDs and uses weak callback ownership with a
600-graphics-frame poll limit. Source GPU state must remain
frozen while a request is queued/in flight/ready; the
`examples/spectrafx/native_ecs_attachments.h` sample now
models this without writing renderer handles into gameplay
ECS. Cancel before rollback, shutdown or gfx::Device
recreation; callback cancellation DOES NOT cancel RHI GPU
reads, which must settle their Stage 32 budget normally.
`GpuPoolRecoveryHost` also rejects replacing an existing
Device with the exact same old graphics object.
**Stage 41 native compilation, the CLI execution, CTest,
Metal readback and device-loss smoke tests have NOT RUN.**
Do not claim successful tests or modify main without request.
Keep distinct Stage 35/40 ownership, readback and kill-fence
rules and separate agent handoff for every stage.


**Latest implementation handoff (Stage 40, source committed; execution NOT verified):**
[docs/agent-handoffs/spectrafx-stage-40-frozen-checkpoint-staging-recovery-2026-11-19.md](../../docs/agent-handoffs/spectrafx-stage-40-frozen-checkpoint-staging-recovery-2026-11-19.md).
A truly frozen Metal frame with no births/kills/live VM parameter
changes now performs geometry gather without Update VM and without
advancing the GPU particle-state revision. The opt-in SpectraFX editor
can **Freeze & capture GPU state** using async per-group
`requestGpuCheckpoint` and only offers binary export for complete,
current-revision data. V2 diagnostic `.spfx` binaries use a 48-byte
header and CRC32 validator; legacy V1 was 40 bytes and is superseded.
These are NOT native game saves or GPU replay/timeline state.

Metal Spawn/VM/Gather input buffers now use checked storage uploads.
A partial upload/compute-begin failure keeps pending births/slots,
invalidates checkpoints, disables stale draws/VM-error diagnostics
and retries inputs on the next GPU step. The State-32 shared readback
budget and State-35 completed-fence quarantine remain mandatory.
`GpuPoolRecoveryHost` preserves its CPU-only effect catalog across
failed replacement-Device imports; the platform host owns the
actual new graphics Device and old GPU particles are NOT restored.
The opt-in VFX load policy now uses hysteresis with a recovery
threshold (default 85%) instead of flapping near particle/readback
limits. Never store renderer state in gameplay ECS or add FPS/RTS
special cases. Do not assert GPU parity or performance from source.
Stage 40 C++/Metal compile, CTest, editor screenshots, checkpoint
CRC fault checks, GPU readback and Device-loss recovery have NOT RUN.
Do not modify main or force-push another agent's branch without
explicit request. Read the dated Stage 40 handoff before further work.


**Latest implementation handoff (Stage 39; functional source, verification deferred):**
[docs/agent-handoffs/spectrafx-stage-39-host-recovery-fresh-export-load-2026-11-19.md](../../docs/agent-handoffs/spectrafx-stage-39-host-recovery-fresh-export-load-2026-11-19.md).
The new `GpuPoolRecoveryHost` is a **native renderer-owned**
status/event coordinator for GPU-reset retry and explicit fresh
graphics-Device recreation. It never owns/replaces platform
`gfx::Device` autonomously and never restores living GPU
particles or old presenter source generations. `PoolRecoveryEvent`
reports exact actions and accepted reset group counts; the game host
must keep the OLD Device alive through old-pool shutdown.
The CPU-only `PoolRecoveryManifest` rebuilds effect definitions.
`GpuPoolWorld::exportFreshCheckpoint` exports **only** fully
complete, CURRENT GPU revision binary diagnostics with no pending
births/kills/reset/readback and a strict <=528KiB+header bound.
This is NOT a portable ECS save, rollback file or replay timeline.
The editor can save the blob only when eligible, and never
starts an implicit GPU readback for the Save button.
`PoolLoadSheddingPolicy` opt-in throttles continuous births under
estimated particle/readback pressure, rotates source start for
fairness, and reserves bursts for manual one-shot VFX. Avoid
genre-specific throttling, hard-coded RTS/FPS pipelines, unsafe
GPU slot recycling, or a blanket claim of measured performance.
No CMake/ObjC++/Metal compiler, CTest, editor launch, real
GPU export or device-loss test was run for this stage. Continue
the mandatory dated per-stage handoff and do not merge to main
without explicit instruction.


**Latest implementation handoff (Stage 38, source implementation; tests still deferred):**
[docs/agent-handoffs/spectrafx-stage-38-reset-recovery-ecs-interpolation-2026-11-19.md](../../docs/agent-handoffs/spectrafx-stage-38-reset-recovery-ecs-interpolation-2026-11-19.md).
Multi-group Metal rollback now records `PoolResetReport` with the
count of groups whose reset uploads were accepted, retry/failure state
and a latched Device-loss signal. If any group rejects its reset,
`GpuPoolWorld::presentationBlocked()` prevents starting new VFX
frames, creating sources and drawing old GPU vertices until a
successful safe-frame retry. This is a restartable ordered GPU command
*preparation*, NOT cross-group hardware atomicity. On irrecoverable
device loss, export `PoolRecoveryManifest` (compiled effect
definitions, policies, parameter defaults only), release the old
pool while the OLD gfx Device still lives, then import into a new
pool on a healthy Device. Live particles and source generations
do NOT survive. Explicitly reconstruct ECS attachments from the
committed World.

Native games may use new `GpuEcsPoseInterpolator` from
`<aiverselib/mechanics/SpectraFX.h>` to store only two renderer-side
committed pose snapshots and interpolate adjacent ticks of the SAME
`Entity::packed()` generation. Do not place float render poses,
GPU tokens or physical particle handles in authoritative ECS.
The editor's last 180 GPU preview attempts are telemetry-only
and can be copied as JSON; they are NOT a scrub/replayable particle
snapshot. Forward-only timeline behavior is unchanged. C++/Metal
build, CTest, on-device recovery, editor launch, screenshots and
performance testing have NOT RUN. Preserve SpectraFX/Prisma
ownership boundaries and all handoff documents. Do not merge to
main or overwrite concurrent agents without user request.


**Latest implementation handoff (Stage 37, features source, tests deferred):**
[docs/agent-handoffs/spectrafx-stage-37-staged-ecs-gpu-reset-timeline-2026-11-19.md](../../docs/agent-handoffs/spectrafx-stage-37-staged-ecs-gpu-reset-timeline-2026-11-19.md).
The read-only `GpuEcsAttachmentPresenter` now stages new source handles
and checks binding revisions, source capacity, finite poses and detach
preconditions before retiring old attachments. GPU hardware failure
during a later detach can still leave a partial application, so it
returns false and requires safe retry; do not claim cross-GPU
transactions are atomic. Use `ComputeParticles::tryClear()` for
checked Metal reset **inside an active Device graphics frame**,
outside all encoders. Partial reset sets a dirty state and blocks
new GPU births/steps/checkpoints until retry. The pooled rollback
must not advance/replay visual sources on rejected reset. Failed
per-group steps must not draw old GPU vertices. The editor Metal
timeline only advances forward with bounded 1/60 steps (300 queued),
tracks actual visual elapsed time, and has explicit checked restart.
Metal device health/categorized upload, compute and reset failures
are exposed without inventing successful GPU operation. This stage
has NOT RUN native C++, MSL, CTest, editor or on-device Metal checks.
Preserve Stage 32 checkpoint budget, Stage 35 kill fences and
Stage 36 scatter-kill ordering. Do not alter main without request.


**Latest implementation handoff (Stage 36, functional source, execution NOT verified):**
[docs/agent-handoffs/spectrafx-stage-36-scatter-kills-native-ecs-2026-11-19.md](../../docs/agent-handoffs/spectrafx-stage-36-scatter-kills-native-ecs-2026-11-19.md).
GPU kill mode 6 operates on a compact uint32 physical-slot list at
compute buffer slot 14, dispatched **before** Spawn/Integrate/Compact,
replacing fragmented per-range Metal state blits. The Stage 35
`completedFrameSerial()` quarantine remains mandatory before
recycling dead physical slots. Do not turn GPU-kill submission into
a CPU-only free-list update, and do not claim performance improvement
without hardware measurements. A reusable native ECS attachment
example (not an executable) is in
`examples/spectrafx/native_ecs_attachments.h` with README.
The editor's Metal preview gains speed and single-frame stepping,
without modifying gameplay ECS, production CPU preview or simulation
time. Clang/CMake, MSL, CTest, editor launch and Metal hardware
execution have NOT RUN. Preserve earlier handoffs and avoid genre
coupling, force pushes or unrequested changes to main.


**Latest implementation handoff (Stage 35, source-only, tests deferred):**
[docs/agent-handoffs/spectrafx-stage-35-ecs-attachments-gpu-fences-2026-11-19.md](../../docs/agent-handoffs/spectrafx-stage-35-ecs-attachments-gpu-fences-2026-11-19.md).
The working SpectraFX branch already contained main at Stage 35 start
(main `9ee6ff6fbf390bd45363bc92c838f492dfe92eee`, feature
`4a50d05329840e713d6179a1519fb2f16a8d8604`); no merge/rebase
was performed. New native `GpuEcsAttachmentPresenter` is a read-only
adapter for the existing `SpectraSourceComponent`; only presentation
owns generation-safe source IDs, no genre-specific ECS changes.
Explicit `syncCommitted` checks for decreasing ticks, requiring
`resetForRollback()` to clear the entire shared presentation pool.
Non-unit ECS source scale is **not** silently approximated on Metal:
the strict pooled path rejects it. Metal source-kill uploads now use
`gfx::Device::updateStorageBufferChecked`; killed slots are not
recycled until `completedFrameSerial()` confirms successful GPU
frame completion. Other RHIs default to unsupported. The editor's
opt-in Metal GPU preview has Pause/Play, Freeze, Loop and Stop.
**NOT RUN:** compiler, Metal shaders, device tests, editor screenshots,
CTest, fault injection, performance. Preserve Stage 34 source ownership,
Stage 32 readback budgets and all agent handoffs. Never force-push,
merge to main or claim production GPU validation based on these writes.


**Latest implementation handoff (Stage 34, source-only; tests deferred):**
[docs/agent-handoffs/spectrafx-stage-34-source-kill-continuous-editor-2026-11-19.md](../../docs/agent-handoffs/spectrafx-stage-34-source-kill-continuous-editor-2026-11-19.md).
In `SpectraGpuPoolWorld`, persistent presentation sources are generation-safe
`PoolSourceId` handles with optional transform snapshots and bounded
rate/distance emissions. `stopSource(id,true)` selectively cancels/invalidates
that source's GPU physical slots (or tracked CPU FX handles); `retireSource`
alone does **not** kill existing particles. Tagged GPU slot reuse is deferred
until an ordered compute step. The editor now has explicit opt-in Metal
`fx_graph_preview {gpu:true}` for the strict GPU subset; CPU preview remains
the default. No native C++/Metal compilation, scene preview, GPU fences, CTest
or loss/parity tests have actually run for this stage. These sources MUST NOT
be promoted as validated or used in authoritative gameplay/ECS. Independent
agents must preserve this feature's scene-pass ordering, generation keys,
pending GPU kills and device lifetime when editing neighboring modules.


**Required handoff:** [docs/agent-handoffs/universal-niagara-vfx-2026-11-18.md](../../docs/agent-handoffs/universal-niagara-vfx-2026-11-18.md).
**Official AI skill:** [skills/visuals/aiverse-spectrafx/SKILL.md](../../skills/visuals/aiverse-spectrafx/SKILL.md).
**System documentation:** [docs/spectrafx.md](../../docs/spectrafx.md).
The previous `aiverse-niagara-vfx` skill is a legacy discovery alias.

The public SpectraFX headers are `engine/fx/SpectraFX.h` and `libraries/include/aiverselib/mechanics/SpectraFX.h`; native targets link `AIVERSE::spectrafx`. New cross-genre opt-in VFX layers are `engine/fx/VfxGraph.h` (graph -> existing CPU effect compiler),
`engine/fx/VfxGraphAssets.h` (versioned assets, parameter variants and hot reload) and
`libraries/include/aiverselib/mechanics/EcsVfxPresentation.h` (read-only ECS renderer bridge,
tick-event bindings). Never put renderer handles, float particles or graphics timing into
authoritative gameplay ECS, never duplicate an FPS-only/RTS-only VFX engine, and do not remove
legacy `GameFx` before production parity tests. The basic graph-v1 ImGui canvas is in
`apps/editor/windows/SpectraGraphWindow.cpp`, graph CRUD in
`apps/editor/core/SpectraGraphOps.cpp` and the editor-only in-memory preview
in `apps/editor/EditorFxOps.cpp`. **GPU VFX, full Niagara graph dataflow and automatic
native-client integration are not complete.** Current test targets are committed but not
verified as passing. Every new VFX stage MUST append a separate handoff commit after code and
tests and record exact SHA, verification status and next work.

**Stage 10 — opt-in Metal GPU particle subset (source committed, not verified):**
`engine/fx/{SpectraGpuParticles.h,SpectraGpuParticles.cpp,SpectraGpuShaders.h}`
and `tests/vfx_gpu_backend_test.cpp`. Public native API:
`spectra::ComputeParticles`, `ComputeEmitterDesc`,
`ComputePreference` and `chooseComputeBackend`, available through
`engine/fx/SpectraFX.h`, linked by `AIVERSE::spectrafx`.
Only a simple additive soft-billboard emitter uses Metal compute;
Vulkan/GL/GLES/WebGL fall back to the existing CPU `fx::ParticleSystem`.
`step()` must execute before a render pass and `draw()` inside it.
**Do not claim real Metal validation, GPU compaction, GPU graph execution
or shipping FPS/RTS integration.** Inspect the stage-10 handoff and
execute a native Metal smoke/CI test before enabling any default GPU
effects. Preserve deterministic gameplay/ECS state and the existing
CPU effect format.

**Stage 11 — SpectraFX graph/runtime compatibility routing (source committed,
NOT TESTED):** `engine/fx/SpectraGraphRuntime.{h,cpp}` adds
`spectra::GraphRuntime` and `inspectGraphGpuEligibility`. Native
modules may opt in via `engine/fx/SpectraFX.h`; the compatibility
gate checks the complete compiled emitter definition, **not** game
genre or output node label. Unsupported graph features preserve
the original CPU `EffectDef`; `RequireGpu` rejects unsupported
graphs/devices rather than dropping features. Existing RTS/FPS
clients are **not** switched to GPU automatically. The editor
`fx_graph_validate/get/list` operations report schema eligibility
but cannot certify a device's Metal capabilities. Required checks:
`vfx_graph_runtime`, `vfx_gpu_backend`, full SpectraFX CTest,
real Metal GPU shader smoke and lossless CPU visual parity.
No local checkout/build was available for this stage. Continue
`docs/agent-handoffs/universal-niagara-vfx-2026-11-18.md` after
each future implementation stage.

**Stage 12 — SpectraFX GPU sparse range and failure-state corrections
(source committed, tests NOT RUN):** `engine/fx/SpectraGpuParticles.*`
limits compute and indexed drawing to the occupied slot prefix
(`ComputeStats::submittedSlots`), skips empty-pool compute and records
loss during GPU→CPU fallback. This is not compaction. `RequireGpu`
never silently becomes CPU after a device-health failure.
`GraphRuntime::step` propagates backend readiness and diagnostics.
The recording RHI test now covers multiple-frame slot reuse and
clear/health failures; real Metal validation is still required.
The focused `.github/workflows/spectrafx-validation.yml` is a
CI gate for headless regression and editor compilation, **not**
proof of a real shader/GPU render. Mandatory continuation log:
`docs/agent-handoffs/universal-niagara-vfx-2026-11-18.md`.

**Stage 13 — SpectraFX Metal dense vertex gather (source committed,
NOT TESTED):** `engine/fx/SpectraGpuShaders.h` uses compute storage
slot 4 for a per-frame CPU-published list of live particle slot IDs,
and `Uniforms.meta.w` for the post-step alive count. The shader
gathers physical GPU particle state into contiguous GPU quad vertices;
`ComputeParticles` draws exactly the live count. No GPU state is
read back to CPU, but the live-slot list costs 4 bytes per live
particle per frame and the CPU still owns lifetime bookkeeping.
**This is NOT GPU-resident prefix-sum/atomic compaction or indirect
drawing.** `ComputeStats::submittedSlots` now means dense emitted
quads, superseding Stage 12 sparse-prefix semantics. GPU implementation
and recording tests have been committed but **real Metal GPU execution
and CTest have not been verified**. Continue the fixed handoff protocol
in `docs/agent-handoffs/universal-niagara-vfx-2026-11-18.md` and
keep existing CPU/native RTS/FPS behavior unchanged by default.

**Stage 14 — SpectraFX opt-in GPU-authored indirect draw (source committed,
NOT VERIFIED):** The general `gfx::Device` adds
`supportsGpuAuthoredIndirectDraw()` (default false; Metal reports
support via plain indexed indirect, not experimental ICB). Only when
`ComputeEmitterDesc::gpuIndirect` or
`GraphRuntimeOptions::gpuIndirect` is explicitly true and the RHI
supports the method may the restricted additive-sprite path use
GPU atomic alive compaction and GPU-authored indirect arguments.
Shader compute storage slots 5/6 are 4-byte alive counter and
20-byte draw args; MSL and future GLSL sources share the contract.
Legacy CPU-published gather, full CPU effects and all genre clients
remain defaults. CPU still owns physical particle lifetime/slot
bookkeeping. If indirect submission fails, skip rather than emit
an unsafe CPU draw with stale vertices.
`tests/vfx_metal_compaction_smoke.mm` is a real Metal shader smoke
on macOS (no device => CTest SKIP); it does not verify image parity.
No real Metal run or passing CI result was available while writing
this stage. Continue the required separate handoff file after every
bounded implementation stage. Do not claim GPU-resident lifecycle,
FPS/RTS production integration or GPU performance measurements.

**Stage 14B — universal cross-instance SpectraFX batching (source
committed, not build-verified):** `engine/fx/SpectraBatchWorld.{h,cpp}`
owns a *single shared* `fx::ParticleSystem` and reuses the
`ParticleSystem::batchFor` material key (GPU atlas texture handle,
blend, shader-graph identity, primitive type). Independent graph
instances can opt into the same context via
`GraphRuntime::initEffect(BatchWorld&, ...)` or
`GraphRuntime::initGraph(BatchWorld&, ...)`. The common host must
call `BatchWorld::setView/update/draw` exactly once per frame;
per-instance `GraphRuntime::step/draw` must NOT re-render/tick
the shared particle world. `GraphRuntime::clear/shutdown` kill
only their own effect handles, not the whole scene. One
`drawIndexed` call can cover particles of different effects and
UV rects sharing one atlas; incompatible blend/pipeline states or
mesh vs quad must remain separate, and large runs split at the
16-bit index cap. Do **not** create a fresh CPU particle renderer
per effect source in native integrations that need batching.
Existing FPS/RTS one-world renderers already share this behavior.
GPU graph execution remains experimental; the new shared API
does not coalesce separate Metal compute emitters, and rejects
`RequireGpu` rather than silently changing the requested backend.
The `vfx_shared_batch_world` regression source exists, but no
confirmed CTest result was available when authored. See
`docs/agent-handoffs/universal-niagara-vfx-2026-11-18.md`.

**Stage 15 — SpectraFX shared CPU cue scheduler and read-only ECS
adapter (source committed; tests NOT VERIFIED):**
`engine/fx/SpectraCueScheduler.{h,cpp}` adds the bounded
`spectra::CueScheduler` over the **existing**
`spectra::BatchWorld`, not another ParticleSystem. Native ECS
projects use `libraries/include/aiverselib/mechanics/SpectraCueEcsAdapter.h`
through `<aiverselib/mechanics/SpectraFX.h>`; it reads the
**existing** `SpectraSourceComponent`, resolves positions from
a caller-provided genre-agnostic callback and converts committed
`SimEvent` values to presentation cues. The scheduler never
mutates ECS/gameplay state or reads GPU resource handles into it.
Effects are registered by complete definition/graph, and module
event types are caller-defined, not hardcoded to FPS/RTS.

`acceptTick` de-duplicates committed tick IDs, `flush` is
bounded per frame, `syncAttachments` uses generation-safe
`Entity::packed()` keys, and `resetForRollback` kills only
this scheduler's handles plus pending cues. Source detaches are
released **before** admitting new attached sources against the
budget; tracked one-shot ring overflow explicitly kills oldest
particle handles to prevent rollback leaks. The host still
calls `BatchWorld::update/draw` ONCE per presentation frame.
The BatchWorld MUST outlive the scheduler. This is a shared
**CPU** simulation/RHI batching path: opt-in experimental Metal
`gpuIndirect` stays separate and unsupported multi-emitter GPU
graphs are NOT automatically enabled. Run `vfx_cue_scheduler`,
`vfx_shared_batch_world`, the full `ctest -L SpectraFX` suite,
editor and Apple Metal smoke before claiming production readiness.
Continue `docs/agent-handoffs/universal-niagara-vfx-2026-11-18.md`
with a separate handoff commit for every code stage.

**Stage 16 — SpectraFX typed graph v2 (source committed, tests deferred):**
`engine/fx/SpectraValueGraph.h` and
`engine/fx/SpectraGraphV2.h` add a strictly bounded
data-node DAG with numeric/vector math, typed exposed parameters,
data-port edges, age-sampled CPU curves and advanced FxEffect
module stages (noise, collision, force, trail, light,
sub-emitters, etc.). `engine/fx/VfxGraph.h` dispatches on
`version:1|2`; `spectra::compileGraph`, native graph assets,
MCP and existing runtime clients keep their API.
V2 lowers to the already-established v1 effect compiler and
`fx::parseEffect`; invalid output fields must NEVER bypass
that parser, and no GPU-only or genre-specific source is
permitted. This is a typed **authoring frontend**, not
arbitrary executable per-particle shader/bytecode.
An example is `examples/spectrafx/typed_noise_trail_v2.vfx.json`.
The user requested completing functionality before running tests.
Continue adding features with bounded implementation stages and
a **separate handoff commit after each stage**; record tests as
NOT RUN until actually executed.

**Stage 17 — SpectraFX graph-v2 editor and safe drafts
(source committed; tests deferred):**
`engine/fx/SpectraAuthoring.h` is the pure common node catalog
and model for execution/data links; `apps/editor/windows/SpectraGraphWindow.cpp`
now creates v2 graphs, supports the grouped node catalog,
math/data wires, field/curve/gradient editors, legacy v1 upgrades
and separate incomplete drafts. `apps/editor/core/SpectraGraphOps.cpp`
exposes `fx_graph_node_catalog`, `fx_graph_edit` (memory-only),
and `fx_graph_draft_set/get/list/publish`. Drafts reside under
`fx/graphs/_drafts` and MUST NOT be consumed by production
GraphAssets; published assets still compile through the strict
`fx::parseEffect` path. Never silently publish invalid graphs,
destroy working assets while editing, or duplicate a genre-specific
VFX editor. No CMake/CTest/ImGui runtime verification was executed
because user prioritizes closing functionality first.
Follow the separate per-stage handoff protocol.

**Stage 18 — SpectraFX reusable typed function graphs and libraries
(source committed, tests deferred):** V2 graph assets may define
pure functions under root `functions`, called using data node
`FunctionCall` with typed arguments and a declared output.
`engine/fx/SpectraValueGraph.h` evaluates nested functions with
bounded depth and recursion guards; `engine/fx/SpectraGraphV2.h`
validates function definitions, types and call DAGs even if
dormant. `engine/fx/SpectraFunctionAssets.h` provides
I/O-injected last-good hot reload and safe import of version-1
function library packages `fx/functions/*.vfxfn.json`.
`apps/editor/core/SpectraFunctionOps.cpp` implements
`fx_function_list/get/set/import/export` and the graph
editor exposes embedded function editing and import/export.
Never execute arbitrary source code as a VFX function;
compile into the existing FxEffect schema, preserve the
published/draft separation and no-genre/no-ECS-mutation
rules. The user explicitly deferred tests, so record them
NOT RUN, and continue the separate handoff after every
bounded code stage.

**Stage 19 — SpectraFX per-particle program runtime (source committed,
tests intentionally deferred):**
`engine/fx/SpectraParticleProgram.h` defines a bounded
pure scalar expression VM (fixed stack, precompiled
instructions, no allocations on particle update).
`EmitterDef::particleProgram` is validated in
`engine/fx/FxEffect.cpp`, compiled into each
`ParticleSystem::Impl::EmitterRT`, and invoked on
Spawn, Update, Death, Collision and Trigger hooks
in `FxEmit.cpp`/`ParticleSystem.cpp`.
Eight custom float attributes belong to each
*visual* particle, not to authoritative ECS.
Graph-v2 `ParticleProgramModule` lowers to
`particleProgram`, preserving the CPU fallback for
advanced effects and legacy compatibility for
old effect files. Never interpret JSON in a
per-particle hot loop or allow arbitrary user
code/external IO from the VM. Program effects are
unsupported by the experimental limited Metal
backend and must retain the full CPU EffectDef.
Example: `examples/spectrafx/runtime_particle_program_v2.vfx.json`.
User deferred testing; keep recorded status NOT RUN.
A separate handoff commit is mandatory before the
next implementation stage.

**Stage 20 — named SpectraFX graph-v2 runtime instance scalar inputs
(implemented in source, tests deferred):** `engine/fx/SpectraInstanceParameters.h`
validates up to eight `instanceParameters` declarations with unique
slots, finite defaults and bounds. `InstanceParameter` value nodes
are runtime-only and lower into read-only CPU VM `param0..param7`
instructions. `fx::ParticleSystem::playWithProgramParameters`
(and `spectra::BatchWorld` counterpart) applies the full
8-value array BEFORE prewarm and Spawn, without changing legacy
`play`. Per-handle `setProgramParameter` can update new or
already-live particles. `spectra::GraphRuntime` exposes raw and
named per-source setters, and the existing `CueScheduler` carries
optional masked values for event cues/attached sources.

The SpectraFX editor right panel now supports named schema JSON,
per-preview-instance sliders and `fx_graph_preview {instanceValues}`.
Keep separate compile-time `parameters` and runtime
`instanceParameters`; do not accidentally constant-fold the latter.
Never put these parameters in authoritative gameplay ECS,
silently switch a runtime VM graph to the limited GPU kernel,
or claim GPU expression parity. The latest `main` was explicitly
merged into `feature/universal-entity-inspector-fps` through
two-parent merge commit `463f69f230f7d02cdaca4c824b014ff1221402c1`
before these editor updates, preserving InputFlow and other
concurrent work. User explicitly deferred tests; mark native
compilation, CTest, real Metal and editor interactions as NOT RUN.
Maintain the separate per-stage handoff in
`docs/agent-handoffs/universal-niagara-vfx-2026-11-18.md`.

**Stage 21 — shader graph inputs sourced from CPU particle VM
(source committed, tests postponed):** `FxEffect::EmitterDef::graphInputs`
is an optional strict four-string array selecting `age`, `lifetime`,
`random`, `zero`, `attribute0..7`, or `param0..7`.
`FxEffect.cpp` validates and serializes this mapping. The
CPU emitter runtime precompiles each source to a byte selector
and fills the **existing 40-byte vertex** `graphData` lanes
without altering render-state batching or shader code.
The shader material compiler exposes the four lanes as
`sg_particle`. No new node language, shader JIT,
renderer handle in ECS, or duplicate FPS/RTS engine exists.
No authored `graphInputs` retains exactly the preexisting
age/lifetime/random/zero values; full unsupported
particleProgram and material graph effects must remain
CPU with strict Metal eligibility fallback.

User deferred tests until functional completion. Mark
CMake/CTest, GPU shader compilation, editor input
and visual output as **NOT RUN**. Continue the required
stage handoff in
`docs/agent-handoffs/universal-niagara-vfx-2026-11-18.md`
with a separate commit and keep native C++ usage
compatible across genres.

**Stage 22 — SpectraFX Metal bounded scalar GPU VM (source committed,
tests NOT RUN):** `engine/fx/SpectraGpuProgram.h` already
defined a fixed ABI payload; Stage 22 adds strict executable
budget/ABI checks and an **actual MSL interpreter** during GPU
particle Spawn/Update in `SpectraGpuShaders.h`. No recompilation
of user text into MSL is performed. `ComputeEmitterDesc::gpuProgram`
and `GraphRuntimeOptions::gpuProgram` default to false and
require explicit opt-in, one simple additive soft-sprite emitter,
a validated subset of 24 statements / 192 scalar VM ops, and
Metal compute support. Unsupported effects preserve the entire
CPU particle-program definition; `RequireGpu` refuses fallback.

Metal storage binding slots 7/9/10 hold 32-byte spawn parameter
snapshots, 32-byte persistent per-particle values, and the
10,256-byte validated GPU VM payload (ABI version 1).
Slot 8 remains the standard GPU frame uniforms. Fields/opcode
numbers are static-asserted against the CPU VM types. No
GPU custom attributes, arbitrary shader graphs, ribbons,
collisions, user-script execution or non-Metal GPU VM is
implemented. Gameplay ECS remains authoritative and unaffected.
Named v2 `instanceParameters` can be set before Spawn or
updated for existing GPU particles without reading positions
back. Existing/simple effects remain on their original backend
unless explicitly configured.

Native editor and MCP now distinguish ordinary billboard GPU
compatibility from `metalVmEligible`/`metalVmReason`.
These are **schema checks**, not GPU availability or success.
The user explicitly deferred CMake/CTest/Metal/device tests.
Mark all Stage-22 test outcomes **NOT RUN**, and append a
separate mandatory handoff commit to
`docs/agent-handoffs/universal-niagara-vfx-2026-11-18.md`
before starting another feature stage. Preserve multi-agent
branches and do not merge into `main` without user request.

**Stage 26 — integrated Metal VM custom scalar attributes
(SOURCE COMMITTED, tests deferred by user):**
`SpectraGpuProgram.h` now accepts VM `attribute0..7`
reads/writes for the restricted Spawn/Update Metal interpreter.
`SpectraGpuShaders.h` binds a separate 32-byte-per-physical-slot
attribute storage buffer at compute slot **11**, resets registers
on every GPU birth and persists changes after every Update.
`SpectraGpuParticles.{h,cpp}` allocates, releases, and clears that
buffer without changing the 64-byte particle state, 40-byte
vertex format, or VM payload ABI version. Strict whole-effect
GPU eligibility and CPU fallback remain mandatory; this does
NOT implement arbitrary `shaderGraph` materials on GPU.

`gpu_program::MetalVmPass` is a **different, standalone**
compute utility without custom register storage. It must
reject any payload reading/writing attribute fields 16–23;
never silently substitute zero or report parity.
Example: `examples/spectrafx/gpu_vm_custom_attributes_v2.vfx.json`.
User explicitly deferred tests until functional completion:
CMake/CTest, MSL compilation, Apple GPU rendering and
CPU/GPU numerical parity are **NOT RUN**. Preserve all
unrelated agent changes and record a separate stage handoff in
`docs/agent-handoffs/universal-niagara-vfx-2026-11-18.md`.

**Stage 27 — restricted Metal particle material shaderGraph and CPU-compatible
vertex source bindings (CODE COMMITTED, TESTS DEFERRED):**
`engine/fx/SpectraGpuMaterial.h` validates an opt-in, bounded
particle-domain `shaderGraph` through the **existing**
`render::graph::compile` system. It allows only trusted
fragment-node operations, the builtin sprite texture, no
external samplers or vertex graph, and maps four `graphInputs`
to `attribute0..7`, `param0..7`, age/lifetime/zero.
Stable UID `random` is currently GPU-unsupported and must
NOT silently substitute another value. The generated Metal/GLSL
material shaders share the CPU shader graph language.
Only the shaderGraph material variant uses vertex Float4
attribute 3 at offset 24; other GPU sprites retain the
existing pipeline.

`GraphRuntimeOptions::gpuMaterial` and
`ComputeEmitterDesc::gpuMaterial` default FALSE. The full
GPU eligibility gate must validate the whole EffectDef,
including `shaderGraph` and `graphInputs`; an unsupported
graph stays on full CPU, and `RequireGpu` must report an
error rather than discard authored features. The GPU
compute material source map is a 16-byte one-time buffer
bound at slot 13, and the CPU fallback carries the full
shaderGraph/graphInputs as originally authored. No
FPS/RTS dependency, gameplay ECS mutation, external shader
source, GPU mesh/ribbon or texture graph extension is
introduced. Example:
`examples/spectrafx/gpu_vm_material_inputs_v2.vfx.json`.

User requested feature implementation first: native compilation,
CTest, actual MSL compilation, GPU screenshots, performance and
CPU/GPU visual parity are **NOT RUN**. Keep the mandatory
handoff in `docs/agent-handoffs/universal-niagara-vfx-2026-11-18.md`
with implementation SHAs, exact limitations and next step.

**Stage 28 — shared genre-neutral GPU emitter resource world
(SOURCE COMMITTED; user-deferred tests):**
`engine/fx/SpectraGpuPoolWorld.{h,cpp}` introduces
`spectra::GpuPoolWorld`, exported from
`engine/fx/SpectraFX.h`, linked through
`AIVERSE::spectrafx`. It deduplicates by the
**complete serialized effect definition** plus effective
GPU program/material/indirect policy, capacity and
quality, then shares one `ComputeParticles` and
one draw for several logical sources with identical
effects. Unsupported effects use one existing
CPU `spectra::BatchWorld`; `RequireGpu` fails
instead of dropping authored features.

This is a pool of **per-effect GPU resource groups**,
NOT a monolithic cross-material GPU allocator.
Each source name retains independent bounded
`instanceParameters` defaults and source burst
snapshots. GPU material-only graphs may now read
`param0..7` without a separate VM program.
`PoolWorldBudgets` bounds group count/capacity
and one-shot births per frame. The host must call
`beginPresentationFrame`, `step`, `draw`,
`endPresentationFrame` ONCE in proper RHI order;
`resetForRollback` clears this pool's entire
visual scene only when no render/compute encoder
is active and device health permits.

Persistent attached ECS emitters, per-source GPU
kill/stop, a single physical GPU pool across
different material groups, global transparent
sorting, GPU-only allocator and native production
FPS/RTS migration are **not implemented**.
Use the mature shared CPU CueScheduler for
persistent sources. No gameplay ECS mutation.
No native compile, CTest, GPU MSL execution,
pixel capture or benchmark was run per user's
explicit request; record **NOT RUN** and update
`docs/agent-handoffs/universal-niagara-vfx-2026-11-18.md`
with exact SHAs in its own separate commit.

**Stage 29 — bounded pooled GPU/CPU one-shot cue routing (SOURCE COMMITTED,
TESTS DEFERRED):** `engine/fx/SpectraGpuCueBridge.{h,cpp}`
builds a genre-neutral presentation cue scheduler above the **existing**
`GpuPoolWorld`, rather than a new CPU/GPU particle simulation.
Native ECS projects include `SpectraGpuCueEcsAdapter.h` via
`<aiverselib/mechanics/SpectraFX.h>` to convert committed
`SimEvent` positions read-only. Each tick is consumed only once;
bounded queue/tick/frame budgets and a pool-level budget apply.
`resetForRollback()` resets the WHOLE pool before clearing tick
state; rejected resets preserve bridge state for safe retries.
**Do not claim per-source GPU kill/move/lifecycle**: the pool currently
supports only one-shot bursts, and attached/continuous effects
remain with CPU `CueScheduler`. Existing engine ECS and FPS/RTS
renderers remain unchanged by default. Tests/build/Metal GUI are
NOT RUN by explicit user instruction. Preserve a separate
Stage-29 handoff and continue feature work before testing.


## Stage 127 — PHYSIS read-only Data Interface adapter

Opt-in presentation bridge `engine/world/SpectraPhysisSnapshot.h` captures up to 4096 full 3D PHYSIS samples from one committed tick, then publishes an immutable snapshot to an existing SpectraFX DataInterfaceRegistry. Nearest sample within half-spacing support; missing atmosphere/hazards and out-of-region samples return unavailable. No PHYSIS ECS access from the burst thread, no second solver, no new graph format. Republish after changes/rollback and remove on unload. Source only; tests deferred by owner. Native graph example and existing graph runtime regression fixture added. Live Studio scene publication is not yet wired.

## Stage 128 — water and terrain Data Interfaces (source; QA deferred)

Native adapters: `engine/world/SpectraWaterSnapshot.h` and `engine/world/SpectraTerrainProviders.h`. Capture NEREID plane or sphere on the committed ECS owner; publish immutable sampled depth, thickness, submerged/found, flow, surface position and up axes. Geometry is explicit; mixed worlds can bind separate names. The shared Stage 127 lattice caps sampling at 4096 nodes. TerraWeave authored height snapshots expose height, clearance and reference up; missing tiles are unavailable. Spherical terrain reuses TerraWeave relief with host-selected rendered radius and scale. Up is radial/reference, not a slope normal. Use the existing graph-v2 Data Interface schema. Host integration remains opt-in; no automatic Studio publication or GPU texture field. No tests run.

## Stage 129 — Data Interfaces in shared emitter pools (source; QA deferred)

`engine/fx/SpectraGpuPoolWorld.h` now exposes setDataInterfaceRegistry between frames and dataInterfaceStats for the latest sampled birth. The existing graph compiler keeps validated bindings per logical effect, independent of GPU resource-group deduplication. Each manual or continuous birth samples the provider at the birth position, using the source's unchanged base parameters; required missing values reject before allocation/submission, optional values use authored fallback. Device recovery manifests retain binding declarations but never callback pointers; rebind providers after recovery or accepted rollback. Existing standalone GraphRuntime behavior and graph schema are unchanged. Native regression source added. No build/CTest/GPU run. Studio's legacy CPU scene source and live environment publication still need integration; no automatic socket or scene provider claim.

## Stage 130 — CPU scene Data Interfaces and prepared socket poses (source; QA deferred)

The native SpectraPresentationSystem now registers validated graph Data Interfaces and samples each attached source once per presentation sync, feeding subsequent births without restarting an emitter for sampled changes. Missing required data prevents or detaches emission under existing killOnDetach policy. Event cues use the same provider at the committed event position. Reset detaches the registry. StudioSpectraSources registers graph bindings for CPU and accepts an explicit immutable owner-thread registry for CPU/GPU via StudioScene::setSpectraSceneDataRegistry. Map/project replacement clears it; native hosts must publish again after activation. Existing isolated FX preview is not bound automatically. `engine/view/SpectraSocketPose.h` resolves precompiled View socket identities against the current model publication and already-evaluated pose, returning position/direction for native CPU/GPU effects; no animation re-evaluation or implicit bind-pose fallback. Source regression fixtures added; no tests executed. Automatic host publication, authored socket selection in Studio, continuous per-particle fields and GPU textures remain open.

## Stage 131 — automatic authored Studio field publication (source; QA deferred)

`apps/studio-native/StudioSpectraFields.h` captures the actual bounded legacy terrain preview mesh corner grid and samples its triangle diagonal (including preview decimation); no data outside map, hidden terrain, entity terrain or unsupported planetary/world-space edit maps. It also copies NEREID configured scene surface declarations and samples them using the shared water candidate policy, including authored grade, currents and vortices. No live confluence/flood/obstruction simulation in this edit-only provider. StudioScene automatically supplies these fields to StudioSpectraSources on document/terrain revision, with explicit native registry override retained and cleared on project/map replacement. PHYSIS is not fabricated in edit mode; actual committed gameplay snapshots and evaluated sockets still need native game host wiring. Existing schema and authoring tools unchanged. Regression source added; compile and all tests NOT RUN.

## Stage 132 — live committed gameplay fields and CPU graph sources (source; QA deferred)

`engine/world/SpectraLiveFields.h` publishes immutable PHYSIS and NEREID registries per source epoch/tick/region/geometry, reuses unchanged captures and invalidates on replacement. Native ClientFx now consumes authored SpectraSource components for visible prepared GameScene objects and graph-v2 assets, using the existing CPU presenter and ParticleSystem. The flat client uses a quantized 9x5x9 observer region, samples only committed World, preserves existing visibility and one particle update per frame. Graph definitions use a separate bounded namespace; stop/start and backward tick reset source state. Native spherical field publication remains available, but client planet graph sources are deliberately gated until body-local/global transform binding exists. Live terrain and GPU pool client routing are not wired by this stage; no new ECS schema, simulation or second renderer. No compile/CTest/hardware or GUI run.

## Stage 133 — body-local CPU graph sources and live terrain (source; QA deferred)

Existing GameView now exposes the prepared planetary object pose and actual surface relief/clearance. ClientFx accepts visible graph sources on the active planet only, using the same placement as drawPlanets, and resets sources on body change. PHYSIS/NEREID captures translate local probes by the Fixed simulation anchor; water surface coordinates are translated back. NEREID's filtered canonical sphere query selects only the matching anchor while preserving the original unfiltered API. Registry identity includes offset/centre/wave time. The client publishes immutable actual terrain values at prepared CPU source positions for both flat and spherical maps; no second terrain or gameplay solver. CPU source sampling only, camera-region nearest weather/water, no transit or other-body emission. GPU pools, authoring sockets, same-tick restore and full per-particle fields remain open. Compile/CTest/hardware/visual QA NOT RUN.

## Stage 134 — game GPU graph pool routing (source; QA deferred)

ClientFx prepares the visible graph-source population before the primary scene pass, registers explicit scenePoolEmission GPU candidates with GpuPoolWorld and GpuEcsAttachmentPresenter, resolves immutable live Data Interfaces, and steps once. Unit-scale, strict GPU eligible flat-map graphs route to the pool; failed admission, incompatible or scaled graphs keep the original CPU path. Distance-based births with exact-source-only TerraWeave bindings stay CPU. Primary HDR pass draws the pool once; secondary LENS cameras do not tick or draw the pool yet. Body/match/backward tick reset and failed source/step suppress pooled drawing. Pool uses existing canonical Device allocation ledger and bounded sources/groups. Planetary GPU remains gated; CPU planetary support unchanged. No runtime test executed. No new authoring schema or MCP operation.

## Stage 135 — swept CPU scene collision (source; QA deferred)

CollisionModule scene=true enables the host's read-only bounded sweep registry in the existing CPU ParticleSystem. Existing ground/plane response, collision VM, lifetime loss and sub-emitter hooks remain shared. Up to eight providers; earliest hit wins, invalid hits rejected; configurable query ceiling (default 8192, max 65536 per update). SDF helper traces a conservative signed-distance field with radius and bounded iterations; Box3D optional point-ray adapter uses Scene::raycast, explicitly rejects nonzero radius and requires host-owned read phase. All probes use particle simulation space, including body-local planets. Native ParticleSystem, BatchWorld and direct CPU GraphRuntime setters; no implicit host binding. No GPU depth/SDF collision or GPU event readback yet. Graph module schema and serialized effect flag updated; regression source only; tests NOT RUN.

## Stage 136 — sampled terrain and Box3D sphere sweeps (source; QA deferred)

`engine/fx/SpectraTerrainCollision.h` provides bounded sampled crossing/refinement (up to 64 intervals, 10 bisections), using reference up and clearance along it. This is not exact SDF or guaranteed continuous mesh CCD; narrow features can be missed, radius is vertical/radial. ClientFx installs a read-only provider over existing published GameView flat/planet terrain, detaches on stopMatch. Studio's supported flat edit maps publish an immutable actual preview-height collision snapshot on terrain/document change. Hidden/unsupported terrain is unavailable. `physics::Scene::sphereCast` uses native Box3D shape proxy and existing query filters; optional SpectraFX sphere adapter supports nonzero radius, no initial-overlap penetration guarantee. Box3D binding remains explicit host responsibility. No new physics simulation. Regression source added; no compile/tests/GPU QA run. GPU collision and visual authoring remain open.

## Stage 137 — analytic GPU static sphere collision (source; QA deferred)

CollisionModule staticSphereRadius (>0) and staticSphereCenter author a sphere obstacle in particle simulation coordinates. CPU and Metal/Vulkan regular and GPU-owned integration implement a swept analytic sphere boundary, particle radius multiplier, bounce and damping; initial penetration resolved along outward direction. The GPU subset requires ground=false, scene=false, no local planes, drag, particle VM, life loss/kill speeds or other unsupported modules; strict existing graph eligibility remains authoritative. CPU preserves full effect; compute fallback descriptor retains analytic sphere. ParticleState and checkpoint layout unchanged; simulation uniform ABI grows to 192 bytes in both shader languages. No depth-buffer or sampled volume SDF, collision event readback, moving obstacles or second impact integration. Collision-enabled checkpoint import remains restricted. Regression source authored, shader compilation/Metal/Vulkan/CTest not run.

## Stage 138 — bounded sampled static SDF volume (source; QA deferred)

CollisionModule staticSdf accepts an object with origin, spacing, dimension (3..16), and dimension³ distances in metres, x-fastest. CPU and both Metal/Vulkan integration paths use trilinear distance/gradient, up to 64 segment samples and 8 refinements, radius/bounce/damping. Probes require initial coverage; no extrapolation or guaranteed CCD. Existing material storage binding appends immutable SDF payload only when enabled; no new binding or ParticleState layout. Allocation accounting includes extra bytes. Strict GPU eligibility accepts one static sphere OR SDF only; unsupported combinations use CPU. No texture assets, depth buffer, dynamic scene volume or GPU feedback. Advanced JSON authoring only. Source regression fixtures authored; no build, shader compilation or tests run.

## Stage 139 — native GPU previous-depth collision (source; QA deferred)

Native ComputeEmitterDesc::depthCollision requires RequireGpu, excludes static SDF/sphere and VM. MetalDepthCollisionHistory captures actual post-scene depth into 64x64 float storage on GPU; Renderer3D::consumeResolvedSceneDepth provides one-shot after writer pass, before post processing. Host passes snapshot to ComputeParticles::setDepthCollisionSnapshot before next step; checked GPU copy, same device, previous submission within two fences, matched projection/inverse and texture orientation. No CPU readback. Both existing compute shader languages reconstruct local surface from 3 depth samples, reject discontinuities and bounce front-to-back crossings. No automatic host wiring, graph authoring, Vulkan capture producer, continuous mesh CCD, hidden geometry or GPU events. Storage payload appended at material slot 13; uniform ABI now 352 bytes, state ABI unchanged. QA deferred.

## Stage 140 — bounded GPU collision feedback (source; QA deferred)

Native ComputeEmitterDesc and GraphRuntimeOptions reportCollisionEvents opt in to a bounded append-only visual contact buffer (64 records per step, count includes dropped). Both Metal/Vulkan integration paths capture corrected particle centre, normal, collision kind and historical slot, using the existing diagnostics storage binding, preserving VM error word zero. Request explicitly once after a successful step using ComputeParticles, GraphRuntime or stepped GpuPoolWorld group; async readback uses existing shared checkpoint reservation, cancels on reset/restore/shutdown. Decoded batch can publish into existing SpectraFxEmitterEventRouter Impact links; no authoritative events or stable per-source attribution. No automatic child authoring or readback scheduling; host controls routing. CPU fallback does not synthesize GPU records. Regression source only; no build/shader/CTest run.

## Stage 141 — authored GPU pooled collision child effects (source; QA deferred)

Existing SubEmitterModule on=collision with one unconditional safe child name and no inheritance flags can opt in via pooled host policy. Dedicated GraphRuntime still rejects sub-emitters; unsupported pooled definitions keep the original CPU graph. GpuPoolWorld automatically requests up to 8 group contact batches per step, stores one completed batch/group and drains at most 32 children per call through existing presentation event router/command queue. ClientFx and Studio scene enable the policy; child asset uses existing fx/<name>.json reader, must be nonlooping/nonrecursive/non-ringbuffer. Child starts at corrected particle centre, unit scale, white tint, own +Y orientation; asynchronous approximate group attribution, no per-source semantics. Old batches discarded after four frame serials, retirement/reset clears weak inboxes. No GPU child execution or standalone preview auto routing. Regression source only; no build/tests run.

## Stage 142 — static mesh SDF baking (source; QA deferred)

`engine/bake/MeshDistanceGrid.h` adds reusable offline bounded indexed-triangle distance baking, double precision closest-triangle distance and nonzero solid-angle winding sign. Exact seam weld and closed directed-edge checks reject open/inconsistent/nonmanifold edges and degenerate faces; no self-intersection or nonmanifold-vertex certification. Auto cubic bounds pad one cell around largest extent, 4..16 nodes per axis, max 8192 triangles and 16 Mi sample-triangle pairs. `engine/fx/SpectraSdfBaker.h` reuses existing model bind-pose mesh extraction, accepts affine transform, produces current staticSdf JSON. Native `aiverse_sdf_bake MODEL DIMENSION OUT.json` tool; paste output into CollisionModule staticSdf. No new GPU ABI, runtime mesh scanning, high-resolution texture atlas or Studio/MCP baking button. Regression source only, no build/tests run.

## Stage 143 — Studio/MCP in-memory SDF baking (source; QA deferred)

Shared `fx_graph_edit` action `bakeSdf` takes Graph v2 CollisionModule node, project content model path and integer dimension 4..16 (optional expectedProjectPath). Uses existing model loader and bounded offline SDF baker. Dependencies restricted to checked project content, no symlink traversal, total 64 MiB input; no filesystem writes. Returns normal graph/compiler diagnostics, preserves unrelated authoring. HTML5 Graph resource inspector adds CollisionModule model asset picker (search/drop), lattice dimension and Bake/Rebake, rejecting stale graph/project responses. Parent local draft save/publish unchanged; no global undo/redo implementation claim. Native MCP uses same edit action; no new op registry. No high-resolution format, asset texture or dynamic geometry. Build/CTest/TS/UI not run.

## Stage 144 — expanded dense static SDF (source; QA deferred)

Existing staticSdf JSON and native lattice now accept dimensions 3..32; baker, CLI and shared Studio/MCP bakeSdf accept 4..32, including 24 and 32 in the existing picker (default remains 16). Max 32768 floats, 128 KiB + five words for GPU source payload. No shader ABI change: both existing Metal/Vulkan samplers use runtime dimension and x-fastest indexing; current allocation budget already charges the actual payload on initialization and resize. The 16 Mi sample-triangle pair cap stays: 32 cubed accepts at most 512 triangles, with no automatic simplification. Existing 1 MiB graph file cap stays; complex/multiple dense inline volumes may exceed it. No external asset/atlas, acceleration, dynamic SDF or new runtime owner. Native CPU interpolation, GPU eligibility, JSON roundtrip/rejection, mesh work cap and shared editor bake regression source updated; no build/CTest/TS/GPU/visual/performance QA executed. Next: external compact asset/atlas and scalable baking.

## Stage 145 — portable binary SDF interchange (source; QA deferred)

Native SpectraSdfBinary.h exposes lossless little-endian SDFB v1 float32 encode/decode/load, exact dimension/byte bounds, CRC32 over header and samples, atomic output; 32 cubed file is 131108 bytes. Existing CLI writes .sdfbin by extension or legacy JSON. Shared fx_graph_edit importSdf accepts checked project content .sdfbin and a Graph v2 CollisionModule; reads at most the codec byte limit and replaces staticSdf in memory only. Studio indexed asset adapter classifies .sdfbin, picker imports a snapshot with existing stale-result guard. No live graph reference, atlas, compression or new runtime resolution; graph JSON size limit remains. Existing fixtures expanded for exact roundtrip, malformed/corrupt file and guarded import. QA/build/TS/Metal/Vulkan deferred. Next persistent external references through all graph compiler/load paths and GPU storage sharing.

## Stage 146 — persistent external SDF references (source; QA deferred)

CollisionModule staticSdf now accepts either legacy inline object or safe content/<name>.sdfbin string. Native effect parser resolves exact binary values via explicit SdfResourceReader after graph expression evaluation. Missing, corrupt, unsafe and no-reader references fail. projectSdfReader bounds bytes before reading, rejects symlink components; trusted root required. Existing GraphAssets explicit load hashes effective dependencies, reuses unchanged compiled IDs and preserves last good on failure. Native dedicated runtime, batch, cue, pool, client and Studio authoring/scene/preview paths accept resource readers; dedicated device restore identity includes resolved bytes. Preview candidate validation/init share a snapshot. Workspace linkSdf and indexed Studio Link button persist only the path; existing importSdf/bakeSdf still materialize inline copies. No GPU ABI/atlas change. Live file watching, automatic scene/client reload, package import/export closure, full replay provenance and generic asset indexing need separate integration/QA. Compiled EffectDef export still materializes samples. Source fixtures expanded; no build, CTest, TS, GPU or visual checks executed.

## Stage 147 — shared immutable GPU SDF storage (source; QA deferred)

SharedSdfGpuCache deduplicates exact immutable slot-13 material lanes + SDF lattice within one explicit same-Device cache. Hash plus full word equality, bounded 4096 weak entries, no global ownership; GpuPoolWorld creates its own cache, native ComputeEmitterDesc/GraphRuntimeOptions can share an explicit instance. Mutable depth data never shared. Each shared payload reserves its logical bytes once in the canonical Device ledger; emitter/resize reserves private envelopes only (base envelope remains conservative). Last lease destroys via existing RHI lifetime path and retires ledger charge using the maximum observed fence. Vulkan pre-frame initial upload with no observed fence retains the charge conservatively. Pool stats separate shared bytes and entries and sum unique cache storage once. Existing shader bindings, serialized graph, CPU collision and authoring unchanged. Not a packed multi-volume atlas; no cross-pool default sharing, offset addressing or 3D texture atlas. Production Metal allocator fixture added for identical/distinct payloads, actual dispatch, lifetime and budget. No tests/build/Metal/Vulkan/performance/Studio QA executed. Next: complete packed atlas or accelerate mesh baker; retain package/replay gaps from Stage 146.
