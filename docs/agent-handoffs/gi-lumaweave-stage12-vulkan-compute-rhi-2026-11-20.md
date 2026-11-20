# AIVERSE GI + LumaWeave — Stage 12: selectively port Vulkan compute RHI (2026-11-20)

## Exact code commits / source ownership
- Repository: `chromealex/AIVERSE`.
- Branch: `feature/gi-lumaweave-vulkan-compute-rhi-stage12-20261120`, isolated child of `feature/gi-lumaweave-vulkan-direct-stage11-20261120` at `7ad82f39228f5a453298c739ee1fdc5b66b763ab`.
- Stage12 implementation commits:
  - `fd5220782e265c2c0cc08a1049fb111f788726f6`: `engine/gfx/Gfx.h` adds optional `computeDispatchSucceeded()` query; no change to existing default behavior.
  - `024fcaf1df4852376fddaca278b9a389c9ec8213`: `engine/gfx/vulkan/VkApi.h` loads core `vkCreateComputePipelines`, `vkCmdDispatch`, `vkCmdPushConstants`.
  - `9ece880f86c31319034802f7ecc14a23fb06c505`: `engine/gfx/vulkan/VkDevice.h` adds four-buffer/one-image Vulkan compute declarations, GPU pipeline owner and per-fence descriptor pools, preserving newer rendering and explicit buffer-statistics interfaces.
  - `b5c3bc2ae426c2e28745e054e2617f2f3ff6eaf8`: `engine/gfx/vulkan/GfxVulkan.cpp` checks graphics queue compute flag, RGBA16F sampled+storage support, workgroup/push-constant/descriptor limits; initializes/destroys/reset compute pools with correct frame fence and closes any open compute at endFrame. Preserves newer successful-fence-only `completedFrameSerial` semantics.
  - `935c51cd378b6a0912be13cd00904032b99f6b4b`: `engine/gfx/vulkan/VkResources.cpp` makes RGBA16F general sampling support independent of color attachment, separately validates color attachment and storage usage, supports `VK_IMAGE_USAGE_STORAGE_BIT`, inserts transfer-write to shader-read (incl. compute) memory barriers after immutable staged buffer uploads and rewrites. Preserves newer failed-staging/failed-device-memory-bind and allocation safety fixes.
  - `a00fe5b5d2498ecaed4adee7d7363259d0baddc7`: new `engine/gfx/vulkan/VkCompute.cpp` selectively copied (blob `9b21f1ffeded8759738af39e0b2ad4a5a9b18ff9`) from `feature/gi-bvh-precision-stage62-20261120`; creates 4 SSBO + RGBA16F storage-image compute pipelines with 80-byte push constants, queue-order dispatch, same-image compute memory barriers, image swap and final shader-read layouts.
  - `7bbcd21ace4dac5cd5b8a099c4d2a391dcd2d278`: `CMakeLists.txt` compiles Vulkan compute translation unit into `aiverse_gfx_vulkan`.
  - `4db751bc0dd14982a6904e359c378510fa53d058`: `tests/render/vulkan_tests.cpp` real hardware/lavapipe-executable compute→pixel test: one intense red point light, then a horizontal BVH triangle blocking that ray. Samples output RGBA16F via the existing Vulkan tex fragment pipeline and checks lit red / fully shadowed black screen pixels.

## Actual behavior / compatibility

This is a selective port of the standalone GI Vulkan compute ABI into the newer integration Vulkan renderer, NOT a wholesale `VkResources.cpp` or `VkDevice.h` replacement. It keeps newer texture-RHI, Metal batching, CPU MDI and buffer lifetime changes. Vulkan compute is optional and false on unsupported devices; GLES/GL RHI keeps ordinary graphics and the existing CPU-probe GI/legacy direct route. Shader struct ABI: four SSBOs at bindings 0..3, a sampled+storage RGBA16F image at 4, a push-constant block exactly 80 bytes, 64 threads per workgroup; generic public `gfx::Device` compute methods are reused. `VkCompute::dispatch` explicitly fails if resources/uniforms are missing and reports `computeDispatchSucceeded()==false`.

**No actual game frame chooses LumaWeave Vulkan direct yet.** It still needs a transactional host/publisher, validated BVH uploads, bounded receiver selection, real queue-fence acknowledgement, output compositing and renderer source exclusivity, despite the new low-level test source. Do not claim that Vulkan LumaWeave is complete, or that hardware shadows/visual parity have passed.

## Actual tests and blockers

- **DONE:** GitHub repository reads from the two diverged branches; source review; surgical source commits with exact SHAs above; CMake target membership and device-pixel test source authored.
- **NOT RUN:** native Vulkan C++ compilation, CMake configure, `ctest -R '^(vulkan_backend|lumaweave_vulkan_direct_shader)$'`, actual glslang compile, lavapipe/validation-layer device, metal hardware, GLES3 shaders, real output pixels/timing/memory, full AIVERSE suite. The red/black pixel test exists but has NOT executed.
- The other GI PR #47 and Stage62 precision PR #57 are on different histories; Stage12 does NOT automatically include that branch's BVH validator. Do not upload an externally malformed GPU BVH without explicit preflight validation.
- Current integration branch is far diverged from `main` and newer renderer/texture work may have advanced further. PR remains draft; do not force-merge.

## Next exact actions

1. On a native Linux/Windows with Vulkan SDK + Mesa lavapipe/real GPU, compile first. If compiler errors, use exact GCC/Clang log and correct the Stage12 source before claiming pass. Run `ctest --test-dir ... --output-on-failure -R '^(vulkan_backend|lumaweave_vulkan_direct_shader)$'` with Vulkan validation layers and capture RGB output.
2. Implement an opt-in `LumaWeaveVulkanDirect` owner around this RHI using bounded 1..8 samples and a strict full-source candidate budget, validated source revisions and BVH packet, immutable receiver input, output texture ownership and `completedFrameSerial` fence; never infer completion from `endFrame`.
3. Add source-level FakeDevice validation for reject/accept/fail-closed paths, and host source bridge. Then integrate receiver/material capture/HDR compositor, GGX and temporal path; forbid duplicate legacy direct when GPU direct is selected.
4. Integrate the separate Stage62 BVH ULP validator and portable GI branch conflict-aware (GI latest branch had 4-SSBO compute) while respecting current `main` renderer changes. GLES3 remains the stable bounded fallback.
5. User preference: finish features first, run final full Debug+Release CMake/CTest and Metal/Vulkan/GLES visual/perf once; record actually tested SHAs/results in a new handoff. Every future bounded stage gets its own source commit(s), then separate handoff.
