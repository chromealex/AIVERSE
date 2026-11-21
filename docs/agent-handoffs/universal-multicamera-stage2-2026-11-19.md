# AIVERSE LENS Stage 2 — shared portrait capture / RTS adapter (2026-11-19)

## Commit and ownership
- Branch `feature/universal-multicamera-render-targets`, forked from main `58e29ab052458fffcdebebffe27022b53adb9367`.
- Stage 2 implementation commit `b385ca427cf8db1175ae4438ce1767af42220d38`.
- Files: new `engine/render/PortraitRenderer.h`; changed `modules/rts/view/RtsPortrait.h` and `modules/rts/view/RtsPortrait.cpp`.
- Existing native and Studio consumers of `game::Portrait` need not change their method signatures.

## Architecture / API
- `aiverse::render::PortraitRenderer` is a genre-independent header-only offscreen 3D capture host, owning `gfx::Device*`, `Renderer3D`, color/depth `TextureHandle` and output dimension. Explicit `init(device,size)`/`shutdown()` are required before device teardown.
- Public `capture(const render::Camera&, Submit&& draw, std::array<float,4> clear)` accepts an arbitrary renderer-submission callback and renders into its own color/depth RT. Call before an active gfx render pass, and sample texture after the capture. No RTS/FPS/editor headers.
- `renderPose(const Model&,const Pose&,yaw)` previews arbitrary animated models and supports any native game.
- Public `texture()`, `depthTexture()`, `renderer()`, `device()`, `size()`, `lastError()`.
- `game::Portrait` remains in `modules/rts/view` as an adapter for RTS `RtsView::modelOf/selectionRadius/drawPreview/applyMaterial`. Procedural material-preview shapes remain in that adapter for now; common capture is no longer RTS-specific.
- Existing `game::Portrait::init/renderPose/renderMaterial/render/texture/lastError/shutdown` signatures preserved. Output clear and camera framing values preserved.
- No on-disk format or wire protocol changes; new generic API is header-only and avoids CMake source additions.

## Tests actually performed
- GitHub branch + source API inspection; code commit successfully created in branch.
- **NOT RUN**: compilation/CTest, Metal/GL/Vulkan, Scene/Game editor, modelview, portrait pixel parity. No runnable owner GPU checkout. Do not claim compile safety until actual runs.
- Existing Stage 1 `render::Camera` rename also awaits build validation.

## Risks and next step
- Confirm `engine/render/PortraitRenderer.h` compiles from both engine-only and game_view targets, especially renderer initialization and ModelDraw/Pose references.
- `PortraitRenderer` is noncopyable and requires explicit `shutdown()` prior to gfx::Device shutdown. The RTS adapter manages its own uploaded preview meshes.
- Repeated `game::Portrait::init` without its matching `shutdown` would leave adapter-owned material shapes alive: follow existing initialization lifecycle.
- Continue Stage 3 by adding a genre-independent `CameraRegistry` + scheduling/capture requests. Include generation-safe handles, manual/fixed-rate/interval/dirty scheduling and cycle-safe dependencies. Commit code and **then** handoff separately.
- Follow `docs/camera-system.md`, `AGENTS.md`, `docs/metal-batching-agent-handoff.md`, `docs/editor-unification-handoff.md`.
