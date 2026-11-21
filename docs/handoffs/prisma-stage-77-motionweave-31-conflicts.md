# PRISMA Stage 77 — MotionWeave merge conflict inventory (user Mac log)

Date: 2026-11-20
Repo: chromealex/AIVERSE
Branch: integration/prisma-motionweave-view (remote pre-merge)
Local Mac branch: integration/prisma-motionweave-local
Local worktree: ~/Projects/AIVERSE-prisma-motionweave
Incoming: origin/feature/universal-ecs-animation-pipeline

## Actual observed three-way merge
User executed `git merge --no-commit --no-ff origin/feature/universal-ecs-animation-pipeline` successfully until Git reported content conflicts.
Result: `Automatic merge failed; fix conflicts and then commit the result.`
`git status --short` shows many automatically staged incoming files, and
31 unresolved `UU` files. A local MERGE_HEAD exists unless the user has
since aborted/committed; this handoff does not assume current Mac status.
No merge commit was performed or pushed by this agent.

## 31 unresolved paths (user-reported)
- modules/rts/view/RtsView.h
- modules/rts/view/RtsView.cpp
- modules/rts/view/RtsFog.h
- engine/world/WorldScene.h
- engine/render/Renderer3D.cpp
- engine/render/PostFxShaders.h
- engine/render/PostFx.h
- engine/render/PostFx.cpp
- engine/render/ModelTextureCache.h
- engine/render/Lights.h
- engine/render/AnimationGraph.h
- engine/render/AnimationGraph.cpp
- engine/gfx/vulkan/VkDevice.h
- engine/gfx/metal/GfxMetal.mm
- engine/gfx/gl/GfxGL.cpp
- engine/gfx/gl/GLApi.h
- engine/gfx/Gfx.h
- engine/fx/FxRender.cpp
- docs/agent-handoffs/universal-niagara-vfx-2026-11-18.md
- apps/modelview/main.cpp
- apps/editor/windows/StatsWindow.cpp
- apps/editor/windows/SceneWindow.cpp
- apps/editor/windows/ModelWindow.cpp
- apps/editor/windows/GameWindow.cpp
- apps/editor/windows/AnimationRigWindow.cpp
- apps/editor/windows/AnimationGraphWindow.cpp
- apps/editor/animation/AnimationEditor.h
- apps/editor/Editor.h
- apps/editor/Editor.cpp
- apps/client/GameClient.cpp
- AGENTS.md

## Interpretation / owners
Resolve RtsView and RtsFog together with the generic MotionWeave/View
contracts; preserve Prisma per-camera state and presentation callbacks.
Next resolve Gfx/Renderer3D/PostFx/AnimationGraph cross-API changes
(including GPU fences/history and shader paths). Finally editor/client
consumers and documentation. Do NOT use bulk `git checkout --ours` or
`--theirs` to discard legitimate changes. Preserve the new GL normal
atlas and verified Metal Stage-74 behavior.

## Validation status
Git merge detects conflicts: real Git output, no compile/test yet.
Earlier Prisma 115/115 and HDR/Metal probes belong to pre-integration
revisions; do not claim post-merge validation.
This repository commit adds only the handoff; it does not resolve the
local Mac worktree or stage merge content.

## Next action
Collect conflict hunks/three index stages for each conflicted path,
without modifying the working tree:
`git diff --name-only --diff-filter=U`, `git show :1:path`,
`git show :2:path`, `git show :3:path`, plus current conflict-marker
file. Inspect and reconcile safely, followed by build and focused
Prisma/MotionWeave/Metal GL GPU regression tests.
