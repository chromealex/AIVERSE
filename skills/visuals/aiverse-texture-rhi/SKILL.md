---
name: aiverse-texture-rhi
description: Reuse AIVERSE native texture dimensions, offscreen MRT/MSAA, DDS/KTX2 imports and frame-budgeted streaming rather than generating a custom texture backend.
---

# AIVERSE Texture RHI — native agent recipe

Read `docs/texture-rhi.md` for actual backend capability differences,
`engine/gfx/Gfx.h` for the typed contract, and
`docs/agent-handoffs/texture-rhi-2026-11-18.md` for the most recent code SHA,
tests actually run and next gates. **This implementation branch is unverified:
never equate source presence with a passing GPU test.**

## Choose an existing API

| Goal | Public native C++ entry | Notes |
|---|---|---|
| 1D, 2D, 3D, array, cubemap | `gfx::TextureDesc`, `Device::createTexture` | `dimension`, `depth`, `arrayLayers`, `mipLevels` |
| Sample subresource | `TextureViewDesc`, `Device::createTextureView` + `bindTextureView` | GL33/GLES3 whole-resource identity views only |
| Custom filtering | `SamplerDesc`, `Device::createSampler` | Compare, LOD clamp, anisotropy per backend |
| Offscreen 1–4 color targets | `gfx::RenderTargetSet` | `init`/`begin`/`end`/`shutdown`, optional depth |
| Low-level mip/face render | `RenderTargetPassDesc`, `Device::beginRenderTargets` | Call `endPass()` only on successful begin |
| DDS DX10, KTX2 arrays/cubes/volumes | `parseDdsTexture`, `parseKtx2`, `uploadContainerTexture` | `CompressedImage` keeps all faces/mips |
| Optional BasisU KTX2 import | `render::parseKtx2WithTranscoder`, `render::uploadKtx2WithTranscoder` | Inject a real licensed codec; exact mip/face validation |
| Real BasisLZ/UASTC LDR | `render::makeLibktxBasisTranscoder`, `render::uploadKtx2WithLibktx` | Requires `AIVERSE_TEXTURE_WITH_LIBKTX=ON` and system Khronos libktx |
| Compressed OpenEXR 3 HDR | `render::parseOpenExrWithLibrary`, `render::loadOpenExrWithLibrary` | Requires `AIVERSE_TEXTURE_WITH_OPENEXR=ON` and system OpenEXR 3 |
| Budgeted deferred texture upload | `gfx::TextureStreamQueue` | Owns GPU texture and staged bytes; `drain()` on device thread |
| Safe coarse-to-fine sampling | `gfx::TextureMipResidency` | Only create/bind sampler when complete coarse-to-fine mips resident |
| Unified native streaming | `gfx::TextureResidentStreamer` | Owns queue+residency; gates sampling, tracks completions and retries |
| Radiance `.hdr` / limited EXR | `render::loadRadianceHdr`, `render::loadOpenExr` | Linear RGBA16F; EXR is uncompressed scanline HALF/FLOAT RGB[A] only |
| Virtual texture atlas | `gfx::VirtualTextureStreamer` | Fixed physical Texture2DArray pages, GPU page table, LRU and pinning |
| Disk page I/O worker | `gfx::VirtualTextureDiskQueue` | Deduplicated, cancelable, bounded filesystem page results |
| Disk-backed virtual atlas | `gfx::VirtualTextureFileStreamer` | Explicit page requests, render-thread pump and GPU page-table publication |
| Bounded UV visibility prefetch | `gfx::VirtualTexturePrefetchPlanner`, `prefetchVisibleVirtualPages` | Clips and deduplicates visible UV rectangles; requests coarse mips first |
| Optional GPU page feedback | `gfx::VirtualTextureFeedbackRuntime`, `VirtualTextureFeedbackDecoder` | Async linear RGBA8 readback decoded into bounded visible UV regions |
| Owned GPU feedback render target | `gfx::VirtualTextureFeedbackPass` | RGBA8 offscreen pass + async capture; app supplies opaque feedback shader |
| GPU subresource readback | `Device::readTextureAsync` | Metal/Vulkan/GL33/GLES3 source; unsupported on WebGL2 |
| Live texture inventory | `Device::textureResources()` | Optional live GPU resource snapshot; byte size is estimated |

### Example: mip-streamed Texture2DArray (native C++)

```cpp
#include "engine/gfx/TextureResidentStreamer.h"

gfx::TextureResidentStreamer stream(device);
gfx::TextureDesc desc;
desc.width = desc.height = 256;
desc.dimension = gfx::TextureDimension::Texture2DArray;
desc.arrayLayers = 4;
desc.mipLevels = 9;
const auto resource = stream.create(desc);
if (resource.valid()) {
    // Demo producer. Replace with pre-decoded packed mip/layer data from
    // your game's asset loader; avoid file I/O on the GPU render thread.
    auto ticket = stream.stageNext(resource,
        [](gfx::TextureMipRequest, int w, int h, int depth,
           std::vector<uint8_t>& out) {
            out.assign(size_t(w) * size_t(h) * size_t(depth) * 4u, 255);
            return true;
        });
    if (ticket.valid()) stream.drain();
    gfx::SamplerDesc requested, resident;
    const auto sampled = stream.sampledTexture(resource);
    if (sampled.valid() && stream.residentSampler(resource, requested, resident)) {
        // Create independent GPU sampler using the returned safe mip limits.
        // Bind it with an identity/full-resource TextureViewDesc on GL or
        // a compatible subresource view on Metal/Vulkan.
        auto sampler = device.createSampler(resident);
        if (sampler.valid()) device.destroySampler(sampler);
    }
    stream.destroy(resource);
}
```

The first request contains **one** face of the coarsest mip. Stage
remaining faces and finer mips during subsequent frames. `drain()`
submits bounded updates on the render thread; it does not load assets
from disk asynchronously. For per-face/full-volume data you must provide
a producer that fills exactly the requested packed byte count. Bind
nothing until `sampledTexture` becomes valid. The facade owns its GPU
texture handles and must be destroyed before `gfx::Device`.

### Virtual texture page streaming

Use `gfx::VirtualTextureStreamer` with `gfx::VirtualTextureStreamConfig`.
Initialize physical atlas capacity and tile dimensions, then stage a
complete guttered RGBA8/sRGB page using `stage(VirtualPageKey, bytes, size)`.
`drain()` submits bounded GPU page copies, publishes the page table
after successful completion, and only then exposes updated mappings.
`physicalAtlas()` and `pageTableTexture()` are the shader inputs.
`engine/gfx/VirtualTextureSampling.h` contains GLSL and Metal helpers.
Stale mappings are invalidated before LRU reuses a physical slot;
a failed table/upload quarantines the entire atlas. This is not hardware
sparse residency, automatic GPU feedback, or disk streaming.

`Device::readTextureAsync` returns false for unsupported source formats
or backends, and otherwise delivers a packed subresource on the graphics
owner thread in a later frame. Metal supports RGBA8/sRGB/RGBA16F;
GL33/GLES3 use PBO+fence for RGBA8/sRGB; Vulkan uses fence-completed
host staging for RGBA8/sRGB/RGBA16F. WebGL2 refuses native async
readback. None of those paths has passing GPU verification yet.

### File-backed virtual pages — native integration

Create immutable RGBA8 files under the trusted game root, e.g.
\`m2/l0/x0_y0.rgba\`, each exactly \`pageSize*pageSize*4\` bytes
including image gutters. The two configs must have matching
virtual tiles, layers, mips and physical page size:

\`\`\`cpp
#include "engine/gfx/VirtualTextureFileStreamer.h"
gfx::VirtualTextureStreamConfig atlas;
atlas.tilesX = atlas.tilesY = 256;
atlas.mipLevels = 9; atlas.virtualLayers = 1;
atlas.physicalPages = 64; atlas.physicalPageSize = 128;
gfx::VirtualDiskPageConfig disk;
disk.assetRoot = "assets/virtual_terrain";
disk.tilesX = atlas.tilesX; disk.tilesY = atlas.tilesY;
disk.mipLevels = atlas.mipLevels;
disk.virtualLayers = atlas.virtualLayers;
disk.pageSize = atlas.physicalPageSize;
gfx::VirtualTextureFileStreamer streaming(device);
if (streaming.initialize(atlas,disk)) {
    auto ticket = streaming.requestPage({8,0,0,0},true);
    // Call each frame on GPU owner thread, not on the file worker.
    streaming.pump(4,[](gfx::VirtualDiskPageTicket,
                        gfx::VirtualPageKey,bool ok,
                        const std::string& error) {
        // Respond to page success/failure in presentation state only.
    });
    streaming.shutdown();
}
\`\`\`

The I/O worker never calls gfx::Device. Pin coarsest mip
fallbacks, request missing detail pages explicitly and use
the existing GLSL/MSL virtual texture lookup snippets.
Automatic GPU-feedback-driven page requests, file packing
and real-device shader validation are still separate work.
No native GPU tests have been confirmed passing.

### Visibility-driven prefetch without a genre dependency

On the graphics/streaming owner thread, include
`engine/gfx/VirtualTexturePrefetchAdapter.h`. A game projects
its visible surfaces (terrain, object, UI atlas, etc.) to normalized
UV rectangles. The planner never depends on a camera component,
FPS/RTS mechanic or ECS authority:

```cpp
gfx::VirtualTexturePrefetchPlanner planner;
gfx::VirtualTexturePrefetchConfig prefetch;
prefetch.tilesX = atlas.tilesX;
prefetch.tilesY = atlas.tilesY;
prefetch.mipLevels = atlas.mipLevels;
prefetch.virtualLayers = atlas.virtualLayers;
prefetch.maxCandidates = 1024;
prefetch.maxExaminedPages = 4096; // includes already loaded/queued tile lookups
prefetch.maxRequestsPerFrame = 16;
if (planner.initialize(prefetch)) {
    gfx::VirtualTextureVisibleRegion visible;
    visible.u0 = .25; visible.v0 = .25;
    visible.u1 = .50; visible.v1 = .50;
    visible.desiredMip = 1;
    visible.layer = 0;
    const auto plan = gfx::prefetchVisibleVirtualPages(
        streaming, planner, {visible});
    // Plan submits disk tickets only; `pump` performs GPU uploads.
    streaming.pump(8);
}
```

`prefetchVisibleVirtualPages` skips exact resident or already
pending virtual pages, not just pages with a coarser resident
ancestor. It upgrades coarsest pinned fallbacks, respects the
planner's finite region/candidate/request budgets, and stops
on streaming-queue backpressure. It does NOT implement hardware
GPU feedback, sparse images, occlusion queries or camera projection;
the game owns UV visibility and desired mip determination.

### Owned offscreen page-ID feedback pass

`engine/gfx/VirtualTextureFeedbackPass.h` owns the generic
`RenderTargetSet` and the existing async feedback runtime.
It avoids creating a special FPS/RTS/Terra render-target manager.

```cpp
gfx::VirtualTextureFeedbackPass feedbackPass;
gfx::VirtualTextureFeedbackConfig fb;
gfx::VirtualTexturePrefetchConfig pf;
fb.tilesX = pf.tilesX = atlas.tilesX;
fb.tilesY = pf.tilesY = atlas.tilesY;
fb.mipLevels = pf.mipLevels = atlas.mipLevels;
fb.virtualLayers = pf.virtualLayers = atlas.virtualLayers;
if (feedbackPass.initialize(device, 256, 256, fb, pf)) {
    // Inside the game's graphics frame, outside other passes:
    if (feedbackPass.begin()) {
        // Bind an Opaque, single-attachment RGBA8 pipeline and draw
        // visible virtual-texture surfaces to the page-ID target.
        // GLSL/MSL helpers: engine/gfx/VirtualTextureFeedback.h
        feedbackPass.endAndCapture();
    }
    // On a later graphics frame, after beginFrame callback dispatch:
    auto dispatched = feedbackPass.submitVisiblePages(streaming);
    streaming.pump(8);
    feedbackPass.shutdown(); // before graphics Device teardown
}
```

The helper allocates one *linear RGBA8 UNORM*, single-sample
Texture2D, clears all bytes to zero, sets viewport and scissor,
and refuses another capture while the prior GPU readback is
pending or awaiting submission. It never creates a shader,
draws a camera, inserts a render graph pass or configures an
ECS entity. Keep blending, sRGB conversion, MSAA, dithering
and postprocessing disabled in the feedback shader/pipeline.
Failures in readback are reported; WebGL2 remains unsupported.

### Optional feedback-buffer-driven page requests

`engine/gfx/VirtualTextureFeedback.h` defines the exact RGBA8 page ID
encoding and GLSL/MSL helpers (`kVirtualTextureFeedbackGLSL`,
`kVirtualTextureFeedbackMSL`). Draw them in a **separate offscreen**
linear RGBA8 UNORM render target with no blending, sRGB conversion,
dithering, MSAA resolve or postprocessing. Clear unused pixels to zero.
Source key layout is 11-bit x, 11-bit y, 5-bit mip, 4-bit virtual
layer and a valid bit: maximum 2048×2048 tile grid, 16 layers.

`engine/gfx/VirtualTextureFeedbackRuntime.h` coordinates optional
native `Device::readTextureAsync` and the existing file-streamer:

```cpp
gfx::VirtualTextureFeedbackRuntime feedback;
gfx::VirtualTextureFeedbackConfig feedbackCfg;
feedbackCfg.tilesX = atlas.tilesX;
feedbackCfg.tilesY = atlas.tilesY;
feedbackCfg.mipLevels = atlas.mipLevels;
feedbackCfg.virtualLayers = atlas.virtualLayers;
gfx::VirtualTexturePrefetchConfig prefetchCfg;
prefetchCfg.tilesX = atlas.tilesX;
prefetchCfg.tilesY = atlas.tilesY;
prefetchCfg.mipLevels = atlas.mipLevels;
prefetchCfg.virtualLayers = atlas.virtualLayers;
if (feedback.initialize(device, feedbackCfg, prefetchCfg)) {
    // After the application's feedback render pass has ended:
    feedback.capture(linearRgba8FeedbackTarget);
    // On a later frame, after Device::beginFrame delivers callbacks:
    feedback.submit(streaming);
    streaming.pump(8);
    feedback.shutdown();  // before Device and streaming teardown
}
```

Do not treat `capture()` as synchronous: it can fail when the
backend does not support the requested readback, and WebGL2
currently rejects this native async path. A helper destroyed
before GPU completion drops its weak callback safely. No
camera pass, shader attachment, framebuffer allocation or
automatic render-graph node is created by this header.
The app supplies an actual feedback shader and render target.

### Optional Basis Universal codec boundary

Include `engine/render/Ktx2Transcode.h` and supply a callback of type
`render::Ktx2Transcoder` when using ETC1S/BasisLZ or UASTC KTX2.
The callback must return all mip-major slices/faces with the same
source dimensions/layers/mip chain. It must select a format the
GPU supports, or return RGBA8 for the existing CPU-upload path.
Without a codec, Basis input fails explicitly. A real **external**
Khronos `libktx` adapter now exists in `Ktx2Libktx.h`, but
requires an explicit system library build:

```sh
cmake -S . -B build/texture-rhi -DAIVERSE_TEXTURE_WITH_LIBKTX=ON
```

Call `render::uploadKtx2WithLibktx(device,encoded,options,error)`.
The adapter prefers native BC7/ASTC/ETC2/BC3 if the GPU supports
the matching linear/sRGB format; it can fall back to RGBA8.
LDR imports use the normal KTX2 container callback. For HDR UASTC,
call `render::uploadKtx2UastcHdrWithLibktx(device,encoded,error)`
instead: it requests the real `KTX_TTF_RGBA_HALF` decoder,
preserves all mips/faces/slices and imports an RGBA16F GPU
resource (if supported). This is not native BC6H/ASTC_HDR.
Real HDR codec and GPU readback tests are still required.
No codec is bundled; license/packaging and GPU fidelity
must be verified with the installed upstream package.

### Compressed OpenEXR import and Vulkan compute images

When using `render::parseOpenExrWithLibrary` from
`engine/render/ExrOpenExr.h`, enable the explicit
`AIVERSE_TEXTURE_WITH_OPENEXR=ON` CMake option and install
OpenEXR 3 first. This supports ZIP/ZIPS/PIZ and other
OpenEXR-library scanline RGBA sources, but not deep/multipart.
Without it, the existing uncompressed HALF/FLOAT parser remains.

Vulkan now implements the optional compute/storage texture
methods. `ComputeDesc.glsl` is a GLSL450 body with
`layout(set=0,binding=0,rgba8) uniform writeonly image2D image0;`
for writable output. The binding layout is storage images
0..3; sampled images 4..11; storage buffers 12..15; UBO 16.
Use `Device::supportsCompute`,
`Device::supportsStorageTexture(PixelFormat::RGBA8,TextureDimension::Texture2D)`,
and check every returned handle and bool before dispatch.
Metal already has its own MSL compute implementation; GLES3/GL33/
WebGL2 do not silently emulate compute. Actual GPU tests
remain pending.

Feedback visibility regions may be reused for up to eight
later frames if detail requests exceed the prefetch budget;
the runtime drops stale regions when a fresher GPU feedback
result arrives and never queues these into authoritative ECS.

### Correctness and compatibility

- Keep existing model material shaders' `sampler2D` resources on the
  legacy first-face path. Only dimension-aware shaders sample arrays/cubes.
- Check `Device::supports` and actual handle validity for format availability.
  `PixelFormat::BC4/BC5/EAC_R11/BC6H` is not universally supported.
- Compressed sRGB BC1/BC3/BC7, ETC2 and ASTC have native RHI variants
  for capable GPUs. Prefer hardware sRGB sampling (no shader gamma
  conversion) when the source KTX2/DDS declares it; unsupported
  combinations fall back to RGBA8_sRGB where CPU decoding exists.
  BC1_RGB keeps opaque-index semantics instead of being forced into
  BC1_RGBA. WebGL2 requires explicit compressed sRGB capability probing.
- The BC5 native GPU path returns RG only; enable
  `TextureUploadOptions.shaderReconstructsBc5NormalZ` **only** if the shader
  reconstructs Z. Otherwise keep the safe CPU-decoded normal map.
- `RenderTargetSet` is not the old paint canvas `render::RenderTexture`.
  MSAA requires a matching `PipelineDesc.sampleCount`, separate resolved
  single-sample texture and a backend supporting the requested sample count.
- The streaming queue is **deferred frame-budgeted submission**, not
  background GPU readback. It owns every `TextureHandle` it creates and
  must be destroyed before `gfx::Device`.
- Never use GPU float/resource state in authoritative ECS tick/checksum.

### Masked VT with real VRAM savings (post Stage 10G)

- Before upload, pack the Terra/Model source RGBA8 into immutable pages.
  For `AlphaMode::Mask` models, configure
  `render::configureModelVirtualMaskedFallback(model,image,archive,...)`
  or `configureTerraWeavePackedMaskedImageFallback(...)`, then
  `render::uploadModel(dev,model)`. This creates a downsampled RGBA8
  `Image::tex` plus a **full-resolution R8** `Image::virtualAlphaMask`
  with the source image's wrap/filter/mip settings.
- `ModelShaders.h` uses the `VIRTUAL_ALPHA_MASK` variant and
  `tex15.r` for full-resolution alpha clipping in colour, shadow,
  selection and LOD paths. Rigid feedback uses the red-channel
  `VirtualTextureFeedbackProgram` variant; deformed feedback
  selects tex15.r through its U6 policy. Never downsample a cutout
  and then test alpha from the coarse RGBA8 fallback.
- This optimization is **off by default** and excludes Shader Graph,
  blended/transparent materials, team modes dependent on base alpha,
  non-base uses of the same image and images already uploaded/cached.
  Audit dynamic `ModelDraw::materials` overrides separately.
  GPU ICB is conservatively disabled for scenes requiring external R8
  mask bindings because the ICB command ABI does not encode tex15.
- `VirtualFallbackSavings` estimates base-level source/fallback/R8
  bytes, not measured VRAM. The full source image is stored in the
  immutable disk page archive. Model::releaseModel releases both
  model-owned GPU handles after their safe lifetime.
- Deferred verification must cover exact alpha silhouette across
  main/shadow/selection/feedback (GL, Metal, Vulkan), R8 mip
  filtering, sampler address semantics and resource lifetime.
  No native build/GPU test was run for this feature stage.

### Experimental VT batch grouping on Metal

- Use `ModelVirtualTextureBinding` as an exact GPU resource
  identity: physical atlas, page-table handle, virtual layer and
  gutter. The `modelVirtualTextureBindingHash` is only a fast
  grouping prefilter; `compatibleModelVirtualTextureBinding`
  must also pass for every group member. A Horizon archive
  revision switch invalidates existing GPU batch signatures.
- CPU MDI and compute-encoded Metal ICB can share VT fragment
  resources **only** inside a homogeneous group. Each GPU ICB
  command keeps its own transform/indices; all commands inherit
  one group atlas/page-table/U6 and source material binding.
  This is not yet a per-command heterogeneous VT material table.
- Metal PBR argument buffer includes optional VT texture slots
  13/14 and R8 alpha slot 15 when the corresponding shader
  permutation is enabled. VT uses the standard material sampler
  s1; the page-table lookup reads an integer texel and needs no
  extra sampler. Never bind empty/surplus sampler IDs 27/29.
- GPU ICB groups capture external textures at preparation and
  compare current handles/parameters just before execute.
  A changed Horizon revision/mask rejects ICB and falls back
  to CPU submission. Unsupported skinned/morphed/graph/animated
  materials remain on CPU. Full Metal PBR ICB remains
  **experimental opt-in**; never enable by default.
- User deferred native/CTest/GPU tests. Do not claim a speedup
  or visual correctness until targeted Metal ICB and CPU MDI
  shader compilation, output-parity and perf data exist.

### Multiple independent Terra VT domains

- In multi-domain mode call
  `TerraWeaveVirtualTextureFeedback::initializeForDomains(dev,renderer,
  depthAttachment=true)` instead of allocating a dummy legacy feedback
  target. All domain feedback passes must have the same depth mode as
  the installed model feedback programs. The manager validates this
  when attaching and adding domains.
- Use `render::TerraWeaveVirtualAtlasDomains` for separate Horizon,
  Forest and impostor atlases. Register stable numeric DomainIds,
  each with a matching immutable versioned page archive and its own
  `TerraWeaveVirtualAtlasSession` / GPU feedback pass.
- **Never decode combined 32-bit page IDs from two archives in one
  GPU readback.** The codec has tile/mip/layer but no domain index.
  The manager filters one domain per capture, preserves occluder
  depth from all other models, and submits readback only to its
  matching streamer via `pumpActive()`. Other domains can use
  `requestPage(domain,key,pin)` for CPU visibility prefetch.
- `beginFrame()` clears previous model/material pointer registrations;
  call `registerDraw` before ordinary `Renderer3D::drawModel`.
  Use `setLiveDomainLookup` to resolve private Horizon models,
  using their real object/revision identity, not their path alone.
- `promote(domain)` requires the candidate fallback pages resident,
  a compatible new feedback decoder, and no pending/unconsumed
  domain readback. The application controls the GPU fence before
  `retireCompletedGpuWork(domain)`. Call `detach()` before
  destroying Renderer3D/feedback objects; the manager's callbacks
  capture its address.
- The manager installs `Renderer3D::setVirtualTextureFeedbackScheduler`
  and uses the original shared model feedback shader programs.
  Independent domains can sample their own VT atlas in one colour
  scene; GPU ICB/MDI still require homogeneity within each group.
- Historical code fix: `injectModelVirtualTextureSampling` must match
  current `ModelShaders.h` Metal signature, including the optional
  R8 `VIRTUAL_ALPHA_MASK` arguments after `GI_FARGS`.
  Do not restore the obsolete `GI_FARGS) {` assumption.
- New functionality is committed but uncompiled; compile C++,
  validate Metal shaders and GPU readback routing during the
  explicitly deferred test phase, not before the user requests it.

### Explicit material Shader Graph VT node

- `sampleVirtualTexture` is a fragment-only, material-only graph
  op with one UV input. It compiles to `SG_SAMPLE_VIRTUAL(uv)`,
  and may specify `srgb:true` for the graph compiler's normal
  sRGB-to-linear conversion. It is not a generic graph texture
  path, and should not be generated for postfx/particles/vertex.
- `graph::Program::usesVirtualTexture` tells the model renderer
  to request an opt-in `ModelVirtualTextureBinding`.
  No mapping? `SG_SAMPLE_VIRTUAL` falls back to `SG_SAMPLE`
  (ordinary tex1) rather than sampling unbound 2D arrays.
- GL33/GLES3/Metal allow only texture slots 0..15 here.
  The graph's independent sources occupy slots starting 13;
  the VT physical atlas and page table need two more:
  0 graph sources -> tex13/tex14; 1 -> tex14/tex15.
  Reject two or more independent graph textures in a graph
  with a VT node. Do not collide with Shader Graph slots or
  hidden R8 mask texture 15 (graph+R8 is not supported).
- Do not silently mark all graph texture nodes as virtual.
  `sampleTexture` and `sampleTextureLod` remain regular;
  authors must explicitly use `sampleVirtualTexture`.
  Graph-backed GPU models stay on the CPU material submission
  path, not experimental Metal ICB, until an explicit graph
  argument-buffer resource ABI is implemented.
- See docs/shader-graph.md and docs/texture-rhi.md. No shader
  backend compile or GPU test has been run yet.

### Verification gates — don't invent passes

```sh
cmake -S . -B build/texture-rhi -DAIVERSE_BUILD_TESTS=ON
cmake --build build/texture-rhi --target \
  aiverse_texture_contract_test aiverse_render_target_set_test \
  aiverse_texture_streaming_test aiverse_texture_mip_residency_test \
  aiverse_texture_resident_streamer_test aiverse_virtual_texture_pages_test \
  aiverse_virtual_texture_page_table_test aiverse_virtual_texture_streamer_test \
  aiverse_virtual_texture_disk_queue_test aiverse_virtual_texture_file_streamer_test \
  aiverse_virtual_texture_prefetch_test aiverse_virtual_texture_feedback_test \
  aiverse_virtual_texture_feedback_pass_test \
  aiverse_hdr_texture_test aiverse_exr_texture_test \
  aiverse_ktx2_layers_test aiverse_ktx2_transcode_test \
  aiverse_dds_layers_test aiverse_texture_asset_test
ctest --test-dir build/texture-rhi --output-on-failure \
  -R '^(texture_contract|render_target_set|texture_streaming|texture_mip_residency|texture_resident_streamer|virtual_texture_pages|virtual_texture_page_table|virtual_texture_streamer|virtual_texture_disk_queue|virtual_texture_file_streamer|virtual_texture_prefetch|virtual_texture_feedback|virtual_texture_feedback_pass|hdr_texture|exr_texture|ktx2_layers|ktx2_transcode|dds_layers|texture_asset)$'
```

Build the editor and native client to catch API/layout regressions.
Run `vulkan_backend` on a real Vulkan device or Lavapipe **and inspect its
output for skipped execution**. Metal, GL33, GLES3, WebGL2 and GPU readback
require their own real device build/validation. Record each real test
command and result in the handoff after every stage; never claim passing
CI if it remains queued or cancelled.


### Per-axis material VT wrapping (Stage 10L)

- The normal `ModelVirtualTextureBinding` defaults to clamped U/V for
  compatibility; opt in with `binding.addressU/addressV =
  gfx::SamplerAddressMode::Repeat` or `MirrorRepeat` independently.
- Keep the original source texture sampler's U/V policy consistent with the
  virtual mapping and pack wrapped tile-edge gutters into the immutable archive.
  `ClampToBorder` is explicitly unsupported; a binding with that mode fails
  `valid()` and must use the non-VT fallback.
- The shader U6 remains 16 bytes, containing layer, gutter, U-address and
  V-address. Shared GLSL/MSL helpers keep legacy clamp-only overloads;
  the model renderer and Shader Graph VT node use the addressing-aware ones.
- The wrap policy participates in exact GPU material-group identity and hash.
  Verify GL/Metal/Vulkan UV seams and sampler parity during the deferred GPU
  validation phase. These additions are source-only, not GPU certified.


### VT immutable archive wrap-aware packing (Stage 10M)

- Generate periodic foliage/ground textures with `tools/virtual_texture_pack.py
  --address-u repeat --address-v repeat`, or use `mirror` independently
  per axis. The default remains `clamp`, including legacy output manifests.
- The packer applies address modes both to packed edge gutters and to
  odd-size mip generation. A non-clamp `manifest.json` records `addressU`
  and `addressV` strings, but the renderer does not automatically infer
  them from the manifest: set the same `ModelVirtualTextureBinding`
  `gfx::SamplerAddressMode` values through the game's resolver.
- Preserve immutable archive versioning and rebuild the full page archive
  when wrapping changes. Test real GLSL/MSL periodic seams during the
  deferred validation phase. Python packer tests for these modes are
  not yet executed/added; do not claim real GPU fidelity.


### Native runtime VT addressing propagation (Stage 10N)

- For native C++ TerraWeave archives, set
  `gfx::VirtualTextureArchivePackConfig::addressU/addressV` before
  `gfx::VirtualTexturePageArchiveWriter::pack`. Both axes default to
  `ClampToEdge`; choose `Repeat` or `MirrorRepeat` per source sampler.
- Packconfig address modes affect edge gutters, odd-size mip filtering,
  the immutable revision hash and the optional manifest fields. Do not
  reuse the prior content revision after changing wrap modes.
- `VirtualTextureArchiveVersion` carries these modes through
  TerraWeave's active session into both single- and multi-domain model
  sampling bindings; no separate per-model patch is needed.
- The Python packer only writes a manifest: a project using its pages
  must explicitly read/provide `addressU/V` to the typed version or
  resolver. There is no automatic manifest-to-version reader.
- `ClampToBorder` is unsupported and rejected. Native/GPU seam tests
  remain deferred; do not treat source-only integration as certified.


### Validated immutable archive manifest import (Stage 10O)

- Use `gfx::loadVirtualTextureArchiveManifest(directory, version, error)`
  before attempting to stream a prepacked Python or C++ page archive.
  The directory is caller-selected and must be trusted/immutable.
  The importer validates all expected page paths, byte sizes, tile/mip
  geometry and sampler U/V modes. It never mutates an active GPU atlas.
- `gfx::importVirtualTextureArchiveManifest(Json,...)` accepts an
  already parsed manifest and the same trusted archive directory.
  The engine's existing `engine/data/Json.h` handles parsing.
- The result is a typed `VirtualTextureArchiveVersion`; supply it to
  the normal TerraWeave session `prepare`/promote lifecycle. Do not
  infer content authenticity from manifest hashes alone.
- This is synchronous filesystem validation; do it during asset
  preparation, not on the render loop. Compile/CTest/GPU tests NOT RUN.


### Direct TerraWeave VT archive import (Stage 10P)

- Use `session.prepareFromArchiveDirectory(trustedDirectory)` for
  standalone sessions or `domains.prepareFromArchiveDirectory(domainId,
  trustedDirectory)` for multiple TerraWeave VT domains. This reads a
  Python/native `manifest.json`, validates page files and builds
  the candidate version; do not call every frame.
- On invalid input the current active atlas and domain stay unchanged.
  Read `session.lastError()` for diagnostics. The returned candidate
  is not active until normal bounded `pumpCandidate` /
  fallback-residency `promote` / GPU fence lifecycle.
- The active version carries U/V wrap policies into the model renderer.
  Never change a live archive in place or trust external path names.
- All C++/GPU validations for the new helper remain NOT RUN.


### GPU page-feedback address parity (Stage 10Q)

- Virtual page sampling and GPU feedback now share U/V addressing:
  `VirtualTextureMaterialMapping::addressU/addressV` follows
  `VirtualTextureArchiveVersion`, and the dedicated rigid/deformed
  feedback shader uniforms carry those modes.
- New `gfx::virtualTextureFeedbackPageAtUV` overload takes typed
  U/V modes. Old normalized-only overload remains for legacy code.
  Shader fragment helpers likewise have old and addressing-aware
  variants. Feedback uses unwrapped UV derivatives for mip selection.
- Rigid feedback U0 is now 192 bytes; deforming feedback U6 is 112.
  Do not reuse a binary shader/uniform contract compiled for the old
  lengths. Sample page IDs in linear RGBA8 without postprocessing.
- CPU visibility prefetch rectangles remain normalized; for wrapped
  large UV domains use GPU feedback or split regions before prefetch.
  No native compiler/driver tests have been run for this stage.


### Wrapped CPU virtual page prefetch (Stage 10R)

- Use `gfx::prefetchWrappedVirtualPages(streamer,planner,views,
  activeArchive.addressU,activeArchive.addressV)` when UV visibility
  crosses repeat/mirror boundaries. It splits at integer seams,
  bounds output to `planner.config().maxRegions` and reuses ordinary
  coarse-to-fine exact-page demand loading.
- `dispatch.wrapRejected` means no request was sent; reduce UV
  coverage, subdivide scene visibility, or use GPU feedback rather
  than continuing with partial incorrect pages. Clamp-only callers
  may retain the old `prefetchVisibleVirtualPages` API.
- The helper is genre independent and does not discover camera
  geometry. Do not mix multiple VT domain IDs in one planner.
  Compilation and runtime CPU/GPU validation have NOT RUN.


### Metal Shader Graph VT source slot expansion — Stage 10S

- Material graphs may author up to four independent source textures
  alongside `sampleVirtualTexture`. Metal stores graph textures in
  tex13..16, then virtual atlas/table in tex17/18 for the four-source
  case, using sampler s1 for the physical VT page and direct table read.
- The renderer computes/validates these slots with
  `supportsModelGraphSourceTextures` and
  `modelGraphVirtualTextureSlots`. GL33/GLES3/Vulkan retain their
  conservative 16-slot cap; graph VT uses source tex1 fallback when
  VT arrays do not fit, and the fourth graph source fails closed.
- Keep graphs out of experimental Metal ICB until its argument buffer
  supports per-graph source and virtual texture descriptors. This
  stage modifies source only: C++/MSL/GLSL compile and device tests
  have not run.


### Fragment texture capacity query (Stage 10T)

- Query `gfx::Device::maxFragmentTextureSlots()` before allocating
  material/VT texture slot layouts. It reports addressable fragment
  *textures*, not separate sampler states. Portable default is 16;
  Metal advertises 32, Vulkan advertises actual descriptor capacity.
- `supportsModelGraphSourceTextures` and
  `modelGraphVirtualTextureSlots` consume that query.
  MSL extended slots require the Metal backend; on GL/Vulkan,
  source tex16 fails closed and a VT graph with insufficient
  remaining texture resources uses its existing tex1 fallback.
- Never reuse high-slot GLSL variants on a device with fewer
  addressable textures, and do not claim Metal ICB resource-table
  support. C++/MSL/GL/Vulkan builds remain NOT RUN.


### Model Shader Graph pipeline cache identity — Stage 10U

- `Renderer3D::ModelShaderKey` requires equality across GLSL/MSL
  fragment+vertex sources, texture paths, material outputs, VT flag,
  shader domain and quality selector, not only the number of
  authored source textures. Avoid weakening this comparison when
  extending the VT resource ABI or Metal ICB grouping.
- GPU material state hashing is a prefilter, not a substitute for
  exact resource/version equality. Keep resource lifetime checks
  at execution after cache lookup.
- Native renderer builds/GLSL/MSL compilers and GPU parity NOT RUN.


### Frame-safe GPU material resource staging (Stage 10V)

- Native C++ consumers may include
  `engine/render/GpuMaterialResourceTable.h` and fill
  `GpuMaterialResourceBinding` with up to four Shader Graph
  texture handles, optional alpha coverage and exact VT resource
  binding. Use `GpuMaterialResourceTable::beginFrame(frameSerial)`,
  `intern`, `matches`, `resolve`, then `reset` on teardown.
- Resource tickets include frame serial+generation+index; never
  use a raw index after a new frame, device/scene shutdown or atlas
  revision promotion. All bindings are compared exactly after
  hash-bucket prefilter; a hash hit alone does not authorize reuse.
- This table stages CPU identity only. It DOES NOT bind a Metal
  argument buffer or permit mixed ICB textures; keep existing
  homogeneous fallback until the GPU/encoder ABI is implemented
  and tested. C++/GPU tests NOT RUN.


### Renderer3D Metal ICB resource ticket safety (Stage 10W)

- `Renderer3D::prepareGpuModelDraws` now stages a
  `GpuMaterialResourceTicket` for each accepted homogeneous GPU ICB
  group. It snapshots VT atlas/page table, layer, wrap/gutter and
  optional R8 alpha coverage. Full/invalid staging tables reject the
  ICB group and keep ordinary CPU fallback.
- `Renderer3D::renderModels` rechecks `GpuMaterialResourceTable::matches`
  for every submitted model, in addition to old exact VT/alpha
  compatibility. Stale generations and asset reloads cannot silently
  submit a reused GPU batch. `shutdownModels` resets the tickets.
- Do not claim per-command mixed Metal ICB resources: the new table is
  still CPU staging only. No native build/GPU shader validation RUN.


### Opt-in per-command Metal ICB VT fragment ABI (Stage 10X)

- RHI: `GpuIcbVirtualMaterial` contains VT atlas/page table handles,
  optional R8 alpha coverage and 16-byte U6 params. The optional
  `prepareGpuEncodedIndexedIcbWithVirtualMaterials` returns zero
  on unsupported backends and requires matching count/descriptors.
- Metal: under `AIVERSE_EXPERIMENTAL_ICB_PER_COMMAND_VT=1`
  *and* existing Metal GPU ICB flags, GPU encoded commands can point
  at per-command 4096-byte PBR argument-buffer slices and 256-byte
  U6 slices. VT resources are revalidated before execution.
- Renderer3D currently only calls this path for homogeneous groups
  with valid virtual textures; it has **not** relaxed grouping to
  allow mixed atlases. The old shared argument buffer and CPU fallback
  remain unchanged by default.
- This is not yet GPU-validated: native C++/Objective-C++, MSL
  compiler, actual Metal frame captures and perf tests NOT RUN.


### Mixed VT atlas groups in experimental Metal GPU ICB (Stage 10Y)

- Experimental only: both
  `AIVERSE_EXPERIMENTAL_ICB_PER_COMMAND_VT=1` and
  `AIVERSE_EXPERIMENTAL_ICB_MIXED_VT=1` are required, along with
  the existing GPU-authored ICB flags. Keep disabled in shipped games.
- `Renderer3D` now permits different VT atlas/table/layer/wrap
  bindings **only** when the underlying built-in PBR material GPU
  state, shader and geometry remain equivalent. Each command
  carries its own `GpuIcbVirtualMaterial` and
  `GpuMaterialResourceTicket`, and every entry is checked again
  before execution.
- `drawModelBatch` skips VT-homogeneity only for this
  prevalidated per-command GPU submission. CPU instancing/MDI
  must never inherit that exception. The first command's
  ordinary PBR materials remain shared, so Shader Graph and
  different conventional source textures are not mixed.
- No actual Metal/GL/Vulkan compile, GPU run, screenshot parity
  or performance evidence has been collected for this path.


### Mixed VT ICB diagnostics (Stage 10Z)

- Read `Renderer3D::gpuModelDrawStats().perCommandVtExecutions`
  and `.mixedVtExecutions` to see accepted GPU execution
  outcomes; these are not counts of prepared or attempted
  commands. A mixed submission must include at least two
  distinct VT resource bindings.
- Compare with `gpuIcbPrepared`, `gpuIcbRejected` and
  `gpuIcbExecuteFailed` when diagnosing CPU fallback.
  Still require Metal GPU capture and pixel A/B tests;
  source-level statistics do not prove shader correctness.
- Both experimental per-command/mixed flags remain
  release-disabled. Native and GPU tests are NOT RUN.


### Experimental Metal VT format gate (Stage 10AA)

- Per-command GPU ICB virtual materials must supply an
  `RGBA8` `Texture2DArray` physical atlas and page table.
  Optional full-resolution alpha cutout must be an `R8`
  `Texture2D`. The native Metal preflight also validates
  page-table virtual layer and gutter inset bounds.
- Unsupported formats and invalid handles reject the entire
  ICB preparation. Do not reinterpret HDR, depth or arbitrary
  compressed textures as VT page-table/physical resources.
  CPU fallback remains the safe path. GPU tests NOT RUN.


### Experimental per-command opaque PBR pipeline grouping (Stage 10AB)

- For experimental Metal ICB only, use
  `equivalentGpuIcbPbrPipelineMaterial` and
  `gpuIcbPbrPipelineHash` to classify built-in opaque, non-graph
  materials. They keep flags that change shader/pipeline variants
  equal, including normal-map presence. Do not use the hash alone to
  authorize grouping or use this path for masked/transparent/graph
  materials.
- `GpuMaterialResourceBinding` can store `pbrEnabled`, six
  source texture handles and 36 U2 floats. Exact resource tickets
  include these values and must be checked again before ICB use.
- This stage is CPU contract only; no GPU ABI or native tests run.


### Per-command Metal ICB built-in PBR resource ABI (Stage 10AC)

- The opt-in `GpuIcbVirtualMaterial` may carry
  `hasPbr=true`, six source TextureHandles (slots 1..6),
  and 36 U2 floats (nine vec4 material rows). All six
  handles must be valid; use renderer fallback textures
  for missing optional maps.
- The Metal ICB GPU kernel can set fragment buffer(3)
  to a 256-byte-aligned per-command U2 and use each
  command's PBR textures and sampler states through its
  4096-byte argument-buffer slice. Legacy groups preserve
  original shared U2 / fragment bindings.
- Renderer3D integration and hardware shader validation
  are still required; do not treat this as an enabled or
  verified heterogeneous ICB batch. Tests NOT RUN.


### Heterogeneous opaque PBR in experimental Metal VT ICB (Stage 10AD)

- Opt in to `AIVERSE_EXPERIMENTAL_ICB_MIXED_PBR=1` only
  with existing mixed-VT/per-command-VT/Metal GPU-ICB flags
  on a test device. Never enable by default in production.
- `packGpuPbrMaterialU2` packs the same nine float4 U2 rows
  as the ordinary renderer. `gpuPbrSourceTextures` resolves
  the six fragment samplers with standard fallbacks.
  The GPU command resource ticket includes exact U2+textures,
  VT atlas/page table/layer/wrap and optional alpha coverage.
- Pipeline grouping is conservative: opaque non-graph materials
  only, with matching shader-sensitive normal-map presence and
  structural flags. Masked/transparent and Shader Graph are
  excluded; CPU MDI/direct batching remains unchanged.
- All native builds, shader compiler/device GPU tests NOT RUN.


### Mixed PBR ICB host-submission counters (Stage 10AE)

- Check `gpuModelDrawStats().perCommandPbrSubmissions`
  and `.mixedPbrSubmissions` after the scene pass to see
  host-accepted per-command GPU ICB work. Mixed means the
  group actually had differing U2 or PBR texture handles.
- These counters do not prove GPU completion or visual
  correctness; still require Metal command-buffer status,
  GPU capture, screenshot A/B and performance evaluation.
- Flags are experimental OFF by default; tests NOT RUN.


### Non-VT per-command Metal PBR ICB ABI (Stage 10AF)

- `gfx::GpuIcbPbrMaterial` supplies six valid PBR source
  TextureHandles plus 36 finite float U2 values, without
  any Virtual Texture. `prepareGpuEncodedIndexedIcbWithPbrMaterials`
  returns zero outside explicit backend support.
- Metal requires `AIVERSE_EXPERIMENTAL_ICB_PER_COMMAND_PBR=1`
  and existing GPU ICB safety flags. It writes independent
  PBR argument buffers and per-command U2 and selects the
  compatible ICB fragment shader variant; no VT array/U6.
- Renderer integration is not yet enabled in this stage.
  Never claim native GPU validation; all tests NOT RUN.


### Renderer3D non-VT mixed opaque PBR ICB integration (Stage 10AG)

- For test-only per-command built-in PBR without virtual textures,
  set `AIVERSE_EXPERIMENTAL_ICB_PER_COMMAND_PBR=1` and
  `AIVERSE_EXPERIMENTAL_ICB_MIXED_PBR=1` (plus the existing
  `AIVERSE_GPU_AUTHORED_DRAWS`, Metal ICB safety flags).
  These are OFF by default.
- The renderer groups only opaque built-in materials whose
  pipeline traits match, sends their six actual/fallback
  TextureHandles and U2 to the optional non-VT RHI API,
  and compares exact per-command generation tickets
  immediately before GPU submission.
- VT groups still use `prepare...WithVirtualMaterials`
  and require the additional VT flag. Masked/transparent
  and Shader Graph material groups retain existing CPU
  or homogeneous GPU fallback; no release GPU validation.


### GPU ICB Shader Graph resource identity — Stage 10AH

- The optional RHI per-command PBR/VT material descriptor can hold
  `graphTextureCount=0..4` and corresponding valid source texture
  handles. Unused slots must be empty; graph sources require U2/PBR
  binding and cannot be supplied in base-only mode.
- Metal snapshots native 2D texture resources **and samplers** with
  each command. This does not yet mean shaders read those slots:
  fragment argument-buffer code and renderer integration follow.
- Maintain 16 native sampler limit; tex16 requires constexpr
  sampler if used. No C++/Metal/GPU tests RUN.


### Shader Graph Metal ICB shader argument layout (Stage 10AI)

- Only **fragment-only** graph shader variants may opt into
  `AIVERSE_SG_ICB_GRAPH`; no graph vertex output, deformation,
  transparent, cutout, palette, or unsupported shader permutation.
- Graph sources use tex13..16; tex16 must use constexpr sampler s16.
  Per-command VT arrays start at tex13+N/tex14+N, N=graph count.
  In the Metal ICB variant, fragment graph arguments are read
  from `IcbPbrTextures`, not direct texture/sampler function
  parameters; legacy source shaders are unchanged.
- Metal validates each graph source handle+native sampler against
  the captured descriptor and shader count. This is not yet an
  enabled Renderer3D graph batching path. No native/GPU tests RUN.


### Experimental fragment-only Shader Graph GPU ICB (Stage 10AJ)

- Opt-in: `AIVERSE_EXPERIMENTAL_ICB_SHADER_GRAPH=1`
  alongside per-command/mixed PBR flags and existing Metal
  GPU ICB gates. For VT graphs also enable the per-command VT
  gate. Default is disabled.
- Only opaque fragment-only material graphs (<=4 source
  textures, no vertex outputs or deformation) are eligible.
  The renderer groups graphs only if their actual compiled
  shader source/output mapping/texture count and structural
  pipeline traits match. Different source paths/textures are
  allowed when the shader program itself is equivalent.
- Every command stages its four-or-fewer graph source handles
  together with U2, PBR sources and optional VT identity.
  `GpuMaterialResourceTable` and Metal native resource checks
  reject changes since preparation. Vertex graph textures
  remain on the safe ordinary draw route.
- No C++/Objective-C++ builds, MSL compiler or Metal GPU
  execution tests have been run. Keep Draft and release OFF.


### Shader Graph Metal ICB host counters (Stage 10AK)

- `Renderer3D::gpuModelDrawStats().shaderGraphIcbSubmissions`
  counts host-accepted graph ICB submissions.
  `.mixedGraphSourceSubmissions` counts accepted groups where
  actual command-local graph source handles differ, not
  groups with only different PBR U2.
- Compare with `gpuIcbPrepared`, `gpuIcbRejected` and
  `gpuIcbExecuteFailed` for fallback diagnostics. These
  counters do not prove GPU completion or pixel correctness.
- All experimental graph and PBR flags remain disabled by
  default and have NOT been exercised on a Metal device here.


### Shader Graph ICB slot fail-closed policy (Stage 10AL)

- Check `Device::maxFragmentTextureSlots()` for graph
  resources at slots 13..16 and optional VT atlas/table at
  `13+N`/`14+N`. Renderer and Metal native preflight both
  reject unsupported layouts, retaining CPU fallback.
- Source-level layout inspection confirms N=0..4 MSL branches
  map to argument IDs 26/28 through 34/36, with no dynamic
  sampler 16. This does **not** substitute for actual Metal
  shader or GPU tests; leave graph ICB opt-in disabled by default.


### Vulkan MSAA runtime query loader (Stage 10AM)

- Under `VK_NO_PROTOTYPES`, every physical-device Vulkan query needs a function pointer entry in `AIVERSE_VK_INSTANCE_FUNCS`. The MSAA image-format query `vkGetPhysicalDeviceImageFormatProperties` was added after CI arm64 GCC failed at `VkResources.cpp:577`.
- GitHub CI from an older SHA diagnosed the issue. Post-fix build and CTest are pending. Do not claim CI is green based on a source-level fix.


### Metal Shader Graph 4th source sampler parity (Stage 10AN)

- In graph ICB, tex16 uses constexpr repeat/linear sampler s16, not a native sampler(16). The Metal texture allocation now records repeat/linear/compare flags; an incompatible fourth graph source fails ICB preparation and uses ordinary CPU drawing.
- Do not enable mixed Shader Graph ICB for clamp/nearest tex16 and expect pixel parity. Real Metal verification remains pending.


### Portable ICB material contract CTest (Stage 10AO)

- New test target `aiverse_gpu_icb_material_contract_test`, CTest name `gpu_icb_material_contract`. It exercises actual C++ headers for frame/generation tickets, source VT/PBR/graph descriptor validation and strict graph/pipeline compatibility.
- Run `cmake --build <build> --target aiverse_gpu_icb_material_contract_test` and `ctest --test-dir <build> --output-on-failure -R '^gpu_icb_material_contract$'`. This was registered but **not run** in the model's Linux environment.


### Texture RHI focused verifier now covers Shader Graph ICB (Stage 10AP)

- `tools/verify-texture-rhi.sh` builds
  `aiverse_gpu_icb_material_contract_test` and runs CTest
  `gpu_icb_material_contract` in both default and `--vulkan`
  modes.
- The test reads `ModelShaders.h` MSL source and audits all N=0..4
  graph-VT atlas/page-table argument layouts and the lack of
  dynamic sampler 16.
- Tested here: only GitHub source registration and branch-string
  consistency, **not** executable CTest nor the Metal compiler.
  Never record PASS for a test whose GitHub job is queued/cancelled.


## Stage 10AQ — aggregate CPU decode budget for container textures

When calling `render::uploadContainerTexture`, `uploadDdsImage` or
`uploadKtx2Image`, use `TextureUploadOptions::maxCpuDecodedBytes`
(default 512 MiB) to bound total retained decoded RGBA8 bytes across
**all** levels, faces, array layers and volume slices. Set it to zero to
reject CPU fallback entirely; increase only for trusted large assets.
Native compressed uploads bypass this CPU-specific budget. Failures occur
before any CPU-fallback texture allocation, with a descriptive error.
The earlier independent 512 MiB *per-mip* bound remains in place.

The registered CTest named **texture_asset** now includes cumulative-budget
boundary cases; their C++ execution is **NOT RUN** in this environment.
Do not infer native GPU decode safety or production readiness from source.


## Stage 10AR — sequential CPU mip uploads

The generic container uploader now decodes/uploads CPU fallback mip levels
sequentially rather than retaining every mip in RAM. The complete *logical*
chain is preflighted against `maxCpuDecodedBytes` first, so reduced peak
memory does not remove the source asset's safety budget. Each mip still
contains every array face/layer and 3D depth slice. If a later level cannot
be decoded or uploaded, the helper destroys the partially initialized GPU
texture before returning invalid. The RHI synchronous source-byte copy
contract is required. Actual C++/GPU tests of this refactor are NOT RUN.


## Stage 10AZ — Metal 3D texture upload selector

Native Metal `MTLTextureType3D` upload and subregion update require
`replaceRegion:mipmapLevel:slice:withBytes:bytesPerRow:bytesPerImage:`
(`slice:0`). Do not call a five-keyword selector with
`bytesPerImage:` but no `slice:`; that selector does not exist.
Keep `origin.z` and `depth` in `MTLRegionMake3D` for the selected
volume subregion. `bytesPerImage` is the stride between source depth
planes. Source fix `094bfe7c602fd57b8defa6bae2474a28cd04c3de`.

Previous Mac Studio run passed 23/23 focused texture CTests; its
`aiverse_editor` ObjC++ build failed on the two old selectors.
The fix is source-only until rebuilt on macOS. Native Metal volume
sampling/readback fidelity and GPU ICB pipeline checks are still needed.


## Stage 10BB — on-device Metal GPU acceptance (macOS, opt-in)

`tests/render/metal_texture_gpu.mm` tests the production Metal
`gfx::Device` against actual GPU command-buffer blit readbacks. It
verifies `RGBA8 Texture3D` 4×4×4 initial per-Z contents, partial
mip0 Z-plane updates, authored mip1 Z=1 content, invalid upload
bounds/pitch, plus 2DArray/Cube/CubeArray face+layer and authored
mip content, and texture-resource inventory lifetime.

Build target: `aiverse_metal_texture_gpu_test`; CTest:
`metal_texture_gpu`. Run on macOS only with:

```bash
git pull --ff-only origin feature/texture-rhi-unification
bash tools/verify-texture-rhi.sh --editor --metal --jobs 4
cat build/texture-rhi/verify-logs/metal-gpu.log
```

The new CMake option `AIVERSE_TEXTURE_METAL_GPU_TESTS` is OFF by
default, so ordinary `ctest` suites **do not register** the Metal GPU
fixture. `--metal` sets it ON for this invocation, and a later normal
verifier run forces it OFF again even with a reused CMake cache.
Normal source-only 23 CTests do **not** run this GPU test; the
`--metal` verifier mode adds a separate **fail-closed** CTest gate,
records exact device name and per-plane byte parity to
`metal-gpu.log`, and rejects absent Metal hardware/test binary
or GPU timeout rather than quietly skipping. Never claim this GPU
test passed based on previous successful macOS editor/CPU builds.
Its first native run is outstanding. Other capabilities (GPU ICB,
fragment shader sampling, virtual texture feedback page demand,
cross-platform parity) require separate hardware tests.


## Stage 10BD — Metal acceptance verifier syntax gate

An initial Mac Studio GPU attempt successfully built the Metal GPU
fixture, but a duplicated/truncated footer of
`tools/verify-texture-rhi.sh` caused an unmatched single quote and
prevented GPU CTest execution. The footer is repaired and the verifier
now preflights the whole file via `bash -n` before any CMake operation.
CI also checks shell syntax before running Linux and Vulkan jobs.

After pulling the texture branch, run
`bash -n tools/verify-texture-rhi.sh`. Because the native Mac test
binary has already built in the user's isolated worktree, run
`ctest --test-dir build/texture-rhi --verbose --output-on-failure
--no-tests=error -R '^metal_texture_gpu$'` directly to gather actual
GPU results without recompiling the entire editor. Full
`--editor --metal` verifier mode is also available. Do not claim
GPU tests passed until the CTest executed and its exact readback
comparisons are reported.


## Stage 10BF — measurable native Metal GPU phases

`tests/render/metal_texture_gpu.mm` now prints `TIMING` lines
for `Metal device_and_layer_init_ms`, per-Texture3D/array/cube
**create_and_upload_ms**, per-group `submit_ms`,
`callback_wait_ms`, `poll_frames`, `compare_ms`, optional
`gpu_command_ms`, and fixture total. These are wall-clock
timings unless `gpu_command_ms` is valid; the latter measures
the *entire* Metal readback command buffer (not pure blit throughput).
The former includes startup, drawables, semaphore waits and 10ms
polling. No latency threshold is enforced, no measured values
can be claimed until the instrumented fixture is run.

Compile the opt-in Mac GPU target and run CTest directly after
pulling the branch:
```bash
git pull --ff-only origin feature/texture-rhi-unification
cmake --build build/texture-rhi --target aiverse_metal_texture_gpu_test --parallel 4
ctest --test-dir build/texture-rhi --verbose --output-on-failure --no-tests=error -R '^metal_texture_gpu$'
```
Source commit `50656c4bd120755e6fa14bf8d2774aeb77b7e752`.


## Stage 10BG — sampled Texture3D fragment GPU acceptance

`tests/render/metal_texture_gpu.mm` additionally renders a full-screen
triangle using AIVERSE's actual Metal
`Device::createShader`/`createPipeline`/`setTexture` RHI and
`texture3d<float>` MSL fragment shader. It verifies base Z=0,
partial patch Z=2 at mip0, and explicit authored mip1 Z=1 through
render-target readback. Shader float-to-UNORM output accepts
at most ±1 LSB per channel; original native blit readbacks
continue to require byte-exact parity. Setup, draw/pass and GPU
errors are fail-closed. Source: `9e747cff974d6f44085cc25cfc841198a6f1c309`.

This is an extension of the optional `metal_texture_gpu`
CTest, not a new default CPU test. No post-change macOS run
has occurred. Run only the target after `git pull --ff-only
origin feature/texture-rhi-unification`, then verify the
individual GPU sampling PASS lines. Do not confuse this with
Shader Graph, GPU ICB or virtual page feedback acceptance.


## Stage 10BI — real Metal virtual-page feedback shader acceptance

The opt-in `tests/render/metal_texture_gpu.mm` fixture now checks
actual `VirtualTextureFeedbackPass` + canonical Metal feedback
MSL encoder/material helpers, not just the separately proven
Texture3D shader and blit readback. A fullscreen draw writes
RGBA8-encoded page IDs to the native offscreen feedback target;
actual `Device::readTextureAsync` and `VirtualTextureFeedbackDecoder`
must agree on mip/layer/XY and pixel frequency.

- valid UV `(0.8125,0.5625)`, mip 1, layer 2 of 8×8 tiles
  emits page (x3,y2,mip1,layer2) for all 16 pixels;
  priority=16, exactly one page, zero invalid IDs.
- invalid negative U emits 16 zero RGBA8 pixels and the decoder
  reports 16 invalid IDs and no page requests.
- Checks `VirtualTextureFeedbackPass::readyToSubmit()` after
  GPU command-buffer callback on the owner thread.
- Exact RGBA8 byte parity is mandatory; unlike sampled float
  texture colors, encoded IDs never permit ±1 LSB drift.

Only source code and docs have been committed; this new stage
has NOT been compiled or run on macOS. Source commit:
`5830d16f8531d462fa02709a0e6a0f8e5c4ef9f1`.
To run, pull the texture branch and rebuild
`aiverse_metal_texture_gpu_test`, then CTest
`-R '^metal_texture_gpu$'`. A passing result covers native
feedback encoding and decoding but **not** actual disk->GPU
physical atlas/page-table publication or experimental GPU ICB.


## Stage 10BK — real Metal VT atlas and page-table publication

The native opt-in `metal_texture_gpu` fixture has a new
`gpuPhysicalVirtualAtlas` test using the production
`VirtualTextureStreamer`, actual Metal Texture2DArray
physical atlas and GPU page table (no fake Device).
It checks pinned coarse residency, exact RGBA8 physical page
bytes, fine-mip override, and crucially **GPU table invalidation
BEFORE an LRU-reused physical slot can be overwritten**.
Then it checks the new page-table entry and physical bytes
after drain, explicit eviction fallback, and complete GPU
texture destruction.

Production data contract: RGBA8 page-table texel = 
`{physicalSliceLow, physicalSliceHigh, residentMip, 255}`.
Invalid texels are `{0,0,0,0}`. The test reads back
the full 4×4 GPU table and physical atlas slices, not only
the CPU cache's encoded table. It uses a two-page atlas,
a pinned mip-2 coarse page, an unpinned mip-0 detail page
and a third mip-0 page forcing LRU reuse.

Commits `d8b498d31d3b421261825048ab313bfefa13c6ee`
and `3ca4b1cf872bab4a06d533aa75c4052d036ea41b`.
The prior on-device VT feedback test passed 1/1 in 0.45s,
but this added physical page-table acceptance is **NOT RUN**.
Pull `feature/texture-rhi-unification`, build
`aiverse_metal_texture_gpu_test` and run
`ctest --test-dir build/texture-rhi --verbose --output-on-failure
--no-tests=error -R '^metal_texture_gpu$'` on the Mac Studio.
Next after GPU acceptance: file-backed disk->GPU worker path
and real shader page-table indirection/ICB as separate tests.


## Stage 10BM — native disk worker to Metal virtual atlas GPU acceptance

The opt-in `aiverse_metal_texture_gpu_test` now additionally runs
`gpuDiskBackedVirtualAtlas`, which uses actual
`VirtualTextureFileStreamer` plus the asynchronous
`VirtualTextureDiskQueue` worker and native Metal GPU
Texture2DArray atlas and page table. No fake GPU or synthetic
worker completion is introduced.

It writes RAII-cleaned temporary packed RGBA8 files in
`m<MIP>/l<LAYER>/x<X>_y<Y>.rgba` layout, then checks
the following lifecycle using bounded `pump()` polling,
completion ticket/key/status matching and exact GPU
`Device::readTextureAsync` bytes:

1. Coarse page `{2,0,0,0}` remains invisible before worker
   publication; GPU table bytes initially all zero.
2. Coarse page is pinned/resident after successful disk read
   and GPU page upload; full 4×4 table maps to slot 0/mip 2.
3. Missing page `{0,1,1,0}` returns one failure with a reason,
   leaving the existing atlas and GPU mapping healthy.
4. Fine page `{0,3,3,0}` uses physical slice 1 and only
   overrides the (3,3) GPU table cell.
5. New page `{0,0,0,0}` evicts the unpinned fine slice,
   preserves coarse slot 0, and republishes the new slot1 bytes
   and page-table mapping with no stale fine entry.
6. Shutdown destroys both GPU resources.

The test is not enabled for regular source CTest. It uses
the existing opt-in `--metal` hardware gate; the CMake test
target now explicitly links `Threads::Threads`. It has **not
yet been compiled or run after this stage's source changes**.
Source commits: `11ad74279ff2bbd348de16e4981efae645d7ab4c`,
`ebf5f1652c1284eaac6c7be5612def0a78e2c895`,
`e8ba85b16a51eaa8794b67f29e2a6ad8503d1893`.

To validate on Mac Studio after pulling
`feature/texture-rhi-unification`, configure the existing
build with `-DAIVERSE_TEXTURE_METAL_GPU_TESTS=ON`,
build `aiverse_metal_texture_gpu_test`, and run CTest
`-R '^metal_texture_gpu$'`. A source-only marker check
is not GPU acceptance. Next: fragment shader page-table
indirection and independent Shader Graph GPU ICB tests.


## Stage 10BO — shader-visible disk-backed page-table indirection

The opt-in macOS `metal_texture_gpu` fixture has a
`VtAtlasFragmentProbe` that **actually renders** the live
`VirtualTextureFileStreamer` atlas/page-table through the
canonical `gfx::kVirtualTextureMSL`
`aiverse_sample_virtual` helper. It uses real Metal
`Device::createShader`/`createPipeline`,
`beginRenderTargets`, `setTexture(0/1)`,
`setUniforms(0)`, `draw(3)`, and asynchronous
render-target `readTextureAsync` to check final RGBA8 pixels.

The probe validates fallback `{18,79,181,255}` before any page,
pinned coarse `{18,71,93,255}` after worker publication,
preservation after failed disk lookup, fine override
`{82,196,41,255}`, the unaffected coarse tile, and after
LRU replacement both old fine address -> pinned coarse and new
tile -> replacement `{220,44,16,255}`. Sampled float/UNORM
uses only 1-LSB/channel tolerance; direct blit checks stay
byte exact. All shader/pipeline/render/pass failures fail CTest.

This is a test of the *real* virtual page-table indirection
data path with the original production page streaming worker.
No new shader page lookup is implemented inside production code.
Source commit `32d69c8c6215ed470f9a1980bb212eb534eac1c5`.

**Prior Stage 10BN:** native Apple M3 Ultra disk worker→Metal
streaming PASS (1/1 CTest, 1.46 s). This does not verify the
new shader sample extension. Stage 10BO is **NOT RUN** as of
creation; test only after pulling into the isolated Mac worktree:
```bash
git pull --ff-only origin feature/texture-rhi-unification
cmake --build build/texture-rhi --target aiverse_metal_texture_gpu_test --parallel 4
ctest --test-dir build/texture-rhi --verbose --output-on-failure --no-tests=error -R '^metal_texture_gpu$'
```
If PASS, next distinct hardware gate is Metal Shader Graph
GPU ICB with heterogeneous source textures and correct
per-command binding; cross-platform tests remain separate.


## Stage 10BQ — separately opted-in Metal GPU ICB Shader Graph ABI

Experimental (NOT RUN) macOS-only hardware test:
`tests/render/metal_icb_graph_gpu.mm`,
CMake target `aiverse_metal_texture_icb_graph_gpu_test`,
CTest `metal_texture_icb_graph_gpu`.
CMake `AIVERSE_TEXTURE_METAL_ICB_GPU_TESTS=OFF` by
default; enabling `AIVERSE_TEXTURE_METAL_GPU_TESTS` alone
does NOT run the GPU ICB test.

CTest injects only for this isolated process:
`AIVERSE_EXPERIMENTAL_METAL_ICB=1`,
`AIVERSE_GPU_ENCODED_ICB=1`,
`AIVERSE_EXPERIMENTAL_ICB_PER_COMMAND_PBR=1`,
`AIVERSE_ICB_TRACE=1`.

Two real GPU-indexed ICB commands must draw different
4×4 RGBA8 left/right halves with distinct material
PBR texture(1), source graph texture(13) and per-command
U2 factor. A deliberately minimal MSL fragment argument
buffer matches Metal backend slot*2 resource IDs and the
`AIVERSE_SG_ICB_GRAPH` count marker; it does NOT run the
full production Shader Graph scene generator.
The test explicitly requires nonzero GPU ICB token,
successful native ICB execute, asynchronous GPU pixels,
correct two-tone output (<=1 LSB/channel).
No CPU draw fallback or silently skipped device.
Unsupported/unsafe native drivers must return failure.

Run only after user knowingly enables experimental ICB
on the *isolated* Mac texture-verification worktree;
see `docs/texture-rhi.md` Stage 10BQ for pull,
configure, build, CTest steps. First build and native
GPU result **NOT RUN** from this assistant environment.
Passing the regular `metal_texture_gpu` is not proof
of real GPU ICB Shader Graph resource binding.


## Stage 10BR — four-source Metal graph ICB hardware fixture (still NOT RUN)

`metal_texture_icb_graph_gpu` now runs **two** GPU-indexed passes:
one graph texture (slot 13) and four textures (slots 13–16).
Each pass requires nonzero native GPU ICB preparation token,
successful execute, and two different readback pixel colors
matching its independently calculated material/U2 oracle.
Graph slot 16 deliberately uses the production constexpr
repeat/linear sampler, with Metal argument IDs 26/28/30/32
for the source textures. Four-source textures must be repeat/linear.
No CPU draw substitution is accepted.

Source: `tests/render/metal_icb_graph_gpu.mm`,
commit `50969517f9fdd4a15b38103c378232459520d2a5`.
Commands and acceptance oracle:
`docs/texture-rhi.md`, Stage 10BR. **Not compiled or run**
on macOS by this agent. Stage 10BP was a distinct, successful
virtual-texture shader indirection test and does not validate ICB.
Do not enable GPU ICB in production or merge Draft PR #21.
After a real-device result, implement full Shader Graph compiler/
Renderer3D material acceptance, then test other backends.

## Stage 10BT/10BU — hardware ABI PASS, full Renderer3D graph test pending

**Observed Apple M3 Ultra Stage 10BT PASS** (user-supplied native log):
`metal_texture_icb_graph_gpu` 1/1 passed in **1.06 s**,
whole CTest elapsed **1.07 s**. Actual GPU ICB executed two
material draws and per-command RGBA8 pixel parity PASSED
for both `graph sources=1` and `graph sources=4`.
The separate fixture operation times were **240.116 ms**
and **151.826 ms**; these are not isolated frame times.
This confirms low-level resource ABI only.

**New Stage 10BU source-only integration gate**:
`tests/render/metal_renderer_graph_icb_gpu.cpp` is a separate
native macOS real `Renderer3D` test. It builds a four-source
Shader Graph via `render::graph::compile` from JSON (not
custom fixture MSL), uploads material textures and primitives
via `uploadModel`, renders a direct comparison and then
GPU ICB, checks nonzero mixed graph ICB counters and
compares captured scene pixels. It must show distinct
left/right materials even when parity is achieved.

CMake opt-in `AIVERSE_TEXTURE_METAL_RENDERER_GRAPH_GPU_TESTS=ON`;
default OFF. It builds `aiverse_metal_renderer_graph_icb_gpu_test`
and registers `metal_renderer_graph_icb_gpu`.
See `docs/texture-rhi.md` Stage 10BU for exact isolated
worktree configure, build and fail-fast CTest commands.

**Not run:** the new integration test's compilation,
MSL shader generation, Metal GPU execution or scene image parity.
Never promote 10BT's ABI PASS to a 10BU Renderer3D PASS.
Do not merge Draft PR #21 or enable GPU ICB by default
until actual native logs and cross-backend acceptance.

## Stage 10BV — full production Renderer3D graph ICB Apple M3 Ultra PASS

**Real hardware acceptance now observed**, not merely source-level:
the user built `aiverse_metal_renderer_graph_icb_gpu_test` and ran its
opt-in CTest `metal_renderer_graph_icb_gpu` on Apple M3 Ultra.
**1/1 PASS (3.73 sec test; total 3.74 sec)**. Diagnostics:
`eligible=2 candidates=1 prepared=1 submitted=1 mixed=1
gpu_commands=2 rejected=0 errors=0`; both ordinary and ICB
image regions were distinct and identical to each other;
`bad_channels=0 max_delta=0`. Production
`render::graph::compile`, `Renderer3D` shader injection,
mixed graph texture resource tickets, GPU ICB and live pixels
were exercised together. See `docs/texture-rhi.md` Stage 10BV.

**Remaining:** Shader Graph + disk-backed virtual texturing in
the same ICB group, separate archive page-table/layer and wrap
modes; LRU transitions; all excluded graph permutations,
Vulkan/GL/mobile fallbacks, and large-scene performance.
These are **not verified** by Stage 10BV.
Keep experimental GPU ICB flags and the risky CMake test
OFF in production; keep PR #21 Draft.

## Stage 10BW — production Shader Graph + disk VT mixed Metal GPU ICB (source only)

A new opt-in native macOS test exercises a *real compiled Shader Graph*
with **four source texture nodes plus `sampleVirtualTexture`** in the
same material using actual `Renderer3D` Metal GPU ICB. It reuses
`gfx::VirtualTextureFileStreamer`, `ModelVirtualTextureBinding`,
the canonical virtual shader lookup and the existing model resource table.
Never implement another VT page lookup or a second disk queue.

- Test source: `tests/render/metal_renderer_graph_vt_icb_gpu.cpp`.
- CMake option: `AIVERSE_TEXTURE_METAL_GRAPH_VT_ICB_GPU_TESTS=ON`
  (OFF by default).
- CTest: `metal_renderer_graph_vt_icb_gpu` in an **isolated**
  `build/texture-graph-vt-icb` configuration.
- Two independent temporary file archives create distinct physical
  atlases and page tables, with virtual layers 0/1 and different U
  addressing. Both material variants use the same compiled graph.
- The fixture captures ordinary vs GPU ICB Renderer3D RGBA8 images
  after pinned coarse pages, fine pages, and LRU replacement.
  Requires per-command VT, mixed VT, graph and mixed graph GPU ICB
  submission counters, actual image changes, coarse pinning and
  eviction, and per-channel parity within 2 LSB.
- Full fail-fast configure/build/CTest commands and exact PASS marker:
  `docs/texture-rhi.md` Stage 10BW.

**Native compilation, MSL shader generation, Metal execution,
on-device image parity and performance have NOT RUN** for this
new source. Stage 10BV's Apple M3 Ultra 1/1 PASS was **non-VT**
Shader Graph and cannot certify this new combined path.

On native failure, fix the first concrete compiler/driver/pixel
error in the same branch, commit the code, and append another
entry to `docs/agent-handoffs/texture-rhi-2026-11-18.md`.
Keep PR #21 Draft and all GPU ICB feature flags OFF in
production. Next after GPU PASS: missing/nonresident disk
fallbacks, wrap seam fidelity, GL/Vulkan/mobile non-ICB parity,
and representative stress/performance.

## Stage 10BX — hardware-proven Shader Graph + two disk VT domains + Metal ICB

**Mac Studio Apple M3 Ultra: real GPU acceptance PASS**, user-supplied
native `metal_renderer_graph_vt_icb_gpu` log: **1/1 PASS**, test
**4.57 s**, CTest elapsed **4.58 s**. Production Renderer3D,
compiled material Shader Graph with four ordinary source textures
and `sampleVirtualTexture`, two independent `VirtualTextureFileStreamer`
archives, per-command VT materials and heterogeneous GPU-authored
ICB executed for three page-residency states.

| State | Mixed graph/VT ICB submissions | Renderer3D pixel comparison |
|---|---|---|
| Pinned coarse | graph=1, mixedGraph=1, vt=1, mixedVt=1, commands=2 | bad_channels=0, max_delta=0 |
| Fine detail | same | bad_channels=0, max_delta=0 |
| LRU recycled | same | bad_channels=0, max_delta=0 |

`rejected=0 errors=0` in all three phases. The native
capture changed by **1392 channels** at fine-page publication
and **2886 channels** after LRU slot reuse, proving that
the test exercised actual page transitions and not a static image.
See `docs/texture-rhi.md` Stage 10BX for exact evidence.

**Still not verified:** nonresident/missing-file page fallback,
Repeat/MirrorRepeat sampling at UV seams outside [0,1],
multiple graph source counts, alpha/deformed graph variants,
GL/Vulkan/mobile/WebGL compatibility, performance and driver stress.
Never infer FPS improvement from the CTest duration. GPU ICB
and its opt-in risky CMake tests remain default OFF; PR #21 Draft.
For the next stage, reuse canonical page sampling and disk workers,
and create a separate acceptance suite with visual seam/fallback
oracles rather than rewriting the VT implementation.

## Stage 10BY — missing disk page, empty VT fallback and wrap boundaries (source only)

Native real-Metal, separately opt-in test:
`tests/render/metal_renderer_graph_vt_seam_gpu.cpp`;
CMake `AIVERSE_TEXTURE_METAL_GRAPH_VT_SEAM_GPU_TESTS=ON`
(default **OFF**), CTest `metal_renderer_graph_vt_seam_gpu`.
Use a dedicated Mac Studio `build/texture-graph-vt-seam` cache.

The fixture reuses production `render::graph::compile`,
`Renderer3D`, `VirtualTextureFileStreamer`,
`ModelVirtualTextureBinding`, and the canonical shader sampler.
It does **not** duplicate core VT/Shader Graph routines.

It requires a hardware GPU ICB readback for every state, and also
independent oracles to avoid identical mistakes by direct and
ICB paths: no-VT fallback pixels must exactly equal sampling an
empty GPU page table; deliberately missing disk files must fail
twice without changing pinned-coarse pixels; after fine tile(3,1)
publication, **Repeat** U=-0.125 and U=-0.0005 must select
fine on the left while **MirrorRepeat** selects coarse on the
right. At U=1.125 and U=1.0005 the selection reverses.
The test predicts which *actual image region* changes based on
the original raw RGBA8 page colors, compares RGB channel
directions, validates CPU page table bytes and requires mixed VT
and mixed graph GPU ICB submission counters. It rejects fallback
to ordinary GPU draws.

Source commits: `694d04531ffd5c1e81efe60b499a0ccd8e096d0d`
and `d95cdc388778a31de5b0f9900863cd28c2e7a737`.
See `docs/texture-rhi.md` Stage 10BY for exact pull,
configure/build/CTest and criteria.

**Native C++ compilation, MSL shader, Metal GPU execution,
missing-file callback, seam pixels and CTest have NOT RUN** for
Stage 10BY. The prior 10BX coarse/fine/LRU 1/1 GPU PASS is
independent evidence, not this stage's acceptance.
Do not merge PR #21 or enable experimental Metal ICB in production.

Remaining: nonuniform page-edge gutters with bilinear filtering,
cross-platform Vulkan/GL/mobile and WebGL, shader counts 0–4,
stress and real GPU frame-time benchmarks.

## Stage 10BZ — native Clang name collision corrected (retest pending)

The first Stage 10BY macOS build failed at four
`metal_renderer_graph_vt_seam_gpu.cpp` call sites because a
fixture helper named `detail` collided with namespace
`aiverse::detail` from `Fixed.h` through
`using namespace aiverse`.
Source commit `8fe7265f2124539a36e388255acd1d9fc5ee3887`
renamed the helper and all invocations to `finePageKey`,
preserving the virtual page tuple (0,3,1,side).
See `docs/texture-rhi.md` Stage 10BZ and append-only
`docs/agent-handoffs/texture-rhi-2026-11-18.md`.

The source fix is committed, but the new test has **not**
been compiled or run successfully yet. The previous
Stage 10BX PASS is a different GPU test. In the isolated
Mac Studio texture worktree, pull the latest branch, then
build the seam target with `set -euo pipefail` before CTest.
Do not report Stage 10BY hardware PASS without the actual
missing-page and Repeat/MirrorRepeat oracle output.

## Stage 10CA — Apple M3 Ultra missing VT page and UV seam oracle PASS

**User-supplied real-device result:** the corrected Stage 10BY/10BZ
`metal_renderer_graph_vt_seam_gpu` ran on the Mac Studio's Metal
device and **PASSED 1/1 (5.65 sec, total CTest 5.67 sec)**.
All seven production Renderer3D graph+VT direct/ICB RGBA8
capture pairs had `bad_channels=0 max_delta=0`.
Each GPU ICB pair submitted a real heterogeneous Shader Graph
and VT batch: `graph=1 mixedGraph=1 vt=1 mixedVt=1 commands=2
rejected=0 errors=0`.

Independent **expected-page** validation also PASSED:
empty page table equaled the no-resolver fallback exactly,
coarse publication changed 23,304 channels, missing disk files
triggered two expected failure callbacks while preserving
pinned-coarse pixels exactly, and Repeat/MirrorRepeat page
selection was correct for U=-0.125, +1.125, -0.0005 and
+1.0005 on both ordinary and ICB captures (**8/8 oracle PASS**).
The non-target region did not change.

These are real GPU acceptance results, not source-only
verification. Source was unchanged after the hardware PASS.
See `docs/texture-rhi.md` Stage 10CA and the agent handoff.

**Next:** test true bilinear seam/filter fidelity over
nonuniform source pixels and guttered physical pages
with an independent CPU reference, then cross-backend
Vulkan/GL/WebGL/mobile acceptance and performance stress.
Solid-color pages and zero gutter do not establish
nonuniform filtering continuity. Experimental ICB remains
OFF by default and PR #21 Draft.

## Stage 10CB — physical-gutter bilinear VT sampling and CPU oracle

**Production shader fix**
`engine/gfx/VirtualTextureSampling.h`,
`1af66f86a754958f6bacf84d279b14a163fca831`:
the old atlas `inset=(gutter+0.5)/pageSize`
prevented a bilinear filter from reading neighboring gutter
pixels at the virtual tile border. Both the canonical GLSL
and MSL samplers now use `inset=gutter/pageSize` for
**positive gutters**, so tile edges receive the correct
half-texel footprint and neighboring gutter data.
When `gutter=0`, they retain the old `0.5/pageSize`
edge-centre clamp to preserve previous acceptance behavior.

**New isolated native Metal test**:
`tests/render/metal_vt_bilinear_gpu.mm`,
`4787302de05704c2c48e7c9fede9a4813b5d2a33`.
It loads two independent 12×6 logical textures as disk-backed
8×8 physical pages (6×6 interior + **1 texel gutters**)
with nonuniform RGB gradients and Repeat/MirrorRepeat
addressing. Real `VirtualTextureFileStreamer` publishes
pages to native Metal atlases and RGBA8 page tables.
The GPU shader embeds the actual `gfx::kVirtualTextureMSL`.
An independent CPU oracle interpolates **original logical
texels** at half-texel coordinates and applies source
addressing; it does not consult physical GPU page mapping.
136 real GPU fragment samples cover internal seams,
outer wraps, negative UV, V edges and points outside [0,1].
Expected max difference ≤2 RGBA8 levels. The fixture
explicitly asserts that old center-inset mapping would fail.

CMake `AIVERSE_TEXTURE_METAL_VT_BILINEAR_GPU_TESTS=ON`
(default OFF), executable `aiverse_metal_vt_bilinear_gpu_test`,
CTest `metal_vt_bilinear_gpu` in separate
`build/texture-vt-bilinear-gpu` cache. Build registration
commit `f2778b19f29817b94c48e73ed65c6465fbf86a27`.
Exact Mac Studio commands / PASS oracle in
`docs/texture-rhi.md` Stage 10CB, guide commit
`57d6b55187e6e2f2903df97194d60a772e6cb450`.

**Not yet executed on Apple GPU:** source compile, Metal
shader compilation, actual 136 samples or CTest 1/1.
Do not claim Stage 10CB GPU PASS based on source-level
or mathematical review. The previously observed 10BX and
10BY tests were on solid-color pages with zero gutter
and cannot validate this new production shader change.

After native success: rerun Stage 10BX/10BY zero-gutter
regressions, add a full production `Renderer3D` graph/VT
GPU-ICB scene with nonuniform gutters and independent
image oracle, and validate Vulkan/GL/mobile, trilinear/
anisotropic filtering and real driver/perf behavior.
All experimental ICB defaults remain OFF; PR #21 Draft.

## Stage 10CC — real-device nonuniform 1-pixel gutter bilinear PASS

The user ran the new `metal_vt_bilinear_gpu` test
on Apple M3 Ultra after the canonical shared
`VirtualTextureSampling.h` Metal/GLSL inset correction.
**1/1 CTest PASS** (6.07 s test; 6.08 s full CTest),
`samples=136 seam_samples=120 max_delta=1`,
Metal shader pipeline failures=0. All GPU RGBA8
samples agreed to within one 8-bit channel level with
an **independent original-logical-texel CPU bilinear
reference**, including nonuniform physical gutters,
inner tile seams, outer Repeat/MirrorRepeat boundaries
and U/V edge coordinates.

This is **actual hardware proof for the canonical Metal
virtual sampler**, not a full Renderer3D ICB acceptance
or a Vulkan/GL pass. Do not call CTest wall duration
GPU render time. The reciprocal GLSL code still needs
independent platform validation.

The Stage 10BX and Stage 10BY graph+disk VT ICB tests
passed **before** the shared shader fix; rerun and
rebuild both on the new head before claiming post-fix
zero-gutter regression freedom. Use the exact existing
build-directory commands in `docs/texture-rhi.md`
Stage 10CC, then implement full production Renderer3D
nonuniform-gutter graph+VT mixed Metal ICB pixel
oracle and trilinear/anisotropic validation.
Do not merge Draft PR #21 or enable ICB by default.

## Stage 10CD — one-shot full Texture RHI acceptance and nonuniform production scene

**New full Renderer3D/graph/VT native Metal ICB test:**
`tests/render/metal_renderer_graph_vt_gutter_gpu.cpp`
(source commit `1c4a3be68f829775c5af4894c1e15f1d265a08e0`).
It runs the *actual* JSON graph compiler with four regular
texture sources plus `sampleVirtualTexture`, two independent
disk-backed VT atlases and page tables, and per-command
heterogeneous Metal GPU ICB materials. Physical pages are
8×8 RGBA8 with 6×6 nonuniform source interiors and
one-neighbor-texel gutters on each side. For U=0.40,
0.48, 0.50, 0.52, 0.60 at V=0.40, direct and ICB
full RGBA8 images must match within 2 LSB/channel.
An independent *original logical texel* bilinear oracle,
calibrated to the Renderer3D endpoint transfer, checks
both paths' intermediate ROI colors independently (≤3
displayed levels) and scene-channel monotonicity. It is
a lighting-transfer-adjusted scene oracle, not a substitute
for the separately passed **absolute** low-level 136-point
Stage 10CC CPU reference test.

CMake `AIVERSE_TEXTURE_METAL_GRAPH_VT_GUTTER_GPU_TESTS`
**OFF by default** adds native CTest
`metal_renderer_graph_vt_gutter_gpu`
(commit `ed9a707e0f48102bd60c1b0640fd408cfb384325`).

**One-shot Mac Studio suite:**
`tools/texture-rhi-final-verify.sh`
(commits `04e9612189b9e10e4a218bce326af405bc8ddd37`,
`86254fca82f0866005fbd2508872f7ca05ede7d4`).
After pulling the branch in the isolated texture worktree,
run `bash tools/texture-rhi-final-verify.sh --jobs 4`.
It compiles/runs 21 portable texture/VT/Shader Graph
CTests plus optional page pack/shader syntax tests,
then builds all seven risky native Metal GPU targets
and executes them serially in separate processes.
`--full-engine` additionally builds/runs the full
nonexperimental AIVERSE default CTest suite.
Any compile/test failure stops the suite. Exact native
GPU success markers are checked and
`build/texture-final-logs/acceptance.txt` with source
SHA only appears at the end of a fully passing run.
See `docs/texture-rhi.md` Stage 10CD for full commands.

**Truthful status:** Neither the new production gutter
GPU-ICB fixture nor the unified one-shot Mac suite has
been run on hardware by this agent. Prior 10CC 136-point
bilinear sampling passed on Apple M3 Ultra and Stage
10BX/10BY older zero-gutter ICB scenes passed before
the gutter shader change. Do not mark this new gate
green until new user-provided one-shot logs show PASS.

Remaining platform/mip/perf limits: GL/Vulkan/WebGL/mobile
native validation, trilinear mip transitions, anisotropy,
long-running streamed page churn, memory ceilings and
representative GPU timing. Do not merge Draft PR #21
or turn on experimental GPU ICB by default based only
on a Metal smoke test. After each fix, commit code and
append a new agent handoff with actual test evidence.

## Stage 10CE — restored one-shot Mac Texture RHI test runner

Post-push GitHub source inspection discovered a malformed
conditional regex and duplicate code in the first optional
VT packing/shader check update to
`tools/texture-rhi-final-verify.sh`. This was repaired
**before native testing** in source commit
`6a6bb9f2a18cb494a93d44eb232907c0df2ad8fe`:
the 142-line runner now has exactly one safe-build path,
one optional Python shader/packer test selector, one
`--full-engine` path, and one sequential seven-test
native Metal CTest loop. See `docs/texture-rhi.md`
Stage 10CE for exact validated invocation.

Run `bash -n tools/texture-rhi-final-verify.sh` on
the isolated Mac Studio worktree after pulling the
feature branch, followed by
`bash tools/texture-rhi-final-verify.sh --jobs 4`.
For every nonexperimental engine CTest, append
`--full-engine`. Only a zero exit and complete
real native GPU logs count as an acceptance PASS.
This runner **has not yet been executed** on a Mac.

## Stage 10CF — support detached HEAD Mac Studio validation worktrees

The first native unified test invocation was rejected by
`tools/texture-rhi-final-verify.sh` before compilation
because the user's isolated worktree is intentionally
in detached HEAD. Git fetch + fast-forward had succeeded;
the old script checked only the local branch name.
Fix commit `1fd7dcba4b16cbf72c64d0f6c81c5be9415c23ce`
permits detached HEAD **only when its exact commit equals**
`refs/remotes/origin/feature/texture-rhi-unification`.
It still rejects stale or unrelated revisions, absent
remote-tracking refs and other local branch names.
Guide commit `f3018a40c4bab2f727f3f89d31b68618f1875646`
explains the reason and exact retry commands.

The corrected runner is **NOT yet native-tested**.
After `git fetch`, `git pull --ff-only` and
`bash -n tools/texture-rhi-final-verify.sh`, run
`bash tools/texture-rhi-final-verify.sh --jobs 4`.
Do not check out a branch merely to satisfy this script.
Only a complete real macOS build/CTest/GPU log and
`build/texture-final-logs/acceptance.txt` ending
`result=PASS` establish final texture acceptance.

## Stage 10CG — allow verified task branch names in one-shot Mac suite

User's existing isolated worktree is on local branch
`verify-texture-rhi-20261119-150819`, and a successful
`git fetch`/`git pull --ff-only` fast-forwarded it to
the exact feature remote commit. The former preflight
guard still rejected that *name* before compilation,
despite correct source identity.

Commit `e7e012b01c6599c85c94f24f4a85197e4a616cf4`
fixes `tools/texture-rhi-final-verify.sh`:
only equality of the actual HEAD SHA to the fetched
`refs/remotes/origin/feature/texture-rhi-unification`
SHA determines source validity. Task-specific local
branches, the named feature branch and detached HEAD
are all allowed at this exact SHA. Any stale/other commit,
or absent remote tracking ref, remains rejected.
The runner removes stale `acceptance.txt` at start so
only a **new complete** PASS persists.

Guide: `docs/texture-rhi.md` Stage 10CG.
**The native 21-portable + 7-real-Metal final run is
still unverified** after this change; do not call it
PASS until the user supplies a successful Mac log.
Do not switch branches just to satisfy the runner.

## Stage 10CH — do not trust CTest PASS with native FAIL assertions

The first **real user** unified one-shot Metal run reached
`metal_renderer_graph_vt_gutter_gpu` and observed all five
nonuniform-gutter GPU ICB draw phases submitted correctly
(`graph=1 mixedGraph=1 vt=1 mixedVt=1 commands=2
rejected=0 errors=0`); direct/ICB full-frame parity
was byte-identical in all five pairs. However, the
fixture emitted **10 FAIL lines** for distinct source
material regions: its inherited solid-color delta
threshold of 35 exceeded actual nearby-gradient ROI
deltas of 16–19. The scene-level independent CPU
bilinear oracle was consequently skipped. AppKit
terminated with process code 0, so raw CTest incorrectly
reported `1/1 Passed`. The one-shot runner correctly
stopped on the absent required final success marker;
`acceptance.txt` was intentionally absent.

**Fixes:**
- `7a50a4c82462407596195b4dc1ea748af9545c3e`:
  new test uses threshold >12 for these actual nearby
  source gradients while retaining original-source
  independent bilinear ROI oracle, nontrivial channel
  gradient and full RGBA8 parity checks. On any failure,
  `callbacks.shutdown` uses `std::_Exit(1)` after
  cleanup so macOS does not turn assertion failure into
  exit status 0.
- `035b6b81c8126a0ea9478ac5b194f9cf3b11561c`:
  CMake adds `FAIL_REGULAR_EXPRESSION "FAIL:"` to
  all seven registered native Metal GPU CTests; the
  guttered production scene also requires its final
  exact PASS output through CTest.
- `65b32f968ed2089c313d51cfb1e3fe89df850069`:
  the one-shot shell runner greps every native GPU CTest
  verbose log for `FAIL:`, independently of CTest's
  process exit status.
- `465967cbfd84384b768c62e68d5adc71e39e4d8a`:
  failed one-shot runs create
  `build/texture-final-logs/failure.txt` containing
  failing SHA, exit code, logs directory. No
  `acceptance.txt` exists without a fully green run.
- `290b2532cde3d43a3410cb3ef82ef0cff30bc6d7`:
  `docs/texture-rhi.md` Stage 10CH documents exact
  real Mac evidence and new fail-fast retry.

**Native verification after fix remains PENDING.**
Existing CTest's `1/1 Passed` with FAIL assertions is
**NOT** a valid Stage 10CD success. After pulling latest
feature SHA on the existing task-named verification
worktree, run `bash -n tools/texture-rhi-final-verify.sh`
and `bash tools/texture-rhi-final-verify.sh --jobs 4`.
Check that the full nonuniform scene independent CPU
oracle prints a real PASS and that all native GPU
CTest logs contain no `FAIL:` diagnostics.

## Stage 10CI — unified native Texture RHI GPU acceptance PASS at 7494f32d

**Actual one-shot Mac Studio/Apple M3 Ultra result:**
the user ran `bash tools/texture-rhi-final-verify.sh --jobs 4`
on tested SHA `7494f32d313a2ac45050be02d25ca3ae97749086`.
The fail-fast runner finished with `result=PASS`
and its manifest reported **21 focused portable tests
and seven native, separate-process Metal GPU tests**.

The new production `metal_renderer_graph_vt_gutter_gpu`
(CTest #128) itself reported **1/1 PASS, 1.90 sec**.
For U=0.400/0.480/0.500/0.520/0.600,
`graph=1 mixedGraph=1 vt=1 mixedVt=1
commands=2 rejected=0 errors=0`.
Every full-scene ordinary vs GPU ICB readback
had `bad_channels=0 max_delta=0`, and the
independent original logical-source, endpoint-calibrated
scene bilinear oracle passed with **max interpolation
error 1 displayed RGB level** on both paths.
The expected nonuniform-gutter, mixed ICB
success marker was printed, with **zero FAIL** lines.
This corrects the earlier false CTest success concern:
the inspected tested SHA contains ROI threshold 12
and the failure-to-exit-status fix.

Read `docs/texture-rhi.md` Stage 10CI for exact log
and proof limits. This is **real native device correctness
verification**, but **NOT** a full-engine-wide run:
the manifest explicitly says `full_engine=0`.
Do not infer Vulkan/GL/WebGL/mobile, trilinear/aniso,
large streamed page stress or FPS. The 1.90 seconds
is CTest wall time, not GPU frame duration.
After these documentation commits, HEAD will differ
from tested 7494f32d; continue to refer to the
actual tested source SHA in reports.

Keep PR #21 Draft pending any desired whole-engine
regression, cross-backend and driver/performance
acceptance. All experimental GPU ICB defaults OFF.

## Stage 10CJ — canonical VT GLSL and Metal compiler gates

The current branch now has `tests/render/check_virtual_texture_sampling_shaders.py`
(commit `6d0d9a8d3ebec7f57c9c4657c2925d6460667b8e`) to
compile the **real embedded VT source code** from
`engine/gfx/VirtualTextureSampling.h`. It exercises both the
address-aware and legacy sampler overloads in complete
OpenGL 3.3 and GLES 3.0 fragments with `glslangValidator`,
and Metal MSL using `xcrun metal` where installed.
Missing requested compilers or broken code **fail** the
test, not an empty skipped PASS.

CMake now registers `virtual_texture_sampling_glsl`
**only with Python+glslangValidator** and
`virtual_texture_sampling_msl` **only with
Python+Apple+xcrun**. The dedicated Linux OpenEXR ON/OFF
and Vulkan CI jobs install `glslang-tools` and run the
GLSL CTest explicitly with `--no-tests=error`.
Toolchain-backed checks were incorporated into both
`tools/verify-texture-rhi.sh` and the previously
hardware-PASS one-shot
`tools/texture-rhi-final-verify.sh`.

Commits for the corrected final versions:
`20c79da8473abf99f56eedcc12e6f2e3fc1547bc`
(CMake registration),
`3a222231828d432a9cb04d4b7d9a69124fb2e2e6`
(focused script),
`3a1a17b1fa061314398a21f40fedb6b69e5cb9a1`
(one-shot Mac script) and
`f40cbef2a14ce5df92daa9cb9152811a7333a57f`
(Texture RHI workflow).

**Validation boundary:** static GitHub source inspection was
done. Actual new GLSL 330 / GLES 300 or Metal toolchain
compilation, full-engine CTest or Vulkan/GL runtime readbacks
have **NOT yet been observed** for this stage. Do not
misrepresent the earlier Apple M3 Ultra **21+7 GPU PASS**
at source SHA `7494f32d` as tests on these newer commits.
The new GLSL test is a compile-only guard, **not**
evidence of GL/GLES GPU pixel parity.

To continue, run the isolated Mac Studio
`bash tools/texture-rhi-final-verify.sh --jobs 4 --full-engine`
after fetch/pull and `bash -n` script checks.
Review CI explicit GLSL CTests and whether Vulkan
GPU tests actually execute instead of SKIPping.
Then build native GPU-rendered Vulkan/GL fallback
framebuffer oracles, nonuniform mip LOD/aniso tests,
stress/budget tests and GPU timing.
Never merge Draft PR #21 or enable experimental
ICB by default without deliberate release gating.

## Stage 10CK — explicit gradient virtual texture sampling (source SHA ee63acda)

- Use `gfx::kVirtualTextureGLSL`: `aiverseSampleVirtualGrad(atlas, table, uv, layer, gutterPixels, fallbackColor, uvDx, uvDy, addressU, addressV)` or its no-address clamp convenience overload. Metal counterpart: `aiverse_sample_virtual_grad(atlas, table, sampler, uv, layer, gutterPixels, fallbackColor, uvDx, uvDy, addressU, addressV)`.
- Derivatives `uvDx` and `uvDy` are **original virtual UV** gradients; do not differentiate `fract`, the page lookup, or physical-page-local UVs. Runtime transforms the derivatives using resident fallback mip, physical-gutter inset and wrap/mirror orientation.
- Existing implicit-derivative entrypoints remain in place. The new gradient variants are **opt-in source only** until native compiler/GPU tests verify them.
- This is not automatic anisotropic/trilinear VT: current physical atlas has one mip per slice and page-table stores one nearest resident parent. Build explicit multi-mip/page transitions and real backend pixel tests in subsequent stages.
- Stage 10CK handoff appended to `docs/agent-handoffs/texture-rhi-2026-11-18.md`. Latest actual Mac 21 portable + seven Metal GPU tests were on older `7494f32d`; full-engine, new GLSL/MSL compile, Vulkan, GL, GLES, WebGL and mobile tests still **NOT RUN** on new SHA. Keep PR #21 Draft and experimental GPU ICB disabled by default.

## Stage 10CL — multi-mip physical page streaming (source SHA ccb0d391)

- Set `VirtualTextureStreamConfig.physicalMipLevels` 1..full chain; defaults to **1**. The atlas is a Texture2DArray with actual mip allocations.
- Existing `VirtualTextureStreamer::stage(key, rawGutteredRGBA8, bytes, pin)` computes physical mips with premultiplied-alpha RGBA reduction and sRGB decode/re-encode for `RGBA8_sRGB`, then queues them atomically for publication. For authored precomputed physical mip arrays, use `stageMipChain(key, std::vector<VirtualTextureMipView>, pin)` with the exact level count.
- The ticket returned from either stage call identifies an entire mip batch for `cancel(rootTicket)`; all mips must finish before page table publication. `drain()` counts mip updates, callbacks count **completed virtual pages**.
- Existing `VirtualTextureFileStreamer` and disk page archives remain base-level files and work without API changes, with optional mip generation at GPU staging.
- Not yet compiled or GPU-tested. Still need second virtual-mip mapping, shader blend, real backend readbacks, and final full engine validation. No changes to `main`; PR #21 remains Draft.

## Stage 10CM — opt-in indexed virtual mip table and shader trilinear

- `VirtualTextureStreamConfig.indexedMipTable` defaults to false. When true, `VirtualTextureStreamer::indexedMipPageTableTexture()` is the ONLY table handle for new shader ABI, with `indexedMipTableLevels()` virtual levels. Legacy `pageTableTexture()` returns invalid in this mode to fail closed.
- The indexed RGBA8 Texture2DArray page-table physical slice is `logicalLayer*mipLevels+requestedMip`. Each RGBA8 entry resolves nearest READY requested mip or COARSER resident page; root must be resident or sampler returns fallback. See `VirtualTexturePageTable::entryForMip` for CPU inspection.
- GLSL: `aiverseVirtualMipLod(uvDx, uvDy, virtualSizePixels, bias)`, then `aiverseSampleVirtualTrilinear(atlas,indexedTable,uv,logicalLayer,gutterPx,fallback,uvDx,uvDy,lod,virtualMipLevels,addressU,addressV)`. Metal equivalents: `aiverse_virtual_mip_lod`, `aiverse_sample_virtual_trilinear` with separate sampler argument.
- Both shaders have clamp-only convenience overloads and explicit-gradient page sampling. Use ORIGINAL virtual UV gradients and provide logical virtual pixel dimensions, not atlas slice dimensions.
- No default renderer auto-binding or GPU test yet. Existing materials must continue to bind the one-slice legacy table; indexed ABI must opt in. 32MiB CPU page-table cap and backend array layer limits are release constraints. Source SHAs `93f4cbb` + `371812a`. PR #21 Draft; tests later.

## Stage 10CN — model/graph virtual trilinear binding (source fe3b78bc)

- For built-in model colour and Shader Graph `SG_SAMPLE_VIRTUAL`, the existing `Renderer3D::setModelVirtualTextureResolver` may now supply a `ModelVirtualTextureBinding` with `indexedMipTable=true`. Set `pageTable=streamer.indexedMipPageTableTexture()`, `physicalAtlas=streamer.physicalAtlas()`, `virtualMipLevels=streamer.indexedMipTableLevels()`, plus `virtualWidthPixels`/`virtualHeightPixels` derived from real virtual pixel resolution excluding gutters, and optional `virtualMipBias`.
- Separate `kKeyModelVirtualTrilinear` shader permutation; 32B U6 payload only for new variant, existing 16B U6 remains unchanged. GLSL/MSL built-in colour and graph macro share `modelVirtualSampleExpression` so both sample requested virtual mip floor/ceil and blend resident fallbacks.
- Texture slots still atlas+table, no new slots; GL/Vulkan slot hard limit and Metal graph slot budget preserved. Metal GPU-ICB graph path intentionally excluded from indexed mode pending tests.
- No tests run for this stage; do not promote to production until C++ and native GLSL/MSL shader compilers and actual framebuffer oracles pass. PR #21 Draft.

## Stage 10CO — binding archive metadata without manual guesses

Include `engine/render/ModelVirtualTextureArchiveBinding.h` in the caller's model VT resolver. Use `makeModelVirtualTextureArchiveBinding(publishedArchive,fileStreamer,layer,out,mipBias)` or overload with `VirtualTextureStreamer`; this checks archive and active streamer geometry and fills handles, indexed status, virtual resolution/gutters/addressing. When creating a matching streamer: `archive.streamConfig(physicalPages,true,physicalMipLevels)`; prior one-argument callers remain unchanged. This source was not yet compiled or GPU-tested; do not assume it is certified.

## Stage 10CP — revision-gated indexed page-table refresh (SHA 9eabd011)

`VirtualTexturePageCache::mappingRevision()` changes on actual mapping/READY state changes, not touches/pins. `VirtualTexturePageTable::rebuild(cache)` now skips identical revisions and scans coarse-to-fine once to resolve all requested mip slices per base tile: O(baseCells * mipLevels). GPU dirty-row flushing and legacy ABI unchanged; still requires GPU and CPU test oracles. Model archive helper now guards gutter multiplication overflow. Do not claim benchmarks; none run.

## Stage 10CQ — incremental virtual table refresh (SHA 6bfa3872)

The page cache now exposes `mappingChangesSince(revision,out,maxKeys=128)`, backed by bounded 512-event history. Page-table `rebuild(cache)` skips unchanged revisions, refreshes only small changed mip-zero rectangles, and falls back to a full rebuild for large changes or truncated history. Still requires CPU reference oracle and GPU hardware tests; none have been run for this SHA. Keep release gates explicit.

## Stage 10CR — indexed table GPU initialization (SHA d64ba9eb)

`VirtualTexturePageTable::createGpuTexture(gpu,true)` allocates empty array slices and retains all dirty rows; `VirtualTextureStreamer::initialize` automatically runs bounded `synchronizeTable()` in indexed mode before exposing a valid texture. Legacy create defaults unchanged. A too-small `pageTableBytesPerFlush` (less than full width*4 row) causes fail-closed initialization. No tests run on this SHA, PR #21 Draft.

## Stage 10CS — virtual-footprint anisotropic trilinear VT (SHAs f26eaf98, 916896ea)

Canonical GLSL `aiverseSampleVirtualAnisotropic` and MSL `aiverse_sample_virtual_anisotropic` take original UV derivatives, virtual pixel resolution, mip bias/levels, a max tap cap 1..8 and U/V address modes. Each tap independently resolves the indexed virtual page table, unlike hardware within-slice anisotropy. `ModelVirtualTextureBinding.virtualAnisotropyTaps` accepts 1,2,4,8 in indexed mode (default1); 48B indexed U6 now includes a third quality vec4, legacy still16B. `makeModelVirtualTextureArchiveBinding(archive,streamer,layer,out,bias,taps)` fills the binding. High tap count costs multiple page lookups and is unverified. No shader or GPU tests run on new SHAs; keep PR Draft.

## Stage 10CT — fail-fast virtual streaming budgets (SHA bf327614)

`virtualTexturePhysicalMipChainBytes(side,levels)` estimates a full physical RGBA8 mip chain. `VirtualTextureStreamer::initialize` now rejects impossible atomic staging (full chain > staging budget or base page > uploadBytesPerFrame), insufficient page-table row flush budget, and combined atlas+indexed/legacy page-table GPU memory exceeding queue's configured GPU budget. `estimatedGpuAllocatedBytes()` includes the table estimate, unlike queue-only `streamStats()`. Errors remain fail-closed; no native test has run for these changes.

## Stage 10CU — optimized sRGB mip CPU converter (SHA dae9aade)

`buildVirtualTexturePhysicalMips` now uses one static 256-value sRGB-to-linear table and binary-search inverse quantization, avoiding per-texel `pow` during runtime VT page uploads. Existing alpha-correct filtering and generated mip dimensions unchanged. Correctness and performance have not been tested for this source SHA; record actual oracle/benchmarks in final acceptance.

## Stage 10CV — new final-stage indexed VT regression fixture (SHA 40398dae)

`virtual_texture_mip_pipeline` CTest now exercises production CPU physical mip generation, indexed mip table partial-vs-full oracle, bounded page-change history, atomic multi-frame upload/cancellation, and fail-fast GPU/staging budgets with mock RHI. CMake, focused verifier, and Mac one-shot script include this 22nd safe test; seven existing Metal GPU tests unchanged. This is SOURCE ADDED, NOT COMPILED OR PASSED. The exact CTest outcome must be recorded only after real CI/Mac validation.

## Stage 10CW — CI queue is not a PASS (SHA 0ad0d8a9)

The focused Texture RHI workflow now triggers on pushes to `feature/texture-rhi-unification` with code/test/script paths. First run `44604340610` is queued for OpenEXR ON/OFF and Vulkan jobs, which must actually complete with build, new `virtual_texture_mip_pipeline`, and GLSL compiler results before claims. Mac Metal GPU and full engine are separate required gates.

## Stage 10CX — cache identity also matters for revision skip (SHA fbaaf8ed)

`VirtualTexturePageTable::rebuild` now requires the same cache object AND same revision to return early. A different cache object with coincidentally equal revision forces full table rebuild. New `virtual_texture_mip_pipeline` regression compares page table values when switching two caches with identical revision counters. Not yet compiled/tested; do not claim PASS until CI or Mac log evidence.

## Main integration acceptance gates (2026-11-20)

PR #86 and #95 have merged to main. Do not use the obsolete feature-only
revision restriction or infer current test status from the historical
SOURCE ADDED entries above. Consult the newest texture handoff.
`metal_vt_bilinear_gpu` now includes independent nonuniform indexed mip,
physical-mip and bounded streamed-LRU pixel oracles.
`metal_renderer_indexed_vt_gpu` checks production graph sampling with the
48-byte uniform contract and safe direct fallback under ICB opt-in;
indexed graph ICB acceleration is intentionally unsupported. Reject indexed
bindings during GPU draw admission, before command staging; a late backend
rejection is not an acceptable steady-state fallback. Preserve this
guard, legacy ICB execution checks and serial native GPU tests.
The final verifier requires an absolute `--build-root`, supports
`--expected-ref` (default HEAD), and builds Debug. On the owner's machine
all builds, logs, module cache and temporary fixtures belong on KINGSTON.
Focused CI triggers for main as well as the historical feature branch.
