# PRISMA Stage 35 — editor viewport GPU lifetime and bounded-pressure cleanup

**Date:** 2026-11-19. **Branch:** `feature/prisma-postfx`. **PR:** https://github.com/chromealex/AIVERSE/pull/26. **Previous:** `docs/handoffs/prisma-stage-34.md`. No change to main; no merge from unrelated feature branches.

## Implemented features / exact commits

1. `41297f13fb9805f60c63b66ddc6e7d6133fb5351` — **new** `apps/editor/ViewportGpuRetirement.h`: editor-lifetime owner for released viewport color/depth render targets. Tracks handles, submitted GPU token (0=quarantine), metadata-derived lower-bound bytes, completed-fence nonblocking collection, explicit GPU-idle drain and configurable pressure budget. **No** CPU frame-number destruction heuristic.
2. `ea7d4c301be9691b296f5fd8f1b22391a33e5665` — `apps/editor/Editor.h`: each `Viewport` binds editor-owned GPU retirement manager on first ensure; viewport destructor retires its handles; copy is disabled to avoid duplicate ownership; Editor's owner outlives all windows.
3. `1d37100b973f5bf6042d57ddc6c18475a59a9959` — `apps/editor/Editor.cpp`: `Viewport::ensure`/release use fence-retire instead of immediate `Device::destroyTexture`; old ImGui texture IDs remain valid until last referencing GPU submission completes.
4. `5babf47e8135208d50a76aaf3bf3f83e064cc01d` — `apps/editor/windows/SceneWindow.cpp`: Scene viewport uses shared manager.
5. `1b8e92ca3c06cd8434311d3dee6a961d2d3e5862` — `apps/editor/windows/GameWindow.cpp`: Game viewport uses shared manager.
6. `299ee583d14e6fba07ddf39768e28ffb09420135` — `apps/editor/windows/FxWindow.cpp`: FX preview viewport uses shared manager, retaining its existing explicit destructor shutdown behavior.
7. `c1f150ad10f92de2e2902f8a6d08cc3b9c8e826d` — `engine/gfx/Gfx.h`: `gfx::Device::waitForSubmittedGpuWork()` optional explicit GPU-idle barrier for previously submitted graphics work. Allowed only at a safe pre-render frame boundary; default backend returns false.
8. `74ca0e8c9c6e7633b12112fcf00ed55143765815` — `engine/gfx/gl/GfxGL.cpp`: GL33/GLES3 `glFinish()` only during a memory-pressure synchronization; WebGL returns false and quarantines rather than claiming guaranteed blocking GPU idle. Normal frame submission remains nonblocking.
9. `8f85f74a53a96a6bcb32f1df6c9052df35ab8c84` — `engine/gfx/metal/GfxMetal.mm`: initial pressure-only Metal wait approach (superseded by deadlock-safe update below).
10. `4ac7e43bdf50e36f2631739b09049dc3f49f1602` — `engine/gfx/vulkan/VkDevice.h`: declare Vulkan GPU idle hook.
11. `952d73b17ec4eb7596074e86ae6255297365c22d` — `engine/gfx/vulkan/GfxVulkan.cpp`: pressure-only `vkQueueWaitIdle`, then advance GPU-completed serial to previous submitted frame and collect already-completed Vulkan garbage, never the unsubmitted current frame.
12. `4c8103edd1ae56d605377d957e2e9b9902269ba0` — `apps/editor/Editor.cpp`: collect viewport/PRISMA/Renderer3D retired resources at the beginning of each GUI frame. Default 128 MiB known pending budget (configurable 32..4096 via `AIVERSE_PRISMA_RETIRE_BUDGET_MB`); at an exceeded budget, synchronize previously submitted GPU work **before any new draws**, then explicitly drain all three owners if and only if synchronization succeeds. Editor shutdown retires windows first.
13. `e0b64c354f470e0feaa9c6065fdd2e526f4c0f43` — `apps/editor/windows/SceneWindow.cpp`: retire GPU framebuffers after Scene tab hides/closes, not only after its C++ window object dies.
14. `90cc6f459b72b2b92585c5d5bae63b2844727c52` — `apps/editor/windows/GameWindow.cpp`: same for Game tab, including hosted client mode.
15. `28f21c533d264fdce174b3d7dc341360916e4479` — `apps/editor/windows/FxWindow.cpp`: same for FX preview tab.
16. `5b5dbef5aa5ecd576f1c5f3e68df216c5bd10130` — `apps/editor/Editor.h`: one-shot `requestGpuRetirementFlush()`, scheduled strictly at next pre-render safe boundary; expose latest attempted flush success.
17. `116215cf591885b5bf81784ced8b9b43a2052b2f` — `apps/editor/Editor.cpp`: one-shot requested flush and budget-driven flush using `waitForSubmittedGpuWork`. During shutdown, drain viewport quarantine only after confirmed submitted-work synchronization.
18. `d32dc9d8dd75d4d2e96f70b3a4fe7e68037c4c7d` — `apps/editor/windows/StatsWindow.cpp`: show viewport fenced/unfenced textures and known pending MiB vs budget alongside PRISMA/Renderer3D counts. Manual button requests next-frame safe cleanup, never synchronizes inside ImGui rendering.
19. `f742b856341400f4c3031bd95782a76210cd63cc` — `docs/prisma.md`: Stage 35 architecture, API, budget, default-off effects, untested status.
20. `0b8d648cc774691d665ec828d81ca8ad256ec70e` — `engine/gfx/metal/GfxMetal.mm`: **fix initial Metal pressure path**: wait for the LAST ACTUALLY COMMITTED main graphics command buffer, rather than creating/committing a new barrier behind a currently created but not-yet-submitted frame. Avoids a potential command-queue creation-order deadlock; records the last committed buffer/token on endFrame. Never wait on the unsubmitted current frame.
21. `c1fbc6a0051b829667638a18d144c3827c2226c8` — `docs/prisma.md`: clarify the corrected Metal completion procedure.

## Native/editor API and ownership

- `Viewport::ensure(gfx::Device&, w, h, ViewportGpuRetirement&)` is now required by the current Scene/Game/Fx windows. Color and depth handles are owned by a `Viewport` while active, and by the Editor's retirement manager when resized, released, hidden or destroyed. The shared manager outlives window destruction and tracks the fence from the currently active graphics submission. It polls `Device::gpuCompletedFence` without stalling.
- `Viewport::release(gfx::Device&)` retires handles and resets displayed dimensions, hover and `drawnFrame`. This prevents UI code treating a released viewport as a live drawable. The fallback immediate-release path is only for legacy call sites never bound to an owner and retains the caller's GPU-idle requirement.
- At each editor frame's **pre-render** boundary, known retired bytes are estimated from RHI `textureInfo` across Viewport, PRISMA and Renderer3D. Budget default is 128 MiB; `AIVERSE_PRISMA_RETIRE_BUDGET_MB` clamps 32..4096. When over budget or a user explicitly requests cleanup, synchronize PREVIOUSLY submitted GPU work, then drain quarantined resources. No release occurs on a failed/unsupported wait.
- RHI synchronization implementations: GL native glFinish (pressure only), Metal last previously committed command-buffer `waitUntilCompleted` (not an uncommitted command-buffer barrier), Vulkan vkQueueWaitIdle. WebGL unsupported returns false and cannot be forcibly drained this way. These methods do not wait on GPU work encoded later in the current frame; call only at the documented pre-render point.
- `StatsWindow` offers a safe next-frame GPU cleanup request. This action is a one-shot flag; no GPU wait happens while the UI render pass is active. The UI shows pending-count and known-MiB *lower bound*, not total VRAM. Active, compressed, aliased, driver and internal RenderGraph memory may not be covered.
- `Editor::shutdown` destroys windows before the manager and attempts a verified GPU submitted-work idle wait before draining the viewport queue. If this fails, pending texture handles remain held until device teardown rather than being unsafely destroyed. Editor shutdown must still be invoked at the host's normal safe graphics lifecycle point.
- No ECS mutation, RTS/FPS gameplay changes, PostFX JSON changes, or default-on effects. The work is presentation-only lifetime infrastructure.

## Tests actually run

**Performed:** connected GitHub source exploration, edits/commits and readback of declarations and call sites in all twelve modified code files; manually reasoned about GPU-fence ordering and corrected the Metal command-buffer wait to avoid an uncommitted-frame deadlock.

**NOT RUN** at user's explicit features-first direction: CMake, native C++/Obj-C++ build, CTest, Windows GL loader checks, Metal shader/runtime checks, Vulkan device execution, Android GLES and WebGL runtime, Scene/Game/Fx GUI smoke, screen captures, VRAM and GPU pressure profiling, CI. No compile success, driver validation or safe shipping status is claimed.

## Limitations and next Stage 36

1. The 128 MiB budget is based on **known pending bytes**, not a hard bound on total GPU VRAM. A backend without a guaranteed idle hook (WebGL) preserves all in-flight objects. A failed wait/driver-loss condition also preserves allocations. It is intentionally safer to temporarily retain memory than free a texture still used by the GPU.
2. The Metal idle wait covers the last actually committed main-frame graphics command buffer on its queue, not custom concurrent queues/unscheduled external GPU workloads. The editor pre-render caller contract prevents current unsubmitted usage of retired textures; test this on macOS and iOS.
3. Hidden-window detection is based on ImGui `drawnFrame` and runs from the window's update() path. Verify dock-tab hide/reopen and hosted GameClient behavior; hidden windows may briefly retain their last framebuffer until the next UI update.
4. The `ViewportGpuRetirement` queue is owned by Editor; custom native view owners must use their own lifetime service. No ABI compatibility is promised for internal Editor::Viewport::ensure() because signature was intentionally updated across its actual source call sites.
5. Device teardown resources beyond Viewport/PRISMA/Renderer3D (e.g. FX color buffers, atlas pages, screenshots, shadow data) retain their prior owner policies. Stage 35 does not certify all engine-wide transient resources.
6. Stage 36 feature priorities: integrate per-viewport GPU lifetime/pressure diagnostics into hosted client ownership and other native screenshot/preview targets; guard GL/Metal/Vulkan device-lost and endFrame failures against permanently stuck quarantine; extend graph pool retirement byte reporting; develop opt-in runtime diagnostics for editor internal GPU allocations.
7. Once user permits tests, validate all three editor viewports, continuous resize churn, close/reopen and pressure button, RHI GPU timeline semantics and Metal no-deadlock, plus C++/Obj-C++/GL/GLES/Vulkan builds, CI and CTest. Record real PASS/FAIL and fix defects.
8. Create a separate `docs/handoffs/prisma-stage-36.md` with exact SHAs, API, implementation details, actually run tests, constraints and the next owner/step. Do NOT merge to main without explicit direction.
