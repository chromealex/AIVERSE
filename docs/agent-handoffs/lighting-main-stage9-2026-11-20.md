# AIVERSE Lighting Stage 9 — Generic custom opaque GI receiver binding (2026-11-20)

## Exact branch and commits

- Repo `chromealex/AIVERSE`, branch `integration/lighting-main-stage1-20261120`; original main base `f9aed9a705982e2c6b77b29869e309995ce8b163`. Draft PR #91 against main; **not merged**.
- Implementation commit `6e4bf62ef41b03883c2c6864f859b5b458164855`: `engine/render/Renderer3D.h`.
- Source regression commit `5e4d4f5e737d5ea25df78b506d6827f54e860c55`: `tests/render/lighting_model_gi_shader_contract.cpp` (existing registered CTest, no new test target).

## What changed and how to use it

- `Renderer3D::bindDynamicGiForCustom()` makes the Stage8 **per-view**, already-admitted GPU atlas and U6 data available to a *custom native C++ opaque render pass*, including custom terrain, foliage, mesh and world geometry. It returns false if no explicit `setDynamicGiOverlay(bridge)` was accepted after `beginScene` or the graphics device is absent. It does not allocate textures, choose a material, modify ECS, or enable global GI.
- Call inside an actual custom renderer's graphics draw callback **after binding the custom shader/pipeline**, before its draw. The custom GLSL material must declare `U6` + `tex13` and call `dynamicGiAt(worldPosition, normal)` from `GiDynamicShaders.h`; the Metal material must declare `GiDynamicU` at `[[buffer(7)]]` and `tex13` at `[[texture(13)]]`, then add an indirect-only diffuse term. The supplied helper binds both resource and parameter block, but does not inject shader source into a foreign shader.
- Strictly treat the atlas as a borrowed handle owned by `GiMainCpuBridge`. Before shutdown, detach all custom shader sampler consumers and prove the graphics GPU queue idle; `clearDynamicGiOverlay` only clears the CPU handle and does not itself wait for GPU.
- A C++ test now has a compile-time check that this generic `Renderer3D` API remains `bool Renderer3D::bindDynamicGiForCustom()`; shader string checks remain from Stage8. This does **not** prove a custom terrain shader drew GI.

## Ownership boundaries and limitations

- Scene/Game view reset remains in `Renderer3D::beginScene`; direct Luma light ownership and static/baked GI ownership are unchanged.
- Existing builtin Terrain/foliage/Shader Graph/material capture shaders have **not been rewritten** in this stage. The new interface is host-callable only. For default terrain GI to actually appear, its shader producer must explicitly include corresponding source and invoke the hook; see next step.
- tex13 is reserved by Shader Graph in built-in ModelDraw. Stage8 excludes graph-driven models from dynamic GI to avoid binding alias. Custom shader authors must reserve tex13 themselves, or migrate to a future slot allocator.
- RHI guarantees GLSL ES3 minimum 16 fragment samplers only; Vulkan/Metal/GLES driver shader compile/pixels not yet validated.

## Truthful verification

- PERFORMED: GitHub source inspection of Stage8 renderer and shader argument layout, acknowledged source/test commits, creation of draft PR #91 to trigger actual CI.
- NOT RUN: local native compiler or CTest, real Metal/Vulkan/GLES shader compiler, raster frame and pixel capture, GPU timings and full project acceptance. At the time of authoring, GitHub CI on PR #91 was queued/in progress, not a PASS.
- Existing Stage8 CTest source includes source-string assertions; it is not an actual shader compiler.

## Next precise step

1. Inspect PR #91 latest CI logs for current HEAD, fix any actual compilation errors with code + per-stage handoff, run focused CTest label `Lighting` and report exact passing SHA.
2. Wire the portable custom GI draw hook into TerraWeave's authored terrain and foliage material shader variants with a designated sampler policy and revision-safe mesh source, rather than claiming automatic appearance from an exported hook.
3. Integrate Luma direct GPU and Hybrid GI Vulkan/Metal compute under main's shared GPU-fence infrastructure; confirm full GPU view lifetime and separate indirect/BRDF ownership.
4. Test on Metal, Vulkan (lavapipe + hardware) and GLES/WebGL2, including actual screenshots, multi-camera resize, thin foliage, moving light shadow geometry and GPU timings, then consider main merge. Do NOT merge the draft before acceptance.
