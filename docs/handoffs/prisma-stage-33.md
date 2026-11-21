# PRISMA Stage 33 — GPU-confirmed timeline and deferred GPU resource retirement

**Date:** 2026-11-19.
**Branch:** `feature/prisma-postfx`.
**Draft PR:** https://github.com/chromealex/AIVERSE/pull/26.
**Previous stage:** `docs/handoffs/prisma-stage-32.md`.
**Main:** not modified; no merges from other agents' feature branches.

## Implemented files and exact commit SHAs

1. `d00eb61863b8284417def969c3f7feae427ee05f` — `engine/gfx/Gfx.h`: introduce optional GPU-completion API `gpuSubmissionFence()` and `gpuCompletedFence()`. These are distinct from CPU `frameSerial()`; both return zero when no verified token is available.
2. `e5d60b7aed33c71b58cef311991bde0655550914` — `engine/gfx/vulkan/VkDevice.h`: expose Vulkan current recording serial as a future GPU-submission fence and the verified `completed_` serial as the completed timeline.
3. `7c95ed1ffb0aea4bc4d9c4890acd5973ccb021f1` — `engine/gfx/vulkan/GfxVulkan.cpp`: only advance completed serial when `vkWaitForFences` returns `VK_SUCCESS`; a device-lost/failed wait cannot certify resource retirement. Vulkan uses existing slot-fence wait, no extra `vkDeviceWaitIdle` during a normal frame.
4. `fe4751c84f20f993f622b0765bc14cbf95fd1328` — `engine/gfx/metal/GfxMetal.mm`: attach a GPU-completion handler to the actual current MTLCommandBuffer and monotonically publish completed command-buffer tokens into lifetime-safe shared atomic state. Only `MTLCommandBufferStatusCompleted` counts as verified GPU completion.
5. `a2a46234ff661e45e27c1dcd42ab733b6309152a` — `engine/gfx/gl/GLApi.h`: Windows GL33 dynamic loader includes `glFenceSync`, `glClientWaitSync`, `glDeleteSync` and the needed GLsync types/enum values.
6. `6a99c944d23b60ae3b4b24502aad912c009b31f5` — `engine/gfx/gl/GfxGL.cpp`: insert a GLsync after current-frame graphics commands, `glFlush` (never glFinish) to submit, and nonblocking timeout-zero polling in `beginFrame` for strictly ordered completion. Delete signaled GLsync resources and publish the verified completed token.
7. `14ef5ff16ee2812fd57ceccfcc13a8e0c7306a11` — `engine/render/Renderer3D.cpp`: `beginScene` now automatically collects retired object-motion GPU textures once a backend confirms completion; `queueObjectMotionTexture` prefers the live current-frame backend-issued submission token, rather than relying on a user-provided or CPU-index token.
8. `ec396ff1aa56ae94f68d6a2cc75bba145abe7636` — `engine/render/Prisma.h`: archive evicted per-size context as `RetiredSized` with a submitted GPU token; public explicit `releaseRetiredSizesAfterGpuIdle()` and `pendingRetiredSizes()`.
9. `cb12b257acf45dc1b67bfd10cb0e847185151394` — `engine/render/Prisma.cpp`: replace immediate `Sized` LRU destruction with a retained whole context (raster/depth targets, bloom cache and GPU graph executor pool); the pool is cleaned only when its last referencing GPU submission has completed or at explicit GPU-idle teardown.
10. `69245a4e099c5bfe5d03602304d18bd467d7e7e5` — `engine/render/Prisma.h`: add a GPU-fenced retirement queue plus unfenced quarantine for targets dynamically replaced inside an active Sized context.
11. `4b64a1f759cb4b7d0ba48dd2bfadf5e2eb730644` — `engine/render/Prisma.cpp`: `target` now queues replaced HDR/mip/size GPU textures for completion-gated release rather than immediately calling destroyTexture; collector drains these and graph executor retired resources using the verified backend token.
12. `7069cb33e3a346fe71de374e216f57721df6d254` — `docs/prisma.md`: API, lifetime behavior and untested limitations of Stage 33.
13. This handoff `docs/handoffs/prisma-stage-33.md` is a separate commit after the implementation.

## Architecture and API

`gfx::Device::gpuSubmissionFence()` returns a nonzero token only for the **current active GPU frame** (including commands encoded now and scheduled for submission in endFrame). This future token is safe to tag a resource that was last used by any command on the same in-order graphics queue up to this frame, even though the current frame is not yet complete. It is NOT a CPU frame-completion value.

`gfx::Device::gpuCompletedFence()` reports the highest verified GPU-completed token, **not** `frameSerial()`. Metal uses command-buffer completion status and an atomic updated in a completion callback. GL inserts glFenceSync and polls glClientWaitSync with timeout zero at next beginFrame. Vulkan reuses already present per-slot VkFence; completed serial advances only on successful fence wait. Tokens are per Device / queue, never transferable to another device or context. A zero token means unsupported or not yet verified; never assume release is safe.

The renderer calls `collectCompletedObjectMotion(dev.gpuCompletedFence())` at the beginning of every scene. It automatically uses the current `gpuSubmissionFence` when replacing object motion velocity/depth/reactive targets. Once the token is confirmed, the Stage 32 PrismaGpuRetirementQueue releases them on the graphics owner thread. The existing manual `setObjectMotionSubmittedFence`, `retireObjectMotionView` and GPU-idle-only drain APIs remain for custom lifetime control. Unknown/unfenced resources are quarantined until explicit GPU idle.

For the PRISMA renderer, at most three `Prisma::Sized` contexts remain *active*. An evicted size set is moved wholesale into `retiredSizes_` and tagged with its current GPU submission. Its persistent Bloom topology and RHI graph pool stay alive with all target and depth allocations until verified GPU completion. `collectCompletedSizes` runs when requesting sized targets; it also services executor-internal retired graph allocations. Individual target replacement for HDR format/dimensions is likewise fence-retired via `retiredTargets_`. Without a usable token, retired allocations remain in unfenced quarantine. `releaseRetiredSizesAfterGpuIdle` requires a host-confirmed GPU-idle point and drains all archived contexts and individual targets. `Prisma::shutdown()` assumes the host's standard device teardown GPU-idle semantics.

**No new user-visible PostFX or ECS API** and no changes to shaders/visual settings/genres: this stage is resource-lifetime plumbing. All existing experimental flags and default-off behavior stay intact.

## Verification actually performed

**Performed:** inspected RHI frame submission/completion/GL loader and Renderer3D/Prisma lifecycle source on the connected GitHub branch. Wrote code via the GitHub connector and re-fetched 9 modified files to confirm new declarations and call sites exist. Manually inspected the new GLsync and Metal completion callbacks plus PRISMA LRU retirement flow.

**NOT RUN** (explicit user instruction to prioritize features first): native compilation, CMake configure, CTest, GLES3/GL33/WebGL2/Metal/Vulkan builds, shader compilation, image capture, GPU fence signal correctness tests, CI, leak/memory/perf profiling, standalone view resize/editor smoke. No claim of production readiness or passing tests.

## Known limitations / residual safety risks

- Backend fence behavior still must be tested on target platforms. WebGL2/driver GLsync limitations may prevent completion, in which case a retired resource is deliberately retained instead of incorrectly freed. Excessive viewport resize churn can therefore grow quarantined memory until explicit GPU idle.
- GLsync is inserted at endFrame and polled at next beginFrame; no additional CPU GPU wait is introduced. Metal completion callbacks execute asynchronously; completed token is read atomically. Vulkan only advances `completed_` when the existing slot fence wait succeeds.
- Frame tokens cover the **normal graphics queue**, not any untracked external queue/compute device. A custom renderer must coordinate multi-queue ownership with additional fences rather than reusing this token.
- Only the Renderer3D object-motion temp target path and PRISMA Sized/target contexts now use these GPU completion tokens. Per-view exposure-history, LUTs, SpriteBatch, FX and other subsystem resource caches still follow preexisting lifetime/shutdown assumptions. Stage 33 does not retroactively certify every transient GPU resource.
- The full Prisma shutdown/device teardown is still expected to happen after the platform makes the GPU idle. The API is designed for safe **frame-to-frame retirement**, not a magical proof that an arbitrary shutdown is safe.
- Vulkan backend already uses a blocking frame-slot fence wait to reuse per-frame resources; the new Device API does not add its own separate per-frame stall. Metal/GL paths are nonblocking.
- No actual native/GPU compile validation was performed, so an implementer should avoid enabling previously experimental graph-owned targets until the user authorizes tests.

## Next Stage 34 — feature work still first

1. Add a well-defined per-view shutdown API for both PRISMA exposure/motion histories and Renderer3D motion targets, using the verified GPU-fence queue; attach to editor Scene/Game close and client camera switch paths.
2. Add bounded GPU-resource retirement diagnostics (pending count/bytes) and idle-drain hooks where the real host synchronizes; distinguish actual GPU fences from CPU elapsed frames.
3. Extend automatic GPU-fenced retirement to PRISMA exposure-history and independently refreshed LUT/graph texture resources. Avoid making shared graph/color caches depend on a single camera ID.
4. When the user allows native tests: check GL33/GLES/WebGL2 fence function loading and signaling, Metal completion callback thread safety, Vulkan device lost path and fence serial invariants, resize churn and RHI resource memory over time. Then update this handoff with actual PASS/FAIL results and fix regressions.
5. Record separate `docs/handoffs/prisma-stage-34.md` after the next feature stage, with exact source SHAs and real test status. Do not merge to `main` unless explicitly requested.
