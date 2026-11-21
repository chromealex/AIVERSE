# PRISMA Stage 41 — truthful per-viewport GPU residency and fenced idle size eviction

**Date:** 2026-11-19
**Branch:** `feature/prisma-postfx`
**PR:** https://github.com/chromealex/AIVERSE/pull/26 (draft)
**Previous handoff:** `docs/handoffs/prisma-stage-40.md`
**Main:** unchanged; no merge of other agents' branches. The feature branch is being edited by connected GitHub API calls, not by a local checkout.

## Implemented changes — precise commits and files

1. `506d715b40b3608f1ccfba2a6cda7bd92eb877bd` — `engine/render/Prisma.h`: `MemoryStats` / `ViewMemoryStats` public API and stable view-to-size references.
2. `1ee100f0f5ccca9591ea1d2a909522bdc7630ea1` — `engine/render/PrismaRhiExecutor.h`: disambiguate live versus retired texture metadata gaps within existing `GpuMemoryStats`.
3. `1fb032892fb4fa663632e514d59eddca887366f8` — `engine/render/Prisma.cpp`: per-size deduplicated resident counts, per-view exposure history, per-view graph/shared reporting, optional resident LUT/graph-input bytes, view registration after a valid scene pass and deregistration at retire/shutdown.
4. `bb578ab3a9fa3846e8eec145d19084710ea2593b` — `engine/render/Prisma.h`: expose the subset of resident size-cache bytes not referenced by any live view.
5. `96aa64e12b1d7c646e988fcb5776b5acd5a2b1dd` — `engine/render/Prisma.h`: declare `retireUnreferencedSizeCaches()` for deliberate graphics-owner cache eviction.
6. `96d0bfbfcac4b1e8b07747f396557f674c1b58ca` — `engine/render/Prisma.cpp`: fenced retirement of size-scoped targets and their graph executor when no active view owns that resolution.
7. `5b22f2f33cfbc0ae51ba841c0ca5ab9d49535fc6` — `apps/editor/Editor.cpp`: call the cache trimming method only after Scene/Game views finish rendering and missing views have been retired.
8. `5aa4eae9fd620abb807a4f0abae0a8069993fa50` — `apps/editor/windows/StatsWindow.cpp`: show deduplicated shared cache memory, private view histories and graph pending/live bytes in a camera breakdown.
9. `3827f380b3a02c3a6388cda447b31aef2c7564de` — `apps/editor/Editor.cpp`: mark a view post-active only when PRISMA is actually active, so disabling post-processing releases stale per-view references.
10. `672996c7fc2f789c234c718ce93d0f9c53986682` — `engine/render/GpuResourceRetirement.h`: identify deferred ShaderGraph/Portrait-style owner bundles, textures and buffers with unquantified bytes; no guessed VRAM.
11. `7de52fd342d623c44c600b5749bba407dfb27501` — `apps/editor/windows/StatsWindow.cpp`: display missing-byte owner counters.
12. `d18abb68b5aa66d7c86106c1a03e3ff91995b28a` — `apps/editor/ViewportGpuRetirement.h`: add a **non-owning** live framebuffer observer with per-viewport color+depth base-level byte estimates.
13. `7d836d0fb056cfe7f51a05f29d023435a38d508c` — `apps/editor/Editor.h`: optional debug name argument to `Viewport::ensure`.
14. `30b7522c81a5610dcfbd91537dd595d691769de0` — `apps/editor/Editor.cpp`: register live handles after framebuffer creation and forget them before GPU-fence retirement.
15. `28588ae03b159607ef801241a6088afd3f45f312`, `6837ab0980cefc69f32cf2300978fb914937bc5c`, `3b6285f4b9eac11812023e234f63a127a2e3b0cb` — label `SceneWindow.cpp`, `GameWindow.cpp` and `FxWindow.cpp` framebuffers respectively.
16. `8b2796085d989c9eeafb5d3e5117969aa4d1bf9c` — `apps/editor/windows/StatsWindow.cpp`: report active per-window framebuffer bytes separately from PRISMA render-target caches.
17. `6584b26ac721a8419ff8c9fb30c0efaed03f1cd2` — `apps/editor/ViewportGpuRetirement.h`: make nested active record declaration precede its inline uses and include required utility header.
18. `cd6ce3655fb36b149d20471a1f75160c0cf4dc13` — `engine/render/Prisma.h`: clarify that the graph executor's cache belongs to output **size**, not to the camera/view ID.
19. `a24bbac8509dd78096cad0583e0cc4e5bf85a20c` — `docs/prisma.md`: full Stage 41 contract and limitations.
20. This file, `docs/handoffs/prisma-stage-41.md`, is a separate handoff commit after code and documentation.

## Architecture and public API

### 1. Prisma::memoryStats(): one allocation, one byte contribution

The editor uses a shared `Prisma` with stable Scene view ID 1 and Game view ID 2. Its `Sized` containers and `PrismaRhiExecutor` are keyed by dimensions, not by camera. Even if two cameras share 1920x1080, their postprocessing size targets/graph pool are one live allocation **for accounting purposes**. The `MemoryStats::sharedSizeKnownBytes` aggregate counts each live cache once. `ViewMemoryStats::sharedKnownBytes` is intentionally repeated for reference in the per-camera breakdown, and MUST NOT be summed across view IDs. `ViewMemoryStats::sharedSizeViews` says how many live camera IDs reference the same dimensions.

`Prisma::scenePass` registers a view's size only once its render target was successfully allocated. `retireView` removes its association and its private exposure history, not any shared cache still used by other cameras. Private `ExposureView` ping-pong targets are counted exactly once. Active `Lut`, graph input images and the black texture are separate `otherLiveKnownBytes`. Active graph-pool allocations are counted within each size cache; graph-retirement entries are reported as **pending**, not live.

`PrismaRhiExecutor::gpuMemoryStats()` adds `unknownLiveTextures` and `unknownPendingTextures` alongside the existing combined unknown counter. `MemoryStats::liveKnownBytes` deliberately does not add pending, viewport or unknown GPU allocations. RHI `textureInfo` reports dimensions/format only; no exact physical allocation size, compression, mip levels, dynamic backing, tiling or driver overhead. Format not supported by the estimate is unknown rather than an invented number.

### 2. Fenced cleanup of unreferenced size caches

`Prisma::retireUnreferencedSizeCaches()` is intentionally separate from `retireView`. It scans the very small active `sizes_` collection at the graphics-owner boundary, checks that no `viewDimensions_` entry references the size, and moves the entire cache (including graph executor) to `retiredSizes_`. It captures `Device::gpuSubmissionFence()` so current/previous GPU commands keep owning the resources until the existing genuine completed-fence collector or an explicit host-confirmed GPU idle. A zero/unavailable token stays quarantined; no CPU frame-number release, no forced GPU wait or immediate destruction is introduced. The editor calls the function after all windows finish producing passes and after detecting inactive Scene/Game tabs.

Editor only marks `prismaSceneRendered_` / `prismaGameRendered_` when `Prisma::active()` is true for that camera, preventing a post-disabled view from indefinitely retaining an old cache. Reopen may create new targets: bounded idle memory is prioritized over retaining stale warm caches. Native hosts may choose a different policy and need not call the trim method.

### 3. Scene/Game/FX viewport targets and deferred preview bundles

`ViewportGpuRetirement::noteLive(owner,name,w,h,color,depth)` and `forgetLive(owner)` maintain an observer list (they do not own GPU handles). `Viewport::ensure` calls `noteLive` after creation; `Viewport::release` calls `forgetLive` before retiring the previous color/depth handles. `liveStats(device)` reports each visible window's 2D framebuffer bytes without changing the lifetime owner. Scene, Game and FX use stable name literals; default `Viewport` remains allowed for other callers.

`GpuResourceRetirement::Stats` now counts `bundlesWithoutByteEstimate`, `texturesWithoutByteEstimate`, and `buffersWithoutByteEstimate`. In particular whole ShaderGraph/Portrait/Animation preview bundles may own substantial unenumerated bytes. The UI lists those as **unknown**, not as zero VRAM; this is not a per-bundle deep inspection. The previous 128MiB pending budget remains a *known-bytes pressure threshold*, not a hard GPU memory budget.

### 4. Ownership and scope boundaries

Everything here is presentation-only and genre-independent. FPS/RTS simulation ECS, deterministic ticks, `postfx` JSON data shape, PRISMA shaders, SpectraFX emitters, animation authoring snapshots and default-off experimental motion blur remain unchanged. Per-view observers are owned by Editor, outlive all editor windows, and do not take ownership away from Scene/Game/FX windows.

## Verification actually performed

**Performed:** inspected connected-GitHub source for Prisma size targets, motion/exposure view lifecycle, graph pool, generic owner retirement, editor frame order and viewport ensure/release call sites. Applied the code to the existing feature branch and fetched all 10 affected source files again. A 17-point **static presence check** confirmed memory API, view-size registration, fence-retire function, both new diagnostic sources, editor hookup and the three window labels. These are *source-structure* checks only, not semantic or runtime test results.

**NOT RUN (features-before-tests preference):** CMake configuration, C++/Obj-C++ build, CTest, editor or player execution, Metal/GL/GLES/Vulkan/WebGL smoke, fence runtime progression, framebuffer screenshots, physical memory measurement, CI, device loss, GUI enable/disable/resize stress. Do NOT mark this code compiled or certified.

## Risks and concrete next steps — Stage 42

1. First real validation when tests are authorized: build editor + client and inspect `MemoryStats` output for 2 equal-size views (one cache counted once), different-size views, Scene/Game/FX framebuffers, post-disabled camera, repeated dock hide/show, resize churn, memory pressure and confirmed backend fence collection. Verify no regression to older Stage 35–40 resource lifetime.
2. Check GL/Metal/Vulkan for `gpuSubmissionFence()` meaning during the editor frame before the UI pass; if any backend only exposes a *previous* submitted token, change fence-token scheduling before relying on the new eviction path. A zero token must quarantine, never free early.
3. All resident bytes are **lower bounds**, not actual driver-reported VRAM. The owner-bundle count is not byte-accurate; add owner-supplied byte estimators only when safe and genuinely available. Do not sum per-camera shared cache bytes or add pending bytes to live twice.
4. The per-viewport observer currently tracks only Scene/Game/FX `Viewport` color+depth. ShaderGraphPreview, Portrait and hosted GameClient are whole-owner bundles and/or different classes, and do not automatically appear in this *active-framebuffer* list. Their unknown bytes are correctly indicated by deferred bundle stats, but active bytes remain only partially measured.
5. `Prisma::retireUnreferencedSizeCaches` deliberately moves all idle sizes into fenced retirement after editor frames, so rapid tab toggles may allocate more textures and cause transient pending GPU pressure. This is a deliberate tradeoff; profile real editor usage before tuning policy (e.g. modest capped warm-cache retention).
6. Next *feature* priority: return to the actual postprocess catalog (for example expand temporal reconstruction/reprojection with vetted motion-vector validity, or another opt-in effect not already implemented). Keep PRISMA and SpectraFX ownership strictly separate and avoid changing default visuals without opt-in. Each new stage needs its own implementation + handoff.
7. After Stage 42, create `docs/handoffs/prisma-stage-42.md` documenting actual source SHAs, changes, tests that really ran, limitations and next step. **Do not merge draft PR #26 into main without explicit user instruction.**
