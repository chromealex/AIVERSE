# AIVERSE Stage 92 — Model image transaction test mock Device ABI and active Release coverage

Date: 2026-11-20
Branch: integration/prisma-motionweave-view
User's clean worktree: ~/Projects/AIVERSE-prisma-motionweave-clean
Previous handoff: docs/handoffs/aiverse-stage-91-ecs-cow-release-assertions.md

## Observed native failure (user report)
At ~58%, compilation of tests/render/model_image_transaction.cpp failed:
```
error: non-virtual member function marked 'override' hides virtual member function
void setIndexBuffer(gfx::BufferHandle) override{}
Device::setIndexBuffer(BufferHandle b, size_t offset=0) is pure virtual
error: variable type 'Device' is an abstract class
```
The existing x86_64 build also reported warning:
unused parameter 'h' in destroyTexture due to assert() removal under NDEBUG.

## Actual source change
Commit 4d9be74a39db03adbf3729f7aef1bab1679fb9ca
File: tests/render/model_image_transaction.cpp
- Change mock override to `setIndexBuffer(gfx::BufferHandle,size_t)`;
  match the actual two-argument base virtual signature; no Gfx.h API change.
- Convert 130 standard C `assert()` expressions into always-on
  `AIVERSE_TEST_CHECK()` from tests/AlwaysCheck.h. In Release/NDEBUG,
  the old tests stripped not just validation but also side-effecting
  calls such as `stage.stage`, `stage.commit`, `pool.collect`,
  `render::uploadModel`, etc. They now execute in Release.
- Do not alter production GPU fence, lifetime, image ownership or
  thread-affinity logic.

## Validation actually performed
- Compared actual Gfx.h declared virtual: two arguments, default offset.
- GitHub source update success and exact code transformation checks:
  the new mock signature is present; all 130 checks are now active.
- Local native compilation, runtime test, full CTest and GPU: NOT RUN
  by assistant. The test may expose previously masked defects after
  enabling the assertions; failures must not be suppressed.

## Next step
Continue the user's full build until completion; no need to interrupt
for warning-only lines. After current command finishes, pull the
integration branch in clean worktree and rerun the incremental target:
```sh
cd ~/Projects/AIVERSE-prisma-motionweave-clean
git status --short
git fetch origin
git switch --detach origin/integration/prisma-motionweave-view
cmake --build build/merge-validation --parallel 4
```
Only after full build succeeds, run a consolidated CTest.
Do not use reset --hard, change main, or touch source agent branches.
