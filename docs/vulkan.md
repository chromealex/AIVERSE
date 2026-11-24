# Vulkan backend

`engine/gfx/vulkan` is a third RHI backend next to GL (`gfx/gl`) and Metal (`gfx/metal`). It runs on
Windows, Linux and Android. GL stays the default; Vulkan is chosen per game or per run and falls back
to GL by itself when the machine has no Vulkan driver.

## Turning it on

| where | how |
|---|---|
| game.json | `"graphics": "vulkan"` (every platform that has it) or per platform: `"graphics": {"windows": "vulkan", "linux": "vulkan", "android": "gl"}` |
| command line | `AiverseGame --gfx vulkan` (or `--gfx gl`); wins over game.json |
| environment | `AIVERSE_GFX=vulkan` / `AIVERSE_GFX=gl`; wins over both (CI, tests, the editor) |

macOS and iOS always use Metal, the web build WebGL2. When Vulkan can't start (no loader, no GPU with
graphics + present, no surface) the reason is printed (`[gfx] Vulkan: ..., using OpenGL`) and the game
runs on GL. On success the device name is printed: `[gfx] Vulkan <GPU> / <API version>`.

Other knobs: `AIVERSE_VK_VALIDATION=1` enables `VK_LAYER_KHRONOS_validation` when it is installed and
prints its warnings and errors; `AIVERSE_VK_DEVICE=<index | part of the name>` picks a GPU (default:
discrete, then integrated, then the rest).

CMake: `AIVERSE_VULKAN` (ON for Windows, Linux, Android) builds `aiverse_gfx_vulkan` and links it into
the runtime. Nothing is linked against a Vulkan loader: `libvulkan.so.1` / `vulkan-1.dll` /
`libvulkan.so` is opened at run time (`VkApi.cpp`), so builds start on machines without Vulkan.

## Same conventions as GL

The backend reproduces the GL behaviour of the RHI exactly, so the engine's GLSL and every renderer
run unchanged (`DeviceInfo::clipDepthZeroToOne = false`, `renderTargetFlipY = false`):

- **Depth range.** The vertex stage is wrapped: `main()` runs, then `gl_Position.z = (z + w) / 2`. The
  depth buffer holds GL's window depth, projection matrices stay GL's, shadow maps compare the same values.
- **Orientation.** Viewports are not flipped, so a pass is stored bottom-up as in GL: render targets
  are sampled with GL texture coordinates, `gl_FragCoord` and `dFdy` count from the bottom row, front
  faces are GL's counter-clockwise ones (`VK_FRONT_FACE_CLOCKWISE` in Vulkan terms).
- **Screen.** The RHI screen is an offscreen back buffer (RGBA8 + depth) of the framebuffer size.
  At present it is blitted upside down into the swapchain image; `copyColor({})` and screenshots read
  the back buffer. The swapchain is UNORM (the back buffer already holds display values, as GL's
  default framebuffer); FIFO present (vsync).
- **Base vertex.** `drawIndexed(..., baseVertex)` offsets the vertex buffer binding (as GL ES does), so
  `gl_VertexID` stays the plain index.
- **HDR.** In an RGBA16F pass a pipeline made for another format uses an `AIVERSE_HDR=1` variant of its
  shader, compiled once on first use (as the GL backend).

## Shaders: GLSL compiled at run time

`ShaderDesc::glslVertex` / `glslFragment` (the same GLSL ES 3.00 bodies GL uses) are translated
(`VkShaderCompiler.cpp: translateGlsl`) and compiled to SPIR-V with the vendored glslang
(`third_party/glslang`, 16.6.0):

- `#version 450`, `AIVERSE_VULKAN` defined, then the colour prefix of `ColorSpace.h`;
- uniform block `U<slot>` → `layout(std140, set = 0, binding = <slot>)`; sampler `tex<slot>` →
  `binding = 8 + <slot>` (a sampler with another name gets slot 0, as it would in GL);
- `gl_VertexID` / `gl_InstanceID` → `gl_VertexIndex` / `gl_InstanceIndex`;
- varyings get matching locations by name across the two stages (glslang's GLSL I/O mapper).

Rules for shader authors (they already hold for GL and Metal):

- uniforms only in blocks named `U0`..`U7` (std140, ≤ 4 KB each); no loose `uniform float x;`
  (Vulkan has no default uniform block: such a shader fails with a compile error);
- textures only as `sampler2D` / `sampler2DShadow` named `tex0`..`tex31`;
- one fragment output.

### Optimizer and disk cache

Every program goes through the SPIRV-Tools performance passes (vendored `third_party/spirv-tools` +
`third_party/spirv-headers`, glslang `ENABLE_OPT`; CMake option `AIVERSE_SPIRV_OPT`, ON; the grammar tables are
generated at build time and need Python >= 3.9 on the build host). Mobile drivers barely optimise SPIR-V
themselves: on a Galaxy A05s (Adreno 610) unoptimised shaders cost ~30% of GPU time (main pass 19.9 → 14.7 ms,
STRATA 20-sphere test 47 → 60 fps). `AIVERSE_VK_SPV_OPT=0` turns the passes off at run time for comparisons.

SPIR-V is cached in memory by source hash (a shader recreated with the same text compiles once) and on disk when
the platform gives a folder (`VulkanWindow::shaderCacheDir`; Android: `<internal data>/vk-shader-cache`). An entry
is keyed by the GLSL, the target, the optimizer setting and `kSpirvCacheVersion` (bump it when the translation or
the compiler output changes). Galaxy A05s, STRATA test: first frame 15.7 s on the first launch, 9.4 s with the cache.

Diagnostics: `AIVERSE_VK_GPU_TIMING=1` logs GPU time per render pass and present every 120 frames (`[vk-gpu]`);
`AIVERSE_VK_DEPS=narrow`, `AIVERSE_VK_DEPTH_STORE=0` are A/B switches for pass dependencies and depth write-back;
`AIVERSE_VK_SPV_DIR=<dir>` writes every program's SPIR-V there and loads `<key>.vert.opt.spv` / `.frag.opt.spv`
replacements (offline `spirv-opt` experiments).

## Resources and frames

- **Descriptor set 0** for every pipeline: bindings 0..7 dynamic uniform buffers (one 4 KB range each,
  offsets into a per-frame ring), bindings 8..39 combined image samplers. Unset texture slots get a black
  1×1 texture (`sampler2DShadow` slots: a depth texture cleared to 1).
- **Memory**: 64 MB blocks per memory type, first fit; buffers and images never share a block.
- **Dynamic buffers** live in host-visible memory with versions: a write never touches a version the GPU
  may still read, so a buffer rewritten between draws of one frame keeps each draw's data (GL semantics).
  Immutable buffers and textures are device-local, uploaded through staging buffers.
- **Upload order**: outside a pass an upload is recorded in the frame's command buffer in queue order;
  inside a pass it goes into a command buffer submitted before the frame (draws earlier in the same frame
  see the new contents too — the only difference from GL).
- Two frames in flight; deleted resources are freed when the GPU is done with them.
- Every image rests in `SHADER_READ_ONLY_OPTIMAL`; passes and copies return it there. Barriers are
  conservative (full) for now.

## Verification

- `aiverse_vulkan_tests` (CTest `vulkan_backend`): an offscreen device checks orientation, viewport /
  scissor, depth range and test, render target sampling, `copyColor`, dynamic buffer versions inside a
  frame, texture uploads (row pitch, R8, mipmaps), shadow compare, alpha blending, the HDR variant and the
  GLSL translation. It skips the device part when there is no Vulkan driver.
- Linux, Mesa lavapipe under Xvfb, validation layer on: every template (`--map 0 --spectate`, 90
  frames) renders without validation errors; pixel difference to GL (llvmpipe) is 0 except animation
  timing (units, particles); a post-effect chain with HDR (fxaa, bloom, sun shafts, tilt shift, colour
  grade, vignette, grain) differs by ≤ 1/255 on average; the RmlUi main menu is identical.
- Windows: MinGW cross-build (warnings as errors) run under Wine 9 with winevulkan over lavapipe —
  Win32 surface and swapchain work, the frame matches the GL one; without a driver it falls back to GL.
- Without a driver (`VK_ICD_FILENAMES` pointing nowhere) the client prints the reason and runs on GL.
- CI (`linux` job): lavapipe installed, `aiverse_vulkan_tests` and a Vulkan screenshot.

`UByte4` vertex attributes (skin joints, packed lightmap uv2) need `R8G8B8A8_USCALED` vertex fetch.
Drivers without it (Adreno 610 does not have it) read them as UNORM; the backend then builds a
vertex-shader variant per pipeline whose `vec4` inputs at the `UByte4` locations are multiplied by
255 before `main()` (`scaleUByte4Inputs`). `AIVERSE_VK_NO_USCALED=1` forces that path on any driver.

Not verified: real GPUs (Windows, Linux), the MSVC build, surface lifecycle on pause / resume.

## Not done yet

- compute (`supportsCompute()` is false, as on GL), GPU timings (`timeGpuFrame`), async readbacks;
- pipeline and SPIR-V disk caches, finer barriers, descriptor sets reused across draws.


### Canonical indexed base vertex in opt-in rigid model batches

Existing `Device::drawIndexed` retains legacy Vulkan semantics:
`baseVertex` shifts the vertex-buffer binding, leaving
`gl_VertexIndex` local. A new `Device::drawIndexedCanonical`
(default forwarding for non-Vulkan) uses Vulkan-native semantics:
bind the original VB page offset, and put `baseVertex` in
`vkCmdDrawIndexed` vertexOffset. This exactly mirrors the indexed
indirect command's addressing. `AIVERSE_VULKAN_POOLED_MDI=1`
activates it **only** for eligible static rigid pooled
Renderer3D ModelItems (not animated/morphed/skinned/baked/ShaderGraph).
Indirect with nonzero pooled baseVertex is still OFF by default
and other RHI clients keep their historical behavior.
Do not change the shader translator or force all draws to the
canonical path. Validate real Vulkan pixels/validation layers
before enabling. CPU feature stage NOT TESTED.


### GPU completion and Vulkan buffer lifetime safety (Stage 50)

Vulkan slot completion tracking now rejects all
`vkWaitForFences` failures, not just device loss, before
advancing its `completed_` serial. `completedFrameSerial()`
is therefore no longer elevated by a failed fence wait;
it returns 0 for unhealthy devices. This is necessary for
GPU-fence-gated geometry suballocation reuse. It does not
fix other higher-level device recovery paths or change
Vulkan base-vertex semantics. Not yet validated by fault
injection on a real driver.
