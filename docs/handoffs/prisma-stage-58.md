# PRISMA Stage 58 — cinematic exposure freeze and per-view circular HDR metering

Date: 2026-11-19
Repository: chromealex/AIVERSE
Working branch: feature/prisma-postfx
Draft PR: https://github.com/chromealex/AIVERSE/pull/26
Base: feature/universal-entity-inspector-fps
Previous stage: docs/handoffs/prisma-stage-57.md
Pre-handoff current branch SHA: 53443bf73019d4be10361f539863652dc15be261
Main unchanged. Never merge without explicit user authorization.
User sequencing: implement remaining functional features FIRST, then a separately reported native CMake/CTest, Metal/GL/GLES/Vulkan/WebGL, visual/performance and bugfix phase.

## Functional result

Stage 58 adds a cinematic lock that holds the last valid, per-camera 1x1 HDR
Auto Exposure value without running new luminance/reduction/adaptation draws,
and an opt-in soft circular metering region with nonzero weight outside
the circle. It also offers native stable-view-ID overrides for each
camera, including two cameras with identical coordinates and dimensions,
without changing authoritative ECS, the postfx JSON root, MCP names,
existing texture-slot layout, or the identity of the HDR owner.

Only the existing `PrismaType::AutoExposure` pipeline is used:
linear HDR SceneColor -> kExposureLog -> kExposureReduce ->
kExposureAdapt -> one camera-private 1x1 history -> final composite
with stage-55 graph+exposure slots 8/9 -> ACES/sRGB, display grading,
LUT/Shader Graph and UI. Existing opt-in `AIVERSE_PRISMA_AUTO_EXPOSURE=1`
and HDR/high quality constraints remain.

## Implementation commits and exact changed paths

1. `0d0005c6dcd22a20ae054d67c6869a55ccd11817` —
   `engine/render/PrismaData.cpp`: append optional fields to the
   existing Auto Exposure catalog in order without modifying old
   flattened parameter indices:
   `freezeExposure` (false), `meterRegionEnabled` (false),
   `meterCenterX/Y` (.5,.5), `meterRadius` (.25),
   `meterFeather` (.10) and `meterOutsideWeight` (.05,
   constrained .02..1). Old profiles are unchanged by default.
2. `b0b774f6c584910fdd723c2e79db33e5881a1884` —
   `engine/render/PrismaShaders.h`: optional screen-space circle
   metering weights in `kExposureLog`, composed with existing
   center Gaussian and stage-57 highlight suppression.
   `region.z<0` disables this branch and preserves old meter.
   Pixel coordinates go through `TUV(q)` to undo Metal target
   flip and normalize X using width/height so radius is
   frame-height-relative; outside weight stays positive.
3. `13aa12af559de2fa49a794fe622d8a8aa1bdc2d4` —
   `engine/render/Prisma.h`: add per-camera
   `ViewEffectReport.autoExposureFrozen` and
   `autoExposureRegionMetered` (last encoded composite only).
4. `3239bbeb6afbd5467f1674a1c9fdbe6dfab47b9d` —
   `engine/render/Prisma.cpp`: dedicated
   `ExposureMeterU {px,p0,sun,region}` ONLY for kExposureLog;
   keep SimpleU for kExposureReduce and all other effects.
   Reset new status fields at start of each resolve.
5. `1ee31332962c187be253886303497fcd6e9cd039` —
   `Prisma.cpp`: use valid `ExposureView.history[current]`
   directly when freeze is requested; skip entire metering and
   adaptation chain while frozen; seed once from current frame
   if cold/cut, and refresh timestamp to avoid a long dt on
   unfreeze. Feed region fields through both normal first-mip
   and 1x1 ExposureLog paths, with the matching UBO sizes.
6. `a82253868df16b0af7a2d4d7a879ec8e9df16934` —
   `Prisma.cpp`: report freeze and region-used flags only if
   the FINAL chosen composite shader contains Auto Exposure;
   failed exposure variants/fallbacks do not count as executed.
7. `625dfdaa2fc0773347cf938fca01b2e1a7453615` —
   `apps/editor/windows/PrismaWindow.cpp`: explain cinematic
   freeze and normalized metering controls in generic effect
   authoring; show the last camera-specific encoded frozen/
   region-metered states without triggering RHI compilation.
8. `8999b31a9ebccc8e289fda57152a9ab363d7e276` —
   `engine/render/Prisma.h`: native per-camera API
   `setViewExposureFreeze(id,bool)`/`clearViewExposureFreeze(id)`
   and `setViewExposureMeterRegion(id,ExposureMeterRegion)`/
   `clearViewExposureMeterRegion(id)`. An explicit false
   overrides profile true.
9. `007d4534fcd3545990f8a37f2fba15240d628d31` —
   `engine/render/Prisma.cpp`: validate and clamp finite native
   region fields, retain overrides in per-view maps, erase them
   in `retireView`/`shutdown`, and include them in the
   `retireUnregisteredViews` live-view union.
10. `15b5923898d75d85fbcb2661ff42ec9e2a1ecc6b` —
    `Prisma.cpp`: resolve effective freeze/region settings
    per stable view before the exposure code. Native overrides
    beat authored volume/profile, including explicit disabled
    modes. The two existing ExposureLog paths use these values.
11. `acbc7b8e40b2a411873cbf1643ecd27994a5a244` —
    `docs/prisma.md`: Stage 58 schema, shader strategy, runtime
    native API, diagnostics and validation limitations.
12. `53443bf73019d4be10361f539863652dc15be261` —
    `skills/visuals/aiverse-prisma/SKILL.md`: Stage 58
    feature usage, binding and ownership rules, agent guidance.

This handoff is a separate documentation commit AFTER the code,
docs and skill updates. Fetch PR HEAD for the definitive SHA.

## API examples and invariants

```cpp
// Stable presentation camera ID assigned by host before render.
prisma.setViewId(viewId);

// Cinematic hold: existing 1x1 exposure sample is reused, with no
// kExposureLog/kExposureReduce/kExposureAdapt draw.
prisma.setViewExposureFreeze(viewId, true);

// Region center normalized bottom-up; radius+feather relative to height.
// The final float is minimum weight outside the region (>0).
prisma.setViewExposureMeterRegion(viewId,
    {true, 0.5f, 0.5f, 0.25f, 0.10f, 0.05f});

// Return to inherited profile/volumes:
prisma.clearViewExposureFreeze(viewId);
prisma.clearViewExposureMeterRegion(viewId);
```

An explicitly set false freeze (or disabled region) supersedes a
volume-authored true. The setters validate floating-point region
values using std::isfinite and clamp to authored catalog bounds.
The host MUST provide its complete stable camera list in
`retireUnregisteredViews(liveViewIds)` after camera submissions.
Only abandoned view IDs lose overrides/history; dimensions are NOT
used as ownership keys. If an override is set ahead of a frame but
its ID is absent from the host roster, retirement is intentional.

A valid frozen history is reused as a READ-ONLY texture in the
final exposure composite. The old texture is never rebound as
a render target in that frame. When no valid sample exists (new
camera, explicit cut, automatic jump, setting transition or
prior failure), the pipeline performs one ordinary GPU meter/
adaptation pass then freezes. Camera cuts still discard stale
exposure validity, as introduced in Stage 57; the existing
GPU-fenced retirement of texture objects is unaffected.
Unfreeze does not create a new owner or invalid delta time.

New shader `region.z<0` is the disabled sentinel. If active,
each of four original luminance taps gets a multiplicative weight
that fades from 1 inside the circular ROI to
`meterOutsideWeight` outside, with `smoothstep` feathering.
The existing Stage 57 bright-outlier rejection stays independent
and the final weighted-log sum and sum-of-weights reduction
remain unchanged. This is a bounded soft ROI meter, NOT a
histogram, texture mask, spatially varying local exposure or
an AI/face detection meter. At the first 1x1 scene input the
new exposure uniform path applies identically.

`ViewEffectReport.autoExposureFrozen` means an already valid
camera history was sampled without running the meter this
frame. `autoExposureRegionMetered` means the current frame
actually metered with ROI while producing a new exposure. They
are never marked as GPU-completion/pixel-parity proof.
If the Auto Exposure shader is omitted by the final composite
pipeline fallback, BOTH reports are false; Shader Graph inputs
and layout remain as in Stage 55.

## Actual verification

- Inspected `docs/handoffs/prisma-stage-57.md`, current branch HEAD,
  DoF/exposure catalog and parser, ExposureLog shader, render path,
  exposure history and camera lifecycle, editor status and
  Stage-55 exposure binding.
- After code writes, read back all five changed source files from
  the connected GitHub repository and performed **36/36 static
  presence/call-order/ownership checks**, including per-camera
  overrides, finite-clamp sanitization, no meter during valid
  freeze, cold-start metering, explicit and automatic camera cuts,
  no self-binding of history textures, positive ROI weights,
  backend UV flip handling, UBO split, final composite gating,
  editor and lifetime cleanup.
- No local AIVERSE C++/GPU checkout was available. These source
  inspections are NOT a native compile, a smoke run or a graphics
  correctness test.

**NOT RUN** per user's feature-first instruction: CMake, clang,
Obj-C++, GLSL/MSL/SPIR-V, CTest, `./run-tests.sh`, editor/client
start, Metal/GL/GLES/Vulkan/WebGL, HDR readback/screenshot parity,
timeline/fence stress, shader and GPU performance, CI.

## Limitations and precise next stage (Stage 59)

1. Audit the complete remaining PRISMA postprocess catalog and
   experimental RHI graph paths, especially Auto Exposure
   failure handling, graph compositor fallback with LUT,
   transparent/reactive surfaces and correct scoped per-camera
   GPU diagnostics. Close concrete functional gaps before
   declaring feature completion.
2. Consider additional authored cinematic features only if
   needed: explicit manual EV (fixed exposure, distinct from
   freeze-the-history) and metering texture masks. Keep them
   optional with GPU-free schemas and per-camera host APIs.
3. Review whether runtime settings changes invalidating the
   history for ALL cameras are necessary, and whether
   volume-only changes can avoid cross-camera exposure resets.
   Never cross-contaminate camera identities by resolution.
4. Once all requested functionality is complete, start a
   dedicated build/GPU validation phase, report actual commands,
   shader compile errors and real PASS/FAIL outcomes, and
   repair verified regressions. Do not mark queued CI as pass.
5. Create a separate `docs/handoffs/prisma-stage-59.md` after
   Stage 59 code; list exact commits, touched paths, actual
   tests (or NOT RUN), limitations and next step. Do not merge
   Draft PR #26 into base or main without explicit approval.

## Mac pull commands

```bash
cd ~/Projects/AIVERSE
git status --short
git fetch origin
git switch feature/prisma-postfx
git pull --ff-only origin feature/prisma-postfx
git log -1 --oneline
```

If the current checkout is rebasing/conflicted, leave it
untouched and use a clean separate worktree based on
`origin/feature/prisma-postfx`; no reset or force push.
