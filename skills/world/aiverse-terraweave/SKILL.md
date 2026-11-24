---
name: aiverse-terraweave
description: Implement, configure or extend AIVERSE genre-independent procedural terrain, biomes, trees, vegetation placement and ecosystem streaming without breaking existing terrain or deterministic ECS rules.
packages: terrain-pbr, vegetation-pbr
---

For every game/world creation task, first follow `skills/workflow/aiverse-terra-first/SKILL.md`. Use native Terra capabilities before custom environment implementations; vegetation routing is `skills/world/aiverse-terra-vegetation/SKILL.md`.


# TerraWeave — AIVERSE procedural world

Read these in order:
1. `AGENTS.md` (engine contract; determinism and RHI).
2. `docs/terrain/TERRAWEAVE.md` (API, units and compatibility).
3. `docs/terrain/AGENT_HANDOFF.md` (latest verified stage, commits and next tasks).
4. `docs/terrain.md` and `skills/world/aiverse-terrain/SKILL.md` (existing terrain renderer/editor).
5. `docs/ecs-agent-handoff.md` when modifying ECS or parallel scheduler.

> **Current implementation:** Horizon source is at Stage 22; **TWH8** is the latest archive. Earlier TWH6/TWH7 sections below are historical. Feature source commits exist, but CMake/CTest/GPU benchmarks have **NOT RUN** in this stage; PR #24 must stay Draft pending a separate testing agent.

## First decisions
- Identify whether work is generation (`engine/world/TerraWeave.h`),
  existing heightfield rendering, editor operations, tree geometry, foliage
  instancing or gameplay simulation. Never silently mix ownership.
- Preserve the X/Z horizontal, Y-up convention. TerraWeave elevation and
  world positions are signed integer **centimetres**; existing RTS heightfield
  uses 1/16 map units. Conversion belongs to an explicit adapter.
- For gameplay-affecting state use deterministic integer or Fixed, seeded
  stable hashes, and proper rollback/replay/save integration.
- Cosmetic foliage belongs in patches/instance buffers; only interactable
  trees/plants require full entities. Per-blade ECS entities are prohibited.
- Workers evaluate pure generation jobs; ECS structural writes and GPU uploads
  use their owning thread/stage. Check generation revision before commit.
- GPU work only via `gfx::Device`. Every shading change includes GLSL+MSL
  and a mobile/web quality fallback.

## Working phase 1 recipe

```cpp
#include "engine/world/TerraWeave.h"
using namespace aiverse::terraweave;
TerrainSettings settings;
BiomeSettings biomes;
TerrainChunk chunk = generateChunk(settings, biomes, {2, -1}, 32, 100);
FoliageSpecies oak; oak.id = 1; oak.biomeMask = biomeBit(Biome::Forest);
auto foliage = scatter(settings, biomes, {}, {6400, -3200, 9600, 0}, {oak});
```

Ensure species IDs unique; bounds half-open; output positions in cm.
Existing maps must remain unchanged when TerraWeave is disabled.

## Verification and documentation
- Run `aiverse_terraweave_test` and CTest; record exact outcomes.
- For terrain integration, test legacy RTS/FPS sample games, checksums,
  headless/replays, scene screenshots, neighbour tile seam continuity,
  negative coordinates, changed chunk revisions and frame budgets.
- **After every implementation phase**, append accurate status to
  `docs/terrain/AGENT_HANDOFF.md` with SHA, API/files, actual tests,
  limitations and next agent instructions. Never claim tests pass without
  running them. This is part of the deliverable, not optional.


## Arbor BranchLOD and final billboard (Stage 2g–2h)

- Near geometry: use `makeArborBranchLodModel(profile, prototypeSeed, BranchLodSettings)`
  for one shared `render::Model` per species. Each `render::Node` is the
  original trunk/branch with an independent `MeshDef::lods` chain.
  `LeafCluster::parentBranch` associates foliage with its structural branch.
  Never regenerate complete trees per LOD or create ECS entities per branch.
- `buildArborBranchMesh(tree, branchIndex, BranchDetail)` builds local Full,
  Reduced and Minimal meshes. Default transition diameters are 96px / 30px
  *per branch*. Keep high-detail bark where visible while individually
  reducing nearby twigs and distant foliage.
- Far representation: `makeArborBillboardModel(profile, prototypeSeed)`
  generates a transparent RGBA8 tree silhouette and one camera-facing quad.
  This is a real alpha-masked impostor model, but single-view / low fidelity;
  a multi-angle texture atlas with normals is not implemented.
- `ArborForestAdapter::addPrototype` registers and reuses both models per
  species; `uploadModels` and `releaseModels` operate on both on the
  render thread. Use `buildFloraBatches` and `submit` to switch to the
  billboard only for `TreeLOD::Impostor`. Existing Renderer3D chooses
  individual branch mesh LODs and handles normal model batching.
- `ForestRenderOriginCm` and `ForestCameraCm` use integer centimetres;
  `forEachDraw(batches, origin, callback, camera)` can inspect the selected
  near/far model on the CPU. Pass the real camera position for accurate
  impostor yaw when calling directly. `submit` derives it from Renderer3D.
- Verify `aiverse_terraweave_model_test` on GCC+Clang and on real RHI.
  Do not assert CI passed before inspecting logs; use
  `docs/terrain/AGENT_HANDOFF.md` for the exact current verification state.
  The adapter is not yet connected to any editor/game WorldScene automatically.

## Editor and MCP operations (Stage 3a)

When the game uses a **flat RTS grid map**, open the editor's
`Рельеф → TerraWeave` section or use the shared Workspace/MCP tools:

1. `terraweave_preview` with `{"seed":42,"reliefCm":5000,"basePeriodCm":6400,"octaves":5}`.
   It samples 9x9 heights; map contents/revision remain unchanged.
2. `terraweave_generate` with the same arguments to bake the current
   heightfield using existing `MapDoc::corners` and create an Undo step.
   By default refuses to replace authored corners or detail surfaces.
   Set `"overwrite":true` **only** when replacement is explicitly intended.
3. Generator metadata is stored under map `terraweave` with
   `version:1`, but is not automatically re-evaluated on map load.

Do not invoke flat MapDoc operations for world-space (`data.worldSpace`) or planet maps (map formats, debt G2). The
generic C++ generation API works across genres but those formats
need separate scene adapters. Preview is currently numeric, not
a GPU screenshot. Before reporting success, verify the map can undo,
redo, save, reload, and display updated terrain; log all actual test
results in `docs/terrain/AGENT_HANDOFF.md`.


## Stage 3b–3e: multi-view Arbor and opt-in native WorldScene host

- Use `makeArborImpostorAtlas(profile, seed, settings)` for 4/8/16
  camera azimuth slices in **one atlas texture** and **one Model**.
  `ArborForestAdapter` selects the tree-local view via stable
  per-species `Material` overrides and `ModelDraw::materials`;
  preserve those vectors until Renderer3D finishes its frame.
- To reduce forest-wide pop without per-tree transparent overdraw,
  set `FloraView::impostorBlendWidthCm` to an allowed nonzero band;
  the radius is deterministic for each instance. This is **not** a
  per-tree fade.
- Native users can opt in via `TerraWeaveForestScene`: add species
  before first stream/upload, update with `streamAt(cameraCm)`,
  then use `uploadModels(gfxDevice)`, `submit(renderer, originCm)`
  and `releaseModels(gfxDevice)` on the GPU-owning thread.
  `setExclusions` invalidates cached patches.
- `WorldScene` now accepts an OPTIONAL root `terraweaveForest`
  JSON block (see `docs/terrain/TERRAWEAVE.md`) and handles
  chunk cache/update/model lifecycle/draw when explicitly enabled.
  Never apply it silently to existing maps; the legacy RTS
  MapDoc editor is a separate path.
- Scatter work is bounded across the entire scene; invalid density/
  spacing or excessive resident candidate budgets are rejected.
  This initial forest scene is still **synchronous** and uses the
  first observer only. Do not claim worker async streaming, authored
  heightmap matching, navmesh/collision, or GPU validation.
- **No tests ran at user request.** Record every implementation stage
  and exact unverified status in `docs/terrain/AGENT_HANDOFF.md`.

## Scene forest preview (Stage 4, opt-in)

In the editor Terrain window, generate a TerraWeave terrain (recipe
`map.extra.terraweave.version=1`) and enable
`Показывать процедурный лес в «Сцене»`. This previews two shared
Arbor species in the editor's existing `Renderer3D`, including
branch LOD and multiview impostor selection. It does **not** add
gameplay trees to the map, ECS world, collision or save data.
Close the toggle to release GPU resources.

Implementation: `apps/editor/TerraWeaveForestPreview.h` (bounded
camera-relative scatter) and `TerraWeavePlacementMask.h` (editor-only
road/structure/water exclusions and unit conversion).
Renderer callback: `Editor::renderScene()`, after
`Renderer3D::beginScene` and before `render`.
Legacy map cells are always one metre in editor display even if
the generation recipe's source cell size differs from 100 cm; use
the conversion helpers rather than directly multiplying raw
generator positions by 0.01.

DO NOT claim this is async streaming or ECS gameplay. The preview
may stutter on first prototype upload or camera tile changes.
Scene screenshots and GPU benchmark measurements must be collected
before considering this feature production ready. Report tests
actually run in `docs/terrain/AGENT_HANDOFF.md`.

## Authored world terrain and exclusions (Stage 5)

When a native project/WorldScene forest must sit on an already-authored
heightfield, do not rewrite TerraWeave terrain noise or duplicate a
genre's collision data. Use the optional generic ForestScene hooks:

- `forest.setHeightSource(fn)`: `fn(worldXcm,worldZcm)->int32 heightCm`.
  Use a **snapshot** from the project's terrain/physics provider,
  not direct unsynchronised ECS state.
- `forest.setPlacementFilter(fn)`: `fn(const FoliageInstance&)->bool`.
  Reject roads, water, structures and unsafe slopes as necessary.
  The filter receives final sampled tree height.
- Both callbacks invalidate cached forest chunks; call
  `forest.streamAt(observer)` on the safe owning CPU stage.
  Reuse stable per-instance keys for persistence.
- The callbacks are **visual**, not a substitute for deterministic
  authoritative gameplay placement or collision. `scatter` still
  uses its own procedural noise for initial biome/height/slope
  checks; explain this limitation in agent handoff.

See `docs/terrain/TERRAWEAVE.md` and the latest
`docs/terrain/AGENT_HANDOFF.md` stage. Verify on real scenes
before claiming trees match ground physics.


## Authored WorldScene terrain + persistent tree state (Stage 6)

- When a forest is hosted by `WorldScene` with the optional
  `terraweaveForest` block, the world-streamed authored
  `ChunkCollision::terrain` samples are converted to integer cm and
  interpolated along the **same diagonal** as its actual physics
  terrain triangles by `AuthoredWorldHeightSnapshot`. Do not
  re-interpolate a bilinear heightfield for forest positions.
- `WorldScene::update` publishes one immutable snapshot of resident
  authored height tiles and collision box footprints through
  `forest.setLandscapeSources(height, mask)`. That single operation
  invalidates async work once and preserves previously published
  snapshots through shared ownership for pending worker jobs.
  An absent/unloaded tile falls back to procedural TerraWeave height;
  document potential seam/pop when streaming.
- Collision boxes are projected to XY-free world X/Z exclusion
  footprints. `FoliageExclusionMask` uses 64 m buckets for building
  rectangles; roads/water/polygons still use their existing policies.
  Standalone `setPlacementMask` is now applied even when no other
  callback is provided, in both sync and async scatter paths.
- Gameplay interactions should use
  `ForestTreeIdentity { key, speciesId }` plus
  `removeTree` / `restoreTree` /
  `setRemovedTrees` / `removedTrees`. The forest only filters
  presentation; a native game owns versioned persistence,
  server authority, ECS health/resource mechanics, physics/navmesh
  and replication. Do not allocate ECS entities for every branch or
  blade of grass.
- No stage-6 compilation/GPU/CTest was run, at user request. Cite
  `docs/terrain/AGENT_HANDOFF.md` for exact commits, limitations
  and verification status.

## Kilometre-scale Horizon HLOD (new)

Use `terraweaveHorizon` in the **WorldScene manifest** only when the
project needs a visibly distant forest/mountain skyline. It is opt-in.
Read `docs/terrain/TERRAWEAVE.md` § TerraWeave Horizon before changing
limits, CPU generation or renderer integration.

- **Near** detailed tree models, **mid** 128m forest cluster cards,
  **far** 512m cluster cards; do not allocate individual tree ECS
  entities or individual tree impostor draw calls beyond the near
  forest streaming range.
- The Horizon CPU baker samples terrain/biome masks and creates one
  shared atlas. `makeHorizonModel` creates two material primitives
  (coarse terrain + forest clusters). The existing `Renderer3D` and
  `gfx::Device` remain the only render paths.
- `TerraWeaveHorizonScene` uses the existing `AsyncChunkStream` for
  one staged region at a time; publish/upload only on the owning render
  thread, keep the old model displayed until the next completes, and
  discard a canceled cell result. Never start an unbounded future per
  camera move.
- `estimateHorizonBudget` is a **theoretical** atlas/geometry bound;
  do not confuse it with measured GPU draw calls or frame times.
  Default maximum: ~7.75 MB RGBA atlas, <8k triangles, 832 cluster
  slots. Distant trees are implied by the atlas, not generated.
- `distantTerrain` creates a coarse terrain ring outside
  `terrainBeginCm` and must not overlap an independently authored
  far-ground renderer without an ownership/clipping policy.
- Verify CTest `terraweave_horizon`, a running WorldScene and near/mid/
  far screenshots on Metal and GL/Vulkan, shadow consistency, memory,
  camera snapping and transition popping before moving Draft PR to
  review. Document untested limitations in `AGENT_HANDOFF.md`.

Known limits: crossed silhouette cards approximate the forest but are
not baked photorealistic multiview normal/depth impostors; alpha
overdraw, atlas uploads and visible transitions are not literally free.

## Horizon LOD/canopy quality (Stage 8)

`terraweaveHorizon` accepts `crownDetail` (1–3),
`orientationViews` (1–2), `nearBlendCm` (default 6000)
and `tierBlendCm` (default 16000). Default 1-view
is the inexpensive fallback for mobile; 2 views bake
separate 90-degree silhouettes into one ~14.77 MiB
RGBA atlas before mipmaps and keep the same number of
cards/triangles. The one-view atlas remains ~7.39 MiB.
Never allocate per-tree cluster draw calls for this mode.

The HLOD transition is stable **spatial atlas pixel dithering**,
not true continuous camera-time alpha fade. If both
`terraweaveForest` and `terraweaveHorizon` are
enabled, the close distance defaults to the existing
ForestScene's `view.cullRangeCm` rather than a
mismatched constant.

Use `TerraWeaveHorizonScene::runtimeStats()` to
inspect the actual running region's CPU prepare/upload
time, atlas bytes, vertex/triangle counts and pending
job state. These numbers become meaningful only
after execution on a real GPU and do not substitute
for frame-capture screenshots or render-pass statistics.
See Stage 8 of `docs/terrain/AGENT_HANDOFF.md`
for exact unverified checks and next tasks.


## Horizon asset baking, authored surfaces and quality control (Stages 9a–9e)

The implementation agent should focus on shipping function/API changes;
a separate test agent owns all actual build/CTest/GPU verification
for these stages. Update `docs/terrain/AGENT_HANDOFF.md` on each
implementation stage, stating `NOT RUN` explicitly.

- Use `HorizonSurfaceSources` with snapshot-owned
  `heightAtCm`, `biomeAt` and `allowsCanopyAt`
  to align far trees/ground with authored surfaces and
  exclude roads, structures and water. In `WorldScene`, use
  `setSurfaceSources(shared_ptr<const...>)` only on
  landscape snapshot changes, not on every frame. Existing
  `AuthoredWorldHeightSnapshot` supplies resident height tiles,
  procedural height is the fallback outside resident tiles.
- `maxCachedRegions` (0–4) keeps bounded immutable CPU
  Horizon bakes and reports `cacheHits` in `runtimeStats()`.
  `regionHysteresisCm` reduces back-and-forth rebuilds at a
  camera grid boundary. CPU retention does not eliminate GPU
  upload cost.
- Select the `HorizonQualityPreset` minimal/mobile/balanced/high
  or `terraweaveHorizon.quality` manifest string, then override
  individual fields as appropriate. All presets reuse one
  standard renderer and **do not** spawn distant tree ECS
  entities.
- Static worlds can `encodeHorizonArchive(bake, fingerprint)`
  and `decodeHorizonArchive(bytes, fingerprint)` using
  `TerraWeaveHorizonArchive.h` (format `TWH3`,
  explicit little-endian, bounded bytes, source hash and
  noncryptographic payload checksum). Default runtime has
  NO filesystem access; optionally set `HorizonArchiveStore`
  before update, or use manifest `archivePrefix` for
  **read-only** assets `<prefix>/<cellX>_<cellZ>.twh`.
- For authored terrains/masks, archive loading requires
  `authoredContentHash` / `archiveContentHash` to be
  nonzero and to change whenever the authored source
  content changes. Never silently use a procedural archive
  for a newly sculpted terrain. Missing/stale/corrupt
  archives must fall back to bounded CPU regeneration.
- Stage 9e handoff lists concrete race/round-trip/corruption,
  Metal/GL/Vulkan and FPS/RTS tests for the **testing agent**.
  The feature agent must not claim measured FPS or validation
  without those results.

## Horizon offline bulk bake and selective updates (Stage 10)

When a static game needs many Horizon regions, **do not write a new
baker** for each project. Use the host-only
`aiverse_horizon_bake` CMake tool or the reusable
`engine/world/TerraWeaveHorizonBatch.h` API:

```sh
aiverse_horizon_bake --manifest content/world.json \
  --out content/world/horizon-cache --cells -4 -4 4 4
```

The runtime will look up `<x>_<z>.twh` using its opt-in
`terraweaveHorizon.archivePrefix`. For custom authored worlds
call `bakeHorizonBatch` with your immutable terrain/mask
`HorizonSurfaceSources`, build-system writer callback, and
a stable nonzero `authoredContentHash` bound to terrain/road/water
content; the generic CLI is for procedural worlds only.
Archive format is now **TWH2**, not TWH1, because fixed atlas
slots were introduced. Always invalidate and regenerate old files
when the baker format or generator fingerprint changes.

For edit-time terrain modification, call
`TerraWeaveHorizonScene::setSurfaceSources(snapshot, editedRects)`
with world-centimetre half-open rectangles; this allows selective
CPU atlas/terrain sample regeneration when an eligible previous
bake matches. `WorldScene` already passes loaded/unloaded
authored collision/obstacle tile bounds to the Horizon adapter.
Maintain a previous immutable CPU bake only for configured
`maxCachedRegions>0`, keep GPU model until publish, and let
the existing `AsyncChunkStream` own cancel/staging/thread
lifecycle. No new ECS entities or dedicated rendering backend.

**Testing is assigned to another agent** by the user.
The function-implementation agent updates
`docs/terrain/AGENT_HANDOFF.md` after each stage and records
`NOT RUN`; do not add/invoke tests or state a pass without
external executed logs.

## Horizon incremental GPU atlas updates (Stage 11b)

When editing the Horizon GPU path, reuse
`engine/render/TerraWeaveHorizonGpuPatch.h`, not
new device APIs. The pure CPU
`horizonCanReuseModelGeometry` must gate all atlas
subregion updates; `planHorizonAtlasUpload` returns
a bounded plan or nullopt; only `gfx::Device` on
the render owner thread may apply the plan.
Geometry or terrain height/colour changes must
fall back to ordinary model upload; keep the old
proxy until the replacement has uploaded.
Do not claim partial **geometry** GPU updates:
this implementation changes texture subregions
only. The separate testing agent owns CTest,
GPU row-pitch/mipmap validation, screenshots and
performance benchmarks. Log all outcomes in
`docs/terrain/AGENT_HANDOFF.md`.

## In-place GPU Horizon terrain updates (Stage 12)

When an authored distant terrain tile changes, first try the
CPU partial Horizon bake and the **existing** GPU atlas patch path.
For a terrain vertex-only or mixed atlas+terrain edit, use
`engine/render/TerraWeaveHorizonVertexPatch.h`:

- The Horizon Model MUST have
  `batching.allowGeometryPool=false` prior to GPU upload.
  Never update a shared pool page using model-relative offsets.
- `planHorizonVertexUpload` rejects changed topology/index
  streams, altered cluster card geometry, relocated UV slots
  and any increase beyond existing conservative model/mesh/
  primitive bounds. It limits work to 20 contiguous ranges
  and 35% of terrain vertices by default.
- Check `horizonCanApplyVertexUpload` on the RHI-owning
  thread, then apply planned `gfx::Device::updateBuffer`
  calls. If the planner rejects the edit, do the safe full
  model upload rather than forcing an in-place change.
- Track `vertexPatchCount`, `vertexPatchBytes`,
  `vertexPatchRanges` and `fullUploadCount`. They do
  not establish measured performance until tests run.
- GL/Vulkan/Metal updateBuffer has a void return and GPU
  device loss isn't transactional. Test agent must validate
  offsets, bounds/culling and driver synchronization.

**Testing ownership:** Another agent runs all build/CTest,
GPU and performance checks. The current feature agent should
update `docs/terrain/AGENT_HANDOFF.md` with implementation
commits and `NOT RUN`, but must not claim success.

## GPU Horizon region cache ownership (Stage 13)

For forests spanning many kilometres, opt into
`HorizonSettings::maxCachedGpuRegions`
(0–3) and `gpuCacheBudgetMiB` (8–96)
to retain recent fully uploaded `render::Model`
regions without copying GPU resources. Use
`HorizonGpuRegionCache` from
`engine/render/TerraWeaveHorizonGpuCache.h`;
do not create per-species or per-tree GPU page
caches.

All cache `take`, `retain`, `prune`, and
`clear(device)` operations run on the
RHI owner thread. Do not cache a model from
an old authored `sourceRevision_`; never
submit a cached model after a terrain/mask
revision that could affect it. Keep the
last visible model while the new region
bakes and uploads, then retain the old
one if within the bounded LRU budget.
Always drain `AsyncChunkStream` canceled
results even when a GPU cache hit returns
without a new worker submission.

`WorldScene::shutdown(device)` MUST release
the active GPU model and every inactive
cached GPU model before the device dies.
Use `HorizonRuntimeStats` GPU cache fields
to diagnose retention; estimated GPU bytes
are NOT driver measured or GPU performance.
Build, sanitizer/CTest, Metal/GL/Vulkan,
WebGL, and benchmark execution are assigned
to the dedicated test agent. Record
`NOT RUN` until that agent provides logs.


The inactive GPU LRU also reports `gpuCacheEvictions`
alongside `gpuCacheHits`, `gpuCachedRegions` and
`gpuCachedBytes`. High eviction counts during
back-and-forth camera movement indicate an overly small
budget or frequent authored-source invalidations; adjust
`maxCachedGpuRegions`/`gpuCacheBudgetMiB` only after
the test agent collects real FPS and memory evidence.

## Horizon region crossfade (Stage 14)

The same `TerraWeaveHorizonScene` can use
`regionTransitionMs=0..1000` to interpolate
between two *already GPU-uploaded* Horizon
regions using standard `Renderer3D` model
tint alpha and its existing transparent pass.
Neighbor transitions are 240ms by default
in Balanced/High and OFF by default in
Minimal/Mobile. The old model must consume
an inactive GPU residency slot and fit
`gpuCacheBudgetMiB`; do not add an
unbounded transition buffer.

Call `finishTransition(device)` on the
owner RHI thread, keep authored-source
revision checks, and release the outgoing
model or return it to the GPU LRU when
the fade finishes. Fast teleports and
terrain revisions intentionally skip
blending. `WorldScene::shutdown(device)`
must also release any pending outgoing
model.

`HorizonRuntimeStats` tracks
`transitionStarts`, `transitioning`,
and `transitionProgressPermille`.
Tests and GPU screenshots remain the
separate testing agent's responsibility;
the feature agent must label results
NOT RUN until actual evidence exists.

## Filtered far-ground / canopy transition (Stage 15)

When modifying Horizon transitions, never blend two
full combined distant terrain meshes. Use the engine
generic `ModelDraw::primitiveMaterialFilter`:

- `-1` is the normal entire model (steady-state);
  default native project behavior remains unchanged.
- Source material `0` is one **opaque incoming**
  far-ground layer during crossfade; draw it only once.
- Source material `1` is the forest cluster canopy:
  fade outgoing/incoming models using existing
  `Renderer3D` transparent tint alpha. Partial draws
  bypass aggregate shadow/material batches and
  static cache; don't add a GPU model copy or
  separate GL/Metal/Vulkan implementation.
- Transient material-filtered draws use no persistent
  scene identity or static light-bake capture. The
  steady state returns to full unfiltered static draw.
- A→B→A while fading should reclaim the old GPU
  model directly from transition ownership; don't
  force it through a one-slot LRU that could evict it.
  Teleports outside the old far-footprint skip
  stale proxy submission until a new one arrives.
- The existing GPU cache/revision/budget checks and
  render-thread release remain mandatory.
  Document alpha/skyline limitations and leave
  CMake/CTest/device validation to the testing agent.

## Horizon far-terrain global lattice and outer skirts (Stage 16)

Never return to camera-local height sampling with
`origin + x*side/resolution`; that changed height
sampling phase between neighboring 512m camera regions
for High 64×64 terrain and non-dividing resolutions.
Always use the globally anchored signed rational
grid `floorDiv(globalIndex*regionSide,resolution)`,
also for negative world positions.

`HorizonTerrainVertex` now records
`slopeXPermille/slopeZPermille` sampled from
both neighboring globally aligned height values,
including ghost samples beyond the current patch.
For authored edits, invalidate an extra grid-step
band so dependent border normals are refreshed.

`HorizonSettings::terrainSkirtDepthCm` (0..5000)
adds a downward-facing outer skirt **inside
material slot 0** and without a new draw call.
Minimal quality disables it; Mobile defaults to
600cm and Balanced/High to 1200cm. Include
the added 4(N+1) vertices / 8N triangles in
analytic geometry budgets; changing skirts can
trigger safe full-GPU-model fallback.

Current static-content archive is **TWH3**;
TWH1/TWH2 files are obsolete and must be
rebuilt via `aiverse_horizon_bake` or runtime
fallback. World-origin agreement only holds
when the two bakes sample the same immutable
authored terrain snapshot. No CMake/CTest/GPU
results are claimed in this stage; the dedicated
testing agent owns regression and visual checks.

For standalone native terrain/HLOD producers, call
`makeHorizonTerrainLattice(settings,cameraXcm,cameraZcm)`
and `worldX(i)/worldZ(i)` (including ghost indices)
instead of duplicating grid arithmetic. The same helper
is used internally by `bakeHorizon` and protects against
invalid zero-resolution division.

## Horizon near-ground ownership and TWH4 (Stage 17)

Do not delete far Horizon quads just because
they are within a hard-coded radius. Prefer
`HorizonSurfaceSources::ownsNearTerrainRect`
when resident near-ground geometry is
available. `WorldScene` uses immutable
`AuthoredWorldHeightSnapshot::coversRect`
to assert **full** half-open rectangle
coverage. Missing near terrain must retain
far-ground geometry; a partial overlap must
not be falsely classified as fully covered.

`HorizonBake::nearOwnedQuads` carries
one 0/1 flag per far cell. `makeHorizonModel`
removes fully owned quads, and optionally
adds a short `innerTerrainLipCm` (0–200cm,
default 40cm), bounded to 512 seam
segments and **no extra material or
draw call**. Incomplete coarse cells can
still overlap near meshes; don't claim
exact polygon clipping.

The far-terrain lattice uses
`terrainGridSnapSteps` (1..16, default 4)
and nearest signed global-grid alignment
to reduce mountain silhouette movement
during 128m forest-atlas updates.
Minimal=1, Mobile=2, Balanced/High=4.
The distant forest radius must remain
inside the guaranteed terrain coverage
for those snap offsets (roughly 5+ km).

Current archive is **TWH4**. Near ownership
maps, inner seam depth and snapped grid
settings must be reflected in archive
version and fingerprint; old TWH1–TWH3
assets must be rebaked. Keep settings
and bake extensions trailing so native
C++ aggregate initializers stay usable.
GPU index topology changes invalidate
partial VB/atlas reuse and trigger
full upload. All validation is owned
by another test agent; mark NOT RUN.

## Exact near-ground rectangle clipping — TWH5 (Stage 18)

For `WorldScene` worlds with loaded near terrain,
implement `HorizonSurfaceSources::ownsNearTerrainRect`
AND `collectNearTerrainRects` using a single immutable
height snapshot. The second callback returns sorted,
non-overlapping half-open world-centimetre rectangles;
the core accepts at most 1024 per far region.

`planHorizonNearGroundClipping(bake)` in
`engine/render/TerraWeaveHorizonClip.h` uses only CPU
polygon subtraction, preserves the two original coarse
triangle planes and their winding, and emits no more
than `maxNearClipTriangles` partial triangles. The
`makeHorizonModel` stage interpolates vertex height,
normal, UV and colour barycentrically on those planes.
All geometry remains on material slot 0, and the
existing forest material remains slot 1. Do not
create one material/model/draw per near tile.

Quality caps: Minimal 0 (feature off), Mobile 512,
Balanced 2048, High 4096; manifest accepts 0..8192.
A geometric budget overflow triggers a complete
fallback to coarse Stage 17 full-quad ownership,
not partially applied clipping. UI diagnostics:
`nearClipQuads`, `nearClipTriangles`,
`nearClipFallback`. In-place GPU patch reuse
MUST compare stored clip rectangles/settings and
fall back to a full model upload on topology changes.

Current static atlas archive is **TWH5**. It stores
bounded near coverage rectangles and a cap with a
new fingerprint; TWH1–TWH4 must be regenerated,
not silently interpreted as current data. The cut
is exact in X/Z, not a height-aware vertical weld;
keep that documented until real GPU tests prove
a stitched seam. Separate test agent owns all
build/CTest/platform rendering/performance work.

## Height-aware Horizon near/far seam welding (Stage 19 / TWH6)

For authored near terrain, supply **all three callbacks from the same
immutable snapshot**: `ownsNearTerrainRect`,
`collectNearTerrainRects`, and
`nearGroundHeightAtCm` (optional signed-centimetre surface
height, nullopt outside resident near-ground tiles).

The CPU worker or offline `bakeHorizonBatch` must call
`populateHorizonNearSeamSamples(bake,source)` after
`bakeHorizon` and before converting to a render Model,
caching or archiving it. Do not query mutable collision/ECS
data from `makeHorizonModel` or the renderer. The sample
table uses Q16 world-centimetre X/Z keys and max 4096
unique near-edge positions; a missing required height
must disable the *whole* optional height-weld table rather
than leaving a half-welded seam.

`makeHorizonModel` replaces only exact clipped-edge Y
with the baked near-tile surface height and retains the
far-source plane on interior clipped vertices. This is
still the **same** bounded triangles and single
material-0 terrain primitive. Observe
`weldedNearVertices` and `maxNearWeldDeltaCm`
in `HorizonRuntimeStats` when deciding whether extra
normal/slope smoothing is needed.

The current archive is **TWH6**, not TWH5. It persists
all bounded seam samples and changes the generator
fingerprint; TWH1–TWH5 static assets must be
rebaked. Tests, CPU clipping fixtures, CTest, MSVC/Clang,
Metal/GL/Vulkan captures and benchmarks belong to the
dedicated testing agent; do not mark them passed from
source code review alone.

## Horizon seam slope and normal policy (Stage 20 / TWH7)

When building precise near/far terrain cuts, use
`maxNearWeldSlopePermille` (0–8000, default 2500)
and `nearSeamNormalBlendPercent` (0–100, default 65)
from shared `HorizonSettings`. Both are configured
in the opt-in `terraweaveHorizon` manifest.

`populateHorizonNearSeamSamples` must preflight the actual
3D triangle wedge BEFORE committing any near-height samples.
If a weld newly exceeds the slope limit, **clear the entire
near-height sample table** and set
`HorizonBake::nearWeldSlopeFallback`. Preserve the accurate
X/Z near rectangle clipping; do NOT silently clamp a seam
vertex's Y and describe it as welded. A large near/far
height difference may need a future wider transition mesh.

`makeHorizonModel` groups clipped corner positions by
world X/Z plus resulting Y and uses area-weighted repaired
face normals. Only the existing clipped vertex normal
attributes are blended; the near collision or authoritative
terrain never changes. Avoid extra tree draws or new GPU
geometry solely for normal shading.

`HorizonRuntimeStats::smoothedNearNormals` and
`nearWeldSlopeFallback` survive GPU model cache reuse.
Current archive is **TWH7**; all previous TWH1–TWH6 assets
must be rebaked after the new version/fingerprint. Keep
the per-stage handoff and explicitly mark tests NOT RUN
until the separate test agent supplies compiler, CTest,
and real Metal/GL/Vulkan evidence.

## Q16 authored seam normals and TWH8 (Stage 21)

Use `AuthoredWorldHeightSnapshot::sampleQ16` for precise
fractional near-terrain X/Z, preserving the old `sample`
API for integral world-centimetre callers. Provide
`HorizonSurfaceSources::nearGroundHeightAtQ16Cm` from
the SAME immutable authored source as
`collectNearTerrainRects` and `nearGroundHeightAtCm`
where possible. Native games may omit the Q16 callback
to retain legacy integer-centimetre seam behavior.

`populateHorizonNearSeamSamples` samples exact clipped
seam positions (Q16), clamps high half-open tile
boundaries by only one Q16 unit, and computes optional
bounded slope X/Z with a +/-200cm tile-local stencil.
Missing derivative input marks `nearNormalValid=false`;
never fabricate a normal or partially weld a missing
height. The Stage 20 `maxNearWeldSlopePermille`
all-or-none safety guard remains mandatory.

In `makeHorizonModel` use authored near-surface
normal targets at welded edges when available; use
Stage 20 area-weighted face normals otherwise.
`HorizonRuntimeStats::matchedAuthoredNearNormals`
reports actual authored-normal matches; it is distinct
from `smoothedNearNormals`. Neither changes GPU
geometry allocations or draw calls.

**TWH8 is the current archive version.** It stores two
signed normal slopes and a validity bit alongside each
height sample; existing TWH1–TWH7 binaries are stale.
Atlas-only patch eligibility must compare all seam
normal/height fields; a vertex patch can apply if the
index layout and bounds are still safe. The test agent
owns all CMake/CTest, archive roundtrips, cross-platform
GPU screenshots and profiling. Label them NOT RUN
until logs exist.


## Stage 22 — current Horizon seam and diagnostics recipe

These rules apply when implementing **or consuming** TerraWeave
Horizon in native C++ games, the generic WorldScene adapter or editor
preview. Do not introduce genre-specific FPS/RTS dependencies.

1. **Capture one immutable authored height snapshot per bake.**
   Keep `HorizonSurfaceSources::nearGroundHeightAtCm` and optional
   `nearGroundHeightAtQ16Cm` from the SAME snapshot used by
   `collectNearTerrainRects`. If available, also supply
   `nearGroundContinuousSlopeAtQ16Cm` with signed Q16-centimetre
   arguments and an `optional<pair<int32_t,int32_t>>` result.
   `WorldScene` already wires these callbacks. Native providers may
   omit optional callbacks without breaking older terrain.

2. **Use neighbor-aware normals only across continuous authored tiles.**
   `AuthoredWorldHeightSnapshot::continuousSlopeQ16` uses a bounded
   200cm default radius and 1cm default boundary-height tolerance.
   It returns signed X/Z permille slopes, or nullopt when a tile is
   missing or has a real seam discontinuity. In
   `populateHorizonNearSeamSamples` prefer the callback, then the
   existing tile-local Q16 derivative, then
   `makeHorizonModel`'s area-weighted face-normal fallback.
   Never invent missing normals; do not alter authoritative physics,
   collision, triangle topology, weld slope limits or RHI layouts.

3. **Read `HorizonRuntimeStats` as a state machine, not as raw numbers.**
   It is returned by `TerraWeaveHorizonScene::runtimeStats()`.
   Use these conditions:
   - `requestedRegionPresent` before reading `requestedCellX/Z`.
   - `activeRegionResolved` before reading `activeCellX/Z`.
   - `activeRegionEmpty` means an accepted no-model bake, not a job still running.
   - `activeRegionPresent` means drawable model only.
   - `requestedRegionPending` means selected target is unresolved,
     different from active cell or at an old source revision; consult
     `preparing` separately for actual CPU worker activity.
   - `staleProxy` means `activeRevision != sourceRevision` for a resolved
     active cell, including resolved empty cells; it does not by itself
     report a wrong-camera-cell teleport.
   - Cell X/Z values are middle-tile grid coordinates, not centimetres.
     A requested region is chosen after camera hysteresis.

4. **Never interpret unknown provenance as zero.**
   Only present `continuousNearNormalSamples` and
   `localFallbackNearNormalSamples` as measurements when
   `nearNormalProvenanceAvailable` is true. They count **unique seam
   sample points**, not pixels, GPU vertices or draw calls.
   `matchedAuthoredNearNormals` is a **render-vertex** count.
   TWH8 archives store sampled slopes/validity but intentionally do NOT
   store provenance; archived bakes can report unknown counters.
   A fresh bake may legitimately have valid zero counters.
   GPU LRU retention/restoration propagates provenance and availability.

5. **Preserve lifecycle and patch semantics.**
   The CPU worker produces bounded immutable bakes; only the owning
   render thread may change RHI state. The Horizon scene may retain
   the previous GPU proxy when the authored source revision advances.
   `shutdown(device)` clears active/requested cells and diagnostic
   stats. Atlas-only patch checks must reject altered seam normal/height
   fields; use the existing compatible partial vertex update or full
   upload fallback. The current **TWH8** layout/fingerprint must remain
   unchanged unless a deliberate format migration is documented.

6. **Handoff after every code or documentation stage.**
   Record the exact branch and source commit hashes, files/API touched,
   actual verification (do not claim CTest/GPU validation from a GitHub
   commit), limitations and the next implementation/test task in
   `docs/terrain/AGENT_HANDOFF.md`. Keep PR #24 Draft until separately
   verified.

### Practical host-side status check

```cpp
const auto stats = horizon.runtimeStats();
if (stats.requestedRegionPending) {
    // Not resolved for selected camera cell/revision. Check stats.preparing.
}
if (stats.activeRegionResolved && stats.activeRegionEmpty) {
    // Resolved empty region: do not keep requesting a mesh just because
    // activeRegionPresent is false.
}
if (stats.staleProxy) {
    // Rendered/resolved result reflects an older source revision.
}
if (stats.nearNormalProvenanceAvailable) {
    reportSeamNormalSamples(stats.continuousNearNormalSamples,
                            stats.localFallbackNearNormalSamples);
} else {
    reportUnknownSeamNormalProvenance();
}
```

The reporting functions above are pseudocode host callbacks, not
AIVERSE APIs. For the exact field semantics and source callback
contracts, see `docs/terrain/TERRAWEAVE.md` Stage 22.

### Required independent test-agent acceptance

- C++20 build for target platforms; no transitive include assumptions.
- Q16 negative coordinates, integer/fractional heights, adjacent
  continuous/discontinuous/missing tiles and half-open corners.
- Source-revision changes while GPU proxy remains, cache hit, partial
  VB/atlas/full GPU paths, empty model, teleport, camera hysteresis
  and shutdown.
- TWH8 archive hit vs newly generated provenance availability,
  exact old-format rejection and never asserting unknown counts
  as observed zeros.
- Verify unchanged draw/index/triangle budget, capture real
  Metal/GL/Vulkan screenshots and benchmark overdraw/frame time.

**Feature-agent validation state: NOT RUN** (code/docs committed only).

## Stage 24 — forest harvesting and regrowth lifecycle rules

**Source feature committed; tests intentionally deferred.** Native projects can use the existing `ForestTreeIdentity{key,speciesId}` and the new sparse `ForestRegrowthSchedule` to restore harvested decorative trees without allocating one ECS entity per tree, rebuilding scatter patches or uploading new models.

- `TerraWeaveForestScene::removeTree(id)` hides a tree and cancels its regrowth schedule. `restoreTree(id)` clears both hidden and scheduled state; `setRemovedTrees(ids)` replaces saved removal data and overrides any matching schedules.
- `scheduleRegrowth({id,regrowAtTick,matureAtTick,initialScalePermille})` atomically replaces that key's schedule and clears its explicit removal. The tree is not rendered until `regrowAtTick`; thereafter its *presentation-scale copy* interpolates from 1..1000 permille to the generator's full scale. `matureAtTick==regrowAtTick` is instant.
- `setPresentationTick(gameplayTick)` uses authoritative simulation/replay time; do **not** use wall time, worker elapsed milliseconds, global RNG, floats or genre-specific components. Tick changes do not invalidate async chunks. Persist sorted `regrowthSchedules()` separately from `removedTrees()` in native save/replication; `setRegrowthSchedules` validates/restores bulk schedules.
- Bounded max 1,000,000 records and max 1e12 tick interval prevent unbounded work and integer arithmetic overflow. Duplicate saved keys, invalid scales/ticks and zero species must be rejected.
- `world::WorldScene::forestScene()` returns the optional generic forest pointer (null when the world manifest did not opt in). For host-side manipulation include `engine/world/TerraWeaveForestScene.h` explicitly; avoid genre adapters in engine/world.
- `ForestSceneStats::scheduledRegrowth` and `presentationTick` are trailing diagnostics. Regrowth is **visual**: the gameplay authority owns resources, collision, destruction, navmesh, persistence and replication. Far Horizon cluster HLOD does not yet reflect per-tree removals; do not claim forest-wide harvest parity.

**Next functional work** before the separate test phase: add bounded harvested-area masks / multi-tree clearings in Horizon from authoritative world patches, or a compatible seasonal growth/dormancy source shared by Flora and Horizon. Do not add tests or run CTest in this feature-first phase unless the user changes priorities. Always append an exact per-stage handoff record after coding.

## Stage 25 — authoring runtime forest clearings across LODs

Use `world::WorldScene::setVegetationClearings(std::vector<terraweave::RectCm>)` to opt in to whole-area forest removal or restoration. This is **not** the same as the sparse per-tree regrowth overlay in Stage 24: rectangle clearings are incorporated into a shared, immutable `FoliageExclusionMask` used by both `TerraWeaveForestScene` and `HorizonSurfaceSources::allowsCanopyAt`. Changed and removed clearings must rebake only via the existing async source-revision invalidation, not by mutating GPU models or a worker-captured callback in place.

- Coordinates: signed world centimetres, half-open min/max X/Z. Rectangles must be nonempty, within `kWorldLimitCm`, <=4096 input shapes. The setter sorts and deduplicates, making unchanged calls no-ops.
- Keep previous **and new** rectangles in the Horizon edited bounds when changing/clearing masks. Otherwise older canopy atlas cards can stay visible after restoring an area. Use the existing bounded full-rebake fallback for oversized edits.
- `WorldScene::update()` is the source publication point. Native hosts call the setter on the scene-owning update thread, never concurrently with a renderer draw or async callback.
- Save `vegetationClearings()` and restore via the setter; gameplay still owns authoritative construction, resource harvesting, collision/navmesh, network and save versioning. Do not introduce RTS/FPS-specific dependencies or per-tree cosmetic ECS entities.
- The far HLOD atlas currently samples mask positions rather than tracking every individual generated near-tree identity. Rectangular clearings suppress far canopy; Stage 24 individual regrowth is near-only. Fully empty cluster cards and visual alpha transitions still require future optimization, not an unverified claim of perfect GPU performance.

**User preference:** finish functional features before running builds/CTest. After each coding stage append an agent handoff with exact commits, changed paths, public API, *actual* checks, limitations and next feature. All GPU/CI tests remain assigned to the later testing stage.

## Stage 26 — native tree selection without per-tree ECS entities

For gameplay interactions that require identifying a nearby planted tree, call `TerraWeaveForestScene::nearestTreeAt(xCm,zCm,radiusCm)` or `treesInRect(RectCm,maxResults)`. They query *already-resident* CPU foliage patches and share the exact removal/regrowth filtering of `visibleBatches()`. Avoid rebuilding procedural chunks merely to select one tree. Use `ForestTreeIdentity{key,speciesId}` as the stable handle, never the position of an instance in a render batch.

Queries use signed world-centimetre coordinates and are bounded: a nearest radius of 1..10 million cm, or up to 100,000 returned rectangle results (caller should normally request far fewer). Tied distance sorting is deterministic by species/key; rectangle queries sort the result by the same identities. The source does not include remote/unloaded trees. The authoritative game retains collisions, harvesting decisions, inventory, pathfinding, networking and save state; no RTS/FPS-specific runtime is required.

**Features-first policy:** do not start a CTest/GPU troubleshooting loop until feature implementation is complete or the user reprioritizes. Append exact code/docs commits and next step to `docs/terrain/AGENT_HANDOFF.md` at every implementation stage.

## Stage 27 — seasonal canopy policy shared with Horizon

Use the reusable `engine/world/TerraWeavePhenology.h` POD `CanopyPhenology` for biome/year/weather-driven **presentation**. It defines broadleaf/evergreen cover [0,1000] and RGB tint multipliers [0,2000], all integer permille. A fully default-constructed value MUST preserve the previous green, full-canopy output.

- Native `WorldScene::setVegetationPhenology(value)` or directly `TerraWeaveForestScene::setPhenology(value)` applies per-species Arbor leaf-material and shared impostor-override appearance on the caller's render/update thread. No near scatter resample or per-tree ECS entity is needed. Conifers use evergreen cover; other TreeProfiles use broadleaf cover. Bark stays unchanged on near geometry.
- `HorizonSurfaceSources::phenology` is an immutable value in the same worker snapshot as authored heights, biomes, masks and near seam sources. Horizon CPU baker deterministically thins sampled broadleaf/conifer crowns and adjusts leaf RGB in the **existing** shared atlas; near-ground height and terrain mesh remain unchanged.
- Changing seasonal policy in `WorldScene` queues one Horizon source revision on the next update, not a terrain edit, and does not cancel Flora workers. Do not mutate `ModelDraw::materials` borrowed vectors after draw submission until the frame finishes.
- Since TWH8 stores **no season key**, its static disk archives MUST NOT be used for a nondefault policy. Keep the nondefault-season bypass until a deliberate multi-season fingerprint/archive asset migration is implemented. Default archive reuse continues.
- Seasonal alpha blending can increase GPU overdraw. An impostor billboard combines trunk and leaves in one atlas, so season fade applies to the whole impostor; do **not** claim pixel-accurate leaf-only seasonal impostors or identical near/far morphology. Future improvements need dedicated trunk/leaf channels or an explicit shared shader.
- PHYSIS/weather/calendar modules are upstream **providers only**. Never introduce direct PHYSIS-to-renderer dependencies or genre-specific seasons inside TerraWeave. The host chooses values and persistence/replication policy.

**Validation intentionally deferred:** no CMake, CTest, graphics backend screenshots or measurable perf evidence yet. User requested features first. Update `docs/terrain/AGENT_HANDOFF.md` after each functional step.

## Stage 28 — PHYSIS-neutral per-tree wind presentation

Use `engine/world/TerraWeaveWind.h` and `ForestWindState` to pass optional wind from a game's PHYSIS/weather adapter; do not link TerraWeave directly against any genre or physical-weather solver. The caller provides horizontal world X/Z velocities in **cm/s** and a simulation tick. Reject magnitude beyond 5000cm/s, gust outside [0,1000], cycle outside [8,4096] ticks or max lean beyond 3000 millidegrees. Default zero velocity preserves previous rendering.

- Native `WorldScene::setVegetationWind(value)` and `setVegetationPresentationTick(tick)`, or direct `TerraWeaveForestScene::setWind` / `setPresentationTick`, feed `ArborForestAdapter`.
- `forestTreeLean(state,tick,stableKey)` returns bounded signed milli-degree X/Z lean using integer piecewise-linear waves with stable hashed phase. Root sway transforms are applied in world space before local tree yaw, after origin rebasing. Do not feed renderer sway transforms into authoritative collision, navmesh, physics, networking, replay checksums or harvest calculations.
- This is **whole-tree cosmetic lean**, not animation of individual branches, leaves or trunk deformation. Impostor quad leans as a unit; far Horizon is intentionally static. Zero-wind path is unchanged; no new GPU models, passes, procedural chunk rescatter or Horizon archive migration.
- Host must not update wind state during `Renderer3D` submission/rendering. Use deterministic tick values and make scene setup/update single-thread-owned. Tree-model GPU buffers are only touched by the existing upload/release lifecycle.

**Testing is postponed by user instruction.** The later agent should benchmark large forest draw rates, camera motion and origin rebasing, verify zero-wind parity, amplitude bounds, negative wind, different tree keys and native PHYSIS integration, plus real Metal/GL/Vulkan visuals. After each feature commit, record exact source SHA, API, actual checks, limitations and next agent task in `docs/terrain/AGENT_HANDOFF.md`.

## Stage 29 — bark/foliage impostor atlas separation

For `makeArborImpostorAtlas(profile, seed, settings)`, assume **two** mask layers per azimuth in **one** shared RGBA8 atlas: bark tiles first, foliage tiles second. The resulting model has ONE node and mesh, two cutout primitives (bark material slot 0, foliage slot 1), 8 vertices, 12 indices. The foliage quad sits 2cm toward camera-facing local +Z to avoid z-fighting. The output `viewMaterials[azimuth]` contains two material overrides with independent UV offsets/scales referencing `baseTex=0`; these vectors must remain alive until the renderer finishes.

- Never seasonally tint or fade `materials[0]` of the impostor: the bark stays visible. Apply `CanopyPhenology` to foliage `materials[1]` only. At 0% cover set `ModelDraw::primitiveMaterialFilter=0` (bark only); restore `-1` for partially or fully leafed trees. The same source primitive convention is used by near Arbor BranchLOD.
- Max atlas width is 8192 pixels and must account for *both* pages: `2 * tile.width * azimuthViews <= 8192`. The change doubles default atlas RGBA8 bytes and can increase material batches/draws. Do not claim zero GPU cost, and do not split this into per-tree GPU textures.
- The *standalone* `makeArborBillboardModel` remains a legacy single-view, single-primitive utility. Do not apply the new 8-vertex layout to it without updating all consumers.
- Existing model test `tests/terraweave_model_test.cpp` still asserts the OLD multi-azimuth 4-vertex/6-index/one-material and atlas width. **Tests are intentionally deferred** while new features are being finished; the testing agent must update the assertions and run targeted/CI GPU validation later.
- The far `Horizon` canopy uses a different statistical HLOD atlas; these near-tree bark/leaf pages do not alter TWH8 archives or the far forest tree samples.

Per user instruction, after each feature stage append `docs/terrain/AGENT_HANDOFF.md` listing exact commits, affected APIs, actual verification, limitations, next concrete step, current branch and PR state. Never label a source-only commit as successfully compiled or GPU-tested.

## Stage 30 — rigid hierarchical BranchLOD wind with bounded poses

Use `ForestWindState::branchFlexPermille` (0..1000, default 600) to opt into or tune independent near-tree branch joint motion. Keep `xCmPerSec/zCmPerSec` and caller tick from existing PHYSIS-neutral bridge. `0` disables only per-branch articulation and preserves Stage 28's root-only sway. Do not create gameplay ECS components for cosmetic tree joints.

- `ArborForestAdapter` caches high-detail tree branch start pivots per **species prototype**, then evaluates child rotations relative to their parent node transforms using existing `ModelDraw::nodeWorld`, without CPU mesh rebuild, skinning, new shaders or RHI allocations.
- High/Medium/Low near foliage tiers use 100/65/25% of requested branch flex; far impostor and Horizon use no branch articulation, preserving cheap distant rendering. The original whole-tree root tilt remains for every tier.
- `TerraWeaveForestScene::setBranchPoseBudgetBytes(0..8MiB)` sets the per-forEachDraw CPU pose memory ceiling (default 4MiB). When exhausted the remaining trees use root-only motion, not unbounded per-tree pose allocation. This setting is independent of gameplay network replication.
- Poses are stored in a stable deque until next `forEachDraw()`. `Renderer3D::drawModel` immediately copies node transforms; callers collecting `ModelDraw` values must not dereference stored `nodeWorld` after the adapter's next submission and must not invoke its mutable pose arena from multiple threads simultaneously.
- Root/branch angles are **render-only**, not physics deformation or procedural tree skeleton edits. Verify that draw culling and per-node LOD remain conservative under poses. PHYSIS is an optional upstream data supplier only.

**Testing deferred** at user request. Dedicated test agent needs to run C++ builds/CTest plus Metal/GL/Vulkan visuals, verify parent-pivot continuity, material borrowed-pointer lifetime, 0 budget/root-only case, limits, dense forest CPU budgets and native PHYSIS integration. Until then mark code as committed, NOT VALIDATED. Update `docs/terrain/AGENT_HANDOFF.md` after every stage.

## Stage 31 — GPU crown flutter through existing Shader Graph

To opt into shader-assisted crown movement while preserving the existing CPU root/branch sway, use the PHYSIS-neutral `ForestWindState` and `WorldScene::setVegetationWind` plus `setVegetationPresentationTick`. Do not create a new render backend, compute pass, per-tree shader program, weather ECS dependency or TWH8 atlas variant.

- `engine/render/TerraWeaveVertexWind.h` owns the **trusted first-party** shared `canopyWindGraph()` (`render::graph::Program` with GLSL+Metal `vertexOffset` and unchanged fragment output). The graph's input wind is the per-instance, already available `I.anim` float4, NOT wall-clock ShaderGraph time. Its four values are packed from source horizontal velocities, simulation tick/cycle, tree yaw and gust policy. Keep `sg_color.a` as 0..1 root-to-canopy vertex weight and `vertexColors=false` for foliage fragment materials.
- The generic renderer now permits `ModelDraw::useVertexParameters`, `vertexParameters` and `maxVertexOffsetMetres`; params are placed in rigid U0 Inst.anim when no baked skin or lightmap occupies it. This DOES NOT add to the 144-byte U0 layout. Never enable for skinned/baked/lightmapped meshes without a separate parameter channel. Use an honest finite local-space offset envelope; unknown arbitrary ShaderGraph vertex offsets retain conservative no-cull.
- Arbor BranchLOD leaf vertices carry alpha height weight; bark keeps its original geometry/material. Seasonal foliage material variants and split impostor foliage (slot 1) use shared graph when wind and branch flex are nonzero. Winter bark-only draws do not submit the hidden leaves. Impostor foliage weights start at 0 at ground and end at 1 at crown top.
- Horizon HLOD keeps terrain material slot 0 **unmodified**, with forest card material slot 1 using the same graph. In `TerraWeaveHorizonScene::submit`, the active and transition region share a bounded upper-card sway without worker regeneration, upload changes or atlas archive migration. The bottom card vertices have zero weight; top vertices have full weight. `setWind` and `setPresentationTick` are owner-thread APIs; a WorldScene host forwards the same values to near and far components.
- Default no-wind behavior must be unchanged, while positive validated wind avoids shader graph compilation per tick by reusing a single static `Program` instance. To reduce distant motion, scale near High/Medium/Low/Impostor GPU flutter 100/75/50/35% and Horizon 30%. Actual GPU material batching and culling must be measured before performance claims.
- `Renderer3D::drawModel` already supports arbitrary graphs with vertexOffset. The special bounded-frustum/LOD path applies only when callers specify an accurate positive bound; do not silently infer bounds for unknown custom materials.

**User instruction:** functions first, tests later. After every feature commit, append the exact source SHA(s), changed API, what was actually checked, risks and next step to `docs/terrain/AGENT_HANDOFF.md`. Dedicated test agent still needs to migrate Stage 29 model tests, compile C++20 and run backend shader/GPU QA on Metal/GL/Vulkan; do not mark this new render path verified until that is done.

## Stage 32 — immutable spatial wind grid and HLOD macro transition

When PHYSIS/gameplay weather differs across map regions, publish a **small immutable presentation wind lattice** through `engine/world/TerraWeaveWindField.h` rather than adding a PHYSIS dependency to TerraWeave or one ECS wind component per cosmetic tree. The host can set `WorldScene::setVegetationWindField(shared_ptr<const ForestWindField>)`; standalone `TerraWeaveForestScene` and `TerraWeaveHorizonScene` expose the same setter and getter.

- `ForestWindField`: origin X/Z signed world cm, positive `spacingCm`, 2..64 nodes each axis, row-major `ForestWindSample` values (signed X/Z cm/sec and gust permille), at most 4096 nodes. Use `validate()` before publishing; valid node wind magnitudes cannot exceed 50m/s. Field remains **immutable while in use**; publish a new `shared_ptr<const...>` snapshot to update values, not mutable callbacks captured by async workers.
- `sampleForestWindField(field,x,z,fallback)` performs Q16 bilinear interpolation and a half-cell interior feather to uniform `ForestWindState` at the border; outside the field it is exactly the fallback. `blendForestWind(local,macro,macroPermille)` mixes only velocity/gust, preserving other policy controls from `setVegetationWind`.
- Near `ArborForestAdapter` samples local wind at each resident tree. From `FloraView.mediumRangeCm` to `cullRangeCm`, wind smoothly approaches the sample at the observer position; this reduces the switch to the distant macro forest's movement. The same per-tree sample drives whole-tree root lean, budgeted branch-node poses, and the existing GPU `vertexOffset` crown flutter — do not sample three different providers for the same tree.
- Horizon samples the field once at its observer position per `submit`; active and outgoing HLOD crossfade models share that macro wind, keeping one atlas/model and no per-frame CPU re-bake. This is an **aggregate approximation**, not per-cluster exact PHYSIS wind. Individual distant clusters cannot currently have independent wind vectors within the shared material; propose a bounded GPU grid input only if profiling/quality justifies the added RHI resources.
- Updating the wind grid is a presentation setting change only. Do not invalidate procedural scatter seeds, WorldScene authored heights, collision, terrain mesh, TWH8 archive, ECS authoritative state or horizon source revisions. Keep optional/null world behavior and the uniform wind fallback.
- CPU integer interpolation is deterministic on validated values; GLSL/MSL floating sine is *not* guaranteed bitwise equal across devices. Preserve the signed world-centimetre convention including negative coordinates.

**User order:** finish all functional features, tests later. After each bounded stage, append `docs/terrain/AGENT_HANDOFF.md` with exact changed files/commit, public API, actual checks, limitations and concrete next step; never claim queued CI or unexecuted GPU shader tests pass.

## Stage 33 — procedural variation within distant Horizon HLOD forest

A far Horizon region has a single shared forest material, atlas and draw path. To avoid the old "every forest cluster sways identically" effect **without allocating per-cluster entities, textures, uniforms or per-frame atlas updates**, use the dedicated Stage 33 `horizonSpatialWindGraph()` with stable world-anchored metadata from `horizonSpatialWindVertexColor(worldXcm,worldZcm,seed,top)`.

- At `makeHorizonModel` time, assign the SAME RGB8 value to all vertices of the two crossed cards of one forest cluster, sampled from smooth 2.4 km deterministic world-centimetre noise. Only alpha changes with crown height (0 bottom, 255 top). `Material::vertexColors=false` keeps the vertex attributes from tinting the fragment RGBA mask.
- In the existing GLSL/Metal `vertexOffset` hooks, `horizonSpatialWindGraph` rotates the macro wind up to ±30 degrees, scales its strength 0.75..1.25, and offsets its phase using the per-card RGB values. Bottom vertices do not move. No extra GPU buffer or render pass; one shared program is reused across all clusters and game ticks.
- The macro vector still comes from `TerraWeaveHorizonScene::setWind` and its immutable `ForestWindField` sampled at the latest observer position. The near Flora system continues to sample the field at each tree and blend near-to-far toward the observer macro (Stage 32). **Do NOT claim exact PHYSIS-per-cluster wind**: the new differences are procedural subvariation on top of one macro vector, not direct samples from remote spatial field nodes.
- `TerraWeaveHorizonScene::setSpatialWindVariation(bool)` and native `WorldScene::setVegetationHorizonSpatialWind(bool)` (default true) switch between this spatial graph and the old uniform macro `canopyWindGraph`. With zero wind, leave graph disabled entirely. Switch on the scene owning render/update thread before frame submission.
- Ensure no source fingerprint/TWH8 changes, no Horizon atlas re-bake or vertex-buffer updates for wind ticks. World coordinate noise/encoded RGB must be independent from camera region/origin to prevent visible phase jumps on region transitions. Maintain Stage 31 conservative 0.65m displacement envelope.
- The additional per-vertex trig is not proven free: benchmark GPU cost, shader graph compatibility and visible near/Horizon transition quality before production. Keep the PR Draft and defer CMake/CTest/GPU checks per the user's feature-first instruction.

After every functional feature, append a handoff in `docs/terrain/AGENT_HANDOFF.md` with commit SHAs, source files, architecture/API, actual verification vs unrun tests, limitations and concrete next step for another agent.

## Stage 34 — exact per-cluster PHYSIS wind with bounded GPU vertex patches

When distant forest regions must reproduce **different PHYSIS velocities at different world locations**, use the opt-in `WorldScene::setVegetationHorizonExactWindField(true)` alongside `setVegetationWindField(shared_ptr<const ForestWindField>)`. This is separate from Stage 33's **procedural** spatial variation. The exact mode has precedence while a valid non-null field is present; disabling exact mode or clearing the field restores Stage 33 metadata at the next safe render-owner update, without a Horizon CPU atlas bake.

- `engine/render/TerraWeaveHorizonWindPatch.h`: `HorizonForestWindOverlay` retains a copy of ONLY the existing contiguous distant forest-card vertex range (max 2 MiB CPU per model), exact cluster centre coordinates, a revision tag. `captureHorizonForestWindOverlay(bake, preparedModel)` must happen **before** `render::uploadModel` discards CPU vertices. It is moved with each `HorizonGpuRegion` through the GPU cache and crossfade paths; it must never be detached from its matching GPU model.
- `horizonFieldWindVertexColor(sample,top)`: RG8 stores signed wind X/Z in [-5000,+5000] cm/sec, B8 gust in [0,1000] permille, A8 canopy height weight (0 bottom, 255 top). Zero velocity MUST encode exactly to R=G=128 and decode back to zero. Values are quantized (~40 cm/sec); do not claim bitwise-exact PHYSIS velocities.
- `horizonExactFieldWindGraph()` in `TerraWeaveVertexWind.h` uses one shared GLSL/Metal `vertexOffset` program for all cluster cards. U0 `Inst.anim` carries only the common policy strength and caller tick phase; per-cluster wind comes from the uploaded vertex RGB. There is no new per-cluster uniform, atlas, shader instance, buffer allocation or ECS component. No per-tick VB upload.
- On a new immutable PHYSIS field snapshot or changed uniform fallback wind, the scene increments `windFieldRevision_` (separate from authored terrain/biome source revision). `HorizonScene::update(device)` tries a **single contiguous VB update per visible model**, active first then crossfade, with **2 MiB total per update**. A changed colour range is applied all-or-none; never draw partially decoded old/new PHYSIS colours with the exact shader. If the RHI cannot safely update shared buffers or the budget is exceeded, retain macro/canopy wind graph until the later update succeeds. A no-op (identical quantized values) advances the revision without a GPU call.
- `HorizonGpuRegion::forestWind` is a trailing field to preserve existing aggregate users. Cached regions preserve their CPU overlay alongside the model; after re-adoption a stale field revision is patched on the next update. Do not refresh inactive cache buffers without a safe owner-thread device point. `HorizonRuntimeStats::windOverlayPatchWrites/Bytes`, `windOverlayPending`, `exactWindFieldActive` expose actual vs pending work.
- The exact mode samples PHYSIS wind at each **statistical cluster centre** using the Stage 32 signed-centimetre Q16 bilinear field. It is distinct from near-tree per-instance samples and does not affect terrain, near seams, collision, gameplay physics or deterministic ECS replay. Changes to a field MUST publish a new immutable shared pointer, never mutate the previously published field data.
- `WorldScene::setVegetationHorizonSpatialWind(bool)` still controls procedural Stage33 variation when there is no exact active field. `setVegetationHorizonExactWindField(false)` disables the new GPU patches and restores procedural RGB through the same bounded update mechanism.

**Status:** functional C++ source committed, but CMake, GCC/Clang, CTest, GLSL/Metal shader pipelines, device backend updates, GPU cache transitions, PHYSIS integration and performance benchmarks are deliberately NOT RUN, per user's feature-first priority. Keep PR #24 Draft. The later QA agent must also migrate Stage29 impostor model tests and validate byte-level wind conversion, no-wind, null field, same-colour no-op, >2 MiB fallback, deferred RHI update, old/new region crossfade, exact-vs-procedural mode switches and negative-world coordinates. Append a new stage handoff with actual results.

## Stage 35 — sparse Horizon PHYSIS wind cluster GPU updates

Use the existing `engine/render/TerraWeaveHorizonWindPatch.h` forest-only shadow plus `planHorizonForestWindUpload(oldVertices,newVertices,maxRanges=8,fullFallbackPercent=70)` to minimize RHI traffic when a new PHYSIS `ForestWindField` snapshot affects only a few distant clusters. Do not change the shared Horizon atlas, terrain prefix, TWH8 archive, per-cluster topology or PHYSIS/genre dependency boundary.

- The planner treats **8 vertices = one cluster** (two crossed alpha-mask cards); compare only quantized vertex RGB wind metadata, preserving A root weight and all non-colour vertex attributes. Merge adjacent dirty clusters into contiguous byte ranges. At most **8 disjoint ranges** per region; if the number exceeds 8 or at least **70%** of clusters changed, replace the ranges with one full-forest-suffix write. Never upload the entire combined terrain+forest VB on a wind-only revision.
- `applyHorizonForestWindOverlay` computes a full candidate in CPU RAM first, verifies GPU model/primitive source offsets, buffer ownership and `canUpdateSharedBuffers()`, plans ranges, checks the **remaining** budget and validates every range before the first device call. Issue `gfx::Device::updateBuffer` only for selected dirty ranges using offsets relative to `overlay.firstVertex`. Commit the CPU shadow and applied revision only when the entire planned sequence was issued. A device-loss mid-sequence cannot be detected because RHI updateBuffer returns void; standard device-loss handling remains necessary.
- `TerraWeaveHorizonScene::refreshForestWindGpu` uses one **2 MiB total per update()** for active + crossfade GPU models. A small sparse patch can proceed even if the region's complete forest shadow would exceed remaining budget. When a plan is too large or buffer updating is unsafe, the old GPU proxy and safe macro wind shader continue; do not enable exact wind graph until that region's `appliedRevision` is current.
- An unchanged quantized field must advance the presentation-only wind revision with **no GPU call**. The existing `windOverlayPatchWrites` counts regions with writes, while `windOverlayPatchRanges` counts actual buffer update calls; `windOverlayPatchBytes`, `windOverlayChangedClusters`, `windOverlayFullRangeFallbacks`, and `windOverlayNoopRevisions` report traffic and fallbacks.
- `HorizonGpuRegion::forestWind` still moves with the GPU model through active, outgoing, and LRU caching. Never let a shadow detached from its model be reused after regeneration or a region change. Wind changes remain independent of terrain source revisions and the near-to-far height-welding logic.
- Testing is explicitly deferred under the owner's instruction. Later testing agent should verify 1/adjacent/8/9 dirty runs, 70% threshold, no-change update, combined active/outgoing budget, negative coordinates, GPU cache restore, shader fallback, Metal/GL/Vulkan snapshots and memory/performance. Existing Stage29 impostor model assertions are still stale.

After the Stage35 source stage, append the exact code/docs commits, tested vs untested status, limitations and next development step to `docs/terrain/AGENT_HANDOFF.md`. Do not mark CTest or GPU measurements as passed unless actually executed.

## Stage 36 — host-tuneable Horizon wind RHI budget

Use `<engine/world/TerraWeaveWindUploadPolicy.h>` and the value
`terraweave::HorizonWindUploadPolicy`, configured via
`WorldScene::setVegetationHorizonWindUploadPolicy(policy)` or direct
`TerraWeaveHorizonScene::setWindUploadPolicy(policy)`.
Its three bounded fields are `maxBytesPerUpdate` (0..2 MiB, shared across
active+outgoing regions), `maxSparseRanges` (1..8), and
`fullFallbackPercent` (1..100). Defaults **2 MiB / 8 / 70%**
exactly preserve Stage35 behavior. Always validate before publication;
the setters already do so. Zero budget defers nonempty GPU transfers,
without changing published PHYSIS data, the atlas or simulation tick.
Policy writes must happen on the render owner thread, not concurrently
with `update` / `submit`.

The Stage35 `applyHorizonForestWindOverlay` overload has a trailing
defaulted policy parameter; its planner must use **both** the policy
thresholds and the remaining combined frame budget before any GPU
write. Do not permit one outgoing transition to exceed what the active
region has left. A small budget may keep the exact field revision
pending indefinitely until the host raises the limit. Keep the macro
graph as a safe visual fallback; do not treat a deferred patch as done.
CPU shadow capture remains independently capped at **2 MiB per GPU model**
and is not configurable by this policy. No genre, PHYSIS, new RHI
resource, TWH8 format change, or terrain-seam changes.

**Status:** code/docs committed, compile/CTest/Metal/Vulkan/GL **NOT RUN**
under the feature-first workflow. Leave PR #24 Draft; append exact SHAs
to `docs/terrain/AGENT_HANDOFF.md` and provide QA cases for budget=0,
budget near one cluster, invalid settings, 4/8 sparse runs, dense
fallback and active/outgoing aggregate accounting.

## Stage 37 — CPU wind shadow residency for inactive Horizon GPU regions

Use `engine/world/TerraWeaveWindShadowCachePolicy.h`:
`HorizonWindShadowCachePolicy{maxCachedShadowBytes=16MiB}` with validated
range [0,16MiB]. Set via
`WorldScene::setVegetationHorizonWindShadowCachePolicy(policy)` or direct
`TerraWeaveHorizonScene::setWindShadowCachePolicy(policy)`; query the
matching getter. The policy affects **only inactive GPU LRU cache entries**.
Active and outgoing fade overlays continue to be owned with their GPU
models. It is independent of Stage36 `HorizonWindUploadPolicy`
(the latter limits *GPU upload bandwidth per update*, not CPU cache).

A per-region `HorizonForestWindOverlay` is the authoritative CPU reference
to **already uploaded** PHYSIS/procedural RGB attributes. Do not clear its
vertex or centre vectors while retaining the GPU model: partial RHI
updates would compare against incorrect colours, and no generic device
readback exists. `HorizonGpuRegionCache::trimWindShadowBytes` therefore
releases the **whole oldest shadow-bearing cached GPU model** with its
CPU overlay until the capacity sum fits, never touching the live active
or outgoing model. `retain` takes a backward-compatible optional final
CPU wind cache budget argument; `TerraWeaveHorizonScene::update` also
trims the LRU after policy changes. Actual RHI release runs only on the
device owner thread; setter merely stores a validated policy.

On revisit of an evicted region, the existing worker/bake/archive +
render-owner upload naturally restores the full model and synchronised
CPU wind overlay. Do not claim an instant GPU cache hit or a GPU-only
colour restoration in this case. Unaffected cached regions still use
the normal fast `take` path. `HorizonRuntimeStats` adds
`gpuCachedWindShadowBytes` (inactive shadow vector capacities) and
`gpuWindShadowBudgetEvictions` (specific whole-region evictions); the
general GPU cache evictions counter includes them. Do not count full
CPU HorizonBake caches, active region or outgoing models as part of the
inactive wind-shadow capacity.

**Source code committed, native/Metal/GL/Vulkan tests NOT RUN** by
owner request. Future tester checks budgets 0/small/16MiB, oldest LRU
selection, no-forest entries, active/crossfade preservation, camera
return path, source-generation changes and the Stage29 model fixture.
Keep PR #24 Draft until full validation; every stage needs its own
accurate handoff with implementation SHA and next step.

## Stage 38 — reversible dormant wind shadow compression

`HorizonWindShadowCachePolicy::compressCachedShadows` (default **false**)
opt-in enables lossless byte packing of inactive Horizon GPU cache shadows.
Use native `WorldScene::setVegetationHorizonWindShadowCachePolicy(policy)`.
It preserves Stage37 `maxCachedShadowBytes` and Stage36 GPU bandwidth
policy independently. Never compress or discard an active/crossfade
`HorizonForestWindOverlay`.

The codec in `engine/render/TerraWeaveHorizonWindCompression.h`
encodes zero runs and literal runs directly from **all 60 bytes** of
`render::ModelVertex`, without converting float bits, GPU colours or
PHYSIS data. `compressHorizonForestWindOverlay` packs only if
capacity truly shrinks; sets `packedVertexCount`; clears raw vertices
while the model is **dormant**. `restoreHorizonForestWindOverlay`
losslessly reconstructs raw vertices before the GPU LRU returns the
region to active. Do not enable Stage35 GPU vertex colour patching on
a still-compressed overlay, and do not update `appliedRevision` just
because of compression or restoration. Malformed stream on the
device-aware `take(device,cell,revision)` triggers full GPU region
retirement and normal worker re-stream, NOT a fabricated colour shadow.
Runtime policy enablement also compacts existing dormant cache entries,
with one scan per entry (no per-frame recompression of failed candidates).

Budget counts raw/packed vector **capacities** plus immutable world-cm
centres. Low-entropy data may still remain raw and be evicted as in
Stage37. No GPU readback, new atlas, RHI path, TWH8 format or
genre/PHYSIS dependency. New stats:
`gpuCachedCompressedWindShadows`,
`gpuWindShadowCompressions`, `gpuWindShadowRestores` and
`gpuWindShadowDecodeFailures`. Preserve strict alignment,
version-free memory-only compression and existing owner-thread barriers.

**QA intentionally NOT RUN.** Future independent tests: codec roundtrip
(including nonzero alpha and arbitrary float patterns), malformed byte
streams, first/last-token boundaries, compression benefit/no-benefit,
toggle after old regions are cached, GPU LRU return without reupload,
multiple PHYSIS wind revisions, Stage35 sparse wind patches, Stage37
zero/low/16MiB limits and device loss. Keep PR #24 Draft and record
all exact implementation commits in the Stage38 handoff.

## Stage 39 — Horizon runtime presentation quality policy

Use `engine/world/TerraWeaveHorizonPresentationQuality.h` to
compose existing runtime features rather than inventing yet another
GPU wind update/cache subsystem. `HorizonPresentationQualityPreset`
has `BatterySaver`, `Mobile`, `Balanced`, `Desktop` presets,
and `horizonPresentationPolicyForQuality(preset)` produces the
POD `HorizonPresentationPolicy` with exact/procedural spatial wind
switches, Stage36 `HorizonWindUploadPolicy`, Stage37/38
`HorizonWindShadowCachePolicy`. Validate the complete policy before
publishing with `validateHorizonPresentationPolicy`.

Native games use
`WorldScene::setVegetationHorizonPresentationQualityPreset(...)`
or `setVegetationHorizonPresentationPolicy(...)`, plus
`vegetationHorizonPresentationPolicy()` for actual values.
Standalone renderer uses
`TerraWeaveHorizonScene::setPresentationQualityPreset` /
`setPresentationPolicy` / `presentationPolicy`.
Individual existing setters remain composable, and getter reports the
effective values after overrides. All calls on the owner thread.

The optional manifest field
`terraweaveHorizon.presentationQuality` chooses
`battery-saver|mobile|balanced|desktop`; this is entirely **separate**
from the preexisting `terraweaveHorizon.quality` geometry preset.
Do not change TWH8 archive serialization, source revisions, terrain
geometry, near-owned seams, wind quantization, PHYSIS ownership or
genre/ECS module selection when applying a runtime preset. When the
manifest has NO new field, preserve legacy Stage38 defaults. Invalid
names must fail with `JsonError`. Copy effective constructor-applied
settings back into `WorldScene` getters.

Policy values: BatterySaver disables exact/procedural spatial wind and
uses 0 GPU transfer bytes, 0 dormant CPU shadow budget; Mobile uses
procedural spatial wind, 128KiB GPU/update, 2 sparse runs, 90% full
fallback, 1MiB CPU shadow + lossless compression; Balanced uses
exact PHYSIS and procedural fallback, 512KiB, 4 runs, 75%, 4MiB +
compression; Desktop uses exact + procedural, 2MiB, 8 runs, 70%, 16MiB
+ compression. All are unbenchmarked starting points. A low limit
may indefinitely defer a dense weather VB patch, with the existing
macro shader safe fallback; do not claim visual parity or a guaranteed
battery/FPS improvement.

`HorizonRuntimeStats` exposes the *effective* GPU/CPU budgets, sparse
limits, compression switch and procedural graph status for editors,
along with earlier actual patch and eviction counters. All are
bookkeeping until tested on a real device.

**QA NOT RUN:** compile, CMake/CTest, GLSL/Metal/Vulkan/GL, performance
and visual screenshots are delegated to the independent testing agent,
as user requested. Tests should cover all preset values, opt-in
manifest and invalid values, custom override and combined getter,
per-frame bandwidth starvation/retry, early no-field behavior,
cache residency/Stage38 packed shadow restoration, crossfade and
shader selection. Record separate implementation commits and next
action in `docs/terrain/AGENT_HANDOFF.md` before ending the stage.

## Stage 40 — optional adaptive Horizon presentation quality

For adaptive quality use
`engine/world/TerraWeaveHorizonAdaptiveQuality.h`.
`HorizonAdaptiveQualityConfig` is **disabled by default**, with
bounded `minimum/maximum/initial` Stage39 quality profiles, target
frame time (microseconds), independent upgrade/downgrade percentages,
consecutive-frame streaks and cooldown. A native project calls
`WorldScene::setVegetationHorizonAdaptiveQualityConfig`,
`submitVegetationHorizonFrameTiming`, and queries
`vegetationHorizonAdaptiveQualityState`; direct HorizonScene has
`setAdaptiveQualityConfig`/`submitAdaptiveFrameTiming` and getters.

Never invent or assume a GPU timing measurement: the host must submit
actual frame CPU/GPU duration in microseconds with strictly increasing
`frameId`, zero for an unavailable metric. The gfx device
`gpuFrameMs()` exists but may return zero/lagging measurements;
do not use asynchronous stale timestamps as same-frame values.
At least one valid time is needed. Ignore absent/invalid/out-of-order
samples; gaps reset sustained streaks. Use the worst available real
timing per frame; the policy is global frame QoS, not a direct
per-terrain cost estimator.

The pure `advanceHorizonAdaptiveQuality` changes only **one**
quality level per decision. By default downgrade after 30 consecutive
frames >115% of 16.667ms, upgrade after 240 frames <80%, with
240-frame cooldown and Mobile..Desktop bounds. All thresholds and
bounds are validated; exact boundary/straddle behavior must be
tested. A manual runtime Stage39 preset/full-policy/individual
presentation setter **disables** the adaptive controller, while
external PHYSIS wind, season and simulation time inputs do not.
Enabling resets controller history and applies the initial preset;
disabling preserves the current effective profile.

An optional `terraweaveHorizon.adaptiveQuality` manifest object
supports `enabled`, `minimum`, `maximum`, `initial`,
`targetFrameUs`, `downgradePercent`, `upgradePercent`,
`downgradeFrames`, `upgradeFrames`, `cooldownFrames`.
Quality strings: `battery-saver|mobile|balanced|desktop`; static
`presentationQuality` supplies the default initial. Manifest
opt-in alone does NOT automatically supply timing samples; game
hosts must wire their measured frame telemetry. Keep geometry
`quality`, TWH8 archive, GPU resources, near authored seam, and
PHYSIS/ECS authority unchanged.

Expose `HorizonRuntimeStats::adaptiveQuality*` current enabled
flag, level, sample count, changes and last measured duration. These
counters are not FPS benchmarks. The user requested features before
test runs, therefore native CMake/CTest/GPU/shader verification is
**NOT RUN**, and PR #24 must remain Draft. The independent tester
should cover consecutive samples, invalid/missing measurements,
monotonic sequence, hysteresis/cooldown, manual override and
manifest quality binding, with platform-specific actual GPU timing
providers and crossfade safety. Record full code commit SHAs
and next step in `docs/terrain/AGENT_HANDOFF.md`.

## Stage 41 — native RTS/FPS host frame timing bridge

Stage40 adaptive Horizon QoS no longer needs hand-written native FPS/RTS
timing calls. Use `engine/world/TerraWeaveHorizonFrameTimingBridge.h`:
`HorizonFrameTimingBridge::begin(device,worldScene)` after the host begins
its graphics frame; `finish(worldScene,realCpuFrameStart)` after submitting
the render work (and `endFrame` when owned by the app). The bridge is
enabled ONLY when `worldScene->vegetationHorizonAdaptiveQualityConfig().enabled`;
no Horizon or disabled adaptation causes no GPU timers or feedback.

Existing integrations: `apps/client/GameClient.h` and `apps/client/GameClient.cpp` uses the bridge
around streamed-world RTS render within the platform-owned frame; native
`apps/fps/client.cpp` begins/ends a graphics frame itself and brackets its
FPS client callback. It uses `std::chrono::steady_clock` *measured CPU
work*, not `dt`, and `gfx::Device::timeGpuFrame(tag)` +
`takeGpuTimings()` only when available. GPU results are asynchronous
and tagged to the ORIGINAL frame. The bridge buffers at most eight
frames of GPU latency, emits ordered matching measurements or CPU-only
fallback (0 GPU microseconds), never waits/stalls and never fabricates
completed GPU results. Do not submit a second timing sample manually
when using the bridge.

**Important:** `takeGpuTimings()` destructively returns **all** ready
device queries, including tags for other systems. The RTS/FPS client
paths have no other current GPU tag consumer, but future benchmarking
must route queries via one shared device owner instead of competing
pollers. Metal tags cover the full command buffer and GL timers start
when the query is issued, so neither is a measured isolated Horizon
cost. Other native hosts may use the bridge but must respect each
backend's `beginFrame/endFrame` ownership. The bridge does NOT add a
new backend API or change PHYSIS/ECS/gameplay/geometry/atlas/TWH8.

`lastFrameId`, `pendingFrames`, `deliveredGpuFrames` and
`deliveredCpuOnlyFrames` provide internal diagnostics. While
`adaptiveQuality.enabled` is false (the default), no new sample is
recorded and no GPU timer is requested. Use the Stage40 optional
manifest to enable; it now drives real native RTS/FPS callback samples
automatically.

**QA deliberately NOT RUN:** CMake/Clang/GCC/CTest/real Metal/GL/Vulkan
frame loop, GPU tag ordering, FPS/RTS smoke, map teardown, concurrent
tag consumers, performance/battery benefit. The independent tester
must verify all these and the Stage29 atlas migration before merging
PR #24. Update `docs/terrain/AGENT_HANDOFF.md` with exact code SHAs.

## Stage 42 — shared gfx Device GPU timing router

Do NOT call `gfx::Device::takeGpuTimings()` directly from a new
native subsystem if the Device is shared with TerraWeave,
Prisma, SpectraFX/VFX, renderer benchmarks or the editor.
This is a destructive device-wide drain. Use
`gfx::Device::gpuTimingRouter()`, defined in the general
`engine/gfx/GpuTimingRouter.h`, not a new TerraWeave-specific
timer manager. Subscribe, request a GPU timing with a native
caller-defined tag, poll once or more on the Device owner thread,
then `take(subscriber)` only your own mailbox. Unsubscribe
during consumer teardown; `cancel(subscriber,tag)` drops a
stale metadata request without stealing other owners' results.
All GPU timings are asynchronous and may be unsupported/rejected.

The Stage41 `HorizonFrameTimingBridge` has been migrated to this
API and maintains its measured CPU fallback and eight-frame limit.
RTS GameClient and native FPS App detach their subscriber at
shutdown; editor batching benchmark, RenderBench, GeometryBench and
instancing preview also use per-device routing. See
`docs/render/GPU_TIMING_ROUTER.md` for native API and bounded
queues (32 subscribers, 64 in-flight, 64 completed per mailbox).
These numbers bound router CPU memory, NOT measured frame time.
GL may allow only one active GPU timing query, and Metal command
buffer timestamps include unrelated GPU passes; this router does
not provide isolated system timing or concurrent timers for
unsupported backends.

No genre, PHYSIS, ECS or TWH8 changes. Existing Stage40 QoS
remains opt-in and uses the original CPU/GPU frame tags. Source
writes and handoff are recorded; native compile/CTest/GPU
benchmark/visual tests **NOT RUN** at user request. Keep PR #24
Draft until independent QA.


### Stage 43 QA — shared GPU timing router (2026-11-19)

The per-Device `gfx::GpuTimingRouter` shared with native RTS/FPS, editor SceneWindow
and benchmarks now has `tests/gpu_timing_router_test.cpp`, target
`aiverse_gpu_timing_router_test`, CTest `gpu_timing_router`.
The fake Device regression covers tag isolation, out-of-order delivery, late
cancel/unsubscribe completions, busy/unsupported backend rejection,
32/64/64 capacity and mailbox drop accounting. TerraWeave isolated GCC/Clang CI
includes it. This fixture does **not** replace real Metal/GL/Vulkan
frame-time or device-recreation tests. The prior nine TerraWeave CPU cases
passed in Mac Studio QA; this **new** test was not compiled or executed by
the authoring agent. See `docs/terrain/AGENT_HANDOFF.md` Stage43.


### NEREID ↔ TerraWeave soil ecology (Stages 45–46, 2026-11-19)

NEREID water from \`main\` is the only water owner. New native water
erosion code is in \`NereidErosion.h\` and optional persistence/system
in \`NereidShorelineSystems.h\`. TerraWeave owns sparse earth edits
(\`TerraWeaveEarthworks.h\`), explicit ECS read-barrier adapter
(\`TerraWeaveNereidSoilBridge.h\`), root/cliff/fall response and grass
aggregation (\`TerraWeaveVegetationSoil.h\`), shared exposed-root
Renderer3D models only if needed (\`TerraWeaveRootModel.h\`).
Native \`WorldScene::setVegetationRootStates\` publishes root snapshots
for near forest. Trees and shrubs carry seed-derived age/light/growth
properties; grass never allocates per blade, with one \`GrassPatchDraw\`
per patch and deterministic \`sampleGrassBlade\` on demand.
\`terraweave_excavate\` edits flat RTS corner heightfields via MapDoc
undo. **Not yet finished:** true Box3D fallen-tree rigid bodies,
streaming terrain collision/navmesh patch/rebuild, GPU grass instancing,
and voxel cave excavation. **No new native tests run** after Stage45/46.
See `docs/nereid/TERRAIN_EROSION.md` and handoff for exact status.


### Stages 47–56: Earthworks + NEREID + canonical ECS native integration (2026-11-19)

- NEREID is authoritative for all water. `TerraWeaveEarthworks.h` has sparse transactional dig/fill, `NereidErosion.h` soil-specific fixed-point erosion/deposition. `TerraWeaveNereidErosionBridge.h` supports a standalone bounded bank sampler OR, when native ECS bank systems run, `TerraWeaveEcsBankBridge.h` commits NEREID's already computed `WaterBankErosionOutputComponent` without calculating water twice.
- `TerraWeaveRootEvents.h` and `TerraWeaveVegetationSoil.h` give root exposure, stable once-only fall events and deterministic age/light/health. `TerraWeaveGrassCover.h` stores grass patches, never one ECS entity per blade. `TerraWeaveGrassRender.h` builds bounded combined foliage models through existing Renderer3D; exposed roots are lazy via the root model adapter.
- `TerraWeaveTerrainPatch.h`, `TerraWeaveTerrainPatchRender.h`, `TerraWeaveBox3DTerrain.h` share one authored terrain triangle topology across RHI/SpatialWorld/optional Box3D. `TerraWeaveNavMeshBridge.h` updates existing `nav::Mesh`, and `TerraWeaveFallenNavigation.h` provides immutable fallen-body obstacle snapshots. A game must retire its old static ground collision/draw when replacing it.
- `TerraWeavePersistence.h` writes a stable, validated versioned binary save with generator recipe ID; `TerraWeaveReplication.h` provides revision/tick-guarded change messages, **not** a socket protocol. `TerraWeaveEcsAuthority.h` is the canonical authoritative `Simulation::World` persistent resource (schema 2); `TerraWeaveNativeWorldBinding.h` reconstructs scene terrain/root/grass caches after rollback. `TerraWeaveNativeDriver.h` is the single opt-in per-frame publication barrier.
- Complete usage is in `examples/world/terraweave_native/{Integration.h,README.md}`. Last handoffs: `docs/terrain/stage-53-persistence-ecs-handoff.md`, `stage-54-replication-handoff.md`, `stage-55-ecs-authority-binding-handoff.md`, `stage-56-native-example-handoff.md`.
- **QA still pending for this new functional phase**: no native compiler/CTest or GPU/Box3D/editor integration pass was performed for Stages 44–56. Earlier 9/9 TerraWeave and 1/1 GPU timing router are from previous revisions. Keep PR #24 Draft. No voxel caves/undercuts yet; this is heightfield excavation. Preserve the one-authoritative-water-writer rule.


### Stage57–59 (2026-11-19): real NEREID coastline → erosion → transported sediment

- `engine/world/TerraWeaveShorelineRaster.h` has `discoverNereidShoreline(World&,EarthworksLayer&,ShorelineRasterSettings,soilAt,rootBindingAt)`, relying on authoritative `WaterQueryIndex` for full NEREID polygon/river/ocean footprints. Only up to 16,384 grid cells per discovery window. `includeWetBedCells=true` (opt-in) adds submerged bed carriers for broad rivers/estuaries; without this, only banks erode/transport. `refreshAutoShorelineBanks` registers/upserts stable ECS bank cells (and does not overwrite manual banks), preserving sediment on dry/dormant banks. Register `terraweave.auto_bank` before simulation setup.
- `engine/world/nereid/NereidSedimentTransport.h`: pure sorted integer mass-conserving advection using NEREID Fixed current, contiguous grid neighbours, time step and persistent **micro-cm** carry. Only same water body or NEREID-authored directed confluence/cascade links; no guessing body connectivity, no removal of suspended mass at unmodelled boundary.
- `engine/world/TerraWeaveSedimentEcs.h`: `registerNereidSedimentEcsPersistence` and `addNereidSedimentTransportSystem` between NEREID bank erosion and TerraWeave soil commit. Source `WaterBankSedimentTransportComponent` is one ECS component PER BANK CELL, not per grass blade. The NEREID bank sediment system does not call erosion a second time, and only transports sediment mass (not ground height).
- Example registration and area scans: `examples/world/terraweave_native/{Integration.h,README.md}`. Read each Stage57–59 handoff under `docs/terrain/` for commits, limits and next work.
- Direct writable ECS access must use `World::pool<T>()` to detach pool COW ownership before `Pool::tryGet()/dataAt()`. Native NEREID water sampling/erosion and TerraWeave bank/transport writers were adjusted accordingly.
- **Status:** features committed, no native CMake/CTest/Metal/Box3D simulation or river screenshot for Stage57–59 yet. Prior green TerraWeave test reports are older revisions. Keep Draft PR #24, then run focused erosion/mass-conservation and native integration QA before main.


### Stage60–61 (2026-11-19): water-undercut cliffs and root-sensitive slope failure

- `engine/world/TerraWeaveSlopeFailure.h`: deterministic integer slope failure from NEREID bank erosion, PHYSIS/game wetness and root reinforcement. `SlopeSoil` (soil, saturation permille, roots permille), `SlopeFailureSettings` and `planSlopeFailures(ground,affectedVertices,sampler,settings)` produce a bounded mass-conserving downhill transfer plan. Sand/silt collapse sooner; rooted soil and rock resist; equal-area volume is retained within the heightfield, no separate sediment/water solver.
- `engine/world/TerraWeaveEcsBankBridge.h` opt-in slope flag merges already computed NEREID bank erosion with downhill slides into **one** canonical Earthworks edit/revision, then updates root support/fallen-tree events and aggregate grass cover using the final ground. Affected ECS bank soil ground heights get correction records.
- `engine/world/TerraWeaveEcsAuthority.h`: `addTerraWeaveAuthoritativeBankSystem(...,enableSlopeFailures,settings)` updates ECS bank ground heights only after canonical soil resource commit, with COW page preflight. `excavateAuthoritativeEcs(world,cfg,brush,tick,true,settings)` provides the same mechanism for manual digging, without requiring water. The opt-in native sample uses wet slope failures by default.
- `docs/terrain/stage-60-wet-slope-failure-handoff.md`, `stage-61-nereid-slopes-ecs-handoff.md`, `examples/world/terraweave_native/README.md` contain full design, API and ownership. Nature remains genre-neutral. Do not claim physically simulated granular soil, voxel undercuts, actual fallen-debris collision or water-flow feedback from soil slides.
- **Verification**: source committed, compilation/CTest/ECS replay/real Box3D + Metal not run for these new stages; older 9/9 CPU and 1/1 router are from earlier branch head. Keep PR #24 Draft and run native validation after feature completion.


### Stage62 — landslide debris and current obstruction / return feedback (2026-11-19)

- Source: `engine/world/TerraWeaveSlopeDebrisPhysics.h` creates finite-lifetime, bounded dynamic Box3D rigid fragments from authoritative `SlopeFailurePlan` (stage60/61) at the host physics owner barrier. These are visual/collision proxies and cannot add/subtract already moved Earthworks soil. `FallenPlantPhysics` handles uprooted trees/shrubs with stable identity.
- NEREID water alone decides current. NEW `engine/world/nereid/NereidFlowObstacles.h` (and `NereidWaterSystems.h`/`NereidQueryIndex.h` integrations) applies optional persistent Fixed current attenuation/deflection around physically present fallen plants and debris after river/current-field/vortex/confluence/cascade sampling. `NativeWaterPipelineConfig::enableFlowObstructions` opts in; native WaterBody remains unchanged.
- `engine/world/TerraWeaveFlowObstructionEcs.h` is Box3D-independent `AutoFlowObstructionComponent` persistence/publication, touching ONLY generated obstruction ECS entities (never authored ones). `engine/world/TerraWeaveNereidObstacleBridge.h` (opt-in Box3D) captures authoritative server physics poses and immutable NEREID source WaterSample/WaterBody IDs at the completed physics step, then publishes current obstruction records to native ECS before the next NEREID sample.
- `WorldScene::setNereidFlowObstructions` consumes a native mapping from ECS WaterBody Entity to stable `NereidSceneSurface.id` and applies the same fixed-point deflection to water whitewater and ripple movement. WorldScene is presentation only and never simulates ECS/Box3D. Source keys make current deflection ordering independent from ECS recycling.
- Native example `examples/world/terraweave_native/Integration.h` requires no Box3D, while `PhysicsIntegration.h` offers Box3D post-tick effects and water-obstacle publication. Read `docs/terrain/stage-62-debris-water-feedback-handoff.md` for actual commits, limits, API, timing, and remaining tasks.
- These are **source commits only**: new native compilation/CTest, ECS waterfall/replay, Box3D and Metal runtime have **NOT** been run for Stage62. Keep PR #24 Draft. Remaining: nonlocal water pressure/river rerouting, network replication of physics-induced obstacle inputs, 3D caves, and full GPU/game QA.


### Stage63 — NEREID flood volume, overtopping and TerraWeave terrain feedback (2026-11-19)

- `engine/world/nereid/NereidFloodRouting.h` is an opt-in **NEREID-owned** bounded equal-area flood reservoir (`WaterFloodResource`, ECS schema `nereid.flood` v1; max 16,384 cells). `stepNereidFlood` transfers finite source water volume by hydraulic head to adjacent grid cells with obstruction edge permeability and strictly checks sum of column volume before/after. `WaterFloodResource::wetnessRevision` tracks newly wet/dry cells for downstream shoreline regeneration. `applyNereidFloodSample` exposes water beyond original NEREID polygons/splines in both reference and indexed water samplers; original higher water surface still wins.
- `engine/world/TerraWeaveNereidFloodBridge.h` seeds a finite grid ONCE from NEREID WaterBody footprints and canonical Earthworks heights. `synchronizeNereidFloodGround` and its optional native ECS system update grid-bottom elevation after digging/erosion/landslide without adding/removing stored water volume. Register the NEREID flood system BEFORE water probes and the TerraWeave ground synchronizer AFTER the last soil commit.
- `engine/world/TerraWeaveFloodShorelineSystem.h` offers persistent auto-raster revision and optional structural ECS bank refresh when water wetness or soil revision changes, not each graphics frame. Native WorldScene owner receives up to 2048 visible grid cells through `NereidFloodVisualScene.h`; a bounded single-model transparent fallback draws extra floodwater beyond/above authored water. This is not yet full NEREID foam/refraction material.
- Example: `examples/world/terraweave_native/{Integration.h,README.md}` (opt-in native flood enabled, explicit seedFloodReservoir, optional autoFloodShoreline scan bounds). Read `docs/terrain/stage-63a-nereid-flood-volume-handoff.md` and `stage-63b-flood-visual-native-handoff.md`.
- Volume conservation applies ONLY to the explicitly seeded *closed local flood grid*, not legacy static WaterBodyComponent/ocean total volume. No rainfall, open-boundary exchange, infinite-river discharge, pressure solve, full shallow-water dynamics or global river geometry rerouting. Core water authority remains NEREID; TerraWeave owns terrain/biomes/vegetation and soil deformation.
- **Tests NOT RUN** for Stage63: new source commits are not proof of compilation/ECS mass-conservation or Metal rendering. PR #24 remains Draft. Older CPU 9/9 and GPU router 1/1 results are not applicable to new headers.


### Stage63c finite water inlet/outlet accounting (2026-11-19)

- `NereidFloodRouting.h` now has `WaterFloodPort` (Inflow/Outflow) and persisted `WaterFloodResource.externalVolumeCm`. NEREID routes the **same finite water** between this outside reserve and its explicit equal-area grid cells; exact global-local ledger conservation across cells + reserve is validated every tick. This is required for an upstream head rise on an initially flat river with incoming water; no infinite, unaccounted pump exists. At most 64 ports, configured before first routing tick with a valid WaterBody Entity and a grid-local coordinate. `WaterFloodTick` exposes `suppliedCm/drainedCm` and new wet/dry results; a depleted reserve halts inflow.
- `TerraWeaveNereidFloodBridge.h::configureNereidFloodBoundaryPorts` and `examples/world/terraweave_native/Integration.h` (`configureFloodRiverPorts`) expose native setup, independent of Box3D. See `docs/terrain/stage-63c-finite-river-ports-handoff.md` and the updated native example README. For changed water geometry or new area, use an explicit account-preserving reservoir migration in the future; never reseed an active simulation.
- **Not a global water-volume proof** including legacy static WaterBodyComponent; no rainfall, ocean tide/river pressure solve, volumetric flood hydrodynamics or automatic riverbed mesh rerouting. Source is committed but native CMake/CTest/Metal/full ECS replay QA NOT RUN. Keep Draft PR #24.


### Stage64 — post-flood grass recovery and topsoil shielding (2026-11-19)

- Pure `engine/world/TerraWeaveShoreRecovery.h` models one aggregated `GrassPatch` recovery record (wet/dry dwell, 0..1000 seed bank, 0..1000 soil crust/litter protection) per saved grass patch. `advanceShoreRecovery` performs deterministic fixed-period growth/drowning damage, with `PlantEnvironment` age/light/water/nutrients, no per-blade ECS allocation, no new vegetation generator or implicit tree resurrection. `ShoreRecoveryCoverIndex` supplies bounded spatial 64m-bucket surface shield lookups for existing water bank points.
- `engine/world/TerraWeaveShoreRecoveryEcs.h` registers `terraweave.shore_recovery` resource schema v1 and `addTerraWeaveShoreEcologySystem` at the LAST native ECS stage. It builds an immutable NEREID WaterQueryIndex only on due ecology periods (default 600 ticks), samples five spots per grass patch including true flood overlays, updates canonical ECS `TerraWeaveAuthorityResource` and saved recovery state, and updates only **auto-tagged** shoreline bank cover ECS components. This uses a staged World copy to avoid mismatched resource/plant states on failure.
- NEREID alone owns water/shore erosion. Optional new `ShoreErosionForcing.surfaceShieldPermille` (0..1000) plus persisted `WaterBankSurfaceCoverComponent` reduce bank surface shearing after regrowth. This is NOT `ShoreErosionCell.rootBindingPermille`: grass does not get tree/shrub anchor roots; this cover is topsoil/litter/crust protection. Existing manually authored bank shielding is never overwritten by auto ecology.
- Native example `examples/world/terraweave_native/Integration.h` enables the recovery ECS stage by default only in the opt-in native composition. Read `docs/terrain/stage-64-shore-ecology-recovery-handoff.md` for exact commits, limits and next steps.
- **QA not run**: no new native CMake, Clang, CTest, ECS replay, NEREID water-erosion, Metal/GPU or perf results for this stage. Earlier green TerraWeave tests predate Stage64. Future Stage65 should add efficient tree/shrub age/light competition and regeneration without respawning fallen/harvested trees; then final end-to-end tests before merging PR #24.


### Stage65 (2026-11-19) — per-species rooted forest ecology, shade competition, recruitment and natural death

- `engine/world/TerraWeaveForestEcology.h` reuses existing `AnchoredPlant` and `PlantEnvironment` rather than introducing a second plant representation. Each opted-in tree/shrub species has maturity/lifespan, shade/drought/flood sensitivity, canopy radius and seed radius. `ForestEcologyResource` saves a stable cursor and per-key last evaluation, shade/flood/drought stress, recruitment identity and death status. Update expensive plant decisions in bounded batches (default 256 every 32 ECS ticks, maximum configured active subset 65,536). `ForestEcologyCanopyIndex` uses 32m spatial bins and an approximate relative-height/overlap sun factor; it is NOT a true shadow/GI/irradiance solver. Age/light/water/nutrients influence existing Arbor presentation, while stable hashed seed positions use saved Earthworks height. No mass cosmetic tree entities.
- `engine/world/TerraWeaveForestEcologyEcs.h`: register `terraweave.forest_ecology` persistent resource v1 and the opt-in ECS update after the Stage64 grass ecology and NEREID flood/soil erosion stages. It uses the existing indexed water query for root-zone flood status and optional PHYSIS climate callback, saves canonical ECS rooted-plant mutations and ecology history together, and emits immutable birth/death outcomes for post-ECS gameplay. NO Box3D or RHI work from the fixed ECS system.
- `TerraWeaveForestScene.h`: `setDeadTrees` independent of fallen/harvested filters; `setRecruitedPlants` renders sparse native saplings using the existing Arbor shared mesh/impostor and Flora LOD instead of per-tree GPU asset generation. `TerraWeaveNativeWorldBinding.h` reconstructs these on rollback/load. Tree-only age/light changes do not rebuild unrelated GPU grass, terrain meshes, colliders or navmesh.
- `examples/world/terraweave_native/{Integration.h,README.md}` demonstrates opt-in species policies and authoritative ECS planting; see `docs/terrain/stage-65-rooted-forest-ecology-handoff.md` for exact commits and limitations. Hidden *harvested* trees must also be removed/flagged in authoritative game state; natural death does NOT itself remove physics colliders, produce fallen logs or decompose resources. Explicit gameplay owns those actions.
- This stage is **source-implemented but not compiled or native-tested**. Expensive decisions are batched, but rebuilding the canopy index and staging ECS root snapshots remains O(N) per due ecology period; no proven million-entity runtime or true photometric light. Do not merge Draft PR #24 before native CMake/CTest, deterministic ECS replay, NEREID water stress and Arbor near/far/Metal validation.

## Native decorative rocks (2026-11-20)

Use `engine/world/TerraWeaveRocks.h` / `engine/render/TerraWeaveRockModel.h`
for reusable seeded stones, boulders, slabs and shards. Read `docs/terrain/rocks.md`.
Do not create a second Python geometry solver or use plant age/foliage components
for rocks. Cache a bounded prototype pool; ordinary Renderer3D owns 1280/320/80
triangle LOD and GPU resources. Integer placement accepts an immutable authored
height snapshot and a road/water exclusion callback. Nine-point support is a
conservative visual approximation, not collision or authority. Native source
uses no new renderer. `aiverse_terraweave_rocks` exports the same generated models
with existing glTF rtsLods and four shared PBR maps, refusing existing output dirs.
Test `terraweave_rocks` and inspect real captures before visual acceptance.

## Immutable exported terrain LOD

For baked terrain assets use `engine/render/TerraWeaveTerrainLod.h` and
`appendTerrainPatchLods` after smoothing shared source normals/UVs, before GPU
upload. The landscape exporter already does this. It preserves exact LOD0 and
all outer edge vertices while appending three error-budgeted alternatives.
Keep the original TerrainPatch collision; live mutable patches and Horizon
retain their existing owners. Read the immutable terrain LOD section of
`docs/terrain/TERRAWEAVE.md` for input restrictions and storage costs.

Inspect actual camera/shadow submitted triangle counters and captures.
Renderer3D crossfade is spatial: a parked camera inside a transition band keeps
both tiers, so choose outside-band views to measure one tier. Do not infer a
broken LOD or FPS gain from summed asset triangles. Check `terraweave_model` and
`tests/terrain/check_landscape_export.py`; backend and editor picking parity
still need their own verification.
