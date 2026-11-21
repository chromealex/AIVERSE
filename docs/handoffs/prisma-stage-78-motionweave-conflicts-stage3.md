# PRISMA Stage 78 — MotionWeave merge conflict resolution package 3

Date: 2026-11-20
Repository: chromealex/AIVERSE
Remote integration branch: integration/prisma-motionweave-view
User's local merge worktree: ~/Projects/AIVERSE-prisma-motionweave
Previous: docs/handoffs/prisma-stage-77-motionweave-31-conflicts.md

## Verified latest user status
The user applied resolution Stage 1 (8 files) and Stage 2 (4 files).
The Python scripts reported exact SHA-256 matches and staged those files.
Git then showed 19 unresolved paths. LF/CRLF warnings are Git
working-tree conversion diagnostics, not a failed merge.

## Prepared resolution Stage 3 (not applied to Mac yet)
From the user's exact uploaded prisma-motionweave-conflicts.zip:
- apps/editor/windows/GameWindow.cpp: preserve incoming GPU device lease
  and offscreen target owner setup, AND current Prisma Viewport::ensure
  signature with explicit editor viewportRetirement owner and name.
- apps/editor/windows/SceneWindow.cpp: same reconciliation for Scene.
- docs/agent-handoffs/universal-niagara-vfx-2026-11-18.md: preserve the
  Stage 7 SpectraFX authoring notes from ours and Stage 45.4 water
  integration notes from theirs instead of dropping either section.

Generated files for user:
- prisma-motionweave-resolution-stage3.zip
- apply_prisma_motionweave_stage3.py

The apply script refuses to run without an active MERGE_HEAD and verifies
SHA-256 of ALL current conflict files before writing or staging ANY.
This is safe only on the same untouched Stage 3 conflict hunks; if a
previous agent edited any of them, obtain a new archive rather than forcing.

## Actually executed vs NOT RUN
- Inspected exact user-uploaded three-way conflict archive in container.
- Generated exact-byte merged files and verified no conflict markers
  in three prepared outputs. Python script syntax checked with py_compile.
- The merge, native C++ compile, CTest, GPU/MotionWeave integration tests
  have NOT been run by this assistant.
- No claim that Editor's combined GPU owner APIs are already implemented
  is made; Editor.h/.cpp and shared RHI still have unresolved conflicts.

## Remaining
After Stage 3 applies, 16 unresolved files expected:
AGENTS.md, apps/client/GameClient.cpp, apps/editor/Editor.cpp,
apps/editor/Editor.h, apps/editor/animation/AnimationEditor.h,
apps/editor/windows/ModelWindow.cpp, apps/modelview/main.cpp,
engine/gfx/gl/GfxGL.cpp, engine/gfx/metal/GfxMetal.mm,
engine/gfx/vulkan/VkDevice.h, engine/render/AnimationGraph.cpp,
engine/render/PostFx.cpp, engine/render/PostFx.h,
engine/render/PostFxShaders.h, engine/render/Renderer3D.cpp,
modules/rts/view/RtsFog.h.

Next prioritize renderer/RHI/PostFx/Editor lifecycle consistency, then
client and animation playback. Do NOT finalize merge until the full
native build and target tests pass. Don't clobber Stage 1/2 resolutions,
or cherry-pick documentation-only remote commits into active merge worktree.
