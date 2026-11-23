---
name: aiverse-spectrafx
description: Author and integrate AIVERSE SpectraFX visual effects: versioned graphs, asset variants, ECS source components, event bindings, CPU fallback, editor previews and eventual GPU backends.
---

## 2026-11-23 — Studio Replay/Capture Stages 19–37 (features only; QA deferred)

**Use the native FX viewport, not fake HTML particles.** The HTML5
`SpectraNativePreview` panel supports two JPEG capture slots, split/side-by-side
comparison, pixel heatmap, JSON comparison report, native camera snapshot,
versioned Replay Preset import/export, and optional CPU or Metal allocation seed.

**Native API** via `studio.spectraPreview`:
- `{action:"cameraRestore",camera:{yaw,pitch,distance,targetX,targetY,targetZ,ortho}}`
  restores the *isolated FX camera*, not the game camera.
- `{action:"replayPrepare",tick:120,parameters:{intensity:0.5},seed:1234}`
  validates bounded tick/scalars and prepares dedicated CPU replay. CPU seed
  clears particles and resets xorshift32/UID history.
- For Metal GPU only, replace `seed` with `gpuAllocationSeed`. This
  controls **slot allocation order**, not shader RNG or CPU/GPU parity.
  Both seed modes are mutually exclusive. GPU reset is deferred to the
  native frame and retries at most eight times; monitor
  `gpuSeedPending`, `gpuSeedFailed`, `gpuSeedRetries`.
- Poll `{action:"status"}` until `replayReady:true` before requesting
  `{action:"capture",maxSide:512,requireReplayReady:true}`, then poll
  `{action:"captureStatus"}`. A ready replay does **not** prove GPU
  pixels came from the exact status frame.
- Capture provenance contains render-owner request-time tick, generation,
  backend, camera, and RHI frame/fence observations. Zero tokens mean
  unsupported. `frameExact:false` is intentional: callback does not
  carry exact captured-frame identity.

**MCP:** Native Studio's authenticated FX preview bridge may expose the same
actions through `fx_graph_preview` when routed to Studio. The legacy
`apps/editor/EditorFxOps.cpp` `fx_graph_preview` is a separate graph-v1
editor operation; do NOT assume it implements replay/capture commands.
Verify the actual MCP tool registry and its schema before invoking. Never
claim GPU capture works from standalone/headless MCP.

**A/B analysis:** Native capture provenance may differ from browser-side
request metadata. Inspect the report's `nativeCaptureMismatch` and
`frameExact:false` before interpreting pixel differences. JPEG sRGB
heatmaps are diagnostic, not HDR parity.

**Tests:** user explicitly deferred all exact-source C++/Metal/Vulkan,
TypeScript/Vite, CTest and visual QA. No PASS may be claimed for these stages.
Handoffs: `docs/agent-handoffs/spectrafx-studio-stage19-ab-capture-2026-11-23.md`
through `docs/agent-handoffs/spectrafx-studio-stage36-rhi-capture-observations-2026-11-23.md`.

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

## 2026-11-23 — HTML5 embedded VFX preview and shared MCP tools

When authoring effects, use the `fx_graph_*` tools exposed by
`aiversemcp`. Published and draft assets use existing Workspace
validation. To preview in running native Studio, call
`fx_graph_preview` with `{"action":"load","graphName":"preview_fx"}`,
then `{"action":"play"}` (or seek/step/status/stop).
`{"action":"cameraReset"}`, `{"action":"cameraOrbit","dx":12,"dy":-4}`
and `{"action":"cameraZoom","amount":15}` navigate the independent
embedded FX camera. `load` also accepts inline graph or `draft:true`
for saved draft. Standalone MCP has no GPU: report the native-view
requirement rather than inventing particles. The HTML5 Graph tab hosts
the **same native renderer**, not a React/WebGL reproduction.
Never mutate map entities or ECS from FX preview.
No current-source Metal hardware acceptance yet. Handoffs:
`docs/agent-handoffs/spectrafx-studio-stage11-embedded-preview-2026-11-23.md`
and
`docs/agent-handoffs/spectrafx-studio-stage12-mcp-camera-2026-11-23.md`.



# SpectraFX — universal AIVERSE visual effects

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


**Canonical documentation:** `docs/spectrafx.md`.
**Exact stage history:** `docs/agent-handoffs/universal-niagara-vfx-2026-11-18.md`.
**Engineering rules:** `AGENTS.md`.
**Latest handoff:** `docs/agent-handoffs/inspector-spectrafx-lighting-sync-2026-11-20.md`.

## Mandatory scope boundary

This skill is ONLY for world-space VFX emitters, particles, ribbon/mesh FX, effect graphs and ECS event presentation. For post-process bloom, tone mapping/HDR resolve, LUT/color grading, depth-of-field, motion blur, screen-space AO/GI/SSR, TAA/upscaling, post-process volumes, or the `postfx` Shader Graph domain, use **PRISMA** (`engine/render/Prisma.h`, `docs/prisma.md`). SpectraFX contributes linear HDR scene particles before PRISMA resolves the image; PRISMA never owns particle simulation. See the mandatory ownership section in `AGENTS.md`. Existing `postfx_*` MCP/JSON names are legacy external interfaces, **not** SpectraFX operations.

## Fast path (current supported capabilities)

1. Create `fx/graphs/<name>.vfx.json` with `version:1`, `nodes`, `links`, `outputs`, and optional `parameters`. Supported chain: `Spawn → Initialize → Update* → SpriteOutput|MeshOutput|RibbonOutput`. Node `properties` reuse `engine/fx/FxEffect.h` emitter fields. Example: `docs/spectrafx.md`.
2. In native C++ include `engine/fx/SpectraFX.h` and call `aiverse::spectra::GraphAssets(particles, reader).load(name, overrides)`. Play the returned effect ID with the existing CPU particle runtime; check the `errors` and `fallback` fields. Parameter overrides create independent variants.
3. For continuous ECS-attached effects include `<aiverselib/mechanics/SpectraFX.h>`. Add `spectra::SourceComponent` to any Entity (unit, actor, spell, vehicle, world prop). The renderer-side `spectra::PresentationSystem::sync` reads it and invokes the native project's render-pose resolver.
4. One-shot impact, fire, ability and death effects use `PresentationSystem::consumeTickEvents(tick, events, bindings)`, exactly once per committed tick. Call `reset()` when the client rolls back or starts a new session.
5. Preserve `aiverse_fx`, `fx/<name>.json`, `fx/fx.json`, legacy `GameFx` and older `Vfx*` aliases until production parity tests pass.

## Effects declared in data, CPU/GPU from one asset (2026-11-22)

Full reference: `docs/spectrafx-entity-effects.md`.

- To give units, buildings, characters, vehicles, projectiles, weapons or abilities effects WITHOUT game code, write a
  `"spectraFx": {"entities": {"<selector>": {"attach": [...], "on": {...}}}}` block in rules.json or the map, or an
  inline `"spectraFx"` rule on rules `types.<id>`, their `weapons[]` / `abilities[]` (RTS) or `actor.weapons[i]` (FPS).
  Selectors: `*`, `unit|structure|projectile|actor|vehicle`, `type:<id>`, `weapon:<id|index>`, `ability:<id>`,
  `player:<n>|player:local|player:remote`, `team:<n>`. Cues: `spawned`, `died`, `despawned`, `fired`, `hit`, `ability`.
- Both the RTS client (`apps/client/ClientFx.cpp`) and the FPS client (`SpectraFxSceneHost`) feed the same presenter
  `engine/fx/SpectraFxEntityEffects.h`. New hosts: enumerate entities with selectors, forward cues; never add a
  genre-specific copy.
- An effect name resolves to `fx/gpu/<name>.gpu.json` (GPU compute on Metal, CPU twin of the same kernel elsewhere or
  with `"preference":"cpu"`; `cpuEffect` is optional) or else `fx/<name>.json` (CPU ParticleSystem).
- QA: `AIVERSE_SPECTRAFX_CPU=1` forces the CPU twin in hosts; `AIVERSE_FX_DEBUG=1` prints presenter/back-end counters;
  `AIVERSE_FPS_WATCH=1` turns the FPS view to the nearest remote character for screenshots.
- Tests: `spectrafx_cpu_gpu_parity` (Metal), `spectrafx_entity_effects`, `spectrafx_retirement`.

## Graph editing through MCP (stage 7)

Use the universal editor-core tools when authoring graphs for an open game:

1. `fx_graph_list` and `fx_graph_get {name}` locate and inspect versioned graph assets.
2. `fx_graph_validate {graph|text|name, overrides?}` checks the compiled emitter definition without writing.
3. `fx_graph_set {name, graph|text}` saves only after strict compile validation. Graph names are lowercase `a-z0-9_`; max 48 chars. Invalid sources are not saved.
4. `fx_graph_bake {name, effect, overrides?, overwrite?}` creates `fx/<effect>.json`, consumed by the original CPU renderer and `fx_preview {effect}`. Existing baked effects are protected unless `overwrite:true`.
5. `fx_graph_delete {name}` removes only the graph source; baked effects remain separate.

The MCP commands work in editor and headless `aiversemcp` via `Workspace::call`; they do not depend on RTS or FPS modules. Baked effects do **not** automatically track graph changes. The separate **SpectraFX** window adds a basic graph-v1 canvas, but it is not a full Niagara-equivalent programmable editor.

## Visual editor and ephemeral preview (stage 8, committed but unverified)

- Open **Графика → SpectraFX** in the editor. Select/create a graph, add Spawn/Initialize/Update/Output nodes, drag and connect their pins. Middle mouse pans; the right panel edits properties and exposed parameters as JSON.
- Save calls `fx_graph_set` and reports the exact graph-v1 compiler errors.
- `fx_graph_preview {graph:<JSON>, name?, overrides?, advance?, x?, y?, z?, loop?, freeze?}` is a GUI-only MCP operation. It compiles the **unsaved graph** in memory into the existing CPU particle backend, without creating `fx/*.json`.
- Node `position:[x,y]` is authoring metadata; compilation and runtime effects ignore it. Older graphs without positions remain compatible.
- Check `view_screenshot {window:"scene"}` after a successful preview. All interactive GUI results still require macOS/Windows manual validation; unit-test source presence alone is not verification.

## Typed graph module inspector (stage 9)

The visual **SpectraFX** window now offers a **Типизированные модули**
section per selected node. Use it for common Spawn/Initialize/Update/Output
fields (number, integer, bool, string, choice and Vector3); the editor
validates values against `engine/fx/SpectraModuleSchema.h` and preserves
unrelated node properties. Choose `+` to add a field and `x` to remove
an explicit override. Use **Расширенные свойства (JSON)** for curves,
gradients, nested modules and unsupported fields; a complex value must
**never** be flattened by a typed control.

Native projects may use `spectra::fieldsForNode(type)`,
`spectra::validateTypedField(field,value,error)`,
`spectra::applyTypedField(node,key,value,error)`, and
`spectra::removeTypedField(node,key,error)`, available from
`engine/fx/SpectraFX.h`. The parser `fx::parseEffect` remains the final
source of truth. CTest source was extended but real compilation/tests
have **NOT RUN** in this session. See stage 9 handoff.

## Metal GPU compute prototype (stage 10 — unverified)

The public `engine/fx/SpectraFX.h` also includes
`engine/fx/SpectraGpuParticles.h`. Native presentation clients can
create `spectra::ComputeParticles` with `ComputeEmitterDesc` and
`ComputePreference::Automatic`, then call `burstAt`, `setView`,
`step` **before the render pass** and `draw` **inside the pass**.
Shutdown must precede destruction of `gfx::Device`.

- Metal with `Device::supportsCompute()`: opt-in GPU simulation and
  soft additive quads. The first version supports only burst count,
  speed, lifetime, size, gravityY, drag and tint.
- GL33/GLES3/WebGL2 and current Vulkan: **CPU fallback** via the legacy
  `fx::ParticleSystem`. `RequireGpu` fails without Metal compute.
- This is **not** automatic graph-v1 GPU code generation. Do not route
  arbitrary `EffectDef`, trails, ribbon meshes, collisions, GPU light
  events or untested graph operations into this subset.
- Track `ComputeStats` and `reason()` for diagnostics. No measured
  performance or real Metal shader-compile verification yet.
- Use `aiverse_vfx_gpu_tests` / CTest `vfx_gpu_backend` for CPU,
  capability selection and lifecycle regressions. A native Metal
  offscreen render/readback test and multi-frame fences are still
  mandatory before shipping.

See `docs/spectrafx.md` stage 10 and the last handoff entry for
the exact implementation status and follow-up checks.

## Graph GPU eligibility and transparent CPU fallback (Stage 11)

Use `spectra::GraphRuntime` (public `engine/fx/SpectraFX.h`) for
opt-in **per-graph** backend selection from any native game genre.
It accepts either `initGraph(device, graphJson, overrides, options)`
or `initEffect(device, compiledEffect, options)` and owns one
`ComputeParticles` or full `fx::ParticleSystem` instance. Call
`burstAt`, `setView`, `step` (before scene render pass),
`draw` (inside pass), `clear` (outside encoders) and `shutdown`
(before device destruction).

Never route a graph to GPU based on output node alone. Use
`spectra::inspectGraphGpuEligibility(effect, options)` to check
whether the **whole** effect fits the Metal prototype. Unsupported
shader graphs, random ranges, noise, trails, collisions, lights,
subemitters, custom colours, non-soft textures and multi-emitters
keep all data via original `EffectDef` in the CPU runtime.
`ComputePreference::RequireGpu` returns an error rather than
silently simplifying the graph.

`fx_graph_validate`/`get`/`list` now expose
`metalGpuEligible` and `metalGpuReason`; the SpectraFX graph
editor shows the same result. **Eligibility is not device support
or measured GPU performance.** The existing editor preview is CPU.

Run `aiverse_vfx_graph_runtime_tests`, `aiverse_vfx_gpu_tests`
and the full `ctest -L SpectraFX` suite once a native checkout
is available. Stage 11 is committed but tests and real Metal
shaders have not been verified in this session. Preserve the
per-stage handoff rule.

## Sparse GPU pool and strict runtime failure reporting (Stage 12)

For a Metal `spectra::ComputeParticles` emitter, `step` must run
after `Device::beginFrame` and before any render pass; `draw` runs
inside an open pass. The first prototype now skips compute when no
particles are active and draws only the occupied slot prefix. This
does **not** compact dead slots in the middle, and does not use
GPU-indirect rendering.

Use `stats().alive` and `stats().submittedSlots` separately:
a sparse pool can have 3 live particles with 6 submitted slots.
`stats().gpuFallbacks` and `stats().lostOnFallback` expose
non-seamless transitions; never report a GPU→CPU transition as a
seamless successful GPU simulation frame.

`ComputePreference::RequireGpu` cannot silently use CPU after GPU
device health failure. On such failure, `ready()` becomes false and
`reason()` carries the error. `GraphRuntime::gpuActive()` and
`GraphRuntime::step()` forward the underlying state accurately.
For `Automatic`/`PreferGpu`, a CPU fallback may initialize,
but GPU-resident particles are discarded; a globally unhealthy
graphics device can still prevent CPU **rendering**, so the host
must arrange device recovery where needed.

A focused PR/manual workflow `.github/workflows/spectrafx-validation.yml`
builds CPU + mocked GPU protocol tests on Linux/macOS and the
editor/MCP graph operations on Linux. It is **not** an actual Metal
shader/rasterization smoke test. A successful headless CTest does
not prove Metal parity, timing or performance. The stage-12 source
tests and workflows have not run to confirmed success in this
execution context. Preserve per-stage handoffs.

## Dense Metal billboard gather (Stage 13 — unverified)

The optional `spectra::ComputeParticles` Metal subset now uses a
**CPU-published live-slot index buffer** plus an MSL compute gather
to emit **contiguous GPU vertices**. Regular RHI draw count becomes
`stats().alive * 6` indices instead of the physical-pool high-water
mark. Earlier Stage-12 text about `submittedSlots` including dead
holes is superseded: `stats().submittedSlots` now counts **actual
dense GPU quads** and matches `alive` after a successful step.

The CPU owns only particle-slot lifespan bookkeeping and uploads
4 bytes per live slot each frame; particle movement and vertex
construction remain GPU-resident, with no GPU→CPU readback.
This is **not GPU-side prefix-sum/atomic compaction** and not
GPU-authored indirect draw. Do not claim their performance benefits.
The RHI bind contract now uses compute storage slot **4** for
`liveSlots` and `Uniforms.meta.w` for the post-step alive count.
Stage 13 changes MSL and GLSL sources, the GPU emitter and recording
RHI tests; real Metal shader compilation and visuals are **NOT RUN**.

Keep the GPU path opt-in and restricted to the strict
`GraphRuntime::inspectGraphGpuEligibility` subset. All other
graphs retain their full CPU effects. To verify locally, run
`aiverse_vfx_gpu_tests`, all `ctest -L SpectraFX` tests, then
real Apple Silicon compute/render/readback smoke with staggered
births and expirations. Continue the mandatory handoff log.

## Opt-in GPU atomic compact and indirect draw (Stage 14)

`spectra::ComputeEmitterDesc::gpuIndirect` and
`spectra::GraphRuntimeOptions::gpuIndirect` default to **false**.
When explicitly enabled and the Metal device supports
`supportsGpuAuthoredIndirectDraw()`, SpectraFX runs the
GPU-resident alive counter / dense vertex compaction / indexed
argument generation. GPU compute modes are 3 reset, 0 spawn,
1 integrate, 4 compact and 5 author indirect arguments.
Storage buffers 5 and 6 carry the atomic count and 20-byte
`gfx::DrawIndexedIndirectCommand`. A separate dispatch
barrier is required between modes. Rendering calls
`drawIndexedGpuIndirect`, not the experimental ICB path.

On unsupported devices the GPU emitter retains the earlier
CPU-published live-index gather (or the full CPU particle
backend when compute is unavailable). Never silently enable
GPU indirect on all platforms. The simple additive sprite
eligibility gate remains mandatory. No GPU-resident slot
allocator, alpha sorting, trails, custom shader graphs or
production FPS/RTS migration is implemented.

`gpuIndirectActive()` tells whether the feature was selected;
`stats().gpuIndirectDraws` reports submitted RHI commands.
`stats().alive` and `stats().submittedSlots` are CPU
estimates in GPU-indirect mode, NOT actual GPU counter readbacks.
A failed indirect submission skips the frame's draw and exposes
a reason, rather than using potentially stale CPU-indexed
geometry.

Validation includes the recording RHI regression and
`tests/vfx_metal_compaction_smoke.mm`, built as
`aiverse_vfx_metal_smoke` on macOS. Lack of a Metal device
is a CTest **SKIP**, not PASS. Real Metal rendering and
performance are not verified. See the Stage-14 handoff.

## Cross-instance texture/material batching (Stage 14B)

Use **one shared** `aiverse::spectra::BatchWorld` for independent
native graph-effect instances that should be drawn together. Do not
create a separate `ParticleSystem` or standalone
`GraphRuntime::initEffect(gfx::Device*,...)` per effect source if
cross-source draw batching is required.

```cpp
#include "engine/fx/SpectraFX.h"

aiverse::spectra::BatchWorld shared;
shared.init(device, aiverse::fx::FxQuality::High);
aiverse::spectra::GraphRuntime sourceA, sourceB;
sourceA.initGraph(shared, graphA); // shared CPU sprite renderer
sourceB.initGraph(shared, graphB);
sourceA.burstAt({0,1,0}); sourceB.burstAt({1,1,0});

// Exactly once per frame, outside gameplay ECS:
shared.setView(camera, viewProj, width, height);
shared.update(dt);
// Inside the scene render pass:
shared.draw(fog);  // one draw per compatible material run, not per instance
sourceA.clear();   // kills only sourceA's handles
sourceA.shutdown();
sourceB.shutdown();
shared.shutdown(); // shared BatchWorld must outlive all sources
```

`ParticleSystem` already globally batches *within one system* by
actual RHI texture/atlas handle, blend mode and shader graph; quads
and meshes are not merged into one primitive draw. Different UV
rectangles in the same atlas can batch, even across different effect
assets/instances. **Same texture with different blend or shader
pipeline cannot safely batch**. Large runs split after 16,384 quads
because of 16-bit indices. Keep existing renderer alpha-layer
ordering semantics; do not collapse incompatible material states.

The shared world's `defineEffect` deduplicates equivalent effect
definitions, not visually different effect settings. Each
`GraphRuntime` retains and kills only its own handles (bounded
65,536 handle history). Shared-source `step()`, `setView()` and
`draw()` are no-ops: the **host must call the BatchWorld's frame
methods once**. This optional shared path intentionally forces CPU
simulation and existing RHI renderer; `RequireGpu` fails rather than
silently losing GPU expectations. Independent Metal GPU
`ComputeParticles` objects do not yet coalesce across instances.

Regression: `aiverse_vfx_shared_batch_tests` /
`ctest -R '^vfx_shared_batch_world

VFX are **presentation-only**, never deterministic damage, collision authority, or gameplay rules. The current graph compiler produces `fx::EffectDef`; it is not yet a GPU compute graph. Node canvas, graph MCP CRUD, GPU simulation, GPU collision and automatic FPS/RTS production wiring are **unfinished**. Avoid separate genre implementations.

Use CMake target `AIVERSE::spectrafx` in a native project. Validate with:

```bash
cmake -S . -B build/vfx-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/vfx-test --target aiverse_vfx_graph_tests aiverse_vfx_ecs_tests aiverse_vfx_assets_tests aiverse_vfx_ops_tests aiverse_vfx_gpu_tests aiverse_vfx_graph_runtime_tests -j4
ctest --test-dir build/vfx-test --output-on-failure -L SpectraFX
python3 tools/skills/catalog.py check
```

**Verification status:** these tests are committed, not proven passing. Never mark them successful based on source presence.

**Handoff requirement:** every bounded implementation stage must end with a separate dated append to `docs/agent-handoffs/universal-niagara-vfx-2026-11-18.md` in the SAME branch, including implementation/test commit SHA, exact tests executed and their outputs, API/format changes, risks and the next step.
` uses a recording RHI Device
to assert exactly one draw across two effects using the same atlas,
two draws for incompatible blend states, and source-scoped cleanup.
This is a source test until actual CI confirms a pass. See the
Stage 14B handoff before extending GPU pooling.

## Bounded multi-source ECS cues (Stage 15)

The native `spectra::CueScheduler` owns presentation-only events
and attachment handles over one existing `spectra::BatchWorld`.
It is a shared CPU particle scheduler, NOT a separate RHI renderer or
automatic multi-emitter Metal/GPU runtime. Use the engine's canonical
`engine/fx/SpectraFX.h` for `CueScheduler`, `CueEvent`,
`CueBudgets`, `AttachedCue` and `CueSchedulerStats`. Native ECS
projects include `<aiverselib/mechanics/SpectraFX.h>` for adapter helpers
`syncEcsCueSources(world, scheduler, resolvePose)` and
`acceptSimCueTick(scheduler, committedTick, simEvents)`.

Protocol:
1. Initialize `BatchWorld` once and register complete CPU effect defs or
   graphs using `scheduler.registerEffect`/`registerGraph`.
2. Bind a module-defined `uint32_t` event type to an effect name with
   `scheduler.bindCue` (no FPS-/RTS-specific enum in this library).
3. Once per committed game tick call `acceptTick`, or
   `acceptSimCueTick` for the existing `SimEvent` fixed-point data.
   Duplicate/decreasing ticks do not enqueue extra particle events.
4. Once per presentation frame, set the shared world's view, read ECS
   source components with `syncEcsCueSources` using a **read-only**
   render pose resolver, then call `scheduler.flush()`,
   `shared.update(dt)` **once**, and `shared.draw(fog)` **once**
   inside the graphics render pass.
5. **Before any rollback/replay/new match**, call
   `scheduler.resetForRollback()` so pending events, sources and tracked
   one-shot handles are killed and the tick cursor is cleared.
   It must not clear another producer's effects in the same BatchWorld.
6. Destroy scheduler **before** shutting down BatchWorld/graphics Device.

`CueBudgets` caps queued events, starts per tick/frame, active ECS
attachments, and one-shot handle history. The bounded history
**kills oldest** handles before evicting them so rollback cannot leak
their live particles. Component identity uses `Entity::packed()`
(including generation) and reuses the existing `SpectraSourceComponent`
type, not a new ECS component. Source omission detaches, revision/effect
change restarts. Use `scheduler.stats()` for budget/drop and replay
diagnostics.

Test source: `tests/vfx_cue_scheduler_test.cpp`, CTest
`vfx_cue_scheduler` / target `aiverse_vfx_cue_scheduler_tests`,
now part of `SpectraFX validation` workflow. It covers two game
genres, queue caps, ECS generation reuse, effect handle ownership and
rollback without altering unrelated effects in a shared renderer.
**Not yet verified executed** in this connector-only session. Check
CI logs and run real macOS Metal smoke separately before production use.

## Graph v2: typed dataflow and module stages (Stage 16)

Use `"version":2` in SpectraFX graph assets when you need
math/data port nodes and reusable module stages; `version:1`
remains supported. Public `spectra::compileGraph`,
`fx::compileSpectraGraph`, `GraphAssets`, `BatchWorld`
and `GraphRuntime` use the same automatic version dispatch.

The stage chain still uses execution links Spawn → Initialize →
module stages → Sprite/Mesh/Ribbon Output. Use `NoiseModule`,
`CollisionModule`, `TrailModule`, `LightModule`,
`ForceModule`, `VelocityModule`, `SubEmitterModule`,
`TriggerModule`, and the other typed modules. Their properties
are lowered to the existing strict FxEffect schema; unsupported
properties are not silently discarded.

Value edges use `{"from":"speed_math","to":"init","toPort":"speed"}`
rather than execution links. `{"$node":"math"}` and
`{"$param":"exposed"}` may appear within JSON properties.
`{"$curve":"age_math","samples":33}` samples the finite scalar
expression at normalized ages into an existing CPU lifetime curve.
Data expression nodes include scalar/vector literals, arithmetic,
clamp/lerp, trig, vector dot/cross/normalize, comparisons and Select.
Input overrides are strictly type-checked; cycles, invalid type
combinations and excessive expression depth are compilation errors.

Sample: `examples/spectrafx/typed_noise_trail_v2.vfx.json`.
Compile it through `fx_graph_validate` or native
`spectra::compileGraph`; render via existing CPU fallback.
Do not mistake V2 static/type-checked authoring for arbitrary
runtime Niagara GPU-per-particle graph execution.
The Stage-16 code is committed but testing is intentionally
deferred per user instruction. Record actual tests later.

## Visual v2 authoring and non-destructive drafts (Stage 17)

Open **SpectraFX** in the AIVERSE editor. New graphs use version 2,
while existing version-1 assets remain editable and can be upgraded
via the v1→v2 toolbar action. The grouped node catalog now includes
typed data/math/vector/logic nodes and module stages in addition to
Spawn, Initialize, Update and Sprite/Mesh/Ribbon Output.
Data links connect a value output to a named input property;
execution links remain the separate module chain.
The visual canvas distinguishes data wires from execution wires.

The right inspector offers literal number/bool/vector/colour
editors, editable Curve/Gradient keys and typed controls for
common noise, force, collision, trail, light and render settings.
Complex/custom fields remain editable as JSON without
flattening $node references or unsupported curve types.

Use `fx_graph_node_catalog` to discover valid node classes
and `fx_graph_edit` to perform safe in-memory
`new|upgrade|add|connect|disconnect|deleteNode|setProperty`
operations. Check the returned `validation` object for strict
compiler diagnostics, but do not assume an intermediate invalid
graph is a fatal authoring error.

If the graph is incomplete, call `fx_graph_draft_set` to save
under `fx/graphs/_drafts`; it never replaces a published graph.
Retrieve drafts with `fx_graph_draft_list`/`fx_graph_draft_get`,
and publish via `fx_graph_draft_publish` only after the compiler
accepts the complete graph. Existing `fx_graph_set` remains the
strict direct-publication path. Native tooling reuses
`engine/fx/SpectraAuthoring.h` via the canonical
`SpectraFX.h` include.

The user explicitly deferred tests. This Stage-17 work is
source-committed, not compiler/GUI validated. See the handoff
for exact SHAs and the next functional stage.

## Reusable graph-v2 functions and asset libraries (Stage 18)

V2 graphs may contain a root `functions` object. Each
function declares typed default `parameters`, a pure
value-node `nodes` array, `output` node ID and optional
`returns` = number/bool/string/vec2/vec3/vec4.
A `FunctionCall` data node uses
`properties:{"function":"name","arguments":{"x":...}}`.
Inputs accept ordinary literals, `$param` and `$node`
references; a function called under an age-sampling
`$curve` shares the current normalized-age context.

Functions are pure and bounded (32 definitions, 64
nodes each, nesting depth 8). They cannot execute
gameplay code or depend on FPS/RTS-specific APIs.
Unresolved calls, type mismatches and recursive functions
reject compilation rather than silently substituting
constant values.

Reusability across graphs is explicit, not hidden IO:
`fx_function_list/get/set` work on
`fx/functions/*.vfxfn.json`; `fx_function_import` merges
a validated library into an in-memory graph, and
`fx_function_export` writes embedded function
definitions to a shared asset. Then publish with strict
`fx_graph_set` or use the safe draft flow.
Native projects can reuse `spectra::FunctionLibraryAssets`,
`spectra::validateFunctionLibrary`, and
`spectra::mergeFunctionLibrary`.
Examples: `examples/spectrafx/common_math.vfxfn.json`,
`examples/spectrafx/function_calls_v2.vfx.json`.

Editor: FunctionCall node catalog, selector for defined
functions, root function JSON editor and library
import/export actions. User deferred tests; do not
claim these code changes compile/run until validated.

## Per-particle CPU expression VM (Stage 19)

Per-particle motion, colour, size and eight user scalar
attributes now support a bounded, precompiled expression
program: add a v2 `ParticleProgramModule` with
`properties:{"version":1,"spawn":[...],"update":[...]}`
to the stage chain, or set a legacy emitter's
`particleProgram`. The core compiler/interpreter is
`engine/fx/SpectraParticleProgram.h`; `FxEffect.cpp`
validates and serializes the program, and `ParticleSystem`
executes it per particle using fixed memory, no JSON
parse/heap allocation in the hot loop.

Each program stage `spawn/update/death/collision/trigger`
contains numeric expressions and assignment actions
`set/add/addRate/multiply` to
`position.x/y/z`, `velocity.x/y/z`, `size`,
`rotation`, `tint.r/g/b/a`, or
`attribute0..7`. Named read-only fields include
`ageNormalized`, `age`, `dt` and `uid`.
Age-dependent expressions run at the current particle age
rather than being baked to a static FxRange curve.
`addRate` multiplies by dt for time-scaled motion.
Expressions may use unary/binary/ternary numeric ops, but
never run unrestricted scripts or write ECS/gameplay.

Example `examples/spectrafx/runtime_particle_program_v2.vfx.json`.
Do not route effects containing `particleProgram` onto
the simple billboard GPU backend. Strict full-effect
eligibility must fall back to CPU until GPU program parity
is implemented. Tests were deferred by the user; no CTest,
native build or Metal shader verification has been run.

## Named per-instance CPU particle VM inputs (Stage 20)

Graph v2 supports up to eight **named runtime instance scalars**.
Add a root `instanceParameters` map of names to
`{slot:0..7,default,min,max}` and an
`InstanceParameter` value node with
`properties:{"name":"lift"}`; link it into a
`ParticleProgramModule` expression via
`{"$runtime":"node_id"}`. At graph compile time the node
is lowered to a read-only VM `param0` … `param7`
instruction, but its value is **not** constant-folded.

C++:
- `GraphRuntime::setNamedProgramParameter(name,value,affectExisting)`
  validates against the graph schema and changes only this source.
- `GraphRuntime::setProgramParameter(slot,value,affectExisting)`
  works with raw numbered CPU VM inputs.
- `ParticleSystem::playWithProgramParameters(effect,pos,dir,scale,tint,
  std::array<float,8>)` and the matching `BatchWorld` overload
  set values **before prewarm and Spawn**, so initial particles
  never receive the wrong default.
- `ParticleSystem::setProgramParameter(handle,slot,value,true)`
  updates particles already alive, including after a one-shot's
  emission instance has ended.
- `CueEvent` / `AttachedCue` carry optional `programParams`
  and `programParamMask`, and a continuous source's parameter
  changes do not force an unnecessary emitter restart.

Editor v2 offers **Параметры экземпляра (runtime)** with
validated JSON metadata and sliders for current preview values.
`fx_graph_preview` accepts `instanceValues` as a map of names
to numeric values, uses the mature CPU particle backend, and
keeps overrides when the preview loops.

Use `examples/spectrafx/per_instance_parameters_v2.vfx.json`
for a complete graph example. Source changes are **committed but
not built/tested**: user deferred tests while finishing functions.
Do not enable arbitrary particle VM code on the limited Metal
compute backend; advanced graph effects must retain CPU fallback
and `RequireGpu` must reject loss of behavior. Every code stage
requires a separate handoff commit.

## Author shader graph inputs from particle VM (Stage 21)

A CPU emitter may include a strict 4-lane
`graphInputs` array alongside `shaderGraph`, for example
`["attribute0","attribute1","param0","age"]`.
Source choices: age/lifetime/random/zero,
attribute0..7 and param0..7. The renderer already
exposes these four float lanes as `sg_particle` to the
existing shader graph. The VM writes custom attributes
via Spawn/Update event statements; per-instance parameter
setters from Stage 20 supply param0..7.

The graph/compiler validates and serializes `graphInputs`
and the CPU renderer compiles selector indices once.
Legacy effects without `graphInputs` preserve the
`[age,lifetime,random,0]` shader graph input semantics.
The 40-byte vertex format, atlas/batch material key
and low-tier CPU renderer stay unchanged.

Do not silently route particle VM or custom shader graph
materials to the experimental simple-billboard Metal backend.
Unsupported full effects must use CPU fallback, and
`RequireGpu` must reject unsupported behavior. No GPU
expression compiler or shader material feature parity
has been implemented. Tests are deferred by user request,
so real material output and platform builds remain NOT RUN.

## Experimental GPU particle VM (Stage 22 — opt-in, not tested)

Metal-only `GraphRuntimeOptions::gpuProgram` defaults to
**false**. When explicitly enabled, the compiled graph may
execute one bounded CPU `particleProgram` in GPU compute
provided the **whole** `EffectDef` passes GPU eligibility.
The existing `SpectraGpuProgram.h` fixed ABI `Payload`
is interpreted by the Stage-22 MSL Spawn/Update kernel;
no general material/JIT shader source is generated.

Only scalar finite +,-,*,min,max,abs,negate,saturate,
clamp,lerp and field loads can execute; numeric assignment
actions set/add/addRate/multiply modify position, velocity,
size and tint. Read-only values include age, normalized
age, dt and `param0..param7`. Any collision/trigger/death
stage, attribute0..7, uid, rotation, division, trig,
comparisons, arbitrary shader graph, multiple emitters,
ribbons/mesh effects, or complex fields must use **full CPU
fallback**. `RequireGpu` must reject incompatible programs.

The Metal shader uses storage buffer slots **7** (32-byte
seed parameter blocks), **9** (32-byte persistent per-particle
parameters), **10** (10,256-byte validated VM program) and
existing slot **8** for frame uniforms. Positions/velocity
remain entirely GPU-resident; per-instance scalar values
are captured before Spawn and existing particle values can
be updated via
`GraphRuntime::setNamedProgramParameter(name,value,true)`
with a bounded queued buffer update before the render pass.
The program is limited to 24 statements/192 instructions
and 48 instructions per statement.

`examples/spectrafx/gpu_vm_lift_v2.vfx.json` demonstrates
the strict eligible program. `metalVmEligible` and
`metalVmReason` in graph MCP ops and the editor describe
**asset compatibility**, not real Metal availability.
CPU remains the default, editor preview remains CPU, and
non-Metal RHI still uses CPU. The user requested functions
first and tests later: no native build, CTest, shader
compilation or GPU benchmark has been run for Stage 22.
See the living handoff for exact commits and follow-up work.

## Persistent GPU custom particle attributes (Stage 26)

The opt-in integrated Metal `ComputeParticles` kernel now stores
eight scalar `attribute0..7` registers in a separate
32-byte-per-particle storage buffer (Metal compute slot **11**).
A reused physical particle slot resets all registers at Spawn;
the supported Spawn/Update CPU VM instruction subset may
read/write them without losing values between frames.

Use a graph-v2 `ParticleProgramModule` with a Spawn statement
such as `{"target":"attribute0","value":{"var":"param0"}}`
and an Update statement that reads
`{"var":"attribute0"}` to influence `tint.a`, `size`,
position or velocity. Example asset:
`examples/spectrafx/gpu_vm_custom_attributes_v2.vfx.json`.
Request `GraphRuntimeOptions::gpuProgram=true` explicitly;
all other restrictions of the Metal billboard GPU eligibility
gate remain in force. The full CPU implementation always remains
the correct fallback for unsupported graph modules or platforms.

Do NOT assume the separate experimental
`gpu_program::MetalVmPass` implements attributes: its own
kernel lacks register storage, so it now rejects any payload
reading or writing fields 16–23 rather than silently dropping
data. Likewise, Stage-21 CPU shader-material `graphInputs`
are not yet supported by the simple Metal sprite pipeline.
GPU attributes affect visible output through supported VM
field writes; arbitrary custom shader/material graph rendering
still uses CPU fallback.

The particle ABI remains 64 bytes, expanded Vertex 40 bytes,
VM Payload version 1. No native build, CTest, Metal shader compile,
pixel verification or performance benchmark has run for this
stage by user's explicit deferral. Keep a **separate Stage-26 handoff commit** and record NOT RUN, not PASS.

## Opt-in GPU particle material shader graphs (Stage 27)

The native Metal-only GPU sprite backend now consumes a strict
subset of the **existing** `render::graph::compile(...,"particle")`
material graph, not a new custom shader language. Opt in using
`GraphRuntimeOptions::gpuMaterial=true` or
`ComputeEmitterDesc::gpuMaterial=true`, independently of the
`gpuProgram` and `gpuIndirect` flags.

Place a valid `shaderGraph` object and optional four-source
`graphInputs` array on a simple additive soft-sprite output.
Allowed source names: age/lifetime/zero, attribute0..7, param0..7.
CPU UID-derived `random` is **not** GPU-equivalent and is
rejected; a default particleRandom graph node must have an
explicit compatible graphInputs mapping. The GPU material
compiler supports one `color` output, at most 32 bounded
pure-fragment nodes, no external textures or vertex graph
and only a restricted set of mathematical/colour ops.
All other graphs use the exact original CPU effect,
including its shaderGraph, graphInputs and particleProgram;
`RequireGpu` must refuse unsupported content.

GPU compute storage slot **13** contains four mapped source
indices; the particle compute kernel writes their values to
the preexisting quad vertex float4 at offset 24. Only a
GPU material pipeline needs vertex attribute 3; default
GPU particle sprite rendering continues using the unchanged
three-attribute pipeline. Vertex size stays 40 bytes and
persistent ParticleState stays 64 bytes.

Use example
`examples/spectrafx/gpu_vm_material_inputs_v2.vfx.json`
to bind a named instance input to `attribute0` and
multiply the base sprite colour by it in the particle
shaderGraph. The graph compiler generates matching
GLSL/MSL fragments from trusted templates; do not interpolate
user-provided shader source.

Native compile, CTest, actual Metal MSL, performance and
CPU/GPU visual parity are NOT RUN by explicit user request.
Do not mark the feature shipping-ready until these pass.
After this bounded stage append a separate Stage-27 handoff
to the canonical running handoff file.

## Pooled GPU emitter definitions with shared CPU fallback (Stage 28)

Native use: `spectra::GpuPoolWorld` from the canonical
`engine/fx/SpectraFX.h` (target `AIVERSE::spectrafx`).
This is presentation-only and genre independent.
Initialize once with a `gfx::Device*` and
`PoolWorldBudgets` (maxGpuGroups/maxGroupCapacity/
maxBurstsPerFrame/quality). Register one full
`fx::EffectDef` or compiled v2 `graph` per
logical effect name via `defineEffect` or
`defineGraph`, passing `GraphRuntimeOptions`
for GPU capability/quality/material/VM selection.

Two names sharing the **identical complete definition**
and effective GPU options share **one** persistent
`ComputeParticles` GPU resource group. Do not merge
different effect logic merely because textures match.
GPU-ineligible definitions retain the full effect via the
single internal `BatchWorld`; `RequireGpu` rejects
instead of downgrading.

Frame protocol: after device beginFrame, call
`pool.beginPresentationFrame(camera,viewProj,w,h)`,
submit bounded one-shot `pool.burst(name,pos,dir,tint,
optionalEightScalarParams)` calls, invoke `pool.step(dt)`
**once**, draw with `pool.draw(fog)` **once** inside
the RHI render pass, then call `pool.endPresentationFrame()`.
Use `pool.stats()` for group/draw/byte counts.
`pool.resetForRollback()` wipes this **pool's**
visual effects between frames without touching external
renderer owners. Do not call during active compute/render
encoders. Destroy pool before device teardown.

Instance defaults from graph v2 `instanceParameters`
remain per logical source name, even for shared GPU groups.
`setNamedParameter` is bounded by schema; each burst
snapshots its own `param0..7`, so one source cannot
silently change already alive particles from another
source. Material-only shaderGraph inputs to these
parameters are supported by the strict Stage-27
GPU material path even without a particle VM program.

**Limitation:** This is resource grouping for *identical*
one-shot GPU effects. There is no single cross-material
GPU physical pool, per-source GPU kill/stop, persistent
attached GPU emitters, global alpha sorting or multi-
material indirect batching. Keep the existing
`CueScheduler` / shared CPU `BatchWorld` for persistent
ECS attachments. No RTS/FPS production loop migration
is automatic. Stage-28 native build, CTest, MSL and
GPU screenshot checks are NOT RUN at the user's request.
Record separate Stage-28 handoff with exact SHAs.

## Boundaries and tests

VFX are **presentation-only**, never deterministic damage, collision authority, or gameplay rules. The v1 graph compiler produces `fx::EffectDef`, while the separate opt-in Metal GPU runtime already has evolution, sorting and compaction kernels. Full graph-programmable GPU simulation, renderer/editor integration, and complete cross-platform GPU QA remain unfinished. Do not duplicate SpectraFX by genre.

**Hard dependency rule:** engine/fx must not include any FPS, RTS, Editor or app module and must not accept a concrete genre camera. Use `SpectraFxView` and the generic `SpectraFxSceneHost` API instead; old camera APIs are source-compatible template adapters. The `spectrafx_architecture` CTest enforces direct source boundaries. Host builds are optional integration regressions, not requirements for using core.

Use CMake target `AIVERSE::spectrafx` in a native project. The optional
Metal `spectrafx_metal_rhi` test (Stage 98) compiles production
`GfxMetal.mm` and validates two offscreen views and per-frame resource
retirement. Do not claim this new test PASS before a hardware log exists.
`gfx::Device::readColorRGBA8` is **test/debug only**, never per-frame
FX runtime readback. Validate with:

```bash
cmake -S . -B build/vfx-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/vfx-test --target aiverse_vfx_graph_tests aiverse_vfx_ecs_tests aiverse_vfx_assets_tests aiverse_vfx_ops_tests aiverse_vfx_gpu_tests aiverse_vfx_graph_runtime_tests -j4
ctest --test-dir build/vfx-test --output-on-failure -L SpectraFX
python3 tools/skills/catalog.py check
```

**Verification status:** these tests are committed, not proven passing. Never mark them successful based on source presence.

**Handoff requirement:** every stage needs a dated file in `docs/agent-handoffs/` and an update to `spectrafx-current.md` in the SAME branch, including code SHA, exact tests run/results, API/format contracts, risks and the next step. Do not claim a test PASS until the log exists.


## Multiple views (Stage 99)

Update a `SpectraFxSceneHost` only once per frame through
`prepareGpu(dt,firstView)` and `prepareCpu(dt,firstView)`.
Before **each** sequential camera render pass, call
`prepareRenderView(view)` *outside* the encoder; render the effect
inside that pass with `render(lighting,view)`. Each new camera may
replace the alpha sort index map: do not prepare multiple cameras up
front and then render them out of order. New API is committed, still
awaiting hardware acceptance; see Stage 99 handoff.


## Stage 100: delayed GPU feedback after live scene reset

GPU impact notifications are presentation-only. `HybridPlayback`
automatically invalidates emitter callback epochs on GPU retirement
(Stop/Kill/key replacement/rollback) without prematurely destroying
Metal storage referenced by in-flight commands. Never call teardown
`reset()` in the middle of a live GPU frame.
The third MetalGPU test `spectrafx_metal_rhi` covers deferred callback
invalidations and particle-generation rejection; run it before claiming
native acceptance. Stage 98–100 tests have NOT RUN as of this handoff.


## Stage 101: deterministic playback retirement

The desktop `spectrafx_retirement` CTest exercises the real
`SpectraFxHybridPlayback` resource accounting and deferred stop/reset
through a controlled fake Metal RHI, while the separate
`spectrafx_metal_rhi` checks actual Apple Metal GPU execution.
These newly authored tests have not been run. Keep tested SHAs separate
from Stage 97's verified 8/8 acceptance.


## Stage 102 — strict GPU evidence before release

When validating on a real Apple GPU, use
`AIVERSE_ACCEPTANCE_REQUIRE_METAL=1 bash tools/spectrafx/acceptance.sh build/spectrafx-validation`.
Check the `metal_evidence` stage: `spectrafx_metal_gpu`,
`spectrafx_metal_billboard`, `spectrafx_metal_rhi` must each be
`PASS`; CTest skip code is not evidence of execution.
The Python parser tests are portable, but C++/GPU acceptance on
Stage 98–102 remains unverified until the Mac Studio run is supplied.


## Stage 104 — real Renderer3D integration QA

The macOS `spectrafx_metal_scene` test links production Renderer3D,
SpectraFxSceneHost and a real Metal RHI. It probes two-camera RT output,
foreground depth occlusion and live reset/retirement. Optional PPM frames:
`AIVERSE_SPECTRAFX_SCENE_CAPTURE=/tmp/spectrafx-scene`.
Strict `AIVERSE_ACCEPTANCE_REQUIRE_METAL=1` now requires 4/4 real
MetalGPU tests to PASS; the old 3/3 evidence applies to Stage 103 only.
Do not claim Stage 104 native acceptance before fresh Mac Studio logs.

<!-- Historical incoming Inspector/SpectraFX branch contracts; current integration gates still apply. -->
**Handoff requirement:** every bounded implementation stage must end with a separate dated append to `docs/agent-handoffs/universal-niagara-vfx-2026-11-18.md` in the SAME branch, including implementation/test commit SHA, exact tests executed and their outputs, API/format changes, risks and the next step.

## Stage 29: GPU pool committed-tick one-shot cues

Use `spectra::GpuCueBridge` over an already initialized
`spectra::GpuPoolWorld` to route bounded gameplay cues into
shared GPU emitters or lossless shared CPU fallback. Bind each
game-defined event type to a graph name, call `accept(tick,events)`
**once per committed simulation tick**, and call `flush()`
between the pool's `beginPresentationFrame` and `step` operations.
Native games include `<aiverselib/mechanics/SpectraFX.h>` and use
`acceptSimGpuCueTick` to convert existing `SimEvent` positions;
no VFX state or handles enter authoritative ECS.

Budget limits cover queued events, per-tick admissions and
per-frame submissions, in addition to global pool burst limits.
After gameplay rollback, call `bridge.resetForRollback()` only
outside active render/compute encoders, then replay committed
ticks. This clears the **whole** `GpuPoolWorld`; never share
the pool with unrelated rollback owners if independent retention
is required. Rejected reset preserves pending cues and tick
cursor so callers can retry safely.

**Only one-shots are supported**. For continuous ECS-attached
sources with lifecycle/pose updates use existing CPU `CueScheduler`
until the GPU pool gains generation-safe stop/kill/retarget handles.
No platform test, visual Metal validation or CI success has
been established for this stage (user deferred tests).

## Stage 34 — render-source ownership, continuous emissions, Metal preview

The preceding Stage 29 warning that the GPU pool supports only anonymous
one-shot events has been superseded for **source-owned pooled emissions**.
Use `spectra::GpuPoolWorld::createSource(effectName)` to get a
`PoolSourceId` (world epoch + slot + generation). For a long-lived
presentation source, call `setSourceTransform` from a native project's
read-only render-pose resolver and `configureContinuous` for
rate/distance scheduling (max bursts/frame). Call `pool.step(dt)` once
before scene rendering: it emits bounded births and simulates.
`pauseSource` prevents new births;
`stopSource(source,true)` additionally kills that source's already-live
GPU/CPU particles without clearing unrelated pooled effects.
`retireSource` releases its generation-safe presentation handle.
A committed-tick rollback still invalidates all pool-source handles,
and the existing `GpuCueBridge` continues to handle one-shots.

The GPU backend tags physical particles with a unique pool source token;
selective kill uploads dead-state ranges before subsequent GPU dispatch.
Slot reuse is deferred until a successful ordered compute submission.
These are renderer-only handles; NEVER persist in authoritative ECS,
never derive gameplay collision/damage from VFX, and NEVER claim
Metal GPU fence safety or image parity without executing the native
device-loss and compaction tests.

GUI authoring: the SpectraFX graph editor has a **Metal GPU** preview
button. MCP: `fx_graph_preview {graph, gpu:true}` opts in to the strict
Metal compute subset. Device and entire-graph eligibility are checked;
unsupported content fails explicitly instead of silently falling back
or changing graph semantics. Existing **Предпросмотр** remains CPU.
The Metal preview starts on a later safe scene pre-pass and attaches
a live GPU pool to the existing diagnostic panel.
No verified native editor/Metal screenshots have been captured.

**Required handoff:** `docs/agent-handoffs/spectrafx-stage-34-source-kill-continuous-editor-2026-11-19.md`.
**Verification:** CMake, CTest, editor, MSL, GPU fences, multi-emitter
stress and actual visuals have NOT RUN. Do not report them as passing.

## Stage 35 — native ECS sources and Metal GPU completion fences

Use `<aiverselib/mechanics/SpectraFX.h>` to import
`GpuEcsAttachmentPresenter` for persistent ECS-driven render effects.
This is a **presentation-only read adapter** to the EXISTING
`lib::genre::SpectraSourceComponent` and `Entity::packed()` generation
keys, not another gameplay ECS type or a genre dependency.

Bind an explicit `PoolContinuousEmission` policy per effect name with
`ecsFx.bindEffect(effectName,emission)`. Native host's render-pose
resolver supplies interpolated positions/directions, and calls
`ecsFx.syncCommitted(tick,const World&,resolver)` between the GPU
pool's `beginPresentationFrame` and `step(dt)`. Same committed tick may
be synchronized repeatedly for interpolated poses. A decreasing tick
requires `ecsFx.resetForRollback()` at a safe graphics boundary
before replay; this clears the **entire** pool, so give each rollback
scope its own pool. Missing pose pauses source emission; disappeared
or disabled components retire their sources and obey `killOnDetach`.

The strict pooled GPU path DOES NOT implement the CPU effect's arbitrary
`scale` semantics; attachments with non-unit
`SpectraSourceComponent::scale` must fail explicitly, not silently
change authored visuals. An effect must already be defined in
`GpuPoolWorld`, and the policy must be bound before sync.
Use `ecsFx.detachAll(killLiving)` to release renderer-side source
tracking when the presenter stops while the pool remains alive.
Do not store `PoolSourceId`, GPU tokens, byte buffers or Metal frame
serials in gameplay ECS components.

Metal source kills now encode checked storage blits and put killed
physical slots in a quarantine until `Device::completedFrameSerial()`
confirms that exact frame's command buffer successfully completed.
Do not reuse slots merely after `step` submits a compute pass.
Unsupported RHI completion queries return zero; conservatively
retain the slots. Observe
`PoolCheckpointStats::killedSlotsAwaitingFence` and
`killedSlotsReclaimed`, plus `GpuCheckpointStatus` before claiming
lossless GPU/CPU fallback. This only applies to the experimental,
strict Metal billboard subset.

Editor **Metal GPU** preview: opt-in, not the default CPU path. The
toolbar now has **Play/Pause**, **Freeze**, **Loop**, and **Stop** controls,
and the checkpoint diagnostics show slots awaiting completion.
It must be run on a native Metal editor/device; no evidence of real
Metal device execution or visual parity has been produced.

**Handoff:** `docs/agent-handoffs/spectrafx-stage-35-ecs-attachments-gpu-fences-2026-11-19.md`.
**Status: functional source committed, all native builds, CTest, actual
Metal runs and editor screenshots NOT RUN.** Never claim otherwise.

## Stage 36 — GPU scatter-kill and native ECS example

For native C++ presentation modules, use
`examples/spectrafx/native_ecs_attachments.h` as a reusable
`GpuEcsAttachmentPresenter` integration example and read
`examples/spectrafx/native_ecs_attachments.md` before wiring it.
The example reads the existing `SpectraSourceComponent` and
fixed-point world pose through a const resolver, creates no
new genre-specific ECS VFX components, and separates
`prepare` (pre-pass), `drawInsidePass`, `finish` (post-pass)
and `afterRollback` (safe completed-frame boundary).
Do not call `afterRollback` while the scene encoder is active.

Metal selective GPU source kill now uploads just a compact
`uint32_t` kill-slot list through a checked storage transfer,
binds it at compute buffer slot **14**, and dispatches shader
mode **6** before the canonical Spawn/Update/Compact phases.
This makes highly fragmented kills a single index-buffer
upload and a single compute dispatch, rather than many per-range
64-byte state blits. The source-generated physical slot IDs
remain quarantined until `completedFrameSerial()` confirms
successful completion. The GPU compute shader does not change
authoritative ECS or networking semantics. Never recycle
physical slots based only on CPU submission.

Editor Metal preview additionally supports a one-frame
`Step GPU` while frozen and an opt-in `GPU speed`
multiplier clamped to `0.1x..2x`. Neither feature changes
the CPU preview or gameplay time. Check telemetry for
`killScatterDispatches` and `killIndexBytes` counters.

**Next agent handoff:**
`docs/agent-handoffs/spectrafx-stage-36-scatter-kills-native-ecs-2026-11-19.md`.

**Verification status:** committed source only; CMake,
Objective-C++/MSL, CTest, editor UI, Metal hardware and
performance have NOT RUN. Source-level complexity
improvements are not measured performance gains.

## Stage 37 — staged attachment transactions, GPU reset and timeline

For persistent genre-neutral VFX, `GpuEcsAttachmentPresenter` now
**prevalidates** all declarations and resolved poses, stages new
`PoolSourceId` handles, and only then retires old bindings.
If staging or detach preflight fails, it releases staged sources and
keeps the old presentation bindings. Changes to named graph
definitions are detected through `GpuPoolWorld::sourceBindingCurrent()`
even for paused sources. Plan spare `PoolWorldBudgets::maxSources`
capacity during replacement because staging needs old + new handles
simultaneously. GPU hardware failures *during* commit can still
leave a partial detach; sync returns false and must be retried.

GPU diagnostics now separate device-health, checked upload, compute
encoder, pass ordering and reset failures. Unsupported hardware
health is unknown, never reported as healthy without a Metal device.
A group failing its GPU step must not render stale last-frame vertices.

`ComputeParticles::tryClear()` provides an explicit checked reset.
Reset on Metal **requires Device::beginFrame() to have been called**
and no active render/compute encoder. `GpuPoolWorld::resetForRollback()`
only clears CPU and source book-keeping after all GPU group uploads
have been accepted. A partially failed reset marks the emitter dirty,
blocks further births/simulation/checkpoints, and requires a retry.
Multi-group GPU reset is best-effort, not a globally atomic operation.
See updated native sample:
`examples/spectrafx/native_ecs_attachments.h`.

The opt-in Metal GPU preview has a **forward-only** simulation
timeline with up to 300 fixed 1/60-second step requests, visual
elapsed time, and a **Restart GPU** action at a safe pre-pass phase.
It does not support reverse scrub, arbitrary snapshot restoration,
or gameplay time manipulation. The loop follows visual time, not
the editor wall clock.

**Handoff:**
`docs/agent-handoffs/spectrafx-stage-37-staged-ecs-gpu-reset-timeline-2026-11-19.md`.

**Verification:** source commits and GitHub reads only; C++/Objective-C++,
MSL, CTest, editor screenshots and Metal device/fault testing
have **NOT RUN**. Never claim GPU correctness without execution.

## Stage 38 — retryable GPU reset, recovery manifest and interpolated ECS poses

Use `GpuPoolWorld::resetReport()` to inspect GPU reset progress.
A failed multi-group rollback becomes a **fail-closed**
`presentationBlocked()` state: don't call `beginPresentationFrame`,
spawn new ECS visual sources or draw last-frame GPU vertices until the
reset has been retried in an open graphics frame outside encoders.
The reset's per-group accepted mask tracks **encoded** commands,
not completed GPU work. Loss of the Metal Device requires explicit
native graphics-device recreation; do not treat a later healthy
flag as proof that destroyed resources survived.

Before releasing an unhealthy GPU world, call
`PoolRecoveryManifest recipe=pool.exportRecoveryManifest()`.
Keep the old Device alive while `pool.shutdown()` releases old
resources, then construct a NEW `GpuPoolWorld` on the new Device:
`recovered.importRecoveryManifest(newDevice,recipe)`.
The manifest contains compiled effect definitions, options and
instance defaults, never live GPU particles, checkpoint VM state,
source handles or authoritative gameplay data. The new ECS presenter
must re-sync sources from the committed World after import success.
See `examples/spectrafx/native_ecs_attachments.h` for the explicit
same-pool replacement variant and device lifetime warning.

To interpolate presentation poses without writing floats or GPU
handles into gameplay ECS, use `GpuEcsPoseInterpolator` exported from
`<aiverselib/mechanics/SpectraFX.h>`. It records two committed
render-pose snapshots with exact packed entity generations. Call
`captureCommitted(tick,const World&,resolvePose)` after a committed
simulation tick, then pass `poses.resolver(alpha)` to
`GpuEcsAttachmentPresenter::syncCommitted` during presentation.
Only adjacent ticks of the same entity generation interpolate.
On rollback, reset both the GPU presenter and pose history, but
only after successful safe visual reset.

The editor Metal GPU preview now tracks 180 previous preview
attempts in a **diagnostic-only** CPU-side history. The graph
window can plot the estimated particle count and copy the
history to JSON. This is **not** GPU particle snapshot playback
or backwards timeline seeking.

**Handoff:**
`docs/agent-handoffs/spectrafx-stage-38-reset-recovery-ecs-interpolation-2026-11-19.md`.

**Verification:** Only GitHub source writes and source inspections.
Native C++/ObjC++/MSL compilation, CTest, real editor/Metal testing,
device-loss recovery and CPU-GPU parity have NOT RUN.

## Stage 39 — native recovery events and bounded GPU diagnostics

Use `GpuPoolRecoveryHost` from the native
`<aiverselib/mechanics/SpectraFX.h>` API whenever a project owns a
`GpuPoolWorld`, a read-only `GpuEcsAttachmentPresenter`, and
an optional `GpuEcsPoseInterpolator`. The recovery coordinator
has **no platform Device ownership** and never automatically
creates graphics devices. `poll()` emits a structured
`PoolRecoveryEvent` only when status/reset progress changes:
`RetryResetInGraphicsFrame`, `RecreateGraphicsDevice`,
`InspectFailure` or `None`. Rollback retry requires an active
gfx frame outside encoders. For an irrecoverable Metal loss, the
host recreates `gfx::Device`, keeps the old one alive through
old-pool shutdown, and explicitly calls `replaceDevice(newDevice)`.
The CPU-only catalog restores compiled effect definitions but
NOT live GPU particles or ECS renderer source handles; reattach
from the latest committed `World` after recovery. See
`examples/spectrafx/native_ecs_attachments.h`.

The Stage 39 checkpoint export is a **bounded diagnostic-only**
binary. `ComputeParticles::exportFreshCheckpoint` and
`GpuPoolWorld::exportFreshCheckpoint(groupIndex)` accept only
an existing fully completed, current-revision GPU capture with no
pending births, kills, incomplete reset or asynchronous readback.
The binary cap is 512 KiB GPU state + 16 KiB CPU lifespan mirror
+ 40-byte header. The API never starts a new GPU readback.
The editor can write a `.spfx` file under
`userDataDir()/spectrafx-diagnostics` only when a GPU group is
`exportReady`. This data is neither a portable savegame nor
reverse playback, and cannot prove lossless GPU-to-CPU fallback.

For automatic emission budget control, opt in via
`PoolLoadSheddingPolicy` / `GpuPoolWorld::setLoadShedding`
between frames. Set the continuous-burst budget, manual one-shot
reservation, estimated particle soft limit and in-flight
checkpoint-byte threshold. Sources are served with a rotating
starting slot to avoid permanent order bias. Policy does not
lower manually requested one-shot limits, and lost births are
not queued for expensive future catch-up. `pressureSkipped`
and `continuousFrameBudget` reflect presentation estimates,
not measured GPU load.

**Mandatory stage handoff:**
`docs/agent-handoffs/spectrafx-stage-39-host-recovery-fresh-export-load-2026-11-19.md`.
**Verification status:** GitHub source writes and code inspection
only; CMake/ObjC++/Metal build, CTest, editor startup, actual GPU
checkpoint export, device loss and budget stress have NOT RUN.

## Stage 40 — frozen-revision captures, checked staging and VFX pressure hysteresis

For an editor preview, use **Freeze & capture GPU state** on the
strict opt-in Metal backend. Stage 40 changes the canonical GPU
frame planner so a `step(0)` with no new births, kills or live
VM parameter changes performs only camera-facing gather/compact
(and indirect-arg publication). It does **not** execute Update VM,
so the GPU particle revision does not advance while the emitter
is frozen. The editor queues one bounded asynchronous
`GpuPoolWorld::requestGpuCheckpoint(groupIndex)` after the
pre-pass step, waits for a complete readback on the RHI owner
thread, and only enables export when
`PoolCheckpointStats::exportReadyGroups>0`. This is not
reverse GPU playback or a gameplay save.

Metal input staging for particles, seeds, VM parameters and
live-slot gather maps now uses `updateStorageBufferChecked`.
If an upload or subsequent compute encoder submission rejects
work, the emitter retains CPU slot/birth queues, marks
`GpuCheckpointStatus::stagingRecoveryPending`, invalidates
checkpoint freshness and suppresses stale drawing. The next
successful GPU step retries staging before committing inputs.
Never interpret an accepted Metal blit as hardware completion.
Check `gpuStagingUploadFailures` and
`stagingRecoveryGroups` in pooled/editor telemetry.

`engine/fx/SpectraCheckpointExport.h` now uses a **48-byte V2**
diagnostic header with CRC32. Call
`validateGpuCheckpointDiagnostic(bytes,&header)` to check
same-build magic, version, slot count, byte length and payload
integrity. It is not a portable save format or particle-state
importer. Stage 39's 40-byte V1 is historical and not accepted
by the new V2 validator. Never expose stale or incomplete
GPU checkpoints as restorable data.

`GpuPoolRecoveryHost` now retains its CPU-only effect recipe if
import on a new graphics Device fails. A subsequent explicit
host-side Device replacement can retry without needing to
resurrect the lost old GPU pool. Only authored definitions
and defaults survive; recreate ECS visual sources on success.

`PoolLoadSheddingPolicy::recoverBelowPercent` (default 85,
valid 1..100) prevents automatic VFX bursts from flapping at
the estimated particle/readback soft limits. Check
`particlePressureLatched`, `readbackPressureLatched` and
`pressureHeldFrames`. This remains opt-in, presentation-only,
and leaves explicit one-shot bursts on the normal global budget.

**Mandatory next-agent handoff:**
`docs/agent-handoffs/spectrafx-stage-40-frozen-checkpoint-staging-recovery-2026-11-19.md`.

**Actual verification:** source commits and GitHub re-fetch only;
C++/ObjC++/MSL, CTest, native editor, readback corruption tests,
Metal device-loss retries and hardware GPU performance have
**NOT RUN**.

## Stage 41 — offline SPFX inspection and native one-at-a-time capture

Use `engine/fx/SpectraCheckpointInspector.h` to diagnose V2
`.spfx` files **offline**. It requires no Metal Device or editor
and accepts at most `kGpuCheckpointExportMaxBytes` (540,720
bytes at 4096 slots). `inspectGpuCheckpointDiagnostic(bytes)`
returns explicit error codes for invalid magic/version,
length/bounds/truncation/trailing bytes and CRC32. A valid
structure may still have warning counters for nonfinite GPU
particle values, ages and CPU-lifetime mirror differences.
Do NOT interpret diagnostic data as authoritative ECS,
GPU restore state, portable savegame or replayable timeline.

For an immediate offline report, build the single-file C++20
`tools/spectrafx/inspect_spfx.cpp` utility from the repository
root with `-I.` and run `inspect_spfx --json path/to/file.spfx`.
Read `tools/spectrafx/README.md` for exact build/exit-code
and security limits. Do not ask an agent to invent a new
binary parser, serialization format or GPU readback loop
when this inspector already exists.

Native projects can include
`<aiverselib/mechanics/SpectraFX.h>` and use the new
`GpuPoolCaptureHost` to request ONE bounded asynchronous
GPU group capture. Call `request(groupIndex)` between frames;
after `Device::beginFrame` and BEFORE any new `pool.step`,
call `pollBeforeStep()`; after the frozen `pool.step(0)`
and BEFORE opening the render encoder call
`submitAfterStepBeforePass()`. Once `Ready`,
`takeReady(bytes)` copies only a complete fresh,
CRC-validated capture. Never block with `readBuffer`
inside the render frame or create background graphics
work. On pool rollback/teardown or Device recreation,
`cancel()` before destroying the old resources. The
controller uses a pool-lifetime generation, a request ID,
weak callback lifetime references and a 600-frame-poll
timeout. It cannot cancel submitted Metal DMA; the RHI
remains responsible for eventual callback/budget settlement.

`examples/spectrafx/native_ecs_attachments.h` illustrates
this in a normal render frame, with VFX state frozen while
Queued/InFlight/Ready to preserve checkpoint revision
freshness; committed gameplay World remains unchanged.
If the capture times out or fails it reports an error and
normal render simulation may resume. Source handles,
GPU checkpoint bytes and render poses must never enter
authoritative ECS state. The Stage 39 recovery host now
rejects `replaceDevice(oldDevice)` when the candidate is
the same still-bound graphics object.

**Stage 41 agent handoff:**
`docs/agent-handoffs/spectrafx-stage-41-offline-inspector-native-capture-2026-11-19.md`.

**Actual verification:** GitHub source inspection and commits only.
C++/ObjC++/MSL compilation, executing the CLI,
CTest, malformed-file tests, RHI callback/device-loss tests,
editor screenshot and Metal hardware checks have NOT RUN.

## Stage 42 — redacted diagnostics, group routing and explicit recovery events

The shared GPU checkpoint V2 inspector now has a
**privacy-first report layer** in
`engine/fx/SpectraDiagnosticReport.h`.
Use `gpuDiagnosticReportJson(inspection)` for safe aggregate
diagnostics: no original file path, per-particle world position,
VM/program scalar, raw blob, absolute GPU frame serial, revision
or CRC fingerprint is included. To disclose only world-space
MIN/MAX bounds or GPU identifiers for a specific internal bug,
call with the corresponding
`GpuDiagnosticReportPrivacy::includeWorldBounds` or
`includeGpuIdentifiers` explicitly set to true. Never make
those opt-ins the default.

The offline `tools/spectrafx/inspect_spfx.cpp` CLI now emits
redacted text and `--json` by default.
`--report <safe.json>` saves a local, redacted JSON report
only on user request. `--include-coordinates` and
`--include-identifiers` independently opt into sensitive
aggregate fields. The CLI never copies raw particle/VM payloads
into a JSON report, auto-uploads files, or interprets them
as authoritative ECS saves. The original `.spfx` V2 binary
is STILL sensitive raw GPU state; share it only deliberately.

Multi-group GPU worlds now expose
`GpuPoolWorld::gpuGroupCount()` and
`gpuGroupSummary(index,out)`, containing only checkpoint
freshness, previous GPU step/reset status, estimated particle
counts and memory; no serialized graph source names are
exposed. The editor has an explicit GPU-group selector and
separate buttons for **Save safe JSON report** (default
redaction) versus **Save RAW GPU checkpoint (sensitive)**.
The editor owns capture scheduling only for its own preview
group 0. External GPU world telemetry must be captured
by its NATIVE renderer owner, not from the editor UI.

For native games, `GpuPoolCaptureHost::requestNext(policy)`
routes ONE bounded async request using
`GpuCaptureTargetPolicy::{RoundRobin,LargestEstimatedGroup,
MissingOrStalestCheckpoint}`. Other groups already
capturing are skipped. The old explicit
`request(groupIndex)` stays supported. All captures remain
owner-thread safe-point operations and must obey Stage 32
shared GPU readback budgets. Do not implement parallel GPU
reads per source or bypass the completed-fence quarantine.

`GpuPoolRecoveryHost::requestPresentationReset(reason)` is
a **status/event request only**. Native platform code later
calls `retryPresentationReset()` after opening a gfx frame,
before any render/compute encoder. Status now separates
`ResetRequested`, `ResetRetryRequired` and
`DeviceRecreationRequired`. `PoolRecoveryEvent` has a
monotonically increasing `transitionSequence` (stable
on repeated equivalent polls), pending reset and retained
CPU-only catalog flags, request and Device replacement
attempt counts. Never claim the event code recreates
platform graphics hardware or restores live GPU particles.

**Next agent handoff:** `docs/agent-handoffs/spectrafx-stage-42-redacted-diagnostics-group-routing-2026-11-19.md`.

**Verification status:** source commits and GitHub re-fetch
only. Native compiler/Metal, CTest, CLI file corruption tests,
GPU multi-group capture, editor UI and on-device recovery
have NOT RUN for Stage 42.

## Stage 43 — safe support ZIP, external GPU capture mailbox and bounded host recovery

For an explicitly requested support package, reuse
`engine/fx/SpectraDiagnosticBundle.h` and
`makeSafeGpuDiagnosticZip(inspection,aggregateFrames,out)`.
The result is a bounded **ZIP STORE** archive (max 128 KiB)
with exactly three fixed entries:
`manifest.json`, `checkpoint_summary.json` and
`preview_timeline.json`. It accepts at most 180
sanitized per-frame counters and NEVER includes an
original `.spfx` binary, per-particle positions, scalar
VM registers, graph source names, GPU serials or world
bounds. There is no option to add raw GPU data to this
safe bundle; `GpuDiagnosticReportPrivacy` overrides
cannot change this rule.

The offline utility
`tools/spectrafx/inspect_spfx.cpp` supports an
explicit `--bundle <safe.zip>` destination, and the
editor exposes a distinct **Save SAFE ZIP bundle** action.
Both require a complete V2 source checkpoint and
produce only local files on user action. They never
auto-upload data. Safe filenames do not include the
absolute GPU frame serial, and the editor does not
mix its preview history into another GPU pool's
support bundle.

For external editor/native integration, use
`GpuCaptureRequestMailbox` from the public
`<aiverselib/mechanics/SpectraFX.h>` API. The native
render-host owns the mailbox; the editor receives
a **non-owning** pointer and may only queue a selected
GPU group or request release. The host consumes
requests and runs `GpuPoolCaptureHost` at its OWN
safe gfx frame boundary (after beginFrame, before
render pass), preserving Stage 32 readback budget,
Stage 35 completed GPU fences and Stage 40 checked
staging. The UI must NOT call any Metal/RHI
compute/readback function or mutate ECS.

The native example's `NativeAttachmentExample` exposes
`poolTelemetry()`, `captureMailbox()` and
`recoveryHost()` for external editor attachment.
**Detach those three pointers from Editor before
native host shutdown or destruction.** The mailbox
statuses are only renderer-presentation data;
old sequences are invalidated on rollback,
shutdown or Device re-creation.

The host recovery coordinator now optionally supports
`serviceQueuedResetAtSafeFrame(ownerFrameOrdinal)`:
it retries a queued/failed GPU-group reset with
frame-based bounded exponential spacing (up to 32
owner frames) and returns NoWork/Deferred/ResetSucceeded/
ResetFailed/NeedsNewDevice. No sleeping/background
tasks. `PoolRecoveryEvent` exposes failure counts
and next eligible owner frame. A lost GPU still needs
the native platform to create a distinct healthy
graphics Device. Editor may display recovery status
and queue a visual reset intent, but does NOT execute
GPU commands while drawing its UI.

**Mandatory handoff:**
`docs/agent-handoffs/spectrafx-stage-43-safe-bundle-mailbox-recovery-2026-11-19.md`.

**Verification status:** source commits and repository
readback only; compilation, ZIP extraction, offline
CLI execution, CTest, editor UI, real Metal capture
and Device recovery were NOT RUN. Do not claim otherwise.

## Stage 44 — executable standalone V2 diagnostics and regression gate

The source-only Stage 41–43 V2 diagnostics now have
**actually executed standalone C++20 verification**.
Use the genre-neutral regression
`tests/vfx_spfx_diagnostics_test.cpp` and offline
`tools/spectrafx/inspect_spfx.cpp` rather than
rewriting custom binary/ZIP parsers.

Standalone development commands, from the AIVERSE
root on a C++20 host:

```sh
c++ -std=c++20 -Wall -Wextra -Werror -O2 -I. \
  tests/vfx_spfx_diagnostics_test.cpp -o vfx_spfx_diagnostics_test
./vfx_spfx_diagnostics_test fixture.spfx fixture.zip

c++ -std=c++20 -Wall -Wextra -Werror -O2 -I. \
  tools/spectrafx/inspect_spfx.cpp -o inspect_spfx
./inspect_spfx --json --report report.json --bundle support.zip fixture.spfx

# Existing CMake/CTest integration:
cmake --build build --target aiverse_vfx_spfx_diagnostics_test
ctest --test-dir build --output-on-failure -R '^vfx_spfx_diagnostics$'
```

The source code and test file were mirrored into a
local Linux compiler environment and their `git hash-object`
values checked against the exact six GitHub blob SHAs.
Actual GCC 14.2 C++20 compile/execution,
`-Wall -Wextra -Werror`, CRC/malformed V2 tests,
privacy-default/opt-in tests, C++ ZIP serialization
read by Python `zipfile`, CLI valid/invalid behavior,
a ONE-TEST isolated CMake/CTest, and ASAN+UBSAN
passed. The GPU-independent mirror used minimal
ABI-compatible `render::float4`/checkpoint struct
declarations, NOT the full AIVERSE engine checkout.

A standalone GitHub Actions workflow
`.github/workflows/spectrafx-diagnostics.yml`
runs Linux and macOS compile, CLI and ZIP tests
on push/PR or manual dispatch. The existing
`spectrafx-validation.yml` focused build includes
the CTest executable. GitHub Actions runs were
`queued`/`pending`, not completed when checked;
**do not falsely report a CI pass**.

**Still not run:** full native AIVERSE CMake/build/CTest,
Apple Objective-C++ and MSL, real GPU compute/frames,
renderer-native mailbox, device loss/cancellation
or performance tests. Never conflate portable
SPFX verification with Metal device correctness.

Stage 44 handoff:
`docs/agent-handoffs/spectrafx-stage-44-offline-diagnostics-tested-2026-11-19.md`.

## HTML5 Studio native FX replay and capture (Stages 26–36)

Use the Studio native FX bridge (`studio.spectraPreview`) for deterministic **CPU** replay, not the older GUI-only `fx_graph_preview` when you need tick-controlled capture.

- `{action:"replayPrepare",tick:120,parameters:{},seed:1234}`: CPU-only RNG seed; the runtime resets particle UIDs and RNG before the initial burst. Seed must be an integer from 0 to 2147483647.
- `{action:"replayPrepare",tick:120,parameters:{},gpuAllocationSeed:1234}`: Metal-only *slot allocation* seed. It does **not** seed GPU shader randomness or guarantee CPU/GPU visual parity. Never pass `seed` and `gpuAllocationSeed` together.
- Poll `{action:"status"}` until `replayReady===true`; check `gpuSeedPending`, `gpuSeedFailed`, and `gpuSeedRetries` (maximum 8). Do not treat a matching tick alone as completion.
- Request `{action:"capture",maxSide:512,requireReplayReady:true}`, then poll `{action:"captureStatus"}` for bounded asynchronous JPEG completion. The capture guard and callback registration run in a single native render-owner dispatch.
- Inspect `capture.provenance`: tick, generation, backend, camera, replayReady, request-time frame serial and fence observations. **`frameExact:false`** means the GPU completion callback does not identify the exact captured frame. Never claim fence-exact capture.
- The HTML5 A/B tool compares decoded 8-bit JPEG pixels, not HDR or alpha. Warn when graph generation, native tick, camera or backend differs. Keep provenance attached to comparison reports.

Read handoffs `docs/agent-handoffs/spectrafx-studio-stage30-cpu-replay-seed-2026-11-23.md` through `spectrafx-studio-stage36-rhi-capture-observations-2026-11-23.md` before changing this flow. The current feature branch has **not** undergone the deferred native/TypeScript/Metal QA; do not report source-only features as tested.

## Stage 38 — discoverable MCP native replay tools

The Workspace tool registry now advertises these genre-independent operations through `Workspace::toolList()` and the existing aiversemcp bridge:
- `fx_replay_prepare {tick:120,parameters:{},seed:1234}`: CPU-only deterministic reset and seek. For Metal slot allocation only, use `gpuAllocationSeed` instead of `seed`.
- `fx_replay_status {}`: poll until `replayReady` is true; inspect `gpuSeedPending`, `gpuSeedFailed`, `gpuSeedRetries`.
- `fx_replay_capture {action:"capture",maxSide:512,requireReplayReady:true}`: request native JPEG; poll `fx_replay_capture {action:"captureStatus"}` for result and provenance.

These operations require an attached native Studio FX viewport and an open project. A standalone headless MCP server may list them but cannot execute GPU capture; it returns an explicit unavailable error. The JPEG provenance includes request-time frame serial/fence observations with `frameExact:false`. Do not interpret them as verified capture-completion fence IDs. Tests are deferred, so the new schemas have not been runtime-validated.

## Stage 39 — asset-picker binding helper (authoring only)

`apps/studio-web/src/panels/spectraAssetBinding.ts` exports `bindSpectraNodeAsset(graph,nodeId,property,{guid,path?})` and `readSpectraNodeAsset(...)`. The helper is immutable, requires a GUID supplied by the Studio Assets index, and rejects unsafe paths. Do not fabricate GUIDs, use raw filenames as resolved references, or assume this helper is wired into the Graph Editor picker yet.

**Important:** this is a browser authoring data structure, NOT a confirmed compiler/runtime asset-reference schema. Before exposing it in the UI or persisting graph JSON, inspect the native SpectraFX graph schema and resolve GUIDs to the compiler's expected asset property representation. Existing native graph validation remains authoritative. Source-only changes have not been compiled or tested.

## Stage 40 — reusable indexed asset picker

`apps/studio-web/src/panels/SpectraAssetPicker.tsx` is a standalone React picker. Pass `assets` from the existing project GUID index, `value`, `onChange`, optional `kinds`, and optionally controlled `locked/onLockChange`. It filters by name/path/type/GUID (first 200 results), handles `application/x-aiverse-asset` drag payloads by resolving GUIDs against the supplied index, and rejects unknown external GUIDs. The lock is a **UI signal controlled by the caller**; the parent must actually prevent external selection updates when locked.

Do not claim the picker is already mounted inside the VFX Graph inspector or that the native compiler accepts GUID objects. First inspect compiler property schema and existing Assets index adapter. Do not persist unresolved references. Source-only; tests deferred.

## Stage 41 — lockable graph asset sub-inspector

`apps/studio-web/src/panels/SpectraGraphAssetInspector.tsx` composes the indexed `SpectraAssetPicker` and Stage 39 immutable binding helper. Pass the current graph, externally selected node ID, native asset property name, indexed assets and an `onGraphChange` callback. When locked, external node-selection changes do not retarget the asset field; if the locked node is deleted, the inspector disables editing. Unlocking follows the latest external selection.

This is a reusable sub-inspector, **not yet mounted** in the existing Graph Editor. Before wiring it to the production inspector, confirm the compiler property representation and translate GUID references to supported native values. Do not persist an unvalidated GUID object. Tests remain deferred.

## Stage 42 — authoritative Studio Assets GUID adapter

`apps/studio-web/src/panels/spectraStudioAssetIndex.ts` exports `indexedSpectraAssets(payload)`, converting the **result** of `invoke('studio.assets',{})` to `SpectraIndexedAsset[]`. Native `StudioSession.cpp` already obtains `StudioAssetCatalog` entries from `content/`, whose persistent GUIDs come from `.meta` and whose SQLite index is disposable. The adapter ignores malformed rows and deduplicates GUIDs. Pass its result to `SpectraGraphAssetInspector`; do not generate GUIDs in React.

The adapter does not yet fetch on its own or mount the sub-inspector in the production graph window. The compiler's asset property schema must be checked before saving GUID objects. No builds/tests executed.

## Stage 43 — native catalog fetch and compiler-safe graph properties

`SpectraIndexedGraphAssetInspector.tsx` now fetches `studio.assets` from the native Studio GUID catalog and supports explicit refresh. It wraps the lockable node asset sub-inspector.

`bindResolvedSpectraAsset` resolves a selected GUID against the authoritative indexed entries and writes the **content-relative path string** to the node property (not a GUID object), matching the existing string-based native graph properties such as `SpriteOutput.properties.texture`. Optional importer-kind restrictions are enforced. The GUI can still use the GUID as stable identity for lookup.

**Not yet mounted in the production VFX Graph Editor panel**. Before mounting, identify the actual node selection and graph mutation state and wire `onGraphChange` into the editor's native validation/draft workflow. The source has not been typechecked or run.

## Stage 44 — production HTML5 VFX Graph asset picker mounted

The actual Graph Editor is `apps/studio-web/src/panels/SpectraDataInterfacesPanel.tsx` (not `SpectraNativePreview`). Its SpriteOutput texture section now mounts `SpectraIndexedGraphAssetInspector`, which loads `studio.assets`, supports GUID-indexed search/drag, selection lock, and routes edits through the panel's existing `apply()` function. Existing `Validate`, `Save Draft`, and `Publish` use native `fx_graph_validate`, `fx_graph_draft_set`, and `fx_graph_set`; the picker does not bypass them.

`bindResolvedSpectraAsset` stores `content/<asset-relative-path>` in the SpriteOutput texture property, as required by the established `putSpriteTexture` compiler contract, and rejects unsupported texture extensions. Existing path-only picker UI has been replaced. This stage has not been built or tested.

## Stage 45 — Assets Browser drag compatibility and project refresh

The production Assets Browser (`apps/studio-web/src/components/AssetsPanel.tsx`) uses MIME `application/x-aiverse-content-path`, carrying a content-relative path, **not** the GUID JSON MIME originally assumed by the picker. `SpectraAssetPicker` now accepts this existing MIME and resolves the path through the current native GUID catalog before selecting it; unknown paths remain rejected. Legacy GUID JSON drags are still accepted after index lookup.

`SpectraIndexedGraphAssetInspector` refreshes the native catalog when the active `projectPath` changes or `workspace.changed`/`document.changed` events arrive. The production graph panel passes `projectPath`. Do not bypass the native graph validate/draft/publish path. No build or UI test has been run.

## Stage 46 — resource fields across renderer and material nodes

The production HTML5 graph panel now has a **Graph resource assets** selector for `SpriteOutput`, `TrailModule`, `MaterialModule`, and `MeshOutput`. The first three bind `properties.texture` as a `content/`-prefixed string using indexed texture assets. `MeshOutput` binds `properties.meshes` as a string array, replacing the first entry while retaining later mesh slots; it accepts indexed `model` importer assets. The asset sub-inspector reads the first mesh slot back for picker display.

Native `SpectraAuthoring.h` declares `MeshOutput` default `{"meshes":["builtin:cube"]}` and `VfxGraph.h` requires a meshes property. This stage does **not** prove every model file is supported by the runtime mesh loader. Always validate through native `fx_graph_validate` before publishing. Tests are deferred.

## Stage 47 — MeshOutput multi-slot editing

The production `SpectraGraphAssetInspector` renders a per-slot indexed model picker when `property="meshes"`, with Add (after slot), Remove, Move Up, and Move Down. All operations call `editSpectraMeshSlot` in `spectraAssetBinding.ts`, which preserves graph immutability, requires a nonempty string array, forbids removing the final mesh, caps the list at 32, and validates added/replaced GUIDs against the authoritative Studio model index. Existing `content/`-prefixed paths remain intact. Add initially duplicates the current indexed slot (or first available indexed model), then users choose a replacement. Invalid/unresolved slots are shown but not automatically rewritten.

The production panel's `onGraphChange` still routes through `apply()` and existing native Validate/Save Draft/Publish operations. No compile, UI or GPU tests have been executed.

## Stage 48 — choose-first mesh insertion and runtime format caution

`SpectraGraphAssetInspector.tsx` now opens a per-slot model picker when Add is clicked, and calls `editSpectraMeshSlot(...,'add',...)` **only after** the user chooses an indexed model. Cancel leaves the graph untouched. Pending insertion is cleared when the selected node or property changes. Existing reorder/remove/replace behavior remains.

Native `engine/fx/FxEffect.h` declares `meshes` as `std::vector<std::string>` and documents built-in mesh names and model paths; this proves the data representation, **not** the availability of every importer format in the runtime renderer. Inspect the actual runtime mesh resource resolver before claiming FBX/glTF support. Do not infer that an indexed model is loadable solely from its importer category. No build/tests executed.

## Stage 49 — build-aware model format discovery

SpectraFxSceneHost wires the CPU ParticleSystem mesh loader to meshFromModelFile (FxModelMesh.h), which calls render::loadModel. Model.cpp registers GLTF/GLB unconditionally, OBJ/FBX and legacy formats conditionally, and supports a neighboring source-path + ".glb" fallback when no native loader exists.

StudioSession's studio.assets now reports modelLoadableFormat for model entries using render::isNativeModelFormat and an indexed converted .glb sibling. spectraStudioAssetIndex excludes explicitly false entries. Missing metadata is tolerated for older hosts. This flag describes format availability, not successful decoding, geometry validity, or GPU execution. No tests run.

## Stage 50 — asset diagnostics

Graph resource inspector reports unavailable indexed paths and legacy paths separately. Explicit Check asset calls studio.spectraAssetCheck with an indexed GUID. Native CPU model/image decode uses a catalog-restricted reader with a 64 MiB cumulative input budget and reports gpuVerified:false. This is not a GPU upload test or a decoder memory/time bound. No tests executed; audit typed bridge registration and native compilation before release.

## Stage 51 — typed diagnostics and MCP

Use `fx_asset_check {guid}` in native Studio MCP or typed `studio.spectraAssetCheck`. Results carry code `ok`, `missing`, `unsupported_format`, or `decode_failed`, detail and gpuVerified:false. Headless Workspace has no handler and returns unavailable. Decode failure can include missing dependencies or input budget exhaustion, not necessarily corrupt bytes. No tests run.

## Stage 52 — Metal shader RNG primitive

ComputeParticles::resetGpuShaderSeed(seed) performs checked reset and initializes independent birth seed/ordinal state. Birth state carries two 16-bit seed halves and a 24-bit ordinal in size.yzw; size.x remains billboard size. MSL birthRandomBits/birthRandom01 implement stateless integer hashing. The primitive is not yet consumed by VM/material operations, exposed in GraphRuntime/Studio, or proven checkpoint-safe. Do not claim deterministic visual replay or CPU/GPU parity. Tests deferred.

## Stage 53 — reserved Metal material random lane

MSL materialSource code 2 now computes birthRandom01(p,71u). The existing GPU material eligibility gate still rejects authored `random`, because CPU prand(uid) semantics differ. This is internal shader wiring only; no authored graph can yet select this lane through the normal compiler. GPU VM random remains outstanding. Add explicit opt-in semantics and seed lifecycle before enabling routing. No tests run.

## Stage 54 — GraphRuntime shader seed lifecycle

GraphRuntime::resetGpuShaderSeed delegates to dedicated active Metal ComputeParticles and rejects shared/CPU paths. ComputeParticles::shutdown clears shader seed and ordinal. Existing authored random eligibility remains unchanged; VM/compiler opt-in and Studio replay plumbing are pending. No tests run.

## Stage 55 — explicit GPU material birth random

Native GraphRuntimeOptions and ComputeEmitterDesc expose gpuBirthRandom (default false) and gpuBirthSeed. Material compiler permits graphInputs random lane only when opted in. Eligibility and emitter pipeline compilation receive the option; initialization seeds birth state. CPU fallback retains original UID random semantics, so cross-backend parity is not promised. VM random and Studio/MCP controls remain pending. No tests run.

## Stage 56 — Particle VM birthRandom

Read-only expression `{ "var": "birthRandom" }` is supported by the CPU VM and by the Metal VM with gpuBirthRandom enabled. Field ID 32, MSL stream 1, stable for the lifetime of each particle. CPU uses UID hash; GPU uses seed/ordinal hash, so no parity guarantee. Existing field IDs 0..31 remain unchanged. No tests run.
