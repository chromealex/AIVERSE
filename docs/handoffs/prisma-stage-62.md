# PRISMA Stage 62 — reactive current-frame coverage on first frame, camera cut and resize

Date: 2026-11-19
Repository: chromealex/AIVERSE
Working branch: feature/prisma-postfx
Draft PR: https://github.com/chromealex/AIVERSE/pull/26
Base: feature/universal-entity-inspector-fps
Previous stage: docs/handoffs/prisma-stage-61.md
Pre-handoff branch HEAD: 01f572eb0ce9b6ee3896dbd3ea072179201416e9
Main unchanged; never merge without explicit user approval.
User sequence: finish functional work first, then native CMake/CTest/GPU
backend rendering, parity, performance/fence tests and fixes.

## Outcome

A first-frame, camera-cut or resized viewport previously returned
early from `Renderer3D::renderObjectMotionVectors`, after seeding
the object-transform history but BEFORE creating current-frame RGBA8
reactive coverage for transparent objects. The reactive code was
also incorrectly nested under availability of the renderer-owned
RGBA16F object velocity texture AND its private depth attachment.

Stage 62 makes the reactive pass independent from previous camera
and object transforms, while preserving the stronger preconditions
for actual object velocity. It covers current rigid translucent /
LOD-faded objects, current-pose animated/morph/skinned/baked
translucent models, water meshes and native/particle callbacks.
When the source depth is unavailable, neither velocity nor
the reactive pass is fabricated; when no coverage providers or
usable pipelines exist, no mask is published.

There is no new per-frame GPU history, duplicated postprocess owner,
sim/ECS mutation, or CPU-based GPU fence guess. Renderer3D retains
exclusive ownership of its private velocity/depth/reactive textures.

## Exact committed source changes

1. `f97c2c26faa62e74e6b6b6f69f5786b944fc4b48` —
   `engine/render/Renderer3D.h`: declare a private
   `renderCurrentFrameReactiveMask` producer separate from
   object velocity and add `ObjectMotionStats.velocitySeedOnly`
   and `reactiveSubmitted` while leaving old `submitted`
   semantically restricted to object velocity.
2. `3b5c06a80460a99e6d3ba9f9589be5eee063b59a` —
   `Renderer3D.h`: forward-declare private nested
   `ObjectMotionHistory` at the method declaration so the
   later full definition is usable without moving ownership.
3. `aac6b5aadf9d8bb3d8e5d1c8d85133853c3ad687` —
   `engine/render/Renderer3D.cpp`: extract current-pose
   reactive generation to its own member helper, allocating
   an independent private Depth scratch attachment when
   no velocity target is prepared, and an RGBA8 reactive target
   when there are actual coverage sources. Continue to publish
   `MotionTextureInput::declared(...,RGBA8)` only after the
   reactive mask pass was encoded. Retain all existing
   rigid/animated/water/callback coverage paths.
   The main motion producer now checks
   `velocityHistoryReady=previousCameraValid && history.valid && !resize`
   and RGBA16F support for VELOCITY ONLY. It seeds current
   poses on cut/first/resize without returning before the
   reactive pass. Camera pose history is updated after
   reactive rendering. The existing queueObjectMotionTexture
   fences and retires velocity, scratch depth and reactive
   targets together on resize.
4. `0d7eecb7ac45a76cd65597a522ec0e75c2acb5d1` —
   `engine/render/Prisma.h`: append
   `ViewEffectReport.objectVelocityInputAccepted` and
   `reactiveWithoutObjectVelocity` fields to distinguish
   a valid current RGBA8 reactive mask from a valid borrowed
   RGBA16F object velocity stream.
5. `906d0a04af2b90ff3bc5629e0b48382c46441490` —
   `engine/render/Prisma.cpp`: initialize both fields every
   per-camera resolve and set them using actual typed
   MotionTextureInput / optional RHI textureInfo checks;
   mask-only means reactive admitted and object velocity
   not admitted for that view.
6. `56fbfe389f559ba2c71c238d37ae5fb3d1ffc1ea` —
   `apps/editor/windows/PrismaWindow.cpp`: show the
   current camera's reactive-only mask versus accepted
   object motion in the existing GPU-free diagnostics panel.
7. `81b7720b75f387edb7dedd5ece35488fd0cc12c7` —
   `Renderer3D.cpp`: normalize helper source indentation
   after moving the prior multi-hundred-line reactive
   implementation out of the velocity path.
8. `8747e69e983d0ce49fb6a001c6fed5951a338ce9` —
   `docs/prisma.md`: architecture, lifecycle, API and
   tests-vs-not-run boundaries for Stage 62.
9. `01f572eb0ce9b6ee3896dbd3ea072179201416e9` —
   `skills/visuals/aiverse-prisma/SKILL.md`: agent
   continuation contract for cut-safe reactive masks.

This handoff is a DISTINCT documentation commit, after the
bounded source implementation, design docs and skill update,
as required by the AIVERSE AGENTS.md policy. Retrieve the
latest branch HEAD after its commit.

## Presentation and GPU ownership contract

When `post.wantsObjectMotionVectors()` is true, Renderer3D
retrieves the stable viewId and previous-camera context.
The optional object-velocity pass uses only valid prior
motion history, unchanged camera identity/epoch, matching
viewport dimensions and RGBA16F availability. On an initial
frame/cut/resize it merely seeds current rigid/deformed poses
for the FOLLOWING frame; `ObjectMotionStats.submitted`
must NOT pretend a velocity overlay was drawn.

The separate reactive helper does not inspect prior camera
or pose histories. It replays current-pose geometry (for
animated silhouettes, current and previous vertex shader
matrices are deliberately identical because only coverage,
not motion, is written), using a borrowed scene-depth
sampler and renderer-private scratch Depth. This private
depth can now be allocated even on a reactive-only first
frame. Its RGBA8 target is cleared before each covered
pass. `ObjectMotionStats.reactiveSubmitted` turns true
only after such a pass and typed mask submission; there is
no stale mask exposed when no coverage was drawn.

At viewport resize, ALL three per-view renderer-owned
textures (RGBA16F velocity, Depth scratch, RGBA8 reactive)
are queued behind the backend's actual
`gpuSubmissionFence` or quarantined until confirmed GPU
idle. No immediate destruction, no CPU frame delay and
no resource ownership transfer to Prisma. The method is
genre-independent: FPS, RTS, cinematic and native plugins
may supply current reactive callbacks with the same API.

`Prisma::resolve` consumes the borrowed typed current
view inputs and publishes `reactiveInputSupplied`,
`reactiveInputAccepted`, `objectVelocityInputAccepted`,
`reactiveWithoutObjectVelocity`, and Stage 61's
`motionBlurReactiveEdgesEnabled`. They describe metadata/
RHI admission or an encoded draw, NOT pixels, GPU
completion, full transparency handling or memory-fence
validation. The editor only reads these snapshots, with
no compile or allocation side effects. Object velocity
history remains distinct from the canonical Temporal
Resolve color/depth history owned by Prisma.

A caveat: the ordinary Prisma post hook requests motion
inputs only when its HDR MotionBlur or Temporal Resolve
is authored/active. A native PostProcess implementation
can independently request the current-frame reactive pass
if it supplies valid scene depth. This stage does not
activate postprocessing when the effect is disabled.

## Actual verification (source-only)

- Retrieved the latest working branch/PR and Stage 61
  handoff first. Read existing Renderer3D object-motion
  implementation and all rigid/animated/mesh/callback
  reactive producers, the internal per-view GPU retirement
  contract, Prisma typed input acceptance, and the editor
  camera status panel.
- After code changes, read back `Renderer3D.{h,cpp}`,
  `Prisma.{h,cpp}`, `PrismaWindow.cpp` and
  `PrismaMotionTextureInput.h` from connected GitHub.
- 42/42 static source presence/call-order/ownership
  assertions passed after correcting one overly broad
  textual assertion that matched a comment rather than
  a GPU allocation.
- Both modified C++ method scopes
  (`renderObjectMotionVectors`,
  `renderCurrentFrameReactiveMask`) were checked for
  lexical brace balance: 2/2 balanced, no negative
  brace depth. This is NOT a compiler.
- Read back Stage 62's design doc and skill on GitHub.

**NOT RUN**, per the user's feature-first sequence:
CMake configure, native clang/C++/Obj-C++ compile/link,
GLSL/MSL/SPIR-V shader compiler, CTest,
`./run-tests.sh`, native editor/client launch,
Metal/GL/GLES/Vulkan/WebGL render, cut/resize reactive
pixel capture or HDR alpha parity, GPU-frame fence
overlap/stress, memory pressure and profiling, CI.
DO NOT claim any of these are PASS.

## Remaining limitations / next bounded stage 63

1. Native GPU tests must confirm current-pose reactive
   rasterization on the first visible frame, after a
   teleported camera and during two same-size/alternate-size
   viewports. The current shader and pipeline readiness
   remains backend-specific; source inspection cannot
   prove a device accepted their formats or produced
   correct alpha coverage.
2. If a scene has NO valid depth attachment, this stage
   still cannot render depth-tested reactive coverage.
   It intentionally avoids publishing a bogus mask.
3. Missing reactive coverage from an individual custom
   transparent shader/particle provider remains a producer
   responsibility, not a reason to synthesize stale pixels.
4. Suggested next functional Stage 63: audit remaining
   camera/live-host lifecycle and overlay producer
   completeness, including alpha-mask/dissolve/LOD
   transitions and reactive callback ownership, plus the
   bounded PostFX catalog. Avoid expanding scope
   indefinitely: after feature closure, move to the
   dedicated native compiler, GPU parity, CTest,
   temporal/fence and performance validation/fix pass.
5. Every further stage needs its own separately committed
   `docs/handoffs/prisma-stage-XX.md` after code
   implementation and documentation, with exact SHA,
   changed paths, actual tests vs NOT RUN, risks and a
   concrete next step. Keep PR #26 Draft; never merge
   main/base without explicit user approval.

## Mac fetch/pull, preserving concurrent work

```bash
cd ~/Projects/AIVERSE
git status --short
git fetch origin
git switch feature/prisma-postfx
git pull --ff-only origin feature/prisma-postfx
git log -1 --oneline
```

If another agent's worktree is rebasing/conflicted, use a
clean dedicated worktree from
`origin/feature/prisma-postfx`; do not reset or
force-push concurrent changes.
