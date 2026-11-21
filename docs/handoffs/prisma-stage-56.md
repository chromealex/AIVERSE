# PRISMA Stage 56 — metric focus distance, depth-edge DoF and fallback diagnostics

Date: 2026-11-19
Repository: chromealex/AIVERSE
Branch: feature/prisma-postfx
Draft PR: https://github.com/chromealex/AIVERSE/pull/26
PR base: feature/universal-entity-inspector-fps
Prior handoff: docs/handoffs/prisma-stage-55.md
Pre-handoff current branch SHA: 84aca5da92254f68b0acb2347f9d2f93ad21655b
Main: UNCHANGED. Do not merge until explicit user authorization.
User sequencing: finish all functional features first, then a separate native build/CTest/Metal-GL-GLES-Vulkan/WebGL visual and profiling phase.

## Scope and feature outcome

Stage 56 adds an opt-in camera-linear focus plane and anti-silhouette color sampling to the existing PRISMA `depthOfField` effect. The old JSON `postfx` effect defaults to the previous hardware-depth mode, with the previous shader formula and 8-neighbor sampling retained. This work is pure presentation, independent of ECS or genre, and does not duplicate SpectraFX.

New fields on the existing `depthOfField` catalog object:

- `metricFocus`: bool, default false. If false, use legacy hardware depth [0,1].
- `focusDistance`: 12 by default, range 0.05..2000; location of view-plane focus along the camera view direction in WORLD UNITS (metres only if the game adopts 1 unit = 1 metre).
- `focusRangeMeters`: 4 by default, range 0.01..500; sharp view-space interval.
- `occlusionToleranceMeters`: 0.35 by default, range 0.001..20; reject sample colors across foreground/background depth discontinuities.

Existing `focusDepth`, `focusRange`, `maxBlur`, `intensity`, default `minQuality=High`, existing `AIVERSE_PRISMA_DOF=1` opt-in, legacy JSON, editor saved panel ID and MCP operations remain compatible. Tier may still be authored below High by project override; legacy DoF was not HDR-only and Stage 56 does not newly impose that constraint.

## Actual changes and exact commits

1. `5c49de6f84e2ee155d96fe1bef06250bb1eedee1` — `engine/render/PrismaData.cpp`: append opt-in metric parameters and descriptions to the existing catalog, preserving prior flattened parameter offsets and defaults.
2. `c1e3902d4c758693675a1b7adf434cb3807137e0` — `engine/render/PrismaShaders.h`: add camera-linear DoF helper and opt-in metric shader branch. Legacy branch retains original focus/CoC, weighted 8-neighbor RGB and opaque-alpha output. Metric path reconstructs view-plane distance, rejects sky taps and discontinuous depth silhouettes, uses soft tolerance and preserves original source alpha.
3. `baa8a6edf74a43c73f2b6f2344d39dbb66352575` — `engine/render/Prisma.cpp`: dedicated DoF uniform payload/GLSL+MSL fields (`px,p0,depthInfo,focusInfo`), reuse PostFrame near/far/orthographic, validate finite depth range (perspective additionally near>0), select legacy shader branch when metric metadata is invalid.
4. `564c9f6254e1b81c46f209c07756a8aaf0e73937` — `engine/render/Prisma.h`: add `EffectExecution::degraded` to distinguish encoded fallback from a fully executed authored effect.
5. `5f9b66498100bf309da6adce4d232ec686ac8970` — `Prisma.cpp`: track actual metric/legacy DoF pass output and only mark degraded+reason when an authored metric request encoded the device-depth fallback because camera metadata was rejected.
6. `6fb240ff19cc82b7438187de797878531a1d3c3f` — `Prisma.cpp`: validate current `src.tex` for the DoF pass, not merely original scene.tex, because the input may be an earlier Motion Blur output.
7. `af8076ec788979f208300aef63feffe960ad7f7d` — `apps/editor/windows/PrismaWindow.cpp`: explain hardware/metric controls in the shared effect editor and show degraded fallback in the per-view diagnostic matrix.
8. `735a94688e2b85a0aac9c503436d4c204c485ecf` — `Prisma.h`: clarify that an encoded-but-degraded effect may still have a non-empty reason.
9. `6a5d270f1fd955e3fbc3b79709682da8b0bed16d` — `docs/prisma.md`: complete Stage 56 architecture, parameter defaults, near/far and genre independence, real/unrun test status, risks.
10. `ae934bf4b597a1f8f9d6c4f2df996364ae5aa6fe` — `PrismaData.cpp`: adjust GPU-free heuristic `prismaEffectWork` from 10 to 18 full-screen texture-read units for metric DoF due to eight additional tap depth reads. This is NOT GPU time.
11. `9e1cda36b6b91ec9baf111bddfcb0f13cf6dcad6` — `Prisma.cpp`: Stage 53 per-effect diagnostic now reflects actual legacy SDR DoF runtime gate (authored quality + env opt-in), not a falsely assumed HDR-only requirement.
12. `b547b04bfd2ec7d529b0e144fb740de8aa6f6ef2` — `PrismaWindow.cpp`: clarify that High is the default minQuality, not a hard requirement on an explicitly overridden profile.
13. `84aca5da92254f68b0acb2347f9d2f93ad21655b` — `skills/visuals/aiverse-prisma/SKILL.md`: per-agent contract for metric focus, deterministic legacy fallback, depth attachment ownership, editor status and tests-first caveats.

This handoff is deliberately a separate commit AFTER the bounded functional source changes and skill/design documentation.

## Architecture/API details

The full-screen shader continues to sample SceneColor from TEX0 and the actual scene's non-reversed device depth from TEX1. It never binds the scene-depth texture as a depth WRITE target. An independent `scratchDepth` is attached to the DoF destination. The changed GPU uniform uses the same `px,p0` legacy prefix but appends `depthInfo(near,far,ortho)` and `focusInfo(focusDistance,focusRangeMeters,occlusionToleranceMeters,mode)`. The other postprocess shaders keep their current UBO layouts.

`Renderer3D::resolvePrisma` already passes camera projection through `PostFrame`. Current orthographic renderer values are near=-200, far=1000. Perspective depth metric uses `near*far/(far-d*(far-near))`, with near>0 and far>near; orthographic uses `near+(far-near)*d`, permitting signed near. The CPU selects metric mode only when near/far are finite and the relevant projection bounds are valid, otherwise the shader executes original device-depth CoC. This intentionally represents view-space focus PLANE distance, not radial distance to objects.

In metric mode a soft confidence function uses the absolute difference of neighboring linear depths with `smoothstep(tolerance,2*tolerance,delta)`. Sky taps and sky center are excluded; RGB samples from unrelated depth layers are rejected to limit halos. The shader uses a bounded 8-neighbor gather with 2x center weight, unchanged on-screen tap topology, and preserves original SceneColor alpha. In legacy mode the original color/alpha calculation is unchanged. The overall final PRISMA composite still normally emits opaque alpha; Stage 56 makes no claim that final presentation alpha has changed.

The `PrismaEffect` generic catalog, native profile, editor, volume parameter overrides and MCP parser accept the appended fields without format migration. `Prisma::EffectExecution::degraded` remains per stable camera `viewId` and reports a representative fallback reason only if the affected pass actually encoded. `pipelinePrepared` does NOT prove correct GPU execution, and `encoded` is NOT a GPU completion event.

## Actual verification and unrun paths

Performed:
- Verified working-branch HEAD and prior Stage 55 handoff through the connected GitHub repository.
- Inspected current DoF fragment shader, program builder, render path, PostFrame projection metadata, editor generic field authoring, Prisma catalog/parser and GPU cost estimator before editing.
- Read back all changed source/design/skill files from GitHub after the functional changes. **35/35 static source-presence, branch, call-order and ownership checks passed**, including legacy defaults/branch, camera metadata, perspective and ortho depth reconstruction, sky/edge rejection, alpha behavior, src guard, per-view degraded reporting, editor opt-in and cost heuristic.
- These were source inspections and logical checks only, NOT compilation or graphics tests.

**NOT RUN** (user explicitly required feature-first): native C++/Obj-C++ build, CMake configure, clang, GLSL/MSL/SPIR-V compile, `./run-tests.sh`, CTest, editor/client launch, Metal/GL/GLES/Vulkan/WebGL GPU smoke, capture/readback, pixel-precision/alpha/visual parity, GPU fence overlap, performance benchmarking and CI. Do not claim any of those pass.

## Limitations and next Stage 57

- The algorithm is simple screen-space 8-neighbor defocus, **not** physically accurate lens bokeh, separated foreground/background scatter or an occluded-background reconstruction. Translucent depth/alpha can still require a dedicated mask/gather path.
- Projection formulas are based on the engine's non-reversed depth [0,1] contract; real-device compatibility must be verified later.
- A low occlusion tolerance can suppress blur on strongly depth-sloped surfaces; this is an explicit authoring control, not guaranteed photographic blur.
- If the project uses non-metre world units, the `Meters` naming denotes world units, not a unit conversion.
- No new GPU history was added; camera lifetimes remain owned by prior Prisma/Renderer3D queues. Do not mutate authoritative ECS state or create FPS/RTS-specific image passes.

Next bounded functional Stage 57: audit Auto Exposure HDR luminance sampling and color-grade/tonemap ordering, define robust camera-specific metering/clamping/fallback for bright outliers and custom Shader Graph with no duplicate HDR paths; alternatively implement additional DoF foreground/background quality tier. Keep ALL opt-in effects disabled by default. After the functional catalog is actually complete, run a dedicated native build, CTest and multi-backend GPU parity/performance/fence validation stage and document fixes. Commit an independently written `docs/handoffs/prisma-stage-57.md` at the end of the next stage.

PR #26 remains Draft. Do NOT merge to main without explicit user authorization.

## Mac pull commands

```bash
cd ~/Projects/AIVERSE
git status --short
git fetch origin
git switch feature/prisma-postfx
git pull --ff-only origin feature/prisma-postfx
git log -1 --oneline
```

If the current checkout is rebasing/conflicted, leave it untouched; create a clean separate worktree from `origin/feature/prisma-postfx` instead of switching/resetting/force-pushing a conflicted checkout.
