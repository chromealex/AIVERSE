# AIVERSE SpectraFX

**SpectraFX** is the official name of the universal, genre-independent visual-effects system in AIVERSE. Its design is inspired by the authoring and event-driven workflow of Unreal Niagara, but it is **not** a copy of Unreal code, asset formats, or editor. `aiverse::spectra` is the public C++ API namespace.

> **Current scope (2026-11-18):** Graph v1 frontend, reusable CPU particle backend, graph-asset variants/hot reload and optional read-only ECS presentation adapter have been **committed**. A basic graph-v1 node-canvas editor and in-memory CPU preview are **committed but unverified**; a limited Metal GPU particle prototype is **committed but unverified**; full GPU/CPU integration, Niagara dataflow and automatic RTS/FPS production integration are **not finished**. Tests were added but have **not yet been run** in the authoring environment. Refer to `docs/agent-handoffs/universal-niagara-vfx-2026-11-18.md` for exact SHA and verification status.

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
- `libraries/include/aiverselib/genre/SpectraFX.h`: `aiverse::spectra::SourceComponent`, `EventBinding`, `PresentationSystem`.
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
#include <aiverselib/genre/SpectraFX.h>
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
headless CPU-fallback emissions and lifetime; **this is not a Metal GPU
integration test**. CI and native GPU validation are still required.

**Known limitations:** no GPU compaction, no camera culling/LOD for
per-particle draws, no GPU-particle soft intersection, no mesh/ribbon
GPU rendering, no advanced emitter graphs, no cross-CPU/GPU effect
scheduler, no automatic FPS/RTS runtime migration, and no GPU
performance measurements. Runtime Metal shader compilation, GPU
resource hazards, multi-frame correctness and framebuffer output
remain **NOT VERIFIED**.

## Validation checklist

```bash
cmake -S . -B build/vfx-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/vfx-test --target aiverse_vfx_graph_tests aiverse_vfx_ecs_tests aiverse_vfx_assets_tests aiverse_vfx_ops_tests aiverse_vfx_gpu_tests -j4
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
