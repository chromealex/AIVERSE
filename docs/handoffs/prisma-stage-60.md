# PRISMA Stage 60 — preserve fixed postFX when Shader Graph / image inputs fail

Date: 2026-11-19
Repo: chromealex/AIVERSE
Branch: feature/prisma-postfx
Draft PR: https://github.com/chromealex/AIVERSE/pull/26
PR base: feature/universal-entity-inspector-fps
Previous handoff: docs/handoffs/prisma-stage-59.md
Pre-handoff feature HEAD: c71233895401ebdba5ffbe672860a93e305198e7
main unchanged. Never merge to base/main without explicit user request.
User development sequencing: finish functional work FIRST, then run native compile / CTest / GPU smoke / visual and performance checks.

## Implemented functionality

A failing authored postFX Shader Graph no longer needs to throw away
valid built-in Bloom, LUT, ColorGrade, Vignette, Grain, etc. Previously
the resolve's last fallback went straight to `composite(0)` after
graph variants failed, even when the fixed-effect mask was valid.
This stage preserves that mask and handles absent GPU graph images.
It also reports actionable LUT/graph upload errors and avoids
repeated failing graph GPU uploads every render frame.

## Exact source commits and files

1. `bc09537cc0772358489c9135026d8177f5a0db7b` —
   `engine/render/Prisma.h`: add GPU-free
   `ViewEffectReport.shaderGraphFallback` to distinguish
   authored graph omission from loss of all fixed effects.
2. `f80a1998e8c778a9dc237e89de0ff85ae2e32900` —
   `engine/render/Prisma.cpp`: snapshot `fixedMask`,
   inspect actual uploaded `graphTextures_` handles for
   each authored Shader Graph input path, and select a
   per-frame `selectedGraph` only if its input images are
   valid. Execute the fallback ladder:
     (i) authored graph + optional AE,
     (ii) same graph without AE,
     (iii) fixed-effect mask without graph + AE,
     (iv) fixed-effect mask without graph and without AE,
     (v) plain composite(0) as last resort.
   Only bind graph textures on a final graph-bearing
   pipeline; `graphCompositePrepared` is true only
   if the actual selected variant includes the graph.
   **Recompute** `PrismaCompositeTextureLayout` after
   selection, using graph count = 0 for graphless fallback,
   preventing a four-texture graph's TEX9 exposure slot
   from leaking into a graphless TEX8 pipeline. Preserve
   Stage 59 manual EV, ColorGrade values and other
   uniforms in the successful fallback.
3. `b2ef84774eba7517ab6c86c7df79c65689295cdb` —
   `Prisma.cpp`: the ShaderGraph per-view effect report
   distinguishes absent/failed texture uploads from shader
   or composite failures, never marks plain/fixed pipeline
   as successful graph execution.
4. `bca558309ce1d3d581370d13a1a8ed2d7980d886` —
   `apps/editor/windows/PrismaWindow.cpp`: visible
   camera-scoped warning if Shader Graph was omitted
   while trying to preserve the built-in effects.
5. `d370ef8c72ec8557a9a8235986b21d36d932ec39` —
   `Prisma.cpp`: error details for LUT image read
   failure, invalid strip size (expected N²×N) or
   failed GPU upload in `lastError()`. Negative-cache
   Shader Graph GPU upload failures via invalid
   `graphTextures_[path]` sentinel, avoiding repeated
   decode/createTexture every frame. Existing
   `setImageLoader` performs GPU-fence deferred
   retirement and invalidates source-image caches.
6. `53050b0a2577ee5021eda0bba58b6194c68aa6e5` —
   `docs/prisma.md`: full Stage 60 fallback architecture,
   input-validation, sampler binding, errors and limits.
7. `c71233895401ebdba5ffbe672860a93e305198e7` —
   `skills/visuals/aiverse-prisma/SKILL.md`: Stage 60
   continuation contract for another agent.

This handoff is a SEPARATE documentation commit after bounded
code, design docs and skill updates as required by AGENTS.md.

## Runtime binding, ownership and API invariants

The original `Prisma::resolve` per-camera effect profile remains
authoritative. An active profile can request built-in postFX and an
optional Shader Graph simultaneously. Graph source paths are
obtained from the existing `composite(mask,graphData)` cache
preflight. All input `graphTextures_[path]` must be valid GPU
resource handles; a valid shared black fallback sampler returned
by `graphTexture` is **not** proof that the authored graph image
loaded successfully. A failed graph image skips only that
optional graph, not the built-in LUT/Bloom/Grade/etc.

A fixed-effect mask snapshot is taken BEFORE any graph or AE
fallback. When the graph pipeline cannot be prepared or any
input image fails to upload, PRISMA tries to render all built-in
effects without the graph, first attempting AE if a valid 1x1
camera-private exposure texture exists, and then omitting AE
only when the fixed AE variant itself is unavailable. Last
resort remains `composite(0)` to show the scene at all.
No new render targets, CPU GPU waits or effect-specific
histories are introduced.

The final shader layout must match the SELECTED pipeline:
- four-texture graph with AutoExposure: Shader Graph slots 5..8
  and HDR exposure TEX9;
- graphless fallback with AutoExposure: exposure TEX8;
- graphless/no-AE: no graph or AE samplers bound.
`PrismaCompositeTextureLayout::plan` is reevaluated
using the actual selected graph, not the authored count.
The GPU passes use `Device::setTexture` for this exact slot.
Stage 59 manual EV remains a scalar in `CompositeU.filt[2]`
and therefore continues in a fixed-only or plain composite
fallback; it is never mistaken for automatic metering.

`ViewEffectReport.shaderGraphFallback` is reset at the
beginning of each stable-camera resolve. It is true when
the camera authored Shader Graph but the selected final
pipeline omits it. The graph's `EffectExecution` reports
`encoded=false`; the fixed effects report from the
FINAL selected effect mask and pipeline, rather than
the failed initial candidate. `PrismaWindow` reads this
per-camera status without allocating GPU resources.

`Prisma::lut()` already negative-caches failed assets
until loader reset; this stage also places the reason
in `lastError()`, including the expected image strip
format. `graphTexture()` now negative-caches a failed
GPU upload as it already did missing image data.
This prevents frame-by-frame repeated GPU upload attempts
on a persistently failing source. Explicit
`setImageLoader` invalidates the LUT and graph caches;
it safely fence-retires successful GPU textures.
Editing image bytes on disk without calling the cache
invalidation routine is NOT live-reload. Do not guess
that a missing asset will self-correct on the next frame.

## Actual verification

- Verified current remote branch head and draft PR, reviewed
  prior `docs/handoffs/prisma-stage-59.md`, and inspected
  `Prisma.cpp` composite/fallback and source image loaders,
  the original Shader Graph helpers, per-camera reporting,
  editor panel and existing shader texture layout.
- Read back `engine/render/Prisma.cpp`,
  `engine/render/Prisma.h`, and
  `apps/editor/windows/PrismaWindow.cpp` from connected
  GitHub after source updates.
- Performed **33/33 static source-presence, call-order,
  error-path and GPU-ownership checks**, including:
  fixed mask retention, graph input validation, graphless
  AE retry, last-resort composite, final TEX8/TEX9 slot,
  actual texture bindings, per-effect and per-view status,
  LUT error strings and negative cached GPU upload.
- Source-only checks are NOT a C++/shader compile, a real
  render, a memory/fence test or pixel validation.

**NOT RUN**, per user's features-first instructions:
native CMake configure/build, clang/C++/Obj-C++,
GLSL/MSL/SPIR-V compilation, CTest, `./run-tests.sh`,
Metal/OpenGL/GLES/Vulkan/WebGL renderer smoke,
editor/client launch, HDR image screenshots and
readback/parity, GPU fence overlap, performance, CI.
Do NOT mark any unrun outcome PASS.

## Remaining limitations / precise Stage 61

1. Native shader compilation and actual runtime remain
   necessary to validate graph + 4 texture + AE TEX9,
   graphless AE TEX8, failed-image handling, LUT and
   multiple effects. The RHI can still reject a shader
   variant even if the source policy is correct.
2. The Shader Graph failure fallback does not provide a
   hot-reload watcher for image edits; the owner must
   explicitly invalidate image caches via setImageLoader.
   Source-level error reporting intentionally does not
   claim recovery without reload.
3. Further functional audit should focus on transparent
   reactive masks, temporal + FXAA composition, motion
   blur HDR/alpha, scene UI postprocess scope, and
   per-camera resource lifecycle or cross-view timing.
   Do not duplicate renderer ownership or SpectraFX.
4. When the functional catalog is truly complete, start
   a dedicated native build, CTest, multi-RHI GPU smoke,
   pixel capture/parity, fence and profiling/fixes phase.
   Record exact commands and actual PASS/FAIL.
5. Every bounded stage must end with a separately
   committed `docs/handoffs/prisma-stage-XX.md`
   after its source/docs changes, listing exact SHAs,
   performed vs NOT RUN checks, current branch and
   concrete next step. Keep Draft PR #26; DO NOT merge
   to `main` or base without user's explicit approval.

## Mac branch fetch/pull

```bash
cd ~/Projects/AIVERSE
git status --short
git fetch origin
git switch feature/prisma-postfx
git pull --ff-only origin feature/prisma-postfx
git log -1 --oneline
```

If the local worktree is rebasing or conflicted, use a new
dedicated worktree based on origin/feature/prisma-postfx;
never reset or force-push concurrent work.
