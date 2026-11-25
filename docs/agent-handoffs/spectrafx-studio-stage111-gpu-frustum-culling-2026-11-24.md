# Stage 111 — GPU-owned billboard frustum culling

Branch: `feature/spectrafx-html5-data-interfaces-20261123`. Previous agent: `492ef6a219aebbd44fdea635d55dd2547fe2ffb7`. Roadmap preparation: `1d1caa711bc8ffd53c87f545e0d347118af2f899`. Implementation: `8ab6830dca085b8c49fe2cf063d9a1377ea50346`. At the start, comparison against main reported 619 commits ahead, zero behind. Main is untouched.

## Implementation

- `engine/fx/SpectraGpuShaders.h`: GPU-owned mode 13 resets the separate visible counter (or sets it to alive count when culling is bypassed). Mode 12 evaluates conservative homogeneous clip-plane support of the actual camera-facing quad, including size and basis, then atomically compacts visible ordinals. Depth test deliberately uses wider [-w,w] for conservative rejection; rasterization handles the remaining clipping. Nonfinite projected extents are retained. Mode 5 publishes only visible draw population for both expanded and procedural geometry. Alive/free lists, births, death, VM and source ownership do not depend on visibility.
- `engine/fx/SpectraGpuParticles.{h,cpp}`: append matrix/control to compute uniforms (144 bytes, original first 64 bytes unchanged); initialize/reset/account for seventh allocator counter. Set view before step; a changed matrix or basis invalidates the previously culled geometry until the next step, even with dt=0. No mandatory CPU readback. Existing async allocation diagnostics now copy 28 bytes, append exact `visible` to AllocationCounters, validate visible<=alive and keep the captured revision. Existing CPU lifespan estimates remain estimates.
- `tests/vfx_gpu_backend_test.cpp`: recording RHI regression for allocator phase ordering, repeated identical view, changed view rejection and zero-dt refresh. Mock does not execute MSL.
- `docs/spectrafx.md`: scope/acceptance and native API notes.

## Feature gate

Implementation/architecture/dependencies/documentation: source changes committed and reviewed via GitHub commit and file readback; execution NOT RUN. Native API: existing setView/step/draw and allocation diagnostics extended; independent native runtime execution NOT RUN. Skill/catalog/MCP/Studio/Inspector/graphs/persistence: no new authored data, property, node or user operation; existing authoring/preview behavior retained, interactive integration NOT RUN. Headless authoritative simulation: no changes; regression NOT RUN. Performance/platforms: Metal GPU-owned allocator only, zero new per-slot allocation, +4 bytes persistent counter and +80 bytes uniforms, actual performance and hardware compatibility NOT RUN. Tests/examples: existing recording test source extended, no new asset format; real-device scenario still required. Publishing: roadmap recorded; real capture and devlog NOT RUN. This is not a verified or merge-ready stage.

## Verification

GitHub tree/commit/ref succeeded; re-fetched exact implementation commit, public counter struct and mode 5 shader source. No CMake, CTest, Metal shader compilation, native Studio, benchmark or capture executed in this session; CI status not verified. Do not reuse earlier test evidence. Main gate remains required.

## Next action

Add an actual Metal allocator stress fixture using asynchronous counters: inside/outside/edge billboards, all invisible, camera recull with dt=0, offscreen lifetime expiration, kill and reuse, both procedural and expanded modes, readback of exact draw args and alive+free invariant. Then implement distance/screen-size LOD and physical page growth as separate stages; avoid killing offscreen particles or using host estimates as GPU counts.
