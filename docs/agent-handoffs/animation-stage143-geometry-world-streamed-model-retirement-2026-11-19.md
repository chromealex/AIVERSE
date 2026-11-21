# AIVERSE MotionWeave / Universal View — Stage 143 handoff

Date: 2026-11-19
Branch: `feature/universal-ecs-animation-pipeline`
PR: https://github.com/chromealex/AIVERSE/pull/22 — OPEN / DRAFT
Previous: [Stage 142](animation-stage142-editor-preview-model-ownership-2026-11-19.md).

## User-approved execution policy

**Complete all functional MotionWeave / Universal View work first; then run all tests in one organized pass.** This stage changes engine presentation and two native client frame hooks, **not simulation authority**. It writes production code and documentation now; CMake configure/build/link, CTest, real Metal/GL/Vulkan, sanitizer, tests, GPU fault injection and benchmarks remain **NOT RUN**. The Mac-only final runner `tools/animation/validate-final.sh` is reserved for the actual feature freeze.

## Exact branch topology and implementation commits

Started at Stage 142 HEAD `83c25c97487ce278857c8f96b6ca04b432b21bdd`.

Commits, in order:

1. `a18d4d49cb866f7fd1ff6cedb83e0979e64f6f17` — `engine/render/GeometryScene.h`, adds generic native Model/HLOD retirement queue and owner API.
2. `acd626b41a6117a0cbf6204e90d7814e5e43976e` — `engine/render/GeometryScene.cpp`, moves resident/HLOD Models into GPU retirement on reload and exception rollback.
3. `0de4b4b09656e4ffedf1024693d316b4a6509807` — `modules/rts/view/RtsView.cpp`, polls static GeometryScene at the existing owner frame boundary.
4. `da01812f51fee3e0bea838a18dfb058293f9a870` — `engine/world/WorldScene.h`, adds managed shared-owner candidates and dedicated ordered retirement queue API.
5. `9bd40a83391b549cc2ed452a2937181b743ea171` — `engine/world/WorldScene.cpp`, defers GPU release until no other resident chunk or background CPU worker borrows shared Model data.
6. `84374b13308ad892fbcc91136ef06d4578450e65` — `apps/client/GameClient.cpp`, polls WorldScene GPU retirements from its pre-render Device-owner frame hook.
7. `fa11c32160ed62f8ecc3db50f83cea1b8d24355d` — `apps/fps/client.cpp`, polls WorldScene before `Device::beginFrame`.
8. `867e64347b8775f30f8e05a9c5ce3ecf2be234fd` — `GeometryScene.h`, adds source reload `clear(device)` that does NOT force GPU idle.
9. `e0bfadf4c29eacc40f295eb9c64b429bf8ab4159` — `RtsView.cpp`, uses nonblocking GeometryScene `clear()` for rules/project reload instead of blocking shutdown.
10. `1777cce83a9e6187c3a8e40472487d35eba3456d` — `apps/fps/client.cpp`, polls FPS static GeometryScene HLOD retirement at the explicit owner frame boundary.
11. `cde50eaf945d1eeb463594abe7b3dacda45e48b6` — `WorldScene.cpp`, returns a retryable false on queue/device callback failures without approving unsafe GPU deletion.

Last source readback HEAD: `cde50eaf945d1eeb463594abe7b3dacda45e48b6`.

At last comparison, main advanced significantly: feature ahead **1096** / behind **467** versus `main` (merge base `d7d9b3f4180dcba98b9238fd56eedc7ccb420bb1`; DIVERGED), and feature ahead **1466** / behind **615** versus PR base `feature/universal-entity-inspector-fps` (DIVERGED; merge base `c944882193c1404a7e47f409f8d7847c766b4e31`). **No merge/rebase/force push/PR retarget or writes to main/other agents' refs.** Re-check branches and renderer/geometry/world peer ownership before integration.

## GeometryScene: source/HLOD GPU lifetime

Before Stage 143, `GeometryScene::loadImpl` called `shutdown(dev)` before building a replacement. The old shutdown called `releaseModel` on every source and HLOD proxy immediately. If the same Device had already submitted those meshes in a previous frame, GPU still could read the released buffers. The catch path also called the same immediate destruction on partially uploaded Models. The successful old geometry was freed even during ordinary reload.

New genre-neutral engine API:
- `GeometryScene::clear(gfx::Device&)`: remove active Model pointers, static cache/instance/group records, and transfer every source/HLOD `unique_ptr<Model>` to the existing generic `NativePresentationRetirement` queue; does **not** block the full GPU for interactive reload.
- `GeometryScene::pendingGpuRetirements() const noexcept`: queued native resource count, for one owner pre-render hook.
- `GeometryScene::collectRetired(gfx::Device&)`: schedule/poll explicit hardware retirement markers, with ordinary NativePresentationRetirement fallback semantics.
- `GeometryScene::shutdown(gfx::Device&)`: detach active Models, wait for real full-GPU-idle completion, and drain both pending token batches and active Model resources before final destruction. An unsuccessful proof raises `std::runtime_error` rather than destroying potentially in-flight allocations.
- `loadImpl` now `retireResident(dev)` before loading and also on exceptions, including partial Model/HLOD GPU uploads. It no longer calls blocking `shutdown(dev)` during ordinary live reload.
- `RtsView::init` calls `geometryScene_.clear(dev)` when resetting rules/project and does not force per-reload GPU idle. `RtsView::collectRetiredModels` / `pendingModelRetirements` incorporate GeometryScene alongside ModelResourceCache, rollback images and road/shore scene queues. FPS `staticGeometry` now also polls in its own pre-frame hook.

**Limit:** This safely handles ownership after reload and rejection; it does not promise a last-good GeometryScene swap if a replacement file is malformed. Old visual source pointers are detached, and a failed reload can leave an empty scene. A later feature may stage the whole new geometry before replacing the old, if needed.

## WorldScene: shared Model + async worker + GPU ownership

Before Stage 143, `releaseChunk` directly called `render::releaseModel` at GPU-ineligible times. It tried to skip freeing shared model files when `shared_ptr::use_count()>1`, but this could lose the **last actual GPU release**: a background worker still borrowing a Model at chunk eviction could drop its reference later, after no host remained to release its GPU buffers. Conversely, destroying a last chunk while previous frame draws still used it could trigger GPU use-after-free.

New engine-only lifetime:
- `WorldScene::releaseChunk` removes every chunk's per-model `shared_ptr` and registers a **single manager-owned strong reference** per unique Model in `deferredModels_`. It does not call the GPU Device or destroy buffers while an unload callback runs.
- At an **explicit real pre-render owner frame boundary**, `WorldScene::collectRetired(device)` inspects the candidate registry. A Model may enter the ordered GPU queue only when `shared_ptr::use_count()==1` — no other resident chunk, stream payload, or CPU worker still owns it.
- The final-owner check and invalidation of matching weak cache entries occur under `ModelCache::mutex` shared with CPU workers' `weak_ptr::lock()`. The code erases only the weak entry pointing to the **same Model**, avoiding deletion of newer per-file imports. This prevents a new worker from borrowing that retiring Model after the last-owner check. Expired weak entries may also be removed.
- `retiredModels_` is a genre-neutral `AsyncResourceRetirementQueue<std::shared_ptr<Model>>`. Its completion token is created only in `collectRetired`, after earlier frame GPU submissions. A pending/failed marker is retained, not inferred from frame serial or elapsed time. Without native token support, explicit GPU idle fallback happens only at an advisory **128 resource** queue pressure threshold or final shutdown, not once per chunk every frame.
- `pendingGpuRetirements()` includes both deferred CPU-shared candidates and GPU queued resources. `GameClient::frame` and FPS client poll at established Device-owner pre-render boundaries, including FPS static GeometryScene's own retirement queue.
- Queue allocation or Device callback exceptions during collection return false without authorizing early GPU release. Rare OOM during initial candidate map insertion remains a potential exception during streaming unload, requiring native fault injection to evaluate.
- `WorldScene::shutdown` first joins/shuts down its chunk CPU preparation workers and retires all resident chunk ownership, then requires a verified `device.waitForResourceIdle()` BEFORE releasing queued/deferred Model GPU handles or NEREID water/cascade allocations. It abandons outstanding retirement tokens only AFTER full idle proof and clears the weak source cache. The embedding client still owns the optional shared `ModelTextureCache`.

**Limit:** This introduces a small manager-side candidate map and queue, not a general concurrent-Model writer. A worker may read immutable CPU metadata; it never mutates GPU handles. An external host **must** call `collectRetired` once per true Device frame boundary. It is not safe to call while an in-flight draw is still being recorded. Strong candidate references intentionally preserve old models/leases until CPU borrowers release; a leaked worker reference can delay final GPU reclamation. A 128-resource threshold is not a byte-accurate VRAM budget.

## Actual validation versus deferred work

**ACTUALLY PERFORMED:** connected GitHub source reads for `GeometryScene.h/.cpp`, `WorldScene.h/.cpp`, `AsyncChunkStream.h`, generic retirement APIs, RtsView and native client/FPS frame/shutdown call sites; source SHA-guarded commits and readback of updated branch files; branch/main/PR base ancestry comparison. No other agent branches modified.

**NOT RUN:** native CMake/C++ compiler/link, CTest, green CI for the Stage 143 head, Metal/Vulkan/OpenGL, source-reload HLOD parity, chunk streaming CPU-worker interleavings with ThreadSanitizer, real GPU token failure/fence rollback, FPS/RTS/editor screenshots, performance/VRAM profiling, or the combined final-validation script. Tests remain deferred until feature freeze by the user's explicit request.

## Next Stage 144 — feature-only

1. Complete the remaining Direct/Legacy View presentation migration and model/LOD/retarget hot-reload continuity, preserving a single MotionWeave graph clock and non-authoritative presentation.
2. Review renderer, texture and world peers' concurrently advancing branches; address unresolved device-generation ownership and stale worker/source reimport, including cancellation on failed GPU barriers, in an isolated integration worktree only after owner coordination.
3. Fix remaining direct GPU ownership in other native scene/preview sources (e.g. runtime character, modelview, custom geometry and NEREID). Coordinate with existing owners; do not overwrite their source while changes are in flight.
4. Keep a separate dated handoff + `AGENTS.md`, animation docs and universal journal after each implementation stage. **Do not run intermediate tests**. Only when the entire feature-closure checklist is honestly complete, run `tools/animation/validate-final.sh` on native Mac and verify real GPU, FPS/RTS/editor visuals, fences and performance before moving the Draft PR toward merge.
