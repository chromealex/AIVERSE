# PRISMA Stage 54 — GPU-fenced Renderer3D custom cameras and ScreenLighting targets

Date: 2026-11-19
Repository: chromealex/AIVERSE
Working branch: feature/prisma-postfx
Draft PR: https://github.com/chromealex/AIVERSE/pull/26
Base: feature/universal-entity-inspector-fps
Previous handoff: docs/handoffs/prisma-stage-53.md
Pre-handoff branch SHA: df66683f93658fcbbb9425e93e396cf1dc0a6386
Main unchanged. Do NOT merge without explicit user approval.
User-requested sequence: finish all functional features first; run native CMake/CTest/GPU, parity, profiling and fixes only later.

## Completed implementation commits

1. `d3e1b717d61840d75a0337ad3fbd8ac515db83af` — `engine/render/Renderer3D.h`: public genre-independent `retireUnregisteredObjectMotionViews(liveViewIds)` contract for an explicit stable view roster.
2. `3bad8c77d349956d690336d6386881cb3748b140` — `engine/render/Renderer3D.cpp`: enumerate only object-motion camera IDs not present in the roster, snapshot IDs before erasing the map, and delegate each to existing `retireObjectMotionView(id, dev_->gpuSubmissionFence())`. No inference by view dimensions or CPU clock.
3. `fafea9e5cfb5360fdfb6750e4230b847c19c2b67` — `apps/editor/Editor.cpp`: pass the same Scene/Game live roster into both Prisma and Renderer3D orphan retirement at the end of camera submission. Preserve legacy explicit Scene/Game transition cleanup and the independent Prisma size cache retirement and GPU collectors.
4. `f84710e44f8e60111c111d78893fcf7cbf594284` — `apps/client/GameClient.cpp`: on menu-only and map/asset loading frames, explicitly retire both GPU owners' inactive per-camera histories and unreferenced Prisma size caches. The normal `sim_` path still retains live camera ID 3. Existing `startMatch` explicit retirement and per-frame GPU completion collection remain.
5. `290c980501d231135d8870fd6c1b54b282f5c136` — `engine/render/PrismaScreenLighting.h`: independent adapter-owned `PrismaGpuRetirementQueue<gfx::TextureHandle>`, unfenced quarantine, public `collectGpuRetirements()`, `drainRetiredAfterGpuIdle()` and read-only `retirementStats()`. Declare the GPU-idle prerequisite for adapter `shutdown()`.
6. `3c683cc4ca58dd46ced332e4ece86656431005a2` — `engine/render/PrismaScreenLighting.cpp`: `queueTarget()` takes real backend-issued submission token or quarantines an unknown token; `releaseTargets()` queues owned bounce/scatter/history/depth textures instead of immediately `destroyTexture`; `scenePass` opportunistically collects confirmed completions and also queues idle auxiliary targets when disabled. Resize is now deferred. Explicit idle drain before shutdown destroys remaining textures; shader/pipeline/triangle release stays under the existing full-device-idle prerequisite.
7. `f968a5e862ab66931fbf4a57bc0ba35884efab87` — `docs/prisma.md`: Stage 54 architecture and lifetime semantics, per-view IDs, opt-in adapter and verification limitations.
8. `df66683f93658fcbbb9425e93e396cf1dc0a6386` — `skills/visuals/aiverse-prisma/SKILL.md`: continuation contract and GPU fence invariants.

This file is a **separate** handoff commit after code, design documentation and skill changes.

## Architecture and public API

1. A host must assign a stable view ID before the camera render. After ALL camera draws for a host frame, it submits exactly the set of still-live IDs to **both**:
   - `Prisma::retireUnregisteredViews(liveViewIds)`
   - `Renderer3D::retireUnregisteredObjectMotionViews(liveViewIds)`
   Then call `Prisma::retireUnreferencedSizeCaches()` and nonblocking GPU completion collectors. A hidden/closed view absent from the roster has its camera-private histories dropped; another camera of the same width/height is unaffected. The resize-keyed `Sized` cache is a separate Prisma owner.
2. Renderer3D motion owner calls the existing `retireObjectMotionView`, which queues exclusively owned object velocity target, private depth and reactive mask. It never touches borrowed Prisma depth, compositor history, external model/texture caches or ECS state. A backend submission fence can cover the lifetime; when token=0 the original `motionUnfenced_` quarantine remains until a GPU-idle drain. The existing `collectCompletedObjectMotion(gpuCompletedFence())` is responsible for actual destruction after confirmed completion.
3. `PrismaScreenLighting` is an *optional distinct adapter*: it owns only its auxiliary bounce, scatter, private legacy TAA history[2], private depth, plus its own shaders/pipelines/vertex buffer. Its target retirement is independent of the existing canonical Prisma queue. On resize, or on the NEXT `scenePass` after `setEnabled(false)`, move targets to a local queue tagged with `dev_->gpuSubmissionFence()`, or quarantine them if no token exists. Do NOT destroy synchronously while the GPU may reference them.
4. `PrismaScreenLighting::collectGpuRetirements()` releases only textures whose completion tokens are actually signaled by `dev_->gpuCompletedFence()`. The query is nonblocking and also invoked at start of adapter `scenePass()`. `retirementStats()` reports pending counts and last completed token. `drainRetiredAfterGpuIdle()` and `shutdown()` **require explicit host-confirmed GPU idle**, not CPU frame delay, because shutdown also releases pipelines/shaders/triangle directly.
5. Main/Legacy Prisma HDR, TAA, motion, camera volumes, editor external `postfx` panel ID, `postfx` JSON schema and SpectraFX responsibilities are unchanged. No feature is activated by default.

## Actually performed verification

- Read the latest GitHub working branch HEAD, Stage 53 handoff, `AGENTS.md` from prior stage, the original Renderer3D/PrismaScreenLighting owners, `Renderer3D.h/.cpp`, `PrismaScreenLighting.h/.cpp`, and both Editor/GameClient frame and lifecycle call sites.
- Wrote six source commits and read back all six changed source files from the connected GitHub repository.
- Static structural/call-order checks: **30 / 30 passed**. Checked generic object-motion API and implementation, stable ID rather than dimensions, real submission token, zero-token quarantine, completed-fence collector, editor roster for both owners, size-cache cleanup, menu/loading transitions, explicit startMatch close, client completed-fence polling, lighting queue/own targets, no direct destroy in releaseTargets, disabled/resize transitions, declared idle drain/shutdown, and preservation of canonical temporal resolve.
- This is **NOT** a C++ compiler check, link test, shader test, GPU fence stress test or gameplay runtime test. It does **NOT** prove memory freedom or production safety on Metal/GL/Vulkan/GLES/WebGL.
- **NOT RUN** per user's features-first sequence: CMake configure/build, clang, `./run-tests.sh`, CTest, editor/client startup, driver/device lost/recreate, GPU completions, frame overlap, HDR screenshots/alpha parity, actual bytes/fence pressure and CI.

## Known risks/limitations

- `Renderer3D::retireUnregisteredObjectMotionViews` relies on an accurate host-provided live ID list and on the graphics backend submission fence covering last use of each retired view's target. Unknown tokens are deliberately quarantined, not freed.
- Existing Editor Scene/Game closure logic remains beside generic cleanup; on an already retired ID, the second call is a no-op. Do not duplicate semantics or infer custom host activity from their hard-coded IDs.
- GameClient's view ID 3 remains the only actively rendered native camera. Other native multi-camera hosts must opt in to both retirement contracts and supply their own stable active roster; no global registry of every graphics owner is automatically manufactured.
- Optional ScreenLighting adapter is not automatically integrated into Editor/GameClient render loops. Its nonblocking collector is invoked on `scenePass`; if the host never calls the adapter again, the owner must explicitly collect and perform final GPU-idle shutdown. No arbitrary CPU-frame age purges.
- Historical ScreenLighting shader/pipeline/triangle shutdown still assumes the host synchronizes GPU idle, unlike nonblocking target retirement. No device-loss recovery is invented.
- No test has verified the backend `gpuSubmissionFence()` / `gpuCompletedFence()` semantics or cross-queue synchronization across Metal/OpenGL/Vulkan/GLES. Treat these as contracts to verify later, not certainties.

## Next functional stage 55, before test phase

1. Audit the rest of the user-requested PRISMA effect catalogue and quality-path fallback semantics (DoF, auto exposure, motion blur, temporal upscaling, sun shafts, shader graph, UI/editor camera volumes), closing real functionality gaps without duplicating SpectraFX or generic RHI ownership.
2. Consider a reusable native camera host lifecycle helper / begin-end frame guard to pass a single ID roster to Prisma and Renderer3D, and opt-in ScreenLighting retirement as appropriate. Avoid deleting unregistered active views: host list must be authoritative.
3. Validate source-level GPU lifecycle for resource creation errors, orphan buffers and effects disabled across camera cuts. Maintain every effect as off by default until GPU-native verification can prove it safe.
4. After ALL functional work is completed, perform the separately documented native build, CTest, Metal/GL/GLES/Vulkan/WebGL pixel/latency and fence-overlap validation and fixes.
5. Commit a separate `docs/handoffs/prisma-stage-55.md` after Stage 55's bounded source changes, recording code SHAs, actual tests vs NOT RUN, known limits and precise next step. Keep Draft PR #26; no merge to main without permission.

## Mac pull commands

```bash
cd ~/Projects/AIVERSE
git status --short
git fetch origin
git switch feature/prisma-postfx
git pull --ff-only origin feature/prisma-postfx
git log -1 --oneline
```

If another worktree is rebasing or has unmerged changes, create a clean dedicated worktree from `origin/feature/prisma-postfx` instead of switching or resetting that conflicting worktree. Never force push or discard concurrent agent work.
