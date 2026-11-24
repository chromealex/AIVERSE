# TerraWeave Caverns — Stage 22 handoff (2026-11-23)

## Repository and commits
- Repository `chromealex/AIVERSE`, feature branch `feature/terraweave-caverns-core-20261123`, **not merged to main**.
- `4799b27a71adc72ca0615ed8da9fca5fd009f5c3`: fixed another literal escaped-newline compiler hazard inside `engine/world/TerraWeaveCavernsSeamGate.h`.
- `4ddcae7f7cf8f2bf8db51f30942036644f782729`: created `engine/world/TerraWeaveCavernsEditRuntime.h`.
- `71caf5e58f6dc0354d0c568aba546edc60cf3f51`: supersede earlier pending dirty entries when source revision advances.
- `a9659e6c53d1ed3e1ed6505b4e25cb65a6993359`: created `tests/terraweave_caverns_edit_runtime_test.cpp`.
- `8180fe80bb8fa70897e646d53b9e1f4cda2ab15f`: removed worker capture of owning runtime `this` pointer.
- `ec1f99873bd643e64ba22c38ce19f9dddd4744a6`: explicit Workspace operation-response adapter.
- `7ac147b69104372021c965b28e6fe811d39f4e9a`: reject stale snapshot source revisions.
- `fc8ae035ce6d82529879f6075ddf415d9e55f96f`: updated `skills/world/aiverse-terraweave-caverns/SKILL.md`.
- This handoff commit follows the above; read branch HEAD for current revision.

## Implementation
`CaveEditRuntimeBridge` is a **genre-independent, CPU-only owner-side integration adapter** between Stage19/20 editor-authored dirty volumes and Stage21 dirty queue plus Stage16 seam certification.

- `invalidate(CaveDirtyBounds,shared_ptr<const CaveVolumeSnapshot>)` enqueues dirty chunks for given resolution/spacing; every new source revision supersedes any uncommitted dirty chunk and invalidates its already prepared visuals. Older snapshots are rejected.
- `consumeEditorResult(Json,shared_ptr<const CaveVolumeSnapshot>)` accepts successful `Workspace::call("terraweave_caverns_paint",params)` response with `dirtyBounds` and the exact matching saved snapshot revision. The real editor host must explicitly call this; there is **no automatic Workspace event hookup** yet.
- `tick(budget)` uses bounded `std::async` workers with entirely immutable snapshots, yields `CavePreparedEdit` CPU meshes and reports dispatched/finished/stale/failed/ready/pending. Old jobs can't clear later edits.
- `registerResident` exposes existing neighbor mesh revisions for seam audit. `publishReady(callback)` requires all dirty chunks ready, no active jobs, successful Stage16 face certification of a prospective copy of the gate, then invokes an **owner-provided publication callback**. Dirty markers and current seam certifications update only when that callback returns true. Failure/throw retains dirty work.
- `drain()` waits on worker completion at world teardown.
- Unit fixture checks edit supersession, bounded background jobs, failure preserving pending work and a successful owner-commit acknowledgment.

## Ownership and runtime limits
- **No actual GPU/STRATA/Box3D/NavMesh update is performed by Stage22.** The callback is the integration point, NOT a safety-proven renderer/physics transaction. The host must prepare all resources, synchronize visibility, commit together and retire previous resources with proper GPU fence/lifetime proofs.
- An old `CaveBatchGeometryResident::publish` still rebuilds whole GeometryScene shards and calls blocking `shutdown`; do NOT wire it to every brush dab without frame-time/fence work.
- A failed mixed-LOD face returns not-ready; Stage22 currently does not schedule the Stage16 fallback seam jobs itself. Later bridge Stage18 versioned `CaveSnapshotRegenerator` into this pipeline or implement generalized safe transition meshing. Do not expose incompatible chunks.
- No automatic 3D brush event listener or Undo/Redo history diff. Callers must supply changed bounds; for Undo/Redo use the union of old/new edited volumes or invalidate loaded region.
- Multiple dirty entries from the same new source revision are coalesced. The bridge expects canonical full-world snapshots (base Settings, entrances, SDF stamps) that represent all affects + gradient halo. It cannot independently verify source settings correctness.
- The bridge currently scans the bounded dirty queue when checking current serials (correctness-first, not large-world optimized).

## Verification performed
- GitHub connector acknowledged commits/updates. Source inspected for API compatibility with Stage16, Stage18 and Stage21 header types.
- **NOT RUN:** stage22 C++20 compilation, CMake/CTest, sanitizer, actual editor UI integration, CPU generated multi-chunk visual seam audit, GPU/STRATA/GeometryScene, SpatialWorld, Box3D, native NavMesh acceptance. Test is authored, not passed.
- Suggested full-checkout command: `c++ -std=c++20 -pthread -O1 -I. tests/terraweave_caverns_edit_runtime_test.cpp -o /tmp/aiverse-caverns-stage22 && /tmp/aiverse-caverns-stage22`.
- Review all stage1–22 header-only helpers for compile errors and CMake integration before merging anything to main. No full checkout is available in this execution environment.

## Next stage
1. Wire the editor host's returned `dirtyBounds`, saved full `MapDoc` snapshot, and Undo/Redo changes into an owned `CaveEditRuntimeBridge` lifecycle. Bridge scene loading, streaming and world transitions.
2. Implement a genuinely staged/fence-aware GPU shard update (no blocking unload), real GPU frame ownership and fail-safe all-owner swap with Box3D/SpatialWorld/navmesh, using the Stage8 `CavePublicationCoordinator` as bookkeeping rather than as false transaction proof.
3. Fix mismatched equal-LOD cave generation seams on actual field samples; implement Stage16 mixed-LOD fallback worker scheduling and whole-neighbor barrier.
4. Register all Caverns stages as actual CMake/CTest targets, compile/link native and run GPU screenshot and gameplay acceptance on Metal, Vulkan and GLES/GL where supported.
5. Continue per-stage changes to `skills/world/aiverse-terraweave-caverns/SKILL.md` and independently dated `docs/agent-handoffs/caverns-stageN-YYYY-MM-DD.md`.
