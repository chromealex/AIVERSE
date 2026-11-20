# Hybrid GI — Stage 29: stale binding fail-closed + Vulkan/GLES RHI code audit (2026-11-19)

## Verified source lineage, current state

- Branch: `feature/gi-lighting-portable-backends-20261119`, based on `feature/hybrid-dynamic-gi`. Target integration PR: https://github.com/chromealex/AIVERSE/pull/47 (DRAFT, targets the GI branch; NOT main).
- Source commit: `01a9ca9b976c9f4d7d10b226d3df0f47d4c6fe47`, `engine/render/GiDynamicRuntime.h`.
- Read prerequisite `docs/agent-handoffs/gi-stage27-portable-light-invalidation-2026-11-19.md` and `docs/agent-handoffs/gi-stage28-world-dirty-clearing-2026-11-19.md`.
- **No native build, CTest, GPU tests or hardware measurements run.**

## Fixed remaining ghosting hole

`GiDynamicRuntime::bind(Renderer3D&)` now calls `Renderer3D::clearDynamicGiOverlay()` if the runtime is not initialized, any GPU-upload clear is pending, or the new RHI texture binding fails. Previously `bind` returned false but *did not unbind an overlay left in Renderer3D from a prior frame*, so rendering could still use stale bounced light even if callers checked or ignored the false return. A successful `update(0,camera)` flushes dirty zeros before re-binding. This is only a defensive presentation boundary, not a substitute for calling `bind` each frame after world/light publication.

## Vulkan and GLES source verification (NOT GPU validation)

Inspected the *current* `main` sources and the GI feature's shader/binding source:

- `engine/gfx/vulkan/VkResources.cpp`: `PixelFormat::RGBA32F` maps to `VK_FORMAT_R32G32B32A32_SFLOAT`; `VulkanDevice::supports` queries `vkGetPhysicalDeviceFormatProperties` for sampled-image features; `updateTexture` uses the format-dependent 16-byte pixel size. This makes an opt-in CPU-probe texture RHI path **plausible**, conditional on runtime device support, but does not establish that the Vulkan GPU output is correct.
- `engine/gfx/gl/GfxGL.cpp`: GLES3/desktop GL create `GL_RGBA32F` `GL_FLOAT` data textures, use nearest sampling for 32-bit float, and issue `glTexSubImage2D` updates with stride handling. GLES 3.0 needs actual device/context verification of texture and fragment sampler limits and HDR render-target extension availability.
- `engine/render/Renderer3DModels.cpp`: opt-in PBR GI uses `U6/tex13` for one dynamic atlas, `tex14` fine validity, `U7/tex15` far fallback, and Metal-only `U8/tex16` third cascade. Textured shader graphs use overlapping slots and remain excluded. The single-cascade RHI CPU path is not GLES/Vulkan GPU ray tracing.
- `engine/gfx/vulkan/VkDevice.h` exposes graphics RHI methods but does not override `gfx::Device` compute virtuals. `GiGpuSmallScene.h` explicitly checks `Backend::Metal` and compiles MSL only; **Vulkan native compute GI NOT IMPLEMENTED**, GLES3 compute unsupported by the ES 3.0 floor. Existing RHI fallback is bounded CPU tracing and sparse probe atlas upload.

**Correctness/test caveat:** The audit source paths on main may diverge from the experimental GI branch; rebase/merge with conflict review before claiming end-to-end parity.

## Next actual steps and commands

1. Checkout stage branch and build the existing GI tests in Debug & Release; run `ctest --test-dir build/gi-portable --output-on-failure -R '^gi_'`; use the project's standard full acceptance runner once all unfinished functionality lands. Fix compile errors and append per-stage handoff with tested SHA.
2. Validate `Renderer3D::clearDynamicGiOverlay` on an actual Metal/GL/Vulkan frame after a light/source deletion and with GI updates paused, including a previous-frame borrowed binding; compare GPU readback/screenshots. Use a real engine renderer fixture rather than FakeDevice identity alone.
3. Implement Vulkan compute pipeline via explicit `VkDevice` compute operations, storage buffers/images, SPIR-V compute source, descriptor/barrier/fence lifetime and device tests; keep GLES3 CPU/fallback, gate cost/quality. GLES shader compilation, GL validation and Android/WebGL2 native smoke are mandatory before production enablement.
4. Integrate real read-only ECS/TerraWeave source events and native light authoring; handle shader graph/terrain/foliage model GI and sampler overflow, clipmap invalidations and quality scaling.
5. Reconcile the current `main` and existing branches without force-pushing/overwriting parallel agent changes. No merged or done claim until actual tests.

## Actual validation

Only GitHub source reads/writes succeeded, including binding fail-closed commit. Native CMake/CTest, Metal/GL/GLES/Vulkan shader compilation, GPU readback, Android Vulkan/GLES, mobile profiling, and end-to-end lighting scenarios are **NOT RUN**. A final check must verify exact SHA.
