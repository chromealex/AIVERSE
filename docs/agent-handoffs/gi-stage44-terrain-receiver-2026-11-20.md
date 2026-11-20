# AIVERSE GI — Stage 44: real terrain shader receives dynamic GI (2026-11-20)

## Branch, implementation commits, actual status

- Branch `feature/gi-lighting-portable-backends-20261119`, draft PR #47 to `feature/hybrid-dynamic-gi` (NOT `main`). Follows Stage 43 remappable genre-neutral custom shader ABI.
- `ec9f16acf122090d038cfe24514095af08a51af0` — `modules/rts/view/RtsTerrainShaders.h`: terrain GLSL/MSL calculates `col += base * dynamicGiAt(worldPos,normal)` only under `DYNAMIC_GI`; additive incident bounce uses the *same linear diffuse base exactly once* and is applied after direct/baked decisions, before fog/tone.
- `84fec38d8ffc41ec5e2ec27a21cc9f09c66dc62a` — `modules/rts/view/RtsTerrainRender.h`: allocates 12 cached terrain shader/pipeline variants (six legacy + six opt-in GI), updates pipeline signature.
- `a508abd02896cee55e80092770486e8ed4556f59` — `RtsTerrainRender.cpp`: integrates core `GiCustomShaderSource` with live terrain shaders, injects GI code into GLSL source without rewriting terrain, and adds MSL functions after `using namespace metal` plus a GI buffer/texture fragment parameter; GI shader uses uniform slot U6 / Metal buffer(7) and sampled tex21. Pipeline descriptor increases from 21 to 22 texture slots only for GI variant, from 4 to 7 uniform slots. Shutdown now visits *all 12* shader/pipeline variants (prior loop destroyed only three of six).
- `35a9f5358590adf6352e125903c56cd31caa9d89`: draw path chooses the capability-gated GI shader inside a deferred opaque terrain pass, calls `Renderer3D::bindDynamicGiForCustomShader(Single,21)` and falls back to the existing shader if binding is refused. All legacy tex0..20 bindings remain untouched.
- `a10be4d1bddfc87245945fb474f789216c1beb30`: do not require the GI atlas to be bound at *queue* time — it can be bound after `terrain.draw()` but before `Renderer3D::render()`. Build GI pipeline only where the device has at least 22 fragment slots; select it when the queued pass actually runs.
- `7bf2f70da1ba0623a41f0fcfd8f77bfa8a616b1a`: do not fall back to a low-tier terrain shader WITH GI; fall back to legacy low instead.
- `5dc741f3b8de51408e93477deeeb6b44b3eac4c1`: `tests/render/gi_terrain_receiver.cpp` checks source contract and ownership of preexisting tex13..20, generated atlas at tex21, MSL buffer/texture args and strict GLES caps.
- `fb9124fed1404820779bb373beab1c31d9347a99`: CMake/CTest registration for `gi_terrain_receiver`.
- **Native compilation, CTest execution, real MSL/GLSL/SPIR-V compilers, GPU images, GLES/Metal/Vulkan hardware timing: NOT RUN. This is source implementation, NOT verified cross-backend feature completion.**

## Architecture and backend behavior

The shared engine remains completely genre-independent: it only supplies `GiCustomShaderSource`, `GiCustomShaderPolicy`, `Renderer3D::availableDynamicGiForCustomShader` and `bindDynamicGiForCustomShader`. One *consumer* in `modules/rts/view` demonstrates those APIs in the preexisting terrain pass; the common engine does NOT include or depend on RTS headers.

The terrain pass already binds tex13 cliff texture, tex14 cliff normal, tex15 fog vision, tex18 height-field trace, tex19/20 baked terrain. It must not overwrite any of them. The opt-in single-atlas GI variant binds tex21 and U6, reads world position and surface normal from existing terrain shader inputs, and adds incoming GI times the same terrain linear base. Both original terrain GLSL and MSL maintain direct, spot, baked, fog and tone logic unchanged when GI is off. Metal MSL additionally receives `GiDynamicU` at fragment buffer(7) and `giFineAtlas` at texture(21). Vulkan GLSL descriptor remapping uses the same declared tex21, on GPUs with sufficient sampled-image descriptors.

The pass prepares the fallback and GI pipeline separately. It defers actual availability/admission until its opaque custom draw callback, because the GI runtime may bind an atlas after the game's terrain draw is queued. Any refusal uses the precompiled legacy terrain pipeline rather than sampling unbound tex21. The pre-existing material tier and extra terrain textures are preserved. Low tier retains no GI.

**GLES3/WebGL2:** the minimum 16 fragment slots cannot fit this terrain's 21 existing bindings plus tex21 GI; it safely retains its original no-GI shader. The original terrain's 21-slot shader resource requirements may independently exceed strict GLES3 limits; this stage does not solve its pre-existing sampler pressure. The **generic model CPU-GI fallback** does work at a much smaller slot budget, but this terrain consumer is NOT guaranteed to render dynamic GI on GLES3. Do not mark terrain GI parity on GLES.

**Metal/Vulkan:** shader permutation can be selected when fragment texture capacity is >=22 and the custom GI source passes the renderer's runtime validation. Real shader compilation, GPU correctness, permission to sample atlas on all mobile GPUs and HDR/LDR interaction remain unverified. This first consumer uses only Single atlas, not masked fine/cascades; a partial fine validity mask means it returns to legacy terrain. It does not automatically bind GI into the separate TerraWeave native patch renderer.

## Known integration limits and exact next steps

1. Run native `aiverse_gi_custom_shader_remap_test`, `aiverse_gi_terrain_receiver_test` and client/terrain targets on Mac. Compile real Metal variant and Linux/Windows Vulkan SPIR-V. Fix C++ and shader errors, annotate PASS/FAIL with the tested SHA; then run full suite after all functional work as requested.
2. Actual GLSL/Metal shader add must confirm appropriate source ordering and MSL fragment argument syntax with a compiler. Source strings/CTest contract are not GPU validation. Compare color/pixel parity with GI Off vs On, especially baked terrain/shadow/alpha.
3. The isolated GI feature branch **does not contain** the newer `engine/render/TerraWeaveTerrainPatchRender.h`, `TerraWeaveGrassRender.h` and `engine/world/TerraWeaveTerrainRuntime.h` currently present on parallel/main branches. Do not copy outdated main terrain sources blindly: integrate the feature branch with current main and other agent work *with conflicts reviewed*, then adapt TerraWeave's real custom shader pipeline to the same generic engine-level API.
4. For GLES3 strict16 terrain: implement genuine compressed/simplified terrain sampled texture permutations or an alternative atlas technique, rather than claiming that a tex21 variant is usable. Keep low-end fallback when slots cannot fit.
5. Keep geometry/ECS source changes transactional using `GiWorldPresentationSync` and `GiWorldGeometryBridge`. This source implementation doesn't auto-wire PHYSIS/TerraWeave foliage or turn on GI by default.
