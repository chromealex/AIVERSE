# PRISMA Stage 43 — opt-in Halton jitter and confidence-aware native-resolution temporal AA

**Date:** 2026-11-19.
**Working branch:** `feature/prisma-postfx`.
**Draft PR:** https://github.com/chromealex/AIVERSE/pull/26
**Previous handoff:** `docs/handoffs/prisma-stage-42.md`.
**Main:** unchanged. No merging other agents' feature branches; changes made directly on this feature branch via connected GitHub file commits.

## Actual completed changes — exact commits

1. `116b4a60d9fbf6ec5ed54b838f236c622084b78f` — `engine/render/PrismaData.cpp`: append `jitterPixels` (0..0.5, default 0.5) and `historyConfidence` (0..1, default 0.65) to existing `temporalResolve` catalogue. The normal editor/MCP/volume parameter parser owns these; no new JSON root or effect ID.
2. `dbd92f1953adb790e0d45412db59d3c646643d68` — `engine/render/Prisma.h`: public presentation-only `RTSCamera jitteredCamera(RTSCamera,int width,int height)` plus private per-view current/previous jitter UV and eight-phase sequence.
3. `0b3ffaa91a90687eb90f11926edac99d7b3577e4` — `Prisma.h`: reset temporal history on jitter enable/amplitude changes.
4. `f1bf0d2e31e2d5e47030c787d6378b80b39350e5` — `Prisma.cpp`: Halton(2,3) NDC jitter for a **copy** of the camera, independently keyed by view ID; requires HDR High, enabled `temporalResolve`, `AIVERSE_PRISMA_TEMPORAL=1`, `AIVERSE_PRISMA_JITTER=1` and no existing camera jitter. It performs no gameplay/ECS writes or GPU passes.
5. `30b4f148057d09a59f1b53fd9bf50c81e78364cf` — `engine/render/PrismaShaders.h`: stable-history temporal color resolve body shared between GLSL/MSL; samples current at stableUV+currentJitter, previous history at reprojectedUV-previousJitter. Computes 3×3 local color mean/variance and depth discontinuity, applies per-tap previous-depth rejection, neighborhood clipping, history color/depth confidence, motion excluding jitter and reactive mask. History RGB stays linear HDR, A stores previous hardware depth.
6. `95f06681ac0a588452f5102c04dd18bd102c6b8b` — `engine/render/Prisma.cpp`: extend only the temporal UBO with a 12th `jitter` vec4, leaving the existing 11-vec4 motion pipeline layout unchanged. Bind history confidence in `sun.w`; after successful resolve store last jitter for that specific camera.
7. `b61cf6b19c32cec2e1920a9e83a799a63e4c57ed` — `apps/editor/Editor.cpp`: configure view ID/volumes *before* `Renderer3D::beginScene`, then render with jittered copy for editor Scene/Game; original `RTSCamera` retained for picking, tools, UI, overlays, zoom and orientation. Debug/disabled Scene post path does not jitter.
8. `77da6f1a7c5f8faa49dc35605040652588ef9725` — `apps/client/GameClient.cpp`: same pre-render ordering and opt-in camera copy for native GameClient view ID 3; original camera remains authoritative for user interactions.
9. `f75fa785ed5f7ed179b62ecc6d653c097d349cd9` — `Prisma.h`: store projection signature (ortho/perspective, FOV, near/far planes) for compatibility.
10. `16a303d6ba5b33015d2c8a1144956bf2065a61cb` — `Prisma.cpp`: on discontinuous projection changes, invalidate previous object velocity and temporal histories using existing `resetMotionHistory(viewId)`; don't reuse stale samples.
11. `b8a587bbceecdf9b5282687e13873e405a44dbe1` — `Prisma.h`: `PrismaStats` gains last-frame booleans `temporalResolved`, `temporalJittered`, and jitter offset in pixels.
12. `5ec609bc289a8a7ac95ee3b88d4bd95887dbd2d5` — `Prisma.cpp`: set those frame stats after a successful temporal output (reset on the next resolve).
13. `297872f6cddb68c0b7359cb1c80ca5683bfdf261` — `apps/editor/windows/PrismaWindow.cpp`: show temporal resolved/fallback state and last subpixel jitter in the existing PRISMA postprocess panel.
14. `ecca97c98e7de58c041ac90d8073323b578bf52b` — `skills/visuals/aiverse-prisma/SKILL.md`: Stage 43 API, opt-in, owner boundaries, limitations and validation requirements.
15. `031703599ff83569dad26dd7958bd9fd47467f3a` — `docs/prisma.md`: Stage 43 architecture and native-resolution JSON/renderer handoff requirements.
16. `952a2b7b5fae8f83243a6d1afa12fba497236029` — `Prisma.cpp`: preflight temporal GPU pipeline before applying jitter; if shader/pipeline creation fails, keep the unjittered camera to prevent output wobble on an unsupported backend.
17. **This file** — `docs/handoffs/prisma-stage-43.md`: separately committed after implementation and source-level checks.

## API and sequencing

Existing canonical `Prisma` flow still owns postprocessing, not the renderer or gameplay ECS. To use the new native-resolution jitter feature in a native renderer, call:

```cpp
prisma.setViewId(stableViewId);
prisma.applyCameraVolumes(volumes, originalCamera.eye());
render::RTSCamera renderCam = prisma.jitteredCamera(originalCamera, width, height);
renderer.beginScene(renderCam, width, height);
// Render game world; keep originalCamera for picking and UI.
renderer.render(pass, &prisma);
drawVfxBeforePostprocessing();
renderer.resolvePrisma();
prisma.clearCameraVolumes();
```

**Activation:** author an enabled `temporalResolve` effect in `postfx.effects`; run Prisma High/HDR; set `AIVERSE_PRISMA_TEMPORAL=1` and `AIVERSE_PRISMA_JITTER=1`. If either flag is absent, no new subpixel jitter is performed. Stage 42 temporal resolve can remain enabled without jitter. The two numeric parameters participate in existing `prismaCatalog`, `postfx_*`/MCP, camera-volume interpolation and editor effect sliders. `RTSCamera::jitter` already implements an NDC projection offset in both perspective and ortho; the renderer strips jitter from the shadow-camera. Nothing genre-specific is added to the generic Prisma RHI pipeline.

**History semantics:** the per-view `TemporalView` owns its own two RGB/A HDR histories (Stage 42). It additionally tracks the current jitter and the previous successfully resolved jitter in *unflipped* UV coordinates (NDC/2). The current scene is rasterized with a subpixel shift; the temporal pass writes a stable output grid by fetching current color/depth/velocity/reactive at `stableUV + currentJitter`. A motion vector gives current-minus-previous *jittered projection* UV, so the previous history lookup subtracts `previousJitter`. Previous jitter is removed from the computed motion magnitude, avoiding false motion rejection on stationary pixels. The shader increases rejection for old colors that disagree with the current local color variance or cross a current depth silhouette. Four nearest history taps are separately compared against expected projected prior depth before reconstruction (no interpolated foreground/background depth). Camera cuts, large translations, projection/FOV/clip changes and jitter enable/amplitude changes invalidate color history before it is blended.

**Two separate temporal paths remain:** `PrismaScreenLighting` is an older optional HDR/screen-lighting temporal resolver. It is NOT merged into `Prisma::resolve()`; never use both temporal histories simultaneously. Full-resolution Halton TAA here does not upsample or change render target dimensions. Renderer2D/UI and gameplay input use the original camera; the shifted copy is for 3D presentation only.

**GPU resources:** jitter adds no render target: the existing Stage 42 velocity and two camera-private HDR history textures remain. The 12th UBO field is only for the temporal program. No new GPU waits occur on frame paths. Existing GPU-fenced history retirement and Stage 41 memory diagnostics remain authoritative. Stage 42 shared motion vectors and Motion Blur are untouched.

## Actual verification and limits

**Performed:** inspected connected GitHub current Stage 42 branch, RTSCamera projection implementation, Renderer3D beginScene/shadow-camera behavior, editor Scene/Game calls, native GameClient call order and shader uniforms; committed changes to the feature branch, then re-fetched all nine source/docs files. **26 of 26 structural source presence checks passed**: required declarations, opt-in gates, Halton phase, camera/projection invalidation, 12 temporal fields, binding of jitter/history confidence, history update, shader sampling/reprojection, camera render/overlay separation, editor diagnostics and docs. A second source inspection confirmed exactly one `kMotionVectors` pipeline draw call, and that client and editor jitter are applied before `Renderer3D::beginScene`. These checks confirm *source text* only, NOT build correctness, GPU performance or rendered image parity.

**NOT RUN** (features before tests): CMake configure, C++ and Obj-C++ native build, shader compiler, CTest, Metal/GL/GLES/Vulkan GPU execution, image quality/temporal shimmer benchmarking, multiple-view scene parity, UI picking and input screenshots, memory pressure, device loss, CI. Do NOT claim a PASS or production image-quality improvement for those.

## Risks and next Stage 44

1. The temporal shader is a **native-resolution** implementation, not a temporal upscaler. For Stage 44, add separate internal render-resolution/output-resolution contracts, confidence-based spatial reconstruction, motion vector coordinate transformations, and dynamic-resolution quality controls; do not pretend this Stage 43 is FSR/TSR.
2. The shader and `RTSCamera` jitter sign/Y-flip conventions must be verified on real Metal, GL and Vulkan images, especially after camera rotation, zoom and screen orientation changes. `TUV` handles texture-origin flip; the `jitter` uniform remains in projected unflipped screen UV. Validate that prior image remains stable rather than alternately shifting by one pixel.
3. 3D selection and gizmo input deliberately use unjittered camera; visuals are jittered by fractions of a pixel. Confirm exact editor picking, highlight/selection outline and transparent particle reactive mask alignment with actual captured screenshots.
4. If temporal shader/pipeline creation fails, the preflight returns an unjittered copy; if later HDR/history render-target allocation fails within an encoded frame, the original Stage 42 fallback may briefly render a jittered frame. A production-safe implementation would need a pre-render allocation capability guarantee or an explicit full-resolution resolve fallback.
5. An abrupt clip-plane/FOV change resets motion and color histories; very frequent automatic clip adjustment might reset the temporal sequence often. Investigate backend camera projection cadence before changing thresholds.
6. Current `historyConfidence` is a heuristic based on 3×3 color variance, depth discontinuity and per-tap reprojection depth; it is not an object-ID/material-confidence pass. Fast deforming models, transparent occlusions, foliage and hair need real GPU evidence and may require reactive/velocity producer improvements.
7. `PrismaScreenLighting` has another temporal implementation. A future stage may consolidate ownership or reject double registration explicitly, but must preserve existing legacy behavior until hardware tests pass.
8. When user permits validation: build editor, GameClient and tests, verify front/back silhouettes, thin lines/foliage, moving occluders, rapid camera cuts, ortho/perspective, zoom/FOV, HDR highlights, reactive particles, dual Scene/Game sizes, GPU fence status and shader compilation across Metal/GL/GLES/Vulkan. Capture identical jitter on/off image sequences and compare aliasing/ghosting/PSNR.
9. **Every next stage requires a separate handoff** `docs/handoffs/prisma-stage-44.md` with exact SHA, actual tests, limitations and next actionable step. Never merge PR #26 into main without explicit user approval.
