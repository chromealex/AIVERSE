# Review fixes: GFX backends + Texture RHI + VT + STRATA (2026-11-21)

Branch `feature/aiv20261121_fix_gfx_strata`, base `973b329` (origin main).
Not pushed, not merged. Full CMake build / CTest were NOT run here (only allowed
on the owner's KINGSTON disk); no GPU is available here.

## Findings

| # | Sev | Finding | Status | Commit |
|---|---|---|---|---|
| 1 | MED | GL: stale `glGetError()` attributed to RHI sections (VT upload marked failed) | FIXED | `817d0b5` |
| 2 | MED | `GeometryScene::loadTransactional` silently disables STRATA | FIXED | `8cd3603` |
| 3 | MED/PERF | Vulkan Storage+Dynamic STRATA arena copied whole per upload frame | DEFERRED | - |
| 4 | MED | `readTextureAsync` row order differs GL/Vulkan/Metal | FIXED (GL, Vulkan; Metal already correct) | `aba1af2` |
| 5 | MED | Metal ICB encoder kernel trusts `baseVertex` | FIXED (UNVERIFIED, MSL) | `60ba808` |
| 6 | LOW | Vulkan `drawIndexedGpuIndirect` Indirect-only + MDI; capability not advertised | FIXED | `b71eb12` |
| 7 | LOW | STRATA disk read failure pinned forever; useless wake in `retainRequested` | FIXED | `bbb0847` |
| 8 | LOW | Vulkan device loss: pending readback callbacks never fire | FIXED | `2558ba6` |
| 9 | LOW | Vulkan sampler `mipFilter=None` sets maxLod 0 < minLod | FIXED | `a2fe676` |
| 10 | LOW/PERF | GL per-row `glTexSubImage`; `TextureStreamQueue` quadratic erase | FIXED | `0298947` |
| 11 | LOW | Metal `updateTextureRegion` CPU `replaceRegion` races GPU for non-array textures | FIXED conservatively (UNVERIFIED) | `de4a989` |

### Details

1. `engine/gfx/gl/GfxGL.cpp`: new `drainPendingGlErrors()` (bounded 32 iterations
   for lost contexts, logs the first stale error once to stderr) called at the start
   of each guarded section: `createDimensionalTexture`, MSAA renderbuffer and
   2D/compressed `createTexture`, `updateTextureRegion`, `createSampler`,
   `bindTextureView`, `captureTexture`, both `readTextureAsync` variants,
   `waitIdle`, `waitForResourceIdle`.
2. `engine/render/GeometryScene.{h,cpp}`: `copyStrataConfigFrom()` copies all
   STRATA flags / budgets / parity gates / disk directory into the staging owner
   before `loadImpl`; after the old resident set is retired, STRATA resources
   (staging, GPU metadata, GPU culling, portable sources, global index leases,
   draw aliases, LOD histories, device pointers, streaming cursors) are moved into
   the live scene. Known, unchanged limitation: the staged load reserves device
   index budget while the live scene still holds its leases, so under a tight
   global budget a transactional reload can decline sources a fresh load keeps.
   Regression test `geometry_transactional_reload_keeps_strata_policy_and_resources`
   in `tests/geometry/runtime.inc` (compares two transactional reloads to `load()`).
3. DEFERRED (needs GPU validation). Precise plan: implement
   `VulkanDevice::updateStorageBufferChecked` (staging write + `vkCmdCopyBuffer` in
   the ordered frame/transfer command buffer + barrier
   TRANSFER_WRITE -> INDEX_READ|SHADER_READ at VERTEX_INPUT|COMPUTE), create the
   STRATA incremental arena (`StrataIndexPages.cpp` ~50, `BufferType::Storage`,
   `Usage::Dynamic`) as `Usage::Immutable` device-local on Vulkan, and route
   `IndexResidency::updateIncremental` page writes (~327) through
   `updateStorageBufferChecked` when it returns true. STRATA already only writes
   never-referenced / fence-retired ranges, so no CPU versioning is needed.
   Today `writableVersion(keepContents=true)` (`VkResources.cpp` ~292-321) memcpy's
   the whole shadow into a new/reused version on every frame that uploads.
   Alternative lower-risk option: per-version dirty-range tracking in `VBuffer`
   so a reused version copies only ranges written since it was last current.
4. GL native PBO path and WebGL acceptance path flip only render targets
   (`GLTextureRead::flipRows`); Vulkan `TextureRead::flipRows = renderTarget`,
   flipped in `deliverTextureReads`. Metal unchanged (render targets top-down,
   `renderTargetFlipY`). Contract comment in `engine/gfx/Gfx.h`. Callers checked:
   `VirtualTextureFeedbackRuntime`, editor `TextureResourcesWindow` preview (row 0
   = top), GPU tests use solid colours / column gradients, except
   `webgl2_vt_integration` whose page-table expectation at key (3,3) already
   assumes unflipped upload rows. `gles_texture_array_gpu.cpp` legacy texture now
   has a row-0 marker (non-solid regression). GL `captureTexture` still flips
   always (it is a render-target capture API) - unchanged.
5. `GfxMetal.mm` ICB kernel `Params.vertexCapacity` = vertex buffer bytes / 12
   (smallest vertex = float3; the real layout belongs to the pipeline) and the
   kernel skips `baseVertex < 0 || baseVertex >= vertexCapacity` (ICB range is
   reset before dispatch). firstIndex alignment: not a bug, indices are addressed as
   `uint*` elements. Metal-only kernel: no GLSL counterpart exists for this path.
6. Vulkan: Storage buffers get `VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT`;
   `drawIndexedGpuIndirect` accepts Indirect or Storage, validates offset/bounds,
   needs `drawIndirectFirstInstance` (new `drawIndirectFirstInstance_`) but not
   multiDrawIndirect; `supportsGpuAuthoredIndirectDraw()` overridden; shared
   encoder is private `encodeIndexedIndirect`. Effect: opt-in
   `SpectraGpuParticles` `gpuIndirect` can now activate on Vulkan (untested on GPU).
7. `StrataDiskPages.cpp`: failures are `ReadFailure{attempts,cooldown}`: refused
   for 2/4/8/16 `request()` calls, permanent after 5 consecutive failures, erased by
   a verified read; `failed()` = record present and no retry in flight. The single
   in-flight job waits on `State::abortActive`, set by `close()` and by
   `retainRequested()` when the active key is no longer wanted (not counted as a
   read failure). Test extended in `tests/render/strata_disk_pages.cpp`.
8. `GfxVulkan.cpp`: `failPendingReadsAfterDeviceLoss()` (from `beginFrame` ->
   `deliverTextureReads` when unhealthy) moves out and invokes every pending
   texture / buffer readback callback once with an empty result; staging buffers
   stay with their entries until shutdown. `readTextureAsync` refuses when
   unhealthy. `captureTexture` captures unchanged.
9. `VkResources.cpp createSampler`: `mipFilter None` -> minLod = maxLod = 0
   (base level, same as non-mipmapped GL/Metal filters).
10. GL `updateTextureRegion` uses `UNPACK_ROW_LENGTH` (+ `UNPACK_IMAGE_HEIGHT` for
    3D/array) when pitches are whole pixels / rows, per-row only for odd pitches;
    state still restored. `TextureStreamQueue::pending_` is a `std::deque`,
    `cancelPendingFor()` is one stable pass.
11. Metal ordered staging-blit path extended from Texture2DArray to Texture2D,
    Cube, CubeArray, Texture3D when no auto mips and no open encoder; otherwise the
    legacy `replaceRegion` (2D array keeps fail-closed). Texture1D and legacy
    `updateTexture()` unchanged.

## Verification actually run

Syntax checks (`g++ -std=c++20 -fsyntax-only -Wall -Wextra -I<wt> -I<wt>/engine
-I<wt>/libraries/include -I<wt>/third_party`), all clean (0 warnings/errors):
- `engine/gfx/gl/GfxGL.cpp` with Khronos GLES3 headers (`-I/tmp/glinc`), also with
  `-D__EMSCRIPTEN__ -DAIVERSE_WEBGL_CONTEXT_ACCEPTANCE`;
- `engine/gfx/vulkan/{VkResources,GfxVulkan,VkDraw,VkCompute}.cpp` with
  `-I<wt>/third_party/vulkan/include`;
- `engine/render/GeometryScene.cpp` with and without
  `-DAIVERSE_STRATA_STAGING_ENABLED=1`; `engine/render/StrataDiskPages.cpp`;
- a TU including `engine/gfx/TextureStreaming.h`;
- `tests/render/gles_texture_array_gpu.cpp` (Khronos EGL/GLES3 headers);
- `tests/tests.cpp` (`-fsyntax-only`, includes `geometry/runtime.inc`): no errors
  in changed code; 3 pre-existing unrelated errors at tests.cpp:18022-18029
  (`dropForeignCMakeCache`, needs `AIVERSE_TESTS_EDITOR_CORE`).

Standalone builds and runs (`g++ -std=c++20 -O0 -pthread`, test TU +
`StrataDiskPages.cpp StrataStreamingCoordinator.cpp StrataIndexResidency.cpp
StrataIndexPages.cpp StrataView.cpp Geometry.cpp engine/data/Json.cpp
engine/core/Fixed.cpp`):
- `tests/render/strata_disk_pages.cpp`: PASS (exit 0); with the pre-fix
  `StrataDiskPages.cpp` it aborts on the new `retried` assert (exit 134);
- `tests/render/strata_index_pages.cpp`: PASS; `strata_streaming_coordinator.cpp`: PASS.
- Header-only tests: `texture_streaming`, `texture_resident_streamer`,
  `virtual_texture_file_streamer`, `virtual_texture_feedback_pass`,
  `texture_mip_residency`: all PASS.

## UNVERIFIED

- Full CMake build and CTest (incl. `geometry_transactional_reload_keeps_strata_policy_and_resources`
  in `aiverse_tests`, never executed here).
- `engine/gfx/metal/GfxMetal.mm` (findings 5, 11): not compiled (Objective-C++/MSL).
- All GPU runtime behaviour: GL error drain, readback orientation
  (`gles_texture_array_gpu`, `webgl2_vt_integration`, Vulkan/Metal GPU tests),
  Vulkan GPU indirect from Storage, device-loss callbacks, sampler validation
  layers, GL ROW_LENGTH uploads, Metal ordered blits.

## CMake registrations needed

None: every changed test is already registered (`aiverse_tests`,
`strata_disk_pages`, `gles_texture_array_gpu`).

## Next step

On KINGSTON: full build + CTest; run the GLES3/WebGL2/Vulkan/Metal texture GPU tests
(`gles_texture_array_gpu`, `webgl2_vt_integration`, `vulkan_tests`,
`metal_texture_gpu`, `metal_icb_graph_gpu`) to confirm 4, 5, 6, 10, 11; then decide
on finding 3 (plan above).
