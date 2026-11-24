# Agent rules: Shared textures/streaming, render resource lifetime (Stages 99-108), CPU MDI and geometry pool (Stages 35-56)

Moved verbatim from AGENTS.md on 2026-11-21 (AGENTS.md keeps the cross-cutting rules; this file is
mandatory reading before working in this area). Newest entries are at the end.

## Shared texture RHI / streaming — reuse instead of regenerating (2026-11-18)

For any native game project needing Texture2D/3D/arrays/cubes,
offscreen rendering, KTX2/DDS assets or mip streaming, **start with**
`skills/visuals/aiverse-texture-rhi/SKILL.md` and
`docs/texture-rhi.md`. Use the existing `gfx::Device`,
`gfx::RenderTargetSet`, `gfx::TextureStreamQueue`,
`gfx::TextureMipResidency`, `gfx::TextureResidentStreamer`,
`gfx::VirtualTextureStreamer`, `gfx::VirtualTexturePrefetchPlanner`,
`gfx::prefetchVisibleVirtualPages`, `gfx::VirtualTextureFeedbackRuntime`,
`gfx::VirtualTextureFeedbackPass`,
`Device::readTextureAsync`,
`render::loadRadianceHdr`, `render::loadOpenExr`, `render::parseKtx2`,
`render::parseDdsTexture`, `render::uploadContainerTexture`,
`render::uploadKtx2WithLibktx`, and `render::loadOpenExrWithLibrary`.
Do not regenerate or fork genre-specific texture managers in FPS,
RTS or other projects. Rendering/streaming is presentation state:
do not make it ECS-authoritative or add it to deterministic save hashes.
Native project modules can include these public C++ headers directly.
Optional BasisLZ/UASTC LDR, UASTC_HDR -> RGBA16F, and
compressed OpenEXR decoding use explicit system Khronos
libktx and OpenEXR 3 build flags; do not
copy codecs into genre modules. Vulkan writable compute textures
are opt-in and require capability checks; do not assume GL33/GLES3
or WebGL2 support. The GPU inspector now permits bounded 3D
mip-slice readback on native backends, not compressed/depth/MSAA.

Texture format/dimension feature support differs per backend:
explicitly check `supports`, `supportsTextureDimension` and resource
creation results. Unavailable features must fail visibly, never silently
reinterpret Cube/Array/3D as 2D, ignore resolve requests or misread
compressed mip payloads. A streaming queue owns its textures and must
be destroyed before the GPU Device. Do not sample mips until the
residency planner reports every required layer/face resident.
Metal/GL/Vulkan API branches and 7A–7E streaming work are source changes,
**NOT verified production behavior** until native build/CTest/GPU checks
pass. After each implementation stage append
`docs/agent-handoffs/texture-rhi-2026-11-18.md` in a separate commit
with actual (not assumed) test results. PR #21 must remain Draft
until source integration and graphics validation gates pass.

**Incremental static CPU cache, LOD-aware packets and ECS presentation (2026-11-19, opt-in):**
The common `engine/render/Renderer3D` owns experimental per-`persistentId`
transform reuse (`AIVERSE_PERSISTENT_CPU_MODEL_CACHE=1`) and immutable
`ModelItem` packet reuse (`AIVERSE_PERSISTENT_CPU_PACKETS=1`). The packet
requires nonzero `ModelDraw::staticPacketRevision`; it validates resolved
GPU material state on each hit, including native/RTS per-draw material copies,
and remaps the material pointer by index. A pointer-stable material mutation
does not silently reuse the previous GPU material state. Runtime rigid LOD
is reselected for the current camera and shadow; a changed LOD rebuilds and
crossfade frames do not use the cache. Animated, skinned, morphed, vertex-
displaced, baked-GI, mutable-node and cluster models fall back as before.
Per-node material overrides and explicit KHR material variants are now
eligible with value-snapshot invalidation: `ModelDraw::nodeMaterial` indices
are compared before replay, material variant index is part of the saved draw
signature, and the baseline's LOD-disabled behaviour for these modes is
preserved. Do not store caller-owned material override pointers in packets.
A model file containing authored animation clips can also use the rigid static
bind-pose cache when its draw has no live pose/baked animation, its skin set
is empty, the hierarchy has been finalized, and nodes are not runtime-mutable.
Active animation is still uncached.
The renderer now memoizes per-model static eligibility through
`engine/render/ModelStaticEligibility.h`, keyed by geometryRevision and
structural/GPU handles, with an 8192-entry cap and periodic expiry; this
avoids rescanning rigid node and mesh flags per rendered instance.
For the separate versioned material-owner opt-in API,
`Model::materialRevision`/`markMaterialChanged()` tracks source material
and GPU texture state, while `ModelDraw::materialOwnerKey` and
`materialOwnerRevision` identify an immutable shared material set. Both
owner fields must be nonzero for a version-based shortcut. Native asset
and override owners must advance revisions on in-place factors, shader,
texture binding **or texture content** changes, including stable handle IDs.
Without these fields, keep exact GPU material fingerprint comparison.
`Renderer3D` now implements a shared, bounded material-owner signature memo
(4096 owners, <=512 materials each, <=8 MiB fingerprint payload) only when
packet caching is enabled and both owner values are nonzero. It hashes
resolved GPU state once per owner revision, then matches a monotonic
signature serial from each saved packet instead of hashing/copying the
same material array per entity. An unchanged owner revision promises
unchanged materials/texture contents even if allocation pointers change.
Model geometry/material revisions, owner revision and material count are
validated. Material quality still controls vertex displacement from the
stored shader threshold. Source changes require `markMaterialChanged()`
or owner revision bump; `invalidateSharedMaterialState(model, ownerKey)`
can immediately retire an individual owner on native unload. The fallback
without trustworthy versions continues exact hash and equality checks.
`StaticModelCachePolicy.h` still validates every draw's pose, static flag,
material displacement, GPU material state, LOD and view state separately.
In-place native changes to node/primitive eligibility must bump
`Model::markGeometryChanged()`; asset unload must clear the model memo
through `Renderer3D::invalidateModelCachesFor(model)`.
`GameView` opts eligible stationary scenery in at the host boundary, but no
packet cache runs without the global experimental flag.

**Cross-model static CPU MDI (experimental, 2026-11-19):**
`AIVERSE_STATIC_CROSS_MODEL_BATCHES=1` enables a separate, static-only
cross-model sort in the existing CPU indexed-indirect batching path.
The common `engine/render/StaticCrossModelBatchPolicy.h` requires distinct
Models to share one `geometryPoolPage` AND identical GPU VB/IB handles.
Only compatible rigid opaque/sun-shadow items (no active deformation, GI,
shader graph, transparent/fade, actor reveal, material-batch roles or LOD
crossfade) can be grouped after independent view/LOD/culling. Material GPU
state, pipeline flags, base indices and per-instance transforms still use
the existing exact checks and MDI command offsets. Do NOT group merely
because two Models use the same texture or Material pointer; different
GPU buffers need separate draws, not fake combined geometry. Vulkan pooled
nonzero base vertices remain on the direct path. The option is independent
of CPU packet caching, disabled by default and forbidden in safe/debug
or reflection/auxiliary passes. Editor Scene → Batching reports submitted
cross-model groups, commands and item counts. Stage 18 added
**page-aware preflight** before this extra sort: group only the ModelItems
already selected for this render pass by exact shared GPU page, resolved
material-state group, flags and VB/IB handles. At least two Models and the
backend's unique indirect-command threshold (Metal 2–32, other backends 4)
must be possible within `instanceLimit` before any page-aware sort runs.
Preflight retains at most 32 distinct primitive pointers per group, caps
candidate groups at 2048, and leaves ordinary sorting untouched on misses,
overflow, or unsupported/safe modes. It never replaces exact final MDI
compatibility checks or per-entity culling/LOD. Stats report **ready groups
versus actual successful CPU MDI submissions**, so neither number alone is
a proven frame-time improvement. Do not use cross-model preflight while
the separate instance-material-palette sorter is active. Stage 19
adds the optional `AIVERSE_CPU_MDI_ADAPTIVE=1` cost-feedback loop,
only active with `AIVERSE_STATIC_CROSS_MODEL_BATCHES=1`.
`engine/render/CpuMdiAdaptivePolicy.h` tracks measured **CPU API
preparation/submission time per distinct indexed command**, separately
for sun-shadow and opaque. It warms up for two attempts, uses a 15% EMA,
and after eight usable samples temporarily prefers ordinary instancing
when the EMA exceeds `AIVERSE_CPU_MDI_MAX_US_PER_COMMAND` (default
25 us; experimental, user-tunable). Stage 20 scopes those decisions
**per shared GPU geometry page and pass**, not globally per pass:
`CpuMdiPageKey` includes the page address + VB/IB handles, and the
renderer keeps a weak page lifetime reference to reject recycled keys.
At most 256 pages have tracked feedback, expiring after 128 unseen epochs;
untracked pages retain normal MDI, never an assumed fallback. Each page
makes one decision per render-scene pass, with a recovery probe every
32 eligible *page* opportunities. A slow page only sends its own
preflight-ready candidates to instancing; fast pages still use cross-model
MDI in the same view. The pass-wide EMA is **diagnostics only**.
Nonpooled/nonstatic MDI and GPU ICB are unaffected. This does **not**
measure GPU time or prove a speedup; keep all flags default OFF until
Metal pixel tests and same-scene CPU/GPU A/B/A. Scene → Batching shows
page counts, shadow/opaque slow pages, per-page skipped/probed groups
and global CPU averages. Stage 21 moves cross-model preflight groups into a
**renderer-owned reusable workspace** in
`engine/render/StaticCrossModelBatchPolicy.h`. It uses an epoch-stamped
4096-slot open-addressed directory, exact key equality, <=2048 groups,
and temporary 16-bit group indices by ModelItem to skip repeated page/
material lookups during the promotion pass. Group/model/page pointers
are valid **only during the current render pass**; beginPass resets the
generation and group contents without releasing high-water capacity.
Stage 22 makes the 16-bit group-index and 8-bit mode arrays
**sparse by epoch stamps**: each actual written item receives a 32-bit
generation, so `groupForItem(index)` and `modeOf(index)` return invalid/0
for stale entries without clearing all ModelItems each pass. `clearModes()`
is a constant-time logical disable; even passes with no ready groups
avoid full-mode array initialization. On item-generation wrap, reset
both stamp arrays once to prevent stale values becoming valid.
Only current-view selected items may be marked; never preserve group
or mode decisions across camera/shadow passes. This costs 8 extra bytes
per retained ModelItem in two generation-stamp arrays, counted in
`retainedBytes()`, in exchange for fewer full-scene writes.
Stage 23 adds `prepareSortRanks()`: after preflight and page-adaptive
admission, accepted groups receive a compact 16-bit rank ordered by
`materialGroup, geometryPage, GPU VB, GPU IB, drawFlags`. When *both*
items are accepted cross-model candidates, the sorter compares that rank
instead of re-reading model/page/material handles on each comparison.
For a shared rank it keeps the original `lmPage, primitive, itemIndex`
tie ordering. All other items continue through the original comparator,
and the MDI pipeline still validates full material and geometry state.
The group-order scratch vector is reused and included in retained memory;
`staticCrossModelRankedGroups` reports ranked groups, **not draw calls**.
Do not cache these ranks across passes or change transparency ordering.
 Stage 33 independently flags persistently expensive CPU model
 stages in the same read-only Stage 31 rolling summary. Reuse the
 existing 120-sample sorted scratch to calculate nearest-rank P50.
 With >=30 samples, only indexes 1..7 are sustained-cost candidates
 if P50 >=0.35ms and stage mean >=15% of mean total model-pass time.
 Metric 0 is a total and NEVER a specific stage. This is independent
 of Stage 32 P95 variability: stable costly stages may flag with no
 spikes, and rows may meet both criteria. Dominant sustained stage
 uses the largest median; don't sum overlapping stage percentages.
 Show orange P95 and blue sustained warnings separately, and never
 use these indicators to change actual batching or GPU submission.
 No extra profiling timestamps, allocation or sort belongs in the
 render path; callbacks read the summary only with profiling enabled.
 Keep Stage 33 handoff and future Metal/Vulkan parity validation.
 Stage 32 annotates the Stage 31 read-only CPU rolling summary with
 conservative P95 variability warnings: at least 30 eligible samples,
 P95 >= 0.25 ms, absolute excess over mean >= 0.15 ms, and P95 at
 least 1.8x mean. Evaluate this only on summary/UI read; do NOT scan,
 allocate, sort or classify spikes in the render hot path. Metric 0
 is the overlapping full model pass and must NOT be named as an MDI
 stage. DominantStage is chosen only from indices 1..7; if only
 total varies say the source is unassigned. Label all warnings as
 possible CPU latency variability, NOT a measured regression, FPS
 loss or GPU time. Continue to keep full scene history invalidation,
 genre independence and stage handoff. Metal/Vulkan validation remains.
 Stage 31 retains the last 120 Stage 30 CPU MDI pass-timing samples
 for shadow and main opaque in a renderer-owned fixed ring OUTSIDE
 `ModelRenderStats`. Per-pass samples are deltas against the existing
 per-beginScene accumulators, not cumulative numbers or whole GPU
 frame timings. Record both the ordinary exit and the empty visible-
 order early return, but never record when profiling is disabled or
 for reflections/auxiliary views. `cpuMdiRollingSummary(phase)` computes
 arithmetic mean and nearest-rank P95 per CPU metric on UI read using
 bounded stack scratch. Never sort or allocate in the rendering hot
 loop. Clear ring on full scene invalidation/shutdown; disabling
 profiling pauses sampling. No GPU timing or speedup claim is justified.
 See Stage 31 handoff and preserve per-stage agent docs.
 Stage 30 adds opt-in `ModelRenderStats::mdiCpuStages[0/1]`
 CPU timing breakdown, controlled by the existing Studio
 "Profile Scene renderer" (`profiling_`) toggle. The shadow and
 main-camera opaque views each report selection, material grouping/
 sorting, compatible-run scanning, command packing/signature validation,
 pipeline/uniform binding, dynamic buffer creation/update and synchronous
 backend `drawIndexedIndirect` API time, plus attempt/warmup/fallback
 counts. The full model-pass wall time includes non-MDI draws and must
 NOT be derived by summing nested stage numbers. Exclude reflections/
 auxiliary views and other phases. These are **CPU**, not GPU timings.
 Do not enable high-frequency timestamps unconditionally, alter fallback
 semantics, or infer GPU performance improvement without A/B/A.
 Preserve per-stage handoff and defer tests per current user priority.
 Stage 29 attributes backend indirect-buffer CPU operations to sun shadow
 (`renderModels(0)`) and main-view opaque (`renderModels(1)`) via
 pre/post monotonic-counter snapshots. Use `gfx::bufferUpdateCounterDelta`
 and `accumulateBufferUpdateCounters` only for additive counters;
 NEVER subtract or sum retained-capacity gauges. A buffer born during
 the pass has a zero baseline; a changed/destroyed buffer handle is
 not comparable. Exclude reflection/auxiliary model views and all other
 phases from the two labelled counters. `ModelRenderStats` resets every
 `beginScene`; these are per-view scene model-pass deltas, **not**
 whole GPU frame, GPU execution, VRAM residency or FPS. Keep Studio
 diagnostics read-only and separate from real draw submissions.
 Stage 28 adds an optional read-only `gfx::Device::bufferUpdateStats(BufferHandle)`
 per-buffer diagnostic for Metal/Vulkan dynamic indexed indirect buffers.
 It accumulates actual backend CPU memcpy/memcmp, skipped equal spans,
 shadow/preserved copies, version/snapshot allocations/reuses and retained
 logical capacities from creation onward, not per frame. Renderer3D surfaces
 this only for its own indirect command buffer in Scene → Batching.
 Metal's current b->bufs[] can alias an indirectSnapshots[] entry and must
 not be double counted as retained capacity. Vulkan Allocation::size is
 only an allocated subrange, not physical GPU residency. No GPU readback,
 frame-time, bandwidth, or performance improvement is implied by the
 diagnostic itself; unsupported RHI backends report unavailable.
 Keep independent in-flight GPU snapshots and optional dirty mode OFF
 until Metal/Vulkan parity and measured A/B. See Stage 28 handoff.
 Stage 27 introduces the narrow `gfx::BufferDesc::discardOnUpdate`
 contract for dynamic indirect buffers whose offset-zero update replaces
 the complete readable command prefix. Renderer3D alone opts into it.
 Metal keeps an independent snapshot for **every** submission and frame
 slot, grows snapshot capacity geometrically, and skips old-tail copies
 for opted-in prefix updates. `AIVERSE_METAL_ICB_DIRTY_UPDATES=1`
 may compare/copy only changed 128-byte chunks of a *completed* slot;
 it stays OFF by default because memcmp costs CPU. Vulkan can omit
 carrying shadow-tail bytes into a new GPU-safe version, but retains
 all frame serial fencing. Never reuse/overwrite command storage that
 a previous draw in the same GPU frame still references, never skip
 model material/LOD/culling validation, and never conflate saved CPU
 memcpy bytes with GPU bandwidth/frame-time gains. Partial updates and
 other buffers remain legacy. Read Stage 27 handoff before changing RHI.
 Stage 26 bounds the per-phase positional CPU MDI layout cache to
 96 entries of at most 256 items each. Cache overflow must use the
 uncached command scratch and must never omit a GPU draw. Each exact
 signature includes Model identity, geometry revision, GPU VB/IB IDs,
 primitive identity and index/base offsets. Unloading a Model calls
 retireIndirectLayoutsFor(model) through invalidateModelCachesFor and
 retires whole mixed-model command groups referencing it. Full scene
 invalidation/shutdown frees layouts; beginScene ages out entries idle
 120 scenes, even in empty scenes. The zero-copy cached command vector
 must never be invalidated *during* drawIndexedIndirect submission.
 The editor reports retained CPU layout bytes, retired entries and
 capacity bypasses. These are not frame-time or GPU bandwidth metrics.
 Stage 25 additionally removes the indirect layout cache's CPU-to-CPU
 command-array copy on exact signature hits: `drawModelIndirectBatch`
 reads `IndirectLayoutEntry::commands` directly until synchronous
 `dev_->drawIndexedIndirect` returns, but still uploads the same commands
 to the GPU and runs exact signature/material/backend compatibility checks.
 Cache misses continue to use renderer-owned scratch and persist their
 finalized layout. Two fixed 64-entry stack arrays replace palette
 preflight/U3 material slot heap allocations without changing material
 order or GPU ABI. Scene Batching reports successfully submitted zero-copy
 layouts and skipped CPU copy bytes, not GPU bandwidth savings.
 Never mutate persistent layouts during a draw, assume the signature
 survives asset changes, or enable experimental cross-model MDI without
 Metal OFF/ON parity tests.
 Stage 24 replaces the optional *large* admitted candidate comparison-sort
 with a bounded ranked bucketization (minimum 64 items). It first partitions
 mode-1 candidates from ordinary/adaptive-mode-2 items; only mode-1 entries
 enter ranked buckets, and each exact group is ordered by lmPage/primitive/index.
 The remaining entries retain the full legacy comparator, and transparent
 passes still use depth sort. An invalid rank causes a safe pre-mutation
 fallback to Stage 23 comparison-sort. Bucket scratch is renderer-owned,
 released on scene invalidation; track bucketed items/groups separately from
 actual MDI submissions. Never reuse rank/mode across camera passes.
`invalidateStaticModelCaches()` and `shutdown()` release this workspace.
Scene → Batching reports its retained CPU memory in KiB. Do not retain
camera, visibility, material equivalence or LOD results across passes, or
change the already mandatory per-draw/final MDI state checks. See
`docs/geometry-performance.md` and the latest batching-agent handoff
before editing these paths.

`engine/render/EcsStaticModelRender.h` provides the **shared genre-independent
ECS presentation adapter** using only integer asset/revision state in the ECS
component and the existing `WorldTransform`; model resolution and namespaced
nonzero persistent draw IDs are caller-owned. The component now also offers
appended `materialOwnerKey` and `materialOwnerRevision` integer fields
(default zero) forwarded into `ModelDraw`, preserving old four-field aggregate
initializers. Set them only for revision-controlled shared material owners;
external material pointers belong in the presentation submit callback, never
in ECS simulation data. Native FPS, RTS and other projects
can compose this view without including either genre module. It must run after
SceneHierarchy's transform update barrier, never in deterministic simulation.

Before editing this feature, read
[docs/agent-handoffs/incremental-static-model-cache-2026-11-19.md](../../docs/agent-handoffs/incremental-static-model-cache-2026-11-19.md),
check current branch/PR #34 and coordinate changes to the shared renderer.
Do **not** enable production defaults until compilation, cross-platform
visual checks and same-scene Mac A/B. Call `Model::markGeometryChanged()` for
in-place geometry/hierarchy edits. On model unload call
`Renderer3D::invalidateModelCachesFor(model)` **before** releasing GPU/model
resources; on ECS entity despawn or asset replacement call
`invalidateStaticModelCache(persistentId)` or the generic
`invalidateEcsStaticModel` adapter. Full scene reload uses
`Renderer3D::invalidateStaticModelCaches()`. These methods run on the
renderer thread between submissions, not inside authoritative ECS ticks.
Camera culling, dynamic LOD decisions, sorting and GPU submissions still
happen each render frame.
**MANDATORY: MotionWeave never depends on game genres or modules.**
The core animation subsystem is NOT allowed to include, depend on or
interpret FPS, RTS, RPG, networking, AI, weapon, economy or game-action
types. Generic sources supply immutable model/graph, stable IDs with
generations, named float/vector parameters and optional animation events.
Any genre host transforms its own state OUTSIDE the animation library.
Core provides reusable ECS components, graph sampling, montage,
retargeting, IK, root-motion policy, scheduling, pose/skin output.
Never fork an algorithm per genre or put genre-specific events in
`engine/render`, `libraries/include/aiverselib/animation` or
`engine/animation`. Use
`"engine/animation/MotionWeaveSourceBridge.h"`.
Document each implementation stage in a separate handoff commit.

**AIVERSE MotionWeave — canonical modular animation system.**
For new projects use `"engine/animation/MotionWeave.h"`,
`aiverse::motionweave`, `motionweave::MotionWeave::update` and
`AIVERSE::MotionWeave`. Legacy `aiverse::lib::animation`,
`AIVERSE::animation_ecs`, `AnimationGraph`, existing JSON asset IDs and
editor window IDs remain source/asset compatible. See `docs/motionweave.md`.

**Active animation PR #22 (`feature/universal-ecs-animation-pipeline`):**
read `docs/agent-handoffs/animation-universal.md` (the latest numbered stage and all
earlier constraints) before editing `engine/render/Animation*`, reusable animation
libraries, editor authoring, or FPS/RTS integration. Keep float-backed animation
in a separate presentation World; never contaminate simulation checksums.

**Required animation handoff for EVERY stage, including partial/failing work:**
commit implementation first, then append a *separate* chronological stage entry to
`docs/agent-handoffs/animation-universal.md` and commit that handoff **before
starting the next stage**. Record (1) branch and exact implementation commit SHA;
(2) changed paths and architecture/algorithm; (3) public C++/ECS/JSON API and
compatibility; (4) commands actually run with results, or **NOT RUN** and reason;
(5) risks, known bugs and work still missing; (6) explicit next file/function/task
with enough detail for another agent to continue independently. Do not replace or
rewrite prior stage entries, and read the latest head before appending. The user
has explicitly deferred animation tests, so preserve test sources and mark runs
**NOT RUN**, never green or passed, until they are actually executed.

## Validation follow-up for CPU MDI GPU safety (2026-11-19)

The Vulkan `writableVersion` API now returns `nullptr` when a
new GPU-safe version could not be allocated, **never** a writable
reference to an in-flight VkBuffer. `updateBuffer` must select a
safe destination BEFORE modifying its CPU shadow, and transactional
growth must preserve the old allocations on failure. Do not revert
to overwriting the previous serial's memory to avoid an OOM. Existing
`tests/render/model_batching_policy.cpp` now exercises Stage 21–24
preflight rank/bucket/generation behavior and Stage 28–29 monotonic
RHI buffer stats; these are CPU tests, not Metal/Vulkan GPU validation.
CI results must be reported only when the corresponding workflow run
actually completes, not inferred from test source or staging.


## AIVERSE Stage 99 render resource lifetime — mandatory for all genres

The generic per-host resource owner is
`aiverse::render::ModelResourceCache` in
`engine/render/ModelResourceCache.h` (with the optional
`ModelPreparationService` CPU worker). Existing
`game::view_adapter::GameModelResourceCache` and
`GameModelPreparationService` are **compatibility aliases**, not
separate implementations. New native project code must use the generic
render headers; no RTS, FPS or editor types may enter the render owner.

On a model replacement or hot reload, detach its MotionWeave graph
sources, `AnimationAssetComponent`, borrowed model/material pointers
and preview packets before the cache entry is invalidated. The cache
queues old Models for deferred GPU release. A host must call
`collectRetired(gfx::Device&)` at a known frame boundary **before**
current-frame submissions reference released objects. This method
uses `gfx::Device::waitForResourceIdle()`: `frameSerial()` is NOT a GPU
completion proof. Never use guessed numbers of frames in flight for
GPU lifetime management. If the barrier fails, retain resources and
retry safely; on cache shutdown a failed barrier raises an exception
and must be treated as a real failure, not ignored. Neither Vulkan
nor Metal nor GL backend completion behaviour has been tested yet.
`Editor::frame` and `GameClient::frame` have integrations; other
hosts must provide their own safe frame boundary. Avoid adding
unrelated raw GPU-release code in legacy GameView.

Keep the persistent Stage 99 handoff in
`docs/agent-handoffs/animation-universal.md`, with exact commit IDs,
changed files, actual tests (or **NOT RUN**) and the next specific
stage. Do not merge to main based only on source inspection; native
builds and GPU lifetime checks are still deferred by user instruction.

## AIVERSE Stage 100 nonblocking GPU resource retirement

For new native code, the generic owned-model entrypoint is
`engine/render/ModelAssetProvider.h` with
`aiverse::render::ModelAssetProvider` and
`aiverse::render::ModelResourceCache`. It accepts a project
`FileReader` and returns borrowed `render::Model*` valid until
source invalidation/shutdown. Never store a borrowed pointer past a
host resource replacement/trim or create a second MotionWeave animation
World during `aiverse::View` packet submission.

The GPU completion API is:
`gfx::Device::beginResourceRetirement()` (token, zero means no
async marker), `resourceRetirementComplete(token)` (nonblocking poll)
and `endResourceRetirement(token)` (close only when complete).
`ModelResourceCache::collectRetired()` uses the
`AsyncResourceRetirementQueue` with independent token batches:
a model evicted AFTER an earlier marker must never be freed on
that earlier token. Use `waitForResourceIdle()` only for unsupported
token fallback or explicit device shutdown, not every frame. The
caller MUST be on the owning GPU thread, at the host frame boundary,
before new draw/compute commands can reference retired models.
Neither a GPU frame serial nor guessed frames-in-flight is a fence.

Do not claim safety in the presence of unsubmitted current-frame GPU
commands referencing retired assets. Backend loss must fail closed:
if completion cannot be established, keep objects alive and do not
silently release GPU allocations. GPU retirement token/real-backend
tests are authored but **NOT RUN** by user request. Follow
`docs/agent-handoffs/animation-universal.md` Stage 100 for status
and the next exact work item. Never merge PR #22 to main on
source inspection alone; wait for native/runtime checks.

## AIVERSE Stage 101 async model resources and GPU retirement pressure

For any native project (independent of FPS/RTS/genre), prefer
`render::ModelAssetProvider` and `AsyncModelCpuQueue` from
`engine/render` rather than adding new model loader code. CPU
`requestAsync`/reader callbacks must be thread-safe. Publish finished
CPU models through `ModelAssetProvider::publishReady(gfx::Device&,...)`
on the device owner/render thread, outside an active pass. The
`source` revision protects against stale workers resurrecting
hot-reloaded assets. Always call `shutdown`/join before destroying the
project reader or shared texture cache.

Retired GPU models remain alive until their OWN GPU completion token
signals (see Stage 100). `ModelResourceCache::retirementStats` reports
pending models/batches, peak and fallback counts;
`setRetirementSoftLimit` is a **soft admission throttle** for new
generic ModelAssetProvider loads, not a hard VRAM cap. Never release
an object to meet a pressure threshold while its GPU use remains
unconfirmed. Completed legacy RTS worker publications must not be
silently discarded under backpressure. The editor exposes the
metrics through Asset Residency statistics.

`ModelWindow` now has an optional raw static-model preview through
the independent generic source provider and canonical View. It
intentionally bypasses type-level tint, material overrides, graph,
attachments and model correction; do NOT switch ordinary animated
character/type portraits to this path without full parity work.
Source code and deferred fixtures were committed, but the actual
CMake/compiler/GPU tests remain **NOT RUN** as requested. Follow the
latest `docs/agent-handoffs/animation-universal.md` and add the
next handoff as a SEPARATE commit after each feature stage.

## AIVERSE Stage 102 — source-derived GPU memory estimates and model import policy

New project code should include `engine/render/ModelResidentEstimate.h`
for read-only estimates of dedicated model geometry, owned image base
levels and baked pose texture storage, plus
`engine/render/ModelAssetProvider.h` for source imports. The estimated
bytes are **not driver VRAM usage**: shared geometry pool pages,
borrowed/shared texture leases, driver padding and unknown morph
storage remain separate. `ModelResourceCache::estimatedRetiredGpuBytes`
and `setEstimatedRetirementByteLimit` optionally throttle admission
of NEW native source models. A budget MUST NOT cause forced GPU
destruction before the correct completion token signals.

`AsyncModelCpuQueue` accepts `ModelLoadPriority`:
`Background`, `Visible`, `Interactive`. Queued work can upgrade,
equal-priority pending work remains FIFO, and executing work is not
preempted. `ModelAssetProvider::publishReady(device,maxCount,budgetMs)`
must run on the render/device thread before scene submissions. The
count cap is hard; elapsed milliseconds are advisory and one upload
may exceed them. Never make a CPU worker invoke `gfx::Device`,
renderer, presentation `World` or `aiverse::View` operations.
Handle stale source revisions and project switches before publication.

The standalone `aiverse_modelview --provider --no-merge` routes
static raw files through the independent `ModelAssetProvider`, then
canonical View. The legacy path is preserved for merging and
animated-model parity. Editor raw preview remains opt-in, with
the default full type portrait unchanged. Do not silently switch
ordinary animated type previews or assume GameView has disappeared.
Test fixture code is committed but **NOT RUN**; follow
`docs/agent-handoffs/animation-universal.md` Stage 102 for exact
next tasks and maintain a separate handoff commit after each stage.

## AIVERSE Stage 103 — bounded multi-worker model preparation and resource owner accounting

For genre-neutral C++ model loading, use
`engine/render/AsyncModelCpuQueue.h` through
`ModelAssetProvider::setCpuWorkerCount(1..8)`,
`setCpuMaxQueued`, `requestAsync(source,priority)` and
`publishReady(device,count,budgetMs)`. The default worker count is
one for compatibility; opt into concurrency only for genuinely
thread-safe `FileReader` providers. Do not touch `gfx::Device`,
`Renderer3D`, View, ECS Worlds or MotionWeave player states on
workers. All GPU uploads and model publication stay on the owner
render thread, between draw submissions. Request and completion
order are distinct: use source revisions to reject stale results
after a hot reload or project switch. Do not destroy readers,
decoded-image caches or other captured sources before joining
the CPU worker queue.

`ModelLoadPriority` affects only CPU job admission and order:
Interactive > Visible > Background. Same priority uses FIFO;
bounded fairness occasionally admits the oldest queued source.
Workers are not preempted. A resource pressure limit never
authorizes GPU release; the exact async retirement token/fence
must complete first.

For GPU diagnostics use `ModelResourceCache::gpuOwnerEstimate`
and `ModelTextureCache::gpuOwnerEstimate`, accounting each unique
shared texture cache entry once. The global geometry pool is
device-owned and must be reported separately via
`geometryPoolStats(device)`; never multiply shared pages by
the number of models or hosts. All byte counts are estimates,
not precise driver VRAM allocations.

The editor `SceneRenderable` model/unselected mesh path uses
`aiverse::View` while selected meshes keep their specialized
silhouette, and the old RTS appearance/animated portrait adapter
still exists. Keep the generic View independent of genre classes.
New tests are authored but **NOT RUN** under the features-first
rule. After each implementation stage append a separate handoff
in `docs/agent-handoffs/animation-universal.md` with exact source
SHAs, changed paths, reality of test execution, risks and next step.

## AIVERSE Stage 104 — persistent CPU workers, shared geometry page accounting and View selection

Use `render::ModelAssetProvider` / `AsyncModelCpuQueue` for
genre-neutral model imports. The CPU queue now keeps up to 8
persistent worker threads (1 default) with condition-variable
dispatch, priority and bounded pending/ready queues. Worker
threads may call only thread-safe FileReader and CPU model
preparation; never gfx::Device, ECS Worlds, AnimationGraph
players or View submission. Render-thread
`ModelAssetProvider::publishReady` still owns GPU publication.
Reconfiguration via `setCpuWorkerCount` joins currently running
imports; it is not a per-frame knob. Invalidated source revisions
must not be published after project/asset changes.

When reporting shared geometry pool memory, gather references
across all relevant ModelResourceCache owners and pass a **combined**
span of Model pointers once to
`geometryPoolReferencedStats(device,models)`. This counts unique
GeometryPoolPage instances using their device identity. Do not add
per-owner page sums together or double charge shared texture
leases; use `geometryPoolStats(device)` for device-global pool
allocation and `ModelTextureCache::gpuOwnerEstimate` once per
texture owner for distinct textures. These are allocator/format
estimates, not driver VRAM readings. Retired-but-fenced models
remain page borrowers until GPU completion is verified.

`aiverse::view::MeshObject` now carries the optional
`selectionOutline` and `selectionColor` fields. Native
editor/scene clients should use `View::submitMesh` for both
unselected and selected/hovered meshes and `View::submitLine`
for supplementary pickable edges, preserving their exact
Renderer3D silhouette/outline behavior. No genre, network,
save/checksum or game-simulation concepts belong inside the
canonical View. Do not introduce a second MotionWeave graph
update when submitting these packet types.

Stage 104 test fixtures and CMake entries are **NOT RUN**
by user request. Maintain `docs/agent-handoffs/animation-universal.md`
after every implementation stage as a separate commit with
architecture, changed files, actual validation, known risks,
current ref and next exact step. Do not merge unvalidated changes
to main based only on source-level inspection.

## AIVERSE Stage 105 — multi-host resource groups and authored static portraits

For native model resource aggregation, use
`engine/render/RenderResourceOwnerGroup.h`. A group can
non-owningly register multiple `render::ModelResourceCache`
hosts and deduplicates exact Model pointers, shared texture
cache owners and device-global geometry pages. Only call
`snapshot(device)` on the device owner thread; all registered
cache allocations must belong to the same gfx::Device.
`snapshotOwners(bool gpuHealthy)` is the CPU-only source
diagnostic path. Do not add shared geometry pool allocations
per model/host, and do not double-charge borrowed textures.
The group's estimated-byte admission limit is advisory and
must NEVER cause forced destruction before the resource's own
GPU fence signals. `PendingGpuRetirement` is normal;
`DeviceUnhealthy` means new imports/publication must pause
until the host resolves backend loss (no timeout-based release).

For static previews that require full authored appearance,
use `game::Portrait::renderAppearance(ModelDraw,camera,...)`
or submit a `view::Object` to `aiverse::View`. The host owns
model/material vectors, variant, geometry policy, tint and
optional precomputed pose until the synchronous submit returns.
The normal `Portrait::render(GameView&,...)` now delegates
static resolved packets to this API with its original camera;
animated/complex scenes keep the legacy fallback until visual
parity and single-writer animation evaluation are verified.
Do not introduce a genre dependency into engine/render or
canonical View.

Stage 105 GPU-owner and portrait API tests are authored but
**NOT RUN**, as are native compiler, render device, FPS/RTS,
screenshot and sanitizer checks. After each feature stage
append a separate detailed handoff to
`docs/agent-handoffs/animation-universal.md` including
exact Git refs, actual tests, limitations and next step.

## AIVERSE Stage 106 — expiring resource owners and failed GPU retirement tokens

Use `ModelResourceCache::lifetimeToken()` when observing native
render caches across project boundaries. `RenderResourceOwnerGroup`
holds a weak token and a borrowed cache pointer and skips expired
registrations before access; do not store unguarded raw pointers to
transient Editor/Portrait/Project model owners. The registration,
snapshot and cache teardown MUST all run on the device owner thread;
weak_ptr expiration does not protect against concurrent destruction.
The group is for accounting/admission only and never destroys GPU
resources.

`gfx::ResourceRetirementStatus` distinguishes `Pending`,
`Complete`, and `Failed`. Metal/Vulkan/GL backends now override
`resourceRetirementStatus(token)`. Never equate normal Pending
with GPU failure or infer completion from time, frame count or a
later unrelated fence. `AsyncResourceRetirementQueue` records a
Failed token once, retains every failed batch, and only frees it
after a successful full `Device::waitForResourceIdle` barrier.
After that barrier the backend may call
`abandonRetirementTokenAfterIdle` to release native marker
bookkeeping. Without a successfully established idle barrier,
device-loss paths MUST keep resources alive rather than
destroying them unsafely. Failed fence status blocks new-source
admission; it does not kill gameplay or rewind MotionWeave.

The live material library preview now uses the generic
`Portrait::renderAppearance` draw packet and canonical
`aiverse::View`. It still resolves materials in the existing
compatibility host and must not create a second MotionWeave
animation player or replace the authored-animated fallback
without verified visual parity.

Stage 106 fixture sources are present but **NOT RUN**.
Maintain `docs/agent-handoffs/animation-universal.md` with exact
commits, changed paths, actual tests and next action after each
stage, in a separate handoff commit. Do not merge source-only
unverified changes to main.

## AIVERSE Stage 107 — scoped GPU device ownership, replayable fence diagnostics and appearance packet providers

For all NEW native render hosts, call
`ModelResourceCache::bindResourceDevice(device)` before GPU
upload/replacement or use `RenderResourceOwnerGroup::registerScoped(
device, cache)`, keeping its move-only lease alive until after
the last resource snapshot. All resource registration, host
mutation, shutdown and View submission MUST run on the same
device-owner thread. Do not share an already GPU-bound cache with
another device. A failed shutdown leaves binding intact and GPU
resources retained; only successful GPU-safe `shutdown` allows
device rebind. `snapshot(device)` now rejects expired/rebound
scoped registrations. A wrong-thread scoped lease release is
diagnosed, not executed. The legacy `add(cache)` is a CPU-only
compatibility observer, not a safe replacement for device-bound
leases. The GPU Device itself must outlive all attached caches.

Use `gfx::ResourceRetirementStatus::{Pending,Complete,Failed}`
and the existing fence completion semantics; NEVER infer GPU
completion from elapsed time or frame serial. For debugging,
`AsyncResourceRetirementQueue::setDiagnosticObserver` emits
token lifecycle events and `render::RetirementStatusReplay`
can replay a scripted synthetic sequence. A synthetic replay is
NOT a native GPU fence. Diagnostic callback exceptions are
ignored so they cannot corrupt retirement. A Failed token
must retain its Models until an actual successful idle barrier.

For genre-neutral fully authored visuals, use
`engine/view/AppearancePacket.h` with `AppearanceSpec` and
`buildAppearancePacket`. The caller supplies Model, materials,
node assignments, geometry, variant, tint, optional animation
graph and source identity; the generic builder creates
`view::Object` without genre dependencies. RTS static preview
and ghost packets now delegate common appearance composition
to this provider. No second animation player may be created:
static/precomputed submissions must have no graph, while graph
inputs require one owning `View::update` before submit.
The remaining GameView rules/material/graph authoring is
compatibility code pending further migration and real
visual parity testing.

Stage 107 regression sources are present but tests have NOT RUN
by user request; handoff in
`docs/agent-handoffs/animation-universal.md` records actual
source changes, limitations and the next agent step. Do not
merge unverified GPU/resource/animation changes to main.

## AIVERSE Stage 108 — explicit GPU device epochs, bounded retirement traces and independent appearance authoring

A native host that supports GPU device recreation must create
one `render::RenderDeviceLifetime` **per actual device
lifetime** and use its `Lease` when calling
`ModelResourceCache::bindResourceDevice(device,lease)`
and `RenderResourceOwnerGroup::registerScoped(lease,cache)`.
A generation invalidates when the host explicitly retires
the epoch, even if a new GPU Device is allocated at exactly
the same pointer address. Do NOT retire a token before all
ModelResourceCache/Portrait/View GPU allocations have been
safely shut down. Only a successful full-device completion
barrier permits cache shutdown and subsequent rebind.
Neither a weak epoch nor `gfx::Device::frameSerial()` is a
GPU completion fence. Resource/group calls remain confined
to the render/device owner thread.

Use `render::RetirementDiagnosticRing` for bounded metadata
tracing of GPU retirement only, never for GPU leases or
proof of completion. It has a fixed event capacity and
overflow counter, and a weak observer to avoid extending
host lifetime. The editor exposes the first 128 events.
Every event callback must be optional, non-authoritative,
and exception-isolated so it cannot change destruction
ordering or release an in-flight GPU resource.

For new native project appearance authoring use
`engine/view/AppearanceAuthoringService.h`.
`resolveMaterial` composes a material library's `base`
and `use` references with bounded recursion. `applyMaterial`
accepts callbacks for project texture upload/warnings;
`compileGraph` compiles inline or external
AnimationGraph assets with host-provided operations/traits.
The service must NOT allocate an animation player, run
MotionWeave graph clocks, or import RTS/FPS/game classes.
The legacy RtsLook and runtime RtsAnimate graph compilation
now delegate to it while retaining host-specific rules,
material texture residency, attachments and pose authority.
For rendering use the canonical `view::AppearanceSpec`
and `aiverse::View`; there is exactly ONE graph evaluator
for each presentation source.

Stage 108 test sources/CTest entries exist but are **NOT RUN**
by user instruction. After each stage append a separate
`docs/agent-handoffs/animation-universal.md` entry listing
architecture, API, changed files/commits, actual tests,
known limitations and the next precise task. Do not merge
uncompiled GPU/resource/animation source into main.

### Animation/View Stage 109: immutable appearance transactions

- The genre-independent authoring API lives in `engine/view/AppearanceAuthoringService.h` and `AppearanceAssetRegistry.h`; do not import RTS/FPS types into it. A `Request` resolves material overrides and optionally compiles an authored/external AnimationGraph before publishing a pinned immutable `Asset`.
- Hot reload must call `invalidateSources` with the host's material library/model/external graph paths, then `publish` at a safe frame boundary. A failed publication leaves the old asset and dirty flag alive. `graphRevision` handles in-memory graph changes; material-only changes preserve the graph instance generation.
- A returned `Asset` owns material overrides and immutable graph, but borrows `Model` and external texture indices. Keep its shared snapshot alive through `Asset::packet` and View submission. Retire/release GPU resources only after the last borrowed render packet and snapshot are gone. TextureLoader callbacks must manage their own side-effect rollback. Only the host-owned View/MotionWeave may advance an animated source once.
- Standalone FPS now uses `AppearanceAuthoringService::compileGraph` and a host-owned `RenderDeviceLifetime` for GPU character load/shutdown; do not multiply AnimationGraph clocks. Do not assume the RTS GameClient, editor-hosted FPS, postFX or other direct GPU owners are fully epoch protected.
- Stage 109 regression `tests/view/appearance_registry.cpp` / `view_appearance_registry` is registered but **NOT RUN**. CMake/CTest/graphics/sanitizer checks remain deferred at user request (implement features first). For exact commits/risks/next task see `docs/agent-handoffs/animation-universal.md` Stage 109.

### Animation/View Stage 110: appearance choice and hot reload contract

- AIVERSE scene/animation modules are **genre-neutral**. Material
  `materials`, node `assign`, and `variant` are now authored through
  `engine/view/AppearanceChoiceAuthoring.h`, not copied into each RTS/FPS
  game. Retain `*`-before-named layer ordering, case-insensitive descendant
  node matching, late-node-rule precedence, and warning policy.
- The FPS animated-character load and `graphFile` hot-reload path use
  `AppearanceAssetRegistry` directly. Its snapshot owns the draw
  materials/node-material vector and immutable graph. Pin that snapshot for
  the synchronous frame. Only the current FPS presentation ECS owns the
  actual graph clock. On successful graph change, restart existing ECS
  players once; on failure do not change players or model draw materials.
- `Request::modelRevision` and explicit `invalidateSources({Model::path})`
  prevent graph program reuse against a replaced Model at the same address.
  Host GPU teardown must still detach all packets/snapshots before Model
  shutdown, and texture loader side effects require host cleanup.
- The legacy RTS adapter consumes the generic choice plan for materials and
  variants, while preserving live animations/IK/attachments/LOD/stages.
  **Do not** assume the adapter is ready for removal or that model textures
  are GPU-transactional.
- Deferred CTest `view_appearance_choice` in
  `tests/view/appearance_choice.cpp`, and prior
  `view_appearance_registry`, are registered but **NOT RUN**. No build,
  CTest, TSAN, FPS/RTS/editor screenshot validation was performed. Follow
  Stage 110 handoff for specific commits, risks and exact next step.

### Animation/View Stage 111 — RTS reload and GPU epoch ownership

- `AppearanceAssetRegistry::publishPrepared` accepts already-resolved
  `Asset` material/node/variant/graph data and stores immutable
  snapshots **without** texture loading or graph recompilation. It
  validates prepared indices before publishing but cannot roll back
  texture/GPU callbacks already performed by native hosts.
- `GameView::reloadAppearanceTypes` now preflights external
  AnimationGraph JSON, named material references and ShaderGraph
  programs, stages a snapshot for each model/LOD/stage, and only then
  swaps committed TypeLooks. For a **material-library-only** reload,
  preserve entity clocks only if all graph signatures, Models and
  runtime shapes match. Never introduce a second pose writer.
- `GameView::drawObject` uses published materials/assignments/variants
  when a snapshot is present. `GameView::shutdown` must detach
  graph/pose/TypeLook/snapshot borrows **before**
  `ModelResourceCache::shutdown`. GPU uploads performed before an
  exception may still be resident: do NOT treat this as an atomic RHI
  transaction.
- A standalone `GameClient` owns one `RenderDeviceLifetime`;
  an editor-hosted GameClient borrows the Editor's generation via
  `ClientHost::start(..., gpuDeviceLease())`. Bind a compatible
  lease to the RTS `ModelResourceCache` before GPU model operations.
  Never create two independent generations for one live shared Device.
  The editor must stop its hosted client before destroying its own
  resource owners and retiring the one device epoch.
- Deferred `view_appearance_prepared` and expanded
  `render_device_epoch` tests have **NOT RUN**. No CMake/CTest,
  native runtime, GPU loss or visual parity validation was performed.
  Read `docs/agent-handoffs/animation-universal.md` Stage 111 for
  exact implementation commits, limitations and next step.

### Animation/View Stage 112 — staged GPU image rollback and render epoch checks

- Hot model-appearance reload must stage new decoded textures with
  `render::ModelImageUploadTransaction`; do **not** mutate
  `Model::images` or publish future material indices before all
  selected types/LODs/stages pass authoring checks and commit.
- Keep `render::ModelImageRollbackPool` alive on the render host
  after a staging transaction is destroyed. Call `collect` only at
  a valid frame boundary, and require a successful full-idle
  `drain` before device shutdown. Pending/Failed GPU tokens
  NEVER authorize destroying a texture. If an allocation and
  GPU-idle fallback both fail, prefer a diagnostic orphaned handle
  until device teardown rather than an unsafe GPU free.
- `GameView::reloadAppearanceTypes` uses the transaction for
  new material images, retains last-good appearances on staging
  failure, and calls image `commit()` before swapping TypeLooks.
  `GameView::collectRetiredModels` polls both image and model
  queues; the **Editor and GameClient** must call it when
  `pendingModelRetirements()` OR `pendingRolledBackImages()` is
  nonzero. The Editor Stats panel surfaces image rollback failures.
  GameView shutdown drains rolled-back images before releasing
  Model resources.
- PostFx, RTS terrain/groundFX, general `fx::ParticleSystem`,
  and the Editor's Scene/Game Viewports now receive the host-owned
  `RenderDeviceLifetime::Lease`. Editor and its hosted GameClient
  must reuse the same generation. Validate before GPU draw/alloc/
  release operations; an epoch is NOT a GPU completion marker.
  ParticleSystem shutdown deliberately avoids calling an expired
  device, possibly leaking orphan handles until actual device
  destruction; all ordinary shutdowns must run before epoch retire.
- Deferred native `render_model_image_transaction` test is wired,
  but **NOT RUN**, alongside all earlier deferred animation GPU
  tests. Stage 112 does not implement global GPU allocator
  transactions or actual device-loss recovery. See the exact
  commits, limitations and next action in
  `docs/agent-handoffs/animation-universal.md` Stage 112.

### Animation/View Stage 113 — scratch Model and GPU alias transaction rules

- For RTS appearance reload, new Models, LOD alternatives, stage
  replacements and attachments must be staged in a temporary
  `ModelResourceCache::ModelMap`. Do not insert new resident
  Models or failed-load tombstones before the full transaction
  commits. Preserve Model* identity through `std::map` node transfer.
- Speculative `uploadModel(dev,model,false,false)` may
  `borrowResident` existing GPU textures from
  `ModelTextureCache` but **must not** publish new shared
  cache keys. Misses use dedicated staged GPU image allocation.
  This protects shared residency on abort; it does **not**
  yet deduplicate newly staged texture misses across Models.
- Rejected scratch Models belong to the GPU-fenced
  `ModelResourceCache::retireUnpublished` path, never plain
  `unique_ptr::reset` after partial uploads. Use
  `AsyncResourceRetirementQueue::enqueueFrom` when preserving
  caller ownership after an allocation failure matters.
  If a full idle barrier fails, retain/leak an orphan handle
  rather than using unsafe GPU destruction. Expose and review
  orphan diagnostic counts before attempting a device recreation.
- `Renderer3D` now has an optional shared
  `RenderDeviceLifetime::Lease`; editor, hosted/standalone
  GameClient and FPS supply their existing host epoch. Never
  mint a second generation for the same gfx::Device. Epoch
  checks prevent stale access but are not GPU fences.
- The editor's bounded `RetirementDiagnosticRing` tags
  Model and Image events. GPU source-event counts are
  informational only; Model/Texture release still needs
  backend completion. Deferred native tests have **NOT RUN**;
  Stage 113 code is unverified for compilation/runtime and
  PR #22 stays Draft. See the separate Stage 113 agent handoff
  for exact commits, unfinished work and next stage.

### Animation/View Stage 114 — shared cache, GPU compiler and offscreen ownership

- New speculative GPU texture misses (Model images and pending
  material override images) are prepared together through
  `ModelTextureCache::prepareGpuPromotion`. Only after the outer
  appearance transaction succeeds may `commit()` publish shared
  GPU owners. Immediately pass returned duplicate owned images to
  `ModelImageRollbackPool::retire` for GPU-fenced destruction.
- Do NOT retain `Image*` pointers into `Model::images` across
  material append/reserve. The promotion plan identifies images by
  **stable Model* + final image index**. Proposed material image
  pointers are valid only until `ModelImageUploadTransaction`
  commits or aborts. Avoid GPU cache publication inside CPU staging.
- `Renderer3D::warmModelPipelinesChecked` distinguishes Ready,
  Pending, Failed. GPU compiler failure must preserve the old
  TypeLook/AnimationGraph; on Pending the editor watcher must retry
  rather than acknowledge. GPU shader cache allocation is NOT an
  all-or-nothing RHI transaction.
- General native `ModelAssetProvider` sync and async publication
  use `ModelStagingTransaction` with the same cache and GPU fence
  ownership. A failed partial GPU model upload is NEVER directly
  released without retirement completion. Keep a persistent rollback
  pool and poll it at frame boundaries.
- Editor Scene/Game offscreen color/depth targets are owned by a
  shared `OffscreenTargetRetirement` queue via weak Viewport handles.
  Resize and window destruction enqueue target pairs; the editor
  drains with a proven GPU idle barrier before retiring its sole
  gfx::Device epoch. The editor GPU diagnostic ring includes
  `RetirementResourceKind::Target`.
- All Stage 114 tests are **DEFERRED / NOT RUN** per user's
  features-first instruction. Source commits are NOT proof of native
  C++ compilation or Metal/Vulkan/GL correctness. Keep animation
  PR #22 Draft and follow separate Stage 114 handoff for actual
  validation, limitations and next task.

## Validation follow-up for CPU MDI GPU safety (2026-11-19)

The Vulkan `writableVersion` API now returns `nullptr` when a
new GPU-safe version could not be allocated, **never** a writable
reference to an in-flight VkBuffer. `updateBuffer` must select a
safe destination BEFORE modifying its CPU shadow, and transactional
growth must preserve the old allocations on failure. Do not revert
to overwriting the previous serial's memory to avoid an OOM. Existing
`tests/render/model_batching_policy.cpp` now exercises Stage 21–24
preflight rank/bucket/generation behavior and Stage 28–29 monotonic
RHI buffer stats; these are CPU tests, not Metal/Vulkan GPU validation.
CI results must be reported only when the corresponding workflow run
actually completes, not inferred from test source or staging.

## Scene-only CPU cross-model MDI override (feature continuation)

The pooled static CPU MDI opt-in no longer requires a process restart
when testing **Scene viewport** features in the editor. The renderer
has `Renderer3D::StaticCrossModelBatchMode` with `Environment`,
`Enable` and `Forbid`; the default is `Environment`, preserving
`AIVERSE_STATIC_CROSS_MODEL_BATCHES=1` behavior and default OFF.
`SceneWindow` explicitly sets this before `ed.renderScene` and
restores `Environment` after Scene rendering, before Game.
`Renderer3D::beginScene` resolves the effective mode once per view.
The optional `AIVERSE_CPU_MDI_ADAPTIVE=1` feedback only runs if the
*effective* cross-model mode is enabled. UI preflight counters must
work for the Scene-only Enable mode, not just an environment flag.
Do not auto-enable GPU ICB, change global geometry pooling or skip
shader/material/VB/IB/LOD/indirect safety gates. Keep it genre-neutral.
Add a separate feature-stage handoff; defer native tests per request.

## Renderer-level CPU MDI policy controls (feature continuation)

`Renderer3D::CpuMdiPolicyMode` (Environment/Enable/Forbid) configures
adaptive page feedback, CPU indirect layout cache and geometry sorting
*per renderer view* using `setCpuMdiAdaptiveMode`,
`setMdiLayoutCacheMode`, `setMdiGeometrySortMode`. Resolve these at
`beginScene()`, never per draw, preserving all process env defaults.
The optional CPU adaptive budget uses
`setCpuMdiAdaptiveBudgetOverrideUs(0)` to inherit env (default 25 µs),
or a validated value in [1,1000] µs/command. Adaptive feedback requires
effective static-cross-model batching and its own opt-in. Studio applies
these controls only to the Scene view and restores Environment/0 before
Game. Never globally toggle Metal GPU-encoded ICB, shared geometry
pooling, scene shader/LOD or bypass GPU safety/fallback. No tests until
all batching functions are finished, per current user instruction.

## Per-host CPU MDI adaptive scope contract (feature continuation)

`CpuMdiPageKey::scope` participates in hash/equality; admission
policy uses it to isolate feedback across independent renderer hosts
sharing a pooled GPU page. Default scope 0 preserves old clients.
Use `setCpuMdiAdaptiveScope(uint64_t)` before `beginScene`. The
Studio Scene window uses a stable nonzero scope and restores zero
before Game. Do not use transient ECS Entity IDs here: scope is a
host/view namespace, not a source model/entity identity. The 256-page
cache bound is shared across scopes; untracked pages keep normal
safe MDI admission. This never changes GPU geometry eligibility,
material compatibility, shader binding, GPU-owned in-flight memory
or draw fallback. Document stage and defer tests until all features.

## CPU MDI host configuration snapshot (feature continuation)

Use `Renderer3D::CpuMdiPolicyOverrides` and
`cpuMdiPolicyOverrides()`/`setCpuMdiPolicyOverrides(...)` to
temporarily override any per-host CPU MDI policy in a renderer.
It includes static cross-model mode, adaptive mode, layout-cache
mode, geometry-sort mode, budget and adaptive-scope namespace.
Always restore the *captured prior policy* after a temporary editor
view; do not blindly write `Environment` or scope 0, as native
game hosts may have configured nondefault explicit policy options.
This snapshot is CPU-only and does not own GPU commands, page EMA
samples, scene entities, shader state or rendering resources.
Preserve the renderer's genre independence and document each stage.
No tests now, per user instruction.


## Scoped CPU MDI policy override — Stage 35

For Scene/native FPS/RTS render hosts sharing a `Renderer3D`, prefer
`Renderer3D::ScopedCpuMdiPolicyOverride(renderer, policy)` around
one view instead of manual snapshot/reset code. It saves the *actual*
previous `CpuMdiPolicyOverrides` and restores it with stack lifetime,
including early returns/exceptions; it is noncopyable and nonmovable.
Nested scopes restore LIFO. Do not let it outlive the renderer or race
across threads, and do not assume it snapshots GPU state, effective
`beginScene` flags, ECS scene state, profiling, shader/LOD or page EMA.
The Studio Scene viewport uses it and does not overwrite a game host's
configured CPU MDI policy. Generic backend/material/geometry constraints
remain authoritative. PR #34 remains Draft; native validation deferred
under user's feature-first instruction. See Stage 35 handoff.


## Model batching host policy isolation — Stage 36

Use `Renderer3D::ModelRenderPolicyOverrides` for instancing,
multi-draw, GPU-authored model draws, model culling, LOD and fade width.
`ScopedModelRenderPolicyOverride(renderer)` snapshots and restores the
existing configuration at scope exit. The two-argument overload can
apply a temporary policy. Nest with the CPU MDI guard from Stage 35
when rendering with a shared Scene/Game/native Renderer3D. Do not
unconditionally restore `true` defaults after an editor view.
Do not treat the value snapshot as a GPU buffer/sampler/scene rollback:
`instanceLimit` is deliberately not included because it can trigger
shader pipeline recompilation; profiling/debug/selection have their
separate existing lifecycle. FPS editor debug Mesh previews are *not*
static ModelDraw packets; the engine's genre-neutral static ECS
adapter is `engine/render/EcsStaticModelRender.h`. Cross-model CPU
MDI still requires genuinely shared GPU geometry and exact material
compatibility. PR #34 Draft, native validation deferred as directed.


## Rigid LOD shared GPU geometry pool — Stage 37

The public shared geometry upload code now has an **experimental**
`AIVERSE_GEOMETRY_POOL_RIGID_LOD=1` opt-in (default OFF) plus
`rigidLodGeometryPoolingEnabled()`/
`setRigidLodGeometryPoolingEnabled(bool)`.
`GeometryPoolEligibility.h::rigidGeometryPoolCandidate` is the
genre-neutral predicate for uploaders: safe rigid LOD chains may
use the shared VB/IB page, but skin, morph, authored clips,
material variants, deformable or clustered meshes remain dedicated.
Reject invalid/self/nonfinite LOD thresholds/targets, and preserve
existing backend and size fallbacks. This affects asset **upload**
only, NOT live per-pass LOD decisions or cross-model material
compatibility; no existing buffers are repacked in place.
Scene Batching checkbox triggers editor `reloadRules()`.
Global setting affects all game/native hosts, unlike scoped
Scene-only renderer policies. Vulkan nonzero pooled base vertex
still falls back to direct draws. Keep PR #34 Draft and do not
claim a speedup or compilation before native testing. Document
every subsequent stage separately.


## Stage 38 — native per-asset geometry-pool admission

`Model::Batching::geometryPoolPolicy` supports
`Inherit` (default), `Dedicated`, `AllowRigidLods`.
The native per-model value is also loaded via the strict optional
`<asset>.batching.json` string `geometryPool`:
`inherit`, `dedicated`, `rigidLod`.
Only the **specific asset** is affected at GPU upload time.
Do not bypass global safe-mode/backend/size gates or try to
repack GPU buffers of already-uploaded models by changing this enum.
Use separate renderer asset-lifetime invalidation/reload.
Cross-model CPU MDI still requires shared page, exact GPU
material/flags, enough commands and no active crossfade.
No FPS/RTS dependency; project C++ may assign policy before
`uploadModel` directly. This feature is source-committed
but not compiled/tested on native GPUs yet. Keep PR #34 Draft
and record a separate handoff for each additional step.


## Stage 39 — read-only geometry-pool rejection reasons

Use `geometryPoolAdmission(device, model)` before GPU upload to
inspect why an asset cannot enter shared rigid geometry pages.
`GeometryPoolAdmission::reason` classifies global enablement,
backend, missing CPU data, dedicated asset choice, unsafe model and
oversized model; `modelBlock` provides specific rigid/deformation/
LOD exclusions (via `rigidGeometryPoolBlockName()`).
The actual upload uses the identical predicate. This is
allocation-free *CPU preflight*, not evidence of a real shared page
allocation, indirect MDI submission or speedup. Query BEFORE the
normal importer discards CPU vertices. No new Studio per-frame work,
no genre dependencies and no default-flag changes. Native and GPU
tests still deferred under user's feature-first direction.


## Stage 40 — shared GPU page last-owner release

`engine/render/GlobalGeometryPool.cpp::releaseModelGeometryPool`
now serializes `shared_ptr` ownership transitions against uploads
and `geometryPoolStats` using `poolMutex`. The last owner retires
the page's weak directory entry and erases empty device entries,
then destroys the GPU buffer *outside* the lock. Keep this invariant
when changing model/asset streaming: no concurrent uncoordinated
write/release of the same Model; RHI must retain in-flight GPU use.
Do not reintroduce an unlocked `use_count()` check or destroy shared
buffers while another Model owns the page. No tests have been run
for this stage; retain Draft PR #34 until feature completion and
Metal/Vulkan/release-stress validation.


## Stage 41 — checked shared geometry Storage updates

`gfx::Device::updateSharedBufferChecked` is the upload API for
pooled rigid vertex/index ranges. Metal and Vulkan return real
CPU-side success/failure on allocation/encoder/version selection;
other backends forward to their existing `updateBuffer` semantics.
This is **not** a GPU fence or proof the driver executed a draw.
In `uploadModelToGeometryPool` commit page offsets, register new
pages and assign Model GPU handles only after both writes succeed.
On failure preserve the existing committed region and leave the
failed range available for the next upload. Do not expose half-
initialized pooled geometry; keep dedicated fallback. Current
build/driver/failure-injection tests are **NOT RUN** and PR #34
remains Draft. See Stage 41 handoff.


## Stage 42 — model upload pair publication

Native `uploadModel` must never publish a half-valid standalone
VB/IB pair or discard CPU source vectors after one buffer allocation
failed. `engine/render/Model.cpp` now allocates temporary handles,
releases the first on second-allocation failure, and returns false
before image/morph preparation. It also retires orphaned single
handles from a prior failed dedicated upload and captures the GPU
requirement before CPU vertex/index clearing. Shared GeometryPool
uploads still take precedence and remain subject to Stage 41's
checked-write contract. No CMake/CTest/Metal/Vulkan validation has
run for this code; preserve PR #34 Draft until final validation.


## Stage 43 — Vulkan immutable buffers fail closed

When changing `VulkanDevice::createBufferObject`, propagate
`vkBindBufferMemory` failure: destroy the unbound Vulkan buffer,
release the allocation and return false. For `createBuffer` with
immutable CPU initial data, `stagingWrite` failure must destroy
the new VkBuffer and allocation and return an **invalid** RHI handle.
Never report a valid Model VB/IB when its immutable contents were
not uploaded. Preserve Stage 42 atomic Model publish/CPU retry,
Vulkan in-flight buffer versioning and the original fallback.
No new tests were run; PR #34 remains Draft with experimental
options OFF until native build/failure-injection/visual parity.


## Stage 44 — GPU-completed serial for geometry retirement

Use `gfx::Device::completedFrameSerial()` instead of
`frameSerial()` alone when considering reuse of GPU geometry
storage. Metal/Vulkan return confirmed completed queue serials;
other backends return 0 (no reuse permission). Match a freed
span's nonzero `frameSerial()` against the completed value
on the **same graphics Device**, and never race source Model
teardown with render submission. No GPU stalls or new draw
format. Stage 44 source is committed but native tests NOT RUN.
See the incremental static cache handoff; user still requires
all features before test phase.


## Stage 45 — quarantined reusable geometry suballocations

Model `gpuPoolVertexBytes`/`gpuPoolIndexBytes` retain byte
extents after CPU data discard. `AIVERSE_GEOMETRY_POOL_RECYCLE=1`
(default OFF) allows the common pool to remember no more than 256
retired vertex+index pairs per page. Only reuse a whole pair when
the **same** Device reports a nonzero completed GPU frame serial
at least its retirement serial. Otherwise the free area remains
quarantined; never overwrite in-flight commands. A checked
upload failure does not consume an available retired pair.
`GeometryPoolStats` read-only counters show retired/reusable
pairs and recycled uploads, not GPU residency gains. No
genre logic, no immediate in-place compaction, no default enable.
Native/Metal/Vulkan/concurrency tests NOT RUN; PR #34 Draft.


## Stage 46 — Vulkan pooled rigid nonzero base-vertex (opt-in)

`AIVERSE_VULKAN_POOLED_MDI=1` (default OFF) makes eligible
static, rigid, pooled, no-ShaderGraph ModelDraw calls use
`Device::drawIndexedCanonical()` on Vulkan, so direct and
indexed-indirect paths both apply `baseVertex` as GPU
vertexOffset. The legacy Vulkan `drawIndexed` continues to
apply VB binding offset for all other callers (especially
morph/skinning). The renderer still rejects nonzero pooled
baseVertex from MDI when flag OFF, and keeps normal fallback
for unsupported/deformed/variant paths. Shader ABI, ECS,
material/LOD/culling, Metal GPU ICB and command thresholds
unchanged. No native builds, pixel comparisons or GPU
validation performed; Draft PR #34 remains open. See Stage 46
handoff for required tests before default enablement.


## Stage 47 — preserve recycled reservation lengths

`Model::gpuPoolVertexBytes` and `gpuPoolIndexBytes` refer to
the reserved *allocation*, which may be larger than the
current Model's uploaded vertex/index payload after reuse.
Save the original retired pair extents when a smaller model
reuses it and return the same whole pair on release. Do not
silently shrink the reservation across repeated streaming.
Keep Stage 44 completed-GPU-serial gate, Stage 41 checked writes,
256-pair cap and the OFF-by-default recycle flag intact.
Native/Metal/Vulkan tests still pending.


## Stage 48 — read-only recycler stats

Studio Scene → Batching displays retired, GPU-fence-ready
and actually recycled GPU geometry allocation pairs via
`geometryPoolStats()`. Keep the distinction: retired
may still be in-flight; ready != submitted; high-water
occupied is not live VRAM residency. Renderer and RHI
behavior unchanged, `AIVERSE_GEOMETRY_POOL_RECYCLE` OFF by
default. No native compilation/GPU tests run yet. See Stage 48
handoff and maintain per-stage documentation.


## Stage 49 — model-batching mock regression tests

`tests/render/model_batching_policy.cpp` extends `MockDevice`
with completed GPU serial simulation, checked shared-write and
dedicated-buffer allocation fault injection. The new test covers
quarantined GPU geometry spans, large->small->large capacity
preservation, failed second pooled upload with dedicated fallback
and failed second standalone IB allocation with successful retry.
No additional CMake target. Recycling is enabled by test-process
environment only, not engine default. Native compilation and CTest
have NOT RUN; never describe authored assertions as passing.
PR #34 remains Draft pending actual CI/Metal/Vulkan parity.


## Stage 50 — Vulkan fence result checked before recycling

Only a successful Vulkan queue fence wait proves
`completed_`. `GfxVulkan.cpp::prepareSlot()` now
checks `VK_SUCCESS` before updating `completed_`;
non-success marks RHI unhealthy, disallowing completed-serial
recycling. Keep this contract through any device-loss recovery.
No new GPU stalls/changes to batching output; native tests NOT RUN.


## Release-mode unit checks in CI — Stage 51

Do **not** rely on `assert(expr)` as the only test assertion in
Release builds compiled with `-DNDEBUG`; all checks become no-ops,
and under `-Werror` test variables can trigger unused warnings.
`tests/TestCheck.h` now exposes `AIVERSE_TEST_CHECK(expr)`
to evaluate/abort with a source location in both Debug and Release.
Four ECS regression files use it after a genuine Linux ARM64 CI
compile failure (run 44540545468 / job 135617454000). These changes
address the compilation blockers observed in that job, but the
new head's CMake/CTest result is NOT YET VERIFIED. Keep preserving
genre independence and the per-stage handoff protocol.


## Stage 52 — Mac batching policy fixture correction

Fix user-reported `model_batching_policy` failure (Mac test
at original line 766): test fixtures must call `Model::finalize()`
before expecting `classifyModelStaticEligibility` to accept them.
An asset containing animation clips may still have a static,
bind-pose instance; only actual instance animation (`pose`,
`nodeWorld`, `baked`) or active displacement excludes it.
Test both sides; do not weaken production cache eligibility.

The 4 ECS CTest entries were **NOT RUN**, because requested
executables were not built and `make` reported a missing target.
Reconfigure `build-metal` with `-DAIVERSE_BUILD_TESTS=ON`
and explicitly build their targets, or use a fresh independent
build directory. All CTest/Mac/GPU outcomes after this stage
remain unverified. PR #34 stays Draft, no experimental defaults.


## Stage 53 — nodeMaterial packet eligibility regression

`staticRenderPacketDrawCandidate` may accept a per-node material
override: `Renderer3D` validates the current override contents
against the saved packet's exact `nodeMaterialSnapshot` before
replay. Keep eligibility and replay checks distinct. The Mac owner
reported 4/5 focused tests passing, one `model_batching_policy`
failure at its stale negative assert line 875. The test assertion
has been updated, engine behavior not changed. No subsequent
Mac/CI result is available yet; leave PR #34 Draft and continue
handoff protocol.


## Stage 54 — documented user Mac CTest success

Owner's latest macOS `aiverse_model_batching_policy_tests`
build **succeeded**; `ctest -R '^model_batching_policy$'`
**PASSED (0.33 s)**. Prior local Mac CTest confirmed four
ECS tests PASS, so 5 distinct focused tests have now passed
across two CTest invocations. This is not proof that the
complete 5-case CTest was rerun together, full CI finished
or Metal/Vulkan parity is good. Follow Stage 54 handoff;
keep the PR Draft and experimental recycle/Vulkan pooled
MDI flags OFF until real GPU and broader test evidence.


## Stage 55 — Metal raster fixture success and remaining gates

User-supplied real Mac Studio results: CPU MDI fixture
`AIVERSE_PALETTE_TEST_CASE=indirect` achieved 2 CPU MDI
submissions / 64 indexed commands with 32→1 shadow,
32→1 opaque calls and `changed_channels=0`. The
persistent static packet fixture achieved 1 warm packet
hit, 0 rebuild/declines, retained 5936 bytes, identical
pixels and unchanged model submission counts. Both
reported 30 CPU checks passed and no renderer/pipeline
failures. Do not confuse raster/submission reductions
with proven FPS, CPU or GPU speedup. These are actual
Mac fixture outputs but GitHub CI remains queued.
Next hardware gate is GPU-authored Metal ICB using
`AIVERSE_EXPECT_GPU_ICB=1`, then Vulkan parity,
recycle safety and performance A/B/A. Experimental flags
remain OFF, Draft PR #34; document each subsequent stage.


## Stage 56 — distinguish CPU MDI and GPU-authored ICB counters

On Mac Studio the owner supplied GPU ICB fixture output
(30 CPU checks passed, 32/32 model submissions reduced
to 1/1, `changed_channels=0`, zero pipeline errors).
When run with the prescribed `AIVERSE_EXPECT_GPU_ICB=1`,
the fixture internally rejects zero GPU-encoded submissions
and commands: it is valid evidence of GPU ICB output parity
for that scenario. **Do not misread** the historical
`GPU indirect supported=1 submissions=1 commands=32`
line: those are `renderer.modelStats()` CPU MDI counters.
Stage 56 instrumentation in `material_batches.cpp` now
prints separate `GPU-encoded ICB` and `CPU MDI` counters
with proper newlines. This change does not affect GPU
rendering or test gates; new diagnostic binary remains
unbuilt and exact GPU ICB counts are not yet recorded.
Leave PR #34 Draft pending Vulkan, recycling, full CI
and controlled A/B/A timing measurements.


