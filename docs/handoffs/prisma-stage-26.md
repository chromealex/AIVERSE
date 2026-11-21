# PRISMA Stage 26 — automatic rigid ModelDraw motion velocity

**Date:** 2026-11-19. **Branch:** `feature/prisma-postfx`; draft PR https://github.com/chromealex/AIVERSE/pull/26.
**Previous:** `docs/handoffs/prisma-stage-25.md`. **Main:** unchanged; no other branch merges.

## What was implemented / commits

- `90f50e5eb7b2b23665369312659d7bbb6acdffae` — `engine/render/Renderer3D.h`: optional PostProcess camera-motion hook, Renderer3D per-view object motion target and previous rigid-node transforms, ModelItem stable ID/node information.
- `e7897b4b0854e013c22e13a6678307c97007123a` — `engine/render/Prisma.h`: PRISMA overrides for previous camera matrix and borrowed velocity texture bridge.
- `cbde8a0d625ff87911fbd7a885f4ab878e49a5a7` — new `engine/render/Renderer3DMotionShaders.h`: GLSL+MSL rigid object velocity overlay using current and previous MVP and scene depth sampling. RG = signed screen UV displacement; B = valid object pixel.
- `f1722700f6e53a19d0d760f55335021615740f5c` — `engine/render/Renderer3DModels.cpp`: populate ModelItem.motionPersistentId and motionNodeIndex for every source ModelDraw.
- `8b6391f7a92f65823a667a888fdec7219b38bf39` — `engine/render/Renderer3D.h`: lazy PostProcess request and current scene-depth bridge, so no extra GPU pass if MotionBlur is disabled.
- `a3350149d963b95a35946edb3e664258e1e8538d` — `engine/render/Prisma.h`: source scene depth handle and callbacks.
- `0a2a5e589d4be11f22a2045db6dda16911409ea4` — `engine/render/Prisma.cpp`: wantsObjectMotionVectors() / current depth and prior matrix access for active HDR motionBlur. New renderer pass is driven by generic PostProcess interface.
- `1c855923589900e7edcdc2342e6ff7dcf150abcc` and `84d2128121d937ee2712b07bf2b71cb340a78988` — `engine/render/Renderer3DMotionShaders.h`: Metal coordinate/depth sampler fixes; sample scene depth as depth2d<float>.
- `9400c2336e9686f48e5936d1460745aa1b37a576` — `engine/render/Renderer3D.cpp`: automatic per-view GPU velocity overlay between the scene pass and Prisma::resolve(), separate scratch depth attachment and RGBA16F color target, depth occlusion test, lazy dedicated pipeline, per-node prior transform lookup and history snapshot. Releases targets and pipeline on renderer shutdown.
- `eb854bb852978df57b81ec777f3a7cb840b275e8` — `docs/prisma.md`: public behavior, scope and limitations.

## Architecture

The app still calls Renderer3D::render(...,&prisma) and Renderer3D::resolvePrisma(). No game/FPS/RTS/ECS changes required. If PRISMA declares active HDR MotionBlur via `AIVERSE_PRISMA_MOTION_BLUR=1`, PostProcess::wantsObjectMotionVectors() returns true. At scene resolve, Renderer3D ends the scene render pass and optionally calls renderObjectMotionVectors(*post), then forwards its texture via PostProcess::acceptObjectMotionVectors. Prisma already accepts this in Stage 25 and uses it to override camera-only velocity where B > 0.5. Renderer3D skips GPU work when the feature is disabled.

Previous rigid-node transforms are stored by `(persistentId,nodeIndex)` inside a map indexed by PRISMA's stable viewId. The current per-camera previous viewProjection is obtained via the same PostProcess interface. No authoritative ECS data is changed. With a missing prior camera frame, Renderer3D only seeds the previous-node map. On later frames it draws eligible visible opaque rigid primitives using the existing ModelVertex vertex/index buffers and their own transforms, even if the normal color pass used instancing. Output is RGBA16F with signed normalized UV velocity. The fragment samples scene depth and discards objects occluded by other scene geometry; scratch depth is separate to avoid source depth feedback. The result is a borrowed texture for this resolve only.

Only `ModelDraw`s with nonzero stable `persistentId` participate. For motion correctness each independently moving attachment must have its own ID; transient draws get camera velocity only. Exclude skinned/baked/morph/deformed, alpha mask/blend, transparent, custom geometry, invisible and no-identity primitives until a specialized replay is implemented.

## Actual verification

**Performed:** read Renderer3D and model batching code, Prisma API, Gfx and shaders through GitHub connector; implemented and committed source/shader/docs files. Checked the repository's established MSL convention uses depth2d<float> and adapted the new source accordingly.

**NOT RUN** (user directed features before tests): CMake, native C++ compilation, CTest, Metal/GL/GLES shader compilers, screenshots, benchmarks, CI and editor smoke. Do not claim native correctness, visual parity, memory savings or ready-for-release status.

## Known risks

- The velocity pass renders per primitive without GPU instancing/MDI: works from batched ModelItems but has additional draw calls. It is opt-in and only active when the full HDR blur is enabled.
- Alpha masking, double-sided materials, custom geometry, skinned/morphed/baked animation and transparent surfaces are excluded. Some opaque material-batch geometry may not precisely match the source material pass; verify pixel coverage.
- The sampled scene-depth rejection uses an approximate depth epsilon, not an exact depth-equal test. Disocclusions, slope and transparency can cause edge artifacts. GL and Metal UV conventions are coded but still unverified.
- The per-view render target is recreated upon resize and released on renderer shutdown using current legacy RHI assumptions; generic GPU completion fences are not yet wired. Never assume CPU frame number is a safe GPU-complete fence.
- No object history budget/pruning for removed IDs outside active views yet; per-view prior map is rebuilt on demand and preserved only for currently eligible nodes.
- Camera cuts should call `Prisma::resetMotionHistory(viewId)`. Next frame skips blur and seeds world transforms before re-enabling velocity overlay.
- GPU object motion is a separate presentation state; do not change game ECS scheduling or deterministic replay.

## Next feature stage (Stage 27, tests deferred)

1. Add dedicated velocity pipelines for skinned/baked and morph-deformed models, requiring prior joint palette and morph weights, not rigid-transform approximation.
2. Add an optional buffer for deformable custom geometry, instanced/indirect GPU velocity batching and better material alpha-mask parity.
3. Expand motion blur with depth-aware neighbor sampling, foreground/background separation, camera-cut heuristics and reactive masks; retain HDR/SDR policy.
4. Add safe per-view GPU resource retirement via backend completion fence or explicit GPU idle guarantee. Explicitly measure GPU draw overhead when allowed.
5. After the next code stage, commit separate `docs/handoffs/prisma-stage-27.md` with changed files, exact commits, tests actually run (NOT RUN until requested), limitations and next steps.
