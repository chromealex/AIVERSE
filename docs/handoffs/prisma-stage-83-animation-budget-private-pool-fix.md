# PRISMA Stage 83 — MotionWeave AnimationBudgetPolicy ECS access compile fix

Date: 2026-11-20
Repository: chromealex/AIVERSE
Branch: integration/prisma-motionweave-view
Previous verified merge SHA: c333b76969e7f7f5061e4862da77c375bec34c16
Previous handoff: docs/handoffs/prisma-stage-82-motionweave-merge-verified.md

## Actual user Mac compilation failure
At ~53% of a fresh full build in ~/Projects/AIVERSE-prisma-motionweave-verify:
```
libraries/include/aiverselib/animation/AnimationBudgetPolicy.h:42:37:
error: 'poolIfExists' is a private member of 'aiverse::World'
engine/core/ECS.h:552:33: note: declared private here
make[2]: *** [CMakeFiles/aiverse_animation_budget_tests.dir/tests/animation/budget.cpp.o] Error 1
make: *** [all] Error 2
```
No full CTest executed. Other targets built up to that point, but this
does not prove full build success.

## Root cause and actual source change
AnimationBudgetPolicy's public inline assignAnimationBudgets() directly
called `presentation.poolIfExists<AnimationSignificanceComponent>()`,
which is private in World; direct pool use was not a supported external API.
Public `World::countExisting<T>()` and `eachExistingConst<T>(callback)`
provide non-registering component access. Public
`World::prepareExistingWrite<T>()` already offers mutable budgets.

Source commit cb0da52a75f7e83af8a94234de3b707f6e23796f:
`libraries/include/aiverselib/animation/AnimationBudgetPolicy.h`.
- Return empty stats if no significance pool exists using countExisting.
- Prepare existing budget component storage once.
- Reserve vector using countExisting.
- Iterate significance with the public, const, non-creating ECS API.
- Preserve entity-alive/budget existence guards, hidden visibility,
  neutralization of non-finite pixel/importance values, forced priority,
  stable tie-break and per-tier allocation.
- No World/ECS accessibility change, genre dependency or new pool creation.

## Verification actually performed
- GitHub source write/readback: PASS.
- Six static source invariants checked: public iteration, absent-pool
  guard, writable preparation, no private pool access, unchanged sort,
  unchanged forced-slot accounting: PASS.
- Native compile, CTest, Metal/GL/Vulkan GPU: NOT RUN since this fix.
- Earlier Prisma 115/115 and Metal HDR/TAA checks were prior to merge,
  not evidence for this integrated revision.

## Next action on Mac (fresh merged worktree)
```sh
cd ~/Projects/AIVERSE-prisma-motionweave-verify
git status --short
git fetch origin
git switch --detach origin/integration/prisma-motionweave-view
cmake --build build/merge-validation --parallel 4
```
If clean switch is blocked by local changes, preserve them; don't reset.
Incremental build uses existing cache, then run one consolidated CTest
after build success. For a further error, supply first Clang diagnostic.
No merge into main or changes to animation source branch.
