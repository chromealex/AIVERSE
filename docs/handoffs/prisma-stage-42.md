# PRISMA Stage 42 — optional HDR temporal color reprojection and shared motion pipeline

**Date:** 2026-11-19
**Branch:** `feature/prisma-postfx`
**Draft PR:** https://github.com/chromealex/AIVERSE/pull/26
**Previous handoff:** `docs/handoffs/prisma-stage-41.md`
**Main:** untouched. No merges from unrelated feature branches. Changes were committed by the GitHub connector (not a native checkout).

## Committed implementation — exact source commits

1. `d8c39cbb304f240f99cf310c237871e8240215fa` — `engine/render/PrismaData.h`: append `PrismaType::TemporalResolve` **after** existing effects, preserving their enumerator values.
2. `49a48001988c4d9721a3194db9e8ac21ffd2f088` — `engine/render/PrismaData.cpp`: normal editor/MCP/volume `temporalResolve` effect, parameters `historyWeight`, `depthThreshold`, `motionScale`, `reactiveStrength`, high-tier cost estimate.
3. `006757a74026d612eff896e01e3485ebe3bb2d89` — `engine/render/PrismaShaders.h`: common GLSL/MSL linear-HDR temporal shader and color compression helpers, 3×3 neighborhood, four **individually depth-validated nearest** history taps, reactive/motion-weight reduction.
4. `8d451abffe7e3e6b05c1ecd13f26f8c40590dd85` — `engine/render/Prisma.h`: per-view ping-pong `TemporalView`, program slot, retirement counters and per-view GPU memory stats.
5. `6b9a77cae75d8f2ab074908f706a95e448b77cf6` — `engine/render/Prisma.cpp`: compile new temporal program with 5 texture bindings and appropriate depth sampler.
6. `edcce6f2a54b7ba204a700b3e83571853747c028` — `engine/render/Prisma.cpp`: retire temporal histories with `retireView`, release at GPU-idle shutdown, invalidate on explicit motion cuts/quality/settings changes.
7. `fb7aee7999dd421394d016edb0dabb21ad62cef5` — `engine/render/Prisma.cpp`: share renderer object motion request for opt-in temporal and motion blur, without altering authoritative gameplay ECS.
8. `ae22f2282f4f1e7a6de07206b29ba867884befeb` — `engine/render/Prisma.cpp`: one velocity pass; TAA output into camera-private HDR history before optional Motion Blur, which consumes the resolved color; omit final HDR FXAA if temporal succeeded.
9. `785cb6c6d4aa591ba64dd1a6c30addf914bbff22` — `engine/render/Prisma.cpp`: include attributed/unattributed temporal histories in lower-bound active GPU memory telemetry.
10. `b77c4d59647e964288d7ab352db60737b5a0a298` — `apps/editor/windows/StatsWindow.cpp`: display temporal view count, private history texture count and known MiB in per-view/GPU-resident diagnostics.
11. `ddcda4c353f00b90a3e712201ea93f11ed637336` — `engine/render/Prisma.h`: record last presentation camera origin for automatic-cut detection.
12. `d275c7948c1828dd794ce66dd14eace989ba90e8` — `engine/render/Prisma.cpp`: invalidate temporal color and camera history after a large (>12 world-unit) per-frame translation or non-finite camera delta, in addition to explicit cut hooks.
13. `f94bc5b033a017e5d74c4bf9c2274ba37904a428` — `skills/visuals/aiverse-prisma/SKILL.md`: add native configuration and correct outdated unimplemented-feature status.
14. `8dab2bada9eca7a97029c5553256e62dff493bab` — `engine/render/Prisma.cpp`: report unreferenced temporal history with one RHI metadata walk.
15. `8eafe22928ec5bb27a6d66481b0a66c086fa7e10` — `docs/prisma.md`: Stage 42 pipeline, JSON example, memory/lifetime rules and limitations.
16. **This handoff** — `docs/handoffs/prisma-stage-42.md`, committed as a separate final documentation commit after source/docs.

## Runtime API, data, and exact behavior

A configured effect alone **does not** enable temporal accumulation. A host must set `AIVERSE_PRISMA_TEMPORAL=1`, have an active `temporalResolve` effect in `game/map["postfx"]["effects"]`, and render HDR at High tier. Existing clients without those prerequisites keep the old render path. Example:

```json
{"postfx":{"effects":[{"type":"temporalResolve",
"minQuality":"high","historyWeight":0.86,
"depthThreshold":0.0075,"motionScale":0.04,
"reactiveStrength":0.95}]}}
```

The data/MCP/editor API is the existing `parsePrisma` / `PrismaType` / `prismaCatalog` / `postfx_*` path, not new externally incompatible JSON. `PrismaVolumeStack` can author the same catalog parameters; there are no FPS/RTS dependencies. `Prisma::resetMotionHistory(viewId)` invalidates the temporal history for an explicit camera cut, and `Prisma::retireView(viewId)` fence-retires private history textures using the existing submitted-GPU fence logic.

Pipeline in `Prisma::resolve`:
1. Render linear HDR scene into existing `Sized` color/depth targets (unchanged).
2. If either opt-in temporal resolve or motion blur requires a previous frame, compute **one** full-screen signed camera-motion vector target, with optional validated native object/deformed velocity from `Renderer3D`. Borrow object `MotionTextureInput` and current-frame `reactiveMask` only until this resolve.
3. Temporal pass, if enabled, writes to alternating `viewId`-private **RGBA16F** history targets (RGB = resolved HDR, A = current hardware depth). The shader rejects invalid/out-of-screen history, uses current/previous VP for expected previous device depth, individually validates four previous depth taps, clips accepted color to a 3×3 current RGB neighborhood in a luminance-compressed space, reduces history weight for large motion/reactive transparency, and seeds a sharp frame on missing history. History target is **never** sampled while attached for write; sampled scene depth is not attached for depth writes (scratch depth is used).
4. Optional Motion Blur now reads the temporal output if both effects are active, and reuses the previously generated velocity pass rather than rendering velocity a second time. The rest of Bloom, color grading, HDR tonemap and HUD order remains intact. HDR FXAA is **not applied** when temporal resolve succeeded in this frame.
5. Store the current camera view-projection and origin for the next frame. A large teleport (>12 presentation-world units) invalidates motion/temporal history even without an explicit reset. Resolution changes retire old history targets behind GPU fences. Editor Stats shows active temporal history bytes separately from size-shared RenderGraph memory.

`Prisma::wantsObjectMotionVectors()` and `objectMotionContext` now return a useful request when either eligible effect is enabled. The Renderer3D bridge still owns all object motion targets and sends a borrowed handle with metadata; Prisma does not destroy it. This does **not** change deterministic ECS, simulation ticks, physics, particle simulation, or the SpectraFX ownership boundary.

## Actual verification

**Performed:** read connected-GitHub source from Stage 41, `Prisma.h/.cpp`, `PrismaData.h/.cpp`, `PrismaShaders.h`, `PrismaScreenLighting`, `Renderer3D`, `PrismaMotionTextureInput`, StatsWindow and agent skill; implemented commits; re-fetched all eight changed source/docs paths. A static 18-point source-presence check passed, confirming the catalog, HDR shader, output/history ownership, fence retirement, view-specific GPU-memory stats, effect opt-in, shared velocity, temporal-before-Motion Blur order, HDR FXAA exclusion and updated docs. Additional source count confirmed exactly **one** `setPipeline(pipes_[kMotionVectors])` call in `Prisma::resolve`. These are structural checks, **NOT** executable tests.

**NOT RUN (user's ongoing features-first direction):** CMake/native C++/Obj-C++ compile, CTest, actual GLSL/MSL shader compile, Metal/GL/GLES/Vulkan/WebGL smoke, GPU renderbench, image/screenshot diff, ghosting/silhouette/dynamic light measurements, memory pressure/VRAM, device-loss tests, CI. This stage is not build-verified or shipping-certified. Do not mark PASS for any unexecuted test.

## Limitations and precise Stage 43 handoff

1. This is **full-resolution temporal color stabilization, not temporal upscaling**. No dynamic resolution, jittered subpixel reconstruction, DLSS/FSR/TSR replacement, optical-flow estimation, explicit thin-transparency depth G-buffer or multi-sample anti-aliasing parity.
2. Prior history alpha stores *device* depth, not a full world-position/linear-depth G-buffer. Individual history texel validation reduces edge ghosting, but fast deformations/disocclusions may still shimmer or reject too much history. Sky/empty depth is intentionally not accumulated and can alias while rotating the camera. Material reactive masks and renderer motion-vector accuracy are limited by their producers.
3. Auto-cut threshold is a heuristic in presentation units. Fast world travel may reset temporal history often; small FOV switches may not trigger it. A production camera should explicitly call `resetMotionHistory(viewId)` whenever its projection changes discontinuously.
4. Temporal full-resolution history is **two RGBA16F textures per active camera** plus the optional size-shared velocity target and scratch depth; these add nontrivial VRAM. `MemoryStats::temporalHistoryKnownBytes` is a *lower bound*, excludes physical driver padding/mip effects and does not sum repeated shared size-cache bytes.
5. Existing optional `PrismaScreenLighting` has its own different TAA history. Do not turn its temporal filter on concurrently with Stage 42 temporal resolve; an explicit central controller/feature routing policy would be needed to unify them. Stage 42 adds to canonical `Prisma` but does not migrate that legacy optional screen-lighting shader.
6. Native build/test next: compile `aiverse_editor`, `aiverse_game_view`, HDR target(s), client and CTest; verify GfxGL/GLES and GfxMetal shader compilation with 5 samplers and the sampled-depth binding. Then measure captured output for moving actors, moving transparent emitters, camera rotations/teleports and duplicate Scene/Game resolutions. Check exact renderer GPU fence lifetimes and exception paths on allocation failure.
7. **Stage 43 feature-first options:** implement explicit jittered subpixel sampling and optional history confidence/reprojection quality controls, unify the old `PrismaScreenLighting` path with canonical temporal resolve, or add a GPU-free validator for temporal shader flags/profile combinations. Avoid enabling visuals globally without quality/hardware proof.
8. After every subsequent feature stage, commit `docs/handoffs/prisma-stage-43.md` (and later stage-specific files) with changed paths, SHA, API, actual test status and the next concrete step. Do **not** merge draft PR #26 into main without user direction.
