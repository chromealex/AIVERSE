# AIVERSE GI — Stage 58: stable light identities and strictly monotonic GPU geometry revisions (2026-11-20)

## Branch, commits, and actual verification

- Repository `chromealex/AIVERSE`, branch `feature/gi-lighting-portable-backends-20261119`.
- Draft PR #47 targets `feature/hybrid-dynamic-gi`, NOT the diverged main branch.
- `fc1d246c549ab5f85657e122982748f159fc7e13` — `engine/render/GiGpuDynamicRuntime.h`: include `GiMovingLight::key` when comparing sorted source snapshots for the Stage50 idempotent light optimization. Identical position/radius/color/cone with a **different stable ECS light identity** is now a source revision, not a no-op.
- `53a499e38c71fda289508cb1ab60993aa4d17608` — `tests/render/gi_gpu_dynamic_runtime.cpp`: replacing an idle light with a new stable ID but identical energy/transform must invalidate affected GI probes.
- `294191c9d6fe7c1391b29252fb90960c16af6344` — `engine/render/GiGpuDynamicRuntime.h`: reject `revision<=gpu_.geometryRevision()` in both initial/full `setGeometry` and partial `setGeometryRegion`. The public GPU runtime must not regress or re-accept duplicate immutable geometry revisions even when a higher-level controller's monotonic checks are bypassed.
- `c4fb7b0d0a1f60b653d2d23842a640e297c11bb5` — `tests/render/gi_gpu_dynamic_runtime.cpp`: exercise rejection of duplicate, zero, and older full/regional geometry revision sources without changing stable chunk-page state.

**Performed:** GitHub source inspection, code commits and registration within the existing CTest target.
**NOT RUN:** native CMake/C++ compilation, CTest, real Metal/Vulkan/GLES shader compile/GPU execution, ECS entity identity collision tests, performance or full acceptance. No PASS is claimed from authored test code.

## Why this fixes correctness

Stage50's repeated-light snapshot fast path compared transform, radius, energy, spotlight direction and cone, but omitted the stable light key. Replacing ECS entity K with K+1 at identical coordinates/color could be incorrectly treated as unchanged GPU lighting. Although radiance may look the same initially, subsequent invalidations and deletions are keyed; failing to update identity can leave old probe energy or accept the wrong event later. The equality predicate now includes the key. It remains safe and efficient to submit the *same* complete stable-key light list every frame, including while a previous GPU compute job is in flight.

The paged-GPU path introduced in Stage56 and compacted in Stage57 maintains a `geometryRevision_` and committed BLAS page arena. Before this stage, direct `GiGpuDynamicRuntime::setGeometry`/`setGeometryRegion` calls could accept a stale `revision` (because only `revision==0` was checked) and override a newer committed snapshot. The host `GiLightingController` already rejected these, but the lower-level public API needed the same guard. Rejecting stale/duplicate publications prevents old WorldScene/TerraWeave/PHYSIS notifications from rolling back a new GPU TLAS/BLAS layout. The GPU queue still requires genuinely completed Metal callbacks or Vulkan VkFence before changing GPU storage.

Stage57 compaction semantics remain: a candidate packet is built without mutating committed page addresses, and it is only committed after accepted GPU upload. No silent relevant occluder truncation and no new gameplay/ECS genre coupling.

## Next steps

1. Native-build `aiverse_gi_gpu_dynamic_runtime_test`, `aiverse_gi_gpu_page_compaction_test`, `aiverse_gi_gpu_paged_bvh_test`, `aiverse_gi_gpu_sparse_patch_test`; execute CTest and record tested SHA + logs, then fix any failures.
2. Add an ECS producer integration fixture that destroys a light entity, creates a new entity at the same position, updates light snapshots and verifies that deletion events remove the correct GI energy. Validate on Metal/Vulkan GPU and GLES CPU.
3. On real Metal/Vulkan devices, measure persistent page update traffic and GPU completion latency across terrain/foliage edits and capture pixels against CPU reference; investigate pages that compact due to high-water fragmentation and verify no GPU use-after-free.
4. Finish remaining world/clipmap streaming and current-main/TerraWeave/PHYSIS reconciliation without clobbering other agents. Run the full requested native and multi-backend test pass only after functional completion; PR #47 must remain DRAFT until successful evidence exists.
