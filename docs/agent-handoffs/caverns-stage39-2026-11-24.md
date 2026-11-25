# TerraWeave Caverns — Stage 39 immutable indexed SDF handoff (2026-11-24)

## Repository and provenance

Repository: `chromealex/AIVERSE`
Branch: `feature/terraweave-caverns-core-20261123` — **not merged into main**.

Stage39 commits acknowledged by the GitHub connector:
- `ab07cf0f205910ccdc5f094b4863dc7d7b512515` — new `engine/world/TerraWeaveCavernsSdfSpatialIndex.h`, immutable per-snapshot bounded spatial grid.
- `2c2dc2c0cd0e953e4295d4ff8b55c6608f4e1a16` — prevent unsigned overflow/underflow on posting-budget edge case.
- `d04730149fbaf064a39c8981277e914bf6ed1407` — `engine/world/TerraWeaveCavernsEditRuntime.h`, shared indexed SDF for Stage22 edit CPU workers.
- `8a7ed33f4a383cf88e51834f394788fdda169031` — `engine/world/TerraWeaveCavernsAuthoredNav.h`, native authored NavMesh reads indexed density.
- `e7a888d1abec03f680ccab8b33dab79425c54689` — `engine/world/TerraWeaveCavernsAuthoredNavStage.h`, native nav stage overload accepting an already built shared index.
- `5b9a561aaa02ca8af8e4b2589d4a06bdcd8f4804` — Stage22 edit bridge exposes indexed snapshot to other owners.
- `e036c621718e20c215daaa0ecd09c140762faa59` — Stage34 gameplay edit host shares one frozen index with native navigation.
- `23fed0a5b4e29d6c59811590dca0769a7121446f` — Stage36 `CaveWorldStreamJobs`, indexed visual and authoritative collision mesh generation.
- `d24eeb7aa0a7fae73c6988fa855db9cc0adf4155` — Stage36 runtime passes the matching immutable index to streamed mesh workers.
- `3d75c7bb86404c33170e0db75519df210ec8e521` — `engine/world/TerraWeaveCavernsRaycast.h`, density-callback shared exact surface-ray traversal and index overload.
- `f75ca5f493f13d863e395ffa946dfbbd4ef565ec` — `apps/editor/windows/TerrainWindow.cpp`, indexed cached brush raycast.
- `7b55f5f49fe7d099993b4c894f9d68ba9701449c` — Stage18 `CaveSnapshotRegenerator` LOD/seam workers reuse one indexed density source.
- `cf64d14bfa58d2ff0c73edcc235d3d3886e32498` — oversized influences accelerated by immutable deterministic median-split AABB tree.
- `4a1ec8c5095a56f812cc3ea488ec97b24acaf4dc` — read-only candidate counts at a queried point.
- `f012d58c9530d2f63291466c0f88b5ee6cd4c3dd` — TerrainWindow shows index candidate counts versus all authored stamps and entrances at the brush hover point.
- `830ee3dedb3f7e8cf1f63c033a502c9cbeba324e` — validate immutable SDF source coordinates/radii before computing spatial bounds.
- `e9f0246e4d9bd84213df3299e5b3003e7f7c3c01` — build/validate index before changing the Stage22 dirty queue, preventing partial invalidation.
- `c21dbb9d7fadb409c22e64a65ef06e51a12c7a7e` — updated `skills/world/aiverse-terraweave-caverns/SKILL.md`.
- This dated handoff is the final subsequent commit; inspect feature HEAD for its exact SHA.

## Implemented architecture/API

New `CaveSnapshotSpatialIndex(shared_ptr<const CaveVolumeSnapshot>,CaveSdfIndexConfig={})` owns the immutable source through its shared pointer. `CaveSdfIndexConfig` defaults to a 4096-cm 3D spatial hash cell, at most 256 postings per influence and at most 262144 total cell postings for each of stamps/entrances. Each small influence is stored as a source array index in all intersecting cells. Rather than allocating an unbounded number of entries for a very large Carve/Fill sphere or entrance influence, the builder stores it in an overflow record with a conservative AABB. The oversized records are built into a deterministic median-split AABB tree with 8 influences per leaf and axis-aligned bounds; field evaluation visits only containing leaves and influences, without allocating per-query scratch memory.

`CaveSnapshotSpatialIndex::field(PointCm)` evaluates the SAME integer `caveFieldCmUnchecked(settings,p)`, applies each matching `entranceFieldCm` via max, applies matching Carve stamps via max, then matching Fill stamps via min of negated field. All mathematical operations remain Stage18's exact integer functions, including signed cm coordinates and Carve-before-Fill order. Within each class, max/min reductions are commutative so spatial traversal order doesn't change the final field. No GPU or ECS state is duplicated; the index is external ephemeral resource metadata for the immutable authored volume. `CaveSdfIndexStats` exposes occupied cells, postings and overflow counts. `index.candidates(p)` counts possible influences without allocations or mutating the index. `frozenSnapshot()`, `revision()`, `snapshot()` preserve source identity.

**Correctness consideration: entrance SDF radial tails.** `entranceFieldCm` can return a negative value above the procedural background even when XZ is outside the entrance's visible radius. Indexing only the visible radius silently changes negative SDF and can affect tessellation interpolation and normals. For each entrance, use conservative XZ radius `r + 100000` (since `(r+100000)^2 >= r^2+2*r*100000`) and full vertically affecting bounds. Beyond that box, the entrance field is `-100000`, a max no-op on the clamped base field. Those large bounds are why the overflow BVH is necessary. Sphere stamps are indexed over their closed radius cube: beyond it, `caveSphereField` returns `-100000`, making Carve max and Fill min (+100000) no-ops. Do not shrink an entrance index to radius `r` without changing physics/meshing results.

**Single-index sharing.**
- `CaveEditRuntimeBridge::invalidate` builds `CaveSnapshotSpatialIndex` ONCE from the incoming immutable snapshot before dirty queue mutation, retains it in `index_`, and captures that pointer in all versioned async mesh jobs. `indexedSnapshot()` allows Stage34 gameplay/Nav owners to use exactly the same SDF index. Old jobs retain old immutable indices and their results are rejected by the existing revision gates.
- `CaveSnapshotRegenerator` caches the index per frozen `shared_ptr` identity, uses indexed mesh generation for async same-snapshot LOD/seam repairs, and replaces the index only when source pointer changes.
- `CaveGameplayEditHost::stageReady` passes its `worker_.indexedSnapshot()` to `CaveAuthoredNavStage::prepare`. Existing callers with only a source snapshot still use `makeCaveAuthoredNavSource(snapshot,...)`, which builds one index per nav source lifetime rather than one per query. A new overload accepts an existing shared index and avoids that extra build.
- `CaveWorldStreamJobs::dispatch` adds an optional immutable index argument. It rejects a mismatch against both `request.sourceRevision` and the exact shared source pointer, then uses the same indexed field for distance-coarsened visual geometry and full-resolution collision mesh. The Stage36 game frame supplies the same index used by gameplay edits and nav.
- `raycastCaveSurfaceSampled(field,ray,maxDistance,step)` factors out the ORIGINAL deterministic sign-change and 1-cm bisection algorithm, with overloads for existing `CaveVolumeSnapshot` and `CaveSnapshotSpatialIndex`. `TerrainWindow` caches one immutable index whenever the map's `terraweaveCaverns` JSON changes, and otherwise reuses it for all Scene brush hover rays. The panel shows matching candidates / total stamps and entrances at the hover position.

## New and changed files

New:
- `engine/world/TerraWeaveCavernsSdfSpatialIndex.h`

Modified:
- `engine/world/TerraWeaveCavernsEditRuntime.h`
- `engine/world/TerraWeaveCavernsSnapshotWorkers.h`
- `engine/world/TerraWeaveCavernsAuthoredNav.h`
- `engine/world/TerraWeaveCavernsAuthoredNavStage.h`
- `engine/world/TerraWeaveCavernsGameplayEditHost.h`
- `engine/world/TerraWeaveCavernsStreamJobs.h`
- `engine/world/TerraWeaveCavernsRuntimeFrame.h`
- `engine/world/TerraWeaveCavernsRaycast.h`
- `apps/editor/windows/TerrainWindow.cpp`
- `skills/world/aiverse-terraweave-caverns/SKILL.md`
- This dated handoff.

## Limitations and verification

- The index accelerates authored Carve/Fill/entrance influence scanning. It deliberately does NOT replace the procedural `caveFieldCmUnchecked` lattice search or change the generator RNG, interpolation, sign convention or nav::Mesh algorithms. Actual speedup depends on index occupancy, large influence density and mesh complexity. `candidates(p)` reports potential influence counts, not elapsed time.
- Large or widely overlapping influences can still be expensive, even with BVH; bounded memory falls back to the oversized influence tree. The index itself is rebuilt once for each immutable authored revision. No persistent serialization of the transient index is needed: persist only authoring settings/entrances/stamps, regenerate index after load.
- All public code assumes legal integer-cm world positions for indexed point queries, consistent with Stage18 `validatePoint` and generated in-bounds meshes. Entrance coordinates are explicitly validated during index construction.
- The index is not directly used by the public `caveSnapshotFieldCm(snapshot,p)` legacy helper; that fallback remains unchanged so previous consumers retain their API. Performance-critical paths described above explicitly pass the shared index.
- **NO tests RUN** in Stage39. User requested feature completion before a broad test pass. No full AIVERSE C++ compilation, CMake/CTest, native Editor run, index-to-reference field parity check, AABB tree perf benchmark, raycast/nav parity, GPU or Box3D/SpatialWorld tests were executed. GitHub connector commit results confirm source publication ONLY and do not prove compilation, bit parity or performance.
- Native full-checkout QA must include exact integer field parity over random and boundary points, negative coordinates, large entrance radial tails, Fill priority, 16k stamps, posting overflow/budget pressure, multiple concurrent async jobs, old-index persistence across revisions, and actual Metal/Vulkan/GLES visuals. Do not claim the optimization is quantified without measurements.

## Concrete next features

1. **Stage40**: complete host integration for an actual native client/editor play runtime and a dedicated server, with world load/update/unload and a real pause/exclusive barrier across GPU/Box3D/SpatialWorld/NavMesh. Ensure no duplicate cave draw path. The game world recovery mechanism must rebuild from one authoritative SDF snapshot if Stage35 sets `RecoveryRequired`.
2. Finish STRATA sidecar fence-backed retirement or bounded GPU paging for oversized shard sources if this remains a requirement; Stage38 covered only model-only GPU retirement.
3. After the user confirms feature completion, run full branch CMake/CTest and fix all native errors, cross-system ownership defects and render artifacts BEFORE merging into main. Update `skills/world/aiverse-terraweave-caverns/SKILL.md` and a new independent dated handoff at each next stage.
