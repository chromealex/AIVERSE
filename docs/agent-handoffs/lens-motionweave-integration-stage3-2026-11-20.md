# AIVERSE LENS + MotionWeave integration stage 3 (2026-11-20)

## Branch and commits
- Branch: `feature/lens-motionweave-integration`; MotionWeave was the base, `main` unchanged.
- Metal offscreen RT capture and idle: `c627f676ed8d82ce827f559c4772f390c93ea595`.
- GL/GLES readback and idle: `4e3c9966e7f03a544fa9dfa2a7069e97faac2c5e`.
- Vulkan frame-slot capture declarations: `bcf18c0e236c2cfacb06511565747942f967357a`; Vulkan implementation: `d486a28309044454ec001fa4e864e990a2e41f73`.
- Editor idle window hook: `917c9a6f7d30b48120c5489c3a9a9dab1cfc95db` and `ef49c1692c0dd99a80cbd169710c826dd6cdfc1b`.
- GameClient opt-in facade: `96f035b91ca6c5cd582f19645d429703721ae871`; GameClient load/render/retirement: `32d09a29377b4f14ff249229c6cc1e88d015d236`.
- Previous handoff: `docs/agent-handoffs/lens-motionweave-integration-stage2-2026-11-20.md`.
- Integration PR (draft): https://github.com/chromealex/AIVERSE/pull/50

## Implemented and preserved
- Adapted the previously written Metal/GL/Vulkan `gfx::Device::captureTexture` code into the newer MotionWeave GPU backends without replacing entire files. Metal callback delivery remains owner-thread at later beginFrame; GL/GLES uses synchronous debug/export readPixels; Vulkan owns staging buffers per frame slot and only delivers on completed fences.
- Added optional `waitIdle()` for the backends, separate from MotionWeave's existing `waitForResourceIdle` and resource-retirement API. MotionWeave's existing lifetime API remains in place.
- Reused MotionWeave Editor's existing `waitForResourceIdle()` before `EditorWindow::shutdownAfterGpuIdle()`, allowing LENS camera previews to safely retire at Editor shutdown.
- GameClient now optionally reads `game.json` `lens.enabled`, packaged `cameras.json`, refuses screen outputs and renders additional offscreen camera targets after the normal GameClient screen/UI pass.
- Client native API exposes frame-scoped `lensColorTarget`, `lensDepthTarget`, `lensTargetInfo`, and `bindLensColorTarget`.
- **Do not use RtsView::drawFrozenModels** from the old LENS branch: this newer branch leaves the MotionWeave RtsView untouched and intentionally renders available static streamed-world geometry only until pure visual snapshots exist.

## Important remaining work
1. The additional client RTs do **not** yet contain all animated RTS entities; additional camera rendering calls must read an immutable, material/pointer-lifetime-safe MotionWeave VisualFrame after exactly one animation update.
2. No native/editor UI slot has yet been bound by material name or stable asset key; API is present for game consumers but automatic material/UI routing is not.
3. All combined code is **uncompiled**. Audit Vulkan async capture staging and completion + legacy `captureNextFrame` interaction; verify actual Metal ObjC++ and GL/GLES compilation.
4. Need inspect `GameClient` / `CameraRuntimeHost` shutdown semantics while hosted in Studio; current client calls blocking `waitForResourceIdle` only if cameras are active.
5. Animation source branch advanced while staging. Refresh its HEAD before a genuine merge or rebase, and reconcile any additional changes.

## Tests actually run
- GitHub file inspection, targeted edits, SHA-bearing commits only.
- **NOT RUN** CMake, CTest, Metal GPU, Vulkan/GLES builds, native editor/client runtime. No test success is claimed.

## Immediate next step
- Adapt MotionWeave's generic `View` to export a read-only per-frame draw snapshot and pass that to N-camera `CameraVisualFrame` submissions; add tests proving a single animation advance and no repeated events; then perform the consolidated validation pass.
