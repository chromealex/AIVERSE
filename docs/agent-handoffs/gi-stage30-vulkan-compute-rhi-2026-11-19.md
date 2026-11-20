# Hybrid dynamic GI — Stage 30: Vulkan compute RHI and BVH kernel (2026-11-19)

## Branch and exact status

- Repository: chromealex/AIVERSE
- Branch: `feature/gi-lighting-portable-backends-20261119`, following stages 27–29. The draft PR #47 targets `feature/hybrid-dynamic-gi`, NOT main.
- **SOURCE COMMITTED ONLY. NATIVE BUILD, CTEST, VULKAN GLSLANG COMPILATION, VALIDATION LAYERS, GPU PIXEL TESTS AND PERFORMANCE ALL NOT RUN.** Do not assert functional GPU parity until execution proves it.
- Vulkan C++ and GLSL source commits, in order:
  - `a9cb6027ba7820b3dcb0bbe05774f6ef2ddc5c7d` Vulkan API function loader
  - `e8533077ff748bdaff034d2438cd40a3d423d8b2` VkShaderCompiler.h API
  - `0998553b08d74ba4a403b94e68b69521a395ae4a` Vulkan compute GLSL/SPIR-V compiler
  - `55262542abd814c99a64961cc637f826753ff271` Vulkan GI BVH shader
  - `9525bc3f9f25f0d3702ca1c3b912089553b31765` VulkanDevice compute types/overrides
  - `569f7010770f88ab07d80e3434aef227a4af9587` shared Device::completedFrameSerial()
  - `69ff3b2749ba504e4b2897c86ca105ab8273cc40` Vulkan capability, compute descriptor lifetimes
  - `b30fc45af84996cd3a30c6b038026297dfb1f071` Vulkan storage images and upload barriers
  - `1960d7264e96599b1603380b0c604d7fa7c7da1c`, `5c610327d123f2c0b48e993630524d2f2573dbdc` VkCompute.cpp implementation / goto fix
  - `77e78f2d3f022503327d5c289fc94a16cb34baf5`, `ab5dbc952ebf147ff54fd3cac50b4781149f8277`, `76d47d7e4162faccbd86d6e584837618f0348808` dispatch result validation hooks
  - `68d387b968ffb4cee841a2cab612d2aab443f7f4` CMake registration
  - `34a8b28babcd718d24d221f14e66be9c5b0ba346` GiGpuSmallScene Metal/Vulkan conditional and failure handling
  - `0467462f99389f5a64198385f200197f8c09029f` Vulkan VkFence-based ticket publication
  - `e90528afbb0271ff7812c8a2b756956ba235598d`, `501d6004d8c3baccae8ea6fb81e57ebbe9102fb9` FakeDevice/GLSL-ABI and frame-fence source tests.

## Architecture and APIs

1. Vulkan RHI uses its already selected **graphics+compute** queue and shared command buffer; `supportsCompute()` is true only when the queue exposes `VK_QUEUE_COMPUTE_BIT`, RGBA16F supports sampled+storage image, and descriptor/64-thread/push-constant limits are sufficient. Vulkan setup retains ordinary GL/graphics fallback when not supported.
2. `gfx::ComputeDesc::glsl` now compiles via `vk::compileComputeProgram` (GLSL 450 -> SPIR-V 1.3, Vulkan 1.1) into a dedicated Vulkan compute pipeline.
3. Compute bindings: set 0 `binding=0..3` four std430 storage buffers (64-byte Tri, 32-byte Lamp, 16-byte Job, 48-byte BVH Node), `binding=4` `rgba16f image2D`; uniform data arrives as 32-byte push constants, matching `GiGpuSmallScene::Uniforms`. A 64-thread workgroup processes dirty probes; output atlas = three float4 texels per probe (384 texels/row) with independent L1 RGB coefficients. The shader implements the same golden-angle directions, BVH traversal, visibility checks and first-bounce source math as the existing Metal prototype.
4. `VkCompute.cpp` owns pipeline, dedicated descriptor pool per dispatch per in-flight frame, storage buffer/image binding, barriers `SHADER_READ_ONLY_OPTIMAL -> GENERAL -> SHADER_READ_ONLY_OPTIMAL`, push constants and GPU dispatch. Descriptor pools are reset only after the actual per-slot VkFence. Device destruction and deferred destruction preserve GPU lifetime. Staged immutable buffer updates now synchronize to compute-shader reads.
5. `GiGpuSmallScene` accepts Vulkan when supported, otherwise defers to the existing bounded CPU tracer outside this GPU class. Metal remains supported. `gfx::Device::computeDispatchSucceeded()` prevents a false GI scheduler success if Vulkan rejected a void dispatch due to descriptor/resource failure.
6. `GiGpuTicketQueue` accepts Metal or Vulkan. Metal continues async marker readback; Vulkan captures the submitted `frameSerial()` and ACKs only when `completedFrameSerial()` reports a **fenced GPU completion**, not after `endFrame()` submission. Device loss aborts tickets, and invalidated-in-flight source revisions remain dirty. Do not call `confirmExternalCompletion` on Vulkan-owned batches.
7. GLES 3.0/WebGL2 remains CPU reference + RGBA32F nearest sampled atlas + sparse RHI texture upload (no compute requirement); no SPIR-V compute fallback is pretended.

## Compatibility and changes

- Changed `engine/gfx/Gfx.h` with two optional default-implemented virtuals; existing devices remain source compatible.
- Changed `engine/gfx/vulkan/VkApi.h`, `VkShaderCompiler.{h,cpp}`, `VkDevice.h`, `GfxVulkan.cpp`, `VkResources.cpp`; added `VkCompute.cpp` and `engine/render/GiGpuVulkanShaders.h`; CMake includes new Vulkan unit.
- Changed `engine/render/GiGpuSmallScene.h`, `GiGpuTicketQueue.h`; added Vulkan-path source assertions to `tests/render/gi_gpu_small_scene.cpp` and simulated Vulkan frame-fence success/stale/device-lost checks to `tests/render/gi_gpu_ticket_queue.cpp`.
- No legacy static baked lightmap, GI probe cache schema, gameplay/ECS state, network or saves altered. Feature still opt-in and subject to 8192 GPU triangles, 16 moving GI lamps, and 32 dirty probe jobs/dispatch. Vulkan compute does not yet establish large-world production performance, hardware ray tracing, three-cascade shader parity or LDR/mobile quality.

## Risks and unverified findings

- **HIGH PRIORITY**: Vulkan graphics descriptor cap and GLSL shader sampler budgets (tex13..15) must be tested on actual selected GPU. Metal tex16 three-cascade remains Metal-only. Textured Shader Graph models are deliberately excluded.
- Vulkan storage RGBA16F support is capability-checked; a Vulkan device without storage support must run the CPU path. Actual `VK_LAYER_KHRONOS_validation` GPU dispatch/pixel readback is necessary to prove all barriers/layouts/descriptor lifetimes.
- GLSL kernel numerics are authored to parallel MSL; actual glslang compilation, image parity, ray traversal and float16 saturation are **NOT VERIFIED**.
- New C++ methods and tests were not compiled in this connector-only environment. Do not remove the opt-in guards until a native compiler catches missed includes, API mismatches or shader issues.
- All GPU jobs must remain within beginFrame/endFrame and outside render passes. Shader/user resources must outlive borrowed overlays, and GI world snapshot publication must wait for queue completion.
- Existing branch diverges from main and must not be fast-forward assumed.

## Exactly what the next agent must execute

1. Work from this branch and reconcile current main only after running narrow validation and resolving overlaps; do NOT merge main blindly into the parallel source branch.
2. Build Vulkan RHI plus GI tests on Linux/Windows with Vulkan SDK or bundled headers/glslang:
```sh
cmake -S . -B build/gi-vk -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_VULKAN=ON -DAIVERSE_BUILD_EDITOR=OFF -DAIVERSE_BUILD_CLIENT=OFF
cmake --build build/gi-vk --target aiverse_vulkan_tests aiverse_gi_gpu_small_scene_test aiverse_gi_gpu_ticket_queue_test -j8
ctest --test-dir build/gi-vk --output-on-failure -R '^(vulkan_backend|gi_gpu_small_scene|gi_gpu_ticket_queue)$'
```
3. Validate `gi_gpu::kVulkanSmallScene` via **actual glslang**. Add a Vulkan offscreen GPU fixture comparing dynamic bounce with Metal/CPU, read RGBA16F atlas pixels after a real GPU-completion fence, enable Vulkan validation, and verify in-flight edit/revision changes.
4. GLES3: compile all PBR GI fragment variants with the GLES3 compiler; test `GL_RGBA32F` data upload, nearest texelFetch and HDR render target extension on real Android/WebGL2. Preserve no-compute CPU fallback, choose quality/probe budgets.
5. Include shader graph/terrain/foliage integration, GI/PHYSIS/TerraWeave event wiring, and major current-main merge conflict resolution. Execute all GI plus full native acceptance as ONE final pass once code is feature-complete.
6. Record **actual** compilation/test results and fixed source SHA in the next separate per-stage handoff. Until then all new work is UNTESTED.
