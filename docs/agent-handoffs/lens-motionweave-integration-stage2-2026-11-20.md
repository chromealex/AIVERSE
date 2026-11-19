# AIVERSE LENS + MotionWeave integration stage 2 (2026-11-20)

## Working branch / PRs
- Integration branch: `feature/lens-motionweave-integration`.
- Source animation HEAD when staged: `7c04338019a80bf6d82150f7db9ad791872c6adc`.
- Initial 63-file camera staging: `d46d273e43e9e22f8e074bce5e794df513ba2a14`.
- Shared Math3D reconciliation: `1cc38700ff145114255977abed51f804f5d1b1fe`.
- RHI Device base contract: `8cb19b1c547040aecf94c4c8d523194c018aa2cf`.
- CMake camera test registration: `42a07ea7c8c00872d1a3192289bb146862b2f603`.
- Integration PR: https://github.com/chromealex/AIVERSE/pull/50 (draft). Previous animation -> LENS PR: https://github.com/chromealex/AIVERSE/pull/49 (conflicted).
- Neither PR merged. No force push, no main changes. Branch head can advance independently after this handoff.

## Exact API changes
- `engine/render/Math3D.h`: retained the existing `Camera3D` and `RTSCamera` type names as aliases to the fully genre-independent `render::Camera`, bringing direct ECS poses, roll, off-center asymmetric frustum and lens shift, oblique clipping, origin-aware world projection and ortho override. Added `mat4::frustum`. Existing orbit-style fields/methods retained.
- `engine/gfx/Gfx.h`: opt-in virtual `Device::captureTexture(TextureHandle,CaptureFn)` and `waitIdle()`, both default unsupported. Backend implementations still need integration.
- `CMakeLists.txt`: registered 14 LENS camera/ecs CPU test targets; tests NOT run. Target names from LENS branch preserved.
- Animation team's `RtsView`, `RtsViewDraw`, `RtsAnimate`, `genericView_`, `submitOne`, appearance source lifecycle remain unchanged in this staged branch.

## Not yet merged / crucial follow-ups
- Backend-specific Metal/GL/Vulkan texture readback and GPU-idle implementation from LENS not yet reconciled with animation branch backend code.
- `apps/editor/Editor.cpp`, `EditorWindow.h`, `GameClient.cpp/.h` and all related UI/editor/client files are still animation branch versions.
- `RtsView::drawFrozenModels` from LENS NOT ported, intentionally: the animation team's universal View is source of truth. Implement immutable prepared render packets after the once-per-frame MotionWeave animation update, then N per-camera submissions without consuming events again.
- Camera preview and standalone `CameraRuntimeHost` exist as staged headers but may not compile/integrate until above reconciliation.
- Important: compare new `Camera3D` alias with any explicit forward declarations, aggregate initializations and ABI-sensitive native modules when compiling.
- PR #50 may show conflicts if animation branch moves concurrently; rebase/reconcile cautiously.

## Tests actually performed
- GitHub API/source/tree inspection, Git-object commits, branch updates and draft PR creation only.
- Native `git merge` could not run: runtime cannot resolve github.com for git clone/ls-remote.
- **NO** CMake, C++, CTest, macOS Metal, Vulkan/GLES build or hardware runtime was run.
- Do not claim end-to-end integration or successful builds.

## Next step
1. Carefully transplant RHI capture/idle code into current animation backend versions using narrow patches and verify on Mac/other platforms.
2. Port Studio/Client opt-in code and resolve lifecycle.
3. Implement `MotionWeaveVisualFrame` / frozen render packets without RTS state mutation.
4. Run consolidated camera + animation CTest, editor/client builds, Metal GPU and Vulkan/GLES compile-only checks, fix failures, then consider true merge.
