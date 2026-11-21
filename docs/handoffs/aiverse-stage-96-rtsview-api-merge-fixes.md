# AIVERSE Stage 96 — Prisma / MotionWeave RtsView source-API merge fixes

Date: 2026-11-20
Repo: chromealex/AIVERSE
Branch: integration/prisma-motionweave-view
Baseline SHA: bee36be3688681c5a8362556e3492e848490c85a
Previous handoff: docs/handoffs/prisma-stage-95-view-null-entity-namespace.md

## Native Mac build evidence
The user's fresh `AIVERSE-prisma-motionweave-clean` Release build stopped at 77%, compiling:
- modules/rts/fx/RtsFx.cpp
- modules/rts/view/RtsView.cpp
- modules/rts/view/RtsTerrain.cpp

Reported:
1. RtsView.h:275 references deleted member `models_`, line 279 references deleted `textureCache_`.
2. RtsView.cpp:86 initializes bool from `RtsRoadView::collectRetired()` and `RtsShoreView::collectRetired()` — both have void return.
3. RtsView.cpp:535 uses `ScenePacketBatch::empty()`, but generic batch only exposes `size()`.

## Source changes
1. modules/rts/view/RtsView.h:
   - Call `resources_.forEachOwnedModel(...)` (the canonical owner iterator) and `appendModelOwnedGpuMemory` for each model, including pending retired Models that still own GPU allocations.
   - Account shared model textures once with `resources_.textures()->appendOwnedGpuMemory(counter)` if the cache exists.
   - Keep a single `GpuOwnedMemoryCounter` for handle deduplication, avoid counting borrowed texture and geometry-page handles through Model.
2. modules/rts/view/RtsView.cpp:
   - Poll road and shore retirement queues via their existing void `collectRetired(*dev_)` functions.
   - Report success only if existing model, image, geometry queues report success AND both road/shore queues have zero outstanding retirement items. Poll every queue unconditionally.
   - Use `genericObjects.size()!=0` instead of nonexistent `empty()`. Do not change the timing or owner of View initialization.

## API contracts verified by reading GitHub
- `ModelResourceCache::forEachOwnedModel(Visit)` visits resident and pending retired Models and executes on the owner thread.
- `ModelTextureCache::appendOwnedGpuMemory(GpuOwnedMemoryCounter&)` counts cache-owned, not Model-borrowed images.
- `RtsRoadView::collectRetired` and `RtsShoreView::collectRetired` return void, with separate pending count getters.
- `ScenePacketBatch::size()` exists.
- The new calls do not require changing shared render/core/View interfaces.

## Actual verification
Source readback, expected edit-anchor checks and structural invariants are performed. Native Clang compile, CTest, GPU backend validation and runtime/pixel parity are **NOT RUN** in this stage; the user must rebuild the new commit.
No changes to main or source feature branches.

## Next exact action
On a clean local worktree after current build exits:
```sh
cd ~/Projects/AIVERSE-prisma-motionweave-clean
git status --short
git fetch origin
git switch --detach origin/integration/prisma-motionweave-view
cmake --build build/merge-validation --parallel 4
```
Only after full build success run consolidated CTest. Log the actual results, and if new errors arise inspect first diagnostic rather than merely trailing make errors.
