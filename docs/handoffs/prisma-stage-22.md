# PRISMA Stage 22 — depthOfField experimental screen-space feature

**Date:** 2026-11-18. **Working branch:** `feature/prisma-postfx`; PR https://github.com/chromealex/AIVERSE/pull/26. **Previous handoff:** `docs/handoffs/prisma-stage-21.md`. No main merge or non-PRISMA feature-branch imports in this stage.

## Commits and paths

- `6e687ca2631916c54c875495379124fa008cf32e` — `engine/render/PrismaData.h`: new `PrismaType::DepthOfField` appended before Count, retaining existing enum IDs.
- `0465ecdbff40e0acf75979b6e1b963f0d7cb351e` — `engine/render/PrismaData.cpp`: new `depthOfField` catalog record, number parameter validation/clamping, quality minimum High, Heavy cost, `prismaEffectWork` pass estimate (10 units, 1 pass).
- `74c74cc68b6bc182652c81fcefad3f10e5c50dd3` — `engine/render/PrismaShaders.h`: screen-space one-pass depth-weighted defocus using original color and hardware depth, nine-neighbour spatial sampling.
- `7cd2a9c39ac1e5d6ca9714821ffa73062e6aa8f4` — `engine/render/Prisma.h`: new shader pipeline slot and opt-in separate scratch depth attachment.
- `07dd643489e0f3927d4accb210efa3c90d57df97` — `engine/render/Prisma.cpp`: opt-in render pass running before FXAA/Bloom, depth feedback avoided, downstream FXAA graph and direct path use selected DoF source color.
- `c97845b1e10ac4278c7510f7b58ffd223d99a225` — `docs/prisma.md`: details, limits, example.

## API and behavior

`postfx.effects`: `{"type":"depthOfField","focusDepth":0.65,"focusRange":0.08,"maxBlur":6,"intensity":0.8}`. The entry uses ordinary data-driven catalog operations; MCP names, editor IDs, serialization and backwards compatibility stay unchanged. `minQuality` defaults to `high`. GPU execution also requires environment variable `AIVERSE_PRISMA_DOF=1`, so new effects cannot change preexisting scene output by default.

`focusDepth` / `focusRange` currently operate in **hardware depth 0..1**, not metres: the PostFrame does not expose enough camera projection semantics to compute physical CoC. This is a bounded screen-space depth-distance defocus. It is not lens-accurate bokeh, occlusion-separated near/far blur or autofocus. Shader uses the existing PRISMA GLSL/MSL common-subset and kDepthOfField pipeline. A separate scratch depth texture of the same resolution is attached while sampling original scene depth, preventing feedback. Output becomes `src` before optional FXAA and Bloom. HUD stays outside the PRISMA chain. No FPS/RTS/ECS logic or SpectraFX code changed.

## Actual verification

**Performed:** source inspection of the current `PrismaData`, `PrismaShaders`, `Prisma.cpp`, `Prisma.h`; connector commits and readback of DoF/FXAA integration.

**NOT RUN by user direction:** C++ compile, CMake/CTest, Metal or GL/GLES RHI smoke, GPU depth sampling, depth conventions/reverse Z, visual comparisons, benchmarks, CI and editor interaction. A committed source is not a passing test. The new shader and public API are experimental.

## Limitations and precise next feature stage

1. Need camera near/far and inverse depth-projection contract to express focus distance in world metres; depth currently non-linear and platform dependent, so the artistic focus UI uses device depth.
2. Improve depth discontinuity/foreground bleeding with foreground/background separation and depth-aware gathers. Add dynamic autofocus and optional multi-resolution CoC & bokeh. Keep runtime opt-in until native driver validation.
3. The current RHI must support sampling a depth render target; some backends may need a dedicated sampled-depth resource or format variant. Do not force on unsupported backends.
4. Consider AutoExposure as Stage 23: exposure control independent of physical DoF, histogram/downsample and temporal adaptation; avoid CPU readback per frame, preserve HDR scene linearity. Its design must not pretend exposure feedback is implemented if only data and shader stubs exist.
5. **Every future stage:** separate code and handoff commits with exact changed paths, API, tests ACTUALLY run (NOT RUN until asked), risks and next action. Do not touch `main` or other agent branches.
