# AIVERSE LENS Stage 33 — transactional Overlays GPU resources and per-camera snapshot submission (2026-11-20)

## Branch / commits / changed files
- Repository: `chromealex/AIVERSE`; feature branch: `feature/lens-motionweave-integration`. `main` unchanged.
- `e16237962884e267e14fca529eabead85e2e647b` — `modules/rts/view/RtsOverlays.h`: additive GPU-ready status and guarded `shutdownAfterGpuIdle`.
- `3dfb2ede289edc1044835c9bb2b76d73709489d2` — `modules/rts/view/RtsOverlaysDraw.cpp`: staging of shader, four pipelines, white fallback texture and per-definition icon texture cache before publishing; rollback on create failures, refused GPU idle and foreign-device invocation. Preserves previous GPU handles until a successful idle barrier.
- `c08b1ae92d23dc2b07d8eb53448bd3084f63ec50` — `tests/tests.cpp`: fault injection for shader, third pipeline, white texture and idle refusal; explicit cross-device and retry assertions.
- `99eccb3dc9cd1f84bec00c96fe68bd6d0c244aa4` — `modules/rts/view/RtsOverlays.h`: add `NativePresentationRetirement retiredBuffers_`, `pendingGpuRetirements()` and frame-boundary `collectRetiredGpuResources`.
- `4e70975fee2d59361f9ab7fef79a49fb532bd98e` — `modules/rts/view/RtsOverlaysDraw.cpp`: allocate replacement dynamic overlay VB BEFORE retiring the old via ordered GPU token, and set `snapshotUpdates=true`. On failure, old VB is preserved; owner shutdown requires successful deferred-retirement drain.
- `a2d4f7238f5703966a505c2ec954820645cb3663` — `modules/rts/view/RtsView.cpp`: aggregate and poll overlay VB retirement at the existing pre-render model/resource frame boundary.
- `3cbb03b25df1628d09a9aeaa5ff4d76e0e175a11` and `33ce9c797db0a3ee8a70e149fcfa83f13c746b21` — `engine/render/TextureCache.h`: explicit noexcept owner/content swap with bundled atlas/page/sprite caches and raw RHI device pointer, plus utility include.
- `f6cf89c6237a0cf845220f7f68845c942fb0b84b` — `modules/rts/view/RtsOverlaysDraw.cpp`: publish staging icon cache with explicit no-throw swap.
- `6ae7316193f887d7ac1971384d1c92b56f171d1c` — `modules/rts/view/RtsOverlaysDraw.cpp`: each drawCustom callback captures an owned per-camera snapshot of vertex data, resolved GPU pipeline/texture handles, draw ranges and vertex buffer instead of reading subsequently mutated `this->upload_`, `buckets_`, `pipes_`, or `vb_`.
- `90ea86f789b9cc0357d89f0d9fa5db0370693760` — `tests/tests.cpp`: test `TextureCache::swap` ownership and exactly-once mock texture destruction.
- Previous handoff: `docs/agent-handoffs/lens-motionweave-integration-stage32-visibility-gpu-transaction-2026-11-20.md`.

## Design / API contract
1. `RtsOverlays::initGpu(dev)` rejects a foreign existing RHI owner. It privately stages shader, four pipeline variants, white fallback texture and icon textures in a fresh `TextureCache`. Any missing shader/pipeline/white texture fails initialization (throws), with cleanup of unpublished resources. Missing optional icon files still generate ordinary warnings and fallback behavior, matching the old nonfatal icon policy.
2. If the instance already owns GPU resources, a successful full `dev.waitForResourceIdle()` is mandatory before publishing replacement handles and destroying old pipelines/textures/vertex buffers. A refused idle preserves old live handles. Publication swaps icon cache, texture references and warning vector without allocating.
3. `shutdownAfterGpuIdle(dev)` provides guarded, idempotent teardown; direct `shutdown(dev)` also verifies no foreign device and drains any deferred vertex buffers before destroying active RHI objects. Host-level `RtsView::shutdown` already establishes a device-wide idle barrier.
4. `flush()` now allocates a new VB before queuing the old buffer for GPU retirement, rather than synchronously destroying a still-referenced VB in a rendering frame. Retirement is polled at `RtsView::collectRetiredModels()` (pre-render frame boundary). The VB opts into dynamic snapshot updates for earlier GPU draws recorded in the same frame.
5. Enqueued custom render callbacks capture immutable per-submission vertex arrays and compact draw commands (pipeline/texture/buffer/ranges), not mutable owner fields. This avoids overwriting a queued camera's overlay payload when another camera prepares different overlays before the callback executes.
6. `TextureCache::swap` moves the complete owner of textures/atlas sprites using noexcept container swaps, preserving heap object addresses referenced by `TextureRef::sprite`. Old caches are destroyed only after the GPU idle proof.

## Tests authored but NOT executed
- `overlays_gpu_init_is_atomic_and_retryable`: mock shader failure, third pipeline failure, white texture failure, idle refusal after a successful init, cross-device reinit/shutdown rejection, then retry and successful shutdown. Counts old versus staged shader/pipeline/texture destructors in `fogfaulttest::Device`.
- `texture_cache_swap_transfers_gpu_owner_without_invalidating_handles`: two generated textures in distinct caches, owner swap, cached reference identity and exactly-once texture destruction.
- Both tests are in existing `tests/tests.cpp` and use its CPU-only recording device; this is not actual GPU validation.
- Deferred buffer growth and multi-camera submission snapshots have source-inspection coverage in this stage but still require a dedicated runtime regression with a captured custom draw recorder and actual Metal/Vulkan/GLES GPU checks.

## Actual verification
- Read current `RtsOverlays.h/.cpp`, `RtsOverlaysDraw.cpp`, `TextureCache.h/.cpp`, `RtsView.cpp`, `NativePresentationRetirement` contracts and the recording test device.
- Re-fetched all modified production and test source files after GitHub commits; verified APIs, staging, snapshot capture, buffer retirement polling, and test names.
- **NOT RUN:** native CMake configure/build, CTest, test binary, Metal/Vulkan/GLES compilation or GPU runtime/pixel parity, benchmarks. No test green/passed claim.

## Known limitations / next steps
1. `RtsOverlays::initPrepared` still updates *CPU* definition and icon reference state before `initGpu`, so a later GPU init failure does not revert the entire overlay definition publication. Complete an outer RtsView-wide transaction to stage CPU+GPU resources atomically.
2. The staged texture cache cleanup handles unpublished allocations. Exception on backend resource destruction after a proven idle still needs an explicit host device-loss/recovery policy. Bounded memory for multiple simultaneous camera snapshots also deserves profiling.
3. Write a regression that schedules two custom overlay draws with different vertices before replay, proving the first callback uses its own snapshot; test resize/retirement token ordering and no draw on failed VB allocation.
4. Audit deferred resource collection for all editors/clients; `collectRetiredModels` must run at actual pre-render frame boundaries. Do not call GPU wait/retirement-token creation in the middle of a recording pass.
5. Stage Roads/Shores/Terrain GPU kit ownership, then reconcile latest MotionWeave against `feature/universal-ecs-animation-pipeline` via real three-way merge preserving both sides. Perform user-requested consolidated CMake build/CTest plus Metal/Vulkan/GLES validation before merging into `main`.

## Stage 33 status
Overlays shader/pipeline/icon staging, deferred VB retirement and per-camera immutable callbacks: source written and committed, **not yet compiled or executed**.
