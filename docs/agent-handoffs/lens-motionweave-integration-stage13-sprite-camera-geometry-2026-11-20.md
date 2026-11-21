# AIVERSE LENS Stage 13 — camera-relative sprite geometry from frozen packets (2026-11-20)

## Commits and scope
- Branch: `feature/lens-motionweave-integration`; `main` not changed.
- Implementation: `62368463165c5c012b256ee2038ef40d8aa4a7e2` (`modules/rts/view/Rts2DView.h`), `854ef094569ac1c8b65aff8f3307c654e906b816` (`modules/rts/view/RtsSprites.cpp`).
- Follows Stage 12: `docs/agent-handoffs/lens-motionweave-integration-stage12-particle-lights-2026-11-20.md`.

## Architecture and API
- Primary `beginFrame` clears `preparedSprites_` and keeps the primary `SpriteBatch`.
- All primary `emit` invocations retain an immutable-by-convention `PreparedSprite` value containing a copy of `SpriteLook` (including FX-scaled dimensions), ground position, facing, action, animation start time, team/tint, corpse flag and fade. This also captures death/corpse visual emissions.
- `emit` computes animation frame UVs from the original `time_` for every camera; no new animation or gameplay update is run on secondary captures.
- Secondary `submitPrepared` uses `Renderer3D::camera()` to determine the camera-local basis and sun caster right axis, temporarily swaps the emission target to a distinct GPU-initialized `secondaryBatch_`, rebuilds geometry from packets, then restores the complete primary camera basis/state.
- Shadow casters and sprite quads use `secondaryBatch_`; existing immutable static tile mesh still uses primary `batch_` for drawMesh.
- This changes neither public method signatures nor ECS components, asset formats or serialization. No genre-neutral engine headers have acquired genre dependencies.

## Validation
- GitHub source inspection and textual invariants (secondary batch init, packet capture, camera state restoration, secondary flush, no old primary batch writes in emit/decal region): **DONE**.
- CMake configure, C++ compile, CTest, Metal/Vulkan/GLES backend execution, image parity and frame profiling: **NOT RUN**. Do not consider stage validated until compiled.
- No benchmarks run; copying `SpriteLook` for each rendered sprite can be expensive at extreme sprite counts. A later stable-handle/compiled-look packet optimization may be warranted.

## Known caveats
- Camera-specific sprites share the same visibility set resolved on the primary frame. Occlusion and visibility *policy* is not re-evaluated for captures, by design.
- Frames are snapshotted by action/start time and use the same frozen presentation clock. Frame-dependent directional sprite atlas rows and ground decals now use the secondary camera basis.
- The secondary batch is scratch storage; it assumes `CameraRenderPipeline` immediately executes the scene pass for each submitted camera, rather than queuing multiple cameras' callbacks for future execution.
- GPU-side dynamic vertex buffers are still rewritten per camera.
- If secondary submission throws during geometry generation, restoration is not exception-safe; use a scope guard if introducing throwing work there.
- Rendering/lighting/shadows/corpse and atlas parity still require native execution.

## Next action
1. Compile `aiverse_client` plus `aiverse_editor` with the updated sprite translation unit and address compilation problems.
2. Add regression tests for two cameras at different yaw/pitch; verify `SpriteBatch` quad orientation, directional atlas rows, shadow caster axes, identical animation phase and unchanged corpse state after multiple submissions.
3. Audit the other non-particle dynamic light producers for per-camera replay and optimize prepared snapshots if profiling warrants.
4. Sync MotionWeave branch with real merge reconciliation before the agreed consolidated test pass; do not claim unexecuted tests pass.
