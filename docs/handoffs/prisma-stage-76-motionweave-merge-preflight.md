# PRISMA Stage 76 — MotionWeave/View branch merge preflight

Date: 2026-11-20
Repository: chromealex/AIVERSE
Integration branch: integration/prisma-motionweave-view
Parent: feature/prisma-postfx
Incoming: feature/universal-ecs-animation-pipeline (PR #22)
Related LENS cross-integration: PR #49 and #50

## User goal
Bring animation agent's changes to RtsView into PRISMA without
losing Prisma HDR/temporal/camera/terrain and other parallel changes.

## Verified GitHub state
GitHub compare reports branches DIVERGED at merge base
9ee6ff6fbf390bd45363bc92c838f492dfe92eee.
At preflight, PRISMA head: 1dd744d30da97d7b8f74f21c05731d0dfecebf8c.
Animation head: b5c8c7f410b749737c596900769e11a91f1bb3e3.
Compare PRISMA..animation: ahead 1578 and behind 902 commits
(at query time, may evolve).
Overlap includes modules/rts/view/RtsView.{h,cpp},
RtsAnimate.cpp, RtsViewDraw.cpp, RtsTerrainRender.{h,cpp},
Renderer3D files, GameClient and CMakeLists.txt.
PR #22 explicitly says tests were deferred and RtsView migration
is unfinished. PR #49 warns that RtsView changes need manual reconciliation.

## Actual action completed
Created integration/prisma-motionweave-view from PRISMA.
No merge commit performed: source branches have thousands of divergent
commits; remote GitHub connector offers PR merge only, not arbitrary
branch three-way merge and conflict editing. Local model runtime cannot
resolve GitHub DNS and has no user worktree, so an honest merge cannot be
completed safely here. Do not represent integration branch as merged.

## Recommended local three-way merge in a NEW worktree
From any existing clone with clean/stable worktree:
```sh
cd ~/Projects/AIVERSE
git fetch origin
git worktree add ../AIVERSE-prisma-motionweave origin/integration/prisma-motionweave-view
cd ../AIVERSE-prisma-motionweave
git switch -c integration/prisma-motionweave-view --track origin/integration/prisma-motionweave-view
git merge --no-commit --no-ff origin/feature/universal-ecs-animation-pipeline
git status --short
git diff --name-only --diff-filter=U
```
If conflicts, preserve both intended implementations and reconcile
rather than `git checkout --ours`/`--theirs` blindly.
Examine RtsView, RtsViewDraw, RtsAnimate, shared Camera/View, Renderer3D,
GameClient, editor adapters, shader and CMake definitions.
Preserve current PRISMA ownership and no genre-specific animation in
MotionWeave. Preserve the distinct GPU-fenced retirement and per-camera
history, current Stage-75 GL normal atlas, and separate LENS camera owner.
Use `git merge --abort` for unsafe/unmanageable conflicts (no hard reset).
Run CMake all-target build, focused animation/Prisma CTest, GL/Metal fixture
before merge commit; create agent handoff and commit. If success,
`git push -u origin integration/prisma-motionweave-view`.
Do not merge PR #22 to main without further authorization.

## Verification
- GitHub PR metadata and two-way compare: completed.
- New integration branch created via GitHub: completed.
- Three-way merge, conflicts, build, tests: NOT RUN/NOT MERGED.
- Existing native Prisma 115/115 CTest and native GL/Metal HDR were
  obtained from older source snapshots; no claims for combined branches.

## Next actionable step
Execute local merge and provide conflicted path list/diagnostics.
Resolve shared files in integration branch, document every bounded fix,
and validate final full build and test suite.
