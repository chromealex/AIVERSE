# AIVERSE GI + LumaWeave — Stage 14: sampled textures for Vulkan compute (2026-11-20)

## Branch, exact code SHAs, ownership

Repository: `chromealex/AIVERSE`. Branch: `feature/gi-lumaweave-vulkan-frame-stage14-20261120`, created from Stage13 `feature/gi-lumaweave-vulkan-direct-owner-stage13-20261120`, head `447046eb81c49498c18c269cfcbbf7ba80604e6c`. This isolated branch does **not** change `main` or the feature parent.

Source changes:
- `006403347b10af1ca97758287acec0da863dae4f`: `engine/gfx/Gfx.h` adds optional `ComputeDesc::sampledTextureSlots` (default 0) and no-op `Device::setComputeSampledTexture` for unsupported RHI implementations.
- `2970c7a177fccdddd8b31faa343a982d13043763`: `engine/gfx/vulkan/VkDevice.h` adds per-compute pipeline sampled slot count, five sampled handle slots and a Vulkan device override.
- `25f036fb24fc7bc782f1a636cf0ed69848844658`: `engine/gfx/vulkan/VkCompute.cpp` allocates pipeline-specific descriptors for 4 SSBO/1 RGBA16F storage image plus 0..5 optional `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER` inputs at bindings 5..9; checks the sampled-handle existence and that it differs from the output; CPU device capability check for requested sampled descriptor limits; per-fence descriptor pool budget now accommodates the maximum sampler count across different compute kernels.
- `fa9230231367280d00aad50d21ea642b088d4e8c`, `8e6071876dbbfb7b919aa63394c1c0c482e6a901`: `tests/render/vulkan_tests.cpp` adds GPU-executable regression: GLSL 450 compute samples five real RGBA8 inputs, writes RGBA16F and samples that output on the immediate following fragment pass in one frame. It verifies missing input rejection, five sampler descriptors, color values and compute-write-to-fragment memory ordering. Includes needed `<array>`.

## API and compatibility

The existing `gfx::Device` Metal and GLES/GL compute implementations retain source compatibility: `ComputeDesc`'s added fourth field defaults to zero and the sampled-texture setter is a no-op unless overridden. Original GPU GI and Stage11/12/13 Vulkan direct receiver kernels still use **zero** sampled descriptors, the exact original four-buffer + one storage image + 80-byte push constant ABI. No existing GI shader, graphics pipeline, texture serialization, ECS authoring or save/replay contract was modified.

For Vulkan screen-space compute only, pass `ComputeDesc{nullptr, glsl, label, 5}` and set sampled inputs at slots 0..4 / shader bindings 5..9 between `beginCompute` and `dispatch`. The producer owns all sampled image lifetimes until the actual fence retires. Source depth/material/surface textures must be in `SHADER_READ_ONLY_OPTIMAL` after their passes; compute output returns from GENERAL to SHADER_READ_ONLY_OPTIMAL in `endCompute`, allowing immediate fragment sampling on the same command buffer. This extension does not fabricate scene G-buffers or change Metal/GLES behavior.

## Tests actually performed vs pending

- **Performed:** GitHub file reads/writes/commit acknowledgments. The Vulkan test C++ source was updated in the already registered `vulkan_backend` CTest executable.
- **NOT RUN:** native CMake/C++ compile, GLSL/SPIR-V compiler, `ctest -R '^vulkan_backend$'`, validation-layer dispatch, actual RGBA pixel capture, Metal/GLES rendering, GPU timings, all AIVERSE tests. Authored test source is not a verified PASS.

## Next action

1. Create a **Vulkan full-screen direct-light compute** shader reading the real existing Renderer3D scene depth and material/surface/depth receiver passes using sampled bindings 5..9; use GL→Vulkan depth remapping correctly when reconstructing world position from `PostFrame.viewProj.inverse()`.
2. Extend the Stage13 opt-in Vulkan owner with a bounded screen-grid dispatch and immutable/correct-view receiver snapshot. Output must contain depth for edge-aware upscale and already BRDF-shaded PBR GGX, not diffuse radiance reused incorrectly.
3. Add a Vulkan-only PostProcess adapter (not a global Renderer3D rewrite) that captures the same per-view material+normal+depth, uses the exact finished scene target, composites only when old dynamic/static point direct is excluded and depth matches, then runs downstream tone mapping. Leave GL/GLES fallback untouched.
4. Add CTest/FakeDevice and actual Vulkan lavapipe/validation tests for material-depth mismatch, metallic/black dielectric, wall shadow, camera cut, source revision and GPU-fence retirement. Do not mark PASS without native logs.
5. Perform conflict-aware reconciliation with current `main` after feature freeze; no force merge, no default enabling. User prefers all functions first and one final consolidated platform acceptance. Every bounded subsequent code stage needs a separate source commit and handoff commit recording exact real status.
