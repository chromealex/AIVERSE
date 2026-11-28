## HTML Studio migration

Support full-document JSON edits validated by the native SpectraFX compiler before updating local history.

Restore middle-button navigation on the HTML graph canvas.

Carry saved v1 canvas arrangements into editable v2 graphs during explicit upgrade.

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

## Stage 135 — swept CPU scene collisions (in progress)

Add opt-in CollisionModule scene query flag, bounded read-only segment/radius queries with earliest-hit ordering, conservative signed-distance sweeps and optional Box3D point-ray adapter. Integrate into existing ParticleSystem collision response, lifetime and sub-emitter VM stage; legacy terrain/planes unchanged without opt-in. Use same particle coordinate space (including body-local planets) and explicit host lifetime. GPU depth/SDF shaders and asynchronous GPU feedback remain subsequent work. Author regression fixtures, defer all execution.

## Stage 135 — swept CPU scene collision (source; QA deferred)

CollisionModule scene=true enables the host's read-only bounded sweep registry in the existing CPU ParticleSystem. Existing ground/plane response, collision VM, lifetime loss and sub-emitter hooks remain shared. Up to eight providers; earliest hit wins, invalid hits rejected; configurable query ceiling (default 8192, max 65536 per update). SDF helper traces a conservative signed-distance field with radius and bounded iterations; Box3D optional point-ray adapter uses Scene::raycast, explicitly rejects nonzero radius and requires host-owned read phase. All probes use particle simulation space, including body-local planets. Native ParticleSystem, BatchWorld and direct CPU GraphRuntime setters; no implicit host binding. No GPU depth/SDF collision or GPU event readback yet. Graph module schema and serialized effect flag updated; regression source only; tests NOT RUN.

## Stage 136 — actual terrain and Box3D sphere sweeps (in progress)

Use existing GameView published height/planet altitude and Studio authored preview height snapshot as read-only sampled terrain collision providers. Bounded segment subdivision and contact refinement, explicit approximation (not conservative SDF). Add generic Box3D Scene sphereCast using pinned b3World_CastShape API and existing query filters, expose through optional SpectraFX adapter. No second physics world; no GPU collision claim. Regression source only, execution deferred.

## Stage 136 — sampled terrain and Box3D sphere sweeps (source; QA deferred)

`engine/fx/SpectraTerrainCollision.h` provides bounded sampled crossing/refinement (up to 64 intervals, 10 bisections), using reference up and clearance along it. This is not exact SDF or guaranteed continuous mesh CCD; narrow features can be missed, radius is vertical/radial. ClientFx installs a read-only provider over existing published GameView flat/planet terrain, detaches on stopMatch. Studio's supported flat edit maps publish an immutable actual preview-height collision snapshot on terrain/document change. Hidden/unsupported terrain is unavailable. `physics::Scene::sphereCast` uses native Box3D shape proxy and existing query filters; optional SpectraFX sphere adapter supports nonzero radius, no initial-overlap penetration guarantee. Box3D binding remains explicit host responsibility. No new physics simulation. Regression source added; no compile/tests/GPU QA run. GPU collision and visual authoring remain open.

## Stage 137 — analytic GPU sphere collision (in progress)

Implement authored static simulation-space sphere obstacle (analytic SDF boundary) with swept particle radius on CPU, Metal and Vulkan. Use a strict GPU subset (no host scene/terrain, local planes, collision programs, subemitters or kill/life-loss semantics); existing graphs retain CPU fallback. Extend compute uniform ABI consistently for both allocator paths, keep particle state unchanged. Depth textures, arbitrary SDF volumes and asynchronous events remain separate. Author fixtures, defer execution.

## Stage 137 — analytic GPU static sphere collision (source; QA deferred)

CollisionModule staticSphereRadius (>0) and staticSphereCenter author a sphere obstacle in particle simulation coordinates. CPU and Metal/Vulkan regular and GPU-owned integration implement a swept analytic sphere boundary, particle radius multiplier, bounce and damping; initial penetration resolved along outward direction. The GPU subset requires ground=false, scene=false, no local planes, drag, particle VM, life loss/kill speeds or other unsupported modules; strict existing graph eligibility remains authoritative. CPU preserves full effect; compute fallback descriptor retains analytic sphere. ParticleState and checkpoint layout unchanged; simulation uniform ABI grows to 192 bytes in both shader languages. No depth-buffer or sampled volume SDF, collision event readback, moving obstacles or second impact integration. Collision-enabled checkpoint import remains restricted. Regression source authored, shader compilation/Metal/Vulkan/CTest not run.

## Stage 138 — sampled GPU SDF volume (in progress)

Author a static bounded cubic distance lattice in simulation coordinates (3–16 samples per axis). Trilinear interpolation, bounded segment sampling and refinement, radius/normal/bounce on CPU and both compute languages. Reuse immutable material-source storage binding with a separately gated appended payload and account bytes in the existing allocation envelope. Strict GPU eligibility keeps unsupported combinations on CPU. No texture import, moving scene SDF, depth collision or GPU event claim. Source fixtures only; execution deferred.

## Stage 138 — bounded sampled static SDF volume (source; QA deferred)

CollisionModule staticSdf accepts an object with origin, spacing, dimension (3..16), and dimension³ distances in metres, x-fastest. CPU and both Metal/Vulkan integration paths use trilinear distance/gradient, up to 64 segment samples and 8 refinements, radius/bounce/damping. Probes require initial coverage; no extrapolation or guaranteed CCD. Existing material storage binding appends immutable SDF payload only when enabled; no new binding or ParticleState layout. Allocation accounting includes extra bytes. Strict GPU eligibility accepts one static sphere OR SDF only; unsupported combinations use CPU. No texture assets, depth buffer, dynamic scene volume or GPU feedback. Advanced JSON authoring only. Source regression fixtures authored; no build, shader compilation or tests run.

## Stage 139 — GPU depth history collision (in progress)

Native opt-in ComputeParticles depth collision consumes a bounded 64x64 hardware-depth GPU storage snapshot, matched projection/inverse and explicit frame identity. Metal capture kernel samples a completed scene depth attachment outside the writer pass; no CPU readback. Existing storage compute kernels consume the copied snapshot in Metal/Vulkan; Vulkan native depth capture and automatic game/Studio routing remain separate. Explicit GPU-only configuration, no silent CPU fallback, no depth-as-gameplay authority. Source fixtures only; execution deferred.

## Stage 139 — native GPU previous-depth collision (source; QA deferred)

Native ComputeEmitterDesc::depthCollision requires RequireGpu, excludes static SDF/sphere and VM. MetalDepthCollisionHistory captures actual post-scene depth into 64x64 float storage on GPU; Renderer3D::consumeResolvedSceneDepth provides one-shot after writer pass, before post processing. Host passes snapshot to ComputeParticles::setDepthCollisionSnapshot before next step; checked GPU copy, same device, previous submission within two fences, matched projection/inverse and texture orientation. No CPU readback. Both existing compute shader languages reconstruct local surface from 3 depth samples, reject discontinuities and bounce front-to-back crossings. No automatic host wiring, graph authoring, Vulkan capture producer, continuous mesh CCD, hidden geometry or GPU events. Storage payload appended at material slot 13; uniform ABI now 352 bytes, state ABI unchanged. QA deferred.

## Stage 140 — GPU collision feedback (in progress)

Opt-in bounded GPU contact records from analytic sphere, sampled SDF and depth kernels, preserve VM error counter and particle ABI. Explicit async request after successful step, shared checkpoint bandwidth reservation, stale epoch cancellation, native GraphRuntime and pool APIs, bridge to existing presentation emitter event router. No gameplay events, no automatic child authoring yet. Regression source only; execution deferred.

## Stage 140 — bounded GPU collision feedback (source; QA deferred)

Native ComputeEmitterDesc and GraphRuntimeOptions reportCollisionEvents opt in to a bounded append-only visual contact buffer (64 records per step, count includes dropped). Both Metal/Vulkan integration paths capture corrected particle centre, normal, collision kind and historical slot, using the existing diagnostics storage binding, preserving VM error word zero. Request explicitly once after a successful step using ComputeParticles, GraphRuntime or stepped GpuPoolWorld group; async readback uses existing shared checkpoint reservation, cancels on reset/restore/shutdown. Decoded batch can publish into existing SpectraFxEmitterEventRouter Impact links; no authoritative events or stable per-source attribution. No automatic child authoring or readback scheduling; host controls routing. CPU fallback does not synthesize GPU records. Regression source only; no build/shader/CTest run.

## Stage 141 — pooled collision child routes (in progress)

Reuse authored SubEmitterModule collision entries with a strict pooled GPU subset (one unconditional child, no inheritance flags). Preserve the full CPU effect for fallback, split collision feedback policy from dedicated GraphRuntime, automatically request bounded contact readbacks and dispatch through existing visual event router/command queue into host ParticleSystem. Client and native Studio scene opt in; group-level attribution only, white/unit child with authored orientation, delayed and budgeted visual approximation. Reset and retirement discard pending events. Source fixtures only; QA deferred.

## Stage 141 — authored GPU pooled collision child effects (source; QA deferred)

Existing SubEmitterModule on=collision with one unconditional safe child name and no inheritance flags can opt in via pooled host policy. Dedicated GraphRuntime still rejects sub-emitters; unsupported pooled definitions keep the original CPU graph. GpuPoolWorld automatically requests up to 8 group contact batches per step, stores one completed batch/group and drains at most 32 children per call through existing presentation event router/command queue. ClientFx and Studio scene enable the policy; child asset uses existing fx/<name>.json reader, must be nonlooping/nonrecursive/non-ringbuffer. Child starts at corrected particle centre, unit scale, white tint, own +Y orientation; asynchronous approximate group attribution, no per-source semantics. Old batches discarded after four frame serials, retirement/reset clears weak inboxes. No GPU child execution or standalone preview auto routing. Regression source only; no build/tests run.

## Stage 142 — static mesh SDF baking (in progress)

Add bounded reusable triangle-mesh signed distance baking in engine/bake; exact-position seam weld, closed consistently oriented edge validation, nearest triangle distance and solid-angle winding sign. Adapt existing model mesh loader and output current staticSdf lattice without new GPU format. Native CLI, regression fixtures, explicit bounds/padding and cost limit. No automatic dynamic scene SDF, high-resolution texture atlas or editor/MCP tool yet. Execution deferred.

## Stage 142 — static mesh SDF baking (source; QA deferred)

`engine/bake/MeshDistanceGrid.h` adds reusable offline bounded indexed-triangle distance baking, double precision closest-triangle distance and nonzero solid-angle winding sign. Exact seam weld and closed directed-edge checks reject open/inconsistent/nonmanifold edges and degenerate faces; no self-intersection or nonmanifold-vertex certification. Auto cubic bounds pad one cell around largest extent, 4..16 nodes per axis, max 8192 triangles and 16 Mi sample-triangle pairs. `engine/fx/SpectraSdfBaker.h` reuses existing model bind-pose mesh extraction, accepts affine transform, produces current staticSdf JSON. Native `aiverse_sdf_bake MODEL DIMENSION OUT.json` tool; paste output into CollisionModule staticSdf. No new GPU ABI, runtime mesh scanning, high-resolution texture atlas or Studio/MCP baking button. Regression source only, no build/tests run.

## Stage 143 — editor mesh SDF bake (in progress)

Extend the shared fx_graph_edit operation with in-memory bakeSdf for an existing CollisionModule and checked project content model. Use existing bounded mesh baker, preserve unrelated graph fields, return normal compiler diagnostics; no disk writes until normal graph save. HTML5 resource inspector uses indexed asset selection, dimension and stale-result rejection. No high-resolution format change in this stage. Tests authored only.

## Stage 143 — Studio/MCP in-memory SDF baking (source; QA deferred)

Shared `fx_graph_edit` action `bakeSdf` takes Graph v2 CollisionModule node, project content model path and integer dimension 4..16 (optional expectedProjectPath). Uses existing model loader and bounded offline SDF baker. Dependencies restricted to checked project content, no symlink traversal, total 64 MiB input; no filesystem writes. Returns normal graph/compiler diagnostics, preserves unrelated authoring. HTML5 Graph resource inspector adds CollisionModule model asset picker (search/drop), lattice dimension and Bake/Rebake, rejecting stale graph/project responses. Parent local draft save/publish unchanged; no global undo/redo implementation claim. Native MCP uses same edit action; no new op registry. No high-resolution format, asset texture or dynamic geometry. Build/CTest/TS/UI not run.

## Stage 144 — expanded dense static SDF (in progress / feature delivery record)

Extend the existing bounded inline lattice from 16 to 32 nodes per axis, not a competing collision system. Keep graph schema and GPU source-buffer layout unchanged. Existing dimensions remain valid. Maintain 16 Mi sample-triangle pairs per bake (at 32 cubed, at most 512 triangles); no automatic decimation. The dense float payload grows to 128 KiB plus five words per volume; reuse existing physical allocation budget and GPU indexing, no atlas or external assets yet.

Acceptance: native validation and CPU interpolation at 32; JSON parse/roundtrip and invalid dimension/sample rejection; graph GPU eligibility and both backend shader indexing (real Metal/Vulkan verification pending); native baker and CLI; Studio/MCP in-memory bake at 32 using current path/read guards; legacy fixtures unchanged, high-resolution positive/negative regression source. Skill/docs, explicit compatibility and separate handoff required. No runtime scene/ECS authority or persistence migration. Build, GPU, frontend, performance and interactive verification deferred by owner; publishing/visual evidence pending. Next stage: external compact SDF assets/atlas and scalable mesh acceleration.

## Stage 144 — expanded dense static SDF (source; QA deferred)

Existing staticSdf JSON and native lattice now accept dimensions 3..32; baker, CLI and shared Studio/MCP bakeSdf accept 4..32, including 24 and 32 in the existing picker (default remains 16). Max 32768 floats, 128 KiB + five words for GPU source payload. No shader ABI change: both existing Metal/Vulkan samplers use runtime dimension and x-fastest indexing; current allocation budget already charges the actual payload on initialization and resize. The 16 Mi sample-triangle pair cap stays: 32 cubed accepts at most 512 triangles, with no automatic simplification. Existing 1 MiB graph file cap stays; complex/multiple dense inline volumes may exceed it. No external asset/atlas, acceleration, dynamic SDF or new runtime owner. Native CPU interpolation, GPU eligibility, JSON roundtrip/rejection, mesh work cap and shared editor bake regression source updated; no build/CTest/TS/GPU/visual/performance QA executed. Next: external compact asset/atlas and scalable baking.

## Stage 145 — portable binary SDF interchange (in progress / delivery record)

First bounded external asset step: define a lossless little-endian versioned .sdfbin codec (float32 lattice, exact size, checksum, atomic decode), independent of GPU/renderer dependencies. Add CLI output by extension, native public load via injected reader, read-only project-checked importSdf graph edit and indexed Studio picker. Import materializes the existing inline property in the local draft; never writes files or changes unrelated properties. Verify source fixtures for roundtrip, corruption, truncation, invalid bounds, import and traversal. Preserve prior JSON and default baker. No persistent graph reference, GPU atlas or raised resolution in this stage; those need integrated compiler/resource lifetime work separately. QA deferred; no main integration.

## Stage 145 — portable binary SDF interchange (source; QA deferred)

Native SpectraSdfBinary.h exposes lossless little-endian SDFB v1 float32 encode/decode/load, exact dimension/byte bounds, CRC32 over header and samples, atomic output; 32 cubed file is 131108 bytes. Existing CLI writes .sdfbin by extension or legacy JSON. Shared fx_graph_edit importSdf accepts checked project content .sdfbin and a Graph v2 CollisionModule; reads at most the codec byte limit and replaces staticSdf in memory only. Studio indexed asset adapter classifies .sdfbin, picker imports a snapshot with existing stale-result guard. No live graph reference, atlas, compression or new runtime resolution; graph JSON size limit remains. Existing fixtures expanded for exact roundtrip, malformed/corrupt file and guarded import. QA/build/TS/Metal/Vulkan deferred. Next persistent external references through all graph compiler/load paths and GPU storage sharing.

## Stage 146 — persistent external SDF references (in progress / delivery record)

Use a string content/<path>.sdfbin as an alternative to the existing staticSdf inline object. Resolve through an explicit bounded reader at the authoritative effect parser, after graph property evaluation; no global IO or second collision implementation. Existing no-reader APIs reject external references. Add safe project-root reader, resource-aware graph asset cache and last-good fallback, native runtime options, Workspace linkSdf validation and Studio authoring. Reference bytes must contribute to checkpoint identity if supported by the consumer. Keep inline snapshot import and existing formats compatible. No new GPU ABI, atlas or higher resolution. Acceptance: v1/v2 compilation, missing/unsafe/corrupt dependency rejection, file-only reload, previous-good fallback, saved graph remains compact; CPU/GPU execution, packaging, scene integration and replay require actual QA or explicit gaps in handoff.

## Stage 146 — persistent external SDF references (source; QA deferred)

CollisionModule staticSdf now accepts either legacy inline object or safe content/<name>.sdfbin string. Native effect parser resolves exact binary values via explicit SdfResourceReader after graph expression evaluation. Missing, corrupt, unsafe and no-reader references fail. projectSdfReader bounds bytes before reading, rejects symlink components; trusted root required. Existing GraphAssets explicit load hashes effective dependencies, reuses unchanged compiled IDs and preserves last good on failure. Native dedicated runtime, batch, cue, pool, client and Studio authoring/scene/preview paths accept resource readers; dedicated device restore identity includes resolved bytes. Preview candidate validation/init share a snapshot. Workspace linkSdf and indexed Studio Link button persist only the path; existing importSdf/bakeSdf still materialize inline copies. No GPU ABI/atlas change. Live file watching, automatic scene/client reload, package import/export closure, full replay provenance and generic asset indexing need separate integration/QA. Compiled EffectDef export still materializes samples. Source fixtures expanded; no build, CTest, TS, GPU or visual checks executed.

## Stage 147 — shared immutable GPU SDF storage (in progress / delivery record)

First GPU storage sharing step: deduplicate exact slot-13 immutable material-lane + SDF payload within an explicitly owned same-Device cache, automatically shared by GpuPoolWorld and opt-in for independent native runtimes. Keep existing shader ABI and mutable depth buffers private. Full-byte equality after hash, bounded weak registry, single ledger reservation per shared buffer, last-user fence retirement via existing RHI destroy semantics. Exclude shared bytes from emitter resize envelope; report shared bytes separately and count unique cache storage once in pool stats. Acceptance: actual Metal fixture for reuse, differing payloads, lifetime, one budget charge, last release and fence completion; no new graph property or authoring operation required (transparent optimization). No packed multi-volume atlas or higher resolution yet; QA deferred.

## Stage 147 — shared immutable GPU SDF storage (source; QA deferred)

SharedSdfGpuCache deduplicates exact immutable slot-13 material lanes + SDF lattice within one explicit same-Device cache. Hash plus full word equality, bounded 4096 weak entries, no global ownership; GpuPoolWorld creates its own cache, native ComputeEmitterDesc/GraphRuntimeOptions can share an explicit instance. Mutable depth data never shared. Each shared payload reserves its logical bytes once in the canonical Device ledger; emitter/resize reserves private envelopes only (base envelope remains conservative). Last lease destroys via existing RHI lifetime path and retires ledger charge using the maximum observed fence. Vulkan pre-frame initial upload with no observed fence retains the charge conservatively. Pool stats separate shared bytes and entries and sum unique cache storage once. Existing shader bindings, serialized graph, CPU collision and authoring unchanged. Not a packed multi-volume atlas; no cross-pool default sharing, offset addressing or 3D texture atlas. Production Metal allocator fixture added for identical/distinct payloads, actual dispatch, lifetime and budget. No tests/build/Metal/Vulkan/performance/Studio QA executed. Next: complete packed atlas or accelerate mesh baker; retain package/replay gaps from Stage 146.

## Stage 148 — bounded accelerated mesh SDF baking (in progress / delivery record)

Use a reusable double-precision distance-query BVH in engine/bake for the existing mesh baker. Existing light-baker Bvh is float ray-only and an optional linked target; keep its interface unchanged rather than using it for inexact nearest/winding queries. Balanced deterministic median splits; nearest AABB branch-and-bound and oriented axis-ray winding count, alternate axes on ambiguous projected edges/vertices, exact solid-angle fallback. Preserve closed oriented topology validation and atomic output. Replace static sample-triangle product admission with measured bounded traversal/triangle work (64 Mi operations), max 32768 triangles, still 4..32 lattice. Expose optional native bake stats, adjust Studio hint. Acceptance: compare accelerated values to exhaustive double distance/solid angle on small closed meshes; nested reversed shell and disjoint dense fixture above old 512-triangle cap; topology, dimension, work-budget failures preserve output. No shader/format change, tests and performance measurement deferred.

## Stage 148 — bounded accelerated mesh SDF baking (source; QA deferred)

MeshDistanceQueries.h extracts existing double triangle distance and solid angle queries, adds balanced deterministic median BVH, nearest-distance AABB pruning and signed oriented axis-ray winding with alternate axes and exhaustive solid-angle fallback on ambiguous edges/vertices/planes. Existing light-baker float/ray BVH is not used for double distance/winding; no new renderer dependency. MeshDistanceGrid keeps closed oriented topology validation, exact weld, grid layout and atomic output. Static sample-triangle product cap replaced by 64 Mi counted traversal/triangle operations; max triangles now 32768 subject to actual budget, not a guarantee for every mesh. Native optional stats report node, distance, ray, fallback and retry counts; caller may lower generic work limit. SpectraSdfBaker passes stats through; Studio hint updated. Regression source compares nested reversed shell to exhaustive reference, checks 768-triangle disjoint fixture above previous cap and exact budget rejection preserving output. Resolution still 4..32 and runtime/file/GPU ABI unchanged. No compile/tests/performance measurements run. Next: high-resolution external lattice and packed atlas; previous package/replay gaps remain.

## Stage 149 — high-resolution external SDF (in progress / delivery record)

Extend native dense SDF and binary codec to 128 nodes per axis (64 and 128 presets); retain 32 inline JSON and in-memory Studio bake/import cap. SDFB v2 for >32, same endian layout and checksum; v1 remains readable and writable <=32. CLI high-res output only .sdfbin; Studio/MCP link existing resource, no large IPC or filesystem mutation. Preserve high-resolution source path for effect serialization; native unlinked large volumes require external export, no silent omission. Avoid large JSON expansion in GPU eligibility and pooled group identity; exact sample equality on group reuse. Limit effective external reads per effect to 32 MiB plus header allowance, keep actual GPU budget accounting, increase offline query cap only above 32 (256 Mi operations). Acceptance: bounded native high-resolution sample and binary roundtrip, legacy/invalid version behavior, no-reader and inline cap, linked Studio persistence, GPU eligibility and budget source fixtures. Actual hardware and performance QA deferred. No packed atlas, automatic editor high-res baking to disk or package closure yet.

## Stage 149 — high-resolution external SDF (source; QA deferred)

Native StaticSdfVolume and offline baker accept up to 128 nodes/axis; inline JSON, Studio bake/import and staticSdfToJson remain capped at 32. SDFB v2 encodes >32, v1 remains exact <=32. 64 cubed is 1 MiB, 128 cubed 8 MiB of samples plus 36-byte file framing. Existing linkSdf supports high-resolution files; CLI requires .sdfbin output above 32, Studio explains workflow. No large graph IPC or new file-write op. Effect parser caps effective binary reads per effect at four maximum volumes (32 MiB + headers), high-res effect serialization keeps resolved asset path and rejects unlinked native export. Eligibility compares structural JSON without sample expansion; group key uses compact hash and full exact volume comparison before reuse. Existing GPU source-buffer indexing and actual allocation charge scale with samples; no shader ABI change. Offline high-res traversal budget 256 Mi operations, <=32 remains 64 Mi; spacing minimum 0.01m and topology limits unchanged. 64/128 resolution does not guarantee a successful complex bake. Regression source includes exact roundtrip, native interpolation, GPU schema, compact linked JSON, rejection, Workspace and 128³ Metal sharing fixture. No actual build, bake benchmark, CTest, TS, Metal/Vulkan or UI QA. Next: packed atlas and resource integration gaps.

## Stage 150 — paged heterogeneous GPU SDF atlas (in progress / delivery record)

Extend the existing SharedSdfGpuCache owner with opt-in packed pages and enable it in GpuPoolWorld. Preserve exact-content deduplication, slot 13 binding and uniform size: collisionResponse.w encodes zero=disabled or exact float integer word offset+1, page <=32 MiB ensures representability. Both Metal/Vulkan SDF and material-lane reads use the block base; depth remains private offset zero. Allocate bounded pages lazily, charge actual page capacity once, use checked first-step upload within an open frame, preserve pending births on failure. Reuse freed subranges only after last fence; no compaction/migration of live ranges. Explicit cache limits 128 MiB active pages/16 pages and fallback to existing exact dedicated buffer on admission failure. Diagnostics distinguish active payload bytes, reserved pages and page count. Acceptance: two distinct SDFs share a page and have different offsets; same volume dedup, correct nonzero offset shader contacts, last-use fence protects ranges, released page budget returns; actual Metal/Vulkan regression source. No graph schema/authoring change; QA deferred.

## Stage 150 — paged heterogeneous GPU SDF atlas (source; QA deferred)

SharedSdfGpuCache now offers packed pages, enabled by GpuPoolWorld and opt-in native construction. Different immutable material+SDF blocks share a page, exact duplicates share one offset. 1..32 MiB page capacity, max 16 pages and 128 MiB active atlas capacity per cache, canonical Device ledger charges full page once; standalone exact-size cache fallback if page admission fails. Private emitter envelope admitted before page. First use uploads through checked frame storage update; rejection preserves pending simulation work. Last lease quarantines range until observed GPU fence completes, merges free neighbors; no live relocation. Last page reference retires buffer and ledger charge. Slot 13 and 352-byte uniform layout unchanged; collisionResponse.w now zero or exact float word offset+1, both shader languages adjust SDF and material source reads; depth private unchanged. Diagnostics distinguish page capacity, payload bytes, entries and offsets. Native Metal/Vulkan allocator regression sources check heterogeneous contacts via GPU checkpoint, host and owned allocation, dedup, first-fit range quarantine and canonical ledger return. No build, shader compilation, hardware, CTest or benchmark executed. Page fragmentation, pre-frame dedicated Vulkan upload accounting and previous packaging/replay/editor generation gaps remain. No multi-SDF-per-emitter collision, sparse texture atlas, automatic compaction, cross-pool default sharing or general residency streaming.

## SpectraFX Volumetrics — new feature direction (user approved)

Volume VFX is a world-space presentation domain, not a second PHYSIS/NEREID gameplay solver or PRISMA postprocess. Existing local fog is analytical density/noise; reuse its composition conventions, not its region representation as a substitute for dynamic voxels. Catalog voxel-world/ticks is authoritative block simulation, not this gas solver. Particle, Volume, Hybrid should ultimately use one SpectraFX graph authoring/runtime envelope.

Stages:
- 151: bounded dense CPU gas reference, typed versioned volume asset, SDF obstacle mask, smoke/temperature/fuel/velocity advection, combustion, buoyancy, dissipation, pressure projection, CPU Beer-Lambert ray integration, headless native API and MCP validation/sample diagnostics.
- 152: GPU 3D field kernels (Metal/Vulkan), budgets, dispatch and CPU reference parity.
- 153: real scene HDR depth-aware volume ray march, emission/scattering, lighting, camera inside; existing renderer/PRISMA composition, no second postprocessor.
- 154: unified particle/volume/hybrid graph nodes and native runtime, Studio inspector/viewport, templates for fire, smoke, explosion, magic, fog; save/load, MCP.
- 155: SDF, PHYSIS wind/gravity, particle/volume data interfaces; sparse bricks, adaptive LOD, lifecycle and performance budgets.
- 156: package/replay/authoring closure, actual platform/visual acceptance. Previous SDF asset completion tasks remain open.

### Stage 151 delivery record (in progress)

Implement headless 8..64 dense cell-centred cubic grid at fixed 60 Hz, bounded continuous sources plus explicit impulse, smoke/heat/fuel and velocity, semi-Lagrangian advection, cooling/combustion, gravity-vector buoyancy and host wind, approximate pressure Jacobi projection and solid mask from existing StaticSdfVolume. Validate inputs, no per-step allocations, clamp fields, fixed bounded pressure iterations. CPU reference ray integration returns premultiplied linear radiance/alpha up to host-supplied opaque distance. Strict versioned authoring asset, native API, bounded MCP validation/headless probe. Do not silently accept volume data in legacy particle graph compiler; unified graph/Studio/render GPU integration follows. Acceptance: headless smoke/fire, decay, SDF mask, ray extinction, malformed config and roundtrip fixture source. Tests deferred by owner. No main integration.

## Stage 151 — SpectraFX Volumetrics CPU reference (source; QA deferred)

New volume domain: engine/fx/SpectraVolumeGrid.h and SpectraVolumeAsset.h, exposed by SpectraFX.h. Dense 8..64 cubic gas grid with real changing density/temperature/fuel/velocity; fixed 60 Hz ticks, bounded continuous sources and impulse, combustion, decay, buoyancy along host gravity, wind acceleration, semi-Lagrangian advection, approximate collocated pressure projection, existing SDF obstacle mask. Bounded CPU emission/absorption ray integration clipped by caller opaque distance. Strict v1 authored volume asset, native API, fire/smoke example JSON, Workspace/MCP fx_volume_validate and bounded fx_volume_probe diagnostics. No gameplay authority, no PRISMA duplicate, no actual GPU solver or scene render yet. Particle-only graph remains unchanged; unified particle/volume/hybrid graph and Studio viewport pending. No automatic SDF/PHYSIS binding, dynamic liquids, sparse grid, checkpoint/package integration or measured performance. Tests authored in existing runtime and ops fixtures, NOT RUN. See docs/spectrafx-volumetrics.md. Next GPU kernels and world-space ray marching.

## Stage 152 — GPU dense gas simulation (in progress / delivery record)

Native GPU gas grid through existing Device storage-uniform compute API, Metal/GLSL from common kernel body. Same CPU v1 Config/Source/Asset and SDF mask, explicit backend failure (no hidden CPU fallback). 32-byte aligned GPU cell, current/advected/candidate buffers, uint solid mask, two pressure buffers and divergence. Fixed tick: advect/reaction/forces/source/walls, divergence/reset, Jacobi, project/walls; publish only successful candidate, never overwrite current on a failed tick. First-frame mask upload and clear; queue bounded impulse; explicit source/gravity/wind setters; canonical Device budget and fence retirement. No per-tick CPU field transfer, only initial mask and uniform parameters; debug snapshot between frames. Native renderer-facing borrowed buffer view, no scene drawing yet. Acceptance: real Metal/Vulkan fixture compares density/temperature/fuel/velocity against CPU with tolerances, obstacle and impulse, clear, resource budget and failure handling; tests deferred. No new graph schema or duplicate simulation authority.

## Stage 152 — dense GPU gas simulation (source; QA deferred)

GpuGasGrid and SpectraVolumeGpuShaders implement Metal/Vulkan native dense 8..64 gas simulation through the shared Device storage-uniform compute ABI, same authored Config/Source/Asset and obstacle sampling as CPU. 32-byte cells, triple state buffers, uint solid mask, double pressure and divergence (112 bytes/cell = 28 MiB at 64 cubed); exact logical buffer reservation via canonical Device ledger, fence-retiring shutdown. First-frame checked mask upload and clear. Fixed 60 Hz advection/reaction/forces/sources/walls, divergence/pressure reset, bounded Jacobi, projection/walls; successful candidate publication only, failed tick retains previous completed state and queued impulse. CPU impulse is immediate, GPU queued impulse applies after advection on next accepted tick. No automatic CPU fallback. Borrowed frame-scoped GPU field view and explicit diagnostic snapshot, no normal-step voxel readback. Native API public through SpectraFX.h. Actual GPU render, lighting, graph/Studio and checkpoint/package integration not installed yet. Metal/Vulkan allocator regression source compares CPU fields/velocity with tolerances, SDF mask, wind, impulse, invalid reinit, budget denial and clear. Shader compilation, CMake/CTest, hardware/parity/performance NOT RUN. Next Stage 153 real depth-aware HDR volume rendering.

## Stage 153 — GPU world-space volume rendering (in progress / delivery record)

Use SpectraFX-owned native compute ray marcher consuming GpuGasGrid field buffers and the scene HDR/depth textures, output separate RGBA16F storage image with premultiplied emission/absorption composited onto scene. Reuse existing TextureResources RHI layout (no new descriptor or PRISMA effect type); reconstruct per-pixel ray using PostFrame inverse VP and render-target/depth conventions, intersect AABB and clip to opaque depth. Trilinear density/heat, Beer-Lambert, anisotropic sun plus ambient and bounded self-shadow ray, up to four explicit local point lights. One-shot frame/view-scoped external HDR scene contribution hook in PRISMA before metering/bloom/tonemap; host owns volume renderer, no reverse engine render->fx dependency or extra tone mapping. One volume per native dispatch; multiple may be composed explicitly, overlapping volumes not unified. Bounded steps, pixels, GPU ledger, checked compute dispatch and fence retirement; no CPU voxel readback. Acceptance source: real GPU volume render fixture with opaque occlusion, nonzero emission, empty volume and HDR/ledger checks, GLSL compile source; tests deferred. Scene/Studio automatic volume ownership and graph authoring follow.

## Stage 153 — native GPU HDR volume renderer (source; QA deferred)

SpectraGpuVolumeRenderer.h + SpectraVolumeRenderShaders.h consume current GpuGasGrid cells and solid mask directly, no CPU voxel readback. Compute uses existing TextureResources layout, separate RGBA16F storage output, matching opaque scene depth/color, inverse VP reconstruction and AABB clipping, camera-inside/ortho-compatible ray construction, trilinear fields, Beer-Lambert extinction, HDR temperature emission, ambient/sun Henyey-Greenstein scattering and bounded density self-shadowing, up to four explicit point lights. Bounds: 128 primary steps, 8 shadow steps, <=4,194,304 pixels; one volume per dispatch. Canonical Device ledger output (8 B/pixel) and fence retirement. PRISMA generic frame/view-scoped one-shot SceneHdrContributor callback after local fog before metering/bloom/tonemap; host owns renderer, no PRISMA volume shader or effect duplicate. HDR-capable native Metal/Vulkan only; no silent LDR fallback, no automatic host/Studio wiring yet, no scene shadow-map sampling, no unified overlapping media, no transparent-depth correctness or performance claims. Existing allocator fixture sources test HDR emission >1, opaque clipping, empty grid, allocation return and Vulkan shader compilation. CMake/CTest/shader/real GPU/visual acceptance NOT RUN. Next unified volume graph and Studio/scene host integration.

## Stage 154A — volume/hybrid graph authoring and native preview (in progress)

Extend Graph v2 with one independent ordered VolumeDomain → VolumeSource(s) → VolumeAdvection → VolumeCombustion → VolumePressure → VolumeOutput chain. Split into the existing particle graph compiler and the strict native volume asset parser; no implicit cross-domain edges or ignored volume output in particle-only hosts. Preserve existing particle v1/v2 and file paths; native compilePresentationGraph, MCP graph edit/validate/save/get and catalog, presets, canvas inspector, dedicated HDR Studio dock preview using GpuGasGrid and GpuVolumeRenderer. Native preview uses existing 60 Hz playback/clear/seek, rejects unsupported checkpoints/replay for volumes; hybrid is co-execution, not bidirectional coupling. Limit first delivery to one volume per graph, no parameter wiring into volume nodes, no runtime game scene auto-instantiation. Acceptance source: pure volume and hybrid compile, invalid cross-link/order rejection, edit/save/reopen, real preview integration source. No actual build/UI tests until feature pass. Stage 154B: game runtime/world integration, richer typed properties and timeline semantics, then Stage 155 coupling and sparse/LOD.

## Stage 154A — volume/hybrid Graph v2 and Studio preview (source; QA deferred)

SpectraVolumeGraph.h provides compilePresentationGraph splitting one independent ordered volume branch (VolumeDomain, repeated VolumeSource, VolumeAdvection, VolumeCombustion, VolumePressure, VolumeOutput) from existing particle Graph v2. Strict no cross-domain/value links, one output and ordered chain; native asset parser remains authoritative. Native presets fire/smoke/magic/fog, existing fx_graph_edit newVolume/addVolume, catalog, validation, save/get/draft; legacy particle bake rejects volume rather than omitting it. Canvas preset creation and selected volume property JSON editor, native reload; StudioSpectraPreview loads GPU grid/renderer alongside optional particle runtime with candidate-before-swap, native fixed ticks and HDR dock contribution (map contribution only if HDR enabled). Volume-only uses continuous sources; hybrid executes both, no exchange/coupling or cross-runtime transactional tick. Replay/checkpoint unsupported and rejected. Native status shows volume resolution, logical bytes and successful HDR command submission (not completion). No automatic gameplay instance, explosion impulse authoring, typed data ports, fully specialized inspector, sparse/LOD, scene shadow maps or package closure yet. Tests and examples source, actual CMake/TypeScript/Metal/Vulkan/Studio QA NOT RUN. Next Stage 154B finish game runtime, typed inspector and hybrid controls, then Stage 155 coupling/sparse.

## Stage 154B — authored impulses and scene volume instances (in progress)

Extend volume asset v1 with optional one-shot source and duration, graph VolumeSource mode=impulse; explosion preset uses a finite injection with no continuous fuel. Shared native VolumeInstance owns GPU grid and HDR renderer, anchors local asset and SDF to a scene source position, fixed 60 Hz bounded stepping, explicit retrigger and reset. Bounded SceneVolumeWorld uses existing SpectraSource identity/revision from Studio/client presentation snapshots, no ECS component or gameplay authority; first delivery flat maps, unit scale, fixed anchor until revision, immediate removal on detach, up to four instances, bounded ticks and back-to-front sequential HDR contributions (not unified media). Hybrid particle branch stays on existing presenter. Native API/preview shares instance semantics; typed inspector and obstacle selection follow if not delivered in this pass. No main merge, tests deferred. Acceptance source: authored impulse and explosion roundtrip, fixed anchor/lifetime, scene source validation, native render integration.

## Stage 154B — scene volume sources and authored impulses (source; QA deferred)

Volume asset v1 now supports one optional impulse, duration .001..1 seconds and signed radial acceleration in Source. Graph VolumeSource mode continuous/impulse, one impulse per branch; Explosion preset injects heat/fuel/density and outward velocity once, not persistent fuel. CPU reference init injects immediately; GPU queued impulse applies on first successful tick. Native VolumeInstance owns placed grid + HDR renderer; local domain, source and SDF origins translated by scene anchor. Studio preview reuses instance; Restart rearms authored impulse, Clear cancels, Trigger explosion queues one pulse and paused preview requests a tick. SceneVolumeWorld admits at most four fixed-anchor volumes, four admission attempts/sync, four ticks/frame, revision/effect changes recreate, detach removes immediately; back-to-front approximate per-volume HDR composition, no unified overlap. Existing SpectraSource ECS identity/visible presentation reused in ClientFx and StudioSpectraSources; hybrid particle branch still existing presenter. Flat maps/unit scale only, source movement does not translate live volume, rotation and planet support pending. HDR must be active, no forced map quality change. Studio typed numeric/vector/mode/path fields replace manual JSON and expose optional settings; obstacle path typed manually (indexed picker pending). Tests source for preset/codec/radial impulse, anchor/revision/detach and admission; no actual builds, GPU, TS or UI acceptance. Next Stage 155: coupling, planet/moving volumes, PHYSIS, sparse/LOD, and scene diagnostics/authoring gaps.
