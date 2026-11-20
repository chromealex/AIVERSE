# AIVERSE GI + LumaWeave — Stage 15: screen-space Vulkan PBR direct compute (2026-11-20)

## Exact branch / commits

Repo `chromealex/AIVERSE`. Branch `feature/gi-lumaweave-vulkan-screen-stage15-20261120`, based on Stage14 `feature/gi-lumaweave-vulkan-frame-stage14-20261120` at `0ae84f011fc5e1c6ede4373bb22deab36e801283`. No changes to `main`.

Source commits:
- `8f2fca5e4fd25c2b3d5e0dd028d55a0defa201d5`: new `engine/render/LumaWeaveVulkanCommonShader.h` extracts the existing GLSL struct, direct point/spot attenuation, importance sampling and **the same BVH shadow ray query** for both Vulkan kernels.
- `5fca5629b3ff7c610f4e7b002876bdad9ba1cf5a`: `engine/render/LumaWeaveVulkanDirectShader.h` now assembles its original explicit receiver kernel from the common source, preserving ABI/functions and avoiding two independent ray-tracing implementations.
- `3c0bb75d2f798c212df16989a5ec2aa41f76af57`: `engine/render/LumaWeaveVulkanScreenShader.h` new GLSL450 low-resolution fullscreen GGX DIRECT compute. Samples existing renderer textures at set0 bindings 5..9: scene depth, material RGBA16F (linear baseRGB/metallic), material receiver depth, normal/roughness RGBA16F, surface receiver depth. Rejects mismatched/depthless/invalid receivers; unprojects with GL clip-depth inverse (`depth*2-1`) because the engine's Vulkan vertex translator maps GL projection z to Vulkan [0,1]. Reuses the exact shared BVH and point/spot light importance. Output RGBA16F: BRDF-reflected linear DIRECT RGB + real scene depth in A. No reapplication of albedo is permitted in a compositor.
- `f450d4694c074bfb60958592257b9762e0f342f1`: `engine/render/LumaWeaveVulkanDirect.h` gains optional screen compute pipeline (`ComputeDesc.sampledTextureSlots=5`), bounded low-resolution output texture, `ScreenInputs`, 112-byte `ScreenView`, `screenAvailable()`, `dispatchScreen(...)`, retained output ownership and same-command-buffer encoded vs actual frame-fence completion. Original explicit receiver path remains intact.
- `ab384f95408f914550ddf7d15ac459cfbb0cf787`: `tests/render/lumaweave_vulkan_direct_shader.cpp` extends existing glslang GLSL→SPIR-V test to compile screen variant and validate all five material/surface/depth inputs and GL-to-Vulkan depth mapping.
- `57f6bbf76191c9dddc5a0623fc5ac91638d19e9a`: `tests/render/lumaweave_vulkan_direct_owner.cpp` adds FakeDevice source validation of screen input completeness, no silent partial-source truncation, 24M candidate budget, same-frame encoded output and true completed-frame serial requirement.

## GPU contract and limits

- Stage14 Vulkan RHI exposes five sampled `COMBINED_IMAGE_SAMPLER` inputs. Stage15 adds no new engine-wide RHI methods and does not touch Metal or GLES shaders.
- ScreenView SSBO uses `mat4 inverseViewProjection,vec4 eye,vec4 resolution,vec4 options` (112 bytes) at binding1. The three other SSBOs are full Luma light registry, shared BVH triangles, shared BVH nodes. Output is one RGBA16F storage+sampled image. Existing Stage13 80-byte push constants describe source count/pixel count/nodes/samples, frame seed, output width, triangle count.
- Strict cost gate `width*height*(samples+1)*sources <= 24,000,000`; pixels <=16384; sample range 1..8. **The shader never silently truncates lights**; if admission fails, the native host must use raster direct fallback, rather than presenting stale output.
- Geometry source is validated by the strict Stage62 BVH preflight. Source/geometry buffers cannot mutate while a previous GPU frame is pending; output is exposed for immediate same-command-buffer post only after Vulkan `endCompute()` transitions the storage image to shader-read. Cross-frame output requires actual `completedFrameSerial()` and healthy device.
- Lighting is **DIRECT** already material-reflected GGX+diffuse, independent of baked/static point/sun and bounced GI. Only a host choosing this route exclusively and adding the output to HDR scene exactly once is correct. Missing surface callbacks for custom terrain, water, particles or translucency must cause depth mismatch/no extra light, not synthetic material guessing.

## Real test status

- **DONE:** GitHub source operations, code and source tests committed on this isolated branch. Existing CMake targets `lumaweave_vulkan_direct_shader` and `lumaweave_vulkan_direct_owner` cover the source/test files.
- **NOT RUN:** native C++ CMake build, glslang/SPIR-V compilation, CTest, Vulkan lavapipe/validation or real GPU PBR pixels, Metal/GLES shader/pixel parity, performance. The Stage14 sampled-image GPU test and Stage12 wall-shadow GPU test are similarly authored but not executed.
- **NOT DONE:** native Renderer3D PostProcess integration/compositor, actual capture lifetimes and final per-camera material application. No claim of Vulkan production renderer readiness.

## Immediate next step

1. Stage16: add an opt-in `LumaWeaveVulkanPostProcess` that wraps the existing linear HDR PostProcess, requests two opaque material/surface capture passes using Renderer3D's existing hooks, samples the scene depth from `scenePass`, calls `dispatchScreen` inside `resolve` after scene pass ends and depth-aware additive composites the material-reflected low-resolution result into the HDR scene before downstream tonemap.
2. Stage16 native frame driver: allow explicit `enableVulkanDirect` choice only when source registry, immutable scene BVH, Vulkan screen pipeline, material capture/HDR chain and no legacy direct are ready. Never enqueue legacy point sources alongside the Vulkan direct route. On publication failure or pending GPU fence, select the existing raster fallback, never stale GPU output. Keep independent CPU GI source synchronization and per-view overlay RAII.
3. Source tests for no double direct, source/geometry changes, actual material-depth mismatch and camera/resize; dedicated lavapipe/Vulkan hardware RGBA pixel fixture. Further quality work: temporal reprojection/denoise and source/scene quality tiers, GLES3 on-device capture.
4. User requested functions first and one final full native Debug+Release/CMake/CTest/Metal/Vulkan/GLES pass. Record tested SHA, commands/results; keep PR draft until genuine execution. Each bounded stage must have its separate handoff.
