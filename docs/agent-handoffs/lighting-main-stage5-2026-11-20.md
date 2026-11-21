# AIVERSE lighting main integration — Stage 5: opt-in CPU GI native bridge (2026-11-20)

## Provenance and commits
- Repository: `chromealex/AIVERSE`, branch `integration/lighting-main-stage1-20261120`. Original current-main base: `f9aed9a705982e2c6b77b29869e309995ce8b163`. Main is unchanged by this feature port.
- Source commit `8b4a16e31da07a950fca7cde5b6e924e25963430`; `engine/render/GiMainCpuBridge.h`, `tests/render/lighting_cpu_gi_bridge.cpp`.
- CMake registration commit `65c131f22f8850963e980cb1a5364ca7bb216ec5`. New test `lighting_lighting_cpu_gi_bridge`, alongside the ten earlier lighting tests; **11 tests registered in CMake**.

## What is implemented
- New genre-independent host API `GiMainCpuBridge` and `GiMainCpuConfig`, explicit opt-in CPU bounced-diffuse GI for any native project including GLES3/WebGL2 fallback. Shared immutable `GiChunkedGeometrySnapshot` is retained by `shared_ptr` until next publication, preventing a dangling borrowed BVH during CPU trace.
- `initialize(device,config)` configures `GiDynamicProbeReference`, bounded `GiCpuTraceBudget` and default sampled RGBA32F `GiDynamicProbeTexture` using the Stage4 safe retirement path.
- `publishGeometry(scene,revision)` forces previous coefficients to zero and uploads the **complete cleared atlas immediately**, before admitting further presentation, even when the per-frame tracer budget is zero. Null/invalid or stale geometry rejects and blocks `borrowedAtlas()`.
- `publishLights()` and `publishEnvironment()` propagate authoritative presentation source changes and upload cleared coefficient IDs immediately to prevent stale light leaks. Invalid or failed publication leaves the gate closed; `flushAll()` is the explicit recovery path once CPU coefficients and atlas are coherent.
- `updateFrame(focus)` consumes only `GiCpuTraceBudget::choose(pending)` tickets, uploads coalesced updated atlas rows and reports actual CPU wall time (trace+upload, **not GPU time**), pending/traced/published counts and a presentation eligibility flag.
- `borrowedAtlas()` exposes the owned sampled texture only after successful current geometry/light publication and healthy RHI. The host must attach it through an explicitly admitted material/shader path, never assume `Renderer3D` already draws GI. `collectGpuRetirements()` and `shutdownAfterGpuIdle()` enforce current-main GPU-fence/host-idle contract.
- New mock-device source fixture covers initialization, no atlas before geometry, geometry+light publication, <=2 probes/frame, rejection of invalid newer geometry, recovery with revision 2, cleared light sources, and full-idle teardown. No actual GPU driver or render output is exercised.

## Boundaries
- This stage does NOT modify active `Renderer3D`, `ModelShaders`, PRISMA, Luma direct compute, Vulkan compute backend, mobile terrain/foliage receivers or gameplay ECS. It cannot yet produce visible bounced light on rendered materials without a host opt-in binding/shader port.
- No automatic static/dynamic GI owner switch, no hidden double-counting of baked/static light or Luma direct sources, no game genre dependencies.
- Caller must ensure all borrowed atlas references are unbound before `shutdownAfterGpuIdle()` and has proven **full GPU queue idle** outside render passes; the method does not wait automatically. Raw borrow has no built-in per-renderer lease tracker yet.
- A failed texture update may throw; presentation gate remains closed until `flushAll()` completes. Device loss and out-of-memory need real hardware tests and explicit recovery policies.

## Actual verification and next steps
- PERFORMED: connected GitHub read of exact imported CPU reference/budget/atlas APIs and successful Git tree/source/CMake write acknowledgements. No overwrite of current renderer code.
- NOT RUN: CMake/Clang/GCC/AppleClang compilation, the eleven focused CTests, full CI, Metal MSL, Vulkan GLSL/SPIR-V, GLES framebuffer/output, CPU profiling or GPU timing.
- NEXT: compile focused tests against exact integration HEAD; fix source-level errors revealed by an actual compiler. Then integrate an opt-in receiver into current `Renderer3D` and `ModelShaders`, respecting main's retained batching and PRISMA ownership; implement Vulkan compute and portable GLES GI shader/fallback. Measure real GPU visuals and resource fences on Metal/Vulkan/GLES before main merge.
