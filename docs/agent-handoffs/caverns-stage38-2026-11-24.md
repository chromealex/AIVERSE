# TerraWeave Caverns — Stage 38 GPU retirement and upload throttling (2026-11-24)

## Provenance

Repository `chromealex/AIVERSE`, branch `feature/terraweave-caverns-core-20261123`, **not merged into main**.

GitHub commits for this stage:
- `c4b9e24069e4ef781d6822bf17fe4242bde0962a`: `engine/render/NativePresentationRetirement.h` — no-global-idle native model retirement queue collection
- `744d17ac1437a8b28e6bf2d3202ed3d794828627`: `engine/render/GeometryScene.h` — deferred shutdown interface
- `d2129dc63bb637ea52ccf7f74ba98901278d3d25`: `engine/render/GeometryScene.cpp` — model-only GeometryScene deferred shutdown and completion polling
- `82416ba7acab31de98ce138a10a982cfd10abd4f`: `engine/render/TerraWeaveCavernsGpuSwap.h` — preserve retired scene owners with submission fence tokens and no-idle poll
- `5cc183fed68c74caa7e736734039d2d68ef2446b`: replace remaining old retired-owner push calls after struct migration
- `2ff5c56e925ac9b0d0901ed8b57304d5f07f5b12`: new `engine/render/TerraWeaveCavernsGpuUploadBudget.h`
- `6e9a8e8df89c92f93a150cf043ec570f82f2d8bd`: `engine/render/TerraWeaveCavernsShardGpuHost.h` — source-byte admission limits, pre-upload fence polling and statistics
- `926088917175e29be50845c9fdd02695e4dc0e9e`: reserve old GPU scene retirement vector BEFORE external irreversible physics callback
- `5d314eb755a82dbd26768a8f6a91f077b0558a9b`: native `apps/editor/Editor.cpp` integration, 32 MiB source limit and one staged shard per frame
- `acee747cd86206f25937d26080886e4f3a1e9da3`, `036eea26ccde9fd11a53d9b206de1bdff765e49c`, `337196efa81dfd976842f242ef88bff8b45a12ec`: native `Editor.h`, `Editor.cpp`, `TerrainWindow.cpp` added source bytes/rejection and STRATA idle retention telemetry
- `edd444903191beb83e7346eca3f0ac0b919fcf8a`, `5a75f97deb91a9389b148f1f61b922031951ea01`: game runtime host exposes nonblocking fence polling
- `76e96eaefe855c401450ef18573605efa4ea4e8c`: `CaveGameplayEditHost` holds CPU prepared physics/NavMesh while incrementally uploading GPU shards across multiple frames, with one final exclusive publication
- `eef8521ad7e6c2a6d5c68d460554d0d7fe165170`: game frame adapter exposes source-byte limits and automatically polls retirements outside render passes
- `29b78c2adf6802cde762fd8ca4b034a29b7073ac`: mandatory `skills/world/aiverse-terraweave-caverns/SKILL.md` update
- This independent dated handoff is a subsequent commit; read branch HEAD for exact final SHA.

## Implemented functionality

**Fence-driven, nonblocking model-only resource retirement.**
`NativePresentationRetirement::collectWithoutIdle(device)` uses existing native asynchronous resource-retirement queue and GPU completion tokens but explicitly disables the fallback to `Device::waitForResourceIdle()`. `GeometryScene::deferredShutdownEligible()` is strict: no STRATA stage, portable scene, GPU index metadata/culling, compact/index sidecars or failed upload owners. `beginDeferredShutdown(device)` removes the old model scene from its owner after it has ceased rendering and enqueues its model GPU handles for retirement. `pollDeferredShutdown(device)` returns success only when GPU retirement tokens are complete. No GPU or renderer thread callback is used to mutate scene ownership.

`CaveGeometrySwapOwner` retains old whole `GeometryScene` owners with their GPU submission token; `collectCompletedWithoutIdle(device)` runs on the graphics owner outside render passes, requires a genuine nonzero `Device::gpuSubmissionFence()` and `gpuCompletedFence()`, then polls the model-only retirement path and erases an owner only when that entire old scene is safe to destroy. Missing/unsupported tokens or inactive-device situations keep the owner alive. `retainedStrataOwners()` reports owners that cannot be safely processed by this new path. The existing idle-proof `collectAfterGpuIdle` / `shutdownAfterGpuIdle` remain the explicit compatibility/final cleanup for STRATA and full device shutdown.

**Per-frame GPU upload backpressure.**
New `CaveGpuUploadBudget(CaveGpuUploadLimits)` provides source-geometry byte estimation (MeshVertex + triangle indices), a per-frame limit (default 32 MiB), max whole-shard GPU uploads per frame (default 1) and max retained previous scene owners (default 64 across shard owners). The limits are checked before `GeometryScene::loadPrepared`. Rejected stages remain pending and retryable next frame, preserving the last-good visible scene. The budget is a CPU source-size estimate and does NOT guarantee GPU upload milliseconds, actual VRAM residency or async intra-scene transfers; a single oversized source is intentionally rejected rather than permitted to violate the cap.

`CaveShardGpuHost` has `setUploadLimits`, `disableUploadLimits`, `uploadStats` and `collectCompletedWithoutIdle`; its `stageSnapshot` reclaims completed non-STRATA owners first, checks limits and preserves pending source and active scenes on refusal. `GpuPreviewStats` includes `retainedStrata`. Editor visual-only preview enables the 32 MiB / one-shard-per-frame cap, polls completions prior to staging, and reports frame source bytes, byte-admission refusals, GPU owner-pressure refusals and STRATA scenes still requiring full idle in the TerrainWindow. Editor preview continues to leave game Box3D/NavMesh unchanged.

**Multi-frame game runtime upload preparation.**
The existing Stage34 `CaveGameplayEditHost` previously attempted a full multi-shard `GeometryScene` GPU stage in one frame, conflicting with a strict per-frame upload cap. Stage38 now captures a complete `CaveMixedLodShardBatch` from the same authored snapshot after preparing Box3D, SpatialWorld and native NavMesh shadows **once**, and retains `gpuBatch_` / `gpuBatchIndex_` while `pumpGpuBatch` uploads one whole shard at each admitted frame boundary. Partial shards stay invisible. Only after the last shard is staged is `stagedRevision_` set and `commitAtOwnerBarrier` allowed to publish the entire batch. A new authored SDF revision cancels the prepared GPU shards and shadow resource candidates. Game owner configures limits via `CaveGameplayRuntimeFrame::setGpuUploadLimits`; frame hook polls completed GPU retirements outside a graphics pass.

**Publication safety.**
`CaveGeometrySwapOwner::activate` now pre-reserves its retired GPU-owner vector BEFORE calling an external callback which may irreversibly publish physical world state. That avoids an extra allocation-failure window between Box3D/native commit and the corresponding graphics scene switch.

## Explicit limitations

1. **STRATA sidecars are NOT yet nonblocking to destroy.** `GeometryScene::retireResident` still requests a device-wide full idle when portable index streams/culling metadata are present. No code in Stage38 attempts to bypass that proof. Those owners remain quarantined in the bounded old-scene queue until a real explicit safe GPU-idle point. Implement true fence-aware STRATA sidecar resource teardown/retirement in a subsequent stage if required.
2. Deferred removal uses backend GPU completion tokens, never just N CPU frames. `collectWithoutIdle` may return pending. A backend with no usable completion tokens will retain old GPU resources; eventually the explicit idle recovery/teardown must occur.
3. The upload cap is an approximate vertex/index source-byte admission budget, not a guaranteed VRAM cost or a GPU time limit. `GeometryScene::loadPrepared` itself is still a synchronous whole-shard submission and can stall CPU/render owner for a large shard. Future intra-shard upload paging and real GPU frame-time telemetry are separate features.
4. The new game-side paced staging assumes the host retains a stable `visualRevision` while a single batch spans frames. When the camera or author snapshot changes the host must cancel stale pending batches; Stage34 already cancels on authored `edited()`.
5. Stage35's Box3D final destruction remains potentially irreversible, and no actual game client/server lifecycle instantiation is yet demonstrated. Do not claim global four-owner transaction atomicity, rendered gameplay caves or feature branch build success.
6. When a shard's source size exceeds the configured hard limit, retrying will not solve it; a future feature must split the shard or provide page-by-page uploads. The editor currently surfaces rejection via its diagnostics and preserves the old scene.
7. Multi-shard GPU staging and the source-revision/LOD seam contracts from Stage37 must be kept for both editor visual-only preview and the real gameplay host; do not publish individual shard visuals early.

## Tests actually executed

Per user instruction **finish all features first, tests after**: Stage38 did NOT run CMake/CTest, native C++ compilation, native Editor, GPU captures on Metal/Vulkan/GLES, Box3D, SpatialWorld or NavMesh acceptance. No Stage38 test executable was added. GitHub connector confirmed all source/skill commits above. These acknowledgements are NOT proof of compilation or GPU runtime safety.

## Next feature Stage39

1. Optimize SDF sampling using an immutable spatial index of Carve/Fill sphere stamps and cave entrance influences. Bounded per-query candidate visits must preserve exact source field evaluation and snapshot identity; prioritize shared geometry/nav density queries and brush raycast.
2. Provide real GPU intra-shard streaming/upload paging for oversized batches (or safe shard splitting) and STRATA sidecar fence-backed release if user keeps this in scope; do not mark full STRATA nonblocking retirement until implemented.
3. Add a concrete game scene/client/server owner integration and a safe `RecoveryRequired` world rebuild path. These remain critical before gameplay caves are usable.
4. Continue mandatory `skills/world/aiverse-terraweave-caverns/SKILL.md` updates and separate dated agent handoff after each feature stage. Defer comprehensive native tests until functionality complete, per user.
