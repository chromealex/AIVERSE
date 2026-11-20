# AIVERSE GI + LumaWeave — Stage 11: Vulkan shadowed direct compute GLSL/SPIR-V source (2026-11-20)

## Scope and exact provenance

- Repo: `chromealex/AIVERSE`.
- Branch: `feature/gi-lumaweave-vulkan-direct-stage11-20261120` created from `feature/gi-lumaweave-portable-geometry-revision-stage10-20261120` at `57ef45f510e1a720302d9450dbaf038c66b1e819`.
- Stage11 source commits:
  - `83c070b170b2a9596ac2aa9b6e9f195b5cd28626` — `engine/render/LumaWeaveVulkanDirectShader.h`: GLSL 450 Vulkan workgroup-64 shader for DIRECT point/spot light with bounded, importance-sampled BVH shadow rays, no baked or bounced term.
  - `a79ad04a44de8ac2593ca2ea6fed00187ddafc15` — `engine/gfx/vulkan/VkShaderCompiler.cpp`: port the compute glslang-to-SPIR-V compiler from the existing separate `feature/gi-bvh-precision-stage62-20261120` GI branch, **without** copying its VkDevice/VkResources and accidentally overwriting newer renderer code.
  - `1dec95008185839b911d736d81ed31a7003e0dfa` — `engine/gfx/vulkan/VkShaderCompiler.h`: exposes `compileComputeProgram`.
  - `8e0e975cfa6bb68e73de55c285775ca8c99e4232` — `tests/render/lumaweave_vulkan_direct_shader.cpp`: source-level test for shared struct layouts, resource binding tokens, compiled SPIR-V magic and rejection of malformed shader.
  - `654e9471bb1dc07178d9d6b99573f5cbac1f8e96` — `CMakeLists.txt`: registers `aiverse_lumaweave_vulkan_direct_shader_test` / `lumaweave_vulkan_direct_shader` if Vulkan and tests are enabled.

## GPU ABI/algorithm

The new Vulkan-only GLSL 450 source uses **exactly the shared GI Vulkan compute descriptor ABI** that is present on the other GI portable branch: descriptor set 0 binding 0–3 = 4 read-only SSBOs, binding 4 = RGBA16F storage image, push constants = 80 bytes, workgroup size = 64 threads.

SSBO order: `LumaGpuLight` (80B, full sorted registry up to 4096) / `LumaGpuReceiver` (32B, explicitly supplied position, normal and validity) / `GiGpuBvhTri` (64B) / `GiGpuBvhNode` (48B). `LumaParams`: five 16-byte vectors = 80B, with `counts=(lights,receivers,nodes,samples)`, `control=(seed,outputWidth,triangles,reserved)`. Output per receiver is RGBA16F linear cosine-weighted incident **DIRECT** light. It is **not** a baked/indirect/GI atlas and must not be multiplied into the bounced term.

The shader uses the same radius, Lambert, spot cone and distance attenuation conventions as the existing `LumaWeaveDirectReference` and Metal direct producer. Every receiver sums importance over ALL registered lights; for each bounded sample it selects one according to its importance PDF and traces at most one occlusion ray through the shared BVH. This is a full-registry reference rather than using the Metal CSR clusters: no silent source/energy truncation on overflow, but CPU-host dispatch admission must cap `receivers * (samples + 1) * sources` before using it in production. Shader BVH traversal checks pointer and leaf-index limits, 64 stack entries and fails dark when malformed. This does not replace validation of immutable BVH packets at upload time.

### Explicit blocker: not yet a Vulkan GPU feature

**Current integration branch's `engine/gfx/vulkan/VkDevice.h` does not expose `supportsCompute` or include `VkCompute.cpp`.** The separate GPU-GI portable branch DOES contain a 4 SSBO/1 image/80 byte implementation, but it diverged substantially from this integration tree; copying the whole outdated Vulkan RHI could destroy concurrent batching/texture/render work. Therefore **Stage11 adds the shader and SPIR-V compiler/test only**: there is currently NO native producer to allocate the buffers, upload receiver/bounds, validate the BVH, encode a dispatch, wait for actual `completedFrameSerial()`, or sample/composite the output. It must NOT be described as working in a Vulkan game or producing screenshots.

This shader uses **explicit receiver buffers** rather than a depth/GBuffer fullscreen path; material GGX PBR/compositor, spatial/temporal denoise, camera cuts and actual renderer integration are separate unfinished work. GLES3/WebGL2 still uses the existing CPU GI + raster LightSystem fallback and must not attempt GLSL 450 compute.

## Compatibility

- No `main` changes; no ECS save/network schema or default renderer change; no legacy LightSystem/sun/baked duplication.
- No `gfx::Device` ABI changed yet. Only a Vulkan shader compiler helper added; existing `compileProgram` unchanged.
- MSL/GLES shader permutations, Metal compute and current RHI resource safety gates untouched.
- Vulkan graph's current 4-SSBO GI compute ABI is reused conceptually, not merged/invoked.

## What was actually tested

- **Performed:** connected GitHub source reads and writes, commits acknowledged, CMake test target registered by source inspection.
- **NOT RUN:** CMake configure/build, `ctest -R lumaweave_vulkan_direct_shader`, real glslang SPIR-V compilation, Vulkan pipeline creation/dispatch, actual BVH shadow pixels, Metal/GLES visual parity, performance, complete acceptance. The test was *authored*, not executed.
- Source fixture should be first to run when a native Vulkan SDK/glslang build is available; fix any GLSL errors from the actual compiler log. Use a real Vulkan lavapipe/device and validation layers for runtime signoff.

## Immediate next stage

1. Conflict-aware port/adapt Vulkan compute RHI from the GI portable branch onto a fresh child of this integration branch: support 4 storage-buffer inputs, RGBA16F storage output and 80-byte push constants, frame-fenced completion and texture layout barriers, **without** reverting newer `VkResources`/batching/texture code. Confirm exact `VkCompute` CMake registration.
2. Add opt-in `LumaWeaveVulkanDirect` host to publish a validated immutable BVH and source snapshot transactionally; bound receiver count/sample budget/work; enforce resource ownership and real queue-fence confirmation (no fictitious `readBufferAsync` callback).
3. Build source-level FakeDevice tests and real Vulkan offscreen pixel tests with (a) one red/blue point+spot, (b) occluding wall, (c) invalid/changed geometry, (d) light count 4096 vs bounded work and (e) no double direct/baked/GI accumulation.
4. Integrate fullscreen depth/material capture, GGX and compositing only after the explicit receiver path is validated. Maintain mobile GLES bounded raster direct and CPU L1 probe GI.
5. Finish all features first, then the owner's ONE consolidated native Debug+Release/full CTest + Metal/Vulkan/GLES visual/perf acceptance, record exact tested SHA, and only then prepare a conflict-aware merge to main. Each stage must have a separate handoff with actual test results or NOT RUN.
