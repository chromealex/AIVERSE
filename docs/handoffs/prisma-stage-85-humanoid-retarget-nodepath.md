# PRISMA Stage 85 — MotionWeave humanoid retarget compile and Release root-motor test fixes

Date: 2026-11-20
Repo: chromealex/AIVERSE
Branch: integration/prisma-motionweave-view
Previous: docs/handoffs/prisma-stage-84-motionweave-scheduler-public-ecs.md

## User-provided Mac build failure
At roughly 60% on Mac Studio in `~/Projects/AIVERSE-prisma-motionweave-verify`:
```
libraries/include/aiverselib/animation/HumanoidRetarget.h:39:28:
error: use of undeclared identifier 'findNodePath'
```
The error appeared while compiling both `tests/animation/humanoid_retarget.cpp`
and `tests/animation/ragdoll.cpp`; the native all-target build terminated
before CTest. `tests/animation/root_motor.cpp` also reported four
unused-variable warnings in Release because its `assert` checks were disabled.

## Source changes
1. Commit `1521bb10cc761fa3d3f689d88653f6634325c742`:
`libraries/include/aiverselib/animation/HumanoidRetarget.h`.
Use existing `bonePath(model,node)` from `GenericRetarget.h` to
resolve exact hierarchy paths during HumanoidAvatar::compile. Do not
invent a `findNodePath` dependency or expose model internals.
Preserve validation for missing/duplicate bone mapping, required hips/head,
parent-first skeleton order and bind-world calculation.
2. Commit `5ae0e539c5f2d6917fb874d5c96cbf5261be5743`:
`tests/animation/root_motor.cpp`.
Replace NDEBUG-stripped `assert()` calls with always-active `check()`
that prints and exits on failure. This both restores actual Release
verification and eliminates the four warnings about unused test variables.

## Validation performed
- GitHub write acknowledgments and readback for both files: PASS.
- Static checks: no findNodePath call, canonical bonePath present, no
  stripped assertions in root_motor test: PASS.
- NOT RUN here: native C++ compiler, CTest, Metal/GL/Vulkan/GLES or
  runtime animation/bone mapping verification. The incoming compiler
  failure is real; the patch awaits incremental Mac rebuild.
- Historical pre-merge Prisma CTest/GPU passes are not evidence for this revision.

## Next step
```sh
cd ~/Projects/AIVERSE-prisma-motionweave-verify
git status --short
git fetch origin
git switch --detach origin/integration/prisma-motionweave-view
cmake --build build/merge-validation --parallel 4
```
Preserve cache; do not reset --hard or touch other agents' branches.
After a complete successful build, run one consolidated CTest and
record actual pass/fail counts. On any new failure, report the first
compiler diagnostic rather than just the make error.
