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


## 2026-11-23 — HTML5 Studio embedded GPU preview and MCP control (Stage 11/12, QA pending)

The SpectraFX Graph HTML5 dock now hosts a real native particle preview
directly inside the graph authoring surface. Its HTML rectangle repositions
Studio's **existing render-thread-owned Scene GPU surface**, switching to
an isolated FX stage/camera with a neutral dark grid and no game-map
objects. The renderer, `spectra::GraphRuntime`, GPU fences and device
retirement remain native. Scene camera is restored when the FX pane
closes. This is NOT a fake React canvas, second graphics context, or
browser video stream. When Scene and Graph are both visible, Graph
temporarily takes priority for the shared native surface.

The existing `aiversemcp` Workspace registry already supports graph
list/get/node_catalog/edit/validate/set/delete/bake and draft
list/get/set/publish. The new `fx_graph_preview` MCP tool controls
the same real Studio runtime as the Graph tab:

```json
{"action":"load","graphName":"preview_fx"}
{"action":"play"}
{"action":"pause"}
{"action":"step"}
{"action":"seek","tick":120}
{"action":"restart","play":false}
{"action":"parameter","name":"wind_lift","value":3}
{"action":"cameraOrbit","dx":12,"dy":-4}
{"action":"cameraZoom","amount":15}
{"action":"cameraReset"}
{"action":"status"}
{"action":"stop"}
```

`load` also accepts `graph` (inline JSON object) or
`graphName` with `draft:true`. Only the active project's sanitized
graph/draft paths are resolved. Standalone/headless MCP advertises this
tool but returns a clear `requires an open native Studio GPU viewport`
error; it never simulates or claims a fake GPU frame.
Preview actions have no authoritative gameplay/ECS writes. Native
camera gestures do not pick, delete, sculpt or alter map objects
when the FX pane owns the surface.

**QA:** source and regression tests are committed, but exact-SHA
Node/Vite/Apple Metal/Windows/interactive UI tests have not passed as
of this update. The older user's Metal 2/2 CTest PASS predates embedded
preview. Read the detailed handoffs:
`docs/agent-handoffs/spectrafx-studio-stage11-embedded-preview-2026-11-23.md`
and
`docs/agent-handoffs/spectrafx-studio-stage12-mcp-camera-2026-11-23.md`.


# AIVERSE SpectraFX

**SpectraFX** is the official name of the universal, genre-independent visual-effects system in AIVERSE. Its design is inspired by the authoring and event-driven workflow of Unreal Niagara, but it is **not** a copy of Unreal code, asset formats, or editor. `aiverse::spectra` is the public C++ API namespace.

> **Current scope (2026-11-18):** Graph v1 frontend, reusable CPU particle backend, graph-asset variants/hot reload and optional read-only ECS presentation adapter have been **committed**. A basic graph-v1 node-canvas editor and in-memory CPU preview are **committed but unverified**; a limited Metal GPU particle prototype is **committed but unverified**; full GPU/CPU integration, Niagara dataflow and automatic RTS/FPS production integration are **not finished**. Tests were added but have **not yet been run** in the authoring environment. Refer to `docs/agent-handoffs/universal-niagara-vfx-2026-11-18.md` for exact SHA and verification status.

> **2026-11-22:** effects declared in data (rules/map/type/weapon/ability `"spectraFx"`) for every host, and one
> asset running on the GPU kernel or its CPU twin with Metal parity tests: see
> [spectrafx-entity-effects.md](spectrafx-entity-effects.md).

## 2026-11-23 — SpectraScale cue significance and external acceleration fields (opt-in)

The scene host exposes `configureCueSignificance()`, `setCueImportance()` and
`cueSignificance()` to rank queued VFX cues by approximate camera-space size,
distance and explicit host importance. For projects, `fx/gpu/routes.json`
optionally declares `cuePriority` and per-route `importance`; omitted policy
preserves the original FIFO order. Each keyed Play/Move/Stop/Kill sequence
remains ordered; Stop/Kill gains lifecycle priority without skipping visibility
or making any gameplay/authoritative simulation changes. Priority reorders
**only the scene cue inbox**; it does not guarantee emitter or GPU spawn budgets.

`SpectraFxAccelerationFields` is a bounded, read-only registry of native
host-provided acceleration fields. Use
`SpectraFxSceneHost::bindAccelerationField(id,weight,callback)` after host
initialization to sample a PHYSIS (or another environment system) snapshot.
Metal GPU and CPU-twin **dedicated** effects receive the same additional
acceleration every render update; legacy `cpuEffect` fallback and already
active homogeneous shared pools do not. While fields are bound, new sources
avoid GPU shared pooling because per-source acceleration differs.
Neither SpectraFX nor this API directly accesses PHYSIS/ECS internals.

**Current verification:** focused Linux CMake smoke + `spectrafx_contract` and
`spectrafx_architecture` 2/2 PASS for pre-field commits, but Stage 3 field
integration has **not yet been accepted** on real Metal/GLES/Vulkan or macOS.
See handoffs:
`docs/agent-handoffs/spectrafx-significance-stage1-2026-11-23.md`,
`docs/agent-handoffs/spectrafx-significance-stage2-routes-2026-11-23.md`,
`docs/agent-handoffs/spectrafx-fields-stage3-2026-11-23.md`.
Full plan: `docs/spectrafx-modernization-roadmap-2026-11-23.md`.

## Graph v2 — read-only Data Interfaces (Stage 4, 2026-11-23)

Graph v2 can now declare scalar **Data Interfaces** next to existing named
`instanceParameters`. Every binding maps an existing, unique param slot
(`param0`…`param7`) to a host-injected provider and channel; no new
per-particle VM opcode or separate gameplay simulation is introduced.
`DataInterface` nodes are runtime-only value nodes and must be consumed
inside `ParticleProgramModule` expressions using `{"$runtime":"node_id"}`.
Binding validation occurs during graph compilation (including editor/MCP
`fx_graph_validate`) and again during runtime initialization.

Example in `examples/spectrafx/data_interface_weather_v2.vfx.json`:

```json
{
  "instanceParameters": {
    "wind_lift": {"slot":0,"default":2,"min":0,"max":25}
  },
  "dataInterfaces": {
    "wind_lift": {
      "provider":"weather",
      "channel":"vertical_acceleration",
      "scale":1,
      "offset":0,
      "fallback":2,
      "required":false
    }
  }
}
```

For any native project, after `GraphRuntime::initGraph`:

```cpp
aiverse::spectra::DataInterfaceRegistry registry;
registry.bind("weather",
    [&stableWeather](std::string_view channel,aiverse::render::float3 point,
                     float& sample) {
        // stableWeather is YOUR immutable presentation snapshot, not an
        // AIVERSE engine type; do not capture an updating ECS world.
        if (channel != "vertical_acceleration") return false;
        sample = stableWeather.accelerationAt(point).y;
        return true;
    });
runtime.setDataInterfaceRegistry(&registry); // non-owning, after initGraph
runtime.burstAt({0,2,0}); // sample once at this burst origin
runtime.setDataInterfaceRegistry(nullptr); // detach before registry teardown
```

Bindings with `required:true` reject a burst if the provider/channel or
transformed value is unavailable or out of declared bounds; optional bindings
use the finite `fallback`. Every batch is validated before any runtime
uniform is changed. Samples are taken **once per burst**, not per particle
and not on every simulation frame. New bursts can read updated snapshot
values while already-live particles retain their original values unless
updated via the older explicit VM parameter APIs. CPU `ParticleSystem`,
shared `BatchWorld`, and opt-in GPU VM paths reuse the same parameter slots;
unsupported GPU programs still follow the conservative existing CPU
fallback policy.

`SpectraFxDataInterfaceRegistry` is deliberately **not** introduced: use
`aiverse::spectra::DataInterfaceRegistry` from
`engine/fx/SpectraDataInterfaces.h`. Provider names/channel names are
bounded safe identifiers; providers are read-only and must be nonthrowing.
There are at most 16 providers, 8 bindings per graph, and 8 scalar parameter
slots. The editor authoring model offers a `DataInterface` node and
`setDataInterfaceBinding` / `removeDataInterfaceBinding` transactional
helpers. A complete HTML5 Studio panel for these bindings is **not yet
implemented**; projects can use the validated JSON/MCP graph workflow.

**Verification:** native graph-v2 compiler/runtime tests were added and the
Linux/macOS focused CI workflow extended, but the current Stage 4 revision
has not yet passed CI or GPU-device validation. See the exact handoff:
`docs/agent-handoffs/spectrafx-data-interfaces-stage4-2026-11-23.md`.

## Integration boundary with PRISMA

**SpectraFX is NOT the post-processing engine.** It owns emitters, particle simulation, FX graphs (VFX `fx/graphs/*.vfx.json`), ECS FX sources and transient visual events. **PRISMA** in `engine/render/Prisma.{h,cpp}` owns HDR composite/tone mapping, bloom, depth of field, motion blur, temporal AA, screen-space lighting and post-process camera volumes. FX sprites/meshes write scene HDR before PRISMA; the full-screen `postfx` Shader Graph domain belongs to PRISMA. The common Shader Graph compiler/RHI is shared infrastructure, not a second VFX or postprocess implementation.

Never add full-screen frame effects to `engine/fx`, and never add emitters/particle lifecycle to PRISMA. PRISMA's persisted `postfx` block and `postfx_*` MCP commands stay stable for existing projects. See `AGENTS.md` ownership table and `docs/prisma.md`. No automatic live binding is implied.

## Architecture

```text
Gameplay ECS (authoritative, deterministic)
  ├─ typed gameplay events ────────────────┐
  └─ data-only SpectraSourceComponent ──┐  │
                                       ▼  ▼
                               SpectraFX presentation
                               (read-only World + events)
                                       │
                          exposed graph parameters
                                       │
                graph assets fx/graphs/*.vfx.json
                                       │
                     compileGraph (versioned IR)
                                       │
                   existing fx::EffectDef schema
                                       │
                    CPU fx::ParticleSystem/RHI
                                       │
                         renderer + quality tiers

Future opt-in path: GPU simulation/backend + advanced Niagara dataflow nodes
```

**No genre-dependent FX engines.** An RTS turret, FPS gun, spaceship engine or racing car all use the same source component and event presenter. The host game supplies only the pose resolver and maps its event IDs to effect assets. Graphics state and particle handles never enter deterministic gameplay state/checksums.

## Public C++ entry points

- `engine/fx/SpectraFX.h`: `aiverse::spectra::Runtime`, `Effect`, `Handle`, `GraphAssets`, `GraphLoadResult`, `compileGraph`.
- `libraries/include/aiverselib/mechanics/SpectraFX.h`: `aiverse::spectra::SourceComponent`, `EventBinding`, `PresentationSystem`.
- `AIVERSE::spectrafx`: CMake link target, alias of existing `aiverse_fx`; the legacy target remains valid for other agents and projects.

### Author a graph

Put this file at `fx/graphs/engine_fire.vfx.json` inside a project:

```json
{
  "version": 1,
  "parameters": {
    "color": [[0, "#ffe8a0ff"], [1, "#ff330000"]]
  },
  "nodes": [
    {"id": "spawn", "type": "Spawn", "properties": {"rate": 24, "duration": 1, "loop": true}},
    {"id": "initialize", "type": "Initialize", "properties": {"life": 0.6, "speed": 1.0, "size": 0.4}},
    {"id": "update", "type": "Update", "properties": {"color": {"$param": "color"}}},
    {"id": "output", "type": "SpriteOutput", "properties": {"texture": "builtin:smoke"}}
  ],
  "links": [
    {"from": "spawn", "to": "initialize"},
    {"from": "initialize", "to": "update"},
    {"from": "update", "to": "output"}
  ],
  "outputs": ["output"]
}
```

The v1 compiler accepts `Spawn → Initialize → Update* → SpriteOutput|MeshOutput|RibbonOutput` chains, with output fan-out but no multiple-input node merges. Every node has a `properties` object whose keys are validated against `engine/fx/FxEffect.h`. The module chain compiles to the existing `fx::EffectDef` — it does **not** execute arbitrary per-particle graph instructions.

```cpp
#include "engine/fx/SpectraFX.h"

aiverse::spectra::Runtime particles;
particles.init(device); // The native host owns gfx::Device.
particles.setFileReader(readGameContent);
aiverse::spectra::GraphAssets assets(particles, readGameContent);

// Converts a versioned graph asset into a CPU particle effect.
auto flame = assets.load("engine_fire");
if (flame.effect >= 0)
    particles.play(flame.effect, {0.0f, 1.0f, 0.0f});

// Overrides produce independent graph variants:
auto cold = assets.load("engine_fire",
    aiverse::Json::parse(R"({"color":[[0,"#aaddffff"],[1,"#0044ff00"]]})"));
```

Runtime loop: call `setView(camera, viewProj, width, height)`, `update(dt)`, then `render(fog)` after scene rendering while its pass remains open, as described in `engine/fx/ParticleSystem.h`.

### Opt-in native ECS presentation

```cpp
#include <aiverselib/mechanics/SpectraFX.h>
using namespace aiverse;

auto entity = world.create();
spectra::SourceComponent source;
source.effect = "graph:engine_fire:<hash>"; // Better: use GraphLoadResult::runtimeName.
source.killOnDetach = false;
world.add<spectra::SourceComponent>(entity, source);

spectra::PresentationSystem presenter(particles);
presenter.sync(world, [](const World& w, Entity e,
                         fx::float3& position, fx::float3& direction) {
    // Resolve the render transform using YOUR game's component types.
    // Do not hardcode FPS/RTS into SpectraFX.
    return gameResolveEffectPose(w, e, position, direction);
});

presenter.consumeTickEvents(tick, sim.events(),
    {{hitEventId, spectra::EventBinding{"impact_flash", 1_fx}}});
```

Use the actual `GraphLoadResult.runtimeName` for continuous graph assets after `GraphAssets::load`; the `fx/graphs` registry does not automatically initialize all graphs when the game starts. After rollback/new match, call `presenter.reset()` to discard outstanding presentation handles and reset the presented-tick cursor; GPU/CPU simulation state still belongs to the host.

### Compatibility

The new branded `Spectra*` types were introduced without changing the identity of the old `Vfx*` aliases. Existing `fx/<effect>.json`, `fx/fx.json`, `aiverse_fx` and `compileVfxGraph` remain available. Engine runtime files stay under `engine/fx` during migration. Do not remove compatibility aliases or change persistent binary save identifiers until native project adoption and regression tests pass.

## MCP graph asset authoring (stage 7)

The editor-core `registerFxOps` also registers universal `fx_graph_*`
operations through `apps/editor/core/SpectraGraphOps.cpp`. They are available
from the GUI Workspace and the standalone MCP server, independent of the
project's gameplay genre. No editor GPU context is required to validate graphs.

| Operation | Inputs | Result |
|---|---|---|
| `fx_graph_list` | none | All `fx/graphs/*.vfx.json`, per-file compile diagnostics |
| `fx_graph_get` | `name` | Graph JSON, source text and validation |
| `fx_graph_validate` | `graph` or `text` or `name`, optional `overrides` | Strict compiler errors, emitter count and textures; read-only |
| `fx_graph_set` | `name`, `graph` or `text` | Validated file creation/update, `Change::Fx` notification |
| `fx_graph_delete` | `name` | Deletes only the graph source asset |
| `fx_graph_bake` | `name`, `effect`, optional `overrides`, `overwrite` | Compiles graph into `fx/<effect>.json` for existing `fx_preview` and legacy runtime |

Graph names are limited to lowercase letters, digits and underscores (1–48
characters) for editor compatibility. Graph documents have a 1 MiB input
limit. Invalid compilation returns `ok:false` and does not overwrite a
previous valid graph; `fx_graph_bake` refuses to replace an existing legacy
effect unless `overwrite:true` is explicitly supplied.

Example MCP workflow:

```text
fx_graph_set {"name":"engine_fire","graph":<graph v1 object>}
fx_graph_validate {"name":"engine_fire","overrides":{"color":[[0,"#ffffffff"],[1,"#ffffff00"]]}}
fx_graph_bake {"name":"engine_fire","effect":"engine_fire_cpu"}
fx_preview {"effect":"engine_fire_cpu","advance":1,"freeze":true}
view_screenshot {"window":"scene"}
```

**Scope:** baking is a CPU-compatible materialization step, not GPU graph
execution and not a live reference: editing the graph again does not update
an already baked effect until re-baked. Graphs still have no dedicated node
canvas in the editor; that is the next authoring stage. The old
`fx/<name>.json`, `fx/fx.json` and RTS event bindings are unaffected.
The new `vfx_graph_ops` test requires `aiverse_editor_core` to be built;
the test is committed but was **NOT RUN** in the connector-only environment.

## Visual SpectraFX graph editor (stage 8)

The standard AIVERSE Editor has an optional **SpectraFX** window in the
**Графика / Look** group, defined in
`apps/editor/windows/SpectraGraphWindow.cpp`. Its source is automatically
included by the editor's `apps/editor/windows/*.cpp` CMake GLOB.

- Open/create `fx/graphs/<name>.vfx.json` directly from the project.
- Add nodes of types `Spawn`, `Initialize`, `Update`,
  `SpriteOutput`, `MeshOutput`, `RibbonOutput`; drag nodes and pan
  the canvas with the middle mouse button.
- Click the source node's output pin, then the target's input pin to link;
  an input may have one incoming edge. Cycles and invalid endpoint types
  are blocked; unfinished graphs show compiler errors until connected.
- Select a node to edit its `properties` JSON. Edit exposed
  `parameters` as a JSON object; undo and redo graph edits.
- Save through `fx_graph_set`; compile diagnostics come from the exact
  `spectra::compileGraph` implementation used at runtime.
- **Предпросмотр** calls the new GUI-only `fx_graph_preview` command,
  which compiles `graph` in memory, defines an ephemeral effect on
  `Editor::gameFx_`, and plays it in Scene/Game. No temp FX files are written;
  screenshots still work via `view_screenshot`.

Graph nodes may optionally include authoring metadata
`"position":[x,y]` (two finite bounded numeric coordinates), validated
by the v1 compiler but ignored during CPU effect construction. Existing
graph assets without coordinates remain valid; the editor places them in
a default left-to-right layout. This metadata does not enter gameplay ECS
state or shader runtime resources.

This is **only a basic v1 editor**, not full Niagara parity: there is no
arbitrary typed dataflow, function graph, GPU compute execution, depth
collision, linked graph preview in shipped clients or complete UE-style
module UI. Source-level smoke coverage was added to
`tests/vfx_graph_compiler_test.cpp` for node layout metadata; GUI launch,
drag/link interactions and GPU screenshots are **NOT RUN** in the
connector-backed development environment. Manual QA is required.

## Typed module properties (stage 9 — source committed, not yet build-verified)

`engine/fx/SpectraModuleSchema.h` provides a **headless, genre-neutral**
catalog for common graph-v1 fields. The canonical public header
`engine/fx/SpectraFX.h` includes it so native projects and the editor use
the same types and value bounds.

```cpp
#include "engine/fx/SpectraFX.h"

aiverse::Json node = aiverse::Json::parse(
    R"({"id":"emitter","type":"Spawn","properties":{"burst":8}})");
std::string error;
bool changed = aiverse::spectra::applyTypedField(
    node, "burst", aiverse::Json::number(int64_t(24)), error);
// Also available: fieldsForNode("Spawn"), fieldForNode(...),
// validateTypedField(...), removeTypedField(...).
```

The **SpectraFX** visual window now has **Типизированные модули** above
**Расширенные свойства (JSON)**. The typed inspector supports number,
integer, boolean, string, choice and 3-component vector properties. Its
catalog currently includes:

| Stage | Common fields |
|---|---|
| Spawn | `burst`, `rate`, `perMeter`, `delay`, `duration`, `loop` |
| Initialize | `life`, `speed`, `size`, `direction`, `spread`, `shape`, `radius`, `gravity` |
| Update | `drag`, `spin`, `rotation`, `speedLimit`, `stretch`, `velocity`, `hidden` |
| SpriteOutput | `texture`, `blend`, `align`, `fps` |
| MeshOutput | `blend`, `align` |
| RibbonOutput | `hidden` |

All typed field operations are **opt-in edits**. Validation rejects wrong
types, invalid enum choices and values outside declared ranges without
mutating the source node. The inspector does not coerce a MinMaxCurve,
range-array, colour gradient, mesh list or nested effect module into a
scalar: such fields remain intact and must be edited via advanced JSON or
the existing particle module tools. Unknown properties are always
preserved when updating one typed field. The graph compiler
(`parseEffect`) remains the final validation authority.

This is authoring metadata only, not an executable GPU node program.
These changes require **native build and GUI verification**; source
commits and added tests do not prove a passing editor build.
`tests/vfx_graph_compiler_test.cpp` contains schema/compile-contract
checks on top of the preexisting SpectraFX CTest target.

## Metal GPU particle prototype (stage 10 — source committed, unverified)

A new **opt-in** `aiverse::spectra::ComputeParticles` class in
`engine/fx/SpectraGpuParticles.{h,cpp}` executes a limited billboard
particle subset on the **Metal RHI compute path**. It is not wired into
existing RTS/FPS clients and does not replace the legacy
`aiverse::fx::ParticleSystem`. This subset intentionally contains a
single additive soft-textured emitter with configurable burst count,
lifetime, initial speed, size, gravity, linear drag and alpha-over-life.
Complex attributes, noise, trails, mesh particles, collision, GPU
subemitters, Niagara dataflow and user-authored shader graphs remain
on the existing CPU emitter backend until separately implemented.

```cpp
#include "engine/fx/SpectraFX.h"

aiverse::spectra::ComputeParticles particles;
aiverse::spectra::ComputeEmitterDesc params;
params.capacity = 1024;
params.burst = 16;
params.backend = aiverse::spectra::ComputePreference::Automatic;
params.quality = aiverse::fx::FxQuality::High;

// Initialization can occur outside a frame. Device may be nullptr for
// pure CPU/headless tests; RequireGpu fails if compute is unavailable.
if (!particles.init(device, params)) {
    // inspect particles.reason()
}

// Presentation frame, AFTER Device::beginFrame, BEFORE scene render pass:
particles.setView(camera, viewProjection, width, height);
particles.burstAt({0,2,0}, {0,1,0}, {1,0.5f,0.1f,1});
if (!particles.step(1.f/60)) {
    // RHI timing/encoder order error; inspect particles.reason()
}

// During the scene's existing still-open depth-tested render pass:
particles.draw();
// After the render pass: Device::endPass, Device::endFrame.
// Before destroying gfx::Device: particles.shutdown().
```

The headless pure policy `chooseComputeBackend(backend, supportsCompute,
preference)` selects the GPU path only for Metal with compute support.
`CpuOnly` forces CPU; `Automatic` and `PreferGpu` fall back on
unsupported RHI or failed pipeline allocation; `RequireGpu` rejects
unsupported devices rather than silently running on CPU. Low-quality
GPU configurations intentionally choose CPU, respecting the existing
particle quality behavior. RHI compute calls must run outside render
passes and inside a GPU frame. The caller is responsible for a
single presentation owner/device thread and cleanup before device
destruction; this module contains no ECS gameplay or deterministic
simulator state.

**Metal implementation:** three compute passes per presentation frame
(queue accepted births → integrate age/velocity/position → expand
camera-facing quad vertices). GPU buffers are persistent:
64 bytes/particle for states and seeds, a 4-byte slot index and
40 bytes per billboard vertex. The compute shader writes a vertex
buffer directly sampled by the regular RHI particle vertex shader;
normal frames do **not** read the GPU positions back to CPU.
One depth-tested additive draw renders the pool. The current
prototype draws the entire capped slot index array (dead slots emit
degenerate quads); this is not a compacted or GPU-indirect renderer.
Maximum pool is 4096 slots, with a maximum of 64 spawned per call.

**GLSL source** is supplied for future Vulkan compute, but Vulkan
`gfx::Device::supportsCompute` is not implemented in the current RHI.
GLES3/GL33/WebGL2 do not run the compute kernel, so the existing CPU
particle renderer is used instead. Do not report those platforms as
GPU-supported. The dedicated test
`tests/vfx_gpu_backend_test.cpp` checks the selection policy, bounds,
headless CPU-fallback emissions and lifetime. It also contains a recording
Metal RHI mock that verifies spawn→integrate→expand dispatch ordering and
render-pass command sequencing **without compiling/launching real GPU
shaders**. It is not a Metal GPU integration test. CI and native GPU validation are still required.

**Known limitations:** no GPU compaction, no camera culling/LOD for
per-particle draws, no GPU-particle soft intersection, no mesh/ribbon
GPU rendering, no advanced emitter graphs, no cross-CPU/GPU effect
scheduler, no automatic FPS/RTS runtime migration, and no GPU
performance measurements. Runtime Metal shader compilation, GPU
resource hazards, multi-frame correctness and framebuffer output
remain **NOT VERIFIED**.

## Automatic graph-to-backend routing (Stage 11 — source committed, not verified)

`engine/fx/SpectraGraphRuntime.{h,cpp}` introduces a new **optional,
genre-independent** `aiverse::spectra::GraphRuntime`. It compiles a graph-v1
asset or accepts a precompiled `fx::EffectDef` and decides whether it can run
without semantic loss on the existing Metal compute prototype. Every
unsupported graph is executed with the **exact same full EffectDef** on
`fx::ParticleSystem`; complex modules are never stripped to fit the GPU.

```cpp
#include "engine/fx/SpectraFX.h"

aiverse::spectra::GraphRuntime runner;
aiverse::spectra::GraphRuntimeOptions options;
options.preference = aiverse::spectra::ComputePreference::Automatic;
options.quality = aiverse::fx::FxQuality::High;
options.capacity = 1024;

if (!runner.initGraph(device, graphJson, aiverse::Json::object(), options)) {
    // runner.reason() and runner.errors() describe the rejection
}

// After Device::beginFrame, before the scene render pass:
runner.setView(camera, viewProj, width, height);
runner.burstAt({0, 1, 0}, {0, 1, 0});
runner.step(1.f / 60.f);

// While the scene pass is open:
runner.draw(fog);
// On match change/rollback: runner.clear() outside an active encoder.
// Before device shutdown: runner.shutdown().
```

The schema validator `inspectGraphGpuEligibility(effect, options)` does not
require a GPU and is safe for native modules, headless tests and editor tools.
It returns `GraphGpuEligibility { eligible, gpu, reason }`. GPU v1 is allowed
**only** for one additive `builtin:soft` camera-aligned point emitter
containing one burst of 1–64 particles, constant speed/life/size/drag,
zero spread, world-Y gravity, and the default white-to-transparent
colour ramp. The current prototype supports only this limited payload.
Custom colours/gradients, multiple emitters, random ranges/curves,
custom shader graphs, trails/ribbons, noise, force fields, sub-emitters,
lights, collisions, texture animation, or any other unsupported module
force a full CPU fallback. The validator compares normalized complete
effect definitions against a known-supported template and also explicitly
checks sensitive unsupported modules and colour keys.

`ComputePreference::RequireGpu` fails if the graph is not eligible,
the real device does not expose Metal compute, the quality tier is too
low or the shader/pipeline cannot initialize. `Automatic` and
`PreferGpu` instead use the unchanged CPU asset. `CpuOnly` never
attempts GPU. A GPU health failure inside `ComputeParticles` can
subsequently fall back to its restricted CPU equivalent; `GraphRuntime`
continues to own that instance and reports the actual backend.

The editor and MCP `fx_graph_validate`, `fx_graph_get`,
`fx_graph_list` responses now add **`metalGpuEligible`** and
**`metalGpuReason`** after successful compilation. These are only
**asset capability diagnostics**, **not** proof that Metal is present
or that GPU execution succeeded. The SpectraFX visual editor shows the
same capability verdict under the properties inspector; its in-memory
graph preview still uses the established CPU renderer.

**Important boundaries:** This is an opt-in native API. It is not
automatically wired into existing FPS, RTS, editor gameplay, ECS
authoritative state, file assets or current save protocols. The
existing production client renderer stays on CPU. Actual Apple Silicon
GPU rendering, MSL compilation, multiple frames in flight and all
regression tests remain **unverified** until run in a real checkout.

Source tests: `tests/vfx_graph_runtime_test.cpp` exercises a supported
graph, strict rejections and CPU fallback; `tests/vfx_graph_ops_test.cpp`
checks MCP eligibility diagnostics. CTest name:
`vfx_graph_runtime` (label `SpectraFX`).

## Metal lifecycle and sparse draw-range hardening (Stage 12 — source committed, unverified)

The opt-in `ComputeParticles` Metal backend now tracks its occupied
**slot prefix** rather than always updating and drawing all allocated
slots. The CPU-owned slot-age bookkeeping determines the greatest
currently live slot index; compute integration and quad expansion
run through that prefix and the indexed draw is limited to
`submittedSlots * 6`. A pool containing no live particles and no
pending births performs no compute dispatch or draw.

**This is not GPU compaction:** gaps between active slots still produce
degenerate quads. True GPU stream compaction, prefix sums, indirect
draw counts, and thousands of materials are subsequent phases.
The hard 4096-slot capacity and original RHI buffer format remain.

`ComputeStats` now exposes:

- `submittedSlots`: prefix length actually submitted in the latest
  GPU draw (may exceed `alive` when holes exist).
- `gpuFallbacks`: automatic runtime transitions to CPU.
- `lostOnFallback`: number of GPU-resident particles discarded
  when the GPU device became unhealthy. Existing stats remain.

A **runtime device-loss event** is not a seamless animation step:
`ComputeParticles::step` reports failure. For `RequireGpu`, the
emitter becomes unavailable, retains its Metal backend identity
until shutdown and **does not switch to CPU**. For `Automatic` and
`PreferGpu`, the class attempts to initialize a CPU emitter, records
the discarded GPU particles and reports `false` for the transition
frame. CPU rendering on a device reported unhealthy is **not guaranteed**;
a real graphics-device recreation may be required. RHI encoder timing
errors continue to return failure without silently switching backend.

`GraphRuntime::step` now forwards the actual readiness, backend and
error message of its contained GPU/CPU runtime. `GraphRuntime::gpuActive`
also checks the underlying compute emitter, preventing false-positive
success reports after `RequireGpu` device loss. On clear, the sparse
range and GPU slot counters reset; attempting `clear` inside an
active GPU encoder leaves state intact and emits a diagnostic.

The RHI recording test
`tests/vfx_gpu_backend_test.cpp` has scenarios for active encoder
clear protection, sparse draw range, empty-pool zero dispatch, GPU
loss with `RequireGpu`, GPU→CPU transition with explicit lost-particle
telemetry and staggered particle expiry over multiple frames.
**These are mocked host-command tests, not actual MSL execution.**

A focused workflow,
`.github/workflows/spectrafx-validation.yml`, provides
pull-request and manual gates: Linux/macOS build and headless CTest
for SpectraFX; Linux additionally builds the editor and tests MCP
graph operations. The focused workflow does **not** validate a real
Metal compute shader or GPU image correctness. Actual compilation,
CI results, Apple Silicon rendering, screenshots and performance
measurements remain **NOT VERIFIED** until they run.

## Dense billboard gathering (Stage 13 — source committed, GPU validation pending)

The optional Metal `spectra::ComputeParticles` backend now **gathers
only live physical particle slots into contiguous GPU billboard
vertices**. The persistent particle-state pool still uses stable indices
for spawn/recycle; one additional 4-byte-per-slot storage buffer maps
dense draw ordinal → physical state slot. The CPU already owns
lifetime/slot bookkeeping and publishes the live-index list to GPU
before the compute encoder starts. The GPU reads each selected
particle's position/velocity/tint from its own resident state buffer
and writes a contiguous block of four vertices. Motion state is
**never read back to CPU**.

The frame order is: upload any new particle states and slot indices →
upload the current live slot map → compute births → integrate the
physical-slot range that was live *before* the step → GPU gather and
expand exactly the particles still alive *after* the step → regular
RHI indexed draw of `alive * 6` indices. The compute kernel uses
slot 4 for the live-index buffer and `Uniforms.meta.w` for the
alive count; both MSL and future GLSL sources use the same layout.
GPU updates stay between `beginFrame` and `beginPass`; draw remains
inside the scene render pass.

`ComputeStats::submittedSlots` now equals the densely emitted live
count (not the high-water physical slot index). For example, after
slots 0–2 expire while slots 3–5 survive, the GPU receives
`[3,4,5]` and submits 18 indices, not 36. The recording-RHI
regression verifies this map and the draw count across staggered
birth/death frames. The new live-slot upload is `4 * alive` bytes
each active frame and is tracked under `ComputeStats::uploadedBytes`.
An idle particle pool continues to skip compute and draw.

**Scope and limitations:** This is GPU *vertex gathering* from a
CPU-maintained live-slot list. It eliminates sparse-dead-slot rendering,
but it is **not a fully GPU-resident atomic/prefix-sum compaction**
or GPU-authored indirect draw. CPU still scans at most 4096 age
records per frame, and live-index uploads incur bandwidth. The
fixed 16-bit index buffer still limits the pool to 4096 billboards.
The backend is restricted to additive soft sprites eligible under
`GraphRuntime` and is not enabled by default in existing RTS/FPS
clients. Metal shader compilation, image output and performance on
Apple Silicon remain **NOT VERIFIED** until native CI/device tests
complete. Non-Metal RHI paths continue using the original CPU
`ParticleSystem`.

## Experimental GPU-authored particle draw (Stage 14)

The Metal subset now has an **explicitly opt-in** GPU-side compact/draw
mode, using the existing optional RHI methods. This does not change
the default backend or any existing FPS/RTS runtime:

```cpp
#include "engine/fx/SpectraFX.h"

aiverse::spectra::GraphRuntimeOptions opts;
opts.preference = aiverse::spectra::ComputePreference::Automatic;
opts.gpuIndirect = true; // experimental; default is false
// For the standalone billboard emitter:
// aiverse::spectra::ComputeEmitterDesc cfg;
// cfg.gpuIndirect = true;
```

### RHI contract and compute passes

The portable `gfx::Device` adds the default-false capability query
`supportsGpuAuthoredIndirectDraw()`. Metal reports support through
its existing plain `drawIndexedGpuIndirect` path, independently of
experimental ICB features. Vulkan/GL/GLES/WebGL continue to return
false. A device advertising only `supportsCompute()` is not enough.

Metal compute uses two additional persistent storage buffers:
`aliveCounter` (4-byte atomic uint, compute slot **5**) and
`gpuDrawArgs` (20-byte `gfx::DrawIndexedIndirectCommand`, slot **6**).
The existing `ParticleState` and 40-byte expanded vertex formats
are unchanged.

```text
Mode 3  reset GPU alive counter
Mode 0  spawn into persistent physical GPU particle slots
Mode 1  integrate physical state / kill expired particles
Mode 4  atomic-fetch-add surviving particle -> dense vertex ordinal
Mode 5  write GPU indexed draw args (indexCount = alive * 6)
Render  gfx::Device::drawIndexedGpuIndirect(gpuDrawArgs, 0)
```

Metal's RHI inserts a buffer memory barrier after each compute
dispatch, and graphics submission occurs in the following render
encoder. Surviving particle order is nondeterministic (atomic allocation),
which is acceptable only for the current **additive** billboard subset.
The GPU counter and draw arguments are not read back to CPU during
normal rendering.

If `gpuIndirect=true` but the device lacks
`supportsGpuAuthoredIndirectDraw()`, SpectraFX retains Stage 13's
CPU-published dense slot gather with regular `drawIndexed`. If an
already-enabled indirect command is rejected *during drawing*, it
reports an error and skips the draw instead of drawing potentially
uninitialized/stale vertices with an approximate CPU count.

`ComputeParticles::gpuIndirectActive()` reports actual selection;
`ComputeStats::gpuCompactDispatches` and
`gpuIndirectDraws` track submitted GPU operations. When indirect is
active, `stats().submittedSlots` and `stats().alive` are **host-side
lifespan estimates**, not GPU-readback measurements. The actual
indexed count comes from the GPU-authored 20-byte command.

### What remains on CPU

GPU indirect removes the **per-frame upload of the live slot map**,
and GPU compaction/draw-count calculation no longer relies on that
map. The CPU **still** owns slot allocation and lifetime/free-list
bookkeeping, and scans a bounded maximum of 4096 age records.
GPU motion and dense quad generation remain GPU-side.
This is not a GPU-only allocator, multi-emitter scheduler,
sorting system, or complete Niagara simulation.

### Validation gates

- `tests/vfx_gpu_backend_test.cpp`: the recording RHI verifies
  reset/spawn/integrate/atomic compact/args ordering, absence of
  host live-slot uploads in GPU-driven mode, capability fallback,
  and rejection without unsafe direct draw.
- `tests/vfx_metal_compaction_smoke.mm`: **real Metal compute test**
  compiling the MSL `cs_main`, running GPU passes and reading
  the counter, indirect arguments and compacted vertex results
  between frames. On macOS without a Metal GPU it returns 77 and
  CTest marks it **SKIPPED**, not PASS.
- `.github/workflows/spectrafx-validation.yml` builds and attempts
  the Metal smoke on macOS alongside the headless suite; Linux
  still tests portable CPU/recording paths.

All of these changes are **committed source, not verified execution**.
At the latest workflow query, the new SpectraFX CI jobs were still
queued. No passing MSL compilation, Metal dispatch, offscreen image,
timing or GPU visual parity claim is made. A real image-level Metal
and multiple-frames-in-flight test is still required before a
shipping/default GPU selection.

## Cross-instance material batching (Stage 14B — source committed, unverified)

Multiple `GraphRuntime` instances previously owned separate
`fx::ParticleSystem` objects and could each submit the same sprite
material as a separate GPU draw call. The opt-in **`spectra::BatchWorld`**
(`engine/fx/SpectraBatchWorld.{h,cpp}`) puts arbitrary graph/effect
definitions into **one shared presentation ParticleSystem**. This
reuses the existing global `ParticleSystem::Impl::batchFor` and
`FxRender.cpp` render queue, which already groups particles
**independent of effect and instance IDs**.

```cpp
#include "engine/fx/SpectraFX.h"

aiverse::spectra::BatchWorld shared;
shared.init(device, aiverse::fx::FxQuality::High);

aiverse::spectra::GraphRuntime smoke, fire;
// smokeJson and fireJson may represent different effects/parameters.
smoke.initGraph(shared, smokeJson);
fire.initGraph(shared, fireJson);

// Each GraphRuntime keeps its OWN handles; no CPU renderer per source.
smoke.burstAt({0,1,0});
fire.burstAt({2,1,0});

// ONCE PER FRAME (not once per GraphRuntime):
shared.setView(camera, viewProjection, width, height);
shared.update(dt);
// INSIDE the existing scene render pass, ONCE:
shared.draw(fog);

// Kill smoke's particles without affecting fire:
smoke.clear();
// Full scene/match reset: shared.clear().
smoke.shutdown();
fire.shutdown();
shared.shutdown(); // MUST outlive all attached GraphRuntime instances.
```

`BatchWorld::defineEffect` caches equivalent fully serialized effect
definitions while preserving distinct emitter data. `defineGraph`
compiles v1 JSON through the canonical compiler and registers its
effect in the same world. `play`, `move`, `stop`, `kill` and
`playing` are the underlying ParticleSystem handle operations.
The caller can set texture/mesh loaders on the shared world before
registering effects, retrieve `stats()`, call `collectLights`, and
use its existing global quality budget. These APIs are **independent
of FPS, RTS or any particular game module** and live outside
authoritative ECS/rollback state.

**Draw batching key:** the actual underlying RHI texture/atlas
handle, **blend mode**, compiled shader-graph identity and
primitive kind (mesh vs quad). Effects with distinct animations,
sizes, instances or spawn parameters can use the same draw call
when these render-state requirements match. Even different
sprites in the **same atlas** can batch because their UVs are
encoded independently in vertex data. The same texture with
different alpha/additive blending or shader graph must NOT batch.
A draw is split at the 16-bit-index limit of 16,384 quads.
Alpha sorting still follows the current renderer's existing
ordering rules; a single cross-texture globally sorted transparent
render queue is a different architectural task.

`GraphRuntime::initGraph(BatchWorld&, ...)` and
`initEffect(BatchWorld&, ...)` explicitly opt the instance into
this shared renderer. Its `burstAt` captures handles and
`clear()/shutdown()` kills **only those handles**, not all the
world's particles. The source handle list is bounded at 65,536:
the oldest handle is killed before overflowing the bound.
In shared mode, per-source `step`, `setView` and `draw`
are no-ops to prevent double updates and duplicate draw calls.
**The host must call the shared world's methods once per frame.**
`RequireGpu` is rejected for shared-world registration, not
silently downgraded; current pooling is for the mature
CPU-simulated/RHI-rendered emitter path.

**Existing game apps:** RTS/FPS clients already use one central
`ParticleSystem` and benefit from its built-in batching.
This feature addresses **new native/graph instance contexts**
that formerly created separate renderers. It does not override
individual GPU `ComputeParticles` instances. Shared Metal
GPU-particle pools across multiple independent effects still
need a dedicated multi-emitter GPU scheduler and actual device QA.

**Regression source:** `tests/vfx_shared_batch_world_test.cpp`
records RHI indexed draw calls and verifies 5 + 7 particles
from distinct effect definitions sharing one atlas texture
produce **one** draw. Different blend modes produce **two**
draws. It also checks independent source cleanup, graph
registration, deduplication and no accidental per-source
updates. CTest: `vfx_shared_batch_world`, target
`aiverse_vfx_shared_batch_tests`, label `SpectraFX`.
The focused CI workflow builds this test on Linux and macOS;
actual CI/CTest results remain **unverified** in this session.

## Genre-neutral cue scheduling and ECS source bridge (Stage 15)

The opt-in **`spectra::CueScheduler`** in
`engine/fx/SpectraCueScheduler.{h,cpp}` coordinates many independent
event/attached-effect sources over the already existing **`BatchWorld`**.
It does **not** create another ParticleSystem or issue `update`/`draw`
itself; the owning presentation host still calls `BatchWorld` once
per frame. All scheduling and particle handles stay outside gameplay
ECS, deterministic hashes and rollback state.

Use the canonical include `engine/fx/SpectraFX.h` for the pure
presentation API, or `<aiverselib/mechanics/SpectraFX.h>` in native
ECS projects to also access `syncEcsCueSources` and
`acceptSimCueTick`. The ECS adapter **reuses**
`SpectraSourceComponent` (no new ECS component type or ID).
The host supplies the same generic render-pose resolver as the
existing `SpectraPresentationSystem`. Source keys use
`Entity::packed()` so recycled indices with new generations
cannot reuse an old effect handle.

```cpp
#include <aiverselib/mechanics/SpectraFX.h>

// BatchWorld and CueScheduler must outlive each other's handles in this
// order: shared BatchWorld initialized first, CueScheduler destroyed first.
aiverse::spectra::BatchWorld shared;
shared.init(device, aiverse::fx::FxQuality::High);
aiverse::spectra::CueBudgets limits;
limits.maxQueued = 512;
limits.maxPerTick = 64;
limits.maxPerFrame = 128;
limits.maxAttachments = 1024;
limits.maxTrackedOneShots = 2048;
aiverse::spectra::CueScheduler cues(shared, limits);

cues.registerEffect("impact", impactEffectDef); // or registerGraph(...)
cues.registerEffect("engine_fire", loopingEffectDef);
cues.bindCue(72, "impact"); // game-defined event type, not an FPS/RTS enum

// On the presentation thread, after a committed gameplay tick:
aiverse::spectra::acceptSimCueTick(cues, tick, simEvents);

// ONCE per presentation frame, with graphics Device in a valid frame:
shared.setView(camera, viewProj, width, height);
aiverse::spectra::syncEcsCueSources(world, cues, resolvePose);
cues.flush();          // deferred one-shots, bounded per frame
shared.update(dt);     // exactly once for ALL sources
// With the scene render pass open:
shared.draw(fog);      // exactly once, batches compatible materials

// Before replaying restored ticks after rollback/new match:
cues.resetForRollback();  // kills only this scheduler's cues/sources
// Do not call shared.clear() unless resetting ALL other presentation owners.
// Destroy cues BEFORE shared.shutdown()/gfx::Device destruction.
```

### Budgets and ownership rules

`CueBudgets` bounds outstanding events, per-tick admissions,
per-frame one-shot starts, simultaneous ECS attachments and the
rollback-tracked one-shot handle ring. Inputs are clamped to safe
hard ceilings; tracked one-shot capacity cannot be zero.
`CueScheduler::acceptTick(tick, events)` ignores duplicate or
decreasing ticks until `resetForRollback()`, but **does not**
advance the underlying particle renderer. `flush()` starts up to
the frame budget and leaves the remaining bounded queue for later
frames. The queue stores resolved effect IDs at acceptance time
to avoid silent behavior changes during an asset hot-reload.

When the tracked one-shot handle ring fills, the oldest effect
is **killed before** its slot is reused, so its particles cannot
escape rollback ownership. `syncAttachments` consumes the complete
set of active sources for the current frame, moves existing handles,
restarts on effect/revision/scale change and retires absent sources
**before** applying the attachment cap to newcomers. Normal detach
uses each component's stop-vs-kill policy; **rollback always kills**
the scheduler's own handles and clears its pending queue, allowing
older restored committed ticks to play again. Other producers using
the same `BatchWorld` are untouched.

`CueSchedulerStats` reports accepted/duplicate ticks, accepted
and emitted cues, queued/attached counts, budget drops, unbound
effects, failed spawns, evicted one-shot handles and source restarts.
Call `registerGraph`/ `registerEffect` once during setup or asset
reload, not every tick; `bindCue` maps generic unsigned event
types to registered effect names. For custom colour, scale or direction
per event, call `acceptTick` directly with `CueEvent`;
`acceptSimCueTick` uses the default up-direction, scale and tint
and converts SimEvent fixed-point positions into presentation floats.

This scheduler currently uses the mature **shared CPU particle
backend** plus RHI rendering/batching. It is deliberately
**not** a multi-emitter Metal compute scheduler and does not
enable `gpuIndirect` for ordinary graph assets. Custom GPU
per-instance scheduling, automatic native RTS/FPS client migration,
full rollback visual parity and actual platform performance remain
future verification/work items.

Tests: `tests/vfx_cue_scheduler_test.cpp`, target
`aiverse_vfx_cue_scheduler_tests`, CTest `vfx_cue_scheduler` with
label `SpectraFX`. Scenarios cover two unrelated ECS genres,
generation-safe attachment replacement, committed tick dedup,
queue/frame/attachment caps, one-shot eviction, rollback cleanup
without affecting an unrelated BatchWorld effect, and graph
registration diagnostics. **Tests are source-committed but not
confirmed executed** until native CI results are available.

## Typed dataflow graph v2 (Stage 16)

SpectraFX now accepts `"version":2` graph assets through the same
`fx::compileSpectraGraph` / `spectra::compileGraph` API used by
the editor, MCP, `GraphAssets`, `GraphRuntime` and shared `BatchWorld`.
All existing `"version":1` assets retain their original compilation
path; the public `spectra::kGraphVersion` now reports the newest
authoring version (2), with `kLegacyGraphVersion=1`.

V2 adds two distinct types of connections: **execution links**
`{"from":"spawn","to":"init"}` order Spawn→Initialize→Update
modules→Output; **typed value-port links**
`{"from":"math","to":"init","toPort":"speed"}` bind the
value of a data node into one property (dotted property paths are
supported, e.g. `noise.frequency`). Equivalent inline references
are `{"$node":"math"}` and `{"$param":"wind"}`.
Data links can connect math nodes to other math nodes, with cycle
validation even for disconnected data branches.

The new `engine/fx/SpectraValueGraph.h` implements a bounded
constant-expression DAG of literal values, typed exposed parameters,
scalar/vector math, comparisons and `Select` operations.
Representative nodes: `Float`, `Integer`, `Bool`,
`Vec2/Vec3/Vec4`, `Color`, `String`, `Curve`, `Gradient`,
`Parameter`, `Add`, `Subtract`, `Multiply`, `Divide`,
`Min/Max`, `Clamp`, `Lerp`, `Sin/Cos`, `Pow/Sqrt`,
`Floor/Ceil`, `Normalize/Length`, `Dot/Cross`,
`ComposeVec2/3/4`, `Select`, `Greater/Less/Equal`.
Invalid types, nonfinite results, division by zero, excessive graph
depth, duplicate node IDs and cyclic references are rejected.
Parameter overrides must match their declared JSON type.

`AgeNormalized` is meaningful only inside an explicitly
sampled `{"$curve":"expression_id","samples":33}`
reference. It compiles a scalar expression over normalized age to
2–64 keys understood by the existing CPU FxRange/curve system.
It is **not** a per-particle script or general programmable GPU
kernel. Math expressions without an age dependency are resolved
at graph compile time; sampled curves are evaluated during
the already-existing particle lifetime LUT update.

The new `engine/fx/SpectraGraphV2.h` compiler lowers module-stage
nodes to existing strict `Update` module properties:
`NoiseModule`, `CollisionModule`, `TrailModule`,
`LightModule`, `ForceModule`, `VelocityModule`,
`SubEmitterModule`, `TriggerModule`,
`InheritVelocityModule`, `SpeedLimitModule`,
`ColorBySpeedModule`, `SizeBySpeedModule`,
`SpinBySpeedModule`, `LifeBySpeedModule`. Flat
`ShapeModule`, `FlipbookModule`, `MaterialModule`,
`SortModule`, `CustomModule` reuse the complete existing
`FxEffect` grammar; unsupported emitter keys are still rejected
by `fx::parseEffect`. `SubEmitterModule.properties.items`
contains the sub-emitter array. Existing sprite/mesh/ribbon
outputs remain available.

The output is an ordinary `fx::EffectDef` compiled by the
**same authoritative v1 parser**, preserving native C++/genre
compatibility and the mature CPU render path. Any effect with
modules outside the limited Metal subset continues to use
the full CPU fallback. There is no graph-specific dependency
on FPS, RTS or other gameplay modules.

**Example asset:** `examples/spectrafx/typed_noise_trail_v2.vfx.json`.
It combines exposed parameters, multiply nodes, explicit value
connections, an age-dependent fade sampled into keys, and noise
and trail modules.

**Explicit limits:** V2 is a typed authoring/dataflow *frontend*
for existing CPU effect modules, not arbitrary runtime per-particle
bytecode or GPU kernel compilation. Full user-defined functions,
variable-length particle attribute buffers and per-particle
event graph execution remain separate implementation steps.
The code is committed, but no CMake build, test run, screenshot
or Metal validation has yet been executed in this stage.

## Visual authoring and safe drafts for graph v2 (Stage 17)

The optional **SpectraFX** ImGui editor now creates graph version 2
by default. Graph version 1 assets can still be opened and edited,
and the **v1 → v2** button upgrades a draft in memory without
immediately overwriting its source file.

The shared pure C++ model `engine/fx/SpectraAuthoring.h`
provides `spectra::authoring::nodeCatalog`, `newGraphV2`,
`addNode`, `connectExec`, `connectData`, `disconnect`,
`inputPorts`, `node`, `isData`, `isOutput` and
`reaches`. It lives outside the editor and can be reused
by native C++ tooling or agents through
`engine/fx/SpectraFX.h`. Node definitions contain
categories, default properties and typed input port names.
The editor shows a grouped catalog for value, math, vector,
logic, simulation, force, event and renderer nodes.
Execution links and typed data links use different canvas colours;
the right inspector lets the author choose and connect input
properties (including nested paths) to a selected value node.
Cycle prevention applies to both link kinds.

The right inspector offers fast editors for numeric, integer,
bool, vector and colour literal nodes, module-specific field
controls for noise, collision, force, trails, light, rendering,
sorting and shape settings, and keyframe editing for
`Curve` and `Gradient` value nodes. A curve can add/remove
keys and edit time/value; a gradient can edit RGBA keys.
Unknown modules, $node expressions and complex nested fields
remain accessible through the existing raw JSON inspector,
without lossy coercion.

### Safe incomplete graph workflow

A graph need not compile successfully at every intermediate
authoring step. Draft assets live under
`fx/graphs/_drafts/<name>.vfx.json` and are *not*
enumerated or loaded by the production graph registry.
The editor has separate **Черновик** / **Опубликовать**
actions and a draft list. A draft is saved even while links
or modules are incomplete; publishing compiles the exact
saved draft and refuses to replace a published graph if
the compiler returns errors. Existing
`fx/graphs/<name>.vfx.json` and legacy `fx/*.json`
remain unchanged by saving an incomplete draft.

Additional MCP actions:

- `fx_graph_node_catalog`: node types, categories, default
  property JSON, data input port names, and a new default graph.
- `fx_graph_edit`: pure in-memory `new`, `upgrade`, `add`,
  `connect`/`disconnect`, `deleteNode`, `setProperty`
  operations. Returns edited graph and compiler diagnostics;
  no file is written.
- `fx_graph_draft_set`, `fx_graph_draft_get`,
  `fx_graph_draft_list`, `fx_graph_draft_publish`:
  non-destructive authoring, validation and explicit publication.
- Existing `fx_graph_get/set/list/validate/bake` continue
  working for both graph versions; published graphs always
  undergo strict compilation.

The code is committed; **GUI launch, CMake build and CTest are
intentionally deferred** until functional work is finished.
Current advanced editing is still a basic ImGui canvas and
raw JSON fallback, not a complete commercial Niagara editor;
function subgraphs, custom per-particle attribute debugging,
profiler timelines and shader node codegen remain further
work items.

## Reusable typed function subgraphs (Stage 18)

SpectraFX graph v2 now supports reusable **pure value functions**.
Function bodies are nested typed value graphs, independent of
rendering, gameplay genres and ECS state:

```json
{
  "version": 2,
  "functions": {
    "scale_speed": {
      "parameters": { "base": 1, "gain": 1 },
      "returns": "number",
      "nodes": [
        { "id": "a", "type": "Parameter",
          "properties": { "name": "base" } },
        { "id": "b", "type": "Parameter",
          "properties": { "name": "gain" } },
        { "id": "out", "type": "Multiply",
          "properties": {
            "a": { "$node": "a" },
            "b": { "$node": "b" }
          } }
      ],
      "output": "out"
    }
  }
}
```

A caller graph references the function with a `FunctionCall`
value node:

```json
{"id":"scale","type":"FunctionCall","properties":{
  "function":"scale_speed",
  "arguments":{"base":{"$param":"speed"},"gain":1.5}
}}
```

Function signatures declare typed default `parameters`, an output
node ID and an optional strict `returns` type:
`number`, `bool`, `string`, `vec2`, `vec3`, `vec4`.
Calls inherit the caller's compile-time or normalized-age
sampling context, allowing reusable fade/wind/response functions
that compile into existing CPU FxRange curves. Function definitions
contain only value DAG nodes, never Spawn/Update/Output stages.
The graph compiler rejects unknown functions, wrong argument
types, recursion (including disconnected definitions), more
than 32 declared functions or more than eight nested calls.
No native scripts, reflection or arbitrary executable code
are evaluated.

### Shared function asset libraries

`engine/fx/SpectraFunctionAssets.h` provides
`validateFunctionLibrary`, `mergeFunctionLibrary` and
`FunctionLibraryAssets` with an injected file reader and
last-known-good reload fallback. Library files use
`fx/functions/<name>.vfxfn.json`:

```json
{"version":1,"functions":{"my_function":{
  "parameters":{"x":1},
  "returns":"number",
  "nodes":[{"id":"result","type":"Parameter",
            "properties":{"name":"x"}}],
  "output":"result"
}}}
```

Importing a library into a v2 graph explicitly copies its
validated function definitions; existing function names with
different bodies reject the import unless replace is explicitly
requested. Import works with incomplete editor drafts, and
the pure graph compiler still performs no filesystem IO.
The native API is available from `engine/fx/SpectraFX.h`.

MCP supports `fx_function_list`, `fx_function_get`,
`fx_function_set`, `fx_function_import`, and
`fx_function_export`. An import returns an edited graph in
memory; the user explicitly publishes or saves a draft.
The SpectraFX editor has a FunctionCall selector, embedded
function JSON editor, plus import/export controls for reusable
function-library names. Example assets:
`examples/spectrafx/common_math.vfxfn.json` and
`examples/spectrafx/function_calls_v2.vfx.json`.

**Status:** full code committed, testing deliberately deferred.
Functions compile pure values and age-sampled curves into the
existing CPU emitter definitions; they do not yet define
per-particle mutable attribute bytecode or programmable GPU
simulation kernels.

## Compiled per-particle CPU programs (Stage 19)

The existing `fx::ParticleSystem` can now execute an optional
`EmitterDef::particleProgram`: a small compiled, bounded expression
program running at **individual particle** lifecycle stages. It
does NOT reinterpret JSON for every particle or run any C++/Lua
script from the effect file. The compiler validates opcodes,
targets, nesting, finite constants and budgets, and converts
the JSON into precompiled instructions in
`engine/fx/SpectraParticleProgram.h`. On every particle step,
the VM uses a fixed-size stack with **no dynamic allocation**.
No gameplay ECS state is read or written.

Use the `ParticleProgramModule` pipeline node in a v2 graph
before an output, or provide `particleProgram` in an ordinary
legacy emitter definition. Supported named program stages are
`spawn`, `update`, `death`, `collision`, `trigger`.
Each stage contains at most 32 assignments/updates and 256
postfix VM instructions. An expression is a finite number,
`{"var":"field"}`, or a tree of built-in arithmetic, clamp,
lerp, comparison and trigonometric operations. Recursion
depth is bounded by 16. No arbitrary executable source,
network, file access or plugin invocation is allowed.

```json
{
  "version":1,
  "spawn":[
    {"target":"attribute0","value":0.75}
  ],
  "update":[
    {"op":"addRate","target":"velocity.y",
     "value":{"op":"multiply",
       "a":2.2,
       "b":{"op":"subtract","a":1,
            "b":{"var":"ageNormalized"}}}},
    {"op":"set","target":"tint.a",
     "value":{"op":"saturate","a":{
       "op":"subtract","a":1,"b":{"var":"ageNormalized"}}}}
  ]
}
```

Supported scalar reads include `age`, `ageNormalized`,
`dt`, `uid`, world-space `position.x/y/z`,
`velocity.x/y/z`, `size`, `rotation`,
`tint.r/g/b/a`, and eight independent user-defined
`attribute0..7` floats stored with each visual particle.
Read-only lifecycle fields cannot be written by a program.
Actions `set`, `add`, `multiply`, `addRate`
(`value * dt` per second) apply to writable fields.
Output values are finite/bounded, tint channels clamp to
0..1 and size clamps to nonnegative.

`spawn` is invoked after built-in particle initialization
and before trails/birth handlers; `update` runs after
existing gravity/forces/integration and before collision
and trigger testing; event stages run at the
corresponding collision, trigger and death boundaries.
The legacy velocity/size/light/noise/trail mechanics still
run for any effect lacking a program with no new script
evaluation. Dynamic attributes can drive size, rotation,
tint and motion through subsequent VM statements.
Function graphs v2 remain a separate compile-time
subgraph feature; arbitrary graph-to-GPU kernel codegen
is not implied.

**Example**: `examples/spectrafx/runtime_particle_program_v2.vfx.json`
uses a custom attribute, age-dependent rising force, alpha fade,
variable size and a death handler. Advanced effects using this
CPU-only VM are automatically excluded from the restricted
experimental Metal GPU subset: `GraphRuntime` must preserve
the full source effect on CPU, not silently drop the program.
User requested postponing builds/tests; source is committed
but **not compiled or runtime verified**.

## Graph v2 named live instance parameters (Stage 20 — functional source committed, tests deferred)

Graphs can declare up to **eight named CPU particle VM scalar inputs**
separately from compile-time `parameters`. Runtime instance values
can change without rebuilding an effect, recompiling a graph, or changing
another instance's data. This is **presentation-only**, genre-neutral
and does not write values into gameplay ECS.

The graph-v2 `instanceParameters` root maps a safe name to
`{slot,default,min,max}` (unique slots 0..7, finite bounded floats).
A data node of type `InstanceParameter` refers to `properties.name`,
and a `ParticleProgramModule` expression using
`{"$runtime":"node_id"}` is lowered at compile time to the CPU VM
read `param0` … `param7`. The values stay dynamic at particle
runtime, unlike normal graph `$param` literals.

```json
{
  "version": 2,
  "instanceParameters": {
    "lift": {"slot": 0, "default": 2.2, "min": 0, "max": 25},
    "intensity": {"slot": 1, "default": 1, "min": 0, "max": 10}
  }
}
```

Embed the declaration alongside the normal `nodes`, `links`,
`outputs` and `parameters` fields. See
`examples/spectrafx/per_instance_parameters_v2.vfx.json` for a
complete graph whose per-particle upward acceleration uses the
named `lift` value. `engine/fx/SpectraInstanceParameters.h`
contains the shared validation metadata.

```cpp
#include "engine/fx/SpectraFX.h"

// Same graph asset can be compiled by multiple independent runtimes:
aiverse::spectra::GraphRuntime fireA, fireB;
fireA.initGraph(device, graphJson);
fireB.initGraph(device, graphJson);

fireA.setNamedProgramParameter("lift", 2.5f);
fireB.setNamedProgramParameter("lift", 10.0f);

// For already-live particles, propagate the new value explicitly:
fireA.setNamedProgramParameter("lift", 5.0f, true);

// Or without a graph schema, use raw CPU VM slot 0..7:
fireA.setProgramParameter(0, 5.0f, true);
```

Calling `setProgramParameter(slot,value,affectExisting=false)` on
`GraphRuntime` changes future births for that **source only**;
`affectExisting=true` additionally updates current particles
owned by its tracked handles. The public `fx::ParticleSystem`
also offers `setProgramParameter(handle,slot,value,affectExisting)`
and atomic `playWithProgramParameters(effect,pos,dir,scale,tint,params)`,
which sets all eight scalars **before prewarm and Spawn callbacks**.
`spectra::BatchWorld` exposes the same atomic playback and per-handle
setter. The standard `play` remains source-compatible and passes zeros.

`spectra::CueEvent` and `AttachedCue` optionally carry
`programParams[8]` plus a `programParamMask`. The cue scheduler
applies enabled values before a new one-shot/attached effect starts.
Changes to the parameters on an existing continuous source update
the current particles **without restarting the effect**. Disabled
slots remain zero; removing an override clears the old slot value.
The scheduler still owns the handles solely on the presentation side.

In the SpectraFX editor (version 2), the right panel has
**Параметры экземпляра (runtime)**. It edits a validated JSON schema,
shows named slider values for the current preview instance, and
passes `instanceValues` to the in-memory `fx_graph_preview`
operation. The preview runs the existing CPU ParticleSystem with
values applied before first Spawn and persists them during loop
restarts. This does not create new `fx/*.json` files.

**Backend contract:** arbitrary CPU particle VM and named
`InstanceParameter` graphs do not run on the experimental Metal
simple-billboard kernel. `GraphRuntime::initGraph` selects the full
CPU runtime for such graphs; `RequireGpu` rejects the request
rather than silently dropping instance parameters. No GPU VM/JIT
translation, HLSL/MSL codegen, or GPU-side arbitrary per-instance
parameter buffer is implied.

**Status:** the user requested functional implementation first.
All changes above are committed source; neither native compile,
CTest, GUI preview nor cross-platform GPU execution was run for
Stage 20. Do not claim source completeness or parity until verified.

## CPU VM custom particle attributes in shader materials (Stage 21 — source committed, tests deferred)

Emitters may now opt into a **four-channel shader-graph input map**
via `graphInputs`. The renderer reuses the preexisting 40-byte vertex
layout and its `float4 graphData` field, so compatible draw batching
and legacy sprite/mesh vertex formats remain unchanged. The source
mapping is validated by `fx::parseEffect`, serialized by
`fx::effectToJson`, and precomputed once in
`ParticleSystem::Impl::build` into four small integer selectors.
It adds no JSON evaluation in the per-particle draw loop.

Example inside any emitter in `fx/<name>.json` or a graph-v2
`MaterialModule` / output node's flat properties:

```json
{
  "graphInputs": ["attribute0", "attribute1", "param0", "age"]
}
```

Exactly **four** source names are required. Valid names:
`age`, `lifetime`, `random`, `zero`,
`attribute0` through `attribute7`, and
`param0` through `param7`. The scalar user attributes are
written by the CPU `particleProgram` at Spawn/Update/event stages.
The instance parameters are captured at emission or updated by the
per-instance setters from Stage 20. Both are presentation-only.

`shaderGraph` already consumes the four vertex channels as
`sg_particle` in the built-in material graph compiler. This
Stage 21 maps chosen VM values into those channels; authors
can combine them with the graph's math/colour/texture nodes.
It does **not** change the material graph language, the number
of vertex channels or shader/texture slot budgets.

**Source compatibility:** when `graphInputs` is absent,
the generated vertex data is exactly
`[particle age, particle lifetime, stable random, zero]`,
matching the original shader graph contract. This mapping is
opt-in for CPU-backed particles and does not alter the generic
non-shader particle material's output. A newly authored mapping
is included in the fully serialized effect identity for
`BatchWorld` deduplication; effects with different vertex
inputs still share draw calls when their actual material/render
state matches, as each vertex carries its own data.

The experimental Metal simple-billboard backend does not execute
the CPU particle VM or arbitrary shader graphs. Modified effects
that use nondefault `graphInputs` are rejected by the strict GPU
eligibility gate and retain the existing full CPU renderer.
No automatic GPU shader VM compiler, GPU graph-param binding,
full custom-attribute register file, or guaranteed visual parity
is claimed.

**Verification:** user deferred building/testing while finishing
functional work. Stage 21 comprises committed C++ source only:
no CMake/CTest, editor screenshot, cross-backend shader compilation,
GPU timing or material output comparison was actually run.

## Experimental Metal scalar particle VM execution (Stage 22 — source committed, unverified)

`engine/fx/SpectraGpuProgram.h` provides a fixed binary payload for a
strict CPU particle-program subset. The Stage-22 Metal compute kernel now
**interprets** that validated payload directly during particle Spawn and
Update. This is not dynamic MSL shader generation; the shader compiles
once, while the payload is an ordinary persistent RHI storage buffer.
The older compiler/ABI preparation existed before the GPU execution
implementation; actual compute execution and parameter buffers were
added in Stage 22.

**Opt-in is mandatory and false by default:**

```cpp
#include "engine/fx/SpectraFX.h"

aiverse::spectra::GraphRuntimeOptions options;
options.preference=aiverse::spectra::ComputePreference::Automatic;
options.gpuProgram=true;  // EXPERIMENTAL; defaults to false
options.gpuIndirect=false; // independent option from Stage 14

aiverse::spectra::GraphRuntime emitter;
if(!emitter.initGraph(device,graphJson,aiverse::Json::object(),options)) {
    // emitter.reason(), emitter.errors()
}
emitter.setNamedProgramParameter("lift",3.5f);
emitter.burstAt({0,1,0}); // parameters snapshotted before Spawn
// Per-frame device sequence: beginFrame -> emitter.step -> beginPass ->
// emitter.draw -> endPass -> endFrame.
emitter.setNamedProgramParameter("lift",5.0f,true);
// affectExisting = true queues a GPU parameter-buffer upload for next step.
```

The opt-in GPU eligibility gate first compares **every** compiled
emitter field against the single additive `builtin:soft` point-burst
contract; it only permits a `particleProgram` when its entire program
passes `gpu_program::prepareForMetal`. Any other module, arbitrary
material `shaderGraph`, `graphInputs`, events, collision, trails,
multiple emitters, mesh particles, forced fields, sorting, random
ranges or graph stages outside the supported subset still use the
**exact original full CPU effect**. `RequireGpu` fails instead of
silently dropping behavior.

Supported VM stages: **Spawn** and **Update**. Readable scalar fields:
`age`, `ageNormalized`, `dt`, `position.x/y/z`,
`velocity.x/y/z`, `size`, `tint.r/g/b/a`, and
read-only per-instance `param0..param7`.
Writable fields: position/velocity components, size and tint.
Actions: `set`, `add`, `addRate`, `multiply`.
Supported expression ops: finite constants and field reads,
add/subtract/multiply, min/max, abs/negate/saturate, clamp and
lerp. Numeric results are finite/bounded; size clamps at zero,
tint channels at [0,1]. The kernel rejects malformed instructions
and skips failed statements rather than modifying particle state.

Unsupported: user `attribute0..7` registers, `uid`, rotation,
division/pow/trig/sqrt/floor/ceil, comparisons, conditional Select,
collision/trigger/death programs, loops/recursion, runtime user
scripts and all non-Metal compute backends. The GPU interpreter
is bounded at **24 total statements**, **192 instructions** and
**48 instructions per statement**. ABI/opcode/field numbers
are compile-time asserted against the CPU VM enums; these checks
still require an actual native build to confirm.

The original 64-byte GPU particle state and 40-byte billboard vertex
are unchanged. The Metal compute pipeline additionally binds:
- slot **7**: seed-time parameter snapshots (32 bytes per queued birth);
- slot **9**: persistent parameter snapshots (32 bytes per physical slot);
- slot **10**: validated GPU VM payload (10,256 bytes, ABI version 1);
- slot **8** remains the existing `Uniforms` constant buffer.

Both old host-gather and opt-in indirect modes call the same
Spawn/Update kernel and use GPU-resident particle state. Runtime
parameter changes without `affectExisting` apply only to later
births; enabling it updates already tracked particles through the
host's bounded CPU slot map and schedules one 32-byte-per-slot
buffer refresh outside the render pass. **GPU particle positions
are never read back to the CPU.** GPU device-loss behavior
continues to report non-seamless CPU fallback.

Use `examples/spectrafx/gpu_vm_lift_v2.vfx.json` for an authored
single-burst, age-dependent lift/fade example whose named `lift`
value is passed to the VM. The editor and `fx_graph_validate/get/list`
report `metalVmEligible` and `metalVmReason` separately from the
original billboard-only GPU verdict. These are **source capability
diagnostics only**; editor previews continue to use the mature CPU
runtime.

**Verification:** tests were postponed by the user. This GPU VM
and its shaders have **NOT** been compiled/run on Apple Silicon,
Linux, or other platforms in this stage. No Metal image output,
CPU/GPU numeric parity or performance evidence is available.
Do not enable `gpuProgram` by default before real-device tests.

## GPU VM custom scalar attributes (Stage 26 — functional source, tests deferred)

The optional Metal `ComputeParticles` GPU VM now supports **eight
persistent per-particle custom floats** (`attribute0` through
`attribute7`) as both expression inputs and writable Spawn/Update
targets, matching the corresponding subset of the mature CPU
`SpectraParticleProgram`. This extends the prior Stage-22 interpreter;
it is **not** a new renderer, GPU-only free-list allocator or an
automatic production switch for native FPS/RTS clients.

- The fixed `ParticleState` stays **64 bytes**; the billboard
  `Vertex` stays **40 bytes**. GPU attributes live in a **separate,
  32-byte-per-physical-particle storage buffer**, bound at Metal
  compute slot **11**, with two `float4` values.
- On the GPU Spawn dispatch, each reused physical slot receives a
  fresh zero-initialized attribute record; the Spawn VM may write
  its registers before the result is stored. Every later Update
  dispatch reads the same slot's values, executes its bounded
  expression statements sequentially, and writes them back.
- The existing Stage-22 GPU payload enum ABI remains **version 1**.
  Fields 16–23 are `attribute0..7`; fields 24–31 remain
  `param0..7`. The CPU capability gate permits these fields for
  the integrated Metal kernel. All other unsupported fields,
  instructions and non-Spawn/Update stages keep the original
  full-effect CPU fallback.
- Initialization, `clear()` and `shutdown()` allocate/reset/release
  this additional buffer. `ComputeStats::gpuBytes` now accounts for
  its full capacity. No per-frame GPU-to-CPU attribute readback
  is required.

Authoring example:
`examples/spectrafx/gpu_vm_custom_attributes_v2.vfx.json`.
It captures a named instance `intensity` into `attribute0` on
Spawn, then reads the stored attribute on Update to drive
`tint.a` and `size`. With `GraphRuntimeOptions::gpuProgram=true`,
this program fits the **restricted additive billboard GPU VM**
schema, subject to actual device and shader capability. The
same graph also executes under the established CPU VM.
Actual numerical/image parity has **not** been verified.

**Important separation:** standalone
`gpu_program::MetalVmPass` is a distinct experimental compute
utility with a different shader. It does **not** yet have a custom
attribute buffer. Its initializer explicitly rejects payloads
reading or writing fields 16–23, rather than silently producing
incorrect values. Only the integrated `ComputeParticles` kernel
has the new register storage.

The Stage-21 CPU-only `graphInputs` shader material mapping is
**not yet implemented in the GPU sprite renderer**. GPU custom
attributes can currently influence visible GPU output indirectly
through supported VM writes to `tint`, `size`, position or
velocity; arbitrary `shaderGraph` remains CPU-only. Event-stage
VM programs (collision, trigger, death), custom rotations, subemitters,
mesh/ribbon outputs, complex material graphs, alpha sorting and
Vulkan/GLES/WebGL GPU execution remain outside this GPU subset.

**Verification:** by explicit user instruction, functionality is
being implemented before testing. This stage has **NOT RUN**
CMake, CTest, MSL compilation, real Apple GPU rendering or
performance parity checks. The commits establish source changes,
not confirmed production behavior.

## Restricted Metal GPU shader graph materials (Stage 27 — source committed, tests deferred)

The opt-in Metal GPU billboard backend now supports a **bounded
subset of the existing particle-domain `shaderGraph` language**,
including per-particle VM custom `attribute0..7` and per-source
`param0..7` inputs. This is the same
`render::graph::compile(graph,"particle")` compiler already
used by the full CPU ParticleSystem, **not a duplicate material
node language**. Complex effects continue to use their complete
CPU effect definitions.

Native opt-in (both switches default to false):

```cpp
aiverse::spectra::GraphRuntimeOptions options;
options.preference = aiverse::spectra::ComputePreference::Automatic;
options.gpuProgram = true; // if ParticleProgramModule is present
options.gpuMaterial = true; // enables the restricted particle shaderGraph
options.gpuIndirect = false; // optional, independent, experimental
```

Use a `SpriteOutput` node (or legacy emitter) with these
complete authoring fields:

```json
{
  "texture": "builtin:soft",
  "blend": "additive",
  "graphInputs": ["attribute0", "param0", "age", "lifetime"],
  "shaderGraph": {
    "version": 1,
    "domain": "particle",
    "nodes": [
      {"id":"input","op":"input"},
      {"id":"gain","op":"particleAge"},
      {"id":"tint","op":"multiply","inputs":["input","gain"]}
    ],
    "outputs":{"color":"tint"}
  }
}
```

Full graph example:
`examples/spectrafx/gpu_vm_material_inputs_v2.vfx.json`.
It captures named `intensity` to particle `attribute0`
in Spawn, then uses `sg_particle.x` in the material's
particleAge node to modulate base sprite tint.

### Capability and rendering contract

- `GraphRuntimeOptions::gpuMaterial` (and lower-level
  `ComputeEmitterDesc::gpuMaterial`) **must be explicitly true**,
  otherwise a shaderGraph emitter stays on CPU. A graph with
  `graphInputs` but no compatible `shaderGraph` is also
  rejected for Metal so its authored data is not ignored.
- `gpu_material::compile` validates at most **32 pure
  fragment nodes**, one `color` output, no vertex graph,
  no extra texture samplers, no time-dependent/unavailable
  operations and a restricted whitelist: input, UV, the
  builtin sprite sample, constants/parameters/vertexColor,
  the four particle graph channels, and elementary colour
  arithmetic/combination. The authoritative
  `render::graph::compile` must also accept the graph.
- The CPU emitter's `graphInputs` schema is reused.
  The GPU compute kernel publishes exactly four source
  floats from particle age, lifetime, `attribute0..7`,
  `param0..7` or zero into the existing **40-byte**
  `Vertex::graphData`. The mapping is compiled at emitter
  initialization into a four-uint (16-byte) buffer at Metal
  compute slot **13**; it is never parsed as JSON per frame.
  Stable CPU UID-derived `random` is rejected in a GPU
  material because the fixed GPU particle state cannot
  reproduce it. Material graphs referencing the default
  particleRandom input without an explicit mapping are
  rejected rather than silently changing the visual.
- A compatible GPU material uses a separate generated
  shader pipeline with vertex attribute **3** bound as
  `Float4` at offset 24. All other particle emitters retain
  the old three-attribute billboard pipeline. Existing
  particle state remains **64 bytes** and GPU vertices
  **40 bytes**; existing per-instance VM params, custom
  registers, host-gather and optional indirect-draw paths
  are preserved.
- The graphics shader is built from a trusted MSL/GLSL
  template and the **existing compiler's generated source**
  (no arbitrary authored shader source is interpolated).
  The fragment receives the same `sg_input`, `sg_color`,
  `sg_particle`, UV/position/view parameters as the CPU
  material implementation and outputs the compiler's
  `color` expression. Only one builtin soft-sprite
  texture is bound; no new material texture loaders
  or project-specific dependencies are introduced.
- Full CPU fallback for standalone `ComputeParticles`
  preserves the authored `particleProgram`,
  `shaderGraph` and `graphInputs`. For
  `GraphRuntime`, the existing strict complete
  `EffectDef` comparison now retains those fields only
  after the GPU material subset has passed validation.
  `RequireGpu` reports an error instead of dropping any
  unsupported material behaviour.

**Boundary:** This is not generic Niagara/HLSL material
authoring parity, GPU mesh/ribbon rendering, external texture
sampling, arbitrary graph shader stages, light/collision modules,
alpha sorting, multiblend materials, or a shared multi-emitter
GPU draw pool. Vulkan/GL/ES/WebGL continue using CPU VFX when
compute is unavailable.

**Verification:** User explicitly requested features before tests.
Stage 27 C++/MSL/GLSL source and example files are committed,
but **CMake, CTest, shader compilation, real Metal rendering,
screenshots, performance and visual CPU/GPU parity remain NOT RUN**.
The generated shader and storage binding should be checked on
actual Apple Silicon before enabling this path in production.

## Shared CPU/GPU emitter resource world (Stage 28 — source committed, tests deferred)

The opt-in, genre-independent `spectra::GpuPoolWorld` creates
**one persistent GPU emitter resource group for each unique full effect**
and reuses that resource across multiple logical source names.
Two vehicles, weapons or spell effects that reference the exact
same graph and GPU policy share one `ComputeParticles`
simulation buffer, VM payload and graphics pipeline.
Unsupported graph modules go into **one existing CPU
`spectra::BatchWorld`**, which can batch compatible
materials/primitive types regardless of gameplay genre.

The public API is exported from `engine/fx/SpectraFX.h` and
linked with `AIVERSE::spectrafx`:

```cpp
#include "engine/fx/SpectraFX.h"

aiverse::spectra::GpuPoolWorld pool;
aiverse::spectra::PoolWorldBudgets budgets;
budgets.maxGpuGroups = 16;
budgets.maxGroupCapacity = 1024;
budgets.maxBurstsPerFrame = 256;
pool.init(device, budgets);

aiverse::spectra::GraphRuntimeOptions gpu;
gpu.gpuProgram = true;
gpu.gpuMaterial = true;
gpu.capacity = 1024;
pool.defineGraph("ship_left", graphJson, aiverse::Json::object(), gpu);
pool.defineGraph("ship_right", graphJson, aiverse::Json::object(), gpu);
// Identical full graph + matching options => ONE GPU resource group.
// Runtime instance values remain independent for each source name.
pool.setNamedParameter("ship_left", "intensity", 0.3f);
pool.setNamedParameter("ship_right", "intensity", 0.9f);

// After gfx::Device::beginFrame(), BEFORE scene beginPass():
pool.beginPresentationFrame(camera, viewProj, width, height);
pool.burst("ship_left", {0, 1, 0});
pool.burst("ship_right", {3, 1, 0});
pool.step(1.f/60.f); // ONE CPU BatchWorld update + ONE update per GPU group

// DURING the already open scene render pass:
pool.draw(fog);      // one CPU batching pass + one draw per live GPU group
// After ending the render pass:
pool.endPresentationFrame();

// Between frames (no active render/compute encoder):
pool.resetForRollback(); // clears this pool's entire visual scene
pool.shutdown();         // BEFORE destroying the graphics Device
```

### Resource identity, budgets and parameter semantics

- GPU resource-group identity includes the **full canonical
  `fx::effectToJson(effect)`**, the capacity, effective quality tier
  and `gpuProgram`, `gpuMaterial`, `gpuIndirect`
  flags. A length-prefixed serialized key prevents collisions
  with authored shader graph strings. Matching definitions share
  exactly one `ComputeParticles` group; different effect
  definitions or GPU policies do not falsely share state.
- `PoolWorldBudgets` limits GPU groups (0..64), per-group
  physical particle capacity (1..4096), one-shot burst starts
  per frame (0..4096) and shared CPU quality. Requested
  per-definition quality/capacity cannot exceed the world
  budget. When a GPU group is unsupported, unavailable or
  budget-exhausted, `Automatic`/`PreferGpu` retain the
  complete `EffectDef` in the shared CPU world.
  `RequireGpu` fails explicitly.
- `GpuPoolWorld::defineGraph` stores independent
  `instanceParameters` defaults for each logical name,
  even when its graph shares a GPU group. Use
  `setNamedParameter(name,parameter,value)` to change
  future emission defaults, with bounded type/range checks.
  `burst(...,const std::array<float,8>* sourceParams)`
  optionally overrides all eight values per call.
  Before a GPU birth, the world updates the group's **future**
  parameter set and `ComputeParticles` captures that
  set at Spawn. Older particles from other sources keep
  their own snapshots; CPU births use
  `BatchWorld::playWithProgramParameters`.
- Stage 28 also allows named `param0..7` inputs
  in a GPU **material-only** graph when no
  `ParticleProgramModule` is present.
  This does not enable unsupported GPU VM operations:
  the material itself must pass the strict Stage-27
  shaderGraph capability gate.
- `beginPresentationFrame / step / draw /
  endPresentationFrame` enforce **one update and draw
  per owned world per presentation frame**.
  The host remains responsible for the actual RHI
  beginFrame/beginPass/endPass/endFrame order.
  The scene must not draw the CPU batch a second
  time through another owner.
- `PoolWorldStats` reports number of unique GPU
  resource groups and CPU effect definitions, submitted
  and dropped burst counts, GPU memory, CPU/GPU
  particle estimates and draw counts. CPU and GPU
  budgets remain renderer-side and never modify
  deterministic ECS simulation.

### Current boundaries

This is **GPU resource reuse by full effect definition**,
not a single global GPU particle allocator for all materials.
Each unique GPU effect still has its own physical pool,
atomic alive counter and indexed draw. Compatible sources
with *identical* effects share those buffers and draw
submissions; unrelated shader/material definitions do
not merge just because they use the same texture.
The host currently submits **one-shot births only** into
these pooled groups. There is no per-source GPU kill/stop
handle, individual continuous GPU emitter lifetime,
cross-group transparent sorting or GPU-only global free
list yet. Rollback clears ALL GPU/CPU effects owned by
this GpuPoolWorld (not external particle worlds) and
must execute between frames while the RHI is healthy.

No existing RTS/FPS client is silently migrated.
`CueScheduler` remains the separate established
shared CPU event/attachment path. Adapt persistent ECS
sources and per-source GPU cancellation only when
semantics, generation identifiers and rollback parity
can be preserved.

**Verification:** The user explicitly postponed testing
until functional completion. No native CMake/CTest,
shader compilation, GPU frame capture, editor screenshot
or CPU/GPU resource-budget benchmark was run for
Stage 28. The implementation is committed **source**,
not a verified production graphics feature.

## Validation checklist

```bash
cmake -S . -B build/vfx-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/vfx-test --target aiverse_vfx_graph_tests aiverse_vfx_ecs_tests aiverse_vfx_assets_tests aiverse_vfx_ops_tests aiverse_vfx_gpu_tests aiverse_vfx_graph_runtime_tests aiverse_vfx_shared_batch_tests aiverse_vfx_cue_scheduler_tests -j4
ctest --test-dir build/vfx-test --output-on-failure -L SpectraFX
python3 tools/skills/catalog.py check
```

The current tests focus on graph compilation, asset caching/validation and read-only cross-genre ECS presentation. Visual regression screenshots, Android/Metal GPU performance, editor/MCP graph UX and FPS/RTS production event parity are still mandatory gates for a full Niagara-like release.

## Planned work

1. Validate current graph CRUD and basic visual node canvas, then add typed module inspector, scalable graph execution, richer dataflow and editor usability features.
2. Validate the Metal GPU compute/draw prototype on an actual device; implement GPU compaction, culling, sorting, richer renderers and graph execution with CPU fallback for GLES3/mobile/WebGL.
3. Rich graph dataflow, event handlers, function modules, simulation stages, user parameters, renderer plugins, soft particles, depth collisions, advanced quality/scalability.
4. Migration of existing RTS/FPS presentation adapters into a single SpectraFX path, native project sample games, performance and platform QA.

**Mandatory for agents:** after each bounded stage, append a separate handoff commit containing code SHA, affected files, API/serialization contracts, tests actually run, limitations and exact next task. See `AGENTS.md`.


## Stage 24 — generation-safe GPU VM particle parameter snapshots (experimental; tests deferred)

The public `engine/fx/SpectraGpuSourceParameters.h` provides a renderer-only
`gpu_program::SourceParameters` registry. `create(initial[8])` returns a
`SourceId {slot,generation}`; `spawn(physicalParticleSlot,id)` snapshots
all eight values at birth. `set(id,param,value,affectExisting)` changes
future births only by default, or also modifies already-born particles carrying
that exact source generation. `retire(id)` invalidates the source and preserves
existing particle snapshot values until explicit `kill(physicalSlot)`.
Generations never wrap to zero; exhausted slots cannot accidentally validate
stale handles. The table is presentation-only, not serialized into gameplay ECS.

`gpu_program::MetalVmPass::dispatchSources(states,count,stage,dt,registry)`
now uploads a 48-byte per-slot `ParticleParams` array and reads its individual
float8 values in the experimental Metal interpreter; legacy `dispatch(...,values)`
continues supplying one uniform parameter set. GPU particles retain the existing
64-byte state ABI. All physical slots must match the host's source registry,
and per-dispatch GPU buffer sizes are bounded. A separate host is responsible
for spawning/killing physical GPU slots in sync with the CPU mirror, and for
correct Spawn/Update scheduling.

**Not production integrated:** `GraphRuntime` and `ComputeParticles` remain
unchanged; no user graph automatically executes through the Metal VM. This pass
has no asynchronous GPU diagnostic readback or live-particle migration on device
failure and currently uploads the active prefix of snapshots each dispatch.
Do not claim GPU/CPU parity, performance or error-free execution. No native
compile, Metal device test or CTest was performed, in accordance with the user's
instruction to defer testing until feature work is complete.


## Stage 25 — GPU VM physical slot lifecycle and incremental snapshot upload (experimental)

The public `gpu_program::ParticleSlots` pairs a renderer-owned physical slot
with a generation-safe `ParticleId` and `SourceParameters` snapshot. The
host calls `acquire(sourceId)` before uploading a GPU spawn seed,
`markSpawnSubmitted(particleId)` only after successful Spawn VM submission,
and `release(particleId)` **only after GPU work using the old physical slot
has completed**. `needsSpawn`, `alive`, `physicalPrefix` and `activeCount`
assist generic native hosts without an FPS/RTS coupling. The adapter does not
schedule rendering or advance game simulation.

Stage 25 also marks newborn snapshots with `reserved=1`; the experimental
Metal kernel executes the Spawn VM only on these pending snapshots and skips
older particles for that stage. The host acknowledges submission to clear the
pending flag. `Update` remains applicable to all active particles. This
corrects the previous risk of repeated Spawn execution on all live particles.

`MetalVmPass::dispatchSources` caches its own previous per-particle values and
uploads only changed contiguous ranges, instead of uploading the entire active
prefix every call. On Metal, the RHI enqueues storage-buffer blits in command
order before a compute encoder. `readErrors(uint32_t&)` provides a strictly
optional RHI diagnostic readback; a failed read is **not** equivalent to zero
VM errors. Readbacks must be done between completed frames, not in an active
compute/render encoder. GPU error handling/fallback is not yet automatic.

**Boundary:** there is still no automatic `GraphRuntime` / `ComputeParticles`
VM integration, no fully GPU-owned multi-source allocator, no proven shader
compilation, no GPU/CPU numerical parity and no lossless device-failure migration.
Tests and native builds remain deferred by user instruction.


## Stage 27 — integrated Metal GPU VM error diagnostics and honest fallback accounting

The integrated Metal particle kernel (`SpectraGpuShaders.h`, *not* the
standalone experimental Stage 23 kernel) now counts failed VM statements,
invalid arithmetic and nonfinite assignment results into a dedicated atomic
storage-buffer counter. The counter is reset before a nonempty compute frame
and bound as Metal compute buffer slot 12. A failed VM statement continues to
be skipped, as the existing CPU VM semantics require; diagnostics do not enable
unsupported GPU graph operations.

`ComputeParticles::readGpuVmErrors(uint32_t&)` and
`GraphRuntime::readGpuVmErrors(uint32_t&)` expose an opt-in, blocking,
**between-completed-frames** diagnostic. A `false` result indicates
unsupported/unavailable readback, never a count of zero.
`ComputeStats::gpuVmErrorsKnown` is false until a successful read and is
invalidated on every new GPU frame or CPU fallback. Do not call this from an
active render/compute encoder. The Metal RHI's `readBuffer` explicitly refuses
an active command buffer and waits for a separate readback submission; only use
for diagnostics, not every game frame.

On unhealthy GPU, `RequireGpu` continues to reject fallback. In permissive
modes, the existing `switchToCpu` creates a new CPU emitter, **not** a live
state migration. The `lostOnFallback` count now includes pending GPU slot
allocations rather than only last frame's drawn particles. This change
improves accounting, but GPU-resident particle positions, attributes,
per-particle ages and remaining lifetimes are still lost. A recovery policy
requiring lossless migration must retain or read back a complete checkpoint
and reconstruct CPU state. **No such migration exists yet.**

Status: source code committed; native C++ build, Metal shader compile,
CTest, editor UI and GPU fault-injection tests NOT RUN. Do not claim that
this is a production-grade recovery implementation.


## Stage 28 — diagnostic Metal GPU checkpoints + asynchronous VM error requests

`ComputeParticles::captureGpuCheckpoint(GpuCheckpoint&)` (and the graph-runtime
wrapper) reads all **physical GPU slots**, including the 64-byte position/age,
velocity/lifetime, tint and size state, the eight 32-bit VM instance parameters
and eight VM custom attributes. It also captures the CPU-side remaining-life
mirror and completed frame serial. Each exported `GpuCheckpointParticle`
occupies 128 bytes. A checkpoint is marked `complete` only after all three
storage-buffer reads succeed; invalid, unavailable, active-frame or pending
birth state is rejected. Calls are blocking, strictly between completed Metal
frames: never run one every interactive frame. The Metal RHI's synchronous
`readBuffer` uses a separate copy command and waits for completion.

`ComputeParticles::requestGpuVmErrors(callback)` uses RHI
`readBufferAsync` to return `callback(bool success,uint32_t count)` on a
subsequent frame. Only one pending diagnostic request is accepted at a time.
The lifetime token cancels callbacks if GPU resources have been destroyed;
frame-revision checking prevents old error results from overwriting stats for
a newer frame. No callback is promised if the VM runtime is destroyed before
its readback returns. `GraphRuntime` forwards both APIs.

**Important:** this is a GPU state *export* and a basis for future recovery,
**not** lossless GPU→CPU fallback. The existing CPU `ParticleSystem` does not
provide an import API for live particles with their custom attributes and
instance state. If the GPU becomes unhealthy, reading the now-lost buffers
cannot be assumed possible. Stage 29 must implement transactional CPU import
and periodic captured checkpoints with a safe resource/fence policy before
automatic recovery is enabled. `lostOnFallback` continues to report lost
particles honestly. `RequireGpu` still forbids silent CPU fallback.

No compiler, CMake, CTest, Metal execution, fault-injection test or CPU/GPU
parity test has been performed for Stage 28; source is unverified.


## Stage 29 — transactional CPU checkpoint import and guarded GPU-failure recovery

`fx::ParticleSystem::importLiveParticles(effect, particles)` appends already-live
particles to a compatible **single-emitter** effect without replaying Spawn or
creating an emitting instance. It validates the entire batch (finite position,
velocity, color, age, lifetime, size, eight VM params/attributes), existing
CPU capacity and supported emitter restrictions before altering the CPU pool.
Each imported particle keeps the position, velocity, age, remaining lifetime
encoded as age/total lifetime, size, tint and custom registers. It is tagged
with no emitting owner, participates in normal CPU Update and renderer batching,
and cannot start new emitter bursts. Unsupported complex ribbons, collision,
trigger, mesh and local-space effects are rejected rather than partly restored.

After a successful blocking `ComputeParticles::captureGpuCheckpoint`, the
runtime retains the complete export plus its presentation-frame revision.
When `switchToCpu` occurs after device failure, it attempts recovery **only**
when the checkpoint matches the last submitted GPU frame, has the full physical
slot count, and there are no unsent births. Imported particles are counted in
`ComputeStats::recoveredOnFallback`; remaining discarded allocations are
reported in `lostOnFallback`. On stale/missing/invalid snapshots the existing
CPU fallback still starts clean and reports losses. `RequireGpu` continues
forbidding an implicit CPU switch.

**Recovery is opt-in by capture timing:** checkpoints are not collected
automatically because the available synchronous Metal RHI readback blocks
until the GPU finishes. Call checkpoint capture only between completed frames
at deliberate safe intervals. An unhealthy GPU cannot be relied on to produce
new snapshots. Exact CPU/GPU numerical continuation is not proven: renderer
and simulation algorithms differ, imported particles have no original source
instance, and a formerly live effect may stop emitting. The term *lossless*
is not warranted until parity, all emitter categories and fault injection
are verified. No native compiler, CTest, Metal test or GUI smoke was run for
this stage; code remains unverified.


## Stage 30 — opt-in chunked asynchronous Metal checkpoint capture (source committed, not validated)

`GraphRuntimeOptions::checkpointEveryFrames` (or the direct
`ComputeEmitterDesc::checkpointEveryFrames`) controls a **disabled-by-default**
periodic checkpoint request. Every N successful Metal compute submissions the
runtime may enqueue a snapshot *after* Spawn/Integration/Update/Compaction,
without waiting for completion. `GraphRuntime::requestGpuCheckpoint(callback)`
and `ComputeParticles::requestGpuCheckpoint(callback)` allow explicit manual
requests. The callback reports `bool complete`, not a mutable GPU buffer
pointer. A failed scheduling attempt returns `false` and must not yield a
usable partial checkpoint.

For each physical GPU slot the snapshot includes 64 bytes particle state,
32 bytes per-particle VM inputs and 32 bytes custom registers — maximum
512 KiB at the current 4096 GPU slots. The Metal RHI now implements
`readBufferRangeAsync(handle,offset,size,callback)` for reads of up to
64 KiB each. The checkpoint splits the three buffers into at most eight
small requests, assembles them only after all replies have succeeded, and
uses a renderer-lifetime token to cancel callbacks after teardown. At most
one checkpoint capture per ComputeParticles instance is pending. A partially
queued batch is rejected and never published.

The GPU frame revision is incremented **on successful compute submission**,
fixing the earlier non-incrementing freshness token. The async result stores
its originating revision; `switchToCpu` may restore it only if it still
matches the latest GPU frame and there are no pending unsent particles. A
periodic snapshot from an earlier frame can remain for diagnostics but is
not eligible for a supposedly lossless restoration. Missing, incomplete or
stale snapshots retain the explicit loss-counting CPU fallback.

`ComputeStats` exposes cumulative `checkpointRequests`,
`checkpointCompleted` and `checkpointFailed`. An automatic request never
blocks render submission with a synchronous read, but enqueues device copies;
the callback and CPU assembly still consume resources. The full 512 KiB
capture is opt-in and can be costly on mobile. Captures from complex effects
are not guaranteed to reproduce an exact CPU trajectory, since GPU/CPU VM
and renderer numerical parity has not been demonstrated.

**NOT RUN:** Clang/CMake, CTest, Metal shader compilation, real GPU
checkpoint/recovery, fault injection and performance analysis. This API
is experimental and not a production-safe guarantee of recovery.

## Stage 29 — shared GPU-pool committed-tick cue bridge (functional source, tests deferred)

`spectra::GpuCueBridge` adapts bounded one-shot presentation events
to the existing `spectra::GpuPoolWorld`. It does **not** create its own
particle simulator, GPU group, RHI resources, or gameplay ECS components.
The pool continues to coalesce compatible graph/material/VM policy
into shared `ComputeParticles` groups and uses the original shared CPU
`BatchWorld` for unsupported effects.

```cpp
#include <aiverselib/mechanics/SpectraFX.h>
aiverse::spectra::GpuPoolWorld pool;
pool.init(device);
pool.defineGraph("impact", graphJson, aiverse::Json::object(), options);

aiverse::spectra::GpuCueBridge cues(pool);
cues.bind(42, "impact");

// Once AFTER a committed gameplay simulation tick:
aiverse::spectra::acceptSimGpuCueTick(cues, tick, simEvents);

// Per render frame, after Device::beginFrame and before its scene pass:
pool.beginPresentationFrame(camera, viewProjection, width, height);
cues.flush(); // queues are bounded; effects route to GPU/CPU via pool
pool.step(dt);
// Within the open render pass:
pool.draw(fog);
// After the render pass:
pool.endPresentationFrame();

// On a gameplay rollback: outside GPU render/compute passes,
// before replaying any restored committed tick:
if (!cues.resetForRollback()) { /* handle cues.reason() */ }
```

`GpuCueBudgets` controls `queued`, `acceptedPerTick`, and
`dispatchedPerFrame`, independently of the pool's global
`maxBurstsPerFrame`. `accept(tick, span<GpuCueEvent>)` refuses
duplicate or decreasing ticks. Accepted cues capture the effect's
**name** at admission time so later rebinding of an event type does
not change queued events. The bridge tracks admission, drops,
successful launches and world-level rejects. Failures during
`flush` are not treated as emitted and are dropped rather than
blocking future frames. `GpuCueEvent` optionally supplies a
source generation key, a direction, tint and a snapshot of eight
`param0..7` values.

`resetForRollback()` calls `GpuPoolWorld::resetForRollback()`
**before** clearing queued events and its tick cursor. A rejected
reset leaves the bridge's state intact so a safe retry is possible.
This reset clears the **entire pool**, not only the bridge's
particles: the host must dedicate the pool to the same presentation
rollback scope.

Native ECS projects use
`libraries/include/aiverselib/mechanics/SpectraGpuCueEcsAdapter.h`
and `acceptSimGpuCueTick` for the existing `SimEvent` type.
The adapter is read-only and makes no FPS/RTS/genre assumptions.

**Important functional boundary:** This bridge handles only
**one-shot** effects. The pool currently does not expose a per-source
GPU particle handle that could selectively stop/kill particles
or retarget a continuous emitter. Continuous ECS-attached effects
remain with the existing shared-CPU `CueScheduler` until there is
a generation-safe per-source GPU lifecycle API. Do not claim
selective GPU source deletion, complete persistent ECS binding,
or standalone production migration.

**Verification:** tests are explicitly deferred by the user.
CMake/CTest, Apple Silicon Metal execution, screenshot/visual parity,
native FPS/RTS host integration and performance have **NOT RUN**.
This is committed source, not confirmed release readiness.


## Stage 32 — pooled checkpoint readback admission and live telemetry

GPU checkpoints are optional and **visual-only**. Stage 32 adds
`SpectraCheckpointReadbackBudget.h`, a graphics-owner-thread limiter that
reserves readback **bytes and chunk counts per GPU frame and across in-flight
requests**. A request exceeding either bound returns `false`; no partial
checkpoint is published. Per-emitter `ComputeStats` reports
`checkpointBudgetDenied` and `checkpointBytesReturned` in addition to
the existing completed/failed/request counters. The byte counter measures
valid callback payload received on CPU; it is **not** a hardware bandwidth
profile. The reservation remains held until callbacks complete or their
closures are destroyed, including rejected partial enqueues.

```cpp
using namespace aiverse::spectra;
auto limiter=std::make_shared<CheckpointReadbackBudget>(
    CheckpointReadbackLimits{});
GraphRuntimeOptions options;
options.checkpointEveryFrames=15; // 0 disables automatic snapshots
options.checkpointBudget=limiter;  // share across standalone GraphRuntime objects

PoolWorldBudgets poolOptions;
poolOptions.sharedCheckpointBudget=limiter;
GpuPoolWorld pool;
pool.init(device,poolOptions); // now every GPU group uses this same limiter

// After any presentation frame / callback deliveries:
const PoolCheckpointStats info=pool.checkpointStats();
// info.pendingGroups / staleGroups / freshGroups / missingGroups
// info.restoreEligibleGroups / budgetDenied / bytesReturned
// info.budget.bytesInFlight / chunksInFlight / frameBytesReserved
```

If `sharedCheckpointBudget` is omitted, `GpuPoolWorld` creates one limiter
for all its own GPU groups using `PoolWorldBudgets::checkpointReadback`.
The default ceiling is 512 KiB and 16 chunks per frame; 1536 KiB and
24 chunks in flight. The Metal RHI supports only 32 total in-flight ranged
reads; the 24-chunk ceiling leaves a margin for VM diagnostics but does not
guarantee admission when other readback clients are active. Multiple worlds
on the **same** graphics device must pass the **same shared limiter** to
enforce one device-wide budget; independent worlds otherwise enforce only
their local bounds. Standalone `GraphRuntime` keeps its old independent
behavior unless `GraphRuntimeOptions::checkpointBudget` is supplied.

Pooled identical effects are grouped by complete serialized effect, GPU
policy, and now checkpoint cadence. The pool rotates its per-frame GPU
group submission start to prevent one permanently first group from consuming
the entire shared checkpoint budget. This changes presentation-only dispatch
ordering, not ECS simulation or authoritative gameplay.

`PoolCheckpointStats` is computed on demand without GPU readback and
includes successful callback payload counts plus freshness classification
from `checkpointStatus()`. Budget metrics reflect the entire limiter
when intentionally shared with other worlds. A fresh checkpoint is merely
current at the last **submitted** GPU simulation revision and is not proof
of completed GPU/CPU numerical parity, source continuity, or lossless recovery.
No stale snapshot is imported automatically.

**NOT RUN:** C++/Metal builds, CTest, device-loss injection, partial-enqueue
fault simulation, multi-pool stress tests, or performance measurements.

## Stage 33 — adaptive checkpoint scheduling and source-safe pooled VFX

Checkpoint readbacks can now adapt their frequency when GPU submission is
backed up. The opt-in `AdaptiveCheckpointPolicy` is defined in
`engine/fx/SpectraAdaptiveCheckpoint.h` and passed through
`GraphRuntimeOptions` to `ComputeParticles`. Scheduling counts
**successful GPU simulation submissions**, not wall-clock frames or ECS ticks.

```cpp
using namespace aiverse::spectra;
GraphRuntimeOptions o;
o.checkpointEveryFrames=15;
o.adaptiveCheckpoint.enabled=true;
o.adaptiveCheckpoint.maxIntervalFrames=240;
o.adaptiveCheckpoint.recoverAfterCompletions=3;
o.checkpointBudget=sharedBudget; // optional Stage 32 shared limiter

// Runtime accepts the options during initGraph / initEffect.
// A budget denial, a pending earlier capture, or RHI rejection doubles
// the interval up to maxIntervalFrames; fully completed captures restore
// it by halves after the configured number of successes.
const auto st=runtime.checkpointStatus();
// st.checkpointIntervalFrames; st.nextCheckpointRevision
// runtime.gpuStats().checkpointAdaptiveBackoffs / Recoveries
```

The scheduling state is optional and does not block rendering. It cannot make
an old snapshot current or guarantee lossless CPU fallback. A capture
completion is counted as a successful transfer even if the simulation has
advanced and the snapshot is therefore stale.

`GpuPoolWorld` now supports **generation-safe** presentation source handles.
A source owns a name-bound effect revision and a distinct 8-scalar parameter
snapshot. It can pause, resume, retire, and emit multiple bursts through the
shared pooled GPU/CPU resources:

```cpp
PoolSourceId source=pool.createSource("impact");
pool.setSourceNamedParameter(source,"speed",2.5f); // if "speed" is declared
pool.emitSource(source,position);
pool.pauseSource(source);        // prevents future emission
pool.resumeSource(source);
pool.emitSource(source,newPosition);
pool.retireSource(source);       // invalidates this handle
```

Source handles carry `worldEpoch`, `slot` and `generation`. A retired,
reused, post-rollback or reinitialized handle cannot legitimately emit.
Redefining a named effect invalidates old handles' binding revisions instead
of silently changing the graph used for future bursts. `maxSources`
limits active/reusable slots (default 4096, max 65536). This remains
genre-agnostic, ECS-read-only presentation state.

**Important limitation:** `pauseSource` and `retireSource` prevent
**future** emissions. Already-born GPU particles continue their natural
lifetime and cannot yet be selectively killed or migrated to a different
source group. Per-particle GPU slot ownership and fence-safe selective kill
are a separate feature. A rollback resets the shared pool and invalidates
source handles. `PoolWorldStats::activeSources` counts active handles,
including paused ones.

The editor SpectraFX Graph Window has an optional **Metal GPU checkpoint —
live** telemetry panel using `PoolCheckpointStats`. The host may attach a
non-owning pool via `Editor::setSpectraGpuTelemetry(&pool)` and **must
detach** it before the pool is destroyed. The existing Scene VFX preview
is CPU-only and **does not** automatically create a Metal GPU pool; without
an attached pool, the window displays an explicit unavailable message.
Diagnostics include fresh/stale/missing/pending, readback budget pressure,
bytes returned, adaptive backoffs/recoveries, current maximum interval and
active source count. These do not imply completed GPU validation.

**Verification:** GitHub source commits only; **NOT RUN** CMake/Clang,
Metal shader compilation, CTest, native editor launch, fault-injection,
render preview, user-facing GPU capture validation or performance benchmark.

## Stage 34 — source-owned particles, continuous emission and Metal editor preview

Stage 34 supersedes Stage 33's limitation that pooled GPU particles cannot be
selectively killed. The resource group still owns one shared `ComputeParticles`
instance per unique effect/policy: this is **not** a separate compute emitter
for every game object or a genre-specific VFX system.

The presentation-only source API now supports an ordered selective kill, a
bounded continuous scheduler, and a host-updated render transform:

```cpp
using namespace aiverse::spectra;
PoolSourceId flame=pool.createSource("flame");
pool.setSourceTransform(flame,{10,1,20},{0,1,0},{1,0.5f,0.2f,1});
pool.configureContinuous(flame,PoolContinuousEmission{
    .burstsPerSecond=12, .metresPerBurst=0.5f,
    .lifetimeSeconds=0, .maxBurstsPerFrame=8
});

// Once after gfx::Device::beginFrame and before the scene pass:
pool.beginPresentationFrame(camera,viewProj,width,height);
// Native scene updates the current transform once or more before pool.step().
pool.setSourceTransform(flame,updatedPosition,forward,tint);
pool.step(dt);   // emits at configured time/distance gates, then simulates
// During depth-tested scene pass: pool.draw(fog);
// After render pass: pool.endPresentationFrame();

pool.pauseSource(flame);         // future emissions only
pool.resumeSource(flame);        // resumes bounded scheduler
pool.stopSource(flame,true);     // also selectively kill current particles
pool.retireSource(flame);        // invalidate handle/release source slot
```

`PoolContinuousEmission` defines a rate gate in bursts per second, a
distance gate in metres per burst, optional elapsed lifetime (0 means no
expiration), and an additional per-source max bursts/frame. Both gates
contribute births, limited by global `PoolWorldBudgets::maxBurstsPerFrame`.
Excess is **dropped**, not queued for unbounded catch-up.
`PoolWorldStats::continuousBursts` and `continuousSkipped` track both.
Attached transforms are renderer-fed values; the engine does not insert
graphics state into authoritative ECS. A native ECS adapter may call
`setSourceTransform` from an immutable/committed render-pose snapshot.

For GPU groups, `ComputeParticles::burstAt(...,sourceToken)` marks every
physical slot with a source ownership token. The token is unique for a
pool lifetime and does not alias a newly recycled source handle.
`killSourceParticles(token)` removes source-matching births which were
never submitted, and queues the submitted physical slots for GPU invalidation.
On the next `step`, contiguous killed slots are coalesced into dead-state
buffer updates **before** the compute encoder; only after the ordered GPU
submission are those physical slots returned to the free list. GPU indirect
compaction subsequently omits invalidated particles. This is a scoped kill,
not a global buffer clear. The source's CPU path retains bounded
`FxHandle` ownership and calls the existing shared CPU `kill` on demand.
The `selectivelyKilled` counter is GPU particles marked/cancelled, not a
GPU-side observed count. Pending kills make checkpoints ineligible for CPU
restore, and a clear invalidates prior checkpoint revision data.

**Editor GPU preview (opt-in):** SpectraFX Graph Window offers
**Metal GPU** alongside the original CPU **Предпросмотр**. Equivalent MCP
operation is `fx_graph_preview {graph:..., gpu:true, loop?:true,
instanceValues?:{...}}`. Graphs are validated with
`inspectGraphGpuEligibility` and a real Metal-compute capability check:
`RequireGpu` refuses unsupported effects instead of silently dropping
features or drawing a CPU approximation. The editor queues the compiled
effect, creates a one-group GPU pool in the next pre-pass phase, submits
compute before `r3d_.render`, draws in the open scene pass, and releases
the preview after the pass. That live pool attaches automatically to the
SpectraFX checkpoint telemetry panel. The default CPU preview path
remains available and unchanged in output semantics. GPU preview does
not yet replicate the CPU preview's `advance`/`freeze` feature and only
previews the one-shot strict Metal billboard subset.

**Important correctness boundaries:** source kill and transform attach
remain presentation-only and require a healthy graphics device. Kill
completion is ordered at the command submission boundary, not GPU-fenced
to actual completion; correctness depends on the Metal RHI single command
queue and buffer-blit ordering. Native source IDs and GPU tokens must not
be serialized into authoritative gameplay saves. Source attachments
are snapshots updated by callers, not a built-in hierarchy/animation
transform-binding system. The GPU effect subset still does not implement
arbitrary Niagara particle graphs, GPU collision, light/event output,
multi-renderer effects, ribbon or material parity. Editor GPU preview
requires native Metal and an open Scene or Game viewport.

**Verification:** source commits/branch reads only. **NOT RUN:** native
CMake compilation, editor startup, Metal shader compilation, CTest,
GPU selective-kill/fence validation, multi-source compaction, editor
screenshots, fallback parity or performance profiling.

## Stage 35 — completed-frame GPU kill fencing and ECS attachments

**Main integration status:** At the start of Stage 35,
`main=9ee6ff6fbf390bd45363bc92c838f492dfe92eee` was already
an ancestor of `feature/universal-entity-inspector-fps`, whose starting
HEAD was `4a50d05329840e713d6179a1519fb2f16a8d8604`. No merge or
rebase was needed or performed. The feature branch was 450 commits ahead
of main; main was zero commits ahead of the feature branch.

The Metal-only `gfx::Device` API now optionally exposes
`updateStorageBufferChecked(handle,data,size,offset)` and
`completedFrameSerial()`. Other RHIs default to unsupported. Checked
Metal uploads must be encoded in the active frame outside graphics/compute
encoders and report whether the ordered copy was accepted. Metal stores
the highest **successfully completed** command-buffer frame serial
in a separate lifetime-safe atomic. A device with an unhealthy GPU or
no completion support must never be treated as though a fence passed.

`ComputeParticles::killSourceParticles(token)` still cancels unsent births
and uploads dead state into only the physical slots owned by the source.
Stage 35 now enqueues the kill writes through the error-reporting RHI
method and retains submitted physical-slot IDs in a per-frame quarantine.
It returns them to the spawn free list **only after**
`completedFrameSerial() >= submittedFrame`. If a checked upload fails,
the pending slots are retained rather than being reused. The
`GpuCheckpointStatus::killsAwaitingGpuFence`,
`ComputeStats::gpuKillFencesPending/killedSlotsReclaimed`, and
`PoolCheckpointStats::killedSlotsAwaitingFence/killedSlotsReclaimed`
diagnostics expose the CPU-side state. While any source kills await
completion, fresh checkpoints are not eligible for fallback and blocking
or async captures are rejected.

### Generic native ECS attachment bridge

`<aiverselib/mechanics/SpectraFX.h>` now exports the header-only
`spectra::GpuEcsAttachmentPresenter`, implemented in
`libraries/include/aiverselib/mechanics/SpectraGpuAttachmentEcsAdapter.h`.
It reads the **existing**
`lib::genre::SpectraSourceComponent` from `const World&` using
`eachExistingConst`. Each source is keyed by `Entity::packed()`, so
recycled ECS IDs cannot inherit another entity's VFX handle.
The caller supplies the same genre-neutral
`SpectraPresentationSystem::ResolvePose` used by the CPU path.

```cpp
using namespace aiverse::spectra;
GpuPoolWorld pool;
pool.init(device);
pool.defineEffect("engine_flame",compiledEffect,gpuOptions);
GpuEcsAttachmentPresenter ecsFx(pool);
PoolContinuousEmission rate{};
rate.burstsPerSecond=15;
rate.maxBurstsPerFrame=6;
ecsFx.bindEffect("engine_flame",rate);

// Once per presentation frame, after Device::beginFrame:
pool.beginPresentationFrame(cam,viewProj,width,height);
// Only the committed (or immutable copied) authoritative ECS snapshot
// is read; renderer pose resolver may interpolate visual positions.
if(!ecsFx.syncCommitted(committedTick,world,resolveRenderPose)) {
    // Handle ecsFx.reason(); never pretend that emission succeeded.
}
pool.step(dt); // GPU/CPU group simulation once BEFORE render pass.
// pool.draw(fog) once INSIDE open render pass.
// pool.endPresentationFrame() after render pass.

// When a rollback is observed: at a safe point outside active encoders,
// before replaying committed ticks or new presentation frames:
bool cleared=ecsFx.resetForRollback();
```

`syncCommitted(tick,...)` is idempotent for source creation at a repeated
tick but can update a render pose multiple times; decreasing ticks are
rejected until explicit `resetForRollback()`. Missing/disabled components
are retired; if `killOnDetach` is set, their existing particles are killed
selectively. A temporarily unresolved pose pauses new births and resumes
without recreating the handle when valid again. Effect/revision/policy
changes detach obsolete sources and create new ones. Policies are explicitly
registered with `bindEffect`; there are no genre-default rules.
The adapter exposes `detachAll(killLiving)` for explicit teardown.

**No silent scale mismatch:** The original `SpectraSourceComponent::scale`
has semantics the current GPU pooled billboard subset cannot fully
preserve. This bridge currently requires unit scale and reports an
error for any non-unit value. It never mutates ECS components, inserts
`PoolSourceId` into a simulation component, writes gameplay state or
uses GPU particles for authoritative collisions. A dedicated pool per
rollback scope is required because `GpuPoolWorld::resetForRollback()`
resets *all* its effects.

### Metal GPU preview controls

The SpectraFX editor window now has **Pause GPU/Play GPU**,
**Freeze GPU**, **Loop GPU** and **Stop GPU** for an explicitly opted-in
Metal preview. Pause stops future births while existing particles
continue to age; Freeze advances at zero visual delta and does not
advance existing particles. Loop repeatedly bursts the same strict
Metal-compatible graph. Preview and frame ordering are still
presentation-only, and the default CPU preview is not replaced.
The live panel now also reports physical slots waiting for GPU
completion and those safely reclaimed.

**Verification:** GitHub source writes and branch ancestry inspection
only. **NOT RUN:** local CMake/Clang build, Objective-C++/Metal shader
compilation, CTest, full editor launch, GPU screenshots, real frame
completion tests, device loss/fault injection, CPU-vs-Metal parity,
continuous attachment stress, or visual performance benchmarks.
Do not treat source completion as production readiness.

## Stage 36 — bounded GPU scatter kill and native ECS example

Stage 36 completes a **source-level optimization**, not a new
authoritative simulation mode. On supported Metal compute devices,
selective GPU source deletion no longer sends a separate full
64-byte dead-particle buffer upload per contiguous range. The
`ComputeParticles` runtime now uploads a compact list of physical
kill-slot indices (at most `capacity*4` bytes; 16 KiB for capacity
4096) into an explicit storage buffer. Shader mode **6** invalidates
only those physical indices before the existing Spawn/Integrate/
Compaction passes within the *same ordered compute encoder*.
The MSL implementation is active only on the strict Metal subset;
the matching GLSL case is future-backend reference code.

On failure of the checked upload, the pending kill slots remain
quarantined; the compute pass is not submitted and the physical slots
are not recycled. On successful compute submission, their recorded
graphics-frame serial is retained. Stage 35's
`completedFrameSerial()` fence check continues to control
subsequent recycling. Other source particles remain untouched.
This is asymptotically one 4-byte/slot index transfer and one
compute dispatch rather than up to capacity separate dead-state
blits. **No hardware performance numbers or shader compilation
have been verified.** Telemetry exposes
`gpuKillScatterDispatches` / `gpuKillIndexBytes`, aggregated through
`PoolCheckpointStats` and the editor.

### Native ECS usage

A new reusable native-project reference header lives at
`examples/spectrafx/native_ecs_attachments.h` with instructions
in `examples/spectrafx/native_ecs_attachments.md`.
It demonstrates `GpuEcsAttachmentPresenter`, the existing
`SpectraSourceComponent` and `Entity::packed()` generation
identity, read-only fixed-point render-pose resolution, frame pass
ordering, source retirement, and whole-pool visual reset after a
gameplay rollback. This is a **host integration example**, not a
standalone executable or confirmation that a project has been
built on a GPU. The host still owns gfx Device, its scene pass,
its own game tick and committed World snapshots.

### Editor GPU preview playback

The experimental **Metal GPU** preview toolbar now offers
`Step GPU` (while Freeze is on, advances only one visual update
of `1/60` seconds and does not emit a new burst) and
`GPU speed` clamped to `0.1x..2.0x`. The existing CPU
preview is unaffected; these parameters never change gameplay
time or deterministic ECS state. The diagnostic panel exposes
the GPU scatter-kill index bytes and dispatches. This preview
still only supports the strict, one-shot Metal billboard subset,
not the CPU path's advanced Niagara-style modules.

**Stage 36 verification:** GitHub connector source updates and
file reads only. Native C++ compilation, Objective-C++/Metal
shader compilation, CTest, editor rendering and Metal hardware
verification, source-generation rollback and performance tests
have **NOT RUN**. The source remains unverified for production.

## Stage 37 — staged ECS source admission, checked rollback reset and forward GPU timeline

### Staged ECS attachment admission

`GpuEcsAttachmentPresenter::syncCommitted(tick,world,resolve)` now uses
**staged** source creation. It validates every requested effect policy,
unit-scale limitation, and finite resolved pose before modifying
existing attachments. It creates/configures replacement
`PoolSourceId` handles in a temporary map and uses
`GpuPoolWorld::canStopSource(handle,killLiving)` as a read-only detach
preflight. Any allocation/configuration/preflight failure frees the
temporary sources and leaves the existing tracked effects alone.

If the old source is still alive but its named effect definition was
replaced in the GPU pool, `sourceBindingCurrent` detects the stale
binding even when that source is paused. The presenter prepares a
fresh source instead of trying to emit with the wrong definition.

For a replacement, the pool needs enough free `maxSources` headroom
to hold both the old source and the staged replacement temporarily.
When it cannot, sync fails **without silently removing existing
sources**; the native host can increase `PoolWorldBudgets::maxSources`
or explicitly retire some effects at a safe presentation boundary.
The commit phase that retires obsolete GPU sources cannot be made
fully transactional against a hardware failure during a later
selective-kill operation: a partial detach may still happen. The
adapter preserves remaining tracked handles and explicitly returns
false so the host can retry. This is not a promise of atomic
hardware-level state transitions.

### Categorized GPU diagnostics

`ComputeStats` now separately records
`gpuDeviceUnhealthy`, `gpuKillUploadFailures`,
`gpuComputeBeginFailures`, `gpuPassOrderRejections`,
and `gpuResetFailures`. `PoolCheckpointStats` aggregates these
counts and reports whether Metal device health is genuinely known
(`gpuDeviceHealthKnown`) and if it is healthy
(`gpuDeviceHealthy`). Unsupported backends show an **unknown**
rather than an invented healthy status. The editor SpectraFX
telemetry view surfaces all these statuses and source-kill fence
counters. A `GpuPoolWorld` group that failed its `step` no
longer draws stale vertices from a previous frame; standalone
`ComputeParticles` also invalidates its submission flags on
important early GPU step errors.

### Checked GPU rollback reset

`ComputeParticles::tryClear()` is a new explicit success/failure
API, while `clear()` remains a compatibility wrapper. Metal reset
requires an **active gfx Device frame outside render/compute
encoders**, not just a completed previous frame. It uses
`updateStorageBufferChecked` for particle state, VM parameter,
and custom attribute reset writes. It does not commit the CPU
free-slot/age mirror until *all* three ordered transfers have been
accepted. On a partial failure `gpuResetDirty_` blocks future
GPU births, simulation and checkpoint capture until a successful
reset is retried; it never reports stale state as an exact recovery.
These checked uploads still confirm **command encoding**, not
successful hardware completion. Later GPU failure must be handled
using device health and completed frame telemetry.

`GpuPoolWorld::resetForRollback()` calls `tryClear()` for all
GPU groups before clearing the shared CPU effect world or
releasing ECS presentation sources. If a group fails, it returns
false and preserves CPU/source tracking for a retry. An earlier
GPU group may already have cleared successfully; the operation
is therefore **best-effort across multiple GPU groups**, not an
atomic multi-group transaction. Source clients must not replay
presentation ticks until the reset succeeds.

```cpp
// At a rollback boundary: finish the old scene/graphics frame first.
device.beginFrame(width,height); // active RHI command buffer required
bool resetOk=attachmentPresenter.resetForRollback();
device.endFrame();
if(!resetOk) {
    // Do not claim visual reset. Retry at a subsequent safe frame.
}
```

The Stage 36 native integration example has been updated to show this
required frame ordering.

### GPU preview timeline (forward only)

The editor's opt-in **Metal GPU** graph preview now displays
`Visual simulation time` (accumulated only from successful
presentation steps). While Frozen, the user can request forward
advancement in 1/60-second increments; at most **300 frames**
may remain queued at once and one frame is consumed for each
rendered preview frame. The **Restart GPU** control schedules a
checked whole-pool visual reset at the next safe pre-pass phase.
No reverse seek or restoration of historical GPU frames is claimed.

The preview loop cadence now follows **simulated visual time**
instead of editor wall time, so GPU preview speed/freeze settings
do not silently change the repeat rate. Advanced frames while
frozen only evolve existing particles and do not create new
bursts. A failed GPU step does not increase the displayed time;
the panel reports the last preview error.

**Actual verification:** GitHub source writes and source/ancestry
inspection only. **NOT RUN:** local CMake/Clang compilation,
Objective-C++ or MSL compilation, CTest, Metal hardware tests,
editor UI launch/screenshots, GPU reset fault injection, multi-group
atomicity stress, source staging under capacity pressure, or
CPU/GPU parity. Implementation is unverified executable source;
do not enable it by default for production.

## Stage 38 — retryable GPU-group reset, Device recovery and interpolated ECS presentation

### Explicit recovery state for pooled Metal VFX

A SpectraFX `GpuPoolWorld` now has a read-only
`PoolResetReport resetReport()` and `presentationBlocked()` gate.
A rollback attempt remembers a **per-GPU-group accepted-reset mask**,
rather than resetting already prepared groups repeatedly. If group
number N rejects its checked reset uploads, `resetForRollback()`
returns false, records `groupsAccepted`, `groupCount` and
`failedGroup`, and blocks new source creation, bursts, GPU frame
submission and drawing. A later safe presentation-frame retry skips
groups whose uploads have already been accepted. The CPU effect
world and renderer-source handles are not invalidated until ALL
GPU groups accept their ordered reset commands.

This is **restartable preparation**, not a hardware-atomic reset.
`groupsAccepted` means the RHI **encoded** the reset uploads;
the GPU may not have finished. If a subsequent Metal command buffer
fails, the host must handle device health/loss and reinitialize the
graphics stack. A lost Device latches `resetReport().deviceLost`;
the pool does not falsely resume merely because a health flag
later becomes true.

A new CPU-only definition catalog, `PoolRecoveryManifest`, allows
a native host to reconstruct effects on a **new, healthy graphics
Device**. It preserves effect names, compiled original `EffectDef`,
graph options, declared runtime-parameter schema and effective
default values. It does NOT contain renderer source IDs, graphics
handles, particle buffers, emitter timing or GPU checkpoints.

```cpp
using namespace aiverse::spectra;
PoolRecoveryManifest snapshot=oldPool.exportRecoveryManifest();
// Keep OLD graphics Device alive until oldPool.shutdown() finishes.
oldPool.shutdown();
GpuPoolWorld recovered;
bool ok=recovered.importRecoveryManifest(&newHealthyDevice,snapshot);
// Recreate the ECS attachment presenter against recovered and re-sync
// the latest committed ECS World only if ok == true.
```

Import fails without publishing a usable partial pool if a
`RequireGpu` effect is unsupported on the new backend. The old
readback limiter is not reused across the Device boundary: a new
checkpoint budget is created. GPU particles are deliberately lost;
the goal is safe re-creation of effect definitions and native
presentation attachments.

### Render-pose interpolation for ECS sources

The public native header
`<aiverselib/mechanics/SpectraFX.h>` exports
`GpuEcsPoseInterpolator` from
`libraries/include/aiverselib/mechanics/SpectraEcsRenderPose.h`.
The native host supplies the same `ResolvePose` callback used by
`GpuEcsAttachmentPresenter`. The interpolator stores at most two
renderer-side pose snapshots, captured once per **committed** ECS tick
through const World iteration, and samples them with
`alpha` in the range 0..1.

```cpp
GpuEcsPoseInterpolator poses;
poses.captureCommitted(committedTick,world,resolvePose);
pool.beginPresentationFrame(camera,viewProj,width,height);
presenter.syncCommitted(committedTick,world,poses.resolver(renderAlpha));
pool.step(dt);
// draw scene + VFX, then end presentation frame
// On rollback: presenter.resetForRollback() at safe frame;
// only when it succeeds: poses.resetForRollback();
```

Only consecutive committed ticks and identical
`Entity::packed()` identities interpolate. New/recycled entities
snap to their current pose, avoiding history leaks across ECS
generations. Nonfinite poses, excessive source counts or decreasing
ticks are rejected without replacing a valid pose history. If a
pose is unavailable, the attachment presenter pauses new emission;
the interpolator never writes float positions into authoritative
ECS. The updated `examples/spectrafx/native_ecs_attachments.h`
shows this integration and explicitly rebuilding after device
loss.

### Read-only diagnostic history in Metal GPU preview

The optional editor Metal GPU preview records the last **180
rendered preview attempts** as a CPU-side timeline with
`deviceFrame`, successfully accumulated visual time, estimated
GPU particle count, checkpoint fresh/stale/pending counts, source
kills awaiting a completion fence, GPU health and whether the
simulation step was accepted. The SpectraFX editor shows a small
history plot and can copy these records as JSON through
`spectraGpuPreviewHistoryJson()`.

The history is intentionally **not** a restorable GPU snapshot:
no particle positions, VM registers or arbitrary framebuffer
content are saved. Reverse seeking is still unsupported; the
Stage 37 forward-only step/restart model remains intact.
Device recovery/partial rollback status appears in the same
editor diagnostics without asserting that accepted GPU commands
have already completed.

**Verification:** GitHub source writes, tree/branch readback and
manual API/source inspection only. **NOT RUN:** local CMake or
Clang compilation, Objective-C++/MSL shader compile, CTest,
editor startup, actual Metal device-loss recovery, multi-group
partial rollback fault injection, ECS pose interpolation tests,
image parity or GPU performance benchmarks. Do not enable these
experimental features by default in production based only on
source code presence.

## Stage 39 — recovery host events, bounded fresh GPU exports and fair VFX pressure

### Native GPU recovery notifications and explicit Device replacement

The genre-neutral native presentation header
`<aiverselib/mechanics/SpectraFX.h>` now exports
`GpuPoolRecoveryHost` from
`libraries/include/aiverselib/mechanics/SpectraGpuRecoveryHost.h`.
This object holds **references only** to an initialized
`GpuPoolWorld`, the presentation-owned
`GpuEcsAttachmentPresenter` and optionally a render-pose
interpolator. It does not take ownership of the platform's
`gfx::Device` or perform background work.

```cpp
GpuPoolRecoveryHost recovery(pool,ecsPresenter,&poseHistory);
recovery.onChange([](const PoolRecoveryEvent& e) {
    // e.phase/e.next: Ready, retry rollback in an active gfx frame,
    // recreate Device, inspect unknown failure or initialize the pool.
    // e.resetGroupsAccepted, e.resetGroupsTotal, e.resetAttempts, e.reason
});
auto status=recovery.poll();
if(status.next==PoolRecoveryAction::RetryResetInGraphicsFrame) {
    // Only after Device::beginFrame, BEFORE any active encoder:
    if(!recovery.retryPresentationReset()) { /* do not render VFX */ }
}
if(status.next==PoolRecoveryAction::RecreateGraphicsDevice) {
    // Host/platform explicitly creates a healthy replacement Device.
    // OLD Device remains alive through old-pool shutdown.
    bool restored=recovery.replaceDevice(newHealthyDevice);
    // Compiled effect definitions survive; live GPU particles do not.
}
```

`poll()` reports only phase/progress/health transitions to its
callback, and never invents successful recovery. The host must
perform actual platform device reconstruction. `replaceDevice()`
exports the CPU-only recovery manifest, releases old graphics
resources, invalidates presentation-owned source handles and
interpolated pose history, then imports effect definitions into a
freshly initialized pool. No GPU-resident particle buffers,
authoritative ECS data, network state or previous renderer source
generations are resurrected. The standalone native integration
example at `examples/spectrafx/native_ecs_attachments.h` now
uses this controller for rollback and new-Device recreation.

### Strict and bounded GPU checkpoint binary diagnostics

`engine/fx/SpectraCheckpointExport.h` defines a 40-byte
`GpuCheckpointExportHeader` with magic `SPFX`, format version,
slot count, captured GPU revision, graphics frame serial and raw
payload length. The capture body consists of the existing 128-byte
per-slot `GpuCheckpointParticle` state plus a 4-byte-per-slot
CPU lifetime mirror. At the existing 4096 physical-slot limit the
maximum is **512 KiB GPU state + 16 KiB mirror + 40-byte header**.
The encoding is a native ABI diagnostic blob, **not** a portable
savegame, stable cross-version serialization, screenshot, or
restorable GPU timeline.

`ComputeParticles::exportFreshCheckpoint(bytes,maxBytes)` and
`GpuPoolWorld::exportFreshCheckpoint(groupIndex,bytes,maxBytes)`
only copy **existing completed** GPU checkpoint data when all of
these are true: Metal GPU is active and healthy, the retained
checkpoint's captured revision is exactly the current GPU revision,
the snapshot is complete and the correct slot count, there are
no unsent particle births or kill slots awaiting a GPU fence,
no incomplete GPU reset, and no concurrent pending checkpoint read.
The encoded buffer is swapped into the output only after all
preflight/size validations; failure leaves the caller's old
output intact. These methods **never initiate GPU readback**.

`PoolCheckpointStats::exportReadyGroups` counts groups that
currently satisfy these conditions. The SpectraFX editor now
shows the count and offers **Save fresh GPU checkpoint** only when
a group is eligible. It saves one bounded `.spfx` file under
`platform::userDataDir()/spectrafx-diagnostics/` using a temporary
file and then renaming it. If an async readback is stale at UI
time, the button is disabled or a clear error is returned; there
is no fake snapshot or blocking synchronous readback. This feature
requires the host to have opted into actual checkpoint cadence
or to have completed a manual checkpoint; by default, no
checkpoints are automatically captured.

### Fair automatic VFX load shedding (opt-in)

`PoolLoadSheddingPolicy` is available inside
`PoolWorldBudgets::loadShedding` and via
`GpuPoolWorld::setLoadShedding(policy)` between presentation
frames:

```cpp
PoolLoadSheddingPolicy policy{};
policy.enabled=true;
policy.maxContinuousBurstsPerFrame=64;
policy.reserveManualBursts=16;
policy.estimatedParticleSoftLimit=8000; // previous frame's estimate
policy.maxReadbackBytesInFlight=1024u*1024u;
pool.setLoadShedding(policy);
```

The scheduler rotates its starting source slot every frame, so
a consistently busy low-index emitter cannot permanently starve
higher-index emitters. The enabled policy caps automatic births
while preserving the existing global limit and an explicit
allocation of capacity for manual one-shot bursts. Particle
pressure is based on the **previous rendered step's estimated**
CPU + GPU particle count; readback pressure uses the existing
Stage 32 scheduler's reserved bytes in flight. This is not a
GPU performance measurement or hard memory-residency guarantee.
Excess automatic bursts are discarded, **not** queued into an
unbounded catch-up backlog. Disabling the policy preserves the
old overall emission behavior (other than fair rotation).
`PoolWorldStats` reports
`continuousFrameBudget`, `pressureActive`,
`pressureSkipped` and `sourceCapacity`; the editor renders
these values when a GPU pool is attached.

**Stage 39 verification:** GitHub source commits and source API
inspection only. **NOT RUN:** native C++/ObjC++/MSL compilation,
CTest, editor launch, on-device Metal simulation, complete/fresh
checkpoint export at various revisions, GPU reset/device loss
fault injection, pressure/starvation validation or performance
benchmarks. The implementation is not production-verified.

## Stage 40 — frozen-revision GPU snapshots, checked input staging and VFX hysteresis

### GPU Freeze retains the particle revision without freezing the camera

Stage 39 async checkpoint captures were often obsolete by the time Metal
delivered the readback callback because `ComputeParticles::step(0)`
still dispatched the Update VM, even though the emitter was frozen.
Stage 40 adds `gpu_pipeline::plan(...,bool integrate=true)`.
When `dt==0` and there are **no pending births, source kills or
live VM parameter edits**, a GPU step skips Update VM mode 1,
but still executes camera-facing vertex gather/atomic compaction and
indirect-argument publication. The particle state revision is not
incremented during this graphics-only redraw. An existing complete
checkpoint can therefore remain fresh across several frozen
rendered frames, even while the editor camera moves.

The new `GpuPoolWorld::requestGpuCheckpoint(groupIndex,callback)`
forwards a bounded asynchronous capture to a valid GPU group. It
never performs a blocking GPU read. The editor's **Freeze & capture
GPU state** button sets Freeze, cancels queued visual-time steps,
then requests the GPU capture at the next safe pre-render phase.
It tracks `Queued / InFlight / Ready / Failed`; a pending or stale
capture does **not** enable binary export. Only once a complete
snapshot of the currently retained particle revision arrives can
the editor save the checkpoint diagnostic. If an earlier scheduled
capture is already in flight, the editor waits for it rather than
enqueueing an overlapping GPU readback.

### Checked Metal input staging with safe retry

The Metal compute emitter now uses
`gfx::Device::updateStorageBufferChecked()` for its per-frame
VM diagnostic reset, per-slot VM parameters, spawn states,
spawn-slot IDs, spawn VM parameters and CPU-authored live-slot
gather map. A rejected checked upload or compute-encoder
submission **retains** pending births and slot ownership,
sets `gpuStagingDirty_`, invalidates previously captured
checkpoint data/revision and suppresses stale GPU drawing.
The next accepted `ComputeParticles::step` retries the
complete set of input transfers; only after a successful compute
submission are dirty upload flags and CPU birth queues consumed.

This is an intentionally conservative strategy against partially
accepted Metal blits. Any already accepted graphics commands
remain ordered in the device's command buffer; it is not an atomic
rollback of earlier GPU DMA operations, and hardware completion
still requires the RHI's Metal command-buffer health/fence contract.
`GpuCheckpointStatus::stagingRecoveryPending`,
`ComputeStats::gpuStagingUploadFailures` and
`PoolCheckpointStats::stagingRecoveryGroups /
gpuStagingUploadFailures` expose the state in the editor. No
gameplay ECS data is affected.

### Diagnostic checkpoint format V2 (supersedes Stage 39 V1)

`engine/fx/SpectraCheckpointExport.h` now writes an explicit
**48-byte V2 header** containing a CRC32 over the payload. The
bounded raw-body size remains 512 KiB of GPU particle/VM state
plus 16 KiB of CPU lifespan mirrors at 4096 slots. The
`validateGpuCheckpointDiagnostic()` function checks header
magic, version, exact byte count, slot count and CRC32 without
touching graphics hardware. The editor uses this validator before
writing a `.spfx` file.

The earlier **40-byte V1** export is historical and superseded
by V2; `validateGpuCheckpointDiagnostic` intentionally validates
only the current V2 format. Neither V1 nor V2 is a portable
savegame, stable across arbitrary compiler ABIs, replayable GPU
history or authoritative ECS serialization. The binary can be
inspected for integrity but does not provide a GPU state import.

### Recovery retry and load-shedding hysteresis

The native `GpuPoolRecoveryHost` retains its CPU-only
`PoolRecoveryManifest` if importing effects on a newly created
graphics Device fails. The host can supply a different healthy
replacement Device and retry without requiring the original GPU
pool to remain alive. Failed imports remain visibly in
`DeviceRecreationRequired` state; live GPU particles and
renderer-owned source handles are never resurrected.

The optional `PoolLoadSheddingPolicy::recoverBelowPercent`
(default **85**, range 1..100) introduces hysteresis for
estimated particle-count pressure and Stage 32 readback
bytes-in-flight pressure. Once automatic bursts are throttled,
they stay throttled until the corresponding estimate reaches
the lower recovery threshold. This avoids frame-to-frame
on/off oscillation near a soft limit. Updating the policy
between frames clears both hysteresis latches. Metrics include
`particlePressureLatched`, `readbackPressureLatched` and
`pressureHeldFrames`. The policy still does **not** apply to
manual one-shot VFX, never queues skipped bursts, and is disabled
by default.

**Actual Stage 40 verification:** GitHub source edits, source
re-fetch and manual callsite/contract inspection only.
**NOT RUN:** native C++/Objective-C++ compilation, Metal shader
compilation, CTest, editor startup/screenshots, checkpoint
CRC corruption/fault tests, real GPU readback, device-loss
recovery and load-pressure benchmarks. New features are
source-implemented but **not production-verified**.

## Stage 41 — offline V2 checkpoint inspection and safe native capture requests

### V2 checkpoint inspector: no graphics Device required

The new pure header `engine/fx/SpectraCheckpointInspector.h`
provides `inspectGpuCheckpointDiagnostic(bytes)` and
`gpuDiagnosticErrorString(error)`. It validates one bounded
same-build `.spfx` V2 diagnostic capture without accessing any
Metal resources, rendering code, ECS state, or GPU readback API.

The inspector distinguishes invalid magic, unsupported V1/other
versions, truncated headers/payloads, invalid header/physical slot
counts, mismatched declared sizes, unexpected trailing bytes and
CRC32 mismatch. Only after the complete bounded structure and
checksum pass does it read individual 128-byte particle records and
4-byte CPU lifespan mirrors. A structurally valid file can still
carry semantic warnings: NaN/nonfinite particle values, invalid
ages, inconsistent CPU remaining-life mirrors or unusual
lifetimes. It reports estimated live/dead physical slots and
bounding coordinates for finite live positions. A structural
success does NOT certify numerical CPU/Metal parity or a
correct gameplay state.

The reusable offline CLI `tools/spectrafx/inspect_spfx.cpp`
requires only a native C++20 compiler and the repository
headers:

```sh
c++ -std=c++20 -O2 -I. tools/spectrafx/inspect_spfx.cpp -o inspect_spfx
./inspect_spfx path/to/checkpoint.spfx
./inspect_spfx --json path/to/checkpoint.spfx
```

The CLI checks disk file size **before** allocating and reading,
caps memory at `kGpuCheckpointExportMaxBytes` (540,720
bytes at the current 4096-slot maximum), detects files that
change while being read and emits plain or JSON diagnostics.
It returns exit 0 for a structurally valid V2 file (even if it
contains semantic warnings), exit 1 for invalid V2
format/integrity, and exit 2 for invalid invocation or I/O.
Full usage and the same-build limitations are documented in
`tools/spectrafx/README.md`.

The V2 checksum covers the checkpoint **payload**. The header
and packet layout still depend on the native build's ABI and
endianness; V1 conversion, arbitrary-version migrations,
serialization portability, GPU buffer import and reverse
GPU rewind are intentionally unsupported. Diagnostics may
include world positions and graph parameters and should be
treated as project data.

### Native owner-thread capture and callback lifetime

A new public `<aiverselib/mechanics/SpectraFX.h>` integration
`spectra::GpuPoolCaptureHost` (implemented in
`libraries/include/aiverselib/mechanics/SpectraGpuCaptureHost.h`)
provides a bounded **one-at-a-time** request API:

```cpp
GpuPoolCaptureHost captures(pool);
// Between frames: one requested GPU group with an explicit byte cap.
captures.request(0);
// Each frame, AFTER Device.beginFrame delivers earlier readbacks and
// BEFORE any new pool.step(dt) can invalidate the captured revision:
auto status=captures.pollBeforeStep();
// Between pool.step(0) and the scene render pass, on the same owner thread:
if(captures.status().phase==GpuCapturePhase::Queued)
    captures.submitAfterStepBeforePass();
// Once Ready, bytes are already copied and validated (no new GPU read).
std::vector<uint8_t> bytes;
if(captures.takeReady(bytes)) { /* offline diagnosis only */ }
// Always cancel before pool rollback, shutdown or graphics-Device recreation.
captures.cancel();
```

The controller maintains an exact `GpuPoolWorld::lifecycleEpoch()`
and request ID, discards results from a recycled/recreated pool,
retains at most one bounded byte vector, and permits up to 600
owner-thread graphics-frame polls before returning a timeout.
Asynchronous RHI callbacks hold only weak references to
the request's shared completion state; they do not capture
a dangling host object or force a second graphics owner.
Cancellation invalidates publication but cannot forcibly
cancel already submitted Metal work; Stage 32 shared
checkpoint budget and RHI callback settlement still apply.
The status distinguishes Idle, Queued, InFlight, Ready and
Failed, with a concrete error when a complete GPU readback
has become stale before export.

The sample `examples/spectrafx/native_ecs_attachments.h`
now embeds the capture controller into the single native
render-frame lifecycle. It freezes VFX progression and defers
read-only ECS source/pose synchronization while the capture is
Queued/InFlight/Ready. Each frame continues drawing the current
particles via `pool.step(0)`; the host takes or cancels
the result before normal presentation resumes. On rollback or
device loss, the capture is cancelled **before** the Stage
38/39 recovery workflow, with no lost/old GPU particle state
published as authoritative ECS data. Recovering a valid GPU
state still requires the host's existing explicit new-Device
reconstruction; captured V2 bytes are not imported.

### Recovery hardening

`GpuPoolWorld::boundToDevice(device)` allows the recovery
controller to explicitly reject an attempted graphics
Device replacement that supplies the SAME old Device object
even if it temporarily reports healthy. A true recovery
requires a different healthy gfx::Device, disposal of old GPU
resources while the old Device still exists, import of the
CPU-only effect manifest and fresh generation-safe ECS
source reattachment. No callback, offline parser or
checkpoint diagnostic may bypass this protocol.

**Verification performed:** source inspection, SHA-guarded
GitHub source writes and file existence/readback only.
**NOT RUN:** native C++/ObjC++/MSL builds, CTest, execution
of the offline inspector CLI, actual corrupted-payload
parsing, Metal readback completion/cancellation, editor
launch, host recovery/fence tests, CPU/GPU parity or
performance measurements. Stage 41 is source-implemented,
not production-verified.

## Stage 42 — privacy-redacted diagnostics, per-group capture and explicit recovery requests

### Privacy-safe diagnostics are the default

Stage 41's read-only inspector exposed world bounds and GPU frame
identifiers in its JSON/console output. Stage 42 introduces
`engine/fx/SpectraDiagnosticReport.h` and
`GpuDiagnosticReportPrivacy`. The new
`gpuDiagnosticReportJson(inspection,privacy)` returns a **fixed
aggregate-only schema**, without original file paths, per-particle
positions, VM scalar values or raw GPU binary data. By default it
also redacts scene-coordinate bounds, GPU serial/revision and
CRC-based content fingerprints. Each sensitive category requires
its own intentional opt-in:

```cpp
auto inspected=inspectGpuCheckpointDiagnostic(checkpointBytes);
GpuDiagnosticReportPrivacy privacy{}; // all sensitive categories OFF
std::string redacted=gpuDiagnosticReportJson(inspected,privacy);

// Only when a developer specifically requires bounds:
privacy.includeWorldBounds=true;
// Similarly, frame serial/revision/CRC fingerprints are opt-in:
privacy.includeGpuIdentifiers=true;
```

`tools/spectrafx/inspect_spfx.cpp` now defaults to redacted text
and `--json`. The explicit `--report <safe.json>` argument
creates one local JSON report, never auto-uploads or automatically
includes the raw checkpoint. The optional
`--include-coordinates` and `--include-identifiers` CLI flags
enable only their respective aggregate fields. No CLI option ever
prints the original per-particle VM parameter arrays.

The editor adds **Save safe JSON report** and an intentionally
separate **Save RAW GPU checkpoint (sensitive)**. A safe report
contains aggregate inspection counters and at most 180
privacy-filtered GPU preview samples (sample ordinal, estimated
particles, step result, fresh/stale/pending counts and pending
kill count). It excludes exact RHI frame serials, authored graph
names, filenames and per-particle positions and VM inputs.
Checkboxes permit optional world-coordinate BOUNDS and GPU
identifiers, but never raw state inside a safe report. Saving
is a direct, local user action under
`platform::userDataDir()/spectrafx-diagnostics`; the system
does NOT upload diagnostics or create recurring jobs.

### Group selection across a pooled GPU world

`GpuPoolWorld::gpuGroupCount()` and
`gpuGroupSummary(index,out)` expose a read-only
`PoolGpuGroupSummary`: active state, estimated particle count,
GPU buffer bytes, pending/fresh checkpoint revision status,
most recent step/reset acceptance and whether a **complete,
current** checkpoint can be exported. These APIs do **not**
expose serialized effect graph keys or the source effect code.
An invalid group index leaves the output unmodified.

The editor displays a bounded index selector for up to the
existing 64 GPU-group maximum and scopes both binary
`saveFreshSpectraGpuCheckpoint(out,index)` and
`saveSafeSpectraGpuReport(out,index,...)` to the chosen group.
The old one-argument binary-save API still auto-selects the first
eligible group for compatibility. Only the editor-owned Metal
preview group 0 can be captured directly by the editor; a
separately attached external renderer pool remains **non-owning
telemetry** and must run its own GPU capture between safe RHI
passes. It is never driven out of render order by a diagnostics UI.

The native `GpuPoolCaptureHost::requestNext(policy)` selects
one currently active group via
`GpuCaptureTargetPolicy::{RoundRobin,LargestEstimatedGroup,
MissingOrStalestCheckpoint}`. RoundRobin rotates starting
group; the other modes use the previous CPU-estimated active
count or the checkpoint's missing/stale revision. Groups already
reading back are skipped. Exactly **one** capture remains
queued/in flight per native host and the shared Stage 32
bytes/chunk budget is still authoritative. Requests are owner
thread operations, with no background polling, extra GPU
contexts or direct synchronous buffer reads.

### Native reset and graphics recreation events

`GpuPoolRecoveryHost::requestPresentationReset(purpose)`
now explicitly schedules a visual reset **without** submitting
GPU commands. On the next safe graphics frame, outside active
encoders, the host calls `retryPresentationReset()`.
`PoolRecoveryPhase::ResetRequested` is distinct from
`ResetRetryRequired` (a failed/incomplete GPU-group reset)
and `DeviceRecreationRequired` (lost GPU or failed new-device
import).

`PoolRecoveryEvent` now includes a stable monotonic
`transitionSequence` that changes only when an observable
phase, progress, health or error detail changes. It also reports
host reset-request and Device-replacement attempt counters,
whether a reset was explicitly queued, and whether a CPU-only
recovery manifest is being retained for retry. No event
handler recreates a Device itself. A real platform host must
construct a distinct healthy `gfx::Device` and keep the old
Device alive until old pooled GPU resources have been released.

The updated `examples/spectrafx/native_ecs_attachments.h`
exposes both explicit per-group and automatic policy-based
capture, safe summary retrieval, and a queued reset/status
workflow. VFX capture bytes and GPU physical-slot identifiers
remain outside authoritative ECS, and lost GPU particles are
never falsely reconstructed.

**Verification:** Stage 42 source changes and GitHub content
readback only. **NOT RUN:** CMake/C++/ObjC++/MSL compilation,
CTest, offline redacted-report CLI, actual editor report export,
Metal multi-group capture, fault-injected device reset, UI
screenshots, CPU/GPU parity or performance benchmarks. These
are experimental source APIs pending executable validation.

## Stage 43 — explicit local safe ZIP bundles, native capture mailbox and bounded GPU recovery retries

### Safe local ZIP support package (no raw GPU state)

A new header `engine/fx/SpectraDiagnosticBundle.h` provides
`makeSafeGpuDiagnosticZip(inspection,frames,output)`. This creates
one bounded, deterministic **ZIP STORE** archive containing
only three fixed filename entries:

```text
manifest.json
checkpoint_summary.json
preview_timeline.json
```

No zlib or other compression library is required: each ZIP member
contains strictly generated JSON and a CRC32. Only a structurally
valid V2 inspection and up to **180** renderer-side aggregate
`GpuSafeTimelineFrame` values are accepted. The total ZIP size
is capped at **128 KiB**, and invalid/oversized requests return
false without modifying the caller's output.

The manifest declares the privacy restrictions explicitly:
`redacted=true`, `containsRawCheckpoint=false`,
`containsWorldCoordinates=false`,
`containsVmScalars=false`,
`containsGpuIdentifiers=false`, and
`uploadedAutomatically=false`. The ZIP builder deliberately
has **no input argument for raw SPFX bytes, authoring graph
paths or arbitrary ZIP entry names**. Calling the builder
cannot package a raw GPU snapshot accidentally. Even when
a developer enables world bounds or GPU identifiers for a
separate Stage 42 JSON report, the ZIP remains fully
redacted. This is a local support artifact, not an ECS save,
GPU replay or cross-build SPFX importer.

The editor's `Save SAFE ZIP bundle` action creates one
`.zip` under `platform::userDataDir()/spectrafx-diagnostics/`
only after the user presses the button and an eligible
current-revision V2 checkpoint is ready. It uses a local
`safe-bundle-group-N-M.zip` filename, **not** a GPU frame
serial, and refuses to overwrite an existing safe bundle.
The related safe JSON report also uses serial-free
filename numbering. Only when the report belongs to the
editor's **own** GPU preview are its last 180 sanitized
sample counters included; external GPU pools do not
inherit unrelated editor preview history.

The offline `tools/spectrafx/inspect_spfx.cpp` utility now
also supports:

```sh
./inspect_spfx --bundle /path/to/support.safe.zip /path/to/checkpoint.spfx
```

The offline ZIP has an empty preview timeline. The binary
SPFX source is **never** added as an archive entry. There is
no implicit upload, email, issue creation or external
sharing. The existing `Save RAW GPU checkpoint
(sensitive)` button is deliberately a separate user action.

### Non-owning native capture mailbox

The public `<aiverselib/mechanics/SpectraFX.h>` includes
`GpuCaptureRequestMailbox` from
`libraries/include/aiverselib/mechanics/SpectraGpuCaptureMailbox.h`.
The mailbox is a same-owner-thread intent/status holder
with states
`Idle / Requested / Claimed / InFlight / Ready / Failed /
ReleaseRequested`. It stores one requested group index,
a monotonically increasing sequence and bounded failure
information; it performs **no GPU commands** and carries
no particle data, graphics handles, ECS mutation or
background scheduling.

The native render host may expose a non-owning mailbox
pointer to the editor through
`Editor::setSpectraGpuCaptureMailbox(mailbox)`.
The UI can call
`Editor::queueSpectraExternalGpuCapture(groupIndex)`
or `Editor::releaseSpectraExternalGpuCapture()` on
that mailbox, but cannot access `gfx::Device` or run
`readBufferAsync` itself. The native presentation owner
claims a request in its next safe pre-render phase,
uses the existing one-at-a-time `GpuPoolCaptureHost`,
keeps VFX state frozen while an async checkpoint is
pending, and publishes the same request's state back
to the UI. Release is consumed by the native host on
the next presentation frame, not during ImGui drawing.

`examples/spectrafx/native_ecs_attachments.h` now
demonstrates this integration and exposes
`captureMailbox()`, `poolTelemetry()` and
`recoveryHost()`. The application must detach all
non-owning pointers from the editor **before** destroying
the native host/graphics Device. If the pool is reset,
recovered or shut down, the native host clears the
mailbox and cancels the capture; late Metal callbacks
cannot reattach to an obsolete source generation.

### Recovery status and bounded retries

`GpuPoolRecoveryHost::serviceQueuedResetAtSafeFrame(ownerFrameOrdinal)`
adds an **optional owner-thread** reset retry pump. The host
calls it only after `gfx::Device::beginFrame`, outside
render/compute encoders. On a recoverable failed reset,
the next attempt is spaced by a bounded exponential
number of host frames (max **32**); this avoids busy
reset writes every frame. The returned
`PoolRecoveryServiceResult` distinguishes NoWork,
Deferred, ResetSucceeded, ResetFailed and NeedsNewDevice.
A lost Device must be rebuilt by the native platform
host; it is not retried as an ordinary reset.

The existing `PoolRecoveryEvent` adds
`consecutiveResetFailures` and
`retryNotBeforeOwnerFrame`, making retry/cooldown
status visible without timers. Its transition counter
changes on observable recovery state changes rather
than spinning duplicate callbacks during the same
recovery service call. The updated native example
invokes this bounded service in its safe frame before
beginning normal presentation and cancels any pending
capture before GPU rollback. An optional non-owning
`Editor::setSpectraGpuRecoveryHost()` attachment
lets the editor show this status and enqueue a
VFX-only reset intent. The editor **never**
executes the actual GPU reset.

### Privacy and execution limitations

The editor resets both sensitive report opt-ins
when the selected GPU group **or GPU telemetry pool**
changes, preventing consent from carrying over to
a different renderer. A safe ZIP never includes
raw particle state or exact GPU device serials.
Raw SPFX still contains scene positions/VM values
and remains a deliberately sensitive export.

**Verification performed:** SHA-guarded GitHub source
commits and source readbacks. **NOT RUN:** native
C++/ObjC++/MSL compilation, ZIP validity extraction,
CLI output/privacy/corruption tests, CTest, editor
UI launch/screenshots, real Metal capture and owner
thread callback/device-loss tests. This stage is
source-implemented, not production-verified.

## Stage 44 — executable offline diagnostic verification and CI regression gates

Stage 44 validates the existing V2 diagnostics, bounded ZIP support
bundle, redacted reporting and standalone CLI using **actual native
C++20 execution**, rather than GitHub source commits alone.

### New regression and build integration

- `tests/vfx_spfx_diagnostics_test.cpp` exercises valid V2
  checkpoint encoding/inspection, corrupted CRC, unsupported
  version, truncated/trailing bytes, invalid magic and physical
  slot count. It confirms sensitive GPU identifiers and world
  bounds are excluded from redacted JSON by default, that
  explicit opt-ins work independently, and that the safe ZIP
  contains only three fixed JSON members with correct STORE,
  central directory and CRC32. Invalid/oversized builder
  requests must leave existing output unchanged.
- `CMakeLists.txt` now registers the standalone test as
  `aiverse_vfx_spfx_diagnostics_test`, with a
  `vfx_spfx_diagnostics` CTest labeled `SpectraFX`.
- `.github/workflows/spectrafx-diagnostics.yml` is a
  standalone Linux/macOS C++20 validation pipeline
  triggered by relevant source/PR changes. It compiles the
  CLI and regression under `-Wall -Wextra -Werror`,
  generates explicit fixture files and uses Python's
  `zipfile` to check actual ZIP extraction, CRC and
  redaction. It also verifies a corrupted SPFX binary
  fails and explicitly enabled sensitive fields become
  visible only when requested.
- The existing `.github/workflows/spectrafx-validation.yml`
  includes the new executable in its focused CMake build,
  so the labeled CTest is not accidentally registered
  without being built.

### Actual Stage 44 execution evidence

Source files were fetched from the active GitHub feature
branch, copied into an isolated local Linux test workspace,
and **git hash-object** of each downloaded copy was
compared with its original GitHub blob SHA. All matched:

```text
engine/fx/SpectraCheckpointExport.h      319b2aedde9265af199aaaefa696eba20d49afb2
engine/fx/SpectraCheckpointInspector.h   79de640771f0c96bb6f070551e00db3c49675d4e
engine/fx/SpectraDiagnosticReport.h      2c4be4c9f993fdad2e67dddc46125cfda95b15a1
engine/fx/SpectraDiagnosticBundle.h      747489406e60249da5e3a4d62d8e9d5bdcd5f808
tools/spectrafx/inspect_spfx.cpp         69a2b3ca2f474503e0d168fda13a61f706a7cd14
tests/vfx_spfx_diagnostics_test.cpp      94884afbc46b7cd41b2aff12f5b8d1cc07356d94
```

Only the small `render::float4` and checkpoint-data ABI
declarations were mirrored locally to isolate the
GPU-independent code without cloning the entire AIVERSE
engine; the actual six test/compiler source files were
**byte-for-byte identical to GitHub**. Therefore these
results verify the portable diagnostics subset, **not**
the full engine or Objective-C++/Metal integration.

Tests actually run on Linux:
- GCC 14.2 C++20 `-Wall -Wextra -Werror -O2`
  compilation and execution of checkpoint exporter,
  inspector, redacted report, safe ZIP writer,
  the full `vfx_spfx_diagnostics_test.cpp` and the
  offline `inspect_spfx` CLI — **PASS**.
- Python standard-library `zipfile` successfully opened
  and CRC-checked the output of the compiled C++ ZIP
  writer; all three fixed entries contained valid JSON,
  with no raw VM scalar/world bounds or RHI identifiers
  under default redaction — **PASS**.
- End-to-end CLI `--json --report --bundle` with
  a generated valid fixture, explicit sensitive opt-ins,
  corruption exit code 1 and missing-file exit code 2
  — **PASS**.
- Isolated CMake/CTest
  `ctest --test-dir build --output-on-failure -L SpectraFX`
  — **1/1 PASS**.
- GCC AddressSanitizer and UndefinedBehaviorSanitizer
  builds of the standalone regression and offline CLI,
  with ZIP extraction afterwards — **PASS**, no
  sanitizer diagnostics observed.

### Still NOT verified

- Full AIVERSE CMake configuration or linking native editor,
  client and the full SpectraFX renderer.
- Apple Clang/Objective-C++/Metal shader compilation,
  actual Metal device compute/draw, GPU recovery,
  asynchronous callback cancellation, ECS renderer
  attachment integration or CPU/GPU visual parity.
- GitHub Actions cross-platform workflow **success**:
  latest workflow instances were **pending or queued**
  when checked, not completed/passing.

These distinctions are mandatory: an isolated Linux
diagnostics PASS is neither a successful full CTest suite
nor evidence that Apple GPU compute already works.


## Inspector graph-v2 integration with current main (2026-11-20)

The graph-v2 authoring/compiler, expression VM, shared `spectra::BatchWorld`
and optional `spectra::GpuPoolWorld` from the Inspector branch coexist with
main's `fx::SpectraFxSceneHost`. They are alternative presentation owners:
a native host must submit an effect through exactly one owner and advance
that owner once per presentation frame. This integration does not switch
existing FPS/RTS hosts or enable experimental GPU/ICB flags.

All four graph runtime view entry points (`BatchWorld`, `GpuPoolWorld`,
`ComputeParticles`, `GraphRuntime`) accept the existing `fx::SpectraFxView`.
Camera-shaped source calls remain compatible through template adapters.
`Device::updateStorageBufferChecked` rejects unsupported backends and unsafe
encoder states; Metal records the upload in the current frame.
`readBufferRangeAsync` returns bounded offset bytes on the owner thread after
GPU completion. The established main GPU completion timeline remains canonical.

Editor GPU-preview replacement, failed candidate initialization and Stop retain
old pool owners until `gpuCompletedFence` covers their `gpuSubmissionFence`.
Unknown completion retains resources until explicit shutdown proves idle;
a bounded eight-owner backlog postpones further replacement requests.
Scene/Game rendering in the same device frame advances preview births/time once.
SpectraFX draws remain before PRISMA resolve and retain main's reactive coverage,
volumes and viewport timing. Public graph-v1 assets remain supported.

Native integration verification is recorded in
`docs/agent-handoffs/inspector-spectrafx-main-2026-11-20.md`.
