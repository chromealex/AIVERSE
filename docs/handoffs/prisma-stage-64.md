# PRISMA Stage 64 — animated alpha/dissolve and Actor Reveal reactive variants

Date: 2026-11-19
Repository: chromealex/AIVERSE
Working branch: feature/prisma-postfx
Draft PR: https://github.com/chromealex/AIVERSE/pull/26
Base: feature/universal-entity-inspector-fps
Previous stage: docs/handoffs/prisma-stage-63.md
Last verified pre-handoff branch HEAD: f4c2e040e2e06aaaaf1faa1fb6fbb7036bf67498
Main unchanged, no merge without explicit user approval.
User sequencing: finish functionality before one consolidated native compiler,
CTest, multi-backend GPU image/parity, fence and profiler verification phase.

## Completed bounded functionality

Stage 63 reproduced rigid material alpha-cutout and dissolve but
still used conservative full-silhouette reactive coverage for
skin/morph/baked animated objects, and did not reproduce the
kDrawActorReveal aperture. Stage 64 closes these named gaps
at the shader-source and Renderer3D binding level:

- For animated current-frame reactive passes, create dedicated
  GLSL and Metal vertex variants from the existing deformation
  motion sources, adding current-deformed model-space position,
  original UV and vertex alpha varyings. Existing ordinary
  velocity shader strings, ABI and draw passes remain unchanged.
- Animate reactive fragments now replay material base texture
  UV transform, AlphaMode::Mask threshold, vertex alpha and team
  alpha overrides, the same object-space dissolve noise as
  ModelShaders and existing precise 4x4 screen LOD dither.
- For flagged kDrawActorReveal objects in the main-view camera,
  both rigid and animated reactive coverage applies the same
  actor-position/radius, view-axis attenuation and 4x4 dither
  aperture used by the ordinary model draw. No gameplay/ECS
  state is modified and no extra persistent GPU history exists.
- Extend CPU source-level draw statistics with
  ObjectMotionStats.actorRevealReactiveDraws. Source code
  can count encoded draw submissions, not pixel correctness.

## Exact commits and changed paths

1. `8014c84341e3dfa16b8d78fdf8e8e6ccd8aab7db` —
   `engine/render/Renderer3DReactiveShaders.h`: add
   `animatedVertexGlsl(motionSource,baked)` and
   `animatedMsl(motionSource,baked)`; the shader variant
   only patches the reactive source, adding
   `v_reactiveObjectPos/v_reactiveUv/v_reactiveAlpha`
   and MSL equivalents. Extend animated kAnimatedFS
   and generated Metal fragment with LOD, material alpha,
   3D dissolve, current-pose actor aperture, and scene
   depth rejection. Both generation functions return an
   empty string if source anchors do not match.
2. `5322ea7ad124fe58123816988e9a133d8bad5ba5` —
   `Renderer3DReactiveShaders.h`: extend rigid cutout
   shader GLSL and MSL U0 with world matrix, actor
   center/radius and axis, passing world position
   into fragments and applying Actor Reveal aperture
   before dissolve/cutout. The borrowed scene-depth
   sampling behavior remains.
3. `9b2d3dc7eb3da6fdc887df7ae838a559a4544c48` —
   `engine/render/Renderer3D.h`: add the CPU-only
   `ObjectMotionStats.actorRevealReactiveDraws`.
4. `ecb80076f4a9f75d4b599fe3ad98a79abaef39b4` —
   `engine/render/Renderer3D.cpp`: include eligible
   rigid and animated Actor Reveal objects and
   animated material alpha-cutouts in current-frame
   reactive mask generation, including otherwise
   fully opaque objects that need a masked aperture.
5. `a66ce187c30d10956fe171f42aee880bc372469e` —
   `Renderer3D.cpp`: use the new reactive-only
   GLSL/MSL generated variants, verify nonempty
   source before shader creation; extend animated
   pipeline vertex layout to position 0, UV 2,
   color alpha 4, joints 5, normalized weights 6;
   preserve current U0 and U3/U4/U5 deformation.
   Add `ReactiveAnimatedU1` (160 bytes) with
   `lod,uv0,uv1,maskInfo,world,reveal,revealAxis`,
   bind authored material texture at slot 1, and
   update reactive alpha/dissolve/reveal draw stats.
   Extend rigid `ReactiveCutoutUniforms` from 144 to
   240 bytes for world/reveal/axis; compute the same
   main-camera actor aperture parameters as
   `Renderer3DModels.cpp` and provide to both paths.
6. `ffc18b17bdfe158e61acfce72e20c04ff2d42f0a` —
   `Renderer3DReactiveShaders.h`: update source
   ownership/feature contract comments, removing
   obsolete "animated dissolve conservative" statement.
7. `7b5c145ef6116c6a9a3b127a42033d3bb68ca6e9` —
   `docs/prisma.md`: Stage 64 architecture, shader
   bindings, source-level behavior, limits and tests.
8. `f4c2e040e2e06aaaaf1faa1fb6fbb7036bf67498` —
   `skills/visuals/aiverse-prisma/SKILL.md`: agent
   continuation guidance, current frame material/
   dissolve/reveal replay contract and GPU warnings.

This is a separate handoff-only commit AFTER bounded
source/design/agent-skill updates, as required by AGENTS.md.

## API and shader ownership invariants

- External project/game callback API unchanged:
  `Renderer3D::addMotionReactiveDraw` registers a
  per-scene water/particle/custom reactive draw.
  It is cleared at new beginScene or after resolve.
  Source pixels are never stored as temporal history.
- `Renderer3D::renderCurrentFrameReactiveMask()`
  continues to run independently of a prior valid
  object-motion camera frame. It creates only
  renderer-owned RGBA8 reactive output and private
  depth attachment when needed, sampling borrowed
  Prisma scene depth READ-ONLY.
- The animated reactive variant is built from the
  original skin/morph/baked motion source without
  changing the normal motion shaders. It adds
  varyings for current deformed model-space position,
  original ModelVertex UV and vertex alpha. The
  current-deformed position is the same object-space
  coordinate that ModelShaders uses for dissolve,
  before multiplication by the draw's world matrix.
- Animated reactive U1 layout is 160 bytes,
  available in GLSL U1 / Metal buffer(2); U0
  and U3/U4/U5 from normal deformation remain
  the same. Base color alpha is texture slot1,
  scene depth is slot0; morph atlas remains slot8,
  baked animation atlas slot7. ShaderDesc.textureSlots
  remains 9, uniformSlots remains 6.
- Rigid detailed coverage uses a 240-byte U0
  with matrix/alpha/dissolve/reveal fields, no
  new sampler. Both rigid and animated fragments
  replay the ModelShaders actor aperture only
  when kDrawActorReveal, geometryMainView_ and
  actorReveal.w>0. Current camera view axis/
  inverse distance are computed like ordinary
  renderer model passes.
- Per-camera GPU-fenced retirement, shared size
  caches, TemporalResolve, other FXAA/MotionBlur
  work and SpectraFX ownership are unchanged.
  No CPU frame-age heuristic is used to retire GPU
  targets; no authoritative ECS changes introduced.

## Actually performed verification (not compiler/GPU)

- Retrieved latest branch SHA, Draft PR #26 and
  Stage 63 handoff, then inspected the real
  motion shader source, reactive shader variants,
  material fragment cutout/LOD/dissolve/aperture,
  native Renderer3D material model binding and
  camera actorReveal formulas.
- Changed source directly on GitHub branch
  `feature/prisma-postfx`, then read back
  `Renderer3DReactiveShaders.h`,
  `Renderer3D.cpp`, `Renderer3D.h`,
  `Renderer3DMotionShaders.h` and
  `ModelShaders.h`.
- **61/61 static source/binding/ownership checks**
  passed after final code, including MSL/GLSL
  U1/U0 layout, material UV/vertex alpha,
  current deformed-space dissolve, actor reveal,
  unchanged native velocity paths and GPU queue.
- Separately tested source-string transformation
  anchors for **4/4 reactive variants**:
  skin/morph GLSL, baked GLSL, skin/morph MSL,
  baked MSL, each retaining current deformed pose,
  UV/alpha and original motion output. These
  are JS static fixtures using retrieved shader
  strings, NOT GLSL/MSL compiler validation.
- Updated docs/prisma.md and the PRISMA skill
  and checked both are available in GitHub.

**NOT RUN:** native CMake/clang/C++/Obj-C++ build,
GLSL/MSL/SPIR-V compilation, CTest,
`./run-tests.sh`, Editor/GameClient runtime,
Metal/GL/GLES/Vulkan/WebGL pixel/alpha/parity,
first frame/cut/resize actors and transparency
image capture, GPU fences, performance/profiler
or CI. No GPU success is claimed from static
source checks.

## Known limitations / next Stage 65

1. Actual backend shaders might reject the added
   interpolators/UV attributes/Metal binding ABI.
   Stage 64 is **source-level feature complete**,
   not runtime/GPU-validated. Native shader compile
   is the first indispensable verification step.
2. Team color alpha, alpha-mask and scene depth
   parity are approximated by the same source-level
   normal material formulas. Arbitrary user
   material Shader Graph vertex displacement and
   complex transparency layering need explicit
   custom reactive provider code and are not
   handled by this built-in variant.
3. No other PRISMA feature should be declared fully
   finished until the remaining bounded catalog
   is audited. Avoid inventing further effects
   indefinitely. Stage 65 recommended: do a
   concrete feature-closure review/manifest and
   fix only actual missing native host wiring,
   then start the user-requested ONE consolidated
   CMake/CTest + GPU validation/repair stage.
   Separate clearly real PASS from NOT RUN.
4. Any further code stage needs a separately
   committed `docs/handoffs/prisma-stage-XX.md`,
   with exact commits, changed files, architecture,
   actual tests/NOT RUN and next step.
   Keep PR #26 Draft and do not merge to main
   or PR base without explicit user approval.

## Fetch branch including pull on Mac

```bash
cd ~/Projects/AIVERSE
git status --short
git fetch origin
git switch feature/prisma-postfx
git pull --ff-only origin feature/prisma-postfx
git log -1 --oneline
```

If another worktree contains a rebase/conflict,
do NOT force push/reset that worktree; use a clean
dedicated worktree from origin/feature/prisma-postfx.
