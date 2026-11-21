# AIVERSE Stage 91 — ECS COW and wave replay Release assertions

Date: 2026-11-20
Branch: integration/prisma-motionweave-view
Baseline head: 75b6d603eb10eb456109114a9070fc3d5f736d4c

## Origin
On a new clean Mac worktree at about 27-28% build, five ECS regression tests emitted unused variable/parameter warnings because Release NDEBUG compiled out assert() expressions. These were warnings; no native failure was reported in this chunk.

## Changes
Convert C assert() to existing AIVERSE_TEST_CHECK() from tests/AlwaysCheck.h while preserving each original expression and control flow. Files/counts: [{"path":"tests/ecs_cow_snapshot_test.cpp","assertions":28},{"path":"tests/ecs_cow_exception_test.cpp","assertions":19},{"path":"tests/ecs_cow_parallel_test.cpp","assertions":5},{"path":"tests/ecs_guarded_parallel_test.cpp","assertions":7},{"path":"tests/ecs_wave_replay_test.cpp","assertions":4}].

This restores true Release checks for snapshot COW, exception safety, parallel ECS and deterministic replay; assertions previously ignored in Release now execute. No production ECS APIs or behavior modified.

## Actual verification
GitHub source fetch and static textual transformation: performed. No local native compile, CTest or GPU execution in this stage. A separate clean Mac build is in progress and must be allowed to complete; new warnings do not require interrupting it. After update, pull the branch and run a single follow-up validation pass only after the in-progress build finishes.

## Next step
Collect first actual build failure (if any) and final build result. Then run a consolidated native CTest after a complete build on integrated branch. Preserve other worktrees and avoid force reset.
