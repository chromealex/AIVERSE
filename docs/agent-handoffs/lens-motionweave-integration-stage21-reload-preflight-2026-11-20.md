# AIVERSE LENS Stage 21 — sprite reload preflight and MotionWeave integration assessment (2026-11-20)

## Repository, commits, changed files
- Repository `chromealex/AIVERSE`, branch `feature/lens-motionweave-integration`; `main` unchanged.
- Code commit `d811574ebae5862c54453711499779213b3ec227`: `modules/rts/view/RtsView.cpp`. Moved the existing `twoD_->shutdownAfterGpuIdle(dev)` guard from the middle of `RtsView::init` to its beginning (after lease-generation validation and before resource bindings, field/fog resets, overlay init, roads/shores initialization and other mutations).
- Code commit `34c8c831ea1c82a408e28d563e6449f81e9acb39`: `modules/rts/view/RtsView.cpp`. Reject reinitialization when an existing sprite view is owned by a different GPU device, before invoking an idle barrier on the wrong device.

## Why
Stage 20 guarded sprite teardown on live reload, but the guard ran **after** mutation of renderer/visual state. If the GPU idle barrier failed, earlier reload operations already changed fog, fields, overlays and other presentation owners, leaving the live view partially altered. The guard now runs before those mutations. A stale GPU device cannot be used to release sprite resources allocated by another device.

## Semantics and compatibility
- On failed sprite GPU idle or stale device generation: `RtsView::init` throws before it binds resources or changes visual state, preserving the existing view for retry. The earlier sprite owner remains stored in `twoD_`.
- After successful idle and sprite cleanup, the remaining reload proceeds as previously. **This is not a full transaction for subsequent failures**: late failures in terrain/asset loading can still leave partial reload state. Future work should stage all presentation owners and swap only on full success.
- No ECS schema, save-format, content, camera API or MotionWeave animation changes; the check remains in the genre adapter, not generic camera code.

## MotionWeave synchronization check
- Compared `feature/lens-motionweave-integration` against `feature/universal-ecs-animation-pipeline` with GitHub compare. At the time of check the branches had diverged (animation ahead by 74 commits and behind by 125). Changed paths included `apps/modelview/main.cpp` and animation Stage 157 documentation.
- **No animation merge was attempted** at this stage. These histories need explicit three-way diff and conflict reconciliation; do not overwrite LENS/ModelView work or claim synchronized.

## Checks performed
- GitHub source inspection of `RtsView::init` and `shutdown`, `GameClient::shutdown`, plus prior Stage 20 handoff.
- GitHub compare of LENS versus MotionWeave and successful GitHub commits of source updates.
- **NOT RUN**: local CMake configuration, C++ compilation, Stage 20 fault injection, CTest, Metal/Vulkan/GLES GPU tests, pixel parity, reload fault injection.
- In particular, the new early guard behavior has **not** been verified in a running engine.

## Next actionable steps
1. Build and run the already registered `lens_sprite_gpu_faults` test; check `tests/cloth/FakeDevice.h` compatibility and fix compile errors before trusting results.
2. Add a mock-device `RtsView::init` regression that fails `waitForResourceIdle`, confirming no fog/overlay/field state mutation, no accidental release and retry on the correct device.
3. Audit `RtsView`'s full later reload stages, ideally perform staged reload of all presentation resources with an atomic publish, and audit cross-device ownership for other GPU subsystems.
4. Properly sync MotionWeave using common merge-base + file-by-file reconciliation, especially `apps/modelview/main.cpp`; create a separate handoff for that stage.
5. After functions and reconciliation, perform the user's requested one-pass native build/CTest + available Metal/Vulkan/GLES backend checks before merging to main.
