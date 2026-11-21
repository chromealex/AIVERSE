# AIVERSE Lighting — Stage 12: compact GLES3 terrain GI (2026-11-20)

## Repository, exact branch and commits
- Repo `chromealex/AIVERSE`; working integration branch `integration/lighting-main-stage1-20261120`, DRAFT PR #91 against main; original base `f9aed9a705982e2c6b77b29869e309995ce8b163`. `main` unchanged.
- Source commit `392422db88e8380b12767ee8eb573473a7361bc0`: `engine/render/Renderer3D.h`, `modules/rts/view/RtsTerrainShaders.h`, `RtsTerrainRender.cpp`, `RtsTerrainRender.h`, `tests/render/lighting_terrain_gi_shader_contract.cpp`.
- This is a bounded addition on top of Stages 10 and 11, which use sampler21 for Metal/Vulkan. It does not merge or overwrite unrelated renderer or TerraWeave work.

## Implemented behavior
- `RtsTerrainRender` has a **13th**, separate shader/pipeline variant for explicit GLES3 low-tier GI, beyond the 6 original normal + 6 optional Metal/Vulkan GI permutations. The compact variant is considered ONLY if `r.dynamicGiOverlayEnabled()`, backend exactly GLES3, `!detailOn_`, `!r.bakedActive()` and `!r.traceActive()`. Otherwise the existing terrain quality/legacy fallback remains untouched.
- Compact GLSL shader uses `#define DYNAMIC_GI_TERRAIN_COMPACT 1` + `DYNAMIC_GI_TERRAIN` and tier0/no-detail. Shared GI L1 interpolation remaps the GI atlas sampler from tex13 to **tex8** only inside the GI helper. Tier0 has no splat layer4 read of tex8, so the render adapter overwrites the otherwise unused slot AFTER binding ordinary terrain textures.
- Compact GLES3 shader descriptor has 7 uniform slots and 16 fragment texture slots; the fragment uses ordinary terrain shadow and cloud, but DOES NOT sample the high-slot traced terrain height texture18 or baked terrain lightmap19/mask20. Host gating ensures this variant cannot replace an active baked or traced terrain scene.
- `Renderer3D::bindDynamicGiForCustomAt` now supports only explicitly allowed 8/13/21 slots. Slot8 is not a generic permission to overwrite a material's texture: only this compact tier0 terrain path is allowed to request it. Stage11 slot21 fix for Metal/Vulkan and no-arg slot13 custom shader compatibility remain.
- `TerrainRenderer::draw` recognizes if the actual selected pipeline is a GI variant (6..12). It binds GI AFTER the regular layer texture writes, avoiding the Stage10/11 bug of overriding an atlas with the last albedo layer. A failed shader/pipeline falls back to baseline terrain without binding invalid GI.
- `tests/render/lighting_terrain_gi_shader_contract.cpp` source checks now also require the compact tex8 alias and dedicated feature macro.

## Explicit limits and ownership
- Compact GLES3 GI deliberately does **not** support terrain baked-lightmaps, traced terrain shadow heightmap or detail/splat/normal tier at the same time. Those scenes continue using the original path, preserving baked GI and avoiding sampler collision rather than silently dropping them.
- Actual GLES3/WebGL2 fragment linker limits depend on driver, active uniforms and RHI sampler mapping. A `ShaderDesc.textureSlots=16` claim does NOT prove the shader compiled on a low-end device. Need real GLES3 shader compile and pixel capture. No GL33 compact activation yet.
- CPU GI producer (Stage5) still requires explicit per-view publication and `Renderer3D::setDynamicGiOverlay()`, and retains full ownership of its atlas until the complete GPU graphics submissions finish. No ECS/save/network or automatic game enable.
- No true mobile GI performance or large-world viability established. Preserve fallback to legacy terrain on any unsupported/GPU failure.

## Verification truth / immediate follow-up
- **Performed:** connected GitHub code inspection, GLSL sampler physical-index audit and source commit acknowledgement. Existing test source extended.
- **NOT RUN:** native C++ compilation, 14+ Lighting CTests, GLES shader linking/pixel output, Vulkan/Metal shader/GPU parity, full CI acceptance or performance.
- **Next:** build and run all Lighting CTests against exact SHA; inspect PR #91 job logs and fix real errors. Test GLES3 low-tier with and without baked and traced terrain to ensure no lost features or invalid unit bindings; then integrate Luma direct Metal and Hybrid GI Vulkan compute under current-main RHI GPU fences, with full platform acceptance before main merge.
