# PRISMA Stage 97 follow-up — MetalDevice compiler scope fix

Date: 2026-11-20
Repo: chromealex/AIVERSE
Branch: `feature/prisma-main-acceptance-stage96-20261120` (Draft PR #84)
Previous: `docs/handoffs/prisma-stage-97-multiview-gpu-lifecycle.md`
Integration baseline: `main` at `f9aed9a705982e2c6b77b29869e309995ce8b163`.
This branch is intentionally not merged into main.

## Incoming real Mac Studio build error

User submitted a native macOS Release build excerpt from:
`/Users/aleksandrfeer/Projects/AIVERSE-prisma-stage97`.

- `aiverse_runtime` failed compiling `engine/gfx/metal/GfxMetal.mm`.
- First diagnostic around line 85:
  `class MetalDevice final : public Device` reported abstract class.
- Next diagnostics reported pure virtual methods `createBuffer`,
  `updateBuffer`, `destroyBuffer`, `createTexture`, etc. not implemented.
- Constructor initializer `layer_(layer)` reported missing member;
  `device_`, `queue_`, `frameSemaphore_`, `info_` appeared undeclared.
- Clang terminated after 20 errors, make exited nonzero.
- The excerpt is **real failed build evidence**. There is no executed
  Stage97 Metal/GL multiview test or complete CTest PASS for this SHA.

## Root cause, verified against exact remote source

Both baseline main and Stage97 branch at the time of diagnosis had this
source immediately after `completedFrameSerial()`'s return expression:

```cpp
uint64_t completedFrameSerial() const override {
    return gpuHealthy_->load(std::memory_order_acquire)
        ? completedFrame_->load(std::memory_order_acquire) : 0;
// MISSING } here
uint64_t gpuSubmissionFence() const override {
```

As a result, the compiler parses the subsequent method definition inside
`completedFrameSerial()`; the many member-lookup and abstract-class errors
are cascading parser errors, not 20 independent missing implementation APIs.

## Applied source fixes

1. `1272949ce420544ae42cc70a830169dd689920e3`:
   `engine/gfx/metal/GfxMetal.mm`, insert exactly one missing `}`
   after the `completedFrameSerial()` return statement; preserve the
   existing GPU submission/completion-fence bodies and all Metal resource
   ownership semantics. No changes to `Gfx.h`.
2. `66f3e3e146fa5ea750f308eea4df2f74dbf02f41`:
   `tests/render/HdrScene.h`, the Stage97 multiview fixture now calls
   `beginFrame/endFrame` **only** when `HdrScene::ownsFrame` is true
   (the standalone GL33 runner); the Metal `Platform` host owns its
   frame lifecycle, so it does not double-submit command buffers.
   This reproduces the existing fixture's backend contract.

## Actual validation

- GitHub readback of the patched Metal source confirms the method closes
  before `gpuSubmissionFence()`, then independent
  `gpuCompletedFence()` and `waitForSubmittedGpuWork()` methods follow.
- A lightweight lexical brace-balance check of the full 2083-line
  `GfxMetal.mm` (ignoring ordinary C/C++ strings and comments) returned
  a zero final brace depth and no underflow. This is only a **static**
  structural check, not an Objective-C++ compiler.
- Verified Stage97 fixture now conforms to GL-owned vs Metal-host-owned
  frame begin/end contract from `hdr_gl.mm`, `hdr_scene.cpp` and
  existing `HdrScene::draw`.
- **NOT RUN by assistant**: native compiler, clang/ObjC++, Metal shader
  compilation, CTest, GL/Metal GPU tests, framebuffer readback,
  real GPU fence completion and Vulkan/GLES validation.

## Next safe Mac Studio commands

```bash
cd ~/Projects/AIVERSE-prisma-stage97
git status --short
git fetch origin
# Only if the worktree is clean. Never force-reset or discard local edits.
git switch --detach origin/feature/prisma-main-acceptance-stage96-20261120

bash -n tools/prisma/acceptance.sh
cmake --build build/prisma-stage97 --parallel 4 \
  --target aiverse_hdr_scene aiverse_hdr_gl

# ONLY after both targets compile/link, try actual backend tests:
AIVERSE_PRISMA_MULTIVIEW_VERIFY=1 AIVERSE_PRISMA_TEMPORAL=1 \
  AIVERSE_PRISMA_JITTER=1 build/prisma-stage97/aiverse_hdr_scene
AIVERSE_PRISMA_MULTIVIEW_VERIFY=1 AIVERSE_PRISMA_TEMPORAL=1 \
  AIVERSE_PRISMA_JITTER=1 build/prisma-stage97/aiverse_hdr_gl
```

If further compiler or GPU errors occur, collect the **first real error**
and fix in a narrowly scoped commit plus a new handoff. Do not count
queued/cancelled GitHub CI or older successful pre-merge test suites.
Once the targeted smoke runs, do the one consolidated acceptance runner
`bash tools/prisma/acceptance.sh build/prisma-stage97`; record exact HEAD,
commands, CTest count, backend names and pass/fail/skipped with logs.

## Remaining scope

Real moving object pixel/ghosting validation, multi-view GPU
fence-completion and resource residency stress, GLSL/Vulkan/GLES parity,
and complete Release CTest on the corrected integrated code are pending.
The PR remains Draft and main unchanged until the new native evidence.
