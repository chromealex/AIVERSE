# AGENTS.md — rules for coding agents working on AIVERSE

Read this file completely before changing anything. It is short on purpose. `README.md` is the
short user-facing introduction (English, with translations `README.<lang>.md`, see §11),
`docs/overview.md` the detailed overview; this file is the engineering contract.

## AIVERSE SpectraFX (universal VFX, in progress)

**Required handoff:** [docs/agent-handoffs/universal-niagara-vfx-2026-11-18.md](docs/agent-handoffs/universal-niagara-vfx-2026-11-18.md).  
**Official AI skill:** [skills/visuals/aiverse-spectrafx/SKILL.md](skills/visuals/aiverse-spectrafx/SKILL.md).  
**System documentation:** [docs/spectrafx.md](docs/spectrafx.md).  
The previous `aiverse-niagara-vfx` skill is a legacy discovery alias.

The public SpectraFX headers are `engine/fx/SpectraFX.h` and `libraries/include/aiverselib/genre/SpectraFX.h`; native targets link `AIVERSE::spectrafx`. New cross-genre opt-in VFX layers are `engine/fx/VfxGraph.h` (graph -> existing CPU effect compiler),
`engine/fx/VfxGraphAssets.h` (versioned assets, parameter variants and hot reload) and
`libraries/include/aiverselib/genre/EcsVfxPresentation.h` (read-only ECS renderer bridge,
tick-event bindings). Never put renderer handles, float particles or graphics timing into
authoritative gameplay ECS, never duplicate an FPS-only/RTS-only VFX engine, and do not remove
legacy `RtsFx` before production parity tests. The basic graph-v1 ImGui canvas is in
`apps/editor/windows/SpectraGraphWindow.cpp`, graph CRUD in
`apps/editor/core/SpectraGraphOps.cpp` and the editor-only in-memory preview
in `apps/editor/EditorFxOps.cpp`. **GPU VFX, full Niagara graph dataflow and automatic
native-client integration are not complete.** Current test targets are committed but not
verified as passing. Every new VFX stage MUST append a separate handoff commit after code and
tests and record exact SHA, verification status and next work.

## Mandatory per-stage handoff protocol (2026-11-18, user instruction)

**Every completed stage MUST leave a durable handoff for the next agent, without exception.**
This applies to code, tests, migrations, performance, editor fixes, documentation-only
architecture stages and interrupted/failed attempts. The working branch for PR #7
is `feature/universal-entity-inspector-fps`; do not create a parallel branch just
to write the handoff.

1. Before implementation, read the last applicable handoff, `AGENTS.md`, and
   the current branch HEAD. Preserve all changes from other agents.
2. Finish one bounded stage and commit its implementation. Do not start the next
   stage until its handoff has been committed **to the same branch**.
3. Append a dated entry to the active feature handoff. Include exact code commit
   SHA(s), branch, affected paths, what changed and what did not, public API/save
   format compatibility, test commands **actually executed** with outcomes,
   CI URLs/status, known risks, blockers and the very next actionable step.
   If tests could not be run, write **NOT RUN** and why. Never describe a
   queued/cancelled workflow as passing.
4. Commit the handoff entry separately. If multiple agents edit the same handoff,
   reload the newest HEAD and append to the combined document; never overwrite
   their entries. Do not force-push or silently resolve semantic conflicts.
5. If an implementation fails or is partially complete, still append a
   handoff describing the failed command/error and unfinished changes so that
   the next agent does not repeat the investigation.
6. For native ECS persistence also maintain
   `docs/agent-handoffs/native-ecs-persistence.md`, plus a brief checkpoint in
   `docs/editor-unification-handoff.md`.

The handoff must make it possible for another agent to resume without
re-reading the entire chat or trusting unverifiable status claims.

**Active Metal batching/performance work (2026-11-17):** Before changing the renderer, Scene viewport, GPU pooling, RTS scene building, or macOS frame loop, read [docs/metal-batching-agent-handoff.md](docs/metal-batching-agent-handoff.md), especially its latest continuation section. It records actual Apple Silicon 2×2/4×4 road-merge tests, CPU profile gains, draw-call visualisation limitations, required flags, the Metal ICB crash gate, and the pending incremental scene-cache work. Distinguish direct/indirect GPU submissions from scene objects and per-material primitives. Keep unsafe experimental ICB disabled by default.

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
`RtsView` opts eligible stationary scenery in at the host boundary, but no
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
[docs/agent-handoffs/incremental-static-model-cache-2026-11-19.md](docs/agent-handoffs/incremental-static-model-cache-2026-11-19.md),
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

## Genre-independent ECS mechanics contract (2026-11-18)

**Full feature-by-feature migration inventory:** `docs/ecs-all-features-migration-plan.md`
(baseline 120 catalog packages, RTS tick stage map, FPS authority backlog, migration
waves and acceptance checks). Keep the inventory and handoff updated as packages
are added. A generic public library is NOT equivalent to converting existing
gameplay; track library publication and actual RTS/FPS adoption separately.

New reusable opt-in ECS packages:
- `AIVERSE::lib_ecs_timed_mechanics` — reload/fire gate, health/energy regen,
  despawn by tick; `skills/gameplay/aiverse-ecs-timed-mechanics/SKILL.md`.
- `AIVERSE::lib_ecs_locomotion` — component-based input/state/output around
  existing shared fixed-point `stepMotion`; `skills/gameplay/aiverse-ecs-locomotion/SKILL.md`.
- `AIVERSE::lib_ecs_inventory` — ordered, transactional inventory add/remove/
  transfer using the shared inventory policies;
  `skills/gameplay/aiverse-ecs-inventory/SKILL.md`.
All have cross-genre CTest/standalone experiment runner entries but **native
results remain unverified** until the suite executes. Keep the legacy FPS/RTS
systems in place until explicit one-writer authority, save/network and replay
parity tests pass. Continue annotating every code stage in
`docs/editor-unification-handoff.md`.

AIVERSE is **a composable game-mechanics engine**, not a collection of
hard-coded RTS/FPS implementations. Every new gameplay feature, library or
mechanic MUST follow the **Components + Systems** model:

1. **ECS component contracts.** Define minimal, reusable data-only components
   describing state, inputs, configuration and outputs. Place shared components
   in engine/libraries rather than a genre module. Do not own authoritative
   gameplay state in duplicated per-genre arrays or custom singleton worlds.
   Components should have stable IDs and deterministic save/rollback semantics.
2. **Independent systems.** Implement behavior as small, configurable systems
   operating on declared ECS component read/write sets and resources, with
   explicit update order, event/command outputs and structural barriers.
   A mechanic can be enabled/disabled or composed with other mechanics by
   registering systems/components; no genre-specific engine forks.
3. **Reuse one shared mechanic across genres.** RTS bullet, FPS projectile,
   missile, rocket, grenade and spell should use the **same** trajectory,
   collision query, impact/penetration/splash, lifetime and damage primitives.
   Genre adapters provide components, target selection, input and tuning, not
   duplicate projectile solvers. Extend this rule to locomotion, vehicles,
   health/damage, inventories, economy, navigation, physics and AI.
4. **Configuration instead of inheritance by genre.** Prefer capability-based
   feature flags, typed rules/assets and composition to `isFPS`/`isRTS`
   branches. Different projects can combine any mechanics regardless of genre.
5. **Native C++ and agent reuse.** Libraries must be callable from the native
   game/project module via public headers, without modifying engine core.
   Deliver corresponding AI skill documentation, stable interfaces and usage
   examples; reference them here and in the relevant handoff to prevent agents
   regenerating equivalent mechanics.
6. **Determinism first.** Fixed-point sim math, stable iteration/command merge,
   explicit RNG ownership, versioned persistence, per-tick replay/rollback
   and multi-worker checksum tests. Keep presentation/float effects out of
   authoritative systems. Confirm parity between old and new integrations
   before removing a legacy path.
7. **Migration without parallel source-of-truth copies.** Transitional adapters
   may support old FPS/RTS saves/netcode but must state the owner of canonical
   data, the barrier where mirrors are updated and when compatibility buffers
   can be deleted. Move one system at a time; preserve gameplay ordering.
8. **Required artifacts for every mechanic:** component schemas, system
   implementation, registration/configuration sample (RTS+FPS or two distinct
   host games), tests (including native linkage and save/rollback), matching
   skill, and update to living handoff. Do not mark production-ready without
   compiling and running applicable checks.

**Required shared shooting package:** use
`AIVERSE::lib_ecs_weapon_launch` (public
`<aiverselib/genre/EcsWeaponLaunchSystems.h>`), composed with the existing
`ecs-projectile-systems` and `ecs-impact-damage` packages, for new
weapon/shot/impact gameplay. Do not implement separate projectile
trajectory/collision/damage math in `modules/rts` and `modules/fps`.
The native shooter/artillery cross-genre fixtures are
`ecs_cross_genre_weapon_pipeline_test` and
`ecs_cross_genre_projectile_test`. Existing RTS/FPS gameplay has NOT
yet been fully redirected to this package: preserve legacy deterministic
save/replay until actual parity tests pass.

The ongoing universal FPS/RTS migration must gradually consume these generic
mechanic libraries, not create new genre-specific gameplay engines.

**Real integer damage migration checkpoint (2026-11-18):** `fps::Module::damage`
and `addDamageResolutionSystem` both call
`lib::genre::applyIntegerHitPointDamage` after host mitigation.
The generic World `DamageQueue` exports v1
`saveDamageQueue/loadDamageQueue/hashDamageQueue`, which native hosts must
wire into their own GameModule byte save/checksum, not just COW snapshots.
Use `ecs_integer_health_commit_test` plus actual FPS ECS ON/OFF,
save v9 and rollback parity. This does **not** make `HitPointsComponent`
canonical for live FPS: the canonical FPS owner remains `ActorRuntime`
until a versioned migration. Do not create parallel mutable health copies.
See `skills/gameplay/aiverse-ecs-impact-damage/SKILL.md`.


**Optional queued ECS impact barrier:** for games that deliberately separate
motion from damage resolution, use `AIVERSE::lib_ecs_impact_queue`
(`<aiverselib/genre/EcsImpactQueueSystems.h>`,
`skills/gameplay/aiverse-ecs-impact-queue/SKILL.md`).
`OrderedImpactQueue` and its monotonically increasing sequence are World
resources that MUST be saved/hashed/rollbacked; callbacks retain independent
armor/faction/shield rules, and queued impacts run in canonical submission
order. The queue swaps pending requests at the barrier: callback-generated
hits run on the next invocation. Existing RTS immediate projectile damage
and same-tick shield/script side effects MUST NOT be moved into a single
end-of-tick queue without a golden parity test. The native cross-genre
fixture is `ecs_cross_genre_impact_queue_test`; compilation and real
RTS/FPS integration remain pending.

**Implemented hitscan capability split (2026-11-18):** Use
`AIVERSE::lib_ecs_hitscan` (`EcsHitscanSystems.h`) to select the nearest
valid fixed-point ray hit over current or rewound poses. For games that can
stage hits after a source ECS traversal, compose
`AIVERSE::lib_ecs_hitscan_resolution`
(`EcsHitscanResolutionSystems.h`, `HitscanIntentComponent`,
`addHitscanResolutionSystem`) with
`AIVERSE::lib_ecs_impact_damage` and its `DamageQueue`/resolution barrier.
Skills: `skills/gameplay/aiverse-ecs-hitscan/SKILL.md`,
`skills/gameplay/aiverse-ecs-hitscan-resolution/SKILL.md`.
Test: `ecs_hitscan_resolution_test` exercises two independent GameModule hosts,
invalid/blocked fire requests, stable multi-shooter order, full ECS health
reduction and deep/COW snapshot replay. As of this entry native execution
has **not** been verified.

**Do not silently change live FPS timing:** `modules/fps/FpsSystems.cpp`
still resolves authoritative hits synchronously within the legacy tick,
including rewind, armor, head/limb multipliers and kill/loot hooks. It reuses
shared hitscan and ordered-impact policies, but the independent hitscan
resolution system stages hit events until after traversal. Switching the live
FPS to it requires same-tick damage ordering golden tests and network/save
checksum parity. The kinematic no-physics RTS path and physical-ballistic
path remain independently composable; no `isFPS`/`isRTS` motion flag.

**Actual shared projectile collision adoption (2026-11-18):** the live FPS
`FpsProjectiles.cpp` programmed-projectile stage now calls
`selectSweptProjectileContact` in
`AIVERSE::lib_ecs_projectile_systems` (`EcsSweepQuerySystems.h`).
Selects nearest dynamic target with fixed-point swept bounds, endpoint
fraction=1 and stable semantic-ID ties. It keeps the older wall, penetration,
damage and projectile-program side effects in the original order. Its
existing `tests/fps/projectiles.inc::ecsProjectileProgramStageParity` tests
legacy/ECS actor and projectile state plus save v9 and deep/COW snapshots;
`ecs_projectile_composition_test` additionally covers swept contact
selection in independent genre hosts. Neither has been executed in this
workspace.

`FpsSystems.cpp::traceShot` now synchronously calls the **same**
`resolveHitscanIntent` pure policy as the standalone
`addHitscanResolutionSystem`, and uses the generic ray selection policy for
vehicles too. The live FPS damage still commits immediately and uses
`applyIntegerHitPointDamage` after gear/armor and knockdown rules.
For any future conversion to queued impacts or scheduled FPS fire, first run
`tests/fps/tests.cpp::ecsSimultaneousHitscanRollbackParity` and network
rollback/parity tests. The standalone resolution system uses deferred callback
order and must not silently replace synchronous FPS multi-pellet execution.
Select physical sweep, non-physical pursuit or instant hitscan per *weapon
capability*, never per genre.

**FPS projectile state migration, current branch:** in
`fps.ecsAuthority=true`, `fps.compat.projectilePrograms` now runs
`Module::stepProgrammedProjectile` directly against authoritative
`ProjectileRuntime` components, sorting by recorded `order`/ID and applying
immediate hits before actor updates. The default FPS mode calls that **same**
solver from the legacy source vector. New branches/expiry are committed to
the ECS World at a structural barrier and projected into
`State::projectiles` for remaining legacy game mode/save/network readers.
**Live accepted weapon shots, Frag/Smoke/Flash actions, and bomb-round reset
now use direct `Module::spawnProjectile` / `clearProjectiles` ECS
Entity creation/deletion when `ecsAuthority` is enabled.**
The State array remains a compatibility workspace for other FPS systems;
do not reintroduce legacy-only spawns or a second solver. See
`tests/fps/projectile_spawn.inc` for deep/COW, v9 and reset parity.
**Do not switch a second projectile solver or reintroduce a writable State
source into this phase.** Mixed nonprogrammed objects, Entity-generation
recycling, COW, v9 saves, rewind and synchronous damage order must retain
parity; see `ecsProjectileProgramStageParity` and
`ecsProjectileBranchAuthorityParity` in `tests/fps/projectiles.inc`.
Native test execution is still pending.

**Live FPS ordinary projectile ECS stage (2026-11-18):**
When `fps.ecsAuthority=true`, both the programmed and ordinary
projectile flight stages now advance **canonical ProjectileRuntime ECS
components**. Ordering is `fps.ecsHydrate -> fps.compat.projectilePrograms
-> fps.compat.ordinaryProjectiles -> fps -> fps.ecsMirror`.
The ordinary-stage `stepOrdinaryProjectile` solver is reused by the
legacy mode and ECS mode, preserves exact instantaneous hit/fuse/smoke
order and uses shared `stepBouncingProjectile` from
`AIVERSE::lib_ecs_projectile_systems` for physical grenade motion.
RTS abstract missiles use separate kinematic motion without physics.

Do not treat the compatibility `State::projectiles` array as canonical
between ECS flight barriers. Other FPS round/actor systems can still
write that array in the main tick; the mirror reconciles it until those
systems migrate. Preserve FPS v9 save/net format and simultaneous-damage
ordering. Run `ecsOrdinaryProjectileAuthorityParity` (real FPS bullets,
grenades, timed smoke, ECS-only edits, deep/COW snapshots, cross-v9
reload), `ecsProjectileProgramStageParity`, `ecs_projectile_composition_test`,
`fps` and `fps_transport` before merging or enabling by default.
All these native results are currently unverified.

**Critical FPS projectile single-writer rule (2026-11-18):**
When `fps.ecsAuthority=true`, the **entire live FPS projectile lifecycle**
(accepted weapon and grenade spawn, programmed/ordinary flight, impacts,
smoke expiry and bomb-round reset) now writes canonical
`ProjectileRuntime` ECS components. `fps.ecsMirror` must use
`syncDynamicEntities(sim,true)` to derive `State::projectiles` as a
one-way legacy/save/network representation, **never** copying stale
State values back onto Runtime. Preserve the original
`syncDynamicEntities(Simulation&)` overload for legacy v9 load/setup,
which is intentionally State→Runtime. New projectile features must
write ECS components/commands, not the State vector. Native proof is
still required; `tests/fps/projectile_spawn.inc` includes a
Runtime=17 / stale-State=777 mirror regression plus deep/COW save and
round-reset fixtures.

## FPS ECS authority migration (PR #7, in progress)

**Current working branch:** `feature/universal-entity-inspector-fps`. PR #8
has been merged as an ancestor into PR #7; do not restore outdated FPS
`FpsModule.cpp` or Inspector sources from the standalone PR #8 branch.

**Mandatory living handoff:** `docs/editor-unification-handoff.md`,
latest FPS stages A–S. After EVERY implementation commit, append the exact
SHA, changed files, behavior, verification status and next action to the
handoff in a separate documentation commit before taking another action.

`fps.ecsAuthority` is an experimental default-OFF storage migration gate.
When ON, full `ActorRuntime`, `ProjectileRuntime`, `LootRuntime` and
`VehicleRuntime` components are canonical between ticks and define
checksum/save v9 entity payloads. `fps.ecsHydrate` creates the ordered
legacy tick workspace; old algorithms not yet migrated still read/write it.
`fps.ecsMirror` commits the tick results to ECS and refreshes read-only
Editor projections. Commands, actor motor and centralized damage already
have direct ECS code paths. **Do NOT call the migration complete.**

Next migrate direct gameplay systems one by one, verify same-tick side-effect
ordering and byte-identical checksums to the default mode, then remove the
temporary State arrays once all uses are gone. Legacy save v9 and network
format must remain compatible until an explicitly versioned migration.
Run `aiverse_fps_tests`, `fps_transport`, checksum replay, sanitizer and
universal Editor tests on the integrated branch before main merge.

## Mandatory universal ECS mechanic contract (2026-11-18)

**Games are compositions of components and systems, NOT fixed RTS/FPS feature silos.**
This section supersedes older genre-specific implementation instructions elsewhere in
this file or in skills. Every new or refactored gameplay mechanic MUST have:

1. **ECS components** holding all mutable authoritative state: e.g. `ProjectileFlight`,
   `DamagePayload`, `WeaponState`, `Inventory`, `VehicleMotion`, `Team`.
   Use generation-safe Entity links, Fixed/integer values and explicit versioned
   configuration. Do not hide gameplay state in global variables, module objects,
   ephemeral mirrors or untracked vectors.
2. **Reusable ECS systems** implementing one feature or stage, with explicit
   `reads`, `writes`, resource/structural dependencies and a deterministic tick
   stage. A system consumes the components it needs; no requirement that the
   hosting game is RTS, FPS, racing, RPG or another predefined genre.
3. **Pure/shared algorithms or policies** when possible, located in
   `libraries/include/aiverselib/` (or shared `engine/` when engine-owned).
   Genre-specific modules only map input/config/presentation and assemble
   shared systems. Never duplicate the same core algorithm in RTS and FPS.
4. **Composable execution**: systems opt in per project through data/config,
   run with deterministic ordering and explicit barriers, and interoperate
   through components, events and deferred command buffers. A single Entity
   may use any combination of shooting, inventory, driving, crafting, AI,
   abilities and other mechanics. Missing components mean a system skips
   that Entity; no hard-coded genre switch or mutually exclusive feature list.
5. **Gameplay determinism and rollback**: all state that affects outcome is
   hashed/serialized/snapshotted, input is a validated Command, RNG is injected
   from Simulation and consumed in canonical order, structural changes are
   deferred, and cross-worker writes are protected. No presentation floats
   in authoritative logic.
6. **Native-project usability**: a standalone game's C++ source must be able
   to include, configure and register the components and systems through public
   AIVERSE targets, without editing the engine or depending on `RtsModule&`
   / `fps::Module`. Optional adapters for legacy modules are allowed, but the
   reusable mechanic itself must have no such dependency.
7. **Cross-genre proof**: before calling a mechanic reusable, compile and
   exercise it in at least TWO independent module hosts (e.g. shooter and
   strategy), plus a combined-mechanics scenario. Validate checksum parity,
   save/load, rollback, component lifecycle and permissions; add CTest/CI.
   No green tests => mark unverified, do not claim production-ready.
8. **Documentation on every feature change**: update the feature skill,
   `docs/ecs-mechanic-composition.md`, the relevant API guide and the current
   branch's agent handoff after EACH implementation commit. The handoff must
   distinguish implemented code from native test results.

**Projectile example:** RTS turret, FPS rifle, missile launcher and RPG spell
must all use shared flight/trajectory/collision/impact policies driven by
`ProjectileFlight` / `ProjectileState` / `Target` / `DamagePayload`
components. Separate launch, motion/homing, collision, damage, effects and
despawn systems; adapt genre-specific rules at the edges. Reuse existing
`engine/sim/ProjectileData.h` and `aiverselib/genre/Weapons.h` as a base,
not duplicate trajectory physics inside each genre module. Parameterize bullet,
shell and rocket behavior rather than branching on the genre name.

**Migration rule:** legacy `RtsModule` or `fps::Module` methods are not
considered universal simply because both run on an ECS World. Extract common
mechanic state + behavior into reusable ECS units, keep old public/save/network
contracts through thin adapters until parity tests pass, then remove duplicated
implementations. See `docs/ecs-mechanic-composition.md` and
`skills/workflow/aiverse-new-mechanic/SKILL.md`.

## ECS 2.0 experimental work (PR #8)

**Parallel migration coordination (2026-11-18):** another agent is actively
moving RTS and FPS to a universal ECS architecture. Do not concurrently
rewrite RTS/FPS gameplay systems or change legacy world-space `Transform`
semantics from the ECS architecture PR. Keep `Parent`, `LocalTransform`,
`WorldTransform` and `SceneHierarchy` generic under `engine/sim`. Share
the integration contract in `docs/ecs-agent-handoff.md`, and merge/cherry-pick
only verified core changes into the genre migration. Both agents must avoid
overlapping edits to `modules/rts` and `modules/fps`.

For nested gameplay entities and native project object hierarchies, read
`docs/ecs-scene-hierarchy.md` and use `Parent` / `LocalTransform` /
`WorldTransform` with `SceneHierarchy`. Do not reinterpret RTS
`Transform` as local space, or feed float presentation transforms into
deterministic simulation. Keep hierarchy topology generation-safe; rebuild
the derived graph on save/load and update only dirty branches.

For any ECS storage, scheduler, rollback or parallelism change, **first read**
`docs/ecs-agent-handoff.md`. This is the continuously updated handoff for
`feature/ecs-architecture-benchmarks` and PR #8. It contains prior benchmark
results, verified tests, exact commits, limitations and remaining work.
Update it after **each** ECS development step, preserving a clear distinction
between code pushed and tests actually run. Production ECS remains deterministic
Sparse Set; new scheduler/guarded interfaces are opt-in experiments. Do not
enable arbitrary existing RTS/FPS gameplay systems in parallel or merge PR #8
without complete test and lockstep/replay validation.

## 1. What this is

AIVERSE is a from-scratch C++20 game engine with RTS and FPS modules. The RTS module covers
real-time strategy games (Warcraft, StarCraft, Red Alert, Dune II,
Total Annihilation, Age of Empires, Company of Heroes, They Are Billions, Northgard, Mushroom
Wars, lane battlers, ...). Targets: Windows, macOS, iOS, Android, HTML5. No third-party engine.

**The engine is a fixed library of mechanics; games are data.** A game is a project folder
anywhere on disk (`<project>/`, created in the editor from a template in `templates/<id>/`) with `game.json`, `rules.json`, `maps/*.json`, `ui/`, `models/`, ... Agents that
build *games* (through the editor or the MCP server) never touch engine code. Agents that work on
the *engine* add mechanics generically — never a special case for one game.

## Engine naming (all agents)

- Product name: **AIVERSE**. Use this name in UI, documentation, logs and agent responses.
- Shared C++ namespace: `aiverse`; CMake project/exports: `AIVERSE` / `AIVERSE::`.
- Common targets, macros and environment variables use `aiverse_` / `AIVERSE_`.
- Apps: `AiverseEditor`, `AiverseGame`; tools: `aiversebuild`, `aiversemcp`.
- RTS is a genre, not the engine name: keep `modules/rts`, `RtsModule`, `RtsData`,
  `Rts*` gameplay types and JSON `module: "rts"`. FPS remains `modules/fps`.
- Keep existing project/save/pack formats, published URLs and compatibility fields stable.
  Binary magic values and the release-index `rtsmcp` key are intentional legacy names. Project files are
  `<Title>.aiverse`; the legacy `<Title>.rtsproj` is still read, rewritten in place and never renamed.
- See `docs/aiverse-migration.md` for native project and build configuration migration.

## 2. Layout

| Path | What | Notes |
|---|---|---|
| `engine/core` | Fixed (Q47.16), Random (PCG32), Hash (FNV-1a), ECS (sparse sets) | sim-safe primitives |
| `engine/data` | JSON reader (comments allowed, numbers parsed as Fixed) | no floats in parsing |
| `engine/sim` | Simulation (lockstep tick, commands, events, checksums), Replay | deterministic |
| `engine/nav` | genre-independent navigation mesh: walkability source -> rectangles + portals, A* + funnel, tile refresh (docs/navmesh.md) | deterministic; no genre types |
| `engine/gfx` | RHI `Gfx.h`; backends `gl/` (GLES3, WebGL2, GL 3.3), `metal/` and `vulkan/` (Windows, Linux, Android on request; the GLSL compiled to SPIR-V, docs/vulkan.md) | never call GL/Metal/Vulkan outside |
| `engine/fx` | particles: effect data (`FxEffect`), `ParticleSystem` (CPU sim, RHI draw, quality tiers), own GLSL + MSL shaders; textures via `render::TextureCache` (docs/particles.md) | float OK, presentation only |
| `engine/render` | Renderer3D (forward, shadows, PCF tiers), shaders (GLSL + MSL in `Shaders.h`, `ModelShaders.h`), models (glTF/OBJ, `Model.h`), animation (`Animation.h`), ImGui layer, camera math | float OK; format: docs/models.md |
| `engine/platform` | Cocoa, UIKit, Android NativeActivity, Emscripten, Win32, X11 | `Platform.h` is the only API |
| `engine/services`, `plugins/` | store / ads / analytics / privacy APIs over native plugins (Java, Obj-C, JS, C++); ready plugins: App Store, Google Play, RuStore, Yandex Games, Steam, Yandex Ads, AdMob, AppLovin MAX, ironSource LevelPlay, GameAnalytics, Facebook, AppMetrica, ATT; a build carries only the plugins the game selects (CI: check-plugin-isolation.py) | docs/plugins.md |
| `engine/assets` | fonts, default assets copied into every build | data only (no code: it is bundled) |
| `engine/atlas` | texture atlas manifest + `.rtex` pages, `TextureLibrary` (path -> texture + uv + trim) | packer in `tools/atlas` (host only) |
| `engine/pack` | resource packs `.rpak`: reader (`Pack`), writer (`PackWriter`, host tools only) | zstd, reproducible output |
| `modules/rts` | generic RTS gameplay: data model (`RtsData`), map/pathfinding (`RtsMap`), module (`RtsModule*.cpp`, `RtsEconomy`, `RtsUnits`, `RtsCombat`, `RtsWorld`, `RtsTriggers`), AI (`RtsAI`) | deterministic |
| `modules/rts/view` | presentation of the sim (shapes, terrain, fog, overlays) | float OK, read-only on sim |
| `apps/client` | the game executable | |
| `apps/editor` | editor: dockable windows (Scene, Game + device simulator, Hierarchy, Inspector, Tools, Project, Rules, Statistics, Agent, Console); `core/` = Workspace/MapDoc shared with the MCP server | new windows: docs/editor-windows.md |
| `tools/mcp` | `aiversemcp`: MCP server (stdio) for agents; forwards to the running editor | game data only, never engine code |
| `apps/headless` | bot matches, replays, CI | |
| `apps/modelview` | model viewer and type gallery (`--game`) through the real view | models agent |
| `tools/aiversebuild` | packager: macOS .app, iOS Xcode project, Android Gradle project, web, Windows | ships content as `content.rpak` |
| `tools/assetpipe` | build / list / verify / extract / diff `.rpak`, import foreign archives (`--json` for MCP) | |
| `tools/unpackers` | unpacker plugins for foreign archive formats (zip, tar, dmg, Warcraft WAR, Westwood PAK, Z PAC/RSC) | protocol: `tools/unpackers/PROTOCOL.md`; host tools only |
| `tools/importers` | importers of old games into game folders (`dune2/import.py` → 2D game: tiles + sprites), Westwood codecs | Python stdlib; output is external data |
| `templates/*` | sample games | data only |
| `tests/tests.cpp` | unit + mechanics + determinism tests | must stay green |
| `third_party/` | vendored libraries with licenses | MIT/BSD/zlib/OFL/CC0/CC-BY only; glslang (BSD with Apache-2.0 files and the Bison exception) allowed by the owner for the Vulkan backend |

## 3. Non-negotiable rules

**Determinism (simulation = `engine/core`, `engine/sim`, `modules/rts` except `view/`):**
- No `float`/`double` anywhere in simulation state or logic. Use `Fixed`, integer math, `fxSqrt`.
- Randomness only via `sim.rng()`; never `rand()`, time, addresses, thread ids, `std::hash`, unordered containers iteration.
- Iterate in deterministic order (ECS order, sorted vectors, `std::map`). Tie-break by entity index.
- Never add/remove components or create entities inside `world.each<...>()` — collect, then apply after the loop. Destroy with `destroyLater`.
- Everything that influences outcomes must be hashed in `RtsModule::hashState`.
- Input reaches the sim only as `Command`s; invalid commands are ignored silently (never assert/crash).
- Presentation reads sim state and `SimEvent`s; it never writes into the sim.
- After touching the sim run the determinism tests and a headless bot match on every sample game.

**Rendering/platform:**
- Before shader or Shader Graph work, read [the agent guide](docs/shader-graph-agents.md)
  and follow its validation, preview and application workflow.
- All GPU work through `gfx::Device` (RHI). Every shader exists in GLSL *and* MSL in the same change.
  Vulkan compiles the same GLSL: uniforms only in blocks `U<slot>`, samplers `tex<slot>` (docs/vulkan.md).
- Must run on weak Android GPUs: every expensive feature has a quality tier (`ShadowQuality`, etc.).
- Content files are read via `platform::readContentFile` (works on Android assets, web preload, packs).
- No threads in code that runs on the web build unless guarded.

**Editor windows:** see `docs/editor-windows.md` (one class per file in `apps/editor/windows/`, `AIVERSE_EDITOR_WINDOW`).

**Data model:**
- New mechanics = new optional JSON blocks on types/rules/maps, parsed in `RtsData.cpp`, with
  readable errors (`type 'x': unknown resource 'y'`). Old game files must keep loading.
- When a fixed block can't express something, extend the trigger system (`RtsTriggers`) instead
  of special-casing a game.
- **Keep a game's rules.json small** (games, importers, the wizard, MCP agents; docs/game-format.md
  § Splitting rules.json). One file holding the whole game grows to hundreds of KB that nobody (and no
  agent context) can read:
  - split by topic with `"include"` (`rules/units.json`, `rules/abilities.json`, `rules/ai.json`, ...);
    a collection may span files (half the types in one, half in another), each key / id lives in one file;
  - shared values go into an `"abstract": true` base and entries `"extends"` it, instead of copying
    the same health / move / cost / effects block into every type or ability;
  - `levels` hold only what changes per level (they merge into the base entry); never a full copy;
  - importer provenance (source paths, client ids) goes into the game's sidecar files, not into rules;
  - don't write values equal to the engine defaults.
  Readers of rules go through `readComposedJson` / `composeJson` (`engine/data/JsonCompose.h`), never
  `Json::parseFile(rules.json)`; data hashes use `hashGameData`. The editor saves each entry back to
  the file it came from.

**Games stay out of the engine repo (owner's rule, all agents):**
- A concrete game project (a port, a commercial game, the owner's or a user's project) is never
  committed here: not as `templates/<game>`, not as data dumps in `tests/`, `tools/` or `docs/`.
  `templates/` holds only the engine's own sample games. A game lives in its own folder with its
  own git repository outside the engine checkout (the owner's projects: on the external drive,
  e.g. `/Volumes/KINGSTON/<Project>/games/<game>`); scratch and test games go next to it, not here.
- Game-specific code lives only in the game project: C++ in `<project>/native/` as native modules
  (`docs/native-modules.md`, `aiverse_native_module`, rules.json `nativeModules`,
  `aiverse_native_use_meta` for a meta backend), platform SDKs as plugins (`docs/plugins.md`).
  This covers bots / AI ports, backend and meta adapters, server stats, importers and converters
  of that game's assets, its skills / modes / map actors, HUD and tutorial glue, input schemes,
  its docs and its tests.
- The engine gets only generic, game-agnostic mechanics and extension points. If the native
  module API lacks a hook, add a small generic one (documented in `docs/native-modules.md`, with a
  synthetic test) and implement the game part in the project. No game names in engine
  identifiers, file names, JSON keys, CMake targets, docs or AGENTS.md rows.
- The engine must build and pass every test without any external game. A test that needs a real
  game takes its folder from an environment / CMake variable and skips when it is absent;
  per-game checksum baselines live in the game's folder, not in `tests/checksums/baseline.txt`.
- Importers and tools never write into `templates/` by default: the game folder is an explicit
  argument.
- Branches and worktrees of game work are not created in the engine repository. Keep them in the
  project's own repository (it may fetch the engine's `main` as a remote); only generic engine
  changes come back to the engine, through the integrator.
- Found game-specific code already in the engine? Move it out to the project (see commit
  "Move the Drone Fall backend adapter out of the engine") instead of extending it.

**Licenses/assets:** only vendor code/assets with permissive licenses and keep their license files.
Original assets of commercial games are never committed; importers read them from the user's own
copy and builds mark them external.

**Model packs: every texture once.** A pack of models (`.glb`) that share materials must not embed the
same images in every file: a 4-texture path kit became 1.4 GB (31 files x 42 MB of identical PNGs, 6 MB of
geometry) and the client no longer fitted in memory. Before a pack goes into a game folder:
- textures are external shared files next to the models (`tex/<hash>.png`, glTF `images[].uri`), one copy
  per distinct image — `tools/citygen/make_city.py: copy_model(src, dst)` does this for any `.glb`;
- texture size fits the game's zoom (tiling surfaces of a top-down game: 512 px; say so when you keep more);
- the pack's install / build script does it itself, so a rebuild does not bring the copies back;
- check the result: `du -sh` of the installed folder against the distinct-texture size, and state both in
  the pack's README. A pack whose size is mostly repeated images is not finished.

## 4. Build, test, verify

```sh
# Linux / CI
cmake --preset linux-release && cmake --build build/linux-release
build/linux-release/aiverse_tests                                   # all must pass
build/linux-release/aiverse_headless --project templates/skirmish --seed 3   # must end with a winner
build/linux-release/aiverse_headless --project templates/mushrooms --seed 3

# macOS (Xcode generator) / iOS
cmake --preset macos && cmake --build build/macos --config Release
build/linux-release/aiversebuild macos --project templates/skirmish      # packaged .app in dist/

# Screenshot check without a display (Linux): any visual change must be looked at
Xvfb :97 -screen 0 1280x800x24 &
DISPLAY=:97 AIVERSE_SHADOWS=medium AIVERSE_SCREENSHOT=/tmp/shot.ppm AIVERSE_EXIT_AFTER_FRAMES=300 \
  build/linux-release/AiverseGame --map 0 --spectate
```

Environment knobs: `AIVERSE_CONTENT` (content root), `AIVERSE_GFX=vulkan|gl` (RHI backend on Windows / Linux / Android;
`AIVERSE_VK_VALIDATION=1`: Khronos validation layer), `AIVERSE_SHADOWS=off|low|medium|high`, `AIVERSE_AUDIO=off|null`
(null: real audio thread without hardware), `AIVERSE_AUDIO_CAPTURE=mix.wav` (record what was played),
`AIVERSE_SCREENSHOT`, `AIVERSE_EXIT_AFTER_FRAMES`, `AIVERSE_SPLASH=off|full` (loading screen; default: full splash, only the loading time with `--map`), `AIVERSE_FAST_FORWARD=seconds`
(bots play that long before the first frame), `AIVERSE_SCRIPT_LOG=1` (client: script log lines `[script] t=<tick> ...` on stdout, as
`aiverse_headless` prints them).

A change is done when: it builds without warnings on gcc/clang (`-Wall -Wextra`), tests pass, the
headless matches still finish, visual changes were checked on a screenshot, Metal code paths that
couldn't be compiled are listed in the commit message as unverified.

### Where build output goes (all agents)

**Never create builds in the shared engine checkout's `build/` folder** (the user's main
checkout, `<repo>/build`). It is reserved for the user's own Mac build (`build/mac`), the
integrator's release steps in §8 and the verify launchers. Everything else goes elsewhere:

- Engine work: build inside your own worktree (`.worktrees/<branch>/build/...` or the
  presets' `build/<preset>` relative to *that* worktree), or a temp dir outside the repo.
- Game / project work (packaging, native modules, test runs of a game): the project's own
  folder, `<project>/Builds/` with intermediates in `<project>/Builds/.temp/` (the editor
  and `aiversebuild` already use this layout).
- Logs, bundles, patches and scratch archives belong with the build that produced them,
  not loose in `build/` or `build/.temp/`. Delete your build trees when the task is merged.

## 5. Working in parallel

**Per-stage continuation rule (multi-stage tasks):** after each stage, update the task's agent handoff document with APIs, files, tests/results, remaining risks and the next-agent entry point. For Enhanced Input use `docs/enhanced-input-agent-handoff.md`. Do this BEFORE starting the next stage; do not claim a stage verified without real build/test results.

Several agents work at the same time. **Each agent works in its own branch/worktree** and only in
its area. Need to change a file outside your area? Keep the change minimal, additive, and say so in
the commit message; prefer adding a new file over editing a shared one.

**Branch names:** `feature/aiv<number>_<feature_name>` — `<number>` is the task number,
`<feature_name>` is short snake_case (e.g. `feature/aiv142_city_traffic`). Do not name new branches
`agent-...` or `codex/...`; those in the area table below are existing branches and keep their names.
The worktree folder follows the branch: `.worktrees/feature/aiv142_city_traffic`.

| Area | Paths | Owner |
|---|---|---|
| Simulation & mechanics, AI, triggers | `engine/core`, `engine/sim`, `modules/rts/*.{h,cpp}` | main agent |
| Editor, MCP server, device simulator | `apps/editor`, `tools/mcp`, `docs/game-format.md` | main agent |
| Models, PBR materials, skinning, procedural animation | `engine/render`, `engine/gfx`, `modules/rts/view`, `apps/modelview`, `tools/models`, `docs/models.md`, `templates/showcase` | models agent |
| In-game UI (RmlUi, HTML/CSS) | `engine/ui`, `apps/client`, `engine/assets/ui` | UI agent |
| Resource packs `.rpak`, build size/compression, unpackers/importers of foreign game data | `engine/pack`, `tools/assetpipe`, `tools/unpackers`, `tools/importers`, `tools/aiversebuild` | packs agent |
| Texture atlases (packer, manifest, runtime lookup; convex meshes, tight packing, framed sheets) | `engine/atlas`, `tools/atlas`, atlas step in `tools/aiversebuild` / `tools/assetpipe`, `tests/tests_atlas_tight.inc`; hooks: `TextureCache::resolveSprite` + `AtlasSprite`, `SpriteBatch` (`spriteShape`, `polygon`, `casterPolygon`, atlased `quad`), `RtsSprites.cpp` (sprite shapes, `decalShape`) | atlas agent |
| Localization | `engine/i18n`, `engine/assets/lang`, `templates/*/lang`, `tools/i18n`, `modules/rts/RtsLocalize.*` | i18n agent |
| Audio | `engine/audio`, `modules/rts/audio`, `tools/audiopack`, `third_party/miniaudio`, `third_party/vorbis`, `templates/*/audio.json`, `docs/game-format.md` § audio.json | audio agent |
| 2D games: ortho camera, sprites, tile maps + autotile, tile tool | `engine/render/{SpriteBatch,SpriteShaders,TextureCache}*`, `modules/rts/RtsTiles.*`, `modules/rts/view/{Rts2DView.h,RtsSprites,RtsTileView,RtsCamera.h}*`, `apps/editor/core/TileOps.*`, `apps/editor/windows/TilesWindow.cpp`, `templates/tiles2d`, `templates/km2d`, `tools/art2d` | 2D-mode agent |
| Dynamic lighting (point lights, light map, flashes, night) | `engine/render/Lights.*`, `engine/render/LightShaders.h`, `modules/rts/view/RtsLights.*` | lighting agent |
| Platforms, device testing, CI (window/input/keyboard per OS, packaging templates) | `engine/platform`, `platform/`, `.github/` | platforms agent |
| Native plugins: bridge (`Plugins.h`), store/ads services, SDK plugins, aiversebuild plugin step | `engine/platform/Plugins*`, `engine/platform/apple/`, `engine/services`, `plugins/`, `tools/aiversebuild/Plugins.*`, `docs/plugins.md` | platforms agent |
| Particles: CPU sim + RHI render, event effects, Effects window | `engine/fx`, `modules/rts/fx`, `templates/*/fx`, `apps/client/ClientFx.*`, `apps/editor/windows/FxWindow.cpp`, `docs/particles.md` | particles agent |
| Terrain simulation (heightfield model, slopes, LOS, `cornersFile`, HPA* for big maps) | `modules/rts/RtsHpa.cpp`, heightfield parts of `modules/rts/RtsMap.*` / `RtsData` (`terrain` block), `MapDoc` corners plumbing, `docs/terrain.md` (sim part), `templates/metaltide` map | agent-terrain-sim (MAIN merges) |
| Terrain overlays: territory colours + glowing borders, diggable rock blocks / veins / dig marks, MW2 lanes, road ribbons (visual `terrain.territory/dig/lanes/roads`) | `modules/rts/view/RtsGroundFx*`, `docs/game-format.md` § Terrain overlays; hooks: `RtsView.{h,cpp}` (member, init/draw/shutdown, `RtsScene` lane/viewer fields), `RtsTerrain.cpp` (invalidate + flat dig cells), `Renderer3D::bindLitFrame` | agent-terrain-fx (MAIN merges) |
| Terrain rendering: chunked terrain + CDLOD, heightfield surface (`RtsView::heightAt`), sub-cell detail surface (`terrain.style`/`terrain.detail`), splat materials / quality tiers, water bodies, sculpt / material / heightmap ops + **Рельеф** window + MCP | `modules/rts/view/RtsTerrain*.{h,cpp}` (`RtsTerrainRender`, `RtsTerrainDetail*`, `RtsTerrainSurface.h`, `RtsTerrainShaders.h`), `apps/editor/core/TerrainOps.*`, `apps/editor/core/TerrainDetailOps.cpp`, `docs/terrain.md` § Detail surface, `apps/editor/windows/TerrainWindow.cpp`, `tools/terrain`, `templates/frostcrown` terrain block + textures, `docs/game-format.md` § Terrain materials; hooks: `Renderer3D::drawCustomOpaque`, `TextureCache::resolve(tiling)`, Metal vertex textures, pause-menu material tier | agent-terrain (MAIN merges) |
| Overlays: data-driven helper elements (build grids, range rings, rally lines, damage numbers, bars, HUD labels), built-in selection / hover / markers / placement / bars as default overlays, **Оверлеи** window + MCP `overlay_*` | `modules/rts/view/RtsOverlay*.{h,cpp}` (`RtsOverlayData` = schema / validation / templates, GPU-free lib `aiverse_overlay_data`; `RtsOverlays` = evaluator + renderer, `RtsOverlayShaders.h`), `apps/editor/core/OverlayOps.*`, `apps/editor/windows/OverlaysWindow.cpp`, `docs/game-format.md` § Overlays; hooks: `RtsView.{h,cpp}` (member, init/draw/drawOverlays, `overlayTop`), client `main.cpp` (OverlayContext, footprint moved to the `placement` overlay) + `Minimap` marks, `Editor.{h,cpp}` (context, preview fields) | overlays agent (MAIN merges) |
| Splash & icons: game.json `icons` / `splash`, icon + launch screen generation per platform, in-engine loading screen, Branding window + MCP | `engine/splash`, `tools/branding`, `tools/aiversebuild/Branding.*`, `apps/client/ClientSplash.*`, `apps/editor/core/BrandingOps.*`, `apps/editor/windows/BrandingWindow.cpp`, `templates/_tools/brandart.py`, `templates/skirmish/branding`, `docs/game-format.md` § Icons and splash; hooks: `GameClient::initStep`, `aiverseMain`, `platform/web/shell.html` splash, `platform/ios/Info.plist.in` storyboard key, window colour in PlatformWin32/PlatformMac | splash agent (MAIN merges) |
| Editor look: theme (dark/light, accents), fonts (Inter + Noto Symbols 2 + Lucide icons), top/status bars, toolbar helpers | `apps/editor/EditorTheme.*`, `apps/editor/EditorIcons.h`, `ui::` toolbar helpers in `apps/editor/EditorUtil.h`, `apps/editor/EditorShell.cpp` chrome, `third_party/fonts` | editor-ui agent (MAIN merges) |
| Scripting: Lua 5.4 VM (sim sandbox with fx numbers + view scripts), `game` API, script effects/trigger hooks, script MCP ops, hot reload, `templates/scripted` | `third_party/lua`, `modules/rts/RtsScript*`, `modules/rts/lua/*.lua` (engine standard library: victory, waves, triggers runtime, auto-build/sell — the C++ versions are gone, edit the Lua), `tests/checksums`, `modules/rts/view/RtsViewScript.*`, `apps/editor/core/ScriptOps.*`, `apps/editor/windows/ScriptsWindow.cpp`, `docs/scripting.md`, `templates/scripted`; hooks: `RtsModule` (setup/system/hash/member), `EffectKind::Script`, trigger `script` action/condition, `embedScripts` calls in loaders | scripting agent (MAIN merges) |
| VFX tools: Unity particle importer, fx MCP ops (list/get/set/delete/templates/import/bind/preview), optional particle modules (trails, sub-emitters, noise, lights, bursts, ...) | `tools/importers/unity_vfx`, `tests/data/unity_vfx`, `apps/editor/core/FxOps.*`, `apps/editor/core/FxTemplates.cpp`, `apps/editor/EditorFxOps.cpp`, `docs/particles.md` § MCP / Unity import; hooks: `engine/fx` modules (with the particles agent), `gfx::BlendMode::Premultiplied`, `Change::Fx`, `Editor::tickFx`, `ClientFx::addLights` | vfx tools agent |
| Sprite shadows & lighting: 2D sprites cast into the sun shadow map, receive shadows / scene light, projected blob + ground-sprite decals, `sprites` block | `modules/rts/view/RtsSpriteShadow.*`, sprite parts of `RtsSprites.cpp` / `SpriteBatch` / `SpriteShaders.h`, `docs/game-format.md` § Sprite shadows; hooks: `Renderer3D::drawCustomShadow` + `lightViewProj()`, `RtsLights::addContactShadows` (skips sprite types), `RtsView` (`sprites` defaults, `isSprite`) | sprite-shadows agent (MAIN merges) |
| Hooks for a game's own backend adapter (the adapter itself lives in the game's `native/`, never here): MessagePack + encodings, plain HTTP server/client, request/response HTTP on the platform stack, matches from an external matchmaker (reservations, tickets, spectators, end report), server extensions (`aiverse_server --extension`), `game.connect`, plugin events in Lua | `engine/meta/MsgPack.*`, `engine/meta/Encoding.*` (`aiverse_meta_codec`), `engine/net/PlainHttp.*`, `engine/net/server/ServerReserved.cpp`, `engine/net/server/ServerExtension.*`, `tests/remote_tests.cpp`, docs/native-modules.md "Remote backends"; hooks: `net::Server::reserve` + `ServerConfig` external-match callbacks (`Server.h`, `ServerInternal.h`, `Matchmaker.cpp`, `ServerMatch.cpp`, `Server.cpp`), `apps/server/main.cpp --extension`, `engine/pack/http/HttpRequest.h` + `httpRequest` in each `engine/pack/http/Http*.cpp`, `plugins::addAnyListener` (`engine/platform/Plugins.*`), `game.connect` / `game.on('plugin')` (`apps/client/ClientScript.cpp`, `Lobby.cpp`, `ClientServices.cpp`) | agent-bw-meta (MAIN merges) |
| Planet rendering: cube-sphere CDLOD terrain, ocean, atmosphere, stars, orbits, orbit camera (surface / planet / system), objects on spheres, planet fog (seam-aware blur), ground overlays on spheres (territory, dig rock, roads, lanes), sphere minimap, editor planet camera + 3D brushes, planet MCP params; territory / dig / rooms / lanes on planets in the sim | `modules/rts/view/RtsPlanet*` (incl. RtsPlanetGroundFx, PlanetFieldBlur), `apps/client/{ClientPlanet,MinimapPlanet}.cpp`, `apps/editor/core/PlanetOps.*`, `docs/planets.md` §5 (+ §3 territory / lanes), `docs/game-format.md` § Planets: visual keys; sim: the `g.surf` branches in `RtsDungeon.cpp`, `RtsLanes.cpp` (rebuildLanesSphere), `RtsAI::dungeon` dig planning; hooks: curved-world hooks in `engine/fx` (ParticleSystem up / altitude / view space) and `RtsFx::setPlaceFn`, `FogOfWar::setBlur`, `RtsGroundFx` look accessors, `RtsView` (planet members, draw / overlay anchors), `RTSCamera::zNear/zFar`, client / editor camera + picking behind `planet()`; content: `templates/planets` fx / territory / roads | agent-planet-render (MAIN merges) |
| Terrain / map generation: integer noise, erosion, rivers, biomes, symmetric fair starts, content from the game's types, presets (flat + planets), match-time `generate`, `aiverse_headless --generate`, editor **Генерация** window + MCP `map_generate` / `gen_presets` | `modules/rts/gen/*`, `apps/editor/core/GenOps.*`, `apps/editor/windows/GenWindow.cpp`, `docs/game-format.md` § Map generation, `docs/planets.md` §4; hooks: `RtsModule::setup` (applyGenerate + `generatedMap()`), `applyTilesToMap` (prepareGenerate), client / editor view read `generatedMap()`, headless flags | agent-terrain-gen (MAIN merges) |
| Planets in the simulation: cube-sphere maths, `Surface`, world block, orbit layer, transit, planet MapDoc / validate, `templates/planets` | `modules/rts/RtsSphere.*`, `RtsSurface.*`, `RtsMapSphere.cpp`, `RtsHpaSphere.cpp` (planet HPA*), `RtsPlanets.cpp`, `apps/editor/core/MapDocPlanet.cpp`, `templates/planets`, `templates/_tools/planetgen.py`, `docs/planets.md` §3; hooks in `RtsMap`, `RtsModule`, `RtsUnits`, `RtsWorld`, `RtsAI`, `MapDoc`, `Workspace` map_info | agent-planet-sim (MAIN merges) |
| Post effects: `postfx` block (game.json / map), effect catalog + presets + cost model, PostFx renderer (GLSL + MSL, quality tiers), **Постэффекты** window + MCP `postfx_*` / `view_postfx` | `engine/render/PostFx*.{h,cpp}` (`PostFxData` = format / cost, GPU-free lib `aiverse_postfx_data`; `PostFx` + `PostFxShaders.h` = renderer), `apps/editor/core/PostFxOps.*`, `apps/editor/windows/PostFxWindow.cpp`, `docs/postfx.md`; hooks: `Renderer3D::render(pass, post)` + `resolvePostFx()`, client `main.cpp` / `Lobby.cpp` / `GameClient.h` (member, configure, governor), `Editor.{h,cpp}` (member, `postFxInScene`), `Workspace.cpp` registration | agent-postfx (MAIN merges) |
| Toolchains: per-target requirement detection (beyond PATH), installers into `~/.aiverse/toolchains`, `aiversebuild --check-toolchain / --install-toolchain / --install-missing`, Build window toolchain panel, selectable Console, MCP `toolchain_*` | `tools/aiversebuild/Toolchain*`, `apps/editor/core/ToolchainOps.*`, README § Инструменты сборки; hooks: `aiversebuild` main (per-target check), `Workspace::startTool` / `toolchainCheck`, `BuildWindow`, `ConsoleWindow` | toolchains agent (MAIN merges) |
| Flow fields (shared integration fields for groups / hordes on flat maps and planets), `movement.flowField`, headless `--bench-group` | `modules/rts/RtsFlowField.cpp`, `FlowField` in `RtsMap.h`, `docs/pathfinding.md`; hooks in `RtsUnits` (moveToFlow, sysFlowFields), `RtsModule` (group orders), `RtsWorld` (waves), `RtsAvoidance` / `RtsPlanets` (stuck fallback), `RtsData` (keys); `templates/lastcolony/maps/horde_plain.json`, `templates/planets/maps/big_planet.json` | agent-flowfield (MAIN merges) |
| Computer player: terrain analysis (high ground, chokepoints, routes, watch posts), terrain-aware waves / rally / siege spots, towers, planets invasions, stalemate escalation, own build spots, expansion, bot save states, rules `ai` knobs | `modules/rts/RtsAI*.{h,cpp}` (`RtsAITerrain.*`, `RtsAITactics.cpp`), `docs/game-format.md` § Computer player; hooks: `RtsData` (`ai` block), `RtsModule::freeSpotNear` (`toward`: spawn on the rally side), `RtsEconomy` (spawn uses it), headless `--save/--load` (`.ai` file) | agent-ai-terrain (MAIN merges) |
| Mechanics consolidation: one implementation per duplicated mechanic (armor / shields / upkeep / worker limit / crews / garrison-paid upgrades / flow fields), agent-mech keys as aliases | `modules/rts/RtsOriginals*.cpp` (damage pipeline, crews), alias parsing in `RtsOriginalsData.cpp` / `RtsMechData4.cpp`, `docs/game-format.md` § Fidelity mechanics + agent-mech alias sections | mech-consolidate (MAIN merges) |
| Navigation mesh (genre-independent): `nav::Source` -> merged rectangles, portals, A* + funnel, tile refresh; RTS `movement.pathfinding: "navmesh"` / `"hybrid"` (HPA* cluster graph + solver per cluster, tile classes), headless `--bench-nav`; FPS bot navmesh adapter | `engine/nav`, `docs/navmesh.md`, `tests/tests_navmesh.inc`; hooks: `RtsMap` (`navPath`, `hybridPath`, `GridNavSource`, dirty marks in `block` / `cellChanged` / `invalidatePathing`), `RtsHpa.cpp` (`hpaRoute`), `RtsFlowField.cpp` (`flowHierFor` via `flatHierarchy`), `apps/headless` (`--bench-nav`), `RtsData` / `RtsModule` (key, `prepareNavMesh`), `libraries/include/aiverselib/bots/NavMesh.h` (FPS adapter), `docs/pathfinding.md` § Navigation mesh | agent-navmesh (MAIN merges) |
| Crowds: flow-field arrival / pass-through, `movement.blockedGoals` | hooks in `RtsUnits` (sysFlowFields), `RtsAvoidance` (same-crowd ORCA, settled obstacles, no yield), `RtsMap` (findPath blocked goals), `RtsData` / `RtsModule` (keys); `docs/pathfinding.md` § Crowds | agent-crowds (MAIN merges) |
| Lua coverage audit: capability matrix, gap list, Lua prototypes of missing mechanics, Lua perf bench | `docs/lua-coverage.md`, `templates/lua_lab` (+ `bench/`), test `lua_lab_prototypes`, `lua_lab` lines in `tests/checksums/baseline.txt` | lua-audit agent (MAIN merges) |
| Project files `.aiverse` (legacy `.rtsproj` still opens) + OS file association (double-click opens the editor: macOS document type / LaunchServices, Windows HKCU ProgID, Linux mime + .desktop), editor / document icons | `apps/editor/core/ProjectFile.*`, `apps/editor/core/FileAssoc.*`, `apps/editor/EditorOpenFile.cpp`, `tools/branding/ProjectIcon.*` + `projicon_main.cpp` (`aiverse_projicon`), `platform/macos/EditorInfo.plist.in`, `templates/*/*.aiverse`, docs/overview.md § Project file .aiverse; hooks: `EventType::OpenFile` + `detail::requestOpenFile` (Platform.h / PlatformCommon, PlatformMac `openURLs`), `Workspace::openGame` (resolve + migration), `createProject`, hub folder browser / status, editor `main.cpp` positional arg, aiversebuild `--project` + `*.aiverse` / `*.rtsproj` ignore | project-file agent (MAIN merges) |
| Grid fields (G5): rules `fields` + map `gridFields`, C++ kernels (decay / diffuse / spread / flow / consume), per-cell effects, Lua `field` table + `on_field_threshold`, field overlay (GLSL + MSL), **Поля** window + MCP `field_*`, `templates/wildfire` | `modules/rts/RtsGridField*`, `modules/rts/view/RtsFieldView.*`, `apps/editor/core/FieldOps.*`, `apps/editor/windows/FieldsWindow.cpp`, `docs/fields.md`, `templates/wildfire`, `templates/_tools/wildfire.py`, `tests/tests_fields.inc`; hooks: `RtsModule` (setup / system / hash / stat speed / member), `RtsSave` (append), `RtsWorld::sysVision` (occluders, reveal), `EffectKind::Field`, `RtsScriptEngine` (`on_field_threshold`), `RtsView` (member, draw, `RtsScene::fields`), client `main.cpp` (particles), `Editor` (Scene preview) | agent-grid-fields (MAIN merges) |
| Vision performance: cached / incremental fog-of-war passes bit-identical to the brute-force reference, `AIVERSE_VISION=ref\|check`, headless `--profile` | `modules/rts/RtsVision.cpp` (sysVision, flat + sphere, reference), `GameState::visionToken`, `Simulation::setProfileClock`, docs/terrain.md § Vision | agent-vision |
| TEST_TERRITORIES game and GDD completion | External `/Users/aleksandrfeer/AiverseProjects/TEST_TERRITORIES`; minimal generic scripting API extensions in `modules/rts/RtsScript*`, explicit entity-action controls in `modules/rts/view/EntityActionControls.h`, restoration verification in `apps/headless`, and minimal generic view-effect/orthographic particle fixes with tests/docs | territory game task (`codex/territory-lua-front`) |
| Native C++ game modules | `modules/rts/RtsNative.*`, `modules/rts/native`, `examples/native`, `docs/native-modules.md`; hooks: RtsModule, RtsSave, CMake, aiversebuild source export | cpp-modules agent |
| Editor with the project's C++ modules: detect missing `nativeModules`, build `<project>/Builds/.temp/editor-<host>`, switch / restart into it, MCP `native_status` / `native_build` | `apps/editor/core/NativeBuild*.{h,cpp}`, `apps/editor/EditorNativeBuild.*`, `docs/native-modules.md` § Редактор с модулями проекта; hooks: `Workspace::startCommand` / `appendBuildLog`, `LineServer::close`, `Editor` (open / Play / frame / init, unsaved prompt `drawUnsavedChanges`, no file association in a project build), editor `main.cpp` (`--native-editor`, `--tools-from`) | native-editor agent (MAIN merges) |
| OpenWar port (Superpowers at War 1:1 in Lua): generic engine support only — script commands `mapClick` (client + editor input), territory / script HUD / region bindings it uses; presentation hooks: game.json `playerColors`, flat lighting keys (`day`, `fogDensity`, `tonemap`), view `camera` / `set_camera`, categorical cell paint layers | `modules/rts/RtsScriptCommands.cpp` (`mapClick`), `apps/client/ClientScriptCmd.cpp`, `apps/editor/windows/GameWindow.cpp` (script card), `docs/scripting.md` § Script commands / Camera / Categorical cell layers; hooks: `RtsOverlayData` (palette), `RtsLightsData` + `Renderer3D` (`Lighting::tonemap`, U1 skyColor.w) + the ACES line of the lit shaders, `RtsViewScript`, `RtsPaintLayers` (cells pipeline), client `runViewScripts`; game data lives outside the engine (`OpenWar/`) | agent-openwar (MAIN merges) |
| Game wizard «Сделать игру»: a whole project from art + idea (template choice, content import, hard tutorial from the rules, soft hints, levels, meta screens, monetization, languages, bot check), MCP `game_make*`, hub page; `"card": false` script commands | `apps/editor/core/GameWizard*`, `apps/editor/core/WizardOps.*`, `apps/editor/EditorGameWizard.cpp`, `apps/editor/EditorHub.h`, `templates/_wizard`, `docs/game-wizard.md`; hooks: hub tab / menu (`EditorProjects.cpp`, `EditorShell.cpp`, `Editor.h`), `Workspace` registration, `ScriptCommandDef::card` (`RtsScript*`, client / editor command cards) | agent-gamewizard (MAIN merges) |
| Engine project import | `tools/importers/{lib,port,unity_project,godot_project,unreal_export}`, `tests/data/port`, `docs/engine-import.md`; minimal editor/MCP/i18n hooks | agent-engine-import (MAIN merges) |
| Unity UI + localization import (Drone Fall port): uGUI / UI.Windows prefabs -> RmlUi screens with slots + Lua stubs, Unity Localization -> lang tables | `tools/importers/unity_ui`, `docs/unity-ui-import.md`; hook: one `add_test` line in `CMakeLists.txt` | agent-ui-import (MAIN merges) |
| Unity world import: binary TerrainData (type-tree SerializedFile reader), terrain → corners / detail / splat palette, embedded & built-in meshes → OBJ, spline roads, data-driven scene markers (Drone Fall rules), `--include`, Unity editor terrain export fallback | `tools/importers/lib/unity/{serialized,terrain,mesh}.py`, `tools/importers/lib/port/{world.py,markers.json}`, `tools/importers/unity_project/{world.py,editor/}`, `tools/importers/port/test_world.py`, `docs/engine-import.md` § World; hooks: `unity_project/importer.py`, `lib/port/emit.py`, `port/aiverse_port.py`, scalar regex in `lib/unity/unity_yaml.py`, CTest `world_importer` | agent-unity-terrain (MAIN merges) |
| Built-in terrain texture quality: filtered world-space grain and micro normals for untextured ground | `modules/rts/view/RtsTerrainShaders.h`, material tier selection in `RtsTerrainRender.*`, `docs/terrain-textures.md` | rt-fx continuation (user assigned) |
| Linear UI blending: game.json `ui.blend`, sRGB layer + linear composite of the RmlUi layer, RHI `PixelFormat::RGBA8_sRGB` / `copyColor` / `setScreenReadable` (GL + Metal) | `engine/ui/UiColor.h`, `engine/ui/UiLinear.*`, linear parts of `engine/ui/UiRender.cpp` / `UiLayer.*`; hooks: `engine/gfx` (format, pipeline `colorFormat`, copy), client `initUi` / frame `setTarget`, editor `GameHud` | ui-linear (MAIN merges) |
| Editor world tree: Hierarchy of every world object (edit-mode preview of the map's setup / live play world), multi-selection, Inspector «Детали» (components, stats, script data, mixed values), `editor.hierarchy` game.json hooks, MCP `world_entities` / `world_entity` / `editor_select` | `apps/editor/core/WorldInspect.*`, `apps/editor/EditorWorld.cpp`, `apps/editor/EditorWorldEdit.h`, `apps/editor/DetailsPanel.*`, `apps/editor/windows/HierarchyWindow.cpp`, entity part of `InspectorWindow.cpp`, `docs/editor-world-tree.md`; hooks: `Editor.h` (Selection kinds, selection set, world members), `Editor.cpp` (frame / startPlay / buildEditScene / renderGame / pickEntity), `SceneWindow.cpp` (multi-select, hover, play picking) | editor-world-tree (MAIN merges) |
| World tree performance: consumer-driven reads, background preview builds, cheap row reads, Details read passes, `AIVERSE_EDITOR_PROFILE` / `AIVERSE_HOSTED_MAP` | `apps/editor/EditorProfile.h`, perf parts of the world-tree files above, `docs/editor-world-tree.md` § Performance; hooks: `ScriptEngine::peekData` (`RtsScript.h` / `RtsScriptEngine.cpp`), atomic component type ids (`engine/core/ECS.h`), `ClientHost` (`AIVERSE_HOSTED_MAP`) | world-perf (MAIN merges) |
| Reusable libraries and AI skills | `libraries/`, `tools/libraries/`, `tests/libraries/`, `skills/`, `docs/libraries.md` | reusable-libraries agent |
| AIVerse InputFlow: semantic Input Actions, Mapping Contexts, Modifiers/Triggers, JSON assets/profiles and FPS/RTS/editor migration; legacy InputSystem compatible | `libraries/include/aiverselib/gameplay/InputFlow.h`, `engine/platform/InputFlow.h`, legacy `EnhancedInput*`, `apps/fps/client.cpp`, `tests/libraries/enhanced_input*`, `skills/ui/aiverse-input`, `docs/enhanced-input*.md` | input agent (PR #20; follow handoff) |
| City libraries and skills: world-calendar, weather-timeline, vehicle-fx, swept-shapes, derived-keys, city-traffic catalog entry + routes, rules-files tool; skills for driving, city server, AutoLOD | `libraries/include/aiverselib/systems/{Calendar,WeatherTimeline}.h`, `libraries/include/aiverselib/SweptShapes.h`, `libraries/include/aiverselib/runtime/DerivedKeys.h`, `libraries/include/aiverselib/genre/{vehicle-fx,traffic}/`, `tests/libraries/city_tests.cpp`, `tools/rules/`, matching `skills/*`, docs/libraries.md § City batch; hooks: catalog entries, `tests/libraries/CMakeLists.txt` block, root CMake `rules_files` test, skill catalog rows, README skill counts | agent-city-libraries (MAIN merges) |
| Weapons and ballistics library policies: penetration, explosions, ammo feeds, charge, attachments, aim solver | `libraries/include/aiverselib/genre/{Ballistics,WeaponMods,WeaponData,WeaponPersistence}.h`, `skills/gameplay/aiverse-weapon-policies`, `examples/libraries/genre/weapon*.json`; additive hooks: `libraries/spec/gameplay.json` sections + generated outputs, `weapon-policies` catalog entry, `GenreConsumer.cpp`, `tests/libraries/genre_tests.cpp`, `docs/genre-packages.md` § Weapons and ballistics | agent-weapons (MAIN merges) |
| Meta-game C++ policies, JSON presets and agent recipes | `engine/meta`, `tools/meta`, `tests/meta`, `examples/meta`, `skills/meta/`, `docs/meta-game.md`; minimal CMake/native dependency hooks | meta-library agent |
| FPS, authoritative sessions and large worlds | `modules/fps`, `engine/net/authority`, `engine/world`, `apps/fps`, `tests/fps`, `examples/fps`, `docs/fps.md`; additive build/platform hooks | FPS task (current agent) |
| AIVERSE naming migration | Engine-wide branding, shared namespaces, build/tool names, platform bridges, documentation; preserve genre RTS | aiverse-rename (user assigned) |
| Shader Graph: material, postfx and particle graphs, compiler, editor and MCP | `engine/render/graph`, shader graph integration in render/fx/view, editor window/core, tests/shader_graph, docs/shader-graph.md | shader-graph agent |
| Programmable animation: trait stacks, subgraphs, Motion Matching, full-body IK contacts/collisions/balance, surface locomotion, visual editors and C++/Lua controls | `engine/render/AnimationGraph*`, `engine/render/AnimationRig*`, `engine/render/AnimationMotion*`, `engine/render/FullBodyIK*`, `engine/render/AnimationContacts*`, `engine/render/SurfaceLocomotion*`, `apps/editor/animation`, `apps/editor/windows/Animation*`, `apps/editor/core/Animation*`, `tests/animation`, `examples/animation`, `docs/animation-graphs.md`; minimal view/model/Lua hooks | animation-system agent |
| Model authoring and clip tools | `tools/modeling`, `tests/modeling`, `apps/editor/core/ModelingOps.*`, `apps/editor/windows/Model*`, `docs/modeling.md`; minimal registry, rig-limit, screenshot and localization hooks | codex/modeling-tools |
| Optional Box3D runtime and physics sandbox | `engine/physics`, `third_party/box3d`, `examples/physics`, `tests/physics`, `docs/physics.md`; additive CMake hooks | codex/box3d-runtime |
| Clusters, HLOD, conservative occlusion, baked surfaces and geometry performance | `engine/render/Geometry*`, `tools/modeling/SurfaceProxy*`, `apps/editor/core/{GeometryOps,SurfaceProxyOps}.*`, `tests/geometry`, `tools/geometrybench`, `tools/models/autolod.py`, `docs/geometry*.md`; minimal model LOD, renderer/view, Metal binding/uniform and macOS benchmark pacing hooks | codex/geometry-visibility |
| XPBD cloth, CPU/GPU sleeping, Metal compute solver, instanced cloth rendering and benchmarks | `engine/cloth`, `examples/cloth`, `tests/cloth`, `docs/cloth.md`; shared projected animation LOD helper in `engine/render/AnimationLod.h`, minimal `RtsAnimate` hook; optional compute/storage/readback/timing RHI hooks in `engine/gfx/Gfx.h` and `metal/GfxMetal.mm`; additive CMake/localization hooks | codex/xpbd-cloth |
| Genre audit implementation: shared strategy kernels and native integration | `libraries/include/aiverselib/strategy`, `tests/libraries/strategy*`, `examples/native/strategy`, matching catalog skills, `docs/genre-implementation.json`, `libraries/include/aiverselib/net`, `tests/meta/local*.py`, `tools/meta/local_match.py`, `docs/local-network.md`; minimal FPS host and loopback transport hooks | codex/genre-libraries |
| Combat actions: hitboxes/hurtboxes, dodge i-frames, guard arc + perfect parry, poise/stagger, lock-on (`combat-actions` package over melee-combat) | `libraries/include/aiverselib/genre/Combat*.h`, `skills/gameplay/aiverse-combat-actions`, `examples/libraries/genre/hitboxes.json`, docs/genre-packages.md § Combat actions; additive hooks: `combat` section of `libraries/spec/gameplay.json` (+ generated files), catalog entries, `GenreConsumer.cpp`, `tests/libraries/genre_tests.cpp` (`combatActions`) | agent-combat (MAIN merges) |
| Stealth and senses: sight cones, hearing/occlusion/noise budgets, trails, suspicion, ally alerts, search, disguises, hiding (`stealth-senses` package, PerceivedTarget records for bots) | `libraries/include/aiverselib/genre/Senses*.h`, `skills/gameplay/aiverse-stealth-senses`, `examples/libraries/genre/senses*.json`, docs/genre-packages.md § Stealth and senses; additive hooks: `senses` section of `libraries/spec/gameplay.json` (+ generated files), catalog entry, `GenreConsumer.cpp` function, tests appended to `tests/libraries/genre_tests.cpp`; record kinds 48..55 | agent-senses (MAIN merges) |
| Meta composition and skill consolidation | `engine/meta/Client.*`, `tools/meta`, `tests/meta`, `examples/meta`, `skills/`, catalog/skill links and meta docs | codex/meta-composition |
| Character locomotion modes: slopes, ledges, ladders, swimming, wall climbing, mantle/vault mode machine on top of stepMotion | `libraries/include/aiverselib/genre/LocomotionModes.h`, `skills/gameplay/aiverse-character-locomotion`, `locomotion` keys in `libraries/spec/gameplay.json` (+ generated files), locomotion tests at the end of `tests/libraries/genre_tests.cpp`, `docs/genre-packages.md` § Locomotion modes; hooks: `examples/native/libraries/GenreConsumer.cpp`, catalog entry | agent-locomotion (MAIN merges) |
| Match objective modes: capture/hold zones + KotH, payload, elimination lives/respawn waves, extraction, bomb/defuse, route gates, contested overtime clock, spectator transitions (`match-rules` 0.2.0) | `libraries/include/aiverselib/genre/MatchObjectives*.h`, `skills/gameplay/aiverse-match-rules`, `examples/libraries/genre/objective*.json`, docs/genre-packages.md § Match objectives; additive hooks: `objectives` section of `libraries/spec/gameplay.json` (+ generated files), catalog entry, `GenreConsumer.cpp`, `tests/libraries/genre_tests.cpp` (`matchObjectives`); save record kinds 40..47 | agent-match (MAIN merges) |
| Match modes in the RTS sim: domination, king of the hill, time limit + game speed (also in network play: `Msg::ClockRate` / `ClockRateSet`), sudden death, zones, team conquest, `matchModes` presets, `timedRules` / `ruleSets`, resource `levels`, match statistics, mode trigger vars; native module hooks (`filterCommand`, `garrisonGrowth`, `upgradeOrder`, `castPlayerAbilityFree`, `matchMap`, `kEvAssault`) | `modules/rts/RtsModes.{h,cpp}`, `modules/rts/RtsModesData.cpp`, `tests/tests_modes.inc`, docs/game-format.md § Match modes, docs/native-modules.md § Hooks; hooks: `Rules::modes` + `loadModes` (`RtsData`), `GameState::modes`, `kEvModePhase`, `kDefeatMode`, systems / hash (`RtsModule.{h,cpp}`), `ioModesState` (`RtsSave.cpp`), level mods in `playerModsStat` (`RtsOriginals.cpp`), `GameModule::speedScale` (`Simulation`), clock rate in `engine/net` (Protocol, Rollback, NetClient, server match clock), `team_elimination` (`victory.lua`, `RtsScriptStd.cpp`), native hooks in `RtsNative.*`, `applyCommand`, `sysGarrison`, assault paths (`RtsSkills.cpp`, `RtsUnits.cpp`) | match-modes agent (MAIN merges) |
| Genre bot adapters: melee/shooter bot policies, squad roles + formations, objective choice hook (`genre-bots` package, record kinds 56..63) | `libraries/include/aiverselib/bots/{GenreCommon,MeleeBot,ShooterBot,ObjectivePolicy,SquadRoles,GenreBotPersistence,GenreBots,GenreBotConfig}.h`, `skills/ai/aiverse-genre-bots`, `examples/libraries/genre/bot-settings.json`, docs/genre-packages.md § Genre bots; additive hooks: `genreBots` section of `libraries/spec/gameplay.json` (+ generated files), catalog entry, `GenreConsumer.cpp`, `tests/libraries/genre_tests.cpp` (`genreBots`), docs/bots.md row | agent-genre-bots (MAIN merges) |
| In-match progression: perks + modifier pipeline, hero xp/levels, level-up offers, xp pickups, summons, battle deck (`match-progression` package, record kinds 104..111) | `libraries/include/aiverselib/genre/{Perks,PerksData,PerksConfigured,PerksPersistence,XpPickups,Summons,BattleDeck}.h`, `skills/gameplay/aiverse-match-progression`, `examples/libraries/genre/perks*.json`, `tests/libraries/perks_tests.cpp`, `tools/libraries/progression_main.cpp` (`aiverse_progression`), `examples/native/libraries/ProgressionConsumer.cpp`, docs/genre-packages.md § Match progression; additive hooks: `perks` section of `libraries/spec/gameplay.json` (+ generated files), catalog entry, test/tool blocks in `CMakeLists.txt` and `tests/libraries/CMakeLists.txt`, GenreConsumer call, skill search aliases | agent-perks (MAIN merges) |
| Battle networking for mobile PvP / PvE: secure WAN UDP (tickets, encryption, NAT rebinding, battle ping), loss redundancy, sticky hero input + correction smoothing, hosted battle server mode (allocation, start payload, result consensus, reports), control plane, load harness, `battle_net_simulate` | `engine/net/{Crypto,MatchTicket,SecureUdp,Datagram,SimNet,HeroInput,Smoothing,BattleProtocol}.*`, `engine/net/server/{ServerHosted,BattleControl}.*`, `third_party/monocypher`, `apps/battle`, `tests/net_mobile`, `apps/editor/core/BattleNetOps.cpp`, `docs/battle-net.md`; hooks: `ReliableChannel::setRedundancy`, `RollbackSession::setRollbackHook`, `NetClient` (payload / reporter / suspend / resume), `Server` (hosted + secure members, Hello / Away / ResultReport / sweep / leaveRoom / startMatch / onInputFrame / endMatch / applyOutcome / saveReplay hooks), `Protocol.h` Msg values, `apps/server/main.cpp` battle flags, `Workspace.cpp` registration, MCP text, `net.*` lang keys | agent-net-mobile (MAIN merges) |
| Unity character animation import: AnimationClips (FBX take splits, .anim → glb), clip sidecars `<model>.clips.json` (splits, external clips by node path, root motion in place), Mecanim controller node (layers, masks, blend trees, transitions, triggers), `aiverse_clipinfo` dry-run tool | `tools/importers/unity_anim`, `engine/render/AnimationController.*`, `engine/render/ModelClips.*`, `tools/unity_anim`, `tests/unity_anim`, `docs/unity-animation.md`; hooks: `AnimationGraph` (`controller` node kind), `loadModel` (sidecar), `AnimClip` source start / fps / rootSpeed (+ `ModelFbx`), `RtsAnimate` built-ins `moveX`/`moveZ`/`attackAge`/`hitAge`/`spawnAge` | agent-unity-anim (MAIN merges) |
| Unity URP materials / Shader Graph / texture import (Drone Fall port): material slot matching, URP Lit/SimpleLit/Unlit/Particles + packed-map repacking, `.shadergraph` → engine graph converter, TextureImporter settings → packaging rules, ASTC 4x4/6x6 KTX2 packaging (astcenc) | `tools/importers/unity_materials`, `tests/data/unity_materials`, `tools/aiversebuild/TextureCompress.*`, `docs/unity-materials.md`; hooks: `unity_project/importer.py` + `lib/port/emit.py` (materials.json, packaging), `lib/unity/unity_scene.py` (`model_instances`), `RtsLook.cpp` material keys (`metallicRoughnessMap`, `occlusionMap`, `occlusionStrength`, `normalScale`, `uvOffset`) + `MaterialOps` key list, ASTC_6x6 in `TextureCodec` / `Gfx.h` / GL / Metal / `Model.cpp`, `FileRules` `compress` keys, CMake block, tests at the end of `tests/tests.cpp` | agent-unity-materials (MAIN merges) |
| Top-down heroes (twin-stick direct control, Drone Fall port): rules / game.json `topDown`, stick input commands `kCmdTopDown` / `kCmdTopDownAbility`, hero movement / aim / auto-fire, hero bot, virtual joystick + buttons + follow camera, `templates/topdown_arena` | `modules/rts/RtsTopDown.*`, `modules/rts/view/TopDownControls.*`, `apps/client/ClientTopDown.cpp`, `tests/tests_topdown.inc`, `docs/topdown.md`, `templates/topdown_arena`; hooks: `RtsModule` (command enum, member, setup / command / hash), `RtsUnits::sysMovement`, `RtsAvoidance` (heroes as obstacles), `RtsSave`, `RtsAI::update`, `RtsScriptApi` / `SimTools` command names, `GameClient` (event / frame / camera / draw), CMake | agent-topdown-control (MAIN merges) |
| Drone Fall stage 2, hero + network: one hero command scheme (`kCmdTopDown` = `net::kCmdHeroInput` 0xF100, `kCmdTopDownAbility` = `net::kCmdHeroAbility` 0xF101, button phases), frozen match outcome (`MatchOutcome`, `outcomeBlob`), RTS top-down battles on the hosted server + load test (`RtsBattle`), squad followers, free fire, hold / aim buttons + windups, hero escape, client visual prediction + rollback smoothing | `engine/net/HeroInput.h`, `apps/battle/{RtsBattle,BattleLoad,HeroDuel}.*`, `modules/rts/RtsTopDown.*`, `modules/rts/view/{TopDownControls.*,TopDownPrediction.h}`, `apps/client/ClientTopDown.cpp`, `tests/tests_topdown.inc`, `tests/net_mobile/battle_tests.cpp`, `docs/topdown.md`, `docs/battle-net.md`; hooks: `RtsModule.h` (enum values, `kUnitFollower`, `MatchOutcome`, friend `TopDown`, `kEvGroupTeleport` value 2), `RtsModule.cpp` (`outcome` system, `result` latch, command filter skips followers), `RtsAI` / `RtsAITactics` `controllable`, `SimTools` topdown_ability fields, `GameClient` (present / pause / members), `apps/battle/main.cpp` `--project`, CMake `aiverse_battle_rts`, `RtsRaidBot` (heroes steered: orbs / circles / objectives / extraction, kiting, telegraph dodge), `RtsAI` (raid hero think rate, `topDown.bot.progression`), `Progression::botGoal`, `RtsNative` (hero ids reserved), `kKeepFlags` in flag clears (`RtsEconomy` / `RtsDungeon` / `RtsMech` / `RtsModule`) | agent-df-hero-net (MAIN merges) |
| In-match progression on real units (Drone Fall port): rules `progression` (match-progression package glue: hero xp from kills / xp orbs, level-up offers chosen by `kCmdProgression` or the bot, perk stats on the unit pipelines, bullet perks as projectile-program variants, on-hit payloads, summon circles / recruits as real units, battle deck from the map's `players[i].deck`), Lua `progression` + view card hooks, `templates/perk_arena` | `modules/rts/RtsProgression*.{h,cpp}`, `tests/tests_progression.inc`, `docs/progression.md`, `templates/perk_arena`; hooks: `RtsModule` (command / events enum, member, setup / system / command / hash, `stat`), `RtsCombat` (`dealDamage`, shot / on-hit hooks, `projectileWeapon`), `RtsProjectilePrograms` (variant flights), `RtsSave`, `RtsData` (`Rules::progression`, loader call), `RtsAI::update`, `RtsScript*` (event names, command fields, API table), `RtsViewScript` (`view.progression_choose`), `SimTools` (MCP command), headless summary line, CMake | agent-df-progression-glue (MAIN merges) |
| Narrative and social scenes: typed facts, relationship meters, knowledge, deferred consequences, chapters, speakers, cinematic CueRequest events, barks, visual-novel backlog/skip/auto (`narrative-graphs` 0.2.0, record kinds 64..71) | `libraries/include/aiverselib/genre/Narrative{Scenes,Cues,SceneData,ScenePersistence,Configured}.h`, `skills/gameplay/aiverse-narrative-graphs`, `examples/libraries/genre/{story-social,narrative-settings}.json`, docs/genre-packages.md § Narrative scenes; additive hooks: `narrative` section of `libraries/spec/gameplay.json` (+ generated files), catalog entry, `GenreConsumer.cpp`, `tests/libraries/genre_tests.cpp` (`narrativeScenes`) | agent-narrative (MAIN merges) |
| Physical interactions: pick up/carry/throw with arc preview, push/pull, XPBD ropes, doors (locks/keys/one-way/auto-reverse), moving platforms with rider carry, breakables with debris requests (`physical-interactions` package, record kinds 80..87) | `libraries/include/aiverselib/genre/{PhysicalCommon,InteractionConfig,Carry,Push,Ropes,Doors,Platforms,Breakables,PhysicalInteractions,InteractionConfigured,InteractionData,InteractionPersistence}.h`, `skills/gameplay/aiverse-physical-interactions`, `examples/libraries/genre/{interactables,interaction-settings}.json`, docs/genre-packages.md § Physical interactions; additive hooks: `interaction` section of `libraries/spec/gameplay.json` (+ generated files), catalog entry (+ object-interactions cross-reference), `GenreConsumer.cpp`, `examples/native/libraries/CMakeLists.txt`, `tests/libraries/genre_tests.cpp` (`physicalInteractions`), skill search aliases | agent-interaction (MAIN merges) |
| Cinematic gameplay: time-scale domains (hit-stop/slow-mo), scene timelines, dialogue/animation sync waits with timeouts, QTE with accessibility, interaction staging + input locks, checkpoints around scenes, narrative cue triggers (`cinematic-gameplay` package, record kinds 72..79) | `libraries/include/aiverselib/genre/Cinematic*.h`, `skills/gameplay/aiverse-cinematic-gameplay`, `examples/libraries/genre/{scene,cinematic-settings}.json`, docs/genre-packages.md § Cinematic gameplay; additive hooks: `cinematic` section of `libraries/spec/gameplay.json` (+ generated files), catalog entry, `GenreConsumer.cpp` function, tests appended to `tests/libraries/genre_tests.cpp` (`cinematicGameplay`) | agent-cinematic (MAIN merges) |
| Puzzle solvers: match-3 (specials, cascades, solvable shuffles, hints), flood/connectivity, sliding/packing, Sokoban sessions + deadlocks/A* (grid-puzzles 0.2.0), small-grid CSP (sudoku/nonogram, uniqueness), puzzle bot (`puzzle-solvers` package, record kinds 112..119) | `libraries/include/aiverselib/genre/Puzzle*.h`, `libraries/include/aiverselib/bots/PuzzleBot.h`, deadlock/A* part of `libraries/include/aiverselib/strategy/Puzzles.h`, `skills/genres/aiverse-puzzle-solvers`, `examples/libraries/genre/puzzles`, `tests/libraries/genre_puzzles_tests.cpp`, `examples/native/libraries/PuzzlesConsumer.cpp`, docs/genre-packages.md § Puzzle solvers; additive hooks: catalog entries (+ grid-puzzles bump), `tests/libraries/CMakeLists.txt` block, library_demo SOURCES + one call in `GenreConsumer.cpp`, skill search aliases | agent-puzzles (MAIN merges) |
| Cards and turn strategy: card rules with rule variants, stack/priority/fizzling and hidden information (`card-games`, composes card-runtime zones), square/hex tactics with cover/flanking, line of fire, AP, initiative, overwatch and WeGo (`tactics-grid`), card evaluator + turn planner bots (`cards-turns-bots`); record kinds 96..103 | `libraries/include/aiverselib/genre/{CardGame,CardGameRunner,CardGameData,CardGameConfigured,CardGamePersistence,TacticsGrid,TacticsTurns,TacticsWeGo,TacticsData,TacticsConfigured,TacticsPersistence}.h`, `libraries/include/aiverselib/bots/{CardBot,TacticsBot}.h`, `skills/aiverse-{card-games,tactics-grid,cards-turns-bots}`, `examples/libraries/genre/cards-turns/`, `tests/libraries/genre_cards_turns_tests.cpp`, `examples/native/libraries/CardsTurnsConsumer.cpp`, docs/genre-packages.md § Cards and turn strategy; additive hooks: `cardGame`/`tactics` sections of `libraries/spec/gameplay.json` (+ generated files), catalog entries, `GenreConsumer.cpp` call, `examples/native/libraries/CMakeLists.txt`, `tests/libraries/CMakeLists.txt`, skill search aliases, cross-references in card-runtime/turn-scheduler skills | agent-cards-turns (MAIN merges) |
| Vehicles + sports: wheeled/tracked/hover/flight controllers, damage zones, seats, race checkpoints/positions, race bot (`vehicle-controllers`); ball possession/passes/shots/restarts/periods/fouls, formations, positioning bot (`sports-rules`); record kinds 88..95 | `libraries/include/aiverselib/genre/{Vehicle*,RaceCourse,Sports*}.h`, `libraries/include/aiverselib/bots/{RaceBot,SportsBot}.h`, `skills/aiverse-{vehicle-controllers,sports-rules}`, `examples/libraries/genre/vehicles-sports`, `tests/libraries/genre_vehicles_sports_tests.cpp`, `tests/libraries/vehicles_sports_example.cmake`, `examples/native/libraries/VehiclesSportsConsumer.cpp`, docs/genre-packages.md § Vehicles / § Sports; additive hooks: `vehicles` / `sports` sections of `libraries/spec/gameplay.json` (+ generated files), catalog entries, `tests/libraries/CMakeLists.txt` block, `GenreConsumer.cpp` call, library_demo SOURCES, skill aliases | agent-vehicles-sports (MAIN merges) |
| Settlement/production + survival/ecology: colonist needs/mood, schedules, job preferences + deterministic assignment with reservations, recipes/bills, hauling, build/gather orders, disaster alerts, power/fluid graphs with brownout shedding (`settlement-production`), body temperature/wetness/hunger/thirst/stamina, weather modifiers, population ecosystems, animal behavior, resource renewal (`survival-ecology`), colony planner bot; record kinds 104..111 | `libraries/include/aiverselib/genre/{Colony,ColonyTypes,ColonyRunner,ColonyPower,ColonyData,ColonyConfigured,ColonyPersistence,SettlementJson,Survival,Ecology,EcologyAnimals,EcologyResources,EcologyData,SurvivalConfigured,SurvivalPersistence,SurvivalEcology}.h`, `libraries/include/aiverselib/bots/ColonyPlanner*.h`, `skills/aiverse-{settlement-production,survival-ecology}`, `examples/libraries/genre/production-survival`, `tests/libraries/genre_production_survival_tests.cpp`, `examples/native/libraries/ProductionSurvivalConsumer.cpp`, docs/genre-packages.md § Settlement and production / Survival and ecology; additive hooks: `colony` + `survival` sections of `libraries/spec/gameplay.json` (+ generated files), catalog entries, `GenreConsumer.cpp` (one call), `examples/native/libraries/CMakeLists.txt`, `tests/libraries/CMakeLists.txt` (two CTests), skill search aliases, docs/bots.md row | agent-production-survival (MAIN merges) |
| Linear HDR scene pipeline | `engine/gfx`, `engine/render`, scene shader outputs in `engine/fx` and `modules/rts/view`, `tests/render`, `docs/hdr.md` | codex/linear-hdr (user assigned) |
| Static model auto-LOD, instancing and frustum culling | `tools/models/autolod.py`, `tools/models/lod_simplify.cpp`, `third_party/meshoptimizer`, model LOD hooks in `engine/render`, `tools/renderbench/reinstance.cpp`, `docs/autolod.md` | codex/sponza-full (user assigned) |
| Sponza full scene, model SSR, screen diffuse GI and temporal AA | `apps/renderbench`, `tools/renderbench`, `engine/render/ScreenLighting*`, model SSR hooks, glTF light metadata, `docs/sponza-full.md` | codex/sponza-full (user assigned) |
| Sponza rendering benchmark | `apps/renderbench`, `tools/renderbench`, `docs/render-benchmarks.md`; optional RHI GPU timings | sponza-benchmark (user assigned) |
| Extraction raids (Drone Fall port): rules / map `raid` block, match phases, extraction points (zones, cycles, called helicopters, channels, contest / interrupt rules), raid inventory over the item bags, hold-to-use interactables + loot tables, raid objectives, receipts for the meta, raid bot, Lua `raid` table, MCP `raid_*` + **Рейд** window, `templates/extraction` | `modules/rts/RtsRaid*.{h,cpp}`, `apps/editor/core/RaidOps.*`, `apps/editor/windows/RaidWindow.cpp`, `tests/tests_raid.inc`, `docs/raid.md`, `templates/extraction`, `extraction` lines in `tests/checksums/baseline.txt`; hooks: `RtsModule` (member, setup, "raid" system before "death", `kCmdRaid`, hash), `RtsSave` (append), `RtsData` (`Rules::raid`, loadRules call), `GameEvent` `kEvRaid*` + `kCmdRaid` (appended), `RtsAI::update` (raid players -> `raidBotThink`), `ScriptEngine::allowNamed`, `lua/victory.lua` (steps aside while a raid runs), Lua event names, headless `Raid receipt:` lines, `Workspace` registration | agent-extraction (MAIN merges) |
| SDK plugins for analytics / attribution / privacy: `analytics` + `privacy` services (fan-out, consent, iOS ATT), their UI Lua tables, plugins `gameanalytics` (Android, iOS, web), `facebook`, `appmetrica`, `att`, mocks `mock-analytics` / `mock-att`, aiversebuild plist escaping + config SKAdNetwork ids | `engine/services/{Analytics,Privacy,ServicesLua}.*`, `plugins/{gameanalytics,facebook,appmetrica,att}`, `tests/tests_services*.inc`, `docs/plugins.md` § Analytics / Privacy; hooks: `MockPlugins.*`, `Plugins.cpp` mock list, `Ads.cpp` (`privacy::attach`), `tools/aiversebuild/Plugins.cpp`, `ClientScript.cpp` (`openServicesLua`), CMake `aiverse_plugins` sources, CI isolation cases | agent-sdk-plugins (MAIN merges) |
| FPS library bots (`fps.bots`: bots-fps controller in the authoritative tick with loot / heal / zone goals, per-building navmeshes, stair flights), rotated map colliders (`yaw` -> 12 triangles, `engine/world/OrientedBox.h`), triangle SAT tolerance for zero-width plane axes | `modules/fps/FpsBots.cpp`, `engine/world/OrientedBox.h`, `engine/world/TriangleCollision.cpp`, `tests/fps/buildings.inc`; additive hooks: `FpsModule.{h,cpp}` (config, state, save, hash, bot branch, collider parse), `libraries/include/aiverselib/bots/Fps.h` (royale zone goal, bots-fps 0.1.4) | agent-br-mechanics (MAIN merges) |
| Procedural buildings (`building-gen` package, `systems/BuildingGen.h`): floors, doors, windows, partition, switchback stairs, loot spots; FPS map `buildings` (collider ids from `kBuildingColliderBase`) | `libraries/include/aiverselib/systems/BuildingGen.h`, `skills/world/aiverse-building-gen`, `tests/libraries/building_tests.cpp`, `modules/fps/FpsBuildings.cpp`, `tests/fps/buildings.inc`; additive hooks: catalog + alias, CMake test blocks, `FpsModule.{h,cpp}` (setup), `apps/fps/client.cpp` (draw generated boxes on chunked maps), `templates/fps_survival` map `buildings` | agent-br-mechanics (MAIN merges) |
| Tiered protective gear (`gear-tiers` package, `systems/Gear.h`): slots, zone absorption, wear, breaking, capacity, swaps; FPS `fps.gear` (loot type 200 + id, save version 9, own loadout in snapshots) | `libraries/include/aiverselib/systems/Gear.h`, `skills/gameplay/aiverse-gear-tiers`, `tests/libraries/gear_tests.cpp`, `tests/fps/gear.inc`; additive hooks: catalog entry + alias, CMake test blocks, `modules/fps` (config, damage, pickup, death drop, saves), `engine/net/authority/Session.*` (snapshot field, bag limit 24), `templates/fps_survival/rules/gear.json` + map loot | agent-br-mechanics (MAIN merges) |
| FPS typed vehicles over vehicle-controllers (`fps.vehicleTypes`, map vehicle `type`): controller on the collision ground height, wall response, crash damage, seats with passengers / gunners who shoot, ejection, save version 9 | `modules/fps/FpsVehicles.cpp`, `tests/fps/vehicles.inc`, docs/fps.md § Typed vehicles, `templates/fps_survival/rules/vehicles.json`; additive hooks in `FpsModule.{h,cpp}` (config, state, tick, save, hash, passenger fire), `FpsSystems.cpp` (Interact, legacy car skip, passenger buttons), FPS skill note in `skills/gameplay/aiverse-vehicle-controllers` | agent-br-mechanics (MAIN merges) |
| Snapshot delta compression (`snapshot-delta` package, `aiverse::lib::delta`): record sets, XOR runs against acknowledged baselines, per-peer sender / receiver history; FPS authority snapshots as deltas (protocol 5, `Server::snapshotStats`) | `libraries/include/aiverselib/net/SnapshotDelta.h`, `skills/network/aiverse-snapshot-delta`, `tests/libraries/delta_tests.cpp`; additive hooks: catalog entry + alias, CMake test blocks, `engine/net/authority/Session.{h,cpp}` (records, delta message, acks from input viewTick), FPS stress assertion in `tests/fps/tests.cpp` | agent-br-mechanics (MAIN merges) |
| Battle royale: phased safe zone, bombardment, supply drops, air insertion (carrier path, skydive, parachute), last-team-standing standings (knockdowns, revives, wipes, placements, kill feed, draws), pre-match lobby (`battle-royale` package in `genre/royale/`, record kinds 120..127); FPS `fps.royale` wiring | `libraries/include/aiverselib/genre/royale/*`, `skills/gameplay/aiverse-battle-royale`, `examples/libraries/genre/royale`, `tests/libraries/royale_tests.cpp`, docs/genre-packages.md § Battle royale; additive hooks: catalog entry + skill alias, CMake test blocks (root + tests/libraries), `tests/libraries/run.py` (package subfolders in the together check); FPS wiring in `modules/fps` (additive, see docs/fps.md § Battle royale); client HUD `apps/fps/RoyaleHud.h` (zone wall, minimap, timers, kill feed, results) + `fps.royale.*` keys in `engine/assets/lang/{en,ru}.json` | agent-br-mechanics (MAIN merges) |
| Boss encounters: telegraphed area attacks (circle/cone/line/ring, windup/active/recovery), slow/stun/burn statuses, boss phases / patterns / enrage / adds, encounter windows with personal bosses and rotation, mob wander/hunt/flee/leash/swarm (`boss-encounters` package, record kinds 112..119), headless reference fight `aiverse_encounter` | `libraries/include/aiverselib/genre/{Telegraphs,TelegraphDecals,StatusEffects,BossEncounter,BossSchedule,MobBehaviour,EncounterData,EncounterPersistence,EncounterScenario}.h`, `skills/gameplay/aiverse-boss-encounters`, `examples/libraries/genre/boss-encounter.json`, `tests/libraries/encounter_tests.cpp`, `tools/libraries/encounter_main.cpp`, `examples/native/libraries/EncounterConsumer.cpp`, docs/genre-packages.md § Boss encounters; additive hooks: catalog entries, `tools/skills/catalog.py` alias, CMake test/tool blocks (root + tests/libraries), library_demo source list + setup call | agent-bosses (MAIN merges) |
| Boss encounters on RTS units (Drone Fall port): rules `encounters` (attacks / bosses / mobs / schedule over the boss-encounters package), monster players (map `"monsters": true` + hostile creeps 255), telegraph strikes -> damage + rules statuses + boss immunity, ability-bound attacks, mob orders (attack / flow-field attack-move / move), personal boss windows, Lua `encounters` table + `boss_*` / `telegraph_*` events, telegraph decals + status rings, `templates/boss_arena` | `modules/rts/RtsEncounters*.{h,cpp}`, `modules/rts/view/EncounterDecals.*`, `tests/tests_encounters.inc`, `docs/encounters.md`, `templates/boss_arena`, `boss_arena` lines in `tests/checksums/baseline.txt`; hooks: `RtsModule.{h,cpp}` (include, `GameEvent` appended, member / accessor, setup, `encounters` system before `raid`, speed stat, hash), `RtsSave` (io), `RtsCombat::castAbility` (`onAbility`), `RtsAI::update` (monster players), `RtsScript.cpp` event names, `RtsScriptApi.cpp` / `RtsScriptInternal.h` (table + reference), `GameClient.cpp` (ground decals), CMake, `docs/game-format.md` § Boss encounters, genre-packages / skill note | agent-df-encounters-glue (MAIN merges) |
| Baked lighting: lightmap UV2 (glTF TEXCOORD_1 / FBX UV set 2, xatlas unwrap -> `.lmuv` sidecars), CPU path-traced lightmaps + light probes (BVH, threads, denoise, content hash), runtime lightmap / probe shader variants and mixed modes (indirect / shadowmask / subtractive), quality tiers, **Запекание света** window + MCP `lightbake_*`, `third_party/xatlas` | `engine/bake`, `engine/render/{BakedLighting,BakedShaders,LightmapUv,Renderer3DBaked}.*`, `apps/editor/core/LightBakeOps.*`, `apps/editor/windows/LightBakeWindow.cpp`, `apps/client/ClientBaked.cpp`, `tests/bake`, `docs/lightbake.md`; hooks: `Model.h` (`Primitive::lightmapUv`), glTF / FBX loaders + `loadModel` (sidecar), `Renderer3D.h` / `Renderer3DModels.cpp` (GI keys, `kDrawStatic`, capture), `ModelShaders.h` (`LIGHTMAP` / `PROBES`), `RtsTerrainShaders.h` / `RtsTerrainRender.cpp` (U3 + tex19/20), `RtsViewDraw.cpp` (static flag), `RtsLights*` (map light `bake`), `LightOps.cpp` (`bake` param), `GameClient` (load per map), `Workspace.cpp` registration | agent-lightbake (MAIN merges) |
| Ext-Client (Drone Fall) VFX port: VFX Graph converter, TrailRenderer / LineRenderer ribbons, effect survey, ME.BECS binding planner, binding options upright / clear | `tools/importers/unity_vfx/{vfxgraph,vfxgraph_model,lines,survey,becs_bindings}.py`, `tests/data/unity_vfx` (VfxGraph, Ribbons fixtures), `tests/tests_vfx_tools.inc`, `docs/particles.md` § VFX Graph / TrailRenderer / Surveys; hooks: `engine/fx/FxEmit.cpp` (line shape arc modes), `modules/rts/fx/RtsFx.*` (upright / clear), `apps/editor/core/FxOps.cpp` (fx_bind options) | agent-vfxgraph (MAIN merges) |
| gRPC + protobuf client: stdlib-only proto3 codegen (`tools/protoc`), wire runtime, HTTP/2 + HPACK, gRPC-Web + GrpcWebSocketBridge (cs-grpc-bridge-v1) transports, TLS over platform libraries (game backend adapters live in the game projects) | `engine/grpc`, `tools/protoc`, `tests/grpc`, `docs/grpc.md`; hook: `add_subdirectory(engine/grpc)` in the top CMakeLists | agent-grpc (MAIN merges) |
| Client kit: Spine 2D runtime + UI views, gamepads (cursor, snap, buttons / chords / game actions from game.json), social / push / links / consent services with SDK plugins (AppsFlyer, push, deep links, UMP, Xsolla, Steam social), UI.Windows audio bank importer, audio.json `chance`, trigger variable `gameplay_scale` | `engine/spine`, `tools/importers/{spine,fmod}`, `engine/services/{Social,Push,Links,Consent,ServiceProvider,MockClientPlugins}*`, `plugins/{appsflyer,push,deeplinks,ump,xsolla}`, `plugins/steam/src/SteamSocial.h`, `engine/platform/Gamepad.h` + `engine/platform/gamepad/*`, `apps/client/{ClientSpine,ClientServices,ClientGamepad,ClientGamepadHost}.*`, `tests/tests_clientkit.inc`, `docs/{spine,gamepad,uiwindows-audio-import}.md`; hooks: GameClient (members, init, frame, shutdown, Lua tables, pad actions), Plugins.cpp / MockPlugins mock list, PlatformAndroid gamepad hook, Lua `game.set_garrison` / `modes` / `player_abilities` / `set_ability_cooldown` / `type_info().upgrades` (RtsScriptApi.cpp), `Simulation::insertSystem` / `wrapSystems` + `RtsModule::gameplayClock`, `gfx::BlendMode::Screen` (GL + Metal), `Consent.cpp` -> `privacy::setConsent`, CMake blocks at the end | client-kit agent (MAIN merges) |
| Map environment (weather, wind, zones, trample fields, auto skills on splines, fog options) and gameplay hooks of native modules (speed, send filter, damage, death, assault, defeat, scene bodies, `kEvNative`, `mapData()`) | `modules/rts/RtsEnv*.{h,cpp}`, `modules/rts/RtsNative.*` (hooks), `tests/tests_env.inc`, `docs/game-format.md` § Map environment, `docs/native-modules.md` § Gameplay hooks; call sites: `RtsModule` (setup, systems, speed stat, hash, send check, `releaseUnit`), `RtsSave`, `RtsCombat`, `RtsSkills` / `RtsUnits`, `RtsModes`, `RtsRoutes`, `view/RtsView` | agent-bw-env (MAIN merges) |
| Generic hooks that came out of the Bunker Wars content port (the game, its importer and its tools live in the game's own repository, never here; tests that need the real game read `AIVERSE_BW_GAME_DIR` and skip without it): `playerMods.byCount`, `modePoint` lists | `playerModsByCount` in `RtsData.h` / `RtsOriginalsData.cpp` / `RtsOriginals.cpp`, `modePoint` list in `RtsModes.cpp`, `playerMods` / `modePoint` rows of `docs/game-format.md`, test `bw_player_mods_by_count_table` | agent-bw-data (MAIN merges) |
| Bunker Wars computer player: AI6 / Bishop / Ashley, per-skill casters, debuts, map bot triggers, difficulty profiles (rules `ai.controller: "bw"`) | `modules/rts/RtsBwAI*`, `tests/tests_bwai.inc`, `docs/game-format.md` § Bunker Wars computer player; hooks: `RtsAI` (holder, update hand-off, save state v4), `RtsData` (controller `bw`, validation), `RtsModule` (`bwAI_`, `bwTypeStat`, `kCmdSend` exact count param < 0) | agent-bw-ai (MAIN merges) |
| Box3D agent reference and physics workflow | `skills/gameplay/aiverse-physics`, physics catalog routing | codex/physics-skill (user assigned) |
| Authored routes / row groups / multiscreen: map `routes` (splines with parallel lines, blockers, screen groups), `lanes.groups` (rows paid per row, merge, close on capture), map `screen`, weapon `rangeAspect` (elliptical weapon range), route / screen MCP ops + Scene drawing | `modules/rts/RtsRoutes*.{h,cpp}`, `modules/rts/RtsScriptRoutes.cpp`, `apps/editor/core/RouteOps.*`, `tests/tests_routes.inc`, `docs/game-format.md` § Routes; hooks: `RtsModule` (setup / send / hash / members), `RtsSave`, `RtsLanes` (sysLanes branch, line speed scale), `RtsEconomy` (screen growth gate, groups, `tagReleased`), `RtsAI` (`canSend`), `RtsCombat` (`rangeAspect`), `RtsData` (keys), Lua `openRoutesApi`, `Workspace` registration, `Editor::drawEditOverlays3D`, client camera follows / frames the screen; stage 2: flying routes, tower coverage, walker offset + end fade (`SceneObject::fade` in `RtsView` / `RtsViewDraw` / `RtsSprites`), one screen API with the env map scroller (`RtsEnv*`, `RtsModes` inZone) | routes agent (MAIN merges) |
| City game (top-down, GTA-like) controls and presentation: touch aim assist, speed camera, camera on the driven car, steered wheels (`steer` procedural), per-vehicle handling data, radar / objective pointers in Lua | `modules/rts/view/TopDownControls.*` (aim assist, speed camera), `TopDown::aimCandidates`, `apps/client/ClientTopDown.cpp`, `ProcAnim::Steer` in `engine/render/Animation.*` + `RtsLook` preset + `SceneObject::steer`, `tools/citygen/{vehicle_handling,minimap_png,hud_atlas}.py`, `docs/topdown.md` § Aim assist / Speed camera, `docs/driving.md` § Client and handling data; rules for asset packs: § 3 Model packs | agent-gta2 (user assigned) |
| City traffic: computer-driven cars on lanes, buses with stops, emergency vehicles, signalised junctions + light props, islands / roundabouts, pedestrians on sidewalks / crossings, panic; incidents and services (ambulance, police, fire, tow); population with homes, jobs and shifts that commutes on foot, by car and by bus; bus lines; gangs of citizens that rob places; simulated only around the heroes (rules `traffic`, Lua `game.traffic_*`) | `libraries/include/aiverselib/genre/Traffic.h`, `modules/rts/RtsTraffic*`, `tests/tests_traffic.inc`, `docs/traffic.md`; hooks: `RtsModule` (setup / tick before the vehicles / "traffic" system after "death" / hash / member), `RtsSave` (after the vehicles), `RtsDriving` (`adopt`, bot input for driverless cars, parked cars at rest sleep), `RtsScriptApi` (`traffic_light`) | agent-traffic (MAIN merges) |
| Procedural decorative cables | `engine/cables`, `tests/cables`, `examples/cables`, `docs/cables.md`; minimal modeling/editor/CMake hooks | codex/cable-module (user assigned) |
| Contact-hardening sun shadows | `engine/render/{Shaders.h,Renderer3D*,ScreenLighting.cpp}`, `tests/render/ContactShadowScene.h`, shared HDR test fixture, `docs/game-format.md` | codex/contact-shadows (user assigned) |
| Third-party license audit and automatic game notices | `third_party/notices.json`, `platform/cmake/*Notices.cmake`, packaging hooks and `tests/notices` | codex/third-party-notices |
| Rules split over files and inheritance: rules.json `include`, entry `extends` / `abstract`, write-back of edits to their files, data hashes over every rules file | `engine/data/JsonCompose.*`, `tests/tests_rules_split.inc`, docs/game-format.md § Splitting rules.json; hooks: `loadRules` / `RtsModule::setup` (resolve extends), `hashGameData` (`engine/sim/Replay`), rules readers in client / Lobby / headless / server / battle / city / aiversebuild / i18n / fps `Project.h` / view (`RtsView`, `RtsTerrainRender`, `RtsGroundFx`), editor `Project` (`rulesOrigin`, `saveRules`, `rulesFiles`), `rules_get` (`resolved`, `files`), `game_file_write` guard, `lintGameData`, game wizard | main agent |
| Sensors and field operations for flow-economy wars: radar / sonar / jammers / stealth / underwater, radar targeting; repair, reclaim, assist, patrol work, self-destruct, stockpiles and interceptors, ambient wind / tide income, attack runs, repair pads; classic-game importer for FBI / TDF / TNT data and the HPI unpacker | `modules/rts/RtsSensors.*`, `modules/rts/RtsFieldOps.*`, `tests/tests_fieldops.inc`, `tools/importers/ta`, `tools/unpackers/hpi`, `skills/gameplay/rts-flow-war`, docs/game-format.md § Radar, sonar and jammers / § Field operations; hooks: `RtsModule` (setup, "fieldops" system after targeting, command, hash, accessors), `RtsSave` (io), `RtsEconomy` (ambient income, factory assist), `RtsUnits` (repair cost, `OrderType::Reclaim`), `RtsCombat` (radar targeting, underwater weapons, stockpile in `canCast` / `castAbility`), `RtsAI` (one radar structure), `RtsView` (blips), `SimTools` command names, client command card (repair / reclaim / self-destruct), CTest `ta_importer` | agent-mech-ta (MAIN merges) |
| Top-down / raid / encounter / progression simulation fixes, stage 3: squad followers fire while walking, per-map rules overrides (map `rules`: topDown tuning, raid, progression, encounters), encounter creeps skip the camp leash, mob `preferTags`, raid `defend` objectives + `failed` status, bot tuning (`topDown.bot`: orb range, kite thresholds, pickups, `pathCheck`), perk `fireCount` / `everyNth` / `pauseOnOffer` / per-type bullet variant count, attack `decal`, frozen outcome in saves (version 9), sparse spatial-hash queries, separation buffer | `modules/rts/RtsMapRules.{h,cpp}`, `tests/tests_maprules.inc`; additive hooks: `RtsModule.cpp` (setup merge, `gameplayClock` pause), `RtsCombat.cpp` (follower stop, shot count / volley), `RtsWorld.cpp` (`sysNeutrals`), `RtsEncounters.*`, `view/EncounterDecals.cpp`, `RtsRaid*` (+ `apps/editor/core/RaidOps.cpp` kind name), `RtsTopDown.*` (bot block, free-fire perks), `RtsRaidBot.cpp`, `RtsAI.cpp`, `RtsProgression*`, `RtsSave.cpp` + `engine/sim/Simulation.cpp` (save version 9), `RtsMap.h` (`SpatialHash::query`), `RtsMechCrowd.cpp`, tests in `tests_{topdown,raid,encounters,progression}.inc`, docs topdown / raid / encounters / progression / game-format | agent-df-engine-sim (MAIN merges) |
| Client / editor glue for single-hero games: editor play-test of single-player maps (`MapDoc::validate` accepts one real player against a raid, hostile creeps 255 / -1, monsters / waves sides, encounters or scripts; editor messages through `editorText`, `editor.*` keys of engine/assets/lang); top-down follow camera lens: `topDown.camera` `fov` / `projection` / `fogFollow`, clip planes and fog following the distance, any yaw, a map's `camera` block over `topDown.camera`; UI Lua `game.joinBattle(ticket, {name, compatKey})`: hosted battles by sealed ticket (`net::joinBattle`, `applyBattlePayload`, `battleCompatible`), outcome blob reporter, suspend / resume on phones, `battleFailed` event; editor `native_build` also builds the client (`aiverse_client`, `AIVERSE_CLIENT_BUNDLE_CONTENT=OFF`) so launch-client runs the project's C++ modules; project `native/` CMake configured after every engine target (`aiverse_native_configure()`), helpers `aiverse_native_link` / `aiverse_native_use_grpc`; UI Lua `plugins.on(name, fn)` / `plugins.off`; `game.start(i, {players, deck})` per-seat start data (`apps/client/StartPlayers.h`); view scripts read live `game.data` (`ScriptEngine::peekData`) | `apps/editor/core/MapDoc.*`, `engine/assets/lang/*.json` (`editor.map.*`), `modules/rts/view/TopDownControls.*` (camera), `apps/client/ClientTopDown.cpp`, `engine/net/BattleJoin.*`, `apps/client/Lobby.cpp` (battle), `apps/editor/core/NativeBuild*` (client target), `modules/rts/native/Build.cmake`, `apps/client/ClientServices.cpp` (plugins.on), `modules/rts/view/RtsViewScript.cpp` (game.data), `docs/topdown.md` § Lens, `docs/battle-net.md` § Game client, tests `mapdoc_validate_single_player_maps`, `topdown_camera_fov_far_yaw_ortho_and_map_override`, `battle_join_by_ticket_payload_and_compat`, `ui_lua_plugins_on_gets_one_plugins_events`, `game_start_players_merge_per_seat_data_into_the_map`, `view_scripts_read_live_game_data_of_neutral_props`, `tests/native_project_build.py` | agent-df-engine-client |
| Spline roads: road types (built-in library + rules `roadTypes`), map `roadNetwork` (Bezier edges, heights → bridges / tunnels, lane branches, bus stop bays), shared Fixed geometry, meshes / paint decals / props / signals, editor «Дороги» + MCP `road_*` | `modules/rts/RtsRoadNet.*`, `modules/rts/RtsRoadTypes.cpp`, `modules/rts/view/RtsRoad*`, `apps/editor/core/RoadOps.*`, `apps/editor/windows/RoadsWindow.cpp`, `docs/roads.md`; hooks: `RtsView` (member, init / draw / setRoadMap), `Editor::syncGrid`, `Workspace` registration, `SpriteBatch::drawMesh(decal)` | agent-roads (MAIN merges) |
| Replica: generic server-authoritative replication (schema, delta snapshots, interest, budget, input, interpolation, prediction helper) | `engine/net/replica`, `tests/replica`, `docs/replica.md`; additive CMake block `aiverse_net_replica` | agent-replica (MAIN merges) |
| Runtime physics scene + character controller for authoritative hosts | `engine/physics/{Scene,Character}.*`, `tests/physics/scene_tests.cpp`, `docs/physics.md` § Scene / Character | agent-replica-physics (MAIN merges) |
| Physics sandbox place on replica (first milestone): server / client logic, bots, SimNet load harness, graphical client, example scene | `apps/place`, `examples/place`, `tests/place`, `docs/replica.md` § First milestone; additive CMake block `aiverse_place` (after the physics demo), `place.*` keys in `engine/assets/lang/{en,ru}.json` | agent-place (MAIN merges) |
| Vulkan RHI backend (Windows, Linux, Android; opt-in, GL fallback): device, swapchain + offscreen back buffer, memory, versioned dynamic buffers, GLSL → SPIR-V at run time (vendored glslang), offscreen backend test | `engine/gfx/vulkan`, `third_party/glslang`, `third_party/vulkan`, `tests/render/vulkan_tests.cpp`, `docs/vulkan.md`; hooks: `Gfx.h` (`Backend::Vulkan`), `engine/platform/GraphicsSettings.h` + `AppConfig::graphics`, device choice in `PlatformLinux` / `PlatformWin32` / `PlatformAndroid`, client `--gfx` + game.json `graphics`, CMake `AIVERSE_VULKAN`, CI lavapipe step, notices | agent-vulkan (MAIN merges) |
| Other areas | as assigned by the user | the agent the user assigned |
| Bunker Wars energy skills: skill objects (`origins`/`consume`, `goal`, `onArrive`/`onExpire`/`onDeath`, `pull`), BW skill converter + modificator table | `modules/rts/RtsSkillObjects.cpp`, `tools/importers/bunkerwars/skills/`, `docs/bunkerwars-skills.md`, `tests/tests_skillobjects.inc`; hooks: `RtsEffects.cpp` (area / projectile spawn), `RtsUnits.cpp` (Enter), `RtsCombat.cpp` (deaths), `RtsSave.cpp` | agent-bw-skills (MAIN merges) |
| Voxel worlds: chunked mutable block world, block JSON, codec / regions / parking, ray and box queries (`voxel-world`), sky and block light (`voxel-light`), chunk meshes (`voxel-mesh`), chunk generator (`voxel-gen`), fluids / falling / growth (`voxel-ticks`), mob A* and spawning (`voxel-nav`), chunk streaming to clients (`voxel-replication`); save record kinds 120..127 | `libraries/include/aiverselib/systems/{voxel,voxellight,voxelmesh,voxelgen,voxelticks}/`, `libraries/include/aiverselib/bots/voxelnav/`, `libraries/include/aiverselib/net/voxel/`, `tests/libraries/voxel_tests.cpp`, `tools/libraries/voxel_main.cpp` (`aiverse_voxel`), `examples/libraries/voxel/`, `docs/voxel.md`, `skills/*/aiverse-voxel-*`; the voxel game: `modules/voxel/`, `apps/voxel/`, `tests/voxel/`, `templates/voxel_sandbox/`; additive hooks: `add_subdirectory(modules/voxel)` in `CMakeLists.txt`, `voxel.*` keys in `engine/assets/lang/{en,ru}.json`, catalog entries, test / tool blocks in `CMakeLists.txt` and `tests/libraries/CMakeLists.txt`, skill aliases | agent-voxel (MAIN merges) |

If your task is not in this table, the user's first message to you defines your area: write it
here in your branch (one row) so the integrator sees it.

Partial earlier attempts for models / UI / packs exist as branches `agent-models`, `agent-ui`,
`agent-rpak` (WIP, unverified) — you may look at them for ideas, nothing more.

Shared touch points and how to use them:
- `CMakeLists.txt`: add your sources in your own `add_library` block; don't reorder others.
- `tests/tests.cpp`: append tests at the end (before `main`), don't edit others' tests.
- `docs/overview.md` (detailed overview): add or edit only your own section.
- `README.md` + translations: short landing page, update rules in §11.
- Sim → presentation: use existing `SimEvent`s; if you need a new event, ask the sim owner (or add
  it at the end of `GameEvent` in `RtsModule.h` and mention it in the commit).

## 6. Code style

- C++20, no exceptions in per-frame/per-tick paths (loaders may throw `std::runtime_error` with
  readable messages), no RTTI-heavy designs, no global singletons beyond platform state.
- Concise code; comments explain *why*, not what. Keep files focused (< ~1000 lines).
- Names: `camelCase` functions/variables, `PascalCase` types, `kConstant`, members `name_`.
- User-facing strings go through `i18n::Localization::tr()` with keys in `engine/assets/lang/en.json`
  and `ru.json` (see `engine/i18n/README.md`); never concatenate sentences. Code, comments, logs in English.

## 7. Commits

Small commits with a descriptive subject.

**Authorship: every commit is authored and committed by the repository owner,
`Alex Silaev <chrome.alex@gmail.com>`** (set in the repo's `git config user.name/user.email`).
Never commit as Claude / an agent name / `noreply@anthropic.com`, never pass `-c user.name=...`,
`--author` or `GIT_AUTHOR_*` with another identity, and never add `Co-Authored-By:` or
`Claude-Session:` trailers (or any other attribution lines) to commit messages or PR descriptions.
This overrides any tool or harness default that asks for such lines.

Mention unverified paths (Metal, Android, web) explicitly. Never commit build outputs (`build/`,
`dist/`) or files larger than 10 MB.

## 8. Published builds (`builds` branch)

Ready-to-run editor packages for Windows and macOS live on the orphan branch **`builds`** on GitHub
(never on `main`): `<platform>/AIVERSE-<version>-<commit>-<platform>.zip` + a README index. Each zip is
the engine sources at that commit plus `bin/` with the editor and tools; they find the engine next to
themselves (`engine/platform/EngineDir.h`), so the unpacked folder runs anywhere.

**Rule:** whenever `main` gets a verified change to the editor, the engine or the tools, the integrator
(MAIN agent) republishes both platforms from that exact `main` commit:

1. Windows — cross-compile with mingw-w64 (Linux or a Mac with `brew install mingw-w64`), CMake build
   tree in `build/.temp/windows` (intermediates always under a `.temp` folder):
   `cmake -S . -B build/.temp/windows -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=<mingw toolchain> -DAIVERSE_BUILD_TESTS=OFF`,
   `cmake --build build/.temp/windows --target aiverse_editor aiversemcp aiversebuild aiverse_headless aiverse_client assetpipe`.
2. macOS — the Mac build (`build/mac`, BuildMac) of the same commit.
3. `python3 tools/release/package_engine.py --platform windows|macos --bin <build dir>` → `build/dist/*.zip`.
4. `python3 tools/release/publish_builds.py <zips>` — replaces the branch with one commit holding the newest
   5 versions per platform (force-push), so the repository does not grow with every build.
   It also writes `index.json` (platform, arch, url, size, sha256, aiversemcp path per zip) that other apps
   download from: keep its fields backward compatible (README § Готовые сборки движка).
   Linux is optional: `package_engine.py --platform linux --bin build/linux-release` (tools only is fine).

The version is `project(AIVERSE VERSION x.y.z)` in CMakeLists.txt + the short commit hash; bump the
version for releases worth naming. Only publish builds whose tests and template matches passed.
Other agents don't publish; they say in their report that a republish is due.

## 9. Reuse libraries before implementing algorithms

Before adding a reusable algorithm, read `libraries/catalog.json` and the matching
`skills/<category>/<skill-name>/SKILL.md`. These are repository instructions, not globally installed skills.
Use `python3 tools/libraries/catalog.py search <term>` to discover capabilities.
Prefer existing engine data/API, then a catalog package, then a minimal generic extension.
Never duplicate a catalog implementation in a game. Respect coverage: `engine-reference`
indexes existing functionality; `cpp-foundation` is a public C++20 header API, not the entire roadmap.
The full target scope and remaining work are in `docs/libraries.md`.
Pin package versions and content hashes in the project. Never silently overwrite modified packages.
Keep simulation, presentation and tools separate. Persist all gameplay state through the engine's
state mechanisms, use its RNG, and preserve deterministic iteration/tie-breaking.
A third-party adapter is not simulation-safe just because it accepts integers.
New dependencies need a pinned revision, approved license, provenance, platform checks and tests;
candidates mentioned in discussions are not installed or approved by implication.
When extending a package update its API, version, examples, tests, catalog and skill together.
Run `python3 tools/libraries/catalog.py check`, `python3 tests/libraries/run.py` and
`python3 tests/libraries/native_consumer.py`.
Integration changes to simulation still require the full tests and all template matches from §4.

Native game projects consume individual `AIVERSE::lib_<package>` targets after
`aiverse_native_module(...)`; include `<aiverselib/Name.h>`. This target must exist before project
native CMake code executes. Keep `libraries/` in aiversebuild source exports. No Lua dependency
or engine-source edit is required for a game to consume these headers. Do not store mutable
library objects as NativeModule members: persist their fields through NativeState instead.

### Where library code goes (`libraries/include/aiverselib`)

Pick the folder by what the code is, not by who wrote it or which game needs it first.

| Folder | Holds | Examples |
|---|---|---|
| root `*.h` | `cpp-foundation`: small generic algorithms with no gameplay meaning | `Graphs.h`, `Grid.h`, `Collections.h` |
| `runtime/` | Native-module infrastructure, not gameplay: memory bounds, work budgets, hot settings, traces, state diff, byte helpers | `Bounded.h`, `WorkBudget.h`, `NativeBytes.h` |
| `systems/` | Cross-genre gameplay building blocks any game can use | attributes, tags, loot, curves, reservations |
| `gameplay/` | Player-facing subsystems with native command adapters | abilities, camera, inventory, construction |
| `genre/<package>/` | Rules that define a kind of game: one subfolder per catalog package | `genre/melee/`, `genre/weapons/`, `genre/cards/` |
| `bots/` | Computer players and navigation, including genre bots | RTS/FPS controllers, navmesh |
| `app/` | Application layer outside the simulation: screens, session, lifecycle, balance tooling | `Screens.h`, `Session.h` |
| `net/` | Network admission and ticket policies | `Admission.h` |

Rules:
- **One package, one folder.** A new genre package gets `genre/<package>/` with all its files:
  the policy, its JSON reader (`Data.h`), generated settings adapter (`Configured.h`) and save
  records (`Persistence.h`). Name the folder after the catalog ID without the genre noun
  (`stealth-senses` → `genre/senses/`). Never add another flat file to `genre/` or `strategy/`.
- **No aggregate headers.** A shared `Persistence.h`, `Configured.h` or `Data.h` that includes
  several packages is forbidden: it makes every user compile and link all of them and forces
  fake catalog dependencies. Shared code may only live in `genre/common/` and must not include
  any package (record framing, kind/version constants, byte helpers).
- **Include only declared dependencies.** A package header may include another package only if
  that package is in its `packageDependencies`; add the dependency because the code needs it,
  never to make a check pass. Save record kinds stay unique across packages (see the area
  table for the ranges already taken).
- **Infrastructure is not a genre.** If code is about running native modules (budgets, memory,
  hot reload, tracing, serialization bytes) it goes to `runtime/`, even when one genre needs it
  first. If it is reusable by any game (tags, curves, loot), it goes to `systems/`.
- Namespaces follow the folder (`aiverse::lib::genre`, `aiverse::lib::runtime`, ...); a package
  subfolder does not add a namespace level.

Current state (legacy, being migrated): `genre/` holds 11 packages as 49 flat files, and
`strategy/` holds non-action genre kits (cards, turns, puzzles, rhythm, idle/offline
production, run and zone graphs, puzzle VM) — these are genre packages too and move to
`genre/<package>/`. The shared `genre/Persistence.h`, `genre/Configured.h`, `genre/Data.h`
and `strategy/Persistence.h` are aggregates to be split per package. When migrating, move
files with `git mv`, leave a one-line forwarding header at every old path so existing
`#include <aiverselib/genre/X.h>` keeps compiling, keep package IDs, CMake targets and
namespaces, then drop dependencies that only existed because of the aggregates and prove
isolation with `tests/skills/isolation.py`. Do not migrate a package while its owner has an
unmerged branch touching it.

## 10. Reuse meta-game policies before generating code

For shops, offers, crafting, currencies/inventory, quests, passes, daily rewards, energy,
inbox, leaderboards, chat and segmentation, start with `skills/meta/meta-game/SKILL.md`.
Load only the matching recipe (`skills/meta/meta-economy`, `skills/meta/meta-progression`,
`skills/meta/meta-social`) and relevant sections of `docs/meta-game.md`.
Use `aiverse::meta` from `engine/meta`; configure project-local JSON via
`tools/meta/scaffold.py` and validate with `aiverse_meta_validate`. Do not reimplement the
same algorithms in game scripts/native code. Native consumers select client or server policies;
registered object modules use `aiverse_native_use_meta(aiverse_native_<id> CLIENT)` or a selected kit target.
Meta clocks/network/storage stay outside deterministic simulation. Online authority,
receipt validation and durable CAS storage belong to the backend. Local social
policies are not an online service. Never describe an offline/mock service as deployed.
Reuse Store providers; do not double-grant alongside the existing Lua shop.
Policy changes must pass `tests/meta` and the external C++ consumer in `examples/meta`.



### Gameplay subsystem packages and project recipes

Before implementing controls, cameras, construction UX, abilities, inventory/equipment,
squad controls, feedback, tutorials, save migrations, diagnostics, content workflows or
acceptance gates, consult `docs/gameplay-packages.md` and the catalog's `cpp-subsystem` /
`tool-workflow` entries. Use `AIVERSE::gameplay_native` for native command adapters.
Existing engine mechanics remain authoritative: custom preview/helper state must not
become a second source of truth for built-in costs, cooldowns, inventories or navigation.
`libraries/spec/gameplay.json` owns settings; regenerate C++ types, schema and reference
with `python3 tools/libraries/generate_gameplay.py`. Never hand-edit generated outputs.
Use `tools/libraries/gameplay.py recipe` / `apply` for supported rules changes; apply
requires real engine validation and refuses stale plans. Read the recipe's checks and
supply game-specific definitions rather than fabricating assets or UI bindings.
Run generated-file checks, gameplay tools tests, C++ tests and external native/export
consumer tests. Presentation adapters require host UI/render wiring and visual QA before
claiming an in-game UX is complete. JSON backup tools do not replace engine binary saves.

### Application packages

For UI bindings/screens/themes, application lifecycle, match rewards, network session policy,
content dependencies, balance experiments, play scenarios, performance budgets, genre recipes
and change planning, read `docs/application-packages.md` and the matching catalog skill first.
Use `libraries/include/aiverselib/app` from project native/host code; never move presentation/session
state into simulation. Match rewards use `grantMatch` with the existing `aiverse::meta::Economy` and
its durable CAS store; no parallel wallet or receipt implementation. A session policy is not a
transport/backend; a genre recipe is not a complete game. Verify integration before claiming it.
RmlUi bindings must die before document roots. Localize all display labels, escape dynamic text,
respect modal input, and validate gameplay actions again on the authoritative command path.
Run app C++/CLI tests, native/export consumers and actual visual QA for client wiring.

### Complete meta-service packages

For friends/clans, instance collections/equipment, entitlements, tournament cohorts,
backend integration and meta screens, search `tools/meta/catalog.py` and read only the
matching `skills/meta-{clans,items,tournaments,backend,ui}/SKILL.md`.
Use `aiverse::meta::Service` from native host code; never run wall-clock/network policy
inside simulation ticks. Preserve authenticated Principal, atomic aggregate Commit,
operation IDs and authoritative payment/match inputs. Do not duplicate the wallet.
Generate only selected features (full only when requested), edit meta source JSON, then run tools/meta/compose.py
with the real C++ validator. Pin/verify with tools/meta/catalog.py. Run tests/meta and
the external examples/meta consumer. Browser screens are not automatic RmlUi bindings;
HTTP handler tests are not proof of live network/TLS or deployment. Document unverified
platforms and visual/network checks. Details: docs/meta-service.md.

LiveOps policies live under `extensions.liveops`: loot, albums, jobs, research, quests,
events, wars, asyncPvp, matchmaking, ratings, referrals, ads, chains, notifications, offline.
Use `tools/meta/catalog.py search INTENT`, then `describe PACKAGE` and only that recipe.
Do not load or regenerate the entire schema. Full preset includes examples for all groups.
Preserve host authority, trusted RewardDraw, provider-event uniqueness and aggregate Commit.
Glicko/Elo are host-only; queue rosters do not start servers and ad completion does not verify
provider signatures. Run `meta_liveops` and `meta_liveops_schema` with the existing meta tests.
Details and explicit policy limits: docs/meta-liveops.md.


### Native code generation and integration toolkit

For module scaffolding, typed NativeState/Command code, reference validation, hot settings,
bounded storage, deterministic work budgets, state diffs and library upgrade plans, consult
`docs/native-toolkit.md` and matching catalog skills before writing another implementation.
Generate native code into a new directory; review the diff and preserve custom handlers.
Generated command validation covers payload shape/ranges only: validate player, ownership,
resources and targets in the authoritative handler. Never bypass engine save VERSION checks.
Hot reload is for host settings; gameplay changes must be explicit replicated commands.
Operation budgets must be deterministic, with cursors/counters saved and hashed. Wall time
must never decide when gameplay computation commits. Diagnostic traces must identify sampled
fields/ticks; they do not replace the complete simulation checksum.
Use the opt-in library showcase for the integration chain; `--verify` is a headless acceptance
check, while `--auto` exercises the RmlUi action and captures the real rendered screen.
Do not report visual validation if the GUI could not start. Run toolkit_tools.py, runtime C++
tests, catalog checks and external native/export builds after toolkit changes.

### Reusable gameplay systems

Before adding attributes/modifiers, status effects, tags/conditions, interactions, custom reservations,
action sequences, loot/pity, progression curves, AI memory or decision explanations, consult
`docs/gameplay-systems.md` and the matching catalog entry. Built-in RtsModule stats/statuses,
Inventory.reserved, effects and commands remain authoritative; do not fork their mechanics.
Use systems/Persistence.h for custom state in NativeState and preserve stable IDs and tick-based
ordering. Feed only visibility-authorized observations to AI memory. Interaction previews do not
authorize mutations. Gather explanation facts from the actual calculation, never guessed formulas.
Sampling loot is not granting rewards; use the existing inventory/meta transaction owner.
Run pure systems tests, real engine systems tests, generated config checks, external native/export
builds and a native restore match after integration. Report platform/visual gaps explicitly.

### Genre bots and automatic navmesh

For RTS/FPS AI controllers and navigation meshes, read `docs/bots.md` and catalog bots-rts,
bots-fps, navmesh and bot-matches. Reuse RtsAI rather than duplicating RTS economy/tactics.
FPS controllers run on one authority and submit ordinary commands; disable the reference fallback
for their slots. RtsNative lifecycle is not an FPS host hook. Persist bot state with simulation.
Build regional navmeshes from authoritative collision and agent dimensions; reject stale geometry
and budget exhaustion. AABB raster meshes do not imply triangle/slope/off-mesh/streaming support.
Use generated mesh components for reachable patrol targets. Run actual motor detour and bot restore
checks, external native/export builds, and full engine/template checks for simulation changes.

### Cross-genre action and narrative policies

For character jump/motion policies, melee timing/combos, recoil/projectiles, rounds/capture and
story choices, read `docs/genre-packages.md` and matching catalog skills. Reuse built-in movement,
weapon/ammo, damage/status and victory owners where applicable; these are explicit custom policies.
Use generated settings adapters and stable localized narrative graph IDs. Persist typed state with
its immutable definition versions. Native OBJECT modules use aiverse_native_use_genre to propagate
backend dependencies. Test collision, repeated commands, invalid choices, overflow and restore.
Do not claim climbing, a complete fighting game, bomb rules or cinematic playback from these policies.
Scene flow, QTEs, time scales and staging use `cinematic-gameplay`; presentation stays a host that reports
progress by tick and never drives the timeline.

## Procedural climbing, spider gaits and surface locomotion

Before implementing procedural footsteps, wall/ceiling crawling or multi-leg IK movement,
read [Surface locomotion](docs/surface-locomotion.md) and [Animation graphs](docs/animation-graphs.md).
Reuse `engine/render/SurfaceLocomotion` / the `surfaceLocomotion` rig unit; do not create a
second per-game gait planner. Use `examples/animation/spider_showcase.py` to generate an
original articulated model and the executable floor/wall/ceiling example. Do not assume
that a humanoid model or a single-bone leg can satisfy the required two-bone chains.

- Configure 3..16 independent hip/knee/foot chains, bind-relative home/pole positions,
  clearance/probes, step timing, minSupport/maxSwing and optional FullBodyIK capsules.
- Drive declared velocity/adhesion/reset parameters through C++ or view Lua. Keep state
  per instance; call graph update once per frame. Evaluate must not advance gait twice.
- Stance anchors must remain fixed relative to their surface until a step/lost contact/reset.
  Check reach and support budgets; missing geometry must not fabricate support. Preserve
  residual diagnostics when goals or collision constraints cannot be satisfied.
- `AnimationEnvironment` queries are read-only. Use stable surface IDs and real host
  collision queries for exact geometry; default per-primitive oriented bounds are approximate. Surface
  locomotion accepts wall/ceiling normals; ordinary full-body foot contacts retain slope limits.
- This controller moves the presentation root, not the RTS authoritative entity. Gameplay
  climbing/navigation, networking, saves and gravity remain with their existing owner.
  Never put floating-point rig state into deterministic simulation or bypass Commands.
- Inner-corner floor/wall/ceiling transitions and adhesive support are implemented. Do not
  advertise outer-edge wrapping, automatic navigation, physical adhesion forces, falling,
  deformable-surface transport, CCD or self-collision as implemented by this controller.
- Put the controller last in the pose graph. Subsequent blends/traits may invalidate its
  contacts and status. Use full weight for locked feet; reset after teleport or full detachment.
- Verify `aiverse_animation_tests`, `aiverse_tests animation_`, and the generated spider in
  the editor/client. Report actual tested platforms; retain the existing C++/Lua/JSON schema.

Dynamic scene support: provide `AnimationEnvironment::surfaceTransform` using the same
stable IDs as raycast hits. `surfaceLocomotion` stores local stance/swing-target anchors,
transports its body with the support, and releases removed surfaces. Use followSurface=false
when the authoritative host already carries the actor. RtsView supplies per-primitive
oriented bounds and current rigid transforms (including entity_fx); custom physics queries
are required for exact triangles/deforming skins. Test translation, rotation, tilt, steps
on a moving support and removal. Use `spider_showcase.py ... --dynamic-tank` and the editor
`animation_runtime_status` / `rig_runtime_status` operations to verify the actual scene;
a static plane-only preview is not evidence of dynamic-object support.

## Meta generation with minimal context

All repository agent skills live in `skills/`. Resolve the exact skill path from
`libraries/catalog.json` or `tools/meta/catalog.py`; general engine skills use
`aiverse-*`, RTS-only navigation/spatial/generation/bots recipes keep `rts-*`.
Keep package IDs and lock identities stable when moving recipes. Do not recreate
skills under docs or load the entire catalog/schema to configure one subsystem.

For a complete or selected meta layer start with `skills/meta/meta-compose/SKILL.md`.
Use `tools/meta/kits.py plan/apply --features ...` (or `--preset full`), with the real
C++ validator. The generated `meta/AGENTS.md` lists only selected recipes. Read
`catalog.py describe PACKAGE --section PATH` for a narrow example. Configure source
JSON and recompose; keep the authoritative wallet, receipts and membership owner.
Native UI/network hosts reuse `engine/meta/Client.h` with a durable per-account outbox;
read `skills/meta/meta-client/SKILL.md`. The same operation survives a lost reply/restart.
Browser UI is selectable through `metadata.metaScreens`; it is not a native RmlUi
adapter. Validate kits, native linking, client retries and selected UI screens.


## City block authoring

For plazas, courtyards, planting beds and street furniture, read
`skills/world/aiverse-city-blocks/SKILL.md`. Resolve curb elevations from the road renderer's
units and support surfaces; keep soil inside planter walls. Project generators and assets
stay in the game's repository. Workflow ownership: city-blocks task; additive catalog routing.

## Adding and updating repository skills

- Write every skill in English: frontmatter `name`/`description` and the body. This applies to
  repository skills and to skills proposed to the user's account. Russian only in quoted UI strings.
- Store each recipe in `skills/<category>/<name>/SKILL.md`; match the frontmatter name to the
  directory. Categories (folder = category, `CATEGORIES` in `tools/skills/catalog.py`):
  `core` (math, data structures, config, codegen, saves, diagnostics), `gameplay` (mechanics
  packages), `genres` (genre rules: cards, tactics, sports, puzzles, colony, survival), `ai` (bots,
  perception, behaviour), `world` (maps, navigation, roads, geometry), `ui` (screens, input,
  camera, feedback), `visuals` (look, lighting, post effects, fx), `network`, `verification`
  (determinism, scenarios, balance measurement), `meta` (meta-game), `workflow` (MCP and
  repository processes). A new category needs a reason and a line here and in `CATEGORIES`.
  MCP / process recipes without a runtime library are registered in `WORKFLOWS` of
  `tools/skills/catalog.py`; run `python3 tools/skills/catalog.py generate` after any change and
  `python3 tools/skills/audit.py --strict` (referenced paths and MCP tool names must exist). A skill that
  needs behaviour the engine lacks ships that code with tests; recipe: `skills/workflow/aiverse-skill-authoring`
  (a code skill = library + tests registered in both CMake trees + catalog entry + SKILL.md, in one change). Use `aiverse-*` for shared gameplay, `rts-*` only for RTS-specific
  behavior, and `meta-*` for meta-game policies. Do not add recipes under `docs/`.
- Before adding a recipe, search the existing catalogs. Extend the existing owner
  instead of creating a duplicate algorithm or overlapping skill. Register gameplay
  packages in `libraries/catalog.json`, meta packages in `tools/meta/catalog.py`,
  and meta composition dependencies in `tools/meta/kits.py`. Keep IDs stable.
- State when to use the skill, its responsibility, public API/header, exact CMake
  target, dependencies, minimal working example and required validation. Link to
  detailed references; do not require loading unrelated recipes or whole schemas.
- Put the library code in the folder that matches what it is (§9 "Where library code goes");
  genre packages get their own `genre/<package>/` subfolder and no aggregate headers.
- Every new code-providing skill must have an independently selectable library
  target with only required dependencies. Header-only implementations may use
  INTERFACE targets; compiled optional libraries must not build by default.
  Workflow-only skills declare that they have no runtime library. An alias to an
  all-features library does not satisfy per-skill isolation. Existing aggregate
  targets are compatibility paths, not a pattern for new packages.
- Explicitly separate client, server and database work. Client builds link
  `AIVERSE::meta_client`; authoritative hosts link `AIVERSE::meta_server`. Native
  OBJECT modules use `aiverse_native_use_meta(target CLIENT|SERVER)`. The legacy
  `aiverse::meta` includes both sides. The server target is the aggregate/transaction core only; select policies with
  `aiverse_add_meta_server(game_meta MODULES ...)` and construct
  `Service(kits::game_meta(), config, state, commit, draw)`.
- Keep credentials, trusted grants, policy configuration, sessions and SQL on the
  server. Document schemas, migration ownership, compatibility, atomicity and
  restart behavior when persistence is involved. Never ship private DB contents.
- Change API, catalog/version, dependencies, recipe and examples together. Verify
  frontmatter and catalog links, run affected tests, build a standalone consumer
  and a real native consumer/export when linking changes. Verify that selecting
  one library does not build/link unrelated libraries (including transitive deps).
  Report actual coverage and unverified platforms; do not describe routing or
  isolation requirements as completed implementations without evidence.

## Skill orchestration and isolated builds

Start cross-subsystem tasks with `skills/workflow/aiverse-orchestrator/SKILL.md` and
`python3 tools/skills/catalog.py search INTENT`. Generate a plan with exact IDs and
`--side client|server|both`; load only selected recipes. `skills/catalog.json` is a
GENERATED responsibility/index map, not a replacement for the source catalogs.

- New code skills get distinct targets. Gameplay header libraries use
  `AIVERSE::lib_<package>` and `aiverse_native_use_libraries(target package ...)`.
  Existing-engine reference/workflow recipes explicitly declare runtime none.
- `engine/meta/modules.json` owns policy IDs, registration and selection dependencies.
  `aiverse_add_meta_server` uses that manifest; a selected kit references only its
  policies. No static registration or all-policy link is permitted on minimal paths.
  `aiverse::meta` and the legacy Service constructor explicitly select everything.
- Native OBJECT modules propagate the selected kit via
  `aiverse_native_use_meta(target game_meta)`; clients use CLIENT. The generated
  factory header must be used by the host. Missing-policy configuration fails closed.
- Register source catalog entries, update CMake/source/API/skill/examples together,
  then run `python3 tools/skills/catalog.py generate` and `check`. Update search
  aliases only for meaningful domain terms; unknown/ambiguous requests need explicit IDs.
- Run `python3 tests/skills/catalog_test.py`, `python3 tests/skills/isolation.py`,
  meta tests and external native/export consumers after selection/linking changes.
  Isolation checks cover default build, actual link commands and binary symbols;
  do not rely solely on dead stripping or a successful compile. Core engine libraries
  already required by a game remain its baseline, not optional skill packages.

## 11. README: keep it current when you ship a feature

`README.md` (English) is the project's front page and the source of truth; `README.ru.md`,
`README.zh-CN.md`, `README.ja.md`, `README.ko.md`, `README.es.md`, `README.pt-BR.md`,
`README.de.md` and `README.fr.md` are its translations. Detailed descriptions belong in
`docs/overview.md` and `docs/*.md`, not in the README.

**When to update.** In the same branch as the feature, if it changes what a user or an agent can do:
a new genre package or genre module, a new template, a new platform or store/ads plugin, a new tool,
editor window or MCP area, an importer (Unity, Godot, ...), or a change of counts quoted in the README
(templates, libraries, skills, MCP tools). Bug fixes, refactors and internal changes do not touch it.

**Where.** Find the matching place and change it there — never add a new section for one feature:
- genre mechanics → the «Any genre» table (one row per genre; add the package name to the row);
- templates → the «Ready-made templates» table; engine parts → the «What's inside» lists;
- importers / porting → «Coming from Unity»; tools and setup → «Quick start»;
- verified platforms → «Status» (only what was actually verified; never write "not run",
  "not verified" or "untested" there — unverified details go to `docs/overview.md`);
- a new document worth reading → one row in «Documentation».

**How.**
- Change `README.md` and **all eight translations in the same commit**. Translate the text
  properly; keep code, commands, identifiers, file names and product names untranslated.
  Translations keep exactly the structure of `README.md`: same sections, rows, order, links and images.
- Every claim must be backed by the repository (code, tests, docs). No future plans as features,
  no invented numbers. Prefer capabilities to raw timings ("armies of thousands of units" rather
  than "13 ms per order"): a millisecond figure without context reads as a cost.
- Keep it short: one line per item, no paragraphs about internals.
- Screenshots live in `docs/images/readme/` (PNG/JPEG, ≤ 1 MB each, real engine renders only).
- Run `python3 tools/readme/check.py` — it must report 0 problems (structure of every
  translation, language bar, broken links and images).

**Documentation languages.** `docs/<name>.md` is English and canonical. Translations live in
`docs/<lang>/<name>.md` (`ru`, `zh-CN`, `ja`, `ko`, `es`, `pt-BR`, `de`, `fr`) for the documents the
README points readers to: `overview`, `game-wizard`, `engine-import`, `genre-packages`. A README in a
language links to `docs/<lang>/<name>.md` when that translation exists, otherwise to the English file.
When you change one of these four English documents, update its translations in the same commit
(same sections in the same order; code, keys and identifiers untranslated). Other documents are
written and kept in English only.


## Rigid-body physics

For Box3D collisions, sensors, queries, joints, motors or detachable assemblies, read
`skills/gameplay/aiverse-physics/SKILL.md` before writing an adapter. Reuse the optional
`AIVERSE::physics` target; distinguish its fixed-manifest JSON wrapper from the direct
C API. Copy transient events before mutations, wake reactivated joints, and validate
real contacts. Physics needs an explicit host and never feeds float state into RTS
Fixed lockstep implicitly. Deterministic vehicle/interaction policies retain their
own authority. A standalone test or a model animation is not an in-client integration.

## Active editor-unification work (PR #7)

Before modifying editor Scene, Inspector, Hierarchy, Transform, ECS component adapters or rendering for RTS/FPS, **read `docs/editor-unification-handoff.md`**. It contains the target architecture, implementation status, acceptance gates and known risks. **Update its progress log after every implementation or verification stage in the same PR**, with commits, actual test results (or NOT RUN), blockers and next steps. Do not merge PR #7 without the documented validation gates.

## AIVerse InputFlow (formerly Enhanced Input) — mandatory stage-by-stage agent handoff

**Canonical name and native API:** AIVerse InputFlow / `<aiverselib/gameplay/InputFlow.h>` / `aiverse::lib::gameplay::inputflow::InputFlow`. Retain compatibility with `enhanced::InputSystem`, old project JSON and `AIVERSE::lib_input`.

For changes in `feature/aiv990_enhanced_input` / PR #20 (and any future
continuation of that subsystem), read
`docs/enhanced-input-agent-handoff.md` **before** editing code. After **EVERY
implementation or verification stage**, update that file **in the same branch
and commit series, before starting the next stage**. This is required even when
tests are deferred, CI is queued, or the work is only partially complete.

Every stage entry must record:

1. Stage number, date, goal and status (implemented / incomplete / blocked).
2. Public API, architecture decisions, invariants, backward compatibility,
   and **exact changed file paths**, plus the commit SHA when known.
3. What was actually checked, with reproducible commands and explicit
   PASS / FAIL / **NOT RUN** (never imply queued CI passed).
4. Bugs, known limitations, unresolved design questions and risks.
5. The next **concrete** task: files/functions to open, edits to make,
   acceptance conditions, and exact commands for the next agent.

Preserve earlier entries; do not replace the handoff with a short summary.
Keep `docs/enhanced-input.md` and
`skills/ui/aiverse-input/SKILL.md` in sync with public API changes.
Respect the user's current instruction to implement first and run/fix tests
in a subsequent phase; record **NOT RUN** where applicable. Keep PR #20
as Draft until verification is evidenced. All input and device state remains
presentation-side; authoritative gameplay receives validated Commands.

## Native ECS state registration (2026-11-18)
Native authoritative ECS components/resources MUST register during setup via `Simulation::registerPersistentComponent<T>` / `registerPersistentResource<T>` with stable namespaced ID and positive schema version, plus portable ADL `io(Archive&, T&)`. Save v10 is enabled only when needed; v9 projects remain unchanged. Version mismatches fail closed. See `docs/native-ecs-persistence.md` and `tests/ecs_native_persistence_test.cpp`. Verify native builds and rollback before production adoption.

### Native schema upgrades and transactional load (2026-11-18)
Register exact-version legacy component/resource readers only through `registerPersistentComponentMigration<T>` and `registerPersistentResourceMigration<T>`. Native ECS migration validates the ORIGINAL source field stream against the recorded checksum; never bypass checksums for an upgraded save. Do not hash registered ECS data twice in GameModule::hashState. Disk deserialize now restores Simulation-owned World/RNG/commands/players on invalid input; game-module private derived caches must use `addStateRestoredHook` and remain outside authoritative simulation state. See `docs/native-ecs-persistence.md` and `tests/ecs_native_persistence_test.cpp`. Native execution still requires CI proof.

## Unified test runner — mandatory for all agents (2026-11-18)

**One user-facing entry point:** `./run-tests.sh` at the repository root
(`docs/testing.md` is the contract). It configures/builds all desktop test
targets, runs every automatically discovered CTest, then all standalone suites
registered in `tests/extra-test-suites.txt`. `--quick` omits the expensive
standalone suites; `--filter` targets a CTest selection. Full reports go under
`build/all-tests/test-reports/`.

- Every **new test MUST be reached by `./run-tests.sh`**. Add an ordinary C++
  or Python test through CMake `add_test(NAME ... COMMAND ...)`, with compiled
  executable built by default CMake ALL targets. Never hard-code CTest test
  names into the runner or use `EXCLUDE_FROM_ALL` for a registered test
  without explicit prerequisite build wiring.
- If the test uses a separate toolchain, sanitizer, backend, benchmark or
  ON/OFF parity build, add one deterministic row to
  `tests/extra-test-suites.txt` (`name|bash|relative/script.sh|--flag`
  or `name|python3|relative/script.py|`). Do not use `eval` or pass
  untrusted shell expressions in registry arguments.
- Before claiming an implementation stage verified, run the appropriate
  `./run-tests.sh` mode, retain its `summary.txt` and logs and record
  actual PASS/FAIL/NOT RUN, exact tested Git SHA and test path in the
  stage handoff. A broken CMake build means **tests not run**, not a passed
  suite. No success claim from queued/cancelled CI workflows.
- The user only needs `./run-tests.sh`; never ask them to maintain separate
  command lists as agents add tests. Keep all tests self-contained inside
  this repository; platform-specific device/SDK smoke checks stay in CI.
- For runner-specific changes and failures, append
  `docs/agent-handoffs/unified-test-runner.md` **after every stage**, in
  addition to the active feature handoff and existing agent update rules.


## NEREID universal water framework (2026-11-18)

New opt-in, genre-independent ECS water capability: `AIVERSE::lib_nereid_water`
with `<aiverselib/water/NereidWaterSystems.h>`. It samples Fixed-point
water-body volumes for native shooter/strategy/vehicle/other hosts; it must not
be reimplemented in FPS or RTS. Existing editor `waterNetwork`, baked tiles,
shore geometry and RTS water paths are still authoritative for legacy games.
**NEREID stages 1–12 are opt-in:** ECS water sampling, polygon/river geometry, buoyancy force output, query index, legacy raster import, surface mesh, Renderer3D flag and reflection fallback, GPU residency, WorldScene manifest, per-body identity and read-only ECS presentation snapshots. GPU effects are presentation-only; no hydraulic solver or native FPS/RTS migration. **Native C++ builds, CTest, GL/Metal and runtime tests remain UNVERIFIED.**
Read `docs/nereid/ARCHITECTURE.md`, mandatory living
`docs/nereid/HANDOFF.md`, and
`skills/gameplay/aiverse-nereid-water/SKILL.md`.
After each NEREID implementation commit, append exact SHA, changes, API,
test status, limits and next step to its handoff as the next separate commit.


## Release-mode test assertion contract (2026-11-19)

Native ECS/C++ regression tests are routinely built in `Release` with `NDEBUG`. **Never rely on standard `assert(expr)` for a test requirement, especially if `expr` has side effects or creates fixtures**, because the expression disappears entirely in Release, yielding false-green tests and Clang unused-variable warnings. Use `#include "tests/AlwaysCheck.h"` and `AIVERSE_TEST_CHECK(expr)`, or an equivalent always-on check throwing an informative exception. Tests must enforce invariants identically in Debug and Release; don't use `(void)variable`, `[[maybe_unused]]`, `-Wno-unused-*`, or `#undef NDEBUG` as a substitute for real assertions. Source: `docs/nereid/stage-50-release-assertion-correctness.md`.

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

## PHYSIS — genre-independent environmental ECS (2026-11-18)

- **Canonical source:** `engine/physis/{Physis,PhysisClimate,PhysisEnvironment,PhysisDisasters,PhysisWind,PhysisGravity,PhysisStorm,PhysisForces,PhysisPresentation,PhysisIntegration}.h`. Native games include these headers and install PHYSIS into generic `aiverse::Simulation` through `physis::install`; do **not** reimplement weather/hazards in FPS, RTS, RPG, any genre, or in renderer-specific code.
- **Mandatory independence:** PHYSIS must have no dependency on any game-genre module or a render backend; use `World` ECS components/resources and `Fixed` arithmetic, fixed committed ticks, deterministic seeds and COW/rollback.
- **Presentation:** consuming Terra, Water, Foliage, VFX, Physics, Audio, editor and native gameplay may query read-only `EnvironmentalResponse` / `PresentationCell`; PHYSIS must work headless. Do not store visual handles in authoritative ECS.
- **System ordering:** one of `advance`, `advanceClimate`, `advanceEnvironment` or `advanceEnvironmentWithDisasters` per authoritative tick; do not advance all of them. `physis::install` registers a single system. Preserve world persistence schemas when changing components.
- **Docs/hand-off on EVERY stage:** `docs/agent-handoffs/physis-{core,climate,environment,disasters,integration}-2026-11-18.md` and `engine/physis/AGENTS.md`. Every follow-on implementation stage must commit code, then a separate handoff with exact branch/SHA, public API, changed files, actual test results, limits and specific next action.
- **Tests:** baseline CTests `physis_core`, `physis_climate`, `physis_environment`, `physis_disasters`, `physis_integration`, plus `physis_wind`, `physis_gravity`, `physis_storm`, `physis_forces`, `physis_presentation`, and opt-in `physis_box3d` (`AIVERSE_ENABLE_PHYSICS=ON`); run `./run-tests.sh --filter '^physis_'` as required by the unified test-runner policy above. These targets have been written and registered but **NOT RUN** in the connector-only development session; do not claim validation or merge before tests and review.
- **Remaining work:** PHYSIS-owned climate/wind/gravity/storm/force fields and host-facing presentation APIs and host-owned opt-in Terra/NEREID/VFX/Foliage/Physics/editor adapters (other agents own canonical water and terrain), spatial indexing, sparse hazards, streaming/LOD, deterministic partitions, network deltas, platform validation and physical-unit calibration. Follow the cross-agent ownership matrix below and `docs/physis-nereid-terraweave-integration.md`; do not duplicate Water or Terra systems.

## Cross-agent ownership: PHYSIS / NEREID / TerraWeave (2026-11-18)

**MANDATORY before weather, terrain, water, foliage, shoreline, hydrology or biome work:** read [`docs/physis-nereid-terraweave-integration.md`](docs/physis-nereid-terraweave-integration.md). Never implement the same capability in two systems. All three engine subsystems and their opt-in adapters are **genre independent**.

| Owner (agent / active feature branch) | Exclusive implementation scope | Shared consumption boundary |
|---|---|---|
| **PHYSIS agent** / `feature/physis-nature-simulation`, draft PR #25 | Atmospheric/climate ECS, rain/snow/wind, surface wetness/snowpack/ice, fire and quake events. `engine/physis/` | Publish read-only `physis::EnvironmentalResponse` / `PresentationCell`; no water body/river/ocean/shoreline solver or terrain meshes. |
| **Water agent (NEREID)** / `feature/nereid-water-core`, draft PR #23 | Generic ECS water bodies, water-surface and flow sampling, river/lake/ocean geometry, buoyancy, optional water mesh and water legacy adapters. `libraries/include/aiverselib/water/`, `docs/nereid/` | Export NEREID `WaterSample` / `WaterContactComponent` and stable water-body geometry for any terrain/render host; do not own PHYSIS climate or Terra terrain. |
| **Terra agent (TerraWeave)** / `feature/terraweave` | Heightfields, biome/soil and terrain geometry, foliage/scatter, scene/terrain adapters, ground/shore presentation. `engine/world/TerraWeave*.h` | Opt-in adapters consume NEREID water and PHYSIS environment; do not duplicate a mutable water solver or weather simulation in Terra core. |
| **Integration agent** / explicitly assigned at merge time | Coordinates, units, tick order, persistence, optional adapters, editor/host integration and cross-system tests | Independent composition target, **not** dependencies from PHYSIS/NEREID/Terra foundational headers into one another. |
| **Render/VFX agent** / separately assigned | Particles, precipitation visuals, wet/snow material layers, foliage deformation, water material effects | Presentation-only snapshots; never write authoritative PHYSIS/NEREID/Terra ECS state from GPU/render frames. |

**Source of truth:** Terra owns terrain height/biome baseline; PHYSIS owns dynamic atmospheric/weather/surface conditions; NEREID owns persistent water bodies, levels, flow and contacts. PHYSIS has an **experimental standalone `HazardCell::waterDepth` flood model**; for NEREID-managed worlds set `physis::InstallOptions::floodMode = physis::FloodMode::ExternalAuthority` to skip/clear PHYSIS local water while retaining fire and quake. The switch and tests have been authored but are **NOT RUN**; no NEREID integration or cross-module validation has been performed. NEREID water becomes authoritative when enabled. Keep legacy `waterNetwork`/baked maps unchanged until explicit migration and regression testing.

**Future Terra bridge:** read NEREID water masks/surface and PHYSIS wetness/snow/ice/wind to build Terra shoreline, vegetation exclusions and ground overlays. Terra uses signed centimetres, PHYSIS/NEREID `Fixed` metres, X/Z ground and Y vertical: checked deterministic conversion and negative-coordinate floor division are mandatory. PHYSIS `rain` is normalized intensity, **not metres of water**; only the Water owner defines a calibrated precipitation-to-water-volume policy. No mutual include dependencies, double authority, hidden state or genre switch.

**Cross-agent handoff after EVERY stage:** commit own changes only on own branch; document implementation SHA, API, changed files, actual PASS/FAIL/NOT RUN results, integration risks, and next step in a stage handoff. Cross-branch integration waits for a dedicated PR, native builds, tests (`physis_*`, `ecs_nereid_*`, `terraweave*`, legacy water/terrain), rollback, save parity and conflict review. Neither separate PR is automatically merged by this guidance.

## PHYSIS diagnostics / visualization boundary (2026-11-18)

- PHYSIS Stage 14 adds a read-only, bounded CPU diagnostic raster in `engine/physis/PhysisDiagnostics.h`, with `sampleDiagnosticRaster`, `DiagnosticLayer` and 16-bit `diagnosticNormalized`. This is reusable by any editor/game/server but is not an implemented Scene overlay or VFX. The API must not write ECS state, inspect render handles or mutate Box3D/Water/Terra.
- World state remains owned by PHYSIS fixed-point ECS; diagnostics sample only committed `World` state, with invalid/out-of-grid cells explicitly marked. `LocalFloodMeters` is PHYSIS's standalone flood, never canonical NEREID water.
- The added `physis_diagnostics` CTest is registered for `./run-tests.sh` and focused `.github/workflows/physis.yml`; it is **NOT RUN** until actual CI/native results are inspected. Handoff `docs/agent-handoffs/physis-diagnostics-2026-11-18.md`.

## PHYSIS Stage 15–16 — diagnostic fast path and snapshot broadphase (2026-11-18)

- `engine/physis/PhysisDiagnostics.h` reads scalar weather/surface/hazard layers **directly** from authoritative ECS cells. Only wind/gravity vector layers need a 3D field query. Old `sampleDiagnosticRaster` signatures and raw-output semantics remain.
- `engine/physis/PhysisSpatialIndex.h` is an **opt-in transient COW-snapshot index**, not persistent ECS state: X/Z buckets prune WindZone, WindShelter, GravityField and StormEvent candidates while keeping exact old Fixed evaluation policies, ECS visit order and priority tie-breaks. `sampleDiagnosticRasterIndexed` is available to editor/host consumers.
- Rebuild the derived index after committed simulation ticks, storm movement, mutations, load or rollback when current values are required. Never store it in `World` resources, serialize it, make rendering authoritative, or depend on Box3D/FPS/RTS/Water/Terra in PHYSIS core.
- Stage tests `physis_diagnostics` (14-layer old-vs-fast and indexed parity) and `physis_spatial_index` (negative positions, field parity, snapshot mutation, bounded global fallback) are registered for the unified CTest and focused Linux/macOS workflow. **NOT RUN**; no benchmark throughput or native success claim. Handoffs: `docs/agent-handoffs/physis-diagnostic-fastpath-2026-11-18.md`, `docs/agent-handoffs/physis-spatial-index-2026-11-18.md`.

## PHYSIS Stage 17 — editor diagnostics and separate preview World (2026-11-18)

- The PHYSIS agent owns `engine/physis/PhysisDebugPalette.h` (CPU-only colour conversion, not authoritative) and `apps/editor/windows/PhysisWindow.cpp` (new optional docked GUI, auto-registered by `AIVERSE_EDITOR_WINDOW`). This **does not** replace or refactor shared Inspector/Scene/Hierarchy from editor-unification PR #7. The window opens from the World menu and is hidden by default.
- The PHYSIS window may **read** the current `Editor::sim()->world()` if its `physis::State` exists, but must never step or modify that World or Box3D. When there is no PHYSIS live simulator, an explicit action creates an **isolated demo World** and any step buttons apply only to it.
- 2D heatmaps use `DiagnosticRaster`, `FieldSpatialIndex`, and `DebugPalette`. Wind/Storm/Gravity circles are visualization only, not colliders or physical objects. Do not duplicate NEREID water or Terra terrain. Avoid introducing FPS/RTS-specific debug paths.
- Tests `physis_editor_view` plus prior PHYSIS regression targets are registered with unified CTest and focused CI, but **NOT RUN**. Also build `aiverse_editor` and manually inspect Russian labels, cell picking, project/source switching and UI layout before claiming editor usability. Stage handoff: `docs/agent-handoffs/physis-editor-debug-view-2026-11-18.md`.

## PHYSIS Stage 18 / current main merge — 2026-11-18

- **Canonical feature branch** `feature/physis-nature-simulation` merged `main` commit `9ee6ff6fbf390bd45363bc92c838f492dfe92eee` via two-parent merge `810feba70f01cdc35003ac80ea8486f39dc16049`, preserving input work and PHYSIS/SpectraFX/VFX. `docs/agent-handoffs/physis-main-merge-2026-11-18.md` records the exact trees/conflict resolution. Native merged build/CTest have NOT RUN.
- **Scene debug overlay:** `apps/editor/PhysisSceneOverlay.h` draws opt-in transparent PHYSIS field tiles and Wind/Storm/Gravity rings in the *real* Scene viewport; `engine/physis/PhysisSceneGeometry.h` is pure Fixed-point CPU geometry, independent of editor/Box3D/genres. The Scene projection is presentation-only and never edits authoritative World.
- **Inspector authoring:** `apps/editor/PhysisAuthoring.h` validates map-level `"physis"` JSON and uses one `MapDoc::beginEdit()` per Apply for Undo/Redo. `apps/editor/windows/InspectorWindow.cpp` shows PHYSIS parameters under `Selection::MapProps` plus extended JSON; supports both generic map and FPS document layouts. Invalid edits must have no side effects.
- **Runtime opt-in:** `engine/physis/PhysisProjectConfig.h` parses map config into `InstallOptions`; any generic native `GameModule::setup` may call `physis::installFromMap(sim,map)`. Do not auto-attach to RTS/FPS genre modules or mutate live ECS when authoring. Authored per-volume arrays are stored but not auto-spawned until a separate stable-ID adapter exists.
- **Verification gates:** `physis_scene_geometry` and `physis_project_config` build under base tests, `physis_authoring` builds when `aiverse_editor_core` is enabled, Studio workflow compiles/executes the editor integration tests. Actual CMake/CTest, editor screenshots and live UI tests: **NOT RUN**. Handoff: `docs/agent-handoffs/physis-editor-scene-inspector-2026-11-18.md`. Preserve PHYSIS/NEREID/TerraWeave ownership, main branch input changes, and editor-unification PR #7 work.

## PHYSIS Stage 19 — authored nature fields, universal ECS spawn and editor gizmos (2026-11-18)

- **Genre-independent source of truth:** `engine/physis/PhysisFieldAssets.h` validates map-level `windZones`, `storms`, `gravityFields` with stable unique authored IDs and creates ordinary `WindZone`, `StormEvent`, `GravityField` components in sorted ID order. `engine/physis/PhysisProjectConfig.h::installFromMap` runs all validation before world initialization; explicit native `GameModule::setup` remains required. Never duplicate these calculations in FPS/RTS/RPG modules.
- **Editor owner:** `apps/editor/PhysisFieldEditor.h` + `PhysisAuthoring.h` list, create, edit, remove fields and provide opt-in W/S/G Scene markers. Click/drag is handled before normal selection ONLY when marker editing is enabled and the pointer hits a marker. No fake runtime ECS selection, live World edits or Box3D force changes. Selected authored field properties are shared with the universal MapProps Inspector.
- **Undo/redo rule:** field addition/deletion/property Apply uses `MapDoc::beginEdit` once. Scene drag starts a single snapshot on the first effective movement and calls `MapDoc::touched` on subsequent moves. All authoring, including FPS map JSON, must use the same strict parser and the same MapDoc history. Do not mutate game save ECS directly; there is no live hot-reload in this stage.
- **IDs/saves:** Stable ASCII `id` (1..64 chars) is a map authoring key, sorted at spawn; it is not itself a new persistent ECS schema. Nonempty field collections require matching `enableWindZones`, `enableStorms`, `enableGravityFields` flags. Use unchanged map + registered schema manifest for replay/load; no runtime field-ID lookup or multi-user edit merge claimed.
- **Water/Terra:** NEREID Water agent retains authoritative water; `FloodMode::ExternalAuthority` prevents duplicate PHYSIS shallow-water authority. TerraWeave agent owns foliage/landscape. Box3D adapter remains optional physics consumer.
- **Tests:** `physis_field_assets`, `physis_field_authoring` (when editor_core built), expanded `physis_project_config` native save fixture, CMake + Linux/macOS/Studio CI updates. **CMake, native C++, CTest and graphical drag testing NOT RUN**; don't claim success without logs. Next handoff `docs/agent-handoffs/physis-field-assets-gizmos-2026-11-18.md`.


### PHYSIS Stage 21 — group editing without runtime writes
- PHYSIS Scene W/S/G markers now support Ctrl/Shift multi-selection,
  group move, safe duplicate and atomic group deletion. Continue building
  genre-independent ECS components/systems; avoid genre-only variants.
- The shared authoring helpers moveFields/duplicateFields/removeFields use
  native field validation for one MapDoc Undo transaction per batch/drag,
  for both regular and FPS-verbatim JSON maps. Move a whole group by the
  same delta; never snap members individually or mutate live ECS World.
- Keep Scene/ImGui in apps/editor/PhysisFieldEditor.h. NEREID owns water,
  TerraWeave terrain/foliage, Box3D dynamics. No new saved ECS schema.
- See docs/agent-handoffs/physis-editor-group-authoring-2026-11-18.md.
  Source test cases added but C++ compilation/CTest/macOS GUI QA NOT RUN.
  PR #25 must stay draft until verified.


- PHYSIS Stage 21 extension: physis_authoring::transformFields performs one
  editor-only group rotation around global Y and uniform spatial scale
  about the active selected marker. It scales radii/storm extents but NOT
  wind velocity or force strengths. No new runtime authority or schema.
  See the Stage 21 handoff; GUI/CTest verification still pending.


### PHYSIS Stage 22 — advanced Scene field gizmos

- PHYSIS mouse controls in apps/editor/PhysisFieldEditor.h now include
  optional box selection, 5 pivot choices, world-axis X/Y/Z handles,
  Yaw-Y and Uniform Scale rings, and prior radius editor. Pure
  apps/editor/PhysisGizmoGeometry.h is ImGui/genre independent.
- Pivots are transient editor state. Group movement snaps the pivot and
  preserves relative offsets; ring drag calculates from original JSON
  via transformFields with baseline/firstEdit and a single Undo.
  Every result gets full native parser validation before MapDoc write.
- Never edit live ECS/Box3D/NEREID/TerraWeave state from scene authoring.
  Yaw changes field positions, not orientation or wind velocity. The
  current field types do not support unconstrained 3D quaternion rotation.
- Independent engine/physis/PhysisFieldChangePlan.h may exist from
  another agent; this is a planning helper only, not ECS hot reload.
- Stage handoff docs/agent-handoffs/physis-editor-marquee-3d-gizmos-2026-11-18.md.
  C++ fixtures authored but native compile/CTest/macOS visual QA NOT RUN;
  keep PHYSIS PR #25 in draft.


### PHYSIS Stage 23 — opt-in reconciler and command authority

- The new engine/physis/PhysisFieldRuntime.h provides generic,
  genre-neutral stable authored ID -> Entity mapping and an opt-in
  Simulation command-boundary reconciler based on FieldChangePlan.
  installEditableFromMap intentionally changes persistent schema
  (physis.field_runtime v1, Wind/Storm/Gravity pools); keep old
  installFromMap and existing save manifests untouched by default.
- Apply complete typed field edits through a host-selected
  GameModule::applyCommand type after server authorization and revision
  checking; never write live ECS from Scene. Stable IDs and fingerprints
  live in rollback-owned World resource; same-kind Update preserves Entity,
  retype Remove+Add, stale batches reject and no-op batches consume revision.
- Simulation Command wire supports <=4096 args, so PHYSIS caps snapshots
  at 32760 bytes. It does not provide automatic network permissions,
  atomic multi-packet chunking, Inspector-to-GameModule dispatch or
  GUI edit acknowledgement. Live climate/grid/flood policy migration
  remains unsupported.
- A new physis_field_runtime CTest target is registered in CMake and
  PHYSIS/Studio CI. Test SOURCE has been written but native C++/CTest/CI
  PASS and actual network/editor integration have not been verified.
- Stage handoff: docs/agent-handoffs/physis-field-runtime-reconciliation-2026-11-18.md.
  Keep PR #25 as DRAFT until genuine native validation.


### PHYSIS Stage 24 — opt-in editor submission and network authority

- apps/editor/PhysisLiveEdit.h holds the generic local capability and
  status tracker, apps/editor/windows/PhysisWindow.cpp the UI.
  GameModule must opt in, select its own command type and authorize
  LOCAL editor mutations explicitly. No PHYSIS World writes from Scene.
- Queued is not Applied. Read runtime revision plus authored field
  fingerprints, invalidate on superseding changes/rollback and visibly
  reject stale revisions or missing dispatch.
- engine/physis/PhysisFieldRuntime.h exposes server-side
  admitAuthorizedFieldEdit with a principal generated from the
  authenticated connection and a host ACL callback. Never promote
  untrusted player=0 commands into authority. Existing player input
  relay is not a PHYSIS admin endpoint.
- Network CommandSink stays unavailable for local editor submit; a
  remote host needs its own privileged transport, routing and ACK/NACK.
  No automatic FPS/RTS integration, climate/water/physics hot edits or
  default persistence schema changes.
- New physis_live_edit CTest + CI entries are authored but native
  compile/CTest/macOS UI/real network runs NOT VERIFIED.
  Handoff docs/agent-handoffs/physis-editor-live-edit-2026-11-18.md.


### PHYSIS Stage 25 — remote editor protocol, dedupe and reconnect

- Reuse net::Conn and versioned PHYSIS envelopes for remote editing;
  do not inject editing into normal player InputFrame or allow
  arbitrary client-issued player=0/system commands.
- ServerGateway takes a verified principal from an authenticated
  server connection and a host ACL; only server-owned commands
  execute at Simulation boundaries. Deduplicate exact requests
  by principal/instance/request ID, reject altered ID reuse,
  serialize admissions and distinguish Queued from Applied.
- Applied requires the right ECS revision and exact fingerprints,
  not a sent packet. RemoteClient preserves ID and bytes on retry,
  queries before resending after reconnect and rechecks old
  Applied receipts for rollback. Server dedupe cache is volatile,
  NOT an exactly-once journal across server process restarts.
- apps/editor/PhysisRemoteEdit.h accepts only a trusted attached
  net::Conn. No default anonymous PHYSIS admin listener or
  FPS/RTS auto-authority. A host must wire real auth, transport
  and durable journal separately.
- Another agent's RuntimeFieldRegistry / installTrackedFromMap is
  independent from the Stage 23 FieldRuntimeState pipeline.
  Keep one authority, do not install both. No NEREID/Terra/Box3D
  ownership changes.
- CTest physis_remote_edit and PHYSIS/Studio workflow targets
  added but native compile/CTest/real security QA NOT RUN.
  Handoff docs/agent-handoffs/physis-remote-edit-reliable-2026-11-19.md.


### PHYSIS Stage 26 — journal recovery and privileged local endpoint

- Canonical PHYSIS editor/runtime authority is Stage 23/24
  FieldRuntimeState + installEditableFromMap + GameModule::applyCommand
  dispatching applyFieldEditCommand. Do NOT install the parallel
  RuntimeFieldRegistry/FieldCommandHost simultaneously.
- engine/physis/PhysisRemoteJournal.h stores the admin request/receipt
  cache as bounded checksummed write-ahead snapshots. Persist INTENT
  before command enqueue and status after tick, fail closed if disk
  writes fail. On startup restore Simulation checkpoint first,
  validate/load journal second, open privileged listener third.
- engine/physis/PhysisAdminAuth.h provides challenge/proof with keyed
  BLAKE2b and secure OS random. PhysisAdminEndpoint.h binds a
  WsListener to loopback only, requires game-owned reserved command
  type, authenticated principal and ACL. Never trust client IDs
  or player=0 as an authority credential.
- Native net::connectWebSocket does not support WSS/TLS. External
  admin requires a real encrypted tunnel/transport, never public
  plaintext ws://. No automatic privileged listener is added to
  FPS/RTS modules or ordinary net match input.
- Sidecar file journal + separate World checkpoint is NOT one
  crash-atomic transaction; Unknown/Conflict after ambiguous
  restart is a correct result, not a false Applied. Further
  atomic checkpoint work required before exactly-once guarantees.
- Test target physis_admin_journal registered and fixture written;
  native build/CTest/macOS editor/actual remote TLS and security QA
  NOT RUN. Stage handoff
  docs/agent-handoffs/physis-admin-durable-journal-2026-11-19.md.


### PHYSIS Stage 26 hardening — queued receipt after process crash

- ServerGateway::restoreJournal must convert every persisted Queued
  receipt to Unknown: a server restart loses its old in-memory
  Simulation.pending commands. First restore matching World,
  then observe() to reconcile to Applied / Conflict from actual
  revision and field fingerprints. For unresolved intent permit
  only the identical original authenticated Submit under ACL
  and expectedRevision. Never fake a successful old receipt.
- New native source tests in physis_admin_journal cover crash
  before scheduled tick and crash after World commit but before
  receipt flush; BUILD/CTEST NOT RUN.
- FileJournal sidecar and Simulation save are NOT one atomic store.
  Keep exact limits/ownership and separate PHYSIS, NEREID,
  TerraWeave and Box3D. Handoff:
  docs/agent-handoffs/physis-stage26-queued-recovery-2026-11-19.md.


### PHYSIS Stage 26 queued receipt crash recovery
- The existing admin journal and authenticated loopback endpoint were already implemented in PHYSIS; do not duplicate them. `ServerGateway::restoreJournal` must treat persisted `Queued` as `Unknown` on process restart because the Simulation pending command may have vanished.
- Reconcile Unknown against the restored World; confirm Applied only for matching revision and typed-field fingerprints. Safe requeue requires the exact original authenticated request and still-current expected revision. Do not claim cross-file exactly-once durability.
- New source tests cover crashes before ECS commit and after ECS commit but before journal Applied flush. Native tests NOT RUN.
- Stage handoff: `docs/agent-handoffs/physis-stage26-queued-recovery-2026-11-19.md`.

## PHYSIS synchronized with canonical NEREID Water in main (2026-11-19)

The two-parent merge `7c617d7fbc1a7ff7529e84a8e7e281f5f3298f0b` imported `main` at `d71c6d47e532b25fb816a8bb1f96c41e5275a165` INTO
`feature/physis-nature-simulation`; **never merge PHYSIS into main
without separate review**. The merge preserved the genre-independent
PHYSIS ECS and imported canonical NEREID Water (components, surface,
buoyancy, currents, confluence, scene, renderer bridges and tests).
NEREID owns authoritative water bodies/flows/buoyancy; PHYSIS owns
weather, wind, gravity, storms, wetness/snow/ice and hazards. For a
native host using NEREID water authority, explicitly opt PHYSIS into
`FloodMode::ExternalAuthority` to avoid double shallow-flood simulation.
`engine/world/NereidPhysisWindBridge.h` is presentation-only; it never
makes PHYSIS and NEREID share mutable authoritative state.
Follow `docs/physis-nereid-terraweave-integration.md` and
`docs/agent-handoffs/physis-stage72-main-nereid-merge-2026-11-19.md`.
Previous PHYSIS/Box3D, Editor authoring and NEREID standalone test
results do NOT establish a PASS for the new combined merge: run a
new native cross-module compile/CTest and preserve the result.

## PHYSIS + NEREID post-merge native test checkpoint (2026-11-19)

User ran the PHYSIS branch's focused post-`main` merge CTest
(`^(physis_|ecs_nereid_|vfx_)`) and reported **40/40 PASS,
0 failures, 10.25s**. This verifies those native test targets after
the two-parent merge `7c617d7f`; it is **not** all-repo CTest or
runtime visual validation. The system authority contract is unchanged:
PHYSIS owns environment/weather; NEREID owns water bodies/flow
and buoyancy; TerraWeave owns terrain/foliage. No duplicate local
PHYSIS shallow flood with canonical NEREID: choose
`FloodMode::ExternalAuthority`. The existing PHYSIS-to-NEREID
wind adapter changes visuals only, not ECS water authority.
See `docs/agent-handoffs/physis-stage73-postmerge-40-green-2026-11-19.md`.

## PHYSIS full repository test follow-up: skill audit and Place performance (2026-11-19)

After the native post-main-merge focused suite passed 40/40,
the user's full 150-test macOS CTest had **148/150 PASS**.
`skill_audit` failed on one PHYSIS documentation reference
(`tests/physis_remote_edit_test.cpp::revisionProbeStopsOnUnhealthyJournal`
was treated as a nonexistent file), while `place` had **51/52
internal checks PASS** but missed a strict performance check:
server tick average **8.204 ms**, required **<5.0 ms**.
All Place convergence/correctness assertions reported in the log
passed; the measured sample had 523 physics bodies, 16 clients/4g
and a late joiner.

Source commit `a61923df653994a9a94c1916f4077db7fb7e9848`:
- In `skills/world/aiverse-physis/SKILL.md` separate the valid
  file path from the function name so `tools/skills/audit.py`
  no longer treats the combined `file.cpp::function` as a path.
- In `CMakeLists.txt`, make `place` CTest `RUN_SERIAL TRUE`.
  This schedules the wall-clock performance test exclusively,
  eliminating competition with parallel CTests as a confound.
  The **original <5.0ms** performance condition and full scenario
  are unchanged; no Place simulation/physics runtime was modified.

**Verification:** CMake 3.31 isolated fixture confirmed
`RUN_SERIAL=True` in `ctest --show-only=json-v1`.
GitHub source readback confirmed the updated skill reference.
**NOT RUN after these changes:** the actual macOS `skill_audit`,
`place`, complete 150-test suite. This is a scheduling mitigation,
not proof that Place meets 5ms on the user's machine. If it still
fails alone, capture Release/Debug build mode, CPU/Rosetta status
and isolated tick timing before changing performance budgets.
Handoff: `docs/agent-handoffs/physis-stage74-full-ctest-skill-place-2026-11-19.md`.

## Stage 75 — universal terminal build monitor + isolated Place triage (2026-11-19)

- New `tools/aiverse-progress.py` (code commit
  `d83716dcd835df40c7861828a92064e961a63bc6`): standard
  library Python 3.8+ CMake/CTest wrapper, animated ANSI terminal
  dashboard, CTest active names and elapsed duration, bounded recent
  output/warning counts, plain CI fallback with 15s heartbeat, preserved
  unabridged per-stage logs and `summary.json`, real exit codes.
- **Actually tested on a local isolated Linux CMake fixture**, not
  AIVERSE: configure/build/smoke CTest 2/2 PASS, deliberate no-tests
  and failing-test exit 8, 16s silent test printed `[alive]`
  heartbeat at 15s. `py_compile` PASS; GitHub blob SHA matches
  the exact locally tested file. macOS ANSI UI remains unverified.
- **User's Stage74 re-run**: `skill_audit` PASS (0.42s),
  `place` FAIL after 309.14s, tick avg **8.110ms** (threshold
  `<5.0ms`), worst 19.245ms; all other 51 Place internal
  checks passed. This is an isolated CTest run after
  `RUN_SERIAL TRUE`, so contention with other CTests is no
  longer an adequate explanation. Do NOT relax the threshold.
- Next: test `place` in a separate optimized Release build with
  the monitor and examine `CMAKE_BUILD_TYPE`, actual Mac architecture/
  Rosetta and then phase profile PlaceServer if needed.
- This stage changes tooling/docs only; no engine PHYSIS/NEREID,
  physics, network, ECS or genre API changes. PR #25 remains Draft.
- Guides: `docs/aiverse-build-monitor.md`,
  `docs/agent-handoffs/physis-stage75-progress-monitor-place-2026-11-19.md`.

## Universal AIVERSE Build Monitor warnings/errors (2026-11-19)

Tool `tools/aiverse-progress.py` now keeps a **pinned colored**
Warnings/Errors panel in interactive Terminal, a diagnostic count
for each configure/build/CTest stage and a complete timestamped
`diagnostics.log` plus `summary.json`. The display retains
recent compiler errors even if later warning lines arrive.
Warn/error counts are matching *output lines*, not distinct compiler
diagnostics. Warnings do not automatically cause build failure.
Python regression CTest `tool_aiverse_progress` is registered
when `AIVERSE_BUILD_TESTS` and Python interpreter are available.
Source commit `47a320624a118d3f5dd23a7e9d4c81d58e095676`.
Only the monitor/test CMake and docs changed, not engine runtime.
Local standalone Python unittest 3/3, standalone CMake CTest 1/1
and CMake warning/success-log integration passed. Actual
AIVERSE macOS Terminal UI is **NOT RUN**. Guide:
`docs/aiverse-build-monitor.md`; handoff:
`docs/agent-handoffs/physis-stage76-build-monitor-diagnostics-2026-11-19.md`.

## PHYSIS Stage 78 — optional per-entity Nature Contacts (2026-11-19)

New `engine/physis/PhysisNatureSampling.h` adds reusable
`NatureProbeComponent` (3D Fixed position/enabled),
`NatureContactComponent` (wind, gravity, atmospheric and surface
fields, fire/quake, validation and two tick stamps), and
`advanceNatureContacts(World&,uint32_t)`.
`InstallOptions::enableNatureContacts` defaults to **false**;
the optional components are persisted only when enabled and
`physis.nature_contacts` runs AFTER the climate system. No
historical save manifest or genre-dependent code is changed by default.

Separate `engine/world/PhysisNereidNatureBridge.h` combines
the published PHYSIS/NEREID read-only contacts on one ECS entity,
checking enabled state, matching tick stamps and probe positions
to avoid stale data. It neither advances water nor writes
NEREID bodies. Host must register both samplers; choose
`FloodMode::ExternalAuthority` if NEREID owns water.

**Code commits:** `354e0689fffcfc4ec9853307fc3a06ba32c86212`,
`a286022cb733c524f30b795501c957ee1ae3f9ec`.
**Verification:** source and test registered in CMake; combined native
`physis_nereid_contacts` test is WRITTEN, **NOT RUN** on macOS
or other full AIVERSE checkout. The earlier 40/40 focused
PHYSIS/NEREID/SpectraFX PASS predates these commits and must NOT
be cited as testing Nature Contacts. Review possible compile or
runtime errors in the new test rather than claiming readiness.

Handoff: `docs/agent-handoffs/physis-stage78-ecs-nature-contacts-2026-11-19.md`.

## PHYSIS Stage 79 — generic wind/fire/quake structural stress (2026-11-19)

PHYSIS now publishes opt-in, deterministic environmental structural
stress **without** duplicating the existing wind force solver. Use
`engine/physis/PhysisNatureStress.h`:
`NatureStressReceiverComponent` for resistance, lever arm, fatigue,
recovery, fire/quake sensitivity, and
`NatureStressOutputComponent` for aerodynamic force, bending moment,
normalized load, accumulated damage and the latched fracture transition.
`InstallOptions::enableNatureStress=true` requires both
`enableForces=true` and `enableNatureContacts=true`. Systems run
climate -> forces -> nature contacts -> nature stress. Source:
`21c016ede31107f48c50ecc5247cfc0cf98ae56a`.

PHYSIS calculates generic **damage potential** only. It never
creates Box3D bodies, uproots trees, rewrites TerraWeave vegetation,
transforms meshes or runs a duplicate water authority. The host
reacts to `newlyFractured` once per (entity, climate tick); the
host must implement its own idempotent transition to fallen
vegetation/rigid bodies. For large grass fields use ECS **cluster**
proxies rather than a component per blade. A dedicated CTest
`physis_nature_stress` and the earlier
`physis_nereid_contacts` test are authored but **NOT RUN** natively.
No new feature may be declared validated until actual macOS CTest.
See `docs/agent-handoffs/physis-stage79-nature-stress-2026-11-19.md`.

### PHYSIS Stage 80 — native macOS recovery after const World fix (2026-11-19)

- Initial user-native AppleClang compile FAILED in `tests/physis_nereid_nature_contacts_test.cpp:103–104` with two `no matching member function for call to 'get'` errors. Test bound `const auto& world=sim->world()`, while `World::get<T>(Entity)` in `engine/core/ECS.h` is non-const.
- Implementation commit `4d9c1adb94f88c7aefef58e3320afda9297e0b48`: replace two reads with `world.tryGetConst<T>(entity)`, check both pointers, copy their component values. Existing mutable test setup continues using `get<T>`. No runtime ECS, PHYSIS, NEREID, Box3D, save schema or renderer changes.
- **Post-fix user-reported macOS native monitor: `AIVERSE: PASS`, 00:00:20, 0 warnings, 0 errors**. Report: `build/physis-nereid-validation/aiverse-reports/20261119-181448-67558`. The pasted output contains no CTest per-test lines: exact number of tests completed must be retrieved from this report's `ctest.log`/`summary.json`.
- Prior combined PHYSIS/NEREID/SpectraFX 40/40 was on older source. Full current-branch repository-wide tests, interactive editor, Box3D/TerraWeave fracture handoff and CI are not verified. PR #25 stays Draft.
- Standalone handoff: `docs/agent-handoffs/physis-stage80-const-world-native-2026-11-19.md`.

### Stage 81 — Stage78/79 confirmed native macOS CTest 2/2 PASS (2026-11-19)

The user supplied the complete AIVERSE Build Monitor JSON and native
`ctest.log` from `build/physis-nereid-validation`,
report `20261119-181448-67558`, on source commit
`4d9c1adb94f88c7aefef58e3320afda9297e0b48`:
`configure` exit=0, `build` exit=0, `ctest` exit=0,
**completed=2, failures=0**, aggregate exit=0, 0 warnings,
0 diagnostic errors, 20.71 seconds for the full command.
Native CTest:
- `physis_nereid_contacts` (#123) PASS 0.85s.
- `physis_nature_stress` (#124) PASS 1.01s.
- 100% PASS 2/2, 0 failed; CTest wall time 1.02s.
This supersedes Stage80's note that the named test results were
not yet available. **It does not establish full AIVERSE suite or
TerraWeave/Box3D physical falling integration.**
No code change; documentation checkpoint only.
Handoff: `docs/agent-handoffs/physis-stage81-nature-tests-green-2026-11-19.md`.

### PHYSIS Stage 82 — persistent fracture event handoff (2026-11-19)

Implementation commit `2b0190cb93e4335e7e458fa03d75239ef65c8424`.
Added `engine/physis/PhysisFractureOutbox.h` defining
`NatureFractureOutboxComponent` with Empty/Pending/Acknowledged
states, a generation-safe Entity/tick event key and persistent
Archive serialization. `advanceNatureFractureOutbox(World&)`
publishes a current-tick newlyFractured stress event once;
`pendingNatureFractures(const World&)` returns sorted pending
messages, retained over later ticks/save, and
`acknowledgeNatureFracture(World&,Entity,key,tick)` rejects
wrong/duplicate acknowledgments.

`InstallOptions::enableFractureOutbox=false` by default.
If enabled, it requires nature stress (already requiring forces
and nature contacts), registers `physis.fracture_outbox` v1
persistence and adds system `physis.fracture_outbox` after
`physis.nature_stress`. Default game save manifests unchanged.
`tests/physis_fracture_outbox_test.cpp` registered as CTest
`physis_fracture_outbox` (PHYSIS/ECS/PERSISTENCE labels):
tests ordering, deterministic keys, pending/ack, native
save/load, rollback and COW.

TerraWeave `feature/terraweave` already owns
`PlantFallEvent`, `AnchoredPlantId` and
`FallenPlantPhysics::spawn` for Box3D. PHYSIS does not
import or duplicate TerraWeave: a game host must map Entity
to plant ID, perform/replicate physical consequences and
then acknowledge. External Box3D float state is not atomic
with ECS ack; host must deduplicate/resynchronize on rollback.
NEREID still owns water. Grass uses per-cluster probes.

Prior Stage81 user macOS 2/2 nature tests PASS confirmed;
Stage82 native compile/CTest **NOT RUN**. PR #25 remains
Draft. Handoff:
`docs/agent-handoffs/physis-stage82-fracture-outbox-2026-11-19.md`.


## TerraWeave procedural terrain/foliage (2026-11-18)

Universal world generation lives in `engine/world/TerraWeave.h` (namespace
`aiverse::terraweave`), independent of RTS/FPS and usable by native C++
project modules. Read `docs/terrain/TERRAWEAVE.md` and the
`skills/world/aiverse-terraweave/SKILL.md` before implementation.

- Preserve existing RTS terrain rendering, simulation and editor contracts.
  TerraWeave is an optional generic generator, **not** a new parallel terrain
  renderer. Explicitly convert its integer centimetres to old map units.
- Genre-independent components and systems: chunk/vegetation patch entities
  for cosmetic foliage; entities per individual tree only when gameplay needs
  them. Never allocate one ECS entity per grass blade.
- All authoritative generation must be deterministic, independent of chunk
  partition, job timing and input species order. Use stable seeds, integer or
  Fixed math and explicit generator-version migration for saved worlds.
- Generate chunks in pure jobs; ECS changes in deterministic commit order.
  Rendering uploads use the RHI. Shader changes need GLSL+MSL and quality tiers.
- **After every development stage update
  `docs/terrain/AGENT_HANDOFF.md`** with exact commits, API/files, tests
  actually run, limitations and the next step. No tests may be marked passing
  without recorded execution.


### TerraWeave agent ownership for current implementation phases (2026-11-18)

- **Feature/architecture agent** owns TerraWeave runtime functions, native
  APIs, opt-in WorldScene/editor integrations, and a handoff document after
  EVERY implementation stage. Do not make the engine depend on FPS or RTS.
- **Separate validation agent** owns build/CTest, the unified test runner,
  Metal/GL/Vulkan screenshot checks, race/sanitizer checks, FPS/RTS
  regressions and performance benchmarks for this feature. The feature
  agent should NOT duplicate that test work while the user requests
  functions first. This task split never permits claims that unexecuted
  tests passed. Record all tests as NOT RUN until the testing agent
  provides actual build/report evidence.
- New `HorizonSurfaceSources`, bounded memory cache, camera hysteresis,
  Horizon quality presets and `TWH8` prebaked atlas archive interfaces
  are documented in `docs/terrain/TERRAWEAVE.md`. Keep their generator
  versions/fingerprints consistent whenever the bake format changes.
  Don't introduce file-system writes or game-authority mutations
  into rendering/presentation code.

### TerraWeave Horizon terrain borders and offline atlas migration (2026-11-18)

- Horizon far-terrain grid world coordinates **must** use the globally
  aligned signed rational lattice in `engine/world/TerraWeaveHorizon.h`,
  not camera-local interpolation. Border normals use ghost-neighbor
  samples and match on overlapping regions if the height source matches.
- `terrainSkirtDepthCm` only adds outer downward geometry to existing
  terrain primitive/material 0; no additional backend, model or ECS.
- `TerraWeaveHorizonArchive.h` currently uses **TWH3** and invalidates
  TWH1/TWH2 assets. Re-run `aiverse_horizon_bake` for static game packs.
  Preserve current archive version/fingerprint semantics when extending
  Horizon geometry; never silently read old incompatible assets.
- After each functionality stage, append to
  `docs/terrain/AGENT_HANDOFF.md`. Another agent runs ALL
  compiler/CTest/platform-GPU validations; label them NOT RUN here.

### TerraWeave Horizon near/far terrain ownership (2026-11-18)

- The current Horizon archive is **TWH4**, not TWH3.
  It stores an optional authored near-ground ownership map,
  inner seam lip and stable skyline settings; all previous
  TWH1–TWH3 binaries must be regenerated. Maintain archive
  version/fingerprint whenever changing geometry layout.
- Do not remove far terrain merely because it lies
  within a near-camera radius if `WorldScene` exposes
  actual authored terrain coverage. Use immutable
  `HorizonSurfaceSources::ownsNearTerrainRect` and
  conservative `AuthoredWorldHeightSnapshot::coversRect`;
  absent near tiles must keep far ground visible.
- Inner near/far seam lips are strictly bounded to
  512 segments, share the original GPU Model/material 0,
  and do not create gameplay ECS data.
- Horizon outer grid snaps to a **nearest global lattice
  anchor** in coarse steps to stabilize mountain silhouette;
  forest radius must not extend beyond guaranteed far
  terrain coverage. Separate agent owns all build/CTest/
  Metal/GL/Vulkan screenshots and benchmarks.

### TerraWeave exact near-ground clipping / archive TWH5 (Stage 18)

- **Current archive:** `TerraWeaveHorizonArchive.h` is
  **TWH5**. Any preceding TWH1–TWH4 reference is
  historical; old binaries must be regenerated.
- Distant terrain overlapping a partial authored
  near-tile rectangle should be subtracted on the CPU
  using `planHorizonNearGroundClipping`, preserving
  original far-ground triangle plane and winding.
  No new RHI backend, material or draw per near tile.
- The immutable source snapshot supplies both
  `ownsNearTerrainRect` and `collectNearTerrainRects`.
  Input rectangles and output mesh triangles are
  strictly bounded; on cap overflow fall back to
  the conservative full coarse quad cutout rather
  than unbounded memory.
- `maxNearClipTriangles`: Minimal=0, Mobile=512,
  Balanced=2048, High=4096, configurable up to 8192.
  Accurate X/Z clipping is NOT proof of seamless
  vertical near/distant terrain heights.
- Update `docs/terrain/AGENT_HANDOFF.md` on each
  source stage with files/commits/actual tests.
  Testing is owned by a separate agent; do not
  claim compilation, screenshots or passing CTest
  without that agent's logs.

### TerraWeave Horizon TWH6 near-surface seam data (Stage 19)

- Current archive is **TWH6**; old TWH1–TWH5 blobs are historical
  and must be regenerated. The version/fingerprint changes when the
  baked seam sample layout changes.
- Authored-world near/far stitching must capture near-terrain heights
  from the **same immutable snapshot** as resident rectangle coverage.
  Run `populateHorizonNearSeamSamples` after `bakeHorizon`
  in both CPU worker and offline batch, BEFORE archiving/caching.
- Seam sampling uses signed world-centimetre coordinates with
  Q16 precision, no more than 4096 unique points, and an
  all-or-none fallback when required heights are missing.
  The source `nearGroundHeightAtCm` returns nullopt when
  near terrain is not resident. The graphics/RHI layer must
  not query live ECS/collision heights during submission.
- `makeHorizonModel` welds the boundary vertex Y into the
  existing material-0 triangles without adding a new model
  or draw call. Real GPU seam quality is still unverified;
  the separate testing agent owns CMake/CTest/driver captures.

### TerraWeave Horizon slope-safe near seam and TWH7 (Stage 20)

- **Current archive TWH7:** `TerraWeaveHorizonArchive.h`
  includes bounded near-weld slope/normal settings and the
  weld-fallback flag. TWH1–TWH6 are historical and must
  be rebaked with `aiverse_horizon_bake` or regenerated.
- `maxNearWeldSlopePermille=2500` (0=off) preflights
  all new welded clip triangle gradients. An unsafe weld
  is fully disabled for its region; precise X/Z clipping
  remains and `nearWeldSlopeFallback` is explicit.
  Do not misrepresent a missing weld as seam-free geometry.
- `nearSeamNormalBlendPercent=65` repairs shared clipped
  vertex normals with area-weighted geometric face normals
  during existing CPU Model construction. No changes
  to collision, near terrain ownership, GPU topology,
  materials or backend APIs are permitted for this stage.
- `HorizonRuntimeStats` exposes smoothed normal count and
  slope safety fallback through the GPU LRU. User assigns
  compilation, CTest, GPU screenshots and benchmarks
  to another dedicated testing agent; always report
  `NOT RUN` unless actual evidence is provided.

### TerraWeave TWH8 exact fractional near-normal seam contract

- **Current Horizon archive is TWH8**. Historical TWH1–TWH7
  blobs must be regenerated; exact Q16 near seam heights,
  signed near-surface gradient components and normal-valid
  flags are versioned and fingerprinted through the
  Horizon archive.
- `AuthoredWorldHeightSnapshot::sampleQ16` is an optional
  fractional-centimetre source contract used only by the
  CPU seam baker. `WorldScene` supplies
  `nearGroundHeightAtQ16Cm` from the SAME immutable
  near-ground snapshot as near tile ownership.
- On the near/far seam, use authored near-surface normals
  when available, otherwise the existing area-weighted
  far triangle normals. The `maxNearWeldSlopePermille`
  all-or-none seam welding guard remains active.
- `horizonCanReuseModelGeometry` must reject changes
  in Q16 seam positions, near height, slopes, normal
  validity and normal blend settings for **atlas-only**
  GPU patching; never leave stale vertex normals.
- `HorizonRuntimeStats::matchedAuthoredNearNormals`
  measures actual near-surface normal matches, not GPU
  performance. Separate testing agent performs all
  CMake/CTest/Metal/Vulkan/GL/benchmark validation.

### TerraWeave Stage36 host quality policy for Horizon GPU wind

Native projects may use `engine/world/TerraWeaveWindUploadPolicy.h`:
`HorizonWindUploadPolicy{maxBytesPerUpdate=2MiB,maxSparseRanges=8,
fullFallbackPercent=70}`. `WorldScene::setVegetationHorizonWindUploadPolicy`
forwards to Horizon's `setWindUploadPolicy`; validate strictly before
publication. Zero byte budget defers changed colour transfers; a too-low
budget may indefinitely defer dense fallback while the safe macro
shader remains. Combined active+outgoing GPU upload limit is per update,
not per model. The **2 MiB per-model CPU shadow remains fixed**; no
new GPU resources or TWH8 format changes. See
`docs/terrain/TERRAWEAVE.md` Stage36 and
`docs/terrain/AGENT_HANDOFF.md`. User prioritizes features before QA;
do not claim native/Metal/GL/Vulkan verification.

### TerraWeave Stage37 — safe inactive wind CPU shadow budget

Use `engine/world/TerraWeaveWindShadowCachePolicy.h` and public
`WorldScene::setVegetationHorizonWindShadowCachePolicy` for a
0..16MiB budget on **inactive GPU cache wind overlay capacity**.
It is separate from Stage36 `HorizonWindUploadPolicy` and does not
cap active/outgoing wind overlays. When cached shadow capacity
exceeds the policy, `HorizonGpuRegionCache` evicts the entire oldest
shadow-bearing **GPU model plus CPU overlay** via the RHI owning
thread; never detach the CPU shadow from a live GPU model whose
RGB bytes may already hold exact PHYSIS data. Re-entry rebuilds
via the existing Horizon worker/CPU bake/asset archive and uploads
a coherent new model/shadow, not a fabricated GPU readback.
`gpuCachedWindShadowBytes` and `gpuWindShadowBudgetEvictions` in
`HorizonRuntimeStats` expose current dormant capacity and specific
evictions. Update `docs/terrain/AGENT_HANDOFF.md` each stage;
CMake/CTest/Metal/Vulkan/GL **NOT RUN** under feature-first request.

### TerraWeave Stage38 — lossless GPU-cache CPU shadow packing

`HorizonWindShadowCachePolicy::compressCachedShadows` is opt-in and
defaults to false. Inactive Horizon GPU LRU entries may losslessly
compress their full `ModelVertex` wind CPU shadow with
`TerraWeaveHorizonWindCompression.h`. Never detach or compress live
active/outgoing shadows; never infer the GPU's exact PHYSIS RGB from
a procedural generator. On cache `take`, fully unpack and validate
the CPU shadow BEFORE the model becomes active or Stage35 sparse RHI
writes resume. A corrupt compressed stream retires the matching GPU
model and falls back to ordinary re-stream; failed allocation still
propagates. Count both raw and packed storage capacity toward Stage37
inactive CPU budget; do not claim a memory benefit unless measured.
Source/PHYSIS/TWH8 semantics, signed coordinates, shader graphs and
all genre-independent ECS ownership remain unchanged. QA is NOT RUN
under the user's feature-first order; update Stage38 handoff on branch.

### TerraWeave Stage39 — runtime Horizon presentation quality profiles

AIVERSE already has `HorizonSettings` geometry quality
`Minimal|Mobile|Balanced|High`. Stage39 adds an **independent**
`HorizonPresentationPolicy` from
`engine/world/TerraWeaveHorizonPresentationQuality.h` with
`BatterySaver|Mobile|Balanced|Desktop` presets. Compose existing
exact/procedural wind shader controls, Stage36 sparse GPU transfer
budget and Stage37/38 inactive wind CPU shadow/cache compression;
do not create parallel renderers, terrain bakes or PHYSIS solvers.
The *optional* manifest `terraweaveHorizon.presentationQuality`
(`battery-saver|mobile|balanced|desktop`) must not alter the
preexisting geometry `quality` key or TWH8 cache fingerprints.
No new field = Stage38 defaults. Native game code may set the whole
policy on `WorldScene` or `TerraWeaveHorizonScene` and override
individual components later. Validate all fields before mutation
and use scene-owner threading. Report effective settings (not
assumed named presets) and do not claim measured speed/battery
improvement; CMake/CTest/GPU QA remains **NOT RUN** until the
separate testing stage. Record the actual code SHAs and continuation
in the per-stage `docs/terrain/AGENT_HANDOFF.md`.

### TerraWeave Stage40 — host-fed adaptive Horizon runtime quality

`engine/world/TerraWeaveHorizonAdaptiveQuality.h` defines an
opt-in, strictly bounded controller over Stage39
BatterySaver/Mobile/Balanced/Desktop **presentation** policies.
The user requires reusable genre-independent runtime mechanics:
do not modify Horizon terrain/forest geometry, TWH8 archives, GPU
buffer types, PHYSIS source ownership or ECS simulation to adapt.
A native host must feed actual CPU and/or completed GPU frame times
in **microseconds** with monotonically increasing frame IDs via
`WorldScene::submitVegetationHorizonFrameTiming`. Zero denotes
unavailable measurements, not a fabricated 0ms frame.
`gfx::Device::gpuFrameMs()` may be zero/delayed; always respect
the actual backend timing availability and sample ordering.
The backend-free policy requires sustained threshold violations
(30 downgrade frames / 240 upgrade frames by default), 240-frame
cooldown, and one-level bounded transitions; gaps and invalid
samples cannot drive switches. Manual Stage39 policy/preset changes
must disable adaptation, environmental/PHYSIS inputs do not.
`terraweaveHorizon.adaptiveQuality.enabled` explicitly opts in
at startup but does not instrument the host's frame.
Keep source-only unverified stages marked **NOT RUN**, document
exact code commits/risks/next step in
`docs/terrain/AGENT_HANDOFF.md`, keep PR #24 Draft until
a separate tester compiles and profiles real platforms.

### TerraWeave Stage41 — native client frame timing ownership

AIVERSE already exposes asynchronous tagged `gfx::Device::timeGpuFrame`
and `takeGpuTimings`. Stage41's
`engine/world/TerraWeaveHorizonFrameTimingBridge.h` connects real
measured CPU callback durations plus asynchronous tagged GPU samples
to Stage40's optional adaptive `WorldScene` controller. The bridge
is included in both `apps/client/GameClient` (platform begins/ends
the RTS frame) and `apps/fps/client.cpp` (FPS explicitly begins/ends
its device frame). Do not add duplicate `beginFrame/endFrame` calls,
use simulation `dt` as measured CPU/GPU time, or treat
`Device::gpuFrameMs()` from an old completed frame as a sample for
the current one. Tagged GPU completions are matched to original
frame IDs; unsupported/late results use bounded CPU-only fallback.
The `takeGpuTimings` API destructively drains **all** ready GPU tags,
so a future app using multiple timing subsystems needs one shared
device timing collector; do not silently race two consumers.
No frame query when `adaptiveQuality.enabled` is false; no
simulation, PHYSIS, terrain geometry, TWH8 or shader change.
Build/tests/native Metal/GL/Vulkan timings **NOT RUN** until the
separate agent performs QA. Continue updating the Stage41 handoff
in `docs/terrain/AGENT_HANDOFF.md`.

### AIVERSE Stage42 — one GPU timing collector per graphics Device

The shared RHI now owns `gfx::Device::gpuTimingRouter()`
(`engine/gfx/GpuTimingRouter.h`). Every new GPU timing client
(TerraWeave, Prisma, SpectraFX/VFX, editor/tools/benchmarks)
MUST use `subscribe`, `request`, `poll`, `take(subscriber)`,
`cancel` and `unsubscribe`, rather than directly draining
`Device::takeGpuTimings()` in parallel. The backend API still
exists but its completed-result drain is destructive across ALL
tags; the router alone should poll it for shared devices. Router
messages restore the original caller's tag and are isolated in
bounded per-subscriber queues; rejected requests retain each
caller's CPU-only/unsupported fallback. Owner-thread only;
never wait for GPU fences, invent timestamps, or claim separate
per-feature GPU costs from a shared Metal command buffer.
Existing native Stage41 RTS/FPS, editor batching benchmark,
RenderBench, GeometryBench and instancing preview were moved
to the generic router. Full API/limits:
`docs/render/GPU_TIMING_ROUTER.md`. No changes to PHYSIS,
geometry, archive TWH8, gameplay ECS or network authority.
Source committed; native CMake/CTest/Metal/GL/Vulkan validation
**NOT RUN** under feature-first instruction. Append exact
per-stage handoff `docs/terrain/AGENT_HANDOFF.md`.

## TerraWeave + NEREID shared ecology ownership (2026-11-19, Stages 53–56)

- User assigned further water/shoreline erosion development to the **TerraWeave integration agent**. NEREID remains the single owner of ECS water bodies, flow, waves/currents and water contact. TerraWeave owns changed terrain, dig/fill, soil materials, root/plant support/falling and grass aggregate coverage; no independent second water physics.
- Shared deterministic ECS workflow: register `registerNativeWaterPipeline(...enableBankErosion=true)`, `registerTerraWeaveBankVertexPersistence` and `registerTerraWeaveAuthorityResource`, then add `addTerraWeaveAuthoritativeBankSystem` **after** NEREID's water bank system. Never calculate `erodeShore` a second time from ECS results, and never have standalone `NereidEarthworksBridge::step` mutate the same bank vertices. The canonical rollback/save state is `TerraWeaveAuthorityResource` schema v2; graphics/RHI, NavMesh and Box3D are derived and rebuilt at the owner-thread barrier.
- `TerraWeavePersistence.h` is an optional snapshot codec, `TerraWeaveReplication.h` is compact tick/revision-guarded per-change transport-neutral data, and `TerraWeaveEcsAuthority.h` is the actual Simulation World resource writer. Read **all** handoffs `docs/terrain/stage-53-persistence-ecs-handoff.md` through `stage-56-native-example-handoff.md` and `examples/world/terraweave_native/README.md` before touching these APIs.
- After an ECS tick/rollback: use `NativeTerraWeaveWorldBinding` + `publishNativeTerraWeaveFrame` to publish pinned physics terrain, rebuild existing `nav::Mesh`, near/far forest and bounded grass GPU models. Trigger falling tree/capsule Box3D only from committed server events; replicate server poses, never claim cross-platform float Box3D lockstep. Fallen-tree visibility is separate from harvested tree removal; grass does not have one entity per blade.
- **Status:** Source committed but Stage 44+ ecology/erosion/native additions have NOT passed new builds/tests or visual QA. The older 9/9 TerraWeave CPU and 1/1 GPU timing router tests do not prove new features. Draft PR #24 should not be merged to main until separate native QA, collision/NavMesh, NEREID replay, Box3D, GPU/Metal and full regression tests are green. No volumetric caves/overhangs yet; heightfield digging is not voxel excavation. Preserve per-stage handoff documents, tested/not-tested status, exact commits and next actions.


## TerraWeave Stage57–59 shoreline auto-discovery and sediment ownership (2026-11-19)

- `engine/world/TerraWeaveShorelineRaster.h`: discover bounded wet/dry bank and optional interior wet-bed grid cells using REAL `lib::nereid::WaterQueryIndex` over canonical water ECS bodies with polygons/river splines; classify soil via authored callback/optional biome helper, wave splash, slope, root reinforcement. `refreshAutoShorelineBanks` creates/persists auto-tagged ECS bank entities only at a structural barrier. It MUST NOT overwrite manually authored bank ECS entities, must retain suspended sediment when water retreats and must reject stale Earthworks ground heights. Register `terraweave.auto_bank` persistence during setup.
- `libraries/include/aiverselib/water/NereidSedimentTransport.h` performs **mass-conserving**, bounded, integer-only transport of suspended sediment across neighbouring same-water-body cells, or across an EXPLICIT NEREID-confluence/cascade directed inlet→outlet link. Micro-cm carry persists so low Fixed currents eventually move small amounts. Sediment transport itself never edits terrain height; it is fed into existing NEREID `erodeShore` deposition/erosion on following ticks.
- `engine/world/TerraWeaveSedimentEcs.h`: register `nereid.bank_sediment_transport` ECS persistence once; order native systems: NEREID sample → local bank erosion → sediment transport → TerraWeave canonical ECS Earthworks commit. The ECS transport stage validates confluence mouth geometry and cascade linkage, uses existing `WaterContactComponent.sample.flow`, and does not create another water solver or rerun erosion.
- `examples/world/terraweave_native/{Integration.h,README.md}` demonstrates optional registration, bounded autoregistration, submerged bed carriers and soil material callbacks. **Do not** scan the full coast every graphics frame; rescan at map start or topology edits on an ECS structural barrier. Grass still uses aggregate patches, not per-blade entities.
- **COW rollback correctness:** `World::prepareExistingWrite<T>()` alone does not detach shared pool ownership. Use `World::pool<T>()` before direct writable ECS component access; the pool then detaches individual pages. Fixed related NEREID sampling/erosion and auto-bank/sediment ECS authorship in these stages. Preserve this contract in further code.
- Read `docs/terrain/stage-57-auto-shoreline-handoff.md`, `stage-58-sediment-transport-handoff.md`, `stage-59-riverbed-delta-handoff.md`. New Stage57–59 code is **NOT compiled or tested yet**; old TerraWeave CPU 9/9 and GPU timing router 1/1 successes predate this work. Keep PR #24 Draft. Remaining: native ECS/gameplay/Metal QA, automatic rescan scheduling, downstream sediment mixing at wider mouths, physical river rerouting/landslides, and water-volume feedback.


## TerraWeave Stage60–61 — collapse of undercut soil (2026-11-19)

- `engine/world/TerraWeaveSlopeFailure.h`: `SlopeSoil` carries NEREID/PHYSIS-sampled wetness, soil type and root strength. `planSlopeFailures(earth,triggerVertices,slopeSoil,settings)` is an integer-only deterministic bounded heightfield relaxation: unstable higher neighbours transfer equal-area centimetre mass downhill, rock is immobile, bedrock/buffer limits are enforced, each of at most 4096 disjoint pairs participates once per tick. `applySlopeFailures` commits the plan through one Earthworks sparse transaction for generic native games.
- `engine/world/TerraWeaveEcsBankBridge.h` additionally accepts opt-in slope failure planning after NEREID computes bank erosion. **Merge water erosion AND soil slides into a single final Earthworks revision**, then recompute root support/fall and sparse grass damage ONCE from the final ground. ECS bank soil corrections are recorded for adjusted bank grid vertices, retaining NEREID suspended sediment/carries. Never call `erodeShore` twice.
- `engine/world/TerraWeaveEcsAuthority.h`: opt-in `addTerraWeaveAuthoritativeBankSystem(...,enableSlopeFailures,settings)` writes the canonical ECS resource once and updates only extra bank soil heights after prevalidating/detaching COW pool pages. `excavateAuthoritativeEcs(...,enableSlopeFailures=true)` supports digging-induced collapses and also synchronizes affected NEREID bank soil heights. Both return sparse slope, tree fall and grass damage records usable by native gameplay/Box3D.
- `examples/world/terraweave_native/{Integration.h,README.md}` opts in to bank slope failure; graphics/physics/navmesh publication stays with the existing `TerrainMutationCoordinator` native owner-thread barriers. Read `docs/terrain/stage-60-wet-slope-failure-handoff.md` and `stage-61-nereid-slopes-ecs-handoff.md`.
- **Quality status:** Source committed; NO native compile/CTest, Box3D fall, Metal screenshot, geotechnical rate calibration or ECS replay run for Stage60–61. Earlier green 9/9 CPU and 1/1 GPU router tests predate these features. Keep PR #24 Draft. This is a heightfield slump, NOT a volumetric cave/overhang or 3D fluid simulation.


## TerraWeave Stage62 — physical landslide debris and NEREID current blockage (2026-11-19)

- User delegated continued water/terrain ecology to TerraWeave integration agent. **NEREID alone** owns the ECS `WaterBodyComponent`, water geometry, current/vortex/confluence/cascade resolution, bank erosion and local sediment transport. TerraWeave owns terrain slide events, Box3D debris proxy generation, tree fall mechanics and native ECS/current-obstacle publication. Never implement a second water solver or alter `WaterBodyComponent.flow` in a terrain worker.
- New `libraries/include/aiverselib/water/NereidFlowObstacles.h`: opt-in persistent `WaterFlowObstructionComponent` and deterministic fixed-point `deflectNereidFlow` kernel. Reference `sampleWater` and `WaterQueryIndex::sample` apply it *after* ordinary NEREID currents and mouth policies. Source key sorting must be consistent in both query paths; cap active overlapping obstacles to 256. Native config `NativeWaterPipelineConfig::enableFlowObstructions` enables schema persistence, off by default in unrelated games.
- `engine/world/TerraWeaveSlopeDebrisPhysics.h`: optional bounded server-owned Box3D dynamic fragment proxies from `SlopeFailurePlan` (max default 64 new/tick, 256 living), with stable event IDs, lifetime pruning, and rendering via borrowed soil prototypes. Earthworks ALREADY transferred mass; debris bodies are visual/collision proxies, NOT a second mass ledger. Keep Box3D behind an opt-in native link and replicate server poses, do not assume float lockstep on clients.
- `engine/world/TerraWeaveFlowObstructionEcs.h` is the **Box3D-free** native ECS writer for generated auto obstacles (distinct from manually authored ones). Tag `AutoFlowObstructionComponent` marks generated records and is separately persisted. Run `publishNereidFlowObstructions` ONLY at the ECS structural-write barrier AFTER the server Box3D step and BEFORE the next NEREID sample. Preserve World COW isolation.
- `engine/world/TerraWeaveNereidObstacleBridge.h` (requires Box3D) reads `FallenPlantPhysics` and `SlopeDebrisPhysics`, quantizes poses to Fixed and resolves the owning NEREID water body with existing `sampleWater`. `synchronizeNereidPhysicalObstacles` writes generated ECS obstacles and maps to the immutable WorldScene flow snapshot. Native game MUST supply ECS body Entity → `NereidSceneSurface.id` mapping, not assume Entity.index equals scene ID.
- `WorldScene::setNereidFlowObstructions` affects optional whitewater shader current and ripple drift via the *same* `deflectNereidFlow` kernel. Visual water does not create or mutate gameplay water bodies, water height/volume, or solve fluid pressure. Existing manually authored scene/NEREID waters remain independent.
- Example: `examples/world/terraweave_native/Integration.h` registers fixed ECS water/soil/obstruction components without Box3D; `PhysicsIntegration.h` is the opt-in owner-thread post-ECS Box3D and after-physics water-feedback adapter; `README.md` describes exact update order.
- Full handoff: `docs/terrain/stage-62-debris-water-feedback-handoff.md`. **QA:** Source committed; no new Clang/CMake/CTest, Box3D scene, ECS replay, Metal screenshots or performance verification yet. PR #24 MUST stay Draft. Deeper hydraulic rerouting/pressure/river volume feedback, precise per-branch collisions, compact water-obstruction network transport and native FPS/RTS opt-in remain future work.


## TerraWeave / NEREID Stage63 — finite-volume flood routing (2026-11-19)

- NEREID is still the ONLY owner of water. NEW `libraries/include/aiverselib/water/NereidFloodRouting.h` is an **optional NEREID extension** (native `NativeWaterPipelineConfig::enableFloodRouting`, off by default for unrelated projects) for local, bounded equal-area water-column routing. `WaterFloodResource` is named persistent ECS resource `nereid.flood`, storing 16k max sorted signed grid cells, column water depth/volume, originating WaterBody Entity, TerraWeave ground height, flow and soil/wetness/tick revisions. `stepNereidFlood` transfers ONLY stored finite reservoir volume along hydraulic-head gradients between neighbouring cells, with conservative before/after mass equality and NEREID `WaterFlowObstructionComponent` permeability. Water can pile up behind a fallen tree, move sideways, flood a formerly dry cell and recede on later ticks. Do NOT invent water from rain/flow outside the finite reservoir.
- `NereidWaterSystems.h` and `NereidQueryIndex.h` apply `applyNereidFloodSample` after existing water-body/flow/obstacle resolution; a flooded cell outside the original polygon/spline footprint becomes a real WaterSample for downstream NEREID interaction/erosion. If the authored water surface is already higher, that existing surface wins.
- `engine/world/TerraWeaveNereidFloodBridge.h` seeds an explicit finite grid ONCE from existing WaterBody + canonical Earthworks; calling `initializeNereidFloodGrid` on an already active reservoir is prohibited (would create/remove volume). Ground synchronization updates grid-bottom elevations after TerraWeave digging/bank erosion/slump, without changing any water-volume column. `addTerraWeaveFloodGroundSyncSystem` must be registered after the TerraWeave soil ECS commit for the NEXT water tick.
- `WaterFloodResource::wetnessRevision` tracks new wet/dry cells separately from sub-cell volume changes. `engine/world/TerraWeaveFloodShorelineSystem.h` provides persistent `AutomaticShorelineRevision` and opt-in last-stage ECS auto-bank refresh when groundwater/flood topology or soil revision changes; newly generated bank cells participate in NEREID sampling NEXT tick. Explicit `requestAutomaticShorelineRefresh` is required for water-body geometry edits not reflected by soil/wetness revision.
- `engine/world/NereidFloodVisualScene.h` and `WorldScene::setNereidFloodCells` upload/draw a maximum 4096 flooded surface quads via ONE existing RHI model and transparent/unlit fallback material (native driver publishes up to 2048 active cells). Only extra water beyond/above base NEREID surface renders an overlay. It does NOT yet share the full NEREID refraction/foam shaders or exactly blend with authored curved coastlines.
- Native example: `examples/world/terraweave_native/{Integration.h,README.md}`. Registration order: NEREID flood routing BEFORE NEREID water sampling/erosion; TerraWeave bank soil/roots/grass commit; flood ground sync; OPTIONAL auto-rasterized shoreline LAST. All owner/thread and COW barriers preserved. Read `docs/terrain/stage-63a-nereid-flood-volume-handoff.md` and `stage-63b-flood-visual-native-handoff.md`.
- **Crucial limit:** volume conservation is within the finite, explicitly seeded equal-area flood window only; legacy static WaterBodyComponent levels and infinite ocean/river reservoir amounts are not hydraulically coupled. No rainfall/pumps/open-boundary inflow, sediment-dependent pressure solve, full shallow-water momentum or actual Navier–Stokes. Do not claim such features, global mass conservation including authored base water, or working Metal without tests.
- **QA status:** Stage63 source committed to `feature/terraweave` but not CMake/Clang compiled, CTest run, ECS replay verified, Box3D flood-simulated or Metal screenshot validated. Keep PR #24 Draft; previous TerraWeave 9/9 and GPU router 1/1 are older revision evidence only. Next: native smoke compilation, tests for 1000-tick volume conservation/overtopping/reference-indexed sample parity and GPU scene validation, followed by perf.


### TerraWeave/NEREID Stage63c finite river boundary flow (2026-11-19)

- Closed `NereidFloodRouting.h` grid alone has no infinite inflow: a flat initial river surface cannot automatically build upstream head. NEW `WaterFloodPort` (Inflow/Outflow, at most 64) and `WaterFloodResource::externalVolumeCm` explicitly model a finite, already-counted outside reservoir. Port water transfers are bounded by user-defined cm-per-tick rates and local cell capacity. Water conservation is checked over **on-grid volume PLUS externalVolumeCm**, preventing fake water creation when a blocked channel backs up or a low bank overtops.
- Configure only before the first flood tick, after seeding the grid and WaterBody entities, via `terraweave::configureNereidFloodBoundaryPorts` or native example `configureFloodRiverPorts`. `NereidFloodRouting.h` persists these ports/reserve in schema `nereid.flood`, validates inlet generation-safe WaterBody ID and port locations. A finite reserve can run dry; outlets return water to the SAME counted reserve. Do NOT call configuration again after the ECS flood tick or silently reseed/migrate the grid.
- Exact result metrics `WaterFloodTick::suppliedCm/drainedCm` and old/new water column totals, `wetnessRevision` from FINAL wet/dry changes, and `WaterFloodResource::groundRevision` for TerraWeave soil sync. Scene overlay only represents water beyond/above existing static water surfaces; full NEREID refraction/foam not yet integrated.
- See `docs/terrain/stage-63c-finite-river-ports-handoff.md`, `stage-63a-nereid-flood-volume-handoff.md`, `stage-63b-flood-visual-native-handoff.md`, and `examples/world/terraweave_native/README.md`.
- **Feature-only commits, NOT compiled/tested.** Do not call this globally volume-conserving (static ocean/river bodies are outside the ledger), a pressure/momentum solver, or production-ready flood rendering. PR #24 stays Draft.


## TerraWeave Stage64 — grass ecology and recovering riverbank surface protection (2026-11-19)

- Native ecosystem code: `engine/world/TerraWeaveShoreRecovery.h` (pure integer recovery policy, versioned `ShoreRecoveryResource`, one small persistent `ShoreRecoveryPatchState` per aggregated `GrassPatch`, seeded regrowth after drying, prolonged flooding damage, nutrient/moisture/light/age response and spatial `ShoreRecoveryCoverIndex`). NO ECS entity per blade and NO separate vegetation renderer, water or soil mass solver.
- `engine/world/TerraWeaveShoreRecoveryEcs.h`: `addTerraWeaveShoreEcologySystem` runs after NEREID flooding/shore erosion/sediment and after TerraWeave bank/slump and auto-shoreline refresh. Builds ONE NEREID WaterQueryIndex at ecology cadence (default 600 ECS ticks) and takes 5 terrain/water samples per grass patch, publishing canonical `TerraWeaveAuthorityResource.grass` changes and separately persistent `terraweave.shore_recovery` with its seed, wet/dry and soil-cover states. Owner-thread staging World copies keep rollback state coherent; beware non-COW deep-copy cost on huge Worlds. Tree/shrub fall state is untouched; never automatically revive harvested/fallen trees.
- NEREID now accepts OPTIONAL `ShoreErosionForcing::surfaceShieldPermille` and ECS persisted `WaterBankSurfaceCoverComponent` (`nereid.bank_surface_cover`) to reduce exposed soil shearing independently of true tree/shrub root binding. Grass has NO physical tree roots; ecology's aggregated soil/litter protection is a separate factor. Generated `AutoShorelineBankComponent` cells receive updated shield from the 64m bucket `ShoreRecoveryCoverIndex`; manually authored bank cover is game-owned.
- Native example `examples/world/terraweave_native/Integration.h` enables the fixed-period ecology resource/systems by default for this example only and allows disabling/configuring `ShoreRecoverySettings`; existing FPS/RTS/other genre clients remain opt-in. Read `docs/terrain/stage-64-shore-ecology-recovery-handoff.md` for exact committed source steps, API contract, remaining functions and no-test disclosure.
- Stage64 covers grass recovery/surface crust only; detailed multi-species forest competition, adaptive seed distribution, persistent rooted-plant active aging/lighting and full ecological succession remain Stage65+. NO new native compiler/CTest/NEREID simulation/GPU/Box3D/Metal QA yet; prior TerraWeave 9/9 and GPU router 1/1 are older branch. Keep PR #24 Draft.


## TerraWeave Stage65 — rooted forest ecology, canopy light and native saplings (2026-11-19)

- `engine/world/TerraWeaveForestEcology.h`: per-species **opt-in** budgeted deterministic rooted tree/shrub life cycle over existing `RootAnchor`+`PlantEnvironment` records. `ForestEcologySpecies` configures maturity, age-based death, shade/drought/flood sensitivity, crown spread and seed interval/dispersal; `ForestEcologySettings` caps at default 256 expensive plant decisions every 32 simulation ticks, 65,536 interactable rooted plants total and 8 recruits per step. A persisted cursor and `lastEvaluatedTick` prevent processing every tree each frame. `ForestEcologyCanopyIndex` indexes crowns in 32m buckets and approximates relative-height shade, not photometric GI. Seed positions/IDs use stable integer hashes, exact canonical Earthworks ground and bounded free-space tests; mortality and recruitment never depend on camera.
- `engine/world/TerraWeaveForestEcologyEcs.h` registers persistent `terraweave.forest_ecology` schema v1 (shade/flood/drought stress, mortality, recruitment, processing cursor). An opt-in ECS system reads **existing NEREID WaterQueryIndex** root-zone water/flood samples, optionally PHYSIS sunlight/soil policy, and stages ecology history plus canonical `TerraWeaveAuthorityResource.rooted` mutations together for rollback safety. Post-tick event callback is enqueuing only: never run Box3D or Renderer3D inside ECS.
- `TerraWeaveForestScene::setDeadTrees` is an independent death visibility channel, separate from `setFallenTrees` (water uprooting/Box3D) and `setRemovedTrees` (harvesting). `setRecruitedPlants` uses existing Arbor shared prototypes and Flora near/LOD batching for sparse new saplings, no per-seedling GPU prototype. `TerraWeaveNativeWorldBinding::reconcile` reconstructs their presentation from the new persistent ecology resource after save/rollback. Tree-only changes DO NOT invalidate terrain/collision/NavMesh or unrelated grass GPU batches.
- Native usage: `examples/world/terraweave_native/{Integration.h,README.md}` accepts trailing `rootedForestSpecies`, `rootedForestSettings` and `onForestEcology`. Empty policies leave old games unchanged. For harvested trees, gameplay MUST remove or suppress the canonical rooted entry too: presentation-only `removed_` does not stop a plant from reproducing. Natural death currently hides the Arbor instance but does NOT perform Box3D deadwood fall or game-resource/collider cleanup. Background decorative millions remain procedural, not individually simulated.
- Read `docs/terrain/stage-65-rooted-forest-ecology-handoff.md` for exact files, commits, API, ownership/perf limits and next steps. **No Stage65 native compiler/CTest/ECS/Metal/Box3D/perf tests have been run**; earlier green tests apply to older commits only. PR #24 remains Draft; prioritize a compile and full native QA after feature implementation.
