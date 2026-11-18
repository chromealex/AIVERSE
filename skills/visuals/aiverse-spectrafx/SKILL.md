---
name: aiverse-spectrafx
description: Author and integrate AIVERSE SpectraFX visual effects: versioned graphs, asset variants, ECS source components, event bindings, CPU fallback, editor previews and eventual GPU backends.
---

# SpectraFX — universal AIVERSE visual effects

**Canonical documentation:** `docs/spectrafx.md`.  
**Exact stage history:** `docs/agent-handoffs/universal-niagara-vfx-2026-11-18.md`.  
**Engineering rules:** `AGENTS.md`.

## Mandatory scope boundary

This skill is ONLY for world-space VFX emitters, particles, ribbon/mesh FX, effect graphs and ECS event presentation. For post-process bloom, tone mapping/HDR resolve, LUT/color grading, depth-of-field, motion blur, screen-space AO/GI/SSR, TAA/upscaling, post-process volumes, or the `postfx` Shader Graph domain, use **PRISMA** (`engine/render/Prisma.h`, `docs/prisma.md`). SpectraFX contributes linear HDR scene particles before PRISMA resolves the image; PRISMA never owns particle simulation. See the mandatory ownership section in `AGENTS.md`. Existing `postfx_*` MCP/JSON names are legacy external interfaces, **not** SpectraFX operations.

## Fast path (current supported capabilities)

1. Create `fx/graphs/<name>.vfx.json` with `version:1`, `nodes`, `links`, `outputs`, and optional `parameters`. Supported chain: `Spawn → Initialize → Update* → SpriteOutput|MeshOutput|RibbonOutput`. Node `properties` reuse `engine/fx/FxEffect.h` emitter fields. Example: `docs/spectrafx.md`.
2. In native C++ include `engine/fx/SpectraFX.h` and call `aiverse::spectra::GraphAssets(particles, reader).load(name, overrides)`. Play the returned effect ID with the existing CPU particle runtime; check the `errors` and `fallback` fields. Parameter overrides create independent variants.
3. For continuous ECS-attached effects include `<aiverselib/genre/SpectraFX.h>`. Add `spectra::SourceComponent` to any Entity (unit, actor, spell, vehicle, world prop). The renderer-side `spectra::PresentationSystem::sync` reads it and invokes the native project's render-pose resolver.
4. One-shot impact, fire, ability and death effects use `PresentationSystem::consumeTickEvents(tick, events, bindings)`, exactly once per committed tick. Call `reset()` when the client rolls back or starts a new session.
5. Preserve `aiverse_fx`, `fx/<name>.json`, `fx/fx.json`, legacy `RtsFx` and older `Vfx*` aliases until production parity tests pass.

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
projects include `<aiverselib/genre/SpectraFX.h>` for adapter helpers
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

## Boundaries and tests

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
