# AIVERSE GI — Stage 53: explicit bounded CPU fallback on local GPU BVH overflow (2026-11-20)

## Source branch, commits and verification

- Repo: `chromealex/AIVERSE`; branch `feature/gi-lighting-portable-backends-20261119`; draft PR #47 against `feature/hybrid-dynamic-gi`, NOT merged to current divergent `main`.
- `1787856d9437d293998a4bcd74907b7ac76b2723`: `engine/render/GiGpuDynamicRuntime.h`, read-only `lights()` getter for accepted stable-key moving source snapshot, usable only at no-in-flight GPU fence point during safe transport migration.
- `0c4d6c140ed6e176f8e414c200610f60ba585506`: `GiLightingController.h`, new `GiTransportConfig::cpuFallbackOnLocalGpuOverflow` (DEFAULT FALSE). When `GiGpuResidencyPlanner` refuses a new locally relevant scene because triangle count exceeds the strict 8192-triangle GPU BVH limit, callers may opt into a **one-way, complete geometry-preserving** switch to CPU GI instead of displaying an obsolete GPU atlas or silently dropping nearby occluders.
- `47133d4f17e5a277ff7397c00a8509749dfb08bc`: extends `tests/render/gi_gpu_residency_planner.cpp` to opt in, exceed GPU BVH budget, switch to CPU, retain complete new geometry + accepted point/spot and sun/sky sources, keep GPU failed-capacity diagnostics and enforce bounded CPU probe trace count.
- Actual verification: GitHub code inspection/commits only. **Native CMake/CTest, GLSL ES3, Metal/Vulkan GPU/shader compilation, memory and performance tests NOT RUN.** Do NOT call these assertions passed.

## Public API and behavior

`GiTransportConfig::cpuFallbackOnLocalGpuOverflow = true` is an explicit quality/performance policy; default remains false to avoid unexpectedly expensive CPU tracing on mobile render threads. A host can opt in for smaller scenes that still overflow the bounded GPU GI prototype, or for a graceful high-geometry fallback. The normal `GiGpuResidencyPlanner` still removes only provably remote world chunks; when **relevant** triangles exceed `GiGpuBvh::kMaxTriangles`, it reports `exceededBudget`. If GPU has NO in-flight compute work, the underlying RHI is healthy and the option is enabled, `GiLightingController::tryCpuOverflowFallback` configures `GiDynamicRuntime`, feeds the *FULL* immutable chunk snapshot and matching source revision, migrates previously accepted stable-key point/spot sources and environment sun/sky, then releases the now-fence-quiet GPU runtime and marks mode=`CpuProbes`. The full snapshot is retained by the controller and queried by CPU BVH, with adaptive budget from `cpuFrameBudgetMs/cpuMaxProbes`.

This is a ONE-WAY transition until explicit `initialize`/shutdown; it never flickers back into partially resident GPU mode just because the camera moves. The CPU result is initially dirty/zero and is incrementally computed/published, not an invented finished GPU frame. Independent previously rejected light and environment gates remain active until their authoritative revisions are accepted. GPU overflow diagnostics remain accessible through `controller.gpuResidency().exceededBudget`; GPU residency policy is NOT silently lifted to a larger GPU ray-trace memory allocation.

If a GPU compute batch is still in flight, conversion is rejected, `pendingGeometry` blocks the stale GPU overlay, and `GiWorldPresentationSync` retains pending world geometry/events. The host retries after authentic VkFence/Metal completion. The caller MUST unbind the old GPU atlas from `Renderer3D` before source publication that could destroy GPU texture resources. A dead GPU device cannot be recovered by this fallback because even CPU probe data needs a functioning sampled texture RHI.

## Limits / next

- The original CPU reference tracer may still exceed the frame-time target on a single expensive probe; its `GiCpuTraceBudget` is a SOFT target and always admits progress of at least one probe. Do not use this option on unconstrained 100k-triangle near scenes without profiling, and never claim proven mobile performance.
- Test a real Metal/Vulkan scene with >8192 local triangles, visible reflection before/after geometry revision, and CPU fallback while the renderer remains lifetime-safe. Confirm the initially stale GPU atlas is never shown and that normal LDR/HDR output uses the new CPU RGBA32F atlas.
- Compile/run `gi_gpu_residency_planner`, `gi_lighting_controller`, `gi_world_presentation_sync`, Vulkan compute tests, and final full CTest suite once other functionality is integrated; report tested SHA+actual log evidence.
- Complete automatic TerraWeave/PHYSIS/WorldScene event wiring with actual current-main branch interfaces, high-quality resident GPU BVH paging/clipmaps and multi-backend GPU pixel parity before merging.
