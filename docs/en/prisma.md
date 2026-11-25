# PRISMA — universal AIVERSE post-processing

PRISMA is the name of the full-screen effects and image-processing system. Not to be confused with **SpectraFX**, the particle/VFX system. Existing game formats do not change.

## Ownership boundaries: PRISMA and SpectraFX

PRISMA owns **post-processing of the final image**: HDR/tonemapping, Bloom, color grading/LUT, DoF, Motion Blur, FXAA/TAA/upscaling, screen-space GI/AO/SSR, camera volumes, screen-space Shader Graph (domain `postfx`), render-pass dependencies and performance budgets.

SpectraFX owns **world visual effects before post-processing**: emitters/particles, smoke, fire, explosions, ribbons, trail streams, CPU/GPU particle simulation, VFX graph authoring and ECS events. SpectraFX produces the scene's HDR contribution; PRISMA may then apply Bloom to the whole scene. The systems do not duplicate each other, do not depend on genre and are not part of deterministic gameplay state.

The canonical sources are now `engine/render/Prisma.{h,cpp}`, `PrismaData.{h,cpp}`, `PrismaShaders.h`, `PrismaVolumes.h`. Editor: `apps/editor/core/PrismaOps.{h,cpp}` and `apps/editor/windows/PrismaWindow.cpp`. Legacy PostFx headers forward to PRISMA; the legacy .cpp files contain no implementation. The stable JSON `postfx`, MCP `postfx_*` and editor saved id `postfx` must **not be renamed**: they are external formats, not code owners.

## Current design
- Main C++ API: `#include "engine/render/Prisma.h"`; class `aiverse::render::Prisma`.
- Compatibility: `PostFx` and `PostFxData` are source alias/forwarding includes; the main compilation goes through `Prisma.cpp` and `PrismaData.cpp`. The old `postfx_*` MCP names are kept.
- `Renderer3D` requests `PostProcess::scenePass` and calls `resolve()`; with HDR/High, `ScreenLighting` performs screen-space lighting and TAA before the final resolve.
- Parameters: `game.json` / map, `postfx` merged by effect type; the editor panel and MCP operations are already implemented.
- GPU/RHI: GLSL/MSL, HDR RGBA16F with ACES and sRGB, RGBA8 fallback; the current chain has a fixed order. UI is drawn **after** PRISMA.

## PrismaRenderGraph — stage 07 (GPU-free planner)

`engine/render/PrismaRenderGraph.h` is the first RHI-independent implementation of a **description of passes and resources**. `PrismaRenderGraph::addResource/addPass/compile` build a stable topological order and check for one writer per resource, identifier errors, feedback read/write within a single pass and dependency cycles. `PrismaGraphPlan` contains `order`, `resources` (first/last use), `slots` and the number of bytes of allocated **internal** textures. Transient resources compatible in format and size with non-overlapping lifetimes share one slot; history and resources with `allowAlias=false` do not share a slot. External input/output/history are supplied by the caller and are not allocated by the planner.

**Important:** for now this is only a dependency graph + lifetime planner, not a GPU executor. The real `Prisma::resolve()` still executes the current fixed pipeline. Do not claim that the RenderGraph already drives rendering; the next stage is an RHI resource pool and execution callbacks with real pass bindings. Incorrect dependencies are deliberately rejected rather than fixed by an implicit shader blit.

Test code: `tests/render/prisma_graph.cpp`, CTest `prisma_graph`; with `AIVERSE_BUILD_TESTS=ON` it is included in the standard CMake ALL build and `./run-tests.sh`. A native test run is still required.

## Target interfaces
1. GPU-free `PrismaProfile`, `PrismaVolume`, `PrismaVolumeStack` and parameter resolution by camera position.
2. A declarative `PrismaGraph` with typed resources (SceneColor, Depth, Velocity, History, LUT, Output), topological order and lifetimes, without ties to FPS/RTS.
3. DepthOfField, MotionBlur, AutoExposure, GTAO and LensDistortion effects as separate passes with quality-tier fallback.
4. MotionVectors / reactive masks and temporal upscaling; a separate history invalidation policy on camera cut/LOD/resize.
5. GPU pass profiler, checks for resource-hazard dependencies, a node editor and full automated RHI verification.

## For agents and integrations

Official skill: `skills/visuals/aiverse-prisma/SKILL.md`, registered in `tools/skills/catalog.py` and `skills/catalog.json`. The old `postfx_*` commands belong to PRISMA as a compatible external API. The generic `ShaderGraph` compiler is shared infrastructure, but the full-screen `postfx` domain and its shaders belong to PRISMA. The old sources `PostFx*.cpp` and `ScreenLighting.cpp` are empty compatibility translation units; do not make changes in them. Follow the boundaries in `AGENTS.md` and write a handoff after every stage.

## Camera Post Process Volumes — stage 08

`PrismaVolumeStack` is now used in the game client and in the editor's Scene/Game viewports. At map start the client reads `game.json["prismaVolumes"]` and `map["prismaVolumes"]` via `parsePrismaVolumes`; before `Renderer3D::render(...,&prisma)` it calls `prisma.applyCameraVolumes(stack,camera.eye())`, and after `resolvePrisma()` it resets the settings to the base profile. The editor caches the profile by JSON signature and resolves it separately for each camera; the `postfx` change is not mutated.

Example `game.json` (the same top-level block can be given on a map):

```json
"prismaVolumes": {
  "volumes": [
    {
      "id": "underwater",
      "shape": "box",
      "center": [12, -5, 20],
      "halfExtents": [15, 6, 12],
      "blendDistance": 3,
      "priority": 10,
      "layerMask": 4294967295,
      "weight": 1,
      "profile": {
        "effects": [
          {"type": "colorGrade", "saturation": 0.5, "temperature": -0.3},
          {"type": "vignette", "intensity": 0.4}
        ]
      }
    }
  ]
}
```

Shapes: `global`, `sphere` (`center`, `radius`) and `box` (`center`, **axis-aligned** `halfExtents`). `weight` is in [0,1], `blendDistance` is the fade distance **beyond the outer boundary**; priorities are processed in ascending order and equal priorities keep insertion order. `layerMask` filters cameras, `enabled` disables the volume.

The profile inside a volume uses the same **existing** `postfx` schema, not a new shader library. Specifying `"hdr"` or `"enabled"` explicitly enables the corresponding override; absence of these keys keeps the base profile's value. On a map, a volume with the same `id` replaces the game one; `"replace":true` in the map block removes all game volumes before applying the map list. Invalid entries are skipped with `issues`; authored JSON is not changed.

**Limitations:** the body volume is not yet an oriented box (no rotation), there is no spatial index/priority GPU culling, the camera mask defaults to all bits; the regular GameClient/Editor apply mask all. Interpolation changes **only explicitly specified effect parameters** (stage 09); other parameters, minQuality and LUT paths keep their base values. For a native C++ profile without masks the legacy full-profile behavior is kept. Discrete settings switch at weight >= 0.5, numeric parameters blend in float on the presentation side. Do not store such a float stack in authoritative ECS state. The new `prismaVolumes` data is optional and does not affect old projects. Code: `PrismaVolumeData.{h,cpp}`, `PrismaVolumes.h`; CPU tests: `tests/render/prisma_volume.cpp` / CTest `prisma_volume` (a test run is still required).

## Stage 09 — per-parameter blending

`PrismaVolumeEffectMask` in `PrismaVolumes.h` and `PrismaVolumeData.cpp::authoredEffects` map each effect to a bitmask of the parameters explicitly specified in JSON, including color components, the `enabled/minQuality` flags and the LUT text path. When a volume sets only `colorGrade.exposure`, the source `colorGrade.saturation`, `contrast`, lift/gamma/gain and minQuality are **not overwritten**. When the base effect is absent, a neutral profile with a smooth entry is added for ColorGrade/Bloom/effects with `intensity` or `amount`; for discrete effects the threshold is 0.5. For direct native C++ creation of a volume without `effectMasks` the old full-profile behavior is kept. The `prisma_volume` tests are extended to cover preservation of unrelated parameters, but a native run is still required.

## Stage 10 — visual editing of Camera Volumes

The `PRISMA — Постобработка` panel (`apps/editor/windows/PrismaWindow.cpp`) now contains a **PRISMA Volumes** section in the current scope: the whole game (`game.json`) or the open map. It can create a global/sphere/box volume, center the volume on the Scene camera, and edit `id`, enabled, sizes, priority, `weight`, `blendDistance`, `layerMask` and nested effects. Next to each effect parameter there is an **inherit/override** toggle: clearing an override removes the corresponding JSON key rather than writing the default. This matches `PrismaVolumeEffectMask` and prevents resetting the global contrast/saturation.

Authoring uses the shared GPU-free `makePrismaVolumeJson(id, shape, center)` from `PrismaVolumeData.h` — the same format as the runtime parser and the CPU test `prisma_volume`. Map changes go through `MapDoc::beginEdit/touched` (undo), game changes through saving the manifest, as in the existing PostFX sliders. The Scene camera shows the number of applicable effects/volumes and parser warnings.

**Limitations:** there are no volume gizmo outlines in the viewport and no Shader Graph payload editor inside a volume — the separate Shader Graph Editor remains for that. The UI uses the existing ImGui and editor layout. Neither compilation, nor GUI smoke/undo parity, nor GPU screenshots were verified at this stage; the agent must confirm them natively before merging. The old panel ID `postfx` and the MCP commands `postfx_*` are kept.


## Stage 11 — GPU-free typed RenderGraph dispatch

`engine/render/PrismaGraphDispatch.h` adds a header-only, genre-independent
`dispatchPrismaGraph<Handle>(graph, plan, bindings, callback)` contract.
`PrismaGraphBindings<Handle>` contains one entry per graph resource for
external handles and one entry per compiled transient slot for physical
allocations. `PrismaGraphDispatchView::resource(id)` resolves a logical
resource through the compiled aliasing plan; the host supplies the typed
RHI handles, command encoders and texture transitions.

Validation completes **before any callback**: invalid plans, mismatched
binding counts, unbound handles, duplicate pass IDs and invalid resource
references are rejected without partially drawing a frame. The callback
runs in deterministic topological order, and returning false aborts subsequent
passes with an error and exact number of completed passes.

This is **not an RHI executor** and does not allocate GPU textures, create
encoder barriers, perform render passes or bind PRISMA's current fixed chain.
An external integration must first create and retain physical slots, manage
external/history resource lifetimes, provide synchronization and connect the
existing `Prisma::resolve` calls. Keep the legacy renderer enabled by default
until native Metal/GL validation and image parity tests pass.

Tests are appended to `tests/render/prisma_graph.cpp` / CTest `prisma_graph`.
Native build, CTest and GPU tests have **NOT RUN** in the GitHub-only agent
environment; see `docs/handoffs/prisma-stage-11.md`.


## Stage 12 — Physical Resource Pool (GPU-agnostic)

`engine/render/PrismaGraphResourcePool.h` provides a host-owned pool of
physical render target handles for `PrismaGraphPlan::slots`. The host creates
targets with a `create(format,width,height)` callback and releases them
with `release(handle)`; this header never calls GPU/RHI APIs.

- `prepare(graph, plan, create, release)` reuses allocations with identical
  format/dimensions and matching history resource name, or creates new targets.
  It never selects the same allocation twice within a single frame. On
  allocation failure it releases targets allocated during that prepare call
  and preserves the previous pool entries.
- `markHistoryWritten(name)` marks successfully produced temporal pixels valid;
  `invalidateHistory()` discards temporal *contents* on camera cuts/teleports
  without discarding reusable GPU storage. If `prepare` returns a false
  history-valid bit, the caller must avoid reading the old history and use
  a non-temporal fallback. Resize changes resource descriptors and always
  allocates a fresh history.
- `pruneUnused(release)` frees allocations not selected by the last successful
  prepare; `clear(release)` releases all allocations. **Call both only after
  GPU fence completion**, not while earlier frames might still reference them.
- Use one pool instance per independent camera/view/temporal stream, not one
  global mutable history across cameras. The pool does not allocate external
  resource bindings, compute barriers, issue render passes or integrate itself
  into the existing `Prisma::resolve`. It is an opt-in host contract.

Tests for reusable physical slots, valid/invalid history, resize/pruning,
allocation rollback and clearing are in `tests/render/prisma_graph.cpp`.
Native build/CTest/GPU screenshot verification is **NOT RUN** for Stage 12.
See `docs/handoffs/prisma-stage-12.md`.


## Stage 13 — Opt-in RHI graph adapter (not yet wired into Prisma::resolve)

`engine/render/PrismaRhiExecutor.h` bridges the GPU-free Stage 11 graph and
Stage 12 allocation pool to the existing `gfx::Device` API. It creates
`TextureDesc{renderTarget=true}` targets, maps RGBA8/RGBA16F/Depth32F to
the backend's supported formats, uses `copyColor` for one-input/one-output
matching RGBA8 Copy passes, and executes Raster callbacks inside balanced
`beginPass/endPass` scopes. Compute callbacks run outside render passes
only if `Device::supportsCompute()` reports support.

A native host explicitly creates a per-camera `PrismaRhiExecutor(device)`
and calls `execute(graph,plan,external,raster,compute,optionalDepth)`
**outside any existing pass**. Raster callback signature is
`bool(gfx::Device&, PrismaGraphId, const PrismaGraphDispatchView<gfx::TextureHandle>&)`;
it binds its own pipeline, uniforms and input textures. Compute has the
same callback signature but is responsible for opening/closing any compute
encoder it needs. `optionalDepth` must match raster target dimensions if
the chosen Metal backend needs a depth attachment. The adapter never calls
`endFrame`, presents, performs implicit shader conversions, or modifies ECS.

**Compatibility constraints:** currently no RG16F/R32F target formats in
`gfx::PixelFormat`; these graphs fail preflight. The portable
`copyColor` interface only guarantees same-size RGBA8; HDR/other copy
operations need explicit raster shader passes. `gfx::Device` does not expose
a universal resource-barrier API. GPU target release and pool reset must
happen at a caller-managed safe point after in-flight work finishes. Temporal
history is valid only after the host has successfully produced its pixels
and explicitly marked it written.

**Not a complete render pipeline migration.** `Prisma::resolve()` still
uses the legacy, shipped fixed pass chain by default, with no automatic
RHI graph execution. The header is an opt-in integration point, and the
test addition currently covers only RHI format mappings in
`tests/render/prisma_graph.cpp`. Native build, driver smoke, screenshots
and numerical parity remain **NOT RUN**. See
`docs/handoffs/prisma-stage-13.md`.


## Stage 14 — experimental graph-driven SDR FXAA

The first production call site of `PrismaRhiExecutor` is an **opt-in**
SDR FXAA prepass in `Prisma::resolve()`. Set
`AIVERSE_PRISMA_GRAPH_FXAA=1` to create a two-external-resource graph
(`scene-fxaa` -> `fxaa-output`) and execute the existing FXAA pipeline,
same uniforms and texture bindings, through a single RHI raster pass.

The default code path is unchanged. The experimental path is only used
when FXAA is active, the scene is RGBA8/SDR, the FXAA pipeline is valid,
both color textures exist, and the matching depth attachment was created.
If the graph cannot be compiled/dispatched, the original direct `draw()`
path runs instead. Graph preflight happens before any draw submission;
the shader uses exactly one full-screen draw and writes the complete
destination. The opt-in pass clears neither color nor depth.

The graph has no internally allocated targets; it uses the existing
`Sized` per-view resources. It does **not** switch on graph-rendered
HDR, bloom, temporal AA, or the final composite. There is no new
JSON schema, editor toggle or change to SpectraFX. This is a guarded
runtime foothold, not a whole-chain render-graph migration.

GPU parity and native CMake/CTest remain **NOT RUN**. The test source in
`tests/render/prisma_graph.cpp` checks only graph structure and
single-pass binding/dispatch logic, not Metal/GL pixel correctness.
For reproduction and outstanding validation, read
`docs/handoffs/prisma-stage-14.md`.


## Stage 15 — Bloom Prefilter opt-in and legacy clear parity

`PrismaRhiExecutor::execute` now defaults raster passes to the same
opaque-black color clear and depth clear as `Prisma::draw`. An additional
`clearRasterTargets=false` argument is available for explicitly overwrite-only
pipelines, but the FXAA and Bloom experimental call sites use the default
legacy-compatible behavior. This fixes a known Stage 14 parity risk.

`AIVERSE_PRISMA_GRAPH_BLOOM_PREFILTER=1` opts only the **first Bloom
Prefilter** raster pass into `PrismaRhiExecutor`. It uses the existing
`kPrefilter` shader pipeline, uniform block and one source texture; the
input/output resources are externally managed `Sized` targets (full-resolution
input, lower-resolution output), and the graph does not allocate transient
textures. In HDR mode both resources are declared `RGBA16F`, otherwise
`RGBA8`. Both are supported by the existing conditional RHI path.

The rest of the Bloom downsample/upsample chain remains the original direct
renderer; the FXAA experiment still uses its independent
`AIVERSE_PRISMA_GRAPH_FXAA=1` flag. The default path is unchanged and any
preflight/allocation failure falls back to `Prisma::draw`. The experimental
raster callback returns true after a submitted draw; failure **after** GPU
submission cannot be rolled back. No full-frame parity has been established.

`tests/render/prisma_graph.cpp` now has CPU-only graph contracts for
SDR and HDR prefilter sizes, bindings, single-dispatch and missing output
preflight. These sources have **not** been compiled or run in this agent
environment; Metal/GL screenshots and GPU timings are also **NOT RUN**.
See `docs/handoffs/prisma-stage-15.md`.


## Stage 16 — opt-in full Bloom down/up RenderGraph

`AIVERSE_PRISMA_GRAPH_BLOOM_CHAIN=1` enables an experimental full Bloom
**Prefilter → Downsample → Upsample** graph inside `Prisma::resolve()`.
The flag is off by default. The graph declares one external Scene input and
external per-level down/up targets backed by the existing `Prisma::Sized`
per-view texture cache. Every downsample reads the previous mip; each upsample
reads the lower intermediate plus the matching down mip. All passes use the
existing `kPrefilter`, `kDown` and `kUp` GPU shader programs, exactly
the original size-dependent uniforms, tint/radius normalization, source
bindings and texture formats (SDR RGBA8; HDR RGBA16F). The host creates
the depth attachments for each output size before execution.

The RHI executor now accepts an optional trailing
`std::function<gfx::TextureHandle(PrismaGraphId)> rasterDepthForPass`.
When supplied, that attachment replaces the shared `rasterDepth` for each
pass; this is necessary on Metal for different Bloom mip dimensions.
Existing Stage 14/15 executor users continue to use the previous optional
shared depth and legacy-compatible clear defaults.

**Scope/limitations:** this is real RHI graph scheduling of Bloom raster
passes, but it is **not yet** adoption of Stage 12's transient graph pool:
the host supplies externally cached target textures, which already persist
per viewport. There is no automatic RHI in-flight retirement queue or
resource barrier API. The complete graph is rebuilt on each opt-in frame,
and `Prisma::resolve()` retains the old path as fallback when graph
preflight fails. On any unexpected failure **after** GPU pass submission,
the graph is not replayed (partial rendering must not be double-submitted).
No effect outside Bloom was migrated during this stage. If both
`AIVERSE_PRISMA_GRAPH_BLOOM_CHAIN` and the Stage 15 prefilter-only flag
are set, the full-chain flag takes precedence on success.

New GPU-free graph tests check 1/2/4 downsample levels for SDR/HDR,
topological order, valid binding resolution, and rejection of missing
mip targets before pass execution. Those sources have **NOT RUN** in a
native compiler or on a GPU; see `docs/handoffs/prisma-stage-16.md`.


## Stage 17 — per-view compiled Bloom topology cache

`engine/render/PrismaBloomGraph.h` provides a GPU-free
`PrismaBloomGraphCache`. `configure(sourceWidth,sourceHeight,format,mips)`
compiles the complete Bloom topology once per source size, HDR/SDR format
and mip shape list. Repeated frames with the same structure reuse
`graph()`, `plan()` and `operations()`; changed quality/mip sizes
trigger compilation of a replacement graph. Failed reconfiguration leaves
the previous valid configuration intact, but the renderer never executes
that stale graph for the incompatible current layout.

The main `Prisma::Sized` viewport cache now owns a
`PrismaBloomGraphCache`, so Scene/Game views have separate graph
layouts and no cross-view history. The Stage 16 opt-in path
`AIVERSE_PRISMA_GRAPH_BLOOM_CHAIN=1` uses the cached immutable graph
and rebuilds only the *frame bindings*: threshold/knee/scatter,
shader uniforms, source and output texture handles, and per-mip
depth attachments. The existing fallback and default-off behavior
are preserved. Stage 12 transient target pooling remains a separate
future integration; cached topology itself allocates **no** GPU textures.

This is implementation-only work; CMake, CTest and real GPU validation
were intentionally deferred at the user's request. See
`docs/handoffs/prisma-stage-17.md`.


## Stage 18 — GPU completion-gated retirement and persistent view executor

`engine/render/PrismaGpuRetirement.h` introduces
`PrismaGpuRetirementQueue<Handle>`: `retire(handle,submissionFence)`
transfers an allocation to a pending queue; `collect(completedGpuFence,
release)` invokes the release callback **only when the GPU has reported
completion** through a caller-managed fence/event. `drainAfterGpuIdle`
is valid only after an explicit GPU idle synchronization. A CPU frame
counter or `gfx::Device::frameSerial()` is not a completion fence.

`PrismaGraphResourcePool::retireUnused/retireAll` now transfer
allocations to the caller instead of destroying them immediately.
`PrismaRhiExecutor::retireUnused/retireAll/collectCompleted`
wires those operations to its queue and calls RHI `destroyTexture`
after the supplied completed fence. `resetAfterGpuIdle()` also
releases all pending and current allocations, but **only** if the
host has ensured the GPU is idle.

Every `Prisma::Sized` viewport now holds a lazy persistent
`std::unique_ptr<PrismaRhiExecutor>`, shared by its opt-in FXAA,
Bloom prefilter and full Bloom graph calls. It retains resource
pool state across frames rather than creating a new executor on
every pass. Lifetime is tied to the existing Sized cache. This
does not yet make PRISMA use graph-owned Bloom targets; those remain
external RHI textures in Sized, and graph execution stays opt-in.

**Backend integration constraint:** Gfx does not yet provide a
portable completed-GPU-fence API. Hosts must pass actual completion
signals to `collectCompleted`. Existing viewport eviction and
renderer shutdown still destroy their legacy textures at the
current existing release points; they are not automatically made
GPU-fence-safe by this opt-in queue. Actual backend fence wiring
and transient Bloom pool migration remain future features.
Testing/build/GPU verification deliberately deferred at user request.
See `docs/handoffs/prisma-stage-18.md`.


## Stage 19 — opt-in graph-owned Bloom mip allocation

With both `AIVERSE_PRISMA_GRAPH_BLOOM_CHAIN=1` and
`AIVERSE_PRISMA_GRAPH_BLOOM_POOL=1`, the complete Bloom RenderGraph
can now allocate its own downsample/upsample targets through
`PrismaGraphResourcePool` in the persistent per-viewport RHI executor.
Only the input Scene/FXAA color is external. The graph declares every
Bloom mip as an **internal** resource, enabling the planner to alias
identical-format/size GPU textures when their lifetimes do not overlap.
Without the new pool flag, the existing Stage 16 graph still uses
`Sized::targets` as external resources. Both experimental flags are
off by default.

`PrismaRhiExecution::resources` now maps each logical graph resource
to its actual external or pooled RHI texture handle. The final Bloom
composite samples `resources[cache.outputId()]` when pooled mode
succeeds. Per-pass depth attachments still come from the established
`Sized::depths` cache; thresholds, scatter, shader pipelines and
source UV conventions are unchanged. On graph preflight/allocation
failure before GPU submission, the code allocates legacy mip targets
and uses the existing Bloom fallback. A failure after GPU submission
does not attempt to replay partial passes.

**Ownership:** pooled RHI textures persist with their viewport executor,
rather than being released after each frame. Reconfiguration keeps
unselected allocations until explicit safe retirement; their eventual
release depends on GPU-completion reporting or a confirmed GPU-idle
shutdown. No portable completed-fence backend is yet wired, so
automatic mid-flight eviction is deliberately not used. Existing
view eviction still follows legacy release assumptions, and memory
benefits are not quantified. This experimental path needs build/GPU
verification before being enabled by default.

Implementation-only stage: testing intentionally postponed at user
request. See `docs/handoffs/prisma-stage-19.md`.

## Stage 20 — Tilt Shift graph opt-in

`AIVERSE_PRISMA_GRAPH_TILT_SHIFT=1` routes the existing three-pass
Tilt Shift (downsample, horizontal Gaussian blur, vertical Gaussian blur)
through `PrismaRhiExecutor`. Four logical SSA resources describe the
source, first A target, B target and final A target. The two versions of
A are deliberately mapped to the same existing physical texture: they
are never simultaneously needed, and the intermediate B is separate.
Per-pass uniforms and original pipelines are preserved. The default
path remains the legacy three draws. Missing resources or failures before
GPU submission fall back to it; partially dispatched passes are not replayed.
This stage does not yet migrate physical Tilt targets to the graph-owned pool,
nor does it activate Tilt Shift by default. Native/GPU tests deliberately
NOT RUN per user request. Handoff: `docs/handoffs/prisma-stage-20.md`.


## Stage 21 — Sun Shafts graph opt-in

`AIVERSE_PRISMA_GRAPH_SUN_SHAFTS=1` enables a two-pass graph for
the existing sunlight prefilter mask and radial scattering operation.
The sun position, offscreen edge fallback, visibility threshold, color,
quality tier, sample count and shader uniforms are unchanged. Three
externally owned textures represent the source, mask and radial output,
with the existing per-size depth attachment. The two-pass graph executes
through the per-viewport RHI executor. Failure before any submission uses
the legacy two-draw path; a partially submitted graph is never replayed.
The path remains off by default and the HUD is still drawn after postprocessing.

No new serialised settings or shader variants were introduced.
This stage is code-only, intentionally NOT compiled or GPU-tested yet.
See `docs/handoffs/prisma-stage-21.md`.


## Stage 22 — experimental depth-based Depth of Field

The new `depthOfField` entry in `postfx.effects` is handled by the shared
PRISMA data catalog (MCP and editor parameter discovery are data-driven).
Parameters are `focusDepth` (0..1 **GPU device depth**, not metres),
`focusRange` (depth interval), `maxBlur` (0..16 output pixels) and
`intensity` (0..1). Defaults: 0.5, 0.08, 8, 1 respectively.
This entry defaults to High post quality and is classified Heavy in
the cost estimation model.

The opt-in `AIVERSE_PRISMA_DOF=1` activates one full-resolution
nine-neighbour blur pass **before** SDR FXAA and Bloom; it reads the
scene color and render-target depth using the existing GLSL/MSL
fullscreen-program path and produces a new color texture used as
the source by subsequent effects. The hardware depth is deliberately
not represented as physical focus distance, since the PostFrame
currently lacks a portable inverse-projection/near-far contract.
The blur radius follows the distance between sampled hardware
depth and `focusDepth`, with a central sharp interval. The shader
is a bounded, approximate defocus, **not** cinematic hexagonal bokeh,
depth-layer separation or physically accurate lens simulation.

The pass samples the original scene depth, but uses its own
scratch depth attachment of matching dimensions, avoiding read/write
feedback on the same depth texture. It can run only when the
configured effect, opt-in flag, necessary GPU resources and
pipeline are present. Without the flag, existing image processing
continues unchanged. DoF is disabled on low/medium quality by its
default `minQuality=high`, unless a project explicitly overrides it.

Example game.json `postfx.effects` entry:
```json
{"type":"depthOfField","focusDepth":0.65,"focusRange":0.08,"maxBlur":6,"intensity":0.8}
```

All native compilation, CTest and visual/GPU smoke are intentionally
**NOT RUN** at the user's request. Real Metal/GL depth texture
sampling compatibility, reverse-Z behavior and scene-depth
interpretation need later validation. The next feature is
true world-distance autofocus via camera projection data, followed
by multi-resolution CoC separation and physically-inspired bokeh.
See `docs/handoffs/prisma-stage-22.md`.


## Stage 23 — HDR Auto Exposure (GPU log luminance + eye adaptation)

`postfx.effects` can now contain `{"type":"autoExposure"}`. The optional
feature remains disabled in rendering unless
`AIVERSE_PRISMA_AUTO_EXPOSURE=1` is set **and** the current scene is
using linear HDR RGBA16F. On SDR/RGBA8, the effect has no GPU action,
because clipped highlights cannot be recovered by metering.

The GPU pipeline in `Prisma::resolve()` is:
1. Read scene-linear RGB and store log2 luminance in a half-resolution
   RGBA16F target (four color taps with Rec.709 luminance coefficients).
2. Repeatedly average log luminance through a 2x2 pyramid to 1x1. This
   estimates geometric mean brightness, not a full per-bin histogram.
3. Determine target exposure `2^(clamp(log2(keyValue)-meanLogL+
   compensation, minEV, maxEV))`. In a dedicated RGBA16F 1x1
   ping-pong pass, blend from previous exposure using
   `1-exp(-speed*deltaSeconds)`. `speedUp` controls increasing
   exposure (dark->bright output); `speedDown` decreasing exposure.
   The time delta is clamped at 0.1s to avoid large jumps after stalls.
4. Sample the resulting exposure multiplier as `TEX8` in the final
   composite **before** HDR tonemapping and sRGB conversion. UI remains
   unprocessed. ColorGrade's manual exposure/compensation still multiplies
   the automatic result.

Parameters (regular `PrismaData` catalog and editor/MCP discovery):

| parameter | default | range | meaning |
|---|---:|---:|---|
| `keyValue` | 0.18 | 0.01–1 | target scene middle grey |
| `minEV` | -6 | -12–0 | lower automatic EV limit |
| `maxEV` | 6 | 0–12 | upper automatic EV limit |
| `speedUp` | 3 | 0.01–20 | brighten response rate, 1/s |
| `speedDown` | 1 | 0.01–20 | darken response rate, 1/s |
| `compensation` | 0 | -5–5 | EV bias |

Example:
```json
{"type":"autoExposure","keyValue":0.18,"minEV":-4,
 "maxEV":4,"speedUp":2.5,"speedDown":1.0,"compensation":0.0}
```

Per-`Prisma::Sized` temporal history uses two 1x1 HDR textures and
a previous timestamp. Explicit native `Prisma::resetAutoExposure()`
invalidates history for camera cuts. Project reconfiguration,
postprocess toggle and quality changes reset it; a disabled HDR/auto
mode also clears valid history. The first enabled frame snaps to the
current measured exposure, avoiding reading an uninitialized history
texture. No CPU brightness readback is used.

**Limitations:** the algorithm computes mean *log* luminance, **not**
a percentile histogram; bright outliers may still influence adaptation,
and the measurement is not a physically calibrated light meter. The
existing `Sized` cache is keyed by dimensions rather than stable camera
identity, so two viewport cameras of identical size can share their
temporal history. A follow-up should add explicit stable view IDs.
The fixed exposure sampler uses slot 8, which is also available for
the fourth custom ShaderGraph texture (slots 5..8); automatic exposure
is skipped for those graphs rather than clobbering user textures. HDR
support and blending of 1x1 RGBA16F must be verified on real Metal/GL.

This is an opt-in source implementation with **NO native/GPU tests run**,
per the user's features-first request. Do not enable it globally until
verified. Handoff: `docs/handoffs/prisma-stage-23.md`.


## Stage 24 — independent camera history and weighted GPU metering

The presentation-only API `Prisma::setViewId(uint64_t id)` selects a
**stable** camera/view identifier before rendering. `viewId() const`
returns it. A view's two 1x1 RGBA16F exposure-history render targets,
adaptation timestamp, valid bit and ping-pong index now live in
`Prisma::exposureViews_`, keyed by that ID **instead of render dimensions**.
`resetAutoExposure(id)` invalidates one view; the no-argument overload
invalidates every view. The older target caches remain keyed by size
for sharing ordinary temporary textures; exposure *history* no longer
cross-contaminates identically sized Scene and Game cameras.

The editor sets view IDs `1` for Scene and `2` for Game before
`applyCameraVolumes`; `GameClient` uses ID `3` within its
own PRISMA instance. A native project that renders multiple cameras
must assign distinct IDs itself. The default ID remains `0` for
legacy native projects. Exposure histories are retained until
`Prisma::shutdown` (no automatic per-camera removal/fence retirement
yet). IDs must be stable, not increment every frame.

`autoExposure` has four additional configurable parameters:
- `centerWeight` (default 0, range 0..1): 0 = uniform metering,
  1 = strongly center-weighted.
- `centerRadius` (default 0.35, range 0.05..1):
  normalized radius for Gaussian center weighting.
- `minMeterLogL` (default -10, range -16..0) and
  `maxMeterLogL` (default 10, range 0..16):
  clamp individual pixel log2 luminance in the initial HDR
  metering pass, reducing extreme brightness influence.
  This is not percentile-based histogram clipping.

The first pyramid pass now outputs two channels: weighted sum of
log2 luminance (R) and sum of weights (G); each later 2x2 pass reduces
both components identically. The 1x1 adaptation shader divides R/G
before computing target EV. No CPU readback, additional fullscreen
texture or extra raster pass is required compared with Stage 23.
The format stays RGBA16F throughout.

Example `postfx.effects` entry:
```json
{"type":"autoExposure","keyValue":0.18,
 "centerWeight":0.75,"centerRadius":0.35,
 "minMeterLogL":-8,"maxMeterLogL":6}
```

All rendering remains opt-in via `AIVERSE_PRISMA_AUTO_EXPOSURE=1`,
High-tier HDR only. Existing `postfx` schema is extended, not migrated.
CMake, CTest and real Metal/GL validation are **NOT RUN** as requested.
See `docs/handoffs/prisma-stage-24.md`.


## Stage 25 — HDR camera motion vectors and Motion Blur (experimental)

`postfx.effects` now accepts `motionBlur` through the shared,
genre-independent PRISMA catalog. The new effect defaults to
`minQuality: "high"` and is opt-in at runtime via
`AIVERSE_PRISMA_MOTION_BLUR=1`; it additionally requires an HDR
RGBA16F scene (signed RG motion vectors are not representable in the
existing RGBA8 path without encoding).

Parameters: `intensity` (0..1, default 0.65),
`maxPixels` (0..64, default 20), `samples` (2..16, default 8),
`shutterScale` (0..2, default 1). Example:
```json
{"type":"motionBlur","intensity":0.6,"maxPixels":20,
 "samples":8,"shutterScale":1.0}
```

The motion-vector pass samples the current hardware scene depth
and reconstructs the world position from the inverse current
viewProjection. It projects that position with the previous
viewProjection, writing a signed UV velocity
`currentScreenUV - previousScreenUV` into an RGBA16F texture.
The next raster pass gathers current-frame scene color along
this velocity and blends it with the sharp color. Motion blur is
executed **before** depth of field, FXAA, Bloom and final composite;
HUD/UI remains unprocessed. Separate scratch depth attachments avoid
sampling and writing the scene depth simultaneously.

The previous camera matrix, validity flag and render dimensions are
stored in `Prisma::motionViews_` per stable `setViewId()`, using the
same camera identity contract as Auto Exposure. The first rendered
frame, resize, changed quality, postprocess toggle or explicit
`resetMotionHistory(viewId)`/no-argument reset invalidates
motion history and results in an unblurred frame; it then seeds the
matrix for the next frame. `setSettings` also resets history.

**Optional object velocity bridge:** a native renderer may call
`prisma.setObjectMotionVectors(viewId, velocityTexture)` each frame
with a borrowed, full-resolution texture whose R/G values are signed
current-minus-previous normalized screen UV and B is a valid-object
pixel mask (B > 0.5). If present, that velocity overrides camera-only
velocity for tagged pixels. The borrowed handle is cleared after each
resolve. PRISMA does **not yet** generate per-object velocity buffers
from mesh transforms/skinning; the adapter is an integration contract,
not automatic object-motion rendering. These inputs are presentation
state, not deterministic ECS.

Limitations: this is a camera-reprojection motion blur, not TAA or
temporal image accumulation; it does not store previous color, and
therefore does not synthesize missing disoccluded samples. Camera cuts
must be signaled explicitly to avoid blur across teleports; material
transparency and sky (depth≈1) receive zero camera velocity in the
basic shader. Neither Metal/GL depth sampling, signed velocity
rendering, nor image correctness have been tested. All compilation,
CTest and GPU checks remain **NOT RUN**, per user request.
See `docs/handoffs/prisma-stage-25.md`.


## Stage 26 — automatic rigid-object motion vectors (experimental)

When HDR `motionBlur` is active with
`AIVERSE_PRISMA_MOTION_BLUR=1`, Renderer3D now has an optional GPU
overlay pass for **opaque rigid ModelDraw primitives with a stable
`persistentId`**. It runs between the scene pass and
`Prisma::resolve()`, outside any open render pass, then sends the
RGBA16F velocity texture to PRISMA through the new generic
`PostProcess` bridge. It does not modify gameplay/ECS, and the
existing camera-derived vectors remain the fallback for pixels
without valid object velocity.

The renderer tracks the previous world transform of each
`(ModelDraw::persistentId, nodeIndex)` in a per-view map. On the first
frame or after a camera cut, it records current transforms without
drawing a velocity overlay. Once the camera history is valid, it
renders each eligible rigid primitive with a dedicated GLSL/MSL
shader and `ModelVertex` position stream. Current and previous
model-view-projection matrices produce signed current-minus-previous
screen UV motion in RG. B=1 marks visible rigid pixels. A separate
scratch depth texture is attached; the shader *samples* original scene
depth and rejects fragments behind the visible surface, so world
occlusion/terrain can hide moving objects without writing scene depth.

This pass operates on the existing ModelItems after ordinary batching:
individual model node transforms are preserved even if the regular
opaque pass used instancing/MDI. The velocity overlay currently uses
one regular indexed draw per primitive (not an indirect/instanced
velocity submission); it reuses the existing model vertex/index GPU
buffers. The overlay render target and depth attachment are owned
per `PostProcess` view ID by Renderer3D and released on its shutdown.
No resource is shared between cameras with different stable IDs.

Only *rigid*, visible, opaque, non-deformed, stable-ID model primitives
are eligible. Missing IDs, transparent/masked materials, skinned or
baked animated meshes, morph targets, custom geometry and transient
immediate draws are skipped. Their pixels continue using the
camera-only motion reconstruction. The scene depth test is an
approximate epsilon comparison, not a full material/alpha/deformation
replay; fast disocclusions and edge artifacts remain possible.

Existing `Prisma::setObjectMotionVectors(viewId,texture)` stays
available for custom native renderers; automatic Renderer3D
generation uses the same borrowed-handle bridge via virtual
`PostProcess::acceptObjectMotionVectors`. There is no ECS scheduling
or genre-specific integration. Default behavior is unchanged when
MotionBlur is not active.

**NOT RUN:** native build, CTest, Metal/GL shader compilation,
screen comparisons and profiling, per user's features-first request.
A GPU fence-completed retirement path is still required for robust
target resizing and teardown while work is in flight. See
`docs/handoffs/prisma-stage-26.md`.


## Stage 27 — deformation-aware and instanced object motion vectors

The optional HDR `motionBlur` path now contains two new
`Renderer3D` velocity render variants, both active only with
`AIVERSE_PRISMA_MOTION_BLUR=1`:

**Rigid velocity instancing.** Geometry-equivalent visible opaque rigid
`ModelItem` instances are grouped by source model pointer, first index
and index count. A separate 16-instance GLSL/MSL pipeline consumes
per-instance *current and previous* MVP matrices, submitting one
`drawIndexed(..., instanceCount)` per compatible group (up to 16
instances per group). The uniforms occupy at most 2064 bytes; a single
item or missing batched pipeline still uses the Stage 26 singleton
shader. Matching shader/geometry groups are presentation-only; the
existing color draw batching is not altered.

**Skinned and morph velocity.** Palette-skinned objects and morph-target
primitives with a nonzero stable `persistentId` can produce actual
deformed vertex velocities. The renderer snapshots the current joint
palette and eight morph weights per
`(persistentId, nodeIndex, geometryPrimitive)`, retaining the previous
frame's values independently for each camera. A separate shader deforms
each mesh vertex for **both** the current and previous pose, computes
two clip positions and stores screen UV displacement. Current and
previous U3/U4 blocks hold up to 80 joints (3 rows each); U5 contains
the two 8-weight morph arrays and morph atlas parameters; tex8 reuses
the existing morph delta texture. GPU skinning/morph math is kept
separate from the deterministic simulation and is not a CPU-skinned
mesh upload. A missing previous pose or changed source model identity
causes the deformable primitive to skip object-velocity override rather
than invent movement.

**Depth-edge motion blur.** Motion Blur now samples the original
scene depth at each color gather position and reduces contributions
across large depth discontinuities. This protects foreground outlines
against unrelated background colors, although the rejection threshold
is based on non-linear hardware depth, so it is a conservative
approximation, not physically exact depth-layer motion reconstruction.
`Prisma::buildProgram` supports `depth2d<float>` for explicitly marked
depth samplers on Metal while keeping GLSL sampler2D conventions.

Eligibility remains intentionally conservative: visible, opaque
non-displaced primitives with stable IDs. Baked animation (whose pose
comes from clip textures), material ShaderGraph vertex displacement,
transparent/masked models and custom geometry are not yet handled by
the new deformed-velocity pipeline. Velocity target resize, first
frame and camera cuts seed history rather than extrapolating invalid
motion. Renderer3D owns lazy velocity shader/pipeline and target
lifetime.

**Verification deferred** by user request: native compilation,
Metal/GL shader validation, visual comparison, CTest and GPU profiling
have NOT RUN. Do not enable these experimental paths by default before
they are validated. Details: `docs/handoffs/prisma-stage-27.md`.


## Stage 28 — baked clip motion, alpha-cutout velocity and depth thresholds

The experimental `motionBlur` pipeline now includes a **baked-clips
vertex velocity** shader variant. For a `ModelItem` with `baked=true`,
the renderer records the current `anim` values (clip A row, clip B row,
blend) and world matrix by stable `(persistentId, nodeIndex,
geometryPrimitive)`. The new GLSL/MSL vertex shader reads the original
baked bone atlas (`tex7`) and original per-primitive joint palette.
Current and previous clip rows and interpolation are sampled separately
to reconstruct both deformed vertex positions. The velocity fragment
then writes current-minus-previous normalized screen UV motion. The
history snapshot is presentation-only; the animation and authoritative
simulation aren't modified. This path intentionally skips baked +
morph simultaneous deformation until a mixed permutation is available.

An optional **alpha-cutout rigid** velocity variant now replays the
material's base texture alpha cutoff with KHR texture transform
(offset/scale/rotation), base alpha and vertex-color alpha (when
enabled). Because cutout surfaces contain empty pixels, the dedicated
variant samples the base alpha texture and discards rejected fragments
before depth-occlusion testing. These materials are kept out of the
ordinary opaque instanced velocity draw and rendered separately.
ShaderGraph-displaced surfaces and deformable alpha-cutout models are
not included, since their visible shape requires matching additional
material stages. Fully blended/transparent models remain excluded.

The rigid history now stores the source Model identity alongside its
world transform, preventing a reused `persistentId` from picking up
a different model's previous transform. Existing per-view reset, initial
frame and resize behavior stays intact.

`motionBlur` also accepts `depthThreshold` (default 0.012, range
0.0005..0.05) for rejecting unrelated depth layers during the
directional color gather. The shader falls back to the sharp sample if
all gathered samples are rejected instead of blending black into the
image. The threshold is in **non-linear device depth units**, not metres.
Example:
```json
{"type":"motionBlur","intensity":0.65,"maxPixels":20,
 "samples":8,"shutterScale":1,"depthThreshold":0.012}
```

All additional paths remain behind the pre-existing
`AIVERSE_PRISMA_MOTION_BLUR=1` and HDR/quality gates. They use the
same masked velocity texture bridge and do not change gameplay ECS or
SpectraFX. Baked clip pose and rigid alpha-cutout motion use new
GLSL/MSL variants; no backend-specific testing has yet verified
their image parity. CPU/GPU builds, CTest, shader compilation,
screenshots and benchmarks are **NOT RUN** at the user's request.
See `docs/handoffs/prisma-stage-28.md`.


## Stage 29 — camera-cut epochs, reactive transparency and baked+morph velocity

PRISMA's experimental HDR motionBlur path now has three additional
presentation-only capabilities, all gated by the existing
`AIVERSE_PRISMA_MOTION_BLUR=1` runtime flag and configured quality.

**Explicit camera/view lifecycle.** `Prisma::resetMotionHistory(viewId)`
increments a monotonically increasing per-view camera-cut epoch exposed by
`PostProcess::objectMotionEpoch(viewId)`. Renderer3D reads this epoch on
each opt-in velocity pass and discards prior rigid and deformable transform
snapshots when it changes. This keeps the camera reprojection and
per-object pose histories in sync across camera cuts, teleports, quality
changes and map reconfigurations without modifying gameplay ECS. The
renderer also provides `resetObjectMotionHistory(viewId)` and
`resetAllObjectMotionHistory()` for explicit standalone control.
`releaseObjectMotionViewAfterGpuIdle(viewId)` releases the per-view
GPU velocity, reactive and depth textures **only** after the host has
confirmed GPU idle; CPU frame serials are NOT a GPU completion signal.
No automatic viewport closure or backend GPU fences are wired yet.

A new read-only `Renderer3D::objectMotionStats()` returns the last
velocity pass's view ID, submitted state and rigid instance/draw, cutout,
skinned, baked, reactive and rejected draw counters. These values are
CPU submission diagnostics, not measured GPU time or performance data.

**Reactive masks.** Renderer3D can emit a separate, current-frame RGBA8
reactive coverage texture for visible **rigid, translucent ModelDraw**
primitives. It samples the final scene depth and uses a dedicated
scratch depth attachment rather than modifying scene depth. The red
channel is coverage strength, while an all-zero background means no
reactive pixels. `PostProcess::acceptMotionReactiveMask` forwards the
borrowed handle to PRISMA. Native renderers can instead provide a
borrowed R=0..1 texture through
`Prisma::setMotionReactiveMask(viewId,texture)` before resolve.
The motion blur shader samples this mask at texture slot 3 and reduces
local blur strength by `1 - reactive * reactiveStrength`. A missing
mask uses the black fallback, so motion blur remains unchanged.

The new optional effect parameter `reactiveStrength` defaults to 0.85
and has a range of 0..1. The renderer's reactive pass is a conservative
geometric silhouette of translucent rigid models: it does **not**
reproduce per-pixel blended texture alpha, alpha-test materials,
skinning, particles or water drawn by other renderer modules.
False-positive reactive coverage may reduce blur inside glass geometry,
which is preferable to inventing object velocities. It remains an
experimental approximation.

**Baked clip + morph.** The baked-velocity vertex shader now applies
both the current and previous morph weights against immutable morph
deltas (tex8) **before** sampling current and previous baked clip rows
from tex7, matching the existing color shader's deformation order.
Palette, animation and morph parameters are all per primitive and
per view; a missing previous frame or changed model identity still
suppresses object-velocity override. This does not add per-vertex CPU
readback or change deterministic animation state.

LOD transition geometry that requires screen-space dithering is now
conservatively excluded from object velocity until a matching LOD fade
shader is implemented; camera motion reconstruction remains the
fallback there.

Example:
```json
{"type":"motionBlur","intensity":0.65,"maxPixels":20,
 "samples":8,"shutterScale":1,"depthThreshold":0.012,
 "reactiveStrength":0.85}
```

No native C++ compilation, shader validation, GPU tests, screenshots
or CTest were run at the user's request. The new GPU passes and
lifetime APIs remain opt-in and are not proven production-ready.
See `docs/handoffs/prisma-stage-29.md`.


## Stage 30 — animated reactive coverage, extensible water/particle masks, LOD fallback and velocity dilation

The existing experimental `motionBlur` chain now supports animated,
current-frame reactive coverage for translucent skinned, morph and
baked-clip `ModelDraw` geometry. These **reactive** passes do not
invent previous animation poses or feed object motion velocity: they
evaluate the *current* GPU-deformed vertex position using the same
vertex path as the Stage 27/29 motion shaders, sample the read-only
scene depth for visibility, and mark covered pixels as reactive (R=1).
The animated Metal shaders reuse the established skinned/baked vertex
shader source while substituting a coverage-only fragment. This handles
baked+morph current-pose silhouettes, but does **not** reproduce
per-pixel transparent material alpha, ShaderGraph vertex displacement,
screen-space water, or particles automatically. These may be more
accurately supplied by the owning renderer.

### Reactive coverage extension for all renderers

`Renderer3D::addMotionReactiveDraw(fn)` registers a presentation-only,
current-scene callback with signature

```cpp
void(gfx::Device& device, gfx::TextureHandle sceneDepth,
     int width, int height)
```

Register **after** `beginScene()` and before `resolvePrisma()` (typically
after the main scene/color pass has been submitted); callback registration
is cleared automatically after the current scene's resolve. The
callback is invoked **inside an already open RGBA8 reactive render
pass** with a separate scratch depth target; it must never call
`beginPass/endPass`. The provider binds its own shader, geometry,
texture/constant state and writes red channel R in [0,1] (0 = no
reaction, 1 = suppress blur according to the configured strength).
Use coverage-union-friendly blending, such as
`gfx::BlendMode::Screen`, so several water/particle/object providers
combine rather than erase one another. The current sceneDepth handle
may only be sampled: never rebind that texture as a writable depth
attachment. The provider must keep any borrowed buffers/textures valid
through the callback and cannot modify gameplay ECS state.

The generic callback is available for rendering engines and projects
implementing water, particles, weather or custom presentation. Stage 30
does **not** automatically instrument existing water/VFX packages;
those owners must register their respective reactive render callbacks
to render the correct coverage with their own material/geometry
semantics. No callbacks are invoked unless HDR MotionBlur is active and
a valid camera history exists; registration by itself cannot force the
postprocessing chain to run.

Built-in reactive pipelines now use Screen blending for accumulation
of multiple overlapping masks. The rigid fallback also reproduces
the pixel-dither coverage rule of `ModelShaders` for dithered LOD
crossfades: these pixels mark themselves reactive rather than
incorrectly receiving normal rigid-object velocities. This is
coverage-aware blur suppression, **not** full motion-vector
reconstruction across LOD changes.

### Depth-matched velocity dilation

The new `motionBlur.velocityDilation` parameter (default 1.0,
range 0..2, 0 disables) samples four neighbouring velocity values at
the requested radius, selecting a stronger velocity only when its
device-depth closely matches the center pixel. This helps preserve
motion around silhouettes without importing motion from clearly
different depths. It is a local approximate depth check (hardware
depth units, not metres) and costs up to four additional velocity and
depth reads per blurred pixel. The cost estimator includes this
additional work when enabled.

Example:
```json
{"type":"motionBlur","intensity":0.65,"maxPixels":20,
 "samples":8,"depthThreshold":0.012,"reactiveStrength":0.85,
 "velocityDilation":1}
```

All additions use the existing `AIVERSE_PRISMA_MOTION_BLUR=1` opt-in
flag, HDR RGBA16F and appropriate quality tier. Reactive mask
callbacks are a `Renderer3D` presentation extension, independent of
FPS, RTS, water or SpectraFX ownership.

**NOT RUN:** CMake/native C++ compilation, CTest, Metal/GLES/GL shader
compilation, screenshots, memory/performance profiling or CI, per
the user's features-first instruction. No hardware parity or shipping
readiness is claimed. See `docs/handoffs/prisma-stage-30.md`.


## Stage 31 — real RTS water/particles reactive integration and optional linear depth

Stage 30 introduced the generic `Renderer3D::addMotionReactiveDraw()`
producer interface. Stage 31 connects **existing runtime producers**:

- **RTS terrain water and generic mesh translucency:** `RtsTerrainRender.cpp`
  queues water with `Renderer3D::drawMesh(water_, ..., waterColor)`.
  `Renderer3D::renderObjectMotionVectors` now picks up the real
  translucent `DrawItem` mesh buffers (including those water sheets).
  A dedicated `MeshVertex` pipeline draws that same geometry into the
  RGBA8 reactive mask with read-only scene depth, no gameplay geometry
  regeneration. The mask strength is the draw item's alpha. The
  fallback shader is two-sided for water sheets. Other semi-transparent
  immediate MeshVertex draws benefit from the same path.
- **Real particle billboards/trails/ribbons/mesh particles:**
  `fx::ParticleSystem::render()` records the non-opaque runs that it
  *actually rendered*, including source alpha texture and vertex ranges.
  `hasReactiveGeometry()` reports whether those runs are available.
  `renderReactive(gfx::Device&,sceneDepth,w,h)` runs **inside the
  already-open** Stage 30 mask pass, using exactly the uploaded
  current-frame particle vertex/index buffer and alpha texture. It
  samples scene depth, combines source alpha with particle vertex
  alpha, and uses Screen blending to union overlapping coverage.
  Quads use the existing 16-bit indexed ranges; mesh particles use
  their nonindexed triangle ranges. There is no second particle
  simulation, CPU vertex expansion or additional vertex upload.
- **Client and editor:** `GameClient.cpp` and `Editor.cpp` now
  register callbacks immediately after `fx_.render` /
  `gameFx_.render` and before `r3d_.resolvePrisma()`. The callbacks
  consume the just-recorded particle geometry synchronously during
  the current scene's reactive-pass. They are not retained between
  view frames.

These paths remain **behind** the regular `motionBlur` effect,
`AIVERSE_PRISMA_MOTION_BLUR=1`, required HDR format and minQuality
gating. Water and particles are genre-independent *producers* of
current-frame presentation coverage; PRISMA still owns the full-screen
postprocessing and does not depend on any specific water/FX module.
Specialized water/particle renderers not using these two concrete
paths can continue using the existing generic callback.

### Optional linear view-depth filtering

`PostFrame` now carries the camera's actual projection near/far
planes and perspective/orthographic mode. The renderer populates
these from its `RTSCamera`; orthographic near/far match its
explicit -200..1000 projection.

`motionBlur` adds two optional settings:
- `linearDepth` (default false): if enabled and camera projection
  planes are valid, compares **linear view distance** for velocity
  dilation and blur gather rather than non-linear window depth.
- `depthToleranceMeters` (default 0.35; accepted range 0.01..10):
  near/far-layer rejection tolerance in scene world units (metres
  when the project uses metre scale).

The renderer uses the ordinary non-reversed 0..1 **window** depth
on both GL and Metal; the helper handles perspective and
orthographic projections separately. If `linearDepth` is false,
`depthThreshold` retains its existing meaning in hardware depth
units, so previously authored scenes retain their original
behavior. Both paths sample only the current frame and are
presentation-only. Example:

```json
{"type":"motionBlur","intensity":0.65,"maxPixels":20,
 "samples":8,"depthThreshold":0.012,
 "linearDepth":true,"depthToleranceMeters":0.35,
 "velocityDilation":1,"reactiveStrength":0.85}
```

**Resource ownership:** the real particle producer owns its shader
and two reactive pipelines and destroys them with `ParticleSystem`.
`Renderer3D` owns the generic MeshVertex reactive pipeline and
destroys it with other object-motion resources. The reactive pass
owns the output texture; producers must not begin/end GPU passes or
destroy its source depth. Raw externally supplied reactive/velocity
textures still have no portable RHI introspection for dimensions
and formats and must be validated by their owner.

**NOT RUN** (user requested features before tests): native C++
compiler, CMake, CTest, Metal/GL/GLES shader compilation, editor
smoke, water/particle image comparison, timing and GPU profiler.
No verified parity or speedup is claimed. See
`docs/handoffs/prisma-stage-31.md`.


## Stage 32 — specialized SpriteBatch water masks, live RHI texture metadata, and GPU-fence retirement contract

Stage 31 connected actual RTS terrain water through `Renderer3D::drawMesh`.
For the **separate** sprite/decal water path, Stage 32 adds
`SpriteBatch::drawWater` alpha-aware reactive rendering: the original
static `SpriteMesh`, water texture, and view-projection transform are
recorded and can be registered with the current scene via
`SpriteBatch::registerWaterReactive(renderer)`. The callback replays
only the existing mesh buffers into the open reactive pass. A dedicated
GLSL/MSL shader samples the water sheet's alpha and per-vertex tint
alpha and discards pixels hidden behind original scene depth.
It uses `gfx::BlendMode::Screen` so water combines with existing
model/particle masks rather than erasing them. The specialized
`drawWater` fallback to lit decals remains unchanged.

**Important integration scope:** repository search found no current
calls to `SpriteBatch::drawWater` in this branch. The producer support
is complete at the API/source level, but a native game using specialized
sprite water must explicitly call `registerWaterReactive` after its
`drawWater` calls and before `Renderer3D::resolvePrisma()`.
Ordinary RTS water through `drawMesh` already registers itself
automatically. `SpriteBatch::begin` clears its captured water draws
every frame, and registration consumes them; never destroy its textures,
mesh buffers or SpriteBatch before the synchronous callback finishes.

### Validating externally owned velocity and reactive textures

`PrismaMotionTextureInput.h` adds `MotionTextureInput`, a non-owning
handle with optional declared width, height and `gfx::PixelFormat`.
The `PostProcess` bridge has new typed overloads of
`acceptObjectMotionVectors` and `acceptMotionReactiveMask`; the
original handle-only overloads remain for existing consumers.

`Renderer3D` now submits typed inputs for its own full-resolution
RGBA16F velocity and RGBA8 reactive targets.
In `Prisma::resolve`, both inputs must match the active viewport
dimensions and required formats. If a mismatch is found, the object
velocity is ignored and camera reprojection is used; a mismatched
reactive mask is treated as absent (black fallback). This prevents a
stale Scene/Game texture of another size being sampled as a valid
input. The owning renderer retains GPU lifetime responsibility.

The RHI adds `gfx::Device::textureInfo(handle,out)`, a CPU-only
metadata query that is implemented by GL, Metal, and Vulkan from
their own live resource tables. PRISMA checks **actual RHI metadata**
where available, even for a handle-only legacy input. Providers
whose backends do not expose metadata are checked against any
declared metadata; the existing untyped legacy path otherwise remains
trusted for compatibility. This API performs no GPU readback and
does not make input textures globally self-describing when passed
outside the RHI.

### Safe handoff for retiring old motion targets

`Renderer3D` now uses the Stage 18
`PrismaGpuRetirementQueue<gfx::TextureHandle>` for view-motion
textures. `setObjectMotionSubmittedFence(token)` registers an
**actual backend-issued** last-submitted fence for subsequent resizes.
Old velocity/depth/reactive targets are moved to a retirement queue
rather than immediately destroyed. `collectCompletedObjectMotion(
completedGpuFence)` destroys only handles whose submission fence
was **confirmed GPU-complete** by the host. The manual
`retireObjectMotionView(viewId,lastSubmittedFence)` transfers one
view's current targets to this queue and discards its CPU history.

If the renderer has no valid submission fence, old targets are put
into an **unfenced quarantine**, where they are retained until
`drainObjectMotionAfterGpuIdle()` is called with a proven idle GPU.
Normal engine shutdown currently assumes the host has reached its
existing safe teardown point. There is no automatic backend fence
emission or polling yet, so this conservative default can retain
GPU memory across many viewport resizes until idle. **CPU frame
counters and arbitrary N-frame delays do not count as GPU fences.**

The older `releaseObjectMotionViewAfterGpuIdle` remains a separate
synchronous API and must only be invoked after the GPU is idle.
These changes do not automatically repair unrelated PRISMA `Sized`
pool or other rendering-resource release points; those require
their own GPU-completion-aware integrations.

All added GPU paths are opt-in under
`AIVERSE_PRISMA_MOTION_BLUR=1`; nothing new is enabled by default.
**NOT RUN:** native compilation, CTest, GLSL/MSL shader compilation,
Metal/GL/Vulkan images, GPU-performance measurements, and CI. The
source is not verified ready for production.
See `docs/handoffs/prisma-stage-32.md`.


## Stage 33 — GPU-completed fences for safe PRISMA resource retirement

The renderer now exposes a separate **GPU-completion timeline** in the
genre-independent `gfx::Device` interface:

```cpp
uint64_t submitted = dev.gpuSubmissionFence(); // current GPU frame, 0 if unavailable
uint64_t completed = dev.gpuCompletedFence();  // GPU-confirmed completion only
```

The submitted token covers work encoded in the *currently active* GPU
frame and is not the CPU frame counter. It can be attached to a target
retirement before that frame is actually submitted; the backend will
complete it only after GPU execution. A nonzero `gpuCompletedFence()`
result is a real, contiguous completed GPU timeline token. Unsupported
devices return zero. All values are per-device; never compare values
from different devices, contexts or GPU queues.

**Metal:** `GfxMetal.mm` uses the current command buffer's frame token
and updates an atomic completed counter in its
`addCompletedHandler` **only** for
`MTLCommandBufferStatusCompleted`. It does not turn
`frameSerial()` into a completion fence. The asynchronous callback
captures shared lifetime-safe state and the graphics owner thread
polls it by reading `gpuCompletedFence()`.

**GL33 / GLES3 / WebGL2:** after the frame's render commands the backend
inserts `glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0)` and flushes
the command stream without waiting. At the next `beginFrame`, it
calls `glClientWaitSync(..., timeout=0)` on its oldest pending fence,
advancing completed tokens strictly in submission order and deleting
signalled GL sync objects. The Windows GL33 dynamic loader now loads
`glFenceSync`, `glClientWaitSync`, `glDeleteSync`.
A failed fence is never treated as successful completion.
No per-frame `glFinish` is added.

**Vulkan:** uses its already existing per-frame `VkFence` and
`serial_`. `gpuSubmissionFence` is the currently recording GPU
frame's serial; `gpuCompletedFence` returns only the highest serial
whose slot fence has been successfully confirmed by
`vkWaitForFences`. A lost device or unsuccessful wait does not
advance the completed fence. This is the existing slot-wait path, not
a new polling implementation that blocks extra frames.

### Automatic motion target reclamation

`Renderer3D::beginScene` now calls
`collectCompletedObjectMotion(dev.gpuCompletedFence())` on the
graphics owner thread. When a viewport changes size,
`queueObjectMotionTexture` takes the current
`dev.gpuSubmissionFence()` if available and associates it with the
old object velocity, reactive or scratch-depth target. On completion
the old targets are destroyed; before then they are retained.
`retireObjectMotionView` and `setObjectMotionSubmittedFence`
remain available for externally managed submission lifecycles.
On devices without submission-fence support, old targets remain in
the Stage 32 quarantine until explicit GPU idle.

### Deferred PRISMA Sized / RenderGraph targets

`Prisma::sized` still keeps three active size entries for Scene/Game
views, but when evicting the least-recently-used entry it now
moves the **whole** `Sized` context — legacy raster targets, depth
targets, cached Bloom graph and persistent `PrismaRhiExecutor`
resource pool — into a fence-tagged retirement list instead of
releasing it immediately. When the GPU reports that the last
referencing submission completed, `collectCompletedSizes` frees
the retired targets and safely resets the graph executor pool.
In addition, `Prisma::target` now fence-retires targets replaced by
size/HDR format changes, avoiding immediate destruction of a texture
that may be in use by an earlier in-flight GPU command buffer.

`Prisma::releaseRetiredSizesAfterGpuIdle()` is the escape hatch for
hosts that have explicitly waited for an idle GPU; the function also
drains unfenced/quarantined individual targets. The `shutdown` path
assumes the platform's normal device-teardown GPU-idle condition.

This stage is about **resource lifetime correctness**, not a change
to image output, bloom topology, exposed `postfx` JSON, particle
simulation or gameplay ECS. Existing experimental flags remain off
by default.

### Limits

The new timeline only covers commands on the backend's regular
graphics submission queue. External GPU queues or devices need their
own fence tracking. Metal handles are released after the command
buffer completion callback; GLES/WebGL sync capabilities and actual
device behavior still require runtime validation. Vulkan may only
advance its completion counter when rotating to a frame slot whose
fence was waited. Without a GPU-completed token, retired resources
are **not** released opportunistically and quarantine can grow.

Auto Exposure per-view 1×1 history, PRISMA LUTs, externally owned
Sprite/FX buffers and other unrelated render-resource caches have
their own existing teardown policies; they are not all migrated to
this queue. RenderGraph internal pool cleanup is deferred together
with the evicted `Sized` context, rather than by assuming every
pixel pass finished at the end of the CPU frame.

Native compilation, CTest, Metal/GL/Vulkan GPU completion tests,
images and memory profiling have **NOT RUN**, per the user's
features-first instruction. See `docs/handoffs/prisma-stage-33.md`.


## Stage 34 — GPU-fenced per-camera history lifecycle and editor diagnostics

PRISMA temporal history is now explicitly **owned by stable view IDs**
instead of render-target size. The Stage 34 public APIs are:

```cpp
prisma.retireView(viewId);               // close only this camera's history
prisma.collectGpuRetirements();          // nonblocking, GPU-completed fence
auto post = prisma.retirementStats();    // read-only CPU diagnostics
auto motion = renderer.motionRetirementStats();
```

`retireView(id)` removes the per-view Auto Exposure ping-pong 1×1
RGBA16F texture handles from the active exposure map and queues them
via the existing GPU-fence-aware `retireTarget()` mechanism. An
unfenced backend retains them until a host-confirmed GPU-idle drain;
a completed GPU token permits collection without stalling.
Per-view MotionBlur matrices, validity and borrowed current-frame
velocity/reactive handles are discarded without destroying those
**externally owned** borrowed resources. Importantly, `retireView`
does **not** delete the shared `Prisma::Sized` scene render targets:
multiple Scene/Game cameras of equal resolution may still use them.

`Renderer3D::retireObjectMotionView(id, fence=0)` is now callable
without a manually supplied fence; the queue first queries the live
backend `gpuSubmissionFence()`. Its separate current-frame GPU
velocity, reactive and depth targets are retired independently from
PRISMA exposure. `Renderer3D::motionRetirementStats()` reports
retained per-view context count, fenced/unfenced texture count,
backend completed GPU fence and known pending bytes.

`Prisma::retirementStats()` reports active/retired size contexts,
exposure/motion view counts, fenced/unfenced textures, retired graph
pool entries, the last GPU-completed fence and **known minimum**
held GPU bytes. The byte estimate uses `Device::textureInfo()` on
live RHI allocations, including the archive of `Sized` targets and
depth attachments; it deliberately does not invent memory for
internal graph retired allocations whose handle metadata has
already been discarded. Statistics are **not** total VRAM use.

### Scene/Game viewport transitions

In the editor `Editor::frame` tracks which view **actually rendered
postprocess** this GUI frame. When a Scene or Game tab is hidden,
closed, switched to non-postprocessed diagnostic mode, or stops
rendering, the editor retires that camera's PRISMA state plus its
Renderer3D motion targets **once**, then continues collecting
backend-confirmed GPU completions on subsequent editor frames even
with no 3D viewport rendered. Reopening a view seeds fresh temporal
history instead of reusing stale exposure after a long hidden period.

The Scene view uses stable ID `1` and Game uses `2`. The editor
previously used Game ID `2` even when the Scene tab reused the
game renderer during play; Stage 34 passes explicit ID `1`
in that path to prevent simultaneous same-resolution Scene/Game
motion and exposure contamination. A hosted standalone game client
uses its own PRISMA instance and view ID `3`.

The native `GameClient` similarly retires view `3` on a new match
before camera and map replacement, and continues checking
pending GPU work on menu/loading frames when the 3D renderer is not
rendering.

The editor's **Statistics** tab now shows a new
`PRISMA · GPU resource retirement` section for completed GPU fence,
active/retired size contexts, view histories, counts of fenced and
unfenced textures, outstanding graph allocations, and the
**minimum known MiB** retained by both PRISMA and Renderer3D.
This is observational and creates no GPU readback or synchronizing
GPU wait.

### Project asset reloads

`Prisma::setImageLoader` now GPU-fence-retires superseded LUT and
user ShaderGraph textures rather than immediately destroying them
while another frame might still reference them. Shared shader
pipelines and graphs keep their existing ownership; this only
changes the texture retirement policy.

### Limitations / compatibility

The API leaves existing `postfx` JSON, editor configuration, game
behavior and ECS unchanged. No new postprocessing effect is enabled
by default. Active `Prisma::Sized` contexts remain keyed by output
dimensions, **not** individual camera IDs, because they contain
reusable frame-local targets; only private exposure and motion history
is camera-keyed.

`Viewport::color`/`Viewport::depth` (the editor's external
framebuffers) and other engine-wide texture resources are **not**
automatically released by `retireView`; the editor's own framebuffer
resource lifetime requires a separate safe owner-side migration.
If the GPU never exposes a completed fence, pending allocations
remain quarantined until explicit GPU idle. GPU bytes shown in the
Statistics tab are lower bounds, not comprehensive VRAM accounting.

**NOT RUN** as requested: CMake/C++, CTest, editor/client runtime,
Metal/GL/Vulkan GPU images, fence/pixel smoke, CI, profiler and
memory leak tests. Code and API integrations are source-level only
and not validated on real hardware.
See `docs/handoffs/prisma-stage-34.md`.


## Stage 35 — editor framebuffer GPU retirement and explicit pressure drain

The editor's actual `Viewport::color` and `Viewport::depth` render
targets now use the same GPU-completed-fence principles as PRISMA and
Renderer3D. They are **not** owned by PRISMA: a new
`apps/editor/ViewportGpuRetirement.h` owner lives in the `Editor`
instance and outlives its Scene/Game/FX window objects. `Viewport`
binds that owner during `ensure(dev,w,h,owner)`, and `release()` /
resize / viewport destruction transfer both GPU handles to the
owner instead of directly destroying in-flight images.

The old color handle may still be in an ImGui draw command even after
the window's `Viewport::image()` returns, so it is only destroyed
after `gfx::Device::gpuCompletedFence()` covers the submission that
may reference it. Unfenced resources are quarantined until the host
has explicitly verified GPU idle. The owner also clears the
viewport's stale `drawnFrame` / hover state when released.

All three editor producers (Scene, Game and FX windows) use this
shared owner. They release hidden/closed offscreen render targets
without relying on window destruction; the owner retains resources
past window lifetime. A hidden/closed tab can later recreate a
framebuffer with no stale ImGui texture handles.

### Memory pressure policy

`ViewportGpuRetirement` reports pending fenced/unfenced texture
counts, metadata-derived `knownPendingBytes`, a threshold and an
`overBudget` indicator. The default shared known-pending budget is
**128 MiB**, configurable through
`AIVERSE_PRISMA_RETIRE_BUDGET_MB` (32..4096 MiB). The editor
checks the sum of *known* pending bytes from Viewport, PRISMA and
Renderer3D on the **pre-render boundary** of `Editor::frame`,
before any new GPU draw submission. It normally collects already
GPU-completed resources with **no wait**.

If that known pending memory exceeds the budget, it can call the
new explicit `gfx::Device::waitForSubmittedGpuWork()` hook exactly
at that safe point. On successful synchronization it drains the
Viewport, PRISMA and Renderer3D GPU retirement queues, including
unfenced quarantine. It never releases an in-flight texture based
on the number of elapsed CPU frames.

This explicit pressure-only wait is implemented with `glFinish()`
on GL33/GLES3 (excluding WebGL), waiting the last **actually
committed** Metal command buffer (never the uncommitted buffer for the
current frame), and `vkQueueWaitIdle` on Vulkan. **These waits
are not performed every frame**; they can cause a visible one-time
stall at memory pressure. The WebGL2 backend explicitly returns
false because a portable synchronous idle barrier is not promised;
resources remain quarantined until an external safe teardown.
A positive wait result only covers **previously submitted** GPU
commands, so the editor must call it before it encodes new frame
work that uses those resources.

The Statistics tab now shows viewport fenced/unfenced resource
counts, known pending MiB versus the configured budget, and a
"Запросить безопасную очистку GPU" button. That button merely
requests a **one-shot cleanup at the next frame's pre-render
boundary**, not while the Statistics window's ImGui pass is active.

**The threshold is an advisory pressure budget rather than a global
hard GPU VRAM cap.** Internal RenderGraph allocations, active resources,
driver overhead, and unknown/unsupported RHI texture descriptors
may not be included in the lower-bound estimate. If a backend cannot
synchronize, the editor deliberately retains memory rather than
risking GPU use-after-free.

The RHI's prior `gpuSubmissionFence()` /
`gpuCompletedFence()` APIs are unchanged, and the new
`waitForSubmittedGpuWork()` is optional. All postprocessing flags
remain off by default; no ECS/gameplay ownership changes.

**Validation:** source-level inspection and GitHub writes only.
Native compilation, CTest, Metal/GL/Vulkan/Android/WebGL runtime,
ImGui texture lifetime, GPU fence pressure smoke and VRAM profiling
have **NOT RUN**, as requested. See
`docs/handoffs/prisma-stage-35.md`.


## Stage 36 — hosted GameClient teardown, screenshot lifetime, ShaderGraph preview retirement and graph memory accounting

### Hosted GameClient GPU ownership

The editor's `ClientHost::stop()` now logically stops the hosted client
immediately (so the UI no longer routes events or frames to it), but moves
its initialized `GameClient` object, its former HostedView and the
current graphics submission token into a deferred-retirement list. It
**does not call `GameClient::shutdown` while its GPU textures may still
be referenced by the editor's current or previous command buffers.**

`ClientHost::collectGpuRetirements()` checks completion on the same
`gfx::Device` that owns the archived client. Once the backend has
actually confirmed its submission fence, the owner restores that
client's saved hosted platform view and invokes its shutdown on the
graphics/UI owner thread. The editor calls this collector at the start
of every GUI frame, including after the Game tab has closed.
`ClientHost::drainAfterGpuIdle()` is available at a host-confirmed
GPU-idle point for clients whose submission fence was unavailable.
The editor includes this drain in the memory-pressure cleanup and
its graphics-device shutdown path. If more than two hosted clients are
archived while waiting for GPU completion, their **count** (not CPU
frame age) also triggers an attempted true pre-render GPU idle at
the next frame. If the backend cannot confirm idle, they are retained
rather than unsafely freed.

If no GPU submission token is available, an archived client is
quarantined rather than destroyed during an ordinary frame; its
GPU resources may remain allocated until a safe host teardown.
The editor Statistics tab reports `ClientHost::pendingRetiredClients()`
as a count. Its GPU bytes are **not yet** included in the numeric
pending-memory estimate, so the 128 MiB budget remains a lower-bound
trigger rather than a hard VRAM cap.

### Editor screenshot callback safety

`Editor::deliverShots` uses the graphics backend's existing
`Device::captureNextFrame` API, whose ownership includes the
backend's own staging/capture resources rather than an Editor-owned
offscreen texture. The callback previously captured an unguarded
`this`; it now also captures a shared atomic lifetime token.
`Editor::shutdown` invalidates that token and clears queued
screenshot requests; a late callback returns without referencing a
destroyed Editor, sending MCP replies through a closed connection,
or touching its viewport pointers. A fresh Editor initialization
creates a fresh token for the new lifetime. The backend still
owns its capture staging buffers; this guard does not change GPU
readback policies or invoke GPU synchronization.

### ShaderGraph Preview color/depth buffers

`ShaderGraphPreview` now binds the Editor's shared
`ViewportGpuRetirement` during initialization, and transfers its
two 256×256 color/depth render targets into that manager on graph
replacement or preview shutdown. Those textures may still be
referenced by an ImGui image or a previous GPU pass; the manager
releases them only after a verified GPU completion fence, or at an
explicit host idle drain. The manager outlives all editor window
objects. Other ShaderGraph preview-owned pipelines, material
portraits, particle systems and texture caches keep their existing
lifecycle policies; this stage does **not** claim every preview
subresource is fully fence-managed.

### RenderGraph memory telemetry

`PrismaRhiExecutor::gpuMemoryStats()` returns:
`liveTextures`, `pendingTextures`,
`liveKnownBytes`, `pendingKnownBytes`, and
`unknownFormatsOrDimensions`. The accounting enumerates
**actual** `PrismaGraphResourcePool` live allocations and the
`PrismaGpuRetirementQueue` pending handles using the RHI
`Device::textureInfo` query. No GPU readback or stalls are required.

`Prisma::retirementStats()` aggregates active and retired
`Sized` graph pools separately. Resources belonging to an archived
size set count as pending even if they were live in that set before
eviction; currently active graph allocations are tracked as *live*,
not added to the pending budget. The editor Statistics tab reports
the extra graph live/pending MiB, unknown allocation count and
deferred hosted-client count.

This improves Stage 35's known-pending memory-pressure estimate,
but compressed/sparse textures, driver overhead, and resources outside
the PRISMA graph or hosted client are not a complete total VRAM
accounting.

All existing `postfx` schema and experimental effect opt-in flags
are unchanged. No FPS/RTS-specific behavior is introduced into
PRISMA; hosted-client, screenshot and preview code remain with their
own owners.

**Verification status:** source inspection and GitHub commits only.
CMake, C++/Obj-C++, CTest, Metal/GL/Vulkan runtime, screenshot
capture callbacks, docking/graph-preview smoke, GPU lifetime
profiling and CI were **NOT RUN**, per user request. Refer to
`docs/handoffs/prisma-stage-36.md`.


## Stage 37 — generic GPU-fenced shaders, pipelines, buffers, FX hot reload

Stage 37 extends the GPU-completion lifetime policy from textures
and viewport resources to **all RHI handle categories used by
Shader Graph and VFX hot reload**.

`engine/render/GpuResourceRetirement.h` introduces a
genre-independent, graphics-owner-thread lifetime service with
overloads for `gfx::PipelineHandle`, `ShaderHandle`,
`BufferHandle`, `TextureHandle`, and `ComputeHandle`.
Each retired handle is detached from its old owner and stored with
the GPU submission token from `gfx::Device::gpuSubmissionFence()`.
A zero token is quarantined. A verified
`gpuCompletedFence()` allows nonblocking reclamation; explicit
`drainAfterGpuIdle()` requires a real GPU-idle guarantee.

When multiple GPU handles become eligible at the same time, the
service destroys pipelines before shaders, preserving dependencies.
It also supports `retireOwned(device,cleanup,knownBytes)` for
complex rendering-owner bundles whose internal destruction order
is already encapsulated. The callback is invoked **only after**
GPU completion on the graphics-owner thread, never from the
Metal completion-handler thread. The service tracks pending
resource-type counts, fenced/unfenced entries and optional
known bytes; these are lower bounds, not complete VRAM metrics.
It owns resources exclusively; do not hand it borrowed handles.

### Shader Graph Preview: whole-bundle retirement

The Shader Graph editor now retains the **entire previous**
`ShaderGraphPreview` object on a graph asset/project/domain change.
The archived object includes its Portrait/Renderer3D,
ParticleSystem, PRISMA, TextureCache, shaders, pipelines,
vertex buffers, and preview color/depth targets.
The new preview is created separately. The old preview's
`shutdownAfterGpuCompletion()` runs only when its last GPU
submission fence has actually completed (or at explicit GPU idle).
This avoids the former immediate pipeline/shader destruction while
a previous GPU frame may still reference those resources.

A UI-only change to the node `position` field does **not** invalidate
GPU shader preview resources; both the window and preview use a
shared `ShaderGraphPreview::resourceKey(graph)` that excludes node
screen positions. Actual changes to shader nodes, connections,
parameters or domain still produce a new GPU preview bundle.

### Editor FX and TextureCache hot reload

`fx::ParticleSystem` exposes the optional
`setGpuResourceRetirement(owner)` hook. When attached,
`reloadAll()` retires each previous shader-graph variant pipeline
and shader rather than directly calling `destroyPipeline` /
`destroyShader` while previous draws may be in flight. Its
normal `shutdown()` also retires owned particle buffers, shader
variants, pipelines, built-in atlas texture and reactive GPU resources.
The old immediate owner path remains for runtime clients without
a retirement owner.

`render::TextureCache` likewise has an optional
`setGpuResourceRetirement(owner)` hook. On reload/shutdown it
deduplicates cached and atlas-page texture handles, queues each
owned texture exactly once and preserves the default legacy
cleanup path when no retirement owner is attached.

The editor's main FX producer (`gameFx_` and `fxTextures_`) and
the separate FX preview window are opted into this service. Existing
rendering and ECS data are not modified.

### Editor lifecycle, memory pressure and UI

The `Editor` owns `GpuResourceRetirement` independently of its
windows; it collects GPU-completed resources on each pre-render
frame and drains at a confirmed GPU-idle pressure/shutdown point.
The Stage 35 pending-memory pressure estimate includes known
bytes of these resource entries. More than eight pending
whole-preview bundles is also a pressure signal for a **real**
pre-render GPU-idle attempt, not permission to assume completion.
In unsupported/failed-fence paths, resources are kept alive
rather than destroyed early.

The Statistics window now reports separately pending
pipelines, shaders, buffers, textures, owner bundles, and fenced /
unfenced counts. Bundles' underlying GPU memory is not fully known
so the budget remains an advisory minimum.

### Constraints

This does **not** automatically migrate every producer in the
engine: a native game, custom rendering module or plugin must opt
into a lifetime owner before destroying shaders/pipelines/buffers
mid-frame. The Shader Graph and editor FX integrations are concrete
paths. Multiple external GPU queues/devices require their own
synchronization and owners. A full graphics-device teardown
still requires an actual GPU idle guarantee; a pending callback
must not dereference an already destroyed `gfx::Device`.

The original `postfx` JSON, PRISMA visual effects, ECS gameplay,
water/VFX feature configurations and default-off experimental flags
remain unchanged.

**NOT RUN** at the user's direction: native builds, CTest,
Metal/GL/Vulkan driver work, screenshots, FX-preview hot-reload smoke,
VRAM profiling and CI. Source-level commits and symbol inspection do
not establish runtime correctness. See
`docs/handoffs/prisma-stage-37.md`.


## Stage 38 — additional Portrait owners, buffer metadata and GPU device-loss abandonment

The editor now uses the existing Stage 37
`render::GpuResourceRetirement::retireOwned()` API for additional
**complete Portrait renderer owners** in the Model and Materials
windows. Those windows previously stored an inline `game::Portrait`
and immediately ran its `shutdown()` when a panel was destroyed.
They now retain the entire old portrait (Renderer3D, generated
models, vertex/index buffers, shader pipelines, color/depth textures)
in a deferred graphics-owner bundle. Its cleanup runs only after
the relevant device's actual GPU completion fence or an explicit
host-confirmed GPU-idle drain. On window closure or loss of the
selected dock tab, the old portrait is retired; reopening creates a
new portrait without reusing the previous in-flight GPU handles.
The standalone model-viewer and other clients with different
ownership are not modified.

### RHI buffer allocation metadata

`gfx::BufferInfo` and the optional
`gfx::Device::bufferInfo(BufferHandle,BufferInfo&)` provide a
CPU-only live-resource query. The GL, Metal and Vulkan backends read
their own buffer tables without waiting for the GPU or readback.
The result contains `logicalBytes` and `versions` (dynamic buffer
copies can take extra GPU memory). These describe **logical buffer
allocation size**; Metal/Vulkan dynamic snapshots and GPU allocator
padding can exceed that amount.

`GpuResourceRetirement::retire(device,buffer)` now automatically
uses `bufferInfo` to estimate the retained bytes if the producer
has not supplied them. Retired textures similarly use actual
`textureInfo` width, height and pixel format. This improves the
known-pending-memory estimate for Editor FX graph hot reload,
model previews and texture caches. It is still not a precise total
GPU VRAM counter; shader/pipeline driver allocations and complex
preview owner bundles may have unknown size.

### Terminal device-loss handling

`GpuResourceRetirement::collect()` checks
`Device::gpuHealthy()`; if the device is lost it does **not**
call any `destroyShader`, `destroyPipeline`, `destroyBuffer`,
`destroyTexture` or `destroyCompute` functions. The new explicit
`abandonDevice(device)` removes pending raw RHI handles without
calling dead GPU APIs. Complete owner bundles are more dangerous:
destroying a captured `ShaderGraphPreview` object can transitively
call `Device` in its destructor. Their callbacks are moved to an
inert process-lifetime orphan sink with **no destructor**. This
intentionally retains that CPU memory until process exit rather
than risking use-after-free after terminal device loss.

A normal `GpuResourceRetirement` destructor also prevents unsafe
bundle callbacks from being run implicitly if the host did not
first drain at a verified GPU-idle point. The editor's shutdown
integrates `abandonDevice` when `gpuHealthy()` is false.
`ClientHost::abandonOnDeviceLoss()` provides a similar terminal
path for deferred hosted GameClient owners, which otherwise might
destroy nested renderers against a dead GPU Device. An archived
client's device must match the verified completion source.

The Statistics window exposes abandoned handles/bundles for
diagnostics. **Abandonment is an emergency device-loss/teardown
fallback, not ordinary memory reclamation**, and does not replace
restoring the device, recreating render targets or handling other
rendering subsystems' old ownership policies.

### Scope and limitations

The common GPU retirement service remains presentation-only,
genre-independent and opt-in for owners that can guarantee the
device is alive during normal collection. The Model and Materials
windows are additional concrete adopters; the Animation Graph
editor, external model viewer and other independent preview
owners remain on their own lifetime paths and should be migrated
via a similarly self-contained complete owner bundle.

No changes to ECS, FPS/RTS gameplay logic, postfx effect schema,
HDR shader output, water, or the default-off Motion Blur flag.

**NOT RUN:** CMake/C++/Obj-C++ builds, CTest, GL/Metal/Vulkan
runtime GPU fences, editor Model/Materials tab switching,
device-loss simulations, memory profiling, screenshots and CI
(per user instruction). Static source inspection is not proof
of runtime correctness. See `docs/handoffs/prisma-stage-38.md`.


## Stage 39 — Animation Graph/Rig preview bundles and stable editor state

The Animation Graph and Rig windows now share a presentation-only
**GPU-fence-deferred preview lifecycle** with the Shader Graph and
Model/Materials editors. In `apps/editor/animation/AnimationEditor.h`,
`AnimationEditorPanel::release()` detaches the old preview's *complete
owner bundle*: the uploaded `render::Model`, immutable compiled
`AnimationGraphInstance` state that may reference that model, and
the `game::Portrait` with its own Renderer3D, pipelines, textures
and color/depth render targets. The previous owner stays alive
in `GpuResourceRetirement::retireOwned` until its actual backend
GPU submission fence completes, or a host-confirmed GPU idle.
Its cleanup resets the graph **before** releasing the referenced
model's GPU buffers and releases the Portrait's GPU resources at
the same safe point. No editor ECS or animation simulation data
is exposed to the retirement manager.

Both `AnimationGraphWindow` and `AnimationRigWindow` call
`AnimationEditorPanel::suspendPreview()` on a hidden or closed dock
tab, retiring GPU memory without erasing the editor document.
When the tab becomes visible again, a new Model/Portrait is built
from the current document. `release()` intentionally preserves
`doc_`, `saved_`, undo/redo history, current selected node, graph
scope, the user-set playback toggle, ground height, yaw, zoom,
camera/canvas layout and animation-control values.

When the program is compiled again, numeric and vector preview
parameters with matching names are restored from the previous
`AnimationParameters`; new/removed parameters still use the
current graph's definitions. The compiled graph's *runtime clock*
and transient per-node state can restart after a model
reload/hidden-tab release — there is no unsafe transfer of
per-model graph pointers between models.

Pure visual graph node `position` changes no longer invalidate the
animation preview's compiled-program key (including nested
subgraphs). Dragging nodes does not needlessly restart the current
animation playback. The same stage fixes the previously authored
ShaderGraphPreview GPU-key loop to use the mutable JSON node
iterator (`itemsMut()`) instead of a const items view; this is
a source-level type-correctness fix pending actual compilation.

The independent `aiverse_modelview` Gallery has a different
lifecycle: its Portrait and Renderers are normally released when
the entire viewer shuts down, not on per-editor-tab hot reload.
Its shutdown now attempts `Device::waitForSubmittedGpuWork()`
before destroying Gallery/Portrait and imported model GPU
resources. That wait is a **shutdown-only** best-effort safeguard,
not a substitute for the platform's own required GPU-idle teardown
contract on unsupported backends.

No changes to the FPS/RTS gameplay ECS, animation graph JSON format,
animation algorithms, PRISMA effect settings, water rendering or
default-off MotionBlur feature flag.

**NOT RUN:** CMake, C++/Objective-C++ build, CTest, Metal/GL/Vulkan
GPU fence validation, tab hide/reopen, animator playback smoke,
ModelView screenshots, memory profiling, and CI. Source inspection
cannot certify actual GPU lifetime correctness.
See `docs/handoffs/prisma-stage-39.md`.

## Stage 40 — GPU-independent preview clock checkpoint (Animation Graph/Rig)

When an Animation Graph or Rig editor tab hides or the preview's uploaded
model is reloaded, GPU-fenced preview retirement from Stage 39 no longer
always restarts simple animation playback from zero.

`AnimationGraphInstance::snapshotPreviewClocks()` provides an optional
pointer-free `PreviewClockSnapshot` with the active state ID, its elapsed
time, clip node names and clocks, source clip identities/durations, state
IDs, node kind order, and the loaded model's skeleton names/parents.
`restorePreviewClocks(snapshot)` first validates all of those facts
against the newly compiled graph and refuses an invalid snapshot without
partially mutating the current instance. The editor *also* requires the
same model path and the same authored program key (which excludes
visual-only node position changes) before attempting to restore.

This is intentionally narrow: a graph with an in-progress transition
fade, subgraphs, native operators, rig state, motion matching, controllers,
traits, or other stateful nodes will restart rather than rebind potentially
model-dependent CPU state. Simple bind/clip/blend/additive/two-bone IK
graphs can restore state and clip time, assuming the graph program and
skeleton/clip identities are unchanged. Camera, document, controls,
playback toggle, and undo/redo remain as in Stage 39. Explicit New/Load/
Discard and Preview Reset clear the saved checkpoint; a changed graph
program or incompatible loaded model cannot consume it.

Files: `engine/render/AnimationGraph.h`,
`engine/render/AnimationGraph.cpp`,
`apps/editor/animation/AnimationEditor.h`.
This is only preview-presentation state; ECS gameplay, animation graph
JSON schema, Prisma GPU shader effects, SpectraFX and the GPU retirement
fence policy are unchanged.

**NOT RUN:** native compile, CTest, editor UI playback, Metal/GL/Vulkan
fence tests and CI (features-first direction). See
`docs/handoffs/prisma-stage-40.md` for committed source SHAs and next step.

## Stage 41 — memory residency by view, shared graph pools, and fenced idle-cache eviction

Stage 41 adds **GPU-free/read-only diagnostic calls** to the Prisma
presentation owner without changing shader outputs or game ECS state:

- `Prisma::memoryStats()`: reports active size-scoped framebuffer targets,
  depth attachments, live RenderGraph texture pools, view-private Auto Exposure
  history, persistent LUT/graph-image inputs and missing RHI metadata.
  `MemoryStats::liveKnownBytes` counts each actual size cache **once**,
  even when Scene and Game have identical width/height. Repeated per-view
  `ViewMemoryStats::sharedKnownBytes` is an explanatory *reference* to
  that cache, **not an additive view allocation**.
- `ViewMemoryStats` records the latest successfully allocated postprocessing
  scene-pass resolution for each stable view ID (Scene=1, Game=2 in the editor;
  native users may supply their own IDs). Its per-view `ownedHistoryKnownBytes`
  is private exposure history, while the RenderGraph pool belongs to `Sized`
  and may be shared by multiple view IDs.
- `PrismaRhiExecutor::GpuMemoryStats` now differentiates unknown **live**
  textures from unknown **pending** textures. Mips, multisample copies,
  compression, backend-alignment and shader/pipeline driver allocations are
  not inferable from the current `TextureInfo` interface: numbers are lower
  bounds, not a VRAM budget or GPU-residency certificate.
- `GpuResourceRetirement::Stats` separately exposes deferred owner bundles
  (including ShaderGraph/Portrait preview bundles) with **no reliable byte
  estimate**. A zero estimate never means a zero-cost bundle.
- `ViewportGpuRetirement` has a non-owning live-framebuffer registry for
  Scene/Game/FX windows. `Viewport::ensure()` publishes color+depth handles
  and size, and `Viewport::release()` unregisters them before fence-retiring
  the textures. `liveStats()` gives a lower-bound per-window memory reading;
  it does **not** grant ownership of these RHI handles.
- `StatsWindow` now shows active shared postprocessing cache bytes,
  private per-camera exposure bytes, Scene/Game/FX framebuffer bytes,
  stale/unreferenced caches, graph pending bytes and missing byte estimates,
  without summing repeated shared-per-view figures.

**Idle size cache lifetime:** `Prisma::retireView(id)` removes the stable
view's reference but does not destroy shared scene targets. At the end of
the editor's viewport work, `Prisma::retireUnreferencedSizeCaches()` scans
up to the small size cache and moves only sizes referenced by **no** live
view into `RetiredSized` using the graphics submission fence. Resources
are actually freed only in the existing nonblocking confirmed-GPU-completion
collector or an explicit verified GPU-idle path. When post-processing is
disabled for one camera, Editor now marks that view inactive and retires its
old reference; it no longer retains a stale cache just because the Game tab
continues to render without effects. This may cause fresh allocations on
reopen, trading cache warmth for bounded editor idle memory.

These diagnostics and retirement paths are opt-in/presentation-only; PRISMA
continues to own post-processing. FX emitters remain SpectraFX, and runtime
games not using the editor remain free to choose their own cache retention.
No gameplay/ECS components, FPS/RTS-specific camera modules or `postfx`
JSON schemas were changed.

**Verification:** GitHub source readback and static call-site inspection only.
**NOT RUN** (features-first): native builds, CTest, GPU fence/driver run,
Metal/GL/Vulkan/WebGL smoke, GUI tab-switch/resize and VRAM profiling. Never
interpret a missing metadata counter as a completed benchmark. Full path
and SHA inventory, dependencies and next steps:
`docs/handoffs/prisma-stage-41.md`.

## Stage 42 — opt-in HDR temporalResolve (full-resolution history reprojection)

This feature extends the existing **canonical Prisma pipeline**, not the
historical `PrismaScreenLighting` wrapper. The normal `postfx` catalog now
supports a `temporalResolve` effect, with four editor/MCP/volume-configurable
parameters. Existing effect IDs, `postfx` JSON roots, saved panel ID, and
PostFx compatibility headers remain unchanged.

To activate, the host must use **HDR/High** and opt in explicitly with
`AIVERSE_PRISMA_TEMPORAL=1`, in addition to an authored effect:

```json
{
  "postfx": {
    "effects": [
      {
        "type": "temporalResolve",
        "minQuality": "high",
        "historyWeight": 0.86,
        "depthThreshold": 0.0075,
        "motionScale": 0.04,
        "reactiveStrength": 0.95
      }
    ]
  }
}
```

`Prisma::resolve()` now reuses a **single full-resolution signed velocity
pass** for both Temporal Resolve and Motion Blur, if requested. Velocity is
current-minus-previous normalized UV and includes camera reprojection and
optional Renderer3D object/deformation velocity (RGBA16F, B presence bit);
invalid provider metadata falls back to camera velocity. The current-frame
RGBA8 reactive coverage is borrowed from Renderer3D/particle callbacks and
never retained as temporal history.

Each `viewId` owns two full-resolution **RGBA16F linear HDR** textures,
with color in RGB and **prior hardware depth** in alpha. The temporal shader
reprojects by signed motion vectors, explicitly reconstructs four previous
texel taps (nearest GPU sampling, separate depth checks), clamps accepted
history against a 3×3 current-color neighborhood in a compressed HDR domain,
then adjusts history weight by motion magnitude and reactive coverage.
Missing/invalid history writes the current unblurred scene and seeds the
next frame. Disoccluded pixels reject stale depth rather than using an
interpolated history alpha. HDR tone mapping, Bloom, LUT, and HUD happen
**after** the resolve. If opt-in Motion Blur is also active, it uses the
temporally resolved source. HDR FXAA is skipped when temporal resolve ran:
do not stack both AA approaches in one frame.

Automatic invalidation applies on resolution change and a large camera
translation (>12 presentation-world units between frames); explicit
`Prisma::resetMotionHistory(viewId)` also invalidates temporal history on
camera cuts and profile switches. `Prisma::retireView(viewId)` moves owned
history handles into the existing GPU-fenced retirement queue. Resource
diagnostics report per-camera temporal history texture counts and
lower-bound resident bytes, without charging them to the shared
size-keyed render target cache.

**What Stage 42 is not:** it does **not** implement dynamic-resolution
rendering, jittered subpixel supersampling, native TSR/FSR/DLSS upscaling,
optical-flow motion estimation, a history confidence/rejection G-buffer, or
proof of parity with the historical optional PrismaScreenLighting TAA.
This is an opt-in, full-resolution temporal color stabilization pass. Sky
pixels with no reconstructed surface depth are not accumulated, and
moving silhouettes can still need more robust velocity dilation/occlusion
masks. At 1080p, two RGBA16F histories plus one RGBA16F velocity target
consume at least ~47 MiB of raw base-level GPU memory per active resolution
plus shared depth/scratch targets; physical allocation can be higher.
Dual-camera views have **separate histories** even if target sizes match.

**Verification:** GitHub source readback and static wiring review only.
**NOT RUN:** CMake/C++/Metal build, CTest, GLES/Vulkan shader compile,
GPU screenshots, silhouette/transparent parity, VRAM profiling, camera
cut/resize runtime, mobile GPU benchmarks or CI. No shipping-readiness or
pixel-quality PASS is claimed. Full handoff:
`docs/handoffs/prisma-stage-42.md`.

## Stage 43 — optional subpixel jitter and confidence-aware temporal AA

Stage 43 builds on Stage 42's **canonical PRISMA temporalResolve** without
introducing a second TAA subsystem or rewriting stable `postfx`/MCP
identifiers. The native-resolution history remains two per-camera HDR
RGBA16F targets; no lower-resolution render target or temporal upscaler
is introduced.

The effect adds two optional float parameters:

- `jitterPixels`: 0..0.5 (default 0.5) controls the maximum subpixel
  projection offset. Jitter additionally requires
  `AIVERSE_PRISMA_JITTER=1` and the original
  `AIVERSE_PRISMA_TEMPORAL=1`, with HDR + High quality and an authored
  enabled `temporalResolve` effect. Existing games are unmodified
  without both explicit environment flags.
- `historyConfidence`: 0..1 (default 0.65) controls how aggressively
  previous HDR color is rejected near sharp luma/depth changes, in
  addition to the Stage 42 disocclusion depth/velocity/reactive rules.

**C++ camera contract:** Call `setViewId(id)`, then
`applyCameraVolumes(stack, camera.eye())`, then
`RTSCamera renderCamera = prisma.jitteredCamera(camera,w,h)` immediately
**before** `renderer.beginScene(renderCamera,w,h)`. `jitteredCamera`
returns a copy and never mutates `camera` itself: keep the original
`RTSCamera` for mouse picking, editor gizmos, UI, and touch input.
The standard GameClient, Editor Scene and Editor Game rendering paths
have been updated to follow this sequence. ShaderGraph, FX and native
preview owners may opt in separately. The real render projection uses
`RTSCamera::jitter`, and the renderer's shadow-camera strips jitter
already, avoiding artificial shadow-map projection shifts.

**Shader behavior:** Each view ID maintains its own 8-step Halton (2,3)
sequence. Jitter is expressed in NDC by the camera; the temporal shader
receives the current/previous jitter in **unflipped normalized UV**.
Each stable output pixel samples the current HDR color/depth/velocity at
`pixel + currentJitter`, then samples the prior history at
`reprojectedInputUV - previousJitter`. This removes projection jitter
from the history-addressing and from physical-motion confidence while
retaining distinct subpixel samples for accumulation. GLSL and Metal
use the same shader body, with the render-target Y flip applied only
by the existing `TUV` macro. A 3×3 current compressed-HDR color
neighborhood yields local mean/variance and bounds; the shader uses
independently depth-validated previous taps, color disagreement and
current depth-edge contrast as confidence multipliers. Moving
silhouettes and reactive particles therefore receive lower history
weight than smooth static surfaces.

On explicit camera cuts, projection type/FOV/near/far discontinuities,
history changes, jitter mode/amplitude switches or viewport size
changes, the temporal history restarts instead of blending
incompatible pixels. The existing camera-position cutoff still
applies. `PrismaStats` reports whether the last frame resolved
temporal color, whether jitter was active, and the applied offsets
in pixels; the **PRISMA — Постобработка** panel displays the values.

This is native-resolution **temporal antialiasing**, not
jittered temporal *upscaling*, DLSS/FSR/TSR, optical flow or
dynamic-resolution rendering. Transparent surfaces and screenspace
effects may still alias if their producers do not supply suitable
reactive or motion masks. `PrismaScreenLighting` contains an older
optional independent TAA; do not activate both histories at once.
The history and GPU-referenced resources remain subject to
GPU-fence retirement from Stages 35–42.

**Verification:** GitHub source inspection and structural readback
only. CMake/native C++ and GLSL/MSL shader compilation, CTest,
GL/Metal/Vulkan/Android GLES runtime, camera/UI parity, multiple
viewports, memory profiling and screenshots are **NOT RUN** by the
ongoing features-first direction. No measurable image-quality
improvement or production-readiness is claimed until those tests.
The exact source commits, APIs and next steps are recorded in
`docs/handoffs/prisma-stage-43.md`.

## Stage 44 — explicit internal render resolution and temporal upscaling

Stage 44 extends the canonical opt-in `temporalResolve` / Halton TAA
path with a true **internal 3D render resolution** that may differ from
the **final output target**. It adds two catalog parameters to the
existing `postfx.effects[{type:"temporalResolve"}]` payload:

- `renderScale` (0.5..1, default **1**): fraction of the output width and
  height used by 3D, depth and motion-vector passes. The chosen scale is
  quantized to 1/16 steps; actual pixel counts are rounded, bounded to
  32px minimum and require at least 64px output dimensions.
- `reconstructionSharpness` (0..0.5, default **0.15**): mild color
  reconstruction/sharpening within the compressed-HDR current-frame
  neighborhood before confidence-weighted history accumulation.

The feature is **disabled unless all three flags are explicitly set**:
`AIVERSE_PRISMA_TEMPORAL=1`, `AIVERSE_PRISMA_JITTER=1` and
`AIVERSE_PRISMA_UPSCALE=1`. An enabled `temporalResolve` authored in
the normal game/map `postfx` catalog, HDR support, High quality,
`historyWeight>0`, `jitterPixels>0` and `renderScale<1` are also
required. Existing projects and low/medium/mobile quality tiers retain
1:1 rendering by default.

**Host call order:** after `setViewId(id)` and
`applyCameraVolumes(stack,camera.eye())`, call
`auto [renderW,renderH] = prisma.renderSizeForOutput(outputW,outputH)`
and then `prisma.jitteredCamera(camera,renderW,renderH)`. Pass this
camera and the smaller dimensions to `Renderer3D::beginScene` but
give `Renderer3D::render` the unchanged **full-resolution** destination
pass. Draw 3D and particle reactive/motion contributions at the smaller
size. `Renderer3D::resolvePrisma` sends the current viewProjection and
internal dimensions to Prisma, which reads final output dimensions from
the RHI output texture, the host's per-view size declaration, or the
swapchain framebuffer. Draw HUD, labels, picking/gizmos and UI using the
original camera/full output dimensions. Both Editor Scene/Game and
GameClient now use these calls.

The main HDR path now performs shutter blur, DoF and other internal scene
effects at the low render resolution; Bloom and sun-shaft buffers remain
low-resolution inputs. The **temporal pass runs immediately before final
color composite** and writes a `viewId`-private **output-resolution**
RGBA16F history (RGB linear HDR, A prior hardware depth). Temporal
samplers use separate reciprocal texel sizes: `inputPx` for current
scene color/depth neighborhoods, `px` for output-history bilinear
coordinates. Motion vectors remain current-minus-previous in normalized
screen UV, so their coordinates do not require ad-hoc multiplication by
the resolution ratio. The shader applies Stage 43 jitter correction,
four individually depth-validated previous history taps, current
luma/depth confidence, reactive coverage and bounded HDR sharpening.
Final tonemap/composite and optional full-resolution UI remain output-size.
HD/SDR FXAA is not layered over a successful HDR temporal resolve.

Per-view history invalidates/retires on changes to output **or input**
dimensions, camera cuts, projection changes or effect quality. Old
targets go through the existing GPU-fence retirement rather than
immediate destruction. `PrismaStats` reports `width,height` (internal),
`outputWidth,outputHeight` (presentation) and
`temporalUpscaled` (successful spatial/temporal resolution step), which
are displayed in the Prisma editor window. `MemoryStats` already counts
private temporal history bytes independently of size-shared scene
buffers; when upscaling it counts the two **larger output histories**.

**Fallback:** GPU output-history/depth allocation or temporal pipeline
failure does not erase the source image: the final composite samples
the internal-size scene across the full framebuffer (ordinary bilinear
enlargement). If this happens after a jittered frame has begun,
single-frame instability may still be visible; this path needs native
runtime validation and potentially earlier allocation preflight.
The previous optional `PrismaScreenLighting` temporal system is not
automatically merged or compatible with simultaneous use.

This is an experimental **fixed-resolution-ratio temporal upscaler**,
not variable-rate dynamic resolution, FSR/DLSS/TSR equivalence, optical
flow, a reactive-mask G-buffer for all translucent content, or proof of
visual/performance quality. Source-level cost estimation is heuristic;
actual GPU cost, bandwidth, ghosting and savings are unmeasured here.

**Checks actually run:** GitHub source readback and 39/39 static call-site
presence checks, including temporal-after-Bloom ordering, output-sized
history/scratch target and Scene/Game/GameClient scaled beginScene.
**NOT RUN:** CMake/C++/Metal/GLSL build, CTest, Metal/GL/GLES/Vulkan GPU
execution, screenshot parity, memory, motion-vector/AA quality or
performance profiling and CI. Stage-specific handoff:
`docs/handoffs/prisma-stage-44.md`.

## Stage 45 — GPU-sampled adaptive resolution and pre-render stable fallback

This stage extends the **opt-in** Stage 44 source/output temporal scale
path. It does not change any defaults or the `postfx` JSON root, existing
MCP IDs, editor tab ID, scene shaders or genre-specific simulation.

**New effect controls** in the existing `temporalResolve` catalog:
`adaptiveResolution` (boolean, defaults false), `targetGpuMs`
(4..50ms, default 16.6) and `minRenderScale` (0.5..1, default 0.5).
The authored Stage 44 `renderScale` remains the *quality ceiling*
and cannot be exceeded by the adaptive controller. To opt in, set
`AIVERSE_PRISMA_ADAPTIVE=1` **in addition to** all three Stage 44 flags
(`AIVERSE_PRISMA_TEMPORAL=1`, `AIVERSE_PRISMA_JITTER=1`,
`AIVERSE_PRISMA_UPSCALE=1`), choose HDR/High and author an enabled
`temporalResolve` effect with `adaptiveResolution:true`.

`PrismaAdaptiveResolution` is a CPU-only, genre-independent policy
in `engine/render/PrismaAdaptiveResolution.h`. It consumes *only* finite,
positive, asynchronously completed `Device::gpuFrameMs()` readings
from **new, strictly increasing** `Device::gpuCompletedFence()` tokens.
A backend without reliable results (e.g. unsupported GL/Vulkan) stays
at the authored scale; the policy never substitutes editor CPU frame
time, FPS, frame serials or predicted GPU timings. On Metal the signal
measures the **whole GPU command frame**, not an isolated Prisma pass
or editor view; adapting multiple editor views to the same whole-frame
measurement is conservative and can shift quality in both.

Hysteresis: GPU duration above 115% target for 12 distinct completed
samples causes a 1/16 renderScale decrease; GPU duration below 80%
target for 36 samples causes a 1/16 increase, up to authored ceiling.
A changed scale starts 48 completed samples of cooldown, reducing
rapid resize/reallocation loops. The floor and ceiling are clamped
and quantized; `Prisma::retireView` removes per-view controller
state when the viewport is closed.

**Pre-render resource preflight:** `renderSizeForOutput()` on the
graphics thread now calls `preflightUpscale()` before returning any
internal resolution smaller than the output. It checks the temporal
and motion GPU programs, the guaranteed fallback composite, the HDR
RGBA16F size-owned scene/velocity textures, internal scene/depth/scratch,
the camera-private pair of output-size HDR temporal histories and the
output-size scratch depth. History reallocations use existing
GPU-fenced retirement. A failed preflight returns **full input/output
dimensions** and inhibits jitter for that view. Allocation retries
are rate-limited (60 render-size queries) so an unsupported or
memory-constrained device is not hammered each frame. No GPU wait
is introduced in this path.

**Jitter-safe runtime fallback:** if a temporal pass unexpectedly
cannot run after a low-resolution jittered scene was already encoded,
the final compositor uses `graphFrame.zw` as a current-jitter
sampling offset `s + currentJitter` for source color and dependent
screen effects. The offset is zero during a successful temporal
resolve or normal non-jittered presentation. That keeps the
fallback image on a stable output pixel grid while still allowing
linear bilinear upscaling and HDR color processing. This is a visual
stabilization measure, not a guarantee of production image quality
or a replacement for real temporal accumulation. HUD and input
remain full-size and unjittered.

`PrismaStats` adds `adaptiveResolution`,
`requestedRenderScale` and `temporalFallback`; the existing
PRISMA editor panel displays them alongside output/internal
resolutions and optional asynchronously completed **whole-frame**
GPU milliseconds. No precise GPU time is shown if unsupported.

**Limitations:** backend timing may be absent; whole-frame timing
includes other GPU jobs; the fallback can still exhibit simple
bilinear aliasing and thin-edge shimmer; a mid-frame device loss
cannot be recovered with texture sampling; cached allocation
pressure may temporarily rise around scale transitions until
actual GPU-completed fences allow reclamation. Adaptive scaling
does not synthesize true per-viewport GPU timing nor promise a
frame-rate gain. Motion-vector reprojection, jitter sign and
GLSL/MSL compilation still need native GPU validation.

**Performed:** direct GitHub source edits and source readback,
including **38/38 static presence checks** for the adaptive/preflight/
shader-fallback wiring. These checks are not a native compile or GPU run. **NOT RUN:** CMake,
C++/Obj-C++ compile, CTest, Metal/GL/GLES/Vulkan runtime,
screenshot/PSNR/ghosting, GPU timing experiments, VRAM profiling,
device-loss tests or CI. For implementation and next stage see
`docs/handoffs/prisma-stage-45.md`.

## Stage 46 — actual scoped GL33 viewport GPU timings and VRAM-pressure guard

**The timing distinction matters:** RHI `gfx::Device` now has optional
nonblocking `beginGpuScope(tag)`/`endGpuScope()` and a separate
`takeGpuScopeTimings()` result queue. Unlike `gpuFrameMs()` and
`timeGpuFrame()`, a scope measures only GPU commands submitted between
its boundaries. Desktop **OpenGL 3.3** on supported macOS/Windows paths
uses a real `GL_TIME_ELAPSED` query. The backend refuses to start a scope
while a whole-frame elapsed query or another scope is running (OpenGL
does not allow nested same-target queries). `takeGpuScopeTimings()`
only returns completed, nonblocking results and does not consume other
`takeGpuTimings()` benchmark results. The bounded queue is capped at
64 pending entries. GL ES/WebGL/Linux fallback, Metal and Vulkan do
**not** claim per-viewport timings: the default API returns false/empty.
Metal's `gpuFrameMs()` is whole-command-buffer duration and must always
be labeled as such.

Editor opt-in: `AIVERSE_PRISMA_VIEWPORT_GPU_TIMING=1`. For Scene/Game,
the editor begins a GPU timing scope immediately before the graphics
`Renderer3D::render` and ends it after `Renderer3D::resolvePrisma`
and the last viewport pass. This covers rendered shadows, world,
post-effects and scene FX in this interval, **not** prior CPU scene
preparation, prior GPU asset streaming or other windows. When supported,
the editor polls finished measurements at its pre-render safe boundary,
passes them to `Prisma::acceptViewGpuTiming(viewId,gpuMs)` and lists
per-viewport samples through `Prisma::viewGpuTimings()`. The source
is presentation/diagnostic only, not the authoritative simulation or
an ECS component. It is opt-in and never enables full-frame timing
automatically.

**Adaptive source selection:** the existing `PrismaAdaptiveResolution`
controller now prefers the latest completed scoped GL query for its
particular view. Each accepted scoped query increments that view's
completed-sample sequence; a repeat value cannot cause repeated
hysteresis updates. If no scoped query is available, the controller
retains the earlier whole-GPU-frame fallback gated by the genuine
`Device::gpuCompletedFence()`. It does **not** convert CPU FPS to GPU
milliseconds. Scoped interval duration still includes shared GPU
shadow/reflection/FX passes and is not a pure postprocess-only cost.
On unsupported backends this scope-specific diagnostic is explicitly
unavailable.

**Bounded known-memory pressure:** `temporalResolve` gains
`maxPrismaMemoryMiB` (32..4096, default 256 MiB) as an opt-in
adaptive-resolution pressure threshold. Only when the Stage 45
`adaptiveResolution` effect and `AIVERSE_PRISMA_ADAPTIVE=1` are
active does the policy compare the threshold against
`Prisma::memoryStats().liveKnownBytes +
Prisma::retirementStats().knownPendingBytes`. These estimates count
shared size-cache allocations **once**, add active per-view HDR
histories and pending fence-retired textures exactly once, and
**exclude** unknown/unobservable bytes and physical driver overhead.
A known lower-bound overage counts as a pressure sample only on a
**new confirmed GPU completion** (actual completed scope query or
GPU fence). After 12 over-budget samples it may step scale **down**
1/16. Over-budget pressure blocks upward quality steps; normal
fast/slow GPU timing and the existing 48-completed-sample cooldown
continue to apply. Pressure alone does not allow unsafe GPU release,
unconditional memory waits, or repeated CPU-frame counting.
Without a verified GPU completion, no adaptation is attempted.

`PrismaStats` and the editor report the chosen timing source,
last valid GPU milliseconds, known Prisma live+pending bytes,
the configured threshold and whether it is exceeded. The Stats
window also displays the recent completed GL33 Scene/Game
intervals, rather than mislabeling Metal whole-frame timings as
isolated view costs.

**Caveats:** `gpuFrameMs` and GL elapsed queries are GPU-timeline
intervals, not identical to busy-time; scopes can include expensive
shared renderer passes and do not measure earlier resource uploads.
Using a shared whole-frame GPU measurement for two simultaneously
active editor views is inherently conservative. The memory threshold
is not total physical VRAM or a hard memory cap, and unmeasurable
ShaderGraph/Portrait/driver allocations remain unknown. Over-budget
known bytes may stay high until actual fence completion; bounded
frame hysteresis prevents per-frame thrashing, but is not a promise
of performance gain. A backend's unavailable query must not cause a
false zero-time result or stale scope escalation.

**Verification performed:** GitHub source readback plus
**42/42 structural checks**, including per-viewport scope begin/end
ordering, GL33 query exclusivity, separate benchmark queues, per-view
sample input, safe memory budget accounting and editor reporting.
These checks are source-presence/ordering checks ONLY; see
`docs/handoffs/prisma-stage-46.md`.
**NOT RUN:** native C++/Metal/GLSL compilation, CTest, actual GL33
GPU scope queries, Metal/GLES/Vulkan/WebGL smoke, frame budget/
VRAM profiling, GPU-loss/queue saturation and CI. No production-ready
claim is made until those checks are performed.

## Stage 47 — Metal hardware-scoped timings, renderer-owned memory and trustworthy source switching

**Metal GPU scope implementation:** `gfx::Device::beginGpuScope`,
`endGpuScope` and `takeGpuScopeTimings` now have a real optional
`engine/gfx/metal/GfxMetal.mm` backend for compatible devices. It
checks available `MTLDevice.counterSets` for timestamp counters and
requires support for blit-boundary or stage-boundary counter sampling.
It uses **two** samples per viewport scope in a
`MTLCounterSampleBuffer`, encoded into lightweight blit encoders
*inside the same Metal command buffer as the actual Scene/Game work*.
Command-buffer submission/presentation, frame semaphores and GPU fences
are unchanged.

Metal counters do not necessarily run in CPU nanoseconds. A timing
sample is only accepted after the actual command buffer reaches
`MTLCommandBufferStatusCompleted` and
`resolveCounterRange(NSMakeRange(0,2))` returns both valid timestamp
values. The backend obtains correlated CPU/GPU clock pairs using
`[device sampleTimestamps:&cpuTimestamp gpuTimestamp:&gpuTimestamp]`
before encoding and after completion. It converts the GPU timestamp
interval to CPU-clock nanoseconds by interpolating those clock pairs.
Zero, error sentinel, nonmonotonic, nonfinite and out-of-range results
are **not** published as valid time. Results are polled nonblockingly;
pending scopes are capped at 64. Hardware without supported counter
sampling or a valid sample buffer reports unsupported, with no
fabricated scoped GPU time. On desktop GL33 the previous real scoped
elapsed GPU-query path remains. Vulkan/GLES/WebGL have not gained
scoped timers; `Device::gpuFrameMs()` remains a **whole-frame** metric
whenever used as an adaptive fallback, never mislabeled per viewport.

Editor Scene/Game still use the existing opt-in
`AIVERSE_PRISMA_VIEWPORT_GPU_TIMING=1`, with bounded scopes around
GPU rendering and Prisma postresolve; the editor's pre-render poll
reads only completed scope results. This excludes GPU work submitted
outside the scope (such as previous texture uploads), and can include
shared light/reflection passes. Scope durations do not sum to total
GPU busy time.

**New GPU memory lower bound:**
`Renderer3D::residentMemoryStats()` uses read-only RHI
`TextureInfo` and `BufferInfo` to count *unique* directly owned
texture/buffer handles. Included: sun shadow map, renderer's owned
selection texture, scene-color capture caches (including depth), trace
placeholder textures, own model placeholder textures, per-camera
object-motion/depth/reactive textures, and directly owned line/
selection/blit/indirect buffers. Scene cache aliases
`sceneColor_/sceneDepth_` are **not** counted twice. Deferred object
motion textures are reported separately as
`pendingMotionKnownBytes`. Dynamic buffer `versions` are noted,
but their physical allocator copies cannot be inferred. Compressed
texture formats without enough metadata are reported as unknown,
not guessed. External `Model/Material` resources,
`LightSystem` reflection/spot targets, shared persistent scene/
geometry pools, shader/pipeline driver allocations, and Prisma
resources are **not** claimed by this reporter.

**Integration:** `Prisma::setExternalRendererGpuMemory(live,pending)`
is an optional presentation-only host input. Editor Scene/Game and
GameClient now invoke `Renderer3D::residentMemoryStats()` and pass
its live texture+buffer known bytes and separately pending motion
bytes to the same Prisma instance **before**
`renderSizeForOutput`. The existing serialized
`temporalResolve.maxPrismaMemoryMiB` key remains unchanged for
compatibility, but its pressure comparison now includes the
nonoverlapping **known lower bounds of Prisma plus that directly
owned Renderer3D subset**, active and pending. This still does not
cover total GPU VRAM, physical device allocation, mips, temporary
backbuffer or unknown owners. It does **not** force GPU idle or
premature texture destruction; Stage 35–46 GPU-fenced retirement
rules remain in control.

**Correctness of timing-source switches:** a completed Metal/GL
scope-query sequence is not comparable with a completed GPU-frame
fence token. Stage 47 tracks these as distinct sample domains,
retaining render quality and cooldown but resetting previous token
and short-term counters whenever the source changes. A scoped
sample older than eight newly completed GPU frames is ignored so
an indefinitely hidden/unsupported view does not block returning
to a valid whole-frame timing source. No stale scope sample is
counted repeatedly as a fresh GPU completion. Neither CPU
frame time nor estimated FPS is used as a substitute.

**UI:** the Prisma window labels the adaptive timing as completed
Metal/GL33 viewport counters or whole-frame GPU fallback and shows
the combined Prisma + directly owned Renderer3D known-memory
threshold. The Stats window lists Renderer3D live texture/buffer
lower bounds, scene-capture/motion subtotals, pending object motion
and unknown allocations separately from Prisma so an owner is
not counted twice.

**Verification:** source inspection and **53/53 structural
source-presence checks**, including bounded Metal GPU scopes, separate
timing queues, GPU-clock calibration, renderer-owner memory telemetry,
deduplicated resource handling, source freshness/domain switching and
host API wiring. This is not a compiler or GPU validation result.
**NOT RUN:** native Obj-C++/Metal compile, CTest, Metal counter
calibration on Apple Silicon/Intel Mac/iOS, GL33 timing capture,
GPU memory pressure and screenshot tests. Apple counter API
details were checked against current Metal documentation, but that
is not proof of this backend's runtime support. See
`docs/handoffs/prisma-stage-47.md` for source commits, unknowns
and exact next steps. PR #26 stays Draft; main unchanged.

## Stage 48 — GPU allocation reporting across LightSystem, persistent GPU scene and shared geometry pages

Stage 48 expands the experimental adaptive-resolution memory-pressure
input from `Prisma` and directly-owned `Renderer3D` resources
(Stage 47) to **independently owned lighting and rendering submodules**
and the **global pooled geometry pages**, without changing the
existing `postfx` JSON root or any ECS/simulation behavior.

The new header `engine/render/GpuOwnedMemory.h` supplies a reusable
read-only `GpuOwnedMemoryCounter` using RHI
`Device::textureInfo` / `Device::bufferInfo`. It de-duplicates
texture and buffer handles by their typed ID *within an owner*, reports
texture/buffer known lower-bound byte totals, known allocation counts,
unknown formats/metadata counts and dynamic multi-version buffer
warnings. Unlike a GPU allocator, it never calls create/destroy,
map, GPU wait, readback or query. It uses saturated byte accumulation
and never equates a missing texture's bytes to real zero physical VRAM.
Compressed formats without exact allocation metadata are left unknown.

New or expanded direct-owner reporting contracts:

- `LightSystem::ownedGpuMemoryStats()` includes its own light map,
  depth attachment, static light atlas (RGBA32F), spot shadow, planar
  reflection color/depth, tiny placeholder texture and dynamic
  light/halo/beam/decal buffers, without touching `Renderer3D`'s
  independently owned sun shadow map
- `PersistentGpuSceneUpload::ownedGpuMemoryStats()` reports GPU
  staging buffers from all three in-flight upload slots, not CPU
  mirror contents
- `GpuSceneCulling::ownedGpuMemoryStats(device)` reports all owned
  three-frame visibility/compute outputs
- `GpuSceneDrawCommands::ownedGpuMemoryStats(device)` enumerates
  GPU-authored input/output/compacted/visible-count buffers for all
  retained submissions and all three GPU in-flight slots
- `Renderer3D::residentMemoryStats()` now aggregates those mutually
  exclusive buckets with its Stage 47 directly-owned sun shadow,
  selection/trace placeholders, scene capture and object-motion/owned
  core buffers. It preserves the categories
  `lightSystemKnownBytes`, `persistentUploadKnownBytes`,
  `persistentCullKnownBytes`, and `gpuAuthoredKnownBytes`. Old
  capture/motion subcategories and pending motion totals remain
  available; pending resources are not added as live GPU resident
  bytes

The shared `GlobalGeometryPool` is **outside** the Renderer3D
ownership tree: `geometryPoolStats(device).allocatedBytes` counts
each allocated shared geometry page once per `gfx::Device`,
regardless of how many Models or primitives reference that page.
These pages are intentionally not part of Renderer3D's direct
buffer enumeration and must not be added per Model. Unpooled
dedicated model buffers and externally owned material textures
are still omitted: they need their actual owner to enumerate
live allocations to avoid duplicates.

**Host integration:** prior to calling
`Prisma::renderSizeForOutput(outputW,outputH)`, both editor
Scene/Game and native GameClient pass two independent memory sources:
`Prisma::setExternalRendererGpuMemory(rendererOwned.liveTextureKnownBytes
+ rendererOwned.liveBufferKnownBytes,
rendererOwned.pendingMotionKnownBytes)` and
`Prisma::setExternalGeometryPoolGpuMemory(geometryPoolStats(device).allocatedBytes)`.
The original `temporalResolve.maxPrismaMemoryMiB` name and API are
backward compatible but the known-memory pressure calculation now
uses a saturating sum of **Prisma live+pending**, **Renderer3D directly
owned + LightSystem + persistent upload/culling/draw buffers live +
pending motion**, and **GlobalGeometryPool live allocated pages**.
It remains a lower bound and never causes unsafe GPU resource
destruction; actual recovery always waits for the previously designed
GPU-fenced ownership policy.

The editor's Stats panel separates the Renderer3D owner total into
light, upload, culling and authored-draw categories and shows the
shared geometry pool only once. The PRISMA window explicitly labels
its displayed adaptive memory value as the combined known minimum
rather than complete VRAM. CPU preparation and draw-call batch
planning remain unchanged.

**Known limitations / nonclaims:** device physical VRAM, driver
alignment/mips, Metal dynamic buffer versions, shaders/pipelines,
unpooled model/material resources, external scene render modules,
offscreen capture bundles and other applications' GPU usage are
not included. Renderer/LightSystem/Persistent owners are separate
and do not double count shared geometry buffers. This work does
not add a global GPU heap allocator, physical budget query or
automatic GPU memory eviction; the `maxPrismaMemoryMiB` parameter
is a conservative, optional quality-controller threshold, not a
strict memory cap.

**Feature sequencing:** further owner integration and functional
completeness before native compilation/CTest/GPU validation, per
user requirement. This stage involved code and documentation,
plus structure-only source inspection; the native tests remain
deferred. See `docs/handoffs/prisma-stage-48.md` for SHA,
touched files, actual checks and next functionality.

## Stage 49 — dedicated model/material ownership and priority-aware camera quality admission

This feature stage continues the policy **features first, native tests
after functional completion**. It adds GPU-memory reporting for
dedicated, non-pooled Model GPU allocations and their actual
texture-cache owners. It also introduces per-view HDR history
admission and priority-aware memory-pressure resolution.

**Model GPU ownership:** New read-only
`appendModelOwnedGpuMemory(const Model&,GpuOwnedMemoryCounter&)`
enumerates Model's own `vb`, `ib` only if
`Model.geometryPoolPage==nullptr`, plus `morphTex`,
`Model::Baked.tex` and each `Image.tex` whose
`borrowedTexture==false`. That mirrors `releaseModel()`, which
destroys standalone buffers and nonborrowed images while leaving
shared pool/borrowed images to their owners. Each distinct typed RHI
handle is counted once through `GpuOwnedMemoryCounter`.
`ModelTextureCache::appendOwnedGpuMemory(counter)` adds its cached
PBR/material texture handles **as the actual owner**, never through
a borrowed `Model::Image` reference.
`RtsView::ownedModelGpuMemoryStats(device)` enumerates each unique
cached `Model` plus the shared model texture cache, without
multiplying allocations by the number of visible entities or
material references. Shared `GlobalGeometryPool` pages remain
accounted ONLY through `geometryPoolStats(device).allocatedBytes`.

The generic `TextureCache::ownedGpuMemoryStats()` reports its own
atlas-page, generated and standalone sprite GPU textures,
deduplicating any page also aliased by a texture cache entry.
Retired textures stay in the GPU-fenced retirement owner rather
than being called live. Other modules may expose equally disjoint
`GpuOwnedMemoryStats` to the host. Compressed formats without
sufficient RHI size metadata and physical Metal buffer/mip copies
still report unknown/underestimated bytes; this is not physical VRAM.

**Prisma host memory API:** `setExternalPresentationAssetsGpuMemory`
accepts a single lower-bound total for **unique** external
presentation model/material/FX sprite owners. Editor Scene/Game
combine `view_.ownedModelGpuMemoryStats` with the independent
`fxTextures_.ownedGpuMemoryStats()` using saturated arithmetic.
Native GameClient submits its RtsView model/material inventory.
`setExternalRendererGpuMemory` and
`setExternalGeometryPoolGpuMemory` remain separate. The
`temporalResolve.maxPrismaMemoryMiB` authored key is unchanged,
but its conservative memory threshold now includes Prisma itself,
Renderer3D/lighting/persistent subowners, global geometry pages,
and the disjoint presentation asset inventory. It is still
**known lower-bound bytes**, never a strict GPU heap budget or a
device-wide capacity query.

**Per-camera policy API** in `Prisma.h`:

```cpp
prisma.setViewQualityPolicy(viewId, {priority, maxTemporalHistoryMiB});
prisma.clearViewQualityPolicy(viewId);
```

The policy is independent of FPS/RTS gameplay, UI input and GPU
resource ownership. Priority is an integer from 0 (yield first)
to 255 (yield last), with default 128. Under **known GPU memory
pressure alone**, the hysteresis controller requires 6 completed
samples for low priority (0..84), 12 for normal (85..169)
and 30 for high (170..255) before a downward 1/16-scale step.
A genuinely over-budget GPU frame time still causes a downgrade
after the existing 12 completed samples for **every** priority,
and fast recovery needs 36 valid samples. Each resize has its
existing 48-completion cooldown, avoiding oscillation.

`maxTemporalHistoryMiB==0` means no camera-specific admission cap.
A positive value limits ONLY the two per-view full-output
RGBA16F histories: `2*outputWidth*outputHeight*8` bytes.
Before any scene is jittered, `renderSizeForOutput` rejects
an upscale whose required output-history size exceeds that view's
quota, returns full native render dimensions and fence-retires
previous history allocations. Rejection is propagated through
`jitteredCamera` AND `Prisma::resolve`; the latter must not
allocate histories that were refused during admission. Thus
a rejected view presents ordinary full-resolution output with
stable camera projection, not a jittered scene without a valid
temporal pass. `PrismaStats` reports priority and admission status.
`retireView` erases the policy and rejected flag, preventing
stale hidden camera settings from leaking into a replacement view.

Default **host choices** in this stage: Editor Scene priority 64
with 128 MiB for the pair of HDR histories; editor Game and
GameClient use priority 224 with 256 MiB history cap.
Other native project hosts may supply custom policies. The
global memory-pressure budget, the per-view history admission
limit and runtime GPU-scoped timings each control separate
decisions and must not be conflated.

**Implemented, not validated:** code, integration, agent skill and
GitHub readback. **NOT RUN:** CMake/native compiler, Obj-C++ Metal
compilation, shader validation, CTest, GPU timings, VRAM snapshots,
view priority/thrashing performance experiments or CI.
`docs/handoffs/prisma-stage-49.md` documents changed commits,
real structural inspection and the remaining functional coverage
before the separate tests stage.

## Stage 50 — external GPU owner memory integration and safe native-resolution TAA admission

The project remains in **features-first implementation**, with native
compilation and GPU testing deliberately deferred until the complete
functional feature backlog has been closed. Stage 50 adds
independently-owned SpectraFX, terrain/planet and streamed WorldScene
GPU telemetry to the existing Prisma quality/known-memory budget.
It also fixes a temporal allocation/fallback asymmetry from Stage 49.

**SpectraFX:** `ParticleSystem::ownedGpuMemoryStats()` counts only
its own particle vertex and index buffers and builtin RGBA atlas,
using `GpuOwnedMemoryCounter` on the existing RHI metadata.
Particle graph textures, imported effect sprites and custom meshes
are **borrowed**, since they are supplied through loader callbacks
and may be shared by a separate texture/mesh owner. The native
`ClientFx::ownedGpuMemoryStats()` combines its private
`ParticleSystem` inventory with its own independently resident
`TextureCache` sprite/atlas pages. In the editor, the equivalent
buffers and texture cache are enumerated independently through
`gameFx_` and `fxTextures_` and added once; Prisma does
not take ownership or retire them.

**Terrain and planets:** `TerrainRenderer::ownedGpuMemoryStats()`
reports all resident chunk/LOD VBO/IBOs, apron and water geometry,
per-cell, dynamic, splat, vision, heightfield and detail
normal/splat render textures, plus the owner-private terrain
material/cliff `TextureCache`. The `TextureRef` objects bound
by terrain shaders are aliases of those cache-owned handles and
are not counted separately. `PlanetRenderer::ownedGpuMemoryStats()`
similarly counts the planet chunks, per-body visibility and detail
atlases, globe/atmosphere meshes, placeholders and its distinct
material texture cache. `RtsView::ownedPresentationGpuMemoryStats`
combines the unique Model/ModelTextureCache inventory from Stage 49
with terrain and planets, **including allocations that remain live
while the presentation is temporarily hidden**. This is an owner
report, not a trigger for GPU eviction.

**Streamed world:** `WorldScene::ownedGpuMemoryStats(device)`
visits current resident chunks and passes each shared/unshared
model through `appendModelOwnedGpuMemory` with one deduplicating
counter. Multiple chunks referring to the same shared Model or
GPU handle count it only once; GeometryPool pages are excluded
and remain accounted through `GlobalGeometryPool`. The external
`ModelTextureCache` can be supplied by another owner, e.g.
GameClient passes `view_.textureCache()` to its streamed world.
Therefore `WorldScene` deliberately does not enumerate that
borrowed cache a second time.

**Per-host integration:** Editor Scene/Game now combine
`view_.ownedPresentationGpuMemoryStats(*dev_)`,
`gameFx_.ownedGpuMemoryStats()` and the separate
`fxTextures_.ownedGpuMemoryStats()` with saturated known-byte
addition. Native GameClient adds the presentation view,
`fx_.ownedGpuMemoryStats()` and optional resident
`worldScene_->ownedGpuMemoryStats(dev)`. Both pass the result
using the existing generic
`setExternalPresentationAssetsGpuMemory` before
`renderSizeForOutput`. Renderer3D/LightSystem/persistent
GPU buffers and globally shared geometry page bytes remain
supplied in their own disjoint host calls. The displayed
`temporalResolve.maxPrismaMemoryMiB` value remains an
**estimated lower-bound quality threshold, not physical
device total VRAM, an allocator budget or a guaranteed peak**.

**Native TAA safety:** Stage 49 introduced a per-camera
`maxTemporalHistoryMiB` limit for two RGBA16F HDR histories,
but only checked it after the temporal-upscaling flags were
accepted. Stage 50 moves this admission test immediately
after confirming an authored active HDR `temporalResolve`
effect, *before* deciding whether to upscale or jitter.
Thus a camera with Stage 42 native-resolution stabilization
gets the same quota protection. A refused history is retired
only via the existing GPU-fenced ownership policy; the camera
remains native/full resolution and unjittered.

For `AIVERSE_PRISMA_JITTER=1`, full native-resolution
TAA now also preallocates and validates the two HDR history
textures, native scene color/depth, motion velocity, internal
and output scratch depth, and its fallback/composite pipelines
through the existing Stage 45 `preflightUpscale(w,h,w,h)`.
The preflight runs **before** `Renderer3D::beginScene`.
A GPU preflight failure marks the view rejected and returns
a stable unjittered camera; `Prisma::resolve` checks the same
`preflightRejected` flag and cannot bypass the rejected
allocation later in the frame. Failed preflight retries remain
rate-limited. Native temporal stabilization without the jitter
flag may still use the legacy Stage 42 non-jittered path.

**Limitations:** RHI metadata cannot report all driver,
Metal dynamic buffer generations, texture mips, compressed
resource bytes, external scene owners, or separate processes'
GPU allocations. The new world/terrain/planet/FX reports
contain only handles that their owners create and maintain.
They never imply permission to destroy a borrowed or in-flight
resource. Metal/GL/GLES/Vulkan projection/origin, temporal
reprojection and resource-failure behavior remain unverified
by real hardware.

**Actual verification:** connected GitHub source inspection,
source readback, and 51/51 structural API/presence and ordering
checks. **NOT RUN:** native C++/Objective-C++ build, shader
compilation, CTest, real GPU operation, VRAM profiling,
screenshot comparisons, CI. See
`docs/handoffs/prisma-stage-50.md`; the next stage should
continue functional closure and NOT start final tests until
the user authorizes that phase.

## Stage 51 — remaining presentation GPU owners, runtime capability reporting, and temporal conflict resolution

Stage 51 completes another functional layer of the feature-first
PRISMA RHI and adaptive-quality implementation. No gameplay/ECS
dependencies were added to `Prisma`, its `postfx` JSON root or
the editor MCP identifiers. Source changes stay confined to
presentation owners, GPU resource metadata queries and optional
legacy temporal wrapper integration.

**Additional disjoint GPU memory owners:**
`SpriteBatch::ownedGpuMemoryStats` reports owned VB/IB/caster buffers,
*not* its caller-owned static SpriteMesh or borrowed texture handles.
`FogOfWar` reports its private sheet geometry and texture.
`RtsFieldView` counts field geometry and per-field texture outputs;
`RtsOverlays` counts overlay buffer, owned white fallback and the
overlay-private TextureCache; `RtsGroundFx` counts live ground
layer meshes/lane mesh, white fallback and its private texture cache.
`Rts2DView` counts its tile mesh, owned SpriteBatch buffers and
sprite texture cache. Road and Shore owners enumerate unique model
VBO/IBO and their kit-owned GPU images, never borrowed image
references duplicated across models. Roads also count paint decals,
wet film and puddle mesh buffers, their own SpriteBatch and paint
textures. `RtsPlanetGroundFx` enumerates planet territory/diggable/
road/lane tile geometry, `RtsGroundCanvas` owns ground sprite batch
buffers and its texture cache, and `RtsPaintLayers` reports its
paint RenderTexture canvases, coastline textures and mesh buffers.

`RtsView::ownedPresentationGpuMemoryStats(device)` now composes
these owners with Stage 49–50 models, terrain, planets and
ModelTextureCache. Assets that remain resident but are not drawn in
this exact frame are still counted until their owning subsystem
releases them. Borrowed textures, aliases to static mesh buffers
that have a different owner, shared GlobalGeometryPool pages,
and Renderer3D/Prisma/LightSystem allocations must never be
counted again from a draw record. The results remain lower-bound
RHI metadata, NOT total physical GPU residency or exact VRAM.

**Requested vs supported vs actually executed:**
`Prisma::runtimeCapabilities() const` is a read-only GPU-free
diagnostic of device availability/health, RGBA16F support, HDR
requested/effective last frame, authored temporal effect with
runtime flag, prepared temporal/velocity GPU pipeline handles,
subpixel jitter/upscale/adaptive requests and their last-frame
actual execution flags. It also reports whether the last scene
used a valid reactive input, whether a scoped timing sample was
accepted and `fallbackReason` for disabled device, unsupported
HDR, insufficient tier, absent pipelines, camera-history quota
rejection, temporal preflight rejection or backend errors.
`runtimeCapabilities` never compiles shaders or allocates
textures; a prepared GPU pipeline does not guarantee successful
GPU execution. The editor's Prisma panel shows each dimension
explicitly. The last-frame fields are statistics from the latest
Prisma resolve, not simultaneous per-camera snapshots.

**Legacy temporal resolver conflict:** the older optional
`PrismaScreenLighting` wrapper owns a separate color-history
resolver and Halton sequence. Running it concurrently with the
canonical `Prisma::resolve` temporal pass would blend two
different histories and jitter sequences. The new
`Prisma::canonicalTemporalRequested() const` designates the
single canonical temporal owner for requested HDR High quality
and an enabled authored Temporal Resolve, *without depending
on lazy shader readiness*. `PrismaScreenLighting::jittered`
delegates to the canonical `Prisma::jitteredCamera` and resets
its own history in that case; its `scenePass` suppresses the
legacy temporal pipeline. Stage 52 now separates the optional diffuse-bounce and volumetric
lighting passes from that legacy temporal resolver: when canonical
Temporal Resolve is selected, only linear HDR lighting runs before
the canonical resolve. No second TAA history or jitter is used.

**Explicit camera lifecycle:** `Prisma::retireUnregisteredViews(
const std::vector<uint64_t>& liveViewIds)` can be called by a
host after recording all live Scene/Game/custom camera passes
for a graphics frame. It unions tracked per-view exposure,
temporal, quality/admission, scoped GPU query, motion matrix
and size-reference IDs, calling existing GPU-fenced
`retireView(id)` only for IDs missing from the provided
live list. In-flight textures stay behind actual GPU completion
fences; the routine does not immediately destroy them or
retire a size shared by another camera. The host separately
retires Renderer3D-owned object-motion views via its existing
API. The editor keeps its explicit Scene/Game RHI-motion
retirement and adds the generic Prisma per-view cleanup
at the end of its viewport submission window; shared size
buffers continue through `retireUnreferencedSizeCaches`.

**Implementation confidence:** source inspection and 59/59
structural presence/ordering checks. Native compilation, CTest,
Metal/OpenGL/Vulkan/GLES GPU execution, device-loss/VRAM runtime
measurements and screenshot parity are **NOT RUN** per the
user's features-first requirement. The next agent must not
describe any of these as passed. Stage-specific architecture,
commits, touched paths, limitations and next functional steps
are in `docs/handoffs/prisma-stage-51.md`.
PR #26 remains Draft and main is untouched.


## Stage 52 — optional HDR lighting independent of legacy temporal accumulation

`PrismaScreenLighting` is an optional `PostProcess` adapter around
`Prisma`; do not copy its implementation to SpectraFX or run a second
TAA owner. This stage separates its HDR scene-lighting passes from its
historical temporal reconstruction.

- When `canonicalTemporalRequested()` is true on HDR/High, the adapter
  delegates `jittered()` to `Prisma::jitteredCamera` and resets legacy
  history. Its bounce shader (screen-space approximate indirect diffuse)
  and optional shadowed volumetric sun scattering still run on **linear
  HDR SceneColor before `Prisma::resolve`**. The last auxiliary texture
  is copied back to scene color through a dedicated shader that keeps
  alpha untouched. Prisma then remains the sole temporal owner.
- If canonical temporal is not selected and the adapter is enabled on
  HDR/High, its old two-history TAA / Halton camera jitter is retained,
  including the old opaque-alpha copy contract. The adapter's
  `temporalShadowResolve()` is true **only** in this legacy mode, so
  optional PCSS shadow sampling is not accidentally randomized without
  the matching legacy history.
- Auxiliary bounce, scatter, history and depth textures are created
  lazily as requested. Canonical-only mode does **not** create the two
  legacy history images or legacy depth attachment; existing allocations
  from a prior mode remain cached rather than being destroyed on a mode
  toggle while the GPU may still use them. Full resource teardown on
  resize/shutdown is inherited from the old adapter and still needs
  GPU-fence lifecycle validation.
- A missing sun shadow suppresses volumetric scatter for that frame,
  even if configured. Without a legacy temporal history the scatter
  sample sequence has a stable phase to avoid animation noise when a
  canonical temporal preflight fails. No GPU capability or visual
  parity is inferred from these source changes.

Public API: `PrismaScreenLighting::lightingOnly()` describes the
adapter's current path; `running()`, `passCount()`,
`temporalShadowResolve()`, `setDiffuseBounceEnabled()` and
`setVolumetricLight()` retain compatible semantics, except that the
temporal shadow flag no longer treats a non-temporal lighting-only
pass as a temporal resolver. The two direct lighting shaders and the
separate alpha-preserving copy are included in the adapter's MSL/GLSL
pipeline set. Existing `postfx` JSON, editor/MCP names and UI-after-
tone-map ordering are unchanged.

Verification: 15/15 **source-presence and ordering checks** after
GitHub readback. **NOT RUN:** clang/native CMake, unit/CTest, Metal/GL/
Vulkan/GLES execution, GPU fences, HDR color/alpha pixel parity,
screen-space bounce/volumetric screenshot tests and CI. Stage 52 code
and precise continuation live in `docs/handoffs/prisma-stage-52.md`.


## Stage 53 — per-camera effect diagnostics (CPU read-only)

`Prisma::effectReport(viewId) const` returns a pointer to a
`Prisma::ViewEffectReport` captured **after the last `resolve()` of that
specific camera**. The `viewId` identifiers are assigned by the host; the value 0
is the inherited default. `effectReportViewIds()` returns the sorted
keys of the stored reports. The result creates no RHI shader/texture,
does not call `program()/composite()`, does not wait for the GPU and does not change ECS.

`ViewEffectReport`: `viewId`, a `sequence` counter (only resolve calls
for this ID), input dimensions, quality, HDR path, scene readiness and
a list of `EffectExecution` for all elements of the current camera profile,
**including authored-disabled/low-tier effects**. Each entry contains:

- `type` — the `PrismaType` from the standard catalog; `authored` means
  present in the current profile.
- `requested` — the effect is enabled at the current tier and its opt-in + numeric
  conditions (if any) are met; a low tier / disabled environment
  variable gives a specific reason and `requested=false`.
- `pipelinePrepared` — the local cached RHI handles are
  available. This is **not** proof of GPU execution; for composite
  the actually selected variant is checked, and for multi-pass
  Bloom/TiltShift/SunShafts the specific prepared pipelines.
- `encoded` — the corresponding effect actually entered the last
  render (a draw for MotionBlur/DoF/FXAA/Temporal, a resource attached to the
  submitted composite for Bloom, LUT, Bloom/etc). A partial
  failed GPU graph whose result is not used does not count as a
  successful effect. Shader readiness without a draw does not give `encoded`.
- `reason` — the reason for skipping (camera cut/first frame, history quota,
  temporal preflight, HDR/tier/opt-in, missing LUT/pipeline,
  shadergraph/exposure texture-slot conflict, missing target, etc.).
  Empty for a successfully encoded effect.

The per-view report does not mix Scene/Game of the same size. On
`retireView` / `retireUnregisteredViews` the snapshot is removed together with
the camera state; on `shutdown()` and `setSettings()` old reports
are cleared. An `applyCameraVolumes()` update is reflected on the next
`resolve`; until then the snapshot is the history of the *last render*, not
a plan of the current settings. Report pointers are invalidated on clearing
or map update; do not cache them between frames.

The `PRISMA — Постобработка` editor displays a collapsible matrix
`View ID / effect / requested / pipeline / encoded / reason` without creating
resources while viewing. The existing global `runtimeCapabilities()`
still describes the latest data from `PrismaStats`, not all
cameras; for a per-view effect use the new API.

Especially important: `encoded` means **GPU draw command issued**, not
a completed GPU fence, correct pixels, an image in the frame or
effect parity across different RHIs. The last four claims
are permitted only after real GPU tests.

Stage 53 source: `engine/render/Prisma.{h,cpp}`,
`apps/editor/windows/PrismaWindow.cpp`; exact commits,
structural checks and the next stage are in `docs/handoffs/prisma-stage-53.md`.
**Actual verification:** 18/18 source-presence/lifecycle checks after
GitHub readback. **NOT RUN:** local CMake/clang/CTest, editor,
Metal/GL/GLES/Vulkan, shader compile, screenshots, GPU passes, CI.
Do not claim performance/pixel verification before a separate test phase.

## Stage 54 — safe camera closing and deferred GPU retirement

In PRISMA, the camera `viewId` denotes the stable identity of a
presentation camera, **not** its size, so two 1920×1080 cameras
have independent temporal/velocity histories.

`Renderer3D::retireUnregisteredObjectMotionViews(liveViewIds)`
(a new public genre-independent API) takes the full set of
active stable view IDs **after** all camera passes of the current
frame have been submitted. For each missing ID it calls the existing
`retireObjectMotionView(id,device.gpuSubmissionFence())`;
only Renderer3D-owned `target/depth/reactive`
textures are queued. They are released via
`collectCompletedObjectMotion(device.gpuCompletedFence())`.
A zero submission token keeps the resources in `motionUnfenced_` until
an explicitly confirmed GPU idle. **CPU frameSerial and time cannot
replace a GPU fence**. Active cameras and size-shared targets are not
released.

After the Scene/Game render, the editor uses a single
`activePrismaViews` for
`Prisma::retireUnregisteredViews(...)` and
`Renderer3D::retireUnregisteredObjectMotionViews(...)`, then
`Prisma::retireUnreferencedSizeCaches()`. The old Scene/Game close
transitions are kept and are a no-op if the generic cleanup has already
removed the ID; custom-camera cleanup no longer skips independent
Renderer3D velocity textures. GameClient keeps 3D view 3 alive
while it renders `sim_`; if a frame contains only a menu or loading,
it removes the private Prisma/Renderer3D states and unused
size caches. `startMatch` still explicitly retires the previous
camera. On the next render the camera creates a clean history.

`PrismaScreenLighting` keeps exclusive ownership of its
`bounce_`, `scatter_`, `history_[2]` and `depth_`.
`releaseTargets()` on resize or in `scenePass()` after
`setEnabled(false)` now moves them into a
`PrismaGpuRetirementQueue<gfx::TextureHandle>` bound to
`Device::gpuSubmissionFence()`. Until the device reports
`gpuCompletedFence()`, no actual `destroyTexture` happens.
If the backend provided no submission token, the resource is kept in
`unfencedTargets_` and is released only via
`drainRetiredAfterGpuIdle()` after an explicit GPU idle wait
by the device owner.

Public adapter API:
`collectGpuRetirements()` — non-blocking collection by
confirmed completion fence;
`retirementStats()` — CPU/GPU-free statistics of the number of
fenced/unfenced textures and the last GPU completion token;
`drainRetiredAfterGpuIdle()` — **only on known GPU idle**;
`shutdown()` also **requires GPU idle** for safe deletion of
pipelines/shaders/vertex buffer, like the historical adapter.

The shared `Prisma` and `Renderer3D` own their own GPU queues;
ScreenLighting **never** deletes other owners' targets, depth or
history. Disabling the adapter removes its current auxiliary resources
on the *next call* of `scenePass`, not immediately on
`setEnabled(false)`. If the adapter is never called again,
its GPU owner must continue `collectGpuRetirements()` and
at the end of the lifecycle complete `shutdown()` after idle.

**Limitations/checks:** only GitHub source readback and
30/30 structural checks of the target transitions/methods. **NOT RUN:**
CMake/clang, CTest, Metal/GL/GLES/Vulkan/WebGL, GPU fence overlap,
integrated Editor/GameClient runtime, memory stress and image
parity. Correct signalling of backend tokens in real RHIs and
a complete live-ID roster in all custom camera hosts must
be confirmed separately. Exact source commits and the next
stage are in `docs/handoffs/prisma-stage-54.md`.



## Stage 55 — Auto Exposure together with four Shader Graph textures

Before this stage PRISMA reserved a fixed sampler `TEX8` for GPU HDR Auto Exposure,
while the postfx Shader Graph used
bindings `TEX5..TEX8` for at most four independent textures.
With four textures, author-enabled Auto Exposure
was **silently disabled**, even though all its other conditions were met.
This was a functional limitation, not a physical RHI restriction.

The new GPU-independent `PrismaCompositeTextureLayout.h` defines a
shared, constexpr-computable resource map. The fixed composite
occupies slots 0..4, the Shader Graph 5..8, and HDR Auto Exposure
slot 8 while the graph uses 0..3 textures, and slot 9 with
four textures. An unsupported texture count (more than 4)
is explicitly rejected. Five `static_assert`s confirm the expected
computations when the library is compiled; **native compilation has NOT yet
been performed**. The scheme does not change serialization, JSON or the MCP API.

In `Prisma.cpp` the plan is used both when building
`ShaderDesc.textureSlots`, when forming the masks for GLSL/MSL
`TEX0..TEX9` and in the actual `Device::setTexture`.
`PrismaShaders.h` selects `TEX9` only with
`PF_AUTO_EXPOSURE_SLOT9`, otherwise it keeps `TEX8`.
The composite cache key remains `effectMask + ShaderGraph JSON`:
this is sufficient, since the layout is uniquely determined by the
graph itself. If building the expanded pipeline fails, PRISMA keeps the
old fallback: the same composite without Auto Exposure, and if that is
also unavailable, the base composite without effects. Do not substitute
a failed pass with "effect executed".

`Prisma::ViewEffectReport` now contains
`shaderGraphTextures` and `autoExposureTextureSlot`.
The latter is `-1` when the **final selected composite pipeline**
does not contain Auto Exposure; the entry is updated only after
the fallback is resolved. The `PRISMA — Постобработка` editor shows the
actual slot for each `viewId` without triggering a GPU
shader compile for displaying diagnostics.

RHI source check: GL configures `texN` from
`ShaderDesc.textureSlots`, the Metal backend contains sampler
bindings that include 9, Vulkan allows up to 32 descriptor
texture slots; the Shader Graph compiler limits the number of
independent textures to four. This is a **structural assessment of the
sources**, not proof of correct operation on all
GLES/WebGL/Metal/Vulkan adapters.

**Actual verification:** after GitHub readback, 25/25
structural source/layout/logic checks were performed; **NOT RUN:**
CMake/clang, shader compilation, editor, CTest, GPU resolve,
HDR pixel readback, backend stress/CI/benchmarks. See
`docs/handoffs/prisma-stage-55.md` for exact commits,
limitations and the next step. PR #26 remains Draft;
no changes were made to `main`.


## Stage 56 — Depth of Field: distance-based focus and silhouette protection

The experimental `depthOfField` remains part of PRISMA and
still requires `AIVERSE_PRISMA_DOF=1` and a suitable quality tier.
Old maps/JSON automatically use the **previous mode**:
`focusDepth` and `focusRange` in hardware depth [0,1];
`maxBlur` and `intensity` stay unchanged. In the shader this
mode is kept in a separate branch with the original formula and
the 10-weight blur scheme, so that the old image is not
reinterpreted as a physical distance.

New optional parameters of the same effect:

| Parameter | Default | Purpose |
|---|---:|---|
| `metricFocus` | false | Explicitly enable linear depth from camera data |
| `focusDistance` | 12 | Focus plane along the view direction (world units) |
| `focusRangeMeters` | 4 | Sharpness interval around the focus plane |
| `occlusionToleranceMeters` | 0.35 | Allowed depth difference between the center and a tap before blending is suppressed |

*The "meters" naming applies to projects where one world
unit equals one meter.* This is linear depth along the camera
direction (view-space Z), **not** the spherical distance
from the eye to an arbitrary point. For perspective:
`z = near * far / (far - depth * (far - near))`;
for orthographic: `z = mix(near, far, depth)`.
`Renderer3D::resolvePrisma()` already passes
`PostFrame.depthNear`, `depthFar` and `orthographic`;
the orthographic bounds of the current renderer can be
negative `-200..1000`, which is supported correctly.

The `Prisma.cpp` code checks finite near/far and
`far > near`; perspective additionally requires `near > 0`.
If `metricFocus` is requested but the input projection is invalid,
the RHI draw runs the **old hardware depth**, and
`Prisma::EffectExecution` for this camera sets
`encoded=true`, `degraded=true` and a specific `reason`.
This prevents NaN and an empty frame. The report pointer stays
read-only; `degraded` does not mean verified visual
correctness of the effect on the GPU.

The metric shader computes CoC from linear depth and, among
eight neighboring color taps, chooses the weight by the
**difference in linear depth**. Points separated by a silhouette
or belonging to sky/background (device depth >= 0.99999)
are not blended; the transition between consistent and
inconsistent layers is smoothed with `smoothstep`.
GPU sampling stays in linear HDR; output alpha in the metric branch
is inherited from the source SceneColor. The legacy branch keeps
the previous output alpha=1 for compatibility.
The sampled scene depth is never bound as a
writable depth attachment; DoF uses a separate
`scratchDepth`. The pass works with `src.tex`, which may
contain the result of the preceding Motion Blur, and now checks
that `src.tex` specifically is present before the draw.

`PrismaData.cpp` automatically exposes the new fields in the shared
JSON/editor/MCP/volume-mask schema; do not create a separate implementation for FPS,
RTS or SpectraFX. `PrismaWindow.cpp` gained
a hint about mode selection and a visible `degraded fallback`
in the effect matrix per `viewId`. Any old `postfx`
without `metricFocus` will keep using the old path.

**Limitations:** this is an 8-tap screen-space defocus, not
physically correct bokeh, Gather/Scatter DoF or
full separated foreground/background scattering.
At intersections of translucent surfaces the depth
constraint can suppress blurring, but cannot
recover colors hidden behind objects.
Depth [0,1] semantics, camera and Metal/GL parity require
verification on a real device.

**Actual checks:** after the commits the sources were
read back from GitHub and **32/32 source-only
structural checks** were run. **NOT RUN:** CMake/clang,
GLSL/MSL/Vulkan shader compiler, CTest, Editor/GameClient,
Metal/GL/GLES/WebGL/Vulkan GPU pixel/readback,
screenshots, profiling and CI. The next agent should read
`docs/handoffs/prisma-stage-56.md`.


## Stage 57 — stable HDR Auto Exposure and consistent camera-cut reset

`autoExposure` still requires the effect in the postfx profile,
effective HDR and the variable `AIVERSE_PRISMA_AUTO_EXPOSURE=1`.
Luminance measurement stays in linear scene-referred HDR before
tonemapping and UI, and its separate 1×1 ping-pong history belongs to
a **stable `viewId`**, independent of Scene/Game viewport sizes.

New optional parameters in the shared PrismaData catalog:

| Field | Default | Range | Meaning |
|---|---:|---:|---|
| `highlightRejection` | 0 | 0..0.95 | How much to reduce the influence of very bright pixels during metering |
| `highlightStartEV` | 3 | -10..16 | Threshold where weight reduction starts, in log2(luminance), NOT camera exposure EV |
| `highlightSoftnessEV` | 2 | 0.1..8 | Smoothness of the transition to maximum weight reduction, in log2 stops |

`highlightRejection=0` **by default** keeps the previous
weighted log-luminance estimator and the already existing
`centerWeight`, `centerRadius`, `minMeterLogL`,
`maxMeterLogL`, EV clamps and adaptation speeds. This is optional
**soft highlight weighting**, not histogram/percentile metering.

The GPU shader `kExposureLog` computes the luminance of each of the four
samples, clamps logL to the current `minMeterLogL/maxMeterLogL`,
then reduces the base spatial weight:
`weight *= 1 - rejection * smoothstep(startEV, startEV+softnessEV, logL)`.
The bound `rejection <= 0.95` guarantees at least 5% of the original
weight even for a fully bright frame and rules out a zero
metering denominator. The `kExposureReduce` pyramid reduction
still separately carries the weighted sum of log2 luminance
and the sum of weights, and `kExposureAdapt` divides the former by the latter
before computing EV. There is no hidden GPU readback or CPU luminance pass.

**Camera cut:** before Stage 57, automatic camera jump
detection happened after GPU metering, so the first frame after a
teleport could blend with the previous exposure. Now the check of
`motionView.previousCameraPos`, finiteness and a jump of more than
12 world units runs at the start of `Prisma::resolve`,
**before** Auto Exposure. The reset simultaneously invalidates
velocity/temporal history and calls `resetAutoExposure(viewId_)`.
Explicit `resetMotionHistory(id)` / `resetMotionHistory()` also
invalidate per-view/all-view exposure history without deleting GPU
textures. The first adaptation pass of the new camera uses
`historyValid=false` and is reinitialized from the current
measurement. The reset does not require waiting for the GPU and does not confuse
two cameras with the same resolution.

Color order is preserved: weighted HDR metering measures
`scene.tex`, the 1×1 exposure is applied to HDR RGB in
`PF_AUTO_EXPOSURE` inside the final composite, then
the ColorGrade-authored `exposure` in HDR enters the pre-tonemap
multiplier `filt.z`, then ACES/sRGB, user
display-space grade/LUT and the postfx Shader Graph.
These are **two independent authored multipliers**, not two
instances of the auto-adaptation algorithm; GPU history is strictly
one per view. Shader Graph resources (slots 5..8) and
Auto Exposure (8 or 9) continue to follow the Stage 55 layout
and the fallback without Auto Exposure on an unsupported pipeline.

`Prisma::ViewEffectReport` now records
`autoExposureHighlightRejection` **only** when the finally
encoded composite actually contains Auto Exposure.
Otherwise the field stays zero, `autoExposureTextureSlot=-1`,
and the Stage 53 per-effect status indicates the effect is absent.
The editor shows the value and hints directly in
`PRISMA — Постобработка`; the effect list and the volume editor
get the new parameters automatically through the shared catalog.
Old `postfx` JSON, MCP, the saved editor panel id and SpectraFX
are not affected. The parameters do not affect authoritative ECS.

**Limitations:** histogram percentile metering,
tonal segmentation of skin/sky, AE metering masks and local
exposure are not implemented. A globally bright background can still influence the overall
result (no less than 5%). Tune `highlightStartEV` in terms of
scene-referred **log2 luminance**, not camera control EV.
Color dynamics quality and GPU shader parity are not yet verified.

**Verification:** GitHub readback of five source files and **31/31
source-only structural checks**. **NOT RUN:** CMake/clang,
GLSL/MSL/Vulkan compiler, CTest, editor/client, Metal/GL/GLES/
Vulkan/WebGL execution, pixel/temporal parity, perf, CI.
See `docs/handoffs/prisma-stage-57.md` for exact commits
and the next functional stage.

## Stage 58 — cinematic Exposure Freeze and a circular HDR metering region

PRISMA still uses **one** global
Auto Exposure algorithm: HDR scene luminance → `kExposureLog` →
`kExposureReduce` → `kExposureAdapt` → 1×1 per-view
exposure history → final HDR composite before tonemapping/UI.
Stage 58 adds control over its already existing
GPU passes, without a new ECS system, a second shader
Exposure owner or mandatory JSON parameters.

### New optional fields of `postfx.effects[].type=autoExposure`

| Field | Default | Semantics |
|---|---|---|
| `freezeExposure` | false | Hold the last valid 1×1 Auto Exposure value of this camera |
| `meterRegionEnabled` | false | Enable local luminance weighting of a circular frame region |
| `meterCenterX`, `meterCenterY` | 0.5, 0.5 | Normalized region center, X left to right, Y bottom to top |
| `meterRadius` | 0.25 | Circle radius relative to the frame **height**, aspect-corrected |
| `meterFeather` | 0.10 | Smooth width of the circle edge, as a fraction of frame height |
| `meterOutsideWeight` | 0.05 | Weight of pixels outside the circle; guaranteed above 0 (minimum 0.02) |

All fields are appended to the end of the existing Auto Exposure catalog:
old offsets and values do not shift. The generic
`PrismaData` parser/serialization, editor, MCP and
`PrismaVolumeStack` get the new fields automatically.
For games without the new keys the previous processing stays
functionally unchanged; both modes are off by default.
The existing effect still requires HDR, a matching tier
and `AIVERSE_PRISMA_AUTO_EXPOSURE=1`.

**Freeze:** with a valid history `ExposureView.history[current]`
the previous 1×1 GPU texture is simply bound to the final
composite. On this frame no extra mip
targets are created, and `kExposureLog`, `kExposureReduce`
or `kExposureAdapt` are not run; the GPU history is not read and
written at the same time. The internal adaptation time
is updated so that unfreezing does not produce a large delta-time.
On the first active camera, after a camera view ends,
a settings reset or a Camera Cut, the history is invalid; freeze
performs **one new metering / adaptation** of the current scene,
after which it holds the result again. Explicit camera cuts
keep the Stage 57 contract — capture the exposure again,
rather than holding the previous frame forever. Disabling the effect itself
invalidates its history without destroying resources immediately.

**Region:** `kExposureLog` gained an additional
`vec4 region` only in its own UBO (`ExposureMeterU`).
`SimpleU` and the interfaces of other passes did not change.
`region.z < 0` disables the region without affecting the old
formula. When enabled, each of the four source sample UVs
is converted back from backend texture orientation into
bottom-up screen coordinates `TUV(q)`; X is scaled by
the width/height ratio. Weights are computed with
`smoothstep(radius,radius+feather,physicalDistance)`,
then multiplied with the old `centerWeight` and the
Stage 57 `highlightRejection`. The outside weight is never
zero. The pyramid reduction keeps
`(weightedLogL, totalWeight)` and does not depend on the mask;
all other shaders/UBOs stay the same.
This is **cheap area-weighted metering**, not a GPU histogram,
segmentation, a user texture mask or
local exposure for different parts of the screen.

### Separate settings for native cameras

`Prisma.h` provides a GPU-free presentation API
keyed by a **stable view ID**, not by camera coordinates
or viewport size:

```cpp
prisma.setViewExposureFreeze(viewId, true);
prisma.setViewExposureMeterRegion(viewId, {
    true, 0.5f, 0.5f, 0.25f, 0.10f, 0.05f
});
// Чтобы вернуться к наследованию postfx/PrismaVolumeStack:
prisma.clearViewExposureFreeze(viewId);
prisma.clearViewExposureMeterRegion(viewId);
```

Also, `setViewExposureFreeze(id,false)` explicitly
overrides `freezeExposure=true` from the profile.
`setViewExposureMeterRegion` checks finiteness and clamps
coordinates, radius, feather and outsideWeight on input.
Thus two cameras at the same geographic point
can have independent freeze and region parameters.
After all camera passes finish, the host passes the full
set of live identifiers to
`Prisma::retireUnregisteredViews`; the new overrides
are included in unionTracked and are removed by `retireView`/
`shutdown` together with per-camera state. This is
presentation-only, not authoritative ECS and not a change
to the global base profile.

`ViewEffectReport.autoExposureFrozen` and
`autoExposureRegionMetered` are updated only if the
**final encoded composite** actually applies
Auto Exposure. Frozen means "on this frame a valid history was
reused, the meter was not run"; RegionMetered
means "on this frame a measurement with a region was performed".
When exposure is absent in the final fallback, both are false.
The editor window shows them for each viewId and gives
hints for the new generic fields; GUI reads do not trigger
GPU creation.

**Verification:** after the source commits and GitHub readback,
**36/36 structural source checks** over the catalog, shader,
CPU plan, native API, lifecycle, UI and final fallback.
**NOT RUN:** CMake/clang, CTest, GLSL/MSL/SPIR-V shader
compiler, Metal/OpenGL/GLES/Vulkan/WebGL GPU, the editor
on a device, luminance readback, alpha/pixel parity,
latency/FPS, GPU fences and CI. Structural checks do not
prove the build or RHI correctness.
Exact SHAs and the next stage —
`docs/handoffs/prisma-stage-58.md`.



## Stage 59 — Manual EV, rectangular ROI and fault-tolerant HDR metering

Manual EV is implemented **inside the existing** `AutoExposure`
postfx effect; it does not start a second HDR owner, GPU history,
a new fullscreen pass or an extra sampler. The
`PrismaData.cpp` catalog extends Stage 58 with the fields:

| Field | Default | Purpose |
|---|---|---|
| `manualExposure` | false | Fixed-exposure mode instead of automatic metering |
| `manualEV` | 0 | −12…+12 stops, `2^manualEV` in linear HDR |
| `meterBox` | false | Use a rectangular ROI instead of a circle |
| `meterHalfWidth` | 0.25 | Rectangle half-width as a fraction of frame **width** |
| `meterHalfHeight` | 0.25 | Rectangle half-height as a fraction of frame **height** |

All new settings are opt-in and appended to the end of the catalog.
Old `postfx`, editor IDs and MCP operations are preserved;
for existing projects `manualExposure=false` and
`meterBox=false` mean no changes. Ordinary
automatic exposure still requires
`AIVERSE_PRISMA_AUTO_EXPOSURE=1`, while an **explicitly enabled**
manualExposure works without this flag (but requires an
authored `autoExposure`, enabled postFX, an allowed tier
and linear HDR).

### Fixed EV and camera state

In `Prisma::resolve` manual mode is selected from the camera profile
and native override. With manualExposure active, GPU metering,
`kExposureLog/Reduce/Adapt`, the 1×1 exposure history and the texture
sampler `TEX8/TEX9` are **not used**. Instead,
the already existing `CompositeU.filt[2]` is multiplied by
`exp2(manualEV)` **after** the author ColorGrade
exposure is computed, but **before** the final HDR composite / ACES / sRGB / LUT /
Shader Graph and UI. This means the fixed EV and the
artistic ColorGrade exposure multiply
compositionally, but there is no automatic adaptation in manual mode.

Manual EV from native calls and the C++ profile is checked for
finiteness and clamped to [-12,+12] before HDR; NaN/Inf
are not passed into the shader. On switching into/out of manual
mode the camera-local exposure history is invalidated; on
returning to automatic mode a new measurement of the current
scene is taken instead of blending with the old luminance. Manual mode
can work immediately on the first frame — unlike
`freezeExposure`, which requires a valid preceding 1×1
adaptation. When a user Shader Graph fails,
the fallback to `composite(0)` keeps the fixed EV, since
`filt[2]` is applied outside `PF_GRADE` and `PF_AUTO_EXPOSURE`.
With an invalid final pipeline the report does not declare manual EV
as executed.

Native API:
```cpp
prisma.setViewManualExposure(stableViewId, true, 1.5f);
prisma.setViewManualExposure(stableViewId, false, 0.0f); // explicit disable
prisma.clearViewManualExposure(stableViewId); // restore profile inheritance
```
Choosing `setViewManualExposure(id,false,...)` explicitly
overrides the authored `manualExposure=true`; clear
returns control to the profile and invalidates the AE history.
Overrides belong to the stable presentation `viewId`, not
to camera resolution/position. `retireView`,
`retireUnregisteredViews` and `shutdown` remove them
together with the rest of per-camera state, without freeing
shared GPU texture caches.

### Soft rectangle for HDR Metering

The Stage 58 circular region stays the default; Stage 59
adds `meterBox=true` with parameters
`meterHalfWidth` and `meterHalfHeight` in normalized
viewport coordinates. The radius and feather of the previous region
are preserved. `ExposureMeterU` gained `regionBox`
(a fifth vec4); `kExposureLog` uses
`regionBox.z > 0.5` to select a signed-distance box instead of a
circle. The X difference is brought to the scale of frame **height**
by the aspect multiplier; the soft edge uses
`smoothstep(0,feather,signedDistance)`.
`TUV(q)` ensures the same Y direction in
GL/Metal, and the existing `meterOutsideWeight >= 0.02`
keeps nonzero contributions outside the region. The global
reduction (weighted log sum and weight sum) is unchanged.

Native `ExposureMeterRegion` extends the existing fields with
`bool box=false; float halfWidth=0.25, halfHeight=0.25`
**at the end** of the struct, preserving old aggregate initializers.
Setters clamp finite sizes to [0.01,0.5], and the ROI
is not part of authoritative ECS. The camera view report
is extended with `autoExposureBoxMetered` (only for
**actually encoded** exposure). In
manualEV mode no meterRegion runs on the frame.

### Fallback on GPU metering failure

If the HDR luminance pyramid did not finish with a complete
1×1 texture (`valid=false` or `pw/ph != 1`),
Prisma now **does not allocate or write**
`ExposureView.history` for this measurement. The old
per-camera sample is invalidated, the final composite
keeps working without sampled Auto Exposure;
a new attempt may be made in future frames.
Partially encoded mip passes are not presented as
successfully adapted lighting, and no separate
GPU sync wait was added.

`ViewEffectReport.manualExposureEncoded` and
`manualExposureEV` report only a truly selected
and encoded fixed EV on the last `resolve`.
`autoExposureTextureSlot` stays -1 for manual.
The existing effect matrix marks manual as
executed via the base/fallback HDR composite, without a
false requirement for ExposureLog pipelines.
`autoExposureBoxMetered` is shown together with
`autoExposureRegionMetered` only after a real
GPU meter pass and a subsequent successful composite.
In the editor these parameters are available from the shared
catalog with explanations and per-viewId status.

This is **not** a full arbitrary metering texture
mask, GPU histogram, local exposure, a physical
camera simulation, TSR or a fix for all Shader Graph
errors. The additional render branches remain
experimental until real GPU tests.

**Actual verification:** GitHub readback of five
changed source files and **49/49 source-only
structural checks**. **NOT RUN:** CMake, clang,
CTest, GLSL/MSL/SPIR-V compilation, Metal/GL/GLES/
Vulkan/WebGL GPU smoke/pixel capture, editor undo
persistence, HDR parity and performance measurement, CI.
For exact commits and the next step see
`docs/handoffs/prisma-stage-59.md`.



## Stage 60 — graceful Shader Graph / LUT fallback that keeps ordinary effects

Previously, on a failure of a user postfx
Shader Graph (GLSL/MSL compilation, unsupported sampler layout
or invalid input textures), `Prisma::resolve()` could end up jumping straight
to `composite(0)`, losing working fixed Bloom,
ColorGrade, LUT, Vignette and other effects. PRISMA now
uses the following **ordered fallback system**:

1. The requested variant with Shader Graph and, if present,
   GPU Auto Exposure.
2. The same Shader Graph, but without Auto Exposure, if
   the first attempt did not create a pipeline.
3. **Without Shader Graph**: the full fixed-effect mask with Auto Exposure
   (if the prepared HDR 1×1 texture is still available).
4. The same fixed-effect mask without Auto Exposure, if
   the previous pipeline is unsupported.
5. Only as a last resort — `composite(0)`
   without fixed effects. If even that is unavailable,
   diagnostics do not claim a successful draw.

The set of fixed effects is kept as a separate copy
`fixedMask` **before** the Shader Graph attempts. If a user
Shader Graph texture is missing or was not uploaded to the GPU,
the whole Shader Graph is excluded from the current composite, and
working fixed effects keep running. Successfully creating a
Shader Graph pipeline does not by itself mean the input
textures loaded: `graphInputsReady` checks
`graphTextures_[path].valid()` for all used paths
after the upload attempt. A missing input is no longer
silently presented as a valid "black" texture inside an
executed Shader Graph.

**Especially important after Stage 55:** in the graph variant with four
images, Auto Exposure uses `TEX9`.
If the graph failed and the fixed-only fallback is used,
the last actually selected pipeline requires `TEX8`.
`PrismaCompositeTextureLayout::plan(...)` is now
recomputed based on the **actually selected**
Shader Graph, not the original authored texture list.
Exactly this `exposureSlot` is used in GPU binding,
`ViewEffectReport` and the shader variation. In a
graphless fallback, textures in slots 5..8 are not bound
unnecessarily. Stage 59 HDR manual EV,
ColorGrade and Auto Exposure settings are preserved if the resulting
variant is supported. The `composite(mask,graphData)` cache
and deferred GPU-retirement ownership are unchanged.

### Diagnostics and error caching

`ViewEffectReport.shaderGraphFallback` indicates that the
authored Shader Graph was skipped, and the rest of the chain
could be used **where its pipelines are valid**.
`EffectExecution` for `ShaderGraph` gets
`encoded=false` and a specific reason: a missing/failed-upload
texture or a shader compile/composite failure. For
working fixed effects, `encoded` is determined by the final
GPU mask, not by the facts of the original request. In the
`PRISMA — Постобработка` editor a corresponding
warning per stable `viewId` appeared.

In `Prisma::lut()`, a read error, an invalid LUT strip
`N²×N` and a GPU upload error now set
`lastError()` with a description and project-relative path.
LUT is still negative-cached, so the other
effects can keep running without it.
`graphTexture()` now also negative-caches
**GPU upload** errors: re-decoding and failing
`createTexture` every frame are not needed. Resetting
the cache via the existing `setImageLoader()`
retires successful texture handles behind a real GPU
fence and allows reloading; changing a file
on disk **without calling** this procedure is not by itself
live-reload and is not promised in this stage.

Unchanged: the `postfx` format in `game.json`,
the `PrismaData` catalog, MCP API, ECS gameplay, default effect
settings, SpectraFX, per-view exposure ownership
and user Shader Graph formats.

**Actual verification:** readback from GitHub of
`Prisma.cpp`, `Prisma.h`, `PrismaWindow.cpp`
and **33/33 source-only checks** of fallback logic, GPU
input availability, final exposure slot, fixed mask and LUT errors.
**NOT RUN:** clang/CMake, native editor/client, CTest,
GLSL/MSL/Vulkan shader compiler, Metal/GL/GLES/WebGL/Vulkan
GPU smoke, actual HDR pixels, timed GPU frames, screenshots
and CI. Detailed handoff:
`docs/handoffs/prisma-stage-60.md`.


## Stage 61 — reactive transparent edges for Motion Blur and safe FXAA

Stage 61 fixes two specific PRISMA presentation paths:
SDR FXAA no longer substitutes an invalid texture for the input when
creating the intermediate render target fails, and optional
Motion Blur can suppress blending of color taps by the
difference in **reactive coverage** at the edges of translucent
objects/particles. The work stays genre-independent and
does not touch authoritative ECS or the separate SpectraFX.

### Motion Blur + reactive input

A new optional parameter of the existing `motionBlur`:
`reactiveTapRejection`, range 0..1, **default 0**.
It is appended **at the end** of the effect catalog in
`PrismaData.cpp`, so all previous numeric offsets,
old `postfx` JSON, presets and native/volume profiles
stay compatible. At zero the previous
shader weight and previous filter apply.

`MotionBlurU` gained a fifth vec4 `tapInfo`. Only
`kMotionBlur` uses the extended UBO; other
passes (Temporal, DoF, Bloom, FXAA, ExposureLog) and the
reactive texture format did not change.
`tapInfo.x` receives `reactiveTapRejection`
**only with a valid input** `MotionTextureInput`
(RGBA8, size of the current scene render target, a check of
declared metadata and the available RHI `textureInfo`).
When the reactive mask is absent/rejected, the value is
zero and the shader keeps the previous behavior.

If the filter is active, the current pixel and its neighboring
color taps read `TEX3` reactive R. Taps
with substantially different coverage get a reduced
weight:
`keep *= 1 - strength * smoothstep(0.1,0.8,abs(tapReactive-centerReactive))`.
This is an **addition** to the already existing depth
filtering (hardware or linear depth), not its
replacement. If the sum of weights is zero, the
original sharp RGB is applied. In new HDR runs
Motion Blur returns the **original alpha**
`centerColor.a`, not `1.0`, since the post effect
changes radiance RGB, not scene transparency.
Canonical `TemporalResolve` still itself
writes depth into its own history alpha —
this contract is unchanged.

Dependency on real reactive geometry: the mask
is built separately by Renderer3D and additional
providers (for example, particles/water),
and the temporary mask image is NOT kept
between frames. This stage does not attempt to fix
missing coverage of a specific material
or read GPU pixels back on the CPU.

`ViewEffectReport` per stable `viewId`
now stores `reactiveInputSupplied`,
`reactiveInputAccepted` and
`motionBlurReactiveEdgesEnabled`. This is
**CPU metadata/RHI acceptance**, not GPU pixel
validation: a legacy borrowed handle without metadata
may be considered acceptable when the backend
does not provide introspection. The last field
becomes true only when MotionBlur
is actually encoded, the required input is verified
and strength is greater than zero. The PRISMA editor shows
mask availability and filter status.

### Safe FXAA degradation

Previously the SDR prepass did `src=target(kAa)`
**before** checking whether the creation result was valid.
With a failed `RGBA8` target or depth attachment,
composite could sample an invalid src.
Now `fxaaInput` keeps the original
scene/post-chain texture, `fxaaOutput`
is created separately, and the prepass runs
only if input/output and depth are valid.
Graph FXAA (opt-in) and the ordinary direct draw
use the same two verified handles,
and `src=fxaaOutput` is assigned **only after an
encoded draw**. If allocation failed,
the rest of the chain continues with the previous src,
without a false `fxaaEncoded=true`.

For the HDR fallback FXAA, which is applied
after the final Composite and only if
TemporalResolve did not run, a check of
`pipe.valid()` was added **before** starting the intermediate
AA target/pass. This rules out reading an empty
composite target if the pipeline could not
be obtained at all. The output target/depth check
is kept. TAA and FXAA remain mutually exclusive
on the single HDR presentation output.

No new GPU history or shared texture
owners were introduced. Rendering keeps the current
Stage 54 fence-retirement contracts.

**Actual verification:** GitHub readback of
`PrismaData.cpp`, `PrismaShaders.h`,
`Prisma.cpp`, `Prisma.h`,
`PrismaWindow.cpp` and
`PrismaMotionTextureInput.h`,
**40/40 source-only structural checks** (checking
logic and calls in the sources). **NOT RUN:**
CMake/clang, GLSL/MSL/SPIR-V compilation,
CTest, Editor/GameClient, Metal/GL/GLES/
Vulkan/WebGL, screenshot/alpha/historical
parity, temporal ghosting and performance.
The next agent should read
`docs/handoffs/prisma-stage-61.md`.


## Stage 62 — reactive mask on the first frame, Camera Cut and resize

From this stage on, `Renderer3D` separates two **independent**
presentation producers: (a) object velocity vectors,
which require the previous camera and a history of current/previous
transforms, and (b) current-frame RGBA8 reactive coverage,
which **does not need** a preceding pose. Previously the whole reactive
pass was inside `if (history.target.valid() &&
history.depth.valid())`, and the function exited with `return`
on the first frame, on camera cut and on resize. Therefore
the transparency of water, particles and morph/skinned translucent objects
could be left uncovered by the reactive mask precisely where
temporal history is invalid.

### New Renderer3D structure

`renderObjectMotionVectors(PostProcess&)` is still called
on the final scene frame, before `Prisma::resolve`.
In it, `velocityHistoryReady` is determined only by
`previousCameraValid && history.valid && !resize`.
When previous data is missing, no false/zero
object-motion velocity frames are **sent** to Prisma.
Current rigid/animated transform poses are collected as before;
after the reactive pass they are saved for the next
frame. If the viewport changed size, the `target/depth/reactive`
textures are *all* placed into the existing GPU-fenced queue
via `queueObjectMotionTexture(...)`, rather than destroyed
synchronously. This preserves the Stage 54 ownership contract.

The new private method
`Renderer3D::renderCurrentFrameReactiveMask(PostProcess&,
ObjectMotionHistory&, gfx::TextureHandle sceneDepth,int w,int h)`
uses only the **current** `viewProj_`, model poses,
scene depth and ready framebuffer sizes. This helper
is called **after** the conditional object velocity pass
and is not nested in it. It provides its own private depth
attachment for reactive-only frames (`Depth`) and,
when the required geometry is present, creates an RGBA8 reactive target.
Scene depth from `Prisma` is passed as a borrowed sampler
and never becomes a writable depth attachment.
The absence of RGBA16F velocity does not block the **helper at the
Renderer3D level** if a native post hook requested such
reactive work and provided scene depth; the ordinary
Prisma HDR policy still decides on its own whether it needs
MotionBlur/Temporal Resolve at all.

Current coverage rendering keeps the previous sources:
rigid transparent models and fading LOD, morph/skinned/baked
translucent models with the current pose for both motion
matrices, immediate water MeshVertex geometry,
`motionReactiveDraws_` from particles and custom native
providers. The mask is still created/cleared
only on the current frame, blending merges coverage,
and it is passed to Prisma as
`MotionTextureInput::declared(reactive,w,h,RGBA8)`.
Cases without reactive geometry/callback, or
without an available depth/target/pipeline, are not declared
a successful reactive pass.

### Separate statistics and diagnostics

`Renderer3D::ObjectMotionStats` now has:
`velocitySeedOnly` — first frame / invalid/cut/resize,
so as not to present pose initialization as GPU object
velocity; and `reactiveSubmitted` — a separate flag for an
**actually encoded** current-frame mask.
The historical `submitted` remains an indicator of the
object-velocity pass, not reactive-only.

The PRISMA camera stores
`ViewEffectReport.objectVelocityInputAccepted`
and `reactiveWithoutObjectVelocity`, taking into account a separate
`MotionTextureInput` check: first the declared
width/height/format are checked, then RHI `textureInfo`,
if the backend supports introspection. These fields
do not prove GPU pixel content. The editor shows
`mask-only pass` for reactive coverage without a valid
object velocity texture; the separate MotionBlur
reactive edge rejection status still depends on
an actually encoded blur and the configured parameter.

**Kept unchanged:** stable per-view identity
regardless of two cameras having the same resolution;
`resetObjectMotionHistory(viewId)` and the camera-cut
epoch; GPU-fenced retirement; no ECS mutation;
`Prisma::resolve` and HDR temporal history as
the sole color history owner; HUD outside
post-processing. The reactive mask does not become a
long-lived temporal input.

**Checks actually performed:** readback of 6
source files from the connected GitHub,
42/42 structural source checks, static
C++ brace scope balance 2/2.
**NOT RUN:** CMake/clang/Obj-C++, CTest, shader
compilation, first frame/cut/resize tests on real
Metal/GL/GLES/Vulkan/WebGL, pixel capture,
reactive coverage screenshots, GPU fences,
profiling and CI. Real backend outcomes
must be obtained in the shared validation phase,
not declared PASS from the sources.

Exact commits and the next task:
`docs/handoffs/prisma-stage-62.md`.

## Stage 63 — exact rigid alpha-cutout / dissolve reactive coverage and lifecycle callbacks

This stage extends the current `Renderer3D::renderCurrentFrameReactiveMask`
(Stage 62) without changing the architecture, the `Prisma`/MCP format,
HDR color history or Reactives ownership. The reactive mask remains an
**RGBA8 current-frame** input from `Renderer3D` to PRISMA;
the per-camera motion history is an independent component.

### Rigid models: exact AlphaMask, dissolve and LOD

For rigid models with `AlphaMode::Mask` or `dissolve > 0`,
a separate shader variant was added:
`reactive_shaders::kCutoutVS/kCutoutFS/kCutoutMSL`
and `objectMotionReactiveCutoutPipeline_`.
The main water/transparent/mesh pipeline is left unchanged.

The shader repeats the fragment conditions of the ordinary
`ModelShaders`: `baseTex` alpha, `baseColor.a`,
optional vertex alpha (excluding
vertex-occlusion/AO), `alphaCutoff`, KHR base-UV
offset/scale/rotation, screen-space 4×4 LOD dither
and **the same object-space noise**
`modelNoise3(opos*7)*0.7 + modelNoise3(opos*19)*0.3`
for dissolve. `TeamMode::BaseAlpha` uses
baseColor alpha without baseTexture alpha; for
`TeamMode::VertexAlpha` it does not multiply vertex alpha
twice, following the material. The scene still
samples the foreign `sceneDepth` read-only;
the reactive pass has its own writable
`history.depth`. `BlendMode::Screen` is used
to merge coverage. The shader source provides
the same clipping logic in GLSL and Metal, but **GPU
parity requires real shader tests**.

New uniforms layout only for this pipeline:
`ReactiveCutoutUniforms {mvp,params,lod,uv0,uv1,maskInfo}`,
144 bytes. Vertex attributes are the real
`ModelVertex` `position[0]`, `uv[2]`,
`color[4]`. AlphaCutout texture bind slot 1,
scene depth in slot 0. Since the shader precisely
discards alpha holes, reactivity no longer
paints the whole triangle for foliage/grilles.
Shader Graph vertex displacement and geometry-driven
models still require separate coverage
producers; do not claim them as supported by this
rigid path.

### Animated transitions and callbacks

For skinned/morph/baked translucent models,
`kAnimatedFS` and its MSL generator gained an
optional `U1.reactiveLod` (fragment
`buffer(2)`) with exact 4×4 LOD dithering on
the current frame. Animated LOD and
`dissolve` now provide coverage even when
the previous pose is absent. **Dissolve for
animated objects is conservative** —
the mask may cover fragments that
the main ModelShaders discards by
object-space noise. Exact animated
alpha-cutout/actor aperture requires a new
current-object-space varying or a separate
material-specific shader, not implemented here.

In `Renderer3D`, mask readiness is checked
**per required source**: separately for
simple rigid, cutout/dissolve rigid,
skinned, baked, MeshVertex or
`motionReactiveDraws_`. The presence of an
unsuitable pre-created shader
(for example, a skin pipeline when the
baked pipeline is missing) is no longer grounds
to publish an empty reactive mask. When a
callback was registered for the current
scene frame but the active postprocess, scene
depth or mask pipeline is unavailable,
`ObjectMotionStats.reactiveCallbacksDropped`
counts the unexecuted calls; the callback
is not carried over to the next frame.

The registration contract is unchanged: `addMotionReactiveDraw`
**after** `beginScene`, but **before**
`resolvePrisma`. The renderer invokes
callbacks only with the mask pass open:
the provider must only record draws,
not call `beginPass/endPass`, keep
its GPU resources until submission completes and
combine coverage without destroying
previous pixels. The list is cleared
after resolve, when the postprocess is inactive
or at the start of a new scene frame.
The intermediate mask is not cached
across camera cuts and is not carried between
`viewId`s.

`Renderer3D::ObjectMotionStats` was extended with
`cutoutReactiveDraws`,
`dissolveReactiveDraws`,
`reactiveCallbacksDropped`; the previous
`reactiveDraws/reactiveAnimatedDraws/
reactiveCallbacks/reactiveSubmitted` are kept.
The counters mean CPU-encoded submissions /
callback invocations, **not GPU completion**
or proven pixel content.
The shared per-view `Prisma::ViewEffectReport`
from Stage 62 and the GPU-fenced retirement of
Stage 54 were not changed.

**Actually performed:** GitHub readback of
`Renderer3D.{h,cpp}`,
`Renderer3DReactiveShaders.h`,
`ModelShaders.h` and `Model.h`;
**51/51 static source-presence, binding and
ownership checks**, static balance of
C++ and shader header brace scopes.
**NOT RUN:** CMake/clang, Metal/GLSL/MSL
shader compilation, native editor/game,
CTest, Metal/GL/GLES/Vulkan/WebGL,
alpha-mask/dissolve/camera-cut pixel readback,
multi-view callbacks, memory/fence stress,
profiler and CI. Commit description and
the next stage — `docs/handoffs/prisma-stage-63.md`.



## Stage 64 — precise animated reactive alpha/dissolve and Actor Reveal

Stage 64 completes the bounded functionality of the gap found in
Stage 63: animated skin/morph/baked reactive coverage used to be
produced only as a conservative silhouette, without applying the
material `AlphaMode::Mask`, object-space dissolve and the
local `kDrawActorReveal` aperture. In the new implementation
the ordinary velocity pipeline stays **unchanged**,
and the reactive path gets a separate **GPU shader variant**.

### Reactive-only vertex stage

`Renderer3DReactiveShaders.h` now provides two
testable generation functions:
`reactive_shaders::animatedVertexGlsl(source,baked)` and
`reactive_shaders::animatedMsl(source,baked)`.
They take the existing `kDeformedVS/kDeformedMSL`
or `kBakedVS/kBakedMSL`, substitute UV
(`ModelVertex` attribute 2), vertex color/alpha
(attribute 4) and the **current deformed object-space pose**,
keeping the previous skin/morph/baked sources
(attributes 5/6, U0/U3/U4/U5 and texture 7/8).
The resulting local text variants
are passed to `createShader`; the original
motion shaders and velocity output stay the same.
If the expected source anchor is missing, the generator
returns an empty string, and Renderer3D does not
create an invalid reactive pipeline.

`kAnimatedFS` (GLSL) and the generated MSL fragment
use the new `ReactiveAnimatedU1`, 160 bytes:
`lod[4],uv0[4],uv1[4],maskInfo[4],world[16],
reveal[4],revealAxis[4]`. It occupies the
ordinary uniform slot 1 (Metal buffer 2) and does not
interfere with the skin/baked deformation slot U3/U4/U5.
The shader still renders only
**current-frame RGBA8 reactive coverage**,
does not write velocity and does not use
previous frame history. Depth
is sampled from the foreign `sceneDepth`, and the
writable attachment is the renderer-private
`history.depth`.

For animated objects the same current-frame
clipping conditions are now reproduced exactly:
4×4 LOD dither, UV offset/scale/rotation,
the material cutoff `baseTex alpha *
baseColor alpha` (+ vertex alpha, if allowed),
and `modelNoise3(currentDeformedLocalPos * 7) * 0.7 +
modelNoise3(currentDeformedLocalPos * 19) * 0.3`
for dissolve. The `TeamMode::BaseAlpha` and
`TeamMode::VertexAlpha` settings do not multiply
vertex/texture alpha twice; vertex
occlusion/AO is not treated as opacity.

### Actor Reveal for rigid and animated

`kDrawActorReveal` with an active
`Renderer3D::actorReveal` now makes the
model a source of reactive coverage, even
if it is fully opaque and has no LOD
fade/dissolve. Only the main camera pass
(`geometryMainView_`) passes the
world-space actor center/radius, camera axis
and view-dependent attenuation from the same
`Renderer3DModels.cpp` formula. The fragment
shader repeats the aperture:
`radial=length(delta-axis*depth)`,
`radius=actorRadius*max(0.01,1-depth*axisScale)`,
`coverage=(1-smoothstep(radius*.48,radius,radial))*
smoothstep(.04,.24,depth)` and 4×4
dither discard. In the inactive mode
`reveal.w=0` and this path does not affect
the mask.

For the rigid alpha/dissolve shader,
`ReactiveCutoutUniforms` was extended from 144 to
**240 bytes**, by adding the world matrix and
reveal/axis in U0. GLSL and Metal receive
the vertex's current world position. For animated,
the current deformed local position is used,
transformed through the passed `world`
matrix in the fragment. This is not a separate
draw pass and not an ECS system: the source remains
the same current-frame reactive pass from Stage 62.

In `Renderer3D::renderCurrentFrameReactiveMask`,
replay of animated materials now
sets up `ReactiveAnimatedU1`, a separate
baseTexture sampler slot 1 and the vertex
attributes UV/color/skin. Actor reveal,
dissolve, alpha cutoff and LOD parameters are set
per `ModelItem`. The statistic
`ObjectMotionStats.actorRevealReactiveDraws`
complements the existing `cutoutReactiveDraws`,
`dissolveReactiveDraws`, `lodReactiveDraws`.
These are CPU-encoded draw counts, not confirmation
of GPU visibility. Resources stay with
`Renderer3D`; the existing GPU-fenced target
retirement and stable per-`viewId` ownership
are unchanged.

### Limitations and verification

The exact source formula of ordinary
alpha/dissolve/reveal is supported for rigid and
animated models. Not claimed: per-pixel
parity with arbitrary material Shader Graphs,
vertex displacement, custom transparent
pipelines, GLES/Metal depth conventions or
correct multi-layer transparency —
these must be provided by separate reactive
providers or native GPU validation.
The GPU shader permutations have not yet been
compiled or run on a device.

**Actually:** GitHub readback and
**61/61 static structural/source checks**;
additionally 4/4 generator fixtures
skin/baked × GLSL/MSL with the expected
current-pose/UV/alpha output. **NOT RUN:**
CMake/clang/Obj-C++, GLSL/MSL/SPIR-V compilation,
CTest, editor/client, Metal/GL/GLES/Vulkan/WebGL,
pixel/alpha parity, Actor Reveal cut screenshot,
GPU fencing/memory pressure, benchmarks/CI.
See `docs/handoffs/prisma-stage-64.md`
for exact commits and next actions.


## Stage 65 — source-level catalog closure and safe numeric parameters

Stage 65 carried out a source/API review of **all 19 PrismaTypes**:
`PrismaData::buildCatalog` (full set),
`prismaEffectWork` (complexity estimates),
`Prisma::resolve` (actual dispatch and separate
`EffectExecution` reports). **19/19** effect types
implement all three levels in code. The Scene/Game
Editor and the separate GameClient use
stable viewId=1/2/3, and the GPU-fenced
lifecycle/private histories do not merge
different cameras of the same size.
This means only **feature implementation
coverage**, not passed GPU rendering.

### New fix: native and JSON numeric safety

`parsePrismaEffect` no longer accepts
non-numeric RGB components and does not let
`NaN`/`Inf` through `std::clamp`.
Number/Color values are checked as finite
`double` and clamped to the catalog
range **before conversion to float**.
With strict MCP validation the error becomes
`std::runtime_error`; in normal mode
the problem is added to `issues`, and
the invalid parameter (or the whole RGB triplet)
keeps its original default. Correct old
profiles keep their behavior.

The new `sanitizePrismaSettings(PrismaSettings&)`
in `PrismaData.h/cpp` is a GPU-free normalizer
of direct native/ECS profiles: it restores
missing flattened values from the default catalog,
drops extra ones, replaces non-finite
with default, performs range clamps and
normalizes booleans to 0/1. It is called
in `Prisma::setSettings` before the base
settings are stored, and `PrismaVolumeStack::resolve`
runs it on the final blended
camera profile. In the ordinary rendering
sources these guarantees add no GPU
pass, shader or new resources.

### Functional status and next tests

The full checklist of effects, integration,
the plan for a single build/CTest/GPU parity
and acceptance conditions are recorded in
`docs/prisma-feature-closure-stage-65.md`.
It lists the actual tests registered
in CMake: `prisma_graph`, `prisma_volume`,
`shader_graph`, `hdr_policy`, the shared
`aiverse_tests` and opt-in HDR backend fixtures.

**Actually performed:** GitHub source
readback; 19/19 catalog/cost/runtime/
diagnostics mappings, **36/36 structural
checks** of settings/volumes/hosts/test
registration. **NOT PERFORMED:**
CMake/clang/Obj-C++/GLSL/MSL/SPIR-V,
CTest, Editor/GameClient, Metal/GL/GLES/
Vulkan/WebGL, screenshots/pixel/alpha parity,
multi-view GPU-fence stress, profiling/CI.
Source-level catalog closure does not
permit merging Draft PR #26.

The next agent should read
`docs/handoffs/prisma-stage-65.md`;
start a **single real test stage**
after final approval that the features are complete,
recording actual PASS/FAIL.


## Stage 66 — actual launch of consolidated validation (CI queued)

Stage 65 closed the catalog only **at the source level**. Stage 66
adds an executable GitHub Actions configuration
`.github/workflows/prisma-final-validation.yml`.
It runs on `feature/prisma-postfx` on changes
in PRISMA/Renderer3D/Editor/Client or manually via
`workflow_dispatch`. Linux (`ubuntu-24.04`)
and macOS (`macos-15`) get the same requirements:
`AIVERSE_BUILD_TESTS=ON`, `AIVERSE_BUILD_HDR_TESTS=ON`,
`AIVERSE_BUILD_EDITOR=ON`, CMake Release, a full
`cmake --build ... --parallel 4`, then **one
full** `ctest --output-on-failure -j 4`.
The Linux runner installs X11/EGL/GLES/Vulkan/Mesa/Xvfb
headers for the desktop renderer. After CTest the
workflow attempts to run an HDR fixture:
Linux `aiverse_hdr_scene` via Xvfb (Mesa),
macOS `aiverse_hdr_gl` with a Cocoa/OpenGL context;
the run is limited to 240 seconds and logged,
and a missing binary is explicitly marked as
`GPU SMOKE NOT RUN`, not `PASS`.
The logs `prisma-configure.log`,
`prisma-build.log`, `prisma-ctest.log`,
`prisma-gpu.log`, a possible `prisma-hdr-capture.ppm`
and CTest LastTest.log are saved as GitHub Action artifacts
(`if: always()`), even on failure.

A new test was added to the already existing
`tests/render/prisma_volume.cpp` for the GPU-free
`PrismaData` numeric API: an invalid RGB
element in strict/non-strict mode, NaN and Infinity
in a native profile, a shortened flattened
parameter vector, extra elements and
boolean normalization. The test runs
as part of `prisma_volume` in the full CTest
and does not create a separate GPU target.

**Actual state per the latest GitHub API readback:**
[GitHub Actions run #44597082684](https://github.com/chromealex/AIVERSE/actions/runs/44597082684)
on source/test commit
`aa262d6b44546985429c0cda0abf0338f270c693`
remains **queued** for Ubuntu 24.04 and macOS 15.
Native compilation/CTest/GPU smoke had no
observable result; queued is NOT PASS.
The previous runs 44595169973 and 44595377866
were cancelled via `concurrency.cancel-in-progress`.
From here on, do not change the test source unless necessary,
so as not to cancel the waiting runner again.

The additional `prisma_graph` CTest now
checks in code 4 variants of reactive shader source
(skin/baked × GLSL/MSL), safe rejection
on unsupported shader anchors and the
TEX8/TEX9 sampler plan for 0–4 Shader Graph
textures. The test implementation itself was read
from GitHub; the CTest result is still NOT RUN.

The next agent must read the actual
run/job status, obtain the real job logs
and artifacts, fix build/test/GPU FAILs,
and then record the real commands,
exit code and results in the handoff;
do not promise Metal GPU verification on a physical
Mac Studio — the macOS GitHub runner checks
the machine available to it, not the user's.
Details:
`docs/handoffs/prisma-stage-66.md`.

## Requirements
- Universal render APIs for different genres; no dependency on `modules/rts`/`modules/fps` in PRISMA.
- The transition is gradual: every new path can be disabled, and the original `postfx` format must not break.
- Do not mix the internal HDR linear space with sRGB/UI; do not run TAA and FXAA simultaneously in full mode.
- `PostFxData` stays available headless/native without a GPU; the new volume layer likewise must not require a device.
- Each stage needs a `docs/handoffs/prisma-stage-XX.md` with verifiable facts; record test results honestly.

## Status
- Stage 01: C++ class name and compatibility. Stage 02: GPU-free `PrismaVolumeStack` (opt-in, not yet connected to the camera loop). Stage 03: physical move of the postprocess core into PRISMA. Stage 04: editor/MCP and documentation of system ownership. Stage 07: GPU-free RenderGraph planner. Stage 08: camera profiles at runtime and in the editor. Later stages implement RHI graph execution, DoF, AutoExposure, motion vectors and GPU lifetime; current details and unconfirmed checks are listed in handoffs 35–40.
- See the handoff documents for further stages.

## Opt-in physical atmosphere and local volumetric fog (2026-11-20)

PRISMA owns `PhysicalSky` and `LocalFog` HDR passes. They run before exposure
metering and motion/temporal processing, then normal bloom/tonemap. New target
roles 200/201 do not overlap DoF (50), metering or motion buffers. Sample scene
depth with the existing separate scratch depth attachment; size resources use
PRISMA's normal fence-retirement path. Pipelines shut down after its GPU-idle
barrier. No second PostFx implementation or SpectraFX emitter ownership.

See [physical sky/clouds/god rays](physical-sky.md) and
[local sphere/OBB fog with runtime reveal](local-volumetric-fog.md).
The read-only PHYSIS bridge targets `Prisma&`; the legacy `PostFx` alias remains
compatible. These features are disabled by default. Native region/settings APIs
are owner-wide: a multi-camera host must set each view's cosmetic data before
its resolve, or use a separate Prisma owner. Camera-volume profile blending does
not yet author or interpolate these atmosphere settings. No gameplay changes.

### Structured fog continuation

`FogVolume::structure` optionally shapes local fog with periodic 3D noise,
per-axis world scale, height attenuation and wind; `Prisma::setLocalFogTime`
provides a reproducible presentation clock. Default uniform fog is preserved.
The existing local fog pass owns this, with no extra history or render targets.
See the structured banks section in `docs/local-volumetric-fog.md`. No terrain
sampling, gameplay visibility or PHYSIS state mutation is introduced.

Local fog now also supports `heightVariation`, three-dimensional `warp`, boundary
`edgeErosion` and optional `selfShadow`/`shadowDistance`. Self-shadowing ray-marches
all active volumes and reveal holes toward the sun, independently of the existing
geometry shadow map. This is bounded single scattering; no new temporal buffers,
shadow-map ownership, terrain dependency or authoritative simulation state.


### Inspector graph-v2 preview composition (2026-11-20)

The integrated optional `spectra::GpuPoolWorld` editor preview contributes through
`Editor::drawSpectraGpuPreview()` in the same open HDR scene pass, immediately
before `Renderer3D::resolvePrisma()`. SpectraFX owns emitters, per-frame simulation
and particle GPU resources. PRISMA retains the full-screen resolve, camera volumes,
reactive coverage and viewport GPU timing. Main's existing completion timeline is
preserved; no new postprocess implementation or renderer default is introduced.
See `docs/agent-handoffs/inspector-spectrafx-main-2026-11-20.md` for integration
verification and remaining UI/platform coverage.


## Optional water on the lens

The independent `lensWater` catalog effect is opt-in, configurable and removable
without disabling NEREID underwater absorption. See [parameters and host contract](prisma-lens-water.md).
NEREID supplies read-only camera immersion; PRISMA owns bounded per-view wetness,
post-exit drainage/drying and the shared GLSL/MSL composite. This is a screen-space
approximation; see the separate stage handoff for actual GPU/test coverage.

## Per-view physical-lens defocus (LENS P2, stage 96)

`Prisma::setViewDepthOfField(viewId, DepthOfFieldOverride{enabled, focusDistance,
focusRangeMeters, maxBlur})` replaces the metric focus plane, sharp interval and
maximum blur of the **authored** `depthOfField` effect for one stable view, in the
same way `setViewManualExposure` overrides the authored exposure. The override
forces the metric (camera-linear) branch and keeps the effect's `intensity` and
`occlusionToleranceMeters`. `enabled=false` switches defocus off for that view.
Values are clamped to the catalog bounds on ingress; `clearViewDepthOfField` and
`retireView` remove the override. It still requires the authored effect and
`AIVERSE_PRISMA_DOF=1`; nothing is added to the profile implicitly.

LENS drives it from `CameraLensComponent::driveDepthOfField` through
`CameraPostFxPool`: the thin-lens sharp interval `[near, far]` becomes
`focusDistance = (near+far)/2`, `focusRangeMeters = far-near` (an infinite far limit
is capped at 500 m) and `maxBlur` = background disc radius at infinity in render
pixels (clamped to 16). This is the same 8-tap screen-space defocus, not a physical
bokeh. See `docs/handoffs/prisma-stage-96-lens-physical-dof.md`.


## Authored frame compositor

A game or map can supply `postfx.compositor`. In Studio open Post effects → Frame compositor, load the glow example, and wire an output port to an input port. Edit target names, resolution divisors, pass operations and parameters in Targets and passes. Save graph validates the complete document before replacing the current scope. A map inherits the game graph unless it replaces the postfx block, declares a graph, or disables it with `compositor:null`.

Use the scene color as `scene`. To isolate objects, configure a LENS texture camera with a visibility mask, then add its target asset key to the consumer camera's reads. A compositor source such as `{"id":"selected","cameraInput":0}` reads that first declared dependency. The managed camera pipeline binds those textures per submission and camera. Standalone native hosts call `Prisma::setCompositorCameraInputs` during the frame before resolve. The full example is in `examples/prisma/camera-glow.json`.

Declare 1–12 named targets and one producer per target. A target's optional divisor is 1, 2, 4 or 8. Declare one full-resolution output. A pass has an id, op, inputs and target. Supported operations are downsample, bright (amount is the threshold), blurX and blurY (amount is the radius multiplier), add (filtered first input multiplied by amount plus the second input), and shaderGraph (a nested postfx shader graph in graph, sampled primary input and optional asset textures). PRISMA compiles dependencies before drawing, and temporary targets with disjoint lifetimes share compatible storage. The graph executes after scene contributions and before screen-space lighting, metering and tonemapping. A missing camera source or shader preserves the incoming scene. `effectReport(viewId)->compositorPasses` reports encoded passes. Use `postfx_compositor_set` from MCP and `postfx_get` to read the stored document.
