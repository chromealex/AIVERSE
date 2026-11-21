# Lighting Stage 13 — native compilation and focused acceptance

Date: 2026-11-20. Branch: `integration/lighting-main-stage1-20261120`.
Base: `730bc3b60`; implementation: `bd9fcc045a47c87446ad74e6891e9224fab19f4f`.
Worktree: `/Users/aleksandrfeer/Projects/AIVERSE/.worktrees/lighting-completion`.

## Change

Actual AppleClang compilation exposed the missing closing brace after
`MetalDevice::completedFrameSerial()`. Cherry-picked the existing one-line
fix `813d49fd12c0005b09682088de3cb7ad12180adc`, changing only
`engine/gfx/metal/GfxMetal.mm`. No GI algorithms, GPU ABI, persistence,
experimental defaults or main/Studio branch files changed.

## Executed verification

- Configured Debug CMake in `/private/tmp/aiverse-lighting-completion`, with
  tests and HDR fixtures ON; editor/tools/client/server/headless OFF.
  Configure PASS. An initial build was attempted before configuration finished
  and failed with `could not load cache`; rerun after completion succeeded.
- Built all 14 `aiverse_lighting_*_test` targets: PASS. Initial native renderer
  build FAIL at the missing Metal brace; log `/private/tmp/aiverse-lighting-render-build.log`.
- `cmake --build /private/tmp/aiverse-lighting-completion --target aiverse_hdr_scene -j 6`:
  PASS after the fix, including real Metal Objective-C++ and Renderer3D sources;
  log `/private/tmp/aiverse-lighting-render-rebuild.log`, no compiler warnings/errors.
- `./run-tests.sh --no-build --build-dir /private/tmp/aiverse-lighting-completion --filter '^lighting_'`:
  PASS 14/14 on implementation SHA above. Report:
  `/private/tmp/aiverse-lighting-completion/test-reports/20261120-140500-27479/summary.txt`.
- Temporary extension of `check_model_lod_shaders.py` enabling DYNAMIC_GI:
  32 desktop GLSL/GLES model variants linked with glslangValidator, PASS.
- Temporary terrain source generator: 12 GLSL/GLES tier/detail variants linked,
  including compact GLES tier0, PASS. First generator omitted the production
  `TERRAIN_GL_NORMAL_ATLAS=0` define and failed; corrected generator passed.
  Scripts `/private/tmp/check_lighting_gi_shaders.py` and
  `/private/tmp/check_lighting_terrain_shaders.py`. These are compiler checks,
  not evidence of hardware sampler limits or correct pixels.

## Remaining work

Real Metal shader/GPU output, Vulkan/GLES device checks, Release/full-suite CI,
performance and full lighting feature integration remain NOT RUN/unfinished.
No push or CI dispatch; CI URL/status N/A. The CPU bridge and receiver tests
passing does not implement the pending Luma direct GPU / hybrid GI compute port.
Next: run the native GPU fixture, then add direct dynamic-GI pixel coverage and
fix actual shader/device errors before claiming visual acceptance. Keep main
unchanged and record every next implementation/handoff separately.
