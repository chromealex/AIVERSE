# AIVERSE GI — Stage 48: bounded large-world GPU BVH residency (2026-11-20)

## Branch, exact code changes, verification

Branch `feature/gi-lighting-portable-backends-20261119` — draft PR #47 targeting original `feature/hybrid-dynamic-gi`, not the substantially diverged `main`.
- `4c1f9f979c33de1a05b3f760dae5a19e3bc242f6`: `engine/render/GiChunkedGeometry.h` gains `GiChunkedGeometrySnapshot::filteredChunks(predicate)`, building a new top-level BVH with **shared existing immutable per-chunk geometry/BVH pointers**. No source geometry copies, edits or collisions.
- `969487bbe51852f2ab616a3e1be995fc55b4b04e`, `d08138f33e99b8f61dbd56f8f5796908f5a0e4b2`: new `engine/render/GiGpuResidencyPlanner.h`, conservative source selection by probe volume AABB expanded by secondary shadow reach; explicit no-truncation budget rejection and per-plan metrics. Fix moves filtered BVH rather than copying it.
- `450e7cecfee255ae6fbfdc627142a360022ef75a`: `engine/render/GiLightingController.h` automatically builds a relevant immutable GPU-only geometry subset on `setGeometry` and `setGeometryRegion` when using GPU transport; full CPU source snapshot is retained. Exposes `gpuResidency()` diagnostics. Adds `GiTransportConfig::maxGpuMovingLightRadius` default 64 m; GPU light updates reject any source with a larger radius, never silently omit an occluder beyond the resident BVH boundary.
- `aa47565ffbe5beb4a4a0b79de22db43d2cebe6a9`: `tests/render/gi_gpu_residency_planner.cpp` source coverage: 9002-triangle world with only 2 locally reachable triangles, far 9000-triangle chunk excluded, pointer-identical immutable resident BVH reuse, explicit admission rejection if near scene is over budget, GPU controller refusal of over-reaching moving-light sources, preserving last successfully accepted geometry on refusal.
- `4ce3d81648552d1309ee218fcc006058dc7df411`: CMake/CTest registration of `gi_gpu_residency_planner`.

**Actual verification:** GitHub source changes committed and read back. Native compiler, CTest, actual Vulkan/Metal GI pixel tests, GLSL/SPIR-V/MSL validation, GLES CPU parity, memory benchmarks: **NOT RUN**. Test assertions are authored, NOT executed.

## Architecture, invariant and limits

The bounded Metal/Vulkan small-scene BVH backend currently supports max 8192 triangles; a full multi-million-triangle world used to fail *regardless of camera/probe location*. New `GiGpuResidencyPlanner::plan(full,origin,spacing,nx,ny,nz,traceDistance,maxGpuLightRadius,maxTriangles)` constructs a conservative **probe-reachable** region. It expands the entire lattice envelope by `2*traceDistance + maxGpuMovingLightRadius + spacing`. This covers the first bounce, a second sky/sun occlusion ray up to traceDistance, and a secondary point/spot shadow segment up to max light radius, with a cell of padding. Only chunks whose immutable collision bounds intersect that reach can contribute; remote chunks can be excluded without influencing a bounded ray from that local probe volume. The selected scene shares per-chunk BVH memory; only its small top-level BVH is rebuilt.

**No silent occluder clipping**: if potentially reachable triangle count still exceeds `GiGpuBvh::kMaxTriangles` (8192), `GiGpuResidencyPlan::admitted=false`, `exceededBudget=true`, `selected=null`. The controller refuses publication and keeps prior GPU geometry and its independent atlas. The world's `GiWorldPresentationSync` retains changed source and events for later retry/LOD reduction. It is **not** correct to truncate nearby foliage or buildings just to hit the GPU limit: that would produce light leaks.

The host may configure `GiTransportConfig::maxGpuMovingLightRadius` (default 64 m, explicit maximum 1000) according to authored lights. Once the GPU scene has been planned, `setLights` rejects sources with a radius above that bound. The CPU fallback does not need a filtered scene and keeps the full immutable original. This prevents a later moving light from seeing geometry outside the GPU's acknowledged source region. GPU mode is opt-in and does not alter authoritative world, ECS or save data.

This is **bounded residency**, not production Nanite-style multi-million triangle streaming. The local cell set can still exceed 8192 if terrain chunks have too much tessellation or forests are too dense, and geometry packets are still repacked/uploaded on accepted revisions. A larger local source requires lower triangle LOD/proxy collision from the terrain/foliage producer, hierarchical SDF/voxel GI, per-brick streaming, or a deliberate CPU fallback. No silent GPU or performance promises. The volume is configured around a fixed origin; moving camera clipmaps need explicit rebase/reconfiguration or a higher-level clipmap owner, not an implicit view origin change.

## Next exact steps

1. Native compile/test `gi_gpu_residency_planner` and `gi_lighting_controller`, actual GPU BVH shader tests and source build. Report exact PASS/FAIL SHA; not source-only assumed success.
2. Feed real TerraWeave collision/forest LOD occluder proxies through `GiWorldGeometryBridge` + `GiWorldPresentationSync`, respecting the stage 47 GLES texture-budget fallback. Measure near scene triangles/droppedRemote/failedNear under procedural large worlds and moving cameras.
3. Add chunk BLAS upload persistence / GPU cache paging and graceful user-facing quality downgrade if near region still exceeds 8192; full per-brick clipmap/atlas parity remains unfinished.
4. Reconcile with actual main and other agents' RHI, NEREID, TerraWeave and scene changes before enabling default GI. Only after feature closure, native end-to-end full CTest and Metal/Vulkan/GLES GPU acceptance.
