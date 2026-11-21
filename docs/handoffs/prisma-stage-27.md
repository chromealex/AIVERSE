# PRISMA Stage 27 — instanced rigid + dual-pose skin/morph velocity

Date: 2026-11-19. Working branch: `feature/prisma-postfx`. Feature PR: https://github.com/chromealex/AIVERSE/pull/26.
Previous: `docs/handoffs/prisma-stage-26.md`. Main was not merged or modified in this stage.

## Changes / commits

1. `720e50d7a13884be6b063eccf4f0113347efefa3` — `engine/render/PrismaShaders.h`: the motion-blur sample gather now checks full-resolution scene depth at each sample and rejects large depth discontinuities; reduces edge bleeding.
2. `4a1902b167722cb591d8996fe47ed59c1420e48c` — `engine/render/Prisma.cpp`: pass scene depth texture to blur shader, use 3 sampled texture slots, and emit the native MSL depth2d<float> sampler for texture slots marked as depth. Same generator fix applies to the Stage 22 Depth of Field and Stage 25 camera-motion reconstruction pipelines. No changes to non-depth postprocess samplers.
3. `1faab99ad54071c176070200c9643d1fdf310f8c` — `engine/render/Renderer3DMotionShaders.h`: new GPU instancing velocity program for up to 16 rigid models sharing exact geometry; each instance has an independent current/previous MVP matrix.
4. `ed54b25ca5ebf72b82c678e94c2654bb1ba2d854` — `engine/render/Renderer3D.h`: persistent pipeline/shader handles for instanced velocity rendering.
5. `79fbf971c68c8b445550194f8a30d69e439009a5` — `engine/render/Renderer3D.cpp`: group motion-eligible rigid draws by exact source model and index range; draw as 16-instance chunks where possible, fall back to existing single-object pipeline on singleton/failed batch shader; release extra shader/pipeline at shutdown.
6. `84a786e7d864d4124ee7ba6ecd6e0c40f6514ded` — `engine/render/Renderer3D.h`: independent previous deform snapshot per (persistentId,nodeIndex,geometryPrimitive), storing previous 80-joint palette (3x4 matrices), 8 morph weights, world transform and source-model identity.
7. `f2324068bce18a87d2183310d0a9ff8e12998608` — `engine/render/Renderer3DMotionShaders.h`: dual-pose skin/morph GPU vertex velocity shader (GLSL and Metal). GPU samples immutable morph delta texture for current and previous weights; evaluates current/previous joint palettes in separate shader buffers.
8. `62086c3d545ca0aef821ec1ff7de46c7eeb61eeb` — `engine/render/Renderer3D.cpp`: snapshot deform state every eligible frame and prune absent entries automatically by swapping current map; skip missing/reused model/ID history and unsupported material/animation paths.
9. `ee0ed519bd9ee7732da3eb7172e7c3fbf297c012` — `engine/render/Renderer3D.cpp`: dedicated deformable-velocity pipeline, render skinned/morph primitives using separate U3/U4 prior/current palettes and U5 morph weights; keep opaque scene-depth occlusion sampling and a separate scratch depth attachment.
10. `c2d7c2db599aa3b0829f70f4673bce22c8d8bbe2` and `3ef09e2947859d0b4c4cb15eb048f9c8597a1508` — source-model identity guard on deformed history; reset velocity overlay and re-seed state when viewport dimensions change.
11. `1b442a4ef0d4e7bc7f565db0cae91f759b3332e6` — `docs/prisma.md`: architecture, constraints, public behavior, verification status.

## Runtime path / API

All additions are rendering-only and genre independent. The optional postfx configuration is unchanged:
```json
{"type":"motionBlur","intensity":0.65,"maxPixels":20,"samples":8,"shutterScale":1}
```
MotionBlur still requires `AIVERSE_PRISMA_MOTION_BLUR=1`, HDR RGBA16F, and the configured PRISMA quality. Renderer3D only creates object velocity when PostProcess::wantsObjectMotionVectors() is true. It uses the Stage 26 PostProcess bridge to pass borrowed velocity texture with signed normalized screen-space RG and B=1 on valid object pixels. PRISMA applies camera reprojection elsewhere. No ECS systems/components or authoritative gameplay updates.

Rigid instances reuse the ordinary per-ModelItem transform identity, even when color rendering uses batching/MDI. Batched motion uses a dedicated RHI indexed instanced shader; maximum 16 instances per draw fit the portable 4096-byte per-draw uniform block. Matching criteria use source Model pointer + firstIndex + indexCount. A single independent rigid draw still uses the previous singleton pipeline.

Deformed primitives use previous **joint palette** and previous **morph weights**: current/previous vertex positions are evaluated on GPU from ModelVertex, existing morph atlas, and two separate palette uniform arrays. The previous model transforms and deformation are keyed by stable entity/node/primitive identity. Missing prior frames or changed model assets do not produce fake velocity for that primitive.

The depth-aware color gather samples the current scene depth at every blur tap. The native Metal shader builder now knows when one of the post-process sampler slots represents depth2d<float>. All experimental results still require shader compile and GPU image validation.

## Exclusions / technical limitations

- Baked GPU animation from clip texture does not use jointData_ palettes and is still excluded. Full baked velocity requires passing previous clip rows and blends as another GPU shader permutation.
- Custom ShaderGraph vertex offsets, procedurally displaced geometry, translucency/alpha-masked materials, sprite geometry, and custom renderers remain excluded from the deformable overlay.
- Deformed draw path is per-primitive, not instanced; rigid path is now batched. Full skinned instancing would require packing previous pose data and a GPU palette buffer.
- Motion blur is a current-frame spatial gather; no temporal color history, reactive mask, TAA or true per-layer depth separation.
- The depth-rejection threshold uses non-linear device depth, not meters. A robust linear-view-depth contract and bilateral weighting need backend-specific work. The source RHI texture formats and sample origins need real GPU validation.
- GPU resource destruction on resize/shutdown still follows existing renderer assumptions. Completed GPU fence signaling and a safe retirement mechanism are not yet wired for these overlay textures.
- History maps are presentation state; stable ModelDraw::persistentId must be supplied by the producer for individual moving actors/attachments.

## Actual verification (do not report as PASS)

Performed: fetched current Renderer3D, ModelShaders, Prisma shaders/renderer, and prior handoff through GitHub connector; committed changes to branch and updated design documentation.
**NOT RUN:** native compiler, build, CMake, CTest, Metal/GL/GLES shader compilation, render smoke, screenshots, CI and performance profiling. User requested features first and tests later. Compilation and visual correctness are therefore unverified.

## Next features: Stage 28

1. Add baked-clip motion history (previous clip A/B rows, blend fraction) and a GPU baked-velocity variant reading bone rows from the existing baked animation atlas. Do not approximate deformation as rigid.
2. Add material-correct cutout/alpha-tested motion pipeline, camera-view cut lifecycle and opaque geometry coverage parity.
3. Consider GPU palette buffers for instanced skinned velocity when real performance measurements justify the implementation.
4. Improve depth discontinuity handling with linearized view depth, motion-vector dilation and foreground/background separation; integrate generated vectors with temporal upscaling later.
5. Keep a separate `docs/handoffs/prisma-stage-28.md` after next feature stage with changed files, exact commit SHA and genuine TESTS NOT RUN status until user requests verification. Do not merge PR #26 to main before GPU/native validation.
