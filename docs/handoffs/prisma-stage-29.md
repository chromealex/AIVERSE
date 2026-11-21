# PRISMA Stage 29 — motion history epochs, translucent reactive masks, baked+morph velocities

**Date:** 2026-11-19.
**Working branch:** `feature/prisma-postfx`.
**PR:** https://github.com/chromealex/AIVERSE/pull/26
**Previous handoff:** `docs/handoffs/prisma-stage-28.md`
**Upstream:** no merge into `main` and no other feature branches imported during this stage.

## Work completed, file changes and commits

1. `9a48328bbabccbb16c38210fd01d7367d5ad7fb4` — `engine/render/Renderer3D.h`: add optional `PostProcess::objectMotionEpoch(viewId)`, `Renderer3D::resetObjectMotionHistory(viewId)`, `resetAllObjectMotionHistory()`, and CPU `ObjectMotionStats`. Each object's per-camera motion history tracks the observed epoch and whether it was primed.
2. `5a9318206e234bcc84dc6b029580a146cb3cd086` — `engine/render/Prisma.h`: per-view cut-epoch state and override for the generic postprocess bridge.
3. `a5171d5f1cba3fab03f6d1c992b6167f9d552744` — `engine/render/Prisma.cpp`: camera-cut reset increments the motion epoch; the renderer can notice even if it has independent previous-object history.
4. `79a23f11e5ab118fe0d45e4b3303b837ec63ddbc` — `engine/render/Prisma.cpp`: avoid interpreting previous-resolution stats as current resolution before `resolve()`; renderer checks dimensions before submitting object velocity.
5. `1df1ece26a3835c9fe82ce99a7f2854859d2b2aa` — `engine/render/Renderer3D.cpp`: actually reset previous rigid/deform pose maps when the camera epoch changes, prime history on initial/invalid frames; expose manual reset methods and velocity diagnostics.
6. `fd61a136deeb1dfd9caddf7766332b80bac9f182` — `engine/render/Renderer3D.cpp`: increment skinned and baked draw counters; explicitly record whether an object-velocity pass was submitted.
7. `d2cdfa31312989c2eaa7eaad93954c7e380a5c9e` — new `engine/render/Renderer3DReactiveShaders.h`: conservative RGBA8 reactive silhouette shader for translucent rigid ModelItems, with sampled scene depth for visibility and separate scratch depth.
8. `a4b7d3fc65d79f5d18ec196374e8bcc66832bc13` — `engine/render/Renderer3D.h`: generic optional borrowed `PostProcess::acceptMotionReactiveMask`, per-view mask target and shader/pipeline ownership.
9. `36f5afc1be78be1bdeef27f5ba880854fa3b220a` and `bfd6e9e17bd72d6118c4cb639ddb0009dea1425f` — `engine/render/Prisma.h/.cpp`: optional `setMotionReactiveMask(viewId,texture)` for custom native renderers, borrowed texture bridge to motion blur. Reset clears borrowed mask as well as object velocity. Resolve clears the borrow after its frame.
10. `c804efc862c253acaaaf01f3e894a00f6c3a67aa` — `engine/render/Renderer3D.cpp`: allocate/reuse per-view RGBA8 mask, lazily compile reactive pipeline, render visible translucent rigid geometry coverage into it after object velocity, and supply it before `Prisma::resolve`. Release mask at shutdown and handle viewport resize.
11. `4eb40459d5f4a5d4b750b59d4b1fddaf9275f7a3` — `engine/render/PrismaData.cpp`: `motionBlur.reactiveStrength` optional normalized parameter, default 0.85.
12. `a6950f94f0615b1dae13e9b94373bbeedd16b6ef` — `engine/render/PrismaShaders.h`: motion blur takes `TEX3` reactive R coverage and locally reduces the shutter-blur strength.
13. `5776043b66ad7437bb207cdd369feb38b51897ae` — `engine/render/Prisma.cpp`: bind mask at slot 3, preserve black fallback when absent, enlarge motion blur sampler set to four and pass `reactiveStrength` uniform.
14. `00e1e756aff2f347fef22eede313624be4b149c5` — `engine/render/Renderer3DMotionShaders.h`: baked-clip vertex velocity now handles **both** current/prior morph delta weights and current/prior baked bone-atlas rows, applying morph before skinning to match existing model color shader.
15. `9de22faec10ea01998f831c4cda7f0fb637ee422` — `engine/render/Renderer3D.cpp`: allow baked+morph eligibility and bind U5 morph weights/delta parameters and texture8 for baked velocity.
16. `2fd453d310c248b3f01cad3e9d9095d3e11a12f4` — `engine/render/Renderer3DMotionShaders.h`: fix a legacy literal `\\n` embedded in Metal source comment that previously commented out a UV declaration.
17. `0bb9effc499ee5bb26aacacaba8dc964b8332f7f` and `3d358aafc404265080018e5dedd39f6c064d25ea` — `engine/render/Renderer3D.h/.cpp`: explicit `releaseObjectMotionViewAfterGpuIdle(viewId)` API and per-view target/depth/reactive cleanup; the caller MUST guarantee the GPU has finished referencing the targets, not infer it from CPU frame count.
18. `d72aca7d48328ada086775f42ca19369de37d0a9` — `engine/render/Renderer3D.cpp`: conservatively exclude dithered LOD transition geometry from velocity coverage until the LOD-fade shader is faithfully replayed.
19. `2194d2ee85051f7db53d1175b272e4274dc164de` — `docs/prisma.md`: Stage 29 architecture, API, reactive and mixed baked/morph pipeline limitations.

## API, ownership and behavior

No ECS/genre-specific modules, serialization format migration or SpectraFX changes. MotionBlur remains an experimental PRISMA effect with `AIVERSE_PRISMA_MOTION_BLUR=1`, HDR (signed RGBA16F), high quality by default. Existing JSON `motionBlur` gains optional `reactiveStrength` (0..1, default 0.85).

The two coupled camera histories are now synchronized through a monotonically increasing presentation-only cut epoch: `Prisma::resetMotionHistory(viewId)` invalidates its reprojection camera and increments `MotionView.cutEpoch`; `PostProcess::objectMotionEpoch(viewId)` exposes the generation; `Renderer3D::renderObjectMotionVectors` clears prior rigid and deformable world/pose snapshots when that epoch differs. A view's first motion frame seeds history without a GPU velocity submission. `Renderer3D::resetObjectMotionHistory(viewId)` and `resetAllObjectMotionHistory()` also permit direct standalone invalidation. `objectMotionStats()` reports last submitted CPU-side draw counts (rigid instances/draws, masked, skinned, baked, reactive, rejected) but NOT GPU timings.

When an eligible translucent rigid `ModelItem` is visible, Renderer3D renders a conservative reactive overlay to an RGBA8 texture. An object may lack stable persistentId for reactive coverage (current-frame mask only), but must have valid rigid geometry. Source scene depth is sampled, never attached for writing. The overlay passes a borrowed handle to PRISMA via `acceptMotionReactiveMask` and the motion blur gather reads slot 3, attenuating blur per pixel. `setMotionReactiveMask(id,texture)` allows native renderers to provide their own mask; the last mask supplied before resolve wins. PRISMA clears this borrowed value after resolve.

The baked GPU shader now applies morph targets using current and previous weights to the local vertex first, then separately samples the baked animation atlas for the current and previous clip rows and blends. The prior pose is keyed by stable persistentId, node and primitive, including model identity and animation snapshot. This remains presentation-only. Missing/mismatched prior state skips the override.

`releaseObjectMotionViewAfterGpuIdle(viewId)` is available for explicit viewport shutdown. It destroys only the Renderer3D-owned velocity/depth/reactive targets and erases CPU histories after confirmed GPU idle. It does NOT release PRISMA Auto Exposure 1x1 histories, whose GPU lifetime remains managed separately. No automatic GPU-fence completion API exists in Gfx yet.

## Exact verification status

**Performed:** inspected the GitHub branch's Renderer3D model shaders, GPU resource interfaces, PRISMA motion blur, prior handoffs and changed code through GitHub connector; authored the shaders and committed C++ and docs on the feature branch. Source-level readback was performed for several changed methods.

**NOT RUN** per user's explicit features-first direction: native C++ compiler, CMake, CTest, Metal/GL/GLES shader builds, actual 3D editor/game render, screenshots, image quality comparison, hardware profiling and CI. No passing tests or verified pixels are claimed.

## Constraints / unfinished functionality

- Reactive pass is conservative silhouette coverage for translucent **rigid ModelDraw** geometry; not per-pixel correct blended texture alpha. It excludes skinned, baked, morph, custom ShaderGraph displacement, water, particles and immediate-mode transparent meshes. False positives can reduce motion blur inside some transparent geometry.
- Mask cutoff for masked material from Stage 28 remains separate from the new reactive pass. True cutout material parity and dithered LOD transition replay are not complete; transitions are skipped rather than generating wrong velocity.
- Baked+morph GPU shader and native RHI slot binding are committed but unverified on Metal/GLES. No SKINNED instancing with previous palettes or morph delta history buffers beyond existing per-draw uniforms.
- The camera/object velocity and reactive textures are still released on resize/shutdown through preexisting RHI device destruction assumptions. The explicit per-view retirement API is safe **only** after GPU idle; automatic completed-fence retirement remains unimplemented.
- Renderer3D and Prisma store per-view histories independently; a native client should call the PRISMA camera reset for cuts (which now signals the renderer via epoch). Other manually supplied native buffers are borrowed, not owned.
- The reactive shader uses approximate depth rejection; translucent occlusion, custom materials and transparent layering are not perfectly reproduced. Current motion blur depth rejection still works in nonlinear hardware depth units, not world metres.

## Next feature stage (Stage 30, tests remain deferred)

1. Add a presentation-only reactive coverage provider for skinned/animated transparent actors, water/particles/custom renderers via a generic callback, while preserving existing pipeline order and accurate material alpha where possible. Avoid touching ECS and avoid forcing all renderers to support it.
2. Improve true LOD-dither parity and depth-aware motion-vector selection, including disoccluded silhouette handling and stricter GPU-format/size validation of externally supplied velocity/reactive textures.
3. Add automatic per-view resource retirement when a future Gfx GPU-completed-fence API exists, and safe idle-backed closure for Scene/Game view lifetimes. Do not release via CPU frame serial.
4. Consider full GPU instanced deformation velocity for many skinned models with previous pose palette buffers, after profiling.
5. When user requests tests, run targeted build, shader validation and Metal/GL scenes; document actual PASS/FAIL. Until then leave experimental paths off by default.
6. Record separate `docs/handoffs/prisma-stage-30.md` after each new stage with code commits, API, actual tests, risk and next precise step. Do not merge PR #26 into main before validation.
