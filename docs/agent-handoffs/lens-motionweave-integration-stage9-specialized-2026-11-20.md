# AIVERSE LENS + MotionWeave Stage 9 — specialized water and selection camera packets (2026-11-20)

## Branch and provenance
- Branch `feature/lens-motionweave-integration`, no main merge.
- `b0f1ed0ecb3ac20bb5f9295f5bfbb45b2795bbfd`: `engine/render/Renderer3D.h`, specialized `CameraGeometryCommand::{Water,Selection}`, ripple/wake/wind/hydraulics/rapidColor/index range and selection outline fields.
- `cbfa1303706f35b204a1934fc06db57ba90d4ce1`: `engine/render/Renderer3D.cpp`, emit complete packet from `drawNereidRippleMesh` and `drawSelectionMesh` rather than ordinary mesh fallback.
- `3ebb2d9b20c6e0e53e271c95f44148665b9508e9`: `engine/render/CameraModelSnapshot.h` replays water via `drawNereidRippleMesh` and selection via `drawSelectionMesh`.
- `82e74be407c82cbd33ec35e11565b942eba3e23e`: `tests/render/camera_model_snapshot.cpp` verifies packet metadata survives value-based capture.
- Prior handoff: `docs/agent-handoffs/lens-motionweave-integration-stage8-geometry-2026-11-20.md`.

## Architecture
- Renderer3D emits geometry tap for a NEREID surface *after* its mesh variant has been authored, preserving all 16 ripples, 16 wake shapes, wind, hydraulics, rapid color, first/index counts and refraction value; the original generic Mesh tap excludes `kDrawWaterSurface`, so it is not duplicated.
- Renderer3D's selection drawing suppresses its generic Mesh packet while it creates the selected mesh, then sends a specialized packet preserving `selection` so subsequent cameras replay their depth-tested selection geometry.
- The snapshot owns all numerical packet data and borrows `Mesh*` from the lifetime-controlled renderer owner. As before, it must be consumed synchronously within a valid GPU/resource epoch.
- The main camera remains unchanged; all extra cameras reuse prepared geometry without executing simulation, water dynamics or animation graph updates.

## Limits and tests
- This stage does **NOT** yet capture terrain RHI command lists, terrain shader uniforms, 2D sprite batches, fog GPU material state, particles or VFX GPU compute submissions. Water paths that do not invoke `drawNereidRippleMesh` remain outside capture.
- Camera-relative billboards and screen-space overlays still need per-view generation, not blind replay.
- Per-frame captured mesh pointers require host GPU lifetime protection; specialized selection outline rendering may require its editor-specific enable flag in each target.
- Test `camera_model_snapshot` was edited to assert NEREID/outline packet data; **NOT RUN**: compiler, CMake, CTest, Metal, Vulkan, GLES, editor/GameClient GPU parity. No functionality is claimed hardware-verified.
- Source file capture guard still resets both callback taps before secondary camera submission. No pass recursion or duplicated animation event dispatch intended.

## Next
1. Introduce a safe immutable terrain/fog render-state provider with per-camera submit; separate state preparation (terrain mesh/fog texture update) from device drawing. Respect pass dependencies.
2. Handle Sprite/particle/VFX per-camera view coefficients and GPU state without rerunning time simulation.
3. Consolidated build and test: CameraModelSnapshot CPU, MotionWeave single-update/N camera regression, GPU shader builds and pixel parity on Metal, Vulkan, GLES, identify/fix compile errors.
