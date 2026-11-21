# AIVERSE LENS Stage 18 — exception-safe camera state and GPU initialization rollback (2026-11-20)

## Branch and commits
- Branch: `feature/lens-motionweave-integration`; `main` unchanged.
- Implementation `1224a4a267f113652797249db9922094a95c0892`: `modules/rts/view/RtsSprites.cpp` — local noexcept RAII guard in secondary-camera sprite preparation, restoring emission target, replay mode, camera, basis and shadow quality on normal or exceptional exits.
- Implementation `4e2062a0e22be7ee51524a84a61f794287e4099f`: `engine/render/SpriteBatch.cpp` — prevent reinitialization, transactional GPU init rollback only after confirmed `waitForResourceIdle()`.
- Implementation `888c13f25f45f03ef68ee28e29a4a55edb812ff5`: `modules/rts/view/RtsSprites.cpp` — clean texture cache for batch init failures; on exceptions during sprite/texture/type initialization, wait for GPU idle and clean all partially created resources, including when `ready_` was never set.
- Previous stage: `docs/agent-handoffs/lens-motionweave-integration-stage17-dynamic-sprite-buffers-2026-11-20.md`.

## Architecture, contracts and compatibility
- Secondary geometry preparation still uses the original frozen presentation state and separate secondary SpriteBatch. A local `RestoreCamera` guard restores all primary camera axes and the emission destination even when `emit`, geometry allocation or texture processing throws.
- `SpriteBatch::init` now rejects repeated init. Failed initialization only releases shader/pipeline/buffer handles after successful GPU idle; if the idle barrier fails, a runtime exception leaves ownership unchanged rather than destroying in-flight GPU resources.
- Partial `Rts2DView` initialization can clean up created tile meshes, both sprite batches, texture cache and prepared packets regardless of `ready_`. The asset-loading exception rollback also requires a confirmed GPU idle barrier.
- No API changes to ECS components, serialization, scene formats, MotionWeave, camera routing or renderer public methods.
- `engine/render` remains independent of game genres.

## Verification actually performed
- Read and inspected feature-branch `SpriteBatch.cpp`, `RtsSprites.cpp`, `Rts2DView.h` and GPU barrier conventions.
- Updated sources through GitHub and fetched them back; confirmed existence of RAII guard, rollback-after-idle path, and partial cleanup logic.
- **NOT RUN**: CMake configuration, native C++ compilation, full or selective CTest, Metal/Vulkan/GLES GPU validation, fault-injection tests and runtime screenshots.
- This stage is committed source code only; no passing build/test claim.

## Remaining limitations and explicit next work
1. Verify local `RestoreCamera` class access and noexcept destructor compile across supported C++ toolchains.
2. Fault-injection-test every GPU allocation and texture loader exception, including GPU idle barrier failure; the `SpriteBatch::init` early shader failure, partial failure, and successful reinitialization after shutdown need coverage.
3. Audit `Rts2DView::init` exception paths for failures thrown by either `SpriteBatch::init` before the asset-loading try block; today those exceptions may leave an earlier initialized batch or texture cache needing cleanup. Implement an outer initialization transaction.
4. A GPU idle barrier failure deliberately preserves objects; add an explicit later host-controlled retirement/recovery path rather than destroying them in-flight.
5. Audit `SpriteBatch::shutdown` and `TextureCache::shutdown` lifetime rules for ordinary teardown with multiple camera submissions in flight; this stage cannot claim device-wide GPU lifetime safety.
6. Sync the latest MotionWeave head with a conflict-aware merge. Follow the user's preference: finish the remaining functions, then run one consolidated native build/CTest plus Metal/Vulkan/GLES compilation and device-backed visual tests.
