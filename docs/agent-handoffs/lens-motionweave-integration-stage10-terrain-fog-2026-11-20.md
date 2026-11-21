# AIVERSE LENS + MotionWeave Stage 10 — prepared terrain and fog replay (2026-11-20)

## Branch, implementation commits
- Branch `feature/lens-motionweave-integration` (NOT main).
- `acb4179127f36a3f4a363270a3a39123464d0e77`: `modules/rts/view/RtsTerrainRender.h` declares `drawPrepared` + shared `drawInternal`.
- `e528c67b42f0a4cbf7dc427e7fa3f08da5cc1bf6`, `c96dc82ba0813ef8c7ffc594711c16a0af71f1b4`, `eb318287fa434336bc96c9e3e61c3263bb3966b7`: `RtsTerrainRender.cpp` separates update/LOD cache maintenance from render-only camera pass.
- `78001c019bd8e6cab35e203e8a23129181a8f9ba`, `48f375f7ba8c25f024a8723dac500fad1931c51c`: `modules/rts/view/RtsView.{h,cpp}` expose `drawPreparedTerrainFog`.
- `251cbca4495c368e49b57cd91a1383c96f84aad4`: `apps/client/GameClient.cpp` invokes prepared terrain/fog in each LENS offscreen camera before snapshot model/geometry replay.
- `4a6868063f71531215384e32b02f7ac195c0ec5a`: `tests/render/camera_terrain_prepared_api.cpp` compile-only method signature check.
- `064e5c9551f75cb546d7a3d5a7c5f17343d38e8c`: CMake registered `camera_terrain_prepared_api`.
- Prior handoff: `docs/agent-handoffs/lens-motionweave-integration-stage9-specialized-2026-11-20.md`.

## Architecture
- The legacy `TerrainRenderer::draw` remains the primary stage: uploads dynamic cell state/vision, may lazily build detail LODs, ages LRU, tracks terrain stats and updates the traced heightfield. It also emits custom opaque render commands, culls terrain chunks and selects LOD per camera.
- The new `TerrainRenderer::drawPrepared(Renderer3D&,MapGrid&,vision)` shares the same renderer command path but NEVER calls updateDynamic/updateVision/buildDetailLod/updateHeightField or increments terrain frame/LRU/stats. It selects per-camera visibility and distance-based LOD from buffers already uploaded by the primary pass; missing prebuilt detail levels fall back to available levels instead of allocating. It only looks up already valid pipeline handles; no shader compilation in secondary passes.
- `RtsView::drawPreparedTerrainFog` never calls `RtsView::draw`, `VisibilityField::update`, `VisibilityField::build`, terrain rebuild, animation or game event handling. It queues the already prepared fog sheet through `VisibilityField::draw` when shader fog isn't used. The fog sheet uses each secondary camera's viewProj and the primary frame's fog texture and elapsed-time uniform.
- Primary terrain water mesh is captured by `CameraModelSnapshot` as an ordinary mesh; `drawPrepared` avoids drawing it a second time.
- GameClient offscreen path calls static streamed-world draw, prepared terrain/fog, then the existing camera model/geometry snapshot. The main screen remains unchanged.

## Limitations and correctness caveats
- Terrain shader-specific GPU textures and native GPU buffers are **borrowed**; they must survive all offscreen passes. This is a same-frame render API, not an asset serialization format.
- Terrain can use a fallback LOD when a requested detail LOD wasn't built on primary pass. Visual parity at seams/morph ranges needs GPU screenshot tests. It does not guarantee identical per-camera high-detail fidelity until a prepare-all-visible-cameras LOD residency policy is implemented.
- Direct view-dependent fog sheet uses the current camera matrix and prepared fog texture. Pixel equality between two different viewpoints is not expected; test world-space visibility consistency.
- `VisibilityField::draw` only queues read-only GPU commands; its visibility transition is still updated by the primary path only.
- Sprite renderers, GPU compute VFX/particles, post effects and some water paths remain separate specialized renderers; they are NOT fully included.
- The global custom pipeline lambda captures borrowed terrain LOD pointers; no asynchronous callback may outlive terrain resource retirement.
- IMPORTANT: animation source advanced to Stage 157 during this work. Comparing against the last integrated animation tree showed six nonconflicting changes and **one conflict in `apps/modelview/main.cpp`**. Do NOT overwrite ModelView with either side wholesale. Latest animation HEAD inspected: `ea28f227b1da3fcfb32da2abc49e60f588861786`. These changes were NOT merged as part of this stage.
- No claims of full render parity or production readiness.

## Tests actually performed
- GitHub source inspection and remote commits; new compile-only CMake test target authored.
- **NOT RUN:** CMake configuration, C++ build, CTest, Metal/Vulkan/GLES compiles, GPU screenshot parity, GameClient/Studio runtime. Test results must not be recorded as passed.
- On final consolidated run: inspect `camera_terrain_prepared_api`, `camera_model_snapshot`, `view_camera_lod`, secondary terrain LOD pixel parity, fog texture update count, water double-draw check, GPU retirement, 2D/3D visibility parity and all platform builds.

## Next
1. Resolve the latest ModelView conflict carefully, preserving the animation agent's hot-reload and LENS prepared GPU lifetime.
2. Implement camera-aware sprite and VFX draw-only submit APIs; keep simulation/update once. Avoid extracting mutable GPU effects from the primary render callback.
3. Establish prepared visual frame lifetime, prevent on-demand terrain LOD compilation/rebuild within the secondary pass, run a consolidated build and fix errors.
