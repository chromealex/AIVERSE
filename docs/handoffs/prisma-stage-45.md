# PRISMA Stage 45 — verified-GPU-sample adaptive scale and safe temporal fallback

**Date:** 2026-11-19
**Branch:** `feature/prisma-postfx`
**PR:** https://github.com/chromealex/AIVERSE/pull/26 (Draft)
**Previous handoff:** `docs/handoffs/prisma-stage-44.md`
**Code/documentation HEAD immediately before this file:** `69ca51d961e0f45d89334ea46fa53f5feb89067c`
**Main:** not modified; do not merge PR without explicit approval.

## Work implemented and committed — precise per-file history

1. `e6e837dd622c89cc4475f80b7f39a610d522e553` — created `engine/render/PrismaAdaptiveResolution.h`. Reusable CPU-only hysteresis controller with bounded [0.5,1.0] 1/16-step scale changes, strict GPU-completed-fence sample deduplication, independent slow/fast thresholds and 48-sample cooldown.
2. `6d49a6e8155ed64edce0ed15bdda0cd7249450d1` — `engine/render/PrismaData.cpp`: extend `temporalResolve` catalog with `adaptiveResolution` boolean (default false), `targetGpuMs` (default 16.6 ms, 4..50) and `minRenderScale` (default 0.5, 0.5..1). No JSON root migration.
3. `02b5b6f18a9a8cf22dcdda79f3a0c411d23e7a0f` — `engine/render/Prisma.h`: include the controller, per-view `adaptiveViews_` map, preflight declaration, `TemporalView` preflightRejected/cooldown and new frame-stat fields `adaptiveResolution`, `requestedRenderScale`, `temporalFallback`.
4. `dce36e12182943130e64c045a4021b440b44e599` — `engine/render/Prisma.cpp`: implement `preflightUpscale(outputW,outputH,renderW,renderH)` on the graphics owner. It preallocates two RGBA16F output history targets, internal HDR scene/velocity, internal depth+scratch depth and output-size scratch depth; checks device health, format and temporal/motion pipelines before jitter.
5. `5c603effec9d6eeb6bad0c7616be10fac5b135b1` — `Prisma.cpp`: `renderSizeForOutput` now observes asynchronous `Device::gpuFrameMs()` **only** with new `gpuCompletedFence()` tokens. Requires `AIVERSE_PRISMA_ADAPTIVE=1`, catalog flag, all Stage 44 opt-in gates, High/HDR, and a valid GPU reading. It uses the authored renderScale as an upper bound and minRenderScale as floor; failure returns full output resolution and suppresses jitter, with 60-query preflight retry cooldown.
6. `d6a3e4bab9e255e3c52b4dfd19f38ee784d76259` — `Prisma.cpp`: save current jitter before temporal fallback, suppress jitter if preflight rejected, expose adaptive/fallback frame stats, and pass stable-sampling UV offset through `CompositeU.graphFrame.zw` after an unexpected temporal failure.
7. `6549c29e550a964e9144f72f1d7ed4de816178ed` — `engine/render/PrismaShaders.h`: the main composite now samples scene at `s+graphFrame.zw`; on successful temporal/nonjittered frames the offset remains zero. The final output/HUD passes are not shifted.
8. `a15b622455ad1c38f0622b22763ab1b8601a15fe` — `Prisma.cpp`: also preflight a working default composite pipeline, ensuring the low-res route has a shader fallback.
9. `564e1b1ddf3be9426989b00b83315c184d647a0e` — `apps/editor/windows/PrismaWindow.cpp`: show completed whole-frame GPU milliseconds, adaptive status, internal scale and whether jitter compensation was needed. Unsupported backends report unavailable rather than claiming zero workload.
10. `8cbcee1307796d73ef775ad5e4e07750cf46db05` — `Prisma.cpp`: erase stale per-view adaptive policy if the effect or backend requirements are no longer met.
11. `5eee620036221f02a06be3b7afad93c4d7f038d0` — `Prisma.cpp`: device lost/invalid disables temporal camera jitter.
12. `98f8ae4720bfffa31d321eaf8fa861bed9a919c5` — `skills/visuals/aiverse-prisma/SKILL.md`: Stage 45 agent instructions, exact opt-in, native host contract, limitations.
13. `cdf6da794440f63391fe8636a0241cc856207061` and `69ca51d961e0f45d89334ea46fa53f5feb89067c` — `docs/prisma.md`: architecture, fallback, GPU timings, Stage 45 actual verification status.
14. **This separate handoff** — `docs/handoffs/prisma-stage-45.md`, created after implementation and static source inspection.

## Public API and host workflow

No breaking signature changes to existing Prisma/Renderer3D APIs. The existing `Prisma::renderSizeForOutput(outW,outH)` chooses the scaled 3D frame size, called **before** `Prisma::jitteredCamera(unmodifiedCamera,renderW,renderH)` and `Renderer3D::beginScene`. `Renderer3D::render` receives the full-resolution target, while the HUD/picking input uses the original camera. GPU scale controller operates inside `Prisma` and never mutates authoritative ECS/gameplay state.

To activate dynamic adjustment (all conditions mandatory):
- Existing Stage 44: `AIVERSE_PRISMA_TEMPORAL=1`, `AIVERSE_PRISMA_JITTER=1`, `AIVERSE_PRISMA_UPSCALE=1`; High/HDR postprocess, authored `temporalResolve` effect with `renderScale<1` and history/jitter > 0.
- New Stage 45: `AIVERSE_PRISMA_ADAPTIVE=1` and effect parameter `adaptiveResolution=true`. `targetGpuMs` and `minRenderScale` can be authored in game/map JSON and camera-volume profiles.
- The active backend must provide finite positive **completed whole-GPU-frame timing** via `Device::gpuFrameMs()` and a strictly advancing real `gpuCompletedFence()`. Metal has an asynchronous implementation; unsupported APIs often return zero. No timing -> the authored render scale is retained, **not** CPU FPS adaptation.

Policy: >115% of budget for 12 fresh completed GPU samples decreases resolution scale by 1/16; <80% budget for 36 fresh samples increases it up to authored ceiling. After an adjustment, 48 new completed samples are cooldown, limiting oscillations and GPU memory churn. A completed fence token **deduplicates measurements only**; it is not inferred from CPU frame serial and does not authorize unsafe destruction. The global GPU ms includes other draw work, editor and postprocessing, and is not per-viewport timing.

## Fallback and resource lifetime

`preflightUpscale` performs an opportunistic GPU allocation of the size-shared HDR scene color/velocity/depth/scratch, output-sized scratch depth and two camera-private HDR color/depth histories, using the same `Prisma::Sized` and `TemporalView` owners as the normal render path. It checks the device format, health, temporal/velocity shader and a working fallback compositor. Failing any required item returns the **unscaled output dimensions** before a jittered 3D frame starts, and `jitteredCamera` sees `preflightRejected` and returns the original unjittered camera. Retries are limited by a 60-query cooldown. Existing GPU-fenced texture retirement owns resize/recreation; no new blocking GPU sync.

If later GPU work fails or a late history allocation cannot be used **after** jittered geometry was rendered, Prisma still uses the ordinary full-output composite. The temporal fallback records `currentJitterUv` and injects it into `CompositeU.graphFrame.zw`; the composite shader uses `clamp(s + offset,[0,1])` for stable scene sampling without allocating another GPU target. On success or unjittered scenes the offset stays zero. This avoids obvious one-frame jitter wobble but cannot recover missing hardware resources, thin-detail quality or a device loss.

`PrismaStats` now exposes `adaptiveResolution`, `requestedRenderScale`, `temporalFallback` in addition to previous input/output dimensions and temporal-resolve flags. The editor `PRISMA — Постобработка` displays these, along with a warning that backend GPU frame time is **whole-frame** rather than scene-specific.

## Actual verification and caveats

**Performed:** inspected current `Gfx.h` and found the asynchronous `gpuFrameMs()` and genuine `gpuCompletedFence()` interfaces, inspected Metal GPU timing and frame/fence override and GL timing limitations; read Stage 44 source before writing. Created/updated code and read back all eight changed code/docs paths. A **38/38 static source-presence check** verified the controller, parameters, opt-in, new-fence-only sample gate, preflight pipeline + five texture roles, cooldown, device-loss/unsupported fallback, shader UV compensation, editor diagnostics and agent docs. These checks are **source-text inspections only**, not runtime tests or a proof of performance.

**NOT RUN:** CMake, C++/ObjC++ compiler, CTest, Metal/GL/Vulkan/GLES/WebGL shader/link/runtime, framebuffer diff, actual adaptive fps/latency improvement, per-device completed-fence progression, low-VRAM failures, screenshot pixel alignment, GPU memory pressure/retirement and CI. Do not mark any such check PASS.

## Risks and precise next Stage 46

1. **First native priority:** compile editor, native client, HDR tests and shaders, then capture GPU images at renderScale 1.0/0.75/0.625/0.5. Check input/output origin conventions, motion-vector sign, transparent FX/reactive edge alignment, HDR tonemap, camera cuts, and meaningful performance benchmarks.
2. Preflight currently allocates from the normal GPU pools before camera submission. Driver-level failures during later render passes and GPU device loss cannot be fully recovered. Verify allocation failures and retry cooldown across Metal/GL/Vulkan; make frame-level fallback atomic if a backend does not support safe allocation checks.
3. The policy's global GPU timing may include expensive editor UI or other views, thus is a **conservative whole-device heuristic**. No per-viewport GPU timer or queue-critical-path attribution exists yet. Do not make frame-budget promises from it. Verify timestamp freshness using actual GPU completion callbacks in each backend.
4. Stable composite fallback corrects jitter on the source image. Bloom/shaft auxiliary textures may still exhibit different scaling/sampling than the current source. The fallback is bilinear spatial enlargement and may alias thin geometry: image quality tests are mandatory before a default.
5. Per-view adaptive controller has a 48-completed-sample cooldown and 1/16 resolution steps; evaluate whether windows rapidly hiding/reopening or a weak GPU cause too many retired size caches, verify the Stage 41 memory telemetry and gpu fence queues.
6. Stage 46 feature candidate: add explicit per-viewport timing ownership and VRAM-pressure-aware scale limits, or improve reactive/history confidence with more reliable depth/velocity semantics. Keep the older `PrismaScreenLighting` TAA separate unless unified intentionally.
7. Write separate `docs/handoffs/prisma-stage-46.md` after the next stage, including **actual** tests and current commit. PR #26 stays Draft; **do not merge into main without explicit approval**.
