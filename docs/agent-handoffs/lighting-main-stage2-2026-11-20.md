# AIVERSE lighting integration — Stage 2: shared GI ray geometry and Luma direct CPU reference (2026-11-20)

## Exact branch, sources, commits

- Target integration branch: `integration/lighting-main-stage1-20261120`, derived from `main` SHA `f9aed9a705982e2c6b77b29869e309995ce8b163`. This does not mutate `main`.
- Authoritative donor: `feature/gi-lighting-portable-backends-20261119` at inspected SHA `770c7ac1d96ab752cb68bebdf34fbb26896cd335`.
- Independent Luma donor: `feature/lumaweave-massive-lighting` at inspected SHA `7943124126796ea03a1d10a70a1d800a696a9669`.
- Source port commit `734cb79a505233e729c3725f4588c6a111b3686e` and CMake CTest registration commit `4fe68d49a31a57a5752d533e9359717cfb7d3ad7`. See Stage 1 for prior imports and registration.

## Portable source files and API

- `engine/render/GiUpdateScheduler.h`: genre-independent dirty probe ticket scheduler and world-space bounds, without authoritative ECS state.
- `engine/render/GiTraceGeometry.h`: non-owning ray query API using current-main existing `StaticPointShadows.h`; caller owns immutable scene lifetime.
- `engine/render/GiChunkedGeometry.h`: immutable snapshot of replaceable chunks, stable identities and small top-level spatial hierarchy, sharing unchanged source BVH pointers on edits.
- `engine/render/GiGpuBvh.h`: same shared BVH representation reused by Luma direct and Hybrid GI, fixed 48-byte GPU node and 64-byte triangle packet, bounded CPU tracing. This import adds **CPU BVH builder**, not a Vulkan/Metal GPU compute pipeline.
- `engine/render/GiEnvironmentLight.h`: opt-in sun/sky indirect source parameters default to zero, preventing accidental double count of baked environment.
- `engine/render/LumaWeaveDirectReference.h`: unbiased importance-sampled point/spot DIRECT light correctness reference, geometric shadow ray through shared BVH. No indirect/baked/sun light is double-added.
- Added existing donor tests `tests/render/gi_chunked_geometry.cpp` and `tests/render/lumaweave_direct_reference.cpp`; registered under `lighting_gi_chunked_geometry` and `lighting_lumaweave_direct_reference` in Stage1's CTest loop (6 lighting test names in total).

## Compatibility and safeguards

- The existing main `StaticPointShadows.h` and `Math3D.h` are used without being replaced; no renderer/PRISMA/SpectraFX/Metal/GL/Vulkan APIs or ECS/save formats are changed.
- The shared packet ABI and light registry are sources for a later staged renderer integration; no Luma or GI rendering is enabled yet.
- Large-world BVH residency/paging and Metal/Vulkan compute remain on donor GI branch and require separate admission, full native shader checks and GPU-lifetime integration. The CPU `GiGpuBvh` budget is finite and should never silently truncate world occluders.

## Actual verification (truthful)

- PERFORMED: GitHub source review for the dependency chain, positive source commit/update acknowledgements, CMake test registration source changes.
- NOT RUN: native C++ compilation, CMake configure, new `lighting_* ` CTests, full CI, Metal/Vulkan/GLES shader compile, actual GPU frame capture, GI/Luma direct-light visual parity or performance.
- Do not infer feature completion from source registration; any build failure must be captured in the next stage's handoff with tested SHA/command/log.

## Next action

1. Run six focused CTests on a compatible C++20 compiler, including strict `-DNDEBUG -Werror` build, fix any concrete failures while preserving current main.
2. Port bounded opt-in `GiDynamicProbeReference` CPU bounce and texture-output reference with their dependency graph and tests; enforce one static GI base, one direct owner.
3. Integrate renderer-side presentation via main's real `GpuResourceRetirement` API and `Device` fence tokens. Never copy the old donor Renderer3D/Gfx files wholesale over modern main.
4. Add proper Vulkan compute and GLES CPU fallback, material receivers and actual Metal/Vulkan/GLES shader tests. Run full integrated build and GPU pixel/performance acceptance before any merge to main.
