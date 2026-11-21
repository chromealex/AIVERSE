# PRISMA Stage 80 — MotionWeave merge conflict package 5

Date: 2026-11-20
Repository: chromealex/AIVERSE
Integration target: integration/prisma-motionweave-view
Local user branch: integration/prisma-motionweave-local, in-progress uncommitted merge
Previous: docs/handoffs/prisma-stage-79-motionweave-merge-stage4.md

## Input and status
The user wrote "Ujnjdj" (Готово in wrong keyboard layout) in response to
Stage 4. This was interpreted as confirmation that Stage 4 was applied,
but no fresh git status output was supplied.
Assumed pending unresolved count before Stage 5: 13.
Stage 5 will only act if exact original unresolved files still match SHA-256.

## Source package assembled from uploaded exact three-way merge archive

Artifacts:
- prisma-motionweave-resolution-stage5.zip
- apply_prisma_motionweave_stage5.py

Two reconciled paths:
1. engine/gfx/metal/GfxMetal.mm:
   - Keep PRISMA gpuSubmissionFence(), gpuCompletedFence(),
     waitForSubmittedGpuWork(), completedGpuFrame_.
   - Keep MotionWeave waitForResourceIdle(), beginResourceRetirement(),
     resourceRetirementComplete(), resourceRetirementStatus(),
     endResourceRetirement(), abandonRetirementTokenAfterIdle(),
     nextRetirementToken_ and retirementTokens_.
   - Both use existing healthy-device/queue checks. Do not conflate
     queued marker tokens with per-frame completion fences.
2. engine/render/AnimationGraph.cpp:
   - Preserve PRISMA snapshotPreviewClocks()/restorePreviewClocks()
     strict program/skeleton validation for editor preview stability.
   - Preserve MotionWeave snapshotPlayback()/restorePlayback(),
     rebindPlayback(), clipTime(), seekClipNode() for animation
     continuity across model/graph reloads.

All original conflict markers removed only from these two paths,
without discarding either side's methods.

## Actually performed
- Inspected exact versions and conflicted hunks from user archive.
- Generated files, manifest with original/resolved SHA256, and apply script.
- Python py_compile PASS for script; static checked named API methods.
- NOT RUN: native compiler, CTest, Metal/GL/Vulkan, model playback
  or resource-retirement test. Merge is not committed by this stage.
- No user's local files were mutated by this assistant.

## Next
In the user's active local Git merge:
```sh
cd ~/Projects/AIVERSE-prisma-motionweave
python3 ~/Downloads/apply_prisma_motionweave_stage5.py \
  ~/Downloads/prisma-motionweave-resolution-stage5.zip
git diff --name-only --diff-filter=U
```
If the SHA or stage checks fail, stop and collect fresh conflict files
rather than forcing. Expected unresolved count: 11 if Stage 4 was applied.

The remaining 11 are AGENTS.md, GameClient.cpp, Editor.cpp,
Editor.h, AnimationEditor.h, ModelWindow.cpp, PostFx.cpp,
PostFx.h, PostFxShaders.h, Renderer3D.cpp, and RtsFog.h.
Merge should not be committed until all are carefully reconciled and
native builds/tests validate actual method ownership/linkage.
