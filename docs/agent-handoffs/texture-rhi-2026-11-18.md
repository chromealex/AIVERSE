# AIVERSE texture resource project — append-only agent handoff

**Date:** 2026-11-18 (time of execution not measured)
**Base:** `feature/universal-entity-inspector-fps` @ `dd711f5e2a7f66ed25bd7330875e6e87b7109e55`
**Feature branch:** `feature/texture-rhi-unification`
**Scope:** all texture dimensions/formats/render targets/imports; work is strictly incremental
and existing RTS/FPS resource usage must remain compatible.

Read `AGENTS.md` (mandatory handoff protocol) and `docs/texture-rhi.md`.
Before continuing, fetch the **latest branch HEAD**, preserve changes from other agents,
and use a separate implementation commit followed by a separate handoff commit after
**every** stage, including any failed/partial stage.

## Stage 1 — Public descriptor contract and fail-closed backends

**Code commits:**
- `5fa2ee589f33a243f72772801ec1bebc95f58020`: descriptor types + validation + backend guards + test + roadmap.
- `9f78184258db136b730ac2d020cb7d2b58097530`: fix default depth argument to mip-count helper.

**Paths changed:**
- `engine/gfx/Gfx.h`: new `TextureViewHandle`, `SamplerHandle`,
  `TextureDimension` (1D, 2D, 3D, 2DArray, Cube, CubeArray),
  `SamplerAddressMode`, `SamplerMipFilter`, `CompareFunction`,
  `TextureAspect`; extend `TextureDesc` with dimension/depth/arrayLayers/
  mipLevels/sampleCount; typed `TextureViewDesc`, `SamplerDesc`,
  `TextureUploadDesc`, `TextureDescError`, `validateTextureDesc`,
  `maxTextureMipLevels`, `textureDescErrorString`. Extend `gfx::Device`
  with `supportsTextureDimension`, `supportsTextureViews`,
  `createTextureView/destroyTextureView`, `createSampler/destroySampler`,
  `updateTextureRegion`, `bindTextureView` as **unsupported defaults**.
- `engine/gfx/gl/GfxGL.cpp`, `engine/gfx/metal/GfxMetal.mm`,
  `engine/gfx/vulkan/VkResources.cpp`: reject invalid/unsupported
  non-2D, array, MSAA and explicitly requested mip allocations rather than
  quietly converting those requests into a plain 2D texture.
- `tests/render/texture_contract.cpp`: CPU descriptor checks.
- `CMakeLists.txt`: `texture_contract` CTest registered for non-Web builds.
- `docs/texture-rhi.md`: implementation stages and acceptance gates.

**Compatibility:**
Existing `TextureDesc` aggregate fields are unchanged and new fields appended
with 2D-compatible defaults. The existing `gfx::TextureHandle`,
`createTexture`, `updateTexture`, `setTexture`, `PassDesc` and
`render::RenderTexture` signatures remain unchanged. Existing 2D content
should continue to use the same allocations. This step does not modify game
files, network messages, simulation state, saves or ECS components.

**Not yet implemented:**
Creating 1D/3D/array/cube GPU resources is **NOT** supported (the descriptor
contract validates them but backends intentionally return invalid handles).
The new native views/samplers/subresource upload methods also return an
invalid handle/false until subsequent implementation stages. No MRT,
array/mip render targets, MSAA, expanded formats, KTX2 transcoding, DDS
array import, EXR/HDR, async/streaming/virtual textures in this stage.

**Verification evidence (do not overstate):**
- GitHub API source reads / branch operations: successful.
- C++ compile: **NOT RUN** (no checked-out native repository in this
  session; raw.githubusercontent.com DNS failed from the execution container).
- `cmake`, `ctest`, Metal/GL/Vulkan GPU validation: **NOT RUN**.
- GitHub Actions query for the feature branch: **0 workflow runs** at
  check time. GitHub commit status state: `pending`, with no statuses.
- This is a committed *source change*, not a verified production-ready feature.

**Suggested immediate verification (next agent):**
```sh
git fetch origin
git switch feature/texture-rhi-unification
git pull --ff-only origin feature/texture-rhi-unification
cmake -S . -B build/texture-rhi -DAIVERSE_BUILD_TESTS=ON
cmake --build build/texture-rhi --target aiverse_texture_contract_test -j 4
ctest --test-dir build/texture-rhi --output-on-failure -R '^texture_contract$'
```
Then build the actual `aiverse_editor` and an RTS/FPS client with the
selected RHI to ensure pre-existing paths compile and render unchanged.
Do not report unsupported platforms as validated.

**Next implementation stage:** Stage 2 — implement native allocation and
subresource updates/binding for 1D, 3D, 2DArray, Cube, CubeArray in
Metal/GL/Vulkan, including dimension-aware GPU storage and shader sampling.
Keep `setTexture` on its legacy 2D compatibility path; use
`bindTextureView` or an explicit dimension-aware binding for new resource
types, not implicit sampler2D reinterpretation. Register per-backend
RHI tests, cap checks, and update this handoff in a separate commit.

## Stage log for future agents

Append **one section per stage** here: date, branch, code commit SHA(s),
changed files, what is implemented vs only declared, binary/API/save
compatibility, tests actually executed and results, CI links/status,
known risks, blockers, and next actionable stage. Never delete this log
or rewrite another agent's entry.

## Stage 2A — Metal sampled dimensions and subresource upload (2026-11-18)

**Code commits:** `d50a8abf1806703fbc5ba09462f9eb598dc3098a`,
`414dc591377a2d5017cfaa4e8062f5c80fd81ff4`.
**Path:** `engine/gfx/metal/GfxMetal.mm` (only).

**Implemented in source:** Metal texture storage now maps `TextureDimension` to native
`MTLTextureType1D/2D/3D/2DArray/Cube/CubeArray`; uses depth/arrayLength/mip
levels when allocating, handles contiguous uncompressed face/slice/volume data,
and accepts compressed mip payloads across all array/cube faces for GPU-compressed
2D/2DArray/Cube/CubeArray types. `updateTextureRegion` supports uncompressed
mip/array-layer/volume subregions with bounds/pitch checks. Existing
`setTexture` can bind these native resources to matching MSL texture
types; `supportsTextureDimension` returns true on the Metal backend.

**Explicitly not implemented:** `TextureView` and independent
`SamplerHandle` still return unsupported defaults. Multisample textures
and non-2D render attachments still reject creation, pending render-pass/view
work. Texture containers (DDS/KTX2) still discard layers/faces; external assets
cannot leverage these new Metal capabilities until importer stages. GL/GLES and
Vulkan still reject non-2D requests. No explicit per-slice compressed updates;
compressed sources require a full level payload. 1D/volume compression is
rejected. Metal device-family-specific limits are not queried.

**Compatibility:** Existing 2D callers keep their legacy descriptor defaults;
native 2D, atlas and render canvas paths should remain the same. No changes to
gameplay/ECS/saves/assets. The Metal implementation chooses native texture
dimensions only when explicitly requested in `TextureDesc`.

**Validation:** Source change committed. Native macOS/iOS compilation: **NOT RUN**;
GPU correctness, array/cubemap sampling, mip generation, layered texture upload
and Metal validation: **NOT RUN**. No successful CI result is asserted.

**Next actionable step:** Build `aiverse_editor` and run an explicit Metal
device test creating a `Texture3D`, `Texture2DArray`, `Cube` and `CubeArray`,
binding to matching MSL shader texture types, reading back rendered colour,
testing mip levels and all slices. Fix any Objective-C++ SDK errors before
proceeding. Then implement GL/GLES native dimensions with explicit feature
queries (GL33/GLES3/WebGL2 have different array/cube capabilities); follow
with Vulkan image/view types, layers and barriers. Keep these as individually
handed-off stages, with exact code commits and actual test outcomes.

## Stage 2B — GL/GLES native 3D, 2D arrays and cubemaps (2026-11-18)

**Code commit:** `5fd11d2f638d77ae281c24a7e2bc91f50f8f19fa`.
**Paths:** `engine/gfx/gl/GfxGL.cpp`, `engine/gfx/gl/GLApi.h`.

**Implemented in source:** Keep the original 2D allocation code; new
`GLDevice::createDimensionalTexture` handles `Texture3D`,
`Texture2DArray`, `Cube` using native `GL_TEXTURE_3D`,
`GL_TEXTURE_2D_ARRAY`, `GL_TEXTURE_CUBE_MAP`. For arrays/volumes
the API uploads contiguous RGBA/R8/HDR payloads, optionally generates
mipmaps and accepts explicit allocation levels. Compressed 2D array/cube
levels can be passed as complete contiguous per-level payloads when the
driver supports the block compression format. `GLTexture` tracks
target/dimension/layers, `setTexture` uses the correct target,
and `updateTextureRegion` handles array face/mip/volume updates
with row-by-row upload of non-tight row pitches.
Windows GL loader adds `glTexImage3D`, `glTexSubImage3D`,
`glCompressedTexImage3D` function pointers and required core constants.

**Explicitly unsupported:** GL/GLES/WebGL universal `Texture1D` and
`CubeArray` (these need platform-specific branching or extensions).
Non-2D render targets, any GL storage image, MSAA and the new
`TextureViewHandle`/`SamplerHandle` API. GL code paths do not yet
provide independent sampler objects. Vulkan remains 2D-only. Some
compressed-array driver combinations may reject allocation via
`glGetError`; GPU support is runtime-dependent.

**Compatibility:** 2D legacy texture path retained, except a new
fail-closed check for previously ignored `storage` or unsupported
MSAA/explicit allocation. Sampler dimension in GLSL must match the
created GL texture target. The editor, game format and ECS are unchanged.

**Validation:** Windows, Linux/GLES, Android and Emscripten compile:
**NOT RUN**. OpenGL GPU validation: **NOT RUN**. No CI passing claim.

**Next:** Ensure Windows GL loader links/initializes all new pointers and
GLES3/WebGL2 headers expose the functions, then add dimension-sampling
GPU tests in the GL context test runner. Implement Vulkan image types,
array layer barriers, sampled image views and full subresource staging
next; do **not** copy only `createImage` without checking all upload,
clear, mip generation and render pass transitions.

## Stage 2C — Vulkan sampled dimensions, layers and barriers (2026-11-18)

**Code commits:** `ddb536106ffd941f80c797dad3d231d0c07e21f2`,
`daa721ef0ce9ebb0aa16ed1c1a34c05711295aca`.
**Paths:** `engine/gfx/vulkan/VkDevice.h`,
`engine/gfx/vulkan/VkResources.cpp`, `engine/gfx/vulkan/VkDraw.cpp`,
`engine/gfx/vulkan/GfxVulkan.cpp`.

**Implemented in source:** `VTexture` tracks dimension, 3D depth and image
array layers. Vulkan image/image-view creation uses matching
`VK_IMAGE_TYPE_1D/2D/3D` and `VK_IMAGE_VIEW_TYPE_1D/2D/3D/2D_ARRAY/CUBE/CUBE_ARRAY`.
Cube-compatible create flags and six-face layer counts are applied.
Memory transitions and clear operations now cover the whole array layer range;
mip blits handle all layers and volume depth. Staging uploads can specify
`z/depth/baseArrayLayer/layerCount`, including full compressed array/cube
mip data and uncompressed 3D data. `updateTextureRegion` accepts
uncompressed volume/slice/mip writes, repacking strided inputs as needed.
Regular texture binding works through the default dimension-aware image view.
`imageCubeArray` is enabled only if available and
`supportsTextureDimension(CubeArray)` reflects that feature.

**Compatibility:** Legacy 2D Vulkan image creation and descriptor binding
retain the same default types/shape and existing `setTexture` path.
`copyColor` now explicitly rejects non-2D images rather than issuing
an invalid 2D blit. Non-2D depth targets, layered render attachments,
MSAA, storage image writes and explicit `TextureView`/`SamplerHandle`
are NOT implemented. RHI source changes do not affect native ECS or saves.

**Risks:** Vulkan's staging allocator reports failure via an existing
void-return `uploadImage` helper; texture-creation callers do not yet
propagate staging allocation failure as an invalid texture handle.
The new layout transition and mip blit paths must be run through Vulkan
validation layers (including partial mip/slice updates and cube-array views).
Compressed 1D and 3D are explicitly rejected; malformed/truncated
mip-level payloads now fail before GPU allocation. Device feature support,
compressed format capabilities and out-of-memory paths need GPU tests.

**Validation:** Source/GitHub commits succeeded. Native Vulkan compilation,
CTests, Lavapipe and validation layers: **NOT RUN**. No passing CI is
asserted. The current code is not production-certified until these gates pass.

**Next actionable step:** Build `aiverse_gfx_vulkan` and
`aiverse_vulkan_tests`, add automated real-GPU/lavapipe sampling tests
for 1D/2DArray/3D/Cube/CubeArray including upload and mip sampling.
Then validate GL/Metal on supported devices and fix failures.
Only afterwards advance to Stage 3 (views/independent samplers).
Use a **new code commit and separate handoff commit** for each substage.

## Stage 2D — Vulkan dimension-sampling integration test source (2026-11-18)

**Code commit:** `5392904f281c2bfdcf282d1bb70e89ed32c77be4`.
**Path:** `tests/render/vulkan_tests.cpp` only.

**Added:** Integration checks for sampled `Texture1D`, `Texture3D`,
`Texture2DArray`, `Cube`, and conditionally `CubeArray`. Each uses
an actual Vulkan texture resource, a GLSL shader with the matching
`sampler1D/3D/2DArray/Cube/CubeArray` type, a screen draw, capture and
colour assertion. Cube arrays are skipped only if the native Vulkan
feature query reports unsupported. The checks run inside existing
`vulkan_backend` CTest when that target is built; no new executable.

**Test status:** test source committed, but **NOT RUN** (native Vulkan
build and driver unavailable in this session). It is NOT correct to
claim the tests passed. Existing Vulkan test runner skips GPU work when
no Vulkan driver can be created; inspect its output to distinguish
a real GPU pass from skipped execution.

**Immediate next step:** Run `cmake --build build/texture-rhi --target
aiverse_vulkan_tests` then `ctest --test-dir build/texture-rhi
--output-on-failure -R '^vulkan_backend$'` on Vulkan SDK+Lavapipe or
native GPU. Fix compiler errors or colour mismatches as needed and
record the actual results in the next handoff. Verify array-layer
readback/update separately. Then continue stage 3 texture views
and independent samplers.

## Stage 3A — CPU view/sampler contract validation (2026-11-18)

**Code commit:** `bee8464f0bbffefde854ff2b13fdb60df4ddf74b`.
**Paths:** `engine/gfx/Gfx.h`, `tests/render/texture_contract.cpp`.

**Implemented in source:** `textureEffectiveMipLevels(TextureDesc)`,
`validateTextureViewDesc(TextureDesc, TextureViewDesc)` and
`validateSamplerDesc(SamplerDesc)`. Bounds checks include valid mip
and array-layer intervals, image aspect (color vs depth), 2D array
single-slice 2D views, and cube-aligned CubeArray subset views.
Sampler limits reject nonfinite LOD/bias/anisotropy, negative minLod,
maxLod below minLod, or anisotropy below 1. Added positive/negative
cases to CPU-only `texture_contract` CTest.

**Compatibility:** Existing public API signatures and 2D defaults
preserved. Only new helpers and tests added; no backend behavior changes
and no game save/network/ECS changes. `TextureViewDesc` still has
mipLevelCount=1 by default: callers requesting all mips must set it.

**What is NOT implemented:** Native `TextureViewHandle`/
`SamplerHandle` objects or binding; the validators are not a GPU
capability claim. Metal, GL/GLES and Vulkan still return unsupported
defaults for view/sampler resource calls.

**Verification:** GitHub source commit succeeded; all four GitHub
workflow runs on prior commit `1640d5...` were **queued**, not
passed at read time. Local compiler/CTest/GPU execution: **NOT RUN**
because the working runtime cannot resolve `github.com` to clone
the full repository. Do not report these tests as passing.

**Next stage:** Implement independent texture views and sampler
objects in one backend at a time, beginning with Metal. Validate
requested view dimensions against *actual* mip/layer counts in
addition to CPU descriptor helpers. Destroy dependent views safely
when parent texture dies and explicitly invalidate stale handles.
Add native tests, verify no regressions in legacy setTexture path.
Commit each backend stage and a separate handoff.

## Stage 3B — Metal native texture views and samplers (2026-11-18)

**Code commit:** `7984d46490ab3f8e59acaa00ad69fd62fadfcfe3`.
**Path:** `engine/gfx/metal/GfxMetal.mm`.

**Implemented in source:** `MetalDevice::createTextureView` validates a
requested mip/slice/aspect against the actual native texture's mip levels,
creates a Metal texture view with `newTextureViewWithPixelFormat:
textureType:levels:slices:`, and tracks its parent by texture ID.
`createSampler` maps min/mag/mip filters, U/V/W address modes,
depth-compare function, LOD range and anisotropy (capped at 16)
to `MTLSamplerDescriptor`. Metal has no independent sampler LOD bias
in this API; nonzero `lodBias` is rejected instead of ignored.
`bindTextureView` binds the selected view + selected sampler (or the
texture's original sampler if none supplied) to vertex and fragment
stages for slots 0..15. Parent texture destruction invalidates all
dependent view handles before the texture ID can be reused; views and
independent samplers have explicit destroy operations.

**Compatibility:** Existing `setTexture` still uses a texture's original
sampler. The new APIs are opt-in; no existing shaders need rewriting.
This does not add new save/network/ECS state. The generic RHI allows
views of selected array slices/mips; Metal implementation additionally
checks physical backing allocation.

**Not done:** No non-2D render-target attachments yet. View binding
does not rewrite `PassDesc`, and storage/compute binding of an explicit
view is not exposed. Independent samplers are exposed only for direct
render encoder bindings; the ICB path needs separate verification.
No Metal GPU tests are added or executed in this stage.
Other backends still have the default unsupported view/sampler methods.

**Validation:** GitHub commit succeeded; actual macOS/iOS compilation,
GPU texture view/sampler sampling and existing scene regression tests:
**NOT RUN**. No successful CI result claimed.

**Next:** Compile Metal (especially SDK availability of border
addressing and texture view APIs), run MSL shaders sampling nonzero
array layers and mip levels with separate samplers, test that destroying
parent invalidates views, and review view handle recycling in long
sessions. Next implement native Vulkan and GL sampler/view support;
GL ES3/WebGL2 lacks general texture-view extensions, so report
unsupported there rather than silently creating aliases.

## Stage 3C — GL33/GLES3 independent sampler objects + identity views (2026-11-18)

**Code commit:** `b945c6a6c24d571e29e31f70a7534cafca13e7ee`.
**Paths:** `engine/gfx/gl/GfxGL.cpp`, `engine/gfx/gl/GLApi.h`.

**Implemented in source:** Windows GL33 loader gains the core 3.3
`glGenSamplers/glDeleteSamplers/glBindSampler/glSamplerParameteri/
glSamplerParameterf` entry points and relevant enum values.
`GLDevice::createSampler` exposes independent min/mag/mip filtering,
U/V/W address modes, compare function, min/max LOD and optional
anisotropic sampling where an extension is available. Desktop-only
LOD bias and clamp-to-border are rejected in GLES3/WebGL2.
`bindTextureView` binds a supported identity view of the parent
texture and an independent sampler object; legacy `setTexture`
explicitly unbinds any prior custom sampler for predictable
compatibility. Destroying the parent removes dependent identity views.

**Portable limitation (important):** OpenGL 3.3 / GLES3 / WebGL2
cannot provide a general subresource alias with the portable API.
`createTextureView` therefore **only** accepts an identity view
covering the full native mip range and all layers. Requests for
per-face or per-mip views fail, not silently collapse to the parent.
`supportsTextureViews()` reports false to communicate that full
subresource views are absent, although explicit identity view handles
are available for independent sampler binding. Full GL texture views
would require GL 4.3 / ARB_texture_view / platform-specific support
and separate resource allocation rules.

**Compatibility:** Existing 2D texture binding still uses
`setTexture`; its sampler reset prevents a previous explicit
`bindTextureView` from leaking state into legacy renderer calls.
`Texture2D` resources record actual requested mip count for
identity-view validation. No data format, save or ECS changes.

**Verification:** Source commit recorded. Windows GL loader,
Linux GLES, Android and WebGL2 compilation: **NOT RUN**.
Native GPU creation/bind/filter tests: **NOT RUN**.
Do not mark backend support production-complete without these tests.

**Next:** Implement native Vulkan `TextureView` + `SamplerHandle`
with descriptor-set binding overrides and GPU-safe deferred destruction,
and expand Vulkan tests to select a nonzero array slice/mip with a
custom sampler. Confirm GLES3 compile on Windows loader and device
lifetime semantics, especially sampler unbinding and anisotropic
feature query. Keep handoff and source as separate commits.

## Stage 3D — Vulkan native subresource views and independent samplers (2026-11-18)

**Code commit:** `0d32b65c464e1e4c768253a412fa3176f347227d`.
**Paths:** `engine/gfx/vulkan/VkDevice.h`,
`engine/gfx/vulkan/VkResources.cpp`,
`engine/gfx/vulkan/VkDraw.cpp`,
`engine/gfx/vulkan/GfxVulkan.cpp`.

**Implemented in source:** `createTextureView` selects a validated
mip/layer range with a real `VkImageView` matching 1D/2D/3D,
array and cubemap dimensions. `createSampler` exposes separate
`VkSampler` resources with filtering, address modes,
compare op, LOD range, bias and anisotropy (enabled only when
physical-device feature is present). `bindTextureView` overrides
the combined image/sampler descriptor for a selected texture slot
without affecting the legacy `setTexture` API. Parent texture
deletion destroys all child views, and explicit view/sampler deletion
invalidates active bindings. Native Vulkan destruction is deferred
through the existing frame-completion queue for GPU lifetime safety.
Device shutdown destroys extra views before parent images and
independent samplers before the Vulkan device.

**Compatibility:** Descriptor layouts unchanged (combined sampler).
`setTexture` clears any prior view/sampler override so existing
RTS/FPS render paths continue to use their default resources.
No save, ECS, serialization or project content changes.

**Limitations:** RHI permits view bindings for direct render passes,
not layered render-target attachment views. No MSAA/resolve, MRT,
storage image or async texture readback. Actual GPU feature support
still needs validation. The Vulkan backend uses combined
image-sampler descriptors, so separate samplers are selected by
rebinding a view, not by an independent sampler-only command.

**Verification:** GitHub code commit succeeded; native Vulkan
compiler, Vulkan validation layers and real GPU tests: **NOT RUN**.
The prior workflow executions were queued; do not mark green.
Check C++ declaration/definition linkage (especially new
`bindTextureView` override) in a native build.

**Next actionable verification:** Extend
`tests/render/vulkan_tests.cpp` to test nonzero array-layer,
explicit mip and separate sampler bindings, then build/execute
`aiverse_vulkan_tests` on Vulkan SDK+Lavapipe. Also test parent
destruction invalidation and sampler destruction during a frame.
After that, validate Metal and GL native tests, then tackle layered
render targets/MSAA and format/importer stages.

## Stage 3E — Vulkan explicit view/sampler integration regression test (2026-11-18)

**Code commit:** `867e4cf999521a1ff3bca683c616b210d9c8790c`.
**Path:** `tests/render/vulkan_tests.cpp`.

**Added:** Real GPU Vulkan fixture creates a three-layer
`Texture2DArray`, then views two different array layers as
`Texture2D`, binds the selected views with a separately created
nearest sampler to one GLSL `sampler2D` slot and asserts blue/green
screen output. Explicitly tests that a destroyed sampler cannot be
rebound and destroying the parent invalidates its child view handles.

**Validation:** New test code committed but **NOT RUN**: this
execution environment cannot clone GitHub, and the native Vulkan
compiler/driver are unavailable here. Existing GitHub CI workflows
were still queued on the branch at last check; no passing result is
claimed. Real colour results depend on driver execution.

**Next:** Run `aiverse_vulkan_tests` on a GPU/Lavapipe machine
with validation layers and fix any shader/compiler/descriptor
validation errors. Build `aiverse_texture_contract_test`;
compile all major client/editor targets with Metal and GLES3 to
check cross-platform RHI changes, then proceed to layered
RenderTexture/MRT/MSAA implementation in independently documented
stages.

## Stage 4A — explicit offscreen attachment-pass contract (2026-11-18)

**Code commit:** `7f05e9eccf0b01897f7cc61d7fa17efee34ba46e`.
**Paths:** `engine/gfx/Gfx.h`, `tests/render/texture_contract.cpp`.

**Source changes:** New `RenderTargetAttachment` (texture, mipLevel,
arrayLayer/face, clear, RGBA clear value) and `RenderTargetPassDesc`
(up to four color attachments, optional depth, depth clear), plus
`validateRenderTargetPassDesc`. The CPU-only helper rejects empty
passes, invalid handles and negative coordinates, duplicate writes
to the same subresource, the same resource as color and depth,
and invalid clear values. The public RHI has opt-in
`supportsRenderTargetPasses` and
`beginRenderTargets(RenderTargetPassDesc)` returning false by
default. A successful call opens a pass ended by existing
`endPass()`; errors must leave no active pass and must never fall
back to an unintended target. New structural contract tests added.

**Compatibility:** Existing `PassDesc` and `beginPass` are unchanged.
The new API is offscreen-only, never implicitly the swapchain.
No gameplay/ECS, save or external game format changes.

**NOT implemented yet:** Native Metal/GL/Vulkan implementation of
`beginRenderTargets`; MRT, mip/layer rendering, MSAA and resolves
are still unsupported by this initial contract.

**Validation:** Source commit confirmed through GitHub connector.
Native compile, CTest and GPU test: **NOT RUN**.
GitHub Actions for prior HEAD were in `queued` state; no CI pass
verified. Do not claim the contract is production ready.

**Next stage:** 4B — Metal native single color + optional depth,
selecting mip/layer using Metal render pass descriptors, validating
actual allocation usage/format/sizes, then add Metal multi-color
targets and capability-specific pipeline variants. Make a separate
code commit and immediately append a handoff commit.

## Stage 4B — Metal color/depth rendering into selected mips and layers (2026-11-18)

**Code commit:** `dfbc5421d4da59e533cf9a9cc8e07121914f8a46`.
**Changed:** `engine/gfx/metal/GfxMetal.mm`.

**Implementation in source:** Metal `createTexture` permits texture
2D arrays/cubes/cube arrays with `renderTarget=true`, preserving
Metal's explicit native `MTLTextureUsageRenderTarget` allocation.
`supportsRenderTargetPasses()` reports true.
`beginRenderTargets` validates the requested subresource against
the actual native texture's mip levels, layers/faces, dimensions,
render-target usage, pixel format, and samples; it requires matching
extent for color/depth. On success the Metal render-pass descriptor
uses the requested `level`/`slice` and per-attachment clear/load/store,
then opens an encoder ended via existing `endPass`.
Both color-only and depth-only are supported. The existing screen
and legacy 2D `beginPass` remain unchanged.

**Explicit limits:** Maximum **one** color attachment is supported
by this stage; multi-target passes return false with a clear error,
since current Metal pipeline descriptors only configure color slot 0.
3D slice and 1D render targets are rejected. Only color RGBA8,
RGBA8_sRGB and RGBA16F are allowed, matching supported pipeline
pixel formats; depth format uses existing `Depth`.
No MSAA or resolve operation. The high-level
`render::RenderTexture` remains a legacy RGBA8 canvas.

**Compatibility:** Adds opt-in offscreen rendering only.
No existing render path or save/network/ECS file is changed.

**Verification:** GitHub code update committed; native
macOS/iOS compilation and execution **NOT RUN**.
Render-to-nonzero-mip/layer GPU fidelity and layout/resource
lifetime validation are still required. No CI pass claimed.

**Next:** Stage 4C — add GL33/GLES3 selected mip/layer color/depth
FBO passes, with strict checking before changing active render
state. Then validate native API calls and attach-layer path
using offscreen GPU tests. Stage 4D adds multi-color pipeline
output formats/MRT, Stage 4E Vulkan and MSAA/resolve.
Commit source and handoff separately after each.

## Stage 4C — GL33/GLES3 single attachment mip/layer offscreen passes (2026-11-18)

**Code commit:** `962e4b7fdaa9f1be51d8b24be19cbaebaeeba71c`.
**Changed:** `engine/gfx/gl/GfxGL.cpp`, `engine/gfx/gl/GLApi.h`.

**Source implementation:** `GLDevice::beginRenderTargets` checks
actual texture usage, target format, mip/layer and matching
color/depth extents, rejects nested or multi-color passes, attaches
the chosen `Texture2D` mip, cubemap face, `Texture2DArray`
layer or `Texture3D` depth plane to a temporary framebuffer.
The Windows GL3.3 loader now exposes `glFramebufferTextureLayer`.
Only after framebuffer completeness is verified does it update
renderer pass state, viewport and color/depth clear state.
The framebuffer is deleted on `endPass`; invalid attachment
requests return false rather than rendering to an unintended
target. Existing `beginPass` continues to function.

`createDimensionalTexture` now permits color render-target
allocation, plus depth arrays/cubes (not depth 3D).
Legacy 2D allocation now allocates all explicitly requested
mip levels, rather than quietly ignoring `mipLevels > 1`,
with its own maximum mip and filter settings.

**Limits:** MRT (more than one color attachment) rejected;
GL multisample texture allocation/resolve and storage textures
still unsupported; cube array/1D not available on portable
GL33/GLES3. The new pass builds a temporary FBO per invocation
rather than caching it; this is correctness-first and may
need a performance pass. No texture readback API added.

**Compatibility:** Existing GL texture binding and legacy
2D passes remain available. The GL new-pass guard tracks
`inPass_`, so callers should always use `endPass`.
No gameplay/saves/network/ECS changes.

**Verification:** Source commit completed through GitHub connector;
Linux/Android/WebGL2/Windows GL compiler and actual framebuffer
completeness tests **NOT RUN**. CI previously queued, no
passing outcome asserted.

**Next:** Implement explicit `beginRenderTargets` for Vulkan
with view selection and renderpass/framebuffer lifetime,
then add GPU tests to verify nonzero mip/layer and depth.
MRT requires pipeline format/layout-aware variants in
Metal/Vulkan; MSAA/resolve requires sample-count and
multisample resource allocation. Implement in separate
code/handoff stages. Fix any native failures first.

## Stage 4D — Vulkan explicit mip/layer color/depth passes (2026-11-18)

**Code commit:** `bda67dfc5cd3b586aa03ed9590b3cd1458c0f7c6`.
**Paths:** `engine/gfx/vulkan/VkDevice.h`,
`engine/gfx/vulkan/VkResources.cpp`,
`engine/gfx/vulkan/VkDraw.cpp`.

**Implemented in source:** `beginRenderTargets` accepts one color
attachment plus optional depth, or depth-only. It checks physical
resource usage/format/subresource bounds, mip-sized color/depth extent
parity and attachment type before creating a render pass.
For `Texture2D`, `Texture2DArray`, `Cube`, or `CubeArray`,
a 2D `VkImageView` selects the requested mip and layer/face,
and a one-layer framebuffer is allocated and used for drawing.
The new pass reuses existing Vulkan single-color renderpass/pipeline
format compatibility and custom clear colors. Temporary framebuffer
and image views are deferred for destruction after
`vkCmdEndRenderPass` and GPU completion. Vulkan resource creation
now permits 2D array/cube/cube-array color and depth render-target
allocation. All legacy `beginPass` paths remain unchanged.

**Explicit limitations:** maximum one color attachment; no MRT
pipeline layout, MSAA or resolve yet. Vulkan does not support the new
pass for `Texture3D` depth planes or `Texture1D`, since appropriate
image-view compatibility flags are not yet configured. Storage images
and high-level RenderTexture changes are pending.

**Compatibility:** no changes to game assets, ECS authority,
save/network data, or existing `PassDesc`. This is an opt-in
offscreen API only.

**Verification:** Source commit succeeded. Vulkan SDK compile,
validation layers, lavapipe/GPU render pass tests **NOT RUN**;
CI has not reported a confirmed pass. A real Vulkan test should
render into array layer 1 / mip 1, then sample a dedicated view;
also verify depth-only array and mismatched attachment sizes.

**Next:** Add regression tests for new attachment pass and
failure invariants; then implement MRT color-output pipeline
format matching, MSAA/resolve and storage texture support in
separate documented stages. Check existing single-color
pipelines and frame transitions on Vulkan validation layers.

## Stage 4E — Vulkan mip/layer attachment GPU test added (2026-11-18)

**Code commit:** `06fe04ebb2560657cabc1a983c13748cc315b290`.
**Changed:** `tests/render/vulkan_tests.cpp`.

**Test source added:** `vulkan_backend` now contains an integration
case that allocates color and depth `Texture2DArray` resources
with three mips, starts an offscreen render-target pass targeting
mip 1 / array layer 1, clears color and depth, ends it, samples
the selected layer/mip through a 2D `TextureView` and custom
nearest sampler, then checks screen pixels. It also asserts that
an invalid mip is rejected without starting a new pass.

**Validation status:** GitHub commit succeeded. The test
**HAS NOT BEEN COMPILED OR RUN** (no native Vulkan environment
in this turn); do not infer Vulkan correctness from the test's
presence. CI workflows for earlier commits were queued/cancelled,
not confirmed passing. Check a real Vulkan+validation layer run
before marking stage production ready.

**Next:** Run the actual compiler/tests; debug Vulkan image view
creation, layout transitions and pipeline compatibility errors.
After GPU validation, implement MRT with color formats and multiple
pipeline attachment slots, then multisampled textures and resolve
operations. The current native APIs still support only one color
target and no MSAA resolve.

## Stage 4F — MSAA resource and resolve contract (2026-11-18)

**Code commit:** `ffe532d8f6deb2d8884b4c687fb867fbd478708f`.
**Paths:** `engine/gfx/Gfx.h`, `tests/render/texture_contract.cpp`.

**Implemented in source:** Extended `PipelineDesc` with an
appended `sampleCount=1` field (legacy-compatible), and
`RenderTargetAttachment` with optional resolve destination,
resolve mip and layer. CPU descriptor validation now permits only
sampleCount 1/2/4/8/16 and rejects multisample compressed formats,
depth-compare multisample textures, and invalid MSAA usage.
`validateRenderTargetPassDesc` rejects resolve to the same
source image, invalid resolve coordinates, and portable-unsupported
depth resolves. Contract test cases added.

**What is NOT implemented:** MSAA GPU allocations, resolve
load/store actions and sample-count-aware graphics pipelines
are not implemented by this contract alone. Pixel format,
extent, sample-count compatibility must be checked by backends.
Native Metal implementation follows, others will still reject.

**Compatibility:** `PipelineDesc` gains only a default-initialized
final field. Old `TextureDesc.sampleCount=1` and no-resolve passes
remain unchanged. No game content/save/network/ECS impact.

**Validation:** GitHub source commit recorded. C++ compile,
CTest and GPU validation **NOT RUN**. CI previous jobs queued
or cancelled, no passing status reported.

**Next:** Stage 4G — Metal 2D MSAA resources, pipeline raster
sample count and color resolve in `beginRenderTargets`.
Then add a device-driven MSAA rendering/resolve test, verify
samples 2/4/8 on supported GPU and document fallback.
Do not silently enable sample count on GL/Vulkan before their
pipeline/renderpass implementation is ready.

## Stage 4G — Metal multisampled resources and color resolves (2026-11-18)

**Code commit:** `b063ab889a91ccc99f1e35e6718f1800b8fc04ee`.
**Paths:** `engine/gfx/metal/GfxMetal.mm`,
`engine/gfx/gl/GfxGL.cpp`, `engine/gfx/vulkan/VkDraw.cpp`.

**Implemented in source:** Metal checks
`[device supportsTextureSampleCount:]` before allocation,
creates 2D multisample resources with `MTLTextureType2DMultisample`,
explicit `sampleCount`, private storage and render-target usage.
Metal pipeline descriptors set `rasterSampleCount` from the newly
appended `PipelineDesc.sampleCount`.
`beginRenderTargets` now checks matching color/depth MSAA sample
counts; with a valid single-sample destination matching format
and extent it writes a native `resolveTexture/resolveLevel/
resolveSlice` attachment and selects
`MTLStoreActionMultisampleResolve`.
The active pass enforces `PipelineDesc.sampleCount` when binding.
Metal `TextureView` explicitly rejects multisampled sources until
a compatible MS texture view contract exists.
GL and Vulkan `createPipeline` now explicitly reject
`sampleCount != 1` instead of silently using a 1x pipeline.

**Compatibility:** All legacy pipelines still default to sample
count 1, old 2D offscreen passes are unchanged. Only opt-in
MSAA render target and resolve paths use the new descriptors.
No save/network/ECS changes.

**Limits / risks:** No GL/Vulkan MSAA allocation or resolve yet.
Metal depth resolve is not portable and deliberately unsupported.
MRT is still limited to one color attachment.
The Metal sample-count pipeline and resolve path **MUST**
be tested on actual macOS/iOS devices with Metal API Validation;
no compile or GPU test was run in this session.
Verify SDK availability of `rasterSampleCount`,
`supportsTextureSampleCount:`, `MTLTextureType2DMultisample`,
and `resolveLevel/resolveSlice` across deployment targets.
Check command-buffer lifetime and exact clear/resolve semantics.

**Verification:** GitHub source commit complete.
Native compilation, Metal validation, 2x/4x/8x MSAA
render-to-resolve tests: **NOT RUN**. CI still not confirmed green.

**Next:** Add a device-level Metal test with 4x MSAA render target,
pipeline `sampleCount=4`, an RGBA8 single-sample resolve target,
and post-pass sampling. Then implement Vulkan MSAA images,
compatible renderpass/pipeline and explicit resolve attachments
as separate stages; GL GLES3/GL33 needs corresponding
multisample textures and resolve blits. Document each separately.

## Stage 4H — fail closed on unavailable GL/Vulkan resolves (2026-11-18)

**Code commit:** `43709245696c5371cc11e5fbc6aee5e0ad5f7153`.
**Paths:** `engine/gfx/gl/GfxGL.cpp`, `engine/gfx/vulkan/VkDraw.cpp`.

**Reason/implementation:** After adding a portable optional
`resolveTexture` field to the pass contract, the GL and
Vulkan new render-target pass methods would otherwise have
ignored a resolve request and returned success without resolving.
They now explicitly return false and set a backend error when
a resolve target is requested. MRT still returns false on
these backends. Metal has the separate native MSAA+resolve path.

**Compatibility:** Only erroneous/unimplemented new-API requests
change behavior (fail instead of silent no-op).
Legacy screen/2D passes and game formats are unchanged.

**Validation:** Source commit succeeded. Native builds, tests,
GPU validation **NOT RUN**; no CI pass claimed.

**Next:** Before marking feature complete, implement MRT and
GL/Vulkan MSAA+resolve with actual GPU tests. Verify the
Metal path and all earlier texture stages through compiler
and real-device tests first; do not stack untested features
on broken backends. Continue append-only handoff with separate
implementation and docs commits per stage.

## Stage 4I — refresh canonical texture RHI architecture docs (2026-11-18)

**Documentation commit:** `fc1886681cc2909e24f3c85a0a742bb5d5f51c4f`.
**Changed:** `docs/texture-rhi.md`.

**Work:** Replaced obsolete "Stage 1 only" document status
with precise staged source progress through 4H. Recorded
Metal/GL/Vulkan dimension differences, texture views/samplers,
one-color-pass limitation, Metal-only source MSAA/resolve,
unimplemented MRT/import/streaming, legacy API compatibility and
an example for rendering into a Texture2DArray mip/layer.
Every feature remains explicitly **UNVERIFIED** until native
compiler and GPU testing is observed.

**Validation:** Documentation update committed. No native code
changed by this stage; renderer builds and tests were **NOT RUN**.
No CI results claimed.

**Next:** Run native builds first (Metal/GL/Vulkan and
`texture_contract`/Vulkan tests) and fix any errors. Then
complete MRT with format-aware pipeline variants,
GL/Vulkan multisampling/resolve and high-level RenderTexture,
followed by extended formats, KTX2/DDS imports and streaming.
Always append a new handoff after each bounded source stage.

## Stage 4J — GL33/GLES3 2–4 color MRT passes (2026-11-18)

**Implementation commit:** `fbb4f3c11eaa45ada4c21dff904a3300ab8db163`.
**Paths:** `engine/gfx/gl/GfxGL.cpp`, `engine/gfx/gl/GLApi.h`.

**Source implementation:** GL `beginRenderTargets` now accepts up to
`kMaxRenderTargetColors=4` distinct color subresources, optionally with
one depth subresource. It checks `GL_MAX_COLOR_ATTACHMENTS` and
`GL_MAX_DRAW_BUFFERS`, validates all mip-level dimensions/array layers,
requires equal color attachment pixel formats (the existing HDR/linear
shader policy is shared) and equal extents for all active targets.
It attaches each color to a separate GL FBO slot, configures all
`glDrawBuffers`, and uses `glClearBufferfv(GL_COLOR, slot, rgba)`
so clear values do not overwrite other color attachments.
Windows GL33 loading adds `glClearBufferfv` and needed enums.
Explicit resolve targets still fail closed rather than silently
dropping their requested resolve. Legacy `beginPass` remains intact.

**Limits / risks:** GL shaders must explicitly declare fragment
outputs at locations 0..N-1 to write each target; the new RHI
does not generate or patch shader outputs. Mixed-format MRT, MSAA
and resolves are still unsupported. This is not a shared
`PipelineDesc` color-format system across Metal and Vulkan.
No FBO cache: a fresh framebuffer is validated/created per
explicit render-target pass and deleted at `endPass`.
Before native use, verify GL `glClearBufferfv` entry-point loading
and framebuffer completeness on Windows GL33, Android GLES3,
WebGL2 and Linux EGL. Test depth-only cases and nonzero mip/layer.

**Verification:** GitHub code commit and source update confirmed;
native C++ build, GPU execution and CI green result: **NOT RUN**.
At start of this stage, latest CI workflows on branch
`9f175e5...` were queued, previous runs had cancelled results.
Do not mark the feature production-ready.

**Next actionable stage:** Add GPU test with two different
fragment outputs (`layout(location=0)` and `layout(location=1)`),
different clear values, and readback of both attachments.
Then implement corresponding Metal MRT pipeline/attachments and
Vulkan MRT render-pass/pipeline variants in bounded stages.
Run builds before merging and continue appending exact SHA/tests.

## Stage 4K — Metal 2–4 color MRT pipeline/output and per-attachment resolve (2026-11-18)

**Implementation commit:** `da0cdbdfcc44ded5947d04d8a7e0f43a7e6e6b1d`.
**Paths:** `engine/gfx/Gfx.h`, `engine/gfx/metal/GfxMetal.mm`,
`tests/render/texture_contract.cpp`.

**Source implementation:** Append `PipelineDesc.colorAttachmentCount=1`
and `additionalColorFormats[3]` to the existing color-format and sample
count contract: slot zero remains `colorFormat`. Metal pipeline creation
now configures native color attachment pixel formats, write masks and
blend modes for each declared color output. The new explicit Metal
`beginRenderTargets` accepts up to four active color attachments,
validates all mip sizes, sample counts and pixel formats, supports
a depth target and separate optional resolve target per color slot,
and creates a single Metal encoder containing those attachments.
The current implementation intentionally requires all color
attachments to share the same format; heterogeneous MRT requires
a shader/HDR-policy integration stage. An active explicit pass
checks the pipeline's color output count, attachment formats and
MSAA sample count on `setPipeline` and rejects mismatches.
Legacy one-color/default pipeline behavior is preserved by defaults.

**Requirements:** Shader authors must supply `fs_main` outputs at
Metal `[[color(0)]]`..`[[color(N-1)]]` to write every attachment.
The generic RHI does not rewrite shader entry points. Existing
`beginPass` remains the legacy path and is not subject to explicit
MRT validation.

**Limits and risks:** No native compilation or GPU validation has
run. Verify the Objective-C++ pipeline descriptor loop, Metal API
availability and correct MSL multiple-output pipeline compilation.
The ICB/PBR optimized path must remain disabled for MRT until
explicitly validated. HDR/mixed formats are not fully implemented.
No GL/Vulkan multisample implementation, no unified MRT pipeline
metadata validation on those backends.

**Verification:** GitHub source commit confirmed. `cmake` build,
`ctest`, Metal capture, runtime pipeline tests: **NOT RUN**.
No CI passing status observed.

**Next:** Add a Metal MRT integration test with two outputs and
distinct per-target clears/readback, including MSAA resolve. Run
native macOS build and validation. Complete Vulkan MRT renderpass
and pipeline layouts before claiming cross-backend support.
Commit code and handoff independently per stage.

## Stage 4L — GL explicit MRT pipeline count/format validation (2026-11-18)

**Implementation commit:** `58d1401ad5c5581e2974691c85f2d858b73b223c`.
**Path:** `engine/gfx/gl/GfxGL.cpp`.

**Source implementation:** Validate `PipelineDesc.colorAttachmentCount`
at GL pipeline creation (1..4). Track the active explicit MRT
pass's color count and common pixel format, and on `setPipeline`
reject mismatched depth-only/color-count/color-formats instead
of silently using the wrong output layout. The legacy
`beginPass` clears this MRT-only validation state and stays
compatible with ordinary single-color scenes. HDR variants
automatically propagate `RGBA16F` to additional color slots,
maintaining parity with the new Metal MRT pipeline policy.
`endPass` releases MRT-specific pass validation state.

**Compatibility:** This affects only explicit `beginRenderTargets`
and the new `PipelineDesc.colorAttachmentCount>1` use path.
The original legacy one-color paths remain available.

**Risks / limits:** GL does not reflect fragment shader output
locations against `PipelineDesc`; fragment outputs must be
authored at locations 0..N-1. Mixed attachment formats remain
unsupported by the common HDR policy. GL separate target
blending is not exposed by this implementation.

**Verification:** GitHub code commit confirmed; no native compiler,
GL context, CTest or successful CI result available. **NOT RUN**.
Do not claim production-ready MRT.

**Next:** Add a headless GL MRT GPU fixture with two GLSL output
locations and per-color readback (including clearing only one
color). Then implement Vulkan MRT with render pass + pipeline
compatibility and source test. Follow the mandatory separate
code and handoff commit protocol.

## Stage 4M — Vulkan 2–4 color MRT pipeline/renderpass source (2026-11-18)

**Implementation commit:** `4ceab89b0742143418bf84f8e15613638d66dd9c`.
**Paths:** `engine/gfx/vulkan/VkDevice.h`,
`engine/gfx/vulkan/VkDraw.cpp`, `engine/gfx/vulkan/GfxVulkan.cpp`.

**Implemented in source:** Add a dedicated cached Vulkan
`renderPassForMrt` with 2–4 color attachments sharing a VkFormat,
per-attachment `clearMask` and optional depth, without changing
the existing single-color renderpass cache. Rewrite the explicit
`beginRenderTargets` path to validate all array/mip attachment
extents, color formats, and depth, allocate 2D VkImageViews
for the selected subresources, and build an MRT VkFramebuffer.
Multiple per-slot clear values flow into VkRenderPassBeginInfo.
New `VPipeline::Variant.colorCount` differentiates pipeline
variants by number of render-target attachments.
`pipelineFor` uses a Vulkan blend-state array and compatible
MRT renderpass when count > 1. Active pass count is tracked
and checked on `setPipeline`. Scoped framebuffer/views
are destroyed after the GPU completion fence. Shutdown also
destroys the dedicated MRT renderpass cache.

**Constraints:** Same color format across all active MRT slots,
matching extents, sample count 1, at most four color targets.
GLSL fragment shaders must declare outputs at locations 0..N-1.
GL/Vulkan MSAA resolve and mixed-color formats are still
unimplemented. No new ECS/save/network data.

**Validation:** Source commit confirmed through GitHub connector.
Native Vulkan compiler, Lavapipe, GPU validation layer and
vulkan_backend CTest **NOT RUN**; no green CI can be asserted.
Pay particular attention to Vulkan pipeline renderpass
compatibility, the newly added variant cache key and color-only
versus depth-only passes.

**Known possible regression to fix next:** The new
`setPipeline` color-count/format validation currently runs
for both explicit MRT and **legacy** single-target passes.
Legacy depth-only, sRGB or HDR-compatible calls may previously
have used a pipeline with a different colorFormat/count.
Scope these strict checks to explicit MRT only; the legacy
`beginPass` semantics must remain unchanged. This is the
immediate next code change before claiming backward compatibility.

**Next:** Implement the legacy/explicit policy separation,
add a Vulkan test with two fragment outputs and distinct
color clears, then compile and execute on a validation-layer
GPU. Keep code and handoff as separate commits.

## Stage 4N — restore permissive legacy Vulkan pipeline binding (2026-11-18)

**Implementation commit:** `a207beab197b5d2c2556d94072b9f653d3bac8df`.
**Paths:** `engine/gfx/vulkan/VkDevice.h`, `engine/gfx/vulkan/VkDraw.cpp`.

**Fix:** Strict color output count/format validation introduced for
new MRT passes in Stage 4M was also running on legacy
`beginPass(PassDesc)`. That could reject existing depth-only
rendering, sRGB render targets or HDR shader variants.
Track a separate `explicitMrtPass_` flag, set only by
successful `beginRenderTargets`, reset by legacy
`beginPass` and `endPass`. Enforce exact count/format
checks only while that flag is set. Legacy pipelines still
select their variants using the actual number of color
attachments in the legacy pass, preserving old permissive
shader/pipeline policy.

**Compatibility:** No data format, saves or gameplay changes.
Existing screen/legacy shadow/depth passes retain their
previous source semantics while new MRT correctly enforces
explicit format/count contracts.

**Verification:** GitHub code commit confirmed; native Vulkan
build, lavapipe, integration GPU tests and CI: **NOT RUN**.
This is a source-level regression fix pending a real compiler.

**Next:** Add MRT test that creates two RGBA8 color textures,
renders two distinct fragment outputs, and samples both
textures afterward. Check Vulkan validation layers for
VkRenderPass/graphics pipeline compatibility. Then address
MSAA/resolve for GL and Vulkan and update handoff separately.

## Stage 4O — Vulkan MRT multi-output renderpass GPU test source (2026-11-18)

**Implementation commit:** `025ede7251cf2230462345ebcdca3f30ca6356f1`.
**Path:** `tests/render/vulkan_tests.cpp`.

**Source additions:** GPU fixture creates two RGBA8 color targets
and an explicit two-color `RenderTargetPassDesc`. First it clears
target0 red and target1 green, samples each into a different
screen half with a texture shader, and checks pixel values.
Then it draws a GLSL fragment shader with two independently
declared outputs (`layout(location=0)`, `layout(location=1)`)
through a `PipelineDesc.colorAttachmentCount=2`, with
distinct blue/yellow results. It verifies each target via
another rendered screen capture. This test catches MRT
renderpass clear policy, pipeline attachment count and
fragment output location binding.

**Verification:** GitHub test source commit confirmed. Native
Vulkan compilation, Lavapipe execution, validation layers and
image comparisons: **NOT RUN** in this environment (GitHub
repository cannot be cloned by the local execution container).
The fixture does not constitute a pass until it runs on a GPU.

**Next:** Compile `aiverse_vulkan_tests` and execute
`ctest --test-dir build/texture-rhi --output-on-failure -R
'^vulkan_backend$'` on an actual Vulkan device / Lavapipe.
Investigate shader output mapping, pipeline renderpass variant
cache and attachment load/store synchronization. Add matching
Metal/GL MRT fixtures; then continue MSAA/resolve.

## Stage 4P — native offscreen RenderTargetSet owner and CPU tests (2026-11-18)

**Implementation commit:** `83d96d5b93e95cb6deab9fd4eab6769b2c3b2116`.
**Paths:** `engine/gfx/RenderTargetSet.h`,
`tests/render/render_target_set.cpp`, `CMakeLists.txt`.

**Source implementation:** New header-only, genre-agnostic
`gfx::RenderTargetSet` wraps creation/ownership of up to four
same-format color `TextureHandle` objects plus an optional
depth attachment. Configurable 2D/2DArray/Cube/CubeArray,
mips, layers, formats and sample count. It owns an extra
single-sample resolve target per color when MSAA is requested.
`begin(mip,layer,...)` populates `RenderTargetPassDesc` and
only becomes active if `Device::beginRenderTargets` succeeds.
`end` closes the encoder; `shutdown` releases every GPU
resource, including cleanup after a partially created set.
`sampledColor(i)` returns the resolve target for MSAA or
the original texture for 1x. This utility does not replace
the existing `render::RenderTexture` paint canvas.

**Tests in source:** `aiverse_render_target_set_test`
is a GPU-independent mock Device CTest target verifying
multi-color/depth allocation, selected mip/layer submission,
nested-begin rejection, invalid index rejection, MSAA
resolve ownership, active pass shutdown and transactional
rollback on failed allocation. Tests consume only public
`gfx::Device` headers and the new utility header; no
renderer/gameplay dependencies.

**Compatibility:** Independent optional wrapper; no changes to
old `RenderTexture`, `PassDesc`, save files or ECS.
Native projects may include `engine/gfx/RenderTargetSet.h`.
The caller must destroy the set before the gfx::Device
lifetime ends; object destruction invokes `shutdown`.

**Unimplemented:** GL/Vulkan MSAA resource/resolve support;
mixed-color-format MRT; async readback, texture streaming.
Device GPU support is always queried via actual allocation
and beginRenderTargets status; no silent downgrade.

**Verification:** GitHub source commit succeeded.
Local compiler/CTest, native GPU tests, CI green outcome:
**NOT RUN** (container cannot resolve github.com for a clone).
Do not describe mock test as passed until actually executed.

**Next:** Build and run `aiverse_render_target_set_test` on
an actual clone, check native C++20 syntax and existing
editor/client consumers, then add metal/GL/Vulkan MRT
render/resolve device tests. After that continue GL/Vulkan
MSAA and the texture format/importer waves in separate
code and handoff commits.

## Stage 4Q — update canonical MRT + RenderTargetSet documentation (2026-11-18)

**Documentation commit:** `8c366e0a22b57636de26e1bae6285324a309f38e`.
**Changed:** `docs/texture-rhi.md`.

**Documentation:** Replace obsolete "only one color attachment" status
with the actual source-only MRT implementation in GL/GLES3, Metal
and Vulkan, including same-format-only and shader-output-location
requirements. Document Metal-only per-color MSAA resolves,
source-only Vulkan GPU regression fixtures, and the new
genre-independent `gfx::RenderTargetSet` with example usage
and explicit resource lifecycle. List remaining missing mixed
formats, GL/Vulkan MSAA, native GPU execution, importers and
streaming. Keep the unrelated Lua brush `RenderTexture`
separate.

**Validation:** Doc source commit confirmed. No C++ files
changed during this stage; native builds/tests **NOT RUN**.
No GitHub Actions green outcome can be asserted without
a completed successful job.

**Next:** First build the current branch and run
`texture_contract`, `render_target_set`,
`vulkan_backend` on a proper Vulkan/Metal/GL toolchain;
fix compilation and GPU validation errors. Only then finish
GL/Vulkan MSAA/resolve, mixed formats, DDS/KTX2 and
async/streaming improvements. Append precise results and
new stage handoffs.

## Stage 6A — full KTX2 array, cubemap and volume payload preservation (2026-11-18)

**Code commit:** `dbb6dfd1f32b40d4b002fd184c5db11be28d13f1`.
**Files:** `engine/render/TextureCodec.h`,
`engine/render/TextureCodec.cpp`, `engine/render/Model.cpp`,
`tests/render/ktx2_layers.cpp`, `CMakeLists.txt`.

**Source implementation:** `CompressedImage` now retains
`depth`, `arrayLayers`, `faces` and `oneDimensional`
metadata; the KTX2 reader preserves all layer/face/3D
z-slices within every mip, in order, for both raw and
Zstandard-supercompressed payloads. It checks dimensional
combinations, mip count, file bounds, expected lengths and
a 512 MiB per-level allocation ceiling. CPU API adds
`levelDepth`, `levelImageCount` and `decodeBlocksSlice`;
the existing `decodeBlocks` continues to decode slice 0.
Model texture upload stays **2D** for shader compatibility:
when a model references layered/cube KTX2 data it submits
only the first 2D slice of each compressed mip. Other
layers remain available in CPU memory to future generic
texture asset importers.

**Tests:** New GPU-independent `ktx2_layers` CTest,
linked against existing models + zstd decoder,
synthesizes KTX2 cube array and 3D RGBA8 payloads and
checks metadata, slice decoding, legacy first-slice
fallback, 1D metadata and corrupted/truncated inputs.

**NOT done:** Generic runtime KTX2 array/cubemap/volume
asset creation and binding to matching shaders; DDS
multi-layer parity; BasisLZ/UASTC transcode; HDR/EXR.
Parser does not automatically reinterpret glTF material
texture2D references as cubemaps. No changes to ECS/saves.

**Validation:** GitHub code commit successful. Native
C++ build and `ktx2_layers` CTest: **NOT RUN**.
GitHub Actions success not confirmed. Before compiling,
check that the new test source explicitly includes
`<algorithm>` for `std::max`; the current fixture
may need that include (identified during static review).
Do not claim test success.

**Next:** Fix the include, build/test KTX2 parser on
a proper clone, then implement generic dimension-aware
TextureAsset resource upload for KTX2 layered payloads
independently of the 2D-only model material path.
Continue with DDS cubemap/array decoding and BasisU
transcoding in separately documented stages.

## Stage 6B — KTX2 test standalone include correction (2026-11-18)

**Code commit:** `7a70493331e54a974d4125aa3ff9159c3a141fba`.
**Path:** `tests/render/ktx2_layers.cpp`.

**Fix:** Add `#include <algorithm>` explicitly; the test uses
`std::max` when calculating synthetic KTX2 slice offsets.
This avoids relying on transitive standard-library includes.

**Verification:** GitHub source commit confirmed. C++ compiler
and the `ktx2_layers` CTest **NOT RUN**. Still requires a
normal clone/native build; no passing result asserted.

**Next:** Add a dimension-aware KTX2 GPU importer that can
create actual texture array/cube/3D resources using the full
retained payload. Keep model-material sampler2D compatibility
separate. Validate import on all actual backends before
describing it as production support.

## Stage 6C — dimension-aware KTX2 GPU resource uploader (2026-11-18)

**Code commit:** `a300050dfb4eab58ce3d8c384e909f42154b7b29`.
**Paths:** `engine/render/TextureAsset.h`,
`tests/render/texture_asset.cpp`, `CMakeLists.txt`.

**Source implementation:** Header-only `uploadKtx2Image`
maps the extended CPU `CompressedImage` metadata to real RHI
`TextureDimension` values (1D/2D/3D/2DArray/Cube/CubeArray),
including all array layers/faces/volume slices. For supported
block-compressed images it hands the entire authored mip chain
to `gfx::Device::createTexture`. Unsupported BC4/EAC_R11/
BC1_RGB and other formats fall back through
`decodeBlocksSlice` to RGBA8 (or RGBA8_sRGB), allocate
the image and upload all nonzero authored mip slices with
`updateTextureRegion`. A failure destroys the partially
created texture and provides a readable error. Native
compressed sRGB sampling needs an explicit opt-in acknowledging
that the caller's shader performs sRGB decoding; by default
the loader chooses a CPU RGBA8_sRGB fallback where possible.
ASTC has no CPU decoder, so native ASTC support is required.
Caller retains ownership of the returned `TextureHandle`.

**Compatibility:** Existing model sampler2D materials still
use the old first-slice upload path. The new function is
opt-in and does not change assets, saves, ECS or legacy
material shader assumptions.

**Tests:** New CPU mock-device CTest `texture_asset`
checks CubeArray dimension/layer count, uploading twelve
separate mip-1 faces, 3D allocation, failed mip upload
cleanup, invalid input rejection. This test is registered
under `AIVERSE_BUILD_TESTS` alongside `ktx2_layers`.

**Validation:** Source commit successful. Actual C++ build,
unit test execution and GPU validation: **NOT RUN**.
Static review notes `TextureAsset.h` should explicitly
include `<algorithm>` for `std::copy`; next stage
should add this before compilation. No CI pass claimed.

**Next:** Fix missing standalone header include; build
`aiverse_texture_asset_test`/`aiverse_ktx2_layers_test`,
and run Vulkan/Metal/GL native sampling of cube and array
assets. Add DDS multi-face import parity, BasisU transcode
and compressed sRGB variants in separate documented stages.

## Stage 6D — standalone TextureAsset include fix (2026-11-18)

**Code commit:** `4893edd6a155f8e60e866cc79f730d67b7684bee`.
**Path:** `engine/render/TextureAsset.h`.

**Change:** Add direct `#include <algorithm>` for
`std::copy`, rather than relying on transitive includes
from RHI or standard containers.

**Verification:** GitHub source commit confirmed.
Standalone header/native target compilation and tests
**NOT RUN**. No completed successful CI asserted.

**Next:** Run the parser and upload CPU tests and
cross-platform GPU tests, then continue DDS 3D/array
support and remaining format/back-end completeness.

## Stage 6E — document layered KTX2 import and native upload API (2026-11-18)

**Documentation commit:** `6c4e1de4e032aa247af1766bb0dd38552a0c9967`.
**Path:** `docs/texture-rhi.md`.

**Updated architecture:** Replaced obsolete "KTX2 drops array
layers and cube faces" description with actual parser and
generic GPU uploader source status, including authored mip
preservation, first-slice legacy material compatibility,
`decodeBlocksSlice`, compressed sRGB caveats and a native
usage example. DDS still drops layers/faces; BasisLZ/UASTC
transcoding, HDR/EXR and full color-management remain
pending. Distinguishes source implementation from
platform-tested support.

**Validation:** Documentation commit completed;
native builds, CTest, GPU validation **NOT RUN**.
No CI pass asserted.

**Next:** Verify the new `ktx2_layers` and
`texture_asset` tests on a native clone, debug any
C++ compiler issues and test actual GPU sampling.
Then migrate DDS with multi-face/array metadata and
complete the missing render-target and format features.

## Stage 6F — DDS arrays/cubemaps/volumes preserved by a new container reader (2026-11-18)

**Code commit:** `c5faf209d8f7c82e9a3a276337de5219078fcb30`.
**Working branch:** `feature/texture-rhi-unification` (draft PR #21).
**Changed paths:** `engine/render/ImageDds.cpp`, new
`engine/render/DdsTexture.h`, new `tests/render/dds_layers.cpp`,
`CMakeLists.txt`.

**Implementation:** New public `parseDdsTexture(bytes, CompressedImage&, err)`
supports DX10 DDS 2D arrays, cubemaps/cubemap arrays and volume textures,
plus complete legacy cubemaps and 2D cases. It retains all faces/layers/z
slices/mips in `CompressedImage` (mip-major, slice-major within each mip),
transposing native DDS **surface-major mip order** to the shared KTX2
representation. BC1/BC3/BC4/BC7 remain compressed; BC2/BC5 are decoded
per slice into RGBA8, as are common legacy and DX10 8/16/24/32-bit masked
RGBA/BGRA formats. Handles sRGB DX10 variants and BC1 RGB-vs-RGBA
sampling semantics. Enforces dimension/cube-face consistency, mip count,
byte bounds and 512 MiB max decoded allocation per mip. No file format
or save/network/ECS changes; `decodeDds(Model::Image&)` remains the
old first-face 2D material pathway.

**Test source:** Registered `dds_layers` CTest with synthetic DX10 BC1
two-cube array (checks 12 faces and per-mip surface-to-mip reordering),
three-layer RGBA8 array, 3D RGBA8 volume with shrinking depth,
and malformed/partial cubemap/truncated payload rejection.

**Compatibility:** New opt-in container API; existing model importer and
sampler2D textures remain unchanged. Caller may pass the resulting
`CompressedImage` to `uploadKtx2Image` (format-agnostic uploader) for
native dimensional GPU resources, subject to backend capabilities.

**Unsupported:** DDS signed/float/packed DXGI formats not already decoded
in this subset; non-tightly-packed DDS pitch variants; BasisU transcoding;
HDR/EXR; GL/Vulkan MSAA resolves. The function does **not** make all DDS
formats supported, and does not auto-rewrite 2D model shader bindings.

**Verification:** GitHub file/tree/commit/ref update succeeded.
CMake/C++ build: **NOT RUN** (full repository checkout unavailable in
this environment). `dds_layers`, other CTests and GPU validation:
**NOT RUN**. CI last checked: queued (not passing); recheck current SHA.
No build or performance claim.

**Next:** Compile `aiverse_dds_layers_test` and run
`ctest --test-dir build/texture-rhi --output-on-failure -R
'^(dds_layers|ktx2_layers|texture_asset|texture_contract|render_target_set)$'`.
Fix any static/API errors. Add direct `uploadDdsImage` name/format
documentation or alias in the shared `TextureAsset.h`, then test DDS
cubemap and 3D resource creation on Metal/Vulkan/GL. Continue the
separate code-commit + handoff-commit protocol after every stage.

## Stage 6G — shared DDS/KTX2 resource uploader API (2026-11-18)

**Code commit:** `9adeddd31691f98d950ab2f60d1cf155bfed8fd5`.
**Branch:** `feature/texture-rhi-unification` (draft PR #21).
**Changed:** `engine/render/TextureAsset.h`.

**Implementation:** Rename the dimension-aware implementation function
to `uploadContainerTexture(gfx::Device&, CompressedImage&, options, error)`
(the actual signature accepts a const CompressedImage reference), since
both the new `parseDdsTexture` and `parseKtx2` emit the same mip-major
`CompressedImage` representation. Retain an inline
`uploadKtx2Image` wrapper for existing callers and introduce
`uploadDdsImage` for native project consumption, both delegating to
the same implementation. Diagnostic strings now refer to container
data rather than KTX2 exclusively. Memory ownership, CPU fallback,
full mip/layer/face uploads, sRGB options and GPU cleanup semantics
are unchanged. Legacy model `sampler2D` import path is unchanged.

**API/save compatibility:** Old public `uploadKtx2Image` name remains;
new opt-in DDS and generic aliases added. No save/network/ECS changes
or on-disk format changes. No new dependency added.

**Verification:** GitHub source update/commit confirmed. C++ compile,
DDS/KTX2 CTests, GPU sampling and CI completion: **NOT RUN**.
No completed successful GitHub Actions outcome observed.

**Next:** Add a CPU mock-device test exercising
`parseDdsTexture` -> `uploadDdsImage` with a genuine multi-layer
DDS fixture, then run the texture-related CTests on a real native
clone and fix failures before extending backends. Continue separate
source and handoff commits after every bounded stage.

## Stage 6H — DDS DX10 array parser-to-RHI upload integration fixture (2026-11-18)

**Code commit:** `8994da135bd8fcbd91e5fbbcb9de24048c4864ee`.
**Branch:** `feature/texture-rhi-unification` (draft PR #21).
**Changed:** `tests/render/texture_asset.cpp`.

**Test source:** Extend existing mock-Device `texture_asset` CTest
with a synthetic DDS DX10 RGBA8 2D-array fixture (two layers,
two mips stored in DDS surface-major ordering). The test invokes
`parseDdsTexture`, verifies it reorganizes mips into the
shared mip-major `CompressedImage`, calls the new
`uploadDdsImage` public alias, checks the requested
`Texture2DArray` descriptor, layer count, explicit mip count,
and per-layer second-mip subresource uploads. It verifies
the returned GPU handle and cleanup through the mock device.

**Compatibility:** No runtime behavior or native public API
changes in this test-only stage. Model sampler2D path remains
unchanged; the generic uploader stays opt-in.

**Verification:** GitHub source commit confirmed.
Build, CTest `texture_asset`, Vulkan/Metal/GLES GPU test:
**NOT RUN**; no CI passing result claimed.

**Next:** Actually compile and run `texture_asset`,
`dds_layers`, `ktx2_layers`, `texture_contract`,
`render_target_set` on a native clone; fix all failures.
Then validate DDS cube array / 3D on real backends,
complete GL/Vulkan MSAA/resolve, and extend remaining
compressed and HDR pixel formats. After each bounded stage
commit handoff separately with exact SHA and test status.

## Stage 5A — native BC4/BC5/EAC_R11 and BC5 CPU normal reconstruction (2026-11-18)

**Code commit:** `b47def0ba8c23dc284907ce44bc25e74f4d9b2c9`.
**Working branch:** `feature/texture-rhi-unification` (draft PR #21).
**Changed:** `engine/gfx/Gfx.h`, Metal, GL and Vulkan RHI
texture implementations, `engine/render/TextureCodec.h/.cpp`,
`engine/render/ImageDds.cpp`, `engine/render/TextureAsset.h`,
`engine/render/Model.cpp` (nine files).

**Source work:** Append `PixelFormat::BC4`, `PixelFormat::BC5`,
`PixelFormat::EAC_R11` without renumbering existing public enum
entries; add compressed-format validation, native Metal pixel formats
(BC4/BC5 on BC-capable hardware, EAC_R11 on Apple GPUs), GL RGTC1/2
and EAC_R11 compressed enums with capability checks, and Vulkan
BC4/BC5/EAC_R11 `VkFormat` mappings, compressed-image flags and
correct 8/16-byte block validation. Extend `BlockFormat` by appending
BC5 and KTX2 vkFormat 141 mapping; new CPU BC5 block decoder
reconstructs tangent-space positive normal Z to match existing DDS
BC5 CPU fallback. DDS container parser now preserves BC5 blocks
instead of eagerly converting all data to RGBA8. The generic asset
GPU upload helper offers native BC4/BC5/EAC_R11 where supported.

**Important BC5 parity guard:** Native BC5 GPU sampling exposes RG
only; it does not reconstruct positive normal Z. The new
`TextureUploadOptions.shaderReconstructsBc5NormalZ` defaults to
**false**, preserving the existing CPU-decoded RGBA normal-map
behavior; enable it only in consumers whose shaders correctly
reconstruct Z. Existing model GPU compressed path adds BC4/EAC_R11
support but keeps BC5 on the decoded fallback.

**Compatibility:** All existing `PixelFormat` and `BlockFormat`
numeric values are preserved by appending entries. Existing 2D
materials, saves, network, ECS authority and asset schemas unchanged.
No new file dependencies.

**NOT implemented:** sRGB compressed BC native format variants
(other than existing manual shader decode), BC5 signed SNORM,
BC6H, RGBA integer/float variants, HDR/EXR, BasisLZ/UASTC
transcoding or GL/Vulkan MSAA resolves.

**Verification:** GitHub source commit/ref update confirmed.
Native Metal/GL/Vulkan compile, CTest and GPU validation:
**NOT RUN** (repository checkout unavailable through this runtime).
No passing CI result asserted.

**Next:** Add unit tests for BC5 CPU decoded RGBA normal,
KTX2 VK_FORMAT_BC5_UNORM and DDS DX10 BC5 preserved blocks.
Use mock device to prove native BC5 GPU upload is gated by
`shaderReconstructsBc5NormalZ` and that the default path
still reconstructs the Z component. Validate native compiler and
GPU format support. Commit test stage and separate handoff.

## Stage 5B — BC5 CPU/native parity regression fixtures (2026-11-18)

**Code commit:** `b0a1ce1225e1a716d33efb76a02ba395e4544514`.
**Branch:** `feature/texture-rhi-unification`, draft PR #21.
**Changed:** `tests/render/texture_asset.cpp`,
`tests/render/dds_layers.cpp`, `tests/render/ktx2_layers.cpp`.

**Test source added:** Synthetic BC5 RGTC2 blocks with constant
R=255/G=128 are checked through `decodeBlocksSlice` with the
expected reconstructed tangent-space Z≈128. The mock GPU
asset test verifies that default BC5 uploads receive RGBA8
CPU-decoded normals even if the GPU advertises native BC5,
while `shaderReconstructsBc5NormalZ=true` explicitly allows
native `PixelFormat::BC5` compressed uploads. DDS DX10 format
83 blocks remain stored as `BlockFormat::BC5` and decode
to the same channels. KTX2 vkFormat 141 is mapped to BC5
and decoded in CPU fallback. The tests reuse existing
`texture_asset`, `dds_layers` and `ktx2_layers` targets.

**Compatibility:** Test-only stage, no gameplay, native API,
save/network or serialized asset changes. Confirms intended
default BC5 normal-map semantics.

**Verification:** GitHub code commit/ref update succeeded.
C++ compile, CTests, Metal/GL/Vulkan native GPU validation:
**NOT RUN** (full repository clone not available here).
No green CI outcome asserted.

**Next:** Compile and execute all three CPU tests, then run
actual BC4/BC5/EAC R11 sampling on each backend with GPU
capability checks. Audit DDS/KTX2 sRGB handling and BC1_RGB
opaque semantics. Continue per-stage handoff after fixes.

## Stage 6I — update DDS and block-compressed format reference (2026-11-18)

**Documentation commit:** `2e612ecf43acc24dee89e2576651729f1f1391ac`.
**Branch:** `feature/texture-rhi-unification` (PR #21 Draft).
**Changed:** `docs/texture-rhi.md`.

**Documentation:** Explain the now-shared layered KTX2/DDS
`CompressedImage` representation, DDS mip/face ordering
conversion, `parseDdsTexture` and `uploadDdsImage` native
usage with `TextureUploadOptions`. Record BC4/BC5/EAC_R11
source-level capability mapping on Metal/GL/Vulkan, explicit
BC5 normal-Z reconstruction requirement, all remaining
native/format/transcoding limitations, and unrun GPU tests.
The older `decodeDds` 2D model path remains separate.

**Verification:** GitHub documentation commit completed;
C++ build, compiler tests, and native GPU validation:
**NOT RUN**. No green CI outcome asserted.

**Next:** Run new and existing texture-related native CTests
on the current branch and fix errors, then continue GL/Vulkan
MSAA, remaining typed formats and asset pipeline integration.
Update this handoff after every code/documentation stage.

## Stage 5C — shared compressed format metadata and fail-closed GPU allocations (2026-11-18)

**Code commit:** `944b08351b67768aa939bda1c7b9089d6345bd3f`.
**Branch:** `feature/texture-rhi-unification` (draft PR #21).
**Paths:** `engine/gfx/Gfx.h`, `engine/gfx/metal/GfxMetal.mm`,
`engine/gfx/gl/GfxGL.cpp`, `engine/gfx/vulkan/VkResources.cpp`,
`tests/render/texture_contract.cpp`.

**Reason:** A platform backend must never reinterpret compressed
BC4/BC5/BC7/EAC data as an RGBA8 upload when a native enum or GPU
format is absent. Previously Metal could report BC compression on
some Apple devices but its iOS compile-time pixel format mapping
did not expose the BC enum, causing allocation to follow the
uncompressed RGBA8 branch.

**Implementation:** Central `isCompressedTextureFormat`,
`compressedTextureBlockBytes`, `compressedTextureBlockSize`
constexpr helpers define compressed capabilities and 8/16-byte
4x4/6x6 storage requirements. Existing compressed format enum
values remain unchanged. Metal `supports(BC*)` now requires an
actual mapped Metal pixel format, and `createTexture` rejects
unmappable compressed requests before any RGBA8 fallback.
GL 2D and dimensional allocations validate format capability
before issuing compressed GPU calls. Vulkan and Metal reuse
the shared block-size helpers for upload validation.
CPU `texture_contract` test now asserts the new BC4/BC5/EAC
classification and block byte counts.

**Compatibility:** Any previously silently accepted unsupported
compressed format is now rejected explicitly. Existing supported
compressed/uncompressed formats preserve their path. No
save/network/ECS changes or new on-disk formats.

**Verification:** Source committed and branch moved through GitHub.
Native compiler, CTest and GPU checks: **NOT RUN**.
CI last observed queued/cancelled, not green.

**Next:** Test on iOS (particularly GPUs that claim BC support),
macOS, Android GLES3, Windows GL33 and Vulkan with actual
device format queries. Run CTest `texture_contract` and
BC5 DDS/KTX2 tests; fix any compiler/SDK enum errors before
expanding typed pixel formats. Continue separate implementation
and handoff commits after every stage.

## Stage 5D — compressed mip/face payload validation before GPU calls (2026-11-18)

**Code commit:** `d94b2b416763903697fc4f45f0c7314051abe900`.
**Branch:** `feature/texture-rhi-unification` (draft PR #21).
**Paths:** `engine/gfx/Gfx.h`, `tests/render/texture_contract.cpp`.

**Source implementation:** `validateTextureDesc` now rejects a
block-compressed texture unless `levelCount >= 1`, `levelData` and
`levelSize` are present, and every mip has a non-null payload
with enough bytes for all blocks, array layers/cube faces and volume
slices. Arithmetic uses overflow-checked products before comparing
byte sizes. Compressed initial data cannot be mixed with ordinary
`TextureDesc.data`, auto-generated mips, storage images, render targets
or multisample attachments. This common contract runs before GPU
allocation and prevents legacy GL/Metal compressed branches from
reading missing level arrays or uploading truncated face payloads.

**CPU test source:** Extended `texture_contract` with BC5
missing/truncated/valid block cases, compressed mip generation
rejection and six-face compressed cube byte sizing.

**API and compatibility:** No public signatures or enum values
changed. Correct compressed source buffers remain accepted;
invalid/undersized payload requests now fail with
`TextureDescError::InvalidUsage` instead of invoking undefined GPU
behavior. No save/network/ECS change.

**Verification:** Code successfully committed through GitHub.
Native compilation, CTest and GPU checks **NOT RUN** due to unavailable
repository checkout and queued CI; do not report passing tests.

**Next:** Compile `aiverse_texture_contract_test` and actual
client/editor on supported platforms; check that existing compressed
atlas/model callers supply full `levelCount/levelData/levelSize`.
Run Vulkan/Metal/GL compressed texture regression tests, fix any
remaining GPU-specific upload bugs, and continue the other feature
stages only with separate source+handoff commits.

## Stage 5E — single/dual-channel data texture formats (2026-11-18)

**Implementation commit:** `b1da7f8059cc651ba7c240ba5cfea71ce572dd3d`.
**Branch:** `feature/texture-rhi-unification`, draft PR #21.
**Paths:** `engine/gfx/Gfx.h`, Metal/GL/Vulkan RHI texture
implementations, Windows `engine/gfx/gl/GLApi.h` and
`tests/render/texture_contract.cpp`.

**Source implementation:** Append `PixelFormat::RG8`,
`R16F`, `RG16F`, `R32F`, `RG32F` without renumbering
existing public format values. Add shared
`uncompressedTexturePixelBytes` and
`isExtendedDataTextureFormat` helpers. Metal maps native
RG8/R16F/RG16F/R32F/RG32F pixel formats and uses the
accurate CPU row/slice byte stride for uploads/subresources.
GL33/GLES3 maps matching sized internal formats, RED/RG
external formats and byte/half/float transfer types for 2D,
3D and array resources; Windows loader enum coverage
extended. GLES3 float32 formats use nearest filtering by
default on platforms without float-linear support. Vulkan
maps matching VkFormats and staging byte strides. Source
contract test checks all new and existing byte sizes.

**Scope / compatibility:** New formats are for sampled data
textures, LUTs and normal/height/physics data only at this
stage. `renderTarget=true` for these formats intentionally
fails closed until graphics pipelines and render pass
format-matching support them. Existing render target,
save/network/ECS/legacy 2D behavior unchanged.

**Risks requiring next correction:** Metal default sampler
still treats new R32F/RG32F as linearly filterable, while
the conservative GL path forces nearest. Align this policy
and explicitly reject unsupported automatic mip generation
for new typed formats instead of silently allocating a
single level (manual `mipLevels`/subresource updates remain
available). This is the next bounded source stage.

**Verification:** GitHub code commit confirmed. Native
C++ compile, GPU sampling, CTest and CI success:
**NOT RUN**; no successful workflow result asserted.

**Next:** Fix float32 filtering/automatic-mip portability,
add corresponding tests, then validate on Mac Metal,
Windows GL33, mobile GLES3 and Vulkan. Avoid describing
typed render targets as supported before per-format
renderpass tests.

## Stage 5F — conservative typed mip policy and Metal float32 filtering (2026-11-18)

**Implementation commit:** `e17f109aeec3e1333d8b73f8f6779b9d5cfeae08`.
**Branch:** `feature/texture-rhi-unification` (draft PR #21).
**Paths:** `engine/gfx/Gfx.h`, `engine/gfx/metal/GfxMetal.mm`,
`tests/render/texture_contract.cpp`.

**Fix:** `validateTextureDesc` now rejects automatic
`mipmaps=true` for `RG8/R16F/RG16F/R32F/RG32F` until
all three backends have verified mip generation for those
formats. Explicit `mipLevels` with subresource uploads remain
supported, preventing silent fallback to a single mip on
one backend. Metal default sampler now requests nearest
filtering for R32F/RG32F just like its older RGBA32F data
path and the conservative GLES3 float32 policy. Added CPU
contract tests for automatic-mip rejection and explicit
mip-level acceptance.

**Compatibility:** Existing RGBA8/R8/depth/HDR shader paths
and `Texture2D` defaults are unchanged. New typed
formats remain sampleable-only until color-target pipeline
support is intentionally added. No save/network/ECS changes.

**Validation:** GitHub source commit confirmed. Native
Metal C++/Objective-C++ compilation, all CTests and GPU
validation **NOT RUN**. No passing CI claim.

**Next:** Run the typed format contract test and native
2D/3D/array upload/sampling on Metal, GL33, GLES3 and
Vulkan; verify row/slice pitch with actual GPU readback.
Then extend typed color render targets and remaining
integer/depth formats only after building and testing.

## Stage 5G — canonical typed/compressed texture documentation and test commands (2026-11-18)

**Documentation commit:** `9bb6fd6fd5d1ecd1cb09f02241c0efe7831c9a9e`.
**Branch:** `feature/texture-rhi-unification` (Draft PR #21).
**Changed:** `docs/texture-rhi.md`.

**Documentation changes:** Update actual source-stage status to
include the later DDS and typed-format work, describe native
BC4/BC5/EAC R11 with safe compressed mip/face validation,
RG8/R16F/RG16F/R32F/RG32F sampled-only scope, manual
mip upload, and float32 nearest filtering. Clarify remaining
integer/packed/HDR targets, Basis transcoding, MSAA, streaming
and WebGL/Android/desktop GPU gaps. Add exact CMake/CTest
commands for the five texture-focused CPU tests and Vulkan
backend integration tests, including real GPU-vs-skipped
distinction.

**Verification:** GitHub documentation commit completed.
No source C++ files changed in this stage.
Native builds, tests and GPU execution **NOT RUN**;
CI completion not confirmed. Do not infer production readiness.

**Next:** On a native checkout, run listed targeted
CMake/CTest commands and write actual outcomes here.
Fix any compiler/API/test failures before merging.
Then implement remaining GL/Vulkan MSAA and typed/HDR
formats, each with separate code/handoff commits.

## Stage 5H — correct GLES3 float32 mip filter selection (2026-11-18)

**Code commit:** `11aa645dded11c5912988f5af86803a09b3f718f`.
**Branch:** `feature/texture-rhi-unification`, draft PR #21.
**Changed:** `engine/gfx/gl/GfxGL.cpp`.

**Bug fix:** In 2D texture allocation, explicit `mipLevels>1`
previously selected `GL_LINEAR_MIPMAP_LINEAR` solely from the
requested `Filter::Linear`, even when GLES3's float32-format
policy had downgraded min/mag to nearest because linear float
filtering is not portable. Minification now derives mip filter
from the *effective* `f` (GL_NEAREST or GL_LINEAR), so
R32F/RG32F/RGBA32F remain nearest sampled with authored mips.
The dimensional texture allocation path already used this
effective-filter policy.

**Compatibility:** Only affects float32 formats with explicit
mips and a linear requested filter; legacy RGBA8/R8 mip
filtering remains the same. No public API, ECS, save,
network or on-disk asset changes.

**Verification:** GitHub code commit confirmed; native GL
compiler/driver tests and CTest **NOT RUN**.
No passing CI asserted.

**Next:** Validate actual GL33/GLES3 sampler completeness
for explicit float32 mip chains on Android, Windows and
WebGL2, then continue other typed formats and production
GPU validation.

## Stage 6J — compressed allocation failure safely falls back to RGBA8 (2026-11-18)

**Code commit:** `1216ee213a6e12bd91007a0587ddaac21d290c3e`.
**Branch:** `feature/texture-rhi-unification`; still Draft PR #21.
**Changed paths:** `engine/render/TextureAsset.h`,
`tests/render/texture_asset.cpp`.

**Defect found by source review:** `uploadContainerTexture` attempts
native compressed allocation first when the format is advertised.
If `Device::createTexture` rejects that allocation, CPU decode to
RGBA8 was attempted using the same `TextureDesc`, but stale
`levelCount` / `levelData` / `levelSize` still pointed to the BC
payload and `mipLevels` had been cleared to zero. Vulkan treats
explicit level data as an upload for uncompressed formats too: the
second allocation could therefore read compressed bytes as RGBA,
fail bounds validation or produce undefined sampling. GL/Metal also
would not reserve the explicitly authored fallback mip chain.

**Implemented:** After successful CPU decoding and before the second
allocation, reset `levelCount = 0`, `levelData = nullptr`,
`levelSize = nullptr`, restore `mipLevels` from
`CompressedImage::levels.size()`, then set `data` to decoded RGBA
level zero. Existing per-layer/per-z explicit `updateTextureRegion`
for the other mips remains unchanged. The native compressed success
path, old sampler2D material import, save/network/ECS code and
backends are otherwise untouched.

**Added test source:** `texture_asset` mock advertises BC5 support
but refuses a compressed `createTexture` call. The test checks the
BC5 native attempt, RGBA8 retry with no stale compressed pointers,
two allocated mip levels, base-level CPU image bytes and explicit
mip-1 upload. The existing BC5 opt-in/safe decoding paths remain
covered.

**Verification performed:** Reviewed the actual Metal, GL and Vulkan
`createTexture` sources and their mip precedence; both code/test
blobs were committed to GitHub. **C++ compilation: NOT RUN. CTest:
NOT RUN. GPU validation: NOT RUN.** This execution environment cannot
resolve `github.com` for a native repository clone. The existence
of test code does not mean it has passed.

**How to verify on a native checkout:**

```sh
git switch feature/texture-rhi-unification
git pull --ff-only origin feature/texture-rhi-unification
cmake -S . -B build/texture-rhi -DAIVERSE_BUILD_TESTS=ON
cmake --build build/texture-rhi --target aiverse_texture_asset_test
ctest --test-dir build/texture-rhi --output-on-failure -R '^texture_asset$'
```

Also run `texture_contract`, `dds_layers`, `ktx2_layers`,
`render_target_set` and actual Metal/GL/Vulkan allocation and
sampling coverage; investigate any native compiler or driver failures.
Keep PR #21 Draft until verified.

**Next bounded step:** Add a source-reviewed regression for
an *array/cubemap* compressed allocation rejection (not only BC5 2D),
and check integer overflow/budget caps before `TextureUploadDesc`
pixel-size conversions; then prioritize real CTest/GPU builds and
GL/Vulkan MSAA resolve support. Append the next agent handoff after
its code commit; never overwrite earlier stage entries.

## Stage 6K — cube-array CPU fallback parity and malformed metadata gates (2026-11-18)

**Code commit:** `bcc9564e38cc1e59b29dccdfd491626da9115774`.
**Branch:** `feature/texture-rhi-unification` (Draft PR #21).
**Paths:** `engine/render/TextureAsset.h`, `tests/render/texture_asset.cpp`.

**Implementation:** Before `arrayLayers * faces` in
`uploadContainerTexture`, reject a caller-created image with
`arrayLayers > INT_MAX / faces`, preventing signed integer overflow
in the RHI dimension conversion and downstream `levelImageCount`.
Reject a `oneDimensional` container with a non-unit height/depth,
arrays or cube faces rather than silently choosing a conflicting 2D/3D
dimension. This does not change valid DDS/KTX2 imports.

**Regression test source:** Expanded the test mock to inspect ALL
base-mip RGBA8 bytes for array/cubemap/volume allocations and retain
explicit mip upload layer IDs and first texels. Added a 12-face BC5
cube-array test with native compressed allocation rejected: the retry
must preserve CubeArray/12 layers/2 mips, upload 12 distinct decoded
base faces and independently upload all 12 layer mip-1 faces. Added
invalid array-layer overflow and malformed one-dimensional container
rejection tests. This builds upon Stage 6J's descriptor reset fix.

**Validation:** Committed source and tests to GitHub. **Native build:
NOT RUN. CTest: NOT RUN. GPU test: NOT RUN.** Execution environment
still cannot clone from `github.com` for full project compilation.
No claim that the new tests pass.

**Verification instructions:**

```sh
git switch feature/texture-rhi-unification
git pull --ff-only origin feature/texture-rhi-unification
cmake -S . -B build/texture-rhi -DAIVERSE_BUILD_TESTS=ON
cmake --build build/texture-rhi --target aiverse_texture_asset_test
ctest --test-dir build/texture-rhi --output-on-failure -R '^texture_asset$'
```

Also run the grouped texture suite documented in `docs/texture-rhi.md`.
Verify actual GPU BC5/cube-array sampling and Metal/GL/Vulkan
handling of uncompressed mip chains. No ECS/gameplay changes.
**Next:** Move to native compilation/CI and actual GPU tests first,
then GL/Vulkan MSAA/resolve, mixed-format MRT and typed formats in
separate bounded stages. Continue append-only per-stage handoffs,
with exact source SHAs and actual test outcomes.

## Stage 6L — preserve declared Texture3D with a single depth slice (2026-11-18)

**Code commit:** `3adb75295497168b0d271b263ca27a46184e22fa`.
**Branch:** `feature/texture-rhi-unification`, Draft PR #21.
**Files:** `engine/render/TextureCodec.h`,
`engine/render/TextureCodec.cpp`, `engine/render/ImageDds.cpp`,
`engine/render/TextureAsset.h`,
`tests/render/ktx2_layers.cpp`, `tests/render/dds_layers.cpp`,
`tests/render/texture_asset.cpp`.

**Root cause:** Both DDS DX10 `resourceDimension=TEXTURE3D` and
KTX2 `pixelDepth>0` declare a volume texture even when its depth
happens to be **one**. Previously `CompressedImage` stored only
numeric `depth`, and `uploadContainerTexture` inferred Texture3D
only from `depth>1`: authored 3D textures with one z slice were
silently turned into Texture2D.

**Implementation and API:** New additive
`CompressedImage::threeDimensional` boolean (defaults false for
existing manually built 2D images). KTX2 marks it when `pixelDepth`
is nonzero; DDS DX10/legacy parser marks it when the header declares
a volume. Both parsers preserve a true 3D image at depth=1.
The generic uploader chooses Texture3D when either this flag is set
or `depth>1` (for compatible legacy callers). Invalid combinations
(1D/cube/array with declared 3D) fail closed. Native BC paths remain
restricted to 2D/arrays/cubemaps, even when a 3D image has depth=1:
3D compressed textures continue through CPU fallback.

**Test additions (source only):** Synthetic single-slice volume in
`ktx2_layers` and `dds_layers`; invalid 3D/cube KTX2 combination;
`texture_asset` mock checks that a one-slice volume allocates a
Texture3D rather than Texture2D. Existing mock lifetime count
adjusted for the added successful allocation.

**Verification performed:** GitHub code commit succeeded. Local
C++ build **NOT RUN**, `ktx2_layers` / `dds_layers` /
`texture_asset` CTests **NOT RUN**, native GPU sampling **NOT RUN**.
The execution container still cannot resolve GitHub for a native
checkout; no green CI status verified. This is source-level
implementation, not platform certification.

**Next agent should run:**

```sh
git switch feature/texture-rhi-unification
git pull --ff-only origin feature/texture-rhi-unification
cmake -S . -B build/texture-rhi -DAIVERSE_BUILD_TESTS=ON
cmake --build build/texture-rhi --target \
  aiverse_ktx2_layers_test aiverse_dds_layers_test aiverse_texture_asset_test
ctest --test-dir build/texture-rhi --output-on-failure \
  -R '^(ktx2_layers|dds_layers|texture_asset)$'
```

Check actual Vulkan, Metal and GL 3D sampling with depth=1. Run the
remaining target set from `docs/texture-rhi.md` and report genuine
results. Continue with GL/Vulkan MSAA and resolve, mixed MRT and typed
formats only after validating the current source stages. Any
additional implementation must have its own append-only handoff.

## Stage 4R — GL33/GLES3 mixed-format MRT color attachments (2026-11-18)

**Code commit:** `fbbe97fbb030ee03bccbd81f13788b6c61c23903`.
**Branch/PR:** `feature/texture-rhi-unification`, Draft PR #21.
**Changed file:** `engine/gfx/gl/GfxGL.cpp`.

**Change:** Removed the unnecessary requirement that the 1–4 GL MRT
color targets all use the same pixel format. The existing permitted
RGBA8 / RGBA8_sRGB / RGBA16F attachment formats now each retain their
own format in `explicitPassFormats_`. `setPipeline` compares each slot
against `PipelineDesc.colorFormat` or
`PipelineDesc.additionalColorFormats[i-1]`, rejecting mismatches
before draw. The historical automatic HDR shader variant still applies
only to all-RGBA16F passes; mixed HDR/UNORM passes require an explicitly
compatible shader/pipeline. Desktop framebuffer sRGB conversion is
enabled when any color attachment is sRGB; the framebuffer stores
linear/encoded values per actual attachment format.

**Unchanged:** GL attachment count, extent, mip/layer checks, FBO
completeness, independent clear operations, legacy `beginPass`,
same-format MRT and pipeline defaults. GL/Vulkan MSAA/resolve,
per-attachment blend overrides and typed color targets are still
outside this stage.

**Verification:** Source edit and GitHub commit confirmed.
**C++ build NOT RUN; GL33/GLES3/WebGL2 GPU run NOT RUN; tests NOT RUN**,
per requested defer-testing workflow. FBO completeness and shader
linkage must be checked on real drivers. The backend still rejects
unsupported render passes instead of silently changing output count.

**Next:** Add GL MSAA renderbuffer attachment allocation + per-color
resolve with strict sample-count/format validation, and Vulkan MSAA
render pass support; then revisit mixed-format MRT across Vulkan/Metal
and independent blend policy. Keep every subsequent stage's source
commit and append-only handoff commit separate.

## Stage 4S — GL33/GLES3 multisample renderbuffers and per-color resolve (2026-11-18)

**Code commit:** `769f7d9d83ced30b3daf0bcccae642223e3a97e7`.
**Branch/PR:** `feature/texture-rhi-unification`, Draft PR #21.
**Files:** `engine/gfx/gl/GLApi.h`, `engine/gfx/gl/GfxGL.cpp`.

**Implementation:** The GL33/GLES3/WebGL2 portable MSAA path now
creates multisampled `Texture2D` render attachments as native
`GL_RENDERBUFFER` objects. The existing `TextureHandle` stores this
presentation-only renderbuffer and carries its sample count; sampled
binding, updating pixels, and constructing texture views of such
renderbuffers fail explicitly instead of treating renderbuffer IDs as
GL texture IDs. Destruction uses `glDeleteRenderbuffers`.
`GLApi.h` adds Windows GL33 function-loader entries for
renderbuffer creation/deletion/storage/framebuffer attachment.

`beginRenderTargets` validates homogeneous sample counts across
all color and depth attachments; resolve destinations must be distinct
single-sample 2D texture targets with matching mip extent and pixel
format. `createPipeline` checks the sample count against device
`GL_MAX_SAMPLES`; `setPipeline` enforces the active pass sample count.
The FBO attaches renderbuffers or existing mip/layer texture views
as appropriate; attachment formats remain per slot (Stage 4R).

At `endPass`, for every color slot with a resolve destination,
a temporary single-sample draw FBO receives a
`glBlitFramebuffer(..., GL_COLOR_BUFFER_BIT, GL_NEAREST)` from the
selected multisample source color attachment. This uses ES3/GL33
renderbuffers and blit rather than unsupported ES3 multisample
sampled texture objects. The legacy `copyColor` path refuses raw
renderbuffer handles; `RenderTargetSet::sampledColor(i)` yields the
allocated single-sample resolve texture. Transient MSAA outputs
without an explicit resolve remain un-sampleable.

**Compatibility and limits:** RHI `TextureDesc` validation already
requires 2D, renderTarget=true, no initial data/storage/mips for MSAA.
Only RGBA8/RGBA8_sRGB/RGBA16F color and Depth targets are wired;
the GPU may still reject a requested format/sample count. GL per-slot
resolve supports selected 2D mip destinations, but not layered
multisampled textures or depth resolve. State and FBO cleanup are
implemented in source; actual behavior on GL33, GLES3, WebGL2 and
Android remains unverified.

**Verification:** Connector wrote code commit and updated branch.
**No build executed. No CTest executed. No GPU rendering executed.**
No passing CI results asserted.

**Next:** Implement Vulkan MSAA attachment allocation/pipeline and
per-color renderpass resolves, plus native GPU verification on Vulkan
and GLES3/WebGL2. On macOS/Windows/Android tests, verify no GL errors,
sample-count mismatch rejection, multi-target clear/resolve, sRGB
conversion, depth attachments, and lifetime cleanup. Revisit mixed
attachment blend policies and sample-count flexibility separately.
Always append a new handoff after each code stage.

## Stage 4T — Vulkan mixed-format MRT renderpass and pipeline compatibility (2026-11-18)

**Code commit:** `2060e448c75550b789a3706072f55f99a1f2b42d`.
**Branch/PR:** `feature/texture-rhi-unification`, Draft PR #21.
**Files:** `engine/gfx/vulkan/VkDevice.h`, `engine/gfx/vulkan/VkDraw.cpp`.

**Implementation:** Vulkan MRT pass validation no longer rejects
different formats in separate color output slots (within the already
supported RGBA8/RGBA8_sRGB/RGBA16F set). MRT renderpass creation and
cache keys now hold the full fixed-size array of per-attachment
`VkFormat` rather than assuming all colors share attachment zero's
format. `VkAttachmentDescription::format` is populated per slot.
The per-frame `passFormats_` records the active pass formats; Vulkan
graphics pipeline variant cache keys additionally compare every
attachment format to avoid an incompatible pipeline being reused for
the same first output format. `setPipeline` checks
`PipelineDesc.colorFormat` and `additionalColorFormats` against the
corresponding actual pass slots, preserving legacy single-output
permissiveness. Automatic HDR specialization applies only when *all*
active color outputs are RGBA16F; heterogeneous passes require an
explicit matching pipeline. No save, ECS, material-cache or
replication changes.

**Limitations:** Only matching attachment dimensions and single
samples are supported by this Vulkan source stage. Per-target
blend policies are still global. Texture create/resolve and pipeline
support for MSAA have not been implemented in Vulkan yet. The loader
and renderpass API remain backward compatible with single-format MRT.

**Verification:** GitHub source update and branch commit completed.
**C++ compilation NOT RUN, Vulkan validation NOT RUN, CTest NOT RUN**,
as requested for deferred testing. Mixed format renderpass
compatibility and cache behavior must be checked with validation
layers and a real Vulkan driver before approval.

**Next stage:** Vulkan multisample image creation + renderpass
resolve attachments and pipeline raster sample matching. Add
GPU fixtures for 2–4 mixed-format MRT output and verify proper
pipeline cache separation. Follow with storage images and
streaming/readback features in separate bounded stages, each
documented immediately afterward.

## Stage 4U — reusable RenderTargetSet per-attachment format policy (2026-11-18)

**Source commits:** `69ee1e1ac6a25863d3b6680e639a06d1e8c10300`
(native owner) and `2f605fe3d231d571a79644b4ca889a7857c45b66`
(regression test source).
**Branch:** `feature/texture-rhi-unification`, Draft PR #21.
**Files:** `engine/gfx/RenderTargetSet.h`,
`tests/render/render_target_set.cpp`.

**API:** `RenderTargetSet::Config` gains
`bool perAttachmentFormats = false` and
`std::array<PixelFormat, kMaxRenderTargetColors> colorFormats`.
The previous `colorFormat` still configures **every** color target by
default. When per-attachment formats are enabled, a separate format
is selected for each slot at allocation and verified before touching
the GPU. The conservative backend-portable set is RGBA8,
RGBA8_sRGB and RGBA16F; unsupported compressed/typed formats fail
closed. Each MSAA resolve target gets its corresponding color
format. The new `colorFormat(index)` accessor reports the expected
pipeline output slot format. Depth-only sets now validate their
depth descriptor independently, without treating color slot zero
as an allocated attachment.

**Test source:** Mock device now advertises the declared color
format capabilities. New `mixedFormats()` tests a three-color
RGBA8/RGBA16F/RGBA8_sRGB target set with depth, per-slot owner
metadata, pass entry, correct lifetime cleanup, fail-before-allocation
on BC7 MRT and depth-only creation. Prior allocation rollback and
MSAA tests remain in place.

**Verification:** GitHub code/test commits confirmed.
**Build NOT RUN; CTest NOT RUN; Metal/GL/Vulkan GPU test NOT RUN.**
Backend runtime support and mixed-format shader output compatibility
must still be checked on real devices.

**Native caller contract:** Configure compatible
`PipelineDesc.colorAttachmentCount`, `colorFormat`, and
`additionalColorFormats` matching `targets.colorFormat(i)`.
Use explicit MRT fragment output locations 0..N-1 and
`targets.sampledColor(i)` when MSAA resolves are requested.
Continue recording architecture/API/files/actual tests and next work
after each bounded step. Next: Vulkan MSAA/resolve; streaming/upload
lifecycle and import format coverage are still unfinished.

## Stage 4V — Vulkan multisample image allocation and capability gates (2026-11-18)

**Code commit:** `d8a539959793e59d45bddcb2a9ab1a5da3e97395`.
**Branch/PR:** `feature/texture-rhi-unification`, Draft PR #21.
**Files:** `engine/gfx/vulkan/VkDevice.h`,
`engine/gfx/vulkan/VkResources.cpp`.

**Source implementation:** `VTexture::samples` stores the physical
sample count; `createImage` uses that count in
`VkImageCreateInfo::samples`. `createTexture` can now allocate
render-only multisampled Texture2D color (RGBA8/RGBA8_sRGB/RGBA16F)
and depth attachments, checking both physical-device framebuffer
sample masks and `vkGetPhysicalDeviceImageFormatProperties`.
Multisample images use attachment-only VkImageUsage rather than the
usual sampled/upload path, and skip sampler creation, initial staging,
and shader-read layout initialization. They are intended to be cleared
and resolved through a later Vulkan render-pass stage, not bound
directly as sampled textures. Pixel updates and texture views reject
multisampled parents. `destroyTexture` continues to retire images
and memory using the existing deferred GPU-safe release mechanism.

**Critical integration gap:** This is an allocation stage ONLY.
`VkDraw.cpp` still rejects explicit MSAA resolves and does not yet
build sample-count-compatible renderpasses or pipelines. The new
resource is not usable as a complete render target until Stage 4W.
Do not claim MSAA Vulkan is ready, even for a single attachment.

**Verification:** Source commit to GitHub succeeded. C++ compile
**NOT RUN**, CTest **NOT RUN**, Vulkan device/validation **NOT RUN**.
Native verification must check imageProps queries, allocation failure,
memory release and compatibility with RenderTargetSet on Vulkan.

**Next:** Build Vulkan `VkSubpassDescription::pResolveAttachments`
with an explicit per-slot resolve mask, sample-matched color/depth
attachments, pipeline multisample state and cache keys; all scoped
resolve image views/framebuffers must be deferred until GPU completion.
Then real validation layers and sampling tests. Record next code SHA
and real verification status in a new append-only handoff entry.

## Stage 4W — Vulkan MSAA renderpasses, resolves and pipeline matching (2026-11-18)

**Code commit:** `fdf75870cbdfb43d50ede67cacbcad1a4bbcdd99`.
**Branch/PR:** `feature/texture-rhi-unification`, Draft PR #21.
**Files:** `engine/gfx/vulkan/VkDevice.h`,
`engine/gfx/vulkan/VkDraw.cpp`.

**Source implementation:** Complete the Stage 4V allocation half at
render-pass level. Vulkan `renderPassForMrt` now takes the physical
sample count and a per-color resolve mask, included in its cache
key. The renderpass declares N multisample color attachments, then
one `VK_SAMPLE_COUNT_1_BIT` attachment for every requested resolve
slot, then optional same-sample depth. Subpass
`pResolveAttachments` maps color outputs to their correct resolve
slot, with `VK_ATTACHMENT_UNUSED` where no resolve exists.
The framebuffer builds the matching temporary 2D image views in
physical attachment order; all color, resolve and depth views
are deferred until after GPU execution completes.

`beginRenderTargets` checks sample counts across color/depth,
the resolved image's format, extent, mip, dimensionality, and
single-sample usage, rejects aliasing resolve destinations, and
requires clear=true for MSAA render attachments while there is no
persistent multisample layout tracker. A depth-only MSAA pass is
supported with a color count of zero. Native renderpasses select
attachment layouts and renderpass dependencies; the resolved
single-sample images return to SHADER_READ_ONLY_OPTIMAL.

`PipelineDesc.sampleCount` may now request supported
1/2/4/8/16x counts. Pipeline cache and `VkPipelineMultisampleStateCreateInfo`
track actual sample count and resolve topology. `setPipeline`
rejects mismatched sample counts. Legacy beginPass deliberately
skips multisampled resources with an error; callers use
beginRenderTargets. Direct sampling of an MSAA-only texture binds
a dummy and reports an error; sample the resolve destination.

**Restrictions:** Source-level only; Vulkan GPU tests/validation
not yet run. There is no depth resolve, no layered multisampling,
no non-cleared persistent multisample pass load, and no storage
image support in this stage. Mixed-format color MRT remains
supported as in Stage 4T. All existing Texture2D 1x pass paths
and native ECS game code remain unchanged in intended behavior.

**Actual verification:** GitHub code/tree/commit/ref update
**SUCCEEDED**; C++ native build **NOT RUN**; CTest **NOT RUN**;
Vulkan validation-layer GPU test **NOT RUN**. Do not merge Draft PR
on the basis of this source-only change.

**Next:** Add Vulkan device fixture with 2x/4x two-color clear,
two independent resolves, depth attachment, mixed HDR/UNORM and
shader sampling of resolved images. Run compiler + validation layers
and fix any errors. Then complete remaining import formats, async
readback/streaming and native editor inspection in separate stages,
each followed by its own handoff.

## Stage 4X — Vulkan 4x MSAA two-color resolve integration fixture (2026-11-18)

**Code commit:** `a5009a998417f3e042044f7d8f484f8a07f65879`.
**Branch:** `feature/texture-rhi-unification` (Draft PR #21).
**Files:** `tests/render/vulkan_tests.cpp`.

**Source:** New Vulkan GPU fixture allocates two 4x multisample RGBA8
render targets, one 4x depth attachment and two separate 1x resolve
textures. It first renders independent red and green clears and samples
both resolved textures into opposite screen halves for pixel assertions.
It then tests a 4x `PipelineDesc.colorAttachmentCount=2` fragment shader
writing blue/yellow into the two MRT slots, verifies the resolve outputs
again and destroys every resource. The fixture prints an explicit skip if
4x image creation is unsupported on the selected GPU; that skip is NOT a
passing MSAA validation. It exercises the already-implemented 4V/4W
Vulkan source stages without duplicating the other agent's code.

**Compatibility:** Test-only source stage. No new public RHI API, ECS, save,
network or runtime rendering changes.

**Actual validation:** GitHub code commit succeeded. Native compiler,
`aiverse_vulkan_tests`, Vulkan validation layers and GPU image
assertions: **NOT RUN** (the execution container has no git checkout
and cannot resolve github.com for clone). GitHub Actions latest results
were queued, not passed. Do not claim Vulkan MSAA is validated.

**Next:** Build `aiverse_vulkan_tests` and execute
`ctest --test-dir build/texture-rhi --output-on-failure -R '^vulkan_backend$'`
on real Vulkan SDK/Lavapipe, inspect validation errors and screen captures.
Then complete GL/GLES MSAA and cross-platform regression tests.
Commit the next code stage and a separate handoff as always.

## Stage 5I — BC6H signed/unsigned HDR container and RHI contracts (2026-11-18)

**Code commit:** `bdbc6f618a6419801757169de65bdbec69b8b2a3`.
**Branch/PR:** `feature/texture-rhi-unification`, Draft PR #21.
**Files:** `engine/gfx/Gfx.h`, `engine/render/TextureCodec.h`,
`engine/render/TextureCodec.cpp`, `engine/render/TextureAsset.h`.

**Implemented source contract:** Added
`gfx::PixelFormat::BC6H_UFloat` and `BC6H_SFloat` as 4×4
16-byte compressed GPU formats and matching `BlockFormat` values.
KTX2 VkFormats 143 (UFLOAT) and 144 (SFLOAT) now preserve native
BC6H compressed levels, faces and layers in `CompressedImage`.
`uploadContainerTexture` maps both to the corresponding GPU format.
BC6H represents HDR float radiance, so there is intentionally
**no unsafe RGBA8 CPU decoder/fallback**: when the backend rejects
native support, import fails explicitly instead of truncating HDR.
The common compressed mip/layer payload validator applies.

**Integration note:** This is the format/codec stage. Native
Metal/GL/Vulkan mappings and DDS DX10 95/96 are the separate next
stage, so do not claim native HDR BC6H sampling yet. Existing
2D material import, ECS, saves and unrelated formats are unchanged.

**Verification:** GitHub code commit confirmed. **Build NOT RUN,
CTest NOT RUN, GPU validation NOT RUN**, per the deferred testing
request. Test source and GPU fixture work is still pending.

**Next:** Add BC6H_UFloat/SFloat to native Metal pixel formats
(macOS only), Vulkan VkFormat, GL BPTC compressed enums and
capability gates, DDS DX10 layered parser, and synthetic import
tests. Record this as Stage 5J with its own handoff immediately.

## Stage 5J — native BC6H HDR sampling and DDS DX10 import metadata (2026-11-18)

**Code commit:** `db07550957cb09e710eefdfd7492fa6687224842`.
**Branch/PR:** `feature/texture-rhi-unification`, Draft PR #21.
**Files:** `engine/render/ImageDds.cpp`,
`engine/gfx/gl/GfxGL.cpp`, `engine/gfx/metal/GfxMetal.mm`,
`engine/gfx/vulkan/VkResources.cpp`.

**Implementation:** Finish the Stage 5I format contract. DX10
DDS layered parser recognizes 95/96 (`BC6H_UF16` /
`BC6H_SF16`) and preserves native mip-major compressed blocks
across array/cube faces as `BlockFormat::BC6H_UFloat/SFloat`.
The classic DDS-to-`Model::Image` 2D RGBA8 adapter deliberately
remains unchanged: it has no decoder for HDR BC6H.

GL33/GLES3 backend maps to BPTC unsigned/signed float 4×4
compressed enums, gated on the BPTC extension. Vulkan maps to
`VK_FORMAT_BC6H_UFLOAT_BLOCK` and
`VK_FORMAT_BC6H_SFLOAT_BLOCK`, with existing format-capability
query. Metal maps macOS BC6H formats (Ufloat/RGBFloat), using
the existing BC support probe and failing closed on iOS.
All backends retain the complete compressed mip payload contract.
The content requires a GPU with native BC6H support; no CPU HDR
fallback is silently substituted.

**Verification:** GitHub source commit succeeded. **No native
build, CTest, or GPU texture sampling run.** Test source for synthetic
BC6H DDS/KTX2 and actual macOS/Windows/Vulkan device sampling is
pending, along with other HDR/EXR import features.

**Next:** Add synthetic unsigned/signed BC6H import, byte-bound,
and GPU capability fallback coverage, and a native shader capture
with HDR radiance values above 1.0. Then implement HDR/EXR
uncompressed float import and runtime texture streaming. Continue
append-only per-stage handoffs.

## Stage 7A — bounded texture streaming queue API (2026-11-18)

**Implementation commit:** `0aea8701e634c89031327576a46fc67b52ab2d47`.
**Branch:** `feature/texture-rhi-unification`, PR #21 Draft.
**Changed file:** `engine/gfx/TextureStreaming.h` (new, header-only).

**Implementation:** Public genre-independent `gfx::TextureStreamQueue`
owns its created sampled, uncompressed GPU textures, creates monotonic
logical `TextureStreamId` tokens independent of backend `TextureHandle.id`,
and accepts `TextureUploadDesc` with caller data copied into queue-owned CPU
staging before the call returns. It validates the texture dimension, format,
mip/layer/3D bounds, row/slice pitches, payload byte size and safe arithmetic,
and enforces configurable total GPU memory, CPU staged memory, per-frame
transfer bytes, and per-frame upload counts. `drain()` runs FIFO on the GPU
device thread **outside** render passes, copying queued subresources through
`Device::updateTextureRegion`; if a GPU write fails it marks its logical
resource failed and cancels remaining queued writes. `cancel(ticket)`,
`destroy(id)`, `shutdown()`, `texture(id)` and `stats()` give ownership,
cancelation, diagnostics, and cleanup. Logical IDs are never recycled even if
the GPU backend reuses texture IDs, preventing pending data from landing on
a replacement texture. No extra dependencies; this is presentation-only and
usable in native game/project C++.

**Contract/limits:** This is **deferred frame-budgeted streaming**, not
background I/O or asynchronous GPU readback. Only uncompressed, sampleable,
non-MSAA textures and explicit manual mip allocations; no compressed partial
updates, storage write textures or virtual page tables. The `gfx::Device`
must outlive the queue, and its `updateTextureRegion` must consume/copy
the provided CPU bytes synchronously before returning. Queue calls must be
made on the rendering thread; concurrent submissions are not supported.
Destroy via this queue, not directly through the backend, to retain logical
generation ownership.

**Compatibility:** Standalone opt-in header; no existing `gfx::Device`
signature, save/network/ECS or renderer path changes.

**Verification:** GitHub source code/tree/ref update succeeded.
No native build, CTest or GPU execution took place because this environment
does not have a local AIVERSE clone and cannot resolve github.com.
CI jobs for earlier branch HEADs were queued, not passing. **NOT RUN**.

**Next:** Add `aiverse_texture_streaming_test` using a mock gfx::Device and
assert CPU buffer ownership, strict per-frame byte/update budgets, cancellation,
allocation rollback, row/slice validation, failed update quarantine and stale
resource-ID rejection. Execute compiler/CTest on a native checkout and write
actual outcomes. Afterwards consider optional readback and streaming residence
policy, keeping code and handoff as separate commits.

## Stage 7B — CPU staging and streaming scheduler regression tests (2026-11-18)

**Code commit:** `3ee65b1b522b8b40250889eb98cfc3436eaf2c3f`.
**Branch:** `feature/texture-rhi-unification`, Draft PR #21.
**Paths:** `tests/render/texture_streaming.cpp`, `CMakeLists.txt`.

**Test source:** Register GPU-independent `texture_streaming` CTest.
A mock gfx::Device verifies bounded texture GPU allocation, staging
byte caps, per-frame transfer/update limits, deep-copy of caller-owned
CPU source before it changes, sequential FIFO drains, ticket
cancellation, logical IDs not reused, stale-token rejection, cleanup
on shutdown, 3D volume upload with padded rows/slices, out-of-bounds
and truncated upload rejection, GPU update failure quarantine and
cancellation of subsequent jobs for the failed texture. The mock
uses the public Device interface so it also tests native C++ include/
link viability once compiled.

**Runtime/code changes:** Test-only, apart from registration in
CMake. Existing render paths, serialization, ECS/gameplay, and
resources unchanged. Staged uploads remain render-thread
synchronous GPU transfers, not actual asynchronous readbacks.

**Verification:** GitHub source commit/ref update succeeded.
Native C++ compilation and CTest execution: **NOT RUN** (no checkout;
container network DNS cannot resolve github.com). CI not confirmed
passing. Do not report fixture green until actually executed.

**Next:** Run
`cmake --build build/texture-rhi --target aiverse_texture_streaming_test`
and
`ctest --test-dir build/texture-rhi --output-on-failure -R '^texture_streaming$'`.
Correct compiler/behavior failures. Then add streaming texture
residency/LOD policy and a separate skill for native game agents;
continue with GL MSAA/resolve and editor resource views in
independent stage/handoff commits.

## Stage 5K — portable Radiance RGBE HDR import to RGBA16F (2026-11-18)

**Source commit:** `e2fb7b723ef6e214ff2709b16858e3c9a34f074f`.
**Branch/PR:** `feature/texture-rhi-unification`, Draft PR #21.
**New file:** `engine/render/HdrTexture.h`.

**Source implementation:** Header-only native C++ Radiance RGBE
loader and RHI upload utility. `parseRadianceHdr(std::string_view,
RadianceImage&, std::string&)` reads the Radiance / RGBE header with
`FORMAT=32-bit_rle_rgbe`, supports flat scanlines and modern
per-channel RLE scanlines, and preserves +/- horizontal and vertical
orientation into a canonical image with linear RGBA16F texels.
`RadianceImage` contains width, height and packed IEEE-754
binary16 RGBA texels; alpha is 1.0. File extent, scanline length,
packet lengths and a 512 MiB output budget are checked before
allocation. `uploadRadianceHdr(Device&, image, error)` calls
`Device::createTexture` using `gfx::PixelFormat::RGBA16F`,
checking format support; caller owns the returned TextureHandle.
Color values above 1.0 remain linear HDR; no tone mapping is
applied. This is presentation-only, and does not change ECS, saves
or existing model-material paths.

**Explicit gaps:** OpenEXR, legacy Radiance repeat-marker scanlines,
arbitrary XY-major orientations, and applying exposure/gamma metadata
are not supported. Do not claim complete HDR/EXR format coverage;
this stage specifically imports Radiance RGBE. The import's RLE,
rounding and GPU upload behavior still require verification.

**Verification:** GitHub source commit created successfully.
C++ build **NOT RUN**, CTest **NOT RUN**, GPU render **NOT RUN**,
per user's request to finish feature source before testing.

**Next:** Add source-level test fixture and convenience decode+upload
function, fix any code-review findings, then introduce optional
OpenEXR scanline parsing and resource streaming/readback phases with
separate source/handoff commits.

## Stage 5L — Radiance HDR one-call upload and half boundary correction (2026-11-18)

**Source commit:** `d0802baf79dd9e5177d8e9231dcab83abb8a4b9e`.
**Branch/PR:** `feature/texture-rhi-unification`, Draft PR #21.
**File:** `engine/render/HdrTexture.h`.

**Change:** Fix the IEEE binary16 subnormal-to-normal rounding
transition: values that round to 1024 units of 2^-24 produce the
smallest normal half (`0x0400`), rather than being incorrectly
clamped at `0x03FF`. Add
`loadRadianceHdr(gfx::Device&, std::string_view, std::string&)`
as a convenience native C++ API combining bounded
`parseRadianceHdr` and `uploadRadianceHdr`. The CPU scratch
lifetime is scoped to the call; GPU ownership remains with the
caller. Existing two-step API remains intact.

**Verification:** Source commit on GitHub verified.
**C++ build NOT RUN; CTest NOT RUN; GPU test NOT RUN** per the
user's defer-testing decision.

**Next:** Add a standalone source fixture and CMake test registration
for flat RGBE, RLE, orientation, HDR values, malformed input,
binary16 boundary and device upload/failure; record exact new
source commit and deferred test status in the next handoff. After
that tackle EXR import and texture readback/streaming coverage.

## Stage 5M — standalone Radiance HDR CPU/GPU-contract test source (2026-11-18)

**Source commit:** `0382dcd42e25c26523d4b737d44d9349e1ee361d`.
**Branch/PR:** `feature/texture-rhi-unification`, Draft PR #21.
**Files:** New `tests/render/hdr_texture.cpp` and `CMakeLists.txt`.

**Implementation:** Registered the GPU-independent CTest target
`aiverse_hdr_texture_test` / `hdr_texture` under
`AIVERSE_BUILD_TESTS AND NOT EMSCRIPTEN`. The new synthetic
fixture source constructs flat RGBE with radiance >1.0, a modern
per-channel RLE scanline, flipped horizontal orientation, truncated
payloads, invalid run-length packets, oversized image metadata,
and a binary16 subnormal-to-normal boundary. A mock `gfx::Device`
captures RGBA16F upload bytes and verifies caller GPU ownership,
unsupported-format early refusal and failed allocation reporting.
The tests are self-contained (no external .hdr asset).

**Execution:** Source and CMake registry commit confirmed on GitHub.
**No CMake configuration, compilation, CTest or real GPU tests ran**,
per user's plan to finish feature source before verification.
Do not claim the fixture passes.

**How to run after implementation is complete:**

```sh
cmake -S . -B build/texture-rhi -DAIVERSE_BUILD_TESTS=ON
cmake --build build/texture-rhi --target aiverse_hdr_texture_test
ctest --test-dir build/texture-rhi --output-on-failure -R '^hdr_texture$'
```

**Next:** Add optional OpenEXR uncompressed scanline support
(half/float RGB[A] channels), and integrate texture load into
project-native APIs; keep unsupported EXR compression explicit.
Work on background streaming and portable readback as separate
source stages with handoff after each.

## Stage 7C — post-drain streaming completion notifications (2026-11-18)

**Code commit:** `a84382bbae16b89cd8f5c99afb3c59f09b092be3`.
**Branch:** `feature/texture-rhi-unification`, draft PR #21.
**Files:** `engine/gfx/TextureStreaming.h`,
`tests/render/texture_streaming.cpp`.

**Implementation/API:** `gfx::TextureStreamQueue::drain` accepts an optional
`CompletionFn` receiving `(TextureUploadTicket,TextureStreamId,bool success)`
for each GPU update attempted in that frame. Results are accumulated during
the bounded FIFO transfer loop and callbacks are called **after** all queue
iteration is finished. This avoids iterator invalidation and supports safe
resource destruction or scheduling new uploads from completion callbacks.
Callbacks execute on the GPU device/render thread, not asynchronously or
on background workers. Existing no-arg `drain()` callers are source
compatible. Cancelled jobs do not produce a GPU completion callback;
`cancel` and `destroy` remain explicit.

**Tests added:** Mock-device streaming fixture now validates two successful
per-ticket completions in submission order, matching stable logical
resource IDs, and a failed GPU submission callback observing its resource
quarantined before notification.

**Compatibility:** Header-only optional API; no new backend virtuals, ECS,
save/network or on-disk formats. The GPU ownership and byte budgets from
stages 7A–7B remain unchanged.

**Verification:** GitHub code/tree/branch update confirmed. Native C++
build, CTest, graphics device validation: **NOT RUN**. Latest GitHub
Actions results have been queued, not confirmed passing. This is an
unverified source change, not a production-ready streaming system.

**Next:** Implement a CPU-only per-mip/per-array-layer residency planner
that requests complete coarse-to-fine mips and maps completion success
to safe sampler LOD clamps. Add deterministic tests, then connect it to
streaming queues. Run the actual native tests and GPU sampling.

## Stage 5N — bounded scanline OpenEXR HALF/FLOAT import (2026-11-18)

**Source commit:** `29bb2e6887305b3ac15adc4e7ad57a9c710e7de0`.
**Branch/PR:** `feature/texture-rhi-unification`, Draft PR #21.
**New file:** `engine/render/ExrTexture.h`.

**Implementation:** Header-only native C++ uncompressed OpenEXR
reader `parseOpenExr(std::string_view, RadianceImage&, std::string&)`
and one-call `loadOpenExr(Device&, bytes, error)`, reusing the
Stage 5K RGBA16F GPU uploader. Supports OpenEXR version 2,
single-part, untiled, **uncompressed** scanlines with arbitrary
channel order and full-resolution planar R/G/B and optional A
HALF (binary16) or FLOAT (binary32) data; unrelated channels are
skipped according to declared byte stride. Honors dataWindow
origin (including negative offsets), chunk scanline offsets and
out-of-order physical scanlines. Missing alpha becomes 1.0;
negative linear radiance is retained with its sign. Disallows
duplicate RGB channels, nonfinite pixel values, integer RGB
channels, subsampled channels, corrupt offsets/row sizes, huge
attributes and 512 MiB+ decoded allocations. Source is
presentation-only; neither ECS nor legacy material import changes.

**Explicit limitations:** This does NOT implement general OpenEXR:
ZIP/ZIPS/PIZ/RLE/DWAA/DWAB/B44/B44A or PXR24 compression,
deep/tiled/multipart images, multi-resolution/mipmap level
files, arbitrary channel naming (requires bare R/G/B), color
management/chromaticities or half-float texture array imports.
These inputs fail closed with errors instead of being silently
converted. No third-party OpenEXR codec dependency introduced.

**Verification:** GitHub source commit confirmed. **C++ compilation
NOT RUN, CTest NOT RUN, GPU test NOT RUN** (requested deferred tests).
Needs synthetic scanline fixtures, malformed payload tests and
actual macOS/GL/Vulkan rendering before production approval.

**Next:** Add EXR fixture source and CMake target without executing
tests; then document EXR/HDR public APIs and plan compressed EXR
integration using a vetted decoder, plus texture readback and
resident streaming. Each stage must get an append-only handoff.

## Stage 7D — generation-safe mip residency planner (2026-11-18)

**Code commit:** `3fba022b93aeec0e865f2941dbcb96cf41a3ed35`.
**Branch:** `feature/texture-rhi-unification`, Draft PR #21.
**File:** new `engine/gfx/TextureMipResidency.h`.

**Source implementation/API:** `gfx::TextureMipResidency` is a CPU-only
presentational mip residency scheduler for `TextureDesc` resources.
`initialize` validates the original descriptor and builds a 0/1/2
absent/queued/resident state per mip and per layer/face (volume textures
use one complete volume per mip). `claimNext` prioritizes coarsest
mip and then layer order. `complete(TextureMipRequest,bool)`
marks upload success or makes a failed slice retryable; `invalidate`
clears residency. Requests contain a generation token so a delayed GPU
completion from a destroyed/reinitialized resource cannot corrupt the
replacement mip state. `completeMip`, `finestResidentMip`,
`sampleable`, `fullyResident`, `pendingCount` and
`residentSliceCount` provide explicit observability.
`residentSampler` returns a validated `SamplerDesc` clamped to the
contiguous coarsest-to-finest mip range, and returns false until all
faces/layers of the coarsest level are loaded. Avoids sampling
undefined uninitialized GPU levels. It does not own GPU textures;
connect it to `TextureStreamQueue` results from Stage 7C.

**Safety/limits:** Only CPU metadata; no virtual page tables or GPU
feedback, no new RHI virtual methods, no independent I/O worker.
The renderer must not bind a texture for sampling if `sampleable` is
false. Stream queue and residency planner must be used from the
render/device thread. Existing ECS, saves/network and model materials
are unchanged.

**Validation:** GitHub source commit and branch update succeeded.
Native compiler/CTest/GPU tests **NOT RUN** because no repository
checkout is available in this environment. GitHub Actions for earlier
commits remained queued/cancelled; no green claim.

**Next:** Add a mock CPU test covering array face-complete readiness,
coarse-to-fine claim ordering, failures/retries, stale completion after
reset, 3D volume grouping and exact LOD clamp. Then document native
agent usage and optional integration with TextureStreamQueue's
completion callback. Keep code and handoff as separate commits.

## Stage 7E — mip residency and sampler LOD contract tests (2026-11-18)

**Code commit:** `e531f0bb38e2a4d61ea6398d018bb18d4d02aee2`.
**Branch:** `feature/texture-rhi-unification` (PR #21 Draft).
**Paths:** `tests/render/texture_mip_residency.cpp`, `CMakeLists.txt`.

**Test source:** New GPU-independent `texture_mip_residency` CTest target.
The fixture tests a six-face cubemap with four mips: claiming all
coarsest faces first, refusing sampling while one face is still
pending, clamping a sampler to the sole completely resident mip,
expanding sampler range only when all faces of the next mip complete,
making failed uploads retryable, refusing duplicate GPU completion,
and invalidating a coarse face to disable sampling again. Also
tests 3D volume grouping as one request per complete mip,
generation-safe rejection of late completion after reset,
invalid texture extents, single-mip fully-resident behavior
and sampler descriptor validation.

**Public API change:** None beyond the CPU-only
`TextureMipResidency` from Stage 7D. No ECS/save/network/render-path
changes. The test does not imply virtual texture GPU pages or
asynchronous device readback are implemented.

**Actual verification:** GitHub source/tree/commit/ref update
succeeded. Native C++ compilation and CTest **NOT RUN** due to
unavailable full checkout in this execution environment;
no Vulkan/Metal/GL GPU tests run and CI not confirmed green.

**Next:** Build `aiverse_texture_mip_residency_test` and run
`ctest --test-dir build/texture-rhi --output-on-failure
-R '^(texture_streaming|texture_mip_residency)$'`.
Then wire a native sample using `TextureStreamQueue.drain(callback)`
to `TextureMipResidency.complete` and update an independent
sampler only after complete mip/face residency. Test real GPU
LOD bounds, including stale resource destruction and reset.
Continue source/handoff commits separately.

## Stage 7F — native texture RHI skill and AGENTS.md registration (2026-11-18)

**Code/documentation commit:** `b122e213c894c63846a95fe895312ba89f2a9705`.
**Branch:** `feature/texture-rhi-unification`, Draft PR #21.
**Files:** `AGENTS.md`, new
`skills/visuals/aiverse-texture-rhi/SKILL.md`.

**Agent-facing deliverable:** Register one concise reusable native
C++ texture recipe in AGENTS.md so future FPS, RTS and other projects
choose existing `gfx::Device` resources, `gfx::RenderTargetSet`,
`gfx::TextureStreamQueue`, `gfx::TextureMipResidency` and
`render::parseKtx2`/`parseDdsTexture`/`uploadContainerTexture`
instead of rewriting texture backends or per-genre systems.
The skill includes API mapping, a one-mip layered stream
example, old model sampler2D compatibility, BC5 normal-Z
reconstruction and GPU compressed-format gates, resource
ownership, mip-ready LOD clamps and exact CMake/CTest
verification commands. It states clearly that streaming is
render-thread deferred submissions, not GPU async readback,
and that this PR's platform GPU verification is still pending.

**Compatibility:** Documentation-only changes; no source C++
implementation, ECS/save/network data, assets or runtime
behavior changed. Agent skill is explicitly scoped to native
project consumers and warns against using GPU resources in
deterministic ECS simulation.

**Actual validation:** GitHub code/tree/commit/ref update succeeded.
No C++ compile, GPU validation or CTest executed.
GitHub Actions have not been confirmed green; do not imply
production-readiness from the skill examples.

**Next:** Run the full CPU texture test matrix and GPU
format/readback tests on native targets. Continue remaining
virtual-texture page cache/feedback, native async readback,
BasisLZ/UASTC/OpenEXR integration and editor resource
inspection as separate bounded stages with handoffs.

## Stage 5O — synthetic OpenEXR CPU regression fixture and registration (2026-11-18)

**Source commits:** `41e18941d46769b2b7c51a76284e3176a4871a37` (fixture plus CMake registration), `0aed9de646aecb0e627846031c6e99e518054869` (explicit offset-table fixture fix).
**Branch:** `feature/texture-rhi-unification`; PR #21 remains Draft.
**Files:** new `tests/render/exr_texture.cpp`; `CMakeLists.txt`.

**Code:** Registered a standalone CPU-only `aiverse_exr_texture_test` / CTest `exr_texture`, constructed from synthetic EXR version-2 uncompressed scanlines, channels in non-RGB file order and mixed HALF/FLOAT samples. Includes two scanlines stored in reverse physical order, negative dataWindow origin, positive HDR values, negative floating HDR, default and explicit alpha, bad magic/version, compressed EXR refusal, truncated scanline, duplicate offset/physical row rejection. The test explicitly retains its generated offset table position, so reverse-ordered chunks cannot confuse test setup.

**API/architecture:** Exercise existing `engine/render/ExrTexture.h::parseOpenExr`; no ECS/game state, rendering pipeline, format expansion or native project dependency changed. No GPU fixture in this stage. These are authored test *sources*; execution is separate.

**Actual verification:** Source and fix commits written to GitHub. **CMake NOT RUN; C++ compilation NOT RUN; CTest NOT RUN; Metal/GL/Vulkan NOT RUN.** Do not treat the presence of a registered test as a successful test run.

**Run later:**
```sh
cmake -S . -B build/texture-rhi -DAIVERSE_BUILD_TESTS=ON
cmake --build build/texture-rhi --target aiverse_exr_texture_test
ctest --test-dir build/texture-rhi -R '^exr_texture$' --output-on-failure
```

**Next stage:** Connect `TextureStreamQueue` and `TextureMipResidency` into a safe genre-agnostic native streaming facade; test callback mapping and stale-token rejection, then add backend readback and editor inspection. Continue append-only per-stage handoffs with exact commit IDs and accurate verification status.

## Stage 7F — native staging + mip-residency coordination (2026-11-18)

**Code commit:** `948df1c88ecad6aa5294173f21db32cd9824d963`.
**Working branch:** `feature/texture-rhi-unification`, Draft PR #21.
**File:** new `engine/gfx/TextureResidentStreamer.h`.

**Implementation:** Header-only genre-independent `gfx::TextureResidentStreamer`
facade coordinates the prior `TextureStreamQueue` and
`TextureMipResidency` without introducing a new backend or genre policy.
`create(TextureDesc)` allocates empty authored-mip sampled storage and
creates residency metadata; `stageNext(TextureStreamId, FillFn)` claims
the next coarse-to-fine mip/face/volume, asks the caller for complete
tightly packed CPU bytes, checks integer/pitch/frame budgets, and
deep-copies through the existing queue. Failed producers or staging
rejections immediately unclaim the mip so it can be retried.

`drain(CompletionFn)` applies GPU completion outcomes to the correct
ticket+resource+generation before notifying callers; on a failed update
the underlying queue quarantines the resource and cancels later
updates, while the coordinator invalidates remaining ticket mappings
and refuses sampling. `cancel` makes a staged mip retryable;
`destroy` cancels all pending jobs before the backend handle is
released, and never recycles its logical resource identity.
`sampledTexture` and `residentSampler` fail closed until *every*
face/layer in the coarsest mip is complete; `finestResidentMip`
and `fullyResident` expose progress without exposing undefined
GPU mip data. `TextureStreamStats` exposes aggregate memory and
upload budgets.

**Native API:** Include `engine/gfx/TextureResidentStreamer.h`,
construct with `gfx::Device&`, create with explicit `mipLevels`,
stage packed data for the chosen mip/layer with `FillFn`, call
`drain` on the graphics thread, then use `sampledTexture` and
the `SamplerDesc` from `residentSampler` with the RHI's independent
sampler binding interface. GPU uploads occur during `drain`; no
separate file I/O worker, sparse page allocator, virtual texture
feedback, or automatic GPU readback is claimed.

**Files changed:** only the new header. Prior streaming and residency
headers are reused unchanged to avoid colliding with another agent.

**Tests actually performed:** GitHub file creation completed.
**C++ compile NOT RUN, CTest NOT RUN, GPU backends NOT RUN.**
A deterministic mock-device coordinator CTest and cross-backend LOD
sampling are needed before considering this feature verified.

**Limitations:** Current queue preallocates the full mip chain, so
streaming defers upload bandwidth and residency, **not** physical
VRAM allocations for missing mips. Eviction destroys whole
resources. Production sparse/virtual texture physical-page
budgeting and worker-driven I/O remain separate work.

**Next step:** Add a CPU-only coordinator test for mixed 2D arrays,
coarse-to-fine sampler clamp, failed producer retry, cancellation,
stale resource destruction, GPU upload failure, and exact per-frame
byte limits. Register it in CMake and append a new handoff with
actual test status. Later integrate native texture inspection/editor.

## Stage 7G — virtual texture CPU page cache with LRU and parent fallback (2026-11-18)

**Code commit:** `2d570cc233578fef48a9a3391b83826a657ba717`.
**Branch:** `feature/texture-rhi-unification`, Draft PR #21.
**File:** new `engine/gfx/VirtualTexturePages.h`.

**Implementation:** Source-only, genre-neutral `gfx::VirtualTexturePageCache`
maintains a bounded number of physical page slots (for a caller-owned
`Texture2DArray` atlas), maps virtual `{mip,x,y,layer}` keys onto
generational physical slice assignments, prioritizes unused/free slots
and deterministically evicts least-recently-used **resident/unpinned**
pages. Pending uploads are never evicted until explicitly canceled by
the caller; stale GPU completions are rejected by global slot generation
tokens even after cache clear/reinit. Optional pinned fallback pages,
`touch`, `pin`, `evict`, `complete`, `abandon` and
`resolveWithCoarseFallback` allow the renderer to choose an available
parent page in the SAME virtual layer; absent parents return invalid
and require an explicit fallback material. `atlasDescriptor` creates
a `Texture2DArray` descriptor with one physical slice per page; actual
GPU allocation, upload, page-table shader and VT feedback are the
host project's responsibility.

**Known edge to fix immediately:** Calling `reserve` again for an
already-pending key currently reports `needsUpload=true`; the next
source stage must make it false for duplicate in-flight requests to
avoid redundant writes to the same physical slot.

**Limits:** CPU mapping/index only, no GPU virtual texture feedback,
no shader indirection/page-table upload and no demand paging from disk.
It is a reusable *foundation*, not complete sparse virtual texturing.
The caller must cancel a pending `TextureStreamQueue` ticket before
`abandon` allows reuse of its physical slice. Rendered page textures
and physical atlas storage remain non-authoritative presentation data.

**Verification:** GitHub source commit successful; C++ compiler,
CTest, real GL/Metal/Vulkan GPU validation **NOT RUN** (full repo
checkout unavailable in this session). No passing CI asserted.

**Next:** Fix duplicate pending `needsUpload` semantics, then add
CPU tests for eviction, pinned roots, generation rejection, pending
upload non-eviction, correct coarser same-layer lookup and page
allocation bounds; each stage requires its own code/handoff commits.

## Stage 7H — virtual-texture LRU and pending-write safety tests (2026-11-18)

**Code commit:** `2b0c207234e75b3213db26e7b1503aba96f1f596`.
**Branch:** `feature/texture-rhi-unification`, Draft PR #21.
**Files:** `engine/gfx/VirtualTexturePages.h`,
new `tests/render/virtual_texture_pages.cpp`, `CMakeLists.txt`.

**Fix:** Repeated `reserve` on an already-pending virtual page
now returns `needsUpload=false` (and not-ready), preventing the
caller from queuing a second GPU write to the same physical
array slice. New allocations still return `needsUpload=true`.
Resident lookups return `ready=true`.

**Test source:** Register GPU-independent `virtual_texture_pages`
CTest. Synthetic tests cover valid physical `Texture2DArray`
atlas descriptors, pinned coarse root residency, in-flight
non-eviction, no duplicate pending upload, root fallback only
within the same virtual array layer, LRU replacement of ready
unpinned detail pages, stale generation rejection after eviction/
abandon/reinit, failure releasing physical slots, touch ordering,
mip geometry bounds and explicit invalid requests.
No GPU or shader-based VT test exists at this stage.

**Compatibility:** Opt-in CPU-only virtual page cache; unchanged
`gfx::Device`, gameplay/ECS/save and existing rendering.
This is a physical slice management foundation, **NOT**
a full virtual texture shader page-table/feedback pipeline.

**Verification:** GitHub source commit/branch update succeeded.
C++ compiler and CTest `virtual_texture_pages`: **NOT RUN**;
real Metal/GL/Vulkan GPU validation: **NOT RUN**.
Latest CI status has not been confirmed passing.

**Next:** Run
`cmake --build build/texture-rhi --target aiverse_virtual_texture_pages_test`
and `ctest --test-dir build/texture-rhi --output-on-failure
-R '^virtual_texture_pages$'`.
Then integrate a texture array atlas + page-table lookup in a
native sample, stage uploads through TextureStreamQueue and
release a slot only after cancellation or GPU completion.
Continue separate code and handoff commits per stage.

## Stage 7G — mip residency streaming coordinator mock tests (2026-11-18)

**Code commit:** `cc5ff6852ae7c72a7df8b9752be95df359d5cc08`.
**Branch:** `feature/texture-rhi-unification`, PR #21 still Draft.
**Files:** `tests/render/texture_resident_streamer.cpp`, `CMakeLists.txt`.

**Test source and API coverage:** New GPU-independent CTest target
`aiverse_texture_resident_streamer_test` / `texture_resident_streamer`
exercises the Stage 7F facade against a strict mock
`gfx::Device`. It covers 2D array coarse-to-fine mip/face
sequencing, unavailable sampling while a coarse face is
missing, callback delivery after GPU completion, per-frame
byte/upload limits, LOD sampler clamps on complete mip
ranges, entire 3D volume mip uploads, packed source ownership
with no source-pointer lifetime dependency, invalid producer
bytes, rejected/throw-free producer retry, ticket cancellation
and replay, destroy/recreate with non-recycled logical
resources, full GPU shutdown, and quarantine after failed
GPU updates. Assertions inspect the mock's submitted
mip/layer coordinates and pixel values.

**Implementation:** Test code only; no changes to existing render
paths or ECS/network state. The production facade remains
`engine/gfx/TextureResidentStreamer.h`.

**Tests actually performed:** GitHub test and CMake commit completed.
**CMake configure NOT RUN, C++ compile NOT RUN, CTest NOT RUN,
Metal/GL/Vulkan GPU sampling NOT RUN.** The test was prepared
for a later native checkout as requested, not reported passing.

**Native verification commands:**
```sh
cmake -S . -B build/texture-rhi -DAIVERSE_BUILD_TESTS=ON
cmake --build build/texture-rhi --target aiverse_texture_resident_streamer_test
ctest --test-dir build/texture-rhi --output-on-failure -R '^texture_resident_streamer$'
```

**Next:** Document new native API and adapt AI skill entry points,
then implement bounded image readback interfaces/backends and
editor diagnostics; realistic sparse/virtual physical-page
streaming is still a separate unimplemented subsystem.
Keep source/handoff commits separate and update tests status.

## Stage 8A — native texture API documentation and AI skill routing (2026-11-18)

**Code/docs commit:** `3627d46f70694e26ec43ad9437bd13c76f09ac3c`.
**Branch/PR:** `feature/texture-rhi-unification`, Draft PR #21.
**Paths:** `skills/visuals/aiverse-texture-rhi/SKILL.md`,
`docs/texture-rhi.md`, `AGENTS.md`.

**What changed:** The existing texture RHI agent skill now covers the
new `gfx::TextureResidentStreamer` facade as the preferred
native-project entry point for staged mip uploads, rather than asking
agents to reinvent manual ticket→mip completion tracking.
A compact standalone native C++ example demonstrates
`create` → `stageNext` → `drain` → checked
`sampledTexture`/LOD-clamped `residentSampler` → `destroy`.
The skill and `AGENTS.md` explicitly point native developers to
the Radiance RGBE and restricted uncompressed OpenEXR importers,
and warn against treating the GPU cache as ECS-authoritative.
`docs/texture-rhi.md` now distinguishes source-level mixed-format
MRT/MSAA from GPU certification, documents streaming ownership,
CPU GPU-byte budgets versus actual virtual texturing, BC6H mappings,
HDR/EXR limitations, and updates the grouped texture CTest commands.

**What did not change:** No engine ABI, actual sampler/renderer
bindings, asset format, save game, ECS or backend driver code was
changed by this stage. Native game integration is an API recipe,
not evidence of running a game or verifying an importer.

**Verification:** GitHub docs/tree commit succeeded. **CMake,
C++ build, CTest, GPU, README translation check and skill
catalog check NOT RUN**. No passing CI status asserted.

**Next exact step:** Implement readback contracts and native
backend extensions (with CPU+GPU fixtures), or integrate
resident streaming/inspection with the editor. After the
code commit, append a separate handoff with exact executed
verification status. Do not merge PR #21 while native build
and platform GPU tests are unverified.

## Stage 7H — explicit async texture readback RHI contract (2026-11-18)

**Code commit:** `5dc72d0b4288e0081454b3ff87543b2335115f6e`.
**Branch/PR:** `feature/texture-rhi-unification`, PR #21 Draft.
**File:** `engine/gfx/Gfx.h`.

**API:** New `TextureReadbackDesc { mipLevel, arrayLayer }`,
`TextureReadbackResult { width, height, PixelFormat format,
std::vector<uint8_t> pixels, success() }`, and optional
`Device::readTextureAsync(TextureHandle, TextureReadbackDesc,
TextureReadFn)`. The base implementation refuses unsupported
operation (`false`), rather than silently taking a screenshot or
modifying the request. Accepted requests must deliver tightly
packed top-left rows of the *actual* texture format at a later
`beginFrame` on the owner thread; GPU driver/completion threads
must never invoke user callbacks. A failed queued operation
returns an empty failure result. Staging lifetime is independent
from subsequent destruction of the parent texture.

**Compatibility:** Existing RHI implementers keep their old
behavior through the new default virtual method; no
existing texture upload/scene/ECS behavior is modified. This stage
only adds the public contract; it does **not** implement readback
on Metal, Vulkan, GL33 or GLES3.

**Actual verification:** GitHub commit completed. C++ compilation
**NOT RUN**, CTest **NOT RUN**, native GPU **NOT RUN**.
No green CI status claimed.

**Next step:** Implement Metal blit-to-shared-buffer readback for
RGBA8/RGBA16F (including BGRA render-target swizzle), with
callback dispatch at later `beginFrame`; then implement a
GL/GLES3 and Vulkan backend where portable. Add lifecycle
and invalid-subresource tests in distinct code/handoff stages.

## Stage 7I — virtual texture page-table CPU encoding and bounded GPU upload (2026-11-18)

**Code commit:** `750313df432b011d4df9dc375b8d1e6bc78839f6`.
**Branch:** `feature/texture-rhi-unification` (Draft PR #21).
**Files:** new `engine/gfx/VirtualTexturePageTable.h`,
`engine/gfx/VirtualTexturePages.h` (metadata getters).

**Implementation:** `VirtualTexturePageTable` converts the CPU page
cache into an RGBA8 mip-zero page-table texture, one 2DArray layer per
logical virtual texture layer. RGBA8 entry encodes physical atlas
slice (16-bit split into R/G), resident fallback mip (B), and valid
mask (A=255). For a missing detail tile, `rebuild` searches
resident parents only within the same virtual layer; no valid
parent emits an invalid table entry. Changes mark only affected
CPU rows dirty. `textureDescriptor` and `createGpuTexture`
allocate a suitable nearest-filtered Texture2DArray with all table
bytes. `flush(Device&,TextureHandle,maxBytes)` submits contiguous
dirty row groups under the requested byte budget via
`updateTextureRegion`, preserves rows on GPU failure for retry
and exposes read-only `entry`/dirty stats for editor diagnostics.
No GPU table is owned by this helper; caller destroys its handle.

**Scope:** CPU page table and render-thread uploads only.
There is not yet a GLSL/MSL virtual page-table sampler,
GPU feedback/readback, sparse hardware texture binding or
disk worker. VirtualPageCache physical atlas must be created
and uploaded independently (e.g. TextureStreamQueue). This
stage does not modify deterministic ECS, saves or netcode.

**Static review follow-up:** `flush` currently limits chunks
by byte budget but must also cap chunk size to
`INT_MAX / (width * 4)` because `TextureUploadDesc.slicePitchBytes`
is an `int`. Add this before native compilation and test it.
The platform runtime must ensure no sampling of page-table
rows until their GPU uploads finish.

**Verification:** GitHub source/tree/branch updates succeeded.
C++ build, CTest and Metal/GL/Vulkan page sampling:
**NOT RUN** (container lacks GitHub repo checkout and network clone).
No passing CI assertion.

**Next:** Fix int slicePitch limit, add GPU-independent
`virtual_texture_page_table` tests using a mock `gfx::Device`,
exercise mapped RGBA bytes, unchanged dirty rows, bounded
successive uploads, GPU failure retry and wrong-layer fallback.
Then create GLSL/MSL sampling helper and validate actual GPU
2DArray texture lookup.

## Stage 7I — Metal asynchronous texture mip/layer GPU readback (2026-11-18)

**Code commit:** `128ecb508a5f06240f6c1b59dfc140035af15a8c`.
**Branch:** `feature/texture-rhi-unification` (Draft PR #21).
**File:** `engine/gfx/metal/GfxMetal.mm`.

**Implementation/API:** Implements the previously optional
`Device::readTextureAsync(TextureHandle,TextureReadbackDesc,TextureReadFn)`
on Metal for sampled/rendered, single-sample 2D, 2D-array and cube
(subresource mip/face selected). Supports source RGBA8, sRGB RGBA8
and RGBA16F. Refuses depth, MSAA, 1D/3D and unsupported formats.
A dedicated `MTLBlitCommandEncoder` records a nonblocking
texture-to-shared-`MTLBuffer` transfer with 256-byte-aligned GPU
row pitch. A retained completion record copies rows to packed
CPU bytes after GPU completion and swizzles Metal BGRA render-target
storage to the portable RGBA result. The MTL completion handler
signals `atomic ready` but does not call user code. The next
`beginFrame` on the device owner thread invokes ready callbacks;
failure produces an empty `TextureReadbackResult`.
Returned source data outlives the parent texture handle because
the command buffer captures its staging target. Requests are
bounded to at most 32 in flight and 256 MiB staging each.
Other backend implementations remain fail-closed.

**Compatibility:** No scene rendering, capture, texture allocation,
ECS, network/save or material-layout changes; app code must check
the boolean return and `TextureReadbackResult::success()`.
Device must outlive callbacks. The API does not promise
automatic CPU orientation correction for custom shader UV
conventions, only packed top-left texture rows.

**Actual verification:** Connector committed source successfully.
**macOS/iOS compiler NOT RUN; Metal GPU capture NOT RUN;
CTest NOT RUN; UI integration NOT RUN.**
No passing status asserted. Real Metal validation is required
for aligned row copies, cubemap face indexing, BGRA swizzle,
HDR byte count, after-texture-destroy lifetime and
callback delivery order.

**Next:** Implement portable GL/GLES3 readback (with clear
sync/deferred semantics) and Vulkan staging/fence readback,
then add runtime GPU tests and editor inspection hooks.
Maintain separate code + handoff commits per stage.

## Stage 7J — virtual page-table RHI dirty-row tests and integer pitch bound (2026-11-18)

**Code commit:** `b444c4955b8accf789dcb9be573fdc5a23abda0e`.
**Branch:** `feature/texture-rhi-unification`, Draft PR #21.
**Files:** `engine/gfx/VirtualTexturePageTable.h`,
new `tests/render/virtual_texture_page_table.cpp`, `CMakeLists.txt`.

**Fix:** `VirtualTexturePageTable::flush` now clamps the upload
batch's row count to `INT_MAX / rowPitchBytes` in addition to
the caller's byte budget. This prevents signed overflow of
`TextureUploadDesc.slicePitchBytes` for large page-table uploads
before calling the GPU backend. Rows remain dirty on a failed
GPU subresource update for a later retry.

**Test source:** New `virtual_texture_page_table` CTest uses a mock
gfx::Device and physical page cache. Validates nearest-filtered
RGBA8 Texture2DArray creation with initially invalid entries,
parent-mip physical page lookup encoding, separate virtual layers,
full dirty rows only after cache changes, bounded per-frame row
uploads, rejection of budgets smaller than one row, preservation of
dirty rows on GPU failure and retry, single-row dirtiness after
one detail tile is resident, fallback invalidation after eviction,
and mismatched virtual dimensions/invalid inspection. No actual
GPU shader lookup or feedback test exists yet.

**Compatibility:** Only new opt-in CPU helpers/tests; no changes
to existing render passes, ECS, saves, networking or asset files.
The GPU page table is owned by its caller and must be destroyed
when the hosting renderer shuts down.

**Validation:** GitHub code commit/update succeeded. C++ compile,
CTest, Metal/GL/Vulkan GPU validation **NOT RUN** (no local checkout).
Do not claim page-table GPU fidelity without a native shader fixture.

**Next:** Provide portable GLSL/MSL virtual texture sampling helpers,
including physical atlas layer/mip decode and tile border addressing.
Then run `aiverse_virtual_texture_page_table_test`, add a real
shader capture that looks up page table rows, and complete
native readback/feedback when supported. Continue separate
code/handoff commits.

## Stage 7J — GL33/GLES3 nonblocking texture readback via PBO+fence (2026-11-18)

**Code commit:** `0b975df615d7de9042d7e3bb3d5e8ff98caaf16c`.
**Branch/PR:** `feature/texture-rhi-unification`, Draft PR #21.
**Files:** `engine/gfx/gl/GLApi.h`, `engine/gfx/gl/GfxGL.cpp`.

**Implementation:** Added a GL33/GLES3 override for
`readTextureAsync` supporting single-sample RGBA8/sRGB_RGBA8
2D, 2D-array and cubemap faces at a selected mip. It builds a
temporary framebuffer, attaches the exact selected mip/array
layer/cube face, queues nonblocking `glReadPixels` into a
`GL_PIXEL_PACK_BUFFER`, then inserts a GPU `GLsync` fence.
`beginFrame` polls `glClientWaitSync(timeout=0)`, maps only
signaled PBOs, copies/flips bottom-up framebuffer data into packed
top-left RGBA8 rows, deletes completed fences/PBOs, and invokes
app callbacks afterward on the GL context owner thread.
Device destructor releases in-flight resources. Up to 32
readbacks, 256 MiB each, are allowed. Unsupported formats,
MSAA, depth, nested passes and invalid subresources return
false, without invoking callbacks. The WebGL2 build returns
false explicitly because portable native glMapBufferRange
readback cannot be promised there. GL loader adds the needed
PBO mapping/fence functions and Windows GL33 constants.

**Compatibility:** Existing `captureNextFrame` path is unaffected;
no ECS/simulation/serialization changes. Asynchronous completion
is not a blocking `glFinish`; callbacks need future frames
and an active context. On an accepted GPU failure the callback
receives an empty `TextureReadbackResult`.

**Actual verification:** Source committed to GitHub.
**C++ compilation NOT RUN, CTest NOT RUN, GL33/GLES3/Android GPU
test NOT RUN, WebGL2 build NOT RUN**. The pending tests must
check GL entrypoint availability, framebuffer completeness,
row orientation, destroyed source lifetime, poll behavior,
signal/failure cleanup and no callback on rejected input.

**Next:** Implement a Vulkan image-to-host-staging readback with
frame-fence ownership, then add portable GPU fixtures and editor
image preview/readback plumbing. Continue source commit +
append-only handoff on every bounded stage.

## Stage 7K — reusable GLSL and Metal virtual texture lookup shader helpers (2026-11-18)

**Code commit:** `1f65dee131573feebdd4f6569676a9ce47f06510`.
**Branch:** `feature/texture-rhi-unification` (Draft PR #21).
**New file:** `engine/gfx/VirtualTextureSampling.h`.

**Implementation:** Native C++ header provides GLSL ES3/GL330 and
Metal Shading Language source snippets via `kVirtualTextureGLSL`
and `kVirtualTextureMSL`. Both decode the same RGBA8 page-table
ABI produced by Stage 7I: R/G little-endian 16-bit physical atlas
slice, B coarse fallback mip and A valid flag. Functions look up
the nearest sampled virtual mip-zero tile in a Texture2DArray
page table, resolve the physical Texture2DArray slice, compute
the local UV within an ancestor page and clamp to texel centers
with caller-supplied gutter/border pixels. Missing or out-of-range
entries return a caller-controlled fallback color rather than
sampling an undefined GPU slice. The snippets contain no shader
version directive so the engine backend can prepend its own.

**Capabilities/limits:** These are **shader source snippets only**,
not new graphics pipeline builders or validated shaders.
The game/material compiler must concatenate/bind the snippets
and bind an RGBA8 nearest page table plus physical array atlas,
each on suitable texture slots. The physical atlas requires
appropriately padded gutters to prevent edge seams. CPU
`VirtualTexturePageTable::flush` must complete before sampling
updated table rows. GPU feedback, readback, actual page fault I/O,
hardware sparse residency and VT mip-tail handling are still
outstanding.

**Compatibility:** New optional source header; no existing shader
materials or render passes changed. Does not affect native ECS,
saves/network or project asset schemas.

**Verification:** A first GitHub multi-call tree/commit attempt failed
with a transient GraphQL API error and left no verified source file.
Recovered by checking the branch and successfully creating the new
file with GitHub's single-file contents API. Final code commit above
is verified. GLSL/MSL compilation and real GPU capture: **NOT RUN**.
No passing CI result claimed.

**Next:** Build a native Vulkan/GL and Metal sampling test that
constructs a physical 2DArray atlas, uploads a CPU page-table
entry, runs the shader helper and checks fallback/mapped pixel
colors. Compile C++ CPU tests and record actual outcomes.
Continue stages for GPU feedback/readback and budgeted page uploads.

## Stage 7K — Vulkan async texture readback with frame-fence staging (2026-11-18)

**Code commit:** `5d8d6da68932f8d1f22e59c0b1c1f5f43ef7d353`.
**Branch:** `feature/texture-rhi-unification` (Draft PR #21).
**Files:** `engine/gfx/vulkan/VkDevice.h`,
`engine/gfx/vulkan/VkResources.cpp`,
`engine/gfx/vulkan/GfxVulkan.cpp`.

**Implementation/API:** Implements `Device::readTextureAsync` for
single-sample, color RGBA8, RGBA8_sRGB and RGBA16F 2D/2DArray/Cube/
CubeArray mip/face subresources. An accepted request allocates a
host-visible+coherent `VK_BUFFER_USAGE_TRANSFER_DST_BIT` staging
buffer, transitions the selected mip's array-layer range to
`TRANSFER_SRC_OPTIMAL`, issues `vkCmdCopyImageToBuffer`
with one requested layer and a tight row pitch, then restores
the image to `SHADER_READ_ONLY_OPTIMAL`. The staging buffer
and `TextureReadFn` remain owned by a pending request
associated with the current `serial_`.

`prepareSlot` already waits a Vulkan fence and updates
`completed_`. At a later `beginFrame`, `deliverTextureReads`
copies only fence-completed buffers to packed CPU result vectors,
releases staging buffers/allocations, and invokes user callbacks
on the owner thread **after** collection. GPU device shutdown
frees all remaining staging buffers after `vkDeviceWaitIdle`.
A destroyed source texture remains alive until the frame serial
completes via the existing deferred texture destruction policy.
Unsupported depth/MSAA/3D/compressed textures and invalid mip/layer
requests return false without callbacks.

**Compatibility:** Existing screenshot/backbuffer capture path
stays unchanged. This source stage does not add sparse/virtual page
allocation or GPU texture query UI. Vulkan packed row orientation
follows the image subresource texel order; no display-specific
Y-flip or tone mapping is applied to raw texture data.

**Actual verification:** Source commit to GitHub succeeded.
**Vulkan C++ build NOT RUN, CTest NOT RUN, validation layers and
GPU readback NOT RUN**; no driver correctness claims.
Check source-texture destruction, frame-slot recycling, sRGB/HDR
byte sizes, depth rejection, failed staging allocation, and
callback delivery on the render owner thread under a real Vulkan
device or Lavapipe.

**Next:** Add tests using Metal, GL33, GLES3 and Vulkan devices
for the new RHI readback contract. Integrate editor texture
preview/diagnostics and optional virtual texturing page cache
only after native compile and format validation.

## Stage 8A — GPU texture inspection contract and logical footprint estimates (2026-11-18)

**Code commit:** `065678454bd968c7e8a54043122bc36bc0f5c39c`.
**Branch:** `feature/texture-rhi-unification`, Draft PR #21.
**Paths:** `engine/gfx/Gfx.h`, `tests/render/texture_contract.cpp`.

**API/source implementation:** New `TextureResourceInfo` describes a live
user-created texture handle, dimension, pixel format, extent, array layers,
mip levels, sample count, render-target/storage usage, and approximate size
in bytes. `estimateTextureFootprint(TextureDesc)` estimates GPU resource
footprint from logical block sizes, mip extents, layers/volume slices and
samples, with arithmetic overflow checks; it is NOT a driver memory query.
The optional `Device::textureResources() const` API safely returns an
empty list until a backend overrides it. A CPU-only `texture_contract`
fixture checks RGBA8 arrays with mips, compressed BC7 cube, multisample
depth, typed half-float and a huge overflow request.

**Compatibility:** All existing Device implementations compile in principle
because inspection is optional with a default virtual implementation; no
change to existing shader, frame, ECS, save/network or on-disk data. Backend
enumeration and the actual editor widget are NOT implemented in this stage.

**Actual verification:** GitHub source/tree/commit/ref update succeeded.
C++ compilation, CTest, editor and GPU execution: **NOT RUN** (this
execution environment has no native checked-out repository).
GitHub Actions completion not confirmed. The size figures are estimates,
not measured allocations or driver-reported memory.

**Next:** Implement actual Metal/GL/Vulkan `textureResources()`
over their live texture pools without exposing swapchain/dummy internal
images. Include correctly scaled 3D mip depth, cube-array faces and MSAA
samples, and record the code commit followed by a separate handoff. Then
add an ImGui texture-resource view in the editor and native regression tests.

## Stage 8B — backend live GPU texture enumeration (2026-11-18)

**Code commit:** `be7f180d627655ac3d30059ff03ac619e55bb5a8`.
**Branch:** `feature/texture-rhi-unification`, Draft PR #21.
**Paths:** `engine/gfx/Gfx.h`, `engine/gfx/gl/GfxGL.cpp`,
`engine/gfx/metal/GfxMetal.mm`, `engine/gfx/vulkan/VkDevice.h`,
`engine/gfx/vulkan/VkResources.cpp`.

**Source implementation:** Metal/GL/Vulkan `Device::textureResources()`
overrides now enumerate only user-created, currently alive resources
from the corresponding backend texture pool (excluding swapchain,
internal dummy images, samplers/views). The returned typed entries
cover handle, texture dimension, pixel format, width/height/depth,
array and cube face counts, mip levels, sample count, attachment/storage
usage and a conservative estimated byte footprint via Stage 8A.
Pool gains a read-only `each`/`eachWithId` overload; existing mutable
iteration behavior is unchanged. `Gfx.h` now explicitly includes
`<algorithm>` for its new `std::max` footprint calculation.
No GPU synchronous readback or driver allocation query is used.

**Compatibility:** Inspection is opt-in, read-only and requires the
graphics owner thread. No existing rendering behavior changes,
save/network/ECS changes or new external dependencies.

**Limits:** Estimated bytes are *not* physical VRAM residency,
texture memory allocator footprint or shared texture accounting.
Editor GUI is not wired yet. As with existing pool-based handles,
only query and act on returned entries on the device owner thread;
do not treat a stale handle ID as permanent identity after deletion.

**Actual verification:** GitHub code/tree/commit/ref update succeeded.
C++/Objective-C++ compile, CTest, editor UI and live GPU resource
inspection: **NOT RUN**; CI passing result not confirmed.
Potential Metal SDK type availability and Vulkan linkage must be
checked by building native targets.

**Next:** Integrate a read-only ImGui editor texture inspector
with optional request to `gfx::Device::textureResources()`,
sorting/filtering by dimension/format and estimated bytes. Build
and run the editor as well as `texture_contract` CTest and
cross-backend resource allocation regression tests. Commit
implementation and handoff separately.

## Stage 7L — virtual texture physical page upload/publish coordinator (2026-11-18)

**Code commit:** `4064825167f76c9d8225c5da2b207c8671e5e083`.
**Branch/PR:** `feature/texture-rhi-unification`, Draft PR #21.
**File:** new `engine/gfx/VirtualTextureStreamer.h`.

**Implementation/API:** New native, genre-independent
`gfx::VirtualTextureStreamer` composes the existing
`VirtualTexturePageCache`, `VirtualTexturePageTable` and
`TextureStreamQueue`. `initialize` allocates a fixed-budget
Texture2DArray physical atlas and RGBA8 Texture2DArray page table.
`stage(VirtualPageKey,packedBytes,pin)` reserves a page or reuses
the deterministic LRU physical slot, **invalidates stale table
entries on the GPU before overwriting evicted physical slices**,
and deep-copies a complete physical page through the staging
queue. `drain` publishes a virtual page in the GPU table only
after successful GPU upload completion, rebuilds nearest parent-mip
fallback mappings, then dispatches app callbacks after synchronizing
the table. `cancel`, `evict`, `pin`, `touch`,
`physicalAtlas`, `pageTableTexture`, `resolve`,
`encodedPageTable`, `pageStats` and `streamStats` expose
lifetime, diagnostics and resource access. If an invalidating
page-table upload fails, the entire facade becomes non-sampleable
rather than risking an old virtual coordinate reading overwritten
physical texels. Source pixels must include the intended gutter.

**Architecture:** Fixed-count 2DArray atlas bounds physical GPU
memory while the logical virtual texture may contain many pages.
The 16-bit physical slot encoding and mip-fallback page-table
layout are from prior Stage 7H–7K virtual texture work and the
existing GLSL/MSL shader helpers. Graphics device owns backend
work; this facade owns its physical texture handles and must be
destroyed before `gfx::Device`. No genre-specific ECS, save
format or network changes. No runtime disk worker, automatic
feedback pass, disk paging, BC page transcoding or sparse API
allocation yet.

**Tests actually performed:** New header committed to GitHub.
**C++ compile NOT RUN; CTest NOT RUN; native GPU shader/render
NOT RUN.** This source-only stage is not production certified.

**Next:** Add a mock-device fixture verifying budgeted page
publishing, LRU+pinning, parent fallbacks, cancellation, staged
source ownership and upload failure quarantine. Register it in
CTest, then integrate virtual texture preview/telemetry into
editor and real GPU scenes. Continue separate code/handoff commits.

## Stage 8C — dockable GPU texture-resource inspector in editor (2026-11-18)

**Implementation commit:** `2408ed1accde2363b69141198531c9a3cc8f585b`.
**Branch:** `feature/texture-rhi-unification`, Draft PR #21.
**New file:** `apps/editor/windows/TextureResourcesWindow.cpp`.

**Source implementation:** Register `TextureResourcesWindow` as a normal
dockable AIVERSE editor window under menu group `look` (Графика),
closed by default. It accesses the editor's existing `device()`
and calls the new Metal/GL/Vulkan `textureResources()` snapshot to
display live user-created texture handles, dimensionality, pixel
format, texture extent/depth/layer count, mip levels, sample count,
render-target/MSAA counts and total estimated GPU resource bytes.
Includes ID/format/dimension text filter, render-target-only filter,
and sorted-by-estimated-bytes or stable-handle ordering. Uses the
existing editor WindowRegistry macro; the CMake editor target
automatically includes `apps/editor/windows/*.cpp`, so no direct
CMake edit is necessary. It does not read back image pixels or
change texture resource state.

**Correctness/compatibility:** The inspector is read-only, optional
and does not alter FPS/RTS components or ECS authority, network
or save data. Estimates exclude driver allocation overhead,
swapchain and internal textures; a row's handle is ephemeral.
No cached persistent GPU state is kept by the inspector.

**Verification:** GitHub source commit confirmed. macOS/Windows editor
build, interactive ImGui window, GL/Vulkan/Metal live texture enumeration,
CTest: **NOT RUN** (no local native checkout). GitHub Actions completion
not confirmed. The window must be tested under the editor's actual
ImGui version before describing it as functional.

**Next:** Build `aiverse_editor` for macOS/Windows and inspect
GPU texture allocations while loading multiple 2D/array resources.
Check new window menu registration, memory estimates, filter and
lifecycle, then run the texture-focused CTests. If the build is clean,
continue native async readback/GPU validation and production importer
integrations. Record exact results in a new handoff.

## Stage 7M — virtual texture streaming coordinator mock tests (2026-11-18)

**Code commit:** `88ba89f5dc89369688ef7ed170e529894f26f97a`.
**Branch:** `feature/texture-rhi-unification` (Draft PR #21).
**Files:** `tests/render/virtual_texture_streamer.cpp` and
`CMakeLists.txt`.

**Test source:** Registered `aiverse_virtual_texture_streamer_test` /
CTest `virtual_texture_streamer` under the existing
`AIVERSE_BUILD_TESTS AND NOT EMSCRIPTEN` gate. Synthetic mock GPU
checks physical atlas + table allocation, coarsest parent fallback
publication only after GPU upload completion, sample invalidation
before an evicted physical slot can be overwritten, deterministic
LRU eviction with a pinned intermediate page, fine-over-coarse
page-table replacement, explicit eviction and safety of pinned
pages, cancellation and re-staging, refusal of truncated physical
page bytes, GPU upload failure quarantine, callbacks and complete
ownership cleanup. The fixture uses no external image, GPU or
virtual texture asset.

**Implementation:** Test-only source and CMake registration.
Production runtime code remains the Stage 7L
`engine/gfx/VirtualTextureStreamer.h`, and the prior page
cache/table/GLSL/MSL helpers are reused unchanged.

**Actual verification:** GitHub test commit succeeded.
**C++ compilation NOT RUN, CTest NOT RUN, GPU test NOT RUN.**
The fixture's own correctness is still subject to compilation
and execution; no fabricated PASS result.

**Verification later:**
```sh
cmake -S . -B build/texture-rhi -DAIVERSE_BUILD_TESTS=ON
cmake --build build/texture-rhi --target aiverse_virtual_texture_streamer_test
ctest --test-dir build/texture-rhi --output-on-failure -R '^virtual_texture_streamer$'
```

**Next:** Document the unified virtual texture physical atlas
API in the native AI skill. Add editor resource telemetry showing
the public page cache stats and page-table entries. GPU texture
readback tests, complete OpenEXR/Basis transcoding, rendering
integration and platform builds are still outstanding.

## Stage 8D — read-only selection and basic 2D GPU thumbnail preview (2026-11-18)

**Code commit:** `b4af34c7f4d7c437d036c9b4ffd4202df7b7353e`.
**Branch:** `feature/texture-rhi-unification`, Draft PR #21.
**Path:** `apps/editor/windows/TextureResourcesWindow.cpp`.

**Implementation:** Editor GPU texture resource window now supports
click-to-select a live resource handle and a read-only ImGui preview
for ordinary single-sampled, uncompressed, non-depth Texture2D
resources. It reuses the same ImGui texture-handle binding and
render-target vertical orientation convention as the existing
`Viewport::image`, without allocating/destroying GPU textures
or modifying their contents. Selection is invalidated when its
resource is no longer present in the live inventory. Array,
cubemap, multisample and depth resources show an explicit
"readback required" message instead of incorrectly binding
them as sampler2D.

**Compatibility:** No changes to `gfx::Device`, ECS/gameplay,
saves/network or material shaders. The optional GPU editor
inspector remains closed by default.

**Limitations:** The preview is a native ImGui texture binding,
not a GPU readback or per-mip/layer color inspection. Filtered
float/typed 2D formats can still differ by sampler support;
actual GPU validation and backend-specific ImGui bindings
remain outstanding.

**Verification:** GitHub commit confirmed. Native C++/ImGui
compilation, editor launch and per-backend rendering:
**NOT RUN**. Latest CI not confirmed green.

**Next:** Build the editor, validate the texture window under
GL33, Metal and Vulkan. Add readback-based slice/face preview
for arrays and depth only if the backend supports the required
TextureReadbackDesc, and never block the render thread.
Continue code and handoff commits separately.

## Stage 8B — virtual pages/readback agent recipes and correct CTest command (2026-11-18)

**Documentation/skill commits:** `4eec698d1dfb1c26132714f0fa4a6143d7a41919`
(AI texture skill, including a corrected CMake multi-target command),
`8e24b098b8da377b722f83efed58c05f5efe0eb6` (canonical
`docs/texture-rhi.md` source-status and CTest matrix), and
`21ec39c179e468c5e9726655310cc5fc1586df87`
(`AGENTS.md` routing to `gfx::VirtualTextureStreamer` and
`Device::readTextureAsync`).
**Branch:** `feature/texture-rhi-unification` (PR #21 Draft).
**Files:** `skills/visuals/aiverse-texture-rhi/SKILL.md`,
`docs/texture-rhi.md`, `AGENTS.md`.

**Changes:** Native games and other agents are directed to reuse
the fixed-capacity physical virtual-texture atlas, page-table and
LRU coordinator, and the separate GPU readback contract rather
than generating per-genre texture APIs. Documents explain
driver/backend support, failed-resource quarantine, page table
publication ordering, color formats and limitations (no
hardware sparse textures, GPU feedback, full EXR/Basis transcoding,
or threaded disk workers). Added `virtual_texture_streamer`,
`virtual_texture_pages`, `virtual_texture_page_table`
and the previously added EXR/HDR/streaming fixture targets to
the CMake/CTest instructions. Fixed a broken line continuation
in the prior texture skill test command.

**Compatibility:** Documentation only; no production C++
source, ABI, ECS, saves or network code changed in this
documentation stage. C++ source stages 7H–7M remain unverified.

**Verification:** Documentation files updated in GitHub.
**CMake build NOT RUN, CTest NOT RUN, GPU tests NOT RUN,
skill-catalog audit NOT RUN, full run-tests.sh NOT RUN.**
Do not claim any passing checks or merge PR #21.

**Next:** Finish the editor texture resource inspector presentation,
OpenEXR/BasisU production transcoding policy, shader storage
image support and mobile/web compatibility. After the user-requested
implementation pass, run `./run-tests.sh --quick`, then full
`./run-tests.sh` and real Metal/GL/Vulkan GPU captures; fix failures
and record exact tested HEAD and outcomes.

## Stage 8E — refresh architecture docs and repair duplicated test commands (2026-11-18)

**Documentation commit:** `1d1f790a7e7a85f0db607fcaaf3da816ab8cc35b`.
**Branch:** `feature/texture-rhi-unification`, Draft PR #21.
**Path:** `docs/texture-rhi.md`.

**Documentation work:** Replace stale statements suggesting virtual
page tables/physical atlas streaming are wholly absent: the CPU-backed
`VirtualTextureStreamer`, fixed 2DArray physical atlas, GPU-uploadable
page table, LRU and GLSL/MSL snippets now exist in source. Clarify that
true hardware sparse textures, GPU-feedback-driven requests, disk paging
and native shader validation are NOT implemented or verified. Document
the new `Device::textureResources()` read-only snapshot and optional
editor **Графика → GPU Текстуры** table/2D thumbnail with byte estimates
distinct from driver-reported VRAM. Fix an earlier duplicated/broken
Markdown fenced validation block by replacing it with a single coherent
CMake/CTest command including the texture and virtual texture CPU tests
and Vulkan native GPU validation instructions.

**Compatibility:** Documentation-only stage, no C++ runtime changes.
No FPS/RTS ECS, networking, saves or existing renderer API modifications.

**Actual validation:** GitHub document update committed.
Native C++ build, CTest and live GPU/editor validation: **NOT RUN**.
No passing CI claimed.

**Next:** Perform native builds and texture/VT CTests; correct actual
failures. Add real hardware shader capture for page-table/physical
atlas sampling and backend readback. Ensure CI completes before merging.

## Stage 8F — Vulkan live resource enumeration regression fixture (2026-11-18)

**Code commit:** `36667df7470167ab07285e2e6b207e79f4754419`.
**Branch:** `feature/texture-rhi-unification`, Draft PR #21.
**Path:** `tests/render/vulkan_tests.cpp`.

**Source:** In the existing `vulkan_backend` integration fixture,
create an 8×8 RGBA8 `Texture2DArray` with two physical layers and
four explicitly allocated mips. Assert that `Device::textureResources()`
reports the live user texture with the correct type/format/extent/
layers/mips/sample count and an estimated footprint of 680 bytes;
check renderTarget/storage flags remain false. Destroy that handle
and assert the resource no longer appears in the returned snapshot.
The fixture is conditional on successful native texture allocation
and records CHECK failure rather than silently claiming success.
This validates inventory lifecycle, not actual GPU memory telemetry.

**Compatibility:** Test-only; no new runtime/editor/backend API
or save/network/ECS source changes.

**Validation:** GitHub source commit confirmed.
Vulkan SDK compile, CTest and real GPU/lavapipe execution:
**NOT RUN** in this environment. The full feature branch
still needs native compiler validation and cannot be considered
production ready.

**Next:** Run `aiverse_vulkan_tests`/`vulkan_backend` on a
working Vulkan device, and test analogous Metal/GL texture
enumeration and editor GUI. Check actual format/memory numbers,
including compressed textures, cube arrays, 3D and MSAA. Continue
separate source/handoff commits.

## Stage 8F — editor readback preview for selected mip/array/cubemap face (2026-11-18)

**Code commit:** `09c2ef46a680bb5a3d31e8ac6fcc783f4745c7b4`.
**Branch:** `feature/texture-rhi-unification` (Draft PR #21).
**Changed file:** `apps/editor/windows/TextureResourcesWindow.cpp`.

**Implementation/API:** The opt-in, read-only `Графика → GPU Текстуры`
inspector now exposes `Read mip` and `Read face / layer`
controls plus an explicit `Read GPU mip / face` action.
The editor calls the existing optional
`gfx::Device::readTextureAsync(TextureHandle,TextureReadbackDesc,
TextureReadFn)`, without treating Texture2DArray/Cube and
multisampled/depth resources as sampler2D. A later owner-thread
callback produces a bounded CPU preview sampled down to at
most 32×32 colored squares using ImGui draw-list rectangles:
no additional GPU preview allocations or texture ownership
are introduced. RGBA8/sRGB use original channels; RGBA16F
diagnostics are approximately tonemapped for display with
explicit warning. The full readback result vector is discarded
after creating its small preview, avoiding persistent large
CPU texture copies.

**Lifetime/safety:** Async callbacks hold a `weak_ptr` to
a private preview state, not the editor window's `this`.
Every new request increments a revision; stale callbacks and
destroyed UI windows cannot publish into invalid state.
If the selected resource disappears, the live GPU table
clears its selection. Unsupported GPU formats and device
operations display a readable fail-closed message; the UI
never implies that unsupported readback succeeded.
The existing direct 2D ImGui texture preview remains
unchanged and closed by default.

**Compatibility:** No authoritative ECS/gameplay/save/network
changes. The inspector reuses `Device::textureResources` and
optional readback; it does not modify native game texture handles.

**Verification:** GitHub code commit succeeded. **Editor C++
build NOT RUN, CTest NOT RUN, real Metal/GL/Vulkan readback/UI
validation NOT RUN, mobile/WebGL not checked.** Preview pixel
orientation, driver format support, shader binding and lifetime
still need real-device checks.

**Next:** Run editor build and all texture CPU fixtures; verify
readback previews for Metal GL33 GLES3 and Vulkan on actual
resources with selected mip/layer/cubemap face. Complete
remaining BasisLZ/UASTC transcode, broad OpenEXR codec coverage
and compute storage-image support as separate bounded stages.
Record every tested SHA and actual outcomes; leave PR Draft
until validated.

## Stage 8G — bound editor async texture previews and invalidate stale callbacks (2026-11-18)

**Code commit:** `7329ee8e853f5fe5b58c303584ff4427f7e30973`.
**Branch:** `feature/texture-rhi-unification`, Draft PR #21.
**Path:** `apps/editor/windows/TextureResourcesWindow.cpp`.

**Source fix:** The shared editor texture inspection window already gained
an independent per-mip/layer async CPU readback preview from another
agent; avoid reimplementing it. This stage sets a strict 16 MiB maximum
for the selected GPU subresource readback before calling the Metal/GL/
Vulkan backend, so a 32×32 downsampled inspector thumbnail does not
cause a giant full-image PBO/blit allocation. Reject unsupported
dimensions, depth and MSAA and defer to backend feature checks for
accepted requests. Suppress repeated readback submissions while a
previous request is still pending. When the selected live texture
changes or disappears, reset the requested mip/layer and clear the
cached preview, incrementing its revision generation. Asynchronous
callbacks carrying old revisions cannot repaint a new resource's
preview (the existing weak_ptr/lifetime guard remains intact).

**Compatibility:** Editor diagnostics only. No GPU texture allocation
paths, scene rendering, ECS, saves/network or asset file formats change.
No new backend methods were added.

**Verification:** GitHub source commit confirmed. Native editor
compilation, ImGui preview and real GPU readback latency/memory tests:
**NOT RUN**; CI green status not verified. The 16 MiB cap is a
source-level budget, not a measured peak memory guarantee.

**Next:** Run the editor on Metal/GL/Vulkan with textures of different
mips/layers and test selection changes during pending readbacks,
resource destruction before completion, unsupported WebGL2 paths
and accepted failed transfers. Then validate all texture CTests,
render passes and platform GPUs before merging.

## Stage 6M — opt-in KTX2 BasisLZ/UASTC transcoder bridge (2026-11-18)

**Code commit:** `81498d54244fed7eaebf267d27aac6f5bcb21444`.
**Branch/PR:** `feature/texture-rhi-unification`, Draft PR #21.
**New file:** `engine/render/Ktx2Transcode.h`.

**Implementation/API:** Added native C++ callback contract
`Ktx2Transcoder`, `parseKtx2WithTranscoder`, and
`uploadKtx2WithTranscoder`. For regular KTX2 containers, the
existing built-in `parseKtx2` is reused unchanged. For KTX2
containers whose VkFormat is zero or whose supercompression
is BasisLZ, callers must provide a decoder/transcoder callback
that yields a canonical `CompressedImage` with all mip/array/
cube/volume slices, not just the first face. The new bridge
checks dimensions, layer/cube consistency, allowed mip counts,
payload byte extents and a per-mip 512 MiB budget before passing
data to the shared GPU uploader. A missing/failed callback or
invalid result fails closed with a descriptive error. GPU
output format selection remains the callback's responsibility,
including block compression and the target device capability.

**Critical limitation:** This stage does **not** include or
implement the actual Basis Universal codec. No Basis library
exists under the repository's `third_party` directory.
Full ETC1S/BasisLZ and UASTC transcoding requires integrating
a correctly licensed and tested external codec provider into
the application or build; the public callback is the seam for
that dependency. No false claim of successful Basis transcoding.
Existing KTX2/DDS importer and ECS/save/network paths remain
unchanged.

**Actual verification:** GitHub code commit completed.
**Native C++ compilation NOT RUN; CTest NOT RUN; real Basis
codec, GPU texture upload, CI pass NOT RUN/NOT CONFIRMED.**

**Next:** Add synthetic Basis-marked KTX2 source with a mock
callback and a malformed-output fixture; register a CPU CTest
and record actual results after tests run. Later vendor/test a
real Basis codec (or use the app's compatible external provider)
and verify transcode formats and full mip/layer/cube fidelity.
Add new handoffs after each stage.

## Stage 8H — focused native Texture RHI build/test log runner (2026-11-18)

**Code commit:** `c89f80fbdba75282352dcc10e8242b92b398d7d8`.
**Branch:** `feature/texture-rhi-unification`, Draft PR #21.
**New path:** `tools/verify-texture-rhi.sh`.

**Source/tool:** Standalone Bash script configuring a dedicated
`build/texture-rhi` CMake tree with `AIVERSE_BUILD_TESTS=ON`,
building the 13 texture-related CPU test targets, running focused
CTest and writing `configure.log`, `build.log`, `ctest.log`
and `summary.txt` into `build/texture-rhi/verify-logs/`.
Optional `--editor` builds `aiverse_editor`; optional `--vulkan`
also builds and invokes `vulkan_backend`. Parallelism configurable
with `--jobs N`, build dir with `--build-dir PATH`, other
`-DCMAKE_*=value` flags forwarded. All target build failures
are collected, not dropped; script exits nonzero on any failed
build or test. A Vulkan skip due to no driver is explicitly
flagged in the summary rather than considered GPU validation.
Run as `bash tools/verify-texture-rhi.sh --editor --vulkan`
on a machine with a real checkout and graphics SDK.

**Compatibility:** New developer-only test script. No engine/game
source, ECS/save/network, asset or editor runtime changes.
GitHub contents API created the file; invoking it with `bash`
does not depend on executable file mode.

**Actual validation:** GitHub source commit confirmed.
Script invocation, CMake configure, compilation, CTest, GPU and
editor testing: **NOT RUN** in this environment. Do not interpret
script presence as passing verification. Existing GH Actions
jobs on older branch commits remain queued without a runner
assigned (ubuntu-latest, windows-latest, macos-latest);
not passing on current HEAD.

**Next:** On macOS/Windows/Linux with native repo checkout,
run `bash tools/verify-texture-rhi.sh --editor` and add
`--vulkan` on Vulkan-enabled machines; send/share the generated
summary and failing logs. Fix *actual* compile/test failures
in the source branch, append separate code/handoff commits,
and only then consider PR #21 production ready.

## Stage 7N — asynchronous disk-backed virtual texture page source (2026-11-18)

**Code commit:** `97b06fcd4b7888c789155858d83e8466cb547d3c`.
**Branch / PR:** `feature/texture-rhi-unification`, Draft #21.
**File:** new `engine/gfx/VirtualTextureDiskQueue.h`.

**Implemented source:** Native C++20 `gfx::VirtualTextureDiskQueue` starts
one owned `std::thread` dedicated to reading immutable, tightly packed
RGBA8 virtual page files. Config declares the trusted asset root, mip-zero
tile dimensions, virtual mip/layer counts, physical page size, maximum
outstanding requests and maximum resident CPU result bytes. Files use a
deterministic integer-only layout
`m<MIP>/l<LAYER>/x<X>_y<Y>.rgba`; requested coordinates are validated
against virtual mip extents and cannot inject arbitrary path segments.
`request(VirtualPageKey)` deduplicates in-flight and ready requests;
`cancel(ticket)` drops queued/completed work and invalidates active
blocking-read results; `takeReady(out,maxBytes,maxCount)` transfers
ownership of bounded result bytes to the render thread. The worker
verifies exact on-disk byte size, reads into CPU-owned buffers, blocks
when the result-memory budget is exhausted and notifies the caller
through polling (no GPU/device calls from the worker). `stats()`,
`lastError()`, `pageBytes()`, `shutdown()` expose lifecycle and
memory accounting. Tickets are monotonic and never recycled on restart.
A missing/truncated file is an explicit failed result, never a silent
partial GPU page. Caller passes successful page bytes to existing
`VirtualTextureStreamer::stage`/`drain` on the GPU owner thread.

**Scope / missing:** This is a real filesystem I/O worker, *not*
a GPU feedback pass, codec/transcoder, native sparse API, or
automatic scene camera demand predictor. The root is a trusted
game asset directory (symlink access is not sandboxed). A single
cancellation of a deduplicated key cancels the shared ticket for
all callers. Thread destruction waits for an active filesystem read;
the worker must be destroyed before its host game shuts down.

**Compatibility:** Opt-in header only; no change to Device ABI,
texture formats, runtime render passes, deterministic ECS,
save/network data or existing project assets. Use as a
separate disk source for the physical page atlas, never as
ECS-authoritative state.

**Actual verification:** File created and code commit confirmed
through GitHub connector. C++ compile, standalone test, GPU,
editor and CI: **NOT RUN** yet. The local container cannot
resolve github.com to clone the AIVERSE checkout. No green
CI status was observed.

**Next:** Write a focused mock disk fixture and CTest target
using a temporary asset root, real binary files, duplicate
request/cancellation, invalid coordinates, exact byte sizes,
ready-byte backpressure, ticket lifetime, restart and missing
file failures. Link with `Threads::Threads`. Run a standalone
local compile where possible and the full native test later.
Then add an integration helper that pumps disk results into
`VirtualTextureStreamer::stage` on the render thread.
Separate source and handoff commits per stage.

## Stage 7O — real disk-page I/O / cancellation / backpressure CPU fixture (2026-11-18)

**Code commit:** `90c654c626d5887f3ebfec1a1c790c8827af975f`.
**Branch:** `feature/texture-rhi-unification` (Draft PR #21).
**Paths:** new `tests/render/virtual_texture_disk_queue.cpp`,
`CMakeLists.txt`.

**Test source:** Registered `aiverse_virtual_texture_disk_queue_test`
and CTest `virtual_texture_disk_queue`, linked with
`Threads::Threads`. Unlike the GPU mock fixtures, this test uses
real temporary filesystem folders and packed `.rgba` page files
with known byte values. It covers two-page FIFO I/O, deduplicated
page request IDs, invalid mip/tile/layer rejection, 64-byte
result-memory backpressure and render-thread polling, rejection
of poll budgets smaller than one page, missing/truncated file
failures without partial pixels, max-outstanding requests,
queued/inflight cancellation, restart of the disk worker with
monotonic ticket IDs, stale ticket rejection and invalid config
refusal. All page paths are created using the exact public
`relativePath` ABI, not manually guessed strings.

**Compatibility:** Test/CMake stage only. No change to GPU
Device, editor, gameplay/ECS state, save/network or file format.
It covers the `VirtualTextureDiskQueue` added in Stage 7N.

**Actual verification:** GitHub source/tree/commit/ref update
succeeded. Native C++ compilation and execution of this CTest:
**NOT RUN** (local container cannot resolve github.com for clone).
It is not a passing test merely because registered in CMake.

**Next:** Compile and execute
`cmake --build build/texture-rhi --target aiverse_virtual_texture_disk_queue_test`
and `ctest --test-dir build/texture-rhi --output-on-failure
-R '^virtual_texture_disk_queue$'` on a native repo checkout.
Add a GPU-thread integration adapter that moves ready pages into
`VirtualTextureStreamer::stage`, handles retry/backpressure,
and discards results for destroyed virtual atlases. Write a
separate code+handoff commit, then update the verification script,
native skill and architecture docs. No background GPU work should
be claimed without real driver validation.

## Stage 7P — disk-to-GPU virtual texture streaming coordinator (2026-11-18)

**Implementation commits:** `d7346d63c96b19b2316609a860d22f653ccb7f59`
(new facade) and `332d9c325d86bb95e9272ebd7a11b8df052c10c3`
(exact page-residency lookup).
**Branch/PR:** `feature/texture-rhi-unification`, Draft #21.
**Files:** new `engine/gfx/VirtualTextureFileStreamer.h`,
`engine/gfx/VirtualTextureStreamer.h`.

**Implemented source:** Native `gfx::VirtualTextureFileStreamer`
composes a real disk I/O worker (`VirtualTextureDiskQueue`) with the
physical GPU page atlas, GPU upload queue, LRU and page-table
publication (`VirtualTextureStreamer`). `initialize` validates
virtual dimensions, layers, page size and RGBA8 format across the
two components and owns their lifetime. `requestPage(key,pin)`
deduplicates in-flight keys, avoids reloading a page already
*exactly* resident and optionally pins important coarse fallback
pages. The added `VirtualTextureStreamer::resolveExact` is
distinct from its existing coarser-fallback `resolve`: callers
still request missing detail pages even when a coarser ancestor
is available. Render-thread `pump(maxNewGpuPages,callback)`
polls bounded disk results, buffers completed page bytes, stages
them into the existing GPU queue (which copies source bytes),
calls the atlas' GPU drain, and delivers events only after
page-table publication or an explicit failed transfer. GPU
staging backpressure retains the CPU bytes for retry. Missing/
truncated disk results produce failure notifications without
GPU data. Disk/GPU pending and buffered pages are cancelable
via `cancel(ticket)` before submission. On a quarantined GPU
atlas, the facade drops outstanding work, masks all success
events and requires explicit reinitialization. Read-only
atlas/table handles, page lookup, stats and error surfaces
are provided. All GPU calls remain on the device thread;
disk worker never calls Device.

**Compatibility:** Opt-in header and one additional method on the
new virtual texture facade, no existing gfx::Device ABI,
ECS/saves/netcode, legacy texture asset or import behavior
changes. Device must outlive the coordinator. The component
implements filesystem-backed page demand **from explicit host
requests**; it is NOT yet automatic GPU-feedback demand paging.
A request already handed to an active GPU submission cannot be
canceled behind the driver's back.

**Verification:** Both commits exist on GitHub; native C++ build,
CTest and real GPU execution are **NOT RUN**. Existing GitHub CI
jobs observed queued/cancelled. The integration path is source-level
and is NOT production-certified.

**Next:** Add a combined real filesystem + mock GPU fixture verifying
coarse pinned page becomes visible only after GPU upload, finer
page replacement, callbacks, byte budgets, missing page failure,
repeated request, cancellation, staging backpressure, and a
quarantined atlas. Register a CTest and then compile on the native
checkout. Add actual virtual-page GPU feedback/prefetch policy
and disk asset packer only in separate documented stages.

## Stage 7Q — integrated real file-to-GPU virtual texture fixture (2026-11-18)

**Code commit:** `b21a5ba93df731174b8f287bf91e6d58511bb9cd`.
**Branch:** `feature/texture-rhi-unification`, Draft PR #21.
**Paths:** new `tests/render/virtual_texture_file_streamer.cpp`,
`CMakeLists.txt`.

**Test source:** Register
`aiverse_virtual_texture_file_streamer_test` /
CTest `virtual_texture_file_streamer` and link `Threads::Threads`.
The integration fixture uses actual immutable page files in a
temporary directory plus a deterministic mock `gfx::Device`.
Exercises the new `VirtualTextureFileStreamer`: constructing a
physical `Texture2DArray` atlas and GPU-backed page table,
deduplication and exact-residency distinction, a coarse fallback
pinned until its disk read and GPU upload complete, independent
fine page upload that overrides the ancestor, deterministic LRU
eviction of an unpinned detail page, restoration of coarse
fallback in the shader table, GPU texture/resource cleanup,
cancel of disk requests, missing file errors without
quarantining healthy GPU resources, and GPU update failure
quarantining the atlas so no partially updated texture can
be sampled. Invalid disk/atlas dimensions fail before allocation.
All tests are source-level and need CMake compilation.

**Compatibility:** Test/CMake only. No new gameplay, ECS,
save/netcode or public rendering API. Existing `VirtualTextureFileStreamer`
production facade from Stage 7P is unchanged.

**Actual verification:** GitHub source commit/ref update succeeded.
Native compiler, CTest and real GPU backend execution:
**NOT RUN**; the container has no clone and cannot resolve
github.com, and prior GitHub runners remain queued.
The test is not a passing verification until it executes.

**Next:** Compile the integrated fixture:
`cmake --build build/texture-rhi --target aiverse_virtual_texture_file_streamer_test`,
`ctest --test-dir build/texture-rhi --output-on-failure
-R '^(virtual_texture_disk_queue|virtual_texture_file_streamer)$'`.
Fix compiler/behavior failures. Update `tools/verify-texture-rhi.sh`,
the native texture RHI agent skill and `docs/texture-rhi.md`
to include disk worker/facade and new tests, each with a
separate code/docs and handoff commit.
After that implement an optional camera/CPU visibility-based
prefetch planner and/or real GPU feedback; do not imply
automatic sparse GPU feedback or complete BasisU transcoding.

## Stage 7R — disk-backed VT skill, architecture and verification script integration (2026-11-18)

**Implementation/documentation commit:** `c4a54cb627be31f1326530fc4c555a1bc44eebd7`.
**Branch / PR:** `feature/texture-rhi-unification`, Draft #21.
**Files:** `tools/verify-texture-rhi.sh`, `docs/texture-rhi.md`,
`skills/visuals/aiverse-texture-rhi/SKILL.md`.

**Changes:** Register the disk-thread CTest `virtual_texture_disk_queue`
and the real-file-to-mock-GPU integration CTest
`virtual_texture_file_streamer` in the focused verification script's
target list and both regular/Vulkan CTest filters. The architecture
document now distinguishes the new *actual* filesystem worker and
explicit page-request-to-atlas pipeline from unimplemented automatic
GPU feedback and hardware sparse GPU resources. The native AI skill
references both public headers and includes a concrete integration
example with trusted `.rgba` page layout, matching disk/atlas configs,
pinned coarse fallback request and render-thread `pump` callback.
Gameplay agents must reuse these independent texture libraries rather
than regenerate an RTS/FPS-specific loader.

**Compatibility:** Developer tooling, docs and skill only. No game
simulation, save/network state or gfx::Device API changed in this
stage. Existing callers need not enable the disk worker.

**Actual verification:** GitHub code/tree/commit/branch update
succeeded. Native compiler, Bash script invocation, CTest
and Metal/GL/Vulkan GPU tests: **NOT RUN**. The container still
cannot resolve github.com for a full checkout; GitHub Actions
were queued/cancelled, without passing results.

**Next:** Run `bash tools/verify-texture-rhi.sh --editor` on an actual
checkout and add `--vulkan` where Vulkan SDK/device exists.
Inspect `build/texture-rhi/verify-logs/{configure,build,ctest,summary}`
and fix real issues. For the two new targets use
`ctest -R '^(virtual_texture_disk_queue|virtual_texture_file_streamer)$'`.
Then implement a camera/CPU visibility prefetch policy and
optional atlas feedback buffer after measured demand; real
GPU page-table shader sampling remains required before merge.
Each stage still needs separate code + handoff commits.

## Stage 7S — dependency-free virtual texture disk asset packer (2026-11-18)

**Code commit:** `fdd23a2e25b51c6231f86308777452fc263f638c`.
**Branch:** `feature/texture-rhi-unification`, Draft PR #21.
**Paths:** `tools/virtual_texture_pack.py`,
`tests/render/virtual_texture_pack.py`.

**Implemented source:** Standard-library-only Python 3 CLI/utility
`pack_pages(inputRGBA8Layers,width,height,pageSize,gutter,destination)`
generates all `m<MIP>/l<LAYER>/x<X>_y<Y>.rgba` disk tile files
expected by the C++ `VirtualTextureDiskQueue`. Supports 1..65536
raw RGBA8 array layers (same source dimensions), a configurable
physical page size with edge-clamped gutters, deterministic mip
generation with 2x2 box filtering and edge clamping, mip count
until the virtual tile grid reduces to 1×1, exact fixed-size
physical pages, and a stable JSON manifest describing schema,
source sizes, virtual tile counts, layers, mip levels, gutter,
physical page dimensions and per-input SHA256 hashes.
The packer checks input dimensions, source file exact size,
pixel budget, tile/gutter validity and output collisions.
All writes happen under a temporary directory in the
destination's parent; only after successfully processing
every layer and mip is the staging directory renamed to
the requested output path. Failures clean temporary content
and do not destroy preexisting output.

**New tests:** A pure-Python `unittest` fixture with
multi-layer 4×4 RGBA gradients, complete mip page count,
edge-gutter sampling, averaged mip texels, deterministic
odd-dimensional output, SHA/manifest metadata, file truncation
rollback and invalid output/pixel/gutter limits.

**Known source-level test issue:** The initial `test_multi_layer_mips_and_gutters`
test locates its expected green-channel pixel at a row-one
offset while checking a row-two source. Correct the index
from `(4+2)*4` to `(2*4+2)*4` in a separate fix commit
before calling that test passing. This is a test assertion
offset mistake, not a production packer algorithm finding.

**Compatibility:** Opt-in packaging tool. No change to
native gfx::Device, simulation/ECS authority, saves/network,
existing project asset formats or render pipeline.
The asset input is **raw RGBA8**, not PNG/EXR/KTX2; convert
source images upstream. Packed byte pages include gutters
and fit the disk I/O worker/physical GPU atlas contract.

**Verification:** GitHub code/tree/commit/ref update succeeded.
Python unittest and CMake/CTest/native GPU execution:
**NOT RUN** yet. Do not claim a passing packer test before
fixing the known assertion and executing it.

**Next:** Fix the gutter-index assertion; register the Python
test in the focused native verifier and CTest where Python3
is available. Execute a real Python unittest in this container
if possible, record its exact outcome; add a matching
manifest sanity check in the disk-backed facade and
configure input conversion to PNG via an optional image
provider later. Each bounded code stage requires a distinct
handoff commit.

## Stage 7T — correct Python packer gutter assertion and register CTest (2026-11-18)

**Code commit:** `e3618dd816b063ccf4af9ce74ef2180824ce444e`.
**Branch / PR:** `feature/texture-rhi-unification`, Draft #21.
**Paths:** `tests/render/virtual_texture_pack.py`,
`CMakeLists.txt`, `tools/verify-texture-rhi.sh`.

**Fix:** Correct the synthetic 4×4 gutter assertion's source
row coordinate from a row-one offset to the intended row-two
offset, `(2*4+2)*4`, matching the actual edge-clamped tile
sampling. Register `virtual_texture_pack` under the existing
`AIVERSE_BUILD_TESTS && Python3_Interpreter_FOUND` CTest gate,
with a direct Python test runner and no third-party packages.
Add this test name to both normal and optional-Vulkan filters
in the canonical `tools/verify-texture-rhi.sh` runner.
Other pure Python tests include odd sizes/multi-layer metadata,
mip generation and transactional failure rollback.

**Compatibility:** Testing and verification tooling only.
No native engine, GPU/scene, ECS, save/network or asset schema change.

**Verification:** GitHub source commit succeeded. Real Python
test process and CTest build/test: **NOT RUN at commit time**.
The test's source code is present and registered but not
assumed passing.

**Next:** Execute the Python unittest from a real checkout,
and test the same source logic in an isolated local sandbox
if the code can be supplied there. Record actual commands,
exit codes and failure diagnostics. Then add a safe game
asset manifest loader/validator to match the disk queue
configuration, and validate the whole file→GPU flow.
Separate implementation and handoff commits for each stage.

## Stage 7U — executed Python packer tests against exact committed blobs (2026-11-18)

**Code:** stages 7S/7T, source commits
`fdd23a2e25b51c6231f86308777452fc263f638c` and
`e3618dd816b063ccf4af9ce74ef2180824ce444e`.
**Branch/PR:** `feature/texture-rhi-unification`, Draft #21.
**Verification-only stage:** no engine/source files changed here.

**Actual executed test environment:** Isolated local sandbox on
Python **3.13.5**, using copies of the two committed files.
The copies were verified byte-for-byte via Git blob hashes:
`tools/virtual_texture_pack.py` =
`82e1a749b926fa4ae410710c78bac14dc2777d88`;
`tests/render/virtual_texture_pack.py` =
`b681fd09ce216606aeee6d2fd43bf4fa00505da1`,
matching the exact GitHub file SHAs on this branch.

**Command actually run:**
`python3 /mnt/data/_vt_check/tests/render/virtual_texture_pack.py -v`
**Result: PASS, 3 tests in 0.006 seconds**, process exit 0:
`test_invalid_sources_are_transactional`,
`test_multi_layer_mips_and_gutters`,
`test_odd_dimensions_and_determinism` all `ok`.

**CLI smoke test actually run:** Invoked the exact committed
packer as a subprocess with an 8×8 raw RGBA8 layer, physical
page size 6, gutter 1 and a temporary output root.
**Result: exit 0**, manifest reported 2×2 mip-zero tiles
and 2 virtual mip levels; generated 5 page files, each
exactly 144 bytes, plus `manifest.json` (verified present).

**Scope of PASS:** Only these Python packer and asset-layout
checks were actually executed. No full AIVERSE repository
checkout was present. CMake/CTest, C++ disk worker/facade
compilation, Metal/GL/Vulkan GPU tests, editor and CI:
**NOT RUN / NOT CONFIRMED PASSING**. The Python results are
not evidence that the native graphics pipeline works.

**Next:** Build and run native
`virtual_texture_disk_queue` and
`virtual_texture_file_streamer` CTests on a repo checkout.
Validate the resulting `.rgba` files with the real C++
filesystem loader and atlas/page-table shader on a GPU;
then implement camera visibility prefetch or GPU feedback,
with separate source/handoff commits.

## Stage 6N — harden KTX2 external transcoder boundary (2026-11-18)

Code SHA: 6f0569f638f186fb8606a9d7065101e80f4dcac3.
Branch: feature/texture-rhi-unification, Draft PR #21.
Changed file: engine/render/Ktx2Transcode.h.

Implementation: Stage 6M's external callback boundary now validates the KTX2
source header/index before calling Basis Universal: magic, total input budget,
dimensions, declared 1D/3D/cube/array topology, mip count, compression scheme
and level index ranges. Callback output must preserve source extents,
array layers, cube faces and mip count. Unknown BlockFormat enum values,
non-exact mip sizes and aggregate decoded payload exceeding 512 MiB fail closed.
Exceptions inside a plugin callback clear output and report failure.
Non-Basis KTX2 still goes through the standard parseKtx2 parser.

API: Existing Ktx2Transcoder, validateTranscodedImage,
parseKtx2WithTranscoder and uploadKtx2WithTranscoder signatures remain unchanged.
The actual BasisLZ/UASTC codec is not bundled; an external codec is required.
This stage does NOT provide a real native Basis transcoder.

Compatibility: No RHI backend, ECS, save/network, other importer or material
shader behavior changed.

Actual verification: GitHub source commit succeeded. Native C++ compiler
NOT RUN, CTest NOT RUN, GPU NOT RUN. Full native checkout unavailable.
No passing result is claimed.

Next: Synthetic CTest fixture covering valid layered Basis transcode,
truncated index, mismatched topology/levels, unknown format, failure,
callback exception and exact mip payloads. Commit source and handoff separately.

## Stage 6O — KTX2 transcoder CPU contract fixture (2026-11-18)

Code commit: 89c146f5b079f9a952d3001786d1f6f904671ceb.
Branch: feature/texture-rhi-unification; Draft PR #21.
Files: tests/render/ktx2_transcode.cpp, CMakeLists.txt.

Source: register aiverse_ktx2_transcode_test / ktx2_transcode with
the existing aiverse_models and aiverse_zstd_dec link targets.
Synthetic KTX2 tests model two-mip twelve-face Basis data and
verify complete mip-major output through an injected callback.
Negative fixtures cover missing codec, callback errors/exceptions,
payload offset pointing inside the header, truncated file index,
unsupported supercompression, illegal faces, wrong array extent,
truncated/oversized mip output and unknown BlockFormat enums.
An ordinary non-Basis RGBA8 KTX2 bypasses the plugin and uses
the standard parser. No decoder implementation is bundled.

Verification performed: GitHub source + CMake commit and fast-forward
ref update succeeded. C++ build NOT RUN; CTest NOT RUN; GPU NOT RUN.
No verified PASS claim is made for the unexecuted fixture.

Next: Ensure the focused texture verification script includes
ktx2_transcode, then implement only a genuine licensed Basis codec
when its dependency is available. Before approval, run a native
build/CTest and a real GPU upload covering normal/sRGB, arrays,
cubemaps and mip chains. Follow the per-stage separate handoff policy.

## Stage 6P — Basis transcoder verifier and native-agent documentation (2026-11-18)

Source/docs commit: 76fce9b3e74ee17e774fd11b59b212f7ae50d783.
Branch: feature/texture-rhi-unification, PR #21 Draft.
Paths: tools/verify-texture-rhi.sh, skills/visuals/aiverse-texture-rhi/SKILL.md, docs/texture-rhi.md.

Changes: Add aiverse_ktx2_transcode_test to the focused build target list
and ktx2_transcode to both normal and --vulkan CTest expressions.
Update the AI texture skill with the injected Ktx2Transcoder API and
requirements for complete mip/face arrays, codec licensing and GPU support.
Document the bridge's exact topology/budget validation separately from
the still-unimplemented concrete Basis Universal codec. No ECS or native
graphics backend files are changed in this documentation stage.

Actual validation: GitHub commit and ref update succeeded. Native C++ build,
CTest, verifier script, GPU tests and skill audit were NOT RUN.
Next: Implement a real licensable/available Basis codec instead of a stub,
or continue independent storage textures, disk streamer integration and
native verification. Do not merge PR #21 before build/driver gates.

## Stage 4Y — explicit compute-writable texture API and Metal binding (2026-11-18)

Code commit: 2c799f4e8f55a03be10aeceba5d8612604865223.
Branch: feature/texture-rhi-unification, PR #21 Draft.
Files: engine/gfx/Gfx.h, engine/gfx/metal/GfxMetal.mm,
tests/render/texture_contract.cpp.

API: Device::supportsStorageTexture(PixelFormat, TextureDimension) defaults
to false and Device::bindComputeStorageTexture(slot, TextureHandle)
defaults to false. Existing setComputeTexture continues to support
read-only sampled compute inputs. Writable binding requires an active
compute encoder, a storage=true texture, a supported format/dimension
and a valid slot; unsupported requests fail visibly.

Metal source now advertises a conservative writable subset:
RGBA8 or RGBA16F Texture2D, Texture2DArray and Texture3D.
createTexture checks the advertised capability before allocation;
MTTexture retains the storage creation flag and the writable
binding checks MTLTextureUsageShaderWrite. No hidden downgrade to
sample-only storage is performed. GL33/GLES3 and Vulkan retain the
safe default unsupported contract because compute/shader image
binding is not yet implemented in those backends.

Source test additions in texture_contract cover structurally valid
2D, 2DArray and 3D writable descriptors and reject compressed
storage formats. These are CPU descriptor tests, not a Metal GPU test.

Actual verification: GitHub multi-file commit/ref update succeeded.
C++ compile NOT RUN, CTest NOT RUN, Metal shader/compute test NOT RUN,
GL/Vulkan unsupported-device test NOT RUN. No GPU capability
has been claimed certified.

Next: Add an actual Metal compute shader test that writes RGBA8
and RGBA16F 2D/array/volume images and confirms readback.
Build native editor, then add Vulkan compute descriptor/pipeline
only if the backend supports the necessary synchronization/bindings.
Do not present the capability as cross-backend storage support.

## Stage 5P — native compressed sRGB format selection (2026-11-18)

Code commit: 71259009accf0d2e6943bda692c59a97a5c4093e.
Branch: feature/texture-rhi-unification, Draft PR #21.
Paths: engine/gfx/Gfx.h, engine/gfx/gl/GfxGL.cpp,
engine/gfx/metal/GfxMetal.mm, engine/gfx/vulkan/VkResources.cpp,
engine/render/TextureAsset.h, tests/render/texture_contract.cpp,
tests/render/texture_asset.cpp, apps/editor/windows/TextureResourcesWindow.cpp.

Implementation: added native PixelFormat variants BC1_sRGB, BC3_sRGB,
BC7_sRGB, ETC2_RGB8_sRGB, ETC2_RGBA8_sRGB, ASTC_4x4_sRGB,
ASTC_6x6_sRGB. Shared compressed-format block byte/dimension
calculations include the variants. OpenGL maps to standard
compressed sRGB GL enums; GLES/WebGL sRGB S3TC capability is
gated separately from base S3TC. Metal maps to sRGB variants
on Apple-supported families and macOS BC; Vulkan uses matching
VK_FORMAT_*_SRGB_BLOCK and native per-format device probing.
TextureAsset now selects hardware-sRGB compressed upload for
appropriate KTX2/DDS color-space metadata rather than requiring
opt-in manual shader gamma conversion or CPU RGBA8 fallback.
Unsupported hardware formats still use CPU fallback where a
decoder exists; ASTC and BC6H remain fail-closed without a
compatible native GPU or external decoder. BC1_RGB special
opaque-index semantics continue to avoid the BC1_RGBA shortcut.
Editor texture resource names include all appended formats.

Test sources added: texture_contract checks sRGB block sizes
and ASTC block dimensions; texture_asset mock verifies native
BC7_sRGB and BC1_sRGB GPU descriptors without manual shader opt-in.

Compatibility: Enum values are appended to preserve earlier
numeric identifiers. No ECS/gameplay, save/network schema,
material shader or non-texture backend API changes.

Verification: GitHub code commit/ref update succeeded.
C++ compile NOT RUN; CTest NOT RUN; GL/WebGL/GLES, Metal
and Vulkan sampling/readback NOT RUN. The enum names and
backend support require real platform compiler/GPU checks.

Next: Add synthetic KTX2 sRGB format upload tests for
ETC2/ASTC and reject unsupported mobile sRGB compression
without silently selecting UNORM; then run native build
and GPU tests before merge. Maintain per-stage handoff.

## Stage 5Q — compressed sRGB KTX2 and CPU fallback regression source (2026-11-18)

Code SHA: ca71c7459ef0940acd38a538e32bc4a083fd2199.
Branch: feature/texture-rhi-unification (Draft PR #21).
Files: tests/render/ktx2_layers.cpp and tests/render/texture_asset.cpp.

New synthetic KTX2 source fixtures cover all seven native sRGB
block variants: BC1, BC3, BC7, ETC2 RGB, ETC2 RGBA, ASTC 4x4 and
ASTC 6x6. The parser must preserve true srgb metadata, source
BlockFormat and correct single-mip block payload byte length.
Mock RHI uploads exercise native ETC2 RGB sRGB and ASTC 4x4 sRGB
without a manual shader gamma opt-in. BC1_RGB sRGB (VkFormat132)
is deliberately decoded to uncompressed RGBA8_sRGB rather than
reinterpreted as BC1_RGBA_sRGB and losing opaque index-3 semantics.

Testing status: GitHub code/test source commit succeeded.
Native C++ build NOT RUN; ktx2_layers and texture_asset CTests
NOT RUN; Metal/Vulkan/GL/GLES/WebGL GPU sampling NOT RUN.
No PASS result claimed.

Next: Update native texture skill/docs for sRGB selection and
test the staged source using real CMake/CTest. Verify shader
sampling/color conversion on actual Metal/Vulkan/GL and mobile
devices. Keep PR #21 Draft until backend and regression gates pass.

## Stage 5R — sRGB compression format documentation (2026-11-18)

Code/docs SHA: ef7b57d478377bedecf1c893b4e36965a160af81.
Branch: feature/texture-rhi-unification (Draft PR #21).
Files: docs/texture-rhi.md, skills/visuals/aiverse-texture-rhi/SKILL.md.

Documentation now describes hardware-compressed sRGB BC1/BC3/BC7,
ETC2 and ASTC variants with backend capability checks, native
color decoding, CPU RGBA8_sRGB fallbacks, WebGL2 sRGB-S3TC
extension requirement and BC1_RGB opaque index-3 semantics.
Legacy trustShaderSrgbDecode remains an explicit shader opt-in
rather than a requirement for actual sRGB-compressed GPU formats.
The native agent skill instructs game coders to reuse these
native RHI paths rather than re-encode or double-convert color.

Actual verification: GitHub docs commit and ref update succeeded.
C++ compile, CTest, skill catalog audit and GPU shader tests NOT RUN.
No new engine code or save/network data changed in this stage.

Next: Native compilation and CTest on current HEAD; GPU color
accuracy captures for Metal/GL/GLES/Vulkan/WebGL2. Record exact
outcomes and correct failures before approving Draft PR #21.

## Stage 5S — explicit pair include for KTX2 sRGB fixture (2026-11-18)

Code commit: 1ddf652f9241e88d573a7ecb87bf7c0511f54a96.
Branch: feature/texture-rhi-unification (Draft PR #21).
Path: tests/render/ktx2_layers.cpp.

Source review correction: explicitly include <utility> for the
std::pair<uint32_t,BlockFormat> vector fixture introduced
in Stage 5Q, rather than relying on transitive declarations
from <vector>. No behavior/runtime implementation changed.

GitHub source commit succeeded. Native compiler NOT RUN,
CTest NOT RUN, actual GPU shader tests NOT RUN.
Next: Build the grouped texture suite on a native checkout
and fix genuine compiler/test errors; preserve stage handoffs.

## Stage 7V — pure UV visibility prefetch planner (2026-11-18)

Code commit: bd1e38fdd4d732d98820527a15293923996cde3d.
Branch: feature/texture-rhi-unification; PR #21 Draft.
New file: engine/gfx/VirtualTexturePrefetch.h.

Public API: gfx::VirtualTexturePrefetchPlanner with initialize(config),
plan(regions,exactResident), stats(), reset(); descriptors
VirtualTextureVisibleRegion, VirtualTexturePrefetchConfig,
VirtualTexturePrefetchRequest and VirtualTexturePrefetchStats.
The planner takes only normalized UV rectangle visibility, mip
target, virtual layer and priority: no FPS, RTS, camera entity,
foliage/terrain or GPU API dependencies. It validates finite input,
clips rectangle bounds, sorts higher-priority views first,
constructs deduplicated coarse-to-fine mip page requests, pins
coarsest pages if selected, optionally skips EXACT resident pages
and caps regions/candidates/new requests per frame. Stable
ordering is mip descending, priority descending, layer/y/x.
Unspecified application visibility and desired mip selection
remain caller-driven, not guesses about camera or textures.

Compatibility: CPU-only opt-in header, no existing renderer
usage/ABI, ECS save/network or source asset-format changes.
Known limitation: candidate budget caps page enumeration; when
world UV coverage exceeds it, later fine mips are deferred.
A camera/visibility adapter and deterministic fixture are next.

Verification: GitHub code commit completed. C++ compilation,
CTest, full native build and Metal/GL/Vulkan GPU tests NOT RUN.
Next: add a mock CPU CTest for invalid rectangles, overlapping
views, coarse-first order, clipping, exact-resident exclusion and
candidate/request limits, then integrate with the existing
VirtualTextureFileStreamer requestPage API. Keep per-stage
implementation/handoff separate.

## Stage 7W — UV visibility prefetch CPU fixture and verification target (2026-11-18)

Source commits: 568a75fe6f2f08cb6c82b66f73d2be18e9a498a5
(fixture + test registration), 445f393060671c79760c367f5b3ad17b53f5e814
(correct overlapping visible-region candidate count).
Branch: feature/texture-rhi-unification, PR #21 Draft.
Files: tests/render/virtual_texture_prefetch.cpp, CMakeLists.txt,
tools/verify-texture-rhi.sh.

Implementation: Register aiverse_virtual_texture_prefetch_test as
CTest virtual_texture_prefetch and add it to the focused verifier.
CPU-only tests exercise coarsest-mip priority, multiple virtual
layers, exact-resident omission rather than parent-fallback omission,
UV clamping, overlapping rectangle deduplication, invalid/nonfinite
coverage, stable ordering, pinned fallbacks, maxRegions,
maxCandidates and maxRequestsPerFrame. Test fixture count was
corrected after source review: 8 distinct pages for overlapping
regions in the tested mip geometry, not 12.

Compatibility: No native renderer, graphics backend, game ECS or
save/network changes. Test code only; planner remains the Stage 7V
engine/gfx/VirtualTexturePrefetch.h source.

Actual verification: GitHub source and correction commits succeeded.
CMake compile NOT RUN, CTest NOT RUN, full native build NOT RUN,
GPU sampling NOT RUN; no passing status is claimed.

Next: Provide optional, genre-independent requestPage integration
with VirtualTextureFileStreamer. It must not interpret parent
fallback as exact residency. Then document native game integration,
with a source commit and a separate append-only handoff.

## Stage 7X — connect UV visibility plans to real disk-backed VT uploads (2026-11-18)

Source commit: e21efd6f3a1a8545fae76699f46e0dd8e9101d6f.
Branch: feature/texture-rhi-unification, Draft PR #21.
Files: engine/gfx/VirtualTextureFileStreamer.h and new
engine/gfx/VirtualTexturePrefetchAdapter.h.

API: prefetchVisibleVirtualPages(VirtualTextureFileStreamer&,
VirtualTexturePrefetchPlanner&, visibleRegions) returns bounded
VirtualTexturePrefetchDispatch containing planner stats, accepted
new disk-page tickets and refused count. It skips both EXACT
resident pages and pending disk/GPU pages; parent-fallback
sampling does not suppress detail loading. It upgrades pins
on already resident/pending coarsest fallback pages.
VirtualTextureFileStreamer now exposes exactResidentPage,
pendingPage and pinPage; duplicate requestPage(key,pin=true)
also promotes an existing pending request to pinned.
Work is sorted coarse-to-fine by the Stage 7V planner; if
queue capacity rejects a page, it stops before submitting
finer pages and allows a later frame to replan. GPU Device
work remains solely inside streamer.pump(); I/O remains on
the existing worker. No genre-specific state is introduced.

Compatibility: new opt-in C++ header and additive inline
facade methods only. No RHI backend virtuals, serialization,
game ECS, save/network or disk asset format changed.

Actual verification: GitHub source/tree commit succeeded.
C++ build NOT RUN, CTest NOT RUN, real disk/GPU integration
NOT RUN. Adapter still needs a deterministic test fixture
covering exact-vs-parent residency, dedup, pin upgrade,
backpressure and cross-frame planning.

Next: add prefetch adapter fixture and native AI skill usage.
Commit source and handoff separately, then execute real native
build/CTest when implementation is complete.

## Stage 7Y — integrated visibility-prefetch-to-GPU test source (2026-11-18)

Test source commit: 641c7977ac0b68dd9be1b20ad5c6080d2c75bab3.
Branch: feature/texture-rhi-unification, PR #21 Draft.
File: tests/render/virtual_texture_file_streamer.cpp.

The existing native filesystem+mock Device CTest now additionally
includes VirtualTexturePrefetchAdapter.h and exercises automatic
UV visibility to disk page queue to owned GPU atlas/table uploads.
First-frame request budget submits coarse and intermediate mip pages;
the following frame skips those pending pages and requests the
fine mip; a third frame does not issue duplicate reads. After
worker/GPU completion, exact residency of three visible pages
is verified and the coarsest mip is pinned. The fixture also
constructs a separately pending unpinned coarse page, requests
the visible region and checks promotion to a pinned fallback
before GPU residency is published. All pages are synthetic
fixed-size RGBA8 assets under a temporary test directory.

No additional CMake target is required: the existing
aiverse_virtual_texture_file_streamer_test / virtual_texture_file_streamer
target now builds this integration test.

Compatibility: test-only update; no native RHI, ECS, save/network
or asset-layout modification. No automatic camera-to-UV projection;
that is still the caller's responsibility.

Verification: GitHub test-source commit succeeded. Native C++
build NOT RUN; CTest NOT RUN; actual disk/GPU hardware tests
NOT RUN. Do not report this fixture as passing.

Next: Document prefetchVisibleVirtualPages in the native texture
skill, along with the UV visibility adapter ownership and
coarse fallback pinning guarantees, then hand off. Later
compile/run CTest and fix actual failures.

## Stage 7Z — native visibility prefetch agent guidance and test routing (2026-11-18)

Documentation code commit: f9e7e8640529fb45b461b6e33ec7b0dc6b5c694f.
Branch: feature/texture-rhi-unification, PR #21 remains Draft.
Paths: skills/visuals/aiverse-texture-rhi/SKILL.md,
docs/texture-rhi.md and AGENTS.md.

Implementation: Native texture skill has a genre-independent
example projecting a visible UV rectangle into
VirtualTexturePrefetchPlanner and prefetchVisibleVirtualPages,
then pumping the existing disk-backed GPU streamer. It documents
exact residency vs coarse fallback, pin promotion, bounded
CPU region/candidate and frame-request budgets, request tickets
and caller ownership of camera/terrain/world visibility projection.
The canonical architecture doc adds an explicit prefetch section,
references both new public headers and includes
aiverse_virtual_texture_prefetch_test and virtual_texture_prefetch
in native CMake/CTest commands. AGENTS.md instructs all game agents
to reuse the generic prefetch APIs, not to build per-genre streamers.

Compatibility: Documentation and AI skill only, no C++ engine
runtime, asset formats, ECS authority, saves or networking changed.

Actual verification: GitHub doc/tree/ref update succeeded.
C++ compile, CTest, skill audit and real driver validation NOT RUN.
No green CI or GPU verification claimed.

Next: Build the feature branch and execute the expanded focused
texture tests. Fix any compile/test failures, especially
disk->GPU prefetch callback ordering and backend source integration.
Add GPU feedback only as a separate bounded pipeline with
safe synchronization and a separate handoff.

## Stage 7AA — bounded RGBA8 GPU feedback codec for virtual textures (2026-11-18)

Code commit: 44dab9d947f51b62ec46827515135a6734f95785.
Branch: feature/texture-rhi-unification, PR #21 Draft.
New file: engine/gfx/VirtualTextureFeedback.h.

Implementation: Defines a portable *opt-in* 32-bit virtual page ID
stored as four RGBA8 UNORM bytes: x/y use 11 bits each, mip uses
five bits, virtual layer four bits, and the MSB marks valid pixels.
Zero is an invalid/cleared pixel. CPU
encodeVirtualTextureFeedback/decodeVirtualTextureFeedbackId
roundtrip this layout. Bounded VirtualTextureFeedbackDecoder
accepts only exact packed linear RGBA8 GPU readback data,
validates virtual tile bounds and format, skips invalid pixels,
deduplicates pages and ranks them by observed pixel frequency.
The decoder converts page coordinates to normalized
VirtualTextureVisibleRegion rectangles for the existing
UV prefetch planner, retaining source mip/layer and priority.
Shader snippets kVirtualTextureFeedbackGLSL and
kVirtualTextureFeedbackMSL encode the same integer bit layout.

Source limits: 2048x2048 mip-zero tile grid, <=31 mip levels,
<=16 virtual layers, 16MiB max readback pixels (config limit),
65536 max unique page records, bounded output regions.
This is a feedback codec + shader helper, NOT a registered
render pass, GPU feedback camera or automatic readback loop.
App must render the helpers to unblended non-sRGB RGBA8
and call Device::readTextureAsync, then feed decoded regions
to prefetchVisibleVirtualPages. Its callback still executes on
the graphics-device owner thread in a later frame.

Compatibility: CPU/GLSL/MSL header only; no change to
gfx::Device ABI, RHI backend, ECS, save/network or asset format.

Verification: GitHub source commit succeeded. C++ compilation,
CTest, GLSL/MSL shader compilation, GPU write/readback accuracy
and native platform tests NOT RUN.

Next: Add CPU codec roundtrip, malformed readback, multi-layer
and frequency priority regression tests; register in CMake and
focused verifier, then add usage instructions to the agent skill.
Run native GPU feedback render/readback before release.

## Stage 7AB — virtual texture feedback CPU regression test and verifier (2026-11-18)

Source commits: cd96dede3aa5cac94d9743dfdb07fcecbd3951af
(fixture, CMake registration, focused test verifier),
5861334b1f6c0a08b4bbef9582dfb45fddf1b5a9
(invalid sentinel assertion uses VirtualPageKey.mip rather
than nonexistent VirtualPageKey::valid()).
Branch: feature/texture-rhi-unification, Draft PR #21.
Files: tests/render/virtual_texture_feedback.cpp,
CMakeLists.txt and tools/verify-texture-rhi.sh.

New GPU-independent CTest aiverse_virtual_texture_feedback_test /
virtual_texture_feedback covers packed 32-bit feedback ID
roundtrips, invalid zero sentinel and out-of-range IDs,
frequency-based deduplication, bounded RGBA8 framebuffer
readback decoding, output UV rectangle priority and mip/layer
preservation, parent-mip-first integration with the generic
VirtualTexturePrefetchPlanner, refusal of sRGB or truncated
readback results, finite unique-page and pixel count limits,
and rejecting unsupported virtual-layer metadata.
Verification tool adds this target and filter in both
ordinary and optional Vulkan runs.

Compatibility: test registration and fixture only; runtime
codec remains Stage 7AA. No ECS, save/network or backend API
changes in this stage.

Actual verification: GitHub code+fix commit succeeded.
CMake configure NOT RUN, C++ compile NOT RUN, CTest NOT RUN,
real feedback shader GPU render/readback NOT RUN.
No claims of passing native tests.

Next: Document render-to-linear-RGBA8 feedback usage in
the native texture agent skill, and connect the decoder to
the regular device readTextureAsync call in an opt-in
owner-thread helper with safe callback ownership. Do not
merge without actual Metal/GL/Vulkan shader and CTest checks.

## Stage 7AC — async GPU feedback-to-disk prefetch runtime facade (2026-11-18)

Code commit: 8bef3d8085bbb327818f3123d1dc847fea34c0e2.
Branch: feature/texture-rhi-unification, PR #21 Draft.
New file: engine/gfx/VirtualTextureFeedbackRuntime.h.

Implementation: Header-only VirtualTextureFeedbackRuntime composes
the Stage 7AA RGBA8 page feedback decoder, Stage 7V CPU UV
prefetch planner and Stage 7X disk-backed request adapter.
initialize(Device&,feedbackConfig,prefetchConfig) validates
consistent virtual tile geometry and bounded decoder budgets.
capture(TextureHandle,TextureReadbackDesc) only requests the
existing optional asynchronous RHI readback, outside encoders.
The callback owns a weak_ptr to shared decoder state, never
captures the helper's this pointer, and can safely be dropped
after shutdown. A second capture is refused while the prior
request is pending or its decoded result awaits consumption.
submit(VirtualTextureFileStreamer&) sends decoded visible tile
requests to the normal disk staging queue on its owner thread;
the caller independently invokes streamer.pump() to issue GPU
uploads and publish page-table mappings.

Explicit limits: app must render a linear non-sRGB RGBA8 feedback
texture with shader IDs and no blending/tone mapping. No
implicit camera pass, scene shader insertion or GPU compute
feedback pipeline is created. RHI backends still require
actual asynchronous readback support. There is no blocking
GPU wait or cross-thread Device call in this facade.

Compatibility: Pure optional native header, no change to Device
virtual API, ECS authority, save schema, network, gameplay
or existing backend render paths.

Actual verification: GitHub file commit succeeded.
C++ compiler NOT RUN; CTest NOT RUN; real asynchronous
Metal/GL/Vulkan feedback rendering/readback NOT RUN.

Next: Add a CPU/mock RHI test that simulates deferred completion,
device refusal, second-pending capture, shutdown with a stale
callback, and visibility submission to the file streamer.
Update native texture skill/AGENTS.md, and keep PR #21 Draft
until full native verification.

## Stage 7AD — deferred feedback callback and disk prefetch integration fixture (2026-11-18)

Source test commit: 1b64a42d7bb279ce9a8f900db9ffb67649118395.
Branch: feature/texture-rhi-unification, PR #21 Draft.
File: tests/render/virtual_texture_file_streamer.cpp.

Expanded the existing virtual_texture_file_streamer CTest with
VirtualTextureFeedbackRuntime in a mock-RHI environment.
MockGpu::readTextureAsync stores a real deferred callback;
a synthetic RGBA8 target emits the same visible detail page
twice. Runtime capture refuses duplicates before completion,
then decodes the next-frame callback and submits only the
coarsest and intermediate physical disk pages within its
per-frame request limit. Consuming a decoded result twice
does not create duplicate disk requests. A second pending
callback is invoked after runtime.shutdown and must find
only an expired weak state, preventing use-after-free.
Device rejection is recoverable; a returned sRGB feedback
target fails closed; mismatched virtual layer configs are
rejected before an async request is sent. GPU resources and
the disk worker are released by the existing streamer.

Compatibility: No RHI backend or production header changes in
this test stage. The runtime facade remains Stage 7AC;
this fixture uses the existing CMake/CTest target.

Actual verification: GitHub test commit succeeded.
C++ compile NOT RUN, CTest NOT RUN, Metal/GL/Vulkan feedback
shader render and asynchronous readback NOT RUN.
No passing native execution claimed.

Next: Register the runtime usage/feedback format restrictions
in the native texture skill and canonical docs. On a native
checkout compile and execute the prefetch/feedback/file-streamer
tests, then validate the GLSL/MSL encoding in a real graphics
feedback pass and compare exact readback IDs.

## Stage 7AE — native feedback shader/readback usage and agent routing (2026-11-18)

Docs/skill commit: 052f5171aa5429c6acbaf9b6d1d64abebab767a6.
Branch: feature/texture-rhi-unification; Draft PR #21.
Paths: skills/visuals/aiverse-texture-rhi/SKILL.md,
docs/texture-rhi.md and AGENTS.md.

Documentation now points all native game agents to
VirtualTextureFeedbackRuntime and VirtualTextureFeedbackDecoder
for opt-in visibility feedback. The skill explains the 32-bit
page encoding, non-sRGB RGBA8 feedback target requirements,
the required unblended/untone-mapped shader pass,
Device::readTextureAsync then later submit(fileStreamer)
and normal fileStreamer.pump() on the owner thread.
Runtime shutdown uses weak callbacks and does not block the
GPU; no explicit camera/render pass/material-shader integration
is claimed. AGENTS.md keeps this presentation-only and
genre independent. Both docs and skill include
aiverse_virtual_texture_feedback_test and CTest
virtual_texture_feedback in focused native verification.

Compatibility: Documentation and agent guidance only.
No runtime C++, ECS, save/network, shader-graph or disk format
changed at this documentation stage.

Actual verification: GitHub doc commit succeeded.
Native compiler NOT RUN, CTest NOT RUN, shader compilation
and GPU readback tests NOT RUN. PR #21 remains Draft.

Next: Source-review and compile the newly introduced prefetch
and feedback CPU fixture targets on a complete native checkout,
fix errors, then validate an actual RGBA8 feedback pass on
Metal/GL33/GLES3/Vulkan and confirm integer-ID fidelity.
WebGL2 still needs a separate portable path.

## Stage 7AF — prevent resident coarse levels from starving fine mip prefetch (2026-11-18)

Code commit: c5de29e27ad178b10364ec323dde969bc495712c.
Branch: feature/texture-rhi-unification, Draft PR #21.
Changed file: engine/gfx/VirtualTexturePrefetch.h.

Bug: prior planner counted all coarse candidate keys against
maxCandidates before checking exact residency/pending status.
A large fully-resident ancestor range could fill the cap and
prevent the desired finer mip tiles from being inspected,
causing repeated no-progress prefetch frames.

Fix: the exact-resident/pending callback is now applied once
per unique key while enumerating tiles. Resident or pending
keys are tracked separately and do not consume the nonresident
candidate budget. Added maxExaminedPages (default 16384)
to cap total visited unique tiles/callbacks even if all
coarse keys are resident. Stats now includes examinedPages;
alreadyResident counts unique visited skipped tiles.
Existing priority, pin promotion, sorted emission and
maxRequestsPerFrame semantics remain unchanged.

Compatibility: additive config/stats field and internal CPU
planner behavior only. No RHI Device ABI, save/network, ECS,
GPU render targets, shader or asset format changes.

Actual verification: GitHub code update succeeded.
C++ build NOT RUN, CTest NOT RUN, GPU NOT RUN.
Next: Extend virtual_texture_prefetch CTest to verify a
maxCandidates=1 plan can still request the only missing
fine tile beneath two already resident coarse ancestors,
and verify maxExaminedPages caps fully-resident full-screen
coverage. Append separate test-source handoff.

## Stage 7AG — regression for absent fine tile beneath resident coarse mips (2026-11-18)

Code/test commit: 7bc72f9cfc685ab8aecde0e0722ab16c1b0404c7.
Branch: feature/texture-rhi-unification, PR #21 Draft.
File: tests/render/virtual_texture_prefetch.cpp.

New GPU-independent test residentCoarseDoesNotStarveFine:
a 4x4 tile grid with 3 mips and one nonresident-candidate
slot has its two coarse ancestors reported as already resident.
The planner must skip these ancestors during enumeration
and issue the one missing mip0 page (3,3), with exactly
three unique lookups, two skipped resident pages and no
candidate-cap blockage. Tightening maxExaminedPages to two
must stop before the fine page and set candidateLimitReached
without performing a third callback. A zero lookup budget
is rejected during planner initialization.

The existing CMake/CTest target virtual_texture_prefetch
is unchanged and now includes this regression source.
No runtime, RHI backend, ECS or disk format changes.

Actual verification: GitHub test-source commit succeeded.
Native C++ compilation NOT RUN, CTest NOT RUN, GPU NOT RUN.
Test asserts are source-level only, not passing evidence.

Next: Update native texture documentation with the separate
maxExaminedPages CPU safety bound. Then compile/run the
focused verifier on a full source checkout and correct
actual failures. Keep PR #21 Draft pending driver checks.

## Stage 7AH — document separate prefetch CPU lookup and candidate budgets (2026-11-18)

Documentation commit: 73981a02610e49b5edcad0eaa9d5e2f4baf9b84e.
Branch: feature/texture-rhi-unification, PR #21 Draft.
Files: skills/visuals/aiverse-texture-rhi/SKILL.md,
docs/texture-rhi.md.

Changes: The native visible-UV usage example configures
maxExaminedPages alongside maxCandidates. Architecture
documentation explicitly distinguishes a bounded number
of all unique page/residency lookups (including already
resident or queued mip ancestors) from the budget for
nonresident pages that still need uploads. This captures
the Stage 7AF no-starvation scheduling correction and
prevents agents from treating the two caps as equivalent.

Compatibility: Documentation-only; no production code, ECS,
serialization, disk layout or network change.

Actual verification: GitHub doc commit succeeded.
Native C++ compile, CTest, skill-catalog audit and GPU tests
NOT RUN. PR remains Draft.

Next: Native build/test all texture RHI sources and relevant
editor + real Metal/GL/Vulkan feedback shader readbacks.
Record exact tested HEAD and fix compiler/behavior errors.
Do not claim the source as production-ready until validation.

## Stage 9A — merge current main into Texture RHI branch (2026-11-18)

Merge commit: `2dc38b95d7525786c98b966113267047666a0290`.
Merge parents: Texture RHI prior HEAD `986310c60cadd9e1a6a74897a813c70c99408f46`
and main `9ee6ff6fbf390bd45363bc92c838f492dfe92eee`.
Working branch: `feature/texture-rhi-unification`, Draft PR #21.

Action: fetched GitHub's exact main and branch heads, compared the
common ancestor `dd711f5e2a7f66ed25bd7330875e6e87b7109e55`
to both branches and verified main introduces 48 changed paths,
only AGENTS.md and CMakeLists.txt overlapping Texture RHI changes.
Built a true, two-parent Git merge commit on GitHub with the latest
main blob SHAs/modes for the other 46 paths, without force-push.
AGENTS.md retains the Texture RHI agent contract and adds
main's InputFlow continuation policy, ownership table row and
Enhanced Input handoff section. CMakeLists.txt keeps all Texture
RHI CTest additions and adds main's AIVERSE::InputFlow links
for Android client, desktop client and editor.

Source changes: most main InputFlow/client/editor changes are
integrated without modifying them; no Texture RHI feature files
were overwritten. Main and feature commit ancestry are now
both represented as parents of the merge.

Actual verification: GitHub merge tree/commit/ref write succeeded
with expected head lease and without a force push. Full local
Git merge command NOT RUN (local container cannot resolve github.com).
Native CMake build NOT RUN; CTest NOT RUN; GPU tests NOT RUN;
input/texture joint feature execution NOT RUN. This stage must
not be mistaken for a successful build or resolved runtime conflict.

Next: verify compare main...feature reports zero commits behind,
then proceed with texture feature source work only after the merge.
Run full native compilation/tests and check InputFlow/Texture RHI
integration before marking the Draft PR ready for merge.

## Stage 7AI — owned linear RGBA8 virtual texture feedback pass (2026-11-18)

Source commit: 156c82a3db86bae26a645e9ca3634fbb5201ea5b.
Branch: feature/texture-rhi-unification, Draft PR #21.
New public header: engine/gfx/VirtualTextureFeedbackPass.h.
Main integration merge parent: 2dc38b95d7525786c98b966113267047666a0290.

API: VirtualTextureFeedbackPass::initialize(Device&,width,height,
VirtualTextureFeedbackConfig,VirtualTexturePrefetchConfig) allocates
a single-sample single-mip Texture2D RGBA8 UNORM offscreen color
target, and validates pixel budget/descriptor/backend capabilities.
begin() clears the buffer to zero (invalid feedback ID), opens
the standard RenderTargetSet render pass and configures a matching
viewport/scissor. The caller supplies a fragment shader writing
the Stage 7AA 32-bit packed virtual-page IDs, one opaque color
output and no sRGB/MSAA/blending/postprocess. endAndCapture()
closes the pass and requests the existing nonblocking
Device::readTextureAsync callback. submitVisiblePages(fileStreamer)
forwards decoded UV visibility through the pre-existing
feedback runtime and disk-backed prefetch adapter in a later
graphics frame. This helper never starts a graphics frame,
does disk I/O, or writes ECS/network/save state.

Lifetime: fully owns the temporary feedback render target;
shutdown invalidates runtime's weak callback state and frees
target via RenderTargetSet. It refuses to start a new capture
when the previous async request is pending or awaits consumption.
Device must outlive this owner. Unsupported RHI async readback
fails explicitly instead of implying a successful feedback pass.

Compatibility: additive, opt-in header-only C++ API. Existing
graphics render paths, shader graph and gameplay are unchanged.
Open issues: no renderer/camera invokes this helper automatically;
shader compilation/feedback render fidelity remain unverified.

Actual verification: GitHub file commit succeeded. Native C++
compile NOT RUN, CTest NOT RUN, real Metal/GL/Vulkan GPU NOT RUN.
Next: add deterministic mock-device tests for allocation rollback,
clear state, begin/end/capture ordering, async completion,
unsupported backend and weak callback destruction. Register
the test in CMake and focused Texture RHI verifier before
integrating in a live renderer.

## Stage 7AJ — standalone feedback render pass/mock-device test (2026-11-18)

Code/test commit: dce980b82c3eb08a1e401261bcfdad60c289e253.
Branch: feature/texture-rhi-unification, Draft PR #21.
Files: tests/render/virtual_texture_feedback_pass.cpp,
CMakeLists.txt, tools/verify-texture-rhi.sh.

Test source: aiverse_virtual_texture_feedback_pass_test /
CTest virtual_texture_feedback_pass uses a strict mock
gfx::Device. It checks a bounded, single-sample, single-color
linear RGBA8 texture is allocated with no extra attachments,
color pass clears all channels to zero, viewport/scissor
match the target, a nested begin is rejected, and a capture
can only start after endPass. A deferred callback completes
the packed RGBA8 page decoder and prevents another begin
until the result is consumed. Device rejection, invalid pixel
budget, incompatible feedback/prefetch configs and texture
allocation failure must fail without leaks or active passes.
A late callback after feedback pass shutdown holds only
the runtime's weak state and is ignored safely.

Registered the target in normal CMake and the separate
focused Texture RHI verifier, including optional --vulkan
filter. No actual native graphics driver is involved in
these mock tests; they do not validate shader pipeline
color formats, integer precision or GPU readback fidelity.

Compatibility: Test source and registration only; runtime
code remains Stage 7AI; no gameplay/ECS or save changes.

Actual verification: GitHub test/tree/ref commit succeeded.
CMake configure NOT RUN; C++ compile NOT RUN; CTest NOT RUN;
Metal, GL/GLES and Vulkan feedback rendering NOT RUN.
No passing tests claimed.

Next: Document the public VirtualTextureFeedbackPass API
and native renderer usage, then perform a real native build,
focused CTest and backend shader render/readback checks.
Do not merge PR #21 while those checks remain pending.

## Stage 7AK — native render feedback pass skill and architecture handoff (2026-11-18)

Docs/skill source commit: ff078bfc20e64514f78ea72b0bbfa90e9964e8c4.
Branch: feature/texture-rhi-unification, Draft PR #21.
Paths: skills/visuals/aiverse-texture-rhi/SKILL.md,
docs/texture-rhi.md, AGENTS.md.

Changes: Public API recipe for VirtualTextureFeedbackPass
owns a single RGBA8 UNORM offscreen color attachment,
clears invalid feedback pixels, then delegates rendering
to caller-owned opaque feedback shaders (one color output),
requests asynchronous texture readback at endAndCapture,
and uses submitVisiblePages plus fileStreamer.pump on
later frames. The native agent skill and AGENTS contract
direct game developers to use the shared, genre-independent
helper instead of creating one-off camera/terrain/RTS managers.
Document no depth/MSAA/sRGB/blending/dithering/postprocess
and the readback limitations; no actual camera draw, shader
compilation or RenderGraph registration is claimed.
Add aiverse_virtual_texture_feedback_pass_test and CTest
virtual_texture_feedback_pass to native verification instructions.

Compatibility: Documentation only. No graphics backend source,
save/network, ECS simulation or game genre code changed.

Actual verification: GitHub docs/tree/ref commit succeeded.
C++ build NOT RUN, CTest NOT RUN, GPU shader/render NOT RUN,
skill catalog audit NOT RUN. No CI status declared passed.

Next: Run source builds and CPU CTests with the merged main
InputFlow targets, then validate feedback drawing and packed ID
readback on Metal/GL33/GLES3/Vulkan. Repair actual failures
before marking PR ready and keep this append-only handoff up to date.

## Stage 8A — native libktx Basis/UASTC adapter (2026-11-18)

Source commit: 0533b36ee19729f38172ebb1357208aa673030b1.
Branch feature/texture-rhi-unification; Draft PR #21.
New header: engine/render/Ktx2Libktx.h.
Provides makeLibktxBasisTranscoder(Device&,options) and
uploadKtx2WithLibktx, using actual Khronos libktx decode API
when compiled with AIVERSE_TEXTURE_WITH_LIBKTX and linked.
Codec is NOT vendored; disabled mode fails closed with clear error.
Selects BC7 -> ASTC 4x4 -> ETC2 RGBA -> BC3 based on
Device::supports for correct sRGB/linear native formats,
or uncompressed RGBA32 (RGBA8 texels) fallback.
Uses libktx mip/layer/face offset functions to pack full
mip-major array/cube/volume output and bounded 512MiB memory.
Original KTX topology and decoded payload sizes are also
verified by existing parseKtx2WithTranscoder code.
Explicit current limitation: UASTC HDR targets requiring
RGBA16F/BC6H are not supported by this LDR output adapter.
No CMake option/linkage added yet in this stage; next stage
must add explicit opt-in dependency wiring and tests.
Compatibility: opt-in header only, no existing RHI/ABI, save
or ECS format changed.
Verification: GitHub source commit succeeded; libktx headers
not available locally, C++ compile NOT RUN, libktx runtime
test NOT RUN, GPU upload validation NOT RUN.
Next: CMake optional libktx target, smoke/negative fixtures,
agent skill and production native build with libktx installed.

## Stage 8B — opt-in system libktx CMake integration (2026-11-18)

Code commit: 4f54f932f933f36423f6def2d8ee7f2b62ac9d8d.
Files: CMakeLists.txt, tests/render/ktx2_transcode.cpp.
New CMake option AIVERSE_TEXTURE_WITH_LIBKTX=ON explicitly
requires installed ktx.h and libktx, wires both into
aiverse_runtime as public interface dependency and existing
aiverse_ktx2_transcode_test. OFF is default and has no
additional package dependency. Test source includes
Ktx2Libktx.h and checks default options; if enabled, checks
compiler sees the real ktxTexture2 API. Existing bridge
synthetic fixture is preserved.
Compatibility: opt-in CMake and test-only; OFF behavior unchanged.
Verification: GitHub tree/commit succeeded; CMake configure
NOT RUN (GitHub repository clone unavailable from this container),
C++ compile NOT RUN, real libktx runtime decode NOT RUN.
Next: Add real ETC1S/UASTC test assets (licensed), compile with
AIVERSE_TEXTURE_WITH_LIBKTX=ON and validate format selection
on BC7/ASTC/ETC2 and RGBA8 devices. Document dependency.

## Stage 8C — Vulkan compute shader SPIR-V compiler (2026-11-18)

Code commit 0bd7e79e0e62efd47c28710cfe225665869ce582.
Files: engine/gfx/vulkan/VkShaderCompiler.h and .cpp.
Adds compileComputeProgram(glslBody,spv,error), using
glslang::TShader(EShLangCompute) and TProgram link, with
Vulkan 1.1 SPIR-V 1.3 target and explicit GLSL450 input.
Automatically adds default workgroup local_size_x=64
when the source does not declare it. Source capped to 1MiB;
empty/invalid compilation fails closed with diagnostics.
Compatible binding convention documented in header:
storage images 0..3, sampled textures 4..11, storage
buffers 12..15, optional uniform buffer 16.
This stage only adds compiler source and API declaration;
native Vulkan pipeline/device support is the NEXT stage.
Verification: GitHub source/tree/ref commit succeeded.
glslang compiler NOT RUN, Vulkan graphics build NOT RUN.
No native or driver tests claimed passed.
Next: Add Vulkan compute resource/pipeline/descriptor/dispatch
backend, appropriate VkApi loader commands and storage-image
capability and memory barriers; document fallbacks.
## Stage 8D — Vulkan compute and storage allocation scaffolding (2026-11-18)

Code commit: 45056f91b0df88315afb79f19c4abf688ede038f.
Paths: VkApi.h, VkDevice.h, VkResources.cpp, GfxVulkan.cpp.
Added Vulkan compute entry points (vkCreateComputePipelines,
vkCmdDispatch), VulkanDevice public compute overrides and
VCompute resource pool, queue-family compute capability
selection, and cleanup for owned compute resources.
Storage texture allocation now conditionally enables
VK_IMAGE_USAGE_STORAGE_BIT, advertises storage in GPU
resource inventory, and checks RGBA8/RGBA16F 2D/2DArray/3D
against VkFormat STORAGE_IMAGE and SAMPLED_IMAGE features.
Reject unsupported storage usages rather than silent fallback.
Status: INCOMPLETE until next code stage implements all new
VulkanDevice compute method bodies in VkCompute.cpp and
adds that file to aiverse_gfx_vulkan source target. Do not
compile this intermediate HEAD; missing method definitions
would cause a linker failure. No change to GL/Metal behavior.
Verification: GitHub commit/ref update only; no native build
or GPU test RUN. Next: complete shader/pipeline/descriptors
dispatch, read/write layout barriers, ownership cleanup;
then compile and fix native tests.

## Stage 8E — Vulkan compute pipeline + synchronized storage images (2026-11-18)

Code commit: e47e11eeb8213204f0305bcbbc569c75bc87bc84.
Files: new engine/gfx/vulkan/VkCompute.cpp and CMakeLists.txt.
Adds real VulkanDevice::createCompute/destroyCompute/beginCompute/
setComputeBuffer/setComputeTexture/bindComputeStorageTexture/
setComputeUniforms/dispatch/endCompute definitions, completing
Stage 8D backend virtual methods. CMake compiles VkCompute.cpp.
Compiles GLSL compute via Stage 8C, creates VkComputePipeline
with descriptors set0 binding0..3 STORAGE_IMAGE, 4..11
COMBINED_IMAGE_SAMPLER, 12..15 STORAGE_BUFFER, 16 UBO.
Descriptor pools are bounded, one dispatch per pool and
deferred until the completed GPU frame serial. A 4KB max
uniform staging buffer is GPU-owned until completion.
dispatch uses 64 X threads per group, validates GPU group
counts, checks read/write alias, transitions storage images
SHADER_READ_ONLY->GENERAL and back with conservative
all-commands memory barriers. GPU queue must expose compute
and storage formats RGBA8/RGBA16F, 2D/array/3D.
Limitations: this is a constrained Vulkan shader resource
binding ABI, not automatic translation of legacy Metal
ComputeDesc.msl. GLSL source must use the documented
Vulkan binding layout; 4 writable image slots, 8 sampled
slots and 4 storage buffers. No transparent emulation in GL.
Compatibility: additive Vulkan backend; other RHI unchanged.
Verification: GitHub commit/ref succeeded. Native Vulkan SDK
compiler/glslang compile NOT RUN; real GPU descriptor validation
NOT RUN. Explicit PASS cannot be claimed.
Next: add Vulkan compute tests with GLSL shader writing
rgba8/rgba16f, readback exact pixel values and verify barriers.
Review compile + device limits before releasing.

## Stage 8F — optional external OpenEXR compressed HDR reader (2026-11-18)

Code commit: d2a17d7ebb41e08250d9b5ac6eb46cd03629f1e5.
New header: engine/render/ExrOpenExr.h.
Adds parseOpenExrWithLibrary and loadOpenExrWithLibrary.
With AIVERSE_TEXTURE_WITH_OPENEXR, uses real OpenEXR 3.x
RgbaInputFile on an immutable bounded memory IStream.
Reads original dataWindow and RGBA half pixels, supports
ZIP/ZIPS/PIZ and other formats OpenEXR library handles,
including first-level tiled sources where supported.
Converts to existing RadianceImage linear RGBA16F and
rejects invalid window sizes or nonfinite channel values.
Without dependency macro uses existing narrow uncompressed
parser, not a fake compressed decoder. No external codecs vendored.
Known: this is currently header-only; NEXT stage must add
optional OpenEXR 3 CMake dependency and a real compressed
EXR regression fixture. Multipart/deep EXR remain unsupported.
Compatibility: opt-in native reader; no renderer ABI or save
format change. Verification: GitHub source commit only,
native OpenEXR headers/compile/test NOT RUN.
Next: wire CMake, add synthetic OpenEXR-generated ZIP/PIZ
test, verify fixture and real OpenEXR native decoding.

## Stage 8G — OpenEXR 3 optional CMake and ZIP/ZIPS/PIZ fixtures (2026-11-18)

Code commit: 3ea20363b15909316fce92f7742c7e07418c8e43.
Files: CMakeLists.txt, engine/render/ExrOpenExr.h,
tests/render/exr_texture.cpp.
Adds AIVERSE_TEXTURE_WITH_OPENEXR=ON opt-in to find
OpenEXR 3 imported OpenEXR::OpenEXR, link runtime and
EXR fixture with public macro. OFF default stays dependency-free
and keeps the original narrow uncompressed parser.
Native test, only when enabled, uses installed library to
WRITE three small RGB[A] EXR files (ZIP/ZIPS/PIZ) and
decode them using parseOpenExrWithLibrary; asserts
linear HALF RGBA including negative blue and alpha.
Explicit includes stdexcept/cstdlib added to the external
header. No external library vendored into repo.
Limitations: tests are source fixtures, not executed.
Multipart/deep EXR still unsupported; uncompressed narrow
import remains the default without system library.
Compatibility: opt-in CMake only; no default build ABI changes.
Verification: GitHub code/tree/ref commit succeeded,
native CMake configure NOT RUN, OpenEXR test NOT RUN,
real image GPU sampling NOT RUN.
Next: run CMake/CTest with system OpenEXR 3 on developer machine,
check PNG/EXR color handling in final renderer and audit
library dependency packaging for app distribution.

## Stage 8H — 3D texture mip-slice GPU readback and inspector (2026-11-18)

Code commit: f694c31dad3f67dca7cb48a0841154c297616142.
Files: engine/gfx/{gl/GfxGL.cpp,metal/GfxMetal.mm,
vulkan/VkResources.cpp}, apps/editor/windows/TextureResourcesWindow.cpp.
Metal readTextureAsync now accepts Texture3D with each
mip depth slice selected via request.arrayLayer; 3D blits
use sourceSlice=0 and sourceOrigin.z. GLES3/GL33 attaches
the requested 3D slice with glFramebufferTextureLayer
to an intermediate PBO/fence readback framebuffer.
Vulkan VkBufferImageCopy now supports Texture3D with
imageSubresource baseArrayLayer 0 and imageOffset.z for
the requested mip-slice. Existing 2D/arrays/cubes unchanged.
Inspector adds per-mip depth slice slider and enables
bounded 16MiB async readback previews for 3D textures.
Textures with compressed/depth/MSAA/unsupported formats
still reject. 3D readback is presentation-only.
Compatibility: additive readback capability, no ECS,
asset format, shaders, serialization, net changes.
Verification: GitHub code/commit succeeded. Metal/GL/Vulkan
native build NOT RUN, readback-on-device NOT RUN, editor
preview UI NOT RUN, no real GPU confidence claimed.
Next: add readback per-backend fixtures and GPU integration
tests for z-slices/mip, verify row orientation and upload
format across platforms. Existing WebGL2 still refuses native
asynchronous readback.

## Stage 8I — focused Texture RHI GitHub Actions (2026-11-18)

CI source commit: 528263eb70544fceedf632599d8f1354bb0a53d7.
New file: .github/workflows/texture-rhi.yml.
Adds opt-in workflow_dispatch and path-filtered pull_request
workflow with concurrency cancellation; Linux CPU matrix
OpenEXR OFF/ON and separate Vulkan compile + lavapipe
test job, uploaded configure/build/CTest logs.
Runs the existing tools/verify-texture-rhi.sh with
the bounded native test list, -DAIVERSE_BUILD_CLIENT=OFF,
editor/server/tools/headless OFF, Vulkan ON in Vulkan job.
Vulkan success is explicitly NOT a real hardware PASS
unless CTest logs show actual driver execution.
Compatibility: CI-only YAML, no source/gameplay changes.
Actual verification: GitHub YAML commit succeeded; workflows
NOT RUN or confirmed successful yet; existing repository
CI mostly queued/cancelled at recent heads.
Next: inspect actions for this exact HEAD, download logs
when a job completes, fix observed native compile/test failures,
then evaluate real Metal/GLES3 GPU tests and reviewer gates.

## Stage 8J — bounded cross-frame deferred GPU feedback visibility (2026-11-18)

Code commit: a88ddd4bb5e74dade0cc90ebcfb3b50dd94e8fa9.
File: engine/gfx/VirtualTextureFeedbackRuntime.h.
After a successful asynchronous page-ID feedback capture,
submit(fileStreamer) now retains the corresponding generic UV
visibility regions across several graphics frames (default
maxDeferredFrames=8, allowed 1..1024). Subsequent calls issue
missing fine-detail pages once earlier coarse pages become
resident or pending. Fresh feedback replaces old visibility
rather than accumulating stale camera requests.
The coordinator releases regions once no candidate or
request-budget deferral remains, on retry-limit exhaustion,
or shutdown. No new GPU capture is required for every
deferred detail page. Device owner thread and existing
streamer/prefetch bounds are unchanged.
Compatibility: optional fourth initialize arg with a default;
existing callers compile without changes. No ECS, serialization,
network, shaders or RHI Device virtual API changes.
Verification: GitHub source commit succeeded. Native C++ build
NOT RUN; virtual_texture_file_streamer CTest NOT RUN; GPU NOT RUN.
KNOWN FOLLOW-UP: existing runtime integration test still assumes
submit() consumes all visibility in a single call. The next
stage MUST update that fixture to expect second-frame fine
mip submission and third-frame no-op, and consider retry cap.
Do not claim passing tests at this intermediate HEAD.

## Stage 8K — deferred feedback multi-frame native fixture correction (2026-11-18)

Code/test commit: b30eff59566988ba4596c401e8979b1e204ac980.
File: tests/render/virtual_texture_file_streamer.cpp.
Adjust the mock GPU/disk end-to-end fixture for Stage 8J.
First runtime.submit still queues coarse and middle mip
within maxRequestsPerFrame=2. Second submit on next frame
now queues exactly one fine mip without another GPU readback.
A third call must not issue duplicate requests. This tests
the intended retained bounded visibility and distinguishes
consuming the feedback result from completing the page work.
Compatibility: test-only; production source Stage 8J unchanged.
Verification: GitHub test commit succeeded, native compile
NOT RUN, CTest NOT RUN, real GPU feedback NOT RUN.
Next: test maxDeferredFrames edge and freshness of a replaced
view, then document the retry semantics. Native CI and GPU
tests remain mandatory before release.

## Stage 8L — Vulkan compute GLSL and real GPU storage-image fixture (2026-11-18)

Code/test commit: e38b4109f2899a64d3649afdeaa1500556065554.
Path: tests/render/vulkan_tests.cpp.
Extended the Vulkan test suite with real compileComputeProgram
valid/invalid GLSL checks (runs even when device is skipped).
When an offscreen Vulkan device and RGBA8 storage capability
are present, the GPU test creates a 4x4 storage texture,
compiles a compute pipeline using binding0 rgba8 writeonly
image2D, dispatches four rows of 64 X lanes with bounds
checks, then records asynchronous texture readback in the
same frame after the pipeline has completed. Subsequent
frames must receive a callback and exactly 64 RGBA8 bytes
matching (255,64,128,255) to one byte tolerance.
Explicitly rejects BC7 storage image capability.
The test always destroys its compute program and image.
Missing device/format prints a SKIPPED marker, not GPU PASS.
No engine runtime or public API changes in this stage.
Verification: GitHub source commit succeeded. glslang compilation,
CTest, Vulkan driver storage readback NOT RUN. No PASS claims.
Next: run `ctest -R vulkan_backend` with Lavapipe and a real
driver, inspect SKIPPED lines, fix any shader/pipeline errors.

## Stage 8M — native agent integration for optional codecs, Vulkan compute and 3D previews (2026-11-18)

Documentation source commit: 688bf554854770301c6d7be7d9ea07a348b72357.
Files: docs/texture-rhi.md, skills/visuals/aiverse-texture-rhi/SKILL.md, AGENTS.md.
Document system libktx and OpenEXR 3 opt-in build flags;
point agents at Ktx2Libktx.h and ExrOpenExr.h rather than
reimplementing decoders. Real LDR ETC1S/BasisLZ/UASTC
transcoder supports capability-selected BC7/ASTC/ETC2/BC3
or RGBA8 fallback. UASTC HDR remains separate/unimplemented.
OpenEXR external package decodes ZIP/ZIPS/PIZ and more
first-part RGBA scanline sources, not deep/multipart.
Describe Vulkan GLSL450 compute shader binding ABI:
set0 storage images 0..3, sampled images 4..11, storage
buffers 12..15, UBO 16, guarded RGBA8/16F storage.
Document limited GL/GLES/WebGL2 compute support, native
Texture3D mip-depth slice inspector readback, and
bounded cross-frame retry when GPU feedback exceeds quota.
Register new path-filtered Texture RHI CI workflow and
the difference between queued/skipped tests and GPU PASS.
No engine/runtime source or serialization changed at
this documentation-only stage.
Verification: GitHub docs commit succeeded. Skill catalog
audit NOT RUN; native build/CTest NOT RUN; Vulkan GPU/Metal/
GLES and OpenEXR/libktx runtime not run.
Next: review code for local/native compile failures,
consume CI logs once results are actually available;
fix issues with separate code and handoff commits.

## Stage 8N — optional UASTC_HDR -> RGBA16F full mip import (2026-11-18)

Source commit: 65f21b4a3dd7a7b48fb457e67207cd61db799e61.
File: engine/render/Ktx2Libktx.h.
Adds uploadKtx2UastcHdrWithLibktx(Device&,encoded,error),
separate from LDR CompressedImage / RGBA8 transcoding.
Enabled only with system Khronos libktx. Validates KTX2
header/mip index via existing inspect, verifies Basis HDR
and linear transfer metadata, then calls real libktx
TranscodeBasis(KTX_TTF_RGBA_HALF). It checks source
dimensions/layers/cube faces/mips after transcode.
Allocates RHI RGBA16F texture (2D/array/cube/3D as supported),
validates every libktx image offset and total 512MiB budget,
uploads every mip and face/depth slice using existing
Device::updateTextureRegion; destroys partially uploaded
GPU texture on any failure. No 8-bit downgrade or HDR
tone mapping. Without libktx macro it fails explicitly.
Unlike texture-pack writers it does not modify ECS/save data.
Known limitation: does not create native BC6H/ASTC HDR
compressed output; uses RGBA_HALF for compatibility.
Verification: GitHub code commit succeeded, libktx native
header/build NOT RUN, UASTC_HDR real file test NOT RUN,
RGBA16F Vulkan/Metal/GL GPU reads NOT RUN.
Next: dedicated HDR fixture with real UASTC_HDR encoded data
and GPU mock tests for mips, topology and unsupported codec;
update docs/AI skills for HDR API, and run libktx CI.

## Stage 8O — Vulkan compute async callback fixture lifetime fix (2026-11-18)

Test source commit: 27dada2769b9480377e431217b89c7e9b9604d14.
File: tests/render/vulkan_tests.cpp.
Earlier Stage 8L GPU readback test captured on-stack bool
and texture pixels by reference inside an asynchronous RHI
callback. When a GPU callback failed to arrive within the
bounded frame loop, test locals could leave scope before
the backend eventually delivered the callback (dangling
references, potential use-after-scope). Fixed by capturing
a shared_ptr<ReadbackState> by value; result remains owned
until the backend consumes or releases the callback.
No changes to GPU source or expected pixel assertions.
Verification: GitHub test commit succeeded; C++ compile
NOT RUN; Vulkan CTest/readback NOT RUN; no PASS claims.
Next: run Vulkan backend test with SDK and compute-capable
driver, inspect precise texture bytes and fix any failures.

## Stage 8P — UASTC_HDR decoder ABI and libktx dependency fixture (2026-11-18)

Test code commit: 4ea9117d44e981970295dc61a543ab9a7bbe2f55.
File: tests/render/ktx2_transcode.cpp.
Adds an actual typed function pointer instantiation for
uploadKtx2UastcHdrWithLibktx(Device&,bytes,error), ensuring
the newly added HDR entry point remains compilable from
a native consumer even when the codec macro is disabled.
When AIVERSE_TEXTURE_WITH_LIBKTX=ON the fixture checks
that the installed libktx development package exposes
ktxTexture2_TranscodeBasis, ktxTexture2_GetImageOffset and
ktxTexture2_IsHDR, in addition to the existing type check.
This test does NOT include production encoded UASTC_HDR
payloads; true half-texture content/GPU uploads still
need runtime test assets and a native library build.
Compatibility: source test-only; no renderer or save changes.
Verification: GitHub source commit succeeded; native
C++/libktx compile, CTest, HDR GPU output NOT RUN.
Next: check exact libc/SDK ABI on Linux/macOS/Windows
and add real UASTC_HDR texture fixture when a licensed
sample becomes available.

## Stage 8Q — UASTC_HDR agent guidance and limits (2026-11-18)

Documentation commit: e984b1b60773fb17d3aeb05e808accd700515814.
Files: docs/texture-rhi.md, skills/visuals/aiverse-texture-rhi/SKILL.md, AGENTS.md.
Record uploadKtx2UastcHdrWithLibktx as optional system
libktx KTX_TTF_RGBA_HALF path, with all mips, cube/array/
volume slices represented by linear RGBA16F RHI resources.
Warn that native BC6H/ASTC_HDR compressed targets remain
unsupported and that production HDR fixtures/real GPU tests
are not executed. Preserve default dependency-free import.
No runtime/ECS/network changes. Source docs GitHub commit
succeeded, native skill audit NOT RUN, GPU tests NOT RUN.
Next: run CI/test on native SDKs and verify half-pixel fidelity
and full mip topology. Keep PR #21 Draft until verified.


## Stage 8R — bounded feedback retry lifecycle fixture (2026-11-18)

Test source commit: 060cdb49cbf094cd943ab7e07f819a9412c11e05.
Changed file: tests/render/virtual_texture_file_streamer.cpp.
Extended asynchronousGpuFeedbackRuntime with a one-frame deferred budget
scenario, verifying that already pending pages remain deduplicated and
that subsequent submit calls do not issue new requests after expiration.
Also verifies initialize rejects maxDeferredFrames=0 and 1025, with
diagnostic errors. Existing normal 8-frame test still checks deferred
fine-mip request on the next submit and safe callback shutdown.

Scope: native mock-GPU CTest fixture only. No runtime, ECS, material,
serialization, or backend behavior changes.
Verification: GitHub commit succeeded. Native C++ build and CTest
NOT RUN because no local repository checkout and github.com DNS fails
in the available container. No GPU pass claimed.
Next: execute aiverse_virtual_texture_file_streamer_test and
ctest -R virtual_texture_file_streamer on a native checkout; fix
any compiler or assertion failures. Then add replacement-camera
visibility fixture and integrate GPU feedback capture into a real
renderer pass, validating Metal/GL/Vulkan separately.


## Stage 8S — retry fixture readback ownership correction (2026-11-18)

Test fix commit: 7f34beec5d9a05470eea1c6fa79ed5e6d6e9484b.
File: tests/render/virtual_texture_file_streamer.cpp.
The Stage 8R fixture reused a TextureReadbackResult after its pixel
vector had been moved into the stale-callback test. Replaced that reuse
with a newly initialized RGBA8 two-pixel feedback result encoding the
same fine-page ID, so the one-frame retry scenario is meaningful and
independent of moved-from vector state.

Verification: GitHub source commit succeeded. Native C++ compile,
CTest and GPU execution NOT RUN. This is not a passing-test claim.
Next: run the focused native test and then test replacement visibility
with a new camera region and distinct missing page keys; integrate
the feedback pass into an actual renderer only after validating its
render-thread and frame-order lifecycle.


## Stage 8T — camera visibility replacement regression (2026-11-18)

Test source commit: 1ce98a3823c28768a634294321a9d6691bb0565.
File: tests/render/virtual_texture_file_streamer.cpp.
Added cameraFeedbackReplacesDeferredVisibility with two distinct
one-pixel RGBA8 page IDs, emitted through MockGpu async completion.
The first camera view uses the two-request frame budget on coarse and
middle pages, leaving its fine page deferred. A second camera view is
captured and decoded before another submit; its dispatch must request
new visibility work without queuing the stale first camera's fine page.
The fixture also verifies owner shutdown releases GPU resources.
This tests the existing replacement behavior rather than adding
automatic camera capture or an engine render-pass hook.

Verification: GitHub commit succeeded; C++ compile, CTest and native
GPU integration NOT RUN. No visual or performance claim.
Next: execute focused native tests, add real renderer feedback draw
integration (opt-in, no genre dependencies), verify Metal/GL/Vulkan
render-to-readback byte fidelity and page-table updates, then assess
remaining backend capability and HDR import gaps.


## Stage 8U — opt-in feedback renderer callback integration (2026-11-18)

Source commit: 5dd9b58bf46ab249ea8a70f25f283b24d223d3a6.
Test commit: 2fdbdcb0989f52d690fad1456f8c1f9e9e9a63cc.
Files: engine/gfx/VirtualTextureFeedbackPass.h,
tests/render/virtual_texture_feedback_pass.cpp.
Added template renderAndCapture(DrawFeedback&&) to the existing
VirtualTextureFeedbackPass. The callback receives Device& inside
the bounded, cleared linear RGBA8 render target. On callback false,
the pass is ended without scheduling partial readback; on success,
endAndCapture submits asynchronous readback. Existing pending and
unconsumed feedback guards remain in effect. The mock-device fixture
tests callback order, success, false return, no partial capture and
rejection while prior visibility is awaiting consumption.

Usage: pass.renderAndCapture([&](gfx::Device& device) -> bool {
    // Bind a single-output RGBA8 opaque feedback pipeline and draw
    // packed page IDs from the active material/terrain visibility.
    return true; // only when all feedback draws were submitted
});
Later, call pass.submitVisiblePages(streamer) and streamer.pump().
No automatic camera/material/Terra integration is claimed.
Callback must not throw; the existing Device and pass contracts are
render-thread only. No ECS or genre-specific dependency introduced.

Verification: GitHub source/test commits succeeded. Native C++ build,
CTest, backend shader compilation and GPU readback NOT RUN.
Next: compile and run virtual_texture_feedback_pass and
virtual_texture_file_streamer; add real material/shader feedback
submission and per-backend GPU readback fidelity tests.


## Stage 8V — exception-safe feedback pass callback (2026-11-18)

Runtime source commit: c2e53674f9856576194f6fab1a13377c4ec53593.
Test source commit: c9692e050ccf98bde275dac2a17c84b5edc879e7.
Files: engine/gfx/VirtualTextureFeedbackPass.h and
tests/render/virtual_texture_feedback_pass.cpp.
renderAndCapture now wraps the caller-provided draw callback in
try/catch(...), ends the active RenderTargetSet pass on exception,
records a diagnostic and rethrows. This avoids leaving the Device
inside a render pass if the callback throws; false return still ends
without readback, successful return still schedules async capture.
The mock-device regression fixture exercises false, throwing and
successful callbacks in sequence, verifying end counts, no stale
readback and safe recovery.

Verification: GitHub commits succeeded. Native C++ compilation,
CTest and GPU execution NOT RUN; no passing tests claimed.
Compatibility: additive exception cleanup in the opt-in callback API;
no ECS, genre, serialization or native backend modifications.
Next: compile with the project's exception settings and run
virtual_texture_feedback_pass; connect a concrete shader/material
feedback draw and verify GPU pixel fidelity across Metal/GL/Vulkan.


## Stage 8W — material UV-to-page feedback shader helpers (2026-11-19)

Source commit: 17eff4e4d73a2e11776e07560da78ed27a949126.
Test source commit: 465a5c3c230d82481948dac6a9977d438fc96388.
Files: engine/gfx/VirtualTextureFeedback.h and
tests/render/virtual_texture_feedback.cpp.
Added CPU virtualTextureFeedbackPageAtUV(u,v,mip,layer,tilesX,tilesY)
and matching GLSL/MSL material helper snippets. These calculate
ceil-divided page counts at explicit mip, clamp UV=1 to the last
page, reject invalid UV/topology and emit the existing 32-bit
packed RGBA8 feedback ID. Material snippets depend on the existing
aiverseEncodeFeedback / aiverse_encode_feedback encoder functions:
include/concatenate encoder source BEFORE material helper source.
Shader consumers must supply an explicit mip and virtual layer,
use an unblended linear RGBA8 single-output target and no sRGB.
CPU fixture covers origin, UV upper edge, mip, non-power-of-two
tile counts, invalid arguments and packed ID validity.

This is a reusable shader building block, NOT a wired material
pipeline or a demonstrated GPU shader compile.
Verification: GitHub commits succeeded. C++ compilation, CTest,
GLSL/MSL shader validation and GPU execution NOT RUN.
Next: compile virtual_texture_feedback CTest, compile GLSL/MSL
helper snippets with backend shader toolchains, and wire a real
material/terrain feedback draw through VirtualTextureFeedbackPass
with correct explicit mip selection and camera-visible geometry.


## Stage 8X — reject nonfinite material feedback UV in GPU helpers (2026-11-19)

Runtime source commit: 9e93e6b9ae159754773e801acd29b2ca1c197947.
Test source commit: aecd1fa6e0621ac388d89d2f5de6994fa6f4602b.
Files: engine/gfx/VirtualTextureFeedback.h and
tests/render/virtual_texture_feedback.cpp.
GLSL material feedback helper now checks isnan/isinf on UV before
integer tile conversion; MSL checks all(isfinite(uv)). The existing
CPU equivalent already rejects nonfinite doubles; new fixture covers
NaN, positive and negative infinity. This prevents undefined or
implementation-specific float-to-int page selection for bad material
coordinates. All valid UV and packed-ID behavior remains unchanged.

Verification: GitHub commits succeeded. Native C++ CTest and
GLSL/MSL compilation/GPU execution NOT RUN. This is not a verified
backend fix until shader toolchains compile both snippets.
Next: run texture feedback CPU CTest, compile GLSL 330/ES 300 and
MSL material helpers with their encoder definitions, then integrate
a concrete renderer draw and compare GPU RGBA8 IDs to CPU results.


## Stage 8Y — embedded GLSL feedback material compilation fixture (2026-11-19)

Source commit: e77767b1e64aef3292dc2dbceae7c9c4aa05e916.
New file: tests/render/check_virtual_texture_feedback_shaders.py.
The standalone Python 3 verifier extracts the actual embedded
kVirtualTextureFeedbackGLSL encoder and
kVirtualTextureFeedbackMaterialGLSL source from
engine/gfx/VirtualTextureFeedback.h. It composes full fragment shaders
for GLSL 330 core and GLES 300 with uniform inputs and one RGBA8
output, and invokes glslangValidator -S frag. Any missing shader
symbol, compiler absence or compilation failure exits nonzero.
Run: python3 tests/render/check_virtual_texture_feedback_shaders.py.
This is an opt-in validation script, not yet registered in CTest;
it does not compile the MSL helper or run actual GPU readback.

Verification: GitHub file creation succeeded. The compiler is not
installed in the available execution environment; no GLSL PASS
or native CTest is claimed.
Next: run this verifier with glslangValidator and correct shader
syntax/precision issues; integrate with CMake/CTest on an environment
with a compiler; compile MSL on macOS and compare rendered feedback
RGBA8 bytes to the CPU page-ID encoder on real devices.


## Stage 8Z — conditional GLSL shader CTest registration (2026-11-19)

Build source commit: 1b44a6976e39889f1cf973c10bd97f1f97b6609e.
Changed file: CMakeLists.txt.
Within the existing texture test block, CMake now detects
Python3 Interpreter and glslangValidator and registers
virtual_texture_feedback_shaders only when both exist.
The test invokes the embedded-source GLSL 330/GLES 300 verifier
introduced in Stage 8Y. This avoids breaking toolchains without
glslangValidator, but missing registration is NOT a PASS:
CI must install the compiler and assert the test actually runs.

Verification: GitHub source commit succeeded. CMake configure,
CTest, GLSL compilation and GPU execution NOT RUN.
Next: run cmake configure with glslangValidator installed, check
ctest -N -R virtual_texture_feedback_shaders includes exactly
one test, execute it, then compile MSL with xcrun metal on macOS.


## Stage 9A — Metal feedback shader compile validation (2026-11-19)

Source commit: dec5d88d3380800232176f956a6dcb12e090e9ab.
CMake commit: 539d07fa08f58f47144b4273d5880b01d4720ff1.
Files: tests/render/check_virtual_texture_feedback_msl.py,
CMakeLists.txt.
New macOS Python verifier extracts the actual embedded
kVirtualTextureFeedbackMSL and kVirtualTextureFeedbackMaterialMSL
strings from VirtualTextureFeedback.h, composes a minimal Metal
fragment entry point, and invokes xcrun -sdk macosx metal -c
to emit AIR. Compiler errors and missing AIR fail the script.
CMake conditionally registers virtual_texture_feedback_msl when
APPLE, Python3 and xcrun are available. A missing test does not
count as a pass. No production renderer source changed.

Verification: GitHub source commits succeeded. macOS xcrun,
CMake configure, CTest, GPU readback and real shader execution
NOT RUN. MSL shader syntax is not yet certified.
Next: run ctest -N -R virtual_texture_feedback_msl and the
test itself on a macOS Metal SDK; fix any compiler diagnostics.
Then add a real Metal/GL/Vulkan offscreen feedback render fixture
and compare readback bytes with CPU encodeVirtualTextureFeedback.


## Stage 9B — focused verifier includes optional shader compilation (2026-11-19)

Source commit: e0dc6cdc978ff91044f96cdcfa84509b1199968f.
Changed file: tools/verify-texture-rhi.sh.
Expanded focused CTest regex to include
virtual_texture_feedback_shaders and virtual_texture_feedback_msl,
including the --vulkan path. The verification script records
registered shader tests with ctest -N and explicitly reports
glslangValidator/xcrun toolchain availability in summary.txt.
Absence of optional compiler is reported as NOT VERIFIED,
not misrepresented as a shader PASS. Existing native build and
CTest gates remain unchanged.

Verification: GitHub commit succeeded. Bash execution, CMake
configuration, CTest and GPU validation NOT RUN in this environment.
Next: execute bash tools/verify-texture-rhi.sh --editor on macOS
with Xcode Metal tools, and on Linux with glslangValidator;
inspect verify-logs/summary.txt and ctest.log. Fix actual
shader compiler errors before enabling renderer integration.


## Stage 9C — frame-level virtual texture streaming coordinator (2026-11-19)

Source commit: ecfd2bd13891d630741aa1aa5b989dbc109ed8d8.
Changed file: engine/gfx/VirtualTextureFeedbackPass.h.
Added VirtualTextureFeedbackPass::FrameResult and updateStreaming(
VirtualTextureFileStreamer&, size_t maxNewGpuPages=8,
const VirtualTextureFileStreamer::CompletionFn&={}).
The method is called on the RHI owner thread after beginFrame
delivers async feedback callbacks. It first submits decoded and
deferred visibility via the existing bounded prefetch planner,
then pumps the independently owned file streamer to stage physical
page uploads and publish page-table updates. FrameResult exposes
the dispatch, staged page count and whether streaming remains ready.
It does nothing if the pass is uninitialized, active, or the file
streamer is unavailable. The existing explicit submitVisiblePages
and streamer.pump APIs remain available.

Compatibility: additive native C++ convenience API; no ECS, genre,
renderer shader, asset format or backend ABI changes. No automatic
scene/material feedback drawing is implied.
Verification: GitHub source commit succeeded. Native compile,
CTest, GPU render/readback and editor integration NOT RUN, per
user request to prioritize functions before tests.
Next: wire feedback rendering into the generic material submission
path with correct camera-visible geometry and explicit mip selection;
provide a safe opt-in lifecycle in renderer/editor, then implement
remaining cross-backend RenderTexture/storage parity before native
test and validation phase.


## Stage 9D — resizable feedback target and configurable deferred budget (2026-11-19)

Source commit: 94c056b8ce1642d262f20e71899b98b3670e507f.
File: engine/gfx/VirtualTextureFeedbackPass.h.
Added optional maxDeferredFrames=8 to initialize, forwarding the
existing 1..1024 bounded retry budget to VirtualTextureFeedbackRuntime.
Added resize(width,height), retaining feedback/prefetch configs and
deferred-frame budget, rebuilding the linear RGBA8 render target and
decoder for viewport changes. Resize requires an initialized idle
pass; same-size resize is a no-op. Reallocation invalidates old
weak readback callback state; the Device still owns any outstanding
GPU staging. Failure leaves the object uninitialized rather than
publishing a partial replacement. shutdown clears retained config.
Existing initialize call sites remain source compatible.

Verification: GitHub source commit succeeded. Per user instruction,
native compile, CTest, GPU readback and visual tests NOT RUN.
Limitations: resize is a destructive reallocation, not an atomic
preserve-on-failure swap; renderer must recreate/retry on failure.
Next: integrate viewport resize and feedback drawing in a generic
renderer/material path, then close remaining backend RenderTexture
and storage image feature gaps. Defer tests until functional phase
is finished, but do not mark source as validated.


## Stage 9E — derivative-based feedback mip policy (2026-11-19)

Source commit: 5d95d281c06debd34fd609aa25f9df28e729b523.
File: engine/gfx/VirtualTextureFeedback.h.
Added optional kVirtualTextureFeedbackAutoMipGLSL and
kVirtualTextureFeedbackAutoMipMSL fragment-shader snippets.
aiverseFeedbackMipFromDerivatives / aiverse_feedback_mip_from_derivatives
accept UV, logical virtual texture texel extent, mipLevels and mipBias.
They use max squared UV derivative footprint, clamp negative LOD to
mip 0 and bound the result to mipLevels-1. Nonfinite UV/extent/bias
or derivative footprint and invalid mip counts return -1. The caller
passes the resulting mip into existing material feedback helper.
These are generic shader utilities: no FPS/RTS/Terra dependency and
no automatic pipeline/material binding has been added.

Important: call derivative function in uniform fragment flow, before
conditional discard/early returns; the texel extent is the logical
virtual texture size (not feedback render-target pixels). The
material helper and encoder must still be concatenated in the
correct order. Real GLSL/MSL compiler behavior remains unverified.
Verification: GitHub source commit succeeded; no CMake/CTest,
shader compiler, GPU or visual tests run, per user's functional-first
instruction.
Next: integrate automatic mip policy with generic renderer feedback
submission and material virtual-texture binding; then close backend
feature gaps before the dedicated validation phase.


## Stage 9F — complete feedback fragment shader source builders (2026-11-19)

Source commits: ba3ef59334d3bd0c76795446a8205277591a183b,
0558da0da78f45bb37d7d25587ab9d884a45b6f7.
File: engine/gfx/VirtualTextureFeedback.h.
Added virtualTextureFeedbackFragmentGLSL(bool automaticMip=true)
and virtualTextureFeedbackFragmentMSL(bool automaticMip=true).
They compose the existing packed page-ID encoder, UV-to-page
material helper, optional derivative-based mip selector and a
complete fragment entry point. GLSL declares vVirtualUV and
uniforms uVirtualBaseTiles/uVirtualLayer/uVirtualMip/
uVirtualMipLevels/uVirtualTexels/uVirtualMipBias. Metal declares
AiverseFeedbackParams in fragment buffer(0) and stage input UV
at user(locn0), with entry point aiverseVirtualFeedbackFragment.
The follow-up commit corrected source newline escaping.

Usage: supply a compatible vertex shader, shader version/header
as required by the selected RHI, set viewport/depth testing to
match visible geometry, disable blending, use a linear RGBA8
single-sample attachment and pass correct material/texture
geometry parameters. MSL parameter struct layout must match
Metal's ABI. Source builder is not yet integrated into the
main renderer or material system. Do not assume this is a
verified production shader until compiler and GPU validation.

Verification: GitHub source commits succeeded. Native compile,
shader compilation, CTest and GPU tests NOT RUN, per user
instruction to complete functionality first.
Next: connect source builders to generic material pipeline
creation and scene-visible draw submission; finish backend
storage texture and render target feature parity.


## Stage 9G — reusable feedback pipeline descriptor (2026-11-19)

Source commit: 4ee20aa106ed38a3341598683035c6e13b742c41.
Changed file: engine/gfx/VirtualTextureFeedbackPass.h.
Added virtualTextureFeedbackPipelineDesc(ShaderHandle,
bool depthAttachment=false), an additive native RHI helper
returning PipelineDesc for one RGBA8 UNORM attachment, one sample,
opaque blend, color writes enabled, and no depth-only mode.
The caller supplies shader, vertex layout and cull policy.
Depth testing/writes are disabled by default because the
current VirtualTextureFeedbackPass creates no depth attachment.
The optional depthAttachment flag is only valid if the caller
has actually configured a matching depth attachment.

Limitations: this is a pipeline description factory, not a
created pipeline, material registry, or renderer integration.
Without a depth attachment overlapping geometry feedback may
report occluded pages. Do not claim visibility-correct GPU
feedback until depth support is implemented and integrated.
Verification: GitHub source commit succeeded; native compilation,
CTest, shader compilation and GPU tests NOT RUN, per user request.
Next: implement optional depth attachment in feedback pass,
then integrate actual generic renderer draw submission and
per-material shader parameter binding; finish backend parity.


## Stage 9H — optional depth attachment for visibility-correct feedback (2026-11-19)

Source commit: a06dfa5e4771270b2d0fffc924feb7d8e8e69f39.
Changed file: engine/gfx/VirtualTextureFeedbackPass.h.
Extended initialize(...,size_t maxDeferredFrames=8,
bool depthAttachment=false) with optional depth support.
Depth-enabled passes require PixelFormat::Depth capability,
allocate depth via RenderTargetSet, and clear depth to 1.0
at every begin(). resize() retains the depth mode, shutdown()
clears it. hasDepthAttachment() and depthTexture() expose
read-only state/handle for renderers. Existing call sites remain
compatible with depth disabled by default. Pair with
virtualTextureFeedbackPipelineDesc(shader,true) and depth-tested
geometry submissions for camera-visible feedback.

Limitations: geometry and camera transforms still come from
the renderer. Depth correctness requires compatible projection,
culling, winding and draw order. Depth attachment is not an
automatic copy of the main scene depth buffer.
Verification: GitHub source commit succeeded. Per user's
functions-first instruction, CMake, CTest, shader compilation,
GPU rendering and readback NOT RUN.
Next: connect the depth-enabled pass to generic renderer
submission and material parameter binding, then close remaining
RenderTexture/storage image backend parity.


## Stage 9I — generic material/mesh feedback draw queue (2026-11-19)

Source commit: 792b6f14ad89c40e26ddc95738142333434e28da.
Changed file: engine/gfx/VirtualTextureFeedbackPass.h.
Added VirtualTextureFeedbackDraw (pipeline, vertex/index buffer,
counts, offsets, instance count and per-draw prepare(Device&)
callback) plus VirtualTextureFeedbackPass::renderDrawsAndCapture().
The helper validates draw handles/counts before opening the pass,
then uses renderAndCapture to bind pipeline and buffers, invoke
the caller's material/uniform preparation callback and issue
draw/drawIndexed. Any failed prepare callback ends the pass
without capture; thrown exceptions use existing exception-safe
renderAndCapture cleanup. This is genre-independent and can
accept terrain, static mesh or procedural geometry submissions.

Important: renderer still owns camera culling, material bindings,
vertex layouts, pipeline creation, depth compatibility and
visibility ordering. This is a native submission adapter, not
automatic interception of the engine's scene render queue.
Non-indexed instancing is intentionally rejected because Device
draw() exposes no instance count. Invalid draw commands are
rejected before rendering. Device draw methods return void,
so post-submit GPU failure cannot be detected here.
Verification: GitHub source commit succeeded. Native compile,
CTest, shader compilation, GPU readback and visual tests NOT RUN
per user request to finish functions first.
Next: integrate visible render queue construction into the
engine renderer and expose per-material feedback params; close
backend render-target/storage image feature gaps.


## Stage 9J — generic renderer visibility queue adapter (2026-11-19)

Source commit: 9a51b6ad86b82245acde20dab1da2c982302db03.
Changed file: engine/gfx/VirtualTextureFeedbackPass.h.
Added VirtualTextureFeedbackDrawSource abstract interface with
collectFeedbackDraws(vector<VirtualTextureFeedbackDraw>&), and
VirtualTextureFeedbackPass::renderVisibleAndCapture(source).
Collection happens before entering the GPU pass, after checking
that the pass is ready/idle and no feedback capture is pending
or awaiting consumption. A failed collection returns false
without beginning a pass; successful collection forwards the
draw queue to renderDrawsAndCapture. An empty visible scene
still produces a valid zero-cleared feedback capture.
The renderer is responsible for translating its already-culled
mesh/material queue into compatible feedback draw commands,
including pipeline, geometry, uniforms and depth policy.
No dependency on terrain, editor or genre-specific systems.

Scope caveat: this is the common renderer adapter API only;
no existing renderer has yet been wired to call it automatically.
Verification: GitHub source commit succeeded. Native compile,
CTest, shader compiler and GPU readback NOT RUN, as requested.
Next: identify and integrate the engine's actual shared
renderer visibility queue with this adapter, then implement
per-material virtual texture parameter binding and remaining
backend RenderTexture/storage parity.


## Stage 9K — Renderer3D opaque mesh queue bridge (2026-11-19)

Source commit: 0f1ed98c646f1068f4de83a21c1d34bcef7afc4d.
Changed file: engine/render/Renderer3D.h.
Added an additive Renderer3D::collectMeshFeedbackDraws(out,pipeline,
FeedbackMeshBinder) method. It walks the existing items_ immediate
mesh queue and emits VirtualTextureFeedbackDraw commands for
opaque valid indexed meshes, copying each model transform into
a per-draw prepare callback. The binder is responsible for
setting virtual texture UV mapping and material uniforms; it
receives the mesh and model matrix. No GPU work occurs during
collection. The caller can submit the resulting vector via
VirtualTextureFeedbackPass::renderDrawsAndCapture() while no
other pass is active.

Important limitations: this bridge currently excludes ModelItem
model/animation/LOD batches and customOpaque_ terrain renderers.
The built-in MeshVertex has no UV attribute; binders must use a
compatible feedback vertex shader and a real UV source (e.g.
procedural/world-space mapping) rather than assuming UV0 exists.
The bridge does not auto-run in Renderer3D::render(), since
that function owns scene/shadow passes and leaves the main pass
active for later drawing. Scene culling is not independently
recomputed by this bridge; items_ may contain offscreen meshes,
so the GPU rasterization clips them but CPU collection includes
them. Feedback is not yet complete for all scene content.

Verification: GitHub source commit succeeded. No native compile,
CTest, shader validation or GPU tests run, per user's instruction.
Next: add ModelItem and custom terrain feedback registration
with correct material UVs, integrate feedback scheduling before
the main pass, then finish backend parity and validate.


## Stage 9L — Renderer3D custom terrain/foliage feedback submission (2026-11-19)

Source commits: 0e2ee2f2145d47ab646d4505b82c58be108c6d2b,
e77768f53107b71bf6e04cdc52998246405776dc.
Changed files: engine/render/Renderer3D.h,
engine/render/Renderer3D.cpp.
Added Renderer3D::drawCustomFeedback(FeedbackCustomDraw)
to register per-scene terrain/foliage/procedural feedback
draw callbacks. Added renderVirtualTextureFeedback(pass,
meshPipeline,meshBinder), which collects opaque immediate mesh
feedback draws, then submits them and the registered custom
draws through a single renderAndCapture invocation. Custom
callbacks execute inside the feedback render pass and must
bind their own RGBA8 feedback pipeline, geometry, uniforms,
and matching depth state. beginScene clears customFeedback_
alongside other per-frame draw queues.

This is opt-in and NOT automatically called by Renderer3D::render:
the main pass remains active after render() so the host must
schedule feedback before it. ModelItem/animated models are not
covered. Terra is not yet wired to register callbacks. No
backend feature parity claims are made.
Verification: GitHub commits succeeded. Native build, CTest,
shader compilation and GPU tests NOT RUN, per user request.
Next: connect Terra's renderer to drawCustomFeedback, add
ModelItem feedback handling, and schedule feedback at the
appropriate point before Renderer3D main scene pass.


## Stage 9M — opt-in automatic feedback scheduling in Renderer3D (2026-11-19)

Source commits: 5c7da4b40798e1ab69fe82bfb72bd935c1f56519,
a4f759f4fb56e873faa3a0d8fb5c675a2e1ca0e5.
Files: engine/render/Renderer3D.h, engine/render/Renderer3D.cpp.
Added Renderer3D::setVirtualTextureFeedback(pass*,meshPipeline,
FeedbackMeshBinder) and clearVirtualTextureFeedback(). The
renderer stores a non-owning feedback pass pointer and optional
mesh material binder. When configured and feedback pass is
ready, with no pending/ready async readback, Renderer3D::render()
submits the opaque mesh and registered custom feedback callbacks
after shadow passes and before opening the main scene pass.
This preserves the existing contract where the main scene pass
stays open for subsequent UI rendering. Frame-level streamer
pump remains external; the application must invoke
VirtualTextureFeedbackPass::updateStreaming after readback
completion, or the pass will eventually stop capturing new data.

Lifecycle: call clearVirtualTextureFeedback() before destroying
the feedback pass or device. This is opt-in, not default.
ModelItem and automatic Terra producer registration remain
outstanding. The renderer's generic mesh binder must supply
compatible UV mapping, transforms and material parameters.
Verification: GitHub source commits succeeded; native build,
CTest, GLSL/MSL compiler and GPU execution NOT RUN by request.
Next: implement ModelItem feedback bridge, integrate Terra
feedback callbacks and finish backend parity before testing.


## Stage 9N — visible rigid ModelItem feedback (2026-11-19)

Source commit: 9a8676fe48f5528bd778cd06b40b422dd3bb93b9.
Changed file: engine/render/Renderer3D.h.
Added setVirtualTextureModelFeedback(pipeline,FeedbackModelBinder)
and clearVirtualTextureModelFeedback(). Binder receives
Device, Model, Primitive, Material and world matrix to bind
virtual UV/material parameters. When configured,
renderVirtualTextureFeedback() now submits in-view rigid
ModelItem primitives using the model's GPU vertex/index buffers
and each primitive's firstIndex/indexCount. This shares the
same optional depth-enabled pass as immediate meshes and
terrain custom feedback callbacks. Skinned/baked/morphed,
transparent/dissolving and shadow-only/suppressed roles are
excluded to avoid misleading undeformed feedback.

Important: no automatic UV binding, shader creation or
deformation feedback; the caller must provide a compatible
model vertex layout/pipeline. LOD transition semantics and
material alpha masking need further integration. This is a
partial ModelItem bridge, not complete model VT support.
Verification: GitHub commit succeeded. Native build, CTest,
shader compiler and GPU execution NOT RUN per user request.
Next: handle alpha masks, model LOD/animation parity and
Terra registration, then finish backend storage/RT parity.


## Stage 9O — conservative material and LOD feedback eligibility (2026-11-19)

Source commit: 43bc183fc0f6d7ef927ee6905c39c7a1708db581.
Changed file: engine/render/Renderer3D.h.
The rigid ModelItem feedback path now rejects AlphaMode::Mask
and AlphaMode::Blend, transparent flag, partially transparent
base color, and LOD cross-fade transition items
(lodThreshold<1), in addition to the previously excluded
skinned/morphed/baked/dissolving items. This prevents
reporting virtual pages from geometry that the simple opaque
feedback shader cannot correctly clip. clearVirtualTextureFeedback
also clears the separate model pipeline and binder.

These are conservative exclusions, not feature-complete
alpha-mask/LOD feedback. Such materials will have no feedback
until the renderer registers compatible alpha clipping and
LOD transition shader variants. No Terra integration yet.
Verification: GitHub source commit succeeded. No native
compile, CTest, shader validation or GPU run, per user's
functionality-first instruction.
Next: implement material-aware alpha-mask and LOD feedback
pipeline variants and Terra integration; close backend gaps.


## Stage 9P — explicit alpha-masked model feedback pipeline (2026-11-19)

Source commit: 0d2464cbf3adc0d66517e7f0acdff95ec79351c6.
Changed file: engine/render/Renderer3D.h.
Added setVirtualTextureMaskedModelFeedback(pipeline,binder) and
clearVirtualTextureMaskedModelFeedback(). The ModelItem feedback
loop selects opaque or masked pipeline and binder according to
Material::alpha. Masked items are skipped if no masked pipeline
and binder are installed; blended and flagged transparent
materials remain excluded. Masked binder MUST supply the same
alpha texture, alphaCutoff, UV transform, and discard behavior
as the main scene shader. clearVirtualTextureModelFeedback()
now clears the masked configuration as well.

This is a material-aware routing feature, not a new alpha
shader implementation or validated rendering parity. LOD
crossfades, skinned/baked/morph models and Terra auto-registration
remain outstanding. No changes to backend RHI.
Verification: GitHub commit succeeded. No native compilation,
CTest, GLSL/MSL compilation or GPU tests run, per user request.
Next: implement alpha-mask feedback shader variant, LOD fade
and deforming model support; wire Terra feedback producers.


## Stage 9Q — alpha-tested virtual feedback fragment shaders (2026-11-19)

Source commits: 12712e0d412b12750d19d7202a8ffb4ca44ceb10,
7127a18a6586fc7d29263269093124da772492bd.
File: engine/gfx/VirtualTextureFeedback.h.
Added virtualTextureMaskedFeedbackFragmentGLSL(bool automaticMip)
and virtualTextureMaskedFeedbackFragmentMSL(bool automaticMip).
Both derive from existing full fragment source builders, replace
the entry point, and sample base-color alpha with two affine UV
rows, an alpha factor and cutoff before writing packed feedback.
Automatic mip selection occurs before the alpha discard.
GLSL uniforms: uBaseColor, uAlphaUvRow0, uAlphaUvRow1,
uBaseAlphaFactor, uAlphaCutoff; MSL uses baseColor texture(0),
baseSampler sampler(0), AiverseAlphaFeedbackParams buffer(1).
The second commit fixed source newline escaping.

Important: alpha UV transform, texture sampling state, material
alpha factor and cutoff must match the main material shader.
GLSL and MSL fragment sources are not yet integrated into
actual pipeline creation or verified against shader compilers.
Some material paths may use additional alpha semantics (vertex
colors, custom graph, dissolve); these are not covered.
Verification: GitHub commits succeeded. Native build, CTest,
GLSL/MSL compilation and GPU readback NOT RUN, as requested.
Next: bind masked shaders in model material renderer, support
LOD transition and deformed geometry, wire Terra and finish
backend feature parity.


## Stage 9R — LOD-crossfade-aware model feedback routing (2026-11-19)

Source commit: da18b554d14ea581cfc908cdba5aab411345eb87.
Changed file: engine/render/Renderer3D.h.
Added setVirtualTextureLodModelFeedback(pipeline,FeedbackLodBinder)
and clearVirtualTextureLodModelFeedback(). Callback receives
Model/Primitive/Material/world matrix plus lodThreshold and
lodInvert to reproduce the main renderer's per-fragment LOD
crossfade. The ModelItem feedback path selects this pipeline
for fading rigid geometry; when no compatible pipeline is
installed, fading geometry is skipped. Masked+fading geometry
is conservatively skipped until a combined discard shader is
available. Clearing model feedback also clears LOD variant.
This avoids using opaque feedback for partial-coverage LODs.

Limitations: this adds routing and parameters, not a compiled
LOD-dither shader. Exact coverage parity, alpha+LOD combined
path, animated/morph models, Terra registration and backend
parity remain outstanding. The caller must bind all material
and camera uniforms.
Verification: GitHub commit succeeded. Native compile,
CTest, GLSL/MSL shader compilation and GPU tests NOT RUN
per user's functions-first instruction.
Next: implement LOD-dither feedback fragment shader matching
the main renderer, combined masked+LOD variant, then address
deformed models and Terra integration.


## Stage 9S — LOD ordered-dither feedback fragment shader (2026-11-19)

Source commit: d253390bb6955d596d285e68376146b45e81e656.
File: engine/gfx/VirtualTextureFeedback.h.
Added virtualTextureLodFeedbackFragmentGLSL/MSL(bool automaticMip)
which compose the existing material feedback shader and insert
the 4x4 ordered coverage pattern used by Renderer3D's
ModelShaders.h LODFADE. Both support threshold and invert
selection, optional derivative-based mip and packed page ID
output. GLSL uniforms: uLodThreshold/uLodInvert. MSL binds
AiverseLodFeedbackParams at buffer(1), threshold and uint invert.
The exact coverage parity requires matching viewport pixel
coordinates; a downsampled feedback target will not preserve
the original main-pass dither coverage unless coordinates
are explicitly mapped. Masked+LOD combined variant is still
not implemented, nor are skinned/morph geometry or Terra
material bindings.

Verification: GitHub source commit succeeded. Native build,
GLSL/MSL shader compilation, CTest and GPU rendering NOT RUN
per user's functional-first request.
Next: implement combined masked+LOD feedback shader and
parameter bindings; account for main viewport-to-feedback
pixel coordinates, then finish deformed model and Terra paths.


## Stage 9T — combined masked + LOD feedback variant (2026-11-19)

Source commits: 5e32f8cb627ba5fbdac6f756294994c52dbcd226,
b93df1739d39dfde7a6ff5ba58ebd1db76137756.
Changed files: engine/render/Renderer3D.h,
engine/gfx/VirtualTextureFeedback.h.
Added setVirtualTextureMaskedLodModelFeedback(pipeline,
FeedbackLodBinder) and matching clear method. Renderer3D now
routes rigid masked+fading ModelItem primitives through a
dedicated combined pipeline rather than unconditionally
skipping them. The binder receives material/world and LOD
threshold/inversion. Added GLSL/MSL combined fragment shader
builders that perform base alpha discard and Renderer3D's
4x4 screen-space ordered LOD discard before writing packed
virtual page feedback. Automatic mip selection is computed
before discards. The Metal combined variant binds alpha params
at buffer(1) and LOD params at buffer(2).

Important: the caller still creates pipelines and binds the
same base texture, sampler, UV transform and alpha cutoff as
the main pass. Downsampled feedback coordinates may not match
main-pass LOD dithering. Animated/morph/skinned model support,
Terra registration and backend parity remain outstanding.
Verification: GitHub commits succeeded. Native compilation,
CTest, shader compiler and GPU tests NOT RUN per user request.
Next: implement deforming-model feedback parity, then wire
Terra's material producer and close RHI backend gaps.


## Stage 9U — deformed ModelItem feedback draw integration point (2026-11-19)

Source commit: 8bab34e667ca3b2cef3581c422c8492eb70478fd.
Changed file: engine/render/Renderer3D.h.
Added FeedbackDeformedModel carrying Model, Primitive, Material,
world transform, joint and morph offsets, baked animation flag,
animation parameters, LOD threshold/invert, index range, and
read-only references to Renderer3D's per-frame jointData_ and
morphData_. Added setVirtualTextureDeformedModelFeedback(
FeedbackDeformedModelDraw) and matching clear. ModelItem
feedback dispatch now delegates nonrigid/skinned/baked/morphed
items to this callback when registered. The callback executes
inside the existing feedback render pass and is responsible
for setting its own deformation-aware pipeline, textures,
uniforms, vertex/index buffers, material alpha/LOD discard,
and issuing indexed draws. Without a callback, deformed
items remain skipped. Clear model feedback also clears the
deformed callback. This interface does NOT itself implement
GPU skinning or morph feedback shaders.

Verification: GitHub commit succeeded. Native compile,
CTest, shader compilation and GPU tests NOT RUN as requested.
Next: implement concrete skinning/baked/morph feedback vertex
shader variants with matching ModelShaders ABI, wire Terra
producers, then complete backend parity and test.


## Stage 9V — feedback-to-scene coordinate mapping for LOD coverage (2026-11-19)

Source commit: ca4e6133546069ddbfa1887cb327e787f44cfead.
File: engine/gfx/VirtualTextureFeedback.h.
Both LOD-only and combined alpha+LOD GLSL/MSL shader builders
now map fragment pixel positions from the feedback viewport
into the main scene's pixel grid before evaluating the same
4x4 ordered LOD dither rank. GLSL adds uniforms
uFeedbackToSceneScale and uFeedbackToSceneOffset. MSL extends
AiverseLodFeedbackParams with float2 sceneScale/sceneOffset.
The caller must supply scale and offset from its actual scene
viewport (identity scale and zero offset for equal viewports).
This allows consistent screen-grid mapping with downsampled
feedback, but downsampling still cannot perfectly reconstruct
every high-resolution coverage sample. No backend tests run.

Verification: GitHub source commit succeeded. Native build,
CTest, shader compiler and GPU tests NOT RUN per user request.
Next: wire viewport mapping values in material binders; implement
concrete deformed-model feedback GPU shaders, Terra producers,
and complete backend feature parity.


## Stage 9W — GPU pooled indices, pixel-grid binding, model-only/terrain-only scheduling (2026-11-19)

Source commits: fe20912660008151c9d42eb6bf8c2ed2d16f9361,
cd146e893650067a411288248aa2a3c1f97bbfc0.
Files: engine/render/Renderer3D.h, engine/render/Renderer3D.cpp.
Fixed regular rigid ModelItem feedback drawing to pass
`firstIndex + model->gpuFirstIndex` and
`int32_t(model->gpuBaseVertex)` to Device::drawIndexed(),
matching Renderer3DModels.cpp's actual GPU geometry pool path.
Expanded FeedbackDeformedModel with gpuFirstIndex/gpuBaseVertex
so callbacks can issue an equivalent indexed draw.
Added Renderer3D::FeedbackPixelGrid (scaleX/Y, offsetX/Y)
and feedbackPixelGrid(pass), derived from sceneW_/sceneH_
and pass target dimensions; offsets use 0.5*(1-scale) to
map feedback fragment centers onto scene fragment centers.
FeedbackLodBinder now receives the computed grid as an extra
final argument; FeedbackDeformedModel includes this grid.
Callbacks must pass the grid into the LOD shader's uniforms
uFeedbackToSceneScale/Offset or the matching MSL struct.

The automatic Renderer3D::render() feedback scheduling no
longer wrongly depends on a configured immediate-mesh
pipeline: model-only, deformed-model-only, and custom terrain
feedback producer configurations are supported. The mesh
collection helper treats no mesh pipeline/binder as opt-out.
Only an initialized, idle feedback pass is used.

API migration: FeedbackLodBinder's signature changed from
(...,float threshold,bool invert) to (...,float threshold,
bool invert,const FeedbackPixelGrid&); downstream custom
callbacks must update. No automatic streamer ownership.
Verification: source committed to branch through GitHub;
NO native compilation, CTest, GLSL/MSL validation or GPU test
run (user requested functions first, tests later).
Next: provide RHI-compatible GPU shader program creation
with real U0 std140/MSL buffer(1) and correct vertex semantics;
connect Terra producers and deformed model shader paths.


## Stage 9X — RHI-native position/UV feedback shaders and pipeline (2026-11-19)

Source commit: dbf5aa99e74d5532abdd35f49303e60c19678036.
New file: engine/gfx/VirtualTextureFeedbackProgram.h.
Added a standalone, cross-backend native C++ builder
`virtualTextureFeedbackProgramSources(alphaMask,lodFade,automaticMip)`
which emits complete GLSL vertex/fragment sources and a combined
MSL source with VS `vs_main` and FS `fs_main`. This resolves
the incompatibility of the earlier standalone fragment snippets
with the RHI's shader-stage, U0 uniform block and Metal entrypoint
contract. GLSL uses U0 std140, sampler `tex0` for masked variants;
MSL uses constant buffer(1), texture(0) and sampler(0).

Added `VirtualTextureFeedbackUniforms` (176-byte U0),
`VirtualTextureFeedbackVertexLayout`, and
`VirtualTextureFeedbackProgram` with synchronous
initialize()/bind()/shutdown(), RGBA8 opaque single-sample
pipeline, optional depth, position attribute 0 and UV attribute 2.
ModelVertex maps to stride 60, offsets 0/24, UInt32 indices.
Optional alpha masking and 4x4 LOD dither variants share the
existing page encoding and derivative mip policy. All parameter
blocks have explicit vector packing for std140/Metal parity.
The program is genre-agnostic and can also be instantiated for
terrain vertices with a different stride/UV offset.

Important: callers MUST compute clipFromLocal (viewProj*world),
virtual texture tile counts, logical texels/mips, alpha UV rows,
and source baseColor alpha factor. For LOD fade they MUST fill
scenePixel with Renderer3D::feedbackPixelGrid() values. Masked
variants require a valid source base-color TextureHandle and
matching sampler. This is a native shader/pipeline path but
not yet a full plug-and-play material registration in
Renderer3D/Terra. Non-position+UV deformation remains delegated
to the separate deformed-model feedback callback.

Verification: source committed to GitHub; NO native compilation,
GLSL/MSL shader validation, CTest or GPU runs (user wants
features first). Shader ABI and backend compilation require
later verification.
Next: wire the new program into model material binding and
Terra's render submissions; concrete skinned/baked/morph
vertex variants must follow ModelShaders.h ABI.


## Stage 9Y — Renderer3D rigid model material program integration (2026-11-19)

Code commits: 41a805dc266493d50a3c9bf4456b3f74f49698d2,
3769b46423347112730678fc1e47dad8eddcc970.
Documentation commit: 9602f747e64025188dd142e900068d376466147f.
Changed/new: engine/render/VirtualTextureModelFeedback.h
and docs/texture-rhi.md.
Added render::VirtualTextureModelFeedback, a reusable,
genre-independent bridge that creates eight pipeline variants
(alpha mask x LOD fade x double-sided), connects existing
Renderer3D model/masked/LOD callbacks, builds U0 feedback
uniforms from Renderer3D::viewProj()*world, reproduces the
exact base material affine KHR UV rotation and alpha cutoff,
sets ordered-dither scene-pixel mapping, selects material
base image and emits packed page requests through the
RHI-native program introduced in stage 9X.
Its resolver maps Model/Material to virtual base tile counts,
layer, texture extent, mip policy and optional virtual UV
scale/offset. Non-VT materials emit zero feedback IDs but
still write feedback depth so they can occlude VT surfaces.
The adapter owns a 1x1 RGBA8 white fallback for alpha-masked
materials lacking an uploaded base color image. Explicit
initialize(), connect(), disconnect(), shutdown() lifecycle;
the caller separately installs the feedback pass into
Renderer3D with setVirtualTextureFeedback(&pass,{},{})
and pumps streaming via VirtualTextureFeedbackPass::updateStreaming.

Limitations and caveats: the adapter presently handles only
rigid position/UV ModelVertex. Deformed models still require
the separate callback and correct GPU deformation pipeline.
Terra producer registration is NOT performed here. The
adapter's disconnect calls clearVirtualTextureModelFeedback,
which currently also clears deformed callback state; owners
must coordinate lifecycle. Resolver callbacks must not refer
to stale model/material resources. Depth/alpha/LOD behavior
requires visual and GPU validation, especially different
viewport sizes and material graph overrides.
Verification: GitHub source and documentation commits
succeeded; native C++ build, CTest, GLSL/MSL compilation
and GPU execution NOT RUN as requested.
Next: implement concrete feedback deformation for
skinned/baked/morphed models, coordinate with Terra agent to
hook custom feedback producers, and complete RHI backend
compatibility checks before the test pass.


## Stage 9Z — actual GPU-deformed model feedback (2026-11-19)

Source commit: 627181287110429ea8add50a1357d47736bb7e34.
Documentation commit: d031f53ba4cee0aa50ff8532463b64de388bb669.
Files: engine/render/VirtualTextureDeformedFeedback.h (new),
docs/texture-rhi.md (appended).

Added a concrete `render::VirtualTextureDeformedModelFeedback`
GPU adapter that registers with Renderer3D's
`setVirtualTextureDeformedModelFeedback()` callback and submits
deformed geometry through the existing VT feedback target.
Unlike the prior stage's callback-only skeleton, the new
`virtualTextureDeformedFeedbackSources()` builds complete RHI
GLSL/MSL shaders by reusing the **same deformation functions**
already authored in `ModelShaders.h` (`kModelShadowVS` and
`kModelShadowMSL`, with their light VP changed to camera VP).
The feedback fragment uses the existing packed page codec,
automatic derivative mip, material alpha discard, and scene
pixel-grid ordered LOD discard. U0/U1/U2/U3/U4 and tex1/7/8
keep the main model shader's semantics. U6 is a dedicated
five-vec4 VT page/policy/viewport uniform block.

The adapter pre-creates six deformation shader variants:
rigid-dynamic, skinned, baked, morph, skinned+morph and
baked+morph; each has front-culling enabled/disabled variants
for normal and double-sided materials. On draw it binds
the selected pipeline, main frame U1 via `bindLitFrame()`,
U0 per-instance transform+animation, U2 material base/UV,
U6 virtual mapping and LOD policy; conditionally uploads
U3 bone palette or baked joint index palette, U4 morph info,
tex7 baked joint matrices and tex8 morph deltas; it binds
tex1 for material alpha and emits the correct firstIndex+
gpuFirstIndex / gpuBaseVertex draw. A per-instance resolver
maps source model/material onto virtual pages. Non-VT
materials emit zero page IDs while writing depth. Incomplete
animation resources are conservatively skipped instead of
drawing undeformed geometry.

API: `VirtualTextureDeformedModelFeedback::initialize(dev,depth)`,
`connect(renderer,VirtualTextureMaterialResolver)`,
`disconnect()`, `shutdown()`. Usage is recorded in
docs/texture-rhi.md. Install this alongside the rigid
`VirtualTextureModelFeedback` adapter; activate the feedback
pass via `Renderer3D::setVirtualTextureFeedback(&pass,{},{})`.
Disconnect before renderer/device destruction; note the
rigid adapter's clear method also clears deformed callback,
so coordinate the teardown order.

Limitations: not wired to any user project automatically;
material-graph vertex displacement and extra shader-specific
alpha semantics are not reproduced. No Terra producer yet.
Deformation parity across Metal, GL33/GLES3 and Vulkan is not
verified; the shader source reuse is intended to reduce this
risk, not proof of parity. Multi-backend compilation and
GPU feedback visibility tests remain for the test stage.

Verification actually performed: fetched and inspected
`ModelShaders.h`, `Renderer3DModels.cpp`,
`Renderer3D::bindLitFrame()` and the RHI shader contract;
GitHub source commit and docs commit succeeded.
Native compilation NOT RUN; CTest NOT RUN; GPU rendering
and GLSL/MSL validation NOT RUN, per user's functions-first
request.

Next: integrate position/UV feedback producer with the
real Terra/terrain renderer without duplicating genre-specific
code (be aware Terra is owned by a different agent),
then address RHI shader/source compatibility and eventual
GPU tests. Review model vertex-displacement graph parity
before claiming full animated-model equivalence.


## Stage 10A — TerraWeave ModelDraw VT feedback bridge (2026-11-19)

**Branch:** `feature/texture-rhi-unification` (PR #21).
**Source commits:** `2252659bc83cdd31fdb33a87e709810a1fe65a7f`,
`c92c6110afba66a1d6aea9e9d74ecec93e3cd070`.
**Docs commit:** `705ffbc2421a93f9fa6ef5271f6f87310cc61aa3`.
**New file:** `engine/render/TerraWeaveVirtualTextureFeedback.h`.
**Updated file:** `docs/texture-rhi.md`.

Inspected Terra's **live separate branch** `feature/terraweave`
using GitHub. Verified from its actual source that
`engine/render/TerraWeaveForestAdapter.h` sends Arbor/Flora
models and per-azimuth impostor `ModelDraw` objects through
`Renderer3D::drawModel`; `TerraWeaveHorizonScene.h` likewise
uses `Renderer3D::drawModel` for near/far Horizon terrain
and distant forest clusters. Terra has no divergent GPU
renderer that needs its own VT raster pass. The Terra branch's
`engine/world/TerraWeave*.h` files are not present on the
Texture RHI feature branch; no merge or cherry-pick was
attempted because Terra is being developed by another agent.

Added `render::TerraWeaveVirtualTextureFeedback`, an
opt-in adapter that initializes the shared rigid
`VirtualTextureModelFeedback` (optionally also skinned/baked/
morph `VirtualTextureDeformedModelFeedback`), installs
`Renderer3D::setVirtualTextureFeedback(&pass,{},{})`,
and resolves per-material VT page metadata on the **same
model draws** already queued by Terra's forest and Horizon
renderers. No additional Terra-specific shader or second
draw queue exists. Supported registration:
`beginFrame()`, `registerMaterial(model,material,mapping)`,
`registerModel(model,mapper)`,
`registerDraw(ModelDraw,mapper)`, and
`setLiveMaterialMapper(mapper)`; the latter enables private
and replaced `TerraWeaveHorizonScene` region models to
resolve their allocations while the normal renderer submits
them. Model/material pointers serve as per-frame keys;
`beginFrame` drops stale keys across region swaps, forest
unload and hot reload. `registerDraw` includes per-azimuth
impostor overrides. Material mappings are validated for
tile/mip/layer bounds and finite texture dimensions/UVs.
`shutdown()` disconnects feedback and destroys only bridge-
owned shader/fallback resources; it does NOT own
`VirtualTextureFeedbackPass`, `VirtualTextureFileStreamer`,
Terra model assets or page archive storage.

No actual virtual page archive is generated from Terra
textures by this feature stage. A material with no allocated
virtual pages writes zero feedback while preserving feedback
depth. The host must supply a live allocation lookup (on the
render thread, no blocking or allocations) and pump
`feedbackPass.updateStreaming(streamer,budget)`. Atlas
revisions must be differentiated by the actual Model/Material
identity, **not** by the generic path
`terraweave/horizon` alone. Transparent Horizon region
crossfade draws are currently conservatively skipped by
the generic opaque feedback loop; this may underrequest
during transitions and requires a separate visibility policy.

**Verification actually performed:** inspected the Terra
branch's `ArborForestAdapter::submit/forEachDraw`,
`TerraWeaveHorizonScene::submit` and generated model/material
paths/alpha settings; GitHub source commits and docs commit
succeeded. Native compilation NOT RUN, CTest NOT RUN,
shader compilation NOT RUN, GPU/streaming execution NOT RUN
per user's functionality-first instruction. This is an
implemented integration seam but *not* evidence of tested
runtime Terra VT rendering.

**Next steps:** coordinate with Terra agent after merging
`feature/terraweave` and Texture RHI branch: register an
actual VT allocation provider for Arbor, impostor atlases
and Horizon texture archive/revisions, connect it during
Terra scene setup, and validate region swaps. Support
Terra materials' source texture atlas unpack/packing into
real VT pages before claiming a full streaming implementation.
Then finish shader/backend parity and run native/CTest/GPU
tests on Metal, GL33/GLES3 and Vulkan.


## Stage 10B — runtime versioned RGBA8 page archives (2026-11-19)

**Source commits:** `dceac4832ef756e7a4ac1475e9cd37635f3366d0`,
`002896341720570c063fd3ae4a334c557e466062`.
**New:** `engine/gfx/VirtualTexturePageArchiveWriter.h`.
**Branch:** `feature/texture-rhi-unification`, PR #21.

Implemented genre-independent native C++ `gfx::VirtualTexturePageArchiveWriter::pack()`
that converts one or more immutable row-major CPU RGBA8 images to
physical VT pages with clamped edge gutters, consistent mip generation
(deterministic 2x2 RGBA8 box filter), and exactly the disk layout
`VirtualTextureDiskQueue` reads:
`m<MIP>/l<LAYER>/x<X>_y<Y>.rgba`.
This is the same asset layout as `tools/virtual_texture_pack.py`.
Configurable pageSize/gutter, CPU source and on-disk size budgets, tile
and layer limits follow the 32-bit feedback codec (<=2048 tiles per axis,
<=16 layers and <=30 mip levels).
A complete directory is built in a sibling staging directory, with
`manifest.json` documenting dimensions, tile counts, format and
contentHashFNV64. A same-filesystem directory rename publishes the
new revision `v<revision>-<hash>` atomically; previous versions
are untouched. Repeating an identical version reuses its manifest,
while immutable source hashes separate changed Horizon atlases.
Failure cleans the staging directory and returns an error.

`VirtualTextureArchiveVersion` exposes the resulting immutable asset
directory and directly creates matching `VirtualDiskPageConfig` and
`VirtualTextureStreamConfig` for the existing disk-backed streamer.
No GPU/renderer/ECS dependency and no concurrent mutation of a
readable disk archive. The owning application must retain old
streamer/texture resources until its GPU accesses and feedback readback
are complete, then activate its replacement at a safe frame boundary.
A region-revision coordinator and material mapping are next;
this source commit **does not yet automatically export HorizonBake::atlasRgba
or hot-swap an initialized streamer**.

Actual checks: inspected `VirtualTextureDiskQueue`,
`VirtualTextureFileStreamer`, existing Python packer and Terra's
`HorizonBake` / model images. GitHub source commits succeeded.
C++ compilation, CTest, shader and GPU tests NOT RUN per user request.
Next: add Terra-facing image/atlas pack and activation helper with old/new
streamer retention; validate alpha atlas mip edge behavior later.


## Stage 10C — TerraVeave runtime atlas packaging and safe revision streaming (2026-11-19)

**Source commits:** `cd3e8a6c6f29e20d9e57b9e8f1fb4b01429913d3`,
`035ed8ab375c021c84dc0b964ebb4572864951fa`.
**Dependency:** generic page archive writer, Stage 10B,
`dceac4832ef756e7a4ac1475e9cd37635f3366d0`.
**New:** `engine/render/TerraWeaveVirtualAtlasArchive.h`.
**Branch:** `feature/texture-rhi-unification` (PR #21).

Implemented a Terra-facing bridge to the real CPU RGBA8 page writer:
`terraWeaveAtlasPixels(Image)` extracts in-memory pixels from
`Image::rgba` or `sharedRgba` while retained;
`packTerraWeaveModelImage(...,revision,Model,imageIndex,...)` rejects
non-`terraweave/` models or uploaded-only images;
`packTerraWeaveAtlas(...,revision,{width,height,span},...)`
supports direct `HorizonBake::atlasRgba` before its upload/CPU release.
Both produce the exact on-disk mip/gutter physical-page directory format
used by `VirtualTextureDiskQueue`, with distinct immutable revisions.

Added `TerraWeaveVirtualAtlasSession` with two-phase GPU streamer
switching: `prepare(version,physicalPages)` initializes a candidate
`VirtualTextureFileStreamer` and requests coarsest fallback pages
pinned for each layer, `pumpCandidate(budget)` advances disk/GPU
uploads, `candidateFallbackResident()` checks residency,
`promote(feedbackPass)` rejects active/pending/unconsumed feedback
and preserves the old `active()` as `retired_`,
`retireCompletedGpuWork()` releases the retired streamer ONLY
after the application's actual GPU fence, and `shutdown()` tears
down all streamers once the device is idle. Multiple promotions
cannot discard an unreclaimed previous generation.
`terraWeaveAtlasMapping(version,layer)` returns matching virtual
tile/mip/layer metadata for `TerraWeaveVirtualTextureFeedback`.

**Important:** `promote` does not implicitly change Terra's main
material shaders or the active renderer's VT sampler bindings;
the scene owner must swap the matching `physicalAtlas()` and
`pageTableTexture()` together, refresh the page/material resolver,
and invalidate stale per-frame mappings via
`TerraWeaveVirtualTextureFeedback::beginFrame()`.
The feedback pass is left as caller-owned and must continue to
pump the *new* active streamer's requests. Page archive packing
does not itself implement shader VT sampling. Old Horizon region
transition draws are still a material/render-policy limitation.

**Verification:** Inspected existing Python packer, disk queue,
file/physical streamer and Terra Horizon models/atlas lifetime.
GitHub commits succeeded. Native C++ compilation, CTest, shader
validation and GPU resource integration **NOT RUN** per user
functions-first instruction.

**Next:** connect VT physical atlas/page-table sampling in shared
model material shader paths and Terra material bindings; add a
revision-aware runtime provider for real Terra atlas allocations,
then finish backend compile tests and visual validation.


## Stage 10D — Terra atlas UV subrect mapping and runtime handoff (2026-11-19)

**Source commit:** `52e0396e207ec1eeaf331cc16660dbf899627185`.
**Documentation commit:** `e1bc4a6a0065af040a28212d50fcb71c3eff3ea2`.
**Files:** `engine/render/TerraWeaveVirtualAtlasArchive.h`,
`docs/texture-rhi.md`.
Extended `TerraWeaveVirtualAtlasArchive.h` with
`terraWeaveAtlasMappingForMaterial(version,Material&,mapping,layer)`,
which derives virtual texture UV scale and offset from the
identical `uvScale`/`uvOffset` used in `ModelShaders.h`.
The helper rejects nonfinite transforms, wrong layer or
nonzero UV rotation because `VirtualTextureFeedbackProgram`'s
virtual-UV U0 representation cannot yet encode an arbitrary
2x2 rotation; silently dropping rotation would cause the
feedback pass to request the wrong page. This is especially
important for Terra's per-azimuth impostor materials.

Documented a concrete call sequence for HorizonBake::atlasRgba:
pack an immutable version, prepare a candidate file streamer
alongside the active atlas, pump it until coarsest mip fallback
is resident, promote only between passes without outstanding
old feedback readback, swap physical atlas/page-table bindings
and material resolver atomically, and release retired GPU
resources only after a true GPU fence. The existing old
streamer is retained until explicit retirement.

Boundaries: one archive is one VT domain and its layers must
have matching sizes; the GPU main scene shaders are not yet
rewired to call `VirtualTextureSampling.h`. Transparent
Horizon transitions and rotated UVs require further work.
Verification: GitHub source/doc commits succeeded. Native
build, CTest and GPU testing NOT RUN per user request.
Next: implement opt-in virtual page sampling in shared model
materials (not just feedback), preserve original non-VT
material fallback, then run backend-specific tests.


## Stage 10E — main model shader virtual-page sampling and Terra active streamer (2026-11-19)

**Source commits:**
- `29cd56740358528db07bdf09e8a55629bc8d5017` — new `engine/render/ModelVirtualTextureSampling.h` with native GLSL/MSL program injection using canonical `gfx::kVirtualTextureGLSL/MSL`.
- `ca845539e3f01f83960b03c270ad393a972f3a48` — new `Renderer3D::setModelVirtualTextureResolver`, `clearModelVirtualTextureResolver`, `resolveModelVirtualTexture`; private resolver storage in `engine/render/Renderer3D.h`.
- `0b6b999af5efd750a1c669a211bed4973888e1ad` — built-in model shader VT permutation key and shader source construction (`engine/render/Renderer3DModels.cpp`).
- `bb2906dcea6bd437f60f9b0693dc15e00179908d` — binds `tex13/tex14` and `U6` per material draw; heterogeneous VT instanced batches split, CPU MDI preflight rejects VT groups.
- `9defcd9277aabaa436ece963d6576c28dd2c13f7` — disables existing GPU ICB model command path in scenes that actually contain VT materials, preventing unencoded atlas identities.
- `b0eb4b0755284277a708966fe64e30d2851f8726` — Terra bridge uses identical mapping lookup for feedback and main material sampling, clears sampling callback at shutdown.
- `42ba916e534d3375de441a344bfe01b714fc609c` — `connectTerraWeaveVirtualTextureSampling(renderer,feedback,session)` reads the active streamer generation/atlas/table and validates it against the material's archive mapping.
- `22377145889e75322b29d4876a22bc15d13450e4` — prevents VT on per-vertex material-palette batches, which can address multiple materials.
**Documentation commit:** `4516a4a9ae9cefecddf3a63f5b9e6a93485493d0`.
**Branch:** `feature/texture-rhi-unification`; PR #21.

**Architecture and API:**
`ModelVirtualTextureBinding` contains two native `TextureHandle` values (`physicalAtlas`, `pageTable`), `layer` and `gutterPixels`. The renderer queries an opt-in `ModelVirtualTextureResolver(const Model&,const Material&,ModelVirtualTextureBinding&)` when selecting built-in material pipelines. Only valid resources enable a shader permutation. Shader injection replaces the regular base-colour `texture(tex1,v_uv)`/`tex1.sample(s1,in.uv)` with the canonical `aiverseSampleVirtual`/`aiverse_sample_virtual`, using physical RGBA8 2D-array page atlas and RGBA8 page table in slots 13/14 and U6=buffer(7) on Metal. The existing `tex1` stays as fallback for nonresident pages; importantly this is **real shader virtual-page sampling** but does NOT free the original source texture VRAM yet. Explicit shader graph, shadow, outline and GPU encoded paths are not virtualized. Graph uses overlapping slots 13+ so is intentionally excluded. Batching compares the external atlas/page-table binding; heterogeneous batches split, CPU indirect is rejected and GPU ICB path is disabled for VT scenes until command ABI supports per-command atlas identity. CPU path still uses normal shared model vertex deformation/instancing, alpha and LOD code.

`TerraWeaveVirtualTextureFeedback::resolveMaterialMapping` is shared by the feedback shader and colour shader. `connectTerraWeaveVirtualTextureSampling` (defined in `TerraWeaveVirtualAtlasArchive.h`) installs a renderer resolver that checks `TerraWeaveVirtualAtlasSession::activeVersion()` and obtains `active()->physicalAtlas()` and `pageTableTexture()`; no stale handles are cached across Horizon revision promotion. The model/material page mapping must have matching tile counts, mip levels and layer. Terra's ordinary `Renderer3D::drawModel` submissions (trees, impostors, Horizon) now have an opt-in end-to-end sampling seam.

**Owner lifecycle:** create `VirtualTextureFeedbackPass`, `TerraWeaveVirtualTextureFeedback`, `TerraWeaveVirtualAtlasSession`; attach feedback and call `connectTerraWeaveVirtualTextureSampling` after initialization; pack immutable source atlas into versioned page files, prepare/pump/promote active file streamer, register material virtual mappings, then render. On teardown clear renderer sampling/feedback resolver before destroying the bridge/session or renderer/device. Old GPU resource retirement remains fenced by the application.

**Implementation limitations:** no tested GPU results; no Shader Graph VT, independent multi-domain atlas binding, source texture VRAM reclamation, GPU ICB per-command VT binding or real Terra Atlas archive provider automatically installed by Terra's separate branch. Cached material hashes in GPU ICB remain source-only, so the conservative CPU draw fallback is necessary. The bridge still lives only in the Texture RHI feature branch; Terra code itself lives in `feature/terraweave`. Transparent region crossfades and animated/custom vertex-displacement shader graphs remain to be addressed. Physical gutter sampling and colour-space behaviour need real GPU validation.

**Actual verification:** GitHub source commits + documentation commit succeeded, inspected current `Renderer3DModels.cpp`, `ModelShaders.h`, `VirtualTextureSampling.h` and backend slot limits (GL only 0..15); **C++ native build NOT RUN; CTest NOT RUN; GLSL/MSL shader validation NOT RUN; Metal/GL/Vulkan GPU rendering NOT RUN** as requested (features before tests).

**Next step:** preserve texture-memory advantages by supporting optional reduced fallback instead of full `tex1`, improve multi-domain Terra sampling with page atlas selection/indirection, restore fast GPU submission by encoding virtual material identity in the command/batch cache, and then run compile/GPU shader verification.


## Stage 10F — reduced GPU fallback for archived opaque model textures (2026-11-19)

**Branch:** `feature/texture-rhi-unification`; PR #21.

**Source commits:**
- `50812808f715134a32ff66902f1e39a1298e84f0` — `engine/render/Model.h`: optional `Image::virtualFallbackMaxDimension` marker; default 0 preserves all existing imports/uploads.
- `de109fd2d7111c4431ac9c9a6df852c5c04da301` — `engine/render/Model.cpp`: when explicitly marked, generate reduced packed RGBA8 fallback before GPU upload; no full-size texture or compressed source upload; bypass full-resolution shared ModelTextureCache entry.
- `b1ce65fc5e12ec483ddb31f042a95e2352e87f4c` — include necessary standard headers and reject oversized fallback image dimensions.
- `250cbb77091fcda544906bfaf521fdaeb01e88c1` — new `engine/render/ModelVirtualTextureFallback.h`: `configureModelVirtualFallback(Model&,imageIndex,publishedArchive,maxDim,VirtualFallbackSavings*,reason*)` validates archived dimensions, retained RGBA8 pixels, source ownership and material safety before opt-in.
- `5fd0eeb81a9e2ff9a0683002e7cf3cd841caa192` — `engine/render/TerraWeaveVirtualAtlasArchive.h`: `configureTerraWeavePackedImageFallback(...)` uses the same generic policy, limited to `terraweave/` models.
- `f2d19a6cab27176bc3aeac3a343ef6e0ea379de6` — opted-in source upload failure now returns false and retains CPU source for retry; no success claim after missing GPU fallback.
**Documentation commit:** `4819842af6c83aa11c38afdc449e82456ce5dc66`.
**Handoff commit:** this commit, after functionality/documentation changes.

**Architecture:** The main shader in Stage 10E already reads `tex13` (physical atlas) + `tex14` (RGBA8 page table), with conventional `tex1` as fallback. Previously `tex1` was always the full-size source, negating some VRAM savings. New default-off asset policy downsamples an **opaque, base-only** model source using deterministic 2x2 RGBA8 box filtering (odd edge clamping) until the longest edge <= requested cap. `uploadModel()` then allocates only that small image in the original `Image::tex` handle, and no full-sized source GPU resource for that image. Main/shadow/editor non-VT code continues binding a valid `tex1`. When the model is released, the reduced image is destroyed via normal model resource ownership. The full source is retained on disk in immutable page archives. The shared texture-cache path is bypassed rather than replacing/reusing a cached full-resolution handle.

**Safety constraints:** `configureModelVirtualFallback()` must be called after the source has been archived and before any GPU upload, with CPU RGBA8 retained. The archive's source width/height must match the image. It verifies that every `Model::materials` use is exclusively as an opaque base-colour texture and not as shader-graph, roughness, metal, normal, occlusion, emissive or team map, nor as an alpha-sensitive or transparent base (including `TeamMode::BaseAlpha`). Dynamic `ModelDraw::materials` overrides are outside this model-local validation and must be audited by the owner. `maxDimension` must be 4..1024 and smaller than the original dimensions. Existing full GPU textures or cache leases cannot be converted in place; a fresh model/image is required, after the relevant GPU fence. Helpers return a conservative `VirtualFallbackSavings` base-level byte estimate; it is NOT actual GPU VRAM measurement and ignores mip/driver overhead.

**Actual limitations:** This improves memory for opaque VT base images only. Terra Horizon forest impostor atlases often use `AlphaMode::Mask`, and are intentionally excluded because reducing the alpha mask changes scene/shadow coverage. A future safe implementation should use full-resolution alpha-only R8 or equivalent mask alongside a reduced RGB fallback, and bind it for main, shadow, selection and feedback passes with guaranteed parity. No source-VRAM reduction is claimed for those masked atlases yet. The old GPU ICB/MDI VT state restrictions from Stage 10E remain; no fast GPU per-command VT material identity implemented.

**Actual verification:** Compared `uploadModel`, `uploadImage`, `ModelTextureCache::upload`, `Model::Image`, `Model::Material` and the Terra archive bridge via GitHub source fetch; connector accepted all code and documentation commits. **Native C++ compilation NOT RUN. CTest NOT RUN. GLSL/MSL compilation NOT RUN. Real GPU rendering, VRAM and profiling NOT RUN** per the user's instruction to finish features before tests. Do not claim build or behavioural parity from these commits.

**Next step:** add alpha-preserving memory reduction for masked Terra Horizon/impostor materials (separate full-resolution alpha mask, robust alpha cutout in main/selection/shadow/feedback), then enable safe GPU ICB batching for VT with per-command atlas/material identity. Afterwards run full native and GPU cross-backend verification.


## Stage 10G — merge-main preflight and full-resolution R8 cutout with reduced VT color (2026-11-19)

**Branch:** `feature/texture-rhi-unification`; PR #21.
**User instruction:** incorporate current `main` *before* next feature.
**GitHub ancestry check:** branch `main` HEAD was
`9ee6ff6fbf390bd45363bc92c838f492dfe92eee`, already the
merge-base and an ancestor of the feature head (then
`861e3bcc386658ae82793c4022a233b26b02b236`).
`compare(main...feature)` reported feature ahead 409,
behind 0; `compare(feature...main)` main ahead 0.
Thus `git merge main` would be **already up to date**:
no merge commit, force-update, rebase, conflict resolution or
non-ancestral copy was necessary. Continue from the actual
existing feature branch. This statement is as of the checked
branch heads, not a claim about any future main updates.

**Feature code commits (chronological):**
- `36ddfa395b11589900795c4d2b6e49407bc80760`:
  `engine/render/Model.h`: `Image::virtualFallbackPreserveAlpha`
  policy and model-owned `Image::virtualAlphaMask` GPU handle.
- `cee1212732ddd7eb07d32e903d640422b6f81d71`:
  `engine/render/Model.cpp`: extract original alpha bytes into
  full-resolution R8 texture (source's wrap/filter/mip settings);
  independently upload reduced RGBA8 color fallback; clean R8
  on failed coarse upload; release both owned resources.
- `d67cb59122c4352ceafc32f02f28494fe293c2dc`:
  `engine/render/ModelVirtualTextureFallback.h`: allow explicitly
  masked `configureModelVirtualFallback(...,preserveAlpha=true)`
  and `configureModelVirtualMaskedFallback`; maintain conservative
  material safety, add `VirtualFallbackSavings::fullResolutionAlphaBytes`.
- `d2609ed50b037728aa3f453425194d049c217908`:
  `engine/render/ModelShaders.h`: GLSL/MSL `VIRTUAL_ALPHA_MASK`
  permutations sample R8 `tex15.r` in main color, shadow and
  scene selection mask; original `tex1` color fallback remains.
- `7ed3fa3bc45913dccde7691a7a471d3636784a8c`:
  `engine/render/Renderer3DModels.cpp`: dedicated alpha shader key
  bit 30, model image alpha handle detection, `tex15` binding,
  shader texture slot count 16. Opt-in only for built-in shaders,
  no conflict with Shader Graph texture slots.
- `00f3a62bb685db8368c221d827e205ddef828456`:
  `engine/render/GpuMaterialState.h`: include R8 alpha handle
  identity in GPU material equivalence and state hash.
- `935e3230d964b674fd57888b4f5ca5a12432283a` and
  `1d68c567fdb2e1265cb16b6d1c07768f44e40591`:
  `engine/gfx/VirtualTextureFeedbackProgram.h`: optional red
  channel sampling in masked feedback shaders; second commit
  scopes generated alpha sampling to masked variants only.
- `79728bcc90ad7413d0aa8455e862297ee10cf694`:
  `engine/render/VirtualTextureModelFeedback.h`: create red-mask
  pipeline variants and use `Image::virtualAlphaMask` instead of
  reduced `Image::tex` for R8 cutout feedback on rigid models.
- `27f30c8043ed8a170922667d9f46ff6267e8d895`:
  `engine/render/VirtualTextureDeformedFeedback.h`: add R8
  coverage source selector to U6 and `tex15` support for
  skinned/baked/morph virtual feedback.
- `21c46ee487d171f52c4b9b0bbc1c6d38582923d3`:
  `engine/render/TerraWeaveVirtualAtlasArchive.h`: preserve-alpha
  option and `configureTerraWeavePackedMaskedImageFallback`
  convenience API for Horizon/Arbor impostors.
- `7ec1a3fd673eb2293def9d1b3b8d0911f8f48281`:
  `engine/render/Renderer3D.cpp`: avoid GPU ICB encoded commands
  in scenes using external R8 alpha masks, since command ABI
  has no per-command `tex15` resource identity.

**Additional documentation commits:** `94c124e78f5983f75555e0e7cce71f6d9d94b97b`
(new detailed API section in `docs/texture-rhi.md`),
`e4c6274c389f25a16e2f06bd7fa6d26ce930215d`
(mark old 'masked source not supported' paragraph as superseded),
`f9005b018d1ee5be7e5bd42298a8199baeafdec8`
(agent skill usage in `skills/visuals/aiverse-texture-rhi/SKILL.md`).

**Architecture & behaviour:** The full-size RGBA8 base source must
already be packaged to the immutable virtual texture page archive;
configure masked fallback *before* `uploadModel` while original CPU
RGBA8 is still available. `Model::Image` owns a small reduced
RGBA8 `tex` for fallback color and full-res R8 `virtualAlphaMask`
for alpha-test coverage. `ModelShaders.h` uses `tex15.r` instead
of the reduced source alpha in built-in color, shadow and selection
variants; the virtual atlas/page table are still bound to tex13/14.
The same R8 alpha source drives both rigid and deformed GPU feedback,
including masked+LOD variants. GPU-state batching accounts for
R8 resources and GPU ICB falls back conservatively. The source
upload path is default-off and bypasses full-res texture-cache
entries instead of replacing them.

**Cost illustration:** original 2048x2048 RGBA8 base = 16 MiB;
2048x2048 R8 alpha base = 4 MiB plus 64x64 RGBA8 fallback =
16 KiB. Savings about 75% of source base-level bytes, NOT
measured total GPU VRAM (mips, page-table/atlas, driver overhead
and other textures remain). `VirtualFallbackSavings` records
the alpha channel base bytes separately.

**Constraints:** `AlphaMode::Blend`, custom Shader Graph,
team modes relying on base alpha, images used for normal/metal/
emissive/occlusion/masks, already-uploaded or borrowed cached
textures, and malformed/unarchived CPU RGBA inputs are rejected.
Future `ModelDraw::materials` overrides are not visible to the
static asset-policy check; users must audit those separately.
GL/Metal shader slots 13–15 must remain mutually exclusive
with Shader Graph's texture bindings. R8 cutout mip/filter
and alpha cutoff parity across all backends still need GPU
verification, as does full model selection/shadow correctness.
Do not claim exact bit-identical rendered alpha until tests.

**Verification actually performed:** GitHub main/feature branch
head and ancestry comparison, source/API inspection, spot
inspection of inserted shader macros, feedback modes, image
ownership and material hash. All GitHub feature and documentation
commits succeeded. **Native C++ compilation NOT RUN; CTest NOT
RUN; GLSL/MSL compilation NOT RUN; GPU smoke/VRAM/shadow/selection
tests NOT RUN**, per user's continuing functionality-first order.

**Immediate next step:** finish per-command VT atlas and R8 mask
bindings for GPU ICB/MDI so model batching can be restored safely;
finish shader-graph virtual texture integration without exceeding
GL/Metal sampler limits (or preserve an explicit no-VT fallback).
Before merging to main, run targeted C++/shader tests and inspect
actual Metal, GL33/GLES3, Vulkan readbacks, alpha coverage and VRAM.
Review branch latest main ancestry again if main has moved.


## Stage 10H — restore homogeneous VT/R8 Metal ICB and CPU MDI (2026-11-19)

**Branch:** `feature/texture-rhi-unification`, PR #21.
**Code commits (chronological):**
- `3eae0251afd01af51e909b7fd38d00e3b1f2673a`:
  `engine/render/ModelVirtualTextureSampling.h`, exact VT
  resource identity hash and compatible binding predicate;
  MSL direct-vs-ICB argument-buffer shader injection.
- `2539f2239ce978aa9de867120ff8b8a92b1d3fb5`:
  `engine/render/ModelShaders.h`, Metal PBR ICB argument
  structure expanded for physical VT atlas/page-table and R8
  cutout; R8 is sourced from ICB fragment argument buffer.
- `9de0fe6e723e36c49b8dff0e251727f44c6020c1`:
  `engine/gfx/metal/GfxMetal.mm`, bind these optional
  texture resources in CPU-encoded and GPU-encoded ICB
  argument-buffer variants; explicit resource use tracking.
- `779d436b8934c1fdba2c938d5c18e91a16914bb4`:
  fixed shader #if newline in injected MSL source.
- `b83cfe96dad195a9c3b6b580dfe30deb660ee24e`:
  `engine/render/Renderer3DModels.cpp`, allow VT colour
  material permutation for compatible GPU_INSTANCE_BUFFER
  groups, bind U6 + tex13/14 in the parent material pass,
  validate shared VT identity for CPU multi-draw groups
  and explicit instancing.
- `739a9d4ce6aa88bd47c6e10d0eeabdf31897564b`:
  `Renderer3DModels.cpp` GPU preflight eligibility includes
  rigid R8 cutout models, resolves VT identity for eligible
  models, includes identity in material hash and cached plan
  signature, and exactly verifies identical GPU VT bindings
  in fresh and cached group membership.
- `a8db1683b345db93ef51f27f8c6eb388947f248c`:
  `engine/render/Renderer3D.cpp`, removed previous
  whole-scene GPU draw disable on any VT/R8 material.
  Individual ineligible draws still use ordinary CPU path.
- `3b863f51eb098a85104dd795eec548f1f23b7e3a`:
  `Renderer3D.h` captures VT+alpha identity per encoded
  GPU ICB group.
- `2e625cd8f995b9fc97fdd9e9ce62a527a9d7313e`:
  `Renderer3DModels.cpp` revalidates all external resources
  before executing the encoded ICB. A Horizon archive
  revision/mask switch rejects old commands and leaves them
  available for safe CPU fallback in the regular pass.
- `c9b75f1455c2c7adf9470e8d7c4b1e38d9595645`:
  Metal backend requires a PBR argument-buffer ICB variant
  for any shader with VT or R8 resources; never trusts
  direct-pipeline fragment texture inheritance.
- `f13ecb1cb24fc10c0d24515cfb8dc915a5230cbc`:
  `ModelShaders.h` does not declare unused sampler IDs
  27/29 for VT array textures; physical atlas uses s1 and
  page table is explicitly read-only without sampler.
- `598e649966de4fb4bb9fdf8615bc59ff335e2daa`:
  Metal backend binds only VT argument textures at 26/28,
  avoiding unused sampler binding/validation errors.

**Docs/agent skill commits:**
`00f5e75f7243e9695201a7858bccdcd808183584`
(`docs/texture-rhi.md`),
`71ba83ed8517572d0f927f046973694cb256e526`
(`skills/visuals/aiverse-texture-rhi/SKILL.md`).

**Architecture/API:** VT remains one shared set of fragment
resources per homogeneous indirect group. RHI shader slot
assignments: model base texture at tex1, physical VT array
tex13, nearest RGBA8 page table tex14, full-resolution R8
mask tex15; one U6 vec4 at Metal buffer(7). The ICB PBR
fragment argument encoder references the optional tex13
(encode ID 26), tex14 (ID 28), tex15+s15 (IDs 30/31).
Shader source is switched between direct fragment resources
and ICB argument buffer based on `AIVERSE_ICB_PBR`; this
is not a heterogeneous per-command VT resource table.
`modelVirtualTextureBindingHash` is a hash-only prefilter,
and `compatibleModelVirtualTextureBinding` performs exact
comparison of atlas, page table, layer, gutter and VT presence.

**Grouping safety:** GPU material source and R8 mask hashes
remain in `GpuMaterialState.h`. In addition, the renderer
incorporates the external VT mapping into the GPU batch plan
hash/signature, and checks exact compatibility in every
cached/fresh GPU group. A staged `GpuEncodedBatch` stores
its original VT/R8 identities and rechecks just before
ICB execution; changed identities cause rejection and
safe CPU drawing. CPU MDI may submit a group sharing the
same VT domain; mixed groups reject to direct/instanced.
Only opaque or R8-masked rigid built-in models with no
GPU deformation, GI, graph, transparency, dissolve or
LOD fading are eligible. Original backend experimental
feature flags and renderer opt-in remain mandatory.

**Actual verification:** fetched and inspected
`Renderer3DModels.cpp`, `Renderer3D.h`,
`GpuSceneDrawCommands.h`, `ModelShaders.h` and
Metal ICB source/argument buffer contracts. GitHub
accepted the above source and docs commits. Native C++
compilation **NOT RUN**; CTest **NOT RUN**; Metal/GL/Vulkan
shader compilers and GPU frame output **NOT RUN**; no
performance benefit can be asserted. A separate test
agent should verify actual Metal ICB pipeline creation,
argument buffer texture residency, alpha silhouette
parity, archive switching and fallback, with device
logs, before enabling for production.

**Remaining:** general per-command heterogeneous VT
material indexes and GPU-resident per-command atlas data;
Shader Graph VT integration without exceeding sampler
budgets; multi-domain Terra atlases and full runtime
tests. Safe existing CPU fallbacks remain for unsupported
groups. The user requested feature completion first,
tests later; do not mark tests passed.


## Stage 10I — explicit feedback-domain filter (2026-11-19)

**Source commit:** `b804e5edd7032eaed6860fec7c5333bc36e40fad`.
**File:** `engine/render/TerraWeaveVirtualTextureFeedback.h`.
**Branch:** `feature/texture-rhi-unification` (PR #21).

Found a hard ABI constraint: `gfx::encodeVirtualTextureFeedback`
stores tile X/Y, mip and layer in a 32-bit word, but no archive
or domain ID. Consequently, mixing independent Terra/Horizon/Forest
archives into one GPU readback would route otherwise valid page
IDs to the wrong physical page streamer.

Added `setFeedbackMaterialFilter(FeedbackMaterialFilter)` and
`resolveFeedbackMaterialMapping`. The callback that the existing
rigid/deformed feedback adapters receive now checks this filter
*before* resolving the virtual mapping. The main color/material
path still uses unfiltered `resolveMaterialMapping`, so all
active domains may render normally in the same scene. Materials
outside a selected feedback domain emit zero page IDs but still
rasterize their depth in the feedback pass. The filter defaults
to unrestricted for backwards-compatible single-domain usage
and is cleared on bridge shutdown.

Verification: inspected source shader codec and pass readback/
file streamer contracts; source committed via GitHub. Native
build, CTest, Metal/GLSL/Vulkan shader compilation and GPU
tests NOT RUN (user requested features first).

Next: add an independent archive-domain owner with one
`VirtualTextureFeedbackPass` per domain, frame-budgeted
round-robin capture and per-domain `updateStreaming`, while
preserving ordinary shared Renderer3D color/ICB submissions.
Never send decoded feedback to another domain's streamer.


## Stage 10J — multi-archive Terra VT sessions and independent GPU feedback (2026-11-19)

**Branch:** `feature/texture-rhi-unification`; PR #21.
**Feature commits:**
- `ab984fbd34c86bbb7945830b69a6b492c4439dd4` — generic optional
  `Renderer3D::setVirtualTextureFeedbackScheduler` / clear API in
  `engine/render/Renderer3D.h`.
- `9ef13f7f4733e1c2c96c8f7f0df12f57413c83ed` — call scheduler after
  shadow passes and before main pass; when configured it replaces, rather
  than duplicates, the old single feedback pass in Renderer3D.cpp.
- `4933038d5c84039d3de64c4c16b63b2a2be55c3f` — new
  `engine/render/TerraWeaveVirtualAtlasDomains.h` manager.
- `6999b17a764584bb2f3afb0e5897f824cfaa835b` — fix
  portable std::less material pointer ordering, std header includes and
  exception-safe feedback-domain selector reset.
- `68b0676d6709529f7b739e5f97ecc923610235ff` and
  `781e4a70a5eabd4e6136b64625ad9dcd79c79dd3` — repair
  `engine/render/ModelVirtualTextureSampling.h` MSL injection ABI:
  its previous `GI_FARGS) {` anchor was stale after
  `VIRTUAL_ALPHA_MASK` extended the fragment function signature.
  The injection now matches the stable `GI_FARGS` line and inserts
  U6 and conditional direct/ICB textures without deleting the
  R8 alpha mask arguments. The second commit fixes newline escapes.
- `b9d2903d3145bf722d896f115b9efbb0a3dbf8ed` — add
  `TerraWeaveVirtualTextureFeedback::initializeForDomains` so multi-domain
  mode does not require a dummy/legacy target.
- `0a0e6c3ac74e3f48f265eba4eb108f4a8006cb08` — expose model
  feedback programs' selected depth attachment mode.
- `98a643e41a71f409c635e69ad6e56ba4e05c38d5` — reject domain
  feedback passes whose depth mode is incompatible with shader programs.

**Documentation/AI skill commits:**
`0fb709294043ac2cdb52271d113a4ebd945b246d`,
`86f1561040623d7178cf6221eecb380bc2f12b4d`,
`ca22456dfc1f0e85f7f46b77845db4dbed7f1b34`,
`ac3f7a3c9408a4d3445a58a1f0ca5cdf844a3020`.
Files: `docs/texture-rhi.md`,
`skills/visuals/aiverse-texture-rhi/SKILL.md`.

**Architecture/API:** `render::TerraWeaveVirtualAtlasDomains`
stores one `TerraWeaveVirtualAtlasSession` and independent
`gfx::VirtualTextureFeedbackPass` per numeric DomainId. This avoids
the packed 32-bit feedback codec's lack of a domain field and
permits different layer counts, tile grids and archive revisions.
`addDomain(id,Options)`, `prepare(id,version)`,
`pumpCandidate(id,budget)`, `promote(id,requireFallback)`,
`beginFrame()`, `registerMaterial(model,material,id,layer)`,
`registerDraw(ModelDraw,id,layer)`,
`setLiveDomainLookup(callback)`, `attach(renderer,feedback)`,
`pumpActive(budget)`, `requestPage(id,key,pin)`,
`session(id)`, `feedbackPass(id)`,
`retireCompletedGpuWork(id)`, `detach()`, `shutdown()`.

`attach` installs one Renderer3D pre-main feedback scheduler,
one sampler resolver for *all active* archive domains, and a
feedback material filter that selects exactly one domain at a
time. At most one ready domain pass is captured each scene frame,
with stable round-robin selection. Other materials write zero
feedback page IDs but preserve depth occlusion. Independent
per-domain feedback targets/decoders ensure asynchronous readbacks
are consumed only through that domain's streamer's
`VirtualTextureFeedbackPass::updateStreaming`. Main scene samples
different VT atlases simultaneously through the existing per-model
ModelVirtualTextureBinding; compatible homogeneous ICB/MDI
grouping from Stage 10H is preserved. Color and feedback
material lookups are resolved using actual Model/Material pointers
or an opt-in live domain lookup for private Horizon region models.
Frame registrations are explicitly reset, preventing stale pointer
keys after region unloads and asset reloads.

`promote` prepares an independent feedback pass for the candidate
archive's tile/mip geometry and uses the existing two-phase
TerraWeaveVirtualAtlasSession switch. The candidate requires
fallback residency (by default), the old readback must not be
pending or unconsumed, and the application must still observe a
real GPU fence before releasing the retired atlas; no timer-based
resource retirement is performed. Each domain's feedback depth
attachment mode must match the one used when model feedback shader
variants were initialized.

**Important source-compatibility fix:** previous main-model Metal
VT injection searched for `GI_FARGS) {`, which no longer exists
after the R8-alpha signature change. The new injection preserves
the R8 parameter block, inserts VT U6 at Metal buffer(7), and
switches tex13/14 between direct resources and ICB argument
buffer. This was source-corrected, not shader-compiler verified.

**Verification actually performed:** GitHub source inspection of
feedback ID codec, `VirtualTextureFeedbackRuntime` and its
planner config, `VirtualTextureFeedbackPass`, model shader
signature, archive streamer, and actual renderer pass ordering;
all named feature and docs commits accepted by GitHub.
**Native C++ compilation NOT RUN; CTest NOT RUN; MSL/GLSL/Vulkan
shader compilation NOT RUN; GPU readback/streaming/VRAM tests
NOT RUN**, per user's functionality-first instruction.

**Next steps:** complete actual Shader Graph VT support within
texture-sampler budgets and semantic graph material parity;
investigate heterogeneous per-command Metal ICB VT material
tables (current GPU ICB groups must remain homogeneous). After
features close, run dedicated cross-backend native/GPU tests,
confirm GPU feedback domains cannot cross-contaminate physical
page streamer requests, and compare viewport/LOD/alpha results.


## Stage 10K — explicit material Shader Graph VT sampling (2026-11-19)

**Branch:** `feature/texture-rhi-unification`, PR #21.
**Feature commits:**
- `2d3b97b4142fea11c51b5445ca1115ffec11a60c`:
  `engine/render/graph/ShaderGraph.h`, add
  `Program::usesVirtualTexture` metadata.
- `8d4e6b9d5952434bdfa39fda5a1f94b3caf95569`:
  `engine/render/graph/ShaderGraph.cpp`, register
  `sampleVirtualTexture` as material-fragment-only graph node,
  emit `SG_SAMPLE_VIRTUAL(uv)`, count it in sample budget,
  and validate that at most ONE independent graph texture
  coexists with its two virtual-array resources.
- `8edab2d591d590b3b7e0191a4aa2029b2faa1055`:
  `engine/render/ModelVirtualTextureSampling.h`, allow
  dynamic physical atlas/table slots (13/14 or 14/15) in
  canonical shared GLSL/MSL virtual lookup injection; retain
  the corrected Metal R8/ICB signature handling from Stage 10J.
- `6b7b5cd67b625648b171adbefe55999ebc7a3c5b`:
  `engine/render/Renderer3DModels.cpp`, select
  `kKeyModelVirtualTexture` for built-in materials OR graphs
  that explicitly request the VT node, add graph macro
  expansion against real atlas/page-table plus fallback
  to conventional `SG_SAMPLE(uv)` if no mapping is present,
  allocate bounded shader slots and bind resources/U6 per
  material draw. Normal `sampleTexture` graph resources stay
  separate; MSL/GLSL use same native VT sampling helpers.
- `8d9edbcb3a1e448faabdbd368ff2904f9b124c73`:
  `engine/render/GpuMaterialState.h`, include graph VT flag
  in exact material equivalence and GPU material state hash.
- `fa0b83121370a2319dddfb8f66b420a17379132c`:
  graph node supports opt-in `srgb:true` conversion through
  the existing compiler colour transform.

**Documentation/agent skill commits:**
`11f3cfbca60c7e88fe7255f649e19afedc791cb2`
(`docs/shader-graph.md` with JSON node example),
`bac54803a4c4ea985d96cb07a0ac5cce6f6c425b`
(`docs/texture-rhi.md`),
`15ac86f21176413a2099efc1af9a7815683c817e`
(`skills/visuals/aiverse-texture-rhi/SKILL.md`).

**API:** Shader Graph material JSON may contain an op
`sampleVirtualTexture` with one UV-vector input and optional
`srgb:true`. It emits a fragment `vec4/float4` sample.
`Renderer3D` uses its installed ModelVirtualTextureResolver
for physical atlas and page-table handles, layer and gutter.
No valid VT mapping produces ordinary source `tex1` fallback
instead of an invalid array sample. Domain=material only;
vertex, postfx and particle contexts are rejected. The graph
compiler's existing eight-sample cap still applies.

**Texture slot budget:** graph-resident textures occupy
tex13 onward. A graph requesting VT may use 0 independent
textures (VT arrays at tex13/tex14) or 1 (arrays at
tex14/tex15), bounded by GL33/GLES3/Metal 16 sampler slots.
Two or more independent graph textures with the VT node are
rejected at graph compilation. R8 masked-alpha reduction
remains forbidden for Shader Graph inputs, preventing collision
with R8 tex15. Shader Graph material draws stay off Metal
ICB/MDI path until the argument-buffer graph ABI supports
their dynamic texture slots. Existing non-VT graphs continue
their previous shader/material code path.

**Architecture:** This is an explicit graph sample node,
NOT automatic virtualization of every `sampleTexture`
node. The new op reuses `gfx::kVirtualTextureGLSL/MSL` page
lookup and existing physical atlas/page table, with no
alternate VT codec or new streamer. `ModelShaderKey`
and `GpuMaterialState` include graph VT permutations.
The model shader's `SG_SAMPLE_VIRTUAL` macro is supplied
either by a real VT shader variant or safe tex1 fallback.
Graph-backed multi-domain Terra samplers can use any active
domain returned by the existing material resolver.

**Checks actually performed:** fetched and inspected
`ShaderGraph.h/cpp`, `ShaderGraphNodes.h`,
`Renderer3DModels.cpp`, `ModelVirtualTextureSampling.h`,
`ModelShaders.h`, and the RHI's texture binding conventions.
Source and docs commits accepted by GitHub.
**Native C++ compilation NOT RUN; CTest NOT RUN; GLSL/MSL
shader compilation NOT RUN; Metal/GL/Vulkan GPU execution
NOT RUN**, per user's instruction to finish functions before tests.

**Remaining work:** proper heterogeneous per-command Metal ICB
VT/resource index ABI (current GPU ICB remains homogeneous),
arbitrary many-texture Shader Graph VT via an explicit
resource/descriptor strategy instead of overflowing 16 slots,
source-alpha/LOD visual parity and source texture lifecycle
verification. Treat this graph node as implemented in source
but not compiler/GPU-validated. Next agent should update
AGENTS.md and add shader/backend tests during the test phase.


## Stage 10L — per-axis VT sampling wrap semantics (2026-11-19)

**Branch:** `feature/texture-rhi-unification`, draft PR #21.
**Main sync:** latest verified compare `main@9ee6ff6` was behind=0
before this stage; no upstream main merge was necessary.

**Implementation commits:**
- `f3f0a760d0edccbf9e238b55e52ff9aa3d46eeab` —
  `engine/gfx/VirtualTextureSampling.h`: GL/MSL table lookup now
  supports per-axis ClampToEdge/Repeat/MirrorRepeat; old helper signatures
  forward to explicit clamp-only overloads.
- `d4195954058025d50516188a49d938535f6959db` —
  `engine/render/ModelVirtualTextureSampling.h`: typed binding address
  U/V policy, validation (border unsupported), hash/equality, unchanged
  16-byte U6 ABI and shader-injection argument forwarding.
- `e78ebefe3df8c88444b6389133908632235d6df6` —
  `engine/render/Renderer3DModels.cpp`: active renderer source binds
  both address modes through U6.zw; material Shader Graph explicit VT
  sampling invokes matching GLSL/MSL entry points.

**Documentation commits:**
- `f8df4c88b9bc1ddd013dc90d6709e5e24d2bab7f` — `docs/texture-rhi.md`.
- `e59cea10e8cc7de06672735029624f56617c0884` —
  `skills/visuals/aiverse-texture-rhi/SKILL.md`.

**Architecture and compatibility:** `ModelVirtualTextureBinding` keeps
default U/V clamp. Opt-in Repeat/MirrorRepeat are independent on each
axis and use `fract` / mirrored-fract in the GPU virtual lookup before
the physical page lookup, retaining existing edge-clamped gutter
sampling. The source texture's ordinary fallback continues through
`tex1` using its own sampler. Address modes are included in both exact
GPU grouping identity and material-group hash, so groups with
different wrap semantics never share one external VT state.
`ModelVirtualTextureUniforms` remains four 32-bit floats; z/w now
store the address modes. Legacy shader entry points still exist and
select clamp. ClampToBorder fails binding validation; there is no
fictional border-colour emulation.

**Checks performed:** GitHub file fetch/diff-targeted source
inspection and strict one-occurrence transform postconditions; all
three source commits and docs commits accepted. The updated shader
overload signatures and generated call sites were fetched again
and manually inspected. **C++ compile NOT RUN, CTest NOT RUN,
GLSL/MSL compiler NOT RUN, Metal/GL/Vulkan GPU tests NOT RUN**. No
local repository checkout or native compiler invocation was possible.
Explicitly do not claim UV-parity from source review.

**Known limitations:** packer must supply periodic/mirrored tile
gutters for high-fidelity seams; ordinary source sampler and
resolver wrap modes must agree. Vulkan/Metal homogeneous ICB/MDI
batches still require original GPU validation and may need explicit
U6 parameter-path checks under per-draw VT bindings. No new GPU
resource slot is consumed and this is not a new bindless strategy.

**Next agent:** inspect and implement a true heterogeneous
per-command Metal ICB VT resource-table ABI (optional opt-in) or
material-table/bindless descriptor strategy for multiple independent
Shader Graph source textures; preserve conservative homogeneous
fallback. During the later verification phase, add wrap tests for
negative UV, exactly integer UV, mirrored edges, mixed U/V modes,
shader compile and source/VT edge pixels on Metal/GL/Vulkan. Keep
this PR draft until actual backend builds/GPU tests pass.


## Stage 10M — immutable repeat/mirror VT page archive gutters (2026-11-19)

**Source commit:** `d034590fef411f16ddc59d1956844982ba5d1bae`.
**Branch / PR:** `feature/texture-rhi-unification` / Draft PR #21.

**Changed source:** `tools/virtual_texture_pack.py` now supports
`address_u/address_v` keyword options on `pack_pages`,
`make_page` and `downsample_rgba`, with CLI switches
`--address-u/--address-v clamp|repeat|mirror`.
The `address_texel` helper implements per-axis wrapping or
edge-duplicating mirroring for negative / over-bound coordinates;
it is reused for gutter extraction and odd-dimension mip samples.
The default is unchanged clamp/edge behavior. A non-default
archive manifest records `addressU` and `addressV` so the
game-side resolver can choose matching binding modes. Output
creation stays transactional and validates modes before staging.

**Documentation commits:**
- `f3a6c0fc32952540523932472e87781bf940eda6` — `docs/texture-rhi.md`.
- `31896f477618f0ce5fe1de7e7c3ddd324d083dc3` —
  `skills/visuals/aiverse-texture-rhi/SKILL.md`.

**Integration/API:** The archive manifest is descriptive; the
native `ModelVirtualTextureBinding` does **not** infer sampler
addressing from it automatically. The project must set the
same `gfx::SamplerAddressMode` on its VT resolver and its
ordinary source material fallback sampler. Rebuild immutable
archives and bump archive version on wrap changes. No new
third-party dependency or GPU texture format was introduced.
Older clamp/clamp manifests retain their original field set.

**Checks actually performed:** GitHub source fetch and
strict unique replacement/postcondition inspections;
GitHub accepted the implementation and documentation commits.
**Python unit tests NOT RUN, Python CLI smoke NOT RUN,
CMake/CTest NOT RUN, MSL/GLSL compilation NOT RUN, real GPU
UV-seam comparison NOT RUN**, per user's feature-first phase.
Do not infer a passing build or verified filtering.

**Risks:** wrap-aware padded tiles may not be automatically
picked up by an existing Terra/Model archive version; caller
must refresh the immutable revision and resolver settings.
Cross-page derivative filtering may still need GPU seams/LOD
tuning. Add explicit source packer unit coverage for repeat,
mirror, mixed U/V and odd dimensions during deferred test
phase.

**Next concrete stage:** implement optional manifest-to-material
address-mode validation during Terra archive activation
(without mutating the active atlas or overwriting explicit
material sampler settings), or proceed with the heterogeneous
Metal ICB descriptor-table strategy. Keep PR draft until
native/Metal/Vulkan/GL compilation and the release gate pass.


## Stage 10N — native archive address policy and TerraWeave propagation (2026-11-19)

**Branch:** `feature/texture-rhi-unification`; Draft PR #21.
**Source commits:**
- `f7efaff6568a49d2d00fea394c1267fdc86c5c0c`:
  `engine/gfx/VirtualTexturePageArchiveWriter.h` adds
  `VirtualTextureArchivePackConfig::addressU/addressV` and
  `VirtualTextureArchiveVersion::addressU/addressV` typed policy,
  per-axis Repeat/MirrorRepeat for gutters and odd-mip filtering,
  fail-closed ClampToBorder rejection, and immutable content hashing
  keyed by non-clamp address policy. Clamp-only hashes/manifest fields
  remain backward-compatible.
- `af38ddd2d0ef2e7eb601fa3524302d2dc53a34c4`:
  `engine/render/TerraWeaveVirtualAtlasArchive.h` single-domain
  sampling resolver forwards active archive U/V policy into
  `ModelVirtualTextureBinding`.
- `6e7551131024e4a7b4655c55e32b2c7cc8c60787`:
  `engine/render/TerraWeaveVirtualAtlasDomains.h` multi-domain
  `resolveSampling` forwards independent active session U/V policy.
**Documentation commits:**
- `8f72346478eb5d2d02373d60f218a935c68135fe`: `docs/texture-rhi.md`.
- `437e21e335d3a41c72c9f454523e8fe96a6c0039`:
  `skills/visuals/aiverse-texture-rhi/SKILL.md`.

**Public API / mechanics:** Producer chooses per-axis
`gfx::SamplerAddressMode::ClampToEdge`, `Repeat` or
`MirrorRepeat` in `VirtualTextureArchivePackConfig`.
The native archive writer stores that policy in its typed Version,
packs matching physical page gutters and mip samples, and includes
it in its immutable revision identity. The TerraWeave session
promotes an all-or-nothing typed version. Both model VT resolvers
now bind the active archive addressing when preparing a draw.
No ECS/gameplay dependency. Source fallback `tex1` must
use the corresponding conventional material sampler. Existing
default clamp archives remain stable.

**Verification actually performed:** Fetched and inspected the
native page writer and both Terra resolvers, strict per-file
transform checks, committed C++ source and canonical docs. Re-fetched
and inspected emitted C++ manifest formatting. **No C++ compile,
CTest, native sample fixture, Metal/GLSL compiler, runtime GPU
seam verification or performance test was run.** CI not counted as
passing.

**Known risks / limitations:** Standalone Python manifest import
is not automatically integrated into the typed
`VirtualTextureArchiveVersion`; an external loader must map
manifest strings to the enum, validate and supply those fields.
Built-in/native C++ packer passes the complete typed Version
to the Terra resolver. Border-color addressing and arbitrary
anisotropic seam fidelity are explicitly unavailable. Multi-domain
GPU readback and real device verification still required.

**Next stage:** optionally add a safe format-specific parser of
Python `manifest.json` into `VirtualTextureArchiveVersion`
with bounded paths/dimensions and immutable page-directory
validation (must fail closed and not change the active atlas),
or complete heterogeneous ICB/graph material table. During
deferred testing, add native packer and Python parity checks for
negative/positive seams, mixed U/V and odd dimensions, then
GPU image A/B for source vs VT across actual supported backends.
Keep PR draft until all compile/CTest/GPU gates are evidenced.


## Stage 10O — bounded disk VT manifest import (2026-11-19)

**Branch:** `feature/texture-rhi-unification`, PR #21 (draft).
**Feature commits:** `a33dfd45567112ebc7ee9e4aacc9f75c8b6a504c`
adds `engine/gfx/VirtualTextureArchiveManifest.h`;
`50c35d47c70ec087db151cd9622f4b3f9c6411a7`
adds page directory symlink rejection and removes an unneeded
pre-calculation.
**Docs commits:** `f9704ae4f1a309ac4c732aca9d418ba10412a661` `docs/texture-rhi.md`;
`752af254663f62ecaea2d3fedf62610a9ba2ff32`
`skills/visuals/aiverse-texture-rhi/SKILL.md`.

**Public API:** `gfx::importVirtualTextureArchiveManifest(const Json&,
trustedDirectory, outVersion, error, maxArchiveBytes, maxPages,
importedRevision)`; optional convenience
`gfx::loadVirtualTextureArchiveManifest(trustedDirectory,...)`.
Reject inconsistent dimensions, formats, mip geometry, page count,
archive bytes, missing/nonregular/symlinked files or invalid sampler
address metadata. Parse strict unsigned integer literals rather than
allowing fractions/negative/overflow. Only caller-supplied paths are
used and a failure leaves the `out` version empty. Page-file
lengths are checked, but the optional hash metadata is not
cryptographic verification of payloads.

**Architecture:** This lives in gfx, uses existing engine/data/Json,
performs no GPU work or authoritative ECS operations. It can import
both Python and native-writer manifest schemas into a
`VirtualTextureArchiveVersion`. A consumer must still call normal
prepare/pump/promote with GPU fence rules. It does not swap active
resources or bypass a live feedback readback.

**Actual checks:** GitHub source inspection of writer/streamer/JSON
APIs; code committed with exact-file edit verification. **C++ build
NOT RUN, CTest NOT RUN, actual archive integration NOT RUN, GPU tests
NOT RUN**. Keep PR draft.

**Limitations:** Caller must choose a trusted immutable archive
directory; only page shapes and sizes, not source cryptographic
authenticity, are checked. Filesystem preflight is synchronous and
should not run per-frame. Native writer revision is supported;
Python packer has no revision and needs a caller-supplied identifier.
A separate integration API with Terra session/domain manager is
still required.

**Next stage:** add `prepareFromArchiveDirectory` to
`TerraWeaveVirtualAtlasSession` and
`TerraWeaveVirtualAtlasDomains`, preserving active revisions on
invalid imports and retaining normal fallback residency/fence gates.
Then add focused manifest parser tests in test phase, including
symlinks, truncated pages, malformed metadata, and mixed axis modes.


## Stage 10P — TerraWeave imported archive candidate preparation (2026-11-19)

**Branch:** `feature/texture-rhi-unification`, draft PR #21.
**Source commits:** `b1240e2cc2341e408024cfd0c1c8240e0ac818ca`
(`engine/render/TerraWeaveVirtualAtlasArchive.h`),
`7b19a949957f4ac939e21947333844c3ff2b0f1c`
(`engine/render/TerraWeaveVirtualAtlasDomains.h`).
**Docs commits:** `de4c659889aa63ff4181fc09208854f0ec8746ad` (`docs/texture-rhi.md`);
`44113441e68e0d0aced62aea2695fd9e0d8307ed` (`skills/visuals/aiverse-texture-rhi/SKILL.md`).

**APIs:** `TerraWeaveVirtualAtlasSession::prepareFromArchiveDirectory`
and `TerraWeaveVirtualAtlasDomains::prepareFromArchiveDirectory`.
The first validates a caller-provided trusted immutable archive
directory with `gfx::loadVirtualTextureArchiveManifest`, then passes
the validated typed version to ordinary `prepare`; errors propagate
to `session.lastError()`. The multi-domain helper delegates to the
matching session with that domain's configured page/buffer budgets.
No promotion, GPU fence bypass or shader-slot reassignment occurs
during import; current active/candidate resources survive errors.

**Compatibility:** Existing `prepare(VirtualTextureArchiveVersion)`
API remains. Both Python and native archive manifest schemas are
accepted via gfx helper and typed addressing flows into the existing
single-/multi-domain VT shader resolver. Python `importedRevision`
is supplied by the caller when necessary; no untrusted paths from
JSON are used. Native C++ archive writer remains an alternative.

**Checks:** GitHub source inspection and source+docs commits accepted.
**C++ compile NOT RUN, CTest NOT RUN, OpenGL/Metal/Vulkan NOT RUN,
full editor integration NOT RUN.** No release readiness claim.

**Known risks:** Validation is synchronous and should run in an asset
staging phase on a trusted immutable directory, not each frame.
CPU import does not cryptographically authenticate the directory,
and source texture fallback sampler modes must be consistent. Old
GPU atlas is still fenced/retired by the owner, not by this helper.

**Next:** ensure GPU VT page feedback and CPU prefetch use the same
Repeat/MirrorRepeat UV addressing as Stage 10L shaders. In particular
audit `VirtualTextureModelFeedback.h`,
`TerraWeaveVirtualTextureFeedback.h`, feedback fragment shader
uniforms, and any CPU UV region planner. Cross-backend shader and
archive integration tests are deferred until feature closure.


## Stage 10Q — wrap-aware GPU feedback page IDs (2026-11-19)

**Branch:** `feature/texture-rhi-unification`, PR #21 draft.
**Source commits:**
- `2829b03fb3b91cc19d79e3213a9eb3a8c37c7b4c` —
  `engine/gfx/VirtualTextureFeedback.h`: explicit per-axis
  Clamp/Repeat/Mirror CPU+GLSL+MSL page-ID lookup, with backwards-
  compatible normalized-only overloads.
- `48b1e9dbbf7b7dd551449771dd545251d5a74d12` —
  `engine/gfx/VirtualTextureFeedbackProgram.h`: append U0 address
  modes for rigid GPU feedback (176 -> 192 bytes) and update native
  fragment shader calls.
- `d8ace88999ff858a8e5409ba4db42602c3abdd85` —
  `engine/render/VirtualTextureModelFeedback.h`: typed mapping
  modes and rigid U0 population.
- `bccd7f0eab76403feba8cf44c7232cb77e8daeea` —
  `engine/render/VirtualTextureDeformedFeedback.h`: append U6
  address modes (96 -> 112 bytes), update GLSL/MSL and runtime U6.
- `a30dd3467a6ee96a73e8534927f9331bde04e06a` —
  `engine/render/TerraWeaveVirtualAtlasArchive.h`: pass active
  version addressing to shared material feedback mapping.
- `ab5e8fe44cb49bb50da765a038412d9a54115af4` —
  `engine/render/TerraWeaveVirtualTextureFeedback.h`: fail closed
  for unrepresentable mapping address modes.
**Documentation commits:**
- `1f9b2163f0f83c7cc2c46deb8c0a408d49a55f5e` — `docs/texture-rhi.md`.
- `94196fadfe37aa4a1e27470c3cf1c65eeb47f1f4` —
  `skills/visuals/aiverse-texture-rhi/SKILL.md`.

**Public API & invariants:** feedback helper overloads accept two
typed sampler address modes; old normalized six-parameter CPU / five-
parameter GLSL/MSL calls remain. App-built material maps default to
clamp. TerraWeave native archives automatically supply modes from
active typed Version to single/multi-domain feedback. Rigid feedback
U0 appended float4 address modes; deformed U6 appended float4; no
regular material/authoritative ECS layout changes. Feedback shaders
apply wrap to page ID only; unwrapped UV derivatives still choose
automatic mip, avoiding fract-related false LOD seams.

**Checks actually performed:** fetched actual feedback/rigid/deformed
program source and uniforms; strict one-occurrence edits; re-fetched
all six changed files and checked generated source fragments and ABI
constants. GitHub accepted all commits. **C++ compile NOT RUN,
CTest NOT RUN, GLSL/MSL compilers NOT RUN, Metal/GL/Vulkan readback
and render tests NOT RUN.** No active CI pass evidence. Keep draft.

**Remaining limitations:** CPU `VirtualTexturePrefetchPlanner`
expects normalized 0–1 rectangles, so its visibility caller must
split UV regions crossing repeat/mirror edges. GPU feedback now has
explicit addressing but still needs native readback and seam tests.
Arbitrary border colors remain unsupported. Per-command mixed
VT resources for Metal ICB still require a distinct descriptor ABI.

**Next stage:** implement bounded wrapped UV-rectangle to normalized
visible-region splitting for CPU prefetch (before
`VirtualTexturePrefetchPlanner::plan`), with per-axis repeat/mirror
and identity for already-normalized clamp, or undertake GPU material
descriptor table design. During deferred testing verify negative UV,
edge equality, repeat/mirror, mip derivatives, rigid/deformed ABI
and backend shader compiler parity; do not merge until green.


## Stage 10R — bounded CPU repeat/mirror UV-region demand prefetch (2026-11-19)

**Branch:** `feature/texture-rhi-unification`, PR #21 draft.
**Source commits:**
- `d4043e9589034687b0bced4e0c63f2711d9dcba0` —
  `engine/gfx/VirtualTexturePrefetchWrap.h`, new CPU-only normalized
  rectangle splitter with independent Clamp/Repeat/MirrorRepeat
  address modes, negative UV handling and output-count limit.
- `16676aa4d4e59deb9a0dd9028c48674234839fe8` —
  `engine/gfx/VirtualTexturePrefetchAdapter.h`, new optional
  `prefetchWrappedVirtualPages` integration using existing planner,
  streamer and exact-resident page checks; a `wrapRejected` flag
  reports all-or-nothing validation failure.
**Docs commits:** `06863bd789be9aff2da5c81bebf6063b4fcfe090` (`docs/texture-rhi.md`),
`13d79346a969308700d6aebc43b44553599e4c4c` (`skills/visuals/aiverse-texture-rhi/SKILL.md`).

**API / architecture:** `normalizeWrappedVirtualTextureRegions` maps
each half-open projected UV rectangle to bounded normalized pieces by
splitting on integer repeat/mirror seams, merging fully covered wrap
periods into [0,1]. Separate U/V policies support mixed clamp/repeat.
Malformed coordinates, huge magnitudes, unsupported border modes or
output count overflow reject the whole batch. The optional prefetch
adapter feeds the successful result into the unchanged
`prefetchVisibleVirtualPages` coarse-to-fine demand scheduler with
existing CPU budgets and pending/resident suppression. No ECS,
filesystem side worker or GPU shader changes in this stage.

**Compatibility:** old normalized prefetch APIs remain; the extra
dispatch flag defaults to false. A caller selects active archive
address modes explicitly and must not mix domain page keys.

**Checks actually performed:** inspected prior
`VirtualTexturePrefetch.h` and adapter contracts, committed new
header and bounded adapter wiring on GitHub. **C++ compilation
NOT RUN, CTest NOT RUN, mixed-wrap visibility fixture NOT RUN,
Metal/GLSL/Vulkan tests NOT RUN.** No production certification.

**Limitations:** projected UV rectangles are conservative boxes
and can overfetch after rotation; a CPU visibility producer still
owns object/camera projection and per-domain selection. Partial
results are deliberately rejected when expansion exceeds the budget,
requiring caller subdivision or a GPU feedback path.

**Next:** complete virtual material descriptor-table design for
heterogeneous Metal ICB and investigate a scalable graph sampler
strategy beyond fixed 16-slot binding. Then execute deferred release
validation: compile native headers/Shader Graph, run texture CTests,
test Python/C++ manifest and wrap parity, and capture Metal/Vulkan/
GL screen-diff and page-request correctness. Keep PR draft until
all required checks have actually passed.


## Stage 10S — four-source Shader Graph VT sampling on Metal (2026-11-19)

**Branch/PR:** `feature/texture-rhi-unification`, #21 Draft.
**Feature commits:**
- `38a88994b08c06965c06a1d56cc5430213ccca24`:
  `engine/render/graph/ShaderGraph.cpp` removes the single
  independent source limit from `sampleVirtualTexture` nodes;
  original overall graph four-source cap still applies.
- `effe82d27578690addf6d00111ca1cdc8e85472b`:
  `engine/render/ModelVirtualTextureSampling.h` adds typed
  device-aware graph/VT slot planning with Metal extension to
  tex17/18; explicit opt-in to extended-slot shader injection.
- `ec7eeefce4a54682bbb03cba0d5d21ebce542f29`:
  `engine/render/Renderer3DModels.cpp` validates graph source
  availability, selects consistent shader/per-draw VT resources
  on Metal (four graph tex13..16 plus VT 17/18), and fails
  unrepresentable GL/Vulkan source textures without aliasing.
**Docs commits:**
- `cf9b0b6952161731d1093c3878c21f695d120bcc`: `docs/texture-rhi.md`.
- `82e0a78553bb66335725315d3b474eb193e24c32`: `skills/visuals/aiverse-texture-rhi/SKILL.md`.

**Architecture/API:** Source graph compiler remains independent of
backend; the renderer owns dynamic material texture slot allocation.
Metal supports 32 texture indices but only 16 dynamic samplers.
The fourth graph source at tex16 uses an existing constexpr sampler;
the two VT arrays use s1 and direct read, so no extra sampler state
is needed at tex17/18. On other backends the legacy 16-texture
budget is enforced, a graph VT node without spare arrays falls back
to ordinary tex1, and a graph with inaccessible source tex16 fails
rather than producing incorrect results. Built-in material VT,
existing MSL/GLSL text injection and non-graph models retain their
prior call signature and behavior.

**Actual verification:** GitHub source inspection of shader graph
compiler, model renderer and Metal setTexture (accepts 0..31,
sampler indices 0..15). All source/docs commits accepted.
**C++ compilation NOT RUN; CTest NOT RUN; Metal shader compiler
NOT RUN; GLES/Vulkan image parity NOT RUN.** Do not count this as
GPU-certified or merge-ready.

**Known limitations:** Metal graph material draws remain on the
normal CPU submission path; this is not a heterogeneous ICB resource
table or general bindless texture system. GL/Vulkan still have a
lower graph VT budget. Need verify MSL function signatures, graph
sampler s16 declaration, U6 uniform binding and fallback output.

**Next feature stage:** publish a first-class RHI fragment texture-slot
capability (default 16, Metal 32) and consume it in the graph
allocator, then design/implement a separate heterogeneous Metal ICB
descriptor ABI if safe; retain homogeneous fallback in the meantime.
During later test phase run real shader compilation, GPU screenshot
parity and negative 16-slot checks on GL/Vulkan.



## Stage 10T — explicit fragment texture slot budget (2026-11-19)

**Branch/PR:** `feature/texture-rhi-unification` / Draft #21.
**Code commits:**
- `383877a58e6e8430bb743b2612687c3ab81eba48` —
  `engine/gfx/Gfx.h`: virtual read-only
  `Device::maxFragmentTextureSlots()` (default 16).
- `7647e8b408970a959fddb755ef8ee0b0988cc30c` —
  `engine/gfx/metal/GfxMetal.mm`: Metal override returns 32
  available fragment texture indices (samplers still 0..15).
- `0137899195657d6c8e368f6c70d115d067aeac48` —
  `engine/gfx/vulkan/VkDevice.h`: Vulkan override returns
  `textureSlots_`, chosen from real descriptor limits.
- `15ebba5c2034f988bc898f36045a6232042c03c9` —
  `engine/render/ModelVirtualTextureSampling.h`: graph and
  VT allocation checks now use queried RHI resource count.
- `6852da8b5ffc82d18a905031edd9643ef49472e8` —
  `engine/render/Renderer3DModels.cpp`: explicit Metal
  extended slot opt-in additionally requires advertised
  resource count >16.
**Docs commits:**
- `cb6a972472da96fa23e7b446afa665c0bbdda239` (`docs/texture-rhi.md`).
- `e4589c6d3a7a653b49e4006adf97f1eed86b82f6` (`skills/visuals/aiverse-texture-rhi/SKILL.md`).

**API:** `Device::maxFragmentTextureSlots()` supplies an RHI
capability independent of genre; defaults to 16 on legacy GL,
returns 32 on existing Metal backend, and resolves physical
Vulkan descriptor capacity. It is a texture resource count,
not a count of dynamic fragment samplers. Graph compilation
remains generic while shader/per-draw binding uses the same
capability-aware layout helper. Legacy Device mocks retain
the 16-slot default, so source compatibility is preserved.

**Source inspection:** re-fetched all five changed files and
checked new method, backend overrides, resource layout guards.
Code/doc GitHub commits confirmed. **C++ compiler NOT RUN,
CTest NOT RUN, native MSL/GLSL/Vulkan shader validation NOT RUN,
GPU execution NOT RUN**. PR remains Draft.

**Risk:** Vulkan device reports limits of allocated descriptor
table; Metal's physical device should be checked for practical
per-stage resource limits on target hardware. Metal high-slot
sampler source uses constexpr state; no per-draw sampler
objects exist beyond index 15. Mixed Metal ICB command
resource tables remain unimplemented. Follow-on work should
never bypass capability checks.

**Next concrete task:** design a descriptor-identity and bounded
per-command Material Resource Table for experimental Metal
ICB that can associate every draw command with its own
VT atlas/table, U6 values and source texture references, without
conflating historical homogeneous groups. Alternatively, add
a separate opt-in graph texture-array table if resource
capacities and source dimensions match. Keep safe legacy
fallback and postpone release tests until feature completion.


## Stage 10U — shader graph cache identity parity (2026-11-19)

**Branch/PR:** `feature/texture-rhi-unification` / #21 Draft.
**Source commits:**
- `26e0ca8d83d964a048c545de1a2475b2e25c9580`:
  `engine/render/Renderer3D.h`, exact ModelShaderKey equality
  includes MSL fragment/vertex source, GLSL source, texture
  paths, outputs, graph domain and Virtual Texture feature flag,
  rather than comparing only GLSL, texture count and outputs.
- `c3fe1fe172571d66207bd4efd2987373df8e5c2a`:
  `engine/render/Renderer3D.h`, also include graph minQuality
  in exact cache identity.
**Documentation commits:**
- `8cc3303277c646cf767e8f498ffa6db86fe427cf` — `docs/texture-rhi.md`.
- `9bfef8fdecce4a31d50f40c4438edf9bb58d7d85` —
  `skills/visuals/aiverse-texture-rhi/SKILL.md`.

**Architecture/compatibility:** shader cache equality is
pointer-fast for same immutable graph and exact for distinct
graph instances. The full source and material shader capability
flags are now checked in renderer cache (not just a metadata
hash). Previously MSL-specific or VT-only variants with the
same GLSL snippets could share an incorrect shader entry.
This change preserves external material API and does not alter
ICB shader arguments or ECS authoritativeness.

**Checks actually performed:** fetched `ModelShaderKey` and
updated it through GitHub Contents; both source commits and docs
accepted. **C++ build NOT RUN, CTest NOT RUN, GLSL/MSL shader
compile NOT RUN, GPU smoke tests NOT RUN.** Do not infer success.

**Known limitations:** GPU material grouping and ICB commands
still need full resource identity checks at execution time;
the existing homogeneous VT grouping is intentionally not
broadened by this cache-key fix. There is no new per-command
VT descriptor binding.

**Next:** Stage 10V should add a bounded CPU-side
`GpuMaterialResourceTable` descriptor identity/payload model
(including up to four ordinary source textures, optional VT
atlas/table, layer/gutter/wrap, revision serial). It must support
deduplication and reject stale index generations. Do not enable
heterogeneous GPU ICB until Metal argument-buffer command ABI,
encoder pipeline and hardware checks are implemented together.


## Stage 10V — bounded GPU material resource table CPU staging (2026-11-19)

**Branch/PR:** `feature/texture-rhi-unification` / Draft #21.
**Source commit:** `55defc79e7872658ee610627bb87bb21f0a95742`
adds `engine/render/GpuMaterialResourceTable.h`.
**Documentation commits:**
- `aa8f8e1324141944ded9146b13752ee6a82beba0`: `docs/texture-rhi.md`.
- `b88bbfe8306eec5c73dcc52ed2dfdb42d8de7287`: `skills/visuals/aiverse-texture-rhi/SKILL.md`.

**Architecture/API:** generic CPU-only, genre-independent
`GpuMaterialResourceBinding` snapshots up to four graph source
handles, optional alpha mask and the exact VT atlas/table/layer/
gutter/wrap policy. `GpuMaterialResourceTable::beginFrame(serial)`
increments a generation and clears the bounded table.
`intern` deduplicates resource signatures only after exact equality
checks in hash buckets and returns a
`GpuMaterialResourceTicket{frameSerial,generation,index}`.
`resolve` copies a staged snapshot, `matches` compares it
with the current live draw just before submission, and
`reset` invalidates all existing tickets on shutdown/reload.

**Safety invariants:** no ownership of TextureHandles and no
GPU allocations; the caller still owns device fences, external
archive revision lifetimes, and command buffer retirement.
Invalid resource handles, excessive graph source count, full
tables or stale generation/frame tickets fail closed. The
class has no dependence on FPS/RTS or authoritative ECS.

**Actual checks:** source authored and GitHub accepted the new
header and documentation. **C++ compiler NOT RUN, CTest NOT RUN,
native consumer linking NOT RUN, GPU Metal/Vulkan/GL tests NOT RUN.**
This is not a production-ready ICB resource table.

**Remaining:** GPU ICB compute encoder currently encodes command
geometry and transforms only; inherited fragment material resources
are still homogeneous. No shader or Metal argument-buffer is yet
reading per-command descriptor indices. Do not claim that
heterogeneous batching is enabled simply because this header
exists. Existing conservative grouping remains active.

**Next stage:** wire the CPU staging table into experimental Metal
ICB prepare/revalidation as a strictly observational resource
snapshot (keep homogeneous checks). Then introduce a separate
opt-in Metal argument-buffer per-command resource-index ABI,
shader lookup, buffer residency/fence tracking and safe runtime
fallback. Cross-backend C++ compilation, tests, real GPU output
and source texture hot reload must be validated in later test phase.


## Stage 10W — Renderer3D observational material table integration (2026-11-19)

**Branch/PR:** `feature/texture-rhi-unification`, PR #21 Draft.
**Feature commits:**
- `2ba1ba90449f05f9d2ae1f655031a952aa7eb72b`
  `engine/render/Renderer3D.h`: include CPU material table,
  store one frame-scoped `GpuMaterialResourceTicket` per ICB group
  and own a bounded table in Renderer3D.
- `53ddaddde9468d64b2ee5c5e10f13a8fc3bc029b`
  `engine/render/Renderer3DModels.cpp`: new frame generation at
  GPU model preparation; snapshot exact homogeneous VT and alpha R8
  handles into the CPU table; reject groups whose ticket is invalid
  or whose live per-item signature does not match the captured one
  immediately before GPU execution.
- `e16b62b4f1cd57abc1b2ad21b6c95e50d25568a1`
  `engine/render/Renderer3DModels.cpp`: reset table and discard
  GPU batch tickets at model renderer shutdown.
**Documentation commits:**
- `a5414443f11281994072812344c8f081f27241f2` `docs/texture-rhi.md`.
- `9909dbff0ecd974c0473787ed238f80ab6f03c68`
  `skills/visuals/aiverse-texture-rhi/SKILL.md`.

**API/architecture:** No public RHI signature changes. Renderer3D's
existing experimental Metal compute-encoded ICB path is still fully
homogeneous; the new CPU ticket adds exact resource identity and
generation/frame ownership. `GpuMaterialResourceBinding` captures
the optional VT pair, layer, gutter/wrap and R8 coverage; graph source
textures remain absent because graphs are excluded from this path.
A failed `intern` does not submit a GPU group; no texture lifecycle
is controlled by this table. Render-phase validation invokes
`matches` for every item before calling the established
`drawModelBatch` / `executeGpuEncodedIndexedIcb` path. Any change
to archive identity, coverage, frame or staging generation falls back
to the ordinary CPU renderer. Only accepted table entries count as
`gpuIcbPrepared`. Shutdown invalidates all retained tickets.

**Actual checks:** inspected source preparation and execution code,
group-eligibility rules, shutdown path and public table ABI; source
and docs commits accepted by GitHub. **Native C++ compilation NOT
RUN; CTest NOT RUN; Metal ICB GPU execution NOT RUN; GL/Vulkan/
MSL shader validation NOT RUN.** No claim of measured improvement.

**Remaining:** no per-command GPU argument-buffer resource index,
no heterogeneous Metal ICB binding. Existing Metal PBR ICB still
binds one inherited parent material/VT group. The staging table is
only an additional fail-closed CPU identity gate and does not permit
combining different atlases. A prepared but rejected GPU token is
not executed; its normal backend frame allocator handles lifetime.

**Next concrete stage:** inspect `engine/gfx/metal/MetalGpuIcb.h` and
`engine/gfx/metal/GfxMetal.mm`, plus `ModelShaders.h`. Define a
new explicitly opt-in shader/encoder resource-index ABI that can bind
textures per GPU command without changing existing homogeneous
parent argument-buffer layout. Retain safe runtime fallback and
do not enable by default until real Metal shader compilation and
frame-fence/device tests pass.


## Stage 10X — opt-in GPU ICB per-command VT fragment ABI (2026-11-19)

**Branch/PR:** `feature/texture-rhi-unification` / Draft #21.
**Feature commits:**
- `a6c6630c3a82397a2b66e1989424a7d872f0cb99` —
  `engine/gfx/Gfx.h`: optional RHI descriptor
  `GpuIcbVirtualMaterial` and
  `prepareGpuEncodedIndexedIcbWithVirtualMaterials`; unsupported
  implementations return zero and leave old API unchanged.
- `f9b5e0f80da5daa68bfce9d9f531163c6270d179` —
  `engine/gfx/metal/GfxMetal.mm`: a source-level opt-in
  per-command VT path. The compute shader records per-command
  fragment buffer index8 (PBR argument table) and index7 (U6)
  pointers using aligned strides; legacy zero strides use old
  inherited data. CPU prepares native VT texture snapshots and
  validates handles, expected dimensions and atlas table layer.
  Execution fills per-command fragment argument-buffer offsets
  with the correct VT resource pair and optional R8 alpha, and
  rechecks native handle identity before submitting ICB.
- `16ec7c0fee754ec4c817306a4782c7e8f59e5943` —
  `engine/gfx/metal/GfxMetal.mm`: clarify bounds guard for
  per-command U6 uniform handling.
- `267c94221325f47b23e78a8b46154b9924e739a3` —
  `engine/render/Renderer3DModels.cpp`: explicit
  `AIVERSE_EXPERIMENTAL_ICB_PER_COMMAND_VT=1` opt-in;
  for already homogeneous VT groups submit a parallel array
  of `GpuIcbVirtualMaterial` descriptors; preserve legacy
  submission when flag is absent.
- `75ae78cd01c36c3f8db39265216516fea4ced9ec` —
  `engine/render/Renderer3D.h`: track per-command mode.
**Documentation commits:**
- `7601bbc03dc1b2299749d349780e8d7aea52dee4` `docs/texture-rhi.md`.
- `77511b72bf52ab39e5778a606a4e025ab7e10cee`
  `skills/visuals/aiverse-texture-rhi/SKILL.md`.

**API and architecture:** This introduces a separate opt-in Metal
GPU command-fragment binding layout that can point each encoded
command to independent PBR argument-buffer/U6 slices. It uses
PBR fragment texture argument IDs 26/28 for atlas/table, optional
30/31 for R8 alpha, and existing s1 material sampler. Each
material descriptor is validated before GPU encoding and again
against native TextureHandles immediately before execution. The
GPU compute kernel uses materialStride=4096 and vtStride=256
only when descriptors are present; otherwise both are zero,
preserving the established homogeneous ICB. No authoritative ECS,
genre dependency or new shader graph source texture binding.

**IMPORTANT:** The current group builder remains homogeneous and
the feature flag is disabled by default. The new GPU ABI has NOT
been exercised successfully on actual Metal, and source presence
alone does NOT establish a working heterogeneous batch.
The existing CPU `GpuMaterialResourceTable` ticket guard and
fallback remain active.

**Actually performed:** GitHub source inspection of Metal command
encoder and fragment resource packing; RHI/Metal/Renderer code
committed; static spot checks of changed method names, source
snippets and buffer offsets. **Objective-C++ native build NOT RUN;
MSL compile NOT RUN; CTest NOT RUN; GPU frame capture/ICB execution
NOT RUN; performance measurement NOT RUN.** Container cannot
resolve github.com for native checkout. Keep PR Draft.

**Risks:** Metal argument-buffer offsets and generated ICB buffer
pointer arithmetic have not been compiler/device verified. The
Metal hardware has historically crashed with experimental ICB,
so DO NOT turn the new flag on by default. Frame semaphore retains
per-command buffer resources; application still owns native
texture lifetimes and old VT revision GPU fence retirement.
Large group per-command PBR storage is bounded to 256 * 4096
bytes and U6 to 256 * 256 bytes; these are source-level bounds.

**Next stage:** after native MSL compiler / GPU ABI checks, allow
different *VT atlas/table bindings* inside one otherwise
pipeline/material-compatible group, using per-command resource
table tickets and retaining strict source-material equality.
Only then attempt different ordinary PBR/ShaderGraph source
textures, which require a separate material-state/graph ABI.
Any failure must use normal CPU per-item rendering without
dropping geometry.


## Stage 10Y — opt-in mixed VT archive GPU ICB grouping (2026-11-19)

**Branch/PR:** `feature/texture-rhi-unification` / Draft PR #21.
**Source commits:**
- `e58c4907655a3c72eecec9a21704a850e2fc2ba1`
  `engine/render/Renderer3D.h`: per-command resource ticket vector
  and scoped GPU-ICB-only material binding flag.
- `1e1f226c5bc3124f4e92b313cbb199ba9e58b74e`
  `engine/render/Renderer3DModels.cpp`: second opt-in
  `AIVERSE_EXPERIMENTAL_ICB_MIXED_VT=1`; hashed grouping can ignore
  per-draw VT external resource identity only when the per-command
  VT ABI flag is also enabled and both items require valid VT.
  Other source PBR/geometry/material/alpha state still goes through
  strict `equivalentGpuMaterial`.
  Per-command `GpuIcbVirtualMaterial` and CPU `GpuMaterialResourceTicket`
  snapshots are collected prior to GPU command encoding.
  Render revalidation compares every current draw against its own
  captured ticket; only prevalidated per-command ICB submission
  bypasses the original `drawModelBatch` homogeneous VT check.
**Docs commits:** `a20f9a063cb99da138216cdf7b4852a2ce419c3e` `docs/texture-rhi.md`,
`c9c51cd48c4e4b3cd3bf6817213b663ff7ee9958` `skills/visuals/aiverse-texture-rhi/SKILL.md`.

**API/architecture:** mixed VT is an optional new GPU batching
policy, independent of game genre/ECS authority. It is strict
about default paths: CPU MDI/instancing and legacy Metal ICB
still group only identical VT resources. Both feature gates
must be enabled *in addition* to the existing GPU ICB flags.
A group can contain multiple physical atlas/table pairs,
layers, gutters and address modes but shares the same built-in
material GPU state/geometry/pipeline. The Metal ICB kernel uses
Stage10X's per-command PBR argument-buffer and U6 pointers.
The CPU table tickets are also checked one-for-one immediately
before execution, including archive promotion and R8 alpha masks.

**Actual checks:** GitHub source inspection of group building,
cache preflight, per-command ticket staging, draw batch
homogeneous gate and execution path. Code and docs committed.
**Native C++ build NOT RUN; Metal MSL compiler NOT RUN; actual
Metal ICB GPU rendering NOT RUN; image parity and performance
NOT RUN.** No claim of production-grade batching; feature flags
MUST remain disabled by default.

**Remaining:** Different conventional PBR source textures/U2
material constants still are NOT mixed; Shader Graph is excluded
from GPU-authored ICB and retained on CPU. The new per-command
Metal buffer pointers and argument encoders must be compiled
and tested on hardware before claims of correctness/performance.
Experimental Metal ICB has a known historical AGX crash risk.

**Next:** introduce a fully bounded per-command Material U2/PBR
source texture resource ABI (or a resource palette) if practical,
without bypassing exact material-state grouping for unsupported
shader permutations. Instrument actual mixed/legacy ICB counters
and fallback reasons; after features close, run macOS Metal
Shader Compiler and GPU A/B tests on multiple Terra archive
domains. Keep draft until verified.


## Stage 10Z — distinguish real mixed Metal ICB executions (2026-11-19)

**Branch/PR:** `feature/texture-rhi-unification`, Draft #21.
**Feature commits:**
- `1bc8b0a27b4f922e7f2c05da849c9f896f181ecd`
  `engine/render/Renderer3D.h` extends
  `GpuModelDrawStats` with `perCommandVtExecutions` and
  `mixedVtExecutions` and records a mixed binding flag per
  prepared GPU command group.
- `43dcdde671fb37c005e6125acb3660c1505e77a6`
  `engine/render/Renderer3DModels.cpp` resets and increments
  these counters only after successful execution, not when a
  batch was merely prepared; mixed counts require at least
  two distinct VT archive binding identities within a group.
**Documentation commits:**
- `b4fc4fea0d11930decffabb904332593e319b1f4` `docs/texture-rhi.md`.
- `0700fcd3ae29a6898ef0371b218e4ba85152ece7`
  `skills/visuals/aiverse-texture-rhi/SKILL.md`.

**API/architecture:** these are read-only diagnostics of the
experimental source-level GPU ICB path, not new draw commands.
They complement `gpuIcbPrepared`, `gpuIcbRejected` and
`gpuIcbExecuteFailed` to distinguish CPU fallback from actual
GPU batch execution. Both optional execution counters reset on
a new `prepareGpuModelDraws` cycle and remain zero when
per-command VT is disabled.

**Checks performed:** GitHub source inspection of stats struct,
aggregate getter initialization, group flag and success-only
increment point. All commits accepted. **Native C++ compile
NOT RUN; Metal MSL compile NOT RUN; CTest NOT RUN; GPU execution
NOT RUN; screenshot parity/benchmark NOT RUN.** No performance
or pixel-equivalence claim.

**Remaining:** full GPU material/source-texture heterogeneity
still needs per-command material U2 and base PBR sampler ABI.
Existing mixed VT path is experimental and unverified; do not
ship with its flags enabled. The next AI agent should read
Stages 10W–10Z, then inspect `GfxMetal.mm`, `ModelShaders.h`
and `GpuMaterialResourceTable.h` before extending material
binding logic. The deferred test gate must include real Metal
command execution of 2+ VT archive domains and compare same
frame against CPU draws, including R8 masks, wrap, LOD and
archive promotion/fence safety.


## Stage 10AA — per-command Metal VT native format/extent guard (2026-11-19)

**Branch/PR:** `feature/texture-rhi-unification`, Draft #21.
**Source commit:** `b1a07b60b04979c93158d45713101c6121e37ebf`
in `engine/gfx/metal/GfxMetal.mm`.
**Documentation commits:** `bcb37262fb96e8e50d61e07a6e2cd2443edcba77` in
`docs/texture-rhi.md`; `23c76582da44c03b69292bf8fa03172c5dd8ded2` in
`skills/visuals/aiverse-texture-rhi/SKILL.md`.

**API/invariants:** The experimental per-command Metal ICB path
checks each physical atlas and page table as native
`TextureDimension::Texture2DArray` `PixelFormat::RGBA8`,
full-resolution optional coverage as 2D `PixelFormat::R8`,
array slice counts, virtual layer range and physical
gutter interior bounds. Unsupported resource combinations
return token zero before command encoding; the standard CPU
fallback stays enabled. No new RHI method or shader argument
layout is added. Other backends' default rejection remains.

**Checks actually performed:** inspected the native Metal
resource descriptor field names and added a bounded fail-closed
preflight check; GitHub accepted code and docs commits.
**Objective-C++ compiler NOT RUN; CTest NOT RUN; MSL compiler
NOT RUN; GPU ICB command execution NOT RUN**. No release claim.

**Known limitations:** Metal RGBA8 physical page samples only;
future half-float/other VT backing formats must have an
explicit matching shader/descriptor format contract. The path
is still feature-flagged, not used by default and not
hardware-certified. The broader heterogenous PBR U2/source
textures and Shader Graph material table are separate work.

**Next agent:** inspect all modifications from Stage 10W onward;
compile entire engine/editor against native Metal, run focused
texture and ICB suites and capture two-domain source-vs-VT
same-camera image comparison. If a native API/shader ABI
fails, fix under a new bounded stage and preserve this
append-only handoff before any merge.


## Stage 10AB — conservative opaque PBR pipeline equivalence and exact U2 resource ticket (2026-11-19)

**Branch:** `feature/texture-rhi-unification`; PR #21 Draft.
**Implementation commits:** `c9272ec945258a9ba0d401b38ff2c2f096237458`
(`engine/render/GpuMaterialState.h`) adds
`equivalentGpuIcbPbrPipelineMaterial` and
`gpuIcbPbrPipelineHash`, limited to nontransparent built-in
opaque PBR shaders with matching double-sided/shadow and
normal-map-presence pipeline flags. `06ad7d7be14e288dda6effce29383d59ac7c3555`
(`engine/render/GpuMaterialResourceTable.h`) extends resource
snapshots with six valid texture handles and 36 float U2 data
plus exact comparison/deduplication.

**Docs commits:** `f97991d14edde34a79c463a5359dc7206c7ec36b` (docs/texture-rhi.md) and
`741ca51c944215a804b0fedbeeaa8002e23602bd` (skills/visuals/aiverse-texture-rhi/SKILL.md).

**Architecture:** The pipeline grouping key is only a necessary
condition: hash collision never establishes equivalence, and
normal texture presence must match because it selects a shader
permutation. Source PBR texture handles and U2 become a separate
per-command identity. Frame/generation tickets remain unchanged,
and VT identity is included in the same exact resource table.
This stage does not itself change GPU resource binding or
enable batching.

**Checks actually performed:** read actual ModelShaders U2 layout
and Renderer3D material state/variant selection; GitHub source
and docs commits accepted. **C++ compile NOT RUN; CTest NOT RUN;
MSL compiler NOT RUN; GPU execution NOT RUN.**

**Remaining:** need a matching opt-in RHI/native Metal per-command
PBR descriptor, 256-byte U2 slice, per-command fragment texture
slots 1..6 and sampler states, and source-side per-draw U2
packing/revalidation before ICB submission. Keep legacy grouping
and CPU fallback unchanged until that feature is ready.


## Stage 10AC — per-command Metal PBR U2 and source texture ABI (2026-11-19)

**Branch:** `feature/texture-rhi-unification`, PR #21 Draft.
**Code commits:**
- `638c45bada0486845641526c14ea030c5788dd50`
  `engine/gfx/Gfx.h` expands the optional
  `GpuIcbVirtualMaterial` resource record with
  `hasPbr`, six source TextureHandles, and 36 U2 floats;
  checks all handles and finite factors.
- `df6968dcb56d681e21e2ce11392c7fb75db768a3`
  `engine/gfx/metal/GfxMetal.mm` extends the source-level
  Metal GPU ICB encoder kernel `Params` with
  `pbrU2Stride`, binds per-command U2 fragment buffer(3),
  captures exact six native source textures + samplers,
  allocates 256-byte U2 stride when hasPbr, and binds
  per-command fragment argument-buffer IDs 2..13 for
  PBR sources 1..6. Uses a 4096-byte argument-buffer
  slice per command and revalidates texture/sampler identity
  before execution. Legacy zero-stride behavior preserved.

**Docs commits:** `461010756be999510b0f6007023144f9a44c8683` in `docs/texture-rhi.md`;
`20b0dfd58283ce5e5cf85bba2c77a9fb54faf5a4` in `skills/visuals/aiverse-texture-rhi/SKILL.md`.

**Architecture/API:** RHI descriptor remains opt-in, with
original VT atlas/table required. PBR path is validated
all-or-none per GPU group; U2 and source textures may vary
per command, but the GPU pipeline and geometry remain shared.
The existing renderer has not yet populated these fields.
This ABI is independent of FPS/RTS and uses no authoritative
ECS material state.

**Checks actually performed:** examined ModelShaders' U2 and
ICB PBR argument buffer indices, existing kernel
pbrArguments binding and RHI texture interfaces. GitHub
code/docs commits accepted. **Objective-C++ build NOT RUN;
MSL compiler NOT RUN; CTest NOT RUN; actual GPU execution
NOT RUN; screenshot parity/performance NOT RUN**.

**Next:** add Renderer3D material U2 pack helper preserving
legacy material path, populate exact PBR handles/U2 for all
eligible commands behind a separate `AIVERSE_EXPERIMENTAL_ICB_MIXED_PBR`
flag, and ensure full U2/source texture identity revalidation
before execution. Only group materials with matching pipeline
traits from Stage 10AB; keep alpha-mask/transparent/graphs out.
Keep PR Draft, experiment flags OFF by default.


## Stage 10AD — opt-in heterogeneous opaque PBR material ICB integration (2026-11-19)

**Branch/PR:** `feature/texture-rhi-unification`, #21 Draft.
**Feature commits:**
- `270cfc374b77b42eaff39ac5a35a9cda6d7542b2`
  `engine/render/Renderer3DModels.cpp`: extract a canonical
  144-byte (nine float4) U2 packer and reuse it in original
  draw path. Add a helper for exact source texture slots 1..6
  with matching white/black/flat-normal fallback handles.
- `8dd1faa74460ea35863166230ffa3f611f0fa358`
  `engine/render/Renderer3DModels.cpp`: introduce
  `AIVERSE_EXPERIMENTAL_ICB_MIXED_PBR=1` requiring both prior
  per-command VT and mixed-VT flags. Group hashing and exact
  compatibility checks accept only pipeline-identical built-in
  opaque materials with valid VT, while allowing differing U2
  and PBR source textures. Per command, stage exact U2/6
  TextureHandles and their `GpuMaterialResourceTicket`.
  Rebuild live U2/source handles and compare exact tickets
  before GPU execution; pipeline structural flags are checked
  against the captured first-material snapshot.
- `dcff6078de6eb7bd1e2f5ac0b88ef9ffdcf179d9`
  `engine/render/Renderer3D.h`: track per-command PBR mode,
  source-payload variation and an immutable pipeline snapshot.

**Docs commits:** `20e7b963a40ca6addbf5cb5a46dc96a725ff7821` `docs/texture-rhi.md`,
`795d5da141fe540e30c99b1f6fb1c7cec433bd38` `skills/visuals/aiverse-texture-rhi/SKILL.md`.

**Architecture:** existing shader graph, transparent, cutout,
material palettes and non-VT models remain on the conservative
path. New grouping uses the Stage 10AB pipeline-only material
equivalence and Stage 10AC Metal ICB per-command U2/texture ABI.
Group geometry, shader permutation, flags and non-command
resources remain shared. Direct CPU/MDI grouping is unmodified.
Source texture fallbacks are identical to the legacy renderer,
and the U2 packer is single-source so both variants share fields.

**Actual checks:** fetched and examined Renderer3D draw state,
ModelShaders MU2 struct, material variants and Metal argument IDs;
strict per-file edits and GitHub commits accepted.
**C++ compilation NOT RUN; Objective-C++ NOT RUN; MSL compile
NOT RUN; CTest NOT RUN; GPU execution NOT RUN; A/B screenshots
and performance NOT RUN**. No release-readiness assertion.

**Known limitations:** currently heterogeneous PBR requires
valid VT materials for all commands (the new optional API
is built on GpuIcbVirtualMaterial). No regular non-VT mixed
PBR path, arbitrary Shader Graph texture semantics or alpha-mask
pipeline heterogeneity. Metal sampler-state pointer identity
is checked at native execution; graphics resource ownership
remains with renderer and device-fence retirement.

**Next:** inspect potentially stale parent PBR source bindings,
U2 array/pointer stride alignment and experimental per-command
validation under true Metal compiler. Add diagnostic counter
for *executed* mixed-PBR groups, then explore a non-VT
per-command material ABI, only without weakening regular
rendering paths. Defer cross-backend tests until feature closure
as requested, but keep PR Draft.


## Stage 10AE — mixed PBR ICB submission diagnostics (2026-11-19)

**Branch:** `feature/texture-rhi-unification`; Draft PR #21.
**Implementation commits:** `a4c2ca202046234b70b57291713201ab4e55a709`
(`engine/render/Renderer3D.h`) and
`0175030e6a175d458879bd18cc19ef2f6e11ec93`
(`engine/render/Renderer3DModels.cpp`) add two public
`GpuModelDrawStats` counters:
`perCommandPbrSubmissions` and `mixedPbrSubmissions`.
Only groups whose GPU ICB execution function returned true
increment these; mixed is set only if the group's per-command
PBR source handles or U2 payload differ. They reset
at frame draw preparation.

**Documentation commits:** `198c047178db65a62939875009d6ba1ab8f74b21` in
`docs/texture-rhi.md`, `7278101e4cbfd35015fea038d73c3532a6505ff8` in
`skills/visuals/aiverse-texture-rhi/SKILL.md`.

**Scope:** counts host-accepted GPU command submissions,
not command-buffer completion or image equivalence; no
new rendering path or pipeline relaxation in this stage.
CPU fallback and all experimental flags stay as before.
No RTS/FPS/ECS dependency.

**Actual checks:** GitHub source edits and docs committed;
read existing GPU ICB success point. **C++ build, CTest,
Metal shader compile, actual GPU execution all NOT RUN**.

**Remaining:** non-VT opaque PBR batching and Shader Graph
material descriptor support are not yet implemented.
Need real GPU captures/driver validation before any release.


## Stage 10AF — native Metal non-VT per-command PBR ABI (2026-11-19)

**Branch/PR:** `feature/texture-rhi-unification` / Draft #21.
**Code commits:**
- `d57c131c62b74b6937cc980375c0194e3746aec1`
  `engine/gfx/Gfx.h`: optional
  `GpuIcbPbrMaterial` with six TextureHandles and 36
  float U2, plus `Device::prepareGpuEncodedIndexedIcbWithPbrMaterials`
  (unsupported devices return zero).
- `635619138f135d45e985744a372170815c7a0df7`
  `engine/gfx/Gfx.h`: explicitly add
  `GpuIcbVirtualMaterial::hasVirtualTexture`, allowing
  the internal descriptor to be used for non-VT PBR only,
  never silently substitute absent VT resources.
- `1a68d2b1f6f42613d113dfc6a37db0d21da39842`
  `engine/gfx/metal/GfxMetal.mm`: opt-in non-VT material
  preparation converts the public PBR array into
  `hasVirtualTexture=false` internal per-command entries
  and stages the same PBR six texture/sampler resources and U2.
  VT U6 and physical array allocations are omitted in non-VT.
- `af527f2d0e4525e957b2d39698d268b039fb19fe`
  `engine/gfx/metal/GfxMetal.mm`: retrieve ICB resource
  mode before pipeline choice; select PBR argument-buffer
  variant for per-command PBR even with non-VT shader.
  Check VT shader mode matches per-command VT flag,
  skip unrelated atlas/table binding when non-VT, retain
  normal per-command PBR resource execution validation.

**Docs commits:** `d5c122e32d69d4953fbde31f5ee3739fa572db9c` docs/texture-rhi.md and
`35d7830ced30ef5d6f1a623531d276aa0c51a99b` skills/visuals/aiverse-texture-rhi/SKILL.md.

**Architecture:** separates optional native
`prepareGpuEncodedIndexedIcbWithPbrMaterials` from
virtual-material submission without changing the old
homogeneous API. Shader permutations for built-in opaque
PBR material are retained, only U2/source textures differ
per command. Non-VT mode has no VT array/U6 requirements.
Requires explicit opt-in `AIVERSE_EXPERIMENTAL_ICB_PER_COMMAND_PBR=1`.
Does not alter ECS state or default editor runtime.

**Actual checks:** GitHub inspection of Metal source
variant compilation and actual fragment argument-buffer
IDs, GPU ICB kernel/payload layout and code committed.
**C++/Objective-C++ build NOT RUN, CTest NOT RUN, Metal
shader compilation NOT RUN, GPU execution NOT RUN**.

**Next stage:** link Renderer3D to this new non-VT API under
`AIVERSE_EXPERIMENTAL_ICB_MIXED_PBR=1` and non-VT command
eligibility. Group only pipeline-identical opaque built-in
PBR, use the same U2/texture pack helper from Stage 10AD,
retain exact resource-table ticket checks and default
CPU fallback. Keep all experimental flags OFF until
real hardware validation.


## Stage 10AG — Renderer3D per-command opaque PBR without VT (2026-11-19)

**Branch/PR:** `feature/texture-rhi-unification`, #21 Draft.
**Source commits:**
- `ae6876c0d907a717d801ac827f8d11be79b879b8`
  `engine/render/Renderer3DModels.cpp`: independent
  non-VT PBR grouping gate requiring
  `AIVERSE_EXPERIMENTAL_ICB_MIXED_PBR=1` and
  `AIVERSE_EXPERIMENTAL_ICB_PER_COMMAND_PBR=1`.
  Existing per-command VT remains a separate opt-in,
  so ordinary PBR without atlas/page-table may submit
  through `prepareGpuEncodedIndexedIcbWithPbrMaterials`.
  Both paths snapshot exact U2 and six source handles
  into `GpuMaterialResourceTicket`, rechecking all
  live per-item payloads and pipeline traits before
  GPU execution. Fallback to legacy is preserved.
- `33a170003d15672e16dc23017c494ad84ddc5abf`
  `engine/render/Renderer3DModels.cpp`: pipeline-only
  grouping is permitted only if the candidate material
  is an opaque built-in non-graph material. Masked
  cutout/other unsupported variants retain exact
  old material grouping and are not disabled or merged
  unintentionally when experimental PBR mode is on.

**Docs commits:** `b95c2b6ae3086ea41933960a075173345e34dcc7` in `docs/texture-rhi.md`
and `f6deada1e089f1e2e00da0f9b26b24f5b1f564c8` in
`skills/visuals/aiverse-texture-rhi/SKILL.md`.

**API and rendering:** `Renderer3D` reuses canonical U2 packing
and six texture fallback handles from Stage 10AD.
The optional PBR non-VT Metal API is called only for
fully compatible opaque pipeline/geometry/flags.
When all commands have VT, the older separate per-command
VT resource API is used. Opaque normal-map presence is a
pipeline key bit; actual normal map texture/sampler handles
remain per-command values. Commands with mixed VT/non-VT
are not allowed in one group. Full MDI/direct draw, alpha
mask, transparent and Shader Graph paths retain original
exact material behavior and fallback.

**Actual checks:** inspected code path and converted all
source grouping/ticket paths with strict GitHub source
edits; code/docs commits accepted. **C++/Objective-C++
compilation NOT RUN, CTest NOT RUN, MSL compiler NOT RUN,
GPU command execution NOT RUN, performance/pixel parity
NOT RUN**. PR must stay Draft, experimental flags OFF.

**Known risks:** Metal PBR indirect shader variant must
compile on actual MTL hardware and use the argument-buffer
PBR slot layout correctly. The command stride for U2 is
256 bytes, and all source samplers must be resolved per
command. Frame-fence and resource lifetime management is
unchanged. Alpha mask and Shader Graph are still excluded
from heterogeneous PBR.

**Next stage:** review Metal non-VT GPU shader mode,
native argument-buffer offsets and ownership, then add
further guardrails/diagnostics around reasons for
per-command rejection. After functional completion,
run native Metal shader compilation and two-material
same-camera GPU vs CPU capture tests, with a normal
map and different sampler states. Do not merge until
real hardware validation passes.


## Stage 10AH — per-command Shader Graph source identities (2026-11-19)

**Branch/PR:** `feature/texture-rhi-unification` / #21 Draft.
**Source commits:**
- `a05cb086e5252beabd80da04fa9cc89296405e6f` in
  `engine/gfx/Gfx.h`: graphTextureCount+up to four exact graph
  source TextureHandles in `GpuIcbVirtualMaterial` and
  `GpuIcbPbrMaterial`. Bounded validation, no sparse slots.
- `9a08dc8d87c009f89b5c75778238bd2cf5e73ad4` in
  `engine/gfx/metal/GfxMetal.mm`: native preparation validates
  and snapshots source `Texture2D`, sampler state and original
  handles per command. The no-VT RHI entrypoint forwards the
  same fields; resource pool now stores the native graph pair arrays.

**Documentation commits:** `64d68c7920eaeeb2d6bf3bbe49431c1b1c18f6c6` in `docs/texture-rhi.md`
and `58d816cb83bcc2d9f7e4581596334cac5a95644a` in `skills/visuals/aiverse-texture-rhi/SKILL.md`.

**Architecture:** descriptive CPU/native-resource staging only.
Stage 10AH does not yet expose graph resources to GPU fragment MSL,
does not loosen Renderer3D eligibility, and does not enable a
graph pipeline in ICB. The existing genre-independent ResourceTable
tracks graph handles in exact equality and hash. RHI and native
resource limits remain bounded to four graph sources, with samples
limited by existing graph compiler policy.

**Actual checks:** GitHub file inspection, source commits accepted.
**C++/Objective-C++ compilation NOT RUN, CTest NOT RUN,
MSL compilation NOT RUN, GPU execution NOT RUN.** Do not
enable any graph ICB by default or imply visual correctness.

**Next:** create a distinct conditional AIVERSE_SG_ICB_GRAPH
Metal fragment argument buffer declaration for source slots 13..16
and variable VT atlas/table slots 13+N / 14+N; preserve the
legacy non-graph ICB layout. Source injection must hide direct
graph fragment arguments in an ICB variant and keep constexpr
sampler 16. Only then permit graph ICB shader preparation.


## Stage 10AI — Metal fragment Shader Graph ICB shader ABI (2026-11-19)

**Branch/PR:** `feature/texture-rhi-unification`, #21 Draft.
**Source commits:**
- `62f0a64781fa2f382687aab81a5ee233b8a4a14b` —
  `engine/render/ModelShaders.h`: conditional
  `IcbPbrTextures` argument buffer layout for up to four
  graph source slots at IDs 26/28/30/32 and optional VT
  atlas/table after graph slot N (13+N/14+N).
  Dynamic Metal samplers only for slots 13..15;
  source tex16 uses constexpr sampler. Old non-graph
  argument table remains.
- `dad58a1c93fbd9ed7326cd6221e896417254b68f` —
  `engine/render/Renderer3DModels.cpp`: generated
  `AIVERSE_SG_ICB_GRAPH` and `AIVERSE_SG_TEXTURE_COUNT`
  preprocessor metadata for fragment-only opaque graphs;
  guard direct graph fragment function parameters behind
  `!AIVERSE_ICB_PBR` and remap source graph sampler aliases
  to `icbTextures` in the ICB fragment variant.
- `c7739abe8c53587edb0960a3b49bd141c55cdc5b` —
  `engine/gfx/metal/GfxMetal.mm`: opt-in graph-specific
  PBR ICB fragment pipeline compilation, count parsing
  and bounds checks, per-command native graph resources
  and sampler validation/bindings; dynamic VT atlas/page
  table argument indices follow authored graph sources.

**Docs commits:** `0906abab6d7950d5a30048a73dc106c1c7a119bb` docs/texture-rhi.md;
`ece2a42f027e79307f7774e54b4df61728a0afbd`
skills/visuals/aiverse-texture-rhi/SKILL.md.

**Architecture/invariants:** no fallback or non-graph
shader path changed intentionally. Graphs with any
vertex output or unsupported material shader variant
are not marked for GPU ICB. Resource IDs in the
argument buffer follow the RHI texture slot convention
2*slot; the 4th graph source has no dynamic sampler
state in Metal. Metadata controls the MSL variant,
not serialized game-specific code. Shader Graph
`Program` itself remains backend agnostic.

**Actual verification:** inspected direct and ICB
ModelShaders.h/Renderer3D graph injection contract,
committed source and docs through GitHub. **C++
compile NOT RUN, Objective-C++ NOT RUN, MSL shader
compiler NOT RUN, CTest NOT RUN, Metal GPU execution
NOT RUN, image parity NOT RUN.** No release claim.

**Next:** opt-in only fragment-only opaque shader graphs
in `Renderer3D::prepareGpuModelDraws`, fill per-command
graph source handle arrays and generation-aware tickets
for both normal and VT-enabled graph layouts, and preserve
the existing CPU/legacy safe fallback. Then explicitly
exclude graph stages requiring direct vertex textures.


## Stage 10AJ — Renderer3D experimental fragment Shader Graph GPU ICB (2026-11-19)

**Branch/PR:** `feature/texture-rhi-unification` / Draft #21.
**Code commits:**
- `eeea5b1c7e4905d4ab4c87b8642153b3590dcc30`
  `engine/render/GpuMaterialState.h`: fragment-only shader
  graph eligibility; strict shader program/layout/output
  equivalence and a nonauthoritative hash prefilter for GPU ICB.
- `465f58a488a038efa0d235d758e8886adf2aa4f1`
  `engine/render/Renderer3DModels.cpp`: new explicit opt-in
  `AIVERSE_EXPERIMENTAL_ICB_SHADER_GRAPH=1`; conditional
  source graph admissibility, normal/VT graph source resolution
  and 0–4 command texture handles, shader pipeline-equivalent
  graph grouping, source ticket capture, exact per-item
  revalidation before ICB execution. No relaxation of ordinary
  CPU/MDI batch grouping.
- `86fb63821c446e481cf278c1b8d3259c13739c5b`:
  `engine/gfx/metal/GfxMetal.mm` explicitly includes
  `<string_view>` used for shader graph slot metadata.
- `be2c4fa02835f3c62eaefcdcb9bfc6530c73aa6b`:
  `engine/render/GpuMaterialResourceTable.h` rejects graph
  source handles without PBR/U2 binding.
- `75f8f4f6c934b99f1b979efe687ef0a40437b5d4`:
  `engine/render/Renderer3DModels.cpp` suppresses unused
  Metal vertex texture arguments for fragment-only graph
  variants, preserving GPU ICB vertex buffer ABI.
**Docs:** `cf6bb6625bf84cc21ba860a283ff9537fa7cffdf` docs/texture-rhi.md,
`b3d3a6ab83eb42bbb75415aed486ca11e969c94c` skills/visuals/aiverse-texture-rhi/SKILL.md.

**Architecture:** model material GPU ICB is still restricted
to opaque rigid geometry sharing compatible native pipeline
and geometry/index buffers. Fragment graph variants may
differ in texture resource *paths*, PBR source textures and
U2, but require byte-equivalent compiled graph program
code, source slot count, and output/quality/VT convention.
Up to four graph source handles are staged in the same
per-command descriptor and frame-scoped ticket; native
Metal captures and checks both texture and sampler objects.
VT-enabled graph atlas/table indices follow the source
count N as prepared by Stage 10AI. Vertex-output graphs
remain excluded; no new editor/genre-specific APIs.

**Actual checks:** compared real code for Renderer3D graph
permutation, source texture bindings, Metal argument-buffer
layout, and exact ResourceTable ticket validation; all
source/doc commits accepted in GitHub.
**C++ compilation NOT RUN, Objective-C++ NOT RUN,
MSL shader compilation NOT RUN, CTest NOT RUN, GPU ICB
execution NOT RUN, image parity NOT RUN, benchmark NOT RUN**.
No assertion of release readiness or GPU success.

**Known risks:** Metal pipeline compilation for experimental
graph ICB, vertex-program resource optimization and 4th
source constexpr-sampler behavior are still unverified.
If a shader variant cannot be prepared or submitted, the
existing CPU fallback must render the scene. The flags
remain OFF by default. Graph source paths must never be
used as a substitute for exact live resource handles.

**Next stage:** expose graph-specific accepted-submission
and genuinely different-source diagnostic counters. Add
RHI/Metal graph argument-table format/slot/layout assertions,
and audit shader code generation for 0/1/2/3/4 sources
and optional VT before running a native Metal compiler.
Do not move PR out of Draft without native and GPU evidence.


## Stage 10AK — host-accepted Shader Graph ICB diagnostic counters (2026-11-19)

**Branch/PR:** `feature/texture-rhi-unification` / Draft #21.
**Code commits:**
- `255d67eb9f2b52ca3ba467e9ea96f6ee4dd12274` —
  `engine/render/Renderer3D.h`: add public
  `shaderGraphIcbSubmissions` and
  `mixedGraphSourceSubmissions` to
  `GpuModelDrawStats`, plus corresponding counters and
  per-batch source identity flags.
- `cc740a5c5571a450e557e5c670ba6f32dd1118da` —
  `engine/render/Renderer3DModels.cpp`: detect whether
  a prepared group contains different graph source handles
  and increment the above counters only after
  `executeGpuEncodedIndexedIcb` returns true.

**Documentation commits:** `9a3c999ff51155a572421d095e79386ba6d67b04`
docs/texture-rhi.md; `3f1ce4395e16fa10de8068abd9a34a7aa0753e67`
skills/visuals/aiverse-texture-rhi/SKILL.md.

**API/architecture:** A host-accepted graph ICB submission
counter is not a GPU fence completion event. The mixed
graph source counter requires actual graph handle differences,
not only per-command U2/PBR textures. Existing stats and
default CPU renderer are unchanged; this stage is
genre independent.

**Actual checks:** GitHub accepted C++ header and source
edits and handoff docs. **C++/Objective-C++ compilation
NOT RUN; MSL shader compiler NOT RUN; CTest NOT RUN;
GPU command-buffer completion NOT RUN; image/perf
tests NOT RUN.** No claim of material correctness.

**Next:** audit the shader generator and native Metal graph
argument layout for count 0/1/2/3/4 and optional VT,
especially MSL preprocessor guards, high-slot constexpr
sampler 16, and fallback on shader/pipeline failure.
Record any source-level guard fixes separately; then
run the deferred native/macOS shader and GPU test phase
when requested. Keep the PR Draft.


## Stage 10AL — Shader Graph fragment resource slot guard + source matrix (2026-11-19)

**Branch/PR:** `feature/texture-rhi-unification`, Draft #21.
**Source commits:**
- `4506cc8cb3571d7050bb0a9ff14bb0c3a5e61115`
  `engine/render/Renderer3DModels.cpp`: GPU Graph eligibility
  rejects a graph when its fragment source indices or the
  optional VT atlas/table would exceed
  `Device::maxFragmentTextureSlots()`. Ordinary CPU
  drawing remains the fallback.
- `ef1014669efe1b05843b55afcef7b2cfaa1fefbb`
  `engine/gfx/metal/GfxMetal.mm`: independent native
  preflight checks the same max fragment texture budget
  for all optional 0..4 graph sources and VT arrays.

**Documentation commits:** `678f2bac22cf1b5801fec14acdccf5b765449015` docs/texture-rhi.md;
`5e57233e5f19ba421a415f98dad1deeb74f66eb7`
skills/visuals/aiverse-texture-rhi/SKILL.md.

**Source-level audit actually performed:** fetched the actual
`ModelShaders.h`, `GfxMetal.mm`, `Renderer3DModels.cpp`,
`Gfx.h` and `GpuMaterialResourceTable.h` HEAD blobs.
Compared the five explicit source-count MSL conditional
layouts N=0/1/2/3/4 to host atlas=13+N, table=14+N
and argument IDs twice the texture slot. All five
checks returned true; high-slot tex16 is constexpr
sampler only, native argument slot bounds and
graphTextureCount<=4 are present, ResourceTable hash
includes every graph TextureHandle.

**Important non-checks:** **C++ compilation NOT RUN;
Objective-C++ NOT RUN; MSL shader compiler NOT RUN;
CTest NOT RUN; Metal ICB execution NOT RUN; image
parity/performance NOT RUN**. Static source matching
does not validate MSL compiler or actual Metal resource
binding correctness. Do not advertise this as tests passing.

**Architecture / limits:** experimental graph ICB still
supports fragment-only opaque Shader Graph materials with
up to four source textures (plus up to two VT arrays).
GPU pipeline and geometry must remain compatible;
per-command handles are exact ticket/native-checked.
Graph vertex stage, masked/transparent graph materials,
more than four source textures and multi-graph shader
permutation heterogeneity remain outside this ABI.
Default is disabled.

**Next:** the functional graph ICB source/renderer/native
integration stages are present. Before merging, execute
native macOS builds and MSL compilation for graph source
counts N=0..4, both with and without VT. Test graph
resources with different sampler states, texture hot
reload, archive promotion/fence safety, non-graph PBR
parity and CPU/GPU same-frame screenshot diff. Use the
new host-accepted counters but require real GPU completion.
Keep Draft until evidence is green.


## Stage 10AM — Vulkan instance-format capability loader fix (2026-11-19)

**Branch/PR:** `feature/texture-rhi-unification` / draft PR #21. **Source commits:** `0ef03e79b44a34d9ce5946ff30e46a254935f34b engine/gfx/vulkan/VkApi.h`.

**Architecture/API and observed results:** The Linux arm64 GCC GitHub Actions job 135396475446 in run 44475967180 failed compiling `engine/gfx/vulkan/VkResources.cpp:577` because `vkGetPhysicalDeviceImageFormatProperties` was not declared under `VK_NO_PROTOTYPES`. The runtime Vulkan loader uses `AIVERSE_VK_INSTANCE_FUNCS` to declare, define, and obtain instance-level function pointers. This entry was missing.

`engine/gfx/vulkan/VkApi.h` now lists `vkGetPhysicalDeviceImageFormatProperties` alongside the other physical-device format queries. The existing `VkApi.cpp` macros automatically declare the runtime pointer, define it and require it during instance initialization. No unconditional Vulkan SDK symbol linkage is introduced.

**Observed evidence:** actual CI log, Linux arm64 GCC at 368/1314 objects, compile error as above. **Post-fix compile/CTest NOT RUN**; the failing run was for an older head SHA and must not be represented as evidence of this new commit passing.

**Documentation commits:** `b02678b55a819b23721d8ef6ced38c42c218eb1e` (`docs/texture-rhi.md`); `eaf8058b40e8c7c7024d2be2c2e7445e18042b45` (`skills/visuals/aiverse-texture-rhi/SKILL.md`).

**Actual tests:** See above. Do not infer CTest/native GPU pass from CI jobs belonging to an older SHA, static source checks, or the presence of a registered test. Current HEAD must be validated independently.

**Next concrete step:** Verify VkApi instance pointer resolves on Vulkan 1.0+ loaders and confirm Linux arm64 GCC compile progresses past VkResources.cpp.


## Stage 10AN — Metal graph tex16 fixed sampler parity gate (2026-11-19)

**Branch/PR:** `feature/texture-rhi-unification` / draft PR #21. **Source commits:** `a8b7f6451b00816d2afbbe2c03029eb0869dd249 engine/gfx/metal/GfxMetal.mm`.

**Architecture/API and observed results:** The fourth independently authored Shader Graph fragment texture occupies tex16. Metal has only sixteen dynamic fragment sampler slots; the current ICB MSL source uses a hard-coded constexpr `s16` with normalized coordinates, repeat address, linear filtering and linear mip sampling. Binding a clamp/nearest source there would silently change rendered texels from the ordinary renderer.

`MTTexture` now preserves the source `TextureDesc` sampler semantics (`samplerRepeat`, `samplerLinear`, `samplerCompare`) during `createTexture`. Experimental ICB resource staging validates source slot 16 against those original flags; it rejects nearest, clamp or compare sources rather than drawing with incorrect sampling. Graph sources 13..15 still use their dynamic native sampler states. Rejected ICB groups retain the normal CPU-render fallback.

**Verification:** source-level inspection of `GfxMetal.mm::createTexture` and `Model.cpp` texture upload and native sampler allocation. No Apple MSL compiler, Metal GPU or image parity run.

**Documentation commits:** `24f5377977d79ea99a8ae5e1e9bf111d61acce17` (`docs/texture-rhi.md`); `8b3ca0835c0e60940e3012f3fb461b1e351a8e8b` (`skills/visuals/aiverse-texture-rhi/SKILL.md`).

**Actual tests:** See above. Do not infer CTest/native GPU pass from CI jobs belonging to an older SHA, static source checks, or the presence of a registered test. Current HEAD must be validated independently.

**Next concrete step:** On macOS, compile MSL with source counts N=0..4; compare clamp/nearest fourth texture CPU fallback and repeat/linear fourth texture ICB on the same camera.


## Stage 10AO — Portable GPU ICB material contract test registered in CTest (2026-11-19)

**Branch/PR:** `feature/texture-rhi-unification` / draft PR #21. **Source commits:** `efe108acc0184cb4353e89e2f53aa37355d2a170 tests/render/gpu_icb_material_contract.cpp`; `63e7831f210ebfe2b5bc9f29201eaaed4e83bfe9 tests/render/gpu_icb_material_contract.cpp (portable NaN)`; `cd807bcee415401fc4968d7b287c591810610fc1 CMakeLists.txt`.

**Architecture/API and observed results:** Added `tests/render/gpu_icb_material_contract.cpp` and registered `aiverse_gpu_icb_material_contract_test` / CTest `gpu_icb_material_contract` under the existing `AIVERSE_BUILD_TESTS AND NOT EMSCRIPTEN` test target section. The test compiles against actual public engine headers `GpuMaterialResourceTable.h`, `GpuMaterialState.h` and their RHI dependencies.

The executable checks bounded exact deduplication; per-frame generation invalidation, even when a frame serial repeats; source/VT table/wrap/U2 identity changes; graph source counts, missing/tail handles, finite U2; native RHI descriptor validation; conservative opaque/masked/normalmap pipeline grouping; and fragment-only graph equivalence, including different authored source paths with byte-identical shader source. The test uses `std::numeric_limits<float>::quiet_NaN()` instead of a compiler-specific builtin.

**Actual check:** code added to GitHub and verified CMake registration exists. **The test was NOT built or executed in this environment** (the local container cannot resolve github.com and cannot clone the complete AIVERSE source tree); no pass count should be claimed.

**Documentation commits:** `476193bd471f2fc38924c29952c2ad9640e4fe3e` (`docs/texture-rhi.md`); `56330d69b076760ead99259cbafba1bc4362ec5c` (`skills/visuals/aiverse-texture-rhi/SKILL.md`).

**Actual tests:** See above. Do not infer CTest/native GPU pass from CI jobs belonging to an older SHA, static source checks, or the presence of a registered test. Current HEAD must be validated independently.

**Next concrete step:** Run the new C++ contract test through Actions and local Mac. Then run full Texture RHI focused verification and Metal GPU capture; fix any new errors before marking PR ready.


## Stage 10AP — wire Shader Graph ICB contract into focused CI verifier (2026-11-19)

**Branch/PR:** `feature/texture-rhi-unification` / Draft #21.
**Source commits:**
- `5917483125edade003d67308e48cc19c897598a4`,
  `tests/render/gpu_icb_material_contract.cpp`: use the actual
  `render::shaders::kModelMSL` generated shader text to audit
  five Shader Graph VT source counts N=0,1,2,3,4 and their
  atlas/page-table Metal texture argument IDs. Confirm
  source tex16 does not declare an illegal dynamic s16.
- `4d618d1a843da1117a0eef9b7f2045ee22952b37`,
  `tools/verify-texture-rhi.sh`: explicitly build the
  new `aiverse_gpu_icb_material_contract_test` target
  and include its CTest `gpu_icb_material_contract` in
  normal and `--vulkan` verification filters.

**Docs commits:** `ca38f3b232f3331c0ab9011888d1a100306407f3`
(`docs/texture-rhi.md`) and `2f76829462e6e140110557c61a4b97458c4f6bbc`
(`skills/visuals/aiverse-texture-rhi/SKILL.md`).

**Actual verification:** fetched current GitHub branch
`CMakeLists.txt`, `tests/render/gpu_icb_material_contract.cpp`,
`ModelShaders.h`, `tools/verify-texture-rhi.sh`.
Ran seven exact text-level assertions in tool orchestration:
all TRUE for build target, both CTest filters, registered
CTest, actual engine header inclusion, test source matrix
check, all five MSL branches, and portable NaN syntax.
This is a source-level validation only.

**Not run:** native compile of new test, CTest executable
execution, Objective-C++ Metal source compile, MSL shader
compiler, real Metal ICB submission and pixel parity.
Old GitHub Actions run `44475967111` texture jobs remain
queued, and the arm64 GCC CI failure from run
`44475967180` belongs to an older SHA; no post-fix
passing run exists for the current head.

**Next concrete step:** execute
`tools/verify-texture-rhi.sh --editor --vulkan --jobs 4`
on macOS; inspect `build/texture-rhi/verify-logs/`.
Or run the new target/CTest explicitly on Linux GCC/Clang.
Then verify macOS Metal graph/VT variants N=0..4 and
same-camera GPU-vs-CPU image parity before marking PR ready.


## Stage 10AQ — total CPU decode memory budget across mip chains (2026-11-19)

**Branch/PR:** `feature/texture-rhi-unification` / Draft PR #21.
**Code commits:**
- `70dfcb1887698934658fa9699356a75e62a26be3` — `engine/render/TextureAsset.h`.
- `dbb67e501182b62fd2257037c33261ea0bb04431` — `tests/render/texture_asset.cpp`.

**Documentation commits:**
- `4d6d22e22fbaf21d0b63b3d1812da08427804d35` — `docs/texture-rhi.md`.
- `3c181fc8134ae3aafb7384ac277f4fdf67c42b3a` — `skills/visuals/aiverse-texture-rhi/SKILL.md`.

**Functional change:** `render::TextureUploadOptions` gains an appended field
`maxCpuDecodedBytes` defaulting to 512 MiB. All three public upload aliases
now preflight the *sum* of all retained RGBA8 mip/layer/face/volume bytes when
using the existing generic CPU fallback. This complements the existing 512 MiB
per-level cap and uses overflow-safe division-based checks. Exceeding the
configured total fails before decoding any slice or allocating a CPU fallback
GPU texture. Zero disables CPU fallback; native compressed uploads are not
subject to this CPU-only byte budget. The option is appended with a default,
preserving existing source-level default and aggregate initialization patterns.
No serialized assets, ECS state, RHI formats or binary file format are changed.
The new limit counts retained RGBA8 bytes, NOT temporary decoder scratch.

**Regression source:** `texture_asset` CTest now describes a two-mip,
12-face cube-array payload decoded to 768+192=960 bytes, rejecting 959 bytes
before GPU creation, accepting exactly 960, and rejecting a zero budget.
Uses an independent mock device to preserve subsequent test counters.

**Actually performed:** GitHub accepted source/test/docs commits. Re-fetched
all four current branch blobs and checked 4/4 budget implementation markers,
4/4 regression markers, and both documentation stage markers: all present.
This is source-text verification only. `git ls-remote` from the Linux
container failed with `Could not resolve host: github.com`.

**NOT RUN:** native C++ compilation, `texture_asset` CTest, full CMake/CTest,
Metal/GL/Vulkan GPU execution, shader compilation, runtime memory profiling
and image parity. There is no new confirmed green CI result for this stage;
do not represent repository text checks as compiler/runtime tests.
PR #21 remains Draft and experimental Metal ICB flags remain OFF.

**Known limitations:** a CPU decoder may temporarily use additional
per-slice scratch buffers outside this retained-byte budget. Existing
500+ MiB individual image cap still applies. Caller-selected high budgets
are an explicit trust decision. Native compressed allocations and GPU
residency need independent device/driver limits.

**Next concrete step:** on a local macOS checkout, pull the latest branch
and run `bash tools/verify-texture-rhi.sh --editor --jobs 4` (add
`--vulkan` only with Vulkan SDK + driver). Confirm `texture_asset`
and `gpu_icb_material_contract` compile/pass, then validate actual
Metal graph/VT variants N=0..4 and CPU/GPU parity; resolve errors before
promoting PR #21 from Draft.


## Stage 10AR — sequential one-mip CPU fallback staging (2026-11-19)

**Branch/PR:** `feature/texture-rhi-unification` / Draft PR #21.

**Source commit:** `e570785a41a8e62db2193c8f65a889c086ab8ec1`,
`engine/render/TextureAsset.h`. **Documentation commits:**
`c2743e0df35799dc5dc05d1523055faca5ff7a99`
(`docs/texture-rhi.md`) and
`0ff39020d7b901e13a513535145ee86aa25636bc`
(`skills/visuals/aiverse-texture-rhi/SKILL.md`).

**Architecture / public API:** Generic DDS/KTX2 `uploadContainerTexture`
(and both public aliases) now decodes at most one full mip of RGBA8 fallback
data at a time instead of keeping a vector per mip. The whole logical decoded
mip-chain is still bounded by Stage 10AQ `maxCpuDecodedBytes` preflight
before CPU fallback allocation. The base mip, including all array/cube faces
and 3D volume slices, initializes the GPU texture; subsequent mips are
decoded, uploaded via `updateTextureRegion`, and the same scratch buffer
is reused. If later decoding or GPU upload fails, the partially created GPU
handle is destroyed and invalid returned. The native RHI methods must
synchronously consume source pointers, as required by their existing
contracts. Public API, source container layout, file formats, ECS and game
save semantics are unchanged. Peak retained CPU data should be no more
than the largest decoded mip, plus one slice decoder scratch and native
backend staging; this is **not measured**.

**Actual checks performed:** re-fetched the real updated branch header
and regression test source. Eight repository source consistency assertions
passed (single mip vector, no all-mip vector, preflight present, decode and
upload cleanup paths, base level allocation, 12-face mip coverage test, and
aggregate budget regression). This was static inspection **only**.

**NOT RUN:** any C++/Objective-C++ build, existing `texture_asset` CTest,
Metal/GL/Vulkan device execution, shader compilation, address/UB sanitizer,
peak RAM telemetry, image parity or performance measurement. The current
Linux execution container still cannot resolve github.com to clone AIVERSE;
successful GitHub file writes do not imply a passing native build or CI run.
PR #21 remains Draft.

**Known risk:** callers/backends violating the documented synchronous
source-byte consumption contract could retain a pointer into the reused
scratch buffer. Validate platform backends with sanitizer/device runs.
Compressed native uploads remain independent and unchanged. An individual
mip can still require up to the prior 512 MiB limit; no allocator telemetry
is exposed.

**Next concrete action:** pull `feature/texture-rhi-unification` on macOS,
run `bash tools/verify-texture-rhi.sh --editor --jobs 4`, verify the
`texture_asset` and `gpu_icb_material_contract` CTests, then capture
Metal/GL/Vulkan base-layer and authored-mip upload/readback parity with
a multi-face cube array and a volume image. Fix compiler or device errors
before promoting the PR; do not treat source assertions as test passes.


## Stage 10AS — macOS Bash verification script repair (2026-11-19)

**Branch:** `feature/texture-rhi-unification`. **PR:** Draft #21.
**Observed user failure:** after a successful `git pull --ff-only`,
`bash tools/verify-texture-rhi.sh --editor --jobs 4` printed
`Configuring CMake...` followed by
`tools/verify-texture-rhi.sh: line 62: cmake_flags[@]: unbound variable`.
It never completed CMake configuration and did not build or run tests.

**Code commit:** `e52728c4a0db07aedd41effe282d98f74acdb9b1`
(`tools/verify-texture-rhi.sh`). **Docs commit:**
`a1e9c1cd592b7b85c95af55697337d325d6499d2`
(`docs/texture-rhi.md`).

**Root cause & changes:** macOS ships an older Bash that can throw
`unbound variable` when an explicitly empty array is expanded with
`set -u`. The script previously interpolated
`"${cmake_flags[@]}"` directly with no extra `-D` flags.
Switched to the Bash-supported conditional
`"${cmake_flags[@]+"${cmake_flags[@]}"}"`, which passes zero
arguments when the array is empty while preserving the exact words
for supplied flags (including spaces). Inspection also uncovered a
pre-existing malformed and duplicated tail: the `ctest -N -R`
registration query had an unmatched single quote, swallowing a
later CTest invocation, and the final test/summary logic was
duplicated. Replaced it with one correctly closed optional shader
registration query, validator availability notes, one focused CTest
invocation, final error accounting and one summary message.

**Actually verified:** re-fetched the updated GitHub branch source
and checked the exact safe Bash expansion and repaired CTest/summary
markers; each appears where expected, with a single
`ctest --output-on-failure` occurrence. In an isolated local
GNU Bash 5.2 shell fixture (not the AIVERSE repo), executed
`bash -n /tmp/check_texture_rhi_flags.sh` (exit 0) and
`bash /tmp/check_texture_rhi_flags.sh` (exit 0);
zero optional arguments yielded exactly 5 mock CMake arguments,
and one flag containing spaces yielded 6 without splitting.
**The complete corrected script was NOT syntax-checked or run
with macOS Bash 3.2 in this environment.**

**Still NOT RUN:** repository-local `bash -n`, native CMake
configure, C++/Objective-C++ compilation, CTest, MSL/GLSL
shader compilation, real Metal/Vulkan/GL GPU runs or visual parity.
The only observed native run was the user's failure on the old script.
Do not mark current PR or stages as passing based on this repair.

**Next exact action:** on the Mac Studio:
```bash
cd ~/Projects/AIVERSE
git switch feature/texture-rhi-unification
git pull --ff-only origin feature/texture-rhi-unification
bash -n tools/verify-texture-rhi.sh
bash tools/verify-texture-rhi.sh --editor --jobs 4
```
If there is another failure, collect
`build/texture-rhi/verify-logs/{configure.log,build.log,ctest.log,summary.txt}`
and fix the first real error; always preserve user/other-agent changes
and append a new handoff after each bounded stage.


## Stage 10AT — first real macOS focused verifier failure inventory (2026-11-19)

**Branch/PR:** `feature/texture-rhi-unification` / Draft #21.
**Source tested:** `ddd09697901bb69a4a874a6f54ec337a0759160a`.
**User machine:** Mac Studio, isolated worktree
`/Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819`;
separate from the primary AIVERSE worktree, which remains mid-rebase.
**User-executed command:** `bash tools/verify-texture-rhi.sh --editor --jobs 4`,
after worktree `git pull --ff-only`, which reported already up to date.

**Observed real native run (reported terminal output):**
CMake `CONFIGURE PASS`.
13 focused targets reported `BUILD PASS`:
`aiverse_texture_contract_test`,
`aiverse_render_target_set_test`,
`aiverse_texture_streaming_test`,
`aiverse_texture_mip_residency_test`,
`aiverse_texture_resident_streamer_test`,
`aiverse_virtual_texture_pages_test`,
`aiverse_virtual_texture_page_table_test`,
`aiverse_virtual_texture_streamer_test`,
`aiverse_virtual_texture_disk_queue_test`,
`aiverse_virtual_texture_file_streamer_test`,
`aiverse_virtual_texture_prefetch_test`,
`aiverse_virtual_texture_feedback_test`,
`aiverse_exr_texture_test`.

8 targets reported `BUILD FAIL (exit 2)`:
`aiverse_virtual_texture_feedback_pass_test`,
`aiverse_gpu_icb_material_contract_test`,
`aiverse_ktx2_layers_test`,
`aiverse_ktx2_transcode_test`,
`aiverse_dds_layers_test`,
`aiverse_texture_asset_test`,
`aiverse_hdr_texture_test`,
`aiverse_editor`.
`CTEST FAIL (exit 8)`; `OVERALL FAIL: 9 build/test gates failed`.
`glslangValidator` and `xcrun` were both found on the host.
No claim of successful shader compilation or test execution.

**What is *not* yet known:** The user provided only the terminal
verifier summary, not the compiler diagnostics in
`build/texture-rhi/verify-logs/build.log` or the test failures in
`ctest.log`. Exit codes 2/8 are not root causes. It is premature
to rewrite texture source in response to the summary: multiple targets
may fail due to one shared compiler error, and CTest can fail because
test binaries were not built.

**Actions this stage:** checked GitHub PR #21 and workflow runs for
tested commit; no PR-triggered workflow runs for that exact SHA.
Confirmed local repository checkout is unavailable in this execution
environment (github.com DNS unavailable); GitHub connector can read
source files but cannot inspect the user's local logs.
No engine source changed in this inventory-only stage.
This entry itself is the required durable handoff.

**Next exact step:** inspect the FIRST build compiler/linker error
(with at least 10 lines of context), ideally each failing target
section in `build.log`, and the failing test diagnostic in
`ctest.log`. Correct the shared root cause in
`feature/texture-rhi-unification`, then append a new handoff and
re-run focused verifier in the isolated worktree; never disturb the
original mid-rebase AIVERSE checkout. Do not merge PR #21 until
native/GPU acceptance passes.


## Stage 10AU — native macOS Clang compilation blocker fixes (2026-11-19)

**Branch/PR:** `feature/texture-rhi-unification` / Draft #21.
**Input:** exact Mac Studio `build.log` and `ctest.log` from
`aiverse-texture-logs.tar.gz` collected for source HEAD
`ddd09697901bb69a4a874a6f54ec337a0759160a`.
Separate worktree
`/Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819`;
the main AIVERSE checkout still has an unrelated unresolved rebase.

**Code commits:**
- `f477dc4873118dcaa1dad326a86d70f837e5a1c4`
  `engine/render/ModelVirtualTextureSampling.h`:
  use `std::string` for `bodyAnchor` before calling `.size()`
  when inserting MSL VT argument-table source. Earlier Clang error:
  `member reference base type 'const char *const' is not a structure or union`.
- `ae375203a458c2996af8a527c95cd335235e23b5`
  `tests/render/virtual_texture_feedback_pass.cpp`:
  split `auto fb=feedbackCfg(),pf=prefetchCfg()` into two declarations
  because the return types are not identical (Clang line 199 error).
- `dd89fce304b4b16e9124771742b178e1c031c5f6`
  `tests/render/hdr_texture.cpp`:
  add `rleRun(data,code,value)` to append exactly two raw RLE bytes,
  replace five invalid calls to the four-component RGBe `pixel` helper.
  Preserves synthetic HDR fixture intended channel runs and malformed run.
**Documentation commit:** `6249c16d96fa113301b7ef8f0eb474c53c24fa9b`
(`docs/texture-rhi.md`).

**Architecture/compatibility:** no public RHI API, serialized data,
resource ownership, or genre/ECS behavior changed. The MSL source
generator now compiles as C++ because a `std::string` is used where
`.size()` is needed. The other changes are strictly in CPU regression
test sources.

**Actual verification:** read the actual user-supplied build log, grouped
compiler errors by unique file/line, and re-read the current GitHub source
before editing each location. GitHub accepted the changes.
**Post-fix native C++ compilation, HDR regression test, GPU ICB test,
editor build, CTest and GPU image validation have NOT RUN.**
The older test run recorded 13 successful builds, 8 failed builds and
15/23 passing tests; it is NOT a passing verification result for these
new commits.

**Separate runtime failure still open:** the older CTest log reported
`virtual_texture_file_streamer` as `FAILED` (not a missing executable),
reason `capture next frame after consuming result`. Investigate
the feedback mock/runtime callback lifecycle in the next bounded stage.

**Next:** investigate and fix the actual `virtual_texture_file_streamer`
runtime failure, record separate handoff; then pull the feature branch
into the isolated macOS worktree and rerun
`bash tools/verify-texture-rhi.sh --editor --jobs 4`.
Keep PR #21 Draft until native tests and real GPU results are verified.


## Stage 10AV — completed feedback mock-slot regression on macOS (2026-11-19)

**Branch/PR:** `feature/texture-rhi-unification` / Draft PR #21.
**Evidence:** Mac Studio focused verifier at
`ddd09697901bb69a4a874a6f54ec337a0759160a`:
`virtual_texture_file_streamer` genuinely **executed and failed**
at `capture next frame after consuming result`, unlike seven
`Not Run` cases from missing test binaries. Read the uploaded
`aiverse-texture-logs.tar.gz` CTest log and fetched the exact
`tests/render/virtual_texture_file_streamer.cpp` / real
`engine/gfx/VirtualTextureFeedbackRuntime.h` for diagnosis.

**Source commits:**
- `1f1fdc4887954ad537fe8b681e1a42d66cc031e6` —
  `tests/render/virtual_texture_file_streamer.cpp`:
  mock `finishFeedback` explicitly empties its
  `TextureReadFn feedbackCompletion` member after moving it to a
  local callback and **before invoking** it. No longer relies on the
  unspecified moved-from `std::function` state for clearing
  the one-outstanding-readback slot. Adds immediate assertion
  that mock callback slot was released after first completion.
- `1031e9e55a4f598615e63bcfeeb81e51c0e3cada` —
  same file: if repeated `runtime.capture` is still rejected,
  throw an error with the real `runtime.lastError()` and whether
  the mock callback slot is occupied; include `<string>`.
- `fa72cf48ba679f3eb9cf9f3e8ef1f6d53974881f` —
  `docs/texture-rhi.md` describes the mock callback state and
  verification status.

**Behavior/compatibility:** test fixture only, not production
RHI/runtime behavior, GPU API, ECS gameplay or serialization.
Callback lifetime remains weak-state invalidation safe for
owner shutdown. Mock reset models a device callback slot
becoming available at completion. The older assertion about
a second capture is retained and enriched with diagnostics.
No assertion was weakened.

**Actual checks:** full user-provided `ctest.log` was inspected;
current GitHub source was re-fetched and matching changes
confirmed. This is static verification of changed source text.
**NOT RUN after fix:** local/native C++ compilation, `ctest`
execution, macOS Metal GPU feedback, Vulkan/GL backend capture
or image parity. Do not mark test PASS or change PR Draft
without the Mac Studio rerun.

**Next exact action:** in the isolated verification worktree
`/Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819`
(which tracks `origin/feature/texture-rhi-unification` through
local `verify-texture-rhi-20261119-150819` branch), run
`git pull --ff-only origin feature/texture-rhi-unification`,
`bash -n tools/verify-texture-rhi.sh`, then
`bash tools/verify-texture-rhi.sh --editor --jobs 4`.
If a build/test fails, inspect the new `build.log` and
`ctest.log` and append another exact handoff.
Do NOT use the original primary worktree with unresolved rebase.


## Stage 10AW — second native macOS focused verifier result (2026-11-19)

**Branch/PR:** `feature/texture-rhi-unification` / Draft #21.
**Working copy:** isolated macOS worktree
`/Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819`
tracking `origin/feature/texture-rhi-unification`. User reported
`git pull --ff-only` returned "Already up to date" before running
`bash tools/verify-texture-rhi.sh --editor --jobs 4`. Last confirmed
remote HEAD before this report was `e86f9aea5d053b163a730139457c165f6be0f761`,
but this terminal paste did not include a fresh `git rev-parse HEAD`;
do not invent an exact local-tested SHA.

**Actual reported results:** `CONFIGURE PASS`; all 20 focused
test-executable targets printed `BUILD PASS` (including
`aiverse_virtual_texture_feedback_pass_test`,
`aiverse_gpu_icb_material_contract_test`,
`aiverse_ktx2_layers_test`, `aiverse_ktx2_transcode_test`,
`aiverse_dds_layers_test`, `aiverse_texture_asset_test`,
`aiverse_hdr_texture_test`, `aiverse_exr_texture_test`).
`aiverse_editor` printed `BUILD FAIL (exit 2)`.
GLSL and Metal validator executables were found, but the terminal
did not show whether validator tests passed. `CTEST FAIL (exit 8)`.
`OVERALL FAIL: 2 build/test gates failed`.

**Interpretation:** previous Clang compile regressions no longer
block the *focused texture test target builds* on this Mac.
This is real compilation evidence for the 20 named targets,
**not** evidence of passing CTest or the editor. Error code 2
does not identify the editor compiler/linker error; CTest
exit 8 does not specify the failed test names or assertions.
Earlier uploaded `aiverse-texture-logs.tar.gz` belongs to the
**previous** 13/20-target build, and cannot be used to diagnose
this new run.

**Changes:** documentation / handoff inventory only; no engine
or test source files changed to guess at a missing log.

**Needed evidence:** from the above isolated worktree, collect
`build/texture-rhi/verify-logs/build.log`,
`ctest.log`, `summary.txt`, `configure.log`.
Determine the first editor error from the `===== aiverse_editor =====`
section of `build.log`, and every `***Failed` CTest entry including
its assertion / stderr. Repair verified root causes in
`feature/texture-rhi-unification`; append another handoff and
rerun focused native verification.

**Unverified:** editor compile, all focused CTest results,
runtime GPU sampling/feedback, graphical parity, and mobile/Windows
cross-backend builds. Keep PR #21 Draft, do not merge based on
a successful test-target compilation alone. Do not touch the
original primary AIVERSE worktree with its unrelated unfinished rebase.


## Stage 10AX — macOS editor compiler and 3 remaining CTest regressions (2026-11-19)

**Working branch/PR:** `feature/texture-rhi-unification`, Draft PR #21.
**Observed baseline:** user ran `bash tools/verify-texture-rhi.sh --editor --jobs 4`
in isolated worktree `/Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819`.
CMake configuration and all 20 texture test target builds PASSED; editor
build FAILED (Clang `Renderer3DModels.cpp:2137:26: use of undeclared identifier
'pipelineMaterialCompatible'`). CTest executed 23 tests: 20 PASS, 3 FAIL:
`texture_asset` (failed native compressed allocation falls back to RGBA8),
`virtual_texture_file_streamer` (capture with single-frame budget),
`virtual_texture_feedback_pass` (shutdown destroys feedback target
independently of GPU callback). GLSL/MSL feedback shader tests PASSED
in the baseline run. No GPU-device visual parity was established.

**Source code commits:**
- `31184ad6ba0b86e611554d5c87c4e05f7b515c0d` —
  `engine/render/Renderer3DModels.cpp`: the original
  `pipelineMaterialCompatible` is a lambda local to
  `prepareGpuModelDraws()`, but the call at line 2137 is in another
  method. Replace that out-of-scope call with exact graph vs non-graph
  functions `equivalentGpuIcbGraphPipelineMaterial` and
  `equivalentGpuIcbPbrPipelineMaterial` for the frozen snapshot and
  current material. Still rejects incompatible GPU ICB submissions;
  no new opt-in flags or unchecked pipeline sharing.
- `a83c2b2644b470b039435d6acecf4699ebc8afb7` —
  `tests/render/texture_asset.cpp`: reset the injected
  `dev.failUpload` immediately after the intended mip-upload failure
  and leak rollback assertion. The later successful BC5 native-failure
  fallback must not inherit a prior artificial upload failure.
- `49ada8f7021ed136580c50a3387aeeac4e3c7542` —
  `tests/render/virtual_texture_file_streamer.cpp`: after extracting
  the pending callback with `std::move` for a late-after-shutdown
  delivery fixture, explicitly clear the mock callback member,
  asserting its pending slot is released. The subsequent reinitialize
  and next-frame capture should not depend on unspecified state of a
  moved-from `std::function`.
- `f51ba98628c65be0e57e4b433e6baf385753698f` and
  `f269126d3ac950bb01b7ed78e9d083e6f7824306` —
  `tests/render/virtual_texture_feedback_pass.cpp`: assert
  `gpu.created == gpu.destroyed` immediately after a deliberately
  rejected mismatched-config initialization; final shutdown checks
  no live texture handles, full creation/destruction balance, and
  at least the two known allocations (failed-init rollback and
  successful retry). The old literal `destroyed == 1` ignored
  the earlier valid allocation and rollback, producing a false failure.
- `2c5544edd6449023d87f020a3fa3bae3f8069b4f` —
  `docs/texture-rhi.md` describes fixes and unverified status.

**Architecture/API:** one renderer-path compile fix; three
regression fixture repairs. Public native RHI APIs, ECS, serialized
asset formats, production async callback lifecycle and material
fidelity requirements remain unchanged. The renderer still validates
pipeline equivalence on the GPU-encoded submission path and falls
back if resource snapshots no longer match.

**Actual verification:** fetched complete current source files in
GitHub for all four files, inspected control flow and the user's Clang
and CTest errors, applied exact edits, re-fetched each new blob and
checked 12/12 static source markers, all present. GitHub accepted all
source and docs commits.

**Post-fix NOT RUN:** native C++/ObjC++ compiler, editor link,
CTests including the three failures, GPU capture, image-parity tests,
memory/performance measurements. The *previous* 20/23 CTest result
does not verify the new commits. Keep PR #21 Draft.

**Next exact step:** in the **separate** texture-verify worktree,
execute:
```bash
cd /Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819
git pull --ff-only origin feature/texture-rhi-unification
bash -n tools/verify-texture-rhi.sh
bash tools/verify-texture-rhi.sh --editor --jobs 4
```
Read `build/texture-rhi/verify-logs/{build.log,ctest.log,summary.txt}`
if any test fails. Do not enter/modify the primary AIVERSE worktree
during its unrelated interrupted rebase.


## Stage 10AY — Mac Studio CTest gate green, editor gate still red (2026-11-19)

**Branch/PR:** `feature/texture-rhi-unification`, Draft PR #21.
**Known remote code HEAD preceding verification:** `3b922c015f63f768e1c2a72e106c7d78adcaf6e9`.
**Actual user environment:** isolated Mac Studio worktree
`/Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819`.
The pasted terminal output shows updated files and a rerun of
`bash tools/verify-texture-rhi.sh --editor --jobs 4`, but does NOT print
the tested `git rev-parse HEAD` or complete individual CTest results.

**Observed Mac build/test result (from user terminal):**
`CONFIGURE PASS`; **all 20** explicitly listed texture test
executable targets `BUILD PASS`, including `texture_asset`,
`virtual_texture_file_streamer`, `virtual_texture_feedback_pass`,
`gpu_icb_material_contract`, DDS/KTX2/HDR/EXR and streaming tests;
`BUILD aiverse_editor` **FAIL (exit 2)**;
`glslangValidator` and `xcrun` present;
`CTEST PASS` (exit 0);
overall verifier `OVERALL FAIL: 1 build/test gates failed`.
The test filter covers 23 named tests. No CTest log was provided on this
turn, so exact counts, skips and per-test runtime cannot be independently
confirmed from the terminal summary. GLSL/MSL validator presence alone
must not be confused with an executed and passing shader validation.

**Implications:** the three earlier failing CPU tests and all other
focused CTest checks no longer caused CTest to return nonzero in this
Mac run. Editor native compilation / link continues to fail.
A previous `Renderer3DModels.cpp:2137` undeclared lambda error
was fixed in Stage 10AX; do not assume it is the current error without
the **new** build log.

**Code changes:** none in this bounded diagnosis stage; avoiding
ungrounded edits to renderer, Metal RHI, CMake or other agents' systems.
Only this append-only handoff is committed. Do not merge PR #21.

**Not checked:** current editor compiler/linker first error, GPU rendering,
GPU ICB execution, true hardware feedback/readbacks, texture image parity,
Windows/Android/WebGL2 builds, benchmark/regression.
No post-commit native execution is performed from the assistant runtime.

**Next exact step:** in the isolated texture worktree, **do not**
rerun all 20 texture test builds. Inspect the latest compiler diagnostics:
```bash
cd /Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819
LOG=build/texture-rhi/verify-logs/build.log
sed -n '/===== aiverse_editor =====/,$p' "$LOG" |
  grep -nE -B 6 -A 12 'error:|fatal error:|Undefined symbols|ld:|FAILED:' |
  head -240
tail -100 "$LOG"
tail -50 build/texture-rhi/verify-logs/ctest.log
```
Supply the current editor compiler context to the next agent. If none
appears, run only `cmake --build build/texture-rhi --target aiverse_editor
--parallel 4`, capturing stderr. Fix the actual failing source,
record a new code SHA and separate stage handoff, then rerun the
`--editor` gate. Main AIVERSE worktree remains blocked in unrelated
unfinished rebase and must be left untouched.


## Stage 10AZ — Metal Texture3D CPU upload selector compile fix (2026-11-19)

**Branch/PR:** `feature/texture-rhi-unification`, Draft PR #21.
**Source commit:** `094bfe7c602fd57b8defa6bae2474a28cd04c3de`,
`engine/gfx/metal/GfxMetal.mm`.
**Docs commits:** `73045601bebf233adb3e315d136ff158834d63a0`
(`docs/texture-rhi.md`);
`5a5df0c21b167af1ff9034a2a56e8d8dd8499d16`
(`skills/visuals/aiverse-texture-rhi/SKILL.md`).

**Observed real macOS result at prior HEAD:** user supplied the end of
`build/texture-rhi/verify-logs/build.log` from isolated Mac Studio worktree
`/Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819`.
`aiverse_editor` failed building `engine/gfx/metal/GfxMetal.mm.o`
on Clang errors at lines 654 and 723:
`no known instance method for selector
'replaceRegion:mipmapLevel:withBytes:bytesPerRow:bytesPerImage:'`.
All 23 named focused CTests (including GLSL and MSL validation)
**PASSED** in the same user's actual run; CTest output explicitly says
100% tests passed, 0 failed, 23/23, 14.99 s. All 20 test executables
were built. The editor was NOT built successfully.

**Root cause:** Metal `MTLTexture` provides
`replaceRegion:mipmapLevel:slice:withBytes:bytesPerRow:bytesPerImage:`
for 3D uploads, not the legacy-looking selector without `slice:`.
Per Apple's documented API, for non-array/cube texture types (including
3D) the `slice` argument is 0. For a volume subregion, `origin.z`
and `depth` remain in `MTLRegionMake3D`; `bytesPerImage` is
the per-depth-slice stride. The initial level-zero 3D volume upload
and the `updateTextureRegion` 3D branch now explicitly include
`slice:0`. Existing 1D, 2D, 2D arrays and cube faces are unchanged.

**Compatibility:** no public RHI signature, texture format, serialization,
ECS or gameplay logic changes. The fix ensures native Objective-C selectors
match Metal's required interface while retaining existing depth-plane
pitch and mip/offset behavior. Not a proof of GPU-visible texel fidelity.

**Actually performed:** inspected the user-provided Clang diagnostics
and exact current GitHub `GfxMetal.mm` source; checked Apple's
Objective-C MTLTexture API documentation for the required selector,
slice semantics and `bytesPerImage`; GitHub accepted the source and
docs changes. **Post-fix C++/Objective-C++ compilation, editor link,
CTest and real-device texture3D update/readback were NOT RUN** in
this environment. No fabricated GPU or CI acceptance.

**Next exact step:** in the existing separate Mac Studio worktree
(not the primary repository, which is in an unrelated rebase):
```bash
cd /Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819
git pull --ff-only origin feature/texture-rhi-unification
cmake --build build/texture-rhi --target aiverse_editor --parallel 4
# If it passes, rerun the focused verifier:
bash tools/verify-texture-rhi.sh --editor --jobs 4
```
Inspect the new compiler/linker output if the editor still fails.
After macOS native validation, perform Metal 3D volume mip + partial
z-region upload/readback on an actual GPU, and separately validate
GL/Vulkan, Windows/mobile/WebGL2 support before marking PR ready.


## Stage 10BA — macOS source-verification gate green (2026-11-19)

**Branch / PR:** `feature/texture-rhi-unification`, Draft PR #21.
**Last source-fix checkpoint verified by user:** `79e8c04fc10074fc5c8723cbe47d77113d9025ba`.
**Documentation commit:** `f07a31faddfbe58074169ffa358ef526d7c7bcaa`
(`docs/texture-rhi.md`).
**Worktree:** isolated Mac Studio checkout
`/Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819`;
the original AIVERSE checkout remains in unrelated incomplete rebase.

**Actual user-executed commands and output:**
```text
cmake --build build/texture-rhi --target aiverse_editor --parallel 4
[100%] Built target aiverse_editor

bash tools/verify-texture-rhi.sh --editor --jobs 4
CONFIGURE PASS
... 20/20 focused texture test executable targets BUILD PASS ...
BUILD PASS aiverse_editor
GLSL validator: present
Metal validator: xcrun present
CTEST PASS (inspect log for SKIPPED tests and GPU execution)
OVERALL SOURCE TESTS PASS (GPU validation remains separate)
```
The prior full `ctest.log` excerpt provided by the same user explicitly
showed 23/23 tests PASSED (including GLSL and MSL validator tests). This
new terminal summary confirms the focused CTest invocation exited 0,
but the new detailed `ctest.log` was not attached and exact timing or
skips cannot be independently reconstructed.

**Validation outcome:** **PASS** for focused macOS native C++/Objective-C++
build, editor target link, and CTest source validation under the configured
test filter. This is the first confirmed full focused verifier pass
following Stage 10AZ's Metal Texture3D Objective-C selector correction.
No new engine code is required by this user result; this is a
documentation-only checkpoint.

**Remaining unverified work:** actual Metal GPU volume upload, mip/subresource
writes and readback; Metal sampled 3D/cube/array color parity; GPU VT feedback,
page-cache transitions and streaming under actual command completion; GPU ICB
heterogeneous fragment resource behavior; device loss/async readback lifetime;
performance/memory; and cross-platform Vulkan/GL/GLES/Windows/mobile/WebGL.
CPU CTest or MSL shader validation does not certify those GPU flows.

**Next concrete step:** implement or run a real Metal GPU acceptance
fixture that creates a Texture3D volume, writes a nonzero mip and partial
z-range via `updateTextureRegion`, samples/renders slices and asynchronously
reads back exact bytes. Also test 2DArray/Cube/CubeArray mip/face parity,
and confirm GPU page-feedback and Shader Graph ICB only where explicitly
enabled. Record actual GPU device, OS, shader/compiler results, visual
pixel output, limits and performance; keep the PR in Draft until validated.
Do not touch the main worktree during its unrelated rebase.


## Stage 10BB — native Metal GPU readback acceptance source (2026-11-19)

**Branch/PR:** `feature/texture-rhi-unification` / Draft PR #21.
**Input checkpoint:** Stage 10BA, Mac Studio native source gate PASS
(20/20 focused C++ targets, `aiverse_editor`, CTest PASS; prior
complete user CTest log 23/23 passed). This success belongs to an
**older source commit**, before the GPU acceptance work here.

**Source commits:**
- `29885e3c5883f14e32fed1f82de0ea9fb25f9c51` —
  NEW `tests/render/metal_texture_gpu.mm`: an opt-in ObjC++ fixture
  against production `gfx::createMetalDevice` / `GfxMetal.mm`, not
  a CPU mock. Uses a small CAMetalLayer on a Mac GPU, creates
  RGBA8 Texture3D 4×4×4 with unique Z-plane colors, uploads an
  authored mip 1 Z-plane and a mip 0 partial rectangle at Z=2,
  rejects invalid source pitch and out-of-range volume z,
  reads back exact GPU bytes/format/extent via asynchronous
  `Device::readTextureAsync` / real Metal blit and callback
  delivery in later `beginFrame`. Also tests Texture2DArray,
  Cube and CubeArray per-face source data, selected mip/face update,
  exact readback bytes, GPU command buffer health and 3D
  inventory creation/destruction.
- `df141da43973c9b442c252f482d8f897a285436c` —
  `CMakeLists.txt`: macOS-only target
  `aiverse_metal_texture_gpu_test` building the production
  `GfxMetal.mm` and fixture separately from the application
  entry point; CTest `metal_texture_gpu`, label `GPU;METAL;TEXTURE_RHI`,
  timeout 60 seconds.
- `c836975b1c5e80460fb608d86cb01cf365fc1c48` —
  `tools/verify-texture-rhi.sh`: optional `--metal` mode
  builds/runs real GPU acceptance, rejects non-macOS, uses
  `--no-tests=error` so an unregistered test cannot pass;
  regular source-only 23-case CTest filter unchanged.
- `87e2abf078c4efe93a4752900a64bf61ba970a38` —
  fixture: explicit `<cstddef>` include.
- `2fa91aafedfc1d5316a6e331f56f531dcfd2a7b3` —
  verifier: capture **verbose GPU device name and exact readback
  byte-comparison output** in `verify-logs/metal-gpu.log`.

**Documentation commits:**
- `efb75035a1ea3dd2fa63de3eaa6bcf0e5f353534` —
  `docs/texture-rhi.md`
- `c3f8c4590eab4f811d4f4d2300acfe09ac8c3119` —
  `skills/visuals/aiverse-texture-rhi/SKILL.md`

**Public API/compatibility:** no change to existing `gfx::Device`
or native shader ABI, ECS, game saves, texture asset format, scene
data or existing CPU verifier defaults. The new test is **opt-in**:
normal `bash tools/verify-texture-rhi.sh --editor --jobs 4`
does not execute the GPU fixture. To request actual Metal hardware
and force a red result if unavailable, append `--metal`.
The test has no RTS/FPS/genre dependency.

**Actual verification in this conversation:** re-fetched current
GitHub `GfxMetal.mm`, `Gfx.h`, CMake test block, verifier and
fixture; source-marker assertions 9/9, CMake markers 5/5,
verifier markers 7/7 were present. GitHub accepted all commits.
Only **static source/registration checks** have been performed
for this stage. **CMake configure NOT RUN; new ObjC++ target compile
NOT RUN; CTest `metal_texture_gpu` NOT RUN; actual GPU texture
upload/readback NOT RUN; no device name/image checksum is reported.**
No green hardware acceptance claim is authorized.

**Known risks/blockers:** the actual Mac SDK/compiler/linker may
require further changes to the standalone Metal test target.
The CAMetalLayer-based frame loop may behave differently in a
noninteractive macOS build runner; missing GPU/drawable must
fail explicitly, not silently skip. Current fixture proves the
RHI texture upload/blit readback path, NOT shader texture
sampling, GPU ICB, virtual texture feedback or full performance.
Other platforms (Vulkan, GL/GLES, Android, iOS, Windows,
WebGL2) remain independently unverified by this test.

**Next exact action, in the isolated Mac Studio texture worktree:**
```bash
cd /Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819
git pull --ff-only origin feature/texture-rhi-unification
bash -n tools/verify-texture-rhi.sh
bash tools/verify-texture-rhi.sh --editor --metal --jobs 4
cat build/texture-rhi/verify-logs/metal-gpu.log
```
If compile/test fails, record `build/texture-rhi/verify-logs`
(`build.log`, `ctest.log`, `metal-gpu.log`, `summary.txt`)
and fix actual compiler/device errors in the same branch, followed by
a separate handoff. The user's primary AIVERSE checkout has an
unrelated unfinished rebase and MUST remain untouched.
Do not promote PR #21 from Draft until GPU evidence is truly passing.


## Stage 10BC — explicit CMake opt-in hardware test registration (2026-11-19)

**Branch/PR:** `feature/texture-rhi-unification` / Draft #21.
**Prerequisite:** Stage 10BB real Metal texture GPU fixture source and
separate `--metal` verifier gate have been committed. Neither was
compiled nor executed on macOS in this tool session.

**Code commits:**
- `b08d22f63958088cdf5c10af1bc95df45dc0a382` —
  `CMakeLists.txt`: new
  `AIVERSE_TEXTURE_METAL_GPU_TESTS` option defaults to OFF;
  `aiverse_metal_texture_gpu_test` and its CTest
  `metal_texture_gpu` are registered only on macOS when both
  `AIVERSE_BUILD_TESTS` and that explicit option are ON.
- `46611e6a2a8890649d415fe092f88564a2b4df2d` —
  `tools/verify-texture-rhi.sh`: set the CMake option ON
  only for `--metal`, and force OFF otherwise, including a
  reused CMake build directory previously configured with
  `--metal`. The normal 23-case CTest filter is unchanged;
  hardware CTest is separate with `--no-tests=error`.
- `30efc33b816fe6d87e6562d19b3d1fb0139910b6` —
  `docs/texture-rhi.md`: document the opt-in CMake option
  and update manual configure commands.
- `7ebf5827f6df03e98309a467d5e75598f69e5a76` —
  `skills/visuals/aiverse-texture-rhi/SKILL.md`: explain
  that full default CTest must not run hardware tests.

**Reason:** adding a `metal_texture_gpu` CTest unconditionally on
every macOS build would make all native `ctest` runs depend on
a Metal device or GUI / layer drawable (even if the focused 23
test filter excluded that name). Hardware tests must instead be
explicitly selected, with no silent GPU-unavailable pass and no
unplanned CI failures.

**Compatibility:** no RHI API, production shader, ECS, asset,
or engine runtime changes. Default source-verification behavior
is preserved. Users opt in by running
`bash tools/verify-texture-rhi.sh --editor --metal --jobs 4`
in the existing isolated macOS worktree. The standalone CMake
alternative must configure
`-DAIVERSE_TEXTURE_METAL_GPU_TESTS=ON` explicitly.

**Actual verification:** GitHub accepted the CMake, verifier,
documentation and skill source updates. **CMake configure,
macOS ObjC++ compile/link, CTest execution and actual Metal
GPU readback have NOT been performed for these new commits**.
Previous Mac Studio editor/source CTest PASS belongs to an
older, GPU-fixture-free commit and must not be reused as
evidence of this stage passing.

**Next exact step:** in the isolated Mac Studio worktree,
run the opt-in `--metal` verifier, inspect detailed
`build/texture-rhi/verify-logs/metal-gpu.log`, and fix the first
compiler/linker/GPU error before marking any hardware gate
passed. Keep PR #21 Draft, preserve the user's main worktree's
unrelated incomplete rebase, and append a new handoff for
the next result.


## Stage 10BD — Metal verifier truncated Bash footer repair (2026-11-19)

**Working branch/PR:** `feature/texture-rhi-unification` / Draft PR #21.
**User test worktree:**
`/Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819`.
The separate primary AIVERSE worktree has an unrelated interrupted
rebase; no attempt to mutate it.

**Evidence of failure (real Mac Studio output):** user fast-forwarded
from `79e8c04fc` to `a81fdbad9`. `bash -n
tools/verify-texture-rhi.sh` reported at line 171
`unexpected EOF while looking for matching '\''` and at line 207
`syntax error: unexpected end of file`. Executing the malformed
script nonetheless configured CMake, built **all 20** focused texture
targets, built `aiverse_editor`, built the new
`aiverse_metal_texture_gpu_test`, and returned focused `CTEST PASS`.
It then encountered the same parser failure. No
`build/texture-rhi/verify-logs/metal-gpu.log` existed.
**The actual GPU CTest was NOT executed.**

**Root cause:** in `tools/verify-texture-rhi.sh`, line 171 of
the checked-out source ended with the *unterminated* regex
`-R '^metal_texture_gpu`, so the line was missing its
`$'` terminator and the output redirect. Lines 189–206 also
duplicated the final GPU result and summary block. A code review
that only checked for expected markers did not catch malformed
Bash syntax; Bash may execute earlier top-level blocks before
discovering a later unmatched quote.

**Code/CI commits:**
- `602fb8ee327146c95612a2b04519a5377796dc57` —
  `tools/verify-texture-rhi.sh`: replaced everything from the
  fail-closed Metal GPU CTest section to EOF with a single complete
  `ctest --test-dir "$build" --verbose --output-on-failure
  --no-tests=error -R '^metal_texture_gpu$' >"$logs/metal-gpu.log" 2>&1`
  command, CTest status capture and exactly one final error/summary
  section. No regular source test filters or target lists changed.
- `b90d50b0c1849b09790c52a82b4eaa269d37065f` —
  `.github/workflows/texture-rhi.yml`: run
  `bash -n tools/verify-texture-rhi.sh` immediately after checkout
  for Linux and Vulkan jobs, before building.
- `1a2db49ebd1426543eaab180bd63e913408a9c9a` —
  `tools/verify-texture-rhi.sh`: preflight full source using
  `bash -n "${BASH_SOURCE[0]}"` at startup; abort before CMake
  if a future footer contains a parser error. This is independent
  of optional `--metal` selection.
- `6baeded7ef0d42a51752568f9a396a9237a14810` —
  `docs/texture-rhi.md`: actual failure and exact run instructions.
- `c31da6a95aa1877a46317ec967546058ebcf5619` —
  `skills/visuals/aiverse-texture-rhi/SKILL.md`: instructions
  for syntax preflight and real Metal GPU acceptance.

**Source/behavior compatibility:** no GPU backend, texture asset
file, ECS/component, public native RHI API or test fixture modifications.
The `--metal` opt-in remains default OFF, fails closed for missing
GPU/test, and logs to `metal-gpu.log`. Normal 23-case source CTest
remains unchanged and does not imply hardware acceptance.

**Actually checked in this assistant environment:** re-fetched the
broken GitHub script and inspected its exact numbered tail; after
the fix re-fetched the new numbered tail to confirm the regex,
single final summary and no duplicate Metal branch. Pure static
checks confirmed one GPU command, complete regex, one status
assignment, one final GPU success message and trailing newline.
The **corrected footer snippet** independently passed
`bash -n` under Linux Bash (exit 0). The full fixed script was
not downloaded into this sandbox because GitHub raw content DNS
was unavailable here; **full-file `bash -n` and native macOS GPU
test are NOT RUN after the change**. The new CI syntax guards
were added but CI jobs are not claimed passing.

**Next concrete step:** in the Mac Studio isolated worktree:
```bash
cd /Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819
git pull --ff-only origin feature/texture-rhi-unification
bash -n tools/verify-texture-rhi.sh
# The test target was already built in the old malformed-script run.
ctest --test-dir build/texture-rhi --verbose --output-on-failure --no-tests=error -R '^metal_texture_gpu$'
```
Record device name, exact GPU readback comparisons and test exit code.
If pass, optional full verifier:
`bash tools/verify-texture-rhi.sh --editor --metal --jobs 4`.
If fail, attach first compiler/runtime error and next GPU handoff;
do not merge PR #21 without a real on-device passing acceptance.


## Stage 10BF — Metal GPU timing instrumentation (2026-11-19)

**Branch/PR:** `feature/texture-rhi-unification`, Draft PR #21.
**Source commit:** `50656c4bd120755e6fa14bf8d2774aeb77b7e752`,
`tests/render/metal_texture_gpu.mm`.
**Docs commits:** `4f7c922a29bf923868ad42553035e2c87c7ffcf5`
(`docs/texture-rhi.md`),
`f73906577736fe50c7be276f23296dca53966420`
(`skills/visuals/aiverse-texture-rhi/SKILL.md`).

**Motivation and real baseline:** user reported on Mac Studio
`metal_texture_gpu` CTest PASS 1/1 in 1.53 seconds and a full
`--editor --metal` verifier ending with
`OVERALL SOURCE AND METAL TEXTURE GPU TESTS PASS`.
They asked why the total was 1.53 s and instructed continued work.
Those PASS results apply to the **previous** fixture before this
timing instrumentation; no post-change timings are known.

**Implementation:** added monotonic `Clock` wall-time
measurement and tagged optional `Device::timeGpuFrame(tag)`
for each group's GPU readback command buffer.
Reports:
- `Metal device_and_layer_init_ms`: Metal device and CAMetalLayer,
  plus AIVERSE MetalDevice initialization;
- `Texture3D` and `Texture2DArray`, `Cube`, `CubeArray`
  `create_and_upload_ms`;
- one `TIMING` per group's actual GPU readbacks, splitting
  submit, callback wait, number of frame pumps, CPU comparison,
  and optional timestamped GPU *command-buffer* duration;
- `Metal fixture_total_ms` and `gpu_test_body_ms`.
`gpu_command_ms=unavailable` when no completed valid backend
timestamp is available. The GPU timing spans the whole command
buffer; it is **not** a pure upload or blit-throughput benchmark.
Wall-clock callback wait includes frame semaphore/drawable overhead
and 10ms sleeps. No fragile timing assertions added.
All existing byte-exact GPU assertions, CPU negative cases
and resource-lifetime tests are unchanged.

**Architecture/API:** test fixture only, no engine RHI behavior,
GPU shader/ICB ABI, ECS, serialized format, or CMake behavior changed.

**Actually performed:** GitHub connector fetched the existing
production fixture and `Gfx.h`/Metal timing API; created scoped
source update and inspected the changed source context.
**Post-commit Mac compile, CTest and GPU timing execution NOT RUN**
in this assistant environment. Previous 1.53 s cannot be
decomposed without a fresh instrumented run.

**Next step:** Mac Studio isolated worktree:
```bash
cd /Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819
git pull --ff-only origin feature/texture-rhi-unification
cmake --build build/texture-rhi --target aiverse_metal_texture_gpu_test --parallel 4
ctest --test-dir build/texture-rhi --verbose --output-on-failure --no-tests=error -R '^metal_texture_gpu$'
```
Record `TIMING` lines and GPU device name, compiler diagnostics
or remaining CTest failures; append a new agent handoff after
actual validation. Separately expand GPU acceptance to real
fragment shader texture sampling, GPU VT feedback/page-table
publication and GPU ICB permutations before PR promotion.
Never touch the main AIVERSE worktree during unrelated rebase.


## Stage 10BG — native fragment Texture3D shader sampling source (2026-11-19)

**Branch/PR:** `feature/texture-rhi-unification`, Draft PR #21.
**Implementation source commit:**
`9e747cff974d6f44085cc25cfc841198a6f1c309`
(`tests/render/metal_texture_gpu.mm`).
**Documentation commits:**
`94da868b67c3687b16617d866a3efe4ce6a2bf01`
(`docs/texture-rhi.md`) and
`36f7adcd52fb259aaf6476f89aed7803422931c2`
(`skills/visuals/aiverse-texture-rhi/SKILL.md`).

**Reason:** Stage 10BB/10BE's actual macOS Metal GPU
CTest PASS validated CPU->native upload, Metal command-buffer
blit readback, RGBA8 byte parity, volume mip/z and cube/array
subresources. It did **not** validate real shader texture sampling.
Stage 10BF added wall-clock + optional GPU command-buffer phase
timings but has not been rerun on the user's Mac Studio.
Continue by covering the shader sampling gap explicitly.

**Changes:** extends the opt-in real-metal
`tests/render/metal_texture_gpu.mm` test with the
`kVolumeSampleMSL` fullscreen triangle. Uses Metal's existing
`Device::createShader`, `createPipeline`, `beginRenderTargets`,
`setTexture(0,...)`, `setUniforms(0,...)`, `draw(3)`
and asynchronous target `readTextureAsync` contract.
The pixel shader samples an RGBA8 Texture3D and explicitly
selects lod from the uniform coordinate. Three output probes:
- original base slice Z0, mip0;
- changed rectangular area at Z2, mip0;
- authored mip1 slice Z1.
The output target is RGBA8 with a valid depth texture to
match Metal pipeline depth format. The 2D render target
readback checks the resulting pixel colors.
Original blit readback tests retain exact byte equality;
only normalized shader-sampled float->UNORM quantization
receives ±1 least-significant-byte tolerance. Reject GPU
shader/pipeline/pass setup failure and all output mismatch.
Adds `<cstdlib>` for integer absolute-difference handling.

**Architecture/API:** no production RHI, shader graph,
ICB, ECS, scene or serialized texture changes; test fixture
only, CMake option and `--metal` default-off behavior unchanged.
This tests a **direct Metal fragment texture3d sample**,
not Shader Graph compilation or actual ICB material mixing.

**Actually verified:** used GitHub connector to inspect
`Gfx.h` shader/pipeline/pass/setTexture APIs, native
`GfxMetal.mm` `setTexture` and depth/pass setup, and
existing MSL conventions in `ModelShaders.h` before
editing the fixture. GitHub accepted source and docs updates.
**Not run**: any native macOS build or on-device GPU readback
for 10BF/10BG since these modifications. The older
noninstrumented GPU PASS must not be treated as source
validation for either new stage.

**Next exact Mac Studio step:**
```bash
cd /Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819
git pull --ff-only origin feature/texture-rhi-unification
cmake --build build/texture-rhi --target aiverse_metal_texture_gpu_test --parallel 4
ctest --test-dir build/texture-rhi --verbose --output-on-failure --no-tests=error -R '^metal_texture_gpu$'
```
Inspect/record the `TIMING` lines, GPU device name, native
shader/pipeline error if any and exact per-pixel mismatches.
Do not claim success without the user's new CTest output.
After actual hardware acceptance, proceed with genre-independent
VT page-table feedback hardware test and GPU ICB sampling
permutations. Keep PR Draft until these separate acceptance gates
pass; do not touch the main AIVERSE worktree mid-rebase.


## Stage 10BE — confirmed native Metal GPU PASS (retrospective handoff, 2026-11-19)

**Recorded after 10BF/10BG to preserve the strictly append-only handoff.**
This is the previously confirmed acceptance for **older uninstrumented**
`metal_texture_gpu` fixture source, not evidence for newer changes.

**Source checkpoint:** `5ade89b546ec861c54377c7fb1c110e4db9136c3`,
Draft PR #21; user used Mac Studio isolated worktree
`/Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819`.
**Real user-reported native CTest result:**
```text
1/1 Test #122: metal_texture_gpu ... Passed 1.53 sec
GPU = 1.53 sec*proc
METAL = 1.53 sec*proc
TEXTURE_RHI = 1.53 sec*proc
100% tests passed, 0 tests failed out of 1
```
The user then supplied the full verification script's final output:
```text
CTEST PASS
Running native Metal GPU acceptance...
METAL GPU PASS
OVERALL SOURCE AND METAL TEXTURE GPU TESTS PASS (other GPU validation remains separate)
```
All 20 texture test target builds and `aiverse_editor` were reported PASS
earlier. This is genuine GPU execution confirmation for fixture
native Texture3D/2DArray/Cube/CubeArray uploads and exact blit readbacks,
but **not a microbenchmark, Shader Graph/ICB GPU result, hardware VT
feedback test, or proof of all cross-platform graphics support**.
The user's GPU model and per-byte verbose log were not supplied.

**Files changed at this documentation checkpoint:** none in production
or tests. The result was incorporated into `docs/texture-rhi.md`
via `71d4316fe7ab075fe17d5f28ae6752bf4340ab09`
and `0443e904f6d2ea2b618150952524ca1256c20365`.

**Next work:** Stage 10BF split CTest startup/upload/readback timing;
Stage 10BG adds actual fragment Texture3D sampling. Both are **source only
and must be recompiled and rerun on the Mac**. GPU Shader Graph ICB,
virtual page feedback and cross-platform parity remain separate
acceptance gates. Keep PR #21 Draft until these pass.


## Stage 10BH — Apple M3 Ultra native sampling and timing PASS (2026-11-19)

**Branch/PR:** `feature/texture-rhi-unification`, Draft PR #21.
**Preceding source stages:** 10BF (timing) and 10BG (real
Texture3D fragment sampling). **Documentation commit:**
`20cfa9f946186fec0dd67db11dbba43e34c4aba5`.
**Mac environment reported:** `Metal Apple M3 Ultra`,
isolated `AIVERSE-texture-verify-20261119-150819` worktree.

**Observed real GPU CTest:** `1/1 Test #122: metal_texture_gpu
... Passed 1.14 sec`, 0 failures.
User-supplied verbose output explicitly contained per-probe
`PASS` for exact 3D base z0, z2 patched region, mip1 z1,
2DArray first/last/mip1, Cube first/last/mip1, CubeArray
first/last/mip1 (all tolerance=0), and new real fragment
shader samples original z0, patched z2, authored mip1
(all tolerance=1). This verifies the new 10BF/10BG code
ran on a physical GPU. No assumed skipped test.

**Actual timings (milliseconds):**
- Metal device/layer init `67.186`.
- Texture3D create/upload `2.439`; 2DArray `0.411`;
  Cube `0.395`; CubeArray `0.531`.
- Texture3D MSL shader/pipeline setup `194.582`;
  fragment sampling total `302.001`.
- Entire fixture `480.275` (body after device init `413.089`);
  enclosing CTest wall clock `1.14 s`.
- Readback GPU command-buffer timings:
  Texture3D `0.007`, 2DArray `0.019`, Cube `0.021`,
  CubeArray `0.009`; fragment sample cases `0.005`,
  `0.006`, `0.005`.
  These are complete command-buffer GPU durations, **not**
  texture upload throughput or pure blit timings.
- Poll frame count `2` in each readback group;
  callback wait range `12.520..21.439 ms`;
  CPU-side submit range `4.100..9.129 ms`.
- The extra `~660 ms` between the reported CTest wall
  clock and fixture timestamp remains unmeasured process
  runtime overhead; do not invent a GPU bottleneck.

**Changed files in this bounded stage:** documentation only.
The previously described 10BF/10BG acceptance is now
verified on device. No production GPU texture code, shader
material ABI or genre/ECS files changed.

**Still unverified:** real virtual texture feedback shader
→ RGBA8 ID → async decoder → page request / physical atlas
publication; experimental per-command Metal Shader Graph
GPU ICB; actual large scene perf, device loss and GL/Vulkan/
Windows/Android/iOS/WebGL cross-backend parity.

**Next:** add separate real Metal VT feedback render/readback
acceptance using production `VirtualTextureFeedbackPass`
and canonical MSL encoder, then verify on Mac Studio. Keep
Draft PR and append a handoff after new source/actual tests.


## Stage 10BI — real Metal feedback shader RGBA8 acceptance (2026-11-19)

**Branch/PR:** `feature/texture-rhi-unification` / Draft PR #21.
**Source commit:** `5830d16f8531d462fa02709a0e6a0f8e5c4ef9f1`,
`tests/render/metal_texture_gpu.mm`.
**Docs:** `981f73fc2279c61d0b4a2baa74838bb160d035ca`,
`docs/texture-rhi.md`; and
`0db01c5e46f6b3c72a8f777060e7e2361e2757b1`,
`skills/visuals/aiverse-texture-rhi/SKILL.md`.

**Prior real GPU evidence:** Mac Studio Apple M3 Ultra,
`metal_texture_gpu` 1/1 PASS in 1.14 s; all exact Texture3D,
2DArray, Cube and CubeArray GPU readbacks and direct fragment
Texture3D sampling passed. This stage **adds new coverage** and is
NOT included in that previous successful run.

**Implementation/architecture:** Same opt-in real `GfxMetal.mm`
production backend fixture, no mock. New
`gpuVirtualTextureFeedback(Device&)` in
`tests/render/metal_texture_gpu.mm` uses:
1. Real `VirtualTextureFeedbackPass` with 4×4 RGBA8
   render target, depth attachment and bounded 8×8 base-page
   topology / 3 mip levels / 3 virtual layers;
2. New MSL fullscreen vertex shader + **canonical**
   `kVirtualTextureFeedbackMSL` and
   `kVirtualTextureFeedbackMaterialMSL` helpers,
   not a copy of the encoder;
3. Native `createShader`/`createPipeline` with
   `virtualTextureFeedbackPipelineDesc`; scene-independent
   rendering `pass.renderAndCapture`, including async
   Metal `readTextureAsync` readback and owner-thread
   `pass.readyToSubmit()`;
4. Additional explicit GPU `Read` probe to validate exact
   32-bit packed RGBA8 ID bytes (zero LSB tolerance) for
   `VirtualPageKey{1,3,2,2}` and the negative-U
   invalid/zero case;
5. CPU `VirtualTextureFeedbackDecoder` over those
   **actually GPU-rendered** pixels: 16 samples, one
   unique page with expected layer/mip/UV and priority16,
   or 16 invalid IDs/zero regions.

**Acceptance separation:** this verifies GPU feedback
shader encoding, RGBA8 color target and asynchronous
decoder behavior. It does NOT stage actual disk-page files,
allocate physical VT atlas pages, publish actual page
table textures, verify runtime GPU ICB multi-material
execution, or measure production frame-rate. No engine
ABI, ECS component, terrain, CMake or native shader graph
production code changed; only opt-in test and docs.

**Actually verified:** fetched the current
`VirtualTextureFeedback.h`,
`VirtualTextureFeedbackPass.h`, prefetch config,
and the native Metal backend's render pass, pipeline
and readback contract via connected GitHub, then
committed the concrete fixture. **NOT RUN for this stage:**
native macOS ObjC++ compilation, real Metal feedback
pixel test, GPU driver validation, source regression,
streaming acceptance or benchmark. Do not claim 10BI PASS
based on the earlier 10BH device results.

**Next step in isolated Mac Studio worktree (original main
worktree still has unrelated incomplete rebase):**
```bash
cd /Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819
git pull --ff-only origin feature/texture-rhi-unification
cmake --build build/texture-rhi --target aiverse_metal_texture_gpu_test --parallel 4
ctest --test-dir build/texture-rhi --verbose --output-on-failure --no-tests=error -R '^metal_texture_gpu$'
```
Collect first compiler error or real `FAIL` lines and
`TIMING` output for each GPU test; fix confirmed
failures in subsequent source stage and append another
handoff. When GPU VT encoding+decoding passes,
extend actual page-table / atlas publication and
Shader Graph ICB as separate opt-in gates.


## Stage 10BJ — Apple M3 Ultra Metal VT feedback PASS (2026-11-19)

**Branch/PR:** `feature/texture-rhi-unification`, Draft PR #21.
**Prior implementation:** Stage 10BI source
`5830d16f8531d462fa02709a0e6a0f8e5c4ef9f1`;
current acceptance record includes the new canonical Metal
VirtualTextureFeedbackPass/Msl encoder path. **Docs commit:**
`847de51dc3ea1b2aa6f89452f937671f37776cef`.

**Actual user-run hardware evidence:** Mac Studio native
`metal_texture_gpu` CTest #122 **PASSED (1/1, 0 failed)** in
`0.45 sec`; reported total CTest time 0.46 sec.
Verbose tail specifically included
`TIMING VT GPU feedback RGBA8 ID readbacks=1
submit_ms=7.758 callback_wait_ms=19.502 poll_frames=2
compare_ms=0.030 gpu_command_ms=0.005`,
`TIMING VT feedback GPU acceptance_total_ms=74.085` and
`TIMING Metal fixture_total_ms=393.549
gpu_test_body_ms=333.485`.
The CTest completed the extended GPU fixture; the supplied
excerpt did not contain individual valid/invalid feedback PASS
lines or a repeated GPU name. Earlier user-provided full
fixture output named `Metal Apple M3 Ultra`.

**Verified scope:** previously passed physical 3D/array/cube
uploads, fragment Texture3D sampling, and now the additional
opt-in GPU feedback shader/async readback/decoder assertion suite.
Test exit zero is grounded; no invented per-probe timings or
blit-only performance inference. Wall clock 450 ms includes
multiple separate GPU tests, not a single texture upload.

**Changes in this bounded stage:** only canonical docs and
this separately committed handoff; no engine/test source
changes required by the observed PASS.

**Not verified:** physical page upload/publish in VT atlas
and GPU page table after page-cache state transitions,
file-backed disk page scheduling, Shader Graph GPU ICB,
larger scene throughput, platform parity. PR remains Draft.

**Next:** add an opt-in real Metal test for
`VirtualTextureStreamer`:
actual stage/drain of a physical RGBA8 atlas slice,
GPU readback of the atlas slice and RGBA8 2DArray page
table, explicitly verifying no page-table visibility
before successful publication, fine-page override,
eviction and coarser fallback. No mock GPU and no
silently skipped CTest. After source commit, append
a separate handoff and ask for Mac Studio execution.


## Stage 10BK — native GPU physical VT atlas publication (2026-11-19)

**Branch/PR:** `feature/texture-rhi-unification`, Draft PR #21.
**Preceding confirmed acceptance:** Stage 10BJ: Apple M3 Ultra,
native opt-in `metal_texture_gpu` PASS (1/1) in 0.45 sec,
VT GPU feedback render/readback `gpu_command_ms=0.005`,
`callback_wait_ms=19.502`, stage `74.085 ms`.
This test did NOT yet include physical atlas/page-table publication.

**Source commits:**
- `d8b498d31d3b421261825048ab313bfefa13c6ee` —
  `tests/render/metal_texture_gpu.mm`: include
  `engine/gfx/VirtualTextureStreamer.h` and implement
  `gpuPhysicalVirtualAtlas(Device&)`, executing against
  actual Metal RHI `gfx::Device` (no mock).
- `3ca4b1cf872bab4a06d533aa75c4052d036ea41b` —
  refine the fixture's final PASS line to reflect
  physical VT atlas and page-table acceptance.

**Documentation commits:**
- `e072e71eecf19c23466db0f411a641bf3469ec35` —
  `docs/texture-rhi.md`.
- `8f55ceafdfb5e490a3047b0037d4ef381da461b5` —
  `skills/visuals/aiverse-texture-rhi/SKILL.md`.

**Architecture/API:** Uses existing genre-independent
`gfx::VirtualTextureStreamer`, which composes
`VirtualTexturePageCache`, `TextureStreamQueue` and
`VirtualTexturePageTable`. A 4×4 physical page, two-slice
RGBA8 Texture2DArray atlas and 4×4×1 RGBA8 page-table
Texture2DArray are created on **real Metal**.
The test validates:
1. `stage` of a pinned coarsest page
   `VirtualPageKey{2,0,0,0}`: before drain, no resident
   CPU page and actual GPU table remains all-zero invalid;
2. after `drain`, physical slot 0 bytes equal the authored
   page (RGBA8), GPU table all cells = `{0,0,2,255}`;
3. `stage/drain` detail `{0,3,3,0}`: physical slot 1
   bytes match source, GPU table contains `{1,0,0,255}`
   **only** at tile (3,3), with coarse fallback elsewhere;
4. `stage` replacement `{0,0,0,0}` with both physical
   slots occupied: pinned coarse survives, unpinned fine
   is LRU-evicted; **before** draining the new upload the
   actual GPU table has removed the stale fine mapping
   and points to coarse fallback;
5. after drain, the reused physical slot and new GPU table
   entry (0,0) contain replacement data, while tile (3,3)
   still falls back to coarse;
6. explicit eviction reverts the GPU table to coarse
   fallback; GPU resource inventory includes atlas/table
   before `shutdown` and excludes them after.
Exact-byte native async readbacks are used after each stage,
not just CPU cache state checks. A `TIMING VT physical atlas
publication_total_ms` marker is added.

**Actual verification:** fetched the exact production
`VirtualTextureStreamer.h`, `VirtualTexturePageTable.h`,
`VirtualTexturePages.h`, `TextureStreaming.h`, and Metal
fixture; GitHub accepted the updates.
**NOT RUN for this new stage:** native Mac compile, GPU
CTest, actual texture parity and post-change editor build.
No claim of GPU pass until the user's next CTest report.
The earlier 0.45 s PASS applies strictly to the previous
fixture that did not contain this code.

**Next on Mac Studio, isolated worktree only:**
```bash
cd /Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819
git pull --ff-only origin feature/texture-rhi-unification
cmake --build build/texture-rhi --target aiverse_metal_texture_gpu_test --parallel 4
ctest --test-dir build/texture-rhi --verbose --output-on-failure --no-tests=error -R '^metal_texture_gpu$'
```
Look for `PASS: VT GPU table invalidated before slot reuse`,
`PASS: VT GPU table after LRU republish` and
`PASS: VT GPU page table falls back after explicit eviction`.
Collect compiler error/CTest stderr and actual timing if failure;
repair and append a new separate handoff. Future tests:
actual disk file backed publication and fragment page-table
indirection / Shader Graph GPU ICB. Keep PR #21 Draft.
The primary AIVERSE worktree still has unrelated
unfinished rebase; do not touch it.


## Stage 10BL — real native Metal physical VT page-table PASS (2026-11-19)

**Branch/PR:** `feature/texture-rhi-unification`, Draft PR #21.
**Source acceptance baseline:** 10BK in opt-in
`tests/render/metal_texture_gpu.mm`, prior branch HEAD
`26f73151a92d0ada0cc76bc918175e3cdffc59b2`.
**Docs checkpoint:** `77944be3b06bb1a6de7ce93b12790405674d5c08`,
`docs/texture-rhi.md`.
**User execution context:** Mac Studio in isolated
`AIVERSE-texture-verify-20261119-150819` worktree,
previously identified native device `Metal Apple M3 Ultra`.

**Actually observed native GPU evidence:** user pasted verbose
`metal_texture_gpu` output with GPU exact RGBA8 PASS lines
for `VT GPU fine-page override`,
`VT GPU table invalidated before slot reuse`,
`VT overwritten physical page contains replacement`,
`VT GPU table after LRU republish`,
`VT GPU page table falls back after explicit eviction`.
All used **tolerance 0**. CTest #122 returned
`Passed 1.62 sec`, 100% (1/1), 0 failed, total CTest 1.63 s.
Last fixture PASS line explicitly mentions subresources,
fragment sampling, VT feedback/page table and async readback.

**Reported timings:** 177.399 ms for complete physical atlas
publication subtest; full fixture 1221.715 ms,
body 475.428 ms. Fine publish GPU command 0.018 ms;
old mapping invalidation 0.005 ms; LRU republish
0.006 ms; explicit eviction 0.005 ms. Corresponding
readback callback waits ~20 ms; those are owner-thread
frame/synchronization waits and must not be equated
with GPU physical-page copy time. No invented GPU benchmark.

**Behavior/architecture:** Existing production
`VirtualTextureStreamer`, `VirtualTexturePageTable`
and real Metal backend are now accepted for the specified
tiny page data, coarse/fine/LRU mapping/byte parity.
No production or test source modifications in this bounded
result stage. **Still unverified**: actual disk worker/file
payloads through `VirtualTextureFileStreamer`,
shader sample of the page-table indirection, material
Shader Graph GPU ICB and other GPU backends.

**Next concrete action:** implement an opt-in native Metal
file-backed streaming test with real temporary page files,
`VirtualTextureFileStreamer` worker, bounded owner-thread
`pump()`, exact atlas/page-table readbacks and cleanup.
Record source commit plus separate handoff. Do not edit
the primary AIVERSE worktree, which still has an unrelated
unfinished rebase, or merge Draft PR #21 before remaining
hardware acceptance.


## Stage 10BM — real disk worker to native Metal VT atlas source (2026-11-19)

**Branch/PR:** `feature/texture-rhi-unification`, Draft PR #21.
**Prior confirmed real GPU gate:** Stage 10BL: actual
`metal_texture_gpu` PASS (1/1) on Mac Studio in 1.62 sec,
including physical VT atlas/page-table LRU invalidation,
replacement and fallback with exact RGBA8 GPU readbacks.
This earlier PASS belongs to source HEAD
`26f73151a92d0ada0cc76bc918175e3cdffc59b2`,
before Stage 10BM's disk worker additions.

**Source commits:**
- `11ad74279ff2bbd348de16e4981efae645d7ab4c` —
  `tests/render/metal_texture_gpu.mm`: add production
  `VirtualTextureFileStreamer` integration GPU test,
  `VtDiskFixture` with temporary RGBA8 page files,
  bounded `pumpDiskPage` worker callback helper and
  `gpuDiskBackedVirtualAtlas` full scenario. Native
  `Device::readTextureAsync` verifies actual Metal bytes.
- `ebf5f1652c1284eaac6c7be5612def0a78e2c895` —
  `CMakeLists.txt`: explicitly link
  `Threads::Threads` for opt-in macOS Metal test target
  using a real `VirtualTextureDiskQueue` worker.
- `e8ba85b16a51eaa8794b67f29e2a6ad8503d1893` —
  `tests/render/metal_texture_gpu.mm`: distinguish
  fixture-write failure from other global GPU assertions,
  so unrelated prior test state cannot suppress this stage.

**Documentation commits:**
- `a07732cacdff9e7d596f7fa5a843be880f891a97` —
  `docs/texture-rhi.md`
- `8f01d2fd215d632e6a5c2224dd027f31336358cb` —
  `skills/visuals/aiverse-texture-rhi/SKILL.md`.

**Architecture/behavior:** test-only, genre-independent,
using existing `gfx::VirtualTextureFileStreamer`,
`VirtualTextureDiskQueue`, `VirtualTextureStreamer`
and real `GfxMetal.mm` backend. No new production API,
binary format, Shader Graph ABI or ECS dependencies.

**Exact test topology:** 4×4 mip0 virtual pages, 3 mips,
one virtual layer; two physical RGBA8 Texture2DArray atlas
slices, each 4×4 pixels; 4×4 RGBA8Array GPU page table.
Temporary `.rgba` files are written in the production
indexed `m<MIP>/l<LAYER>/x<X>_y<Y>.rgba` layout under a
uniquely generated OS temp directory; RAII removal.
The worker is actually started and reads files asynchronously.
The owner/render thread pumps with bounded 6s timeout per
ticket, verifying exact key, one callback and explicit
success/error status, with worker+upload timing/poll counts.

**Acceptance cases:**
1. Request pinned coarse `{2,0,0,0}`; GPU table
   readback remains all-zero before owner-thread `pump`;
   request is pending rather than resident.
2. Complete worker and GPU upload; physical slot0 contains
   exactly authored bytes; all 4×4 GPU table cells encode
   `{0,0,2,255}`, coarse pinned.
3. Request missing file `{0,1,1,0}`; worker completion
   reports nonempty failure reason, no new residency;
   streamer remains healthy and GPU table unchanged.
4. Load fine `{0,3,3,0}` into physical slot1; GPU
   page table (3,3) only changes to `{1,0,0,255}`.
5. Load replacement `{0,0,0,0}`, triggering one
   LRU eviction; coarse remains resident, old fine is
   evicted. GPU reads confirm slot0 unchanged,
   slot1 overwritten with replacement data, GPU table
   (0,0) maps slot1 while (3,3) falls back to coarse.
6. Shutdown clears both atlas/table GPU resource
   handles from live inventory.
This stage tests disk->GPU ownership and byte parity,
but not application shader page-table indirection,
hardware sparse residency, or GPU ICB.

**Actual verification performed by assistant:** inspected the
production queue and file streamer contracts in the
current GitHub branch, and committed fixture, CMake,
docs and skill updates via the connected GitHub tool.
**NOT RUN on Mac after Stage 10BM:** ObjC++ compile,
CTest and real disk-worker/native GPU path. Do not
claim this new stage passes based on Stage 10BL.

**Next exact user-run command in isolated Mac Studio worktree:**
```bash
cd /Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819
git pull --ff-only origin feature/texture-rhi-unification
cmake -S . -B build/texture-rhi -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_TEXTURE_METAL_GPU_TESTS=ON
cmake --build build/texture-rhi --target aiverse_metal_texture_gpu_test --parallel 4
ctest --test-dir build/texture-rhi --verbose --output-on-failure --no-tests=error -R '^metal_texture_gpu$'
```
On failure collect first compiler error or actual
`FAIL: disk VT ...` / `TIMING VT disk worker` diagnostic,
fix, and append new handoff. When this passes, create
fragment shader GPU page-table indirection acceptance,
then GPU ICB Shader Graph validation. Keep PR #21 Draft
and do not touch the primary repo's unrelated unfinished
rebase.


## Stage 10BN — verified real Metal disk-backed VT source to GPU acceptance (2026-11-19)

**Branch/PR:** `feature/texture-rhi-unification`; Draft PR #21.
**Previously implemented:** Stage 10BM added temporary RGBA8
asset files, native asynchronous `VirtualTextureDiskQueue` worker,
`VirtualTextureFileStreamer::pump` on the render thread, and
actual Metal physical atlas + page-table readbacks.
**Documentation commit:** `5d74f4f4d178116eb083a44936389e3bd226efca`
(`docs/texture-rhi.md`).

**Actual user-provided Mac Studio CTest output:** the new native
`aiverse_metal_texture_gpu_test` target built and linked
successfully. CTest `metal_texture_gpu` #122 PASSED
**1/1 in 1.46 seconds**, zero failures (overall CTest 1.47 sec).
Earlier full device identification: `Metal Apple M3 Ultra`.
The pasted verbose log shows every prior Texture3D,
2DArray, Cube and CubeArray exact readback, fragment
Texture3D sampling, canonical VT GPU feedback encoding,
and physical atlas page-table acceptance still PASS.

**Specific Stage 10BM real GPU PASS evidence:**
- Actual GPU page table all-zero before disk-worker `pump()`.
- Coarse `m2/l0/x0_y0.rgba` requested and loaded on the
  worker, mapped pinned physical slot 0; raw GPU pixels
  and RGBA8 table matched.
- Missing disk page returned explicit failed completion
  with atlas and page table intact.
- Fine `m0/l0/x3_y3.rgba` allocated physical slot 1;
  its authored page bytes and GPU fine override matched.
- Replacement `m0/l0/x0_y0.rgba` evicted the old
  unpinned fine page without disturbing pinned coarse;
  GPU atlas readback showed the overwritten physical slice
  contains the new bytes, and the page table discarded
  the stale fine mapping.
All reported readback checks used **tolerance 0**.

**Measured timing evidence:** full disk worker→GPU
stage `148.740 ms`, coarse read+upload `0.399 ms`,
missing failure `3.701 ms`, fine `3.980 ms`,
replacement `3.746 ms`. The displayed Metal GPU
command buffer durations for readback were
`0.005..0.024 ms`; CPU callback waits approximately
19–21 ms. Fixture total `694.571 ms`, CPU body
`640.120 ms`, enclosing CTest `1.46 sec`.
These intervals are not isolated upload bandwidth metrics.

**Changed files:** only documentation and this handoff
in this bounded validation stage; no source fixes were
necessary after the user-reported PASS.

**Remaining:** real fragment shader page-table indirection
over these disk-backed atlas resources, GPU ICB per-command
Shader Graph source material sampling, cross-backend GPU
parity and larger-scene performance. Stage 10BO begins
the shader-indirection source work; its new source has
NOT been built or run on Mac. Keep PR Draft.


## Stage 10BO — real Metal fragment page-table indirection (2026-11-19)

**Branch/PR:** `feature/texture-rhi-unification`, Draft PR #21.
**Prior real Mac Studio acceptance:** Stage 10BN actual native
`metal_texture_gpu` CTest PASS (1/1, 0 failed, 1.46 s),
confirming real `VirtualTextureFileStreamer` disk worker,
Metal atlas physical page bytes, GPU table and LRU eviction;
shader indirection was NOT covered by that earlier run.

**Source commit:** `32d69c8c6215ed470f9a1980bb212eb534eac1c5`
— only `tests/render/metal_texture_gpu.mm`.
**Documentation commits:**
`5d74f4f4d178116eb083a44936389e3bd226efca`
(`docs/texture-rhi.md`: Stage 10BN result and 10BO source),
`1d65245654d8c3e0c4903a95ad65a5921f95c21c`
(`skills/visuals/aiverse-texture-rhi/SKILL.md`: usage and scope).

**Source and architecture:** Added
`#include "engine/gfx/VirtualTextureSampling.h"` to the
existing isolated real-Metal GPU fixture. New
`VtAtlasFragmentProbe` constructs an actual native
`ShaderDesc`/Metal pipeline and 4x4 offscreen color+depth
render target, then appends the production canonical
`gfx::kVirtualTextureMSL` to a test fullscreen vertex/fragment
shader. The fragment invokes **the real**
`aiverse_sample_virtual(atlas,pageTable,sampler,uv,layer,
gutter,fallback,addressU,addressV)`. It takes the **live**
`VirtualTextureFileStreamer::physicalAtlas()` at Metal
texture slot 0, `pageTableTexture()` at slot 1 and
samples them via the real Metal `Device::setTexture` binding.
Uniform UV comes from `Device::setUniforms(0,...)`; draw uses
`beginRenderTargets`, `draw(3)`, `endPass`, followed by
native asynchronous RGBA8 render-target `readTextureAsync`.
Only this thin render test is new; production page lookup,
streamer and RHI are unchanged.

**Exact acceptance cases added to 10BM:**
1. Before disk worker publishes any page: fine UV (0.875,0.875)
   yields fallback `RGBA8{18,79,181,255}`.
2. After pinned coarse file-backed GPU publication: both
   base UV (0.125,0.125) and fine UV yield source page
   `{18,71,93,255}`.
3. A missing-file worker error must retain coarse physical
   sampling at another UV (0.375,0.375).
4. After fine page publication at mip0 tile (3,3), fine UV
   reads physical page `{82,196,41,255}` while a different
   UV still reads coarse `{18,71,93,255}`.
5. After disk-backed LRU replacement at tile (0,0), the
   old fine UV reverts to coarse `{18,71,93,255}` and
   new tile UV reads the replacement physical slot
   `{220,44,16,255}`.
All output is read from a **real Metal render target**
(no mock), compared as RGBA8 with <=1 LSB/channel
float-to-UNORM tolerance; the existing direct native
blit tests still require **zero tolerance**.

**Failure safety:** Shader compilation, pipeline setup,
offscreen pass and readback failures increment test
failures. RAII `VtAtlasFragmentProbe` releases created
shader/pipeline/color/depth, with explicit shutdown
before releasing streamed atlas/page table. Test stays
behind macOS-only `AIVERSE_TEXTURE_METAL_GPU_TESTS=ON`
and `--metal`; no production API, ECS, shader material
ABI or CMake changes.

**Actual verification in this assistant environment:**
re-fetched `engine/gfx/VirtualTextureSampling.h`,
`ModelVirtualTextureSampling.h`, `GfxMetal.mm` and the
prior native GPU fixture via connected GitHub to check
the canonical signature and Metal binding contracts;
committed source and docs. **New Stage 10BO native
ObjC++ compilation, CTest and hardware pixels NOT RUN**.
Previously reported Stage 10BN device PASS does NOT
verify this new shader-sampling code. Don't merge Draft
PR until the new on-device gate passes.

**Next action, isolated Mac Studio texture worktree only:**
```bash
cd /Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819
git pull --ff-only origin feature/texture-rhi-unification
cmake --build build/texture-rhi --target aiverse_metal_texture_gpu_test --parallel 4
ctest --test-dir build/texture-rhi --verbose --output-on-failure --no-tests=error -R '^metal_texture_gpu$'
```
On failure, report the first native shader/compiler or GPU
pixel mismatch with device name. On PASS, record exact
timing/label evidence and proceed to a separate **real Metal
Shader Graph GPU ICB heterogeneous source-material** acceptance
gate; cross-platform Vulkan/GL/Windows/mobile testing remains
independent. Do not touch primary AIVERSE worktree with
unrelated incomplete rebase.


## Stage 10BP — real Apple M3 Ultra Shader VT page lookup PASS (2026-11-19)

**Branch:** `feature/texture-rhi-unification`; Draft PR #21.
**Prior source:** Stage 10BO, native
`VtAtlasFragmentProbe` against production `gfx::kVirtualTextureMSL`
and live `VirtualTextureFileStreamer` atlas/table.
**Documentation result commit:**
`2dd9a7ec9b9eb9524dc2cf1a1cef933b760a50dc`
(`docs/texture-rhi.md`).

**Actually performed by user on native Apple M3 Ultra:**
provided full verbose `metal_texture_gpu` CTest log:
`Test #122 ... Passed 2.47 sec`, **1/1 PASS** (0 failed),
2.48 sec elapsed total. All eight shader indirection
readbacks showed `PASS` with 1 LSB pixel tolerance:
pre-publication nonresident fallback, pinned coarse sample,
fine address coarse fallback, unchanged coarse after an
intentionally missing page file, fine resident slice,
coarse elsewhere, post-LRU evicted fine fallback, and the
replacement page in the recycled physical slice.
All physical page bytes/page-table blit readbacks
continued to PASS at zero tolerance.
The reported shader lookup/pipeline setup was 251.017 ms,
the worker/atlas+shader suite 701.805 ms,
fixture 1301.235 ms, and selected GPU command intervals
~0.004–0.020 ms. CTest process elapsed time must
not be equated to texture lookup GPU duration.

**Verification boundary:** native source at the user's
last pull passed the test, no rendering/backend code
fixes needed. Hardware Shader Graph ICB material mixing,
sampler wrap permutations, Vulkan/GL/WebGL/mobile parity
and large-scene profiling remain separate, NOT RUN.
**Changes:** documentation/handoff only for this bounded PASS.
Keep Draft PR until remaining acceptance; primary
AIVERSE worktree remains in unrelated unfinished rebase.

**Next:** isolated opt-in test for production Metal
GPU-encoded ICB per-command Shader Graph PBR textures.
Do not conflate generic fragment texture rendering with
GPU ICB validation; must inspect actual
`Device::prepareGpuEncodedIndexedIcbWithPbrMaterials`
and `executeGpuEncodedIndexedIcb` and require
a nonzero GPU token + successful native execute and pixels.


## Stage 10BQ — isolated native Metal GPU ICB graph-source ABI test (2026-11-19)

**Branch/PR:** `feature/texture-rhi-unification`, Draft PR #21.
**Preceding real on-device acceptance:** Stage 10BP, Apple M3 Ultra
`metal_texture_gpu` **1/1 PASS** in 2.47 s, including eight
fragment shader samples via production VT page-table lookup
over real disk-backed physical pages and LRU transitions.
This prior PASS is NOT evidence for the experimental ICB test
added in 10BQ.

**Code commits:**
- `facd011f45efd11a6883b615003435c204c83300` —
  NEW `tests/render/metal_icb_graph_gpu.mm`.
  Test runs in its own process against actual AIVERSE
  `gfx::createMetalDevice` and the same production
  `engine/gfx/metal/GfxMetal.mm` as the editor.
- `6d9876960432d2db472e9bce704de014d9eef261` —
  `CMakeLists.txt`: new
  `AIVERSE_TEXTURE_METAL_ICB_GPU_TESTS=OFF` option;
  on macOS with explicit ON, build/link
  `aiverse_metal_texture_icb_graph_gpu_test`,
  register `metal_texture_icb_graph_gpu` in CTest,
  set `METAL;GPU;TEXTURE_RHI;EXPERIMENTAL_ICB` labels,
  90-second timeout, and CTest-scoped GPU ICB environment.
- `e577a2e696b24c213d4c9e480d2a5fc0e917722c` —
  add explicit `<cstddef>` for source pixel offsets.

**Documentation commits:**
- `731ee5c31c6472b9a8d67e610883d2d953377853` —
  `docs/texture-rhi.md`: contract, opt-in and exact commands.
- `5ee67eb7825dcced4ddab442113093a09c6ecebc` —
  `skills/visuals/aiverse-texture-rhi/SKILL.md`.

**Architecture / low-level GPU acceptance:** `GfxMetal.mm`
supports GPU-authored indexed ICB only under
`AIVERSE_EXPERIMENTAL_METAL_ICB=1`,
`AIVERSE_GPU_ENCODED_ICB=1`, plus
`AIVERSE_EXPERIMENTAL_ICB_PER_COMMAND_PBR=1` for
per-command material sources. The isolated test creates a
small native Metal device/layer, all six ordinary PBR
Texture2D handles per material, different source
`texture1` and `texture13` Shader Graph source colors,
and two different per-command U2 vectors. It builds
uint32 indices and two GPU draw-argument commands in a
real Metal `BufferType::Storage` buffer, and provides
two `36-float` instance records matching the current
GPU ICB encoder input contract.

A deliberately minimal graph-like MSL fragment
declares argument resource IDs `slot*2` for the
backend `IcbPbrTextures` layout and exact
`#define AIVERSE_MODEL_SSR 1`,
`#define AIVERSE_SG_ICB_GRAPH 1` and
`#define AIVERSE_SG_TEXTURE_COUNT 1` markers.
This is **not** the production Shader Graph compiler or
a full renderer/material pipeline; it specifically
tests **underlying per-command GPU-encoded ICB fragment
resource and U2 binding ABI** independently of game genre.
Two indexed quads cover the left/right halves of the
RGBA8 render target. They must independently produce
expected colors `{90,0,160,255}` and
`{100,140,0,255}` (<=1 LSB/channel), rather
than both inheriting a homogeneous first material.
Requires **nonzero ICB token**, successful native
`executeGpuEncodedIndexedIcb`, healthy GPU
completion and real asynchronous pixel readback.
Unsupported hardware, invalid resources, GPU faults,
timeout or mixed-source pixel errors fail; no CPU draw
fallback counts as success.

**Scope / safety:** intentionally **separate process** and
default-OFF option because AGX drivers have historically
failed under experimental GPU-authored ICB. The existing
`--metal` test stays registered only under its own
separate `AIVERSE_TEXTURE_METAL_GPU_TESTS`; ordinary
23 focused CTests and hardware texture source acceptance
remain unaffected. Public native `gfx::Device` API,
production renderer/GfxMetal, ECS, serialized assets,
scene and genre code remain untouched.

**Actually verified for Stage 10BQ:** checked production
Metal `prepareGpuEncodedIndexedIcbInternal` and
`executeGpuEncodedIndexedIcb` and `GpuIcbPbrMaterial`
contracts via GitHub, authored and committed C++/ObjC++
fixture and separately gated CMake registration.
**NOT RUN:** native macOS CMake configure, ObjC++
compilation/linking, experimental GPU ICB CTest,
real hardware pixel parity, driver stability or CI.
Do not reuse the previous Stage 10BP PASS as 10BQ success.

**Next exact command in isolated Mac Studio worktree:**
```bash
cd /Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819
git pull --ff-only origin feature/texture-rhi-unification
cmake -S . -B build/texture-rhi -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_TEXTURE_METAL_GPU_TESTS=ON \
  -DAIVERSE_TEXTURE_METAL_ICB_GPU_TESTS=ON
cmake --build build/texture-rhi \
  --target aiverse_metal_texture_icb_graph_gpu_test --parallel 4
ctest --test-dir build/texture-rhi --verbose --output-on-failure \
  --no-tests=error -R '^metal_texture_icb_graph_gpu$'
```
The CTest target automatically scopes experimental
environment flags to the new binary. If native Clang
MSL/ObjC++, GPU ICB compute, driver, or pixel validation
fails, capture the actual first error/CTest output and
patch only the confirmed root cause in this branch.
Append a new handoff per fix. Do not touch the unrelated
unfinished rebase in the primary AIVERSE worktree.
Keep PR #21 Draft. On success, next stage should test
the **actual production Shader Graph compiler/material**
permutations, and separately Vulkan/GL/mobile parity.


**Stage 10BQ test-isolation addendum:** the instructions above are
valid, but a *separate* CMake build directory is preferred so an
explicitly enabled, potentially unstable GPU ICB test cannot remain
registered in the already-passing ordinary
`build/texture-rhi` cache. The recommended **exact** commands
(also updated in `docs/texture-rhi.md`, commit
`c6994149aa3729dbe11cf2553f40c4fff2118672`) are:
```bash
cd /Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819
git pull --ff-only origin feature/texture-rhi-unification
cmake -S . -B build/texture-icb-gpu -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_TEXTURE_METAL_GPU_TESTS=OFF \
  -DAIVERSE_TEXTURE_METAL_ICB_GPU_TESTS=ON
cmake --build build/texture-icb-gpu \
  --target aiverse_metal_texture_icb_graph_gpu_test --parallel 4
ctest --test-dir build/texture-icb-gpu --verbose --output-on-failure \
  --no-tests=error -R '^metal_texture_icb_graph_gpu$'
```
No additional engine/test source change was made for this isolation
recommendation; native compile/GPU test remains NOT RUN.


## Stage 10BR — 1/4-source GPU ICB Shader Graph resource ABI (2026-11-19)

**Branch:** `feature/texture-rhi-unification`; **PR #21 remains Draft**.
Primary `/Users/aleksandrfeer/Projects/AIVERSE` worktree has
an unrelated unfinished rebase; do not operate there. Use isolated
`/Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819`.

**Source commit:** `50969517f9fdd4a15b38103c378232459520d2a5`
(`tests/render/metal_icb_graph_gpu.mm`).
**Separate documentation commits:**
`4a796624eef7075908eabeca5817f5cce6d721ae`
(`docs/texture-rhi.md`; corrected malformed CTest command and
duplicate trailing Markdown fence, documented expanded test) and
`8b90d9e265a83147131ca2006d35dfe11ea114eb`
(`skills/visuals/aiverse-texture-rhi/SKILL.md`).

**Implemented changes / exact API:**
- Retained the existing **default-OFF**, isolated macOS CMake
  `AIVERSE_TEXTURE_METAL_ICB_GPU_TESTS=ON` acceptance harness.
  Production `gfx::Device`, `Renderer3D`, `ModelShaders.h`,
  `GfxMetal.mm`, materials and scene formats are **unchanged**.
- Parameterized the native fixture to execute both one-source
  (`AIVERSE_SG_TEXTURE_COUNT 1`) and four-source
  (`AIVERSE_SG_TEXTURE_COUNT 4`) graph variants, in sequence.
  The 4-source MSL test adds graph textures 14, 15, 16 at Metal
  fragment argument-buffer texture IDs 28, 30, 32 beside source
  slot 13 at ID 26. Slot 16 uses constexpr repeat/linear
  sampling; source textures are created with compatible metadata.
- Each variant encodes **two actual GPU indexed commands** through
  `Device::prepareGpuEncodedIndexedIcbWithPbrMaterials` and
  `executeGpuEncodedIndexedIcb`. Different native source textures
  and U2 factors produce distinguishable left/right RGBA8 pixels.
  The existing one-source pixel oracle remains
  `{90,0,160,255}` / `{100,140,0,255}`. The four-source
  oracle is `{100,20,180,255}` / `{110,160,30,255}`.
  All channels must match within 1 LSB; failures are nonzero exit.
  Test fails if a GPU token is rejected or device is unsupported.
- The ShaderDesc fragment texture-slot capacity is 14 for 1 source
  and 17 for 4, matching the highest used slot 16.
- No fallback CPU draw is used to manufacture a GPU-ICB PASS.
  The fixture's minimal MSL is not the authored scene's production
  Shader Graph compiler; the test checks backend resource ABI only.

**Verification honestly performed:** fetched and inspected the
production Metal GPU-encoded ICB preflight/execute, argument-buffer
layout and graph source sampler contract via GitHub connector;
source/docs/skill written and committed through the GitHub connector.
Native CMake configure/build, Objective-C++, MSL compiler, CTest,
Metal driver execution, readback pixels, screenshots and
performance profiling for Stage 10BR were **NOT RUN** here.
The last **observed** hardware result was Stage 10BP's separate
Apple M3 Ultra virtual-texture fragment page-table 8/8 PASS
(`metal_texture_gpu` 1/1 PASS, 2.47 s); that is not an ICB result.

**Next action — actual Mac Studio isolated test, NOT primary tree:**

```bash
cd /Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819
git fetch origin feature/texture-rhi-unification
git pull --ff-only origin feature/texture-rhi-unification
cmake -S . -B build/texture-icb-gpu -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_TEXTURE_METAL_GPU_TESTS=OFF -DAIVERSE_TEXTURE_METAL_ICB_GPU_TESTS=ON
cmake --build build/texture-icb-gpu --target aiverse_metal_texture_icb_graph_gpu_test --parallel 4
ctest --test-dir build/texture-icb-gpu --verbose --output-on-failure --no-tests=error -R '^metal_texture_icb_graph_gpu$'
```

CTest passes experimental env flags solely for this process.
If build or GPU fails, record the **first concrete compiler/MSL/Metal
device/error/pixel diagnostic** and fix only the root cause in
a separate source commit plus appended handoff. If both 1/4-source
GPU pixel cases pass, separately test the real production
Shader Graph material/compiler Renderer3D pipeline, then
Vulkan/GL/mobile parity, filtering fidelity and performance.
Do not merge PR #21 based on source-only changes or prior
non-ICB hardware PASS.


## Stage 10BS — fix native Mac Studio GPU-ICB fixture compilation (2026-11-19)

**Branch:** `feature/texture-rhi-unification`. PR #21 **Draft**.
**Previous source:** Stage 10BR `50969517f9fdd4a15b38103c378232459520d2a5`.
**Observed native user log:** CMake macOS configuration succeeded. Clang
then failed at `tests/render/metal_icb_graph_gpu.mm:237`:
`shDesc.msl=shaderSource;` cannot convert `std::string`
to `const char*`. The subsequent CTest **1/1 PASS** used an old
executable because build failed; its single generic PASS line is
**NOT** validation of the new four-source fixture.

**Source commit** `3243fc67157187e087c68048e388dfae15bed05b`:
- Change `shDesc.msl=shaderSource.c_str();` in
  `tests/render/metal_icb_graph_gpu.mm`.
- Keep `shaderSource` alive through synchronous
  `Device::createShader(shDesc)`, which copies the source in
  `GfxMetal.mm`.
- Print per-case `PASS: GPU ICB graph sources=1` and
  `PASS: GPU ICB graph sources=4` on correct readback so
  stale 1-source-only executables are easy to distinguish.
- Production RHI, renderer, Shader Graph compiler and platform
  code are unchanged. Experimental Metal GPU ICB stays opt-in.

**Documentation source commit:**
`c8160e84fb57b62cde5a72196401d257dd7ffed6`
(`docs/texture-rhi.md`): observed failure, why old CTest
PASS is insufficient, exact fail-fast commands, dual expected
pixel output markers.

**Tests/verification actually performed:** native compiler failure
and old-binary PASS were supplied in the user log. The code fix was
authored and pushed via GitHub connector and its updated source
reviewed. The newly fixed source has **NOT** been compiled,
linked, run on Metal or visually/performance validated.
Do not report a Stage 10BR/10BS GPU PASS until a fresh successful
build and both per-source-case pixel markers are observed.

**Next exact step** — run only in isolated Mac Studio worktree,
not primary AIVERSE checkout (unrelated unfinished rebase):
```bash
set -euo pipefail
cd /Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819
git fetch origin feature/texture-rhi-unification
git pull --ff-only origin feature/texture-rhi-unification
cmake --build build/texture-icb-gpu --target aiverse_metal_texture_icb_graph_gpu_test --parallel 4
ctest --test-dir build/texture-icb-gpu --verbose --output-on-failure --no-tests=error -R '^metal_texture_icb_graph_gpu$'
```
`set -e` prevents CTest from running a stale executable after
a build failure. Inspect output for **both**:
`PASS: GPU ICB graph sources=1, per-command textures/U2 pixel parity`,
`PASS: GPU ICB graph sources=4, per-command textures/U2 pixel parity`,
and 1/1 native CTest PASS. On failure, inspect the actual first
MSL/driver/pixel error, fix in a new code commit and create the
next handoff. After genuine GPU PASS, proceed to production compiled
Shader Graph/Renderer3D integration and cross-platform fidelity.
Do not merge PR #21 until all required gates succeed.


## Stage 10BT — Apple M3 Ultra Metal GPU ICB four-source ABI PASS (2026-11-19)

**Branch/PR:** `feature/texture-rhi-unification`, Draft PR #21.
**Native user verification:** the user ran the repaired Stage 10BS
source on Apple M3 Ultra in the isolated Mac Studio worktree
`/Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819`.
The verbose CTest reported
`Test #122 metal_texture_icb_graph_gpu: Passed 1.06 sec`,
**1/1 PASS, zero failures; total CTest real elapsed 1.07 sec**.
Actual backend `prepareGpuEncodedIndexedIcbWithPbrMaterials` /
`executeGpuEncodedIndexedIcb` submitted two native GPU commands
and 4x4 GPU RGBA8 pixels matched different per-command materials.

**Distinct PASS markers observed in the user's log:**
- `PASS: GPU ICB graph sources=1, per-command textures/U2 pixel parity`;
  fixture interval `240.116 ms`.
- `PASS: GPU ICB graph sources=4, per-command textures/U2 pixel parity`;
  fixture interval `151.826 ms`.
- Both displayed `PASS: production Metal GPU ICB executed two material commands`.
- Driver reported `Metal Apple M3 Ultra`, `pipelineFailures=0`.

**Scope:** first observed real-device PASS for both ABI layouts,
including argument texture slot 16 with constexpr sampler and heterogeneous
source/U2 descriptors. No CPU fallback. These operation intervals are
not isolated fragment GPU timings or frame FPS. The fixture still
uses a deliberately minimal MSL graph ABI, **not** the production
`render::graph::compile` or `Renderer3D` scene shaders.
Shader Graph scene integration, Vulkan/GL/mobile, GPU robustness and
large-scene performance remain unverified.

**Change commit:** `332258c766eadb719c434a523294231583ac006e`
records the actual user log and verification boundary in
`docs/texture-rhi.md`. No rendering source was changed by this
bounded result-recording step. Keep Draft.

## Stage 10BU — real compiled Shader Graph + Renderer3D GPU ICB scene (source only)

**Branch:** `feature/texture-rhi-unification` (Draft PR #21).
**New source commit:** `353537d34021690c284689fab5db88eb752cdd2d`
adds `tests/render/metal_renderer_graph_icb_gpu.cpp` (304 lines).
**CMake commit:** `c28699610bee997b7db0c8fd0ac91803b75bd3bd`
adds isolated opt-in CMake/CTest gate with
`AIVERSE_TEXTURE_METAL_RENDERER_GRAPH_GPU_TESTS=OFF`
by default, executable
`aiverse_metal_renderer_graph_icb_gpu_test`,
CTest name `metal_renderer_graph_icb_gpu`.
**Docs commits:** `368a6bfab138cd108defa57cecd3504f95b48b89`
updates `docs/texture-rhi.md`, and
`8348352fa818680d40d0627e501bf61b67d2a837`
updates `skills/visuals/aiverse-texture-rhi/SKILL.md`.

**Architecture and API:** The fixture constructs an authored material
graph as JSON with **four connected sampleTexture outputs**,
runs real `render::graph::compile(json,"material")`,
and binds its immutable Program to both `render::Material` values.
A generated native model has two opaque quad primitives,
different sampled `Model::images` for each of four source
slots on either material, and separate U2 base-color factors.
Normal `render::uploadModel` prepares real GPU buffers/textures.
`Renderer3D::beginScene`, `drawModel`, `render`,
`Device::captureNextFrame` first record conventional pixels
and then the same scene with
`Renderer3D::setGpuAuthoredDrawEnabled(true)`.
The test requires real `GpuModelDrawStats` nonzero:
`shaderGraphIcbSubmissions`, `mixedGraphSourceSubmissions`,
`perCommandPbrSubmissions`, at least two encoded commands
and zero execution failures. It also checks both output regions
are visibly different and the entire direct/GPU ICB RGBA8
capture differs by <=2 LSB/channel. Zero draws/blank frames,
reused first material, CPU fallback and stale test binaries
are explicitly rejected.

**Production files untouched:** `Gfx.h`, `GfxMetal.mm`,
`Renderer3DModels.cpp`, actual graph compiler, materials,
serialized assets and RTS/FPS gameplay. No new engine API is required.
Only test source, CMake/docs/skill/handoff change. Driver-unsafe
GPU-ICB remains default-OFF and in its own CTest process.

**Verification done:** static cross-file review of CMake target
registration and production shader graph, model material/image,
renderer scene/capture and ICB counter signatures via GitHub.
**NOT RUN:** native macOS build, Objective-C++/C++ linking,
`render::graph::compile` in this fixture, MSL generated shader
compilation, actual production Renderer3D ICB GPU draw,
screen capture, visual parity or performance. Stage 10BT
low-level hardware PASS must not be misreported as 10BU PASS.

**Next exact commands** (dedicated build cache and only the
isolated Mac Studio texture worktree, never primary AIVERSE
checkout with unrelated unfinished rebase):

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

**Acceptance:** require successful build and 1/1 CTest PASS,
plus `PASS: production Renderer3D compiled Shader Graph 4-source
mixed Metal GPU ICB pixels` (not just 10BT's 1/4-source ABI markers).
On compiler/Metal GPU shader/driver/counter/pixel failure,
capture the first concrete diagnostic and fix root cause on this
feature branch; commit code, then append a fresh handoff.
Only after native proof should the next agent expand to
multiple Shader Graph permutations, VT+graph integrated
scene draws and separate Vulkan/GL/mobile acceptance.
**Do not merge PR #21 until independently verified.**

## Stage 10BV — production compiled Renderer3D Shader Graph ICB GPU PASS (2026-11-19)

**Branch / PR:** `feature/texture-rhi-unification`, Draft PR #21.
**Prior implementation:** Stage 10BU source
`353537d34021690c284689fab5db88eb752cdd2d`,
CMake `c28699610bee997b7db0c8fd0ac91803b75bd3bd`.
**Verification documentation source commits:**
`88e9d8b07f1c1c60f4f371853cc38f394482360f`
(`docs/texture-rhi.md`, exact hardware logs/results/limitations),
`55ddd6a8c5cb1852948433fb50346f978784c2d4`
(`skills/visuals/aiverse-texture-rhi/SKILL.md`).

**Actually executed by user on Apple M3 Ultra:**
`aiverse_metal_renderer_graph_icb_gpu_test` built and linked
successfully (100%). The isolated native CTest
`metal_renderer_graph_icb_gpu` reported **1/1 PASS, zero failed,
3.73 s**, total CTest elapsed **3.74 s**.
CTest injected the intended explicit safety-gated environment,
including `AIVERSE_GPU_AUTHORED_DRAWS=1`,
`AIVERSE_GPU_ENCODED_ICB=1`,
`AIVERSE_EXPERIMENTAL_METAL_ICB=1`,
`AIVERSE_EXPERIMENTAL_ICB_PER_COMMAND_PBR=1`,
`AIVERSE_EXPERIMENTAL_ICB_MIXED_PBR=1` and
`AIVERSE_EXPERIMENTAL_ICB_SHADER_GRAPH=1`.

**Exact native output:**
```text
[metal-assets] libraries=0 sourceMisses=11 pipelines=17 pipelineFailures=0 archive=0 strict=0
GPU ICB graph stats: eligible=2 candidates=1 prepared=1 submitted=1 mixed=1 gpu_commands=2 rejected=0 errors=0
Renderer3D Shader Graph regions: left=(196.0,84.0,94.0) right=(87.0,196.0,122.0) delta=249.0
Renderer3D Shader Graph regions: left=(196.0,84.0,94.0) right=(87.0,196.0,122.0) delta=249.0
Renderer3D graph CPU/ICB pixels: bad_channels=0 max_delta=0
PASS: production Renderer3D compiled Shader Graph 4-source mixed Metal GPU ICB pixels
1/1 Test #122: metal_renderer_graph_icb_gpu ..... Passed 3.73 sec
```

**Architecture verified in this bounded scenario:** The authored
JSON Shader Graph goes through real `render::graph::compile`;
`Model` has two opaque rigid primitives with independent
four-texture sources and packed per-command material U2.
`Renderer3D` compiled its production MSL shader and pipeline
(`pipelineFailures=0`), created one mixed graph ICB group
containing two GPU-authored commands, and captured real hardware
pixels. The direct and GPU ICB images differed by **zero channels
at any location**, max absolute channel delta zero, not merely
within the <=2 acceptance tolerance. Two material regions
remained visibly different in both images, so the capture was
not a blank-screen false positive or accidental material collapse.

**Test boundaries / honesty:** This is a production path test
for ONE four-source fragment-only, opaque rigid Shader Graph
material on ONE Metal device, NOT proof that all Shader Graph
nodes, all sampler/wrap combinations, VT atlas/page-table,
morph/deformation, alpha modes, WebGL/GL/Vulkan/mobile, driver
robustness, scene stress or frame performance work. The
3.73 s CTest duration is NOT GPU draw time or gameplay FPS.
Experimental ICB remains explicitly OFF by default.
Keep PR #21 Draft pending cross-backend/VT acceptance.

**Stage 10BV changes:** documentation + skill + this append-only
handoff; **no production code fix** was required after the user
reported PASS. The underlying Stage 10BU integration fixture remains
unchanged. Actual test evidence came from user-provided terminal log,
not remote CI; do not claim CI passed.

**Next agent / exact next action:** Implement a separate isolated
macOS GPU test exercising production `ShaderGraph` with
`sampleVirtualTexture` plus independent graph source
textures through `Renderer3D`, first with one VT binding and
then different physical atlas/page-table/layers per command
(gated `AIVERSE_EXPERIMENTAL_ICB_PER_COMMAND_VT` and
`AIVERSE_EXPERIMENTAL_ICB_MIXED_VT`). Validate actual
GPU resource/group counters, direct-draw RGBA8 parity,
nonresident fallback, coarse/fine disk-backed page publication,
LRU eviction, and sampler address-mode seams. Avoid
duplicating `VirtualTextureFileStreamer` or the existing
`gfx::kVirtualTextureMSL` and `ModelVirtualTextureSampling`
helpers. Keep GPU ICB tests default-OFF and in separate CMake
build caches. Do not touch unrelated unfinished rebase in
primary AIVERSE worktree. Next, Vulkan/GL/mobile tests
and representative GPU timing/performance.
After **each** bounded code stage, commit source first and
then separately append exact SHA, files, API and actual
tests/next step to this handoff.


## Stage 10BW — production graph + two disk VT domains on Metal ICB (2026-11-19)

**Branch/PR:** `feature/texture-rhi-unification`, Draft PR #21.
**Preceding confirmed hardware:** Stage 10BV Mac Studio Apple M3 Ultra
`metal_renderer_graph_icb_gpu` 1/1 PASS in 3.73 s, byte-identical
production Renderer3D 4-texture graph direct/ICB images **without VT**.
Stage 10BT's lower-level 1/4-source ICB ABI also passed on device.
Neither result proves the new graph + VT integration.

**Stage 10BW source commits:**
- `8946de8a03e65a2eefd1b8d40dbb7270f3ad8124`:
  new `tests/render/metal_renderer_graph_vt_icb_gpu.cpp`
  originally created from the working Stage 10BU production fixture,
  replacing its material graph with a real `sampleVirtualTexture`
  plus four connected regular source samples.
- `2a4d64c102627aff48f1e7c756260b377f0c9e7f`:
  correct single anonymous C++ namespace for test helpers, add
  explicit `<cstddef>` include.
- `70019a92cf5657c05c3f3e3a749eafcd4061b753`:
  CMake default-OFF opt-in option
  `AIVERSE_TEXTURE_METAL_GRAPH_VT_ICB_GPU_TESTS`,
  separate `aiverse_metal_renderer_graph_vt_icb_gpu_test`,
  `metal_renderer_graph_vt_icb_gpu` CTest, GPU/VT/graph
  feature flags restricted to this separate test process.
- `ed7f9072700e92e792d8e7282807847ab3a07208`:
  `docs/texture-rhi.md` Stage 10BW usage, scope, acceptance and
  exact native commands.
- `a92187df6784c4ce653cc19562eae9603965ba21`:
  `skills/visuals/aiverse-texture-rhi/SKILL.md` reusable agent recipe.

**Architecture / actual code:**
1. JSON version-1 Shader Graph `material` is compiled by
   `render::graph::compile`. All four `sampleTexture` nodes
   contribute to `color` along with connected `sampleVirtualTexture`.
   No test-only shader compiler or custom virtual page shader exists.
2. A two-primitive, opaque rigid `Model` with distinct image
   textures/U2 per material exercises production
   `Renderer3D::beginScene / drawModel / render`,
   per-command graph resource table and GPU ICB. This is
   genre-independent; RTS/FPS/gameplay is untouched.
3. Two `gfx::VirtualTextureFileStreamer` instances each own a
   separate ephemeral raw-RGBA8 disk archive, real Metal
   physical Texture2DArray atlas and distinct RGBA8 page-table.
   Material resolver supplies different handles, virtual layers
   **0 vs 1**, gutter=0, U Repeat vs MirrorRepeat, V ClampToEdge.
   Existing `engine/render/ModelVirtualTextureSampling.h` and
   `gfx::kVirtualTextureMSL` handle the shader lookup.
4. Each atlas has **two physical page slices**: pin mip2 coarse
   page from disk; request mip0 fine tile (1,1); request
   replacement mip0 tile (2,2) to force LRU eviction of fine,
   retaining coarse. The native worker publishes only through
   `stream.pump()` on the real render thread, no GPU work
   on disk worker.
5. For each coarse, fine and LRU phase the test captures ordinary
   Renderer3D and Metal GPU ICB frame images. It requires two
   distinct material regions, per-channel full-frame parity
   within 2 LSB, **>64 changed channels** after both
   page transitions, nonzero real
   `shaderGraphIcbSubmissions`,
   `mixedGraphSourceSubmissions`,
   `perCommandVtExecutions`,
   `mixedVtExecutions`, at least two encoded commands,
   zero failed GPU executes, and both archives' exact resident
   page / LRU eviction checks. A CPU fallback cannot count as
   a GPU ICB PASS. Test timeout and unsupported hardware fail.
6. Temporary disk archives are removed at teardown; streamers
   and model GPU textures are released; native production
   `GfxMetal.mm`, RHI, renderer, file streaming and Shader
   Graph code were NOT changed.

**Validation this stage:** GitHub connector source-level API
inspection and static file/registration checks only.
**NOT RUN:** native macOS CMake configure, C++ compilation/link,
MSL compile, actual Renderer3D VT shader draw, disk worker/GPU
publication, real CTest, pixel comparison, GPU timing/driver
stress or other backend tests. No staging code may be called
production-certified on the basis of source review.

**Next agent exact fail-fast commands** (isolated Mac Studio
texture worktree ONLY, never primary AIVERSE with unfinished rebase):

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

**Real success requires:** native compile succeeds, three
`PASS: Shader Graph+VT [phase] Renderer3D CPU/ICB pixels`,
two changed-channel transition lines, real nonzero mixed VT
and graph GPU ICB counters, final
`PASS: production Renderer3D Shader Graph+disk VT mixed
Metal GPU ICB coarse/fine/LRU pixel parity`, and CTest
**1/1 PASS** on the fresh build. This new test is risky, opt-in
and isolated from earlier green CMake caches. On any failure,
record the FIRST compiler/MSL/worker/driver/ICB/pixel error and
fix its root cause in a separate source commit, followed by
a new append-only handoff. Do not run old CTest binaries
after failed builds.

**Remaining feature/validation work:** nonresident and
intentionally missing disk page fallback, address/linear-filter
seam fidelity at UV outside [0,1], graph 0..4-source variants,
Vulkan/GL/mobile/WebGL fallback and real performance / driver
stress. Keep all experimental flags OFF by default and PR #21
Draft until cross-platform acceptance.

## Stage 10BX — observed native dual-disk VT + Shader Graph + GPU ICB PASS (2026-11-19)

**Branch / PR:** `feature/texture-rhi-unification`, PR #21 Draft.
**Implemented fixture:** Stage 10BW
`tests/render/metal_renderer_graph_vt_icb_gpu.cpp` from
source commits `8946de8a03e65a2eefd1b8d40dbb7270f3ad8124` and
`2a4d64c102627aff48f1e7c756260b377f0c9e7f`; CMake
`70019a92cf5657c05c3f3e3a749eafcd4061b753`.
This stage adds result documentation, not new engine behavior.

**User-confirmed real Apple M3 Ultra/Metal execution:**
`metal_renderer_graph_vt_icb_gpu` **1/1 CTest PASS**,
test **4.57 s**, total elapsed **4.58 s**, zero failures.
The test ran with explicit flags for Metal GPU ICB,
GPU-authored commands, heterogeneous PBR source resources,
per-command VT, mixed VT and Shader Graph; the experimental
features remain OFF by default for released builds.

**Exact observed GPU state in all THREE phases:**
`eligible=2 prepared=1 graph=1 mixedGraph=1 vt=1
mixedVt=1 commands=2 rejected=0 errors=0`.
The ordinary and GPU-encoded ICB Renderer3D captured images
matched **exactly (bad_channels=0, max_delta=0)** on all three
distinct residency states:

1. Pinned coarse: distinct image regions
   left `(140.0,121.0,151.0)`,
   right `(125.0,149.0,103.0)`,
   region delta `91.0`.
2. Fine detail: left `(151.6,114.3,141.2)`,
   right `(119.4,150.7,116.0)`,
   region delta `93.7`.
   Transition altered **1392 captured channels**.
3. LRU recycled: left `(138.4,130.6,143.7)`,
   right `(134.5,146.3,114.7)`,
   region delta `48.5`.
   Transition altered **2886 captured channels**.

**Observed acceptance markers:**
```text
PASS: Shader Graph+VT pinned coarse Renderer3D CPU/ICB pixels
PASS: Shader Graph+VT fine detail Renderer3D CPU/ICB pixels
PASS: Shader Graph+VT LRU recycled Renderer3D CPU/ICB pixels
PASS: production Renderer3D Shader Graph+disk VT mixed Metal GPU ICB coarse/fine/LRU pixel parity
1/1 Test #122: metal_renderer_graph_vt_icb_gpu ... Passed 4.57 sec
```

**What is actually verified:** A real compiled `render::graph::Program`
with four connected Shader Graph source textures and
`sampleVirtualTexture`, two independently owned disk-backed VT
archives, distinct GPU atlas/page-table handles and virtual layers,
pin/coarse publication, fine publication and LRU slot reuse,
per-command material resource binding, real native GPU ICB
submission and full-screen RGBA8 CPU/ICB pixel parity.
The two material regions are visibly distinct, so a blank frame
could not satisfy the test. The measured CTest duration is
**not** a gameplay GPU frame time or ICB performance gain.

**Code changes for this verification stage:** none.
**Documentation commits:** `2f7ed7b43a309e7ac5489fb405beaa85be71edbe`
(`docs/texture-rhi.md`, Stage 10BX evidence and scope),
`1acf0a7a07cf26ce66277fe1764405c5f11e0b25`
(`skills/visuals/aiverse-texture-rhi/SKILL.md`,
hardware-verification status, next steps), plus this
append-only handoff commit. The native log is user-supplied;
do not misstate it as GitHub Actions CI success.

**Still unverified:** intentionally missing/corrupt disk pages,
GPU shader nonresident and coarse fallback in the combined
Renderer3D ICB path; seam/filtering parity around UV < 0 / > 1
for Repeat/MirrorRepeat, non-4 graph source layouts, graph
vertex displacement, alpha blends/masks, stressed/stalled disk
I/O or GPU driver stability, platform Vulkan/GL/WebGL/mobile
fallbacks and realistic frame-time performance.

**Next agent action:** Work on isolated texture verification
branch/worktree, not primary AIVERSE checkout with unresolved
rebase. Add a **separately gated** test for missing-page failure
and UV seams, using `VirtualTextureFileStreamer`,
`VirtualTextureSampling` and `Renderer3D` in their existing
production paths. Validate fail-closed page-table state after
missing-file errors, correct coarse fallback for unresolved
fine detail, and Repeat/MirrorRepeat texture results at
UV boundaries (including negative/out-of-range UV).
Require actual scene GPU ICB graph+VT counters, CPU/ICB
pixel match and independent expected-color/oracle checks.
Document exact native CMake/CTest commands and outcomes.
Do not merge PR #21 or enable experimental GPU ICB by
default until multi-backend and stress acceptance is complete.

## Stage 10BY — missing page + UV seam independent GPU oracle (2026-11-19)

**Branch:** `feature/texture-rhi-unification` (Draft PR #21).
**Last confirmed previous-stage hardware:** Stage 10BX Mac Studio
Apple M3 Ultra `metal_renderer_graph_vt_icb_gpu` real
**1/1 PASS, 4.57 sec**, identical ordinary/ICB pixels for
coarse/fine/LRU and separate source VT archives. This
does NOT cover the newly authored Stage 10BY cases.

**Stage 10BY source commits:**
- `694d04531ffd5c1e81efe60b499a0ccd8e096d0d`:
  **new** `tests/render/metal_renderer_graph_vt_seam_gpu.cpp`
  (real production compiled four-source+VT Shader Graph,
  Renderer3D and two independently owned disk-backed Metal
  VT resources, ordinary vs GPU ICB captures).
- `d95cdc388778a31de5b0f9900863cd28c2e7a737`:
  append `CMakeLists.txt` default-OFF experimental test
  `AIVERSE_TEXTURE_METAL_GRAPH_VT_SEAM_GPU_TESTS`;
  new binary `aiverse_metal_renderer_graph_vt_seam_gpu_test`,
  CTest `metal_renderer_graph_vt_seam_gpu`, scoped opt-in
  Metal ICB, per-command PBR/VT, mixed PBR/VT and graph flags,
  120-second timeout. NOT in default suite / ordinary --metal.
- `3b0b1bff1c9c467a8bfa900e28f5af6ed8792b34`:
  eliminate unused phase helper in new fixture (strict-build
  hygiene, no behavior change).
- `36d3a5faf8d0a8f2de36f619b10b42135d581eed`:
  `docs/texture-rhi.md` exact acceptance, limitations and
  fail-fast Mac Studio commands.
- `7e603f3e9eec47a03487aaa9fc8e907240766b40`:
  `skills/visuals/aiverse-texture-rhi/SKILL.md` agent
  selection and authoritative source recipe.

**Implementation / test pipeline:**
1. Compile a version-1 material JSON graph through the
   production `render::graph::compile`: four connected source
   `sampleTexture` nodes and a connected `sampleVirtualTexture`
   node; no synthetic alternative shader compiler. Use
   existing `ModelVirtualTextureBinding` and canonical
   `gfx::kVirtualTextureMSL` through Renderer3D.
2. Two differently textured rigid opaque model materials
   use separate `VirtualTextureFileStreamer` instances,
   physical RGBA8 array atlases, GPU RGBA8 page tables and
   virtual layers 0 and 1. Left addressU=Repeat,
   right=MirrorRepeat, both V=ClampToEdge. The test creates
   only its own trusted 4×4 solid-color page files in an
   isolated temporary folder. Streams are cleaned up at exit.
3. Before any page: first capture a no-VT-resolver direct
   Shader Graph `tex1` fallback reference. Then attach real
   VT resolver with **empty valid page tables** and render
   direct + GPU ICB. CPU lookup `encodedPageTable(3,1,side)`
   must be all zero. The entire resulting GPU image must
   equal the no-VT reference pixel-for-pixel, plus ordinary
   and GPU-encoded captures must match each other.
4. Publish pinned mip2 coarse from real disk worker. The
   expected table bytes at tile (3,1) are `{0,0,2,255}`.
   Require image transition from fallback, actual page
   residency and pinning.
5. **Intentionally omit** mip0 page (2,1) for both disk
   archives and request it. Must get TWO asynchronous worker
   error callbacks with diagnostics, preserve streamer
   health/pinned coarse page and unchanged table bytes,
   and emit an **unchanged** direct framebuffer image.
   GPU ICB must still render the exact same framebuffer
   in the failed-read state.
6. Publish known mip0 fine page tile (3,1) in physical
   slice1 and check expected table bytes `{1,0,0,255}`.
   Model material UV transform uses scale (0.0001,0.0001)
   and V=0.375, keeping every visible sample in one tile.
   Four authored out-of-range U points:
   - `-0.125`: Repeat→0.875 (tile3 fine on **left**);
     MirrorRepeat→0.125 (tile0 coarse on **right**).
   - `+1.125`: Repeat→0.125 (left coarse);
     MirrorRepeat→0.875 (right fine).
   - `-0.0005`: Repeat→0.9995 (left fine);
     MirrorRepeat→0.0005 (right coarse).
   - `+1.0005`: Repeat→0.0005 (left coarse);
     MirrorRepeat→0.9995 (right fine).
   Both the ordinary and GPU ICB image pass a *separate*
   expected-region oracle: the predicted material's ROI
   must differ substantially from the pinned-coarse capture,
   while the other material ROI must not. The oracle also
   requires the expected channel direction based on known
   authored page colours: left fine raises red / lowers green,
   right fine raises red / lowers blue.
7. Every VT GPU ICB state additionally requires nonzero
   `shaderGraphIcbSubmissions`,
   `mixedGraphSourceSubmissions`,
   `perCommandVtExecutions`,
   `mixedVtExecutions`, >=2 encoded GPU commands and
   zero rejected/execute-failed commands. Direct/ICB
   full RGBA8 captures must match within 2 LSB/channel.
   The independent fallback/wrap oracles prevent a false
   pass when both paths choose the same wrong page.
8. A hard error or deadline aborts CTest. No CPU draw fallback
   is accepted as proof. The fixture has no dependencies
   on project games, RTS/FPS or other agents' work.

**What was actually verified in this assistant environment:**
read existing public Renderer3D, VT disk queue, exact page-table
encoding and canonical MSL/GLSL shader source through GitHub;
cross-checked presence of production graph compiler, two
archives, missing file request with expected failure, GPU-ICB
counters, out-of-range UV modes, independent ROI RGB oracle,
CMake registration and stage docs. Source commits pushed to
GitHub connector. **NOT RUN:** local/native CMake config,
C++20 compilation/link, Metal shader compiler,
`VirtualTextureFileStreamer` worker on Apple hardware,
GPU command execution, CTest, actual RGBA8 screenshot,
or seam/fallback accuracy. Static review is not GPU proof.
The Stage 10BX green log must NOT be repeated as Stage
10BY verified. Production texture/shader/VT/renderer code
was not modified by this stage.

**Exact next run — isolated Mac Studio texture worktree only:**

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

**PASS gate:** compile succeeds; image parity in all seven
nonresident/coarse/missing/seam pairs with real mixed GPU ICB
submissions; exactly two expected missing-file callback PASS;
zero changed channels for fallback and missing-file references;
four independent wrap-oracle PASS markers for each of direct
and ICB; terminal
`PASS: Renderer3D Shader Graph disk VT missing-page and
Repeat/MirrorRepeat seam GPU ICB oracle`; CTest **1/1 PASS**.
If source/driver/MSL/runtime/oracle errors arise, capture
FIRST native diagnostic and fix root cause, record a new
code commit followed by its own append-only handoff.

**Important outstanding:** this test uses *solid-color
physical pages with zero gutter* to certify page selection
across UV address boundaries. It does NOT certify full
bilinear/anisotropic mip filtering and seam continuity with
nonuniform physical page edges, or GL/Vulkan/WebGL/mobile
platform parity, 0–3 graph source counts, stress or scene
frame-time performance. Keep PR #21 Draft; GPU ICB features
and risky CTests remain default OFF.

## Stage 10BZ — observed Clang helper-name collision fixed (2026-11-19)

**Branch/PR:** `feature/texture-rhi-unification`, PR #21 Draft.
**Immediate preceding work:** Stage 10BY new
`metal_renderer_graph_vt_seam_gpu` missing-page and
Repeat/MirrorRepeat UV GPU acceptance fixture. The earlier
Stage 10BX graph+disk-VT coarse/fine/LRU test passed on M3 Ultra,
but **does not cover Stage 10BY**.

**User-supplied native Mac Studio build failure:**
`aiverse_runtime` built successfully, but compiling
`tests/render/metal_renderer_graph_vt_seam_gpu.cpp`
produced **four Clang errors** at test lines 387, 490, 523
and 578, all `reference to 'detail' is ambiguous`.
The candidate `(anonymous namespace)::detail(int)` was
ambiguous against existing `aiverse::detail` namespace
declared by `engine/core/Fixed.h:11` and imported by
`using namespace aiverse;`. The test binary did not link.
**CTest/GPU execution was not performed for this failure.**

**Source fix commit:**
`8fe7265f2124539a36e388255acd1d9fc5ee3887`
only changes `tests/render/metal_renderer_graph_vt_seam_gpu.cpp`:
rename helper definition to `finePageKey(int)`, update
three direct calls and the function pointer passed to
`request(finePageKey,false,...)`. Semantic page address
remains `{mip=0,x=3,y=1,layer=side}`. Public
RHI/Streamer/Renderer3D/ShaderGraph/ECS interfaces, production
GPU/streaming code and material assets are **unchanged**.

**Documentation commit:**
`bdb3196be2cceeae07b58fcf0469fcbef65aeeab`
(`docs/texture-rhi.md`, Stage 10BZ with exact observed
error, fix and fail-fast test commands), followed by this
append-only handoff record.

**Verification actually performed:** retrieved the source
through the GitHub connector, found all five `detail`
identifiers (one declaration, three calls, one function
pointer) and replaced all five. Refetched source and
committed fix. **Native macOS recompile, Metal shader
compile, CTest and GPU pixels after the fix NOT RUN**.
Do not misreport the previously successful Stage 10BX
CTest as the new Stage 10BY passing.

**Next command — isolated Mac Studio texture worktree ONLY,
not primary AIVERSE with unrelated unfinished rebase:**

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

**Acceptance:** only count the test if new build succeeds and
native `metal_renderer_graph_vt_seam_gpu` prints
`PASS: Renderer3D Shader Graph disk VT missing-page and
Repeat/MirrorRepeat seam GPU ICB oracle`, all missing-page
failures/wrap oracle checks, and CTest **1/1 PASS**.
On further failure, inspect the first native compiler/Metal
driver/worker/independent RGB oracle error, fix its confirmed
cause, and append another standalone handoff entry.
Keep risky ICB tests and feature flags default OFF; keep
PR #21 Draft pending multi-backend acceptance.

**Stage 10BZ agent-skill addendum (same bounded fix):**
`864fe6135f0f88f2d06b8f490cf0fb70e72c6fde`
updates `skills/visuals/aiverse-texture-rhi/SKILL.md`
to record the actual Clang failure, renamed helper and
pending native hardware validation. This documentation
does not change source/test behavior. The complete Stage
10BZ change set is one test-source fix plus the main guide,
the agent skill and these append-only handoff entries.

## Stage 10CA — Apple M3 Ultra Shader Graph VT fallback/wrap Metal ICB GPU PASS (2026-11-19)

**Branch / PR:** `feature/texture-rhi-unification`, PR #21 Draft.
**Prior source:** Stage 10BY fixture
`tests/render/metal_renderer_graph_vt_seam_gpu.cpp`,
`694d04531ffd5c1e81efe60b499a0ccd8e096d0d`.
**Native build fix:** Stage 10BZ commit
`8fe7265f2124539a36e388255acd1d9fc5ee3887`
renamed `detail(int)` to `finePageKey(int)` to avoid
`aiverse::detail` namespace ambiguity.

**Observed native user verification after the fix:**
`metal_renderer_graph_vt_seam_gpu` on Mac Studio
**1/1 CTest PASS; 0 failed; 5.65 sec test time,
5.67 sec CTest wall time**. The user supplied complete
verbose CTest diagnostics, including actual GPU ICB
submissions and seven frame comparisons. This is user
hardware evidence, **not** GitHub Actions CI.

**GPU acceptance results:**
`graph=1 mixedGraph=1 vt=1 mixedVt=1 commands=2
rejected=0 errors=0` for each of **seven** states:
nonresident; pinned coarse; missing-file preserved coarse;
U=-0.125; U=+1.125; U=-0.0005 seam; U=+1.0005 seam.
Every direct/Metal GPU ICB full-screen RGBA8 comparison
was **byte identical**:
`bad_channels=0 max_delta=0`.

**Independent expected-color / failure tests confirmed:**
- Nonresident page-table fallback matched the original
  no-resolver graph `tex1` fallback exactly,
  `changed_channels=0`.
- Publishing pinned coarse from actual disk files changed
  **23,304 image channels** relative to nonresident output.
- Both virtual disk streams returned the expected errors:
  `missing page or incorrect RGBA8 page byte count`,
  one for `side=0` and one for `side=1`. They remained
  usable; the pinned coarse image was unchanged,
  `MISSING PAGE ORACLE coarse changed_channels=0`.
- Four independent expected-page wrap policies succeeded
  on **both** ordinary and ICB images (8 oracle passes):
  U=-0.125 and -0.0005 → Repeat selects left fine
  while right MirrorRepeat stays coarse; left region
  delta=210, right=0, observed left RGB delta
  (+104,-101,-5). U=+1.125 and +1.0005 →
  right MirrorRepeat selects fine and left Repeat stays
  coarse; right region delta=289, left=0,
  observed right RGB delta (+108,+67,-114).
- The native log concluded:
  `PASS: Renderer3D Shader Graph disk VT missing-page and
  Repeat/MirrorRepeat seam GPU ICB oracle`.

**Architecture/API actually exercised:** production
`render::graph::compile`, `Renderer3D` material graph
injection, per-command Metal GPU ICB with mixed Shader
Graph and virtual texture resources, disk-backed
`gfx::VirtualTextureFileStreamer`, native GPU RGBA8
page tables and physical atlases, two independently
owned archives/virtual layers, Repeat and MirrorRepeat
U-addressing, no-page fallback and a recoverable missing
file read. No CPU draw fallback can be counted as a
GPU ICB PASS because the fixture requires nonzero
mixed graph and VT submission counters.

**Change set for Stage 10CA:** no C++ or production
renderer changes after PASS. Documentation commit
`7eeb5d1168fa5e7c14d33b505dc7c733524587d2`
updated `docs/texture-rhi.md`, and
`578c937126e1cc9e9fa42c184955469581ec8b6a`
updated `skills/visuals/aiverse-texture-rhi/SKILL.md`.
This entry is the separate append-only agent handoff
commit recording actual native evidence.

**Important remaining limitations:** the test used
small **solid-color** 4x4 physical pages and
`gutterPixels=0`. It establishes correct page
selection at out-of-range UVs but does **not**
validate nonuniform bilinear/anisotropic filtering
near physical page boundaries, mip transition seams,
large archive/streaming stress, multiple graph source
counts, alpha/deformation paths or other GPUs and
GL/Vulkan/WebGL/mobile RHI parity. Test wall time is
not a GPU draw-timing or FPS improvement measurement.

**Next bounded feature/acceptance stage:** use the
current production `VirtualTextureSampling`,
`VirtualTextureFileStreamer` and native
`Renderer3D` without rewriting or replacing them.
Create an opt-in test with nonuniform neighboring
source tiles and edge-clamped/repeat/mirror gutters;
implement an **independent CPU bilinear expected-pixel
oracle** grounded in the original page assets. Sample
both sides of tile seams and mip0→coarse fallback with
gutter-aware UVs; require actual GPU pixel tolerances
and ordinary-vs-ICB parity, track reallocation/LRU.
Test separately on real Metal, and plan
GL/Vulkan/WebGL/mobile compatibility and scene
performance/driver soak. Keep every high-risk
CMake CTest default-OFF, experimental ICB flags
OFF in production and PR #21 Draft until the
remaining acceptance gates pass. For each
bounded code stage, first commit source, then
separate append-only handoff with exact SHA,
modified files, actual tests and next step.

## Stage 10CB — canonical bilinear physical gutters + independent native Metal oracle (2026-11-19)

**Branch / PR:** `feature/texture-rhi-unification`, Draft PR #21.
**Prior verified hardware:** Stage 10CA user-provided Apple M3 Ultra
`metal_renderer_graph_vt_seam_gpu` **1/1 PASS**, all seven
Renderer3D graph+VT mixed ICB image pairs byte-identical and
eight expected page-wrap color oracles PASS. That test used
**solid 4×4 virtual pages with zero physical gutters**; it
cannot establish true bilinear continuity for nonuniform tiles.

**Production issue found by code analysis:** the shared
`engine/gfx/VirtualTextureSampling.h` MSL/GLSL code used
an inset of `(gutterPixels+0.5)/pageDimension`.
For `gutterPixels>0` the physical atlas sample was constrained
to **interior texel centers** at a virtual page seam.
It could never blend across the first interior texel and
its neighboring gutter texel. A texture with constant page
colors hides the error; gradients and adjacent page content
are sensitive to it. For each virtual tile's UV coordinate
0 or 1, correct bilinear coverage is between gutter texel
and first/last interior texel; the normalized inset must
be `gutterPixels/pageDimension`. We retained the existing
0.5-pixel inset for `gutterPixels==0` (old behavior).

**Source fix commit**
`1af66f86a754958f6bacf84d279b14a163fca831`:
only `engine/gfx/VirtualTextureSampling.h` changed.
Both `gfx::kVirtualTextureGLSL` and `gfx::kVirtualTextureMSL`
now compute a conditional `sampleInset` for positive
vs zero physical gutter. Public API, page table encoding,
streaming queue and resource slot ABI unchanged.
The exact helper is reused by actual production
`Renderer3D` material graph shaders; the isolated test
uses that same canonical MSL literal.

**New native acceptance source commit**
`4787302de05704c2c48e7c9fede9a4813b5d2a33`:
`tests/render/metal_vt_bilinear_gpu.mm` (real Metal RHI,
macOS Objective-C++). It constructs two independent
**12×6 logical RGB gradients**, one for Repeat-U and one
for MirrorRepeat-U, both ClampToEdge-V. Each domain
uses two 8×8 RGBA8 **physical pages** containing a 6×6
interior and one neighboring-gutter texel on each edge.
The authored gutter bytes are computed from the adjacent
original logical texels, including the outer U wrap and
MirrorRepeat reflected boundary; they are not duplicated
from the local page interior. Each page is written to a
temporary file under an isolated directory and loaded
by the production `gfx::VirtualTextureFileStreamer`;
the test verifies two page GPU uploads per domain and
exact resident table bytes/slice mapping. Temporary
files and GPU streaming resources are released.

A real Metal render target shader embeds
`gfx::kVirtualTextureMSL`, samples the native physical
atlas with linear filtering through
`aiverse_sample_virtual`, and makes actual async
`Device::readTextureAsync` readbacks after each probe.
CPU expected colors are computed **independently** in
original full-resolution virtual-image logical space:
compute `address(uv)*logicalExtent - 0.5`, integer
Repeat/MirrorRepeat/Clamp source texel lookup, bilinear
blend of four known source texels. Crucially this oracle
does not use the shader's physical atlas, page-table,
tile UV, `inset`, or GPU results. A separate source
sensitivity guard requires the old 0.5-texel center-inset
mapping to differ by >=4 RGBA8 levels at the page seam;
a flat or incorrectly generated fixture cannot silently
pass. 17 U values × 4 V values × 2 wrap modes yields
**136 independent native fragment probe samples**,
including internal tile seam at U=0.5, Repeat/Mirror
outer UV boundaries, negative and >1 U, V=0/1.
Each 4×4 output is also checked spatially uniform;
each RGBA8 channel must match CPU expected by ≤2.
GPU errors, bad upload, unresolved pages, readback
timeouts or nonuniform render targets fail closed.

**CMake commit**
`f2778b19f29817b94c48e73ed65c6465fbf86a27`:
`AIVERSE_TEXTURE_METAL_VT_BILINEAR_GPU_TESTS=OFF`
(default) adds only on explicit macOS opt-in:
binary `aiverse_metal_vt_bilinear_gpu_test`,
CTest `metal_vt_bilinear_gpu`, 120-second timeout,
appropriate Metal/QuartzCore/Foundation/Threads links.
Does not include the new test in previous safe/default
CMake or risky GPU-ICB acceptance caches. The test
checks canonical Metal page sampling maths but does
NOT itself encode GPU indirect draw commands.

**Guide / reusable skill commits:**
`57d6b55187e6e2f2903df97194d60a772e6cb450`
(`docs/texture-rhi.md` Stage 10CB mathematical rationale,
test contract, commands, remaining matrix);
`42a7af375d32d154a9bf24d8ac5abcf59358a37e`
(`skills/visuals/aiverse-texture-rhi/SKILL.md`
handoff instructions); this append-only Stage 10CB
handoff is a **separate commit**.

**Actual verification in this agent environment:**
read production Metal VT fragment test, sampler, streamer
and model shader integration via connected GitHub;
reviewed all three changed sources and CMake registration.
A standalone pure JavaScript numerical model evaluated
the same 17 U × 4 V × 2 address-mode **mathematical cases**,
comparing original logical-texel bilinear values against
the new positive-gutter atlas mapping and the old legacy
center-inset mapping. The new mapping matched the source
bilinear model at the tested coordinates, while old
mapping showed substantial errors. This is only a
math/preflight check: **native macOS C++/ObjC++ compilation,
linking, MSL compilation, GPU execution, actual disk worker
and GPU page-table publication, asynchronous RGBA8 readback,
CTest and performance have NOT BEEN RUN** here.
Do not report Stage 10CB hardware PASS until the user
supplies the actual successful Mac Studio log.

**Exact next step on isolated Mac Studio texture worktree**
(`/Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819`,
NOT primary checkout with unrelated incomplete rebase):

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

**Acceptance gate:** native Mac successful build and
`Metal canonical VT bilinear: Apple ...` device banner;
`VT bilinear independent CPU oracle: samples=136
seam_samples=[positive] max_delta=[0..2]`;
`PASS: native Metal canonical VT bilinear nonuniform gutter
seam and Repeat/MirrorRepeat CPU pixel oracle`, CTest
**1/1 PASS**. On any first compiler or Metal
renderer/worker/oracle mismatch, capture exact diagnostic,
fix source, append next-stage handoff separately.
Do not run a stale executable after a failed build.

**Remaining / next agent:** after native PASS, rerun
10BX/10BY zero-gutter Shader Graph+VT mixed GPU ICB
tests to validate no regression from the shared shader
change, then add a production `Renderer3D` scene with
nonuniform authored edge gutters under mixed Metal GPU
ICB and independently predicted pixels; extend to
mip transitions and high-quality filtering. Vulkan/GL/
WebGL/mobile actual hardware acceptance, scale/stress
tests and representative GPU time/FPS remain open.
PR #21 stays **Draft**; experimental GPU ICB flags
remain OFF by default. New CMake test remains
default-OFF pending real native validation.

## Stage 10CC — observed nonuniform VT physical-gutter native PASS (2026-11-19)

**Branch:** `feature/texture-rhi-unification`; PR #21 Draft.
**Previous source feature/fix:** Stage 10CB
`1af66f86a754958f6bacf84d279b14a163fca831`
corrected production `engine/gfx/VirtualTextureSampling.h`
in both GLSL and Metal. For `gutterPixels>0`,
page boundary samples now include physically neighboring
gutter pixels instead of stopping at interior texel
centers; `gutterPixels=0` retains legacy center-clamp
semantics. Stage 10CB GPU test source
`4787302de05704c2c48e7c9fede9a4813b5d2a33`
created `tests/render/metal_vt_bilinear_gpu.mm` to
test the canonical MSL helper against an independent
logical-texture CPU bilinear oracle using nonuniform
logical texels, genuine 1-pixel neighbor gutters and
two production disk-backed native VT streams.

**User-supplied native Apple M3 Ultra hardware log:**
```text
[metal-assets] libraries=0 sourceMisses=1 pipelines=1 pipelineFailures=0 archive=0 strict=0
Metal canonical VT bilinear: Metal Apple M3 Ultra, page=8 interior=6 gutter=1
VT bilinear independent CPU oracle: samples=136 seam_samples=120 max_delta=1
PASS: native Metal canonical VT bilinear nonuniform gutter seam and Repeat/MirrorRepeat CPU pixel oracle
1/1 Test #122: metal_vt_bilinear_gpu ............ Passed 6.07 sec
100% tests passed, 0 tests failed out of 1
Total Test time (real) = 6.08 sec
```

**Verified on actual hardware:** native Metal production
sampler's `aiverse_sample_virtual`, uploaded physical
RGBA8 pages with actual neighbor gutters, native page
table/addressing and async GPU fragment readback all
matched the independent source-logical-pixel interpolation
within **1 LSB** at each of **136** UV probes (including
**120 seam-adjacent cases**). A guard separately rejects
the prior buggy center-of-interior formula with this
nonuniform pattern. No driver/shader pipeline failures
were observed. CTest wall duration 6.07 seconds is
not an isolated shader/GPU draw time.

**Scope and limitations:** the native GPU test exercises
the same canonical Metal MSL sampler later used in
`Renderer3D` graph material shaders, but the test is
a standalone direct Metal fragment oracle. It did
**not** execute Renderer3D or GPU-encoded ICB on a
nonuniform guttered model scene. The symmetric GLSL
source was changed but GL/Vulkan/mobile/WebGL hardware
acceptance remains unverified. Trilinear mip transition,
anisotropic filtering and driver soak/performance
are still open. Earlier Stage 10BX/10BY native GPU
PASS for Shader Graph+VT+ICB with **zero gutters**
predates the shared shader change; those previous
logs must not be represented as a post-fix regression
test.

**Stage 10CC files/commits** (no new production source
changes after the 10CB fix or GPU PASS):
- `23620dae0e5bf3101f58a2083eef56eddedeb547`:
  `docs/texture-rhi.md`, exact hardware results,
  distinction between direct Metal and ICB, and
  mandatory fail-fast zero-gutter regression commands.
- `86ce7a606e407b58842286fbf8702be12f645a01`:
  `skills/visuals/aiverse-texture-rhi/SKILL.md`,
  native PASS recorded and remaining regression gates.
- This **separate append-only handoff commit**
  records actual native verification, architectural
  boundaries, source revisions, residual risks and
  the next concrete agent action.

**Next exact agent step** (Mac Studio's isolated
`/Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819`
worktree only; never touch unrelated in-progress rebase
in the primary AIVERSE checkout):

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

Rebuild **both** binaries first (shared header change);
do not reuse a stale green executable after a failed
build. For Stage 10BX require all three pinned coarse,
fine and LRU image parity PASS, nonzero real mixed
graph/VT ICB counters and CTest 1/1. For Stage 10BY
require both missing-file failure callbacks, seven direct/
ICB pair PASS, eight independent Repeat/MirrorRepeat
page-selection oracles and CTest 1/1. If native compiler,
shader, GPU driver or pixel errors occur, capture first
real failure, fix source in a distinct code commit, then
append a new handoff.

**After the two regression PASS logs:** build a separate
default-OFF opt-in production `Renderer3D` Shader Graph
+ disk VT mixed Metal ICB scene acceptance fixture using
nonuniform gutters and an independently computed
expected-pixel oracle; additionally test dissimilar
mip coarsening and trilinear/anisotropic behavior.
Check same-code fallback and resource limits on Vulkan,
GL, WebGL and mobile. Stress/benchmark real GPU frame
timings and memory separately; do not infer performance
from CTest wall duration. Keep all experimental ICB
flags OFF by default; do **not** merge Draft PR #21
until cross-backend and stress gates succeed.

## Stage 10CD — full production nonuniform VT ICB scene and one-shot verification (2026-11-19)

**Branch/PR:** `feature/texture-rhi-unification`, Draft PR #21.
**Pre-existing observed native Apple M3 Ultra evidence:**
Stage 10BT low-level 1/4-source Metal ICB ABI 1/1 PASS;
Stage 10BV real Renderer3D 4-source Shader Graph ICB 1/1 PASS;
Stage 10BX real dual disk VT+Shader Graph+ICB coarse/fine/LRU
1/1 PASS; Stage 10CA missing-page and Repeat/MirrorRepeat
independent expected-page GPU ICB oracle 1/1 PASS;
Stage 10CC nonuniform-gutter canonical Metal VT bilinear
absolute CPU oracle 136 samples (120 seam), max_delta=1
and 1/1 PASS. **10BX/10CA logs predate the positive-gutter
shared shader correction and are NOT post-fix regression
evidence**. All these observations came from user logs,
not GitHub Actions CI.

**New code commits in this bounded Stage 10CD:**
- `1c4a3be68f829775c5af4894c1e15f1d265a08e0`
  adds `tests/render/metal_renderer_graph_vt_gutter_gpu.cpp`
  (new production Renderer3D + actual compiled Shader Graph
  with four connected source textures and VT, two independent
  raw-disk VT streaming domains, 8×8 nonuniform physical
  page images and one-pixel gutters from adjacent source
  texels; five direct-vs-real-GPU-ICB image pairs).
- `ed9a707e0f48102bd60c1b0640fd408cfb384325`
  updates `CMakeLists.txt` with default-OFF
  `AIVERSE_TEXTURE_METAL_GRAPH_VT_GUTTER_GPU_TESTS`;
  executable `aiverse_metal_renderer_graph_vt_gutter_gpu_test`
  and CTest `metal_renderer_graph_vt_gutter_gpu`.
  It receives the exact explicit Metal ICB per-command
  PBR, mixed graph, per-command VT and mixed VT flags
  in its own isolated test process.
- `04e9612189b9e10e4a218bce326af405bc8ddd37`
  adds `tools/texture-rhi-final-verify.sh`, a complete
  Mac-only fail-fast suite.
- `86254fca82f0866005fbd2508872f7ca05ede7d4`
  updates the suite to also run Python VT packing and
  shader feedback syntax tests when CMake registered.
- `2f8beaf3e5a3decfd7f461429d97c533354007bd`
  updates `docs/texture-rhi.md` with architecture,
  full command and exact proof limits.
- `2cb703d48fc7957cabd36c4b9e405ed7de961a14`
  updates `skills/visuals/aiverse-texture-rhi/SKILL.md`
  for the next agent.
- This **separate append-only handoff commit** records
  real current status and precise next actions.

**New full production ICB+gutter fixture architecture:**
Uses the standard `Renderer3D::beginScene`, `drawModel`,
`render`, `setGpuAuthoredDrawEnabled` and
`GpuModelDrawStats`; no test-only render pipeline or
special game ECS path. It compiles a version-1 Shader
Graph from JSON using `render::graph::compile`.
Four sampled graph source textures and the virtual
sample contribute to actual pixel color. Two materials
have independent source image handles and distinct
VT physical atlas, GPU page-table and virtual layer.
Their physical pages are **8×8** with a **6×6 interior**
of deterministic nonuniform R/G/B source pixels and
a one-texel gutter from adjacent source pixel
coordinates on all four edges. The logical source
size is 12×6 and the virtual domain contains
two mip0 pages per material. Both disk streams publish
their two pinned pages through real
`VirtualTextureFileStreamer::pump` on the render
thread. This uses existing production
`ModelVirtualTextureSampling` and the corrected
`gfx::kVirtualTextureMSL`, not duplicate shader math.

For U=0.40,0.48,0.50,0.52,0.60 and V=0.40,
the fixture captures ordinary and GPU ICB full
RGBA8 frames and demands byte-close (≤2 LSB)
complete-image parity and distinct material ROIs.
Each actual GPU ICB must submit one mixed graph+VT
group with at least two commands and no rejects
or execution failures. In addition, an independently
authored CPU **original logical-texel** bilinear
source oracle predicts channel directions and
the fractional color progress of the ROI for
U=0.48,0.50,0.52 relative to two measured
Renderer3D endpoint ROIs at U=0.40,0.60.
This endpoint calibration accommodates scene's
gamma/lighting transfer, while each prediction
depends only on the original logical image and
the authored UV (not the physical page table
or shader inset). Intermediate screen RGB
ROI error must be ≤3 display levels/channel,
and the source/scene gradients must be nontrivial.
The midpoint is sensitive to a half-texel
virtual page seam misalignment. This is
independent scene-level **metamorphic** validation,
not a full absolute rendered RGB reference.
Stage 10CC independently covers absolute Metal
canonical bilinear RGBA8 values.

**One-shot `tools/texture-rhi-final-verify.sh`:**
expects isolated `feature/texture-rhi-unification`
worktree on real macOS and current sources
already pulled; accepts `--jobs N` (default 4)
and optional `--full-engine`. It configures a
safe OFF-by-default suite; builds/runs **21**
portable texture/VT/shader-graph CTests plus
conditional VT packing/shader compiler checks;
with `--full-engine` it compiles and runs the
whole nonexperimental AIVERSE CTest suite.
Separately it configures a real Metal GPU suite
with all seven GPU acceptance options enabled,
builds ALL SEVEN target binaries before running
any CTest, then runs the seven tests
**sequentially in separate processes**.
The gate includes 10BT (graph ABI), 10BV
(graph Renderer3D), post-shader-fix 10BX
(coarse/fine/LRU), post-fix 10CA (missing,
wrap), 10CC (136-point absolute bilinear)
and the new 10CD full guttered graph/ICB test.
Each stage has strict CTest exit checking and
explicit success-marker validation to prevent
stale-binary/false-positive GPU claims.
Logs at `build/texture-final-logs`;
`acceptance.txt` with git SHA and
`result=PASS` appears only when all pass.

**Actual tests performed for Stage 10CD itself:**
source/API review of proven scene fixture and
reused production headers, connector-side CMake
and shell script source registration checks
only. The independent CPU logical gradient
and screen reference was reasoned from
actual source texel values; no new Mac
C++ compilation, Metal MSL compilation,
GPU execution, capture/readback, CTest or
pixel-oracle success was observed.
A temporary build container has no access
to Mac Studio's Apple GPU; a remote GitHub
file copy could not be downloaded due
restricted network/DNS; no Linux/macOS
build of the new fixture was performed.
**Do not promote Stage 10CD or the one-shot
suite to GPU PASS without the user's
new native verification log.**

**Next exact command in the isolated Mac Studio worktree**
(not the primary checkout with unrelated unfinished rebase):

```bash
set -euo pipefail
cd /Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819
git fetch origin feature/texture-rhi-unification
git pull --ff-only origin feature/texture-rhi-unification
bash tools/texture-rhi-final-verify.sh --jobs 4
# If the user also wants the whole default engine CTest suite:
# bash tools/texture-rhi-final-verify.sh --jobs 4 --full-engine
```

**Pass/fail interpretation:** Require the shell command
to exit 0 and produce
`build/texture-final-logs/acceptance.txt`
ending `result=PASS`, including the current
feature SHA. Independently inspect 7 native GPU
logs for actual hardware device banners,
nonzero mixed graph and VT ICB counters,
the 136-point absolute logical CPU oracle
`max_delta<=2`, post-fix nonresident/coarse/
fine/LRU/UV wrap regressions and the full new
nonuniform scene success marker
`PASS: production Renderer3D Shader Graph
nonuniform guttered disk VT mixed Metal GPU
ICB seam parity`. If the build/test fails,
inspect the FIRST error, fix it in its own
source commit and separately append a handoff.

**Open platform/performance validation (not
completed by a Metal one-shot):**
Vulkan/GL/GLES/WebGL/mobile hardware shader
and resource-slot parity; trilinear/anisotropic
mip transitions with disparate residency;
large streamed page churn and memory cap
stress; representative GPU timestamps,
real scene frame times and FPS. These
are separate backend/device gates, not
evidence of an immediate universal
production-ready Metal ICB.
PR #21 remains Draft; all experimental
GPU ICB defaults remain OFF.

## Stage 10CE — one-shot shell integrity fix and Mac syntax preflight (2026-11-19)

**Branch:** `feature/texture-rhi-unification`, Draft PR #21.
**Relevant prior source:** Stage 10CD full nonuniform guttered
production `Renderer3D` Shader Graph + disk VT + Metal GPU
ICB fixture `tests/render/metal_renderer_graph_vt_gutter_gpu.cpp`,
source `1c4a3be68f829775c5af4894c1e15f1d265a08e0`,
CMake gate `ed9a707e0f48102bd60c1b0640fd408cfb384325`.
The one-shot runner is in
`tools/texture-rhi-final-verify.sh`.

**Specific defect identified before user execution:**
after a first optional Python VT pack / shader syntax
test-selector edit, a remote GitHub refetch revealed
that the runner unexpectedly contained 333 lines,
duplicated its entire GPU-build/test block and
had an unterminated single-quoted optional-test
regex. This was a script editing error, NOT a
Metal or texture runtime failure. No Mac test
was invoked on the corrupted source.

**Corrective code commit:**
`6a6bb9f2a18cb494a93d44eb232907c0df2ad8fe`.
Regenerated the shell script from original clean
commit `04e9612189b9e10e4a218bce326af405bc8ddd37`
and replaced the entire optional-test condition
in one bounded source edit (including
`virtual_texture_pack` and optional
`virtual_texture_feedback_shaders/msl`).
The repaired script was fetched back from
the feature branch; it contains **142 lines**,
one optional-test CTest block, one `--full-engine`
block, one `gpu-build` block and one sequential
GPU tests loop. Its documented required success
markers for all seven tests are intact.
No production shader, RHI, streamer or test
C++ file changed in this bounded repair.

**Documentation/skill commits:**
`1dbca3c2306482d80e5c1c5b5bf1c752166b0285`
added Stage 10CE integrity/repro instructions in
`docs/texture-rhi.md`;
`81348a29171e4bc5fbfe14e9c29c5edec3e1f425`
updated `skills/visuals/aiverse-texture-rhi/SKILL.md`.
This is the separate append-only handoff commit
for the repair stage.

**Actual verification:** connector-side remote script
refetch and block-count/expected-source inspection
only. A model execution container could not access
the repo over DNS/network to run local Bash syntax
or CMake; the user's macOS shell/GPU has not run
the updated one-shot script. The user-provided
older Stage 10BT/10BV/10BX/10CA/10CC PASS
logs remain separate evidence of their original
source revisions; the new Stage 10CD source and
final runner remain **UNTESTED** on native hardware.

**Next exact user/agent command** (only the isolated
Mac Studio texture worktree, primary AIVERSE
has an unrelated unresolved rebase):

```bash
set -euo pipefail
cd /Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819
git fetch origin feature/texture-rhi-unification
git pull --ff-only origin feature/texture-rhi-unification
bash -n tools/texture-rhi-final-verify.sh
bash tools/texture-rhi-final-verify.sh --jobs 4
# Add --full-engine to also compile/test the full
# nonexperimental engine, not only the texture domain.
```

**Pass gate:** Bash syntax preflight 0; the
complete runner compiles and runs the portable
21-test texture suite plus any registered
packing/feedback checks, builds seven distinct
native Metal GPU test targets and runs them
serially. All actual CTest codes and required
native markers must PASS, especially
`PASS: production Renderer3D Shader Graph
nonuniform guttered disk VT mixed Metal GPU
ICB seam parity`. A successful suite emits
`build/texture-final-logs/acceptance.txt`
with the current feature SHA and `result=PASS`.
If syntax, compilation, GPU, ICB or independent
oracle fails, inspect the first actual log,
correct that code in a new commit and append
a further handoff. Do not skip or silently
mark broken tests green.
Keep experimental ICB OFF by default and
PR #21 Draft pending cross-platform/driver
and performance validation.

## Stage 10CF — detached HEAD one-shot Mac texture suite preflight fix (2026-11-19)

**Branch/PR:** `feature/texture-rhi-unification`, PR #21 Draft.
**Context:** Stage 10CD added a full compiled `Renderer3D`
nonuniform disk VT+Shader Graph mixed Metal GPU ICB
acceptance fixture and the one-shot runner
`tools/texture-rhi-final-verify.sh`; Stage 10CE
repaired a prior editing duplication and restored a
single clear shell script before native use.

**First actual user one-shot attempt:** after
`git fetch origin feature/texture-rhi-unification`
and `git pull --ff-only origin feature/texture-rhi-unification`
the worktree successfully fast-forwarded
`636e96d8b..67cd9a421`, and
`bash -n tools/texture-rhi-final-verify.sh`
returned successfully. The next command stopped
immediately before CMake with
`FAIL: run only in the feature/texture-rhi-unification
isolated worktree`. This was a shell preflight
misclassification, not a failing Metal/Vulkan/GL,
texture stream or Shader Graph test. The user's
isolated verification worktree was already intentionally
checked out at **detached HEAD**; therefore
`git branch --show-current` produced an empty name
even though the fetched source code was current.

**Source fix:** `1fd7dcba4b16cbf72c64d0f6c81c5be9415c23ce`
modifies **only**
`tools/texture-rhi-final-verify.sh`.
It replaces the brittle local-branch-name-only
guard with a strict commit identity comparison:
`git rev-parse HEAD` MUST equal
`git rev-parse refs/remotes/origin/
feature/texture-rhi-unification^{commit}`.
The remote-tracking ref must exist; no unnamed
or stale detached SHA can run. If symbolic-ref
does return a branch, only exactly
`feature/texture-rhi-unification` is allowed.
The source SHA and branch/detached state are
displayed. No source switch or local rebase
is performed. No native C++, Metal shader,
GPU ICB logic or asset data was changed.

**Documentation and agent skill:**
`f3018a40c4bab2f727f3f89d31b68618f1875646`
appended Stage 10CF to `docs/texture-rhi.md`;
`7b8e552d72b00dc2def870ab0e44451abd1aa9d2`
updated `skills/visuals/aiverse-texture-rhi/SKILL.md`;
this append-only handoff is a separately committed
stage record, preserving its native failure, source
fix and next action.

**Verification actually performed:** fetched the
GitHub script before modification, identified the
exact false-rejecting guard, replaced it using
GitHub connector, reviewed correct `HEAD` and
origin ref equality logic and its fail-closed
branch cases. The source update is committed.
The user supplied terminal evidence for the
prior fast-forward and preflight failure.
**Native one-shot full CMake/CTest, 21 portable tests,
seven native Metal GPU gates, the new Stage 10CD
nonuniform scene oracle and final acceptance.txt
are NOT RUN / NOT VERIFIED after this fix.**
Do not infer PASS from the earlier green
per-feature Metal GPU tests.

**Next exact one-shot command, detached HEAD safe:**

```bash
set -euo pipefail
cd /Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819
git fetch origin feature/texture-rhi-unification
git pull --ff-only origin feature/texture-rhi-unification
bash -n tools/texture-rhi-final-verify.sh
bash tools/texture-rhi-final-verify.sh --jobs 4
# Optional: append --full-engine for all default AIVERSE tests.
```

**Full success criteria:** script accepts the
exact fetched remote source SHA while detached;
both safe and GPU CMake configurations generate;
21 portable texture/VT/graph tests pass plus
optional tool-dependent checks; all **seven**
native Metal GPU test binaries build before
any native GPU tests run, then execute serially
and report their specific GPU success markers.
The real compiled Renderer3D four-source
nonuniform guttered virtual texture and GPU ICB
oracle must pass. The process exits 0 and
creates `build/texture-final-logs/acceptance.txt`
with the exact accepted SHA and `result=PASS`.
On any next error, capture the FIRST actual
CMake/compiler/Metal/pixel failure, fix code
and append another handoff with real results.
Keep Draft PR #21 and GPU ICB default OFF.

## Stage 10CG — task-named verification worktree correctly accepted by SHA (2026-11-19)

**Repository/branch:** `chromealex/AIVERSE`,
`feature/texture-rhi-unification`, Draft PR #21.

**Actual user's Mac Studio failure:** after fetching the latest
feature branch and fast-forwarding local checkout
`67cd9a421..954ed5a8d` in
`/Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819`,
`bash -n tools/texture-rhi-final-verify.sh` returned successfully.
The one-shot suite exited **before configure/build/CTest** with
`FAIL: refusing another local branch: verify-texture-rhi-20261119-150819`.
The worktree was NOT detached HEAD as previously inferred.
Its local branch is an independently named, valid
task-specific validation branch. The prior Stage 10CF
guard only allowed the exact feature name or detached
HEAD, despite already comparing the correct remote SHA.

**Fix source commit:**
`e7e012b01c6599c85c94f24f4a85197e4a616cf4`,
only `tools/texture-rhi-final-verify.sh`:
remove the unnecessary test
`CURRENT_BRANCH != EXPECTED_BRANCH`.
Keep `CURRENT_SHA=$(git rev-parse --verify HEAD)` and
`REMOTE_SHA=$(git rev-parse --verify
refs/remotes/origin/feature/texture-rhi-unification^{commit})`
with a mandatory **exact SHA match**. This safely
accepts local `verify-texture-rhi-...`, the feature
branch and detached HEAD only when their currently
checked-out code equals the freshly fetched remote
feature revision. A stale worktree, wrong SHA or
missing remote ref fails closed. The source revision
and actual local name are logged. Also delete
`build/texture-final-logs/acceptance.txt` when
starting a new run so old green evidence cannot
survive a failed subsequent run.

**Documentation/source tracing:**
`7e5728215e027bc0145bebcf751931ef4fcfa9fa`
updated `docs/texture-rhi.md` with Stage 10CG
failure/fix and exact retry;
`54d4371e6edbd33345e9eec94019634a7bc9902a`
updated `skills/visuals/aiverse-texture-rhi/SKILL.md`;
this is the required separate append-only handoff
commit, preserving source SHA, user evidence,
and next step.

**Tests actually performed:** the user performed
`git fetch`, `git pull --ff-only` and `bash -n`
on the **prior** runner successfully, then hit
the branch-name rejection. The corrected source
was written and inspected through GitHub.
**No fresh native `bash -n` of the corrected runner,
no CMake configure/build, no 21 portable CTests,
no seven native Metal GPU CTests and no full-run
PASS log have yet been observed.** Do not claim
test success based on earlier per-stage logs.
No GPU/texture production code was modified by
this bounded runner repair.

**Next exact one-shot Mac command, no checkout/rebase:**

```bash
set -euo pipefail
cd /Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819
git fetch origin feature/texture-rhi-unification
git pull --ff-only origin feature/texture-rhi-unification
bash -n tools/texture-rhi-final-verify.sh
bash tools/texture-rhi-final-verify.sh --jobs 4
```

Add `--full-engine` to test the whole
nonexperimental engine (outside the scoped
21 texture/VT/graph tests), as requested.
All native GPU gates run serially and stop on the
first actual failing stage. The sole accepted
full-suite PASS is exit 0 + a new
`build/texture-final-logs/acceptance.txt`
showing the exact current SHA, seven valid real
Metal test markers, and `result=PASS`.
If there is a compiler/Metal/CPU/ShaderGraph
failure, inspect the first failing test log,
fix it as a separate source commit and append a
new handoff stage. Keep Draft PR #21 and
experimental GPU ICB defaults OFF.

## Stage 10CH — native Metal gutter scene FAIL corrected and false CTest PASS prevented (2026-11-19)

**Branch/PR:** `feature/texture-rhi-unification`, Draft PR #21.
**Prior feature:** Stage 10CD introduced real
`tests/render/metal_renderer_graph_vt_gutter_gpu.cpp`,
a full production `Renderer3D` compiled four-source
Shader Graph + two disk-backed nonuniform VT atlases,
true one-pixel source gutters and native mixed GPU ICB.
Stage 10CG corrected source identity checking for
task-named isolated validation worktrees.

**Actual user-supplied Mac Studio Apple M3 Ultra
`metal_renderer_graph_vt_gutter_gpu` native log:**
- The unified suite reached the **last of seven** real
  Metal GPU CTests, #128, after building the new native
  GPU target and executing earlier stages. The final
  test ran under explicit PBR/Shader Graph/mixed VT
  GPU ICB environment flags.
- Two physical VT slices per archive published:
  `PASS: disk-backed nonuniform VT physical pages 0/1 published`.
- **All five** tested U values `0.400, 0.480,
  0.500, 0.520, 0.600` recorded
  `graph=1 mixedGraph=1 vt=1 mixedVt=1
  commands=2 rejected=0 errors=0`: the scene
  actually used compiled Shader Graph, virtual
  texture fragment resources and real Metal
  GPU-encoded ICB with two draw commands.
- All five full ordinary-vs-ICB framebuffer
  comparisons reported `bad_channels=0 max_delta=0`,
  **byte-identical** results on hardware.
- Yet the fixture printed ten failures (two
  per U sample):
  `FAIL: compiled Shader Graph draws distinct regions`
  and
  `FAIL: GPU ICB draws distinct graph-source regions`.
  The measured left-vs-right region RGB-distance
  values were `18,19,17,18,16` for intentionally
  *similar nonuniform gradient* materials, lower
  than the inherited **35** threshold designed for
  previous saturated solid-color scenes.
- The fixture's independent original-source
  CPU bilinear intermediate-ROI oracle was
  **skipped** because its global `failed`
  flag was set. Therefore the full Stage 10CD
  pass cannot be claimed from GPU ICB parity alone.
- The native process incorrectly returned zero
  through AppKit NSApplication termination (before
  `aiverseMain` could propagate `failed?1:0`).
  CTest consequently printed `1/1 Passed`
  despite actual `FAIL:` assertions. The
  higher-level one-shot shell script **correctly
  refused acceptance** because the required final
  `PASS: production Renderer3D Shader Graph
  nonuniform guttered disk VT mixed Metal GPU ICB
  seam parity` marker was absent.
  `build/texture-final-logs/acceptance.txt`
  did not exist by design: it is issued only
  after a complete passing one-shot run.

**Stage 10CH source fixes committed:**
1. `7a50a4c82462407596195b4dc1ea748af9545c3e` —
   `tests/render/metal_renderer_graph_vt_gutter_gpu.cpp`
   lowers ONLY this scene's inter-material color
   distance threshold from `35` to `12`,
   which continues to reject blank/identical
   material images but permits the observed
   genuine distinct gradient materials
   (delta 16..19). All stronger checks remain:
   a nontrivial independent original-logical-texel
   gradient in each channel, correct RGB direction,
   ≤3 display levels between calibrated logical
   source and scene intermediate ROI images, and
   ≤2 raw RGBA8 levels full-frame direct/ICB parity.
   In `callbacks.shutdown`, after normal renderer,
   model and streamer cleanup, enforce
   `finished` and `std::_Exit(1)` if any
   assertion failed. This ensures AppKit
   termination cannot produce an exit-zero
   false-positive for this fixture.
2. `035b6b81c8126a0ea9478ac5b194f9cf3b11561c` —
   `CMakeLists.txt` now sets
   `FAIL_REGULAR_EXPRESSION "FAIL:"` for
   **all seven** registered native Metal
   texture/Shader Graph/VT GPU CTests and
   `PASS_REGULAR_EXPRESSION` for the
   exact final nonuniform scene PASS marker.
   Even if an app's OS-level exit status
   is zero, CTest must report a FAIL for
   actual test error output. No experimental
   GPU feature was enabled by default.
3. `65b32f968ed2089c313d51cfb1e3fe89df850069` —
   `tools/texture-rhi-final-verify.sh`
   also scans each verbose native CTest log
   for `FAIL:` separately from the CTest
   status and success-marker validation.
4. `465967cbfd84384b768c62e68d5adc71e39e4d8a` —
   `tools/texture-rhi-final-verify.sh`
   clears old `acceptance.txt` and
   `failure.txt` before a new run and
   registers an EXIT trap which writes
   `build/texture-final-logs/failure.txt`
   with `result=FAIL`, current source SHA,
   script exit code, and log directory
   on **any failed one-shot run**.
   The existing `acceptance.txt` remains
   PASS-only and is created at the very end.
5. `290b2532cde3d43a3410cb3ef82ef0cff30bc6d7` —
   `docs/texture-rhi.md` Stage 10CH records
   exact user hardware evidence, why CTest
   falsely passed and concrete fixes/retry.
6. `d47a5cef4617255affa30c37bf66d97b98ed0854` —
   `skills/visuals/aiverse-texture-rhi/SKILL.md`
   documents false-positive detection and
   that independent logical source oracle
   has not yet run successfully.
7. This **separate append-only handoff commit**
   preserves the fix, changed paths, real
   tests and next step for another AI agent.

**Actual verification since fixes:** inspected
the failing concrete native log from user,
traced source `differentRegions` and
`compareCaptured`, and checked
`engine/platform/macos/PlatformMac.mm`
AppKit `applicationWillTerminate` / `run`
control flow. Committed the test fixture,
CMake and shell corrections through GitHub,
and reviewed their source snippets.
**NO Mac Studio native CMake compilation,
CTest, independent nonuniform-gutter
scene oracle or full-suite PASS has been
observed yet for the new fixed revision**.
Do not state "fully green" until the new
one-shot `acceptance.txt` exists and its
success markers are backed by native logs.

**Exact next user/agent command** in existing
task-named isolated Mac Studio worktree
(without branch switching or touching
primary checkout):

```bash
set -euo pipefail
cd /Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819
git fetch origin feature/texture-rhi-unification
git pull --ff-only origin feature/texture-rhi-unification
bash -n tools/texture-rhi-final-verify.sh
bash tools/texture-rhi-final-verify.sh --jobs 4
```

On success, expect
`build/texture-final-logs/acceptance.txt`
containing exactly the checked out HEAD
SHA and `result=PASS`, seven separate
native Metal CTest PASS logs without
`FAIL:` and, crucially, the scene-level
independent bilinear oracle PASS plus
the exact final Stage 10CD marker.
On failure, expect `failure.txt` and
the first failing `gpu-*.log`. Fix
the first actual compiler, page,
Metal pipeline or independent logical
scene-oracle mismatch in a new source
commit; append an independent handoff
for that bounded fix.

**Remaining outside this one-shot scope:**
real GL/Vulkan/GLES/mobile/WebGL shader
and resource-slot parity, trilinear mip
transition and anisotropy with
nonuniform pages, long-lived streamed
page churn and GPU frame-timing/FPS
profiling. Keep Draft PR #21 and
Metal GPU ICB disabled by default
until broader platform/driver evidence.

## Stage 10CI — full focused Texture RHI one-shot PASS, Apple M3 Ultra (2026-11-19)

**Repository / PR:** `chromealex/AIVERSE`,
`feature/texture-rhi-unification`, PR #21 Draft.
**ACTUAL tested source revision:**
`7494f32d313a2ac45050be02d25ca3ae97749086`.
This SHA was independently verified via GitHub file
lookup to contain the Stage 10CH native fixture
corrections, including ROI contrast threshold
`delta>12.0` and `std::_Exit(1)` for a
macOS application shutdown after a failed check.
The earlier Stage 10CH false-positive CTest run was
therefore not confused with this successful run.

**User-supplied final one-shot Mac Studio log:**
```text
sha=7494f32d313a2ac45050be02d25ca3ae97749086
safe_texture_tests=21
native_metal_gpu_tests=7
full_engine=0
result=PASS
PASS: all 21 portable texture/graph and 7 native Metal GPU tests
Logs: /Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819/build/texture-final-logs
```

**What ran:** `tools/texture-rhi-final-verify.sh
--jobs 4` configured/compiled 21 focused
portable texture, Shader Graph, VT and ICB-material
tests, ran them through fail-fast CTest, and built
seven independent native Metal GPU test binaries
before running each GPU CTest serially. The suite
has explicit success marker and FAIL-log checks,
and creates `acceptance.txt` only after
all required checks pass. The user supplied the
entire final native GPU CTest #128 output plus
the completed shell manifest. Results for the
earlier six GPU processes are grounded in the
fail-fast suite's successful end status and
mandatory marker checks, not individually pasted
full logs here. Optional tool-dependent shader
syntax/pack checks may also have run; do not
claim counts for optional checks without logs.

**Full production new guttered scene details:**
`metal_renderer_graph_vt_gutter_gpu` **1/1 PASS**,
test #128, 1.90 sec CTest wall time; 0 Metal
pipeline failures. Disk-backed native physical
VT slices `0/1` published successfully in
both independent 8x8 nonuniform-gutter archives.
Five UV points, `0.400`, `0.480`, `0.500`,
`0.520`, `0.600`, each recorded exactly
`graph=1 mixedGraph=1 vt=1 mixedVt=1
commands=2 rejected=0 errors=0`.
The two distinct material ROI color deltas
were 18/19/17/18/16 (all >12), and
**every direct/Metal GPU ICB full RGBA8
frame pair was byte-identical**:
`bad_channels=0 max_delta=0`.

The separate independent original-logical-source
bilinear scene oracle **ran and passed**, both
ordinary rendering and GPU ICB paths, two
material regions, three intermediate seam U
points (0.480,0.500,0.520), three RGB channels.
All authored source gradients and actual scene
channel directions agreed and errors were <=
**1 displayed RGB level** in the observed
calibrated expectation tests. The test printed:
```text
PASS: production Renderer3D graph VT nonuniform gutters independent CPU seam oracle
PASS: production Renderer3D Shader Graph nonuniform guttered disk VT mixed Metal GPU ICB seam parity
1/1 Test #128: metal_renderer_graph_vt_gutter_gpu ... Passed 1.90 sec
```
No `FAIL:` diagnostics appeared in the real
final native log. The prior aborted first run
had been correctly diagnosed as a threshold
mismatch and OS CTest false-positive issue;
Stage 10CH fixed both and this revision
passed its independent oracle.

**Changes this Stage 10CI:** no runtime,
shader, C++ fixture, CMake or shell code changes.
The authoritative native PASS was recorded in
`docs/texture-rhi.md` (commit
`17657ed8492c8f1fb416b0c10a53a0ed1a86ea6f`)
and the reusable agent skill
`skills/visuals/aiverse-texture-rhi/SKILL.md`
(commit `3e5bce0ee62c2fe9c4861ac9fd88350a7758cce2`).
This append-only handoff is a separate commit.
Documentation-only commits later advance the
branch head beyond the *tested* 7494f32d;
never attribute an unobserved Mac test run
to the newer docs HEAD.

**Coverage and remaining responsibilities:**
This is a complete **focused Texture RHI**
Mac Studio acceptance run at one SHA,
including earlier zero-gutter Shader Graph
and VT GPU ICB regression gates as well as
the new nonuniform bilinear/scene oracle.
It is **NOT** the entire engine CTest suite:
the manifest explicitly says `full_engine=0`.
It does NOT establish correctness on Vulkan,
GL/GLES, WebGL or mobile, trilinear/mipmap
transitions with mixed residency, anisotropic
sampling, long-running streaming/page churn
or scene FPS/GPU performance. These are
separate hardware/backend and scaling gates.
Metal experimental GPU ICB flags remain
default-OFF and PR #21 Draft.

**Next exact agent work:**
If a whole-engine regression is required,
rerun the one-shot with
`bash tools/texture-rhi-final-verify.sh
--jobs 4 --full-engine` on the latest
feature SHA, and record its actual results.
Then prioritize genuine GL/Vulkan/mobile
non-ICB texture/VT shader parity (respect
16-slot source texture limits), trilinear
mip transition/anisotropic seams, real GPU
timing and long-run disk-page stress.
Do not claim a new GPU PASS after commits
without a new native log. After every stage
append API/source SHA/changed files/actual
tests/limits/next action to this handoff.

## Stage 10CJ — source compilation coverage for GL330/GLES300 and full-engine regression gate (2026-11-19)

**Branch/PR:** `feature/texture-rhi-unification`, GitHub PR #21
still Draft, base branch `feature/universal-entity-inspector-fps`.
**Earlier hardware evidence:** Apple M3 Ultra one-shot
`tools/texture-rhi-final-verify.sh --jobs 4`
completed **21 portable focused CTests + seven native
Metal GPU CTests, all PASS**, at explicitly tested
commit `7494f32d313a2ac45050be02d25ca3ae97749086`.
The manifest reported `full_engine=0`. The full
engine nonexperimental suite and other GPU backends
were therefore **not** proven by that result.

**User request / chosen bounded next step:** continue
toward a full engine regression and GL/Vulkan/mobile
portability. We first inspected current public
AIVERSE test infrastructure and found an existing
`.github/workflows/texture-rhi.yml` Linux OpenEXR
ON/OFF and Vulkan-focused pipeline, plus the main
`.github/workflows/ci.yml` with Linux, Windows,
macOS, Android and Web platform builds. Existing
conditional `virtual_texture_feedback_shaders`
only compiles feedback GLSL. The modified canonical
`gfx::kVirtualTextureGLSL` in
`engine/gfx/VirtualTextureSampling.h` used by
GL/GLES had no dedicated GL330/GLES300 shader
compile test for positive physical gutters,
wrap modes, two-dimensional atlas/page table
and its legacy sample overload.

**New test source:**
`tests/render/check_virtual_texture_sampling_shaders.py`
commit `6d0d9a8d3ebec7f57c9c4657c2925d6460667b8e`,
with Python 3.8 compatibility follow-up
`f28377b7fc8ddb6a5af8451862c5917f8c33bec9`.
The script extracts the **actual production raw
GLSL and MSL strings** from
`engine/gfx/VirtualTextureSampling.h`,
rejects missing/ambiguous symbols, and compiles
complete fragment shader wrappers through real
`glslangValidator`:
  - `#version 330 core` OpenGL 3.3
  - `#version 300 es` GLES 3.0
The probes exercise address-aware
`aiverseLookupVirtual`,
`aiverseSampleVirtual`, and legacy clamp overloads.
On macOS, `--backend metal` additionally compiles
the same production `gfx::kVirtualTextureMSL`
through `xcrun -sdk macosx metal -c` and requires
an actual nonempty AIR artifact. No compiler found
for a *requested* backend is an explicit FAIL.
This is compile-time source validation, **not**
GL/Vulkan/Metal framebuffer pixel parity.
Python uses `from __future__ import annotations`
to support Python 3.8 on older developer machines.

**CMake registration:**
`d9f8e0396c86f26f2723a0cb169659930536a7f6`
added conditional `virtual_texture_sampling_glsl`
and `virtual_texture_sampling_msl` CTests.
Commit `20c79da8473abf99f56eedcc12e6f2e3fc1547bc`
repaired the GLSL test's CMake block scope:
it is now registered **inside** the
`Python3_Interpreter_FOUND AND AIVERSE_GLSLANG_VALIDATOR`
guard. MSL test is registered only when Apple,
Python and `xcrun` are detected.
No production engine or shader source was modified.

**Existing verification script integration:**
`tools/verify-texture-rhi.sh` now adds these optional
shader-compiler tests to its CTest include filter
and registration diagnostics, final corrected commit
`3a222231828d432a9cb04d4b7d9a69124fb2e2e6`.
`tools/texture-rhi-final-verify.sh` adds the same
toolchain-backed source compiles to the optional
safe-stage test matcher **without changing** its
21 portable / seven native Metal target list,
final corrected commit
`3a1a17b1fa061314398a21f40fedb6b69e5cb9a1`.

**Existing CI integration:**
`.github/workflows/texture-rhi.yml`, final corrected
commit `f40cbef2a14ce5df92daa9cb9152811a7333a57f`:
Linux OpenEXR ON/OFF matrix and Vulkan-focused
job install Ubuntu `glslang-tools`, ensuring
`glslangValidator` exists. Both jobs run
`ctest --no-tests=error --verbose
-R '^virtual_texture_sampling_glsl$'` explicitly
after their focused builds. This makes a missing
test registration or shader syntax error fail the
workflow, rather than silently SKIP. Existing
artifact uploads and Vulkan source tests remain.

**Important correction within this stage:**
The first connector code-based JS string replacements
mistakenly used a replacement *string* containing
literal `$'` (end-of-regex), which JavaScript
`replace` interprets as the suffix after the
match. That corrupted the focused/one-shot shell
regexes and duplicated YAML workflow sections.
All damaged files were **immediately detected
by a second GitHub fetch**, restored from the
pre-feature verified `00d7411a...` source
(or tested `7494f32d...` one-shot source), and
rewritten safely using direct line replacements /
replacement callbacks. Final checks refetched
all files: workflow 113 lines with two complete
strict CTest `-R` selectors and both upload
artifact steps; focused verifier 197 lines;
one-shot Mac verifier 175 lines, exactly one
GPU-build block and one sequential GPU loop,
two completed optional shader-test selectors;
CMake GLSL test under its correct feature guard.
There is no leftover malformed regex in the
current HEAD. Initial invalid intermediary
commits must not be used for validation.

**Actual verification completed by this agent:**
GitHub connector fetches/source inspection of
scripts, CMake guards, workflow, and test wrapper.
The prior 21+7 Mac GPU PASS was real user evidence
from the older tested revision.
**NOT RUN for Stage 10CJ changes:** an actual
`glslangValidator` compile on Linux, macOS
`xcrun metal` source compile, GitHub Actions
green status for this new commit, full-engine
CTest, Vulkan/GL/GLES/WebGL/mobile GPU frame
readback parity, performance/stress.
No tool here has direct access to the user's Mac,
so these are pending acceptance gates.

**Exact next Mac Studio full-engine verification**
in task-named isolated worktree (do not touch
the primary unfinished-rebase AIVERSE checkout):

```bash
set -euo pipefail
cd /Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819
git fetch origin feature/texture-rhi-unification
git pull --ff-only origin feature/texture-rhi-unification
bash -n tools/verify-texture-rhi.sh
bash -n tools/texture-rhi-final-verify.sh
bash tools/texture-rhi-final-verify.sh --jobs 4 --full-engine
```

Require new `acceptance.txt` with
`full_engine=1`, `result=PASS`, all default
CMake/CTest results plus native seven Metal
test markers, and no `FAIL:`. On Linux
CI check the actual `virtual_texture_sampling_glsl`
execution for both GL330/GLES300, and inspect
Vulkan `vulkan_backend` for a *real test* vs SKIP.
If any compiler error occurs, fix that actual
shader/compiler diagnostic, then add another
source commit and append-only handoff.

**Remaining beyond this bounded Stage 10CJ:**
real GL/Vulkan/GLES/WebGL/mobile rendering
and resource/slot parity with GPU pixel readbacks;
different mip resident/fallback trilinear seam
cases, anisotropic filtering, nonuniform gutter
stress and LRU/streaming churn, GPU timestamps/
FPS. Existing high-risk experimental Metal GPU
ICB flags are still default OFF and PR #21
remains Draft until intentionally approved.

## Stage 10CK — opt-in explicit-gradient virtual-texture atlas sampling (2026-11-19)

**Source SHA:** `ee63acda3a4eb9d4977aab717ccbe5ccfa8b75a4`.
**Branch:** `feature/texture-rhi-unification`. **PR:** #21 Draft; no main merge.
**Source changes:**
- `engine/gfx/VirtualTextureSampling.h`: both canonical embedded GLSL 330/GLES 300 and Metal shader snippets now retain an `AiverseVirtualPage.gradientScale` computed from the fallback resident mip and the physical gutter inset. This maps original virtual UV derivatives to continuous physical atlas UV derivatives, avoiding implicit derivative spikes when the chosen virtual page/slice changes.
- New **opt-in** overloads `aiverseSampleVirtualGrad(..., uvDx, uvDy, addressU, addressV)` / `aiverse_sample_virtual_grad(..., uvDx, uvDy, addressU, addressV)` and corresponding clamp-mode convenience overloads, with explicit derivative orientation for ClampToEdge (zero outside [0,1]), Repeat, and MirrorRepeat (negated derivative on alternate periods). Return the provided fallback color for invalid/unresident table entries. Caller passes original pre-wrap virtual UV gradients, not derivatives of `fract` or page-local atlas UV.
- Existing `aiverseSampleVirtual` / `aiverse_sample_virtual` entrypoints are unchanged; consuming shaders must explicitly opt in after backend verification.
- `tests/render/check_virtual_texture_sampling_shaders.py` now compiles references to **both** new addressed and legacy clamp-style gradient overloads in its generated real production GLSL and MSL fragments. It continues to fail when requested toolchains are absent.

**Important scope limits:** This stage addresses virtual-page discontinuity in derivative *mapping*, not full virtual-mip trilinear blending. The physical atlas descriptor currently allocates exactly **one mip per physical slice**, so real authored physical mip chains and anisotropic footprint/gutter fidelity need a subsequent RHI/runtime stage. This stage does not alter the page-table encoding, residency transition, streamer, material graph bindings, or the default sampler behavior. Use sufficiently populated gutters for footprints; no cross-mip or neighbor-page filtering guarantee yet. GLSL and MSL function availability must be verified on real compilers before activation. In particular, `textureGrad` sampler2DArray and Metal `gradient2d<float>` are currently source assumptions.

**Actual verification:** Source anchor uniqueness and two shader raw-string block presence checked during write, followed by GitHub API commit success. **NOT RUN on this source SHA:** glslangValidator, xcrun/Metal, C++, CMake/CTest, Mac GPU pixels, Vulkan, GL33/GLES3, WebGL2, iOS/Android. Never inherit the previous 21+7 Mac GPU PASS (tested on SHA `7494f32d`) as verification of this new code.

**Next stage:**
1. On the isolated Mac verification worktree, pull from `feature/texture-rhi-unification` and run `bash tools/texture-rhi-final-verify.sh --jobs 4 --full-engine`. Require `acceptance.txt` to state `full_engine=1`, `result=PASS`, and inspect the new `virtual_texture_sampling_msl` compiler probe; fix any actual MSL compiler diagnostic first. Linux texture CI must pass `virtual_texture_sampling_glsl` for GL330 and GLES300.
2. Add **real rendered** pixel-readback cases: affine/sheared UVs, mirror-repeat and mixed-address seams, nonuniform resident mip vs coarse fallback, and gradient LOD/aniso; compare explicit-grad to a CPU sampling reference. Build a second physical-page mip path and a genuine virtual coarse/fine transition representation before claiming trilinear support. Keep per-material source texture slot limits and sample fallback parity.
3. Keep GPU ICB experimental flags default OFF. PR #21 remains Draft until full-engine and cross-backend acceptance. Append another exact source SHA, actual test outputs, and next action after each stage.

**Reproduce from the user's Mac Studio without disturbing the primary unfinished rebase:**
```bash
set -euo pipefail
cd /Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819
git fetch origin feature/texture-rhi-unification
git pull --ff-only origin feature/texture-rhi-unification
bash -n tools/texture-rhi-final-verify.sh
bash tools/texture-rhi-final-verify.sh --jobs 4 --full-engine
```

## Stage 10CL — atomic streamed physical VT mip chains (2026-11-19)

**Source SHA:** `ccb0d391522af5da9fa79e0c2206b3d56988ff13`.
**Branch:** `feature/texture-rhi-unification`, **PR #21 Draft**, `main` untouched.

**Implemented source:**
- New `engine/gfx/VirtualTexturePhysicalMips.h`: `buildVirtualTexturePhysicalMips(base, baseBytes, side, levels, srgb, output)` builds RGBA8 per-physical-page mip chains, 2x2 linear-light and premultiplied-alpha reduction with clamp-to-edge at odd final texels and sRGB transfer when requested. Caller supplies guttered physical page; no automatic neighbor gutter synthesis.
- `engine/gfx/VirtualTextureStreamer.h`: `VirtualTextureStreamConfig.physicalMipLevels` defaults to **1** to preserve existing callers. Atlas allocation honors explicit `mipLevels`, capped by `maxTextureMipLevels` and physical size <=4096.
- `stageMipChain(key, vector<VirtualTextureMipView>, pin)`: accepts exact mip-count, exactly packed RGBA8 payloads per mip, snapshots every level into bounded existing `TextureStreamQueue`, pre-invalidates any LRU-evicted slot in GPU table, fails/cancels partial staging and abandons incomplete reservation.
- Legacy `stage(key, baseRGBA, bytes, pin)` preserves one-mip behavior and automatically generates the complete physical mip chain when opt-in `physicalMipLevels>1`. The already-connected `VirtualTextureFileStreamer` uses this method, thus existing disk archive format continues to work and receives optional physical mip chains.
- Batch tickets are tracked by generation and original root ticket. Page publication and completion callback only happen when *all* mip uploads report success; `cancel(rootTicket)` cancels every remaining queued mip and retires unpublished page; failure quarantines atlas. Ownership remains render-thread-only. `drain()` return value still counts **individual submitted GPU uploads**, not logical page completions.

**Explicit limitations:** The first ticket only identifies the entire batch for `cancel`. Existing one-mip behavior is the default and old pixel results should remain unchanged. The generated mips are **physical-page-local**, not two-level virtual parent-child trilinear sampling: page table still resolves exactly one fallback mip. Anisotropic seam quality depends on gutter width; high LOD with narrow gutters can bleed, so do not claim finished anisotropic or cross-mip seam fidelity. Cross-backend native atlas mip allocation/update support is not proven. Check Metal/Vulkan/GL array update per selected mip and driver capability.

**Verification:** GitHub source API fetch/anchor-based modification, blob-tree commit and branch ref update only. **No** compile, tests, GPU readbacks, CI or benchmark executed (user explicitly requested features first, tests after all features). Prior 21+7 Mac PASS belongs only to older SHA `7494f32d`.

**Next:** implement optional second (coarser) resident page-table mapping and explicit virtual-mip trilinear shader APIs on GLSL/MSL, with strict publishing/invalidation order for both tables. No default behavior change. Then create device-GPU pixel oracle and whole-engine regression. Append handoff after stage.

## Stage 10CM — indexed virtual mip lookups and trilinear shader helpers (2026-11-19)

**Code SHAs:** `93f4cbb2351145c8411f39a52070631abf47a2c4` (CPU/RHI) and `371812a6d3d21f2a478f7078e80d7062713d51a4` (GLSL/MSL).
**Branch/PR:** `feature/texture-rhi-unification`, #21 Draft, no `main` changes.

**Implemented:**
- `engine/gfx/VirtualTexturePageTable.h`: `initialize(...,bool indexVirtualMipLevels=false)`. Optional layout stores one full-size RGBA8 2DArray page-table slice per `logicalLayer * mipLevels + requestedMip`. The entry maps a base mip-zero tile to nearest READY page at requestedMip **or coarser**, with encoded resident virtual mip in B and physical array slice in RG. The unchanged default still has one slice per logical layer and existing `entry()` returns mip-zero mapping. CPU encoded data are capped at 32 MiB; every table slice has independently dirty-tracked rows and bounded uploads. New `entryForMip(x,y,logicalLayer,requestedMip)`, `selectableMipLevels()`, `tableArrayLayers()`.
- `engine/gfx/VirtualTextureStreamer.h`: config flag `indexedMipTable=false`; explicit `indexedMipPageTableTexture()`, `indexedMipTableLevels()` and `encodedPageTableForMip()`. Legacy `pageTableTexture()` deliberately fails closed when the indexed ABI is selected; renderers must not silently interpret indexed physical slice indices as logical layers. GPU page table is invalidated/flushed across **all** indexed requested-mip slices before evicted atlas storage is overwritten and before newly uploaded pages publish.
- `engine/gfx/VirtualTextureSampling.h`: both canonical GLSL and MSL `AiverseVirtualPage` carry resident mip. New `aiverseVirtualMipLod(uvDx,uvDy,virtualSizePixels,bias)` / `aiverse_virtual_mip_lod`; `aiverseSampleVirtualTrilinear(... virtualMipLod,virtualMipLevels,addressU,addressV)` / `aiverse_sample_virtual_trilinear` explicitly select floor/ceil virtual mip array slices and blend two nearest resident fallback samples, honoring original virtual UV gradients, physical gutters and wrap orientation. Clamp convenience overloads provided.
- `tests/render/check_virtual_texture_sampling_shaders.py` source compiler probes extended to invoke both new trilinear overloads and LOD helpers. No legacy shader entrypoint removed. **No automatic renderer opt-in yet.**

**Limits:** Indexed table consumes `baseTilesX*baseTilesY*virtualLayers*virtualMipLevels*4` CPU bytes (<=32MiB) and corresponding GPU array slices, which may exceed per-device layer limits and must fail closed at allocation. Sampling uses trilinear on **two requested virtual levels** with independent nearest-ready/coarse fallback. If no root page exists, fallback color may appear. Physical-page mip gradient/aniso quality still depends on authored gutter width; full seam parity and real anisotropic sampling are not yet GPU-proven. The table layout deliberately uses constant mip-zero XY for every requested mip, because RHI mip dimensions floor-divide odd base tile counts whereas logical virtual tile counts ceil-divide. This is an opt-in source contract, not a finished integrated material path.

**Verification:** Reviewed source changes through GitHub fetch and write commits. **No C++ compile, GLSL/MSL compile, CI, CTest or GPU readback run** for these SHAs (features first at user's request). Existing 21+7 Mac PASS belongs to older `7494f32d`.

**Next:** integrate selectable table and trilinear sampling with native model/terrain/material shader bindings under opt-in flag and physical resource slot caps, add actual framebuffer pixel oracles for affine/mirrored and mixed-residency transitions, then whole-engine tests. PR remains Draft, experimental ICB OFF.

## Stage 10CN — opt-in model and Shader Graph indexed virtual-mip trilinear integration (2026-11-19)

**Source SHAs:** `8f4be9465c2a9f2b31783b93137187ec43703bee` (first implementation), `9d3129ebfdc9b5feefadd1b5766e0acbab3db5e0` (C++ shader string concatenation), `fe3b78bc57ac747be81d86ae49b04fcb8f884382` (Metal direct-draw preprocessor uniform aliases and signed-zero hash fix).
**Branch/PR:** `feature/texture-rhi-unification`, PR #21 Draft; `main` untouched.

**Code changes:**
- `engine/render/ModelVirtualTextureSampling.h`: `ModelVirtualTextureBinding` now optionally declares `indexedMipTable`, `virtualMipLevels`, `virtualWidthPixels`, `virtualHeightPixels`, `virtualMipBias`. Validation requires finite positive virtual resolution and 1..30 mip levels only when indexed table is enabled. Hash includes trilinear identity and parameters; `-0` bias canonicalized to avoid unequal hashes of equal material bindings.
- New `ModelVirtualTextureTrilinearUniforms` is two aligned vec4 values (32B). Legacy `ModelVirtualTextureUniforms` remains EXACTLY 16B and is still used by default. `modelVirtualSampleExpression` emits the same canonical sample expressions for built-in base-colour replacement and Shader Graph's `SG_SAMPLE_VIRTUAL`, avoiding variant divergence.
- `injectModelVirtualTextureSampling(...,bool extendedMetalSlots=false,bool trilinear=false)` preserves existing default; indexed mode emits U6 two vec4 uniforms and Metal struct with two float4 entries at the existing `buffer(7)` binding. Metal uniform preprocessor aliases go BEFORE `#if defined(AIVERSE_ICB_PBR)`, while ICB-only texture aliases remain inside its guarded block. The generated fragment sampling uses original `dFdx/dfdx` and `dFdy/dfdy` UVs and `aiverseVirtualMipLod` helper with supplied virtual resolution/bias.
- `engine/render/Renderer3DModels.cpp`: new shader permutation bit `kKeyModelVirtualTrilinear=1u<<31` when existing VT resolver returns a valid indexed binding. Serializes U6 32B only in this mode, maintains 16B in legacy. Atlas and table still reuse existing two slots including Shader Graph (Metal 17/18 with existing constraints, other backends <=15). Explicitly excludes indexed trilinear variant from experimental Metal PBR ICB graph candidate to avoid unsupported GPU encoded argument behavior. Other scene/selection/shadow pipelines unchanged.
- No automatic activation: the caller's `ModelVirtualTextureResolver` must bind `VirtualTextureStreamer::indexedMipPageTableTexture()` and fill the matching metadata. Legacy callers should continue to use `pageTableTexture()`.

**Testing / warnings:** Changes committed and inspected via GitHub fetch. User requested features BEFORE testing, so **no C++ compilation, shader compilation, CTest, Mac/GL/Vulkan pixel readback, benchmark, or CI were executed on these SHAs**. In particular, the exact Metal uniform layout, GLSL/Msl permutations, native sampler/resource count, and compiler acceptance are **UNVERIFIED**. Prior 21+7 Mac PASS is only on old SHA `7494f32d`, not these commits.

**Further feature work before final tests:** shader capability/array-layer failure diagnostics, optional per-archive resolver helper for deterministic metadata, sRGB/alpha and gutter/LOD quality policies, eviction/streaming stress protection, realistic platform support caps. After features are finished perform ONE Mac full-engine validation and focused backend shader compilation, then real GPU framebuffer pixel parity and stress/performance. Keep #21 Draft and experimental ICB OFF.

## Stage 10CO — validated archive-to-material trilinear setup (2026-11-19)

**Source SHA:** `ccb0de9114fd92321d9785db85aeb53b7a432d0b`. **Branch:** `feature/texture-rhi-unification`, PR #21 Draft, `main` untouched.
**New file:** `engine/render/ModelVirtualTextureArchiveBinding.h`; helper `makeModelVirtualTextureArchiveBinding(archive,streamer,logicalLayer,out,mipBias)`, overloaded for native `VirtualTextureStreamer` or disk-backed `VirtualTextureFileStreamer`. The resolver now has an opt-in convenience that extracts real archived width/height, gutter, addressU/addressV, virtual mip levels, page-table ABI and physical handles; rejects unpublished archive, inactive streamer, mismatched base tile extents, mip/layers/page size and invalid layer / unrepresentable binding.
**Other files:** `VirtualTextureArchiveVersion::streamConfig(physicalPages=128,indexedMipTable=false,physicalMipLevels=1)` preserves existing calls; native streamer exposes read-only `configuration()` and disk facade exposes `atlasStreamer()`, `indexedMipPageTableTexture()`, `indexedMipTableLevels()`. The helper does not change ownership, mutate GPU state, or auto-enable indexed VT.
**Actual checks:** GitHub API staged blobs/tree/commit and branch ref success only. No CMake, compile, CTest, GPU or CI run because user requested all features before tests. Stage 10CO is **unverified**. Prior old 21+7 Metal GPU PASS applies only to SHA `7494f32d`.
**Limitations and next:** Renderer is still opt-in, and table/physical sampler capabilities remain GPU-untested. Guard archive metadata against pathological gutter overflow; optimize `VirtualTexturePageTable::rebuild` to avoid repeated O(tiles*mips^2) cache queries and full rescans when mappings are unchanged. Verify backend array mips, sampler anisotropy, sRGB visual parity and LRU before merging PR #21. Experimental ICB remains OFF.

## Stage 10CP — avoid full VT table rebuild every frame; linear mip scan (2026-11-19)

**Code SHA:** `9eabd01129657b1687ad843e8ed763dc835298ac`. **Branch:** `feature/texture-rhi-unification`; PR #21 Draft; main untouched.

**Changes:**
- `engine/gfx/VirtualTexturePages.h`: new monotonic `mappingRevision()`. Incremented when physical slot mapping or READY state changes on accepted reserve, complete, eviction or clear. Does not increment for touch/pin, which do not change shader page lookup.
- `engine/gfx/VirtualTexturePageTable.h`: `rebuild` fast-returns if the cache mapping revision is unchanged. On a change, scan parent mip levels coarse-to-fine once per base tile to retain nearest-ready parent for each requested virtual mip; changes complexity of indexed table resolution from O(baseTiles * virtualMipLevels^2) to O(baseTiles * virtualMipLevels). Maintains exact RGBA8 entry encoding, dirtyRows behavior and upload budgets. Clear invalidates cached revision.
- `engine/render/ModelVirtualTextureArchiveBinding.h`: overflow-safe gutter validation using 64-bit arithmetic.

**Actual validation:** Source staged via GitHub API and committed to branch; no compile, tests, shader compile or GPU work executed (features-first instruction). Runtime/perf characteristics are theoretical complexity, not measured timings.

**Open work and next:** This is still a full CPU rebuild on each residency change. For high churn, add bounded dirty-region updates per changed page and track cross-frame upload budgets; verify correctness against full-rebuild oracle in final tests. Check multi-frame batch publication/cancellation and indexed mip blending with real Metal framebuffer pixel results. Analyze texture sampler caps, virtual gutter quality and cross-backend compatibility. Final CTest only after functional stages are ready; keep PR Draft.

## Stage 10CQ — bounded incremental page-table dirty rectangles (2026-11-19)

**Source SHA** `6bfa3872d3d7d6ac8b3e478d85b87d78e3cb6806`, branch `feature/texture-rhi-unification`, PR #21 Draft.

**Implementation:** `VirtualTexturePageCache` records up to 512 `(revision, VirtualPageKey)` entries for newly reserved, evicted, completed or cancelled pages, including both old and replacement keys when reusing an LRU slot. `mappingChangesSince(revision,out,maxKeys=128)` fails closed if any event fell out of retained history or the caller budget was exceeded; clear/reinit also invalidates incremental history. Touch/pin do not change mappings.

`VirtualTexturePageTable::rebuild` now uses this history after the first full rebuild: calculates each changed key's mip-zero XY coverage with 64-bit shift bounds, updates affected tiles only when total visited cells stay below half the entire page-table size, otherwise falls back to the full reference scan. Each visited tile resolves all selected virtual mips in one descending pass. A changed mapping increments the revision but never publishes an in-flight page. The existing dirty-row GPU flush budget remains unchanged and is not bypassed.

**Coverage boundary:** This is committed source only: no CPU compile/CTest, real GPU tests or performance measurements run. No measured speedup claim. Need validate overlapping changed page rectangles, history truncation, partial-vs-full byte equivalence, eviction and async cancellation, budget flush, staged multisubresource publication, and multi-backend rendering in the final one-shot suite.

**Next:** add CPU reference-parity test fixtures and GPU native shader pixel oracles after remaining functionality, examine anisotropic/wrap gutter and memory limits, then run all CTests and native Metal verification as a separate final stage. PR #21 remains Draft; experimental ICB flags OFF.

## Stage 10CR — explicit GPU zero initialization of indexed VT table (2026-11-19)

**Source SHA:** `d64ba9ebe10587f982618e383ecceaffa91c86fc`. **Branch:** `feature/texture-rhi-unification`, PR #21 Draft.

**Scope:** `VirtualTexturePageTable::createGpuTexture(Device&,bool explicitSliceInitialization=false)` now permits explicit indexed table initialization: allocate empty RGBA8 Texture2DArray storage and retain all CPU table rows marked dirty. Existing one-slice legacy callers retain the original data-pointer initialization behavior. In `VirtualTextureStreamer::initialize`, if `indexedMipTable=true`, the streamer runs `synchronizeTable()` before returning true, flushing all zero table slices through existing bounded `updateTextureRegion` dirty-row uploads. Any rejected upload or insufficient complete-row flush budget fails initialization and destroys both atlas and table; never exposes undefined shader mappings.

**Test status:** Source committed via GitHub connector and reviewed at edit time. No native tests, CMake, GLSL/MSL compilation, framebuffer pixel or driver validation run for current SHA (user requested features first). This is a safety mechanism, not a confirmed GPU-fence/synchronization guarantee beyond Device::updateTextureRegion's existing contract.

**Still needed:** native C++/Metal/GLSL compilation, real backend array layer/mip upload safety and GPU framebuffer oracles; CPU parity for incremental indexed table; sRGB/gutter/aniso quality; streaming churn/perf; whole-engine regression. Keep #21 Draft, no defaults changed except explicit opt-in indexed mode. Next agent should implement remaining quality safeguards and regression fixtures, then perform one final full-engine test pass.

## Stage 10CS — opt-in virtual-footprint anisotropic VT multi-page sampling (2026-11-19)

**Source SHAs:** `f26eaf983dd2792a6a4c688830864b69e9d67ed0` (GLSL/MSL helpers/compiler probes), `916896ea76062df59144b3614d2f80763a045001` (model/Shader Graph material integration). Branch `feature/texture-rhi-unification`, PR #21 Draft; `main` untouched.

**Features:**
- `engine/gfx/VirtualTextureSampling.h`: opt-in `aiverseSampleVirtualAnisotropic` / `aiverse_sample_virtual_anisotropic`, including clamp convenience overload. In fragment shaders, derive major/minor virtual-pixel UV footprints, cap the tap count at 8, distribute taps along major derivative and individually perform complete virtual table lookups + resident mip trilinear fallback. Distribute derivative across taps and compute an effective minor/remaining-major mip LOD. This avoids relying on hardware anisotropy to cross physical array slices, and respects independently wrapped/mirrored pages and missing fallback colour.
- `tests/render/check_virtual_texture_sampling_shaders.py` has canonical GLSL330/GLES300 and macOS Metal compile probes calling addressed and clamp variants. No toolchain was run.
- `engine/render/ModelVirtualTextureSampling.h`: binding gets `virtualAnisotropyTaps=1` by default, accepts 1/2/4/8 only for indexed virtual mip table, adds tap count to the grouping hash. Indexed material U6 expands from the Stage 10CN 32B to **three vec4 = 48B** (base 16B + virtual size/mips/bias 16B + anisotropy quality 16B); **legacy still sends 16B**. Built-in model base and Shader Graph share one `modelVirtualSampleExpression` emitting the anisotropic function with the selected uniform taps. Metal parameter struct/aliases gain `quality`.
- `engine/render/Renderer3DModels.cpp`: supplies `anisotropyTaps` in the third vec4. `ModelVirtualTextureArchiveBinding.h` helper accepts optional final `anisotropyTaps` argument (default 1); legacy archive resolver continues unchanged. Same two texture slots and same dedicated trilinear pipeline permutation. Experimental ICB variant still excluded from indexed mode.

**Limits:** Manual anisotropy adds up to eight virtual table + atlas lookups per fragment and is **not** automatically selected; disabled by default (taps=1). It is approximate and scene-specific. A narrow/incorrect gutter can still cause edge artifacts at some physical mip/UV footprints; generated mip pages are box-filtered locally. No claim of strict vendor hardware anisotropic equivalence, derivative precision, filter quality, or driver acceptance. Materials must provide verified virtual resolution, address policy, physical atlas mips and indexed table; unsupported backends should fail closed. The 48B U6 ABI is new for indexed shaders only.

**Actual tests:** NONE on source SHAs; only GitHub source staged and committed. This is intentional features-first development. Old 21 portable + seven Metal GPU PASS at `7494f32d` does NOT verify this stage. Need full-engine build/CTest, native GL330/GLES300/MSL shader compilers, real Metal pixel oracle for repeat/mirror, fractional anisotropy, nonuniform gutters and mixed mip residency, and perf comparison 1/2/4/8 taps.

**Next:** Finish any remaining resource/perf instrumentation and runtime clamps, then write reference image/readback oracle and run the single requested final verification on Mac Studio. Keep PR #21 Draft; no changes to `main`.

## Stage 10CT — reject impossible physical mip chain and combined page table GPU budgets (2026-11-19)

**Source SHA:** `bf327614989b7756d654b590efe46fe51f63b28f`. **Branch:** `feature/texture-rhi-unification`, PR #21 Draft, `main` untouched.

**Changes:**
- `engine/gfx/VirtualTexturePhysicalMips.h` adds `virtualTexturePhysicalMipChainBytes(side,levels)` for exact uncompressed RGBA8 per-page physical mip storage (sum of sizes from base mip to authored count).
- `TextureStreamQueue::configuration()` adds read-only access to CPU/GPU/upload budgets already enforced by the queue.
- `VirtualTextureStreamer::initialize()` rejects configurations where (1) a full base page cannot fit in one frame's upload byte budget, (2) all physical mip subresources cannot fit simultaneously within stagingBytes, (3) the page-table flush budget cannot hold one complete row, (4) the estimated physical atlas PLUS either legacy or indexed table exceeds the configured GPU budget, (5) uploadsPerFrame==0 or metadata arithmetic overflows. This is fail-fast before allocating a GPU atlas or starting disk processing; avoids repeating uploads that are impossible by contract. The existing TextureStreamQueue still enforces limits during staging.
- `VirtualTextureStreamer::estimatedGpuAllocatedBytes()` includes the directly-owned page-table estimate in addition to queue-owned atlas bytes, unlike `streamStats().gpuAllocatedBytes` which intentionally covers only queue resources. Estimates intentionally exclude driver alignment, extra transient staging, compression and sparse allocation.

**Actual verification:** Source write/blob-tree commit through GitHub only. User requested all functional changes before tests, so no compiler, CMake/CTest, backend, driver, perf or stress tests run on this SHA. The old 21+7 GPU PASS remains evidence only for `7494f32d`.

**Next:** source feature review and necessary fixes, then record exact final feature freeze SHA and run 1 full Mac regression + canonical GLSL/MSL compilers + real native GPU readbacks + CPU reference stress. Full cross-backend/mobile/CI validation remains separate until actually executed. PR Draft, experimental ICB OFF.

## Stage 10CU — sRGB physical mip CPU conversion without hot-loop pow (2026-11-19)

**Source SHA:** `dae9aade812d943884b79ed4d8c99bd935af7d01`. **Branch:** `feature/texture-rhi-unification`, PR #21 Draft.

**Implementation:** In `engine/gfx/VirtualTexturePhysicalMips.h`, sRGB decoding now uses one function-local static monotonic 256-entry linear-light lookup table initialized once. Each source RGB code is converted to premultiplied linear light in O(1), and the output is quantized via `std::lower_bound` on the same table to the nearest representable sRGB code under linear-light distance (O(log 256)); linear RGBA8 retains direct byte encode. This removes repeated `pow` from per-pixel generation and avoids gamma-space average artifacts. Alpha remains linearly averaged and colours premultiplied while filtering; generated mip layout, dimensions, and I/O API are unchanged.

**Actual verification:** Source pushed via GitHub connector, not compiled, tested, benchmarked, or GPU-validated. The CPU mip kernel should receive golden-image and performance reference fixtures in the final verification stage. No speedup number asserted. Prior old Mac 21+7 PASS is still for `7494f32d` only.

**Next:** feature freeze and final cross-backend acceptance tests including indexed residency partial-vs-full reference, mixed-mip anisotropic seams, GPU atlas mips, model permutations, streaming budgets and performance. Keep #21 Draft and experimental ICB OFF.

## Stage 10CV — final-gate CPU fixture for physical virtual mips and streaming (2026-11-19)

**Source commit SHA:** `40398dae9129a87681f7b152786b1d7e7ba876e7`
**Branch:** `feature/texture-rhi-unification`; **PR:** #21 Draft; `main` unchanged.

**New source:** `tests/render/virtual_texture_mip_pipeline.cpp` is a real C++ test intended to compile against the production `VirtualTextureStreamer.h` and `VirtualTexturePageTable.h` and a mock RHI. It covers:
- RGBA8 mip-chain sizes, linear vs sRGB averaging, alpha-premultiplied fringe prevention, invalid/truncated page buffers.
- Indexed page-table array layers, nearest resident coarse fallback, unchanged-revision no-op, and incremental rebuild compared byte-for-byte to a newly initialized **full-reference rebuild**, through many LRU page changes including bounded journal history overflow.
- Explicit indexed zero-initialization, multi-frame 4-mip upload publication only after all levels complete, no early READY, root ticket cancellation after partial upload, re-staging and failure quarantine.
- Early rejection of impossible staging/GPU/table-row budgets without allocating GPU textures.

**CMake + script integration:** added `aiverse_virtual_texture_mip_pipeline_test` target and `virtual_texture_mip_pipeline` CTest in `CMakeLists.txt`. `tools/verify-texture-rhi.sh` includes it in focused build targets and test regex (CPU/Vulkan modes). `tools/texture-rhi-final-verify.sh` includes it in safe targets/test names; the expected safe CTest count becomes **22** (21 older + 1 new) and the seven native Metal GPU targets stay unchanged. Existing Linux texture PR CI invokes the updated focused script so it is automatically included in its OpenEXR ON/OFF and Vulkan compile jobs.

**Actual validation:** GitHub source fetches, targeted string/anchor replacement checks and Git blob/tree/commit/update_ref only. **NOT RUN**: C++ compiler, CMake/CTest, GLSL/MSL shader compiler, macOS/Metal native GPU or performance. The user requested features first, then a single validation phase; a test added to source must not be called PASS until executed. Older 21 portable + seven Metal GPU PASS was observed at `7494f32d`, not this SHA.

**Next exact action:** finish remaining source fixes and validate CMake registration, compiler-backed canonical shaders, model material permutations and system package dependencies. Run the one-shot Mac Studio command in isolated worktree:
```bash
set -euo pipefail
cd /Users/aleksandrfeer/Projects/AIVERSE-texture-verify-20261119-150819
git fetch origin feature/texture-rhi-unification
git pull --ff-only origin feature/texture-rhi-unification
bash -n tools/verify-texture-rhi.sh
bash -n tools/texture-rhi-final-verify.sh
bash tools/texture-rhi-final-verify.sh --jobs 4 --full-engine
```
Require actual new SHA and `result=PASS`, `full_engine=1`, `safe_texture_tests=22`, `native_metal_gpu_tests=7` in current `build/texture-final-logs/acceptance.txt` and absence of `FAIL:` log assertions. Inspect whether shader compiler tests truly ran, not just registered. Need additional GPU pixel oracles for mixed virtual mips, anisotropy and indexed shader model path. Keep high-risk experimental ICB disabled by default; do not merge #21 until certified.

## Stage 10CW — feature-branch Linux focused validation trigger (2026-11-19)

**Code SHA:** `0ad0d8a9264f97c72d3e06130b7b1cf298f832c7`.
**Source:** `.github/workflows/texture-rhi.yml`. **Branch:** `feature/texture-rhi-unification`, PR #21 Draft, `main` unchanged.

**Change:** Add an explicit feature-branch `push` event with source and script path filters, in addition to the existing `workflow_dispatch` and `pull_request` events. This makes GitHub Actions run the existing Linux OpenEXR OFF/ON and Vulkan-focused build matrix when the texture implementation branch advances, rather than depending exclusively on a potentially stale/draft PR event. Tests/targets are inherited from the Stage 10CV updated `tools/verify-texture-rhi.sh`, including new `virtual_texture_mip_pipeline` CTest. This workflow is not a Mac Studio, Metal GPU, Android, WebGL or Windows validation, and does not verify full-engine CTest.

**Execution evidence at this writing:** GitHub Actions workflow run `44604340610` for exact SHA `0ad0d8a9...` is **QUEUED**, with three queued jobs: Vulkan compute/source CTests; Linux texture OpenEXR=ON; Linux texture OpenEXR=OFF. No job has passed or failed yet. This is an actual scheduled GitHub workflow run, not a hardware test outcome. Inspect the run's exact logs and all job conclusions before reporting PASS. The old 21+7 Mac GPU PASS remains at `7494f32d` only.

**Next:** review real GitHub Actions job logs once run concludes; fix any build/GLSL regression with source commit and separate handoff. Then run macOS whole-engine + seven native Metal GPU acceptance on latest feature source SHA. Do not infer green GPU tests from Linux shader compilation, and do not merge PR #21 until all necessary gates pass.

## Stage 10CX — page-table cache-identity revision safety (2026-11-19)

**Code SHA:** `fbaaf8ed87840d7cea7d2d931991a3673cb43184`. **Branch:** `feature/texture-rhi-unification`; PR #21 Draft, no main merge.

**Bug/fix:** Two distinct `VirtualTexturePageCache` instances can have identical geometry and `mappingRevision()` counters but different physical resident pages. A long-lived `VirtualTexturePageTable::rebuild(newCache)` could incorrectly fast-return on equal revision and retain old sampled slice mappings. `VirtualTexturePageTable` now also records the non-owning identity `lastCache_`; skips / incremental dirty-region rebuilds are permitted only if both pointer identity and mapping revision match the same cache. New cache identity forcibly triggers a full rebuild, refreshing invalid old entries and new same-layer fallback. Clear resets pointer+revision. It never dereferences the cached pointer.

**Regression:** extended `tests/render/virtual_texture_mip_pipeline.cpp` with two independent caches whose revision/geometry match but ready tile coordinates differ, and asserts table rebind invalidates stale samples. Existing streaming and indexed table CTests unchanged.

**Validation:** GitHub source commit and ref update succeeded; new source/test not yet compiled/executed. The Linux feature-branch CI trigger is available, but prior run `44604340610` was only queued when last observed; a newer source push may cancel it and schedule a new run. Confirm exact SHA and individual job results before declaring a PASS. Full-engine Mac Studio and native Metal GPU tests remain outstanding.


## 2026-11-20 - Stage 3 integration native acceptance

Merge `38dbd0602119067425f4f1a776466e5e686131f5` on
`integration/texture-rhi-main-stage3-20261120` preserves pinned main
`f9aed9a705982e2c6b77b29869e309995ce8b163`. Existing owner-run full
CTest 289/289 was confirmed in local logs. This agent built and ran seven
native Texture RHI Metal tests: 7/7 PASS (23.54s), mandatory execution
markers present, zero FAIL assertions. Skill checks passed. Unified runner
no-build guard rejected unspecified build type; no wrapper PASS claimed.
No runtime edits or production default changes during this acceptance.
See [exact source, commands, log hashes and remaining gates](texture-rhi-stage3-native-acceptance-2026-11-20.md).
Next: publish branch and inspect PR #86/CI/base; do not update main or infer
Vulkan/GLES/WebGL/indexed-anisotropic GPU validation from these tests.

## 2026-11-20 — indexed native Metal sampling acceptance

Code `ed1a427a61960b02a658e1c46f29dc1ac4261ee7`, branch
`codex/texture-completion`: 18 indexed mip/anisotropic GPU color probes
plus 136 preserved bilinear probes PASS (7.28 s) through the unified runner,
Debug build entirely on KINGSTON. Runtime/API/save formats unchanged.
[Exact commands, limitations and next step](texture-rhi-indexed-metal-2026-11-20.md).
Prior focused Linux/GLES/Vulkan CI remains queued; no CI PASS claimed.

## 2026-11-20 — final GPU gates exposed indexed ICB admission defect

Code `a6d32f7b8eb02302eb0242ba656c1c7b52defe8b`: 1330 low-level GPU
color probes and six shader/packer tests pass. New indexed Renderer3D
fixture fails because unsupported ICB submission is attempted every frame;
pixels still match direct rendering. Full build is in progress on KINGSTON.
[Failure evidence and exact next action](texture-rhi-final-gates-2026-11-20.md).

## 2026-11-20 — indexed VT early admission fix

Code `b17dba1a02066fd23d9764a5e07fbc16b9e27e63`: indexed bindings are
excluded before legacy ICB preflight; eligible counters match actual supported
items. Full Debug build PASS; targeted legacy/indexed Renderer3D scenes 2/2
PASS, with zero execution errors. Full 300-test regression is RUNNING, not
yet accepted. [Exact evidence and continuation](texture-rhi-indexed-admission-fix-2026-11-20.md).

## 2026-11-20 — final native Texture RHI regression PASS

Runtime code `b17dba1a02066fd23d9764a5e07fbc16b9e27e63`: full Debug
editor/test build and **300/300 registered CTests PASS**, 0 skipped/failures,
949.78 s, including all eight serial Metal texture tests and 1330 canonical
GPU color probes. Indexed ICB admission bug fixed; legacy ICB remains intact.
All local build/cache/report output is on KINGSTON. PR #96 targets main.
[Final evidence, hashes and external CI/release boundary](texture-rhi-indexed-admission-fix-2026-11-20.md).
Cross-backend runtime CI remains queued; Windows/macOS republish is due
from the integrator per AGENTS §8. No unsupported parity/performance claims.

## 2026-11-21 — review fixes (branch feature/aiv20261121_fix_gfx_strata)

Texture/RHI part of the gfx-strata review pass; full detail in
[review-fixes-gfx-strata-2026-11-21.md](review-fixes-gfx-strata-2026-11-21.md).
- `817d0b5` GL: stale `glGetError()` drained before guarded sections (VT uploads no longer fail on foreign errors).
- `aba1af2` `readTextureAsync` row order unified to top-left: GL and Vulkan flip only render targets; Metal unchanged; `gles_texture_array_gpu` gets a row-0 marker.
- `a2fe676` Vulkan sampler `mipFilter None`: minLod = maxLod = 0.
- `2558ba6` Vulkan device loss: pending texture/buffer readbacks complete once with an empty result.
- `0298947` GL region uploads via `UNPACK_ROW_LENGTH`/`IMAGE_HEIGHT`; `TextureStreamQueue` deque + one-pass cancel.
- `de4a989` Metal ordered staging-blit uploads also for 2D/cube/cube-array/3D (no auto mips, no open encoder). UNVERIFIED (ObjC++).
Verification: syntax checks only for GL/Vulkan TUs; header-only streaming tests (texture_streaming, texture_resident_streamer, virtual_texture_file_streamer, virtual_texture_feedback_pass, texture_mip_residency) built and PASS standalone. GPU tests and full CTest NOT RUN.
