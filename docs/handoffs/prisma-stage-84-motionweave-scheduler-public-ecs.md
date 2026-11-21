# PRISMA Stage 84 — MotionWeave scheduler ECS access hardening

Date: 2026-11-20
Repo: chromealex/AIVERSE
Branch: integration/prisma-motionweave-view
Previous handoff: docs/handoffs/prisma-stage-83-animation-budget-private-pool-fix.md
Main, feature/prisma-postfx and animation source branch unchanged.

## Context
The latest user-supplied native build stopped around 53%:
AnimationBudgetPolicy.h attempted the private World::poolIfExists().
Stage 83 fixed that first compiler blocker. The native rebuild after
Stage 83 has not been reported yet.

## Proactive source inspection
Inspected seven likely MotionWeave/View headers in the integrated GitHub
branch. Four similar private World::poolIfExists() calls were found in
libraries/include/aiverselib/animation/MotionWeaveScheduler.h. The public
World::tryGetConst<T>(Entity) reads a component non-mutatingly without
registering a pool; missing pools return nullptr.

## Source change
Commit 300990557749984fd5dd11520fe76b0cafffd1eb:
libraries/include/aiverselib/animation/MotionWeaveScheduler.h:
- Remove private poolIfExists lookups for AnimationInputsComponent,
  AnimationAssetComponent, AnimationWorkHintComponent and
  AnimationBudgetComponent.
- Use read.tryGetConst<T>(e) for each optional component when processing
  an already-live animation entity.
- Keep existing worker classification, cached graph plan, costs, budget
  visibility, persistent worker pool, safe/callback phase ordering, and
  scheduler barrier intact.
- Do not change World public/private accessibility, ECS simulation or
  source animation branch.

## Validation
- PASS: GitHub update followed by exact file readback.
- PASS: static assertions for 4 public lookups, absence of remaining
  private access, and preservation of worker + evaluation flow.
- NOT RUN: native Clang compilation, full target build, CTest,
  motionweave_cost/budget tests, Metal/GL GPU and runtime perf.
- Previous 115/115 Prisma CTest was pre-merge and does not apply to this
  combined branch.

## Next Mac verification — one incremental build, then consolidated tests
```bash
cd ~/Projects/AIVERSE-prisma-motionweave-verify
git status --short
git fetch origin
git switch --detach origin/integration/prisma-motionweave-view
cmake --build build/merge-validation --parallel 4
```
Only after build success:
```bash
ctest --test-dir build/merge-validation --output-on-failure --parallel 4
```
Preserve cache and all parallel worktrees. On failure, collect the first
actual C++ diagnostic, not the trailing make Error 2, then fix the bounded
source defect with another agent handoff.
