# PRISMA Stage 44 — opt-in internal-resolution rendering and full-output HDR temporal upscaling

**Date:** 2026-11-19.
**Branch:** `feature/prisma-postfx`.
**Draft PR:** https://github.com/chromealex/AIVERSE/pull/26.
**Previous handoff:** `docs/handoffs/prisma-stage-43.md`.
**Main:** unchanged; no merges from unrelated feature branches.
**Code/docs HEAD before this handoff commit:** `267532451b07ee94b5a55f180355ed967c0c1112`. This handoff itself is committed separately as the stage's last change. All work used connected GitHub operations; no native checkout was available.

## Implemented changes and exact commits

1. `2b271c7f9decdeb4f70568de54801831638d0d3e` — `engine/render/PrismaData.cpp`: new optional `temporalResolve` params `renderScale` (0.5..1.0, default 1.0) and `reconstructionSharpness` (0..0.5, default 0.15). Existing `postfx` schema/IDs stay stable.
2. `f480459291ea0eafa86934b6391ec239e30120f3` — `engine/render/Prisma.h`: `Prisma::renderSizeForOutput(outputWidth,outputHeight)`, per-camera history source dimensions and `PrismaStats` output/upscale diagnostics.
3. `032f8459506d3381b2ee3b58d4e44dee5213b122` — `engine/render/Prisma.cpp`: explicit opt-in low-resolution 3D size selection with High/HDR and validated temporal pipeline.
4. `3d20859311270426b0a86743a878f71da1e10a59` — `Prisma.h`: remember output dimensions by view ID for render targets without available RHI metadata.
5. `9e6dc80ea6729f7550ad728eee3358a447226e38` — `Prisma.cpp`: save per-view output dimensions and clear them on view retirement/shutdown.
6. `02786c733bab358ddac2c805c16210c411b6035b` — `Prisma.cpp`: determine final output dimensions from target texture metadata, explicit per-view host size or actual swapchain. Keep `PostFrame.width/height` as the scene's internal render size.
7. `da76cf6c04964358aa2c22a06166fa21cff4469e` — `Prisma.cpp`: move temporal resolution from early HDR passes to immediately **before** final HDR color composite, after internal-sized motion blur, DoF, bloom and shafts processing. Temporal history color/depth now allocates at output dimensions; a change to internal OR output dimensions invalidates old history and fence-retires owned textures. Move motion history finalization after temporal reconstruction.
8. `7a2cc5cb46dbd5faf894ad8e1b024fc4588bc24d` — `Prisma.cpp`: introduce distinct `inputPx` native-source texel uniforms in the temporal shader (13 `vec4` fields total) and use output-size viewport/composite/optional AA targets.
9. `3ce80e3047b66cfc2a5ab70db331e281803c10f4` — `engine/render/PrismaShaders.h`: 3×3 color neighborhood sampled at **internal** texel size, previous-depth-validated history at **output** texel size, mild compressed-HDR bounded spatial sharpening before temporal history confidence.
10. `d216afb62b53ae26c0785db2b371a05ab70b9b58` — `Prisma.cpp`: bind `reconstructionSharpness` and sample the signed motion texture with nearest filtering.
11. `3ebcc8b7cd42b01c62ab45edb80bb718962a3283` — `apps/editor/Editor.cpp`: editor Scene/Game render **world/FX** at internal resolution while offscreen viewport output, picking, ImGui and overlays retain full dimensions; apply jitter in internal pixel coordinates.
12. `25aabf8155a1f504ba98f9d66965067a2d87c48c` — `apps/client/GameClient.cpp`: same split for native GameClient, and pass internal dimensions to FX projection/update while retaining the unjittered camera for UI and input.
13. `483e5a9d6ac122a9e6602a91ae66e318190efe4e` — `Prisma.h`: direct `<utility>` include and clearer internal-vs-output API preconditions.
14. `371a65c16fe9bf9acd40bb843dea1d70187fad96` — `apps/editor/windows/PrismaWindow.cpp`: show `render WxH -> output WxH` and whether temporal upscaling actually ran.
15. `aaf418c28de8bd14c684abcbd1d1eeffb47d30f5` — `Prisma.cpp`: quantize `renderScale` to 1/16 steps to avoid continuous output reallocations under smoothly blended camera volume settings.
16. `0bba921b8e55e0bc35f3fa48eea6cdf8c1362093` — `PrismaData.cpp`: split-size workload heuristic for cost reports. This is **not** a measured performance number.
17. `a106f55899e3da9af18f96296266fec196494f67` — `skills/visuals/aiverse-prisma/SKILL.md`: native agent integration contract and strict opt-in/quality limitations.
18. `baf3cfb8ac1454d5bd89de7f9aac787755acde4a` and `267532451b07ee94b5a55f180355ed967c0c1112` — `docs/prisma.md`: Stage 44 architecture, GPU lifetime, fallback and final static verification status.
19. This document: separate `docs/handoffs/prisma-stage-44.md` commit after code, skill and design documentation.

## Native API / runtime sequencing

```cpp
prisma.setViewId(cameraId); // stable presentation view ID
prisma.applyCameraVolumes(cameraVolumes, originalCamera.eye());
const auto [renderW, renderH] = prisma.renderSizeForOutput(outputW, outputH);
const auto renderCamera = prisma.jitteredCamera(originalCamera, renderW, renderH);
renderer.beginScene(renderCamera, renderW, renderH);
drawWorldAndSceneFx();                 // world, particles, scene depth at internal res
renderer.render(fullSizeOutputPass, &prisma);
drawReactiveSceneFxIfAny();
renderer.resolvePrisma();              // output-resolution temporal + final composite
prisma.clearCameraVolumes();
// Use originalCamera and outputW/outputH for HUD, picking and input.
```

**Activation:** explicit `temporalResolve` effect, nonzero `historyWeight`/`jitterPixels`, `renderScale<1`, High quality and HDR support, plus **all three** environment flags set to `1`:
`AIVERSE_PRISMA_TEMPORAL`, `AIVERSE_PRISMA_JITTER` and `AIVERSE_PRISMA_UPSCALE`. Without these the host uses the original full-resolution 3D render. Scale is quantized to 1/16; render sizes are rounded and bounded. This is a fixed authorable resolution ratio, NOT automatic dynamic resolution.

**RHI ownership:** the Scene pass and its depth, camera/object velocity, Motion Blur, DoF, Bloom and sun-shafts execute in `renderW × renderH`. `Renderer3D::beginScene` carries these dimensions in `PostFrame`; the final output texture/full framebuffer is still owned by the original host. `Prisma::resolve` derives output dimensions from `Device::textureInfo(out.colorTarget)`, from the explicit host declaration keyed by view ID, or from the swapchain framebuffer dimensions. The late temporal pass receives source RGB/depth/velocity in internal-size UV coordinates but writes the history at `outputW × outputH`, with matching output-size scratch depth attachment. It does not sample the previous history target while writing it. Color history RGB is linear HDR, A stores hardware depth; color clipping and per-tap depth validation reuse the Stage 43 policy. Signed motion vectors remain normalized current-minus-previous screen UV, so source/output pixel differences are handled through distinct reciprocal texel uniforms rather than multiplying vectors by an arbitrary ratio. `Prisma::retireView`/GPU-fenced retirement remains responsible for prior histories.

**Uniform fields:** `TemporalUniforms` is 13 contiguous `float[4]` fields (`px,p0,sun,inv0..inv3,prev0..prev3,jitter,inputPx`); GLSL std140 and Metal float4 structures use the same field ordering. `px.xy` are **output** texels, `inputPx.xy` are **source** texels, `inputPx.z` the spatial sharpening amount. The older MotionVectors UBO remains unchanged. Nearest velocity sampling reduces blending unrelated object motions across silhouettes. The final composite viewport, pixel-size uniforms and its optional FXAA framebuffer use **output** width/height.

**Pass order:** internal velocity → internal Motion Blur → internal DoF/other HDR effects → internal Bloom/shafts → **full-output temporal reconstruction** → full-output color composite/tonemap → full-output HUD. On temporal failure, the final composite samples the low-res current RGB with ordinary bilinear enlargement and still fills the output target. The application remains readable, but a jittered failed frame can still wobble until next-frame recovery. Need native allocation preflight if making this ship-safe.

**Scope:** genre independent PRISMA presentation ownership. No RTS/FPS simulation, authoritative ECS, input camera transformations, `postfx` schema version, SpectraFX emitter simulation, GPU-fence semantics or default-on image changes. `PrismaScreenLighting` remains a separate legacy optional temporal path; do not simultaneously activate it.

## Verification actually completed

**Performed:** current-branch GitHub source reads before modifying Renderer3D/PostProcess, Prisma, editor Scene/Game and GameClient; commits to code/docs; re-fetch of nine affected files; **39/39 static source-presence checks** covering new authored params, opt-in gates, 1/16 scale quantization, output metadata, per-view history invalidation, 13-field shader UBO, distinct history and input texels, full-size output viewport, editor/client low-resolution beginScene, FX dimension forwarding and updated agent documentation. Source-order inspection confirmed Motion Blur → DoF → Bloom → temporal history → composite. These are only **structural checks**, not compiled/ran tests.

**NOT RUN** under the user's ongoing features-before-tests instruction: CMake configure, C++ or Obj-C++ compilation, shader GLSL/MSL compilation, CTest, game/editor smoke, GL/Metal/GLES/Vulkan/WebGL device execution, image/PSNR/ghosting quality tests, GPU memory/driver-loss, performance measurements, CI. No fps improvement, runtime GPU safety or production readiness can be claimed.

## Known risks and concrete Stage 45 next work

1. **Highest priority:** implement pre-render capability/allocation preflight (including sized output history/scratch depth) or a guaranteed stable unjittered fallback if the temporal pass fails after a low-resolution jittered scene was already drawn. Current fallback presents a bilinear-upscaled scene but may have one-frame jitter wobble.
2. Measure and potentially correct projection-origin/Y-flip conventions on Metal/GL/Vulkan with a checkerboard and translating camera. Motion vectors in normalized UV should remain consistent across internal/output sizes; verify rigid/deformed velocity and reactive FX alignment at boundaries. Do not claim universal correctness without GPU images.
3. Add a bounded optional **adaptive resolution policy** with hysteresis only when trustworthy GPU timing/budget information is available; the current `renderScale` is fixed/author-driven and quantized to 1/16. Avoid per-frame scale oscillation and unbounded retirement queues.
4. Advanced anti-ghosting still needs object-ID/material confidence, depth/normal disocclusion and transparent-surface coverage where the renderer can provide trustworthy data; current nearest motion + depth/luma acceptance is heuristic.
5. Low-resolution bloom/sun-shafts and other screen-space passes are blended at full-output composite resolution; check their look when output is much larger than internal pixels. `PrismaScreenLighting` remains independent; handle double temporal registration intentionally before wider adoption.
6. `PrismaStats` exposes internal/output dimensions and successful upscaling. `MemoryStats` estimates output-size history bytes per camera but excludes exact physical VRAM/driver overhead and some owner bundles.
7. Before merging, compile editor, client and HDR targets; run CTest and Metal, GL, GLES, Vulkan GPU snapshots at scales 1, 0.75, 0.67, 0.5 across camera rotations, zoom, cross-viewport resize, motion, transparency, FX, HDR exposure and device-loss. Use truthfully recorded PASS/FAIL.
8. Stage 45 MUST have its own `docs/handoffs/prisma-stage-45.md` with SHA, files, APIs, actually run tests and next actionable step. **Never merge the draft PR into main without explicit instruction.**
