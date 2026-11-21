# PRISMA Stage 90 — isolate clean build from an unfinished local rebase

Date: 2026-11-20
Repository: chromealex/AIVERSE
Remote integration branch: integration/prisma-motionweave-view
Previous handoff: docs/handoffs/prisma-stage-89-stale-worktree-source-diagnostics.md

## Real user Mac observations
In ~/Projects/AIVERSE-prisma-motionweave-verify:
- git status --short reported UU engine/render/Renderer3D.cpp
  and M skills/catalog.json.
- git switch --detach origin/integration/prisma-motionweave-view failed:
  "fatal: cannot switch branch while rebasing".
- A subsequent cmake --build build/merge-validation hit
  engine/render/Renderer3D.cpp:201:1: error:
  version control conflict marker in file ("<<<<<<< HEAD").
This is a local, active rebase with unresolved conflicts. The error
does not establish a defect in the published integration branch.

## What was verified
Fetched full engine/render/Renderer3D.cpp from GitHub branch.
Blob 47c3c2b94c3cb99773e96d63e5f699825b413dcf,
2118 lines, no lines beginning with <<<<<<<, ======= or >>>>>>>.
No source fix required. No native test or compile was run here.

## Safest next action
Keep the existing rebase worktree unchanged for future reconciliation.
Use a separate NEW path and new CMake build directory:
```sh
git -C ~/Projects/AIVERSE fetch origin
git -C ~/Projects/AIVERSE worktree add --detach \
  ~/Projects/AIVERSE-prisma-motionweave-clean \
  origin/integration/prisma-motionweave-view
cd ~/Projects/AIVERSE-prisma-motionweave-clean
git status --short
git rev-parse HEAD
cmake -S . -B build/merge-validation -DCMAKE_BUILD_TYPE=Release \
  -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_HDR_TESTS=ON \
  -DAIVERSE_BUILD_EDITOR=ON
cmake --build build/merge-validation --parallel 4
```
Only after full successful build, run one full CTest. Do not use
rebase --abort, reset --hard, git clean -fd, force-switch or discard
skills/catalog.json without examining whether the local changes have
value. If the clean worktree already exists, choose a unique new path.
The GitHub source branch and main are unmodified by this Stage 90.
