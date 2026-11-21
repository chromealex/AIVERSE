# Lighting main stage 38 — large-world GPU GI residency (2026-11-21)

Branch `feature/aiv20261121_gi_stage38`, base main `efa96dd25`. Item 1 of the
step-2 plan in `main-integration-gi-2026-11-21.md`.

## What changed

- Ported from `feature/gi-bvh-precision-stage62-20261120@082ae5538`
  (unchanged except unused-parameter warnings): `GiGpuChunkBvhCache.h`,
  `GiGpuPagedBvhArena.h`, `GiGpuResidencyPlanner.h`, `GiWorldDirtyAccumulator.h`.
- `GiMainGpuBridge` (main's owner, no second runtime):
  - `publishGeometry` plans residency against the probe volume
    (`2*traceDistance + lightReach + spacing` around it): chunks no GI ray can
    reach are not resident. If the reachable set exceeds the 8192-triangle
    budget the publication is refused (never drops a relevant occluder).
  - The resident set is packed by the paged arena: unchanged chunks keep
    their GPU addresses, an edit uploads only changed BLAS pages + TLAS prefix
    (through the existing sparse `setPackedGeometry`, fenced as before).
  - `initialize(device, config, lightReach=64)`; `publishLights` refuses a
    light whose radius exceeds the resident reach while chunks are excluded;
    the next `publishGeometry` plans with that radius.
  - `planResidency`, `residency()`, `pageStats()` for hosts and diagnostics.
- `DynamicGiHost` chunks the scene spatially (cells of about one trace
  distance) instead of by 65536-triangle runs, and decides GPU vs CPU with
  `planResidency` instead of the whole-scene triangle count.
- `StaticPointShadows.h` includes `<cstdint>` (latent bug: `uint32_t`).

Not ported: `GiGpuTicketQueue.h` (+ test). Main's bridge already owns the
fence/ticket logic (`gpuSubmissionFence`/`gpuCompletedFence`); a second queue
would be a parallel implementation.

## Tests

New CTests in the Lighting CPU fixture loop: `lighting_gi_gpu_bvh_validation`,
`_bvh_sparse_upload`, `_chunk_bvh_cache`, `_chunk_bvh_lifetime`,
`_paged_bvh`, `_residency_planner` (now also covers `GiMainGpuBridge`:
far chunk excluded, light-reach refusal, far edit reuses pages, relevant
overflow refused), `_world_dirty_accumulator`. Tests adapted to main's
fence-retired buffers (`shutdownAfterGpuIdle`, retired dummy handles).

New GPU CTest `lighting_compute_gi_gpu_far_world`: the compute GI scene gets
an extra 9000-triangle chunk 5000 units away (over the budget alone); the
run must report it excluded and still pass the pixel check against the CPU
reference.

Run here (g++ 14, single files, not a build tree): all 7 CPU fixtures and the
existing `lighting_gpu_bridge` pass; `tests/render/hdr_scene.cpp` and
`DynamicGiHost.h` pass `-fsyntax-only`. Full CMake/CTest and Metal: on the Mac
(KINGSTON) run.

## Next

Item 2: world/TerraWeave geometry feeds into `GiChunkedGeometry` snapshots.
