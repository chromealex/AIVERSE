# AIVERSE LENS Stage 23 — staged sprite replacement during RtsView reload (2026-11-20)

## Branch, commits, files
- Repository `chromealex/AIVERSE`, branch `feature/lens-motionweave-integration`; `main` unchanged.
- Implementation commit `59ee989097dc9fb2284ca3461b4647126226316c`: `modules/rts/view/RtsView.cpp`, `RtsView::init` preloads the prospective `Rts2DView` before destroying the existing instance, and removes the late unguarded sprite allocation.
- Prior handoff `docs/agent-handoffs/lens-motionweave-integration-stage22-cross-device-ownership-2026-11-20.md`.

## Architecture and semantics
- Existing GPU generation and cross-device checks run before any change.
- A GPU idle barrier is required when a prior sprite view exists, before preparing a replacement. Failure throws without modifying the old view.
- The incoming `FileReader` is used to resolve sprite defaults from incoming `game.json` and composed rules. A new local `Rts2DView` is initialized with these defaults, the new `Rules` and existing content base / deferred-loading mode.
- If new sprite initialization returns `false`, `RtsView::init` returns `false` without retiring its old `twoD_` or mutating the other presentation subsystems. The new `Rts2DView::init` is responsible for cleanup on its failure path.
- On successful staging, the old sprite owner is shut down after the previously established GPU idle, then replaced. The standard model, fog, road, shore, terrain, overlay and animation reload proceeds afterwards.
- The late `twoD_ = std::make_unique<Rts2DView>()` / `init` call is removed to avoid allocating twice or deleting the staged view.
- This is a **sprite-only staging transaction**, not an atomic `RtsView` reload. Later errors can still partially update the other owners.

## Compatibility
- No changes to ECS components, on-disk schema, public C++ APIs, camera source interfaces or genre-neutral renderer headers.
- No extra scene simulation or MotionWeave update, and no second sprite animation clock.
- The config is read in a lightweight preflight and read again later through the original full reload path. This is deliberate for minimum scope but should be deduplicated before optimizing reload latency; rapidly mutating files could yield inconsistent defaults between the two reads.
- A rare failure in the staged `Rts2DView::init` that cannot establish GPU idle still needs a host-owned recovery/retirement solution: a temporary owned by `unique_ptr` is insufficient for robust device-loss recovery when partial GPU allocations persist. Do not claim this corner case solved.

## Actual verification
- GitHub read-back of modified `RtsView.cpp`: verified new sprite init occurs before `resources_.bindResourceDevice` and before `twoD_=`, and old late allocation is absent.
- CMake configuration: **NOT RUN**.
- Native C++ compilation: **NOT RUN**.
- CTest/fault-injection/Metal/Vulkan/GLES builds and runtime screenshots: **NOT RUN**.
- No benchmark or end-to-end reload test was run.

## MotionWeave status and next actions
- Last checked source branch: `feature/universal-ecs-animation-pipeline`. Divergent from the LENS integration branch, including meaningful changes to `apps/modelview/main.cpp`. MotionWeave was not merged in this stage.
- Next: add a persistent failed-init resource owner or explicit device-loss retirement pathway; deduplicate preflight configuration reads; stage other presentation resources with atomic publication where feasible.
- Write reload regression tests: successful old->new swap, failed staged sprite init preserves prior view, foreign device rejection, refused idle, and later non-sprite initialization failure.
- Reconcile MotionWeave explicitly with merge base and per-file conflict preservation; then perform consolidated CMake/client/editor build, CTest and Metal/Vulkan/GLES checks before merging to main.
