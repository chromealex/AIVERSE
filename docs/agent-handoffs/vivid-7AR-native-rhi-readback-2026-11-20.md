# VIVID Stage 7AR — native Vulkan/GLES3 async ID readback, CPU-authoritative GPU diagnostics

Date: 2026-11-20  
Branch: `feature/vivid-digital-humans`  
Repository: `chromealex/AIVERSE`  
Integration ownership: no merge/push to `main`; no mutation of `feature/texture-rhi-unification`.

> **Evidence:** code and tests have been committed via connected GitHub sources. No local C++/Obj-C++ build, CMake configure, CTest, GPU shader compilation, driver execution, screenshot, pixel, benchmark or timeline has actually been run on these exact commits. New tests are definitions, **not passing results**.

## Scope and architecture

Stage 7AR extends the Stage 7AQ partial RHI support: opt-in ID/depth pixels can now use Metal, Vulkan and native GLES3/GL33. The single-pixel readback is always asynchronous from the gfx-device owner thread, with GPU completion verified by a real command-buffer or GL/Vulkan frame fence. WebGL2/Emscripten and Windows' custom GL loader remain unsupported, and CPU BVH selection remains the default and fallback everywhere.

**This stage explicitly does NOT implement production Renderer3D GPU vertex override/visible LOD source parity.** A result produced by the current GPU pass is an independent rasterization of a CPU-derived morph-first/skin-second mesh snapshot. It can only corroborate the CPU-selected author vertex; on any mismatch or stale reply, the CPU choice is preserved. The experimental Creator checkbox defaults OFF. No new renderer or ECS is introduced.

## Code changes / exact commits

| Component | Commit | Implementation |
|---|---|---|
| `engine/gfx/vulkan/VkDevice.h` | `b094f3d8aad4a4ec6401f1e12c11ebaeba4acb8e` | Declare per-texture async pixel callback, frame serial, 4-byte pending staging buffer and owner-frame delivery method |
| `engine/gfx/vulkan/GfxVulkan.cpp` | `9e10630ce817b8cf907ceee77db810078a9baf20` | Implement offscreen linear RGBA8 1×1 Vulkan image-to-buffer copy, TRANSFER_SRC layout, transfer→host visibility barrier, frame-slot fence completion, owner-thread callback and pending staging retirement |
| Vulkan slot fence safety | `2e88b8fb7b792483e4d6bb566ae13a6687c53826` | Do **not** advance `completed_` on failed `vkWaitForFences`; lost-device replies are empty instead of validated pixels |
| `engine/gfx/gl/GfxGL.cpp` | `0b18c3a9ed6c99a4417274137e85fb533fa20e44` | Expose monotonic frame identity while a native GLES3/GL3 frame is active; existing Stage 7AQ PBO/GLsync proves actual GPU completion. WebGL2/Windows loader still report zero |
| `engine/character/vivid/SculptGpuIdPass.h` | `acc3832f77093a477d77469767cf9068ec4e204b` | Allow Metal/Vulkan/native GLES3/GL33 with real frame serial; a failed post-draw readback disables repeated GPU attempts until idle-safe shutdown to avoid accumulating huge temporary buffers |
| Shader/pipeline initialization | `da50f996884bc054def0c333a95d39cf3cae534d` | Move frame serial requirement from shader initialization to actual pick recording; headless tests and normal apps can compile pipelines before beginFrame |
| `tools/vivid-creator/main.cpp` | `b230637c670b43a3672fbd5435464236453a5f2a` | Rename opt-in UI to Experimental GPU ID check; accept all supported native backends; store the CPU-picked source ID and on reply confirm only if **exact same source ID** is resolved. Never silently overwrite CPU-authoritative selection with this CPU-fed GPU raster |
| Vulkan hardware fixture | `5778d784cc0d954c6e2dc4c11cb97aea2174d0e5` | `tests/vivid/vulkan_sculpt_id.cpp`: headless Vulkan Device, two near/far overlapping depth-tested triangles, background ID 0, closer external zero-ID blocker and true async pixel callback |
| Vulkan CMake | `50bb0ae552b30fbd225c20c4728eb0eb17e4c5db` | Native target `aiverse_vivid_vulkan_sculpt_id_tests`, CTest `vivid_vulkan_sculpt_id`, driver absence returns skip 77 |
| GLES3 hardware fixture | `ef4bae9d59e14f077f9c33806a4f5e6101330d0b` | `tests/vivid/gles_sculpt_id.cpp`: Linux EGL pbuffer and GLES3 context, same near/far/background/zero-ID occluder cases with real PBO+fence pixel callback |
| GLES3 CMake | `1735f853a1f6feae3dddf569ea7d0aadd64b0f06` | Linux-only target `aiverse_vivid_gles_sculpt_id_tests`, CTest `vivid_gles_sculpt_id`, context absence returns skip 77 |
| Creator instructions | `5fe08b73c9302ee1e67c5e84536e2bd188c657f8` | `tools/vivid-creator/README.md`, native execution commands, exact GPU vs CPU parity limitations |
| Architecture docs | `6acca990d287bf306781056c004bf3e4aab874f5` | `docs/vivid.md` Stage 7AR |

## APIs and invariants

- `gfx::Device::readTexturePixelAsync(TextureHandle target,int topLeftX,int topLeftY,PixelReadFn cb)` remains shared RHI API added in Stage 7AO; only offscreen, linear, single-sample RGBA8 render targets. Metal uses a completed Metal command-buffer blit, native GL uses a PBO+GLsync, Vulkan uses a 4-byte host-visible VkBuffer+existing submission frame-slot VkFence. **Never treat `frameSerial` as proof of completed GPU work.**
- Vulkan's `frameSerial()` is nonzero during recording, `VkDevice::completed_` advances only after successful wait for the relevant slot fence; `deliverPixelReads` runs at the next owner `beginFrame`, not inside GPU command recording, and releases pending VkBuffer allocations after delivering.
- RHI readback is in the **same** submitted GPU command stream as the depth/ID draw. The readback's original texture must remain alive through the transfer command; existing Vk deferred texture retirement, Metal command buffer retains and GL framebuffer state restoration apply. The callback is 4 canonical RGBA bytes or empty on GPU failure, never fabricated.
- Vulkan's 1x1 copy uses `VK_FORMAT_R8G8B8A8_UNORM` (no channel swizzle), image TRANSFER_SRC layout transitions and a transfer-to-host visibility barrier. Current Vulkan pass is GL/bottom-left-oriented; readback converts `y` to `textureHeight-1-y` at copy time.
- Native GLES3 `frameSerial()` is an owner-thread monotonic **recording identity**, but its pixel completion is based on the existing `glFenceSync` and later nonblocking `glClientWaitSync`. WebGL2/Windows custom loader continue to return zero, plus `readTexturePixelAsync=false`.
- `SculptGpuIdPass` may compile both ID and sampled-mask shaders outside a frame, but `record()` requires a matching nonzero current frame serial. A failed post-draw readback permanently disables further GPU ID submits until an idle-safe owner shutdown, bounding retired in-flight vertex allocations.
- `CreatorApp` stores expected original source vertex ID from the immediate CPU BVH selection. A matching GPU pixel can show a successful *CPU-fed GPU diagnostic*, but a different GPU triangle/source ID must **not** replace the CPU-selected ID or sculpt marker. New click/model/mesh/viewport invalidates stale tokens as in Stages 7AN–7AQ.
- No source changes to HumanDNA, MotionWeave ECS, skinning math, model vertex layout, texture streaming/samplers, ShaderGraph material semantics, indexed geometry import, actor memory ownership or persistent save data.

## Native acceptance commands (deferred, not executed)

On **Linux** with Vulkan driver (Mesa lavapipe allowed):

```sh
cmake -S . -B build/vivid-vulkan -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_VULKAN=ON
cmake --build build/vivid-vulkan --target aiverse_vivid_vulkan_sculpt_id_tests -j 4
ctest --test-dir build/vivid-vulkan --output-on-failure -R '^vivid_vulkan_sculpt_id$'
```

On **Linux** with EGL/GLESv2 development libraries and a native GLES3-capable EGL context:

```sh
cmake -S . -B build/vivid-gles -DAIVERSE_BUILD_TESTS=ON
cmake --build build/vivid-gles --target aiverse_vivid_gles_sculpt_id_tests -j 4
ctest --test-dir build/vivid-gles --output-on-failure -R '^vivid_gles_sculpt_id$'
```

On **macOS Metal**, the previously defined `vivid_metal_sculpt_id` remains in the user's deferred `bash tools/vivid-validate.sh --full` suite. **Passing only macOS cannot certify Vulkan/GLES.** Record exact SHA, compiler errors, actual RGBA bytes, GPU timings, callback frame delays and memory.

## Remaining unresolved functionality / Stage 7AS

1. **Renderer-owned visible draw provenance:** Need the exact Renderer3D view's GPU vertex override + active mesh/LOD + shader vertex deformation, with an immutable frame snapshot and device-completion fencing, instead of a second CPU-expanded triangle stream. Stage 7AR deliberately preserves CPU authority because this fidelity cannot currently be proven.
2. **Hardware validation:** Native Vulkan and GLES3 CMake builds, shader/driver PBO/transfer/barrier tests, Metal regression and actual cross-frame async callbacks have not run. Fix real compiler/shader errors after one-pass acceptance; no fabricated PASS numbers.
3. **Image/depth semantics:** Stage 7AQ supports standard material base alpha/cutoff but no custom ShaderGraph alpha, derived depth-after/translucent cornea, exact skin/groom GPU flow, or texture residency parity from parallel Texture RHI. Keep conservative blocker or explicit unsupported fallback.
4. **Backend coverage:** WebGL2/Emscripten and Windows custom GL-loader still require their own proven pixel delivery pipeline, or continue CPU fallback. Vulkan/GLES source code path is present but not certified before driver tests.
5. **Semantic merge:** `Gfx.h`, `GfxGL.cpp`, `GfxVulkan.cpp`, `VkDevice.h`, `Renderer3D*`, Model and ShaderGraph may conflict with Texture RHI. Merge in an isolated integration branch after API freeze and native tests, retaining all independent semantics. Do not merge `main` from this stage without clear acceptance evidence.

Next stage must add a new standalone handoff and canonical `docs/agent-handoffs/vivid.md` update. Do not interpret creation of tests as actual test passes.
