## Stage 18 — native FX snapshot capture (2026-11-23; QA deferred)

The independent SpectraFX Graph GPU preview has a one-shot **Capture
Frame** action with native RHI readback, bounded 256/512/768px JPEG
thumbnail, Save JPEG and async capture status. There is no continuous
GPU-to-browser bitmap streaming. The existing authenticated MCP tool
`fx_graph_preview` supports `{"action":"capture","maxSide":512}`
and `{"action":"captureStatus"}`, with results under
`capture:{pending,ready,width,height,mime,image?,error?}`.
The native host enforces 128–768px max dimension, 600 KiB JPEG,
one in-flight capture, ten-second timeout and callback-safe
project-switch/close behavior. No gameplay ECS writes or untrusted
disk path. **Current source intentionally not built or tested per
user request; previous 2/2 CTest applies to older source only.**
Agent handoff: `docs/agent-handoffs/spectrafx-studio-stage18-native-gpu-frame-capture-2026-11-23.md`.

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

# AIVERSE SpectraFX modernization — implementation plan (2026-11-23)

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


Status: **planned multi-stage program, NOT fully implemented**. This document is the technical contract for subsequent agents. The current branch contributes camera/event priority scheduling, not every system below.

## Baseline confirmed in `main` at `87ba5b1439267bea86c570489e8869b3f7641a0e`

- Universal VFX `engine/fx`, `aiverse::spectra` and `fx::SpectraFxSceneHost`; version 1 and version 2 graphs; bounded GPU VM subset and shared CPU world.
- Metal GPU particle path, CPU twin parity report from 2026-11-22; cue queue/scene host, pooled emitters, GPU budget governor and LOD; entity effects declared in game/map data.
- `SpectraGraphWindow` is native/editor-specific and cannot be assumed to be ported to the ongoing `feature/studio-html5` editor. Existing graph-v2 and module interfaces should be reused rather than forked.
- SpectraFX owns particle simulation and scene HDR effects. PRISMA owns bloom/postprocess. LENS owns cameras. PHYSIS owns weather/wind. TerraWeave owns terrain. NEREID owns water. Box3D owns physics. MotionWeave owns animation/poses. No genre-specific copies.

## Current implementation notes (feature branch)

- SpectraScale queued-cue significance: implemented in source, focused Ubuntu
  build and 2/2 CTests passed for a pre-fields revision. Other platforms and
  complete engine acceptance are not yet confirmed.
- Initial SpectraFields acceleration bridge: implemented in source for dedicated
  GPU and CPU-twin emitters; generic provider registration, validation and
  lifecycle tests added. Full environment Data Interfaces, shared-arena GPU
  fields, physics collision, fluids and editor authoring remain future work.
- Focused Linux/macOS PR workflow: `.github/workflows/spectrafx-significance.yml`.

## Stage 4 (2026-11-23) — typed scalar Data Interfaces implemented in source

- The Graph v2 authoring model now supports a bounded `dataInterfaces`
  declaration, a runtime-only `DataInterface` value node, and a strict
  name/slot/fallback validator. See
  `docs/agent-handoffs/spectrafx-data-interfaces-stage4-2026-11-23.md`.
- Existing CPU/Metal VM param0..param7 carries the values without creating
  an incompatible second graph language. Data is sampled once at `burstAt`,
  not continuously per particle. Shared CPU `BatchWorld` is covered by
  source-level tests.
- Only scalar host callbacks and shared authoring helpers are implemented;
  full HTML5 Studio panel, texture/buffer GPU Data Interfaces, real PHYSIS
  adapters, Vulkan compute and fluid/destruction systems remain unimplemented.
- Test status is **pending**, not accepted: focused graph runtime/compiler
  CTest additions and CI workflow were authored, but no native PASS has been
  recorded for the latest Stage 4 commit.

## Stage 5–6 — HTML5 SpectraStudio authoring (2026-11-23; draft PR #115)

Source in `feature/spectrafx-html5-data-interfaces-20261123`, based on draft
PR #113. The `fx_graph` HTML5 dock tab now uses a dedicated Graph v2
Data Interface inspector, native graph/draft operations and real project
Assets texture picker. Stage 6 adds a topology canvas with node drag, zoom,
native catalog, Add/Delete and execution/typed-port Connect/Disconnect through
`studio.op` → `fx_graph_edit`; the front end does not implement a second
graph compiler. Changes save only after native Validate or into explicit
unpublished drafts. Test models are in `apps/studio-web/tests/` and linked
by `npm run test:graphs`. Separate handoffs:
`docs/agent-handoffs/spectrafx-studio-stage5-2026-11-23.md`
and `docs/agent-handoffs/spectrafx-studio-stage6-graph-canvas-2026-11-23.md`.

**Not accepted yet:** current feature SHA has no confirmed Node TypeScript
or Vite PASS, WebView interaction QA or real particle GPU preview. Full GPU
preview/time controls, complex graph function/program editors, advanced
curves/gradient widgets, external PHYSIS adapter, Vulkan compute and
fluid/destruction systems remain separate work.

## Stage 7 — Curve / Gradient key authoring (2026-11-23, unverified UI)

The HTML5 Graph v2 pane now includes `SpectraCurveEditor.tsx` and
`spectraCurveModel.ts`. Existing Curve nodes support key time/value
edits and a static scalar plot, and Gradient nodes support RGBA/alpha
key stops with a static gradient strip. The immutable model validates
1..32 unique normalized keys, finite ±1e7 scalar values and safe RGB(A)
hex strings; it does not invent a new effect format or GPU renderer.
`spectraCurveModel.test.mjs` has been included in `test:graphs`.

**Current test status:** source committed in draft PR #115, CI pending;
do not call TypeScript, Vite, WebView or native graphics PASS on this SHA.
Handoff:
`docs/agent-handoffs/spectrafx-studio-stage7-curve-gradient-2026-11-23.md`.

**Still remaining:** real RHI-owned particle preview and timeline, advanced
Bezier curves, actual hardware scene captures, Vulkan/Metal GLES parity,
PHYSIS adapters, shared arena fields, fluids/destruction and final merge.

## Stage 8 — native render-thread SpectraFX preview (source committed, QA pending)

Stacked draft PR #115 now implements the first **real native Scene preview**
for Graph v2. `StudioSpectraPreview.h` owns one `spectra::GraphRuntime`
on StudioScene's existing render thread/gfx Device. Its playback operations
(`status/load/play/pause/restart/step/seek/parameter/stop`) are allowlisted
through `studio.spectraPreview`; 60 Hz fixed timeline, 60-second cap and
bounded 8-tick seek replay. GPU simulation is scheduled before native render
passes, and the effect is drawn into the still-open Scene HDR pass before
PRISMA resolves. HTML5 Graph panel uses
`SpectraNativePreview.tsx` to control the real Scene, not to render
fake DOM particles. Actual native backend is exposed as Metal GPU or full
CPU fallback; support remains capability-driven.

A hardware CAMetalLayer CTest `studio_spectrafx_preview` and dedicated
macOS CI workflow are included. **Not yet observed as passing on current
SHA.** The preview is displayed in the native Scene window, not embedded
inside the Graph pane. Next stage: exact-SHA compiler/Metal/Windows QA,
frame pixel comparison, safe offscreen surface/panel integration and
provider adapters. Handoff:
`docs/agent-handoffs/spectrafx-studio-stage8-native-preview-2026-11-23.md`.

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

## Stage matrix

| Stage | Feature | Design/implementation requirements | Completion criteria |
|---|---|---|---|
| 0 | Audit/baseline | Diff `main` against active SpectraFX/Studio branches; list actual graph node/compiler/backend coverage; reproduce baseline build | SHA-tagged baseline + native acceptance results |
| 1 | SpectraScale cue priority | Camera/event weighted scheduling, bounded queue, no lifecycle inversion; project JSON opt-in | Native + mobile smoke and full regression; code present in this feature branch but not build-validated |
| 2 | SpectraGraph 2.0 | Typed SSA/VM IR with module schemas, functions/subgraphs, variable lifetime, stable serialization; compile CPU/GPU from same effect; avoid a second incompatible graph format | Compiler fuzz/property tests, shader parity for supported ops, old graph compatibility |
| 3 | SpectraStudio HTML5 | Graph canvas, typed inspector, curves, color gradients, drag-drop assets, parameter search, lockable fields, undo/redo, live GPU/CPU preview, frame stepping, profiling | Browser interaction QA on macOS, captured frames, persisted import/export, UI-scale tests |
| 4 | Vulkan compute backend | RHI compute/upload/barriers/indirect dispatch; feature gates match Metal semantics; explicit CPU fallback for unsupported ops/devices | Real Vulkan tests, GPU parity, buffer lifetime and device-loss cases |
| 5 | GLES/WebGL compatibility | CPU twin and reduced GPU options, strict capability map, no implicit unsupported compute; memory and shader variants | Android GLES3 + WebGL2 smoke and visual QA |
| 6 | SpectraCollision | Depth + SDF + TerraWeave heightfields, collision events with bounded async feedback; Box3D adapters are read-only and presentation-only | Collision screenshots, GPU/CPU parity on agreed collision subset; no authoritative ECS mutation |
| 7 | SpectraGeometry | GPU mesh/sprite, flipbook, ribbon, beam, spline and decal output stages sharing simulation state | Multi-camera render tests, correct transparent order/depth/PRISMA, performance budgets |
| 8 | SpectraFields | Data interfaces for PHYSIS wind/turbulence, TerraWeave terrain, NEREID water, actor sockets and generic vectors; no cyclic includes | Pure adapters, sampling tests, fixed-timestep behavior and explicit fallback |
| 9 | SpectraFluid | Grid smoke/fire/gas temperature/density with sparse adaptive tiles and deterministic authoring cache; optional 2D fallback | Mobile limits, desktop compute/perf tests, real image captures, stable cache serialization |
| 10 | SpectraLighting | Transient particle lights into common light collector, capped clustered injection and distortion scene pass before PRISMA | Cross-backend HDR/Bloom/depth captures; no competing postprocess engine |
| 11 | SpectraDestruction/Surface | Submeshes, debris, material stamps, char/snow/wetness masks, Box3D rigid bodies with visual-only small fragments | Resource retirement and surface lifetime tests; gameplay-safe ownership |
| 12 | SpectraEvents | Bounded GPU-to-GPU child events and spawning, generation-safe handles, per-frame quotas | No recursive unbounded effects, no stuck emitters, rollback tests |
| 13 | SpectraCache | Capture/play/reverse interpolation, versioned files, hashes/checkpoint redaction, no leakage of raw sensitive state | Artifact compat & corruption tests, playback video/frames |
| 14 | SpectraAudio | Render-only audio band envelopes, FFT/beat synchronization, deterministic envelope replay optional | Audio-disabled fallback, sample correctness and latency tests |
| 15 | SpectraProcedural/AI | Preset generator and MCP create/validate/preview/screenshot/profile tool chain, without unsafe arbitrary code execution | Agent can edit asset, collect screenshots, diagnose shader/graph errors |
| 16 | Final QA/perf | Hardware matrix, 10K/100K/1M particle benchmarks, CPU p50/p95, GPU frame p50/p95, memory lifetime, overflow and visibility/fog | Acceptance artifacts on hardware and clean cherry-pick/merge review |

## Non-negotiable runtime contracts

1. `SpectraFxSceneHost::prepareGpu` runs once per presentation tick before render passes; multiple LENS views may prepare camera ordering separately but must never step the same particles multiple times.
2. GPU work is submitted only at safe RHI frame points. Recycled GPU resources remain fenced/quarantined until hardware completion, preserving existing retirement code.
3. CPU fallback preserves authored effect semantics; never silently drop an unsupported module when selecting a backend.
4. Visual presentation is allowed to be nondeterministic, but never writes to authoritative ECS, server simulation, rollback checksums, or network ownership.
5. Quality/LOD may defer or suppress only explicitly cosmetic Play; all Stop/Kill and entity removal cleanup always run. Never weaken visibility checks in order to render hidden opponents' events.
6. Use shared graph and GPU capability/feature contracts. Do not add genre-specific FPS/RTS particle engines, duplicate PRISMA stages, or clone NEREID/PHYSIS/TerraWeave simulation.
7. Every implementation stage must have a **separate** `docs/agent-handoffs/` file describing changed files, APIs, branch and commit SHA, actual tests/results (including skipped tests), limitations and an unambiguous next step. Update `docs/agents/spectrafx.md`, `skills/visuals/aiverse-spectrafx/SKILL.md` and this matrix when behavior changes.
8. Preserve existing versioned assets and source aliases; migration must be explicit, lossless or diagnostic.
9. Do not merge to `main` while native acceptance is missing or another agent's branch has unreconciled changes.

## Performance validation

Record real GPU device model, backend and test project. Suggested scenarios: 10,000 sprites with random births, 100,000 additive particles, 1 million particles under heavy culling, ribbons/mesh output, transparent alpha sort, 16 cameras in one simulated tick, 1,000 entity attachments, queue saturation 10,000 cues, editor hot-reload at 60 Hz, rollback and device recreation. Record per-scenario CPU/GPU p50/p95, peak allocations, dropped/deferred particles, emitted/dropped events and stable image comparisons. Never substitute whole-GPU frame time for SpectraFX-only time.

## Next immediate work after Stage 1

1. Run the existing CMake smoke and `spectrafx_contract` tests on a clean checked-out feature SHA, fix any errors.
2. Verify routes.json `cuePriority` validation and `importance` ranking under real `SpectraFxSceneHost`, including no behavior change when absent.
3. Capture bounded queue profiling and non-regression of rollback/Stop/Kill; add positive/negative property tests.
4. Integrate a typed editor inspector and HTML5 Studio source browser **after** synchronizing `feature/studio-html5` ownership; do not modify that parallel branch blindly.
5. Continue with Stage 2 IR design and capability-based compiler as a separate code+handoff stage.

## Stage 127 — PHYSIS Data Interface snapshot bridge (in progress)

Owner: SpectraFX presentation, existing feature branch. User requested remaining functionality before more test runs. Reuse PHYSIS read-only sampleNatureField; host publishes a bounded immutable 3D lattice at a committed tick, no World pointer on the render thread. Named scalar channels for wind/gravity and atmosphere, signed metres including spherical worlds, explicit missing grid data. Use existing GraphRuntime DataInterfaceRegistry and graph-v2 bindings, no schema or backend ABI changes. Native example and regression source; no compile/CTest until user requests QA. Studio live environment publication is a separate host integration; no fake preview samples. Remaining matrix rows stay open until source and evidence support closure.

Stage 127 source: native PHYSIS snapshot publication and named scalar channels implemented; regression fixture and graph asset added. NOT RUN. Remaining: actual Studio scene host publication, TerraWeave/NEREID/socket inputs, GPU vector/texture data, other matrix features. No claim of live Studio or cross-platform acceptance.

## Stage 128 — water and terrain Data Interfaces (in progress)

Use the existing NEREID plane and spherical sampling functions and TerraWeave authored height snapshots / procedural sphere relief. Share the Stage 127 bounded 3D region and coordinate validation, publish immutable values only, explicit planar/spherical mode and host-selected planet centre/rendered base radius. No physics or world state writes. Acceptance: native provider API, graph authoring through existing schema, negative and missing coverage, radial vs planar distances and teardown. Source and regression fixtures first, QA deferred. Automatic host publication, sockets and full GPU field textures remain separate.

## Stage 128 — water and terrain Data Interfaces (source; QA deferred)

Native adapters: `engine/world/SpectraWaterSnapshot.h` and `engine/world/SpectraTerrainProviders.h`. Capture NEREID plane or sphere on the committed ECS owner; publish immutable sampled depth, thickness, submerged/found, flow, surface position and up axes. Geometry is explicit; mixed worlds can bind separate names. The shared Stage 127 lattice caps sampling at 4096 nodes. TerraWeave authored height snapshots expose height, clearance and reference up; missing tiles are unavailable. Spherical terrain reuses TerraWeave relief with host-selected rendered radius and scale. Up is radial/reference, not a slope normal. Use the existing graph-v2 Data Interface schema. Host integration remains opt-in; no automatic Studio publication or GPU texture field. No tests run.

## Stage 129 — pooled Data Interface integration (in progress)

Audit found GpuPoolWorld compiled graphs but discarded their data-interface bindings. Preserve declarations per effect (not per physical GPU group), resolve each birth against the host's read-only registry, reject missing required data before any GPU/CPU birth, preserve authored fallbacks and per-source values, and retain schema in device-recovery recipes without retaining live provider pointers. Regression source only; QA deferred. Socket and automatic Studio host publication remain subsequent work.

## Stage 129 — Data Interfaces in shared emitter pools (source; QA deferred)

`engine/fx/SpectraGpuPoolWorld.h` now exposes setDataInterfaceRegistry between frames and dataInterfaceStats for the latest sampled birth. The existing graph compiler keeps validated bindings per logical effect, independent of GPU resource-group deduplication. Each manual or continuous birth samples the provider at the birth position, using the source's unchanged base parameters; required missing values reject before allocation/submission, optional values use authored fallback. Device recovery manifests retain binding declarations but never callback pointers; rebind providers after recovery or accepted rollback. Existing standalone GraphRuntime behavior and graph schema are unchanged. Native regression source added. No build/CTest/GPU run. Studio's legacy CPU scene source and live environment publication still need integration; no automatic socket or scene provider claim.

## Stage 130 — scene source and socket bridge (in progress)

Use existing ECS CPU presenter and ParticleSystem parameter snapshots: resolve declared data before initial spawn and refresh future births once per presentation sync, without restarting for sampled changes. Required missing data stops emission; event cues sample at committed event position. Studio scene source registers graph bindings and accepts explicit owner-thread provider registry for both CPU and GPU paths. Reuse View compiled socket identity and prepared pose for native emission transforms; no animation re-evaluation, no new ECS or graph schema. Tests authored but execution deferred. Automatic live world publication remains separate.

## Stage 130 — CPU scene Data Interfaces and prepared socket poses (source; QA deferred)

The native SpectraPresentationSystem now registers validated graph Data Interfaces and samples each attached source once per presentation sync, feeding subsequent births without restarting an emitter for sampled changes. Missing required data prevents or detaches emission under existing killOnDetach policy. Event cues use the same provider at the committed event position. Reset detaches the registry. StudioSpectraSources registers graph bindings for CPU and accepts an explicit immutable owner-thread registry for CPU/GPU via StudioScene::setSpectraSceneDataRegistry. Map/project replacement clears it; native hosts must publish again after activation. Existing isolated FX preview is not bound automatically. `engine/view/SpectraSocketPose.h` resolves precompiled View socket identities against the current model publication and already-evaluated pose, returning position/direction for native CPU/GPU effects; no animation re-evaluation or implicit bind-pose fallback. Source regression fixtures added; no tests executed. Automatic host publication, authored socket selection in Studio, continuous per-particle fields and GPU textures remain open.

## Stage 131 — automatic authored Studio environment (in progress)

Publish actual authored legacy terrain mesh heights and configured NEREID surface samples to scene CPU/GPU sources on document activation/revision. No fake PHYSIS simulation in edit mode, no pretending flat terrain covers planetary or entity terrain. Bounded immutable terrain grid follows the exact preview mesh diagonal and decimation. Native registry override remains explicit. Regression source only; QA deferred. Live gameplay host publication remains separate.

## Stage 131 — automatic authored Studio field publication (source; QA deferred)

`apps/studio-native/StudioSpectraFields.h` captures the actual bounded legacy terrain preview mesh corner grid and samples its triangle diagonal (including preview decimation); no data outside map, hidden terrain, entity terrain or unsupported planetary/world-space edit maps. It also copies NEREID configured scene surface declarations and samples them using the shared water candidate policy, including authored grade, currents and vortices. No live confluence/flood/obstruction simulation in this edit-only provider. StudioScene automatically supplies these fields to StudioSpectraSources on document/terrain revision, with explicit native registry override retained and cleared on project/map replacement. PHYSIS is not fabricated in edit mode; actual committed gameplay snapshots and evaluated sockets still need native game host wiring. Existing schema and authoring tools unchanged. Regression source added; compile and all tests NOT RUN.

## Stage 132 — live gameplay graph sources (in progress)

Implement shared committed-tick field registry coordinator, bounded camera-region PHYSIS/NEREID publication, and native ClientFx CPU graph-source presentation using the existing ECS presenter and ParticleSystem, visible GameScene objects only. Keep legacy effects unchanged and gate planetary client integration until body-local/world transforms are explicit. Native spherical registry capture remains supported. Tests deferred; no shader, persistence or authoritative ECS changes.

## Stage 132 — live committed gameplay fields and CPU graph sources (source; QA deferred)

`engine/world/SpectraLiveFields.h` publishes immutable PHYSIS and NEREID registries per source epoch/tick/region/geometry, reuses unchanged captures and invalidates on replacement. Native ClientFx now consumes authored SpectraSource components for visible prepared GameScene objects and graph-v2 assets, using the existing CPU presenter and ParticleSystem. The flat client uses a quantized 9x5x9 observer region, samples only committed World, preserves existing visibility and one particle update per frame. Graph definitions use a separate bounded namespace; stop/start and backward tick reset source state. Native spherical field publication remains available, but client planet graph sources are deliberately gated until body-local/global transform binding exists. Live terrain and GPU pool client routing are not wired by this stage; no new ECS schema, simulation or second renderer. No compile/CTest/hardware or GUI run.

## Stage 133 — planetary game field coordinates and terrain (in progress)

Keep particle positions body-local, translate provider probes by the simulation body anchor using Fixed, translate water surface points back, and select only NEREID sphere bodies with the matching anchor. Reuse existing GameView placement and terrain surface, sample terrain at visible CPU graph-source positions into an immutable registry. Clear presentation on body switch. Do not conflate camera view-space with simulation coordinates, change authority, or enable planetary GPU pools without their view-space contract. Source/regression fixtures first, QA deferred.

## Stage 133 — body-local CPU graph sources and live terrain (source; QA deferred)

Existing GameView now exposes the prepared planetary object pose and actual surface relief/clearance. ClientFx accepts visible graph sources on the active planet only, using the same placement as drawPlanets, and resets sources on body change. PHYSIS/NEREID captures translate local probes by the Fixed simulation anchor; water surface coordinates are translated back. NEREID's filtered canonical sphere query selects only the matching anchor while preserving the original unfiltered API. Registry identity includes offset/centre/wave time. The client publishes immutable actual terrain values at prepared CPU source positions for both flat and spherical maps; no second terrain or gameplay solver. CPU source sampling only, camera-region nearest weather/water, no transit or other-body emission. GPU pools, authoring sockets, same-tick restore and full per-particle fields remain open. Compile/CTest/hardware/visual QA NOT RUN.

## Stage 134 — live game GPU graph pools (in progress)

Use existing GpuPoolWorld and GpuEcsAttachmentPresenter on the primary ClientFx frame. Explicit scenePoolEmission opt-in, strict GPU eligibility and unit-scale source, same visible scene rows and immutable registry as CPU. Prepare before render pass; draw once into HDR; no second simulation per camera. Preserve CPU fallback when admission fails or on planetary maps. Distance emission with exact-source-only terrain bindings stays CPU. Reset on match/body/rollback, maintain device resource lifetime. Source/regression fixtures first; QA deferred.

## Stage 134 — game GPU graph pool routing (source; QA deferred)

ClientFx prepares the visible graph-source population before the primary scene pass, registers explicit scenePoolEmission GPU candidates with GpuPoolWorld and GpuEcsAttachmentPresenter, resolves immutable live Data Interfaces, and steps once. Unit-scale, strict GPU eligible flat-map graphs route to the pool; failed admission, incompatible or scaled graphs keep the original CPU path. Distance-based births with exact-source-only TerraWeave bindings stay CPU. Primary HDR pass draws the pool once; secondary LENS cameras do not tick or draw the pool yet. Body/match/backward tick reset and failed source/step suppress pooled drawing. Pool uses existing canonical Device allocation ledger and bounded sources/groups. Planetary GPU remains gated; CPU planetary support unchanged. No runtime test executed. No new authoring schema or MCP operation.
