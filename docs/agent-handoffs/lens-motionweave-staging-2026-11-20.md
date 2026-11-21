# AIVERSE LENS + MotionWeave integration staging (2026-11-20)

## Actual repository state
- Staging branch: `feature/lens-motionweave-integration`.
- Base animation HEAD at staging: `7c04338019a80bf6d82150f7db9ad791872c6adc`.
- Staged tree commit: `d46d273e43e9e22f8e074bce5e794df513ba2a14`.
- LENS source HEAD when scanned: `6a5f32102686c14d8337da74add5425fa2e0c38f`.
- Added exactly 63 previously absent LENS files (camera core headers, ECS components, Studio camera window, CPU test fixtures and LENS handoffs) by reusing their existing Git blob hashes. No MotionWeave files were overwritten.
- **This is NOT a full merge**. The staging commit has one animation parent, not both branches as merge parents; the divergent histories remain unmerged. LENS core call sites and backend integration are not yet reconciled.
- Existing draft PR #49 (animation branch -> LENS branch) was non-mergeable; avoid force-merging.

## Verified blockers in staging base
- `CMakeLists.txt` lacks camera_* test registrations.
- `engine/render/Math3D.h` lacks LENS asymmetric frustum API and pose camera modifications.
- `engine/gfx/Gfx.h` lacks `captureTexture` and `waitIdle`; camera headers will not compile until the RHI contract is merged.
- `apps/client/GameClient.{h,cpp}`, `apps/editor/Editor.cpp`, `engine/gfx/{metal,gl,vulkan}` need careful semantic merge. Animation changes in `RtsView.{h,cpp}`, `RtsViewDraw.cpp`, `RtsAnimate.cpp` are source of truth; do not overwrite them with the LENS branch.
- `RtsView::drawFrozenModels` is a temporary LENS-only path and is NOT in the animation base. Replace it with immutable packet capture + per-camera submit over the animation branch's universal View API, not a second animation evaluation.
- New Studio camera file is staged but editor build and renderer integration are **not compiled**.

## Integration rules
1. Preserve animation team's `genericView_->update`, `submitOne`, `View::submitModel`, attachment composition, appearance generation and reload contract.
2. Integrate generic LENS-only RHI, Math3D, scheduler and ECS changes by targeted three-way reconciliation, not wholesale source replacement.
3. Produce a genuine immutable camera frame after the single MotionWeave update, then submit to N cameras independently (per-camera LOD and culling). Do not replay mutable `RtsView::draw`.
4. Ensure texture source and animation model/material lifetime through GPU fence completion.
5. Run the consolidated build/CTest and backend tests before claiming integration complete or merging to main.

## Actual checks
- Remote GitHub recursive tree inventory and Git-object creation/branch update succeeded.
- Native `git merge` and full three-way conflict resolution could not be performed in this runtime (no direct GitHub network checkout; connector only).
- **NOT RUN**: CMake, C++, CTest, Metal/Vulkan/GLES compilation or GPU tests.

## Next step
- Reconcile Math3D/Gfx/CMake from LENS against MotionWeave, then source-level compile check.
- Merge GameClient/Editor and render backend files.
- Implement and test MotionWeave VisualFrame adapter.
