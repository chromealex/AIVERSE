# PRISMA Stage 28 — baked animation and alpha-cutout object motion

Date: 2026-11-19.
Branch: `feature/prisma-postfx`. PR: https://github.com/chromealex/AIVERSE/pull/26.
Previous stage: `docs/handoffs/prisma-stage-27.md`.
Main remains unchanged; no additional feature branches were merged.

## Implemented files and commit history

- `1d63eec22dd8e72e1e2a7c611ddfba546a49d833` — `engine/render/Renderer3D.h`: retain baked-clip parameters and baked-mode marker per previous deformable primitive; reserve separate baked shader and pipeline handles.
- `252272d6e2d5ef8addc2152c443b58be4cf18fda` — `engine/render/Renderer3DMotionShaders.h`: GLSL and Metal vertex shader variants reconstruct both current and previous baked positions from immutable animation atlas `tex7`, joint palette U3 and current/previous clip row/blend U4. Screen motion from previous/current MVP.
- `82e220d5184235c953771816a8186a0ac456d106` — `engine/render/Renderer3D.cpp`: capture baked clip pose history per persistent model+node+primitive, lazily create baked GPU pipeline, render eligible opaque baked motion into RGBA16F object-velocity target, and release shader/pipeline on shutdown. Baked+morph combined deformation remains excluded.
- `5751cb703e5a9093891731c013335de7d1828ae9` — `engine/render/Renderer3DMotionShaders.h`: alpha-cutout rigid velocity GLSL and Metal variants with base alpha texture and material UV transform/cutoff.
- `3b63afac0a7575e228f357b9314e429c12f18dee` — `engine/render/Renderer3D.h`: alpha-cutout motion shader/pipeline handles.
- `0472f954f517801a15d7c28544c554c9d2ca272a` — `engine/render/Renderer3D.cpp`: allow eligible rigid alpha-mask items to enter motion history, exclude them from opaque instancing, and render dedicated cutout velocity pass with source material alpha threshold, UV transform, optional vertex alpha and source texture.
- `79fd32b2c0a1724db48dbde1f7360527f3b44b43` and `07eb41d8739a877adc66ec234066fb33bdeba657` — rigid history now checks source Model identity, guarding reused object IDs.
- `9cd04921911fc5d07f725a05cee1d726809ef6ca` — `engine/render/PrismaData.cpp`: optional MotionBlur `depthThreshold` parameter, 0.012 default in device-depth units (range 0.0005..0.05).
- `10074e00003d8d2d5681c82d1a26f759437d2437` — `engine/render/PrismaShaders.h`: depth discontinuity filter now uses configurable threshold; all-samples-rejected fallback returns original sharp source rather than zero/black.
- `a2f3a5a8a3e9d42b7616573f3b573aaa5f9cfcc3` — `engine/render/Prisma.cpp`: send depthThreshold to motion-blur shader through otherwise-unused `SimpleU.sun.x`.
- `0520363461c3e572648735002cbddd42cb95ed61` — `docs/prisma.md`: Stage 28 architecture and JSON config.

## API / integration and scope

The existing config type `motionBlur` and opt-in `AIVERSE_PRISMA_MOTION_BLUR=1` remain stable, HDR/High min-quality requirements unchanged. JSON optional `depthThreshold` is handled by the existing data-driven PRISMA editor/MCP parsing and defaults to 0.012.

The renderer's object velocity target uses signed RG for current-minus-previous UV and B=1 for covered pixels; it samples scene depth for approximate visibility rejection without writing original scene depth. A scene pass must finish before the overlay. The baked GPU shader uses clip rows and blend weights from the existing draw-item `anim` field and the same baked atlas/palette semantics as `ModelShaders.h`. It saves previous animation state per stable `(persistentId,nodeIndex,geometryPrimitive)`, without changing ECS or authoritative simulation. A newly visible or replaced model without valid prior state is not assigned invented previous baked motion.

Alpha-mask rigid materials replay base alpha from texture, texture offset/scale/rotation, base alpha and vertex alpha when enabled. They are drawn separately from opaque velocity instancing because masked fragments must discard. Fragment blending and complex ShaderGraph displacement are not covered.

## Actual validation

Performed: inspected Renderer3D model draw items, GPU skinned/baked shader conventions, PRISMA shader/cost data through the connected GitHub repo; wrote source/header/docs and this handoff to the feature branch via GitHub connector.

**NOT RUN (user explicitly requested features first):** native C++ compilation, CMake, CTest, Metal/GL shader compilation, runtime/editor visual smoke, screenshots, performance measurement, CI. Do not report a successful build or parity based on source commits alone.

## Known limitations / risks

- Baked animation + morph deformation on the same primitive is excluded. Baked sprite/geometry ShaderGraph vertex displacement and masked *skinned* variants are excluded.
- Alpha-cutout masking currently reproduces base alpha cutoff and simple UV transform; it may not exactly match team-color, dynamic material, LOD dithering and other feature permutations. Material alpha parity on real hardware is unverified.
- Depth occlusion uses a fixed screen depth epsilon. The new `depthThreshold` is in nonlinear hardware depth, not physical metres, and does not produce physically exact near/far layer separation.
- Object velocity overlay is a presentation-only extra pass. Memory ownership, RHI completed-GPU fence lifetime safety, and Metal/GLES shader correctness remain unverified.
- Renderer3D rigid/baked history identity relies on stable `persistentId` and source Model pointer; a new Model object allocated at the same address may require an asset generation ID for perfect reload handling.
- Existing experimental render stage may have unrelated issues introduced in prior stages; native validation and follow-up fixes are still required.
- No automatic new UI toggle; enabled only when the existing MotionBlur effect, opt-in env var, HDR and supported device all allow it.

## Next implementation stage (Stage 29)

1. Add presentation-only velocity availability/diagnostics and explicit camera-cut invalidation for renderer object histories, independent from PRISMA exposure history.
2. Implement exact opaque/alpha-mask parity for LOD fade, material-based deformations, and transparent/reactive pixel masks as separate safe variants.
3. Optionally add baked+morph mixed shader variant and skinned motion instancing with GPU palette buffer support.
4. Later, when user allows tests: native CMake/CTest; GLSL/MSL compile, animated skin/baked/morph scenes, masked foliage, identical geometry instancing, shadow/terrain occlusion, camera cut/resize, and depth threshold visual comparisons. Record actual outcomes.
5. Commit `docs/handoffs/prisma-stage-29.md` after next feature stage; keep tests marked NOT RUN until executed. No implicit merge to main.
