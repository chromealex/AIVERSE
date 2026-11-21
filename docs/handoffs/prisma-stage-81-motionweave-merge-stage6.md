# PRISMA Stage 81 — MotionWeave integration Stage 6: AGENTS contracts

Date: 2026-11-20
Repository: chromealex/AIVERSE
Remote branch: integration/prisma-motionweave-view
User active local merge: integration/prisma-motionweave-local
Previous handoff: docs/handoffs/prisma-stage-80-merge-stage5-metal-animationgraph.md

## Incoming user status
Stage 5 script applied and staged GfxMetal.mm and AnimationGraph.cpp.
Remaining 11 unresolved: AGENTS.md, apps/client/GameClient.cpp,
apps/editor/Editor.cpp, apps/editor/Editor.h,
apps/editor/animation/AnimationEditor.h,
apps/editor/windows/ModelWindow.cpp,
engine/render/PostFx.cpp, engine/render/PostFx.h,
engine/render/PostFxShaders.h, engine/render/Renderer3D.cpp,
modules/rts/view/RtsFog.h.

## Prepared Stage 6 merge packet
- prisma-motionweave-resolution-stage6.zip
- apply_prisma_motionweave_stage6.py

AGENTS.md has two conflict hunks. The first has the PRISMA vs SpectraFX
render ownership contract only on our side. The second has the short
PRISMA contract on our side and comprehensive MotionWeave/animation/visual
resource-lifetime instructions on the incoming side.
Merged output retains both sides in their original relative positions,
without replacing any of the rules.

## Validation
- Extracted exact conflicted AGENTS.md from user's uploaded merge archive.
- Resolved precisely two markers; verified both primary contract sections
  are present and no conflict markers remain.
- Generated ZIP with original and resulting SHA256; Python apply script
  compiled successfully.
- Native build/CTest/graphics/merge commit: NOT RUN.
- The user has not yet applied this packet; expected 10 conflicts once
  the AGENTS resolution is staged and the worktree still matches archive.

## Safe next steps
On Mac active merge worktree:
```bash
cd ~/Projects/AIVERSE-prisma-motionweave
python3 ~/Downloads/apply_prisma_motionweave_stage6.py \
  ~/Downloads/prisma-motionweave-resolution-stage6.zip
git diff --name-only --diff-filter=U
```

Do not merge into main, base or source agent branch. The remaining
engine/render/PostFx* and Renderer3D are architectural API conflicts:
Prisma owns the canonical PostFx alias while the animation branch has
legacy PostFx class implementations. A naive concatenation introduces
duplicate type definitions. Resolve via compatibility adapter and
cross-file API checking, not by choosing whole files. Likewise the
RtsFog alias to generic VisibilityField needs GPU memory accounting
reconciled in its generic owner, not a duplicate RTS Fog class.
