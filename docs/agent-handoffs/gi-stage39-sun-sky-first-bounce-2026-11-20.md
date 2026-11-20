# AIVERSE GI — Stage 39: indirect sun / overhead sky first-bounce transport (2026-11-20)

## Scope / branch / source commits

Branch: `feature/gi-lighting-portable-backends-20261119`, ongoing draft PR #47 into `feature/hybrid-dynamic-gi`, NOT main. Genre-independent, optional presentation only; does not alter gameplay/ECS authoritative state.

Implementation commits:
- `00b7b90312cd3496558c9cfcce0c5fc515c71d33` — `GiEnvironmentLight.h`: source description and value equality/validation, sun direction/color/intensity, overhead sky color/intensity. Both amplitudes DEFAULT ZERO (no baked/static GI double-count).
- `1e4401fd2206dd3c29ae22be512334a0668d7c6f` — `GiDynamicProbeReference.h`: CPU bounded visibility-tested first bounce for sun and sky onto hit albedo; environmental invalidation clears ALL L1 coefficients, invalidates scheduler revisions and optionally reports cleared IDs. No change when input equals previous source.
- `3bfdb808e7f68c6794039dd69cb36d77f9993966` — `GiDynamicRuntime.h`: `setEnvironment` forces a full invalidated-zero atlas flush at `update(0)` before further shader sampling.
- `8272ea3a25efb5712b47c580cb05664cb651360f` — `GiGpuSmallScene.h`: GPU uniform ABI grows from 32 to 80 bytes, carries normalised surface→sun direction/energy and overhead sky color/energy; supports `setEnvironment` without mutating active GPU resources.
- `bf9ee1744f98792fbbc11ce477c92a175aa74ac0`, `07c51e945d4d4a54e5c0e70d8dd8ab9d267ca7fc` — `GiGpuSmallSceneShaders.h` (MSL) and `GiGpuVulkanShaders.h` (GLSL 450) mirror CPU bounce/visibility checks and write only dynamic indirect into the same L1 atlas.
- `3523bab56ea7478049747110286d44c8d9da6fa3`, `b3a2cc5ca0009912c99334411804579287aa35df` — Vulkan compute-capability minimum push constants >=80 bytes; VkCompute pipeline descriptor and dispatch ABI changed to 80, preserving 128-byte RHI backing storage.
- `909ddf2dc244b3c270f5b9fe4d8b55a614037969`, `5c5157d038153e8997796ae2e2ddd7b6ac9edbb0` — `GiGpuDynamicRuntime` and `GiLightingController` propagate setEnvironment and dirty revisions, including in-flight stale GPU job rejection.
- `f8f560b6b7a6ce2a3d1e3e8082323a945a598558` — new `gi_environment_transport` source fixture tests CPU floor reflection, roof-blocked sky, source-off invalidation and GPU shader ABI strings.
- `499e5bf4f4d133c9f0d48c6813b33200bd577fa1` — `GiEnvironmentBridge.h` converts standard Renderer3D Lighting sun/sky into optional GI input, using explicit nonnegative bounce multipliers and normalised sun direction.
- `bb55428bbd15bdec9001513fa20c6152c9feb9f4` — `GiLightingController::syncRendererEnvironment(renderer, sunMultiplier=0, skyMultiplier=0)`.
- `fcf2e64008a2f21f60a3056c6580af614f12f97a` — `gi_environment_bridge` contract fixture.
- `f17171aa24d02d294d7a40be130f204107a0c0bc`, `10a6e25ab9844cd88ebb69230d1a832f05268c19`, `6aee88a91198662ee6a3b945cd41fa855c686ace` — extended GPU ABI/controller fixture and CTest registrations.
- `8c31234ad1bcee655274d44f9852a6e005d21894` — GPU runtime regression source for environment changes in-flight, rejects stale completed GI tickets before a new accepted job.

## Public API and lighting ownership

`GiEnvironmentLight` holds a normalised +toward-sun direction, linear sun/sky RGB and independently opt-in positive intensities. CPU, Metal and Vulkan first trace a dirty probe ray into opaque geometry, then evaluate sun radiance at the *surface* (Lambert `max(N·L,0)` plus secondary visibility ray in the sun direction) and sky radiance at that surface (bounded **single +Y overhead** visibility ray multiplied by `max(N.y,0)`). Multiply by hit albedo and project into the dynamic-only L1 probe delta. The shader does **not** add direct sun/sky at the shaded receiver. This is one bounce, not multi-order path tracing, physically correct hemispherical sky integration, atmospheric scattering, volumetric clouds or direct skylight replacement.

`GiDynamicProbeReference::setEnvironment` validates and revises the entire probe volume; CPU runtime uploadAll clears old texels even with tracing paused. `GiGpuDynamicRuntime::setEnvironment` can update CPU-side descriptor during an outstanding GPU dispatch, but it invalidates every scheduler revision so old Metal/Vulkan completed tickets are rejected. The next dispatch receives new 80-byte uniforms. No unsafe GPU buffer mutates until beginCompute. The renderer still gates binding on actual completed GPU fences and no dirty/in-flight probes.

The host chooses explicit sunBounce/skyBounce multipliers via `GiLightingController::syncRendererEnvironment`; DEFAULTS 0/0. A baked static lightmap/probe field that already includes sun or sky contribution MUST exclude the same sources from the dynamic delta or be regenerated; this API cannot remove historical baked radiance. The existing direct Lighting, LightSystem, sun shadows, tonemapping and static baked assets remain untouched.

## Actual verification and limits

**DONE:** GitHub source reads, commits and explicit CMake test registration. **NOT RUN:** native compiler/CTest, real glslang SPIR-V compile, MSL compilation, GPU Vulkan validation, Metal render readback, Android/GLES CPU/GLSL LDR output, CPU/GPU visual parity, full acceptance. Do not mark production-ready.

Test targets authored: `aiverse_gi_environment_transport_test`, `aiverse_gi_environment_bridge_test`, plus extensions to `aiverse_gi_gpu_small_scene_test`, `aiverse_gi_gpu_dynamic_runtime_test`, `aiverse_gi_lighting_controller_test`. The Vulkan push constant layout changed; actual driver/layout validation and a real sample pixel fixture are mandatory. GLES3 remains CPU GI, no compute dependence.

Limits: one secondary sun/sky visibility ray per hit, sky hemisphere approximated only in vertical direction, no dynamic sky clouds or ambient occlusion transport, no implicit auto-publish from ECS/world, and existing GPU 8192-triangle bounded scene limit remains. For large open world sunlight, need hierarchical GI/clipmap budgets and more robust sky visibility/temporal reuse.

## Next exact work

1. Add terrain and foliage integration using existing `GiWorldGeometryBridge` and `GiGeometryScene` scene source; do not replicate TerraWeave geometry or mutate ECS state. Wire real read-barrier revisions and proper CPU/GPU publication order, avoiding old/new geometry double count.
2. Verify shader graph and terrain PBR GI composition under texture slot caps; Shader Graph currently collides with `tex13+`, so do not blindly bind both.
3. When functional gaps are resolved, compile all GI targets and run native CTest, glslang/Metal/GLES shader variants, GPU pixel parity and a full acceptance suite; fix real errors and update this living handoff. Review conflicts with current main before any merge.
