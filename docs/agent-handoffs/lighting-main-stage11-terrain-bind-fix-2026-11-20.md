# AIVERSE Lighting — Stage 11: explicit custom GI atlas slot binding correction (2026-11-20)

## Branch, commits, files
- `chromealex/AIVERSE` branch `integration/lighting-main-stage1-20261120`, DRAFT PR #91; **main not modified**.
- Code commit `54069fc272ca0ed9120281fa4b38554ea3939f19`: `engine/render/Renderer3D.h`, `modules/rts/view/RtsTerrainRender.cpp`, `tests/render/lighting_model_gi_shader_contract.cpp`.
- Stage10: `18f25c93703ed2a82a3e8288f9c4192ed345ae94` (Metal/Vulkan terrain source GI variant). Stage11 is a necessary correction before treating it as functional.

## Concrete correctness issue fixed

The Stage10 terrain GI shader correctly declares the source GI atlas at fragment `tex21`, because `tex13` remains used for authored cliff albedo. However the C++ custom-draw callback erroneously called `Renderer3D::bindDynamicGiForCustom()`, which always binds GI to `tex13`, not `tex21`. This would overwrite the cliff texture during a terrain draw and leave the actual GI shader sampler unbound, producing wrong terrain appearance or undefined sampled values. No GPU test had previously shown this as safe.

- Add `bool Renderer3D::bindDynamicGiForCustomAt(int textureSlot)` to explicitly bind only supported `13` (ordinary ShaderGraph-free custom materials) or `21` (stock terrain Metal/Vulkan variants) alongside the existing U6 parameter block.
- Retain the old no-argument `bindDynamicGiForCustom()` as a stable compatibility wrapper for slot 13; the Stage9 contract remains valid.
- Change stock terrain GI draw callback to use `r.bindDynamicGiForCustomAt(21)` only when the actual pipeline is in the opt-in GI variant cache. Legacy terra pipelines continue to bind their original `tex13` cliff albedo.
- Extend the existing C++ API fixture to check the new method type; no ABI, saves or ECS state are modified.

## Limits / tests
- This fixes the **source-level binding mismatch**, not proven Metal/Vulkan output. Native CMake/CTest, glslang/Metal shader compiler, real GPU pixel captures, GLES runtime and full AIVERSE tests are **NOT RUN**. Code acknowledged by connected GitHub only.
- `tex21` is outside conservative GLES3/GL33 physical texture-unit limits; stock terrain GI remains disabled for those backends. Do not call `bindDynamicGiForCustomAt(21)` there.
- Host owns the GI atlas and must ensure GPU completion before retiring any borrowed texture. Shader Graph owns model tex13 and is still excluded from the Stage8 dynamic GI variant.
- Next: run exact-SHA CI on PR #91, fix actual compile errors; design GLES low-tier compact terrain GI via a truly free physical texture slot and preserve baked/vision fog. Integrate remaining GPU backend work only after code and shader ABI pass.

## Actual verification
- Performed: source and GLSL/MSL shader slot inspection, connected GitHub code commit + API signature fixture update.
- Not performed: compiler, tests, GPU, device loss or profiling. Earlier stage handoffs document the rest of the branch.
