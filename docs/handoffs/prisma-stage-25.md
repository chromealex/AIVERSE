# PRISMA Stage 25 — Camera Motion Vectors + HDR Motion Blur

Date: 2026-11-19. Branch: feature/prisma-postfx. PR: https://github.com/chromealex/AIVERSE/pull/26. Previous handoff: docs/handoffs/prisma-stage-24.md. Main unchanged.

## Changes and commits

- 4f0c88f114cb5e73f54118d43e7d01b9983d71fa — engine/render/PrismaData.h: new MotionBlur effect enum.
- 5002d8eaed7b235d666e6b2b276245cae080c187 — engine/render/PrismaData.cpp: configurable intensity (0..1), maxPixels (0..64), samples (2..16), shutterScale (0..2); High tier, Heavy cost.
- c87f4d878ea36c3bc0b7736a701261f68c13ed10 — engine/render/PrismaShaders.h: camera reprojection shader and directional current-color shutter blur; optional object velocity override.
- f8c7f8c8cea1534ed01a0c0ccee84d9c07f5678a — engine/render/Prisma.h: per-camera motion history and public view reset/object velocity API.
- 98de280cc498729c6ebddaadd9748e12fa116515 — engine/render/Prisma.cpp: shader pipelines, matrix uniforms, reset and cleanup.
- 67ec49b4a1be64c855ce5fea6818e0d7544bffb7 — engine/render/Prisma.cpp: HDR full-resolution velocity and blur passes before DoF/FXAA/Bloom; separate scratch depth attachment.
- 4959e12df7963acfca9a6636387462f98cb0201f — engine/render/Prisma.h: reset motion history on quality and postprocess enable changes.
- 009709479f9d9e5318e727a17b6ff1080094503c — engine/render/Prisma.cpp: bind optional third sampler only for vector pipeline.
- 3bbd51a518cec1cdf4ecf12ca920e7592bb2d021 — docs/prisma.md: use and limitations.

## API, behavior and compatibility

Effect JSON: {"type":"motionBlur","intensity":0.65,"maxPixels":20,"samples":8,"shutterScale":1}. Runtime gate AIVERSE_PRISMA_MOTION_BLUR=1; linear HDR RGBA16F required for signed UV velocity; High minQuality default. Off by default. Motion vectors are current-minus-previous normalized screen UV, computed from depth and inverse current and previous viewProjection matrices. The next pass gathers CURRENT frame color along velocity and clamps displacement to maxPixels. No temporal color accumulation. History matrices/size/valid bit are keyed by stable viewId. First frame or explicit camera cut uses no blur and seeds the next frame; call resetMotionHistory(viewId) on cuts/teleports.

Native renderer may supply setObjectMotionVectors(viewId, velocityTexture) each frame, with RG signed current-minus-previous normalized UV displacement and B>0.5 validity mask. Borrowed handle cleared each resolve. This is an interface only: the renderer is NOT yet generating object velocity for rigid/skinned/instanced geometry, and dimension/format validation is the host's responsibility. This module is genre-independent presentation-only and does not mutate ECS or SpectraFX. No changes to existing external postfx JSON key or postfx_* MCP names.

## Actual validation

Performed: GitHub connector source inspection, code writes, and documentation. NOT RUN: compiler, CMake, CTest, Metal/GL/GLES, screenshots, visual parity, GPU profiling, CI or editor smoke, because user requested feature implementation first. Do not claim passing tests or production-grade behavior.

## Limitations / next

Camera-only velocity supported automatically; optional object velocity input is not automatic mesh velocity generation. HDR-only; depth sampling and hardware depth convention require native GPU verification. No occlusion-aware blur around disocclusions, no accurate skinned/morph velocity, no reactive mask or TAA. The camera cut reset must be called explicitly. Existing RHI resource-retirement limitations persist.

Stage 26: implement presentation-only previous-world transforms keyed by stable render identities, depth-tested per-object velocity pass and integration with the optional motion-vector input, without changing deterministic simulation. Handle camera cuts, missing IDs, transparency, instancing, previous poses and camera/view isolation. Keep a separate docs/handoffs/prisma-stage-26.md after code changes. Run tests only once user requests.
