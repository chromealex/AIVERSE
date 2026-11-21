# AIVERSE LENS Stage 16 — sprite GPU upload and matching-orientation camera reuse (2026-11-20)

## Branch and code commits
Branch: `feature/lens-motionweave-integration`; `main` untouched.

- `79bad3d519dd861bf886aa2283a7e5d08fbf7035`: `engine/render/SpriteBatch.h` — dirty/upload-valid state.
- `7526a8c0a834443ea2c00d05d6fce953660f1bbf`: `engine/render/SpriteBatch.cpp` — sort, CPU vertex packing, and GPU upload only when the geometry is dirty.
- `8bea4e0631653a1c2fe74de1b3919ff3ec0ee047`: `modules/rts/view/Rts2DView.h` — previous secondary camera basis/quality cache.
- `3137f8ce1a93db9580a1f157b40f414dcc53d78b`, `9d34cbdc3f5cedad63d71bf77953cc446acb594c`: `modules/rts/view/RtsSprites.cpp` — camera-basis lookup and same-frame geometry reuse.

## How it works
1. `SpriteBatch::begin` invalidates vertex and caster GPU uploads. `quad` and `quadEx` invalidate draw upload; `caster` invalidates caster upload. `shutdown` invalidates both.
2. `flush` and `flushCasters` retain already packed GPU data when no intervening geometry mutation occurred; projection and per-camera lighting uniforms are still rebound on every draw. Draw-call grouping and ordering are unchanged.
3. Secondary `Rts2DView::submitPrepared` compares the requested camera's derived basis (right, up, forward, ground right/front), shadow caster axis and shadow quality with the immediately previous secondary draw. When identical, it reuses `secondaryBatch_`, including its GPU vertex data. Different camera orientations still rebuild their own billboards/atlas facings/ground shadows.
4. Each primary `beginFrame` and shutdown invalidates the secondary geometry cache. Camera translation or projection/FOV alone does not require new world-space billboard vertices if derived axes are identical; each renderer still supplies its own matrices and light uniforms.
5. This is an optimization of renderer presentation only; no additional ECS simulation tick, MotionWeave update, corpse transitions, or particle update.

## Compatibility
- No changes to public method signatures, ECS schema, save data or asset formats.
- `SpriteBatch` is generic engine code; `Rts2DView` remains a genre adapter.
- A cached batch is confined to the same `Rts2DView` instance and logical presentation frame. No inter-frame geometry reuse.
- When orientation or shadow quality differs, all preparation and GPU upload paths still run.

## Actual validation performed
- Current GitHub branch source inspection and confirmation of invalidation/reuse code paths.
- Read-back of edited `SpriteBatch.cpp` and `RtsSprites.cpp` and inspected the new cache and draw logic: DONE (source-level only).
- CMake configure / C++ compiler: **NOT RUN**.
- CTest, device-backed Metal, Vulkan, GLES tests, screenshot/pixel parity: **NOT RUN**.
- CPU/GPU benchmark: **NOT RUN**; improvement is not quantified.

## Risks and next work
- Must verify identical-orientation capture correctness for cameras with different translations, near/far/FOV and render targets; confirm no stale shadow geometry after quality or sun direction changes. Equality checks intentionally use exact computed basis values, so near-equivalent cameras may miss cache without producing incorrect geometry.
- Existing `SpriteBatch` GPU buffers are fixed size and rely on RHI staging semantics; capacity limits, out-of-bounds uploads and multi-camera in-flight lifetime must be audited before claiming stress safety. This stage **does not** implement a GPU-safe growable ring.
- Repeated submits are sequential in `CameraRenderPipeline`; asynchronous deferred draw callbacks or unrelated writes into the same batch need explicit epoch/lifetime validation.
- Exception safety of the mutable temporary camera basis in `submitPrepared` is unchanged from Stage 13; add scope-guard protection before allowing throwing user-provided callbacks.
- Next bounded step: audit/fix dynamic sprite buffer capacities and actual backend upload behavior; add regression tests for stable versus changing bases, light/sprite parity, and test in one consolidated native run per user preference. Synchronize newer MotionWeave head with proper conflict reconciliation before final validation.
