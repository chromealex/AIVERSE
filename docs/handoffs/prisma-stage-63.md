# PRISMA Stage 63 — alpha-cutout/dissolve reactive masks and callback lifecycle

Date: 2026-11-19
Repository: chromealex/AIVERSE
Working branch: feature/prisma-postfx
Draft PR: https://github.com/chromealex/AIVERSE/pull/26
Base: feature/universal-entity-inspector-fps
Previous stage: docs/handoffs/prisma-stage-62.md
Verified pre-handoff branch HEAD: 8c92f530d270b4c28d87311e0472f79d5b481b96
Main unchanged. Never merge without explicit user authorization.
User sequencing: complete bounded functionality, THEN run full native
CMake/CTest, GPU image/alpha, performance and fence validation/fixes.

## Scoped outcome

Renderer3D's independent current-frame reactive mask (Stage 62) now
supports rigid alpha-cutout leaves/grilles and object-space dissolve
without wrongly covering their discarded pixels. It preserves the
original RGBA8 water/simple translucent path. An additional animated
skin/baked coverage uniform accurately reproduces 4x4 screen-space
LOD fade, and dissolving animated meshes can submit conservative
current-pose coverage. Reactive callback lifecycle/failed delivery
is now explicitly accounted for, without storing callbacks across
camera frames.

This does NOT change the borrowed scene depth, stable-view-ID
ownership, canonical Prisma HDR color/temporal history or SpectraFX.
It is genre-independent and does not mutate simulation/ECS state.

## Exact code commits and modified files

1. `c506d2e8291e0fc0dd53a2e6a3876746c6467ce2` —
   `engine/render/Renderer3DReactiveShaders.h`: introduce
   `kCutoutVS/kCutoutFS/kCutoutMSL`. Rigid-model shader
   reproduces actual material UV offset/scale/rotation,
   base-texture alpha * material baseColor alpha, optional
   vertex color alpha, `AlphaMode::Mask` cutoff, 4x4 LOD
   dither and the same object-space
   `modelNoise3(opos*7)*0.7 + modelNoise3(opos*19)*0.3`
   dissolve rule as `ModelShaders.h`. Scene depth is sampled
   read-only; output is only current-frame reactive coverage.
2. `f55180d9dfce1e6402fd402ee2f8bf1e25eca4ef` —
   `engine/render/Renderer3D.h`: dedicated
   `objectMotionReactiveCutoutShader_/Pipeline_`, as well as
   `ObjectMotionStats.cutoutReactiveDraws` and
   `dissolveReactiveDraws`.
3. `d221e33697443e515f1eac6e208c0f899a7ff7b6` —
   `engine/render/Renderer3D.cpp`: include alpha-mask and
   dissolving rigid models in current reactive eligibility.
   Prepare cutout pipeline with actual `ModelVertex` attributes
   0 position, 2 UV, 4 UByte4N color and a 144-byte
   `ReactiveCutoutUniforms` payload. Bind material base
   image/UV transform/alpha cutoff and dissolve amount.
   Keep the existing simple rigid, water, animated and
   callback paths unchanged. Own and destroy new
   shader/pipeline alongside existing renderer resources at
   the host-confirmed GPU-idle shutdown point.
4. `9732c7acbbbb99c76cbba2b32c9f7fc636cc671b` —
   `Renderer3D.h`: add
   `ObjectMotionStats.reactiveCallbacksDropped` beside
   existing current-frame callback invocation counts.
5. `a7a48d88cfcf335201ca03a981b9593d5a068215` —
   `Renderer3D.cpp`: require the right pipeline for
   each current reactive source (simple rigid, exact rigid
   cutout/dissolve, skin, baked, mesh or custom callback).
   Avoid publishing an empty mask just because an unrelated
   pipeline happened to be prepared. Count dropped callbacks
   on unavailable post, scene depth or reactive pass,
   while preserving the per-scene callback clearing contract.
6. `481c12621e0b3a4830feaaeeb3abf26e52cad297` —
   `Renderer3DReactiveShaders.h`: add GLSL U1/Metal
   buffer(2) exact 4x4 LOD dither to the current-pose
   animated reactive fragment. Shared motion/deformation
   vertex transforms are unchanged.
7. `b68aced18093be7ad65e9d7c76b1505115924b94` —
   `Renderer3D.cpp`: include animated skin/baked LOD fade
   and partial dissolve as reactive sources, supply U1 LOD
   uniforms, count animated transition submissions.
   Dissolve is deliberately CONSERVATIVE for animated meshes:
   it does not reproduce deforming model-space discard noise.
8. `89ce9a0bfebb1430cc09f543b5414187568e43a4` —
   `Renderer3D.cpp`: account for actual Material team-alpha
   rules: `TeamMode::BaseAlpha` ignores texture alpha at
   cutoff, and `TeamMode::VertexAlpha` does not apply
   vertex alpha twice. Retain vertex-occlusion exceptions.
9. `122b406b539bc5342ea243142ac0780a2fcc8bc3` —
   `Renderer3DReactiveShaders.h`: update ownership,
   supported rigid alpha/dissolve and limited animated
   coverage comments.
10. `d454194e2519e69eeadab02bfc606c7d81c147ce` —
    `Renderer3D.cpp`: distinguish skin/baked provider
    needs during pipeline preflight. A cached skinned
    pipeline must not cause the renderer to publish a
    blank reactive mask for a baked-only scene whose
    matching pipeline failed.
11. `68b4f3b656ed1984595b0db3d94a8506d10c8a28` —
    `docs/prisma.md`: full Stage 63 source/API,
    shader behavior, known limits, callback ownership
    and explicit unrun tests.
12. `8c92f530d270b4c28d87311e0472f79d5b481b96` —
    `skills/visuals/aiverse-prisma/SKILL.md`: Stage 63
    continuation contract and safe genre-agnostic use.

This handoff is committed SEPARATELY after the feature code,
architecture docs and skill, as required by the AIVERSE agent policy.
Fetch the latest branch HEAD after this handoff commit.

## Invariants and host-facing API

Renderer3D invokes `renderCurrentFrameReactiveMask` after
the optional object velocity pass but BEFORE
`Prisma::resolve`. Alpha-cutout and dissolve eligibility
is independent of previous camera motion; the rigid
shader uses CURRENT geometry and the same deterministic
fragment-discard conditions as the main model shader.
An untextured simple reactive shader still handles ordinary
translucent models and water meshes. Only a successfully
encoded reactive pass publishes a typed borrowed
`MotionTextureInput::declared(...,RGBA8)` to Prisma.

The rigid-specific `ReactiveCutoutUniforms` has:
```
float mvp[16], params[4], lod[4], uv0[4], uv1[4], maskInfo[4];
```
It is 144 bytes, well within the RHI U0 uniform limit.
Mask shader input TEX0 = borrowed scene depth;
TEX1 = model base-color alpha (or renderer white texture
for materials that ignore base texture alpha). The shader
reads the scene depth and writes only to the renderer-owned
reactive RGBA8 target with a separate private Depth
attachment, never to borrowed scene depth. Shader output
coverage combines via existing Screen blend.

The animated reactive fragment adds U1 `reactiveLod` and
corresponding MSL buffer(2); the existing skin/baked
deformation vertex stage still uses current transforms for
both positions and produces only coverage. LOD dither
matches the normal model's 4x4 screen-space pattern.
For animated dissolve the mask is intentionally
conservative, so alpha-cutout/dissolve pixel parity is
NOT claimed for animated/skinned/baked or Shader Graph
vertex-displaced objects. Actor Reveal aperture also
remains outside this exact mask path.

`Renderer3D::addMotionReactiveDraw` is still registered
after `beginScene` and before `resolvePrisma`.
Callbacks run only inside an existing current-frame mask
pass and must encode their own draws without
beginPass/endPass; producer GPU resources must outlive
their submission. The callback list is cleared on resolve,
on a no-post path, and at the next scene start. The new
`reactiveCallbacksDropped` CPU metric counts callback
invocations that could not be attempted because the
post hook, scene depth or necessary mask pipeline/target
was unavailable. It does NOT count GPU-pixel correctness.
`reactiveCallbacks` counts attempted invocations, not
verified draw count; similarly the rigid/animated
reactiveDraw counters represent submitted draw calls.

All camera-private velocity/depth/reactive render targets
retain existing GPU-fence retirement and stable
presentation `viewId`. No new temporal history or
simulation state has been added. Renderer3D creates
one extra lazy GPU shader and pipeline only when
a cutout/dissolve rigid reactive source is present.

## Actually executed verification

- Fetched current PR/head and Stage 62 handoff before work.
- Read actual `Renderer3D` current-frame masks, native
  reactive callbacks and motion history, original
  `Renderer3DReactiveShaders` and MotionShaders,
  `ModelShaders` alpha/LOD/dissolve fragments,
  `Renderer3DModels` material UV/alpha binding,
  `Model.h` layout/TeamMode and camera status APIs.
- After source edits, read back
  `Renderer3D.cpp`, `Renderer3D.h`,
  `Renderer3DReactiveShaders.h`,
  `ModelShaders.h` and `Model.h` from connected
  GitHub. **51/51 source-level structural/binding,
  error-path and ownership checks passed**. Also
  checked lexical brace balance of renderer/reactive
  shader/header source; all balanced.
- These are static checks; no native compiler or
  GPU backend was actually run.

**NOT RUN** per user's feature-first instruction:
native CMake/clang/Obj-C++ build, GLSL/MSL/SPIR-V
shader compile, CTest, `./run-tests.sh`, editor/client
startup, actual alpha-cutout and dissolve GPU readback,
cross-RHI Metal/GL/GLES/Vulkan/WebGL pixel parity,
multi-camera, LOD/dissolve lifetime and callback
stress tests, GPU fence validation, benchmarks and CI.
No such outcomes are claimed PASS.

## Limits and Stage 64

- The new rigid shader is source-level logically aligned
  with ModelShaders; true depth/UV/alpha parity across
  GLES/Metal/GL/Vulkan must still be tested on GPUs.
- Animated dissolve, alpha mask and actor reveal
  aperture are not exact pixel-matched. The animated
  path supports exact LOD dither and conservative
  partial-dissolve coverage; fully material-specific
  animated cutout needs a separate specialized shader.
- Rigid material shader graphs, displaced geometry,
  terrain/custom immediate meshes with nonstandard alpha,
  and offscreen particle systems require their own
  current-frame reactive provider when appropriate.
- Callback drop accounting does not prove a callback
  encoded actual draws; a no-op callback can still run.
- Next bounded Stage 64: audit exact animated
  alpha-cutout/dissolve and actor-reveal transition
  coverage, determine whether additional shader
  variants are warranted, and close remaining named
  functionality. Avoid indefinite scope expansion.
  Once the feature backlog is complete, start a
  separate native compile/CTest, GPU visual parity,
  fence/memory/profiling and fix phase with real
  executable PASS/FAIL outcomes.
- Every further feature stage requires its own
  `docs/handoffs/prisma-stage-XX.md` separately
  committed AFTER code changes, containing exact
  source commits, API/ownership, actual tests vs
  NOT RUN, limitations and next step.
  Keep PR #26 Draft and do not merge main/base
  without explicit user approval.

## Mac fetch and pull

```bash
cd ~/Projects/AIVERSE
git status --short
git fetch origin
git switch feature/prisma-postfx
git pull --ff-only origin feature/prisma-postfx
git log -1 --oneline
```

If your active checkout has a rebase or conflicts,
leave it intact and create a separate clean worktree
from `origin/feature/prisma-postfx` instead of
resetting or force-pushing another agent's work.
