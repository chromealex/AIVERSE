# AIVERSE texture resource unification — implementation stages

Base: `feature/universal-entity-inspector-fps` at `dd711f5e2a7f66ed25bd7330875e6e87b7109e55`.
Implementation branch: `feature/texture-rhi-unification`.

## Status — staged source implementation (not GPU-validated)

The append-only stage log covers substantial source work through 7M
(including stages 4R–4W, 5I–5O, 7A–7M and 8A). Stage numbers track feature
areas rather than chronological order. **No native C++ build,
GPU validation or CTest result has been confirmed passing for these changes.**
Do not call the entire texture stack production-ready merely because a C++ method exists.
Read the append-only `docs/agent-handoffs/texture-rhi-2026-11-18.md` for per-stage
SHA, unverified platforms and the next exact task.

- **Dimensions:** Metal/Vulkan have native sampled 1D/2D/3D/2DArray/Cube/CubeArray
  paths; the portable GL33/GLES3 subset has 2D/3D/2DArray/Cube, not 1D/CubeArray.
- **Texture views/samplers:** Metal and Vulkan have subresource views and independent
  samplers; GL has independent samplers but only *whole-resource identity* views.
- **Offscreen passes and MRT:** `RenderTargetPassDesc` selects mips/layers and
  1–4 color attachments, each with its own clear operation, plus optional depth.
  Metal, GL33/GLES3 and Vulkan have source implementations for 1–4
  color outputs, including mixed-format MRT with per-slot pipeline checks. Metal and Vulkan graphics pipelines expose explicit
  color output counts and additional per-slot formats. GLSL/MSL shaders must
  explicitly write the corresponding fragment output locations. Native
  cross-backend GPU validation is still outstanding.
- **MSAA:** the shared descriptor/resolve contract and Metal allocation, pipeline
  sample counts and per-color resolve operations are implemented in source.
  GL uses multisample renderbuffers with per-color framebuffer blit resolves;
  Vulkan has multisample images and renderpass resolve attachments. None
  of these paths has a confirmed passing cross-backend GPU result. Tests in `tests/render/vulkan_tests.cpp` include
  MRT clear/output and nonzero mip/layer rendering, but remain unexecuted.
- **Reusable native owner:** `engine/gfx/RenderTargetSet.h` provides a
  genre-agnostic owner of color/depth targets and optional MSAA resolve targets.
  `begin`/`end` wrap an explicit offscreen pass; allocation failure rolls
  back all created handles. `tests/render/render_target_set.cpp` is a CPU-only
  mock-device lifetime and rollback test registered as `render_target_set`.
  This is separate from the existing Lua `render::RenderTexture` paint canvas.
- **Import:** KTX2 and the new `DdsTexture.h::parseDdsTexture` both preserve
  array layers, cube faces and volume slices in a shared `CompressedImage`
  representation. DDS surface-major mip chains are reordered to mip-major.
  `TextureAsset.h::uploadContainerTexture` converts either format to
  GPU resources (or CPU RGBA8 fallback) with authored mipmaps, while
  `uploadKtx2Image`/`uploadDdsImage` remain convenience aliases. Existing
  model `sampler2D` imports intentionally keep their first 2D slice.
  A declared Texture3D stays a 3D resource even when the source volume
  contains only one depth slice (KTX2 pixelDepth>0 or DDS volume dimension).
  When native BC allocation fails, the RGBA8 CPU fallback clears compressed
  mip pointers/count and retains the authored mip chain; cube-array fallback
  also has 12-face regression coverage. Malformed layer-count overflow and
  contradictory 1D/3D metadata are rejected before resource creation.
  These are source-level fixes, not GPU-certified behavior.
  HDR RGBE and a limited uncompressed single-part scanline OpenEXR
  HALF/FLOAT RGB(A) importer are now available in native C++; BC6H signed
  and unsigned native HDR compression is mapped by backend. Complete EXR
  compression (ZIP/PIZ/etc.), DDS signed/exotic formats, non-tight pitch,
  BasisLZ/UASTC transcoding and all compressed-sRGB variants remain unavailable.
- **Compressed formats:** source-level native mapping now includes BC4,
  BC5 (RGTC) and EAC_R11 across Metal/GL/Vulkan, subject to GPU capability.
  Shared block-size helpers reject missing/truncated mip and face payloads
  before GPU allocation, and unsupported compressed formats fail closed.
  For BC5 normal maps, native GPU sampling produces RG without reconstructed
  blue normal Z; the default generic importer therefore uses CPU RGBA
  reconstruction. Set `TextureUploadOptions.shaderReconstructsBc5NormalZ=true`
  only when the consuming shader reconstructs tangent-space Z.
- **Typed data formats:** `RG8`, `R16F`, `RG16F`, `R32F`,
  `RG32F` are additionally mapped in Metal/GL/Vulkan, with shared
  CPU texel byte strides and subresource upload paths. These formats
  are currently **sample-only**; render target requests and unverified
  automatic mip generation are rejected. Explicit `mipLevels` and
  subresource updates can be used to upload authored mips. Float32
  sampled data conservatively uses nearest filtering on Metal/GLES3.


| Stage | Deliverable | Acceptance |
|---|---|---|
| 1 | Public TextureDimension/TextureDesc/TextureViewDesc/SamplerDesc/TextureUploadDesc, validation and backend fail-closed | Header compiles, descriptor tests pass, existing texture rendering unchanged |
| 2 | Native Metal + GL/GLES + Vulkan 1D/2DArray/3D/Cube/CubeArray allocation, upload per subresource, sampled bind | Real per-backend device tests; shader sampling on every requested dimension |
| 3 | Native views and independent sampler objects with matching bindings | Create/destroy/bind view, anisotropic and compare samplers; preserve existing setTexture |
| 4 | RenderTexture abstraction: layer/mip targets, MRT, depth, MSAA/resolve, storage images | Render pass and readback test for each backend; explicit resource transitions |
| 5 | Additional typed pixel formats (R/RG float/int, BC4/5/6H), full sRGB and HDR/EXR | Format capability checks and cross-platform fidelity tests |
| 6 | Complete KTX2 and DDS array/cubemap/3D payloads, transcoding, import rules | Container roundtrip, bounds/fuzz tests, material/glTF integration |
| 7 | Async texture upload/readback, streaming/budget/residency, optional virtual textures | Resource-lifetime stress tests, no read-after-free, deterministic fallback |
| 8 | Editor resource inspection + feature tests and docs | GPU validation (Metal/Vulkan/GL), mobile/web compatibility gate, README translations |

## Native streaming and HDR import — source-only additions

- `engine/gfx/TextureStreaming.h`: owned GPU texture allocation, deep-copied
  CPU staging, FIFO per-frame byte/update limits and GPU completion callbacks.
- `engine/gfx/TextureMipResidency.h`: generation-safe coarse-to-fine residency
  tracking per mip and array/cube face; no sampler until every coarsest face
  completes; clamp LOD to contiguous populated levels.
- `engine/gfx/TextureResidentStreamer.h`: combines both APIs into a
  `gfx::TextureResidentStreamer` native facade. `stageNext` obtains packed
  mip/layer/volume bytes from a caller-provided producer, and `drain`
  submits them on the render/device thread. `sampledTexture` never exposes
  incomplete or failed resources. `destroy` cancels pending uploads.
- `engine/render/HdrTexture.h`: `parseRadianceHdr` / `loadRadianceHdr`
  for Radiance RGBE in linear RGBA16F.
- `engine/render/ExrTexture.h`: `parseOpenExr` / `loadOpenExr`
  for single-part uncompressed scanline OpenEXR with HALFs and FLOATs.
  Compressed/tiled/deep/multipart EXR is deliberately rejected.
- `BC6H_UFloat` and `BC6H_SFloat` preserve signed/unsigned HDR block
  payloads for DDS/KTX2 and map them to capable Metal/GL/Vulkan hardware.

**GPU readback:** `Device::readTextureAsync` is an opt-in mip/face
readback API. Metal uses shared-buffer blit, GL33/GLES3 use PBO+
nonblocking fence polling, and Vulkan uses host-coherent staging
with completed frame serials. All accepted callbacks run on the
device owner thread in later frames. WebGL2 explicitly refuses
native async readback. No actual GPU backend readback tests have
completed yet.

**Virtual textures:** `gfx::VirtualTextureStreamer` composes the
`VirtualTexturePageCache`, `VirtualTexturePageTable` and GPU staging
queue. It allocates a fixed-size Texture2DArray physical atlas and
RGBA8 page table, invalidates old mappings before LRU reuse, and
publishes uploaded pages only after successful transfer. The
`VirtualTextureSampling.h` GLSL/MSL helpers expect pre-guttered
physical pages. This is not a sparse image API, feedback pass or
background disk worker; these remain separate work.

**VRAM and virtual texture caveat:** Ordinary TextureResidentStreamer
allocates the full mip chain up front and budgets *uploads* rather than
physically uncommitting mip memory. The separate VirtualTextureStreamer
now contains a fixed physical Texture2DArray atlas, an LRU physical-page
cache, a GPU-uploadable page table and reusable GLSL/MSL lookup helpers.
Hardware sparse textures, automatic GPU feedback, a disk I/O worker and
production GPU sampling validation are **not implemented/verified**.

**Testing caveat:** Headers and test fixtures have been committed, but
native compilation, CTest, API validation and GPU sampling remain
**NOT RUN**. Keep PR #21 Draft until all verification gates pass.

## Current compatibility constraints

- `gfx::TextureDesc` and `PipelineDesc` new fields are appended with legacy 2D
  and 1x-sample defaults. Existing `PassDesc`, `beginPass`, `setTexture`
  and `render::RenderTexture` remain available.
- A successful `beginRenderTargets` is ended with `endPass()`. It is offscreen
  only. Check its boolean return before issuing draw commands; unsupported
  MRT/resolves must fail explicitly, never downgrade.
- Render-target textures need `renderTarget=true`; color/depth attachments must
  have matching extent and sample counts at the requested mip.
- Metal MSAA pipelines explicitly set `PipelineDesc.sampleCount` to match
  multisample attachments; default 1x pipelines cannot draw into MSAA targets.
- New GPU texture handles are presentation state only, not ECS-authoritative.
  No changes to serialization, game saves or replication.
- **Known missing pieces:** automatic GPU feedback/disk paging, sparse
  hardware residency, fully verified virtual texture shaders, integer/packed
  formats, typed color targets, Basis transcoding, compressed/tiled/multipart
  EXR, exotic DDS variants and real-device validation of MRT/MSAA/readback.
- The old `TextureCache` and `render::RenderTexture` still use 2D-only paths.

## Example: selected array layer and mip

```cpp
gfx::TextureDesc tex;
tex.width = 512;
tex.height = 512;
tex.dimension = gfx::TextureDimension::Texture2DArray;
tex.arrayLayers = 4;
tex.mipLevels = 3;
tex.renderTarget = true;
gfx::TextureHandle target = device.createTexture(tex);

gfx::RenderTargetPassDesc pass;
pass.colorCount = 1;
pass.colors[0].texture = target;
pass.colors[0].mipLevel = 1;   // 256x256
pass.colors[0].arrayLayer = 2;
pass.colors[0].clearColor[3] = 1.0f;
if (device.beginRenderTargets(pass)) {
    // Set a compatible pipeline and issue draw calls.
    device.endPass();
}
device.destroyTexture(target);
```

The client must keep the texture alive until all GPU work completes and must
never sample a subresource while simultaneously writing to that subresource.
Use a matching `TextureViewDesc` and independent sampler for post-pass reads
where supported. The code is a usage example, not a claim of verified GPU
execution on this branch.

## Native offscreen render target owner

```cpp
#include "engine/gfx/RenderTargetSet.h"

gfx::RenderTargetSet targets;
gfx::RenderTargetSet::Config cfg;
cfg.width = 1024;
cfg.height = 512;
cfg.colorCount = 2;                  // two same-format color targets
cfg.depth = true;
cfg.mipLevels = 1;
if (targets.init(device, cfg)) {
    if (targets.begin(0, 0, true, true, {0, 0, 0, 1})) {
        // Use a matching PipelineDesc.colorAttachmentCount=2 and a shader
        // writing both fragment color outputs at locations 0 and 1.
        targets.end();
    }
    // Sample via targets.sampledColor(0/1) in later render passes.
    targets.shutdown();  // required before the Device is destroyed
}
```

For MSAA, choose `cfg.sampleCount = 4`; the owner allocates a matching
single-sample resolve target per color. If the backend cannot implement
the selected render pass, `targets.begin` returns false, not a fallback.
Always check `init` and `begin` results. These APIs are under native
verification and should not be assumed to run on every device.

## KTX2 arrays, cubemaps and 3D volumes

```cpp
#include "engine/render/TextureCodec.h"
#include "engine/render/TextureAsset.h"

// ktx2Bytes are the encoded KTX2 container bytes read by the caller.
aiverse::render::CompressedImage source;
std::string error;
if (aiverse::render::parseKtx2(ktx2Bytes, source, error)) {
    aiverse::render::TextureUploadOptions opt;
    // For native compressed sRGB GPU paths ONLY when the material shader
    // explicitly decodes sRGB; otherwise use the RGBA8_sRGB CPU fallback.
    opt.trustShaderSrgbDecode = false;
    auto tex = aiverse::render::uploadKtx2Image(device, source, opt, error);
    if (tex.valid()) {
        // Bind only with a shader sampler matching the imported dimension.
        // Destroy the texture when the consuming resource ends its lifetime.
        device.destroyTexture(tex);
    }
}
```

`CompressedImage::levels` keeps all faces/layers/slices for every mip.
`decodeBlocksSlice(source, level, index, rgba)` retrieves a specified
2D slice for CPU processing. The generic uploader does **not**
automatically convert a glTF material's existing 2D sampler into an
array/cubemap shader. Native SDK builds and GPU tests are still pending.

## DDS DX10 arrays/cubes/volumes

```cpp
#include "engine/render/DdsTexture.h"
#include "engine/render/TextureAsset.h"

aiverse::render::CompressedImage dds;
std::string err;
if (aiverse::render::parseDdsTexture(ddsBytes, dds, err)) {
    aiverse::render::TextureUploadOptions options;
    // BC5 default: CPU reconstructs normal Z into RGBA8.
    // Opt in to native BC5 ONLY if the texture shader reconstructs Z.
    options.shaderReconstructsBc5NormalZ = false;
    auto handle = aiverse::render::uploadDdsImage(device, dds, options, err);
    if (handle.valid()) {
        // Bind a matching shader sampler; release after GPU usage ends.
        device.destroyTexture(handle);
    }
}
```

The old `decodeDds` material adapter still reads the first face and uses
the 2D model-shader path. The new `parseDdsTexture` is the separate
dimension-aware container API; it intentionally does not support every
historical DDS pixel format. DDS layout is reordered from per-surface
mip chains into complete mip levels for the RHI. No GPU tests have been
run on this branch.

## Disk-backed virtual pages — source implementation (unverified)

\`engine/gfx/VirtualTextureDiskQueue.h\` provides an actual background
filesystem worker reading immutable packed RGBA8 page files from
\`m<MIP>/l<LAYER>/x<X>_y<Y>.rgba\` inside a trusted game asset root.
It validates dimensions/file size, deduplicates requests, supports
cancellation, and enforces outstanding request and RAM result budgets.
Polling and GPU work remain on the render thread.

\`engine/gfx/VirtualTextureFileStreamer.h\` combines this disk source
with the existing physical \`VirtualTextureStreamer\` atlas and GPU
page table. \`requestPage\` accepts explicit virtual tile requests;
\`pump\` stages ready disk pages to the GPU, handles backpressure,
and publishes shader mappings only after GPU upload completion.
A missing/truncated file never becomes a resident GPU page.
The game must still request pages via an application prefetch
policy or future GPU-feedback pass. Native GPU/compiler tests have
not yet been executed.

New CPU CTests \`virtual_texture_disk_queue\` and
\`virtual_texture_file_streamer\` exercise actual temporary files
and a mock GPU, respectively.

## Visibility-based virtual texture prefetch

`engine/gfx/VirtualTexturePrefetch.h` defines a CPU-only, genre-agnostic
UV visibility planner. A game passes normalized visible UV rectangles,
logical virtual layer, desired mip and priority. It validates/clamps
bounds, deduplicates overlapping regions, plans coarsest mip tiles
first, optionally pins fallback pages, and limits the work using
`maxRegions`, `maxCandidates`, `maxExaminedPages` and `maxRequestsPerFrame`.
The separate `maxExaminedPages` limit bounds CPU work including
already-resident/coarsely-loaded tiles, whereas `maxCandidates`
limits only tiles that still need uploading. This prevents a
large resident coarse mip from starving missing high-detail tiles.

`engine/gfx/VirtualTexturePrefetchAdapter.h` connects those plans
to the disk-backed `VirtualTextureFileStreamer` without putting
disk file reads or GPU uploads on the visibility thread. It uses
**exact** page residency and pending-request checks, not just
sampleability through a coarser fallback. Coarse pending requests
can be promoted to pinned fallbacks. `prefetchVisibleVirtualPages`
returns disk request tickets; the caller still invokes
`VirtualTextureFileStreamer::pump` once per render frame.

Visibility projection, asset UV-to-world mapping, per-surface
LOD selection and optional GPU-feedback passes remain game/editor
choices. The planner is presentation state, not authoritative ECS.
Coverage is source-level only: native C++/CTest/GPU validation remains
outstanding. The `virtual_texture_prefetch` CTest and the existing
`virtual_texture_file_streamer` CTest contain authored cases.

## Owned feedback render target

`engine/gfx/VirtualTextureFeedbackPass.h` is an opt-in,
genre-independent owner of the feedback color target and
the Stage 7AC async readback runtime. It builds one
Texture2D RGBA8 UNORM offscreen render target through
`RenderTargetSet`, no depth, no sRGB, no MSAA, one mip.
`begin()` clears to zero and configures the pass viewport;
the game binds its opaque one-color-output feedback shader
and draws the visible UV surfaces; `endAndCapture()` ends
the pass before requesting nonblocking readback. On a later
frame `submitVisiblePages(fileStreamer)` submits missing
pages through the generic UV prefetch and disk worker queue.
Call `fileStreamer.pump()` separately for GPU uploads.

This helper does not own a camera, geometry, render graph,
shader compilation or game ECS state. It must be destroyed
before `gfx::Device`; late callbacks are ignored using weak
state. The corresponding GPU-independent mock CTest is
`virtual_texture_feedback_pass`. It has NOT been executed
against native SDK builds or GPU drivers.

## Optional GPU feedback → prefetch source pipeline

`engine/gfx/VirtualTextureFeedback.h` adds a bounded 32-bit tile
feedback ID layout in a **linear RGBA8 UNORM** render target:
11 bits x + 11 bits y + 5 bits mip + 4 bits virtual layer + valid bit.
Zero is reserved for unused pixels. GLSL and MSL fragment helpers
encode the same ID, and `VirtualTextureFeedbackDecoder` refuses
non-RGBA8, malformed/truncated or over-budget readback results.
It groups visible pages by frequency and produces normalized
`VirtualTextureVisibleRegion` requests for the genre-independent
coarse-first prefetch planner.

`engine/gfx/VirtualTextureFeedbackRuntime.h` connects the optional
`Device::readTextureAsync` to the CPU decoder and then to
`prefetchVisibleVirtualPages`. It owns no render pass, shaders,
images or feedback draw commands. Game/editor render code must
generate feedback with **no sRGB write conversion, blending,
MSAA, dithering or postprocess**, end the feedback pass, call
`capture(feedbackTexture)` and later `submit(fileStreamer)` after
async callbacks have arrived on the graphics thread. Continue
calling `fileStreamer.pump()` to upload physical pages.
The runtime uses a weak callback state, allowing destruction
before a GPU command completes without dangling owner pointers.

This is an opt-in shader feedback path, **not** automatically
integrated in the editor, camera, material graph or Terra.
Only Metal/GL33/GLES3/Vulkan have source readback code; WebGL2
fails closed. Real shader compilation, per-backend render capture
and readback accuracy remain NOT RUN. The CPU contracts are
`virtual_texture_feedback` and `virtual_texture_file_streamer`.

## Live GPU texture inspector — source-only

`Device::textureResources()` now enumerates user-created live textures from
Metal, GL and Vulkan backend pools. The returned `TextureResourceInfo`
contains dimension, format, extent, mip/layer/sample counts, resource flags
and an approximate `estimateTextureFootprint` size. It is an estimate,
**not a driver VRAM allocation/residency query**; swapchain and internal
dummy resources are excluded.

In the editor, open **Графика → GPU Текстуры**. The window is closed by
default; it supports filtering, sorting and an optional async mip/layer/face/volume-slice
CPU preview on supported uncompressed formats (not depth/MSAA/blocks). Neither the editor
window nor the backend enumeration has been compiled or tested on a native
target in this environment.


## Native compressed sRGB formats

The shared RHI provides separate sRGB GPU variants for BC1/BC3/BC7,
ETC2 RGB/RGBA, and ASTC 4x4/6x6. When DDS or KTX2 metadata declares
sRGB and the device supports its compressed variant,
`uploadContainerTexture` selects the hardware-decoded sRGB format
without requiring shader-authored gamma conversion. Backend support
varies; GL/GLES/WebGL2 must probe the dedicated sRGB S3TC extension.
Unsupported compressed sRGB formats fall back to CPU RGBA8_sRGB if
a decoder exists. ASTC without native support still fails rather
than treating block bytes as RGBA pixels. BC1_RGB opaque blocks
are not silently reinterpreted as BC1_RGBA blocks.

`TextureUploadOptions.trustShaderSrgbDecode` remains a legacy
opt-in for callers that have explicit shader decode semantics;
it is not needed for the new true sRGB GPU block formats.
See KTX2 and TextureAsset CPU tests for the format contract.
**Native GPU and shader validation has not yet run.**

## Optional Basis Universal KTX2 bridge

`engine/render/Ktx2Transcode.h` accepts an externally supplied
`Ktx2Transcoder` callback for ETC1S/BasisLZ and UASTC KTX2.
The engine checks the original header/level index, rejects broken
dimensions and supercompression, and requires the transcoder to
return the same dimensions, layers, faces and number of mip levels.
Unknown native block formats, incorrect mip sizes and excessive
decoded memory fail closed. Regular KTX2 bypasses the callback.

This standalone callback is only an integration boundary. To use a
**real** Basis Universal LDR transcoder, opt in to the system Khronos
`libktx` adapter in `engine/render/Ktx2Libktx.h`:

```sh
cmake -S . -B build/texture-rhi -DAIVERSE_TEXTURE_WITH_LIBKTX=ON
```

Install the matching system `ktx.h` / `libktx` development package
first. `render::uploadKtx2WithLibktx(device,ktxBytes,options,error)`
selects BC7, ASTC 4x4, ETC2 RGBA or BC3 using the RHI capability
query; it otherwise falls back to RGBA8 if permitted. The adapter
is completely opt-in; no codec is vendored and default builds
still fail Basis input explicitly. For **UASTC HDR**, use the separate
`render::uploadKtx2UastcHdrWithLibktx(device,ktxBytes,error)`
API. It uses system libktx's `KTX_TTF_RGBA_HALF` path,
validates original mip/face/array/volume topology, and uploads
a full mip chain as linear RHI RGBA16F (never RGBA8). This
path requires a GPU with matching RGBA16F dimension/support.
Native BC6H or ASTC HDR compressed import is still separate.
The HDR code has a public-header compile fixture but **no**
production encoded HDR asset/GPU fidelity verification yet. Check the upstream
license and real GPU output before releasing.
Test source: `tests/render/ktx2_transcode.cpp` (`ktx2_transcode`).

## Optional OpenEXR 3 compressed HDR import

With `-DAIVERSE_TEXTURE_WITH_OPENEXR=ON` and a compatible
system OpenEXR 3 package, include `engine/render/ExrOpenExr.h`
and use `parseOpenExrWithLibrary(bytes,image,error)` or
`loadOpenExrWithLibrary(device,bytes,error)`. The external
library decodes scanline ZIP/ZIPS/PIZ and other supported
first-part RGBA files to linear RGBA16F; no exposure/tone
mapping is implicit. Multipart/deep workflows remain
unsupported by this adapter. Default builds still use
the narrow dependency-free uncompressed EXR parser.
CTest `exr_texture` includes conditional real compressed
image fixtures when OpenEXR is enabled, but they are
NOT RUN on a native OpenEXR SDK in this branch.

## Vulkan writable compute textures

`VulkanDevice` now has opt-in `Device::supportsCompute`,
`Device::supportsStorageTexture`, and the normal
`createCompute` / `beginCompute` / `bindComputeStorageTexture` /
`dispatch` / `endCompute` methods. Vulkan compute source is
GLSL450 **without** a `#version` directive; the shared
glslang compiler emits SPIR-V. Bindings: set 0, storage
images 0..3, sampled images 4..11, storage buffers 12..15,
uniform buffer 16. Storage images are conservatively
limited to RGBA8/RGBA16F and 2D/2DArray/3D devices that
advertise STORAGE_IMAGE and SAMPLED_IMAGE format features.
Default X workgroup size is 64, as on Metal. GLSL shader
bindings must match these declarations; neither Vulkan
nor GL silently emulates Metal shader source.

The Vulkan test `vulkan_backend` now contains an authored
compute shader writing known RGBA8 colors and reading them
back asynchronously. All Vulkan shader compilation and
hardware/lavapipe test execution remain **NOT RUN** until
a runner actually executes them. GL33/GLES3/WebGL2 currently
retain explicit `supportsCompute=false` rather than
pretending to support 4.3+ compute.

## Editor volume texture inspection

The GPU Texture Resources window now has a per-mip depth
slice selector for Texture3D, in addition to existing
2DArray/Cube face/layer selection. Native Metal, GL and
Vulkan async readback code supports uncompressed 3D
RGBA8/sRGB/16F slices (GL still limited to RGBA8/sRGB),
subject to the 16MiB preview staging cap. The preview
remains a CPU thumbnail/diagnostic and is not a driver
VRAM residency query; compressed/depth/MSAA readback
still fails explicitly. WebGL2 has no native async
readback and should instead use CPU UV prefetch.

## Focused automated verification

`.github/workflows/texture-rhi.yml` defines an opt-in
OpenEXR OFF/ON Linux test matrix and a Vulkan CMake/CTest
job with uploaded logs. The existing
`tools/verify-texture-rhi.sh [--vulkan] [--editor]`
may also be run locally. A queued/cancelled CI run is not
a PASS. Vulkan CTest may skip the driver if no Vulkan
device is available; inspect the job logs before
claiming real storage-image validation.
## Native build and targeted tests (pending execution)

```sh
git switch feature/texture-rhi-unification
git pull --ff-only origin feature/texture-rhi-unification
cmake -S . -B build/texture-rhi -DAIVERSE_BUILD_TESTS=ON
cmake --build build/texture-rhi --target \
  aiverse_texture_contract_test aiverse_render_target_set_test \
  aiverse_ktx2_layers_test aiverse_ktx2_transcode_test \
  aiverse_dds_layers_test \
  aiverse_texture_asset_test aiverse_texture_streaming_test \
  aiverse_texture_mip_residency_test aiverse_texture_resident_streamer_test \
  aiverse_virtual_texture_pages_test aiverse_virtual_texture_page_table_test \
  aiverse_virtual_texture_streamer_test \
  aiverse_virtual_texture_disk_queue_test aiverse_virtual_texture_file_streamer_test \
  aiverse_virtual_texture_prefetch_test aiverse_virtual_texture_feedback_test \
  aiverse_virtual_texture_feedback_pass_test \
  aiverse_hdr_texture_test aiverse_exr_texture_test
ctest --test-dir build/texture-rhi --output-on-failure \
  -R '^(texture_contract|render_target_set|ktx2_layers|ktx2_transcode|dds_layers|texture_asset|texture_streaming|texture_mip_residency|texture_resident_streamer|virtual_texture_pages|virtual_texture_page_table|virtual_texture_streamer|virtual_texture_disk_queue|virtual_texture_file_streamer|virtual_texture_prefetch|virtual_texture_feedback|virtual_texture_feedback_pass|hdr_texture|exr_texture)$'
```

Build `aiverse_editor` and a native client to detect editor/renderer
ABI regressions. Build and run `aiverse_vulkan_tests`/`vulkan_backend`
with Vulkan SDK and a working GPU or Lavapipe; distinguish a real GPU
run from driver-unavailable skips. Metal, GL33, Android GLES3 and WebGL2
need separate native builds and visual/readback validation.
**No tests are claimed as passed for this feature branch.**

## References and handoff

- `engine/gfx/Gfx.h` — public resources, descriptors and inspection
- `engine/gfx/RenderTargetSet.h` — MRT/MSAA render-target owner
- `engine/gfx/TextureResidentStreamer.h` — bounded complete-mip streaming
- `engine/gfx/VirtualTextureStreamer.h` — bounded physical atlas streaming
- `engine/gfx/VirtualTextureDiskQueue.h` — asynchronous packed page file reads
- `engine/gfx/VirtualTextureFileStreamer.h` — disk-to-GPU physical atlas coordinator
- `engine/gfx/VirtualTexturePrefetch.h` — bounded normalized UV visibility planner
- `engine/gfx/VirtualTexturePrefetchAdapter.h` — disk-page request integration
- `engine/gfx/VirtualTextureFeedback.h` — bounded RGBA8 page feedback codec
- `engine/gfx/VirtualTextureFeedbackRuntime.h` — optional async readback-to-prefetch coordinator
- `engine/gfx/VirtualTextureFeedbackPass.h` — owned RGBA8 offscreen feedback pass
- `engine/gfx/VirtualTextureSampling.h` — GLSL/MSL virtual page lookup
- `engine/render/TextureAsset.h` — DDS/KTX2 GPU uploads
- `engine/render/Ktx2Libktx.h` — opt-in native Khronos libktx ETC1S/UASTC LDR adapter
- `engine/render/ExrOpenExr.h` — opt-in OpenEXR 3 compressed HDR reader
- `engine/gfx/vulkan/VkCompute.cpp` — Vulkan writable image and compute pipeline backend
- `apps/editor/windows/TextureResourcesWindow.cpp` — live GPU inspector
- `docs/agent-handoffs/texture-rhi-2026-11-18.md` — append-only handoff log

Every implementation stage requires a code commit and a **separate**
handoff commit with exact SHAs, actual test results and next steps.


## Native Renderer3D material feedback (new RHI program)

The public C++ implementation now includes
`engine/gfx/VirtualTextureFeedbackProgram.h` and
`engine/render/VirtualTextureModelFeedback.h`.
The former generates complete engine-RHI GLSL/MSL shader stages
and creates an RGBA8 position/UV pipeline with the real U0
(std140/Metal buffer 1) binding contract. The latter owns
eight rigid `ModelVertex` pipeline variants: alpha mask
on/off, LOD fade on/off, and single/double-sided culling.
It maps `Model::images[material.baseTex]`, alpha cutoff,
`uvScale`/`uvRotation`/`uvOffset` and LOD pixel-grid
parameters onto those pipelines automatically.

Example initialization and ownership:

```cpp
gfx::VirtualTextureFeedbackPass feedback;
const bool ok = feedback.initialize(dev, feedbackWidth, feedbackHeight,
                                    feedbackConfig, prefetchConfig,
                                    8, true); // enable depth
render::VirtualTextureModelFeedback modelFeedback;
if (ok && modelFeedback.initialize(dev, feedback.hasDepthAttachment())) {
    modelFeedback.connect(renderer,
        [](const render::Model& model,
           const render::Material& material,
           render::VirtualTextureMaterialMapping& map) {
            // Look up your project's VT allocation for this material.
            // If no VT is assigned return false: still draw depth, no page ID.
            if (!hasVirtualPages(model, material)) return false;
            map.baseTilesX = ...; map.baseTilesY = ...;
            map.layer = ...; map.mipLevels = ...;
            map.texelWidth = ...; map.texelHeight = ...;
            return true;
        });
    renderer.setVirtualTextureFeedback(&feedback, {}, {});
}
```

Call `Renderer3D::beginScene` and submit models normally;
`Renderer3D::render` invokes feedback automatically before
opening its main scene pass when the pass is ready and idle.
Call `feedback.updateStreaming(fileStreamer,budget,...)` from
the frame coordinator to consume readback requests; otherwise
pending readback intentionally blocks new capture.
Before destroying GPU resources, call
`renderer.clearVirtualTextureFeedback()`,
`modelFeedback.shutdown()` (disconnects model variants),
then `feedback.shutdown()`; do so before destroying `dev`.

The resolver is genre-agnostic. A model without VT allocation
still writes depth with zero feedback IDs, preserving occlusion.
The adapter creates and owns a 1x1 white alpha texture for
masked materials without a valid base image.

Limitations: no skinned/baked/morph GPU feedback vertex program
is created here; use `setVirtualTextureDeformedModelFeedback`
for those until an exact deformation variant is available.
The program is not yet registered by Terra automatically.
For fixed material alpha/LOD/UV parity, special shader graphs,
material palettes, GPU-driven mesh LOD, and main-pass depth
must be reviewed during the later GPU test stage.
The isolated fragment source generators in
`VirtualTextureFeedback.h` are not themselves complete
`ShaderDesc` sources; use the new program builder for native RHI.


### Deformed model VT feedback: skinning, baked clips, morphs

`engine/render/VirtualTextureDeformedFeedback.h` provides the opt-in
`render::VirtualTextureDeformedModelFeedback` adapter. It uses the
same `ModelShaders.h` GLSL/MSL deformation functions as the main
model pass (rather than a simplified second implementation),
including `U3` joint palette, baked joint texture `tex7`,
`U4` morph weights and `tex8` morph deltas. The adapter creates
six deformation shader configurations, each with backface or
double-sided culling: rigid-dynamic, skinned, baked, morph,
skinned+morph, and baked+morph. Material alpha test and LOD fade
are controlled per draw using a shared U6 feedback block;
the fragment shader computes automatic mip before any discard.

Add the adapter after initializing the model feedback renderer:

```cpp
#include "engine/render/VirtualTextureDeformedFeedback.h"

render::VirtualTextureModelFeedback rigidFeedback;
render::VirtualTextureDeformedModelFeedback animatedFeedback;
rigidFeedback.initialize(dev, feedback.hasDepthAttachment());
animatedFeedback.initialize(dev, feedback.hasDepthAttachment());
rigidFeedback.connect(renderer, resolveVirtualMaterial);
animatedFeedback.connect(renderer, resolveVirtualMaterial);
renderer.setVirtualTextureFeedback(&feedback, {}, {});
```

`resolveVirtualMaterial` is the same
`VirtualTextureMaterialResolver` callback used by rigid models.
It fills virtual page tile counts, logical texture resolution,
mip level count, page layer and optional UV scale/offset.
Unmapped materials write zero feedback while retaining depth
occlusion. The deformed callback uses the shared `Model::vb/ib`,
including `gpuBaseVertex` and `gpuFirstIndex`, and matches
the main renderer's 60-byte `ModelVertex` vertex layout.

On teardown, clear the pass from `Renderer3D`, disconnect
`animatedFeedback` and `rigidFeedback`, then shut down both
adapters and the pass **before** destroying the RHI device or
renderer. Deformed feedback requires valid per-frame animation
state from normal `Renderer3D::beginScene` and `drawModel`.

Caveats: this is not a CPU skinning implementation; it issues
GPU vertex deformation using the same shader functions as
the visible draw. Shaders with custom material graphs and
vertex displacement require further parity work. Geometry
residency and alpha texture availability must be valid.
Deformed objects whose required bone/morph GPU state is
incomplete are skipped conservatively rather than drawn
undeformed. Multi-backend compilation and visual parity
have **not yet been tested**.


## TerraWeave integration (model queue, forest, impostors, Horizon)

`engine/render/TerraWeaveVirtualTextureFeedback.h` provides a
genre-independent VT material mapping bridge. The current
`feature/terraweave` implementation uses
`render::Renderer3D::drawModel()` for
`ArborForestAdapter::submit()` / `forEachDraw()` (near trees and
far impostor quads) and for `TerraWeaveHorizonScene::submit()`
(distant terrain + forest horizon). Therefore **no separate Terra
geometry renderer or duplicate feedback draw queue is necessary**.
The bridge depends only on `ModelDraw`, `Model`, `Material`,
`Renderer3D` and the generic gfx feedback program. It remains
compilable on `feature/texture-rhi-unification` *before* Terra's
separate branch is merged.

Initialize after the feedback target has been allocated:

```cpp
render::TerraWeaveVirtualTextureFeedback terraFeedback;
bool ok = terraFeedback.initialize(dev, feedbackPass, renderer);
// Optional: enable future skinned/morphed Terra prototypes with true.
// The existing Terra-generated forest/horizon models are rigid.
```

There are **two methods for assigning existing VT pages**:

1. Explicit registration before `Renderer3D::render()`:
   `terraFeedback.registerMaterial(model, material, mapping)`,
   `registerModel(model, mapper)` or `registerDraw(draw, mapper)`.
   `registerDraw` also registers per-azimuth impostor material overrides
   (`ModelDraw::materials`). A forest integration after the Terra branch
   is merged can submit the exact same draw once:

   ```cpp
   terraFeedback.beginFrame(); // invalidate stale pointer keys from last frame
   forest.forEachDraw(batches, origin, [&](const render::ModelDraw& draw) {
       terraFeedback.registerDraw(draw, lookUpExistingVirtualPages);
       renderer.drawModel(draw);
   }, cameraCm);
   // normal Renderer3D::render() performs feedback and scene rendering
   ```

2. For `TerraWeaveHorizonScene`, whose active/replacement models are
   intentionally private, call
   `terraFeedback.setLiveMaterialMapper(lookUpExistingVirtualPages)`.
   The feedback binder receives the *actual* Model and Material
   pointers when `Renderer3D` submits them; it can match active
   region revision, forest atlas/material, and exact VT allocation
   without modifying Terra's API or duplicating `submit(renderer)`.
   The callback runs on the render thread during GPU feedback
   submission. It must only **look up existing allocations**, not
   allocate GPU resources, generate pages or block on disk IO.

`VirtualTextureMaterialMapping` includes virtual base tile counts,
layer, mip policy, logical texture dimensions, and optional UV
scale/offset. The bridge rejects malformed/non-finite mappings.
Unregistered Terra materials still write **depth with zero feedback**
and correctly occlude virtualized surfaces behind them. This is
intentional: not every bark, foliage or impostor material needs VT.

The host owns the physical atlas, page-table lifetime, packed page
archives and `VirtualTextureFileStreamer`. The new bridge does not
automatically transcode Terra image atlases or assign virtual pages.
It only connects actual VT allocations to the existing shared
shader/readback/streaming pipeline. Continue calling
`feedbackPass.updateStreaming(streamer, budget)` when no render pass
is active, and consume its visibility result.

At teardown, call `terraFeedback.shutdown()` before destroying the
Renderer3D, `feedbackPass` or `gfx::Device`. The bridge clears the
renderer feedback configuration and destroys the shader resources
it owns, but does not delete the caller-owned feedback pass or
file streamer. `beginFrame()` must occur before re-registering
Forest/Horizon materials after asset reloads/region replacement.

**Branch coordination:** Terra's source currently lives in
`feature/terraweave`; the VT bridge lives in
`feature/texture-rhi-unification`. The Terra agent should use this
adapter after the two branches are merged. Do not duplicate the
VT system in `engine/world` or modify genre-specific RTS terrain
just to integrate Terra. Existing Terra `ModelDraw` visibility,
LOD, material overrides and the engine's virtual feedback queue
are the shared integration seam.

**Not yet verified:** native C++ build, GLSL/MSL/Vulkan shader
compilation, visual alpha/LOD parity, real packed archive
integration, distant Horizon region transitions, and GPU/IO
performance. Terra's independently developed custom material
paths or transparent transition draws may need additional
feedback policies beyond this rigid-model bridge.


### Runtime Terra atlas page files and Horizon revision switch

Use `engine/gfx/VirtualTexturePageArchiveWriter.h` for generic RGBA8
assets or `engine/render/TerraWeaveVirtualAtlasArchive.h` to pack
Terra-generated RGBA8 directly from retained CPU memory, before
`uploadModel` frees `Image::rgba`. For an active Horizon region
whose `HorizonBake::atlasRgba` is available:

```cpp
gfx::VirtualTextureArchivePackConfig settings;
settings.pageSize = 128;
settings.gutter = 2;
gfx::VirtualTextureArchiveVersion nextVersion;
std::string error;
render::TerraWeaveVirtualAtlasSource source{
    int(bake.atlasWidth), int(bake.atlasHeight),
    std::span<const uint8_t>(bake.atlasRgba)};
bool packed = render::packTerraWeaveAtlas(
    cacheRoot / "horizon-vt", horizonRevision, source,
    settings, nextVersion, error);
```

The writer generates the same `m<MIP>/l<LAYER>/x<X>_y<Y>.rgba`
page paths that `VirtualTextureDiskQueue` reads, applies
deterministic 2x2 RGBA box mip filtering and clamped page gutters,
and publishes the complete version via a same-filesystem
directory rename. Existing versions are immutable and stay
available until readers/GPU accesses have retired.

After packing, on the graphics-device thread, prepare the new
streamer while the previous one remains active:

```cpp
render::TerraWeaveVirtualAtlasSession atlasSession(dev);
if (atlasSession.prepare(nextVersion, /*physicalPages=*/128)) {
    // Called each frame while the new archive's disk reads and
    // GPU page uploads progress, not in a worker thread.
    atlasSession.pumpCandidate(8);
    if (atlasSession.candidateFallbackResident() &&
        atlasSession.promote(feedbackPass)) {
        // Bind the NEW active()->physicalAtlas() and
        // active()->pageTableTexture() in the virtual-sampling shader.
        // Then update the active VT resolver to the matching generation.
        terraFeedback.beginFrame(); // remove stale per-material page keys
    }
}
```

Promote only at a frame boundary with no outstanding old feedback
readback; it will refuse a switch while one is pending/unconsumed.
The old streamer is retained separately. After an **actual GPU fence**
confirms all prior commands no longer reference the old atlas/page
table, call `atlasSession.retireCompletedGpuWork()`. Never infer that
a frame delay alone is a GPU fence. Switching to the new streamer
changes page ID ownership: the application must swap its
material sampling bindings and feedback resolver in the same
frame. Continue `feedbackPass.updateStreaming(*atlasSession.active(),
budget)` thereafter. If the new archive or GPU allocation fails,
the previous active atlas remains unaffected.

For an unrotated material whose UVs select a subrectangle of
a shared Terra impostor atlas, use
`terraWeaveAtlasMappingForMaterial(version,material,mapping,layer)`.
It includes `Material::uvScale` and `uvOffset` in the virtual UVs.
Nonzero `uvRotation` currently returns false deliberately because
the compact feedback U0 virtual-UV mapping does not yet support
rotation. This avoids silently loading unrelated atlas pages.
A `setLiveMaterialMapper` callback can return the mapping for
the active Horizon atlas model (material with `baseTex=0`);
it must distinguish old/transition models using real model
identity, not just `Model::path`. Forest impostors use their
actual per-azimuth `ModelDraw::materials` overrides.

The writer works on one VT *sampling domain* at a time: all
image layers in one archive must have identical width/height.
Different-sized forest atlases may use separate VT domains,
or a build-time common atlas layout with explicit UV scale.
`TerraWeaveVirtualAtlasSession` handles one active domain;
it does not combine independent page-table resources.

The archive writer and session do **not** modify the main model
shader to sample virtual data. Actual page-table sampling must
be bound by the game's material/shader integration, using
`VirtualTextureSampling.h`. Until this step, the feedback
and disk streaming pipeline may operate but the main draw
still samples the regular material texture. The implementation
has not been compiled or exercised on the GPU yet.


## Opt-in virtual texture sampling in the main Model renderer

The VT page loader and feedback pipeline now connect to the **normal**
`Renderer3D` material fragment shader, rather than only rendering
feedback page IDs. New `engine/render/ModelVirtualTextureSampling.h`
reuses `engine/gfx/VirtualTextureSampling.h`'s existing physical
page lookup, page-table format and mip fallback implementation.

Call `Renderer3D::setModelVirtualTextureResolver(resolver)` to
provide a valid `ModelVirtualTextureBinding` for the exact
`Model` and `Material`. It contains:
`physicalAtlas`, `pageTable` (RGBA8 2D arrays),
`layer` and `gutterPixels`. Only a valid binding creates the
`kKeyModelVirtualTexture` variant of the regular model shader.
This shader reads the page table from `tex14`, the physical atlas
from `tex13`, and a single U6 vec4 holding virtual layer/gutter.
Both resources are selected at each draw, not cached by material
pointer across Horizon revisions.

The primary fragment shader's normal `tex1` base-colour sample
becomes `aiverseSampleVirtual(tex13,tex14,uv,layer,gutter,tex1Fallback)`
in GLSL or `aiverse_sample_virtual(...)` in Metal. It retains
the original material texture `tex1` as fallback if no virtual
page is resident, so regular materials, alpha masking, lighting,
team-colour blending, tint and LOD coverage remain unchanged
while streaming loads. **Because tex1 remains a fallback,
this first integration does not yet reclaim source texture
VRAM**; a separate fallback policy/resident coarse mip texture
will be needed before claiming VT memory savings.

Only ordinary built-in model shaders use the opt-in slots
`tex13/tex14` (bounded by GL's 16 texture units). Shader Graph
programs also use slots from 13 onward and therefore remain on
their existing sampling path, as do shadow and outline passes,
GPU ICB/indirect variants and per-vertex material-palette
batches. Model batching separates submissions if their
external VT resource pair/layer/gutter differs. To avoid
binding the wrong page table in existing GPU encoded commands,
a frame with any VT material falls back to CPU model submissions;
mixed CPU multi-draw indirect batches are also rejected.
No game genre is referenced in the shared sampler.

For the current TerraWeave integration, initialize the existing
`TerraWeaveVirtualTextureFeedback` bridge and
`TerraWeaveVirtualAtlasSession`, then install a resolver
that always reads the currently active revision:

```cpp
render::connectTerraWeaveVirtualTextureSampling(
    renderer, terraFeedback, atlasSession);
```

The helper is in
`engine/render/TerraWeaveVirtualAtlasArchive.h`. It confirms
that a material has an actual VT allocation in the feedback
bridge, checks that it matches the active archive's virtual
page dimensions, and returns `atlasSession.active()->physicalAtlas()`
plus `pageTableTexture()` as a *single consistent domain*.
Calling `atlasSession.promote(feedbackPass)` at the correct
frame boundary changes both bindings atomically for new draws.
Never release the old GPU atlas until its final fence is done.
The `TerraWeaveVirtualTextureFeedback::shutdown()` path also
clears its installed sampling resolver. Ensure the bridge
outlives any renderer callbacks that capture its reference,
and explicitly clear callbacks when replacing ownership.

**Unsupported or deferred:** custom Shader Graph VT sampling,
per-command VT identity for GPU ICB/MDI, fully virtualized
source texture residency, opaque-to-transparent transition
feedback, multiple independent Terra atlas domains in one
session, and validated texture/colour parity on every backend.
The existing source texture fallback is intentional for
correctness. Native build, CTest, GL shader compilation,
Metal compilation and actual Vulkan/GPU tests have not been run.


### Reduced source-texture fallback: actually saving GPU memory

`engine/render/ModelVirtualTextureFallback.h` introduces an opt-in,
pre-upload asset policy. Once a full-resolution source image has been
packed into an **immutable** VT page archive, a compatible model base
image may upload a much smaller regular `tex1` fallback rather than
the original full-resolution RGBA8 texture. The regular model shader
uses this small fallback until the virtual atlas has resident pages.
No change is needed in `ModelShaders.h`'s physical sampling function.

Use **before `render::uploadModel()`**, while the source RGBA8
pixels remain in `Image::rgba` or `Image::sharedRgba`:

```cpp
#include "engine/render/ModelVirtualTextureFallback.h"
// Or include TerraWeaveVirtualAtlasArchive.h for its Terra helper.

render::VirtualFallbackSavings estimate;
std::string reason;
bool reduced = render::configureModelVirtualFallback(
    model, sourceImageIndex, publishedArchive,
    /*maxDimension=*/64, &estimate, &reason);
if (!reduced) {
    // Keep the normal full-resolution upload; do not silently degrade
    // a material with alpha cutouts or shared non-base texture usage.
}
bool uploaded = render::uploadModel(dev, model);
```

For a Terra-generated model whose path begins with
`terraweave/`, the corresponding convenience method is
`configureTerraWeavePackedImageFallback(model,imageIndex,
archive,64,&estimate,&reason)`.

The policy verifies the VT archive's source dimensions, bounds
the fallback resolution (4–1024 pixels on the longest edge),
requires retained full RGBA8 input and forbids modification
after a native GPU or shared-cache image upload. It rejects
images reused as roughness, metal, normal, occlusion, emissive,
team-mask or Shader Graph input. All materials referencing the
same base image must be **opaque**, non-graph, alpha-independent
and not use `TeamMode::BaseAlpha`. The caller must separately
ensure dynamically supplied `ModelDraw::materials` overrides
do not require the original image. The fallback texture is
created as a standalone model-owned resource; it cannot poison
`ModelTextureCache`'s full-resolution shared GPU entry.

The fallback is downsampled by deterministic edge-clamped RGBA8
2×2 box filtering until `max(width,height) <= maxDimension`.
For example, a 2048×2048 RGBA8 base level is 16 MiB; a 64×64
base is 16 KiB before any GPU-generated mip/driver overhead.
The reported `VirtualFallbackSavings` is an **estimate of base
level byte counts**, not measured GPU VRAM or overall residency.
The full immutable source is in the disk VT archive; the GPU
owns only the coarse fallback plus the bounded physical page
atlas and page table (the latter are managed independently).

If the backend rejects the smaller fallback texture,
`uploadModel()` returns failure for the opted-in image and
retains its CPU source, so a retry or explicit fallback policy
change is possible. The non-VT image loading path is unchanged.
`releaseModel()` destroys the model-owned coarse image like any
other uploaded texture.

**Historical Stage 10F restriction (now superseded):** the
original coarse fallback implementation excluded masked Terra
Horizon forest clusters and Arbor impostors because their source
RGBA8 alpha supplied the cutout silhouette. The later section
**Full-resolution cutout alpha with reduced VT colour fallback**
adds a separate full-resolution R8 alpha resource and shader
variants for colour/shadow/selection/feedback. Masked images can
now use the explicit opt-in preserve-alpha policy, subject to
its material-compatibility checks and deferred GPU validation.

Native builds, GPU allocation accounting and visual comparisons
have not been run yet. Verify actual driver residency and
alpha/LOD quality when the user's deferred test phase begins.


### Full-resolution cutout alpha with reduced VT colour fallback

`engine/render/ModelVirtualTextureFallback.h` now supports
**alpha-preserving fallback** for opaque cutout models
(`Material::alpha == AlphaMode::Mask`). This closes the earlier
restriction that Terra Horizon/Arbor impostor atlases always had to
retain the full source RGBA8 GPU image.

After writing the full-resolution source RGBA8 to an immutable VT
archive, configure the model **before** calling `uploadModel()`:

```cpp
render::VirtualFallbackSavings estimated;
std::string reason;
bool reduced = render::configureTerraWeavePackedMaskedImageFallback(
    horizonModel, /*imageIndex=*/0, archiveVersion,
    /*maxDimension=*/64, &estimated, &reason);
if (reduced) {
    // Image::tex becomes reduced RGBA8 fallback, and
    // Image::virtualAlphaMask becomes full-resolution R8 coverage.
    bool uploaded = render::uploadModel(dev, horizonModel);
    // Check uploaded before publishing this model for rendering.
}
```

Generic models can use
`configureModelVirtualMaskedFallback(model,imageIndex,archive,...)`,
or `configureModelVirtualFallback(...,reason,true)` to request
the R8 mode explicitly. The default plain
`configureModelVirtualFallback()` still rejects alpha-masked
images unless preserve-alpha is enabled.

`Image::virtualFallbackMaxDimension` and
`Image::virtualFallbackPreserveAlpha` are opt-in CPU upload policies.
`uploadImage()` extracts the original full-size alpha channel into
a separate R8 GPU texture, including the image's existing repeat,
filter and mip policy. It independently downsamples the source
RGBA8 pixels via edge-clamped 2×2 box filtering into the small
`Image::tex` fallback. A failed R8 or coarse-texture GPU
allocation aborts the opted-in upload without discarding the
CPU source; any newly created R8 resource is cleaned up when
the coarse allocation fails. `releaseModel()` destroys both
model-owned handles.

The built-in model shader's new `VIRTUAL_ALPHA_MASK` permutation
uses `tex15.r` for original-resolution cutout coverage while its
base-colour `tex1` and VT `tex13/tex14` paths can use much smaller
colour resources. Both GLSL and MSL perform alpha clipping with
R8 for colour, sun/spot shadow and selection-mask passes. The
same source alpha is used for LOD-fading fragment rejection.
`VirtualTextureModelFeedback` has dedicated R8 red-channel
feedback pipelines; `VirtualTextureDeformedModelFeedback`
also reads full-resolution tex15.r for skin/baked/morph paths.
`GpuMaterialState` accounts for distinct alpha handles when
grouping batches. GPU ICB submissions are conservatively
disabled when an in-view model uses an external R8 mask,
because their per-command bindings do not encode tex15 yet.

For a 2048×2048 RGBA8 source, original base level = 16 MiB.
A 2048×2048 R8 alpha mask = 4 MiB; a 64×64 RGBA8 colour
fallback = 16 KiB. That is approximately **75% base-level
source-texture reduction**, excluding RHI mip chains, driver
alignment, the bounded physical VT atlas, page table, and
actual virtual pages resident in GPU memory. The
`VirtualFallbackSavings::fullResolutionAlphaBytes` field
records the calculated base-level R8 cost; these estimates
are **not measured VRAM statistics**.

Restrictions: `configureModelVirtualMaskedFallback()` accepts
masked but nonblended, non-graph source materials. It excludes
shared non-base textures, `TeamMode::BaseAlpha` and
`TeamMode::VertexAlpha`, and requires the full RGBA8 CPU
source before any existing GPU upload/cache lease. Dynamically
overridden `ModelDraw::materials` must follow the same
invariants; model-local validation cannot inspect unknown
future overrides. Material shader graphs occupy texture slots
13–15 and are deliberately not virtualized through this
built-in shader permutation. A valid VT mapping and packed
source archive are still required for high-detail colour.
Without them, the main scene retains a visually coarse
colour fallback while keeping the full-resolution cutout
silhouette. For alpha-only silhouette equivalence, compare
coverage with the original texture at multiple mip levels
on each GPU/backend; R8 and RGBA GPU mip filters may have
implementation-dependent rounding.

**Verification deferred:** GitHub source modifications committed;
native C++ compilation, GLSL/MSL shader compilation, Metal/
OpenGL/Vulkan rendering, shadow/selection silhouette tests
and actual driver VRAM accounting have NOT been run.


### Metal GPU-encoded ICB and CPU multi-draw for VT materials

The experimental native model batching paths can now handle **homogeneous**
VT material groups without disabling GPU-authored model draws for the
entire scene. The implementation builds on `GpuSceneDrawCommands` and
the existing Metal argument-buffer PBR ICB variant; it does **not**
implement a distinct VT render submission API or copy the atlas for
each command.

A homogeneous group requires the same built-in material GPU state,
vertex/index buffer bindings and an **exact match** for:
`ModelVirtualTextureBinding::physicalAtlas`,
`pageTable`, `layer` and `gutterPixels`.
The VT identity participates in the GPU material hash and cached group
signature, but hash collisions are still checked with exact binding
equality. R8 masked-cutout eligibility requires
`Image::virtualAlphaMask` and the same full-resolution R8 handle;
other alpha/blend and Shader Graph variants retain CPU fallback.

Metal's PBR ICB fragment argument buffer now includes the physical
VT `texture2d_array` (argument ID 26, formerly direct tex13) and
`texture2d_array` page table (argument ID 28, direct tex14), plus an
optional R8 alpha texture and sampler (IDs 30/31, direct tex15).
The physical atlas uses the existing material sampler `s1`; page
table sampling uses `read()`, so **no extra samplers** are encoded
for VT slots 13/14. The VT-specific fragment shader also receives
U6 (Metal buffer 7), holding layer and gutter. These external
textures are bound once for the complete compatible ICB group,
not individually for every GPU-encoded indexed command. GPU instance
matrix U0 remains per-command as before.

`GpuEncodedBatch` snapshots the external binding and R8 alpha
handle during compute preparation, and revalidates them immediately
before ICB execution. If a Horizon region switches its page-table
or physical atlas in between, that group is rejected and falls
back to normal CPU draws; cached groups are also invalidated when
VT identity changes. Homogeneous CPU MDI groups can likewise use
the VT material variant with shared fragment arguments; mixed groups
return to direct/instanced submission. An incompatible source
shader or unavailable Metal ICB PBR argument-buffer pipeline is
still a safe rejection, not a partial virtual draw.

**Experimental enabling:** the existing model batching switches and
`AIVERSE_GPU_AUTHORED_DRAWS=1`,
`AIVERSE_GPU_ENCODED_ICB=1` and
`AIVERSE_EXPERIMENTAL_METAL_ICB=1` (plus
`AIVERSE_METAL_CPU_MDI=1` for an independent CPU MDI experiment)
remain in effect. GPU-authored draws also require the renderer's
`setGpuAuthoredDrawEnabled(true)`, Metal compute support, and
supported Apple GPU argument buffers. Do not enable these options
by default or assume driver support from a successful source commit.

The reenabled GPU route currently targets only valid rigid
opaque or R8 cutout model primitives with conventional material
state and no shader graphs, GI, skinning, morph, transparency,
active dissolves or LOD fade. Its fallback maintains the existing
`Renderer3D` path on other platforms and for unsupported draws.
A fully general **per-command** VT resource index, heterogeneous
multi-domain ICB groups and Shader Graph VT are still future work.

**Verification deferred:** native compilation, Metal ICB PBR shader
compilation, actual GPU encoding/execution, visual comparison and
performance profiling have NOT run. There is no measured speedup
claim; this is a restored opt-in code path with conservative
resource identity checks.


## Multiple TerraWeave VT archives in one scene

`engine/render/TerraWeaveVirtualAtlasDomains.h` manages separate
virtual-texture domains (for example Horizon terrain, individual forest
impostor atlases, and other procedurally generated texture sets).
Each domain owns a `TerraWeaveVirtualAtlasSession` (immutable archive
revision and independent disk/GPU streamer), plus its own RGBA8
`VirtualTextureFeedbackPass` configured to that archive's virtual
tile dimensions. **Do not submit page IDs from different domains to
one feedback decoder:** the existing 32-bit page ID has coordinates,
mip and layer, but no atlas/domain field.

Initialize model feedback programs once using the pass-free
`initializeForDomains` entry point, then register independent
archives. No dummy legacy `VirtualTextureFeedbackPass` is required.
The model feedback programs and all per-domain passes **must use
the same depth-attachment mode**. Legacy `initialize(pass,...)`
continues to work for single-domain applications.

```cpp
#include "engine/render/TerraWeaveVirtualAtlasDomains.h"

// Render-thread objects: device and renderer outlive domains.
render::TerraWeaveVirtualTextureFeedback feedback;
feedback.initializeForDomains(dev, renderer, /*depthAttachment=*/true);
render::TerraWeaveVirtualAtlasDomains domains(dev);
render::TerraWeaveVirtualAtlasDomains::Options options;
options.feedbackWidth = 256;
options.feedbackHeight = 256;
options.feedbackDepth = true;
domains.addDomain(1, options);  // Horizon
domains.addDomain(2, options);  // Forest atlas

// After publishing immutable archive files:
domains.prepare(1, horizonVersion);
domains.prepare(2, forestVersion);
// Each graphics frame, before a pass is open, pump candidate streamers
// until their coarsest mip pages are resident.
domains.pumpCandidate(1, 8);
domains.pumpCandidate(2, 8);
if (domains.session(1)->candidateFallbackResident()) domains.promote(1);
if (domains.session(2)->candidateFallbackResident()) domains.promote(2);

// Install callbacks once, with active archives available.
domains.attach(renderer, feedback);

// Once per scene frame, after Device::beginFrame:
domains.pumpActive(8);
domains.beginFrame();
// Forest/Impostor: register the exact material override before submitting
// the same ModelDraw through the ordinary shared Renderer3D queue.
domains.registerDraw(forestDraw, 2);
renderer.drawModel(forestDraw);
// HorizonScene's active Model is private: assign its live DomainId
// through domains.setLiveDomainLookup(...) during scene setup.
renderer.render(...); // runs at most one archive's GPU feedback pass
```

The scheduler overrides the legacy single-pass slot only while
attached. Every `Options::feedbackDepth` value must match
`feedback.feedbackDepthAttachment()`; mismatches are rejected
instead of rendering feedback with incorrect depth state.

The render callback runs after shadow passes and before the main
colour pass. It selects one **idle** domain per frame by round-robin
order; only that domain's models output page IDs. All other models
still rasterize into feedback depth, so they occlude virtual pages
behind them correctly. Different domains may be sampled during
the **same primary scene frame**: `Renderer3D` resolves the
model's domain to its active physical atlas/page-table and binding
identity. CPU/Metal ICB batching stays valid for homogeneous
resource groups and separates different VT domains.

Use `domains.setLiveDomainLookup(fn)` for private Horizon region
models that cannot be registered by `registerDraw()`; its
callback receives the actual Model and Material and returns
`DomainId` and the domain-local layer. The stored per-frame
assignments and live lookup refer to real objects, not strings
like `terraweave/horizon` that may be reused across revisions.
The callback must not allocate GPU resources or read files.

`domains.pumpActive(budget)` submits **each completed domain
readback only to that domain's own** `VirtualTextureFileStreamer`,
then pumps bounded GPU uploads. The caller may explicitly request
coarse/visible pages with
`domains.requestPage(domainId,VirtualPageKey{...},pin)`.
A domain update uses `prepare(id,version)`,
`pumpCandidate(id,budget)`, then `promote(id)`. Promotion
creates a new feedback decoder for the new page-table geometry,
keeps old GPU atlas resources until explicit
`retireCompletedGpuWork(id)` after a real graphics fence,
and rejects promotion while that domain's readback is pending.
Other domains remain unaffected.

Disconnect with `domains.detach()` **before destroying**
Renderer3D/feedback; call `domains.shutdown()` only once
all GPU work using its resources is finished. The feedback
adapter still owns the shader programs and requires its
separate `shutdown()`. Multi-domain support does not require
modifying Terra's separate `feature/terraweave` branch.

Limitations: there is one VT feedback capture per scene frame by
default, not one per domain simultaneously. Domains not selected
for that frame may use manual CPU prefetch/previously resident
pages. The main shader still needs ordinary source-texture fallback
until its pages arrive. Multi-domain grouping remains homogeneous within any one GPU
ICB/MDI batch; a heterogeneous single-command resource table remains
future work. Shader Graph now supports an explicit material-only
`sampleVirtualTexture` node with a bounded one-extra-texture budget,
documented below. Native build, shader compilation, CTest and GPU tests
for this new manager have **not run**.


## Shader Graph virtual texture node

The normal material graph compiler now offers
`sampleVirtualTexture(uv)` as a material-only fragment operation.
Its `Program::usesVirtualTexture` flag selects a shader permutation
that uses the same `ModelVirtualTextureBinding` and physical
page-table lookup as normal `Renderer3D` materials.

`graph::compile` counts the node toward the existing sample
limit, rejects it in vertex/postprocessing/particle graphs, and
enforces an explicit **16-fragment-texture-slot** budget:
one or zero independent graph textures may coexist with the
two VT arrays. With zero additional graph textures the virtual
atlas/page table are `tex13/tex14`; with one, `tex14/tex15`.
These are bound per material draw by the existing resolver.
When the resolver returns no valid VT allocation, generated
`SG_SAMPLE_VIRTUAL(uv)` falls back to ordinary `tex1`
sampling so the shader remains usable while streaming or for
non-virtual materials.

The graph node may specify `"srgb":true` to request the existing
compiler's sRGB-to-linear approximation. Raw VT pages are
RGBA8 without any implicit shader-colour reinterpretation.
ShaderGraph GPU material equivalence/hash includes the new
flag. It is not available with the separate R8 reduction
policy for graph assets, and graph-backed models still use
direct/CPU submission rather than experimental Metal ICB.
The graph may continue sampling its own non-VT `sampleTexture`
resources independently.

See `docs/shader-graph.md` for the graph JSON/API example.
This is an opt-in **explicit node**, not automatic conversion of
every Shader Graph texture input into virtual storage.
Native C++ builds, MSL/GLSL compilers and GPU validation have
not yet run for the new material graph path.


## Per-axis virtual-texture address modes (Stage 10L, source only)

`render::ModelVirtualTextureBinding` now exposes `addressU` and `addressV`
using `gfx::SamplerAddressMode`. Defaults remain `ClampToEdge` on both axes
for binary/source-compatible legacy material resolvers. `Repeat` and
`MirrorRepeat` are supported independently along U/V in the shared GLSL/MSL
virtual-page lookup; the original tex1 fallback continues to use its normal
RHI sampler. The wrap policy is part of exact binding equality and the
GPU batching hash to prevent mixing incompatible material groups.

For repeat/mirror sampling, the archive must contain edge-safe gutters for
tiles at wrapping boundaries and the source material's ordinary sampler must
use the corresponding U/V modes. Updating only the virtual resolver without
updating the texture sampler/packer does not guarantee filtering parity.
`ClampToBorder` is intentionally rejected by `binding.valid()` because the
current RGBA8 page-table shader cannot reproduce an arbitrary border colour.

`ModelVirtualTextureUniforms` retains its existing 16-byte U6 layout:
`(layer, gutterPixels, addressU, addressV)`. The existing five-argument
lookup/six-argument sample shader helpers still compile as clamp-only wrappers;
opt-in callers pass both address modes as the final arguments. Built-in and
Shader Graph VT material variants now forward U6.zw, so wrap modes are honored
without allocating additional sampler slots.

**Validation:** source-level changes committed; C++/MSL/GLSL compilation,
GL/Metal/Vulkan renders, wrap-boundary filtering parity, batch identity tests
and GPU profiling remain **NOT RUN** in the current environment.


## Periodic and mirrored immutable VT page packing (Stage 10M)

The standard-library `tools/virtual_texture_pack.py` CLI now accepts
`--address-u clamp|repeat|mirror` and `--address-v clamp|repeat|mirror`
(default: clamp). Its `pack_pages(..., address_u=..., address_v=...)`
interface and `make_page`/`downsample_rgba` helpers apply the selected
per-axis modes to gutters **and** to odd-size mip box filtering.
For mirror, the edge texel is repeated at each reflection boundary.

An opt-in manifest contains `addressU` and `addressV` as strings;
legacy clamp/clamp output manifests are byte-for-byte unchanged in structure.
The archive reader / material resolver **does not yet automatically import
these modes** into `ModelVirtualTextureBinding`: configure the matching
`gfx::SamplerAddressMode` in your project and use the same conventional
source sampler mode for fallback tex1. Changing wrap policy changes archive
content; rebuild immutable pages and bump the archive version. This is not
a sparse-GPU page table implementation.

Example: `python3 tools/virtual_texture_pack.py --layer grass.rgba
--width 1024 --height 1024 --page-size 128 --gutter 2 --address-u repeat
--address-v repeat --output grass-pages`.

**Execution:** only source review; Python unit tests, CLI fixture, C++ native
build and GPU seam parity NOT RUN for this newly added behavior.


## Native C++ archive addressing and Terra propagation (Stage 10N)

`gfx::VirtualTextureArchivePackConfig` now has independent `addressU` and
`addressV` (`gfx::SamplerAddressMode`), both defaulting to ClampToEdge.
The native `VirtualTexturePageArchiveWriter` applies Repeat or MirrorRepeat
while building gutters and odd-dimension mips just like the standalone
Python packer. Explicit `ClampToBorder` is invalid: it would misrepresent
the source border color in the current physical page shader.

`VirtualTextureArchiveVersion` carries the addressing metadata through
`TerraWeaveVirtualAtlasSession::prepare/promote`, and the single-domain
`connectTerraWeaveVirtualTextureSampling` and multi-domain
`TerraWeaveVirtualAtlasDomains::resolveSampling` both forward active
version address modes to `ModelVirtualTextureBinding`.
Archive content-hash and manifest include address mode changes, so
one revision cannot silently reuse clamped bytes for repeating textures.
Legacy clamp-only archive hashes/manifests retain their former layout.

This automatic propagation applies to **C++ packed archives passed as
typed `VirtualTextureArchiveVersion` values**, not to Python-generated
manifest.json auto-loading. Projects using the standalone Python packer
must still read `addressU/V` themselves and assign matching typed
`VirtualTextureArchiveVersion` fields or binding modes explicitly.
Source texture samplers must agree. A live active archive is never
mutated; prepare a new immutable version and promote only after fallback
page residency and GPU work safety gates.

**Verification deferred:** C++ build, CTest, GPU page filtering and
cross-backend sampler parity NOT RUN for this stage.


## Validated immutable page archive import (Stage 10O)

`engine/gfx/VirtualTextureArchiveManifest.h` provides
`gfx::importVirtualTextureArchiveManifest(Json, directory, out, error, ...)`
and `gfx::loadVirtualTextureArchiveManifest(directory, out, error, ...)`.
Both accept native C++ archive-writer and standalone Python packer
`manifest.json` records (schema `aiverse.vt.disk-pages.v1`).
The reader uses the existing `engine/data/Json.h` implementation and
does not add another JSON library.

Only a caller-selected trusted directory is used: JSON cannot provide
paths. Required source dimensions, physical tile size/gutter, layers,
mip-level geometry and optional `pageCount`/`archiveBytes` must agree
with computed values and configured upper limits; every expected page
must be a regular non-symlink file of exactly `pageSize² * 4` bytes.
Mip/layer directories are also checked against symlinks. Optional
`addressU/V` import clamp/repeat/mirror as typed sampler policy.
Unsupported `ClampToBorder`, malformed metadata, incomplete archives
and invalid source hash shape fail closed. The optional source SHA-256
list and native FNV-64 are metadata, **not cryptographic authentication
of page files**; an external signed/immutable content distribution
policy is still required.

The importer fills `VirtualTextureArchiveVersion` only on complete
success. Existing active GPU state remains untouched; callers use the
returned version in their normal prepare/pump/promote sequence.
The importer performs synchronous filesystem metadata reads and must
not run every graphics frame or concurrently with archive mutation.
The Python manifest has no native revision/hash: callers supply a
trusted `importedRevision` if needed. Native writer manifests carry
their own revision.

**Verification:** source commits only; C++ compilation and CPU/GPU
integration tests NOT RUN.


## TerraWeave archive-directory prepare (Stage 10P)

`TerraWeaveVirtualAtlasSession::prepareFromArchiveDirectory(directory,
physicalPages, maxBufferedPages, importedRevision, maxArchiveBytes,
maxPages)` imports a trusted immutable archive using the bounded
`gfx::loadVirtualTextureArchiveManifest` validator and then runs
the existing candidate preparation path. A parse/metadata/disk error
leaves any active/candidate resources unchanged and is available via
`session.lastError()`; it does not promote an invalid archive.

For multiple archives use
`TerraWeaveVirtualAtlasDomains::prepareFromArchiveDirectory(domainId,
directory, importedRevision, maxArchiveBytes, maxPages)`.
Each domain retains its configured physical-page/CPU staging budgets.
After success call the existing `pumpCandidate`, wait for fallback
pages, `promote` and eventually retire old GPU resources **only after
a real fence**. The model sampling resolver uses active-version wrap
policy automatically; no manual U/V sampler reconstruction is needed
for these imported archives. The original source tex1 fallback sampler
must still match the source addressing.

This is a *synchronous staging API*, not a GPU streaming/background
filesystem mount. The source remains uncompiled and untested on native
devices in this conversation.


## Wrap-aware GPU virtual texture feedback (Stage 10Q)

The **virtual page feedback** path now shares the same per-axis
ClampToEdge/Repeat/MirrorRepeat addressing used by physical VT sampling.
`gfx::virtualTextureFeedbackPageAtUV(u,v,mip,layer,tilesX,tilesY,
addressU,addressV)` is the CPU selection equivalent; its historical
six-argument overload retains normalized-only rejection for backward
compatibility. Both GLSL and Metal `aiverseMaterialFeedback` helpers
also retain their original five-argument functions while adding
per-axis address-mode arguments.

`VirtualTextureMaterialMapping::addressU/addressV` default to clamp.
TerraWeave's single- and multi-domain archive mapping inherits these
from the active typed `VirtualTextureArchiveVersion`. The rigid model
feedback U0 now has an appended 16-byte `addressModes` vector
(total `sizeof(VirtualTextureFeedbackUniforms)==192`). Deformed model
feedback U6 appends the same vector (total 112 bytes). Its GLSL/MSL
shader implementations use those modes when calculating page feedback
IDs. Unsupported border modes fail closed instead of indexing a
different physical page.

Automatic mip derivation intentionally uses the **unwrapped** UV
derivatives to avoid discontinuities in `fract` at tile borders;
addressing is applied only to page-ID selection afterward. This is
crucial for UVs greater than one, negative UVs, and mirrored repeats.
Shader ABI changes affect only the dedicated feedback pipeline, not
authoritative ECS or the regular material uniforms. The existing
legacy helper overloads remain compatible, but native shader
recompilation for the new U0/U6 layouts is required.

**Known scope limit:** direct CPU visible-region prefetch still assumes
normalized 0–1 UV rectangles; arbitrary wrapped UV rectangles require
an upstream split/wrap visibility adapter. GPU feedback captures use
the new policy. Native builds, GLSL/MSL compilation and readback/GPU
fidelity remain **NOT RUN**.


## Bounded wrapped CPU VT visibility/prefetch (Stage 10R)

The standalone header `engine/gfx/VirtualTexturePrefetchWrap.h` adds
`gfx::normalizeWrappedVirtualTextureRegions(sourceRegions,
addressU,addressV,output,maxOutputRegions)`.
The helper splits each potentially negative/out-of-range visibility
rectangle on repeat/mirror integer UV seams, maps each monotonic piece
into normalized [0,1], and returns only bounded half-open
`VirtualTextureVisibleRegion` rectangles. Full repeated periods
collapse to one [0,1] interval, preventing unbounded CPU expansion.
Clamp retains the original normalized-range clipping behavior.

`gfx::prefetchWrappedVirtualPages(streamer,planner,views,addressU,addressV)`
in `VirtualTexturePrefetchAdapter.h` runs the normal exact-residency,
coarse-first budgeted disk prefetch only after successful expansion.
On invalid/non-finite coordinates, unsupported address modes, excessive
raw UV magnitude or output region-count overflow, it submits *no*
partial work and returns `VirtualTexturePrefetchDispatch::wrapRejected`.
The existing `prefetchVisibleVirtualPages` entry point and result
fields remain compatible.

This is independent of camera/world/game genre. Callers must provide
actual projected UV rectangles for a known archive domain and use its
active version U/V policy. One domain's regions must not be sent to
another domain's streamer. The helper does not fetch files on worker
threads and never updates ECS authoritative game state.

**Validation:** source changes committed; C++ build/CTest and
project-level wrapped UV visibility tests NOT RUN. Limitations include
CPU-side rectangular overfetch for highly rotated/nonrectangular visible
surfaces and no GPU filtering/seam parity proof yet.


## Shader Graph virtual textures with up to four source textures on Metal — Stage 10S

The material graph compiler now permits the original four independent
`sampleTexture` / `sampleTextureLod` source paths together with the
explicit `sampleVirtualTexture` node. Graphs remain backend-neutral;
their actual fragment texture binding budget is resolved by the renderer,
never assumed by source JSON.

Metal's existing RHI supports fragment texture indices 0..31 while
fragment sampler indices 0..15 remain separate. The four authored graph
textures bind at tex13..tex16; tex16 uses the existing constexpr sampler.
The two virtual atlas/page-table arrays then use tex17/tex18, reusing
material sampler s1 for the physical atlas and a texel read for the table.
This requires no new Metal fragment *sampler* slot. Graphs with zero to
three authored textures use lower free indices as appropriate.

For GL33, GLES3 and Vulkan, the current RHI binding budget stays at
16 texture slots. An explicit graph virtual sample retains its
conventional tex1 fallback if the additional two resources cannot be
bound, while the four-source graph (which needs tex16) fails closed
rather than silently dropping its fourth texture. Shader Graph's
non-VT behavior, material equivalence/hashing, original texture
bindings and the conservative non-ICB graph drawing path are retained.

The same layout helper is used for pipeline permutation selection
and for actual `setTexture`/U6 draw bindings to avoid conflicting
atlas/table slots. The injection helper now requires an explicit Metal
extended-slot opt-in to permit a page table beyond tex15. It still
rejects unsupported addresses, malformed shader anchors and other
unrepresentable bindings.

**Verification:** source-only change committed. No native C++ build,
MSL/GLSL compiler, GPU capture or cross-backend image parity has
been run. This is not a bindless material system, arbitrary source
texture count, or heterogeneous per-command ICB descriptor table.


## RHI fragment texture resource capacity — Stage 10T

`gfx::Device::maxFragmentTextureSlots()` now exposes how many
independently bound **texture resources** a renderer may address
with `setTexture(slot, handle)`, not how many dynamic sampler
objects it may bind. The default portable RHI contract remains 16.
Metal advertises 32 texture indices, while its dynamic sampler
indices still stop at 15 (high-slot material graphs use a
compile-time sampler). Vulkan reports the selected device's
actual descriptor texture capacity, which can be below its
compiled-in upper bound. GL33/GLES3/WebGL2 retain the portable
16-slot default.

The Shader Graph material VT allocator now uses this RHI
capability directly instead of testing a hard-coded backend name.
The first four normal graph texture sources are still authored
at tex13..16; a device capable of binding only 16 resources
rejects the unrepresentable fourth source. A VT graph requests
two additional independent texture slots and falls back to
ordinary tex1 when those two slots cannot be allocated.
Only a Metal RHI with the extended slot capacity opts into
the MSL virtual-resource injection above tex15.

This is a non-breaking, read-only Device API; consumers should
query it during pipeline material layout selection, and must not
confuse texture resource indices with sampler indices.
The per-command Metal ICB descriptor table is still not
implemented. C++ compilation and native backend tests for
this stage are NOT RUN.


## Shader Graph shader cache identity correctness — Stage 10U

`Renderer3D::ModelShaderKey` now compares all shader-generating
graph properties, not just GLSL fragment/vertex source, source texture
**count** and output map. The equality path now covers material
domain, GLSL/MSL fragment sources, GLSL/MSL vertex sources, exact
source texture paths, output connections, the `usesVirtualTexture`
feature flag, and the material quality selector. Pointer-identical
immutable graphs still use the existing quick path.

This prevents an MSL-only difference, source texture set change or
VT-material permutation from aliasing an unrelated shader cache
entry on another backend. The safety improvement does not add a
dynamic GPU material descriptor table or enable graphs in Metal ICB;
it protects the existing direct-rendering shader and pipeline
cache. Any cached shaders remain GPU resources and must be retired
according to their usual device lifetime.

**Verification:** source inspection and commit, no native compiler,
shader compiler or GPU tests run for this change.


## Frame-scoped heterogeneous material resource identities — Stage 10V

`engine/render/GpuMaterialResourceTable.h` adds a genre-neutral,
header-only, **CPU staging** `render::GpuMaterialResourceTable` for
future heterogeneous material/Metal ICB command resources. It
is directly accessible from native game projects; no Lua runtime or
genre dependency is required.

`GpuMaterialResourceBinding` stores up to four ordinary Shader Graph
source handles, optional full-resolution alpha coverage and an
optional exact `ModelVirtualTextureBinding` (physical atlas and
page-table handles, virtual layer, gutter and per-axis address modes).
`table.beginFrame(frameSerial)` increments a table generation and
invalidates stale resource tickets even when the caller begins another
stage with the same frame serial. `table.intern(binding)` deduplicates
only exact bindings after hash-bucket lookup. The table is bounded
(default 4096, max constructor limit 65536), fails closed on invalid
handles and returns a typed `GpuMaterialResourceTicket` containing
frame serial, generation and index.

`table.matches(ticket, current)` must be called before GPU execution,
so archive promotion, source texture reloading, alpha mask or wrap
changes cannot silently reuse the original descriptor identity.
`table.resolve(ticket, out)` copies an immutable resource snapshot,
never exposing unstable vector pointers. Neither the table nor its
tickets hold GPU resources alive; fences and texture lifetime are
still owned by the renderer. Call `reset()` on device/scene shutdown
and invalidate any cached command snapshots.

**This is NOT yet an executable heterogeneous Metal ICB GPU argument
buffer.** The existing ICB path remains homogeneous with CPU fallback.
The header establishes a bounded identity and validation contract
before extending GPU-side per-command resource indices and the MSL
material shader ABI. No native builds, CTest, GPU compilations or
device tests have been run for this new header.


## GPU material resource ticket validation at ICB submission — Stage 10W

The experimental `Renderer3D` Metal ICB workflow now uses the existing
`GpuMaterialResourceTable` as a **CPU safety gate** for its *homogeneous*
command groups. `prepareGpuModelDraws` starts a new staging generation
for every scene-frame preparation (including ICB-disabled paths).
After GPU command encoding, a group interns its exact captured
`ModelVirtualTextureBinding` and optional R8 alpha coverage and retains
the generation-qualified ticket. Invalid/capacity-exhausted entries
are rejected: prepared ICB commands are never submitted and the regular
draw path remains eligible.

Immediately before executing a prepared group, `renderModels`
resolves the current VT archive resources and alpha coverage for **every**
model and calls `GpuMaterialResourceTable::matches` in addition to the
pre-existing strict group compatibility checks. Any archive swap, page
table change, wrap mode change, alpha-mask change, stale ticket or
new staging generation rejects GPU submission, preserving CPU fallback.
`shutdownModels` invalidates tickets before model GPU handles retire.
The table does not own textures or alter GPU command shader layouts.

**No heterogeneous Metal ICB is enabled yet.** Different materials still
require compatible parent fragment state; real per-command texture/VT
resource indexing requires a new Metal shader and argument-buffer ABI.
This stage makes lifecycle and identity verification explicit before
that extension. **C++ compilation/CTest/GPU tests NOT RUN.**


## Experimental GPU-encoded Metal ICB per-command VT argument buffers — Stage 10X

The RHI now exposes `gfx::GpuIcbVirtualMaterial` and the optional
`Device::prepareGpuEncodedIndexedIcbWithVirtualMaterials(..., materials)`
entry point. The descriptor carries native TextureHandles for the
physical VT atlas, page table, optional full-resolution R8 alpha mask,
and the four-float U6 (virtual layer, gutter, U/V address modes).
Unsupported backends explicitly return an invalid token (zero);
the original `prepareGpuEncodedIndexedIcb` signature and path
remain unchanged.

The Metal backend implements a source-level **opt-in** variant gated by
`AIVERSE_EXPERIMENTAL_ICB_PER_COMMAND_VT=1`, in addition to the
existing GPU ICB/Metal experimental safety flags. A compute-encoded
ICB command now supports a per-command PBR argument-buffer offset
(4096-byte aligned stride) and a per-command U6 offset (256-byte
stride). When this mode is disabled, both strides are zero and the
legacy shared PBR/U6 buffers are used. Each native VT atlas/table is
checked against its original handle before execution; it is bound
through the existing `IcbPbrTextures` IDs 26/28, with optional alpha
at ID 30 (sampler ID 31). This does not require a new material shader
permutation or additional fragment sampler slots. All referenced
textures must remain alive through the actual GPU fence.

`Renderer3D::prepareGpuModelDraws` optionally supplies per-command
descriptors for already-compatible VT material groups. Grouping still
requires the exact same original material state and external VT
binding as before: **this stage exercises the per-command Metal
argument-buffer ABI only, and does not yet batch heterogeneous VT
materials**. Stage 10W's CPU ticket revalidation remains in place,
and stale/invalid descriptors reject execution through the original
CPU fallback. The legacy path remains selected by default.

**Verification:** only GitHub source inspection and commits;
Objective-C++ compilation, native MSL compiler, real Metal ICB
execution and visual/performance checks **NOT RUN**. Treat this GPU
path as experimental/uncertified, and do not enable the environment
flag in shipped games.


## Explicit opt-in mixed virtual-atlas Metal ICB groups — Stage 10Y

`Renderer3D` can now *in source* assemble a Metal GPU-encoded ICB
group with differing **VT atlas/page-table bindings** when all draws
otherwise share the same equivalent built-in PBR material state,
geometry buffers, shader variant, and render flags. Two independent
experimental flags are required:

```sh
AIVERSE_EXPERIMENTAL_ICB_PER_COMMAND_VT=1
AIVERSE_EXPERIMENTAL_ICB_MIXED_VT=1
```

These flags do not replace the existing
`AIVERSE_GPU_AUTHORED_DRAWS`, `AIVERSE_GPU_ENCODED_ICB` and
`AIVERSE_EXPERIMENTAL_METAL_ICB` safety gates. **The feature is
disabled by default.** Ordinary CPU MDI/instancing and direct draw
paths retain exact VT resource compatibility.

In mixed mode, batch preflight uses normal `equivalentGpuMaterial`
for exact PBR state and material shader compatibility, but permits
different VT resource identities between commands that *both*
require valid VT sampling. CPU hash prefilters omit only the VT
archive identity while preserving the no-VT/VT distinction. Every
GPU command records its own `GpuIcbVirtualMaterial` descriptor
(physical atlas, page table, layer, gutter, U/V wrap, optional alpha
coverage) and its own generation-safe
`GpuMaterialResourceTicket`. Before execution, every live
per-command binding is re-resolved and checked against its captured
ticket. Failed or stale groups remain eligible for ordinary CPU
draw submission; the GPU path cannot silently inherit the first
command's atlas.

The `drawModelBatch` homogeneous VT check is bypassed **only**
while an already-prevalidated opt-in per-command GPU ICB submission
is active. Normal CPU batches still reject mismatched atlases.
Metal's optional per-command fragment argument-buffer/U6 ABI from
Stage 10X supplies the distinct VT resources at GPU execution.

**Important limits:** this supports heterogeneous VT bindings,
*not* arbitrary different source PBR textures or Shader Graph
materials in one command group. Full shader/material-state
heterogeneity needs additional per-command U2/source texture ABI.
Metal command-encoder compilation and real GPU execution are
**NOT RUN**; do not enable these flags in release builds or
treat this as a verified performance optimization.


## Executed mixed-VT ICB diagnostics — Stage 10Z

`Renderer3D::gpuModelDrawStats()` now includes two additional
execution-only counters:
`perCommandVtExecutions` and `mixedVtExecutions`.
They count successful GPU ICB submissions after the device's
`executeGpuEncodedIndexedIcb` call reports success, **not**
mere candidate groups, buffer preparations or fallback draws.
The mixed counter increments only when that accepted command
group contains two or more non-identical
`ModelVirtualTextureBinding` values. The per-command counter
includes both mixed and homogeneous opt-in GPU batches.

The renderer resets these counts each `prepareGpuModelDraws`
cycle. Existing `gpuIcbPrepared`, `gpuIcbRejected` and
`gpuIcbExecuteFailed` remain available to distinguish
staging, validity gate rejection and actual execution failures.
Reading a nonzero new counter is NOT evidence of image
correctness or GPU performance improvement: a real backend
A/B capture is still required.

This stage is a diagnostics/API addition only. No native C++
compilation, Metal shader compilation, real ICB execution or GPU
performance tests were run in this environment.


## Fail-closed per-command Metal VT native resource validation — Stage 10AA

Before encoding any experimental per-command VT ICB, the Metal
backend now requires both the physical page atlas and the page
table to be actual `Texture2DArray` `RGBA8` resources. The
optional cutout coverage must be an `R8` `Texture2D`.
It also checks native slice counts, the requested logical
virtual layer against the page-table layer count and physically
feasible gutter inset based on the atlas dimensions.

An incompatible resource rejects the whole GPU group before
encoding rather than passing mismatched texture types to the
shader. The ordinary CPU draw fallback and existing legacy
ICB resource binding are unchanged. This restricts the
experimental path to the currently implemented RGBA8 VT
archive contract; typed HDR/alternative formats require an
explicitly compatible MSL shader variant in a later stage.

**Verification:** source review and GitHub commit only; native
Objective-C++, Metal shader compiler and actual GPU execution
NOT RUN. Leave experimental flags disabled in production.


## Conservative opaque-PBR pipeline key and per-command resource signatures — Stage 10AB

`render::equivalentGpuIcbPbrPipelineMaterial(a,b)` and
`render::gpuIcbPbrPipelineHash(material)` define a conservative
experiment-only grouping key for future per-command PBR resources.
Only built-in opaque materials without Shader Graph or transparency
are admitted. Double-sided/cast/receive-shadow and the normal-map
presence bit remain part of pipeline compatibility, while ordinary
PBR texture content/handles and U2 factors are intended to vary per
GPU command. Hashes are only prefilters: exact pipeline comparison
must still gate grouping.

`GpuMaterialResourceBinding` now optionally contains
`pbrEnabled`, six texture handles corresponding to fragment slots
1..6, and 36 floats representing the exact nine float4 rows of
ModelShaders' U2. It checks finite U2 values and valid texture handles,
and includes the full source handles/U2 in the resource-table hash
and exact equality. Existing VT/alpha and frame-generation ticket
semantics remain intact. These are CPU-only primitives; they do not
yet change GPU command submission.

Only source inspection and GitHub commits were performed. Native
build, tests and Metal GPU execution remain NOT RUN.


## Experimental Metal per-command built-in PBR U2/texture ABI — Stage 10AC

`gfx::GpuIcbVirtualMaterial` can opt in with `hasPbr=true`
and supply six valid fragment texture handles for slots 1..6,
plus 36 finite float components for the existing nine-float4
material U2. The original VT atlas/table/layer/wrap descriptor
remains mandatory for this experimental entry point.

Metal's compute-encoded ICB kernel has an additional
`pbrU2Stride` byte offset for fragment buffer index 3. The
opt-in mode writes one 144-byte U2 into each 256-byte aligned
allocation slice. Fragment texture slots 1..6 are filled from
per-command native textures and **their own** sampler states
inside each 4096-byte PBR argument-buffer slice. The Metal
backend stores and verifies actual texture and sampler object
identity before GPU execution. Legacy groups use zero U2
stride and inherited U2/fragment textures.

This stage implements the GPU backend **resource ABI only**.
Shader Graph is not supported by this ICB path, nor are
arbitrary material shader permutations. Renderer3D has not yet
started emitting per-command PBR records. Existing features
remain disabled by default. Objective-C++/MSL compilation,
GPU execution, CTest and real image parity are NOT RUN.


## Renderer3D per-command heterogeneous opaque PBR grouping — Stage 10AD

`Renderer3DModels.cpp` now uses a shared
`packGpuPbrMaterialU2(material)` helper for the existing
ordinary `drawModelBatch` U2 upload and for experimental
ICB command descriptors. The helper packs the exact same
9×float4 (144-byte) U2 payload; actor-reveal overrides remain
in the ordinary draw path, since GPU ICB eligibility excludes
`kDrawActorReveal`. Built-in PBR source texture slots 1..6
are resolved with the same white/black/flat-normal fallbacks
as normal Renderer3D drawing.

**Explicit experimental flags**: `AIVERSE_EXPERIMENTAL_ICB_MIXED_PBR=1`
requires both `AIVERSE_EXPERIMENTAL_ICB_MIXED_VT=1` and
`AIVERSE_EXPERIMENTAL_ICB_PER_COMMAND_VT=1`, in addition to
the pre-existing GPU-authored/Metal ICB flags. Only built-in
opaque PBR materials with matching structural pipeline traits
and valid VT bindings are eligible. A grouping hash intentionally
ignores U2 and source textures *only* inside that opt-in path;
exact `equivalentGpuIcbPbrPipelineMaterial` still checks
pipeline-sensitive flags.

Each command captures `GpuIcbVirtualMaterial` with its six
source TextureHandles and U2 payload, and
`GpuMaterialResourceBinding` with the same values for exact
frame/generation ticket validation. The render-phase check
rebuilds the live material payload and resolves source handles
again before executing a GPU ICB. A changed texture, sampler
handle identity (checked again on Metal), U2, or pipeline mode
causes CPU fallback. Shader Graph and masked, blended and
non-VT materials still use their prior conservative path.
No regular CPU material batching policy was relaxed.

**Validation**: source inspection/commits only. Native
C++, Objective-C++, MSL, CTest and real hardware execution
remain NOT RUN; keep all new flags disabled by default.


## PBR ICB host-submission diagnostics — Stage 10AE

`Renderer3D::gpuModelDrawStats()` now publishes
`perCommandPbrSubmissions` and `mixedPbrSubmissions`.
Both are incremented only when
`Device::executeGpuEncodedIndexedIcb` returns success
for an opt-in per-command PBR group. The mixed counter
requires at least two distinct U2 or source texture
payloads in the accepted group. These are **host-accepted
submission** counters, NOT GPU-completion events or
screenshot parity evidence.

They reset on GPU draw preparation; existing ICB
preflight/rejected/failed counters remain unchanged.
No native compile or GPU test was run for this stage.


## Non-VT per-command built-in opaque PBR Metal ICB ABI — Stage 10AF

The optional RHI `GpuIcbPbrMaterial` consists of six sampled
TextureHandles for built-in fragment slots 1..6 and 36 finite
float values representing the nine-float4 U2 block.
`gfx::Device::prepareGpuEncodedIndexedIcbWithPbrMaterials`
accepts a per-command array and returns zero on unsupported
backends. It is independent from Virtual Texture atlas/page-table
resources, though both paths reuse the same experimental Metal
ICB argument-buffer machinery.

On Metal, the opt-in flag
`AIVERSE_EXPERIMENTAL_ICB_PER_COMMAND_PBR=1`
permits source-level preparation of a non-VT per-command PBR
group. The backend converts these descriptors into internal
records with `hasVirtualTexture=false`, stages independent
PBR texture/sampler pairs and 256-byte-aligned U2 slices, and
requires the PBR ICB shader variant during execution even if
the ordinary pipeline could be used for homogeneous ICB.
It does **not** bind VT atlas/table arguments or a per-command
U6; the original non-VT shader remains selected.

Renderer3D has not yet been wired to submit this new API;
only the optional RHI/Metal implementation is present in this
stage. The existing VT and legacy shared-resource ICB paths
are unchanged by default. Full Objective-C++ build, Metal
shader compilation, actual ICB execution and GPU image parity
are NOT RUN. This is not production-ready.


## Renderer3D mixed opaque PBR without Virtual Texture — Stage 10AG

`Renderer3D::prepareGpuModelDraws` can now group GPU-equivalent
opaque built-in PBR materials with different U2 and fragment
source textures even when **none** of the draws use a virtual
texture. This path requires the two opt-ins
`AIVERSE_EXPERIMENTAL_ICB_MIXED_PBR=1` and
`AIVERSE_EXPERIMENTAL_ICB_PER_COMMAND_PBR=1`, in addition
to the existing Metal GPU ICB enable/safety flags.

The six source handles and 144-byte U2 are staged for each
command in a `gfx::GpuIcbPbrMaterial`. The renderer then
uses `prepareGpuEncodedIndexedIcbWithPbrMaterials`; for
VT-containing groups it still uses the separate per-command
VT entry point. In either case, `GpuMaterialResourceTable`
holds one exact resource ticket per draw, including the
PBR source handles and full U2. During render execution
the current source handles and U2 are resolved again,
checked for exact equality, and pipeline structural flags
are checked against the staged snapshot. Failed or stale
submissions remain eligible for standard CPU drawing.

The pipeline group prefilter keeps nonopaque/masked,
transparent, alpha cutout, Shader Graph and unsupported
permutation materials on the original exact-equivalence
path. A non-VT PBR opt-in never bypasses CPU MDI or
ordinary-instancing compatibility checks. All feature
flags remain disabled in default builds.

**Validation:** source inspection and GitHub commits only.
No C++/Objective-C++ build, CTest, MSL shader compilation,
Metal device frame capture or pixel parity test was run.


## Stage 10AH — bounded Shader Graph resource identities in GPU ICB

The opt-in RHI material descriptors `gfx::GpuIcbVirtualMaterial` and
`gfx::GpuIcbPbrMaterial` now carry `graphTextureCount` (0..4) and four
optional source `TextureHandle` values. The first N handles must be valid,
and the unused tail must be zero. The existing `GpuMaterialResourceBinding`
already supports these exact four graph resources.

Metal's experimental per-command resource preparation now captures both
the `Texture2D` objects and their native sampler states for all authored
graph textures, and preserves handle identities for later execution
checks. It rejects invalid handles, array/non-2D sources and
multisampled textures. A descriptor containing graph sources without
per-command U2/PBR is invalid. The non-VT convenience API forwards
the same graph resource list.

This stage is **CPU/native resource staging only**. MSL argument-buffer
declarations, GPU shader permutation eligibility and Shader Graph
renderer submission are separate subsequent stages. Native/Metal
compiler and GPU tests have **NOT RUN**.


## Stage 10AI — Metal Shader Graph ICB fragment argument ABI

`ModelShaders.h` has a dedicated `AIVERSE_SG_ICB_GRAPH`
`IcbPbrTextures` argument-buffer layout for fragment-only graphs.
It encodes up to four graph textures in IDs 26/28/30/32 (normal
texture index 13..16) and samplers in IDs 27/29/31 for 13..15.
The fourth graph texture uses existing constexpr sampler s16;
Metal has no dynamic sampler slot 16. For a graph that also
uses virtual texture sampling, its physical atlas/page table
follow the graph sources: texture slots `13+N` and `14+N`,
or argument IDs `2*(13+N)` and `2*(14+N)`.
The original non-graph ICB layout is kept.

`Renderer3DModels.cpp` marks opaque, fragment-only compatible
graph shader variants with
`AIVERSE_SG_ICB_GRAPH` and `AIVERSE_SG_TEXTURE_COUNT`.
It generates conditional direct Metal fragment parameters,
which are absent from the `AIVERSE_ICB_PBR` shader variant.
Fragment graph sample macros alias normal texture slots to
`icbTextures` within the opt-in ICB code path, without
touching the ordinary non-ICB shader.

`GfxMetal.mm` now permits such an explicitly marked graph to
build the PBR ICB fragment pipeline. For every command it
validates shader source count against its staged resource
snapshot and encodes the exact graph source textures/samplers
into argument buffer IDs. When VT is active, atlas/table IDs
move after those sources. Changed handles or sampler objects
reject execution.

**Important:** graph geometry eligibility and Renderer3D per-draw
snapshot submissions have not been enabled by this stage.
These changes are a source-level ABI; native C++/Objective-C++,
MSL and GPU shader compilation/tests have **NOT RUN**.


## Stage 10AJ — opt-in fragment-only Shader Graph in Renderer3D GPU ICB

`Renderer3D` now admits bounded fragment-only, opaque material
graphs to the experimental GPU-authored Metal ICB path **only**
when `AIVERSE_EXPERIMENTAL_ICB_SHADER_GRAPH=1`, the existing
`AIVERSE_EXPERIMENTAL_ICB_MIXED_PBR=1` and
`AIVERSE_EXPERIMENTAL_ICB_PER_COMMAND_PBR=1` gates, plus
the platform GPU ICB safety gates, are enabled. VT graph materials
also require the existing per-command VT opt-in. All flags remain
off by default.

`gpuIcbFragmentGraphEligible` rejects vertex/skin-deforming
graphs, non-material graphs, missing colour output, transparent
or cutout materials and more than four source textures.
`equivalentGpuIcbGraphPipelineMaterial` compares actual
compiled fragment and vertex graph sources, node output map,
graph quality and VT usage, plus pipeline-sensitive model flags.
Resource path differences are allowed **only** when generated
shader code and texture slot counts are identical; every command
then carries its own graph source handles and sampler state.
`gpuIcbGraphPipelineHash` is strictly a prefilter for this
full equivalence, never authoritative alone.

Per-command `GpuIcbPbrMaterial` and `GpuIcbVirtualMaterial`
carry the graph's 0–4 source handles; these exact handles
participate in `GpuMaterialResourceTable` identity and
generation checks. Renderer3D resolves current texture handles
again immediately before GPU execution; Metal validates native
texture/sampler identities too, using the conditional graph
`IcbPbrTextures` ABI introduced in Stage 10AI.
Non-VT fragment graphs use PBR U2 and graph textures without
requiring VT resources; explicit VT graph samples use the
domain's physical atlas/page table after those graph sources.
Graphs needing vertex outputs continue to draw normally.
The Metal vertex shader is no longer injected with unused
source-texture arguments for compatible fragment-only graphs.

**Verification:** source inspection/commits only. Neither native
C++/Objective-C++ nor MSL pipeline compilation nor actual GPU
execution/image parity has been tested. This is opt-in,
experimental functionality, not a release-ready feature.


## Stage 10AK — Shader Graph GPU ICB host-accepted diagnostics

`Renderer3D::GpuModelDrawStats` now has two additional
host-submission counters, `shaderGraphIcbSubmissions`
and `mixedGraphSourceSubmissions`. They are updated only
after the Metal ICB execution API returns success, and are
reset at GPU batch preparation. The second counter requires
different graph source `TextureHandle` arrays or graph source
count within a shader-compatible submitted group; different
ordinary PBR U2 alone does not count as mixed graph resources.

The group snapshot records whether an experimental fragment-only
Shader Graph pipeline was selected and whether any staged
per-command graph handles differ. These diagnostics supplement
`gpuIcbPrepared`, `gpuIcbRejected`,
`gpuIcbExecuteFailed`, `perCommandPbrSubmissions` and
`mixedPbrSubmissions`. They are host-accepted submissions
only, NOT completed GPU fence events, runtime pixel parity
or measured speedups.

No native code, MSL shader compiler, actual GPU execution
or benchmark was run for this stage.


## Stage 10AL — fail-closed Shader Graph fragment slot preflight

The experimental `Renderer3D` GPU ICB graph candidate gate
now checks `Device::maxFragmentTextureSlots()` before allowing
a fragment-only graph on the GPU-authored path. It rejects
source indices beyond the current RHI budget and, for graphs
that request virtual texture sampling, requires space for
both physical atlas and page-table slots after the authored
graph sources. The normal CPU renderer remains eligible
for every rejected draw.

The native Metal per-command resource preparation separately
checks the same physical texture-slot capacity for up to four
graph source textures and an optional VT atlas/page table.
It cannot encode an out-of-bounds fragment argument ID even
if an application bypasses Renderer3D and directly uses the
optional RHI descriptor.

A source-only cross-file audit of the current shader and Metal
host code checked all N=0,1,2,3,4 layouts with optional VT:
atlas slots `13+N`, tables `14+N`, Metal argument IDs
`2*(13+N)` and `2*(14+N)`. All five shader source branch
declarations matched the host index formula; the fourth
graph source uses compile-time s16 and no dynamic sampler 16.
These are **source consistency checks**, not native shader
compilation or GPU execution.

No C++/Objective-C++ compile, Metal shader compiler, CTest,
GPU execution or visual/performance test was run.


### Stage 10AM — Vulkan MSAA image format query loader repair

The Linux arm64 GCC GitHub Actions job 135396475446 in run 44475967180 failed compiling `engine/gfx/vulkan/VkResources.cpp:577` because `vkGetPhysicalDeviceImageFormatProperties` was not declared under `VK_NO_PROTOTYPES`. The runtime Vulkan loader uses `AIVERSE_VK_INSTANCE_FUNCS` to declare, define, and obtain instance-level function pointers. This entry was missing.

`engine/gfx/vulkan/VkApi.h` now lists `vkGetPhysicalDeviceImageFormatProperties` alongside the other physical-device format queries. The existing `VkApi.cpp` macros automatically declare the runtime pointer, define it and require it during instance initialization. No unconditional Vulkan SDK symbol linkage is introduced.

**Observed evidence:** actual CI log, Linux arm64 GCC at 368/1314 objects, compile error as above. **Post-fix compile/CTest NOT RUN**; the failing run was for an older head SHA and must not be represented as evidence of this new commit passing.


### Stage 10AN — Metal Shader Graph tex16 sampler parity guard

The fourth independently authored Shader Graph fragment texture occupies tex16. Metal has only sixteen dynamic fragment sampler slots; the current ICB MSL source uses a hard-coded constexpr `s16` with normalized coordinates, repeat address, linear filtering and linear mip sampling. Binding a clamp/nearest source there would silently change rendered texels from the ordinary renderer.

`MTTexture` now preserves the source `TextureDesc` sampler semantics (`samplerRepeat`, `samplerLinear`, `samplerCompare`) during `createTexture`. Experimental ICB resource staging validates source slot 16 against those original flags; it rejects nearest, clamp or compare sources rather than drawing with incorrect sampling. Graph sources 13..15 still use their dynamic native sampler states. Rejected ICB groups retain the normal CPU-render fallback.

**Verification:** source-level inspection of `GfxMetal.mm::createTexture` and `Model.cpp` texture upload and native sampler allocation. No Apple MSL compiler, Metal GPU or image parity run.


### Stage 10AO — portable GPU ICB material resource CTest

Added `tests/render/gpu_icb_material_contract.cpp` and registered `aiverse_gpu_icb_material_contract_test` / CTest `gpu_icb_material_contract` under the existing `AIVERSE_BUILD_TESTS AND NOT EMSCRIPTEN` test target section. The test compiles against actual public engine headers `GpuMaterialResourceTable.h`, `GpuMaterialState.h` and their RHI dependencies.

The executable checks bounded exact deduplication; per-frame generation invalidation, even when a frame serial repeats; source/VT table/wrap/U2 identity changes; graph source counts, missing/tail handles, finite U2; native RHI descriptor validation; conservative opaque/masked/normalmap pipeline grouping; and fragment-only graph equivalence, including different authored source paths with byte-identical shader source. The test uses `std::numeric_limits<float>::quiet_NaN()` instead of a compiler-specific builtin.

**Actual check:** code added to GitHub and verified CMake registration exists. **The test was NOT built or executed in this environment** (the local container cannot resolve github.com and cannot clone the complete AIVERSE source tree); no pass count should be claimed.


### Stage 10AP — focus verifier executes GPU ICB material and MSL layout tests

The portable `gpu_icb_material_contract` CTest now also includes
`ModelShaders.h` and checks that the actual generated MSL source has
matching atlas/page-table argument ID declarations for every Shader Graph
source count N=0..4 (atlas at 13+N, table at 14+N), and that tex16
does **not** declare dynamic Metal sampler 16.

The focused verification script `tools/verify-texture-rhi.sh` now
explicitly builds `aiverse_gpu_icb_material_contract_test` and includes
`gpu_icb_material_contract` in **both** normal and `--vulkan` CTest
filters. Merely registering a CMake test did not cause this targeted
verification script to execute it; this is fixed. The existing Texture
RHI GitHub Actions workflow invokes this script, so a future run will
exercise the new portable contract.

**Actual verification performed in this turn:** current GitHub source
inspection confirmed the build target and CTest name match, both test
filters include the test, the test includes real headers, and all five
MSL Shader Graph variant branch strings are present (7/7 static
registration/source assertions). The GitHub Actions run observed was
on an older SHA and had queued texture jobs; **the updated test has NOT
been compiled or executed, Metal MSL has NOT been compiled, and no
GPU execution/image parity has been recorded**. Stay Draft.


### Stage 10AQ — bounded total CPU fallback for layered DDS/KTX2 imports (source-only)

`render::TextureUploadOptions::maxCpuDecodedBytes` is the maximum **aggregate
retained RGBA8 decoded bytes** in the entire mip chain for
`uploadContainerTexture`, `uploadDdsImage` and `uploadKtx2Image` when they
must use CPU decompression. Default: 512 MiB; set zero to disallow CPU
fallback, or increase deliberately for trusted large assets. The cap does
not apply to native compressed uploads that the RHI accepts.

Before decoding any slice or calling `Device::createTexture` on the CPU
fallback path, the uploader now preflights the decoded width × height ×
4 bytes × layer/face/depth count at **every mip**, checks the existing
512 MiB per-mip limit and uses division-based overflow checks to sum the
whole chain. On exceeding the caller's budget it returns an invalid handle
with `decoded texture chain exceeds CPU fallback byte budget`. Native
compressed allocation may already have been attempted, if requested, but
no CPU fallback texture is allocated when preflight fails.

New source-level test cases in `tests/render/texture_asset.cpp` cover a
12-face 2-mip array (768 + 192 = 960 decoded bytes): a 959-byte budget
fails before GPU creation, exactly 960 succeeds, and zero rejects CPU
fallback. These C++ cases have been **added but NOT COMPILED/EXECUTED**
in this tool environment. This budget caps retained decoded images, not
instantaneous decoder scratch, native GPU allocations or file size.


### Stage 10AR — one-mip-at-a-time CPU fallback uploads (source-only)

After the Stage 10AQ *logical decoded-chain* budget preflight, the importer
no longer allocates a `std::vector` for every decoded mip simultaneously.
It reuses one RGBA8 mip buffer, including all of that mip's faces, layers,
and volume slices, to initialize the base texture and then uploads each
subsequent authored mip via `Device::updateTextureRegion`. The native
`Device::createTexture` and `updateTextureRegion` contracts must consume
caller-provided CPU bytes synchronously. An error decoding a later level
now also destroys any already allocated GPU handle; upload failures retain
the same rollback behavior.

`maxCpuDecodedBytes` limits the **logical total number of decoded RGBA8
bytes** across all levels, not simultaneous resident CPU memory. Peak
retained mip data is instead bounded by the largest level (plus per-slice
decoder scratch and backend staging). Existing cube-array/3D/second-mip
mock checks cover the upload ordering and resource cleanup, but this
refactor has **NOT been compiled or run** on an actual backend.


### Stage 10AS — focused texture verifier Bash compatibility repair

The standard macOS command remains:

```bash
git switch feature/texture-rhi-unification
git pull --ff-only origin feature/texture-rhi-unification
bash -n tools/verify-texture-rhi.sh
bash tools/verify-texture-rhi.sh --editor --jobs 4
```

The verifier now expands zero optional `-D` CMake flags safely under
macOS Bash with `set -u`, and has a single syntactically complete CTest
runner following optional shader validator registration. The older script
stopped at line 62 with `cmake_flags[@]: unbound variable`, before CMake
configuration finished. This repair addresses the **verification script**;
it does not imply native compilation, CTest passes or GPU validation.
Read `build/texture-rhi/verify-logs/{configure.log,build.log,ctest.log,summary.txt}`
for actual results after rerunning locally. The PR remains Draft until
native/platform checks confirm correctness.


### Stage 10AU — Mac Studio Clang compile regressions (source fixes)

A real macOS `--editor` verifier run on `ddd096979` configured CMake and
compiled 13 focused targets, but eight build targets failed. The extracted
`build.log` identified three root causes rather than eight independent
failures: `ModelVirtualTextureSampling.h` used `bodyAnchor.size()` on
`const char* const`; `virtual_texture_feedback_pass.cpp` declared
different config types together with a single `auto`; and the Radiance
HDR fixture called the four-component `pixel()` helper with only two
RLE code/value arguments. The source corrections give `bodyAnchor`
a `std::string` type, split the two declarations, and use a separate
two-byte RLE helper. This preserves the engine API and source asset data.

**Actual evidence:** user's macOS build/CTest logs from
`aiverse-texture-logs.tar.gz` for commit `ddd096979`; fix commits exist
on the feature branch. **Post-fix macOS compile/CTest NOT YET RUN**.
Verify in isolated worktree with
`bash tools/verify-texture-rhi.sh --editor --jobs 4`.
The original checkout remains in an unrelated unresolved rebase.


### Stage 10AV — async feedback mock callback slot cleanup

The Mac Studio CTest report at `ddd096979` additionally reported
`virtual_texture_file_streamer` as an **executed, failed** test, not merely
a missing executable. Its assertion was `capture next frame after consuming
result` in `asynchronousGpuFeedbackRuntime()`.

The mock device's `finishFeedback` now clears its `std::function` member
explicitly **before** invoking the moved callback. C++ permits a valid but
unspecified moved-from function state; the mock must model an RHI whose
one-in-flight readback slot becomes free at completion. A new assertion
checks the cleared slot after the initial callback, and the second-capture
assertion reports `runtime.lastError()` and whether the mock slot remained
occupied if it still fails. Production `VirtualTextureFeedbackRuntime` and
real backend code are unchanged. This change is a test-harness lifecycle
fix, not confirmation of real GPU readback behavior.

**Post-fix CTest NOT RUN.** Run the focused texture verifier on macOS and
examine `ctest.log` for `virtual_texture_file_streamer`.


### Stage 10AX — macOS editor and remaining CPU CTest regression fixes

The second real macOS verifier run of the previous texture branch successfully
compiled all 20 focused texture targets, but failed `aiverse_editor` in
`Renderer3DModels.cpp:2137` (undeclared
`pipelineMaterialCompatible`) and three of 23 CTests:
`texture_asset`, `virtual_texture_file_streamer`, and
`virtual_texture_feedback_pass`.

The editor's GPU ICB submission path now performs the equivalent
pipeline-only compatibility test **within its own scope**, using
`equivalentGpuIcbGraphPipelineMaterial` for graph variants and
`equivalentGpuIcbPbrPipelineMaterial` for non-graph variants. It
remains fail-closed if a staged material has changed before ICB execution.
The original local grouping gate in `prepareGpuModelDraws` is unchanged.

The texture importer test clears its mock `failUpload` fault injection
immediately after verifying GPU rollback, so a later successful
compressed-to-RGBA8 fallback is not inadvertently forced to fail.
The file-streamer test explicitly empties a callback member after
`std::move` hands it to a simulated in-flight GPU completion, allowing a
new capture after runtime reinitialization. The feedback-pass test checks
all successfully created textures have been destroyed, including the
allocation from an intentionally rejected intermediate initialization,
instead of expecting exactly one destruction over the entire test.

These are narrowly scoped changes to one renderer source file and three
CPU test fixtures. Existing GPU / ECS ABI, file formats, and public APIs
are unchanged; test assertions are preserved or strengthened.

**Actual evidence:** user-provided native Clang error and CTest stderr,
plus source-level review of the current GitHub files; source/test changes
committed to the branch. **Post-fix native compile and CTest have NOT RUN**
in this environment. The PR remains Draft until Mac tests and cross-backend
GPU acceptance pass. Run
`bash tools/verify-texture-rhi.sh --editor --jobs 4` in the isolated
texture verification worktree after pulling the latest feature branch.


### Stage 10AZ — Metal Texture3D upload Objective-C selector correction

The Mac Studio verification run passed **23/23 focused CTests** but
`aiverse_editor` still failed compiling
`engine/gfx/metal/GfxMetal.mm` at lines 654 and 723. Both Texture3D
CPU upload paths used an Objective-C selector that does not exist:
`replaceRegion:mipmapLevel:withBytes:bytesPerRow:bytesPerImage:`.

Metal's supported 3D upload method is
`replaceRegion:mipmapLevel:slice:withBytes:bytesPerRow:bytesPerImage:`.
For `MTLTextureType3D`, the `slice` argument is **0**; the
`MTLRegionMake3D` origin.z and depth describe the actual volume region.
Both full-volume creation and `updateTextureRegion` now pass `slice:0`,
while preserving `bytesPerRow`, `bytesPerImage`, mip level, origin.z and
depth. This does not change the generic RHI API, existing 2D/array/cube
upload semantics, ECS, or serialized textures.

**Source fix:** `094bfe7c602fd57b8defa6bae2474a28cd04c3de`.
Reference: Apple MTLTexture documentation for
`replaceRegion:mipmapLevel:slice:withBytes:bytesPerRow:bytesPerImage:`.

**Observed before this fix:** all 23 focused CTests passed on Mac Studio;
the editor compile stopped at the two invalid 3D Metal selectors.
**Post-fix macOS ObjC++ compilation, editor linking and GPU volume
upload/readback were NOT RUN** in this assistant environment. Real GPU
format/copy parity remains a separate requirement.

In the independent macOS worktree:
```bash
git pull --ff-only origin feature/texture-rhi-unification
cmake --build build/texture-rhi --target aiverse_editor --parallel 4
# Once the editor builds:
bash tools/verify-texture-rhi.sh --editor --jobs 4
```


### Stage 10BA — macOS native focused verification PASS (2026-11-19)

**Actual user-executed evidence** from Mac Studio, isolated worktree
`/Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819`,
after pulling `feature/texture-rhi-unification` (source fix checkpoint
`79e8c04fc10074fc5c8723cbe47d77113d9025ba`):

- `cmake --build build/texture-rhi --target aiverse_editor --parallel 4`:
  `[100%] Built target aiverse_editor`.
- `bash tools/verify-texture-rhi.sh --editor --jobs 4`:
  `CONFIGURE PASS`; every one of the **20** named texture test
  executable targets reported `BUILD PASS`; `BUILD PASS aiverse_editor`;
  GLSL validator and Metal `xcrun` reported present; `CTEST PASS`;
  final line `OVERALL SOURCE TESTS PASS (GPU validation remains separate)`.
- A preceding Mac CTest log explicitly showed **23/23 PASS**, including
  `virtual_texture_feedback_shaders` and
  `virtual_texture_feedback_msl`, and the latest focused verifier
  reported CTest success with the same source test filter. The latest
  per-test `ctest.log` has not been uploaded in this turn, so do not
  claim new per-test timings or hardware execution.

**Status:** the native macOS compile/link + focused source/test gate for
this texture branch is now confirmed green by user terminal output.
This **does not** establish Metal GPU runtime fidelity, real GPU
Texture3D partial z-region readback, layered mip/cube visual parity,
actual VT physical-page publication on hardware, GPU ICB completion,
performance, or Windows/Linux/Android/iOS/WebGL2 compatibility.
Test executables and GLSL/MSL validators are not substitutes for
on-device graphics acceptance. PR #21 remains Draft until these
independent acceptance checks are performed and recorded.

**Next suggested focused validation:** perform a real-device Metal
Texture3D volume creation and nonzero-mip `updateTextureRegion`
with partial z-range, then read/sampling parity; check all six cube
faces / array layers and authored mip levels; verify async readback
callback lifetime and VT feedback/page-table publication with actual
rendered pixels. Run `--vulkan` only where a Vulkan SDK and suitable
driver are available. Keep the existing main worktree's unrelated
incomplete rebase untouched.


### Stage 10BB — on-device Metal texture acceptance fixture (opt-in, NOT YET RUN)

A real macOS GPU test now lives at `tests/render/metal_texture_gpu.mm`.
It directly creates the **production** `gfx::createMetalDevice` from
`engine/gfx/metal/GfxMetal.mm` using a small `CAMetalLayer`; it does not
substitute a mock texture device. The standalone CMake target is
`aiverse_metal_texture_gpu_test`, and CTest name is `metal_texture_gpu`
(labels `METAL;GPU;TEXTURE_RHI`, timeout 60 seconds).

The test covers:

- `RGBA8 Texture3D` 4×4×4 base volume initialized with distinct per-Z
  colors; an x/y 2×2 patch at mip 0, Z=2; an authored mip-1 Z=1 slice;
  invalid Z and too-short row-pitch rejection.
- `Texture2DArray`, `Cube`, `CubeArray` with different initial face/layer
  colors; update of the last face's authored mip 1.
- **Actual Metal command-buffer copy** through
  `gfx::Device::readTextureAsync()` for each selected face, Z plane and
  mip. Checks exact RGBA8 bytes, dimensions, format, callback completion,
  GPU device health, and the live/destroyed 3D resource inventory.

It is deliberately excluded from the standard 23 focused CTests and
from **all default CTest registration**. CMake option
`AIVERSE_TEXTURE_METAL_GPU_TESTS` defaults to `OFF`; the verifier's
`--metal` switch turns it `ON` and builds/runs the hardware test.
Opt in on a Mac with a working Metal GPU:

```bash
cd /Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819
git pull --ff-only origin feature/texture-rhi-unification
bash tools/verify-texture-rhi.sh --editor --metal --jobs 4
cat build/texture-rhi/verify-logs/metal-gpu.log
```

Alternatively, explicitly configure GPU testing first:

```bash
cmake -S . -B build/texture-rhi -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_TEXTURE_METAL_GPU_TESTS=ON
cmake --build build/texture-rhi --target aiverse_metal_texture_gpu_test --parallel 4
ctest --test-dir build/texture-rhi --verbose --output-on-failure --no-tests=error -R '^metal_texture_gpu$'
```

`--metal` also explicitly disables hardware-test registration on a later
ordinary verifier run using the same CMake cache. The GPU test is
registered **only** when requested; normal CI CTest remains unaffected.
`--metal` fails on non-macOS hosts, fails if the GPU is unavailable,
fails if the test binary is not registered, and fails on incorrect
readback bytes or callback timeout. A CTest `SKIPPED` event must never
be reported as GPU success. The verifier records detailed GPU output
under `build/texture-rhi/verify-logs/metal-gpu.log` and reports the
GPU test's result separately from the focused source tests.

**Scope limitation:** this first native GPU acceptance tests uploads,
blit readbacks and resource lifetime, **not** fragment shader sampling,
GPU virtual page feedback, ICB multi-material execution, performance,
hardware sparse residency, or cross-platform driver support. It does
not prove successful GPU execution until the Mac Studio actually runs
the opt-in target and supplies its log. Previous confirmed 23/23 CTest
and editor PASS apply only to source validation on the older tested
commit, not this new GPU test source.

  
### Stage 10BD — repair truncated `--metal` verifier footer after macOS run

The first Mac Studio `--metal` attempt based on `a81fdbad9`
**compiled** the 20 focused texture targets, `aiverse_editor`, and the
new `aiverse_metal_texture_gpu_test`; focused source CTest also exited
0. However, the verifier reached a malformed shell footer:
line 171 contained only `-R '^metal_texture_gpu`, with a missing
closing quote, output redirect, and a duplicated final block. Mac Bash
reported `unexpected EOF while looking for matching '\''`
and never ran `metal_texture_gpu`. No `metal-gpu.log` was produced,
so GPU acceptance was **NOT RUN**.

The repaired footer restores the complete CTest command
`-R '^metal_texture_gpu$' >"$logs/metal-gpu.log" 2>&1` and a single
error-accounting/summary block. The verifier now runs
`bash -n "${BASH_SOURCE[0]}"` at startup, BEFORE CMake or build
operations; Linux and Vulkan GitHub Actions jobs also run
`bash -n tools/verify-texture-rhi.sh` before invoking the script.
The corrected footer alone was parsed successfully under Linux Bash
(`bash -n`, exit 0); **the complete script and real Metal CTest have
NOT yet been run on macOS after the fix**.

Since the GPU target already built on the Mac, it can be executed
without repeating all focused source builds:

```bash
cd /Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819
git pull --ff-only origin feature/texture-rhi-unification
bash -n tools/verify-texture-rhi.sh
ctest --test-dir build/texture-rhi --verbose --output-on-failure \
  --no-tests=error -R '^metal_texture_gpu$'
```

This direct CTest does not create `metal-gpu.log`; capture its terminal
output or use `ctest --output-log`. For a complete later run, invoke
`bash tools/verify-texture-rhi.sh --editor --metal --jobs 4`.
A native GPU PASS must be based on measured readback bytes, not
`BUILD PASS` or source-only CTest success.


### Stage 10BE — on-device Metal texture GPU CTest PASS (2026-11-19)

**Actual user-run Mac Studio CTest result:** in the isolated
`AIVERSE-texture-verify-20261119-150819` worktree, the explicit
`metal_texture_gpu` test (CTest #122, labels `GPU`, `METAL`,
`TEXTURE_RHI`) completed **Passed** in **1.53 seconds**.
The CTest summary reported **1/1 test passed; 0 failed**,
total elapsed time 1.54 seconds.

This is on-device acceptance evidence for the scope of
`tests/render/metal_texture_gpu.mm`: creation and RGBA8 data
upload for Texture3D; selected Z-plane and mip updates;
2DArray/Cube/CubeArray face/layer updates; asynchronous GPU
blit readbacks and exact expected byte comparisons;
negative bound/pitch checks, GPU-health and resource inventory.
No explicit device model or per-probe byte trace was supplied,
so neither a precise GPU name nor individual test timestamps
are asserted here.

**Scope limitation:** This is **one actual hardware GPU CTest PASS**,
not full validation of shader sampling, Metal ICB,
fragment-texture Shader Graph, virtual page feedback/page
publication, rendering visual parity, performance or other
backends/platforms. The user subsequently provided the final output from
`bash tools/verify-texture-rhi.sh --editor --metal --jobs 4`:
`CTEST PASS`, `METAL GPU PASS`, and
`OVERALL SOURCE AND METAL TEXTURE GPU TESTS PASS
(other GPU validation remains separate)`.
**The full combined macOS source + Metal GPU acceptance gate is PASS.**
These final lines establish the test exit-status gates, but do not
identify the physical GPU model or include the verbose per-pixel log.

All source gates previously passed on this Mac; Stage 10BB's
new Metal acceptance binary was reported to build successfully
in the preceding user run. PR #21 should remain Draft until
the other explicit GPU scenarios are verified. No production
engine source modifications were needed for the reported PASS.


### Stage 10BF — Metal GPU fixture phase timing (source only)

Previously the user measured a **1.53 s CTest total** for the native
`metal_texture_gpu` test and asked why. The wall-clock CTest time is
**not** the duration of GPU texture uploads: it includes test process
launch, `MTLCreateSystemDefaultDevice`, `CAMetalLayer`, AIVERSE
`MetalDevice` construction, texture allocation and uploads, one or
more queued command buffers, `beginFrame/endFrame`/drawable/semaphore
overhead, asynchronous readback completion, CPU comparisons and sleeps.

`tests/render/metal_texture_gpu.mm` now logs **measured milliseconds**
for device/layer startup, Texture3D and each 2DArray/Cube/CubeArray
create-and-upload segment, each group's readback submission, callback
wait (includes frame loop and 10ms poll sleeps), the number of
pumped frames and CPU comparison, and overall fixture runtime. It also
requests `gfx::Device::timeGpuFrame(tag)` on the readback command buffer,
then reports the backend's `GpuTiming.milliseconds` if valid.
The optional GPU command interval measures the **whole readback
command buffer**, not isolated blit bandwidth. If the GPU timestamp
is missing, the test prints `unavailable` and does not invent a value.

This stage adds instrumentation only and preserves all exact RGBA8
GPU parity assertions; it does **not** assert a fragile latency
threshold or run a GPU microbenchmark. Source commit:
`50656c4bd120755e6fa14bf8d2774aeb77b7e752`.
The previous 1.53 s user-run GPU acceptance **PASSED** on the older
noninstrumented fixture. The new timing source has **not yet been
compiled or run on macOS**. Actual timing breakdown is pending,
not inferred from total CTest time.

For a focused rerun in the isolated Mac Studio worktree:

```bash
cd /Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819
git pull --ff-only origin feature/texture-rhi-unification
cmake --build build/texture-rhi --target aiverse_metal_texture_gpu_test --parallel 4
ctest --test-dir build/texture-rhi --verbose --output-on-failure --no-tests=error -R '^metal_texture_gpu$'
```


### Stage 10BG — real fragment Texture3D sampling acceptance (source only)

Extend `tests/render/metal_texture_gpu.mm` with a new **real
Metal fragment shader** sampling test. Previously the GPU acceptance
fixture covered native texture upload and blit readback, but not
hardware shader texture sampling. The `kVolumeSampleMSL` shader
uses an ordinary `texture3d<float> tex0 [[texture(0)]]`,
the sampler bound by the existing `gfx::Device::setTexture(0,...)`,
and `coord [[buffer(1)]]` supplied via
`setUniforms(0,...)`. It draws a fullscreen triangle through
`createShader`/`createPipeline`/`beginRenderTargets`/`draw`
into an `RGBA8` Metal render target with a matching depth target.

The fixture now tests three independent sampled colors:
the original volume Z=0 texel, a partial XY patch in Z=2 of
mip 0, and the user-authored Z=1 plane in mip 1. The
rendered `RGBA8` pixels are read back using the same asynchronous
AIVERSE RHI path and checked against expected colors. Existing
blit-level byte comparisons retain **zero tolerance**; shader
float-normalization/output UNORM conversions permit only **1 LSB
per channel**. All shader setup, pass failures, GPU device errors,
and pixel mismatches fail the opt-in GPU CTest; no backend
mock or silent skip is introduced.

Source commit: `9e747cff974d6f44085cc25cfc841198a6f1c309`
(`tests/render/metal_texture_gpu.mm`). Default CPU source tests
and public RHI APIs are unchanged. This is not a Metal Shader Graph
or ICB material test, nor a virtual page feedback/page-table
streaming test. **Not yet compiled or executed on macOS**;
no shader-sampling GPU PASS is claimed until a new device run.

After pulling the feature branch in the isolated worktree,
build and run only the opt-in target:

```bash
git pull --ff-only origin feature/texture-rhi-unification
cmake --build build/texture-rhi --target aiverse_metal_texture_gpu_test --parallel 4
ctest --test-dir build/texture-rhi --verbose --output-on-failure --no-tests=error -R '^metal_texture_gpu$'
```


### Stage 10BH — Apple M3 Ultra fragment sampling and timing PASS (2026-11-19)

**Actually executed:** user ran the opt-in native Metal GPU fixture on
`Metal Apple M3 Ultra` in the isolated macOS texture worktree after
Stages 10BF/10BG. CTest #122 `metal_texture_gpu` **PASSED**
(1/1; 0 failures) in **1.14 s**.

The verbose output confirmed exact RGBA8 readbacks (**0 LSB tolerance**)
for three Texture3D planes/mips, three 2DArray subresources, three
Cube subresources, and three CubeArray subresources; it additionally
confirmed real fragment shader Texture3D sampling of the original
Z=0, changed Z=2 and authored mip1 Z=1 (all **1 LSB tolerance**).

**Measured wall-clock stages:** Metal device/layer initialization
67.186 ms; Texture3D create/upload 2.439 ms; 2DArray 0.411 ms;
Cube 0.395 ms; CubeArray 0.531 ms; Texture3D shader pipeline
setup 194.582 ms; complete fragment sampling group 302.001 ms;
fixture body 413.089 ms; total fixture 480.275 ms.
Per readback group, command buffer GPU intervals ranged
**0.005–0.021 ms** on this machine. These command intervals
include the **whole GPU command buffer**, not a standalone
upload benchmark. Each group required 2 polling frames;
CPU-side callback waits were approximately 12.520–21.439 ms
and submission wall times approximately 4.100–9.129 ms.
The difference between overall CTest time (1.14 s) and fixture
wall-time (0.480 s) includes process/runtime overhead
outside the timestamped test body; its causes were not separately
instrumented. This does **not** show texture transfer taking 1.14 s.

**Scope:** validates real Metal upload/readback and direct fragment
Texture3D sampling, not GPU ICB/Shader Graph resource mixing or
actual VT page feedback and page-table upload. No game
performance regression or cross-backend claim can be made.
The previous source+Metal verifier pass was also confirmed
on an earlier commit. The timing+fragment GPU run applies
to the source at the user's test time, before any further
VT GPU acceptance additions.


### Stage 10BI — real Metal virtual-texture feedback acceptance (source only)

An additional opt-in **real Metal GPU acceptance** path has been added
to `tests/render/metal_texture_gpu.mm`. It exercises the
**production** `gfx::VirtualTextureFeedbackPass`, the canonical
`kVirtualTextureFeedbackMSL` and
`kVirtualTextureFeedbackMaterialMSL` shader helpers, the existing
`gfx::Device` Metal render pipeline, actual offscreen RGBA8
rendering and asynchronous GPU readback, and finally the
`VirtualTextureFeedbackDecoder`.

The test draws a fullscreen triangle to a 4×4 single-sample feedback
target (with depth attachment to match the pipeline), using
8×8 base tiles, three mip levels and three virtual layers.
Two cases must **fail closed** on any error:

- valid UV `(0.8125,0.5625)`, mip `1`, layer `2`,
  expected tile `(3,2)`: all 16 rendered pixels must equal
  `encodeVirtualTextureFeedback({1,3,2,2})` as four **exact**
  RGBA8 bytes. After actual native GPU completion, the generic
  decoder must return one visible page at UV origin `(0.75,0.5)`,
  mip `1`, layer `2`, priority `16`, with no invalid IDs.
- invalid UV `(-0.125,0.5625)`: all 16 pixels must be zero;
  the decoder must report 16 invalid IDs and no visible region.

The `VirtualTextureFeedbackPass` must also enter
`readyToSubmit()` after the GPU callback is delivered on the
graphics device owner thread; no CPU mock substitutes for GPU
completion. This proves GPU shader feedback ID encoding and
owner-thread decoding, not disk-backed physical page allocation
or atlas/page-table streaming. Those are separate future steps.

**Implementation:** source commit
`5830d16f8531d462fa02709a0e6a0f8e5c4ef9f1`.
No native library, public RHI API, genre/ECS, CMake option or
regular CPU test behavior was changed. The new acceptance extends
the existing `--metal` opt-in test binary.

**Verification:** source inspection and GitHub update performed.
**Not yet built or run** on Mac Studio for this commit.
An earlier on-device M3 Ultra test confirmed the previous
Texture3D fragment shader and 3D/array/cube readback (1/1 CTest
PASS in 1.14 s), but this is not a PASS for the new feedback test.

```bash
cd /Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819
git pull --ff-only origin feature/texture-rhi-unification
cmake --build build/texture-rhi --target aiverse_metal_texture_gpu_test --parallel 4
ctest --test-dir build/texture-rhi --verbose --output-on-failure --no-tests=error -R '^metal_texture_gpu$'
```


### Stage 10BJ — on-device Metal virtual texture feedback PASS (2026-11-19)

The user ran the newly extended `metal_texture_gpu` native CTest
on the Mac Studio following Stage 10BI. Actual test report:
`1/1 Test #122: metal_texture_gpu ... Passed 0.45 sec`,
`100% tests passed, 0 tests failed`, `Total Test time 0.46 sec`.
The provided tail of the verbose output showed a real
`VT GPU feedback RGBA8 ID` readback with
`submit_ms=7.758`, `callback_wait_ms=19.502`,
`poll_frames=2`, `compare_ms=0.030`, and
`gpu_command_ms=0.005`. The VT acceptance stage reported
`VT feedback GPU acceptance_total_ms=74.085`,
fixture `393.549 ms` and total CTest `0.45 s`.

**Result interpretation:** the single opt-in hardware CTest is
confirmed PASS on that machine with the added real feedback
rendering/decoding coverage. The excerpt does not separately show
the valid-ID and invalid-UV probe lines or device name, so it
cannot be used to claim individual per-probe timings. The earlier
user-provided full log explicitly identified an Apple M3 Ultra;
the current report does not repeat the device identification.
No production source changed in this record.

**Still not validated:** full physical VT atlas and page-table GPU
publication, disk-backed page streaming, actual GPU ICB/Shader Graph
resource mixing, other backends or a multi-frame game performance
benchmark. Direct feedback ID readback is fast on the GPU, but
the 0.45 s CTest wall time is not a texture transfer benchmark.


### Stage 10BK — native Metal physical VT atlas/page-table publication (source only)

The existing opt-in `tests/render/metal_texture_gpu.mm` hardware CTest
now additionally exercises the **production** `VirtualTextureStreamer`
with a real Metal `gfx::Device` (no mock, no disk worker). It creates
a 4×4 RGBA8 Texture2DArray physical atlas with two slices and a 4×4
RGBA8 Texture2DArray GPU page table for one virtual layer, with
4×4 virtual mip-zero tiles and three virtual mip levels.

The new `gpuPhysicalVirtualAtlas` acceptance verifies, via real GPU
blit readback (zero tolerance for exact RGBA8 bytes):

- Pinned coarsest page `{mip:2,x:0,y:0,layer:0}` stays **unpublished**
  in both CPU state and GPU page table while an upload ticket is pending.
- After `VirtualTextureStreamer::drain`, the physical slice
  contains the exact 4×4 source image and the full page-table texture
  points to physical slice 0, coarsest mip 2, valid=255.
- A fine page `{mip:0,x:3,y:3,layer:0}` enters the second physical
  slice and overrides only its corresponding mip-zero table texel,
  preserving all other pinned coarse fallback mappings.
- A third page `{mip:0,x:0,y:0,layer:0}` triggers deterministic LRU
  reuse of the unpinned fine slot, never the pinned coarse slot.
  **Before** the replacement GPU transfer, the previous fine mapping
  must already be invalidated in the real GPU table and return to
  coarsest fallback, preventing old physical bytes from being sampled
  as the new virtual page.
- After `drain`, the reused physical slice contains the third
  page's new exact pixels and its corresponding GPU table texel
  points at that slice. An explicit eviction subsequently returns
  the GPU table to coarse fallback.
- The final texture inventory must contain both live resources,
  then neither after `shutdown`.

This tests actual Metal atlas/page-table **resource bytes and
publication ordering**, not disk file IO, an application-level
shader sampling through page-table indirection, hardware sparse
residency or Shader Graph ICB. Existing public RHI and native GPU
backend are unmodified. The acceptance remains behind
`AIVERSE_TEXTURE_METAL_GPU_TESTS=ON` / verifier `--metal` only.

**Source commits:** `d8b498d31d3b421261825048ab313bfefa13c6ee`
and `3ca4b1cf872bab4a06d533aa75c4052d036ea41b`.
**Post-change macOS build and CTest NOT RUN.** Earlier 10BJ
`metal_texture_gpu` PASS at 0.45 s applies only to the older
fixture without this physical atlas test.

```bash
cd /Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819
git pull --ff-only origin feature/texture-rhi-unification
cmake --build build/texture-rhi --target aiverse_metal_texture_gpu_test --parallel 4
ctest --test-dir build/texture-rhi --verbose --output-on-failure --no-tests=error -R '^metal_texture_gpu$'
```


### Stage 10BL — Apple M3 Ultra physical atlas GPU acceptance PASS (2026-11-19)

**Actual native GPU evidence, provided by the user:** in the isolated
Mac Studio texture-verification worktree, the extended
`metal_texture_gpu` CTest #122 **PASSED (1/1, 0 failed)** in
**1.62 s** (total CTest 1.63 s).
Exact RGBA8 native GPU readbacks (tolerance 0) confirmed
fine-page override, invalidation of old page-table entries
**before** LRU reuse, physical page replacement, post-LRU
republishing, and coarse fallback after explicit eviction.
The actual test ends with
`PASS: Metal GPU texture subresources, fragment sampling,
VT feedback/page table and async readback parity`.

**Native timestamps:** fine publication readbacks=2,
submit=9.053 ms, callback wait=20.277 ms, GPU command=0.018 ms;
pre-overwrite invalidation readbacks=1, submit=8.905 ms,
callback wait=20.219 ms, GPU command=0.005 ms;
replacement publication readbacks=2, submit=9.035 ms,
callback wait=20.100 ms, GPU command=0.006 ms;
explicit eviction readbacks=1, submit=8.845 ms,
callback wait=20.719 ms, GPU command=0.005 ms.
The complete `VT physical atlas publication_total_ms` was
177.399 ms; the GPU fixture total was 1221.715 ms and
`gpu_test_body_ms` was 475.428 ms. These are distinct
measurement intervals; CTest wall time is **not** page-upload
GPU time and the GPU command interval includes the whole
command buffer, not pure texture-transfer throughput.

**Verified scope:** production `VirtualTextureStreamer`
with actual Metal allocation/update/blit readbacks; per-slot
page-table validity, coarser fallback and deterministic LRU
transition. This stage does not yet exercise the real
`VirtualTextureFileStreamer` disk worker or sample the
page table through a fragment shader, nor does it validate
GPU ICB Shader Graph material mixing or other backends.
No source code changed to achieve this observed PASS.


### Stage 10BM — disk-worker to real Metal virtual texture publication (source only)

Added `gpuDiskBackedVirtualAtlas` to the macOS opt-in
`tests/render/metal_texture_gpu.mm` hardware acceptance.
Unlike earlier direct `VirtualTextureStreamer::stage` tests, this
path starts the **actual** `VirtualTextureFileStreamer` asynchronous
I/O worker. The test creates unique temporary files under the OS temp
directory in the production `m<MIP>/l<LAYER>/x<X>_y<Y>.rgba`
layout, writes 4×4 packed RGBA8 pages, and deletes the files
on test destruction. No test-only GPU device or fake worker result
is used.

The test checks, with bounded 6-second disk/owner-thread polling
and byte-exact real Metal blit readbacks:

- Requesting a pinned coarsest page does **not** publish a mapping
  or make the page resident before `VirtualTextureFileStreamer::pump`.
  The GPU page-table texture initially contains only invalid zero bytes.
- Real disk worker loads the coarse file and owner-thread `pump()`
  stages its data through `TextureStreamQueue` into physical
  atlas slice 0. Both the real atlas pixels and page-table
  bytes match the expected pinned coarse fallback.
- A missing file yields one failed completion with an error string.
  The atlas remains healthy, no exact residency is created and the
  GPU page-table mapping is unchanged.
- A fine page loads into physical slice 1 and overrides the mip-zero
  page-table entry for tile (3,3), while all other tiles retain
  coarse fallback.
- A third page loads from disk and triggers LRU eviction/reuse of
  the unpinned fine physical slice. Real Metal readback confirms the
  pinned coarse slice survives, the reused slice has the new page,
  and the GPU table removes the old fine entry and publishes the
  new mapping.
- `shutdown` releases both Metal textures from resource inventory.
  Timing `VT disk worker to Metal publication_total_ms` and each
  bounded worker/upload completion are logged.

Source commits:
`11ad74279ff2bbd348de16e4981efae645d7ab4c`
(test fixture),
`ebf5f1652c1284eaac6c7be5612def0a78e2c895`
(explicit CMake `Threads::Threads` linkage), and
`e8ba85b16a51eaa8794b67f29e2a6ad8503d1893`
(independent fixture failure handling).
No native production RHI, virtual texture format, user asset
or ECS behavior changed. Hardware test remains opt-in with
`AIVERSE_TEXTURE_METAL_GPU_TESTS=ON` / `--metal`.

**Verification:** source changes committed to GitHub; this new
disk-backed stage has NOT been compiled or executed on the Mac.
The confirmed Stage 10BL Metal GPU PASS (1/1, 1.62 seconds)
does not cover these new lines. In the isolated macOS worktree:

```bash
cd /Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819
git pull --ff-only origin feature/texture-rhi-unification
cmake -S . -B build/texture-rhi -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_TEXTURE_METAL_GPU_TESTS=ON
cmake --build build/texture-rhi --target aiverse_metal_texture_gpu_test --parallel 4
ctest --test-dir build/texture-rhi --verbose --output-on-failure --no-tests=error -R '^metal_texture_gpu$'
```

Passing this would validate the real disk-to-GPU page publication
path for tiny synthetic files. It would **not** yet validate
application-level fragment shader page-table indirection,
hardware sparse residency, GPU ICB, or cross-platform parity.


### Stage 10BN — verified Apple M3 Ultra disk-backed VT GPU streaming PASS (2026-11-19)

Actual Mac Studio native CTest `metal_texture_gpu` #122 **PASSED**
(1/1 tests, 0 failed) in **1.46 sec**, total CTest 1.47 sec.
The exact test log confirmed production disk-worker-to-GPU
`VirtualTextureFileStreamer` acceptance, with all GPU RGBA8
readbacks using **zero byte tolerance**:
- page table invalid before disk worker publication;
- pinned coarse file read and physical atlas/page-table publication;
- missing page file returns a failure without poisoning existing
  GPU mappings or atlas health;
- fine page from disk overrides only the intended tile;
- disk-backed LRU reuse preserves pinned coarse physical slice,
  replaces the fine slice with authored bytes and publishes the
  correct replacement table mapping without stale fine entries;
- GPU cleanup succeeds.

The verbose log measured `VT disk worker to Metal
publication_total_ms=148.740` and per-file
`worker_and_upload_ms` of **0.399 ms** (coarse,
one poll), **3.701 ms** (missing page, two polls),
**3.980 ms** (fine, two polls), and **3.746 ms**
(replacement, two polls). Metal command-buffer intervals for
the displayed readback groups were approximately
**0.005–0.024 ms**; callback waits were approximately
19–21 ms. Total fixture 694.571 ms, body 640.120 ms.
As before, the 1.46 s CTest total is not the duration of
a single GPU texture transfer.

This is **on-device acceptance for Stage 10BM** only:
application fragment shader sampling *through the actual
page-table indirection* was not yet tested in this run.
No engine code changes were required to achieve the PASS.

### Stage 10BO — fragment page-table GPU sampling over disk-backed VT (source only)

Extend the opt-in Mac `metal_texture_gpu` fixture with
`VtAtlasFragmentProbe` and eight sampled GPU pixel checks
after each real `VirtualTextureFileStreamer` publication
transition. Instead of checking only the atlas and page-table
subresources independently via blit, it draws through an
ordinary AIVERSE Metal fragment shader and the **canonical**
`gfx::kVirtualTextureMSL` helper
`aiverse_sample_virtual`. The source of physical atlas
pages is still the real asynchronous disk worker; no
fake backend or separate page-table lookup implementation.

The probe binds the live RGBA8 physical Texture2DArray
atlas to texture slot 0, the RGBA8 page-table Texture2DArray
to slot 1, and the physical atlas sampler to slot 0.
It renders a fullscreen triangle to RGBA8 using a
4×4 color/depth offscreen pass and reads back actual
pixels asynchronously. A nonresident page must yield
fallback `{18,79,181,255}`. A resident pinned coarse
page yields `{18,71,93,255}`, the fine page
`{82,196,41,255}`, and a later recycled physical slot
`{220,44,16,255}`. The test also verifies that a
failed disk read leaves coarse mapping intact and that
the evicted fine address again samples coarse. Pixel
comparisons allow at most **1 LSB/channel** for
GPU float-to-UNORM quantization; the existing native
blit readbacks still require exact bytes.

This does not validate Shader Graph material ICB mixing,
sparse residency, tile gutters for heterogeneous images,
or every address mode; it verifies the real page-table
sampling data flow for the test's pinned/evicted pages.
**Source commit:** `32d69c8c6215ed470f9a1980bb212eb534eac1c5`,
`tests/render/metal_texture_gpu.mm`. Public RHI and
streaming implementation are unchanged. New shader
sampling test has **NOT YET been compiled/run on macOS**.

Run after pulling the feature branch:
```bash
cd /Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819
git pull --ff-only origin feature/texture-rhi-unification
cmake --build build/texture-rhi --target aiverse_metal_texture_gpu_test --parallel 4
ctest --test-dir build/texture-rhi --verbose --output-on-failure --no-tests=error -R '^metal_texture_gpu$'
```


### Stage 10BP — actual Metal fragment page-table indirection PASS (2026-11-19)

**Actual Mac Studio device evidence:** user-provided verbose
`metal_texture_gpu` CTest #122 output reports
`Metal Apple M3 Ultra` and **1/1 PASS (0 failures)**,
2.47 s test / 2.48 s total CTest. All eight
`VtAtlasFragmentProbe` cases in Stage 10BO returned PASS:
nonresident fallback before page publication; pinned coarse
sampling; coarse fallback for the unallocated detail tile;
preserving coarse after a missing disk file; fine physical slice
sampling; pinned coarse elsewhere; correct fallback after
LRU eviction; and new replacement bytes from the reused
physical slot. Each 4×4 GPU RGBA8 render-target readback had
a **1 LSB/channel** tolerance. All direct atlas/page-table blit
readbacks also passed with exact **0 LSB** tolerance.

**Timings in the actual test log:** `disk VT
shader_lookup_setup_ms=251.017` (shader/pipeline setup,
not GPU lookup bandwidth). The eight asynchronous
fragment readbacks showed GPU command-buffer timings
of roughly **0.004–0.020 ms**. The disk-worker through
Metal publication stage (including the newly added
shader draws and repeated callback waits) was
`701.805 ms`; the complete fixture reported
`1301.235 ms`, with `1232.526 ms` after device
initialization. Fragment GPU timing is not an end-to-end
per-frame scene benchmark.

**Conclusion:** this is the first real hardware acceptance of
the production `gfx::kVirtualTextureMSL` page-table
lookup over a **disk-backed, changing physical page atlas**,
including LRU-reused slices. It does **not** verify experimental
per-command GPU-authored ICB Shader Graph binding, all sampler
address modes, broad asset sizes, or GL/Vulkan/mobile parity.
No fixes were required for the observed PASS.


### Stage 10BQ — experimental Metal GPU-encoded ICB graph-source ABI pixel test (source only)

Added a **separate process, explicitly opt-in** macOS native
`tests/render/metal_icb_graph_gpu.mm` GPU acceptance fixture for
the existing Metal `gfx::Device` GPU-encoded indexed indirect command
buffer (ICB), `prepareGpuEncodedIndexedIcbWithPbrMaterials` and
`executeGpuEncodedIndexedIcb`. This path is experimental and
may cause driver failures on unsupported hardware; it is **NOT**
registered during normal source CTest or the already-green
`--metal` acceptance.

The fixture uses the production Metal backend and explicit flags:
`AIVERSE_EXPERIMENTAL_METAL_ICB=1`,
`AIVERSE_GPU_ENCODED_ICB=1`,
`AIVERSE_EXPERIMENTAL_ICB_PER_COMMAND_PBR=1`.
CMake `AIVERSE_TEXTURE_METAL_ICB_GPU_TESTS=ON` registers
`metal_texture_icb_graph_gpu` (default **OFF**). CTest supplies
these opt-in flags via the test environment.

Two GPU-authored indexed commands render the left and right
halves of a 4×4 RGBA8 render target. Both commands share one
shader/pipeline and geometry buffers, but have different
per-command `GpuIcbPbrMaterial` descriptors. Those contain
distinct `texture(1)` built-in PBR colors,
distinct Shader Graph source `texture(13)` colors,
and distinct packed per-command U2 factor vectors.
A minimal MSL fixture declares the same Metal
argument-buffer `slot*2` resource layout and graph-source
count marker used by the production Shader Graph ICB ABI.
It deliberately does **not** claim to execute the entire
model rendering or graph compiler.

Acceptance requires:
- Metal device reports `supportsGpuEncodedIcb()`;
  no skip / implicit fallback when unavailable;
- all source textures, uint32 index and vertex buffers,
  GPU storage command buffer, shader/pipeline allocated;
- `prepareGpuEncodedIndexedIcbWithPbrMaterials` returns a
  **nonzero GPU command token**, and
  `executeGpuEncodedIndexedIcb(token)` succeeds;
- actual Metal RGBA8 output readback from the GPU matches
  **distinct left and right pixels** within 1 LSB/channel.
  A command that accidentally inherits the first material
  for both halves must fail the test.
- test process returns nonzero on unsupported GPU, refused
  GPU encode/execute, GPU fault, callback timeout or
  pixel mismatch.

This hardware test uses **no CPU/ordinary draw fallback** to
manufacture a PASS. It does **not** yet validate the full
`ShaderGraph` authoring/compiler-generated scene materials,
per-command virtual texture mode, all ICB shader permutations,
or actual in-game batching performance.

Source commits:
`facd011f45efd11a6883b615003435c204c83300` (new opt-in
native MSL fixture),
`6d9876960432d2db472e9bce704de014d9eef261`
(default-OFF CMake registration / isolated CTest with env flags),
`e577a2e696b24c213d4c9e480d2a5fc0e917722c`
(include correction).
**Source only: CMake/Objective-C++ compilation and actual
Metal GPU ICB execution have NOT RUN on the Mac Studio.**
Previous passing `metal_texture_gpu` is a different, safer
test and is not evidence of this ICB path.

### Stage 10BR — four Shader Graph source slots in GPU ICB acceptance (source only)

Source commit `50969517f9fdd4a15b38103c378232459520d2a5` extends
`tests/render/metal_icb_graph_gpu.mm` to run two independently
checked native GPU-ICB cases within the opt-in fixture:

1. One Shader Graph fragment source texture at slot 13
   (existing Stage 10BQ acceptance layout).
2. Four independent graph sources at slots 13, 14, 15, 16.
   The corresponding Metal argument-buffer texture resource IDs
   are 26, 28, 30, 32. Slot 16 uses the same constexpr
   repeat/linear sampler contract as production `ModelShaders.h`,
   because dynamic fragment sampler binding stops at slot 15.

Both cases use two real GPU-encoded indexed commands with
different per-command source textures and U2 factors. The four-source
case expects left RGBA8 `{100,20,180,255}` and right
`{110,160,30,255}`; the one-source case retains its previous
`{90,0,160,255}` / `{100,140,0,255}` oracle. Every pixel
must match within one LSB per channel; allocation, GPU token,
execution or readback failure is a hard test failure.

This extension still tests the native material resource-binding ABI
with a minimal graph-like shader, **not** the full Shader Graph
authoring/compiler and Renderer3D scene. It does not establish
cross-backend parity or real batching performance. Experimental
feature flags stay OFF by default.

**Verification:** Source committed, but native macOS build,
Metal shader compilation, execution and pixel parity have **NOT RUN**
for Stage 10BR. Earlier Stage 10BP VT shader PASS is unrelated.

In the **isolated** Mac Studio worktree, not the primary
AIVERSE checkout (which has an unrelated unfinished rebase), use:

```bash
cd /Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819
git fetch origin feature/texture-rhi-unification
git pull --ff-only origin feature/texture-rhi-unification
# Do not modify the already-green build/texture-rhi CMake cache.
cmake -S . -B build/texture-icb-gpu \
  -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_TEXTURE_METAL_GPU_TESTS=OFF \
  -DAIVERSE_TEXTURE_METAL_ICB_GPU_TESTS=ON
cmake --build build/texture-icb-gpu \
  --target aiverse_metal_texture_icb_graph_gpu_test --parallel 4
ctest --test-dir build/texture-icb-gpu \
  --verbose --output-on-failure --no-tests=error \
  -R '^metal_texture_icb_graph_gpu$'
```

CTest scopes experimental environment flags to this test process.
The fixture fails closed if the device cannot execute real GPU-encoded ICB.
Mac drivers with previous AGX ICB instability may reject or abort it;
report the actual first compiler/driver/pixel failure, not a
substituted CPU result.

On native success: record both 1-source and 4-source GPU pixel PASS
logs and build commit, then implement a separate full production
Shader Graph material/compiler scene acceptance. On failure: fix
the first confirmed root cause and create another handoff entry.
Keep <https://github.com/chromealex/AIVERSE/pull/21> **Draft**.

### Stage 10BS — ObjC++ compile correction and stale-binary test safeguard

On the Mac Studio's initial Stage 10BR attempt, native Clang
reported `tests/render/metal_icb_graph_gpu.mm:237`:
`assigning to 'const char *' from incompatible type 'std::string'`.
The fixture mistakenly assigned a mutable `std::string` to
`gfx::ShaderDesc::msl`, whose public contract is `const char*`.

**Fixed in** `3243fc67157187e087c68048e388dfae15bed05b`:
`shDesc.msl=shaderSource.c_str();`. The local `shaderSource`
outlives synchronous `Device::createShader(shDesc)`, and
the Metal backend copies it to its shader source field.
The fixture now also prints `graph sources=1` and
`graph sources=4` on successful per-case GPU pixel comparison.

**Why the original CTest PASS is not Stage 10BR validation:**
the build failed, but CTest was invoked separately and ran a
previously present executable. The resulting **1/1 PASS**
with one generic Shader Graph pixel message is evidence for
that older binary only. It does not test the new four-source case.

For a **fail-fast** rerun on the isolated Mac Studio worktree:

```bash
set -euo pipefail
cd /Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819
git fetch origin feature/texture-rhi-unification
git pull --ff-only origin feature/texture-rhi-unification
cmake --build build/texture-icb-gpu \
  --target aiverse_metal_texture_icb_graph_gpu_test --parallel 4
ctest --test-dir build/texture-icb-gpu \
  --verbose --output-on-failure --no-tests=error \
  -R '^metal_texture_icb_graph_gpu$'
```

`set -e` prevents running CTest if Clang fails again.
A valid Stage 10BR hardware PASS must include **both**:
`PASS: GPU ICB graph sources=1, per-command textures/U2 pixel parity`
and
`PASS: GPU ICB graph sources=4, per-command textures/U2 pixel parity`,
plus 1/1 CTest PASS from the same successful build.
If Metal shader compilation, execution or image comparison fails,
record the first real native error before continuing.
**This corrected source has not yet been built or executed
in this tool environment.** Keep PR #21 Draft.

### Stage 10BT — Apple M3 Ultra four-source GPU-ICB ABI PASS (observed)

The user supplied the native Mac Studio `ctest --verbose` result
after pulling Stage 10BS `3243fc67157187e087c68048e388dfae15bed05b`.
`metal_texture_icb_graph_gpu`: **1/1 PASS, 0 failures**,
CTest test duration **1.06 s**, full CTest real elapsed **1.07 s**.
Both separate native Metal Apple M3 Ultra cases completed:

- `graph sources=1`: production GPU-encoded ICB submitted
  two material commands; independent Shader Graph source/U2
  **RGBA8 pixel parity PASS**. Fixture operation **240.116 ms**.
- `graph sources=4`: same real GPU-encoded ICB path, distinct
  per-command graph textures 13–16 / packed U2; **RGBA8 pixel
  parity PASS**. Fixture operation **151.826 ms**.

The test did **not** use CPU fallback. These reported fixture
intervals are not isolated GPU draw time or gameplay FPS.
The output showed `[metal-assets] libraries=0 sourceMisses=1
pipelines=2 pipelineFailures=0 archive=0 strict=0` for each run;
no actual Metal shader compilation errors were reported.

**Proof boundary:** Both layouts of the low-level per-command
Metal argument-buffer / fragment resource-binding ABI are now
hardware-verified. This is not yet a full production
`render::graph::compile` + `Renderer3D` scene material integration
test, nor Vulkan/GL/mobile parity, source/VT filtering seam coverage,
large-scene stress, GPU driver robustness or performance evidence.
Keep experimental ICB flags OFF by default and PR #21 **Draft**.
Next: authored Shader Graph compile plus production Renderer3D
execution on native hardware, in another opt-in gate.


### Stage 10BU — production Renderer3D + compiled four-source Shader Graph GPU-ICB scene fixture (source only)

Unlike the Stage 10BQ/10BR minimal MSL argument-buffer probes, the
new default-OFF macOS CTest **compiles an authored JSON material graph**
using the production `render::graph::compile`, allocates eight
real model image textures, and renders two differently textured opaque
model primitives with the **real `Renderer3D`**.
All four graph source texture nodes feed the material output, including
tex16; each primitive has different source image handles and different
material base-colour U2 factors. The fixture uses normal
`render::uploadModel`, native `Renderer3D::beginScene` /
`drawModel` / `render`, `gfx::Device::captureNextFrame`, and
`gpuModelDrawStats()`. It is not a handwritten backend MSL shader.

The test first captures ordinary renderer output, then enables the
existing opt-in Metal GPU-encoded ICB path. It requires the actual
production Renderer3D counters `shaderGraphIcbSubmissions`,
`mixedGraphSourceSubmissions`, `perCommandPbrSubmissions`
and at least two GPU commands with no failed executes.
It also requires a visibly different left and right material region
in **both** outputs, and all full-frame captured RGBA8 channels
matching direct output within 2 LSB. Blank scene, identical material
for both commands, CPU fallback or stale images must fail.

**Source changes:** `tests/render/metal_renderer_graph_icb_gpu.cpp`
(new standalone production integration executable),
`CMakeLists.txt` (opt-in `AIVERSE_TEXTURE_METAL_RENDERER_GRAPH_GPU_TESTS=OFF`
by default; isolated `metal_renderer_graph_icb_gpu` CTest).
Only this test receives the experimental GPU/Shader Graph
environment flags. The previous low-level `metal_texture_icb_graph_gpu`
test remains separately gated and unchanged.

**Status:** source committed, **NOT yet built, compiled, run or
hardware pixel-validated on Mac Studio**. The Stage 10BT 1/4-source
low-level ICB PASS is a different test. This fixture may uncover
real Renderer3D shader compiler, GPU grouping, camera/capture or
resource-slot integration bugs. Keep the experimental flags OFF in
production and PR #21 Draft.

**One-shot fail-fast commands in the isolated Mac Studio texture worktree:**

```bash
set -euo pipefail
cd /Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819
git fetch origin feature/texture-rhi-unification
git pull --ff-only origin feature/texture-rhi-unification
cmake -S . -B build/texture-renderer-graph-icb \
  -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_TEXTURE_METAL_GPU_TESTS=OFF \
  -DAIVERSE_TEXTURE_METAL_ICB_GPU_TESTS=OFF \
  -DAIVERSE_TEXTURE_METAL_RENDERER_GRAPH_GPU_TESTS=ON
cmake --build build/texture-renderer-graph-icb \
  --target aiverse_metal_renderer_graph_icb_gpu_test --parallel 4
ctest --test-dir build/texture-renderer-graph-icb \
  --verbose --output-on-failure --no-tests=error \
  -R '^metal_renderer_graph_icb_gpu$'
```

A valid PASS must print the explicit
`PASS: production Renderer3D compiled Shader Graph 4-source mixed Metal GPU ICB pixels`
marker and an actual 1/1 CTest PASS from the same freshly built target.
A build failure **must stop** before CTest; do not report a stale
executable as verified. Capture the first native compiler/MSL, image
mismatch, group counter or driver diagnostic and fix that root cause.
Do not run this high-risk opt-in process as part of the existing
green default texture suite.

### Stage 10BV — production Shader Graph + Renderer3D mixed GPU ICB PASS on Apple M3 Ultra (2026-11-19)

**Real user-supplied Mac Studio test evidence for the source added in Stage 10BU**:
the native target `aiverse_metal_renderer_graph_icb_gpu_test` completed its build and
`metal_renderer_graph_icb_gpu` **PASSED 1/1 CTest, 0 failed** in **3.73 s**
(total CTest real elapsed: **3.74 s**). Unlike Stage 10BT's low-level minimal
MSL ABI test, this fixture used the actual authored JSON material graph compiled by
`render::graph::compile`, the production `Renderer3D`, its graph injection and
model resource bindings, and a live Metal GPU-encoded ICB draw path.

Exact observed diagnostics:

```text
[metal-assets] libraries=0 sourceMisses=11 pipelines=17 pipelineFailures=0 archive=0 strict=0
GPU ICB graph stats: eligible=2 candidates=1 prepared=1 submitted=1 mixed=1 gpu_commands=2 rejected=0 errors=0
Renderer3D Shader Graph regions: left=(196.0,84.0,94.0) right=(87.0,196.0,122.0) delta=249.0
Renderer3D Shader Graph regions: left=(196.0,84.0,94.0) right=(87.0,196.0,122.0) delta=249.0
Renderer3D graph CPU/ICB pixels: bad_channels=0 max_delta=0
PASS: production Renderer3D compiled Shader Graph 4-source mixed Metal GPU ICB pixels
1/1 Test #122: metal_renderer_graph_icb_gpu ..... Passed 3.73 sec
```

**What this proves:** both the direct and ICB render paths displayed two distinct
material regions (the region delta was nonzero in each), the GPU ICB path submitted
one heterogeneous Shader Graph batch with two GPU commands, no rejects/execute errors,
and the **entire captured RGBA8 image matched byte-for-byte** (`bad_channels=0`,
`max_delta=0`; stronger than the test's 2-LSB tolerance).

**Limitations:** This covers the fixture's rigid, opaque, fragment-only
four-texture Shader Graph permutation on **Apple M3 Ultra / Metal**.
This CTest's 3.73 s duration is not a GPU frame benchmark, nor evidence
that ICB is faster than instancing/CPU MDI. The test does **not** exercise
graph + virtual-texture atlas/page-table sampling together, vertex graph
displacement, alpha blend/masking, other permutations or content sizes,
mobile/WebGL, or Vulkan/GL. A single passing hardware fixture is not
driver/stress certification. Experimental ICB flags and
`AIVERSE_TEXTURE_METAL_RENDERER_GRAPH_GPU_TESTS` stay **OFF by default**;
do not merge Draft PR #21 until the broader acceptance matrix passes.

**Next stage:** test graph + VT simultaneously on real Metal:
disk-backed atlas/page-table with a graph `sampleVirtualTexture`
node and independent 1–4 source textures, distinct archive layers/
page-table identities per command, proper fallback and LRU changes.
Require GPU ICB submissions, direct rendering parity and actual
GPU fragment readbacks. Then separately validate GL/Vulkan/mobile
non-ICB fallbacks and profile larger scenes with representative workloads.


### Stage 10BW — two independent disk-backed VT domains + compiled Shader Graph / Metal GPU ICB (source only)

New opt-in **macOS production integration** fixture
`tests/render/metal_renderer_graph_vt_icb_gpu.cpp` starts from the
previous Stage 10BU **actually verified** Renderer3D/Shader Graph test.
It compiles a real version-1 **material JSON graph** through
`render::graph::compile`, with four connected ordinary texture
samples plus a connected `sampleVirtualTexture` node, runs the
production Renderer3D shader injection/lighting path, and retains
its two distinct textured model primitives. The compiled graph
uses the production page-table sampler from
`engine/gfx/VirtualTextureSampling.h`; the test does not duplicate
the VT shader lookup or invent a custom MSL backend.

The test creates two **distinct, temporary, trusted on-disk raw
RGBA8 page archives**, each owned by its own production
`gfx::VirtualTextureFileStreamer`. Different native physical
atlas handles, page-table handles and virtual layers (0 and 1)
are assigned through `Renderer3D::setModelVirtualTextureResolver`.
The left domain uses Repeat-U, the right uses MirrorRepeat-U;
both clamp V. Each atlas has a two-physical-page budget,
four-by-four virtual mip-zero tiles and three mip levels.

For each of **three successive page-residency states**,
the fixture captures both ordinary Renderer3D and opt-in Metal
GPU-encoded ICB output and compares full-screen RGBA8 pixels:
1. Coarse page (mip 2) published and pinned from disk.
2. Fine page (mip 0, tile 1,1) loaded to the remaining physical slice.
3. A new mip-zero tile (2,2) published after the fine page's
   physical slot is recycled by LRU. Requires coarse pinned
   page still resident, evicted fine page absent, and nonzero
   eviction counters on **both** archives.

The test requires visibly different left/right material
regions and **more than 64 changed image channels** across
each residency transition. For every matched pair of ordinary
and GPU-ICB captures, differences >2 LSB/channel fail. The
ICB result must have real `shaderGraphIcbSubmissions`,
`mixedGraphSourceSubmissions`, `perCommandVtExecutions`,
`mixedVtExecutions`, at least two encoded commands and no
execution failure. This rules out no-op texture changes,
unverified CPU draw fallback, or both draws accidentally
using one VT atlas. A driver/compiler/readback/worker error
is a hard test failure. Native async worker operations
remain on the existing file streamer; only its render-thread
`pump()` touches the RHI. Temporary page files are deleted
at fixture shutdown.

**Build:** new default-`OFF` flag
`AIVERSE_TEXTURE_METAL_GRAPH_VT_ICB_GPU_TESTS`. When enabled
on macOS with tests, executable
`aiverse_metal_renderer_graph_vt_icb_gpu_test` is registered as
`metal_renderer_graph_vt_icb_gpu`. This is completely separate
from the Stage 10BT low-level ABI and Stage 10BV scene tests
and is **not** included in default or ordinary `--metal` CTests.
Opt-in CTest injects the necessary GPU ICB, mixed PBR,
per-command VT, mixed VT and Shader Graph environment flags.

**Source commits:**
`8946de8a03e65a2eefd1b8d40dbb7270f3ad8124` (new fixture);
`2a4d64c102627aff48f1e7c756260b377f0c9e7f`
(correct fixture helper namespace and explicit cstddef);
`70019a92cf5657c05c3f3e3a749eafcd4061b753`
(separately gated CMake / CTest).
No production RHI, shader, streaming, Renderer3D or game code
was modified.

**Verification boundary:** native macOS C++ compile/link,
MSL shader compilation, hardware GPU ICB execution,
disk page transitions, pixel parity and performance for
this Stage 10BW source are **NOT RUN** in this tool environment.
The Stage 10BV Apple M3 Ultra **1/1 PASS** proves the prior
four-source **non-VT** Shader Graph scene, not Stage 10BW.
This new experimental test may expose an integration defect;
do not claim it has passed based on static API review.

Run explicitly on the **isolated texture worktree** on Mac Studio
(primary `AIVERSE` worktree has unrelated unfinished rebase).
Use a fresh build directory to preserve the last green CMake caches:

```bash
set -euo pipefail
cd /Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819
git fetch origin feature/texture-rhi-unification
git pull --ff-only origin feature/texture-rhi-unification
cmake -S . -B build/texture-graph-vt-icb \
  -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_TEXTURE_METAL_GPU_TESTS=OFF \
  -DAIVERSE_TEXTURE_METAL_ICB_GPU_TESTS=OFF \
  -DAIVERSE_TEXTURE_METAL_RENDERER_GRAPH_GPU_TESTS=OFF \
  -DAIVERSE_TEXTURE_METAL_GRAPH_VT_ICB_GPU_TESTS=ON
cmake --build build/texture-graph-vt-icb \
  --target aiverse_metal_renderer_graph_vt_icb_gpu_test --parallel 4
ctest --test-dir build/texture-graph-vt-icb \
  --verbose --output-on-failure --no-tests=error \
  -R '^metal_renderer_graph_vt_icb_gpu$'
```

Only a fresh successful compile plus **three** per-state
`PASS: Shader Graph+VT ... Renderer3D CPU/ICB pixels` markers,
nonzero real GPU mixed VT/graph counters, two changed-pixel
transition reports and the final
`PASS: production Renderer3D Shader Graph+disk VT mixed Metal
GPU ICB coarse/fine/LRU pixel parity`
**and** CTest 1/1 PASS count as native verification.
If a compiler/Metal/resource/worker/page-table/pixel check fails,
capture the first concrete error and fix the root cause in
a new code commit and append-only handoff. After success,
extend to nonresident/missing disk page failures, edge-filtering
seams, mixed wrap modes at UV outside [0,1], and independent
Vulkan/GL/mobile non-ICB parity. Do not infer any frame-time
speedup from CTest duration. Keep PR #21 Draft and GPU ICB
flags OFF in production until further validation.

### Stage 10BX — Apple M3 Ultra full Shader Graph + dual disk-backed VT + GPU ICB PASS (2026-11-19)

**Native hardware result supplied by user:** isolated macOS fixture
`metal_renderer_graph_vt_icb_gpu` on **Metal Apple M3 Ultra**
reported **1/1 CTest PASS, 0 failed**, test **4.57 s**,
entire CTest **4.58 s**. Exact test environment included the
experimental GPU-authored, PBR, Shader Graph, per-command VT
and mixed VT flags. The test is deliberately independent
from the already-passing non-VT Shader Graph and low-level ICB
fixtures.

For **each** of pinned coarse, fine detail and LRU recycled
states, real `Renderer3D` logged:
`eligible=2 prepared=1 graph=1 mixedGraph=1 vt=1 mixedVt=1
commands=2 rejected=0 errors=0`.
The production-compiled material Shader Graph sampled four
ordinary source textures **and** the virtual atlas/table from
two independent, file-backed archives and virtual layers.
Both source material regions were nonidentical in each phase.

**Actual device pixel checks**:

| Virtual residency | Direct vs GPU ICB | Max channel delta | Region delta |
|---|---|---:|---:|
| Pinned coarse | bad_channels=0 | 0 | 91.0 |
| Fine detail | bad_channels=0 | 0 | 93.7 |
| LRU physical slot recycled | bad_channels=0 | 0 | 48.5 |

Observed page-transition changes in ordinary render images:
`fine detail changed_channels=1392` and
`LRU recycled changed_channels=2886`.
The fixture confirmed pinned coarse page survival, fine page
residency and replacement after LRU eviction. A false PASS from
ordinary-draw fallback was prevented by nonzero mixed VT/graph
ICB counters. The final native log printed:

```text
PASS: Shader Graph+VT pinned coarse Renderer3D CPU/ICB pixels
PASS: Shader Graph+VT fine detail Renderer3D CPU/ICB pixels
PASS: Shader Graph+VT LRU recycled Renderer3D CPU/ICB pixels
PASS: production Renderer3D Shader Graph+disk VT mixed Metal GPU ICB coarse/fine/LRU pixel parity
1/1 Test #122: metal_renderer_graph_vt_icb_gpu ... Passed 4.57 sec
```

No production source fix was needed for this observed PASS.
**Scope is specific:** one Apple M3 Ultra/Metal, 2 primitives,
4 Shader Graph source textures, two distinct raw RGBA8 disk
VT archives, three residency states, one pair of U-addressing
modes. This is *not* an FPS measurement, broad driver soak test
or a validation of all UV seam/filtering cases. Shader Graph
missing-page behavior, out-of-range UV Repeat/MirrorRepeat
boundaries, alpha/deformation permutations, remaining source
counts and GL/Vulkan/mobile/WebGL acceptance are still open.

**Current policy:** keep opt-in Metal ICB tests and feature flags
OFF in production; PR #21 remains Draft until the broader
backend/compatibility matrix passes. Next high-value validation
is a separate dedicated test for nonresident and intentionally
missing disk pages with coarse/fallback persistence, sample UV
outside [0,1] at seams, and native GPU screenshot/oracle
comparison. Reuse `VirtualTextureFileStreamer` and the existing
canonical shader sampler; do not implement a parallel VT runtime.


### Stage 10BY — missing VT disk page, nonresident fallback and out-of-range wrap oracle (source only)

**New file:** `tests/render/metal_renderer_graph_vt_seam_gpu.cpp` is
a **separate, default-OFF** macOS opt-in test of real
`render::graph::compile` + production `Renderer3D` + two
`VirtualTextureFileStreamer` domains + Metal GPU-encoded ICB.
It reuses the working Stage 10BW synthetic two-material, four-source
Shader Graph fixture and canonical `gfx::kVirtualTextureMSL`.
The test does not modify the production shader, disk worker,
page-table, RHI, gameplay, or mesh API.

**Independent GPU image oracles, not merely CPU/ICB agreement:**

- Without a VT resolver, ordinary Shader Graph sampling provides
  a `tex1` fallback reference. With a live but fully nonresident
  VT page table, both the ordinary renderer and real mixed VT GPU
  ICB must produce **exactly that same** full RGBA8 image. Both
  GPU page table entries for virtual tile (3,1), respective layers
  0 and 1, must remain `{0,0,0,0}` before publication.
- Each archive then publishes and pins a known mip2 coarse page
  from a trusted 4×4 RGBA8 file. The image **must change** from
  the fallback, and the target mip0 table entry must be
  `{0,0,2,255}`.
- A requested, deliberately **absent** mip0 file (tile 2,1)
  must trigger a real asynchronous worker **failure callback**
  with a nonempty diagnostic. Neither GPU atlas can be
  quarantined; the pinned coarse page and table mapping must
  remain. The full ordinary image **must remain identical**
  after the failed lookup; the corresponding GPU ICB frame
  must still agree with the ordinary renderer.
- A known mip0 fine tile (3,1) is then published in the second
  physical slice, with table entry `{1,0,0,255}`. Each material
  sets tiny UV scale (0.0001) and fixed V=0.375, making every
  visible fragment sample one predictable tile. The left material
  has **Repeat-U**; the right has **MirrorRepeat-U**.
  At `U=-0.125` and `U=-0.0005`, Repeat must address tile3
  (fine), while MirrorRepeat addresses tile0 (coarse).
  At `U=1.125` and `U=1.0005`, these roles reverse.
  A pure CPU oracle, based on authored raw page colors and these
  known wrap policies, requires **only the predicted material
  region** to change from the pinned-coarse image. It also
  checks the expected RGB channel **direction**, including
  red increasing and green decreasing in the left fine region
  versus red increasing and blue decreasing in the right fine
  region. The same oracle runs separately on ordinary and
  GPU ICB frame captures.
- In every VT state the real GPU ICB path must expose nonzero
  `shaderGraphIcbSubmissions`, `mixedGraphSourceSubmissions`,
  `perCommandVtExecutions`, `mixedVtExecutions`,
  at least two GPU commands, **zero rejected/execute-failed**
  commands, and full-frame ordinary-vs-GPU-ICB RGBA8 agreement
  within 2 LSB/channel. Missing GPU execution/blank output is
  not accepted as a PASS.

The new CMake option
`AIVERSE_TEXTURE_METAL_GRAPH_VT_SEAM_GPU_TESTS=OFF`
(default) registers the separate process
`metal_renderer_graph_vt_seam_gpu` only on explicit macOS
opt-in; it is not run by ordinary texture or Metal acceptance.
Experimental ICB flags are provided only to this test process.

**Source commits:**
`694d04531ffd5c1e81efe60b499a0ccd8e096d0d` — fixture;
`d95cdc388778a31de5b0f9900863cd28c2e7a737` — opt-in
CMake/CTest registration.

**Evidence and limitations:** the earlier Stage 10BX Apple M3 Ultra
graph+VT coarse/fine/LRU test **PASSED 1/1**, but the new Stage 10BY
compiler, native test, GPU screenshots and seam/missing-page oracle
have **NOT RUN**. This test addresses *selection across wrap
boundaries* for solid-color physical tiles with gutter=0; it
does **not** certify bilinear filtering and gutter continuity
for nonuniform page edges. Hardware result required before claiming
this feature works on the current branch.

**Fail-fast Mac Studio command**, *isolated* texture worktree only:

```bash
set -euo pipefail
cd /Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819
git fetch origin feature/texture-rhi-unification
git pull --ff-only origin feature/texture-rhi-unification
cmake -S . -B build/texture-graph-vt-seam \
  -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_TEXTURE_METAL_GPU_TESTS=OFF \
  -DAIVERSE_TEXTURE_METAL_ICB_GPU_TESTS=OFF \
  -DAIVERSE_TEXTURE_METAL_RENDERER_GRAPH_GPU_TESTS=OFF \
  -DAIVERSE_TEXTURE_METAL_GRAPH_VT_ICB_GPU_TESTS=OFF \
  -DAIVERSE_TEXTURE_METAL_GRAPH_VT_SEAM_GPU_TESTS=ON
cmake --build build/texture-graph-vt-seam \
  --target aiverse_metal_renderer_graph_vt_seam_gpu_test --parallel 4
ctest --test-dir build/texture-graph-vt-seam \
  --verbose --output-on-failure --no-tests=error \
  -R '^metal_renderer_graph_vt_seam_gpu$'
```

A valid PASS must include: exact nonresident-vs-no-VT fallback
equality; observed real missing-file error for BOTH archives;
unchanged coarse frame after that error; coarse/fine GPU
page table mapping; four `PASS: independent wrap oracle`
lines for ordinary **and** four for GPU ICB; the final
`PASS: Renderer3D Shader Graph disk VT missing-page and
Repeat/MirrorRepeat seam GPU ICB oracle`, and a native CTest
1/1 PASS after a successful build. The test duration is not
an isolated GPU draw time or FPS benchmark.

**Outstanding after Stage 10BY:** bilinear/anisotropic filter
edge gutters at actual physical tile seams (nonuniform page
data), other graph texture counts and material permutations,
large-scene memory/performance/stress, cross-platform
Vulkan/GL/WebGL/mobile acceptance. Keep PR #21 Draft and
experimental GPU ICB flags OFF by default pending verification.

### Stage 10BZ — fix native macOS Clang helper-name ambiguity (2026-11-19)

The first real Stage 10BY macOS compilation **FAILED** (4 errors) in
`tests/render/metal_renderer_graph_vt_seam_gpu.cpp`:
Clang reported `reference to 'detail' is ambiguous` at lines
387, 490, 523 and 578. The test's unqualified helper
`detail(int)` collided with the existing namespace
`aiverse::detail` from `engine/core/Fixed.h`, brought into
scope by `using namespace aiverse;`. No test execution
occurred after this failed build.

**Fix:** source commit `8fe7265f2124539a36e388255acd1d9fc5ee3887`
renames only the test helper to `finePageKey(int)`, including
all four uses (direct invocation and function-pointer passing
to the disk page request helper). The key remains
`VirtualPageKey{0,3,1,side}`. No runtime, VT sampling,
GPU ICB, platform, ECS or material behavior is modified.

**Status:** source fix committed, a fresh native macOS
recompile and GPU/CTest acceptance are **NOT YET RUN**.
Previous Stage 10BX PASS is unrelated to this 10BY compiler
failure. Repeat **fail-fast** in the isolated test directory:

```bash
set -euo pipefail
cd /Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819
git fetch origin feature/texture-rhi-unification
git pull --ff-only origin feature/texture-rhi-unification
cmake --build build/texture-graph-vt-seam \
  --target aiverse_metal_renderer_graph_vt_seam_gpu_test --parallel 4
ctest --test-dir build/texture-graph-vt-seam \
  --verbose --output-on-failure --no-tests=error \
  -R '^metal_renderer_graph_vt_seam_gpu$'
```

`set -e` prevents a stale prior test executable from
accidentally running if compilation fails again. Only a new
successful build, the Stage 10BY native expected fallback and
wrap-oracle diagnostics, and 1/1 CTest PASS certify the new gate.

### Stage 10CA — native Metal Shader Graph VT fallback and UV seam oracle PASS (2026-11-19)

**Real user-provided Mac Studio hardware evidence** after the Stage 10BZ
`finePageKey` compilation fix: `metal_renderer_graph_vt_seam_gpu`
**1/1 CTest PASS (0 failed)**, test **5.65 s**, CTest elapsed **5.67 s**.
GPU backend was Metal on the Mac Studio; the fixture uses the actual
production `render::graph::compile`, `Renderer3D`,
`VirtualTextureFileStreamer` and GPU-authored Metal ICB.

**Observed per-frame GPU ICB counters for all seven states:**
`graph=1 mixedGraph=1 vt=1 mixedVt=1 commands=2 rejected=0 errors=0`.
Ordinary and ICB full-screen RGBA8 images in each state compared with
`bad_channels=0 max_delta=0`; the CTest explicitly required
nonzero mixed Shader Graph and VT command execution.

**Independent fallback evidence:**

- **Empty page table:** identical to no-VT `tex1` fallback image,
  `FALLBACK ORACLE ... changed_channels=0`.
- **Pinned coarse published from file:** image changed in **23,304
  channels** relative to nonresident fallback.
- **Missing file, both archives:** two expected disk-worker errors,
  `missing page or incorrect RGBA8 page byte count`; current
  pinned coarse resources remained healthy and their image
  was **identical** to pre-failure (`changed_channels=0`).

**Independent UV/page-selection oracle**, executed on both the
ordinary and Metal ICB captured images:

| Authored U | Expected fine page | Correct region change | Other region |
|---|---|---:|---:|
| -0.125 | Repeat, left | 210 RGB-distance | 0 |
| +1.125 | MirrorRepeat, right | 289 | 0 |
| -0.0005 (seam) | Repeat, left | 210 | 0 |
| +1.0005 (seam) | MirrorRepeat, right | 289 | 0 |

Each UV point produced **two** explicit
`PASS: independent wrap oracle` lines (ordinary and ICB),
**8/8 PASS**. The reported RGB deltas also matched the
independently authored physical page color directions:
left fine `(+104,-101,-5)`, right fine `(+108,+67,-114)`,
and the non-target region stayed `(0,0,0)`.

The fixture concluded:
```text
PASS: Renderer3D Shader Graph disk VT missing-page and Repeat/MirrorRepeat seam GPU ICB oracle
1/1 Test #122: metal_renderer_graph_vt_seam_gpu ... Passed 5.65 sec
```

**Proof boundary:** This verifies expected *page selection* at
negative and out-of-range UVs and fault isolation after a missing
page on **one real Metal device** with solid-color 4x4 pages
and zero gutter. It does **not** test filtering continuity over
nonuniform page borders, bilinear/anisotropic filtering, varying
mips at a seam, broad source-count/material permutations, large
archive stress, Vulkan/GL/WebGL/mobile or production FPS.
The CTest's wall time is not an isolated draw-call GPU timer.
No additional production code change was necessary after PASS.

**Next validation:** use nonuniform, adjacent physical page
edge patterns and immutable gutters. Build an independent
bilinear CPU reference (including Repeat/MirrorRepeat),
test pixel-center offsets across tile boundaries and
mip0→coarse fallback while page residency changes.
Require visual error bounds and GPU ICB/direct parity,
then validate non-Metal backends separately. Keep all
experimental ICB flags default OFF and PR #21 Draft.

### Stage 10CB — guttered VT bilinear continuity fix and independent Metal GPU oracle (source only)

**Production source fix:** `engine/gfx/VirtualTextureSampling.h`,
commit `1af66f86a754958f6bacf84d279b14a163fca831`.

Previously both GLSL and Metal MSL computed physical atlas inset as
`(gutterPixels + 0.5) / physicalPageSize`. When gutters were
present, this stopped the sample footprint at the **center**
of the first/last interior texel and never blended through the
neighbor-authored gutter at a virtual page seam. An image may
look correct with constant-color pages yet exhibit discontinuities
with textured pages. For a positive gutter, map the exact
*boundary* of the logical tile interior instead:
`sampleInset = gutterPixels > 0 ? gutterPixels : 0.5`,
`atlasUv = mix(sampleInset / pageSize,
1 - sampleInset / pageSize, tileUv)`.
At seam coordinate `tileUv=0` this places the hardware linear
filter halfway between the neighboring gutter and first interior
texels; `tileUv=1` gives the analogous last-interior/gutter
blend. **Zero-gutter resources retain their existing 0.5-pixel
clamping behavior**, preserving the prior Stage 10BY/10BX
fixture contracts. Both portable GLSL and native Metal MSL
are updated symmetrically.

**New isolated native GPU acceptance source:**
`tests/render/metal_vt_bilinear_gpu.mm`, commit
`4787302de05704c2c48e7c9fede9a4813b5d2a33`.
The test uses the real `gfx::Device` Metal backend, a real
`VirtualTextureFileStreamer` and its on-disk raw RGBA8
physical pages, their GPU `Texture2DArray` atlas and page
table, and **the literal production `gfx::kVirtualTextureMSL`
sampler**. It does not simulate GPU readbacks or replace
the shader with a hand-coded page lookup.

Two independent test domains use **Repeat** and
**MirrorRepeat** U-addressing and ClampToEdge V.
Each logical source is 12×6 RGBA8 with several independent
channel gradients. Every domain contains two virtual pages
at mip0 and two physical slices; each 8×8 physical page
contains a 6×6 nonuniform interior with an actual **1-texel
gutter** sourced from neighboring *logical* texels across
both the internal page seam and outer Repeat/MirrorRepeat
edges. Pages are written to temporary disk archives,
loaded by real workers and published on Metal only after
valid callbacks and GPU page-table publication.

The CPU expected-value oracle is deliberately **independent
of the GPU implementation**: it bilinearly interpolates
the original full-resolution logical texel grid at
`addressedUV * textureExtent - 0.5`, applying separate
integer Repeat/MirrorRepeat/Clamp addressing to both
horizontal and vertical source texels. It never reads a
physical atlas, GPU result, page table, physical slice or
`inset` from the shader. Each of **136 native GPU
fragment probes** uses 17 U coordinates × 4 V coordinates ×
2 addressing modes, including internal page seams,
texture wrap boundaries, negative and greater-than-one U,
and V=0 and V=1. Every rendered 4×4 pixel probe must be
uniform and within **2 LSB/channel** of that CPU oracle.
The test also requires a sensitive nonuniform pattern:
the old `gutter+0.5` mapping differs from the expected
midpoint by at least four 8-bit levels and therefore
cannot accidentally pass this fixture.

**CMake opt-in:** commit
`f2778b19f29817b94c48e73ed65c6465fbf86a27`
adds `AIVERSE_TEXTURE_METAL_VT_BILINEAR_GPU_TESTS`
(default **OFF**) to build only
`aiverse_metal_vt_bilinear_gpu_test` and register
`metal_vt_bilinear_gpu` on macOS when explicitly enabled.
This precision shader test is separate from the already
hardware-proven `Renderer3D` + mixed graph/VT ICB suites.
It validates the *shared canonical sampler*, which is the
one included into production Shader Graph permutations.
It does not itself execute GPU ICB commands.

**Verification done so far:** GitHub-source/registration review;
a separate pure numerical model compared full logical-texel
bilinear reference against the proposed shader mapping at
the U/V test positions. The expected new mapping agreed
(in that mathematical model); the old half-texel inset
showed material discrepancies. This is NOT native
driver proof. **Native macOS compile/link, Metal GPU
shader compile, disk worker test, readback, pixel oracle,
and CTest have not been run** for this stage.

Run fail-fast in the isolated Mac Studio texture worktree
(primary AIVERSE remains untouched):

```bash
set -euo pipefail
cd /Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819
git fetch origin feature/texture-rhi-unification
git pull --ff-only origin feature/texture-rhi-unification
cmake -S . -B build/texture-vt-bilinear-gpu \
  -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_TEXTURE_METAL_GPU_TESTS=OFF \
  -DAIVERSE_TEXTURE_METAL_ICB_GPU_TESTS=OFF \
  -DAIVERSE_TEXTURE_METAL_RENDERER_GRAPH_GPU_TESTS=OFF \
  -DAIVERSE_TEXTURE_METAL_GRAPH_VT_ICB_GPU_TESTS=OFF \
  -DAIVERSE_TEXTURE_METAL_GRAPH_VT_SEAM_GPU_TESTS=OFF \
  -DAIVERSE_TEXTURE_METAL_VT_BILINEAR_GPU_TESTS=ON
cmake --build build/texture-vt-bilinear-gpu \
  --target aiverse_metal_vt_bilinear_gpu_test --parallel 4
ctest --test-dir build/texture-vt-bilinear-gpu \
  --verbose --output-on-failure --no-tests=error \
  -R '^metal_vt_bilinear_gpu$'
```

**Acceptance:** require a freshly successful build, actual
`Metal canonical VT bilinear:` native-device banner,
`VT bilinear independent CPU oracle: samples=136`,
nonzero seam samples, max channel deviation ≤2,
`PASS: native Metal canonical VT bilinear nonuniform gutter
seam and Repeat/MirrorRepeat CPU pixel oracle` and CTest
1/1 PASS. `set -e` prevents mistakenly running a stale
binary after compilation failure. On first native error,
fix it in a source commit and append a new handoff.

**Scope/remaining:** this stage verifies the physical gutter
bilinear math against an independent logical-texture oracle.
It does **not** validate texture anisotropy, trilinear mip
boundary transitions with dissimilar parent/detail page
residency, alpha/transparency, a full `Renderer3D`
mixed GPU-ICB capture with nonuniform pages, Vulkan/GL/
WebGL/mobile hardware parity, or GPU performance/soak.
Run the existing Stage 10BX/10BY non-gutter GPU acceptance
again after a native Stage 10CB PASS as a regression gate.
Keep PR #21 Draft and all experimental GPU ICB flags
OFF by default.

### Stage 10CC — native nonuniform gutter Metal bilinear PASS (2026-11-19)

**User-supplied real Apple M3 Ultra native CTest result** after
Stage 10CB's shared Metal/GLSL sampling fix
`1af66f86a754958f6bacf84d279b14a163fca831`
and independent Metal fixture
`4787302de05704c2c48e7c9fede9a4813b5d2a33`:

```text
[metal-assets] libraries=0 sourceMisses=1 pipelines=1 pipelineFailures=0 archive=0 strict=0
Metal canonical VT bilinear: Metal Apple M3 Ultra, page=8 interior=6 gutter=1
VT bilinear independent CPU oracle: samples=136 seam_samples=120 max_delta=1
PASS: native Metal canonical VT bilinear nonuniform gutter seam and Repeat/MirrorRepeat CPU pixel oracle
1/1 Test #122: metal_vt_bilinear_gpu ............ Passed 6.07 sec
100% tests passed, 0 tests failed out of 1
Total Test time (real) = 6.08 sec
```

This establishes **actual GPU fragment pixel agreement** against
the independent logical-texel CPU bilinear oracle for 136 real
sampling coordinates, including 120 seam-adjacent samples.
The maximum RGBA8 channel error was **1/255**, within
the fixture's required 2-LSB bound; the Metal shader pipeline
reported **0 failures**. Source pages were nonuniform 8×8
physical RGBA8 tiles with 6×6 interiors and **one real
neighbor-matched gutter texel** on every edge; two independent
disk-backed streamer domains exercised Repeat and MirrorRepeat.
The reported 6.07 s is CTest wall time, **not** GPU frame time.

**This is not an indirect-command benchmark:** the test
validates the production canonical VT fragment sampler on
Metal, not a complete Renderer3D/Shader Graph GPU ICB scene.
The GLSL symmetric source change exists but has not received
a matching Vulkan/GL or mobile hardware test yet.
No production source changes were needed after this PASS.

**Mandatory next validation gates:**

1. Rebuild and rerun previously passed **zero-gutter**
   production graph/VT GPU ICB fixtures in the same isolated
   Mac Studio texture worktree after pulling this shader fix.
   The tests exercise 10BX's pinned coarse/fine/LRU lifecycle
   and 10BY's nonresident/missing-page and UV wrap boundaries.
   Do **not** use old binary PASS without recompiling.
2. Add a separate opt-in native full Renderer3D mixed graph+VT
   GPU ICB scene using **nonuniform guttered** source pages
   and an independent expected-pixel oracle. The 10CC
   low-level bilinear proof and 10BX/10BY ICB parity are
   complementary, not equivalent.
3. Expand to trilinear/mip transitions and anisotropy,
   Vulkan/GL/WebGL/mobile, driver soak and real GPU timing
   before considering enabling the experimental feature.

Fail-fast commands for the known **existing isolated build
directories** (no new CMake configure required if their
options/caches remain unchanged):

```bash
set -euo pipefail
cd /Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819
git fetch origin feature/texture-rhi-unification
git pull --ff-only origin feature/texture-rhi-unification

cmake --build build/texture-graph-vt-icb \
  --target aiverse_metal_renderer_graph_vt_icb_gpu_test --parallel 4
ctest --test-dir build/texture-graph-vt-icb \
  --verbose --output-on-failure --no-tests=error \
  -R '^metal_renderer_graph_vt_icb_gpu$'

cmake --build build/texture-graph-vt-seam \
  --target aiverse_metal_renderer_graph_vt_seam_gpu_test --parallel 4
ctest --test-dir build/texture-graph-vt-seam \
  --verbose --output-on-failure --no-tests=error \
  -R '^metal_renderer_graph_vt_seam_gpu$'
```

Until these **new post-fix runs** are observed, their
earlier Stage 10BX/10BY PASS records remain valid evidence
of the pre-fix source but do not prove post-fix regression
freedom. Keep `feature/texture-rhi-unification` PR #21
**Draft** and GPU ICB CMake flags **OFF** by default.

### Stage 10CD — nonuniform guttered production Renderer3D ICB + one-shot texture acceptance (source only)

This stage closes the **source-level** gap left after the observed
Stage 10CC real M3 Ultra 136-point canonical Metal VT bilinear PASS:
a full production scene with nonuniform physical gutters now exercises
the actual compiled Shader Graph, `Renderer3D`, per-material
`VirtualTextureFileStreamer` and mixed per-command GPU-authored Metal
ICB. The existing Stage 10BX/10BY tests still verify coarse/fine/LRU,
nonresident fallback and UV selection with zero gutters, separately.

**New production scene acceptance test source commit:**
`1c4a3be68f829775c5af4894c1e15f1d265a08e0`
adds `tests/render/metal_renderer_graph_vt_gutter_gpu.cpp`.
It reuses the proven four-source compiled JSON material graph
with connected `sampleVirtualTexture`, two opaque rigid model
materials and distinct GPU source images. Two independent
file-backed VT archives each publish mip0 tile0 and tile1 into
distinct native physical atlas and page-table handles
with separate virtual layers 0 and 1. Each physical tile
is an 8×8 RGBA8 image with a 6×6 **nonuniform interior and
genuine neighboring logical texels** as 1-pixel gutters
on all edges. This is not a manually created MSL renderer;
the production Renderer3D MSL graph pipeline is used.
The fixture uses the same fixed Mac camera and full-frame
native `Device::captureNextFrame` readback as previous
real-device Shader Graph ICB scene tests.

For five U positions `0.40, 0.48, 0.50, 0.52, 0.60`
at fixed V=0.40, the fixture renders the same scene through
both conventional model submission and explicitly enabled
heterogeneous Metal GPU ICB. It requires:
- Both page table entries published from actual disk page
  files with expected physical slice indices 0/1.
- Nonzero real `shaderGraphIcbSubmissions`,
  `mixedGraphSourceSubmissions`,
  `perCommandVtExecutions`,
  `mixedVtExecutions` and at least two GPU encoded draws,
  with zero rejected or execute-failed commands.
- Two chromatically different material ROI regions in every
  capture; full direct vs GPU ICB image agreement within
  2 RGBA8 levels/channel for every U position.
- A second independent CPU **logical-source-texture** oracle
  for both material ROIs and both paths. It evaluates original
  logical source texel bilinear values at the authored U/V
  positions, without consulting atlas slot/page table or
  sampler inset. Because actual Renderer3D lighting and
  output transfer are nonlinear, the oracle calibrates
  each channel from the `U=0.40` and `U=0.60` GPU
  ROI endpoints, then requires the intervening sample
  ROIs to follow the independently predicted logical
  texture interpolation to within **3 displayed RGBA8
  levels**. It also checks channel direction and minimum
  visible scene change, and tests both direct and GPU ICB
  independently. The `U=0.50` midpoint is sensitive
  to the old half-texel inset seam defect.
The test is an independent scene-level metamorphic oracle,
not a full absolute RGB shading reference; the earlier
10CC Metal fragment test already validates absolute
RGBA8 against the original logical texels.

**CMake source commit:**
`ed9a707e0f48102bd60c1b0640fd408cfb384325`
adds `AIVERSE_TEXTURE_METAL_GRAPH_VT_GUTTER_GPU_TESTS`
(**OFF by default**) to register the standalone process
`metal_renderer_graph_vt_gutter_gpu` only on explicit
native macOS opt-in. Other Metal GPU and Shader Graph
CMake test processes retain their separate opt-in flags.

**One-shot test script commits:**
`04e9612189b9e10e4a218bce326af405bc8ddd37`
creates `tools/texture-rhi-final-verify.sh`;
`86254fca82f0866005fbd2508872f7ca05ede7d4`
includes Python virtual-texture-pack and optional
shader-feedback compiler tests when registered.

The script configures a **safe** build with all experimental
ICB options OFF, compiles/runs **21 portable texture, VT and
Shader Graph CTests**, plus optional on-host registered
VT pack and shader syntax checks. With `--full-engine`,
it additionally builds and runs the full default AIVERSE
CTest suite. In a separate **explicit opt-in** GPU build,
it compiles all **seven** independent native Metal GPU
test targets before running any GPU test. It then runs
them in dedicated processes **sequentially** in this order:
`metal_texture_gpu`, `metal_texture_icb_graph_gpu`,
`metal_renderer_graph_icb_gpu`,
`metal_renderer_graph_vt_icb_gpu`,
`metal_renderer_graph_vt_seam_gpu`,
`metal_vt_bilinear_gpu`,
`metal_renderer_graph_vt_gutter_gpu`.
An error at any build/CTest step stops the run; old
executables cannot be counted as freshly passing.
Critical GPU tests additionally require exact success
markers (beyond CTest's return code). Logs and final
commit SHA are saved to `build/texture-final-logs`;
`acceptance.txt` is emitted **only after everything passes**.
The script is Mac-only and requires the isolated worktree
to be on `feature/texture-rhi-unification`.

**One complete user invocation after updating the isolated
worktree** (the script itself performs every configure,
compile, portable test and native GPU test in one go):

```bash
set -euo pipefail
cd /Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819
git fetch origin feature/texture-rhi-unification
git pull --ff-only origin feature/texture-rhi-unification
bash tools/texture-rhi-final-verify.sh --jobs 4
# Add --full-engine to include every nonexperimental AIVERSE test as well.
```

**Native status:** The Stage 10CC canonical bilinear
GPU test previously **passed** with 136 samples,
120 at seams and max deviation 1. The earlier Stage
10BX and 10BY mixed GPU ICB tests also passed, but
those logs **predate** the positive-gutter sampler fix.
**Stage 10CD full production gutter scene code and the
one-shot suite have not been compiled or executed
on Mac hardware in this agent environment.** Static
source/API review does not count as a green test.
The actual unified run is therefore still a required
acceptance gate. Do not merge PR #21 until its native
logs are observed and reviewed.

**Still outside proven scope after this test:** validated
GPU hardware on Vulkan/GL/mobile/WebGL, trilinear mip
transitions and anisotropic filtering for nonuniform
pages, heavy memory/streaming stress and actual
GPU-frame-performance comparisons. These require
their own backend/device capabilities and performance
measurements; they are not justified by a single
Metal acceptance. Metal ICB remains experimental and
**OFF by default** in production.

### Stage 10CE — one-shot shell runner integrity repair

While inspecting the pushed `tools/texture-rhi-final-verify.sh`
after Stage 10CD, the first optional-test editing pass
had accidentally introduced duplicated runner blocks and an
unterminated regex. This corruption was caught by **remote
GitHub source inspection before any user executed the script**.
It is not a C++/Metal defect and does not alter the fixtures.

**Fixed source commit:**
`6a6bb9f2a18cb494a93d44eb232907c0df2ad8fe`.
The runner was restored from its original clean commit
`04e9612189b9e10e4a218bce326af405bc8ddd37`
and the optional VT pack/feedback shader selector was
reinserted as one complete bounded block. Post-write
GitHub inspection confirms 142 lines and **exactly one**
safe/full-engine block, native GPU build block and
seven-test serial execution loop. CTest must see a
successful fresh compile before any GPU marker can
be accepted; the isolated final CMake cache remains
independent from previous green test directories.

**Source/API verification only:** the script has been
checked for single-instance blocks and expected variable
expansions after the correction. It has **not** been
executed on the user's Mac. Native code compilation,
GPU command execution and all one-shot test results
remain pending. Run with `bash -n` first as an extra
local syntax preflight, then invoke the whole suite:

```bash
set -euo pipefail
cd /Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819
git fetch origin feature/texture-rhi-unification
git pull --ff-only origin feature/texture-rhi-unification
bash -n tools/texture-rhi-final-verify.sh
bash tools/texture-rhi-final-verify.sh --jobs 4
# For the entire engine's nonexperimental tests as well:
# bash tools/texture-rhi-final-verify.sh --jobs 4 --full-engine
```

The success record `build/texture-final-logs/acceptance.txt`
is trustworthy only when the script exits zero and all
required CTest logs contain real native GPU execution
markers, not merely the previously seen test binaries.

### Stage 10CF — detached HEAD isolated worktree accepted safely

**Observed user Mac Studio result:** the one-shot runner stopped before
CMake or CTest with:
`FAIL: run only in the feature/texture-rhi-unification isolated worktree`.
The user had successfully fetched and fast-forwarded to
`67cd9a4214309a3614f492efb79e980311d553a4` but the
existing isolated validation worktree was in **detached HEAD**,
as previously used by concurrent AIVERSE agent workflows.
`git branch --show-current` is blank in detached HEAD.
This was a **runner preflight false rejection**, not
a texture/shader/compiler or GPU test failure.

**Source fix commit:** `1fd7dcba4b16cbf72c64d0f6c81c5be9415c23ce`
updates only `tools/texture-rhi-final-verify.sh`.
The script now obtains `HEAD` and the fetched
`refs/remotes/origin/feature/texture-rhi-unification`
commit IDs and **requires exact equality** before doing work.
A named local branch must still be
`feature/texture-rhi-unification`; detached HEAD
is permitted only at this same fetched source SHA.
Missing remote-tracking ref, stale HEAD and other local
branches fail closed, rather than trusting detached
HEAD indiscriminately. The shell reports the accepted
source SHA and detached/branch state explicitly.

**Status:** the worktree failure is explained and a
source fix was pushed. The **corrected one-shot runner
has not yet been executed on the user's Mac**; no
21-test or seven-GPU-test PASS is claimed here.

Run the same complete one-shot command without changing
or creating local branches:

```bash
set -euo pipefail
cd /Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819
git fetch origin feature/texture-rhi-unification
git pull --ff-only origin feature/texture-rhi-unification
bash -n tools/texture-rhi-final-verify.sh
bash tools/texture-rhi-final-verify.sh --jobs 4
```

To include the entire nonexperimental engine suite as
well as the texture gates, add `--full-engine`.
Review the first error if anything new fails and keep
the existing fail-fast / native GPU success-marker rules.

### Stage 10CG — allow task-named Mac validation branch (2026-11-19)

**Observed native user log:** the isolated worktree's local branch
was `verify-texture-rhi-20261119-150819`, **not** detached HEAD.
The user successfully fetched `origin/feature/texture-rhi-unification`
and fast-forwarded local HEAD from `67cd9a421` to `954ed5a8d`.
`bash -n tools/texture-rhi-final-verify.sh` succeeded, but the
Stage 10CF runner stopped **before CMake** with
`FAIL: refusing another local branch: verify-texture-rhi-20261119-150819`.
This was a second incorrect source-identity guard, not a failed
texture, build or Metal GPU test.

**Source fix:** commit `e7e012b01c6599c85c94f24f4a85197e4a616cf4`
changes only `tools/texture-rhi-final-verify.sh`. It accepts
**any local ref name** (including the existing task-specific
validation branch and detached HEAD) when the actual `HEAD`
commit equals the fetched
`refs/remotes/origin/feature/texture-rhi-unification^{commit}`.
An absent remote-tracking ref, stale/other branch commit or
wrong checkout still fails closed. The runner prints the
actual SHA and local branch/detached name for traceability.
The start of each run also removes an old
`build/texture-final-logs/acceptance.txt`, preventing a
previous PASS manifest from being mistaken for a new result
if the current run fails.

**Verification:** GitHub source patch and its guard were inspected;
the remote commit was created. A fresh `bash -n`, native CMake
build, CPU CTest, 7 native Metal GPU tests, and a complete
`acceptance.txt` have **NOT YET RUN** after the fix.

**Exact one-pass retry in the same existing Mac Studio worktree
(do not switch local branches):**

```bash
set -euo pipefail
cd /Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819
git fetch origin feature/texture-rhi-unification
git pull --ff-only origin feature/texture-rhi-unification
bash -n tools/texture-rhi-final-verify.sh
bash tools/texture-rhi-final-verify.sh --jobs 4
```

With `--full-engine`, additionally run all nonexperimental
engine tests, not just the texture-focused acceptance.
Only a zero exit and a **new** `acceptance.txt` ending in
`result=PASS` after seven actually executed native Metal
CTest cases certify the full suite. Keep Draft PR #21
and GPU ICB default-OFF until the actual results.

### Stage 10CH — fix false CTest PASS and nonuniform gutter scene ROI gate (2026-11-19)

**Observed real Apple M3 Ultra terminal output:** the one-shot Mac
runner compiled its Metal targets and reached the final
`metal_renderer_graph_vt_gutter_gpu` (CTest #128).
The production compiled Shader Graph + real dual-disk VT
+ GPU-encoded Metal ICB executed at five U positions:
0.40/0.48/0.50/0.52/0.60. Every position emitted
`graph=1 mixedGraph=1 vt=1 mixedVt=1 commands=2
rejected=0 errors=0`, and all five ordinary/ICB
full RGBA8 captures reported `bad_channels=0 max_delta=0`.
The native atlas published both nonuniform guttered
physical pages.

However, the fixture printed **10 real FAIL assertions**:
five times `FAIL: compiled Shader Graph draws distinct regions`
and five times `FAIL: GPU ICB draws distinct graph-source regions`.
The two gradient materials differed with ROI RGB-distance
only **16–19**, below an inherited solid-color threshold
of **35**. This was a *test-fixture acceptance threshold
mismatch*, not evidence of a GPU image disagreement.
The stronger independent logical-source bilinear
intermediate-ROI oracle, already in the fixture,
was skipped due to `failed=true`. Thus the new scene
has **not yet passed its full independent CPU oracle**.

CTest incorrectly printed `1/1 Passed` despite these
FAIL lines; macOS `NSApplication::terminate` exits the
app process, bypassing the `aiverseMain()` return code.
The one-shot shell runner **correctly rejected** the
missing mandatory final PASS marker. The user therefore
did not find `build/texture-final-logs/acceptance.txt`.
This file intentionally exists **only** after a complete
zero-error suite, never on a partial FAIL.

**Actual fix commits:**
- `7a50a4c82462407596195b4dc1ea748af9545c3e`:
  `tests/render/metal_renderer_graph_vt_gutter_gpu.cpp`
  changes the *new fixture's only* material ROI
  discrimination threshold from 35 to **12** for
  intentionally nearby nonuniform gradient colors;
  it does **not** remove nonblank-region checks or
  weaken full-frame CPU/ICB tolerance (≤2 LSB)
  or the existing independent source-logical
  bilinear scene oracle (≤3 displayed levels).
  The native observed delta range 16–19 satisfies
  12 and preserves nonzero inter-material separation.
  After platform shutdown's standard resource cleanup,
  `callbacks.shutdown` checks `finished` and calls
  `std::_Exit(1)` after flushing logs if any fixture
  `check()` failed or the acceptance sequence did
  not complete. This ensures failures propagate through
  AppKit termination to CTest.
- `035b6b81c8126a0ea9478ac5b194f9cf3b11561c`:
  `CMakeLists.txt` adds `FAIL_REGULAR_EXPRESSION "FAIL:"`
  for all seven registered native Metal texture GPU
  CTests (only when the tests exist), and explicitly
  requires the production gutter scene's final
  `PASS: production Renderer3D Shader Graph nonuniform
  guttered disk VT mixed Metal GPU ICB seam parity`
  marker using `PASS_REGULAR_EXPRESSION`. A future
  false CTest success despite a native FAIL line
  must now become a real CTest failure.
- `65b32f968ed2089c313d51cfb1e3fe89df850069`:
  `tools/texture-rhi-final-verify.sh` additionally
  scans each verbose native GPU test log for
  `FAIL:` even if CTest returned zero; the
  exact success-marker gate is retained.
- `465967cbfd84384b768c62e68d5adc71e39e4d8a`:
  the one-shot runner removes any previous PASS/failure
  summary at start and now writes
  `build/texture-final-logs/failure.txt`
  containing `result=FAIL`, checked-out SHA,
  exit code and log path on **any nonzero exit**
  (including explicit marker rejection).
  `acceptance.txt` remains success-only.
  Users can inspect a single summary file
  even when a native test fails.

**Actual verification after these commits:** source
review/remote GitHub file checks only. **No new macOS
native build, independent gutter scene CPU oracle,
GPU execution or one-shot PASS has run** since
these fixes. Do not claim Stage 10CD full acceptance
from CTest's prior false-positive 1/1 Passed.

**One-shot Mac Studio retry**, safe on the existing
`verify-texture-rhi-20261119-150819` worktree:

```bash
set -euo pipefail
cd /Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819
git fetch origin feature/texture-rhi-unification
git pull --ff-only origin feature/texture-rhi-unification
bash -n tools/texture-rhi-final-verify.sh
bash tools/texture-rhi-final-verify.sh --jobs 4
```

The safe CPU/texture tests and all 7 Metal CTests
are rebuilt from the latest HEAD; the loop will
rerun the previously observed real GPU
gutter scene and newly expose its independent
gradient/CPU bilinear oracle, not silently bypass
it. A successful run must end with the final PASS
marker and new `build/texture-final-logs/acceptance.txt`
containing `result=PASS`. On failure, inspect
`failure.txt` and the first named test log and fix
the actual compiler/Metal/independent oracle error.
Keep Draft PR #21 and experimental Metal GPU ICB
default-OFF until full native and platform evidence.

### Stage 10CI — unified Mac Studio Texture RHI acceptance PASS (2026-11-19)

**Actual user-supplied Mac Studio / Apple M3 Ultra execution:**
`tools/texture-rhi-final-verify.sh --jobs 4` completed with
`PASS: all 21 portable texture/graph and 7 native Metal GPU tests`.
The generated `build/texture-final-logs/acceptance.txt`
contained the exact *tested* commit:

```text
sha=7494f32d313a2ac45050be02d25ca3ae97749086
safe_texture_tests=21
native_metal_gpu_tests=7
full_engine=0
result=PASS
```

The commit `7494f32d313a2ac45050be02d25ca3ae97749086`
**does include** the Stage 10CH corrections to
`tests/render/metal_renderer_graph_vt_gutter_gpu.cpp`:
the nonuniform-gradient ROI threshold 12 (not the obsolete
solid-color 35) and fatal-shutdown error propagation.
The native run therefore applies to the corrected source, not
an earlier binary or an unrelated checkout. It also ran after
the CTest failure regex and script PASS-marker fixes.
The suite builds all targets before CTest, and the success
manifest is generated only after all required processes
and markers complete. The user supplied the last test's
full verbose output and the final one-shot manifest.
Results for earlier steps are supported by the script's
fail-fast success conclusion, not independently reproduced
from each earlier log in this chat.

**Final full Renderer3D scene native Metal result:**

- Test: `metal_renderer_graph_vt_gutter_gpu` (#128),
  **1/1 PASS**, `1.90 s` CTest test and `1.90 s`
  total; `pipelineFailures=0`.
- Two distinct disk-backed nonuniform RGBA8 VT physical
  page slices published; two separate materials with
  independent atlas/page-table bindings and 1-pixel gutters.
- At **U=0.400, 0.480, 0.500, 0.520, 0.600**,
  real GPU ICB counters were identical:
  `graph=1 mixedGraph=1 vt=1 mixedVt=1
  commands=2 rejected=0 errors=0`.
- Every ordinary Renderer3D / GPU ICB frame comparison
  returned **`bad_channels=0 max_delta=0`**.
  Distinct left/right region deltas across the five
  samples were **18, 19, 17, 18, 16**, all above
  the corrected nonuniform gradient threshold 12.
- The separate independent original-logical-source
  scene bilinear oracle ran on **both** direct and ICB
  images. All its expected interpolation progress and
  RGB direction checks succeeded for two material
  ROIs, three interior U positions, and three color
  channels. Observed worst interpolated ROI-channel
  difference from the calibrated original-source
  prediction was **1.00 displayed RGB level**.
- The native test printed both mandatory final markers:
  `PASS: production Renderer3D graph VT nonuniform gutters
  independent CPU seam oracle`, and
  `PASS: production Renderer3D Shader Graph nonuniform
  guttered disk VT mixed Metal GPU ICB seam parity`.
  There were no native `FAIL:` assertions.

**Accurate coverage statement:** the entire scripted
Texture RHI acceptance passed at SHA `7494f32d...`:
21 focused portable CTests and seven separate,
serial real Metal GPU CTests, including low-level
texture acceptance, Shader Graph ICB, dual-disk VT
coarse/fine/LRU, intentionally missing-page fallback,
Repeat/MirrorRepeat wrap oracles, the absolute
136-point nonuniform bilinear sampler test (previously
observed 120 seam points and max delta 1), and this
new full scene. This is an end-to-end correctness
milestone on **Apple M3 Ultra / Metal**.

**Not claimed:** the user ran with `full_engine=0`,
so the **entire unrelated engine CTest suite has not
been exercised by this one-shot result**.
Likewise Vulkan/GL/GLES/WebGL/mobile compatibility,
trilinear transitions and anisotropic filtering,
memory/streaming soak, GPU frame time/FPS and
performance scaling remain separate acceptance
requirements. `1.90 s` is CTest duration, not GPU
frame time. Metal GPU ICB and high-risk tests remain
default-OFF in production; PR #21 remains Draft
until release gating is explicitly resolved.

**Reproduction at the verified source revision:**

```bash
cd /Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819
git fetch origin feature/texture-rhi-unification
git pull --ff-only origin feature/texture-rhi-unification
bash -n tools/texture-rhi-final-verify.sh
bash tools/texture-rhi-final-verify.sh --jobs 4
```

After this documentation commit, the branch HEAD
naturally advances beyond the tested `7494f32d`;
the source-code acceptance remains tied to the
actual tested SHA above. Documentation-only changes
do not retroactively change which commit the Mac
tested. To also include the whole nonexperimental
engine test suite, run with `--full-engine`.


### Stage 10CJ — compile canonical VT sampling on GL33/GLES3 and Metal (2026-11-19)

**Motivation:** The last fully verified Metal Texture RHI source revision
`7494f32d313a2ac45050be02d25ca3ae97749086` passed **21 focused
portable CTests and 7 native Apple M3 Ultra GPU CTests**. That hardware
proof includes a real Renderer3D/mixed Shader Graph ICB graph with
four graph textures + virtual-texture sampling. GL/Vulkan/GLES/WebGL
source compatibility had not been independently proven for the
**new positive-gutter** form of `gfx::kVirtualTextureGLSL`.

**New source-level toolchain acceptance (NOT GPU pixels):**
`tests/render/check_virtual_texture_sampling_shaders.py` reads the
production `engine/gfx/VirtualTextureSampling.h` and extracts exactly
the two `R"AVT(... )AVT"` canonical shader strings. It fails if a
symbol is missing/ambiguous or a compiler is missing for the explicitly
requested backend. It compiles a **complete fragment shader** embedding
all overloaded `aiverseLookupVirtual/aiverseSampleVirtual` functions
for OpenGL **GLSL 330 core** and OpenGL ES **GLSL 300 es** through
`glslangValidator -S frag`. Both the full address-mode path and the
legacy clamp overload are used so none can be accidentally excluded.
On macOS with `xcrun metal`, a second mode compiles the matching
canonical Metal MSL overloads to a native `.air` object and checks
that the output exists. This is shader-source syntax and resource-ABI
acceptance, not a claim that GL or Vulkan rendered the right pixels.

**Committed implementation:**
- `6d0d9a8d3ebec7f57c9c4657c2925d6460667b8e`
  adds `tests/render/check_virtual_texture_sampling_shaders.py`.
- `d9f8e0396c86f26f2723a0cb169659930536a7f6`
  registers `virtual_texture_sampling_glsl` when Python +
  `glslangValidator` are available and
  `virtual_texture_sampling_msl` on Apple when Python +
  `xcrun` are available. The CMake scope was then corrected by
  `20c79da8473abf99f56eedcc12e6f2e3fc1547bc` so the GLSL
  test is never registered when its compiler is absent.
- `a82391820c804a4c533bbcb1aaa995cedaaf15ff` first edited
  the focused script; `3a222231828d432a9cb04d4b7d9a69124fb2e2e6`
  restores its clean 197-line version and includes the two new
  toolchain checks in the focused CTest filter/registration log.
- `c8a4afd585d5fc082fe846e31001179f2dc765dc` first edited
  the one-shot Mac script; `3a1a17b1fa061314398a21f40fedb6b69e5cb9a1`
  restores the known-green 175-line version and adds optional
  `virtual_texture_sampling_glsl/msl` to its one-shot safe
  shader compilation checks, retaining all previous 21 portable
  test and 7 native Metal GPU gates.
- `f2d96b5c0d3db3766fb8dc56110df9653a146178` first edited
  the existing `.github/workflows/texture-rhi.yml`;
  `f40cbef2a14ce5df92daa9cb9152811a7333a57f` restores the
  original intact workflow and adds `glslang-tools` to the
  OpenEXR ON/OFF Linux and Vulkan build jobs. Both jobs now
  explicitly run `ctest --no-tests=error --verbose
  -R '^virtual_texture_sampling_glsl$'` so a missing test or
  syntax failure is a hard CI failure.

**Why two corrective edits were needed:** Javascript string replacement
treats the literal characters `$'` in replacement text as a special
suffix-substitution. Initial string-based edits truncated regular
expressions in the shell/workflow and duplicated portions of the
one-shot runner. This was **detected through immediate GitHub source
refetch before any hardware/CI run**. All affected files were rebuilt
from the last known verified parent commit and checked for exact
block counts and completed regexes. The final one-shot runner retains
one GPU-build block and one seven-GPU-test loop; workflow retains both
upload-artifact steps, plus CMake's compiler guard is now correctly
scoped.

**Actual verification completed:** read and reviewed the final committed
CMake, Python, workflow YAML and both Bash scripts from the feature
branch via the GitHub connector. Confirmed GL33/GLES3 and Metal
source entry points, completed regexes and CMake optional registration,
and that GitHub Actions Linux/Vulkan jobs install
`glslangValidator` and require one actual test.
**Not executed yet for these new changes:** native GLSL toolchain
compilation, macOS `xcrun metal` compilation of the new fixture,
the full engine regression, an actual GL/Vulkan runtime GPU
framebuffer probe, or new post-change GitHub CI PASS.

**Next complete local full-engine run (Mac Studio)**:

```bash
set -euo pipefail
cd /Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819
git fetch origin feature/texture-rhi-unification
git pull --ff-only origin feature/texture-rhi-unification
bash -n tools/verify-texture-rhi.sh
bash -n tools/texture-rhi-final-verify.sh
bash tools/texture-rhi-final-verify.sh --jobs 4 --full-engine
```

`--full-engine` compiles and tests **the complete default
nonexperimental AIVERSE CTest suite**, before a separate GPU
build runs all seven risky Metal tests in serial. Its full
engine acceptance is not recorded until actual fresh logs
and the final `result=PASS` manifest are observed. On Linux
with the needed development dependencies, run the existing
`tools/verify-texture-rhi.sh --vulkan` and inspect
`vulkan_backend` for real execution vs SKIPPED; GLSL syntax
compilation alone is insufficient.

**Still pending after a GLSL source-compile PASS:** actual GL
and Vulkan render-target readback parity, GLES/mobile/WebGL
runtime resource limits, nonuniform mip transition/trilinear
and anisotropic visual tests, streaming stress and real GPU
time/FPS benchmark. PR #21 remains Draft; the Metal
GPU-authored ICB path remains experimental and OFF by default.

## Indexed Metal sampling acceptance (2026-11-20)

The opt-in `metal_vt_bilinear_gpu` test now also executes the canonical
MSL indexed trilinear and anisotropic helpers through the real Metal RHI.
Its additional 18 RGBA8 readback probes use independently authored solid
colors with a deliberately permuted physical slice order: fine left/right
pages, fractional/clamped virtual LOD, logical layer stride, missing-page
fallback/blending, invalid layer, Repeat/MirrorRepeat, 1/2/4/8-tap footprints,
mip bias and a Y-major derivative. Color expectations are closed-form;
the test does not call the shader lookup algorithm to derive expected values.
The existing 136 nonuniform logical-texel/gutter probes remain in the same gate.

This isolates canonical shader sampling. It does not certify indexed
Renderer3D/ICB integration, physical mip-chain filtering, nonuniform
anisotropic quality, streaming stress, other backends or performance.
Use the existing `AIVERSE_TEXTURE_METAL_VT_BILINEAR_GPU_TESTS=ON` option
and a Debug build on `/Volumes/KINGSTON/AIVERSE-builds/`; run via
`run-tests.sh --no-build --build-dir <external-build> --filter '^metal_vt_bilinear_gpu$'`.
Exact results and code SHA are recorded in the stage handoff.

## Final local acceptance gates (2026-11-20)

`metal_vt_bilinear_gpu` additionally covers 1170 nonuniform indexed GPU
samples against logical-space CPU bilinear references, with independently
authored coarse/detail images, fractional virtual LOD, Clamp/Repeat/MirrorRepeat,
2/4/8-tap cross-page footprints and 18 detail-page LRU transitions. Each page
has four physical mip levels; an intentionally one-upload-per-frame budget
checks that partial mip chains do not become visible. Six separate authored
physical-mip color probes check derivative-selected hardware mip blending.

`metal_renderer_indexed_vt_gpu` runs the existing nonuniform disk-backed
Shader Graph scene with indexed table slices and the 48-byte sampling
uniforms. It compares direct draws with ICB opt-in enabled, verifies the
independent scene-color oracle, and requires zero GPU-encoded commands:
indexed graph VT deliberately uses the safe direct fallback. GPU admission
excludes indexed bindings before preflight and command-cache construction;
unsupported 48-byte parameters never enter the legacy 16-byte ICB ABI. This is the
supported behavior; it is not evidence of indexed ICB acceleration. Legacy
mixed-material ICB tests continue to require actual indirect submissions.
All eight native Metal tests are serial in CTest, including full-suite runs.

The final verifier accepts a clean main/feature checkout, an optional
`--expected-ref` revision and an explicit absolute `--build-root` (or
`AIVERSE_TEXTURE_FINAL_BUILD_ROOT`). Safe/GPU builds, logs, temporary files
and the Clang module cache live beneath this root; both caches are Debug.
On the owner's Mac use a directory under `/Volumes/KINGSTON/AIVERSE-builds/`:

```sh
bash tools/texture-rhi-final-verify.sh --expected-ref HEAD \
  --build-root /Volumes/KINGSTON/AIVERSE-builds/texture-final --full-engine --jobs 8
```

The focused CI workflow now also triggers on texture changes merged to main.
Local MSL/GLSL/SPIR-V compilation and Metal image tests cannot substitute for
GL/GLES/Vulkan runtime readbacks on their respective backends. See the final
stage handoff for actual commands/results, skips and external blockers.
