# PRISMA Stage 79 — MotionWeave merge resolution package 4

Date: 2026-11-20
Repository: chromealex/AIVERSE
Remote integration branch: integration/prisma-motionweave-view
Local active merge: integration/prisma-motionweave-local
Previous: docs/handoffs/prisma-stage-78-motionweave-conflicts-stage3.md

## Verified incoming user result
Stage 3 apply script reported 3 files applied/staged; 16 conflicts remain.
Do not assume other active merge changes have been committed.

## Package 4 files (prepared, not yet applied)
Source: exact uploaded prisma-motionweave-conflicts.zip.
Output: prisma-motionweave-resolution-stage4.zip and
apply_prisma_motionweave_stage4.py.
- engine/gfx/vulkan/VkDevice.h — preserve PRISMA backend-confirmed
  gpuSubmissionFence / gpuCompletedFence / waitForSubmittedGpuWork API
  together with MotionWeave waitForResourceIdle / explicit retirement token
  lifecycle declarations.
- engine/gfx/gl/GfxGL.cpp — keep both includes (<deque> and <map>),
  separate PRISMA gpuFences_ and MotionWeave retirementFences_ queues
  with explicit cleanup, and both scoped elapsed GPU queries and resource
  retirement methods.
- apps/modelview/main.cpp — require a healthy GPU, PRISMA submitted-work
  barrier AND MotionWeave resource-idle proof before destroying native
  rendering models and buffers. Refuse teardown if one cannot certify idle.

No other files are modified or staged by this package; a matching SHA-256
check runs on all three original conflict files before any write.

## Verification performed
- Inspected exact conflict hunks and both sides in uploaded archive.
- Generated three marker-free resolved files.
- Static structural checks for both GL fence lifetimes and Vulkan
  declarations; Python script py_compile PASS.
- NOT RUN: native compiler, full CTest, GL/Metal/Vulkan renderer, memory
  lifetime tests. API co-existence still needs linker/behavior validation.

## Next
Apply package only in existing active local merge, verify 13 remain.
Then reconcile PostFx/Renderer3D and editor/client API lifetimes before
compiling. Do not merge into main or overwrite source agent branches.
