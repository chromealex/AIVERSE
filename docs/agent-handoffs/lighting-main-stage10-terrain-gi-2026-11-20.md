# AIVERSE Lighting — Stage 10: opt-in terrain GI for Metal/Vulkan (2026-11-20)

## Provenance, branch and source commits

- Repo: `chromealex/AIVERSE`; working branch `integration/lighting-main-stage1-20261120`, draft PR #91 against `main`. Original baseline: `main` `f9aed9a705982e2c6b77b29869e309995ce8b163`. Main and the independent GI/Luma/TerraWeave feature branches are not modified by this stage.
- Implementation commit: `18f25c93703ed2a82a3e8288f9c4192ed345ae94`, paths `modules/rts/view/RtsTerrainShaders.h`, `RtsTerrainRender.cpp`, `RtsTerrainRender.h`.
- Fixture and registration commit: `39630ae5571eae4a06630343bc553438f4e8ae26`, paths `tests/render/lighting_terrain_gi_shader_contract.cpp`, `CMakeLists.txt`.
- New CTest `lighting_lighting_terrain_gi_shader_contract` is added to the existing `Lighting` label group (now fourteen source fixtures). **Registered is not passed**.

## What was changed

- The stock `RtsTerrainRenderer` now has separate compiled pipeline/shader permutations for ordinary terrain (slots 0–5) and optionally GI-receiving terrain (slots 6–11). The original six shader variants, default lighting and quality tiers stay selected when GI is not explicitly enabled.
- GI variants compile only when the same-view `Renderer3D::setDynamicGiOverlay(GiMainCpuBridge)` was previously accepted and the actual backend is Metal or Vulkan. They add `#define DYNAMIC_GI 1` and `DYNAMIC_GI_TERRAIN 1`; GLSL/Metal shader descriptor resources use 7 uniform blocks, 22 fragment texture slots, with GI atlas **tex21** and frame-local U6 block at GLSL binding 6 / MSL buffer(7).
- `RtsTerrainShaders.h` includes shared `GiDynamicShaders.h`: GLSL temporarily maps helper sampler `tex13` to `tex21` inside the GI helper only, then unmaps before existing cliff terrain definitions, preventing the prior occupied tex13 cliff-albedo slot from being overwritten. Metal declares GI texture21 without using a sampler beyond hardware's 16 sampler slots because the shared helper calls `read()` by texel coordinates. `tex19/20` baked lighting, `tex18` terrain traced shadow map and `tex13/14` cliff textures are unchanged.
- Both GLSL and MSL terrain fragment stages add only `base * dynamicGiAt(worldPosition,normal)` before fog/tone mapping, retaining the normal map/splat/direct dynamic light/sun/baked GI paths. This is indirect diffuse, not a second direct light or baked base.
- `RtsTerrainRender::draw` captures whether the newly compiled/fallback pipeline is really a GI variant. Its custom opaque pass calls `Renderer3D::bindDynamicGiForCustom()` only for a genuine GI pipeline. If shader/pipeline creation fails, `pipeline()` drops to the existing lower detail/no-detail/legacy terrain path instead of making the entire terrain disappear or feeding an unbound texture.
- All twelve shader/pipeline handles are now destroyed at existing TerrainRenderer shutdown rather than only the first three, which also closes an existing detail-tier handle cleanup gap. This teardown still inherits the native host's GPU-idle lifetime contract; it is not a new asynchronous retirement queue.

## Explicit platform and safety boundaries

- GPU GI for default terrain is **not** silently enabled. Caller must initialize CPU GI, publish safe geometry/lights/environment and call `r.setDynamicGiOverlay(bridge)` after `beginScene` for each camera and before `TerrainRenderer::draw`. The GI producer/atlas must remain alive until all referencing GPU draws are finished.
- This stage does NOT enable terrain GI for GLES3/GL33: high/detail terrain already occupies many fragment texture units and GLES minimum is 16; attempting tex21 on those backends could be invalid. Their existing terrain presentation remains unchanged, while Stage8 ordinary PBR models can still opt into CPU GI on compatible fragment binding budgets. A compact low-tier GLES terrain GI shader is a separate follow-up.
- This stage changes the RTS terrain presentation adapter, not universal TerraWeave geometry authoring or PHYSIS/ECS world state. Other native terrain adapters can use the generic Stage9 custom GI hook but are not automatically modified.
- Metal and Vulkan shader APIs and texture binding counts need real native compile/reflection. A completed compute callback is not proof of graphics retire, and neither the borrowed GI atlas nor the legacy terrain pipeline should be destroyed while GPU draws are in flight.

## Actually performed versus NOT RUN

- **Performed:** connected GitHub source inspection, verified shader slot collision and terrain tier/pipeline paths, acknowledged source/tree and CMake test commits. Added a source-string CTest with explicit GLSL/MSL GI binding checks; did NOT execute it.
- **NOT RUN:** local CMake/Clang/GCC/AppleClang build, focused CTest `Lighting`, actual glslang/Metal shader compilation, Vulkan validation/driver, Metal output, GLES3 GPU screenshots, multi-camera PBR visual parity, performance or entire AIVERSE acceptance.
- PR #91 CI is queued/in-progress on recent heads. Only a real passing job on the exact tested SHA may establish build readiness.

## Immediate next action

1. Inspect full CI build job logs for latest PR #91 HEAD; fix any compiler/shader build failures with a new bounded source + handoff stage. If runner starvation persists, keep status pending and use a clean native CMake build before declaring PASS.
2. Add an explicit compact GLES terrain GI variant using 0..15 sampler slots (e.g. low-tier unused slot, with strict texture-budget admission) without overwriting normal/splat/cliff/vision textures. Validate GLES3 link and pixel output; no phantom fallback.
3. Integrate Luma direct GPU and Vulkan/Metal Hybrid GI compute into main's existing renderer/RHI with proper submission/completion fence ownership. Check one direct-light owner, baked vs dynamic indirect ownership.
4. Only after exact-SHA full test/build/Metal/Vulkan/GLES acceptance, transition draft PR #91; leave `main` untouched until all gates pass.
