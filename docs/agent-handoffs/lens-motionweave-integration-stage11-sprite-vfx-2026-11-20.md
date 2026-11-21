# AIVERSE LENS + MotionWeave Stage 11 — sprite and particle draw-only secondary cameras (2026-11-20)

## Branch, commits, files
- Working branch `feature/lens-motionweave-integration`. `main` unchanged.
- `8ca53dbcf58dfae096e207a6278bc22aa180c8c5`, `1c3949506c5875460fb2fff49e19cfd682465565`: `modules/rts/view/Rts2DView.h`, non-mutating-to-visual-clock prepared API declaration.
- `a6c3fd2bbdcef31c5216f080cdc50958a7f663d0`: `modules/rts/view/RtsSprites.cpp`, `Rts2DView::submitPrepared(Renderer3D&)`.
- `bb5d2418450b613d9d9870456109589b66eac820`, `9dd12e57a7b876c513fb149f95d9bf9337f3a9b8`: `modules/rts/view/RtsView.h/.cpp` forwards prepared sprite-only draw.
- `83419fc44e1be4aa380dd7b56bdadf581299be75`: `engine/render/CameraRenderPipeline.h` adds optional after-scene callback after `renderer_.render` and before `resolvePostFx`.
- `3a8bbc3a78f404fbb5870fdde7434e76cc9dee28`, `e6a84459f87d2c2e577e3f914f8bef1835a50bbd`: `engine/render/CameraRuntimeHost.h` and `CameraClientAdapter.h` forward optional callback via defaulted template parameter preserving previous call sites.
- `6038d3a9fa7e2d5970171bfe17512eb2d395e0d7`, `54bc438a42bb170b524eae03f81f497d70c37e6c`: `apps/client/ClientFx.h/.cpp` `renderPrepared(Camera3D,viewProj,width,height,Lighting)`.
- `3b5e48a86cc7c50f37c464eb54d45e658dbc5cc2`, `00c65a909ef6689f1bf7b3db784967da5b90a808`: `apps/client/GameClient.cpp` queues sprites and VFX in the appropriate LENS camera stages.
- `00d9a1145dc2d9dca34846f8ebe1cddf01423d73`, `0be449584e1f9607a44263449db9c19c096bacf6`: compile-only `tests/render/camera_sprite_vfx_prepared_api.cpp` and CMake `camera_sprite_vfx_prepared_api`.
- Previous Stage 10: `docs/agent-handoffs/lens-motionweave-integration-stage10-terrain-fog-2026-11-20.md`.

## Sprite architecture
- The original primary camera calls `Rts2DView::beginFrame/addObject/endFrame/submit` ONCE. They update sprite animation frames, corpse lifetime, tile contents and world-facing sprite geometry for that primary viewpoint.
- Secondary cameras call `Rts2DView::submitPrepared(Renderer3D&)`, scheduling the **already prepared** shadow caster, static tile mesh and dynamic sprite `SpriteBatch` through the standard `Renderer3D::drawCustomShadow/drawCustomOpaque/drawCustom` paths.
- Secondary sprite submit never calls `beginFrame`, `addObject`, `endFrame`, `refreshTiles`, sprite time advance or corpse transitions. It recomputes each target camera's viewProj and lighting bindings but `SpriteBatch::flush` updates transient vertex GPU buffers to submit the batch.
- Important: the existing sprite vertices / world billboards were authored for the primary camera. Other cameras currently get their own projection but DO NOT reorient upright/billboard sprite quads. Full parity for rotated/different camera angles requires a per-camera immutable sprite authored-object snapshot and per-camera vertex preparation, not a second sprite simulation.

## VFX architecture
- Particle lifecycle and FX events are still driven only from the main GameClient `fx_.update()` (which calls bridge update and `ParticleSystem::update(dt)` exactly once) and the original `fx_.render()`.
- LENS pipeline callback `AfterScene(view,renderer,width,height)` is optionally invoked between the per-camera `Renderer3D::render(pass,post)` and `resolvePostFx`, while the pass is still open, like GameClient's original FX draw stage.
- `ClientFx::renderPrepared` only calls `ParticleSystem::setView`, `setLightDir` and `render`. Each camera receives its own culling, depth sort and billboard axes without ticking emitter/particle clocks, spawning subemitters or consuming gameplay events. GPU buffers and renderer diagnostics may be updated during each render pass.
- Existing CameraRenderPipeline and Host APIs remain source-compatible with defaulted callback argument.

## Limits / blockers
- Lighting for particles remains based on the per-camera `renderer.lighting`, but collectLights has NOT been duplicated; point light list is only prepared once on the primary pass. Secondary Renderer3D may require copying the already prepared light list or using a camera-neutral frame light snapshot.
- SpriteBatch's dynamic GPU buffers are rewritten once per camera, even though the visual sprite scene is frozen; optimize using precomputed per-view GPU buffers if performance demands it.
- Screen-space FX, temporal history, post-FX quality parity and trails on multiple cameras require GPU validation.
- Terrain/fog from Stage 10, water/selection from Stage 9, generic View and MotionWeave animation from prior stages remain in place.
- The source animation branch Stage 157 (`ea28f227b1da3fcfb32da2abc49e60f588861786` at last comparison) has one unresolved overlapping file `apps/modelview/main.cpp` against the last integrated Stage 156. This newer animation head was NOT merged. Preserve both sides when reconciling.
- Source level implementations are UNCOMPILED; no implied functional parity or GPU correctness.

## Tests actually run
- GitHub tree/source inspection, targeted edits and commits ONLY.
- `camera_sprite_vfx_prepared_api` compile-time signature assertions authored and CMake target registered, NOT executed.
- **NOT RUN:** CMake, C++, CTest, Metal GPU, Vulkan/GLES compilation, editor/GameClient runtime, screenshots, particle event-count/parity profiling.
- Acceptance for the consolidated pass: sprite animation state unchanged after N camera draws; particle ages/FX callbacks exactly once, proper overlay pass and render target, lighting parity, sprite billboard orientation caveat, no device resource retirement races, end-to-end multi-camera pixel tests.

## Next steps
1. Finish immutable camera-independent sprite object packet preparation and per-camera billboard rebuild (without altering sprite animation times).
2. Preserve prepared particle light lists in a frame snapshot shared by all cameras.
3. Resolve pending ModelView source conflict with actual three-way per-hunk reconciliation.
4. Complete consolidated builds and CTest, then Metal/Vulkan/GLES compile and GPU validation before merging to `main`.
