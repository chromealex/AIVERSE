# AIVERSE MotionWeave / Universal View — Stage 120 handoff

Date: 2026-11-19
Branch: `feature/universal-ecs-animation-pipeline`
PR: https://github.com/chromealex/AIVERSE/pull/22 (**Open / Draft**)
Previous: [Stage 119](animation-stage119-playback-continuity-2026-11-19.md)

## Git/parallel team status

- Continued Stage 119 HEAD `2d0fd8f04d5a7b0370ac961288713fd956f50983`.
- Before changes, connected GitHub compare: `main`
  `d71c6d47e532b25fb816a8bb1f96c41e5275a165`,
  branch ahead 798 / behind **0**. After functional changes:
  ahead 815 / behind 0, HEAD `17ed5a6716023736945dcf549632d43b32139dcb`.
- Stage 119's merged main ancestry remains intact. No rebase,
  force push, merge to main or conflicts during GitHub source updates.
- This stage owns genre-neutral GPU canvas lifetime and the legacy
  RTS paint *adapter*, not MotionWeave animation logic, NEREID water,
  SpectraFX particles, TerraWeave or Prisma PostFx.

## Implemented source commits (exact)

1. `667a2283da44375450a39570cbf63be8f428aaa0`:
   NEW `engine/render/CanvasGpuRetirement.h`.
   A general, header-only, device-owner GPU completion queue for
   `gfx::BufferHandle`, `TextureHandle`, `PipelineHandle`,
   `ShaderHandle`. Uses existing
   `AsyncResourceRetirementQueue<std::variant<...>>`, optional
   `RenderDeviceLifetime::Lease`, per-batch GPU completion tokens,
   a real idle fallback for unsupported tokens, and no destruction
   on Pending/Failed fences. Its `retire` is noexcept and records
   failed enqueue/orphan diagnostics; an OOM enqueue fallback may
   perform a proven idle barrier rather than releasing in-flight GPU
   resources. `collect` belongs only at the host frame boundary.
2. `d94df5da067be46dd1fdaba00f867e3c17a041b5`:
   `engine/render/RenderTexture.h` exposes borrowed owner
   `setRetirementOwner` and optional device lease, plus standalone
   `pendingRetirements/collectRetiredResources`. A native canvas
   with no borrowed owner creates its own owner during initialization
   and must explicitly shutdown/drain before the Device is destroyed.
3. `970d9e924f88315d149f135a0d36ff28d5256ff4`:
   `engine/render/RenderTexture.cpp` makes startup transactional and
   queues completed canvas texture/programs for GPU retirement.
   Borrowed owner avoids a synchronous barrier on each layer
   reset; standalone shutdown drains only after real full GPU idle.
4. `4dd1e7c3edff7f2489915d8bca970e12f572fe55`:
   `RenderTexture::flush` allocates the scratch vertex buffer BEFORE
   beginning the pass. If upload fails it retains pending brush
   commands and does not publish a partial stroke update. A successful
   pass retires scratch geometry through the owner, never by direct
   post-submit destroy. Exceptions likewise retire the buffer.
5. `6a0f3b0e864b56ae783c9987e2f8769adde63335`:
   `RenderTexture::blit` rejects invalid scratch buffer allocation
   without opening a pass, honors unflushed brushes, and queues its
   vertex buffer after submission. Partial upload's staging texture
   likewise retires through the shared fence queue; no direct
   post-blit `destroyTexture`.
6. `a1708bb9e0f75c67902322b3a85b8789b181b7d5`:
   `modules/rts/view/RtsPaintLayers.h` owns one shared
   `CanvasGpuRetirement` per GPU epoch (no new frame or device
   clock); exposes frame-boundary pending/collect and diagnostics.
7. `cfd1a864c41fd506dacff87187d58df8ac1c5cf7`:
   `modules/rts/view/RtsPaintLayers.cpp` changes layer source/size/
   viewer/clock resets to `resetLayers` that queues old canvas
   images, coast textures, meshes, shaders and pipelines but DOES
   NOT destroy the shared queue. Deleting a layer is asynchronous.
   Final `shutdown` drains once, with a proven GPU idle barrier.
   All generated canvas instances borrow that single lifetime owner.
8. `bec759b6017f522d7aa325767c02213be0fc2988`:
   `modules/rts/view/RtsView.h` forwards the existing host
   RenderDeviceLifetime lease to paint layers and exposes generic
   pending/collect/orphan metrics.
9. `044ffe2179240d3407dd95d14d76bf17ba4e5f1b` and
   `f2f11619743042612b5a3cf073fe0aeae0e4de1b`:
   `apps/client/GameClient.cpp` and `apps/editor/Editor.cpp`
   collect queued paint GPU handles ONLY at the owner frame boundary
   before new GPU draw passes (alongside existing Model/PostFx queues).
10. `47f6ecc25091a85ac357a792deeb1079c56ad73a`:
    `RtsPaintLayers.cpp` now preserves its last-good paint mesh
    when replacement vb/ib upload fails, while retaining/retiring
    partial new GPU handles. Empty geometry is handled explicitly;
    failed surface shader recompilations retire previous handles.
11. `947ba2726e4fe1d97a0c961b413876540fc78d5d`:
    `apps/editor/windows/StatsWindow.cpp` shows queued canvas
    GPU resource count and orphans.
12. `78854b4338b9459d2614816fd27855e000a94b4f` and
    `e2a601dd87767e14a82c7f03f0d59a0ecf6b01c7`:
    retirement-drain diagnostics distinguish unknown orphan handles
    from pending work. A real full idle allows safe Device epoch
    destruction even when native handles had to be orphaned after
    enqueue failure; the orphan diagnostic remains observable.
13. `37d16ef189d95e89b782a73a28c3add9c9b8bb88`,
    `f151011657461bbbabeed6c3e38e92c718645b1a` and
    `17ed5a6716023736945dcf549632d43b32139dcb`:
    expose GPU fence Pending/Failed/idle fallback and enqueue
    failures from the RtsPaintLayers owner through RtsView into
    the editor Statistics window.

## Resource ownership and API guarantees

- Native project C++ code can use `render::CanvasGpuRetirement`
  directly without referring to FPS or RTS. It owns device-bound
  transient buffers/textures/programs; `retire(handle)` never
  releases a resource without a real completion token or successful
  device idle. Supports optional host Device epoch.
- `RenderTexture::setRetirementOwner(CanvasGpuRetirement*)` must
  run before `init`. An external owner MUST outlive every canvas
  and be polled at one real host frame boundary per device frame.
  A standalone canvas owns a private queue and must be explicitly
  drained; otherwise it may need one blocking idle on shutdown.
- `RtsPaintLayers` is a host-side adapter of that generic owner.
  One shared queue survives per-layer removal, scene resets, editor
  multiple-view redraw and Lua source replacement. `RtsView`
  forwards the host GPU lease. Both GameClient and Editor poll at
  the beginning of the frame; they never poll inside a render pass.
- Direct `destroyBuffer/destroyTexture/destroyShader/destroyPipeline`
  calls have been removed from the modified
  `RenderTexture.cpp` and `RtsPaintLayers.cpp` files.
  Graphics shaders, brushes, Lua gameplay and MotionWeave ECS
  animation logic are unchanged.
- Last-good paint geometry remains resident until a valid
  replacement is available. Vertex-buffer allocation failure on
  a brush does not commit the pending strokes.

## Actual validation — no misleading test claims

Performed:
- Fetched current GitHub source/PR, compared branch with main,
  checked RtsView/RtsPaintLayers lifecycle and Editor/GameClient
  frame ownership, performed exact branch blob-SHA updates and
  re-read edited files.
- Source inspection confirmed no direct `destroy*` calls remain
  in the two changed paint/canvas implementation files.

**NOT RUN:** local checkout, native CMake configure/build/link,
CTest, CI, Metal/GL/Vulkan GPU submission/fence testing,
Lua paint visual parity, Editor two-view rendering screenshots,
ASan/TSan/UBSan, memory/performance benchmarks, and OOM injection.
All new code remains **UNVERIFIED FOR C++ COMPILATION OR RUNTIME**.
PR #22 must remain Draft until full feature completion and testing.

## Limitations and risks

1. `CanvasGpuRetirement` follows existing
   `AsyncResourceRetirementQueue` policy, including a possible
   synchronous idle fallback ONCE at a host collection boundary
   on a backend without async tokens. It does not issue GPU idle
   for every brush or blit. Enqueue OOM may require emergency idle.
2. Standalone canvas usage without explicit owner must call
   `collectRetiredResources` periodically or at least `shutdown`
   before destroying the GPU Device.
3. Shader, pipeline and transient GPU resource allocation error
   paths and rendering exception cleanup are source-only and not
   experimentally verified.
4. `RtsPaintLayers::drawCells` still retains legacy CPU palette/
   coast updates; this stage did not overhaul its visual algorithm
   or island-boundary sampling. A returned void `upload` API
   cannot propagate every async failure to Lua; later retry and
   visual diagnostics may improve this.
5. Independent `PostFx`, terrain/roads, SpectraFX and other
   GPU owners are separate domain contracts. Do not overwrite
   parallel agents' code or assume their lifetime is solved by
   this canvas queue.
6. Full retirement of legacy `RtsView` and exhaustive FPS/RTS/
   editor model/LOD/attachment visual parity remain incomplete.

## Exact next Stage 121

- Refresh `main...feature` and all relevant peer-branch heads.
  Do not force push, rebase over or overwrite parallel teams.
- Finish any remaining genre-independent model/LOD/attachment
  migration to canonical `aiverse::View` and preserve MotionWeave
  state through graph/model hot reload.
- Audit unleased PostFx and native RenderTexture callers not
  borrowing a host queue; make the owner contract explicit and
  address failed canvas uploads without losing source intent.
- Continue additional features FIRST. Then compile and run full
  native CMake/CTest, deferred failure-injection suites, Metal/
  Vulkan/GL editor/UI visual parity and sanitizer benchmarks;
  fix errors and warnings before PR #22 can leave Draft.
- Append a separate Stage 121 handoff and update `AGENTS.md`.

## Final Stage 120 owner-device preflight

After the initial handoff and documentation commits, two small safety
corrections were committed:

- `fe28951d0e65006f910246553287b53a7a2e56d4`:
  `CanvasGpuRetirement::validFor(const gfx::Device&)` verifies the
  exact Device identity and any optional, still-live host epoch.
- `b966ab4615b340e5185f3d2589765ced71f1f2e7`:
  `RenderTexture::init` now rejects an expired/foreign owner before
  allocating a new canvas texture or pipeline. A canvas cannot reuse
  the previous Device's retired GPU queue after device recreation.

Both are GitHub source updates only, without native build/CTest/GPU
validation. Continue Stage 121 after refreshing the branch head.
