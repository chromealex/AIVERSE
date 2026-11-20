# AIVERSE GI — Stage 42: custom terrain / foliage / PBR shader GI bindings (2026-11-20)

## Branch, commits, test status

- Branch `feature/gi-lighting-portable-backends-20261119`, ongoing draft PR #47 into `feature/hybrid-dynamic-gi`, not current `main`.
- `b608479acfb6bc399471e811d1d2723e04cf4c3a` — `engine/render/GiCustomShaderPolicy.h`, exact resource mode/slot contract and capacity checks.
- `898a4a7369a215af1655d66b22dd403d4fee70ca` — `engine/render/Renderer3D.h`, public opt-in `GiCustomShaderMode` binding API.
- `d7b973f166cb9df200e68a60750675a8f8d96aec` — `engine/render/Renderer3DModels.cpp`, bind the requested GI overlay U6/tex13, fine validity tex14, far U7/tex15 and Metal-only third U8/tex16 after a custom PBR pipeline is chosen.
- `957bed07885fc686815d1dc82e8173026a615501` — `tests/render/gi_texture_budget.cpp` validates exact custom shader resource modes and Metal/Vulkan/GLES caps at the source-contract level.
- **NOT RUN:** local compiler, CTest, shaders, GPU/driver validation, terrain/foliage captures, device performance, full acceptance.

## API and intended usage

`GiCustomShaderMode`: `Off`, `Single`, `Validity`, `DualCascade`, `TripleCascade`. A custom scene/material renderer (terrain, foliage, water, editor preview or other genre-neutral subsystem) compiles its shader including the correct sources from `GiDynamicShaders.h` and `GiCascadeShaders.h`, declares the matching macros (`DYNAMIC_GI`, `DYNAMIC_GI_VALIDITY`, `DYNAMIC_GI_CASCADES`, Metal `DYNAMIC_GI_TRIPLE`), sets the shader pipeline and then calls:

`renderer.bindDynamicGiForCustomShader(requested)`

Only the exact requested mode is accepted and returned. On refusal the API returns `Off` without binding any GI resources. The shader must not sample GI if that happens: custom renderers must choose the matching shader permutation BEFORE setting their pipeline and only request a mode their materials support. Do not request one mode with a shader compiled for another. It does not add GI automatically to arbitrary shader bodies.

The API checks active source ownership, LDR/HDR compatibility, available fragment texture slots, per-cell validity and far/third cascade presence. Refuses reading a partially valid fine atlas through an unmasked `Single` shader when the runtime has supplied a validity texture. Triple is Metal only. CPU GLES3/Vulkan fallback uses sampled RGBA32F and may work in LDR; GPU-written RGBA16F stays HDR-only. Uniform mapping: GLSL U6/U7 and Metal fragment buffers 7/8/9, sampled textures 13..16. Custom shaders must add dynamic-only incident to their selected static indirect owner **once** and apply albedo/AO only once. Never add to direct sun/point energy.

This is an opt-in **binding hook** and reusable header policy, not automatic terrain or foliage shader modification. A genre-neutral client still must provide its own PBR shader insertion and integrate its world-space hit position/normal; the per-material/per-pass resource layouts may differ and must be independently verified.

## Risks and required work

- Real fragment/shader-graph/terrain/GLES3, Metal and Vulkan compilation and pixel parity all NOT RUN. Tests only check constexpr slot policy and existing source ABI strings.
- Caller must ensure the renderer's borrowed GI atlases remain alive until draw completes, unbind before runtime shutdown and never use `Off` with a shader that unconditionally reads tex13. This API does not create/toggle pipeline permutations itself.
- Vulkan shader compiler's maximum uniform slot U7 and texture descriptor caps must be checked with actual drivers. Metal tex16 third remains optional and uses texture read without a new sampler state.
- Next agent: integrate the hook into actual terrain and foliage renderers with explicit shader permutations and material acceptance. Keep dynamic GI off by default for unsupported quality/backend combinations; add real GPU readback with terrain/trees and all shadow modes, then final native CMake/CTest/Metal/Vulkan/GLES compiler/driver pass. Reconcile main without overwriting concurrent render agents.
