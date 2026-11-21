# AIVERSE lighting — Stage 8: first opt-in CPU GI model receiver (2026-11-20)

## Provenance / branch
- Repository `chromealex/AIVERSE`, integration branch `integration/lighting-main-stage1-20261120`, originally created from `main` at `f9aed9a705982e2c6b77b29869e309995ce8b163`. No `main` or source feature branch was overwritten.
- Source commit `746df4b5f9b1b64578defc506f201e7a8b588e99`: `engine/render/Renderer3D.h`, `Renderer3D.cpp`, `Renderer3DModels.cpp`, `ModelShaders.h`, donor `GiDynamicShaders.h`.
- Follow-up source cleanup `564b3a010e55748ea979befe7fd5c95a80a56fda` removes a nonfunctional placeholder from GI per-draw binding.
- Source test + CMake registration `4f9394a0080a6fbee447038f08502f2e418e304d`: `tests/render/lighting_model_gi_shader_contract.cpp`, appended `lighting_lighting_model_gi_shader_contract` entry to the CTest loop.
- Total new lighting CTest source fixtures registered on this integration branch: **13** (all registration/source-only until native execution).

## What was added

- `Renderer3D::setDynamicGiOverlay(const GiMainCpuBridge&)`: explicit host opt-in AFTER `beginScene` and BEFORE `render`. Validates source device, full and safe `borrowedAtlas()`, positive finite spacing and `tex13` fragment binding budget. Stores a one-view borrowed `gfx::TextureHandle` and 3xfloat4 U6 values (world origin/inverse spacing; nx,ny,nz; zero toroidal offset). `clearDynamicGiOverlay()` detaches without freeing atlas. `dynamicGiOverlayEnabled()` reports current view's admission. `beginScene` automatically clears the borrowed handle before any new Scene/Game camera, preventing cross-view material contamination.
- On ordinary eligible PBR ModelDraw paths, `Renderer3DModels::modelPipeline` uses a separate shader-key bit `1<<25`, `#define DYNAMIC_GI 1`, 7 uniform slots and 14 fragment texture slots. Sampled RGBA32F GI atlases use tex13, U6 at GLSL binding 6 / Metal [[buffer(7)]]. Bindings are explicitly refreshed for every GI draw (even on cached pipeline hits); the old shader permutations stay unchanged unless opt-in.
- Donor `GiDynamicShaders.h` includes bounded four-probe tetrahedral L1 RGB interpolation with zero outside a configured volume and fade near boundaries. On both GLSL and Metal PBR shaders, the indirect-only dynamic term is added as `diff * dynamicGiAt(worldPos,normal) * ao` **after static GI and ambient-specular calculations** and before tone mapping. It does not multiply sun, emit extra specular or overwrite the selected baked/static base. For unlit materials the existing unlit override still wins.
- Existing per-material Shader Graph starts at tex13 and may collide. Stage8 excludes Shader Graph materials instead of breaking them; ICB GPU-authored models also have a different per-instance/texture ABI and are disabled for an actively opted-in GI view, using normal CPU-submitted PBR models instead. Existing nonopted-in GPU ICB, MDI, shader graph and quality tiers retain their baseline behavior. Morph/skinned/baked rigid model variants use the existing CPU per-draw shader route, but cross-backend visual parity is unverified.
- Test source asserts assembled GLSL/MSL strings have opt-in defines, U6 and Metal buffer7, tex13, additive diffuse-only call sites and minimum fragment texture budget. These are **string/source checks**, not real shader compilation.

## Boundaries and important limitations
- **Not automatic**: host/editor/game must call `setDynamicGiOverlay(cpuGiBridge)` every applicable view after `beginScene`; there is no global allocation/registration or automatic activation. Main renderer default remains unchanged.
- This stage only targets ordinary ModelDraw PBR receivers. Custom terrain/foliage shaders, particle materials, Shader Graph and GPU ICB do **not** receive CPU GI yet. Mobile GLES2/WebGL1 are not supported; RGBA32F sampling and GLSL ES 3 compiler still need hardware tests.
- This is the CPU-probe texture presentation path. Metal/Vulkan GPU GI compute, Luma direct GPU and real device raster pixel acceptance are separate upcoming stages.
- `GiMainCpuBridge` owns the atlas. Renderer borrows it for one view and does **not** own the producer or GPU memory. Host must unbind/finish all draws, prove true graphics GPU idle before bridge teardown; current main `GpuResourceRetirement` handles old atlas versions. `beginScene` only detaches CPU handle state, not GPU fence semantics.
- Shader Graph conflict is handled by exclusion, not slot remapping. Do not widen shader-graph support without a real texture-slot planner.
- `setDynamicGiOverlay` admits the API's guaranteed 16-slot GLES3/GL model path; no backend-specific `GL_MAX_TEXTURE_IMAGE_UNITS` runtime query was added. Shader compile failures, U6/Metal reflection mismatches and correctness must be tested on target devices.
- Always-on CTest asserts via `tests/AlwaysCheck.h` prevent Release NDEBUG false passes. `Lighting` CTest label is retained.

## Actually performed vs not performed
- PERFORMED: connected GitHub read of current main shader source and renderer model pipeline, fragment slot/shader key audit, source tree/commit creation with fast-forward lease, isolated CTest source registration.
- NOT RUN: CMake, GCC/Clang/AppleClang, any of the 13 lighting CTests, assembled GLSL/MSL compiler, Metal GPU, Vulkan/SPIR-V, GLES3 render/pixel output, multi-camera regression, real material/Shader Graph visual output or performance. Code commits are not tests.
- There is no evidence Stage8 is production-ready. Fix actual compiler or GPU issues in separate code/handoff commits before merging.

## Next actionable step
1. Compile the integration branch in Debug/Release and execute `ctest --output-on-failure -L Lighting`. Confirm shader #define generation, Model/Renderer declarations and CMake registration on an actual C++20 compiler, fix any source errors.
2. On Metal test scene, render 2 cameras, GI off/on, static baked + dynamic sources, moving terrain/model shadows, texture resize and real graphics fence lifetime. Compare pixel outputs and ICB fallback.
3. Implement opt-in custom opaque terrain/foliage receiver via material/shader contracts without hardcoding RTS/FPS; then integrate Vulkan compute GI, portable GLES CPU fallback visually and Luma direct lighting without source double count.
4. Rebase/merge current main changes with lease, run final comprehensive CI + GPU acceptance against exact SHA, then promote main only after user-ready validation.
