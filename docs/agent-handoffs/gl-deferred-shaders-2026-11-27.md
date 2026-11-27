# GL deferred shader builds — handoff (2026-11-27)

Branch `feature/aiv1126_gl_lazy_shaders`, code commit `019c6f2ed` (from `main` 53330a9f8).
Area: GL backend (`engine/gfx/gl`), RHI (`engine/gfx/Gfx.h`), client renderer init.

## Problem

A game started on the web build froze for several seconds at a battle start; on a Mali-G52 phone about 6 s of
it was synchronous WebGL program linking. Two causes:

1. `GfxGL::createShader` linked and queried `GL_LINK_STATUS` at once; WebGL waits for the link there.
2. A pipeline drawn in an HDR pass is bound through its linear variant (`hdrPipeline`, built on first bind), so
   the base program of every lit pipeline was compiled and linked and never drawn.

## What changed

- `gfx::Device::setDeferredShaderBuild(bool)`: opt-in, default no-op. GL: `createShader` keeps the sources and
  returns a handle; `programFor` compiles + links at the first `setPipeline` (or warmup). A failing program sets
  `lastError()`, logs `[gfx] shader '<label>': ...` once and its draws are skipped (`pipelineId_ = 0`).
- `GLShader` has a build state (`Source`, `Linking`, `Ready`, `Failed`), its prelude (the GLSL prelude in force at
  creation; the HDR variant reuses its base's prelude) and label. `GLPipeline` no longer stores a program.
- GL `warmPipelineColorStatus` / `warmPipelineColor`: build the variant a draw into the format will bind. In
  deferred mode with `KHR_parallel_shader_compile` (`GL_COMPLETION_STATUS_KHR`, added to `GLApi.h`) the first
  call starts the build and returns `Pending` until the driver finishes. Same contract as Metal.
- `apps/client/GameClient.cpp`: deferred builds on `Backend::GLES3`, `AIVERSE_EAGER_SHADERS=1` turns them off.
  Editor and other hosts keep eager builds.

API: additive virtual on `gfx::Device`. No save / data format change.

## Verification (actually run)

- `aiversebuild --target web` of a game project against this change (emsdk 3.1.50): builds.
- Desktop Chrome, WebGL2, 400x780 touch emulation, same session (browser program cache warm), counting
  `createProgram` / `linkProgram`:
  - deferred: 4 programs before the main menu, 30 at the battle start, `getProgramParameter` total 126 ms;
  - eager (`AIVERSE_EAGER_SHADERS=1`): 32 before the menu, 39 at the battle start, 201 ms.
  Frames look the same.
- NOT RUN: native tests / `./run-tests.sh --quick` (KINGSTON not mounted on this Mac), Android APK on GL,
  desktop GL 3.3, Vulkan / Metal untouched. The `Pending` path is only reached when a renderer warms
  pipelines (`Renderer3D::warmModelPipelinesChecked`); that path was not exercised on a phone.

## Next

- Phone check (Mali-G52) of the battle start time with and without the change.
- Games can move the remaining links off the first battle frame with the loading warmup
  (`loading.warmTypes`, client `initialAssetRadius_`), which now polls instead of blocking.
- 3.1.0 notes: "GL / WebGL: shader programs are built on first use; fewer and non-blocking links at a level start".
