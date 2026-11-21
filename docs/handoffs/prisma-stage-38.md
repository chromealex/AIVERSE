# PRISMA Stage 38 — safe Model/Materials Portrait retirement, RHI buffer accounting and terminal device-loss policy

Date: 2026-11-19.
Working branch: `feature/prisma-postfx`.
Draft PR: https://github.com/chromealex/AIVERSE/pull/26.
Previous handoff: `docs/handoffs/prisma-stage-37.md`.
Main not modified; no merge of other agents' branches.

## Implemented files and exact commits

1. `2829fee0817926926c4f3ecbee1005345d30fa74` — `apps/editor/windows/ModelWindow.cpp`: change inline `game::Portrait` to a separately owned shared object. On window destruction, transfer the complete old GPU-owning Portrait (Renderer3D, models, shaders/buffers and color/depth targets) to `GpuResourceRetirement::retireOwned`, with a cleanup callback running only after GPU completion.
2. `1ea668a32f1d272fb5c477579cc77764d03ce7f8` — `apps/editor/windows/MaterialsWindow.cpp`: equivalent complete deferred Portrait lifetime for material sphere/box previews; bind the Editor-owned retirement service.
3. `8bf81b8a15dc72c88856d00da4119b85ff0d376d` — `engine/render/GpuResourceRetirement.h`: add explicit `abandonDevice(device)` for terminal GPU device loss. No `destroy*` calls occur against a failed GPU. Complete owner bundles whose destructors could access a dead Device are parked in an inert no-destructor process-lifetime callback sink; this intentionally leaks those CPU owners until process exit rather than executing unsafe GPU cleanup. The generic service destructor also abandons any still-pending complex owner bundles.
4. `1f09b2c9bf761ca9be758b4310a83320b6692354` — `engine/gfx/Gfx.h`: optional nonblocking, CPU-only `BufferInfo` / `Device::bufferInfo(BufferHandle,BufferInfo&)` contract (logicalBytes and GPU version count); no RHI readback.
5. `2ec83c478e03386e4f40d1f6b5bf602803315d37` — `engine/gfx/gl/GfxGL.cpp`: bufferInfo from live GLBuffer pool's recorded size.
6. `128a186504ce6053bbb2901a2a29cb230cf94f0c` — `engine/gfx/metal/GfxMetal.mm`: Metal bufferInfo from live MTBuffer pool, including dynamic version count.
7. `3a64da583a800ea42665aba0f3dabae9f2f5be00` — `engine/gfx/vulkan/VkDevice.h`: Vulkan bufferInfo override declaration.
8. `f52d2212c1267666ddfd8a74f8f33d981277ae4c` — `engine/gfx/vulkan/VkResources.cpp`: read live Vulkan VBuffer size and number of allocated versions.
9. `4c1a5313fc39de2b3920d9a38cf373d4b4d99ba2` — `engine/render/GpuResourceRetirement.h`: auto-derive known pending buffer size from `Device::bufferInfo` and texture size from `Device::textureInfo` when a producer supplies no knownBytes. This improves the FX/preview pending memory lower-bound without GPU waits.
10. `e41f1092a4484596f4f6340246a92cf13824174e` — `apps/editor/windows/ModelWindow.cpp`: retire old model preview Portrait also when its dock tab is hidden/closed, not just at C++ object destruction.
11. `8a1acf213630028c3523675a149f174df60ef0a3` — `apps/editor/windows/MaterialsWindow.cpp`: retire hidden/closed material Portrait analogously.
12. `6f233568332880cf72cc798d29e940260065e65b` — `apps/editor/ClientHost.h`: terminal `abandonOnDeviceLoss()` API for archived hosted clients.
13. `6fd1c2cb98b1222c37535c7732f761c471c59833` — `apps/editor/ClientHost.cpp`: on lost graphics device, archive outgoing hosted GameClient C++ owners in a no-destructor process-lifetime sink rather than invoking `GameClient::shutdown`/destructors through stale GPU Device pointers. Normal frame paths still collect clients through actual GPU completed fences.
14. `a292f4365ddb9b9a6d25eb49529066fabffecbce` — `apps/editor/Editor.cpp`: distinguish normal GPU idle flush and terminal lost-device abandonment for queued generic resources and hosted clients in shutdown.
15. `3b5c8acb0fd0c4faebf4087b4dd28f50fe1120b6` — `apps/editor/windows/StatsWindow.cpp`: report abandoned handles/bundles and clarify pending FX buffer bytes in lower-bound GPU-memory estimate.
16. `e2bd2cec2e461b4bfa25f885293f3f42d1997f75` — `apps/editor/windows/ModelWindow.cpp`: do not queue an uninitialized Portrait owner.
17. `b11539aee2129fc9b66280501a57ec891ad8beb9` — `apps/editor/windows/MaterialsWindow.cpp`: do not queue an uninitialized material preview owner.
18. `550016da3f3b235d8350ae376b9c09974d2bc8dd` — `docs/prisma.md`: Stage 38 API, ownership, failure semantics, memory estimates, non-tested status and limitations.
19. This file `docs/handoffs/prisma-stage-38.md`: complete stage-specific handoff, committed separately after source/docs changes.

## Architecture, API and runtime behavior

**Complete Portrait ownership.** The editor Model and Materials windows own `std::shared_ptr<game::Portrait>` rather than inline nonmovable renderers. Upon closure or disappearance from the active dock tab, the existing owner transfers to `GpuResourceRetirement::retireOwned(gfx::Device&,cleanup)`. A fresh preview is created separately upon reopening. The queued shared owner and lambda outlive the window and can release its internally owned renderer/model/pipeline/buffer and framebuffer resources only after its GPU submission token completes. No API or GPU lifetime modifications are required inside `game::Portrait` itself. Standalone `apps/modelview/Gallery` and Animation Graph editor continue to own Portrait through their existing lifecycle; they were not automatically migrated.

**Buffer metadata.** `gfx::BufferInfo { logicalBytes, versions }` and `Device::bufferInfo` are optional CPU-only queries on the live device resource table. The GL, Metal and Vulkan implementations read actual buffer size metadata. `GpuResourceRetirement` fills `knownBytes` for an owned buffer automatically if the producer didn't specify it; textures likewise use width/height/format metadata. This improves `StatsWindow` and 128 MiB pressure accounting; GPU resource driver padding, dynamic versions and internal pipeline/shader allocations are not exact/complete bytes.

**Terminal device loss.** `GpuResourceRetirement::collect(device)` checks `!device.gpuHealthy()` before touching GPU APIs. On loss, `abandonDevice` drops RHI-handle records without GPU destroy; bundle callbacks are parked in an inert process-lifetime no-destructor sink to avoid running C++ destructors that may touch a destroyed GPU device. `~GpuResourceRetirement()` does the same for any still-pending bundle callback. `ClientHost::abandonOnDeviceLoss` parks archived GameClient objects analogously; normal client collection still needs a matching Device and a confirmed completed fence. `Editor::shutdown` uses these only when `gpuHealthy()` is false. **This is an intentionally conservative CPU leak until process exit** on terminal GPU loss, not a general resource-recovery mechanism.

**Scope:** These additions are genre-independent presentation/runtime lifetime mechanics; no modifications to FPS/RTS simulation, deterministic ECS, game.json PostFX schema, water/FX gameplay or default-off PRISMA visual settings. Other engine-wide resource owners that don't adopt `GpuResourceRetirement` still follow their old paths.

## Verification actually performed

**Performed:** searched/fetched current branch paths using GitHub connector; inspected renderer, RtsPortrait, GPU resources, GL/Metal/Vulkan buffer structs and editor Model/Materials windows. Implemented and committed code, re-fetched all touched headers/backends/window sources to verify required symbols, and inspected `Editor::isWindowInFront` public API for dock-tab hidden handling. No source was compiled/executed.

**NOT RUN** by user instruction prioritizing functions: CMake or native C++/Objective-C++ compile, CTest, Windows GL loader, Metal/Vulkan GPU execution, editor Model/Materials show-hide previews, device-loss simulation, dynamic buffer version count/VRAM profiling, CI and visual screenshot comparisons. No passes or hardware correctness are claimed.

## Risks, limitations and precise next step (Stage 39)

1. **GPU safety on device loss is not universal:** Stage 38 protects the opted-in generic deferred retirement service and archived hosted clients, but unrelated `RtsView`, Renderer3D, Shadow, PRISMA teardown, SpriteBatch and other owners may still call device destruction on a failed driver. A future architecture should centralize device-loss-aware teardown and drop invalid RHI resource registrations before the device disappears.
2. Orphaned preview/client objects remain allocated until process exit on terminal GPU loss, intentionally preventing their destructors from dereferencing the destroyed device. No ordinary-stage CPU memory release is possible for those owners without implementing explicit CPU-only detach/discard APIs in each nested render subsystem. Do not call `abandonDevice` under normal memory pressure.
3. `BufferInfo::logicalBytes` is not Vulkan allocator block size or Metal dynamic snapshot bytes; driver allocations can be larger. Its buffer version count is diagnostic, not proof of exactly allocated physical bytes. Future broader GPU resource registry should count GPU allocator actual bytes and enforce budget safely.
4. Complete `game::Portrait` owner retirement on hidden Model/Materials views may restart previous preview animation and GPU assets when returning to the tab, trading CPU/GPU churn for predictable VRAM lifetime. Verify tab visibility API on native ImGui builds.
5. The Animation Graph editor embeds its own Portrait plus a mutable Model+animation instance that may be hot-reloaded while GPU work is in flight. **Next functional Stage 39** should migrate that *complete* preview/model bundle (not just its color target) to GPU-fence retirement, while preserving custom animation graph editing state.
6. Additional independent consumers: `apps/modelview/Gallery`, model import preview, UI thumbnail renderers and custom editor plugins. They need explicit device/owner lifetime contracts, not blind adoption of the Editor queue.
7. Expand graphics owner health status into explicit `Healthy/Lost/Shutdown` lifecycle if device recovery is intended. Add an actual device-loss simulation test and ensure no queue destroys an RHI handle after the Device is destroyed.
8. When the user authorizes testing, compile CMake/CTest and run Metal/GL/Vulkan Model/Materials editor view hide/reopen, rapid material edits, GPU loss/suspend, memory pressure reclamation and ordinary shutdown. Record true PASS/FAIL and fix mistakes.
9. After Stage 39 implementation, create `docs/handoffs/prisma-stage-39.md`, with exact file paths/SHAs, architecture/API, actual verification, limitations and next action. Never merge this branch/PR #26 to main without user approval.
