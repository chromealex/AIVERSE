# AIVERSE LENS — architecture proposal checkpoint (2026-11-19)

## Stage type / status
Documentation-only design checkpoint, **not a code implementation stage**.
Branch `feature/universal-multicamera-render-targets` was created from `main` at `58e29ab052458fffcdebebffe27022b53adb9367`.
Previous source migration handoff: `docs/agent-handoffs/universal-multicamera-stage1-2026-11-19.md`.
Design implementation commit: `217534736a55dc5565e04b7b0f999621f5233294`.
No changes to `main`.

## What changed
- Added `docs/camera-system.md` with a P0/P1/P2 roadmap for universal multi-camera capture.
- Designs `CameraRegistry`, ECS components, `CameraDirector`, rigs, `CameraRenderScheduler`, `RenderView`, `RenderTargetPool`, generic `PortraitRenderer` and editor camera UI.
- Explicitly covers GPU-safe target lifetime, render dependency ordering, avoiding RT read/write hazards, independent temporal effects per camera, per-camera update policies and cross-genre isolation.
- Optional external libraries: OpenXR (XR), TinyEXR (HDR export), FFmpeg only as external export pipeline; none needed for P0.

## API and compatibility
- **No API, ABI, CMake, save schema, runtime or GPU code changed in this design checkpoint**.
- P0 render::Camera source rename from stage 1 is still NOT build-validated. `modules/rts/view/RtsPortrait` has NOT been extracted yet.
- `docs/camera-system.md` describes proposed APIs, not existing APIs. Do not present them as implemented.

## Tests actually performed
- Verified branch head before write with GitHub connector and created the architecture document successfully.
- **NOT RUN**: build, CTest, GPU captures (Metal/GL/Vulkan), editor runtime smoke, model preview parity. No local source checkout or GPU available in connector execution.

## Risks / next step for another agent
1. Read `AGENTS.md`, `docs/metal-batching-agent-handoff.md`, `docs/editor-unification-handoff.md`, Stage 1 handoff, and `docs/camera-system.md`.
2. Complete the next bounded **implementation** stage: split `modules/rts/view/RtsPortrait` into genre-free `engine/render/PortraitRenderer` (scene+texture+model pose) and a thin `game::Portrait` adapter for `RtsView`/material settings. Update CMake linkage, editor/animation/modelview callers as appropriate. Preserve existing public `game::Portrait` adapters while de-RTS-ifying common code.
3. Commit implementation, then separately commit a detailed stage handoff before another stage.
4. Continue camera registry and multi-target renderer using `gfx::PassDesc`, with tests in one later consolidated hardware pass if requested by the owner.
5. Ensure a full `git grep RTSCamera` sweep and real compiler check; Github code search may have omitted hidden usages, code generation and scripts. Never claim tests passed until executed.
