# AIVERSE MotionWeave / Universal View — Stage 139 handoff

Date: 2026-11-19
Branch: `feature/universal-ecs-animation-pipeline`
PR: https://github.com/chromealex/AIVERSE/pull/22 — keep OPEN / DRAFT.
Previous: [Stage 138](animation-stage138-image-morph-gpu-admission-2026-11-19.md).

## User-approved execution order and branch constraints

- Implement features first, document each bounded stage, and perform the **entire CMake/CTest/sanitizer/graphics validation only after actual feature freeze**. Do not mark source-only regression fixtures passing.
- Continued Stage 138 at `34cc7c971114529b49f08846a982b7d99b70ae91`.
- Source commits in this stage:
  1. `a7b1f49cec36f2532a5815c564ebee48df20c8df` — new `engine/render/FailedModelUpload.h`;
  2. `d67f66e76a12b57a26f7b11ba6b160b7efc5f38d` — road direct uploads;
  3. `3ce8530338e1238a901530dece12d675d3fcfc51` — shore direct uploads;
  4. `54358bb010e23534a43f07374937e0994b69fb5f` — modelview raw Model uploads.
- Last code HEAD `54358bb010e23534a43f07374937e0994b69fb5f`, as verified by GitHub readback.
- Against `main`: feature ahead **1037**, behind **305**; against PR base `feature/universal-entity-inspector-fps`: ahead **1407**, behind **615**. Both diverged. Do not merge/rebase/force push or alter peers' branches. Peer-owned renderer/terrain/water/VFX/batching changes must be reviewed on a separate integration worktree.
- No main, PR base or other agent branch changes were made.

## Problem

Stage 138 made `render::uploadModel` return false on missing geometry/image/morph GPU handles and gave cache-owned Model uploads a safe GPU-fenced rollback. Several *non-cache* native presentation hosts still called `uploadModel` without checking false and then published the broken Model. If upload threw partway through, they could `reset()` the unique pointer before the GPU had finished referencing newly allocated resources. Road/shore mesh chunks and modelview could have stale/invalid draw records. Some vector/map publication allocations also occurred after GPU creation.

## New generic direct-host abort contract

`engine/render/FailedModelUpload.h` exposes:
- `render::FailedModelUploadDisposition` with `NoModel`, `ReleasedAfterGpuIdle`, `OrphanedWithoutGpuProof`.
- `render::abortFailedModelUpload(gfx::Device&,std::unique_ptr<render::Model>&) noexcept`.

This is a **rare failure-path-only synchronous fallback**, intended for native direct Model owners that lack `ModelResourceCache`. If the Model exists and `waitForResourceIdle()` proves GPU completion, it calls `releaseModel` then destroys the CPU object. If the backend cannot prove idle, or a Device release throws, it deliberately relinquishes CPU ownership without destruction, preserving potentially GPU-visible resources (intentional leak instead of use-after-free). The render-owner thread and a live Device are required. Hosts log a warning on the orphaned path.

**Never use this for routine chunk replacement or frame-to-frame eviction**: an idle barrier can stall. Prefer `ModelResourceCache::retireUnpublished` or a proper host-owned GPU-token queue for ordinary retirement.

## Updated native hosts

### RTS roads: `modules/rts/view/RtsRoadView.cpp`
- Direct prop imports now check `uploadModel`, preserve CPU-only load failures as null tombstones, and call `abortFailedModelUpload` only after the GPU upload was attempted. `propModels_` capacity is reserved before GPU upload to prevent a later growth failure from destroying a live Model.
- Road chunk generation pre-reserves `chunks_` and checks `uploadModel` for each chunk, skipping incomplete chunks. On failure or exception, it safely aborts the partial GPU Model and writes an explanatory warning.
- Editor road-ghost upload now checks the result and uses the same fail-closed cleanup path, without publishing a broken preview Model.

### RTS shores: `modules/rts/view/RtsShoreView.cpp`
- Direct prop imports now check `uploadModel` and safely abort partial GPU uploads; the prop vector is reserved before GPU allocation.
- Generated shoreline chunks and transparent foam meshes check upload success. Vector capacity is reserved in advance, and failed partially uploaded models are aborted rather than inserted into the draw lists.
- The old `-1` failed-prop result is retained, and generated-geometry failures are reported through `warnings_`.

### Standalone preview: `apps/modelview/main.cpp`
- Creates the map slot for the raw Model **before** GPU allocation. The old `models.emplace(path,std::move(m))` after upload could throw and discard a GPU-live Model.
- Checks `uploadModel`; if upload or later model/clip/bake preparation throws, partial GPU resource destruction requires verified idle through the generic failure helper. If unsuccessful, map slot remains null so the model is not drawn; logs describe failure.
- The separate `ModelAssetProvider` path is unchanged.

All changes are in host adapters or the genre-neutral render helper. No MotionWeave ECS/gameplay clock, FPS/RTS rules, networking/save format, animation asset schema or real GPU-fence semantics changed.

## Important unresolved feature boundary

**Routine destruction of successfully uploaded road/shore geometry is NOT solved by Stage 139.** `RtsRoadView::release`, `buildGhost`, `RtsShoreView::release`, their `shutdown` paths and some custom decal meshes currently call `releaseModel` / destroy native resources directly while replacing geometry. Their host renderer ownership/frame submission boundary and any existing GPU completion policy must be inspected before converting to an explicit per-host fenced retirement queue. Do not replace this with a per-frame `waitForResourceIdle` stall. This is a key Stage 140 feature.

Other direct `uploadModel` calls in editor/ModelAssetProvider/geometry/scenes/tools may ignore bool and need a separate source audit. `abortFailedModelUpload` on an unresponsive GPU intentionally leaks CPU Model storage; it does not claim recovering the Device, reporting actual driver VRAM or providing a bounded retry queue.

## Actual verification and withheld tests

**ACTUAL:** GitHub branch/main/PR-base comparisons, Stage 138 read, affected native source/header and GPU retirement queue inspection; four exact source commits with SHA-guarded writes, readback verifying checked upload/abort helper integration and current HEAD.

**NOT RUN:** CMake configure, native compile/link, CTest, macOS Metal, GL/Vulkan, FPS/RTS/editor visual comparisons, real GPU fault injection, sanitizers, benchmarks or final validation runner. This follows the user's feature-first requirement and current lack of a mounted macOS worktree. No tests passing or performance improvements are claimed. PR #22 remains DRAFT.

## Next Stage 140 — feature-only

1. Finish **normal successful-model lifecycle** for road/shore/ghost props and procedural chunks with owner-owned ordered GPU retirement tokens, safe retry/idle fallback and explicit shutdown drain, without blocking ordinary frames or violating renderer submission boundaries. Confirm corresponding image/paint/decal owners separately.
2. Audit ModelAssetProvider, editor previews and other native direct Model upload users for missed upload results and post-upload exception ownership; preserve geometry/animation API behavior and genre independence.
3. Continue Universal View and AnimationGraph host integration/LOD/hot-reload work from the Stage 137 feature-freeze gate; do not claim full feature completion without scope validation.
4. Keep source-only work and independent handoff for each stage, update `AGENTS.md`, journal and `docs/models.md`. Only **after all functional gaps are closed** run `tools/animation/validate-final.sh` once on a supported Mac and collect real GPU/visual validation and logs. Avoid automatic merge of divergent main/PR base.
