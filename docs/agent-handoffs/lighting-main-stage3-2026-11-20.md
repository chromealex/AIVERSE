# AIVERSE lighting integration — Stage 3: bounded CPU dynamic GI reference (2026-11-20)

## Branch and exact source commits

- Repository `chromealex/AIVERSE`, isolated branch `integration/lighting-main-stage1-20261120` based originally on `main` `f9aed9a705982e2c6b77b29869e309995ce8b163` (main not modified by this integration stage).
- Donor `feature/gi-lighting-portable-backends-20261119` at observed SHA `770c7ac1d96ab752cb68bebdf34fbb26896cd335`.
- Source commit `5c3ddbc45daaabd5af08bf31edd3d5f0dd1f2cdd` imports GPU-free CPU bounded indirect lighting implementation and 3 test source files.
- CMake test registration commit `115c23de6b1e818d5ab248b7958f7c48a42af03f`, same isolated integration branch, append-only within the existing Stage1 CTest list.

## Modified files and actual capabilities

- `engine/render/GiDynamicProbeReference.h`: CPU-only bounded dynamic diffuse bounce reference using the shared immutable ray scene and opt-in moving point/spot lights, optional first-bounce sun/sky, geometry revisions and dirty ticket scheduler.
- `engine/render/GiDynamicSampling.h`: CPU tetrahedral interpolation of L1 RGB coefficients. Does not inject into existing model shaders.
- `engine/render/GiCpuTraceBudget.h`: explicit adaptive per-frame probe cost budget for GLES3/WebGL2/unsupported-compute devices, default 1.25ms / max 4 probes. **Not automatically enabled.**
- `tests/render/gi_dynamic_probe_reference.cpp`, `gi_cpu_trace_budget.cpp`: exact donor test source for actual bounce tracing and adaptive backpressure.
- `tests/render/lighting_gi_cpu_sampling.cpp`: integration fixture for interpolation of a genuinely traced material/geometry + red lamp against source coefficient and no bogus bounce from an untouched reference.
- Three further CTest registrations: `lighting_gi_dynamic_probe_reference`, `lighting_gi_cpu_trace_budget`, `lighting_lighting_gi_cpu_sampling`; total Stage1–3 lighting CTests now **9 registered source fixtures**.

## Integration constraints

- Does not change the existing main Renderer3D/Gfx, PRISMA, SpectraFX, PHYSIS, TerraWeave or ECS authoritative gameplay. Source/header opt-in only, no default lighting pipeline change.
- `GiDynamicProbeReference` is an expensive correctness and constrained CPU fallback, NOT a full-world 100k-surface 60fps global raytracer. Scheduler and mobile budget prevent unbounded per-frame work; actual runtime must provide a coherent immutable collision snapshot.
- Do not add Luma's direct light incident term into these indirect coefficients. Baked or static environment must have a selected single static GI owner; dynamic/sky GI excludes baked duplication.
- This stage **does not** implement/upload the actual RGBA32F atlas, bind the probe sampler to modern main model shaders or claim mobile GLES GPU rendering. Next step is to port/modernize `GiDynamicProbeTexture` and renderer integration with real main `GpuResourceRetirement` and `gfx::Device` fences.

## Verification truth

- PERFORMED: donor dependency and existing main `StaticDiffuseField.h`/shadow geometry inspection, isolated GitHub source commits and CMake CTest registration acknowledgements.
- NOT RUN: local CMake/GCC/Clang compile, 9 registered CTests, actual CPU profile, GPU Luma/GI pixel parity, Metal shader compilation, Vulkan compute/validation, GLES/WebGL render output, full acceptance.
- CI dispatch was not yet triggered; don't interpret GitHub source commit acceptance as a build PASS.

## Exact next step

1. Compile and execute nine focused CTests on real C++20 compiler with Release `-DNDEBUG -Werror` flags; address any direct source/runtime failures in separate commits/handoffs.
2. Integrate CPU GI RGBA32F atlas and modern Renderer3D's explicit borrow/admission, one GI owner at a time. Reuse current main retirement primitives, never replace Renderer3D wholesale with stale donor.
3. Integrate shared BVH GPU compute for Metal and Vulkan and explicit GLES LDR/CPU route with per-material receivers. Verify actual MSL, SPIR-V and GLES shader compilation on target toolchains.
4. Do not merge `integration/lighting-main-stage1-20261120` into main until full native+GPU acceptance against a single exact tested SHA is recorded.
