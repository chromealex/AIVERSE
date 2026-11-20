# AIVERSE GI — Stage 43: relocatable custom PBR shader atlas bindings (2026-11-20)

## Source and branch

- Branch: `feature/gi-lighting-portable-backends-20261119`, draft PR #47 targeting `feature/hybrid-dynamic-gi`, NOT `main`.
- Source commits: `ba0419e3d00bff0e9bc227c02f4b3e79a7009f4b` (GiCustomShaderPolicy.h), `4c5f3c405ed70c7bc59bc2cf1c4ce6eac54a2c47` (GiCustomShaderSource.h), `b28db0ee87e7aa79b6f668339ffc9e8e2ced195d` (two-phase source replacement fix), `babcdd3d3b84a173efe34a0568c5f3a8aee63983` (Renderer3D.h), `ca255229038456ab3fdd5c2d599c4241eedf2ac5` (Renderer3DModels.cpp), `18160564b4029f7f77e7a9dbd967bac4f43ddd9f` (test source), `3b067be09abb7ba4d263d775057c96dcef893bb7` (CTest registration).
- **STATUS: COMMITTED SOURCE ONLY. Native C++ compiler, CTest, GLES3 shader compilation, MSL compiler, SPIR-V validation and real GPU pixels NOT RUN.**

## Why needed

The Stage 42 custom shader binding hardcoded GI at tex13–16. A real terrain pass in `modules/rts/view/RtsTerrainRender.cpp` already owns tex13 cliff albedo, tex14 cliff normal, tex15 vision, tex18 traced terrain height and tex19–20 baked terrain. Blindly binding GI to tex13 would overwrite its diffuse and fog material inputs. More general custom PBR passes/vegetation may have arbitrary reserved ranges. GI is genre-independent; the host's material layout must dictate the first free binding.

## Architecture and API

- `giCustomModeTextureCount(mode)` describes the number of contiguous sampled bindings: Single=1, Validity=2, Dual=3, Metal Triple=4. `giCustomModeFitsAt(mode,firstSlot,availableSlots,metal)` checks bounds without integer overflow. Legacy `giCustomModeFits(mode,available,metal)` still assumes firstSlot=13 for existing clients.
- `Renderer3D::availableDynamicGiForCustomShader(mode,firstSlot=13)` is a read-only preflight query for shader permutation selection. It validates device/atlas ownership, all requested masks/cascades, HDR requirements, available bindings and Metal-only triple. `bindDynamicGiForCustomShader(mode,firstSlot=13)` uses the SAME admission and writes U6/U7/U8 plus the exact contiguous texture slots. No partial downgrade and no binding on refusal.
- `GiCustomShaderSource.h` builds isolated `giCustomGlslFunctions(mode,firstSlot)` snippets by remapping **only the GI code** in `GiDynamicShaders.h` and `GiCascadeShaders.h`. The shader builder must concatenate them with its existing terrain/foliage fragment source; never mass-rewrite an authored material's pre-existing tex13..tex20 declarations. Replacement is two-stage via unique placeholders so renaming tex13->tex14 does NOT accidentally remap again to tex15. `giCustomGlslExpression` creates the standard sampler call, Metal helpers generate GI function bodies, texture/buffer argument declarations and corresponding MSL expressions. GLSL triple is intentionally rejected; Metal handles the third cascade.
- A terrain pass reserving tex0..20 selects `firstSlot=21`: Metal/Vulkan with at least 22 texture bindings can run Single GI; GLES3 with its guaranteed minimum of 16 must fall back to non-GI. A strict GLES3 device may require additional baseline terrain shader reductions separately, because that pre-existing terrain path already declares tex19/20; Stage 43 does NOT fix those unrelated legacy GLES shader limits.
- Custom shaders must select a matching permutation with the preflight result, then bind resources inside the actual render pass. The GI sampler returns incident **indirect-only** light; multiply by the receiving material's linear diffuse albedo only once, after existing baked/direct selection and before fog/tonemapping.

## Tests and limits

New `tests/render/gi_custom_shader_remap.cpp`, registered as `gi_custom_shader_remap`, asserts capability bounds, GLSL single/validity/dual remaps including tex14->tex15 chaining, MSL fragment args tex21–24/buffer 7–9, GLSL triple refusal. This is **source-authored only, not executed**, and does not prove GLSL/MSL compiler validity or actual descriptor correctness.

Next: use these engine helpers in a real terrain/foliage receiver without putting any RTS/FPS-specific code into core engine, add one capability-filtered GI shader variant and exact fallback. Then native compiler/tests and real Metal/Vulkan/GLES device image captures once the user-requested feature completion milestone is reached. Preserve the actively diverged main and parallel TerraWeave renderer work.
