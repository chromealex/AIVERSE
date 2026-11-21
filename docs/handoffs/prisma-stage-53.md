# PRISMA Stage 53 — per-view effect execution and fallback diagnostics

Date: 2026-11-19
Repository: chromealex/AIVERSE
Branch: feature/prisma-postfx
Draft PR: https://github.com/chromealex/AIVERSE/pull/26
Base: feature/universal-entity-inspector-fps
Previous stage: docs/handoffs/prisma-stage-52.md
Pre-handoff feature branch SHA: 9b1436bdf2bedcaff247aaf111ac765e2446b7a4
Main unchanged; never merge before explicit user approval.
User requested to finish functionality first, then run native builds/CTest/GPU tests.

## Scoped outcome

Stage 53 completes the GPU-free, stable-camera-ID **per-effect execution status**
layer and editor diagnostic display. It does not claim to finish all postprocessing
features, Renderer3D custom-view cleanup or screen-lighting GPU lifetime.
Those are the next functional stages, not silently counted as complete here.

## Source commits (exact)

1. `7b4c4df15d622d497325c10ac5b1592e5ae8de66` —
   `engine/render/Prisma.h`: public read-only `EffectExecution`,
   `ViewEffectReport`, `effectReport(viewId) const`,
   `effectReportViewIds()`; private map keyed by stable presentation camera ID.
2. `8135a8c003758c1b9f28b2892d49e0667f58272d` —
   `engine/render/Prisma.cpp`: GPU-free const report accessors, deletion
   from `retireView` and inclusion in `retireUnregisteredViews` live-ID union.
3. `fec588b2eb0e89e1d3784d2da5d65f4a1e7ca302` —
   `engine/render/Prisma.cpp`: per-camera report lifecycle, no-scene
   failure reporting, actual MotionBlur/DepthOfField/FXAA draw markers,
   successful shadergraph composite vs fallback distinction, clear
   substituted mask on fallback to plain composite.
4. `d0731c63a301fb8f1aa0628c48171161f0c67b29` —
   `engine/render/Prisma.cpp`: status snapshot at the end of every resolve,
   mapping the whole authored catalog and checking tier/opt-in/parameters,
   relevant cached shaders, used composite mask, history admission, camera
   cut/first-frame rejection, exposure shadergraph texture slot conflict,
   absent LUT/targets and fallback cases. The result is a last-encoded
   snapshot, never an asynchronous GPU-completion event.
5. `635a5b28f41b30f45e0beab6752ee36b459d2070` —
   `Prisma.cpp`: separate Bloom, TiltShift and SunShafts own RHI pipeline
   preparedness from composite readiness in effect reports.
6. `3731ba01007f2cfb62e6ef40a6c61ce75738338a` —
   `apps/editor/windows/PrismaWindow.cpp`: a collapsible per-camera
   matrix with each effect's requested/prepared/encoded and reason,
   explicit no-GPU-completion wording, and stable view ID + sequence.
7. `e0d1d78408d169bf988cc49b50bc142e6d2016d4` —
   `docs/prisma.md`: Stage 53 interface/lifetime/semantics/limits.
8. `008d9ecb0d7c96a80c0e0d05cfeec15df0938e04` —
   `skills/visuals/aiverse-prisma/SKILL.md`: recommended usage and
   continuation of single-camera ownership.
9. `9b1436bdf2bedcaff247aaf111ac765e2446b7a4` —
   `Prisma.h`: clarify that per-view sequence resets with settings.

This handoff is committed separately after code, design documentation and
skills update. Retrieve current branch HEAD from PR #26 after the handoff commit.

## API and GPU ownership contract

Call `prisma.setViewId(stableCameraId)` before each view render, apply any
per-camera volumes, then use `Renderer3D::render` and
`Renderer3D::resolvePrisma`. The `Prisma::resolve` call updates a report
with `viewId`, per-view resolve `sequence`, width/height, quality/HDR,
sceneReady, and a vector of all camera-profile authored effects (including
disabled and quality-skipped items).

`EffectExecution` fields:
- `type` and `authored` identify a configured effect.
- `requested` means enabled at this camera's tier with necessary opt-in
  and nonzero parameter gates. Some HDR-only effects require HDR/High.
- `pipelinePrepared` means the relevant cached RHI pipeline handles
  exist; NO shader compilation is triggered by the query.
- `encoded` indicates the corresponding draw or final composite
  contribution was actually encoded in the last `resolve`. It does NOT
  mean GPU-completed, visually contributing nonzero color, correct output
  pixels, or effect performance was measured.
- `reason` explains why an effect was rejected, omitted or downgraded.
  Several independent failures can be possible; this is a single
  representative reason, not a full graph of errors.

`effectReport(viewId)` and `effectReportViewIds()` are read-only CPU
queries. No GPU allocation/compile/queue synchronization, driver calls
or ECS state changes. The returned pointer is owner-managed and expires
when reports are overwritten/cleared; do not retain across another render,
camera retirement, `setSettings` or shutdown.

View IDs are independent of output resolution. `retireView` and
`retireUnregisteredViews` erase diagnostics of closed camera IDs,
`setSettings` and `shutdown` clear old snapshots. Actual RHI resources
continue to retire using existing independently-owned GPU fences.
The editor's old global `runtimeCapabilities` display is still about
the last Prisma resolve overall; prefer the new matrix for per-camera
effect status.

The code reports issued draw/selected contribution, not a GPU query
or shader-ready extrapolation. A failed composite pipeline falls back
to `composite(0)` and its effect mask is cleared before reporting;
this also prevents a nonexistent color variant from appearing encoded.

## Actual verification and limits

- Checked live branch and previous Stage 52 handoff before editing.
- Read back `Prisma.h`, `Prisma.cpp` and
  `PrismaWindow.cpp` from GitHub after edits.
- 18/18 **static structural source checks**, including API, per-camera
  maps, read-only queries, retirement, settings/shutdown clearing,
  per-view sequence updates, scene-failure reason, concrete FXAA/DoF/
  motion draw markers, shadergraph and composite fallback, camera-cut/
  quota reasons, editor labels, and avoidance of allocations in report queries.
- **NOT RUN:** CMake, clang/C++ compile, `./run-tests.sh`, CTest,
  editor runtime, GPU backend execution, shader compilation, Metal/GL/
  GLES/Vulkan/WebGL, GPU completion/fences, actual image comparisons,
  benchmarks or CI. No passing claims about those tests.
- Caveats: ordinary `runtimeCapabilities` is still latest-owner stats,
  not a synchronized per-view camera report. Per-effect `encoded`
  records submission, not eventual completion or actual visual benefit.
  Without an active `Prisma::resolve` invocation there is no new report;
  the editor labels retained reports as last-resolve snapshots.

## Next bounded functionality: Stage 54

1. Provide native host/API support to retire `Renderer3D` object-motion
   camera state together with `Prisma` camera state using the real GPU
   submission token; preserve all active views including size-sharing.
   Audit both Editor and GameClient for lifecycle correctness.
2. Make historical `PrismaScreenLighting` bounce/scatter/history/depth
   target resize and runtime disable safe with deferred, GPU-fenced
   retirement; do not call `destroyTexture` on in-flight draws.
3. Continue remaining effect catalog/editor usability gaps, then conduct
   a separate native-build/CTest, GPU/visual/perf testing phase once
   all user-requested functional work is finished. Never invent results.
4. Each bounded source stage must have another separately committed
   handoff in `docs/handoffs/prisma-stage-XX.md`, with exact commits,
   execution and test status, concrete next step and unchanged draft PR.

## Pull on the Mac (including origin branch fetch)

```bash
cd ~/Projects/AIVERSE
git status --short
git fetch origin
git switch feature/prisma-postfx
git pull --ff-only origin feature/prisma-postfx
git log -1 --oneline
```

If the main checkout is rebasing/conflicted, do not `git switch` it;
add a clean separate worktree from `origin/feature/prisma-postfx`
and repair the original worktree independently. No force push.
