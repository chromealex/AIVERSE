# PRISMA Stage 57 — robust HDR exposure metering and pre-meter camera-cut reset

Date: 2026-11-19
Repository: chromealex/AIVERSE
Branch: feature/prisma-postfx
Draft PR: https://github.com/chromealex/AIVERSE/pull/26
Base: feature/universal-entity-inspector-fps
Previous handoff: docs/handoffs/prisma-stage-56.md
Last verified pre-handoff feature SHA: 70ae2073faa5315f36f3dc5c46ffb51d9bde9e97
Main: unchanged. No merge without explicit user approval.
User sequencing: finish all functional features first; run native CMake/CTest/GPU/visual/performance validation only after feature closure.

## Functional outcome

HDR Auto Exposure now supports opt-in downweighting of extremely bright
outlier samples during its existing weighted log-luminance pyramid. A
camera's automatic or explicit motion-history cut resets its
camera-private exposure validity BEFORE the current frame's metering,
so a teleported camera no longer intentionally blends the previous
location's adapted brightness in the first newly presented frame.

This is one global, genre-independent Auto Exposure owner, not a
second HDR pass or effect duplicated in SpectraFX. Defaults and
external `postfx` JSON, MCP IDs, editor saved window ID, shaders
outside the exposure meter and other genre modules remain compatible.

## Source commits / modified files

1. `c3bd8a87b33d5c37a2bd49557e245dc1582e99a4` —
   `engine/render/PrismaData.cpp`: append three optional Auto Exposure
   fields without shifting old flattened parameter offsets:
   - `highlightRejection` (default 0, range 0..0.95)
   - `highlightStartEV` (default 3, range -10..16, log2 scene luminance)
   - `highlightSoftnessEV` (default 2, range 0.1..8 log2 stops).
2. `6ed2a5adc02b4955f28a1d0cbd073459915842c1` —
   `engine/render/PrismaShaders.h`: robust optional highlight
   confidence in `kExposureLog`. Its existing spatial center-weighted
   luminance numerator/denominator remain unchanged at rejection=0,
   and all bright scenes retain at least 5% weight per sample. The
   existing `kExposureReduce` and `kExposureAdapt` are unchanged.
3. `998d2d59cf88197e1c970146c88c2876c175c2d2` —
   `engine/render/Prisma.cpp`: move automatic camera-cut detection to
   immediately before GPU metering; clear motion/temporal validity and
   `resetAutoExposure(viewId_)` on a camera jump; send outlier
   configuration in the first ExposureLog draw and the 1x1 input path.
   No new GPU textures, CPU readback, or extra shader pass.
4. `369666d92919e045deae2cde1701fa01cc2837ed` —
   `Prisma.cpp`: explicit `resetMotionHistory(id)` also invalidates
   `resetAutoExposure(id)`; all-view motion reset invalidates
   corresponding exposure histories. History GPU allocations are kept
   until the existing GPU-fenced retirement path.
5. `80b910fca8ebe71060cdbd56b231321ccd05dceb` —
   `apps/editor/windows/PrismaWindow.cpp`: authoring help for
   highlight weights, log2 thresholds, >=5% floor, camera histories,
   and pre-tonemap color order.
6. `6f6f25ce742c0a8eb0679b2957974984b4c71d0e` —
   `engine/render/Prisma.h`: `ViewEffectReport` includes the
   effective `autoExposureHighlightRejection` value (0 if absent).
7. `f8a1be7345fe2a2d7d889cd4b66181a9ea00fd68` —
   `Prisma.cpp`: clear report field at resolve entry; set its value
   only if the final selected composite pipeline has
   `kPfAutoExposure`. Failed pipeline variants cannot falsely
   appear to apply the outlier configuration.
8. `0fd2b14561ff83dc879be13eed53ba06ec79c3d9` —
   `PrismaWindow.cpp`: display the effective per-view highlight
   rejection alongside actual HDR exposure sampler slot only when
   exposure was encoded.
9. `44ee2fc18bcfe69ea4e2c1befdfbe7d25cd3e4fd` —
   `docs/prisma.md`: Stage 57 design, authored defaults, weighted
   shader algorithm, color order and explicit unverified status.
10. `70ae2073faa5315f36f3dc5c46ffb51d9bde9e97` —
    `skills/visuals/aiverse-prisma/SKILL.md`: Stage 57 agent
    contract, exposure GPU ownership and next stage guidance.

This handoff is a distinct commit AFTER the code and skill/design
documentation. Confirm the new GitHub branch SHA after publication.

## Shader math and camera lifetime

Existing pipeline: SceneColor (linear HDR) -> four-sample log luminance
measurement -> mip reductions of (weighted log sum, weight sum) ->
1x1 `kExposureAdapt` -> per-view 1x1 ping-pong history -> final
composite before ACES/sRGB -> display grade/LUT/Shader Graph -> UI.
The new `kExposureLog` alone uses the optional parameters:
```text
logL = clamp(log2(max(luma, .0001)), meterMin, meterMax)
baseWeight = mix(1, centerGaussian, centerWeight)
reduction = highlightRejection *
            smoothstep(highlightStartEV, highlightStartEV + highlightSoftnessEV, logL)
weight = baseWeight * (1 - reduction)
```
`highlightRejection <= .95` prevents the per-pixel weight from
reaching zero, even for an entirely bright HDR frame. This is
soft bright-outlier downweighting, NOT histogram percentile metering,
a local exposure engine, a global sky mask or proof of optimal AE.
With the default `highlightRejection=0` the old spatial estimator
and all original exposure settings remain authoritative.

`Prisma::resolve` now detects abrupt camera movement before the
exposure pyramid starts. It compares the current presentation
`f.cameraPos` with the prior per-camera `MotionView`, rejects
non-finite changes and jumps >12 squared-root presentation world
units, invalidates velocity and temporal histories and resets only
the same view's exposure validity. Explicit `resetMotionHistory`
does the same (per ID or for all cameras). No immediate texture
destruction, GPU queue wait, cross-camera history reuse or simulation
ECS changes are introduced.

The final composite continues to apply the GPU Auto Exposure factor
only once through `kPfAutoExposure`, before tone mapping. The
ColorGrade-authored exposure is a separate user multiplier
`u.filt[2]` in linear HDR, not another history. No extra sampler is
introduced: Stage 55's Auto Exposure slot 8/9 graph texture binding
rule remains in effect. The last encoded per-view report includes
the effective `highlightRejection` only if that chosen composite
contains exposure. No status API triggers shader compilation.

## Actual checks (not GPU/native)

- Retrieved current branch SHA and Stage 56 handoff from the
  connected GitHub repository before edits.
- Inspected existing `kExposureLog`, `kExposureReduce`,
  `kExposureAdapt`, camera-specific history and cuts, final HDR
  composite/ColorGrade order, GPU program setup, PrismaData catalog,
  editor controls and per-view diagnostics.
- After updating five code files, fetched the content back from
  GitHub and performed **31/31 static source-presence and
  call-order checks**, including default compatibility, >0 sample
  weight, uniform propagation in both mip paths, early automatic
  cut, explicit camera cuts, stable view IDs, no double exposure,
  dynamic Shader Graph sampler mapping, and per-view actual report.
- Source-level checks are NOT a compiler, runtime or image test.

**NOT RUN:** local CMake/clang C++/Obj-C++ build, shader compilation
(GLSL/MSL/SPIR-V), CTest, `./run-tests.sh`, editor/client startup,
Metal/GL/GLES/Vulkan/WebGL GPU execution, 1x1 HDR color/pixel readback,
bright-fire/sky/dark-room temporal parity, GPU fence/cut stress,
performance/memory measurement or CI. Never describe these as PASS.

## Open issues / precise Stage 58

1. Finish optional camera-specific *exposure authoring and temporal
   controls*: consider cinematic exposure lock / temporary freeze
   and metering mask selection, still without duplicating an HDR
   pipeline or introducing authoritative ECS state. Extend Editor
   and per-camera diagnostics for author-visible behavior.
2. Validate source-level exposure pass accounting, especially failed
   mip allocation and fallback to disabled AE, and mixed
   Shader Graph/LUT/ColorGrade combinations. Never mark a
   missing GPU pipeline or unencoded effect as success.
3. Continue the broader remaining postprocess catalog/quality
   integration, then perform a separate native compiler, CTest
   and GPU image/fence/profiling verification phase once all
   functional work is completed.
4. Every new stage requires a separately committed
   `docs/handoffs/prisma-stage-XX.md` listing actual commits,
   APIs, source tests vs NOT RUN, risks, current SHA and next
   step. Keep PR #26 Draft; no merge into base/main without
   explicit user approval.

## Mac fetch / pull

```bash
cd ~/Projects/AIVERSE
git status --short
git fetch origin
git switch feature/prisma-postfx
git pull --ff-only origin feature/prisma-postfx
git log -1 --oneline
```

If the local working tree is conflicted, do not switch/reset or
force-push it; create a clean dedicated worktree from
`origin/feature/prisma-postfx`.
