# PRISMA Stage 47 — hardware Metal scoped GPU timers and Renderer3D-owned memory accounting

**Date:** 2026-11-19
**Repository:** `chromealex/AIVERSE`
**Branch:** `feature/prisma-postfx`
**PR:** https://github.com/chromealex/AIVERSE/pull/26 (Draft)
**Previous handoff:** `docs/handoffs/prisma-stage-46.md`
**HEAD before this handoff:** `82ddfc6306ec5094f60055e6e47b447df3744aea`.
**Main:** not changed or merged. This document itself is committed as a separate final stage commit.

## Completed changes and exact source commits

1. `526db6c4e83e1a4f91f3e24b4ee1c6201d0f3b92` — `engine/gfx/metal/GfxMetal.mm`: implement real optional bounded GPU timestamp scopes using Metal counter sampling on hardware advertising the timestamp counter set and blit/stage sampling capability; reject invalid samples; resolve after GPU command buffer completion; calibrate the GPU timestamp interval with two CPU/GPU reference clock pairs. Independent queue from full-command-buffer `timeGpuFrame` benchmark timings.
2. `0a1e73dbbcd66352b0bd53320aa50b8c0b7f8a06` — `GfxMetal.mm`: guard counter-encoder code with macOS 11.0/iOS 14.0 runtime availability. Unsupported devices fail closed and return no fake per-view ms.
3. `21a4349cc43f0892feab0b910679ffb9d8ee1aa2` — `engine/render/Renderer3D.h`: public `ResidentMemoryStats` and `residentMemoryStats() const` read-only, directly owned GPU resource lower-bound API.
4. `5384bf4947bd882b19b1067cc874f998530c9f38` — `engine/render/Renderer3D.cpp`: deduplicate texture/buffer handle IDs; count RHI-known shadow map, selection, trace placeholders, renderer-owned model placeholders, size-scoped scene capture, private object motion/depth/reactive targets and core owned render buffers. Report deferred motion textures separately. Unknown compressed textures/buffers remain unknown rather than guessed; external Model/Material, LightSystem and persistent geometry owners excluded.
5. `12796d834066bd1e25f7aecb3647dd25e0920218` — `engine/render/Prisma.h`: explicit optional `Prisma::setExternalRendererGpuMemory(liveKnownBytes,pendingKnownBytes)` host API. This does NOT transfer ownership or grant GPU-memory release rights.
6. `b46e5d87e5a4fab011772e0b1b28035e773e5253` — `engine/render/Prisma.cpp`: combine known Prisma bytes and externally supplied directly-owned Renderer3D live/pending bytes with saturated uint64 additions for the Stage 46 adaptive memory-pressure threshold. Device driver allocations, external model/LightSystem/pools remain excluded.
7. `1582b2ed64057a8dad869da88e290a95d30e4883` — `apps/editor/Editor.cpp`: provide renderer's counted live and deferred motion memory separately before the Scene and Game viewport render-size decisions. Full-resolution UI and original gameplay/picking camera remain intact.
8. `aee69ba685cde0767d212fd000a7969b4fa9ef40` — `apps/client/GameClient.cpp`: supply the same nonoverlapping direct renderer memory estimate before choosing internal render size.
9. `6d556c24856e9d3b22c24a4e4c88283a46ef1dd9` — `apps/editor/windows/StatsWindow.cpp`: display real completed scope samples on supported Metal or GL33, directly owned Renderer3D texture/buffer lower-bound totals, scene capture/motion subtotals, pending motion and missing metadata without double counting Prisma.
10. `0fc584088ed6838da6234270ba7f9f39d4c2c9ba` — `engine/render/PrismaAdaptiveResolution.h`: source-aware sample token domains. Scoped query sequence IDs cannot be compared with whole-frame GPU fence IDs; on a source switch reset only the last token and short-term counters, not chosen scale/cooldown.
11. `d726d079a5c6c4e75b61fa66b4a3279ee6317cfa` — `engine/render/Prisma.h`: snapshot true GPU-completed fence when accepting per-view scoped measurement, for freshness handling.
12. `985c73797d80d2006bc9ee1414340ed28d5b3a79` — `engine/render/Prisma.cpp`: treat a scoped measurement as stale after more than 8 later GPU fence completions with no new scope; prefer scoped if fresh, otherwise valid whole-frame fallback. Never repeat a prior completed scoped sample to increase hysteresis.
13. `9ce1520ded4477f0123244d25d6c3b5f9bf8897f` — `apps/editor/windows/PrismaWindow.cpp`: label Metal/GL33 per-view counter timing separately from whole GPU frame and label memory as Prisma + directly owned Renderer3D.
14. `2fb3a49eb48d755a25598ef5a1da1343fb3ed750` — `Prisma.cpp`: update adaptive source contract comments.
15. `4107858bd4b2e423a024252b5e778f59de5779b7` — `skills/visuals/aiverse-prisma/SKILL.md`: next-agent usage and Metal backend requirements, memory ownership, source-token freshness.
16. `7f0cb758e05fdf2302ace49c2dfefab4eb18c9fe` and `82ddfc6306ec5094f60055e6e47b447df3744aea` — `docs/prisma.md`: architecture, API, omissions, actual static verification 53/53.
17. **This handoff** — `docs/handoffs/prisma-stage-47.md`, committed as a separate final document.

## GPU timing architecture / API

The existing engine-wide `gfx::Device` interface from Stage 46 exposes:

```cpp
bool beginGpuScope(uint64_t tag);
void endGpuScope();
std::vector<gfx::Device::GpuTiming> takeGpuScopeTimings();
```

Editor uses `AIVERSE_PRISMA_VIEWPORT_GPU_TIMING=1` and scopes GPU work for Scene (1) and Game (2) from the start of Renderer3D GPU render through the last viewport pass, not from earlier asset streaming. Timings are asynchronous, polling does not stall GPU and is separate from existing whole-GPU-frame/benchmark timers.

Stage 47 Metal implementation **does not** infer per-view duration from `MTLCommandBuffer.GPUStartTime/GPUEndTime` and does not create extra command buffers. On macOS 11+/iOS 14+ it looks for `MTLCommonCounterSetTimestamp` and support for `MTLCounterSamplingPointAtBlitBoundary` or `MTLCounterSamplingPointAtStageBoundary`. It allocates one 2-sample `MTLCounterSampleBuffer` per scope. The two endpoints are sampled with blit commands (or blit-stage sample-buffer attachments) in the **existing** command buffer, separately from normal render-pass encoders. The per-scope record holds the specific command buffer and reference CPU/GPU timestamps; `takeGpuScopeTimings()` only resolves samples after command-buffer completion (using `resolveCounterRange`), rejects zero/error sentinel/nonmonotonic/nonfinite results and converts raw GPU ticks into CPU nanoseconds based on a second simultaneous CPU/GPU clock read. Records are capped at 64; failing capability checks returns false. The old whole-command-buffer Metal timing and GPU completion fences continue to operate as before. The earlier GL33 scoped timer implementation remains in place; GLES, WebGL and Vulkan have not received scoped implementations.

**Important limitation:** this is a GPU-clock interval, not a true isolated GPU busy-time measurement or a guarantee about async parallel work; Metal counter sampling can perturb performance. Timestamp buffer support and calibration differ by GPU/OS, so native validation remains mandatory. No fallback path ever invents a per-view metric when counters are unavailable.

Apple references used for API design:
- https://developer.apple.com/documentation/metal/sampling-gpu-data-into-counter-sample-buffers
- https://developer.apple.com/documentation/metal/converting-gpu-timestamps-into-cpu-time
- https://developer.apple.com/documentation/metal/converting-a-gpus-counter-data-into-a-readable-format

## Renderer-owned memory / owner contract

`Renderer3D::residentMemoryStats()` is a read-only lower-bound GPU-memory diagnostic for resources that `Renderer3D` allocates **itself**, not all resources rendered by it. It adds unique `gfx::TextureHandle` and `gfx::BufferHandle` IDs only once using RHI metadata. Scene capture aliases `sceneColor_/sceneDepth_` point to the already counted scene targets and are not counted again; per-camera motion, reactive textures and retired motion targets are separate. Known totals include directly owned shadow, selection, trace placeholder, model placeholder, capture and motion images, line, triangle, selection, blit and indirect buffers. Dynamic buffer versions beyond the logical minimum are diagnosed separately without claiming physical bytes. Missing RHI metadata is `unknown`, not zero VRAM.

The report deliberately does **not** include external assets (`Model/Material` meshes/textures), `LightSystem` shadow/reflection textures, persistent geometry upload/culling/indirect pools, unknown shader/pipeline memory, driver allocation alignment, certain mip chains, framebuffer/drawable copies or PRISMA textures. A complete RHI allocation registry across GPU owners is a separate project.

`Prisma::setExternalRendererGpuMemory(live,pending)` receives the no-overlap direct Renderer3D report from **each graphics host before** `renderSizeForOutput`; it does not own or free the passed handles. Stage 46's existing `temporalResolve.maxPrismaMemoryMiB` data identifier remains stable, but its comparison now incorporates the **known lower bounds of both Prisma and directly-owned Renderer3D**. This is not a hard VRAM cap. Existing GPU-fenced retirements still decide when GPU objects can be destroyed; the quality controller may only change future internal render scale and cannot force unsafe destruction.

## Scoped-to-whole timing source switching

`PrismaAdaptiveResolution::observe(gpuMs,completionToken,...,memoryPressure,scopedSample)` treats a completed scoped query's monotonically increasing `ViewGpuTiming::sampleSequence` as a different token namespace from `Device::gpuCompletedFence()`. If the source switches, the old token and short-term counters reset, **not** the current scale or cooldown. When `acceptViewGpuTiming(viewId,ms)` receives a completed true query it also stores the current GPU completed fence as a freshness marker. A scope sample expires if more than eight later completed GPU frames have occurred without a replacement, allowing a truthful whole-frame timer fallback. No CPU FPS or inferred GPU timestamp enters the policy.

## Verification actually performed

**Performed:** inspected current GitHub branch and Metal command-buffer lifecycle, GL33 scoped timing, renderer-owned texture/cache/lifetime fields, Prisma resident and deferred memory estimator, editor Scene/Game and GameClient calls. Confirmed relevant Apple Metal counter APIs/clock-correlation requirements from current public Apple documentation. Committed all source changes and read back all **13** changed source/docs files. **53/53 static source-presence checks** passed, including source clock correlation, capability gates, counter invalid handling, queue separation, unique handle reporting, per-view timing-source expiration, host wiring and UI labels. Extra static ordering checks confirmed Scene/Game beginGpuScope precedes GPU render and endGpuScope follows last viewport pass.

**NOT RUN:** native Metal Obj-C++ build, Metal header/API availability warning checks, CMake, CTest, macOS/Apple Silicon/iOS GPU runtime, real scope counters, clock-frequency calibration experiments, GPU completion/fence stress, GL33 scoped queries and benchmark arbitration, memory-profiling/VRAM pressure tests, visual screenshots, device-loss/low-memory tests and CI. Static source checks are **not** a compile pass. Do NOT claim any measured per-view time, measured savings or production readiness.

## Concrete Stage 48 next work / risks

1. First verify Stage 47 `GfxMetal.mm` with the real native compiler, Apple SDK and an actual GPU supporting timestamp counters. Verify `MTLCounterSamplingPointAtBlitBoundary` and `MTLCounterSamplingPointAtStageBoundary` availability, the Obj-C++ counter descriptor API, stage boundary behavior of blit-only sample passes, valid `MTLCounterResultTimestamp` conversion, and no unguarded availability warnings.
2. Validate GL33 scoped query priority/benchmark exclusion, 64-item saturation, abandoned sample handling, hiding/showing tabs, multiple view contexts, incomplete scope fallback, and timing-source domain switching with controlled latency.
3. Measure Stage 47 Renderer3D owned bytes against GPU Instruments and RHI texture/buffer metadata; do not add external model/light/geometry memory until each owner reports unique allocations and no duplicate handles. Report unknown/unestimated GPU bytes explicitly.
4. Add Metal/Vulkan per-effect/pass GPU scopes only with proven counter sampling support and avoid nested/overlapping elapsed queries. Both can increase profiling overhead and must remain off by default.
5. Stage 45–47 adaptive resolution remains experimental; run image parity and FPS/VRAM quality at 1.0/0.75/0.625/0.5 scales and GPU over/under-budget simulations on actual devices.
6. If a new Stage 48 source feature is implemented, add **separate** `docs/handoffs/prisma-stage-48.md` with exact SHA/files, *actual* build and test outcomes and next step. Draft PR #26 stays open; do not merge `main` without user permission.

## How another agent pulls this exact branch on macOS

```bash
git fetch origin
git switch feature/prisma-postfx
git pull --ff-only origin feature/prisma-postfx
```
