# PRISMA Stage 30 — animated and extensible reactive masks, LOD coverage, velocity dilation

**Date:** 2026-11-19.
**Working branch:** `feature/prisma-postfx`.
**Draft PR:** https://github.com/chromealex/AIVERSE/pull/26
**Previous stage:** `docs/handoffs/prisma-stage-29.md`.
**Last source/docs commit before this handoff:** `43bc3fead4c8e6a12ebf003edb26c489aaec2bf7`.
**Main:** unchanged. No merge of other agents' feature branches occurred.

## Changes and exact source commits

1. `098d9a0f7bb73545295b29ca0fdebb4c9f435124` — `engine/render/Renderer3D.h`: introduced the optional per-scene `Renderer3D::addMotionReactiveDraw(MotionReactiveDraw)` callback for any water/particle/custom renderer; added animated reactive pipeline ownership and CPU diagnostics for animated, callback and LOD-reactive draws.
2. `70486c22c086802d0f59857048dcf1f51f3269e3` — `engine/render/Renderer3DReactiveShaders.h`: introduced generic reactive fragment source for existing dual-pose skinned/baked GPU vertex shaders and a helper to substitute the Metal fragment without rewriting deformation math.
3. `979e56500ca4fb5a05b2b2515810137427239ac1` — `engine/render/Renderer3D.cpp`: clear per-scene reactive callbacks at `beginScene` and after `resolvePrisma`, handle postprocessing-inactive frames, and release new animated GPU pipelines on shutdown.
4. `eac8dff3a405bda9a79baacedae71542cf6aa98c` — `engine/render/Renderer3DReactiveShaders.h`: the rigid reactive fallback now mirrors the 4×4 pixel-dither coverage rule of the normal ModelShaders LODFADE path on GL and Metal.
5. `92ed43a7ede78107dbcafaccdc3b39e3c43b3a4e` — `engine/render/Renderer3D.cpp`: extend reactive eligibility to animated translucent ModelItems and rigid LOD-transition geometry; allocate a mask if either native geometry or callbacks need it; use Screen blend for overlapping builtin coverage.
6. `a750e5991e1c6c8268c09ef5e4a3d75817e9a35e` — `engine/render/Renderer3D.cpp`: compile additional reactive skinned and baked shaders (same deformed vertex stage as motion, reactive-only fragment); implement LOD coverage uniforms, with a pass that can run for animated or callback-only scenes.
7. `1e3e0cccc7234fb5e407e60fdcf67f40e702b44c` — `engine/render/Renderer3D.cpp`: render current-pose skinned, morph, baked and mixed baked+morph translucent silhouettes without needing previous animation history; invoke optional external callbacks inside the same open reactive pass and record diagnostics.
8. `9da82a7306077a0cf953f8ca686ac681552348eb` — `engine/render/PrismaData.cpp`: new MotionBlur `velocityDilation` (default 1, range 0..2 pixels); updated fill-rate estimate.
9. `f9abf09c10a3f4e7d1b4664077707470c921bbf5` — `engine/render/PrismaShaders.h`: optional 4-neighbor depth-aware velocity dilation, selecting stronger movement only where depth is close to the source pixel.
10. `1a55ddc4e7ca076157b8b4ed95434e0e6cabae7e` — `engine/render/Prisma.cpp`: feed velocityDilation to motion blur shader via SimpleU.sun.z, preserving other parameters.
11. `24df1f4bb4548dd0cc1ee39045a821a9d3aa9827` — `engine/render/Renderer3D.h`: explicit utility header include for std::move in callback registration.
12. `703173073ea4e9165bcb4c453d0faf1ebaa308bc` — `engine/render/Renderer3DReactiveShaders.h`: clarify LOD dither shader parameter contract.
13. `978e7df4704d84e19571ca9f47e6d769af5e1605` — `engine/render/Renderer3D.cpp`: decouple reactive pass submission from rigid-velocity shader availability; animated/external reactive callbacks still run even if the rigid velocity pipeline fails.
14. `43bc3fead4c8e6a12ebf003edb26c489aaec2bf7` — `docs/prisma.md`: Stage 30 behavior, runtime constraints, public integration guidance, unrun test status.

## Public and native integration

The existing `postfx.effects` ID `motionBlur` and opt-in `AIVERSE_PRISMA_MOTION_BLUR=1` remain unchanged; a new optional `velocityDilation` parameter enables 4-neighbor depth-aware velocity expansion. Existing `depthThreshold`, `reactiveStrength` and other blur settings continue working.

The new `Renderer3D::MotionReactiveDraw` signature is:
```cpp
std::function<void(gfx::Device&, gfx::TextureHandle sceneDepth,
                   int width, int height)>
```
Register with `r3d.addMotionReactiveDraw(callback)` after `beginScene()` and before `resolvePrisma()`. The callback runs with the per-camera RGBA8 reactive mask pass open, **after** built-in reactive draws but **before** Prisma's HDR Motion Blur. It must only submit its own draw calls; never open/close a pass or write the original sceneDepth target. Write mask coverage into the red channel [0,1] and prefer gfx::BlendMode::Screen for Screen-union coverage, so water/particle providers do not erase other registered contributors. The callback owns shader/buffer resources and must keep them alive through the resolve; this API does not automatically wrap specific Terra/water/SpectraFX systems.

The built-in reactive path now uses the accurate *current* animated GPU vertex deformation for visible translucent skinned, morph and baked ModelItems, and mixed baked+morph, instead of a rigid geometric proxy or an invalid prior animation pose. It emits current-frame coverage, **not** object motion vectors. Camera reprojection remains the fallback for animated translucent pixels, with reactive-strength blur reduction in Prisma. It does not change ECS/genre code.

The rigid LOD transition fallback reproduces the 4×4 fragment rank/dither rule of ModelShaders in its reactive shader. These fade pixels no longer receive fake rigid object velocity; they receive blur-suppression coverage instead. True velocity reconstruction for multiple transitioning LOD meshes is still separate future work.

## What was actually verified

**Performed:** fetched and inspected current source from GitHub connector, committed implementation and documentation to `feature/prisma-postfx`, read back all main modified files, and checked that new API/shader/runtime symbol references exist and match. This is static source inspection only.

**NOT RUN:** CMake configuration, native C++ compile, CTest, GL/GLES/Metal shader compilation, GPU visuals, screenshot/pixel comparisons, CI, memory/performance benchmarks and editor smoke. Tests intentionally deferred by user. Do not call any new shader compiled or parity-verified.

## Known limitations and safety

- The generic reactive extension does **not** automatically make existing water/VFX packages emit masks. Each owning renderer must register a callback and render truthful material coverage, with its own shaders and correct state. Callbacks run only when the experimental MotionBlur pipeline is active and a valid previous-camera frame exists.
- Built-in animated translucent coverage is a conservative *silhouette*: it does not replay exact per-pixel blending, material ShaderGraph displacement, vertex cutouts, water foam or particles. Some reactive pixels will suppress blur more than strictly necessary.
- The scene-depth sample and LOD dithering use backend-dependent pixel origin and non-linear hardware depth epsilon. Stage 30 does not implement exact linear-depth motion layering or disocclusion reconstruction; motion blur still gathers current-frame color rather than accumulating previous-frame history.
- Velocity dilation costs up to four extra scene-depth and velocity reads per pixel and is not a temporal super-resolution feature.
- Motion velocity/render target allocations remain per-view with existing RHI lifetime limitations. No portable GPU-completed fence has been wired and no automatic safe retirement on GPU-in-flight resize; `releaseObjectMotionViewAfterGpuIdle` still requires explicit true GPU idle.
- Generic callback cannot force MotionBlur on when absent/disabled. The current Gfx interface exposes no portable runtime texture dimension/format introspection, so user-provided mask resource compatibility relies on the native provider.
- `Renderer3D::beginScene()` clears callbacks; registration before beginScene is lost. Prefer callbacks after color scene draw and before postprocess resolve, without any gameplay mutation.
- No non-PRISMA branches or water/SpectraFX renderer files were modified.

## Next Stage 31 — features first, tests later

1. Introduce producer-owned reactive coverage adapters for the actual Water/Terra and SpectraFX rendering paths where possible, through the documented generic callback, without cross-module ownership conflicts. Handle multiple water/particle contributors with Screen blending.
2. Add a generic reactive pixel-format/size metadata contract to avoid binding mismatched or stale native textures; preserve backwards-compatible unvalidated bridge for existing owners until migration.
3. Implement linearized view-depth motion blur, occlusion-aware foreground/background velocity selection and disocclusion reactive mask dilation; account for orthographic/perspective and GL/Metal depth conventions.
4. Add a portable GPU-completion timeline/fence contract and safe per-camera resource retirement without substituting CPU frame serial.
5. Only when user permits tests: run native build, GPU GLSL/MSL compilation, LOD dithering/morph/transparent/particle/water screenshots and CTest; record real PASS/FAIL and fix discovered issues. Keep experimental flags off globally until then.
6. Create a separate `docs/handoffs/prisma-stage-31.md` with exact SHA/branch, source/API changed, actual tests performed, constraints and next step. Never merge PR #26 into main without explicit instruction.
