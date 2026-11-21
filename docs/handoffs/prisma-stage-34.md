# PRISMA Stage 34 — per-view temporal history retirement and editor GPU-resource diagnostics

**Date:** 2026-11-19.
**Working branch:** `feature/prisma-postfx`.
**Draft PR:** https://github.com/chromealex/AIVERSE/pull/26.
**Previous handoff:** `docs/handoffs/prisma-stage-33.md`.
**Main:** not modified. No other agent branches merged.

## Implemented work / exact commit SHAs

1. `40f5bca165053e3a8e1b3194310ca7d45e3d1957` — `engine/render/Prisma.h`: public per-camera `retireView(uint64_t)`, frame-independent `collectGpuRetirements()`, and `RetirementStats` for exposed RHI resource diagnostics.
2. `93bcd4fac0e82efe242ec893796c555614b6ae20` — `engine/render/Prisma.cpp`: retire one camera's two exposure history textures with submitted-GPU-fence tagging (or unfenced quarantine), remove only its motion projection/borrowed-input state; explicitly do **not** destroy size-shared scene render targets. New nonblocking GPU collector and known-minimum pending VRAM estimate based on live RHI texture descriptors, including archived Sized targets/depth and graph-pool entries.
3. `a49e5bccb50c4003ca5cbd0cf540b37f45da2778` — `engine/render/Renderer3D.h`: optional default fence argument on `retireObjectMotionView(id, lastSubmittedFence=0)` and public `MotionRetirementStats`.
4. `58cdc3b433c7e0a10751bb55d7d7c144c64bb570` — `engine/render/Renderer3D.cpp`: read-only per-camera motion retirement diagnostics: live views, fenced/unfenced textures, last backend GPU-completed token, and known lower-bound memory bytes via RHI textureInfo.
5. `64700c373011c6815e589e98841a24e1ea9ca03c` — `apps/editor/Editor.h`: frame-scoped flags for Scene/Game postprocess rendering activity and separate historical liveness. `Editor::renderGame` now accepts an optional explicit stable PRISMA viewId (default 2).
6. `6ef8a9281449f6c5e1105516ab983002925c0457` — `apps/editor/Editor.cpp`: Scene while playing uses viewId=1, Game uses viewId=2, preventing shared temporal history. Editor UI tracks which tabs **actually invoked** PRISMA this frame and at transition to hidden/closed/not-rendering calls `prisma_.retireView(id)` and `r3d_.retireObjectMotionView(id, dev.gpuSubmissionFence())` exactly once. GPU-completed resources are also collected on frames without a Scene/Game render pass.
7. `8eda68a91208c0e5e4827c15f8e1bdd6467ae7c5` — `apps/client/GameClient.cpp`: retire the native client's view ID 3 before starting a new match or changing the camera/map context. Poll pending retirement on UI/menu/loading frames, not just while scene rendering.
8. `7793271d754c73aa8f9893cd982f96c1cb9537c1` — `apps/editor/windows/StatsWindow.cpp`: render a new "PRISMA · GPU resource retirement" section with GPU completed fence, active/retired post FX sizes, live camera histories, pending fenced/unfenced targets, graph targets and minimum-known MiB waiting for release.
9. `97605304197bb190dd71bba7440166920a85812c` — `engine/render/Prisma.cpp`: fix diagnostics to distinguish fenced and unfenced `RetiredSized` context allocations in the reported texture counts.
10. `3cb9c4bd8fb0c961cfc6a3f1f78675a2b77b74f7` — `engine/render/Prisma.cpp`: `setImageLoader()` now GPU-fence-retires replaced LUT and ShaderGraph texture handles instead of immediately releasing potentially in-flight GPU resources at a project reload.
11. `23a151761b13268f6735e07d05136575442581de` — `apps/editor/windows/StatsWindow.cpp`: clarify the asset-memory section is separate from, and not comprehensive with, partial render-target retirement estimates.
12. `b0d93590f6f1f5c49d1a4d5b66174b1d8a3c4471` — `docs/prisma.md`: Stage 34 behavior and integration constraints.
13. **This handoff** — `docs/handoffs/prisma-stage-34.md`: branch/architecture/API/commit changes/test state/limitations/next action.

## Architecture and stable API contract

- `Prisma::retireView(viewId)` removes **private per-camera temporal state only**: two 1x1 RGBA16F AutoExposure history textures are transferred to the Stage 33 fence retirement queue; borrowed velocity/reactive texture descriptors and previous camera transforms are discarded *without destroying the externally owned textures*. Same-resolution Scene/Game views may share `Sized` working targets, so these are intentionally NOT invalidated on camera close.
- `Prisma::collectGpuRetirements()` is callable from the graphics owner on frames with no 3D view; it polls the already completed backend `gpuCompletedFence()` and releases properly tagged old resources without blocking. Unfenced resources remain quarantined until explicit GPU idle.
- `Prisma::retirementStats()` reports `activeSizes`, `retiredSizes`, `exposureViews`, `motionViews`, `fencedTextures`, `unfencedTextures`, `graphRetiredTextures`, `knownPendingBytes` and `lastGpuCompletedFence`.
- `Renderer3D::motionRetirementStats()` exposes the analogous rendering-side object velocity history resource counts.
- The lower bound `knownPendingBytes` uses actual `gfx::Device::textureInfo` where available (GL/Metal/Vulkan) and excludes allocations whose live metadata cannot be queried. It is NOT all GPU VRAM and does not guarantee accurate compressed/mip/graph internal accounting.
- `Editor::frame` owns the Scene/Game lifecycle rather than individual tab destructors: dock-hidden, closed, preview-disabled and nonrendering cases retire the prior view exactly on transition. The updated `renderGame(vp,cam,viewId)` keeps Scene view1 separate from Game view2 while the editor play simulation is shown twice.
- The standalone client uses view3, which is retired when a new match starts. Collection continues on UI frames even without scene rendering.
- `Prisma::setImageLoader` may be called when changing projects. LUTs and custom graph texture handles are now retired under GPU-fenced ownership rather than synchronously destroyed while older command buffers still read them.
- No gameplay ECS writes, FPS/RTS-specific systems inside PRISMA, changes to serialized `postfx` keys, or modifications to SpectraFX ownership.
- All experimental PRISMA flags remain off by default.

## Actual verification

**Performed:** inspected current connected GitHub branch's PRISMA history, Renderer3D resource queue, editor Scene/Game window render paths, GameClient map changes and Statistics panel; implemented code via GitHub connector and re-read changed source anchors. Docs and this handoff committed.

**NOT RUN (user explicitly requested features first):** native CMake configure, C++ compilation, CTest, Metal/GL/Vulkan GPU code, screenshots, editor/client runtime smoke, CI, GPU timing, memory leak detection, per-view idle/resume smoke. A commit is not a passing test; behavior remains unverified on a graphics device.

## Known limitations and residual risk

1. `Editor::Viewport::color` and `depth` are independent external framebuffers, still owned by their respective editor windows and resized via their own path. Stage 34 only retires PRISMA and Renderer3D temporal view resources, not these host-owned presentation targets. A follow-up should implement GPU-fence-backed retirement for editor viewport color/depth textures as well.
2. A tab may be dock-hidden for one frame and its private exposure history will be reset. This intentionally avoids stale adaptation on resume, but means adaptation restarts when switching tabs.
3. Statistics show a minimum accounting, not total VRAM. Graph-pool retired handles may not be included in the byte estimate. GPU resource sizes reported by RHI do not include driver overhead and may not reflect MSAA/sparse/aliased resources precisely.
4. If a backend never confirms a GPU fence, retired exposure/motion textures accumulate in quarantine until a host-confirmed GPU idle; this is preferable to unsafe early release but still needs a bounded pressure strategy.
5. The renderer's `Prisma::Sized` LRU remains keyed by render size; it is not a per-camera framebuffer cache. Avoid altering this without explicit view ownership/reference counting.
6. Existing full-device shutdown assumes the host has actually synchronized GPU idle as documented in Stage 33. The new per-camera `retireView` can be called while GPU work is in flight, but shutdown is NOT an implicit fence.
7. All shader/GPU features introduced in earlier stages remain uncompiled/unverified, including Metal/GL extension availability from Stage 33.
8. UI-level StatsWindow frame ordering means the latest completed-fence count and resources are a snapshot of the current graphics-owner frame, not an asynchronously refreshed performance benchmark.

## Next feature stage — Stage 35 (tests still deferred)

1. Implement GPU-fence retirement for actual editor `Viewport::color` and `Viewport::depth` GPU handles on resize/tab hiding, without invalidating image previews or releasing textures still referenced by ImGui draw commands.
2. Add a bounded GPU-memory-pressure API for quarantined resource pools, with explicit host GPU-idle synchronization at true pressure points (never a CPU frame-N heuristic).
3. Extend per-camera lifecycle handling for hosted client rendering modes and broader native camera owners, preserving stable IDs across simulated camera cuts and preventing accidental reuse of stale transient inputs.
4. Further integrate resource diagnostics across `PrismaRhiExecutor` internal graph retirements and optionally per-renderer FX/Water GPU allocations.
5. Once user authorizes tests: build the editor/client on Metal, GL and Vulkan, verify Scene/Game simultaneous views, tab hide/reopen, match changes, AutoExposure transition, resize/format churn, expired texture handles, and resource counts before/after real GPU completion. Fix regressions and document actual PASS/FAIL.
6. Record `docs/handoffs/prisma-stage-35.md` separately after the next functional stage, with exact commits and genuine test status. Do not merge PR #26 into main without an explicit request.
