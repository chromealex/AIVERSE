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

SPIR-V is cached in memory by source hash (a shader recreated with the same text compiles once). There
is no disk cache yet; on lavapipe the skirmish map reaches its first frame faster than GL on llvmpipe.

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

Not verified: real GPUs (Windows, Linux), the MSVC build, Android (not built here: surface lifecycle
on pause / resume, mobile drivers, `R8G8B8A8_USCALED` vertex support — without it `UByte4` attributes
read as UNORM).

## Not done yet

- compute (`supportsCompute()` is false, as on GL), GPU timings (`timeGpuFrame`), async readbacks;
- pipeline and SPIR-V disk caches, finer barriers, descriptor sets reused across draws.

## Experimental compute RHI and dynamic GI (2026-11-19, SOURCE ONLY)

This branch adds a dedicated Vulkan compute path for opt-in indirect GI. See
`docs/agent-handoffs/gi-stage30-vulkan-compute-rhi-2026-11-19.md` for exact
source commits and the required native validation. Vulkan's selected graphics
queue must also expose compute; RGBA16F needs optimal-tiled sampled **and
storage-image** format features, at least 4 storage-buffer descriptors,
one storage image, 32 bytes of push constants and a 64-thread workgroup.
Devices without those capabilities continue with the independent CPU dynamic
probe path (`GiDynamicRuntime::updateAdaptive`), never with a fake GPU result.

`gfx::ComputeDesc::glsl` is compiled to SPIR-V by glslang; `VkCompute.cpp`
encodes four storage buffers and one RGBA16F storage image via a dedicated
compute pipeline layout. The RHI transitions the atlas from shader-read to
general for compute writes and back to shader-read for lighting. Uploads of
immutable GI BVH buffers use transfer-to-compute barriers. Descriptor pools are
reused only after the in-flight frame's Vulkan fence has completed.
`gfx::Device::completedFrameSerial` is a completed-GPU-fence signal, NOT a
CPU command buffer submission timestamp; the GI ticket queue never accepts
Vulkan work before this signal confirms the matching frame.

For material shading, dynamic GI consumes tex13 (single), tex14 (R8 fine
validity), and tex15 (dual cascade). The 17th texture binding tex16 for a
third cascade remains Metal-only. The renderer checks the actual Vulkan
`textureSlots_` capacity. Shader-graph textures colliding with GI slots remain
excluded. GLSL/Metal GI transport are still separately opt-in.

**NOT VERIFIED:** Vulkan C++ compilation, SPIR-V parser/link, Vulkan validation
layers, pixel readback/real GPU fences or mobile GPU performance; GLES3 device
tests also remain outstanding. Do not ship or merge based on this source
documentation alone. The feature branch is significantly diverged from main,
and conflict-aware integration must preserve other agents' render changes.
