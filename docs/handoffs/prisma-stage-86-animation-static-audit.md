# PRISMA Stage 86 — Consolidated animation/View source API audit

Date: 2026-11-20
Repository: chromealex/AIVERSE
Working branch: integration/prisma-motionweave-view
Main and original feature branches unchanged.

## Trigger
The user's full macOS build of the merged branch stopped at ~62%:
```
tools/animation/root_motion_bake.cpp:31:24:
error: use of undeclared identifier 'findNodePath'
```
Additionally tests/animation/root_motion_bake.cpp warned that
`accumulated` was set but unused, caused by Release NDEBUG disabling
`assert()` checks.

User asked for a broad audit before the next Mac build rather than
reactively fixing one compile error at a time.

## Scope actually inspected
Using the connected GitHub branch at current integration head:
- all 26 public headers in libraries/include/aiverselib/animation
  (including shared ECS budget, workers, scheduler, retargeting, events,
  root motion, montage and asset loading);
- library motionweave facade, tools/animation C++ sources, engine/view
  shared headers and visibility implementation;
- 30+ tests/animation translation units for stale private ECS access,
  missing findNodePath symbol, and NDEBUG-stripped assert() calls.
This is a targeted static API and regression-test audit, NOT a complete
5,908-file compiler verification of the AIVERSE engine. Connected GitHub
source reads do not compile or execute code.

## Issues discovered and fixes
1. Commit `0ef2c7259bba65e82a08f086d937522e0e8523d0`:
   libraries/include/aiverselib/animation/GenericRetarget.h now declares
   public inline `findNodePath(const render::Model&, const std::string&)`,
   matching canonical exact full bone paths from `bonePath()`.
   Existing consumers in root_motion_bake CLI and AnimationAssetLoader
   are served by their existing GenericRetarget.h include, with no new
   genre-specific model code.
2. Commit `1b7d0d127a0b08506f8a221b65cc9ab6136978e3`:
   AnimationEventDispatch.h replaces the illegal
   `const World::poolIfExists<AnimationAssetComponent>()` access with
   public read-only `eachExistingConst`. Retains maxEvents, alive-entity
   checks, graph and montage cue order, sorting/tie-breaking and callback
   purity; does not create ECS pools.
3. Commits
   `4ba4dfe9341e45d48b09e207f67c4139f2864deb`,
   `90df49bd6dd044de5d13644886fa2c4420f367ac`,
   `a1cc6dadefa1b78f43c110df7915545f6211073a` and
   `3c59eb34006de787c48fdbec9bc245f91cd130b8`:
   25 tests/animation *.cpp translation units replace 154 C assert()
   evaluations with `AIVERSE_TEST_CHECK` from ../AlwaysCheck.h.
   Expressions preserved so failures execute in Release/NDEBUG and
   unused-variable warnings disappear for assert-only test variables.
   The root_motor test was handled already in Stage 85.
   This conversion makes the CTest suite STRICTER: real regressions
   previously hidden by Release may now FAIL, which should not be masked.

## Verification actually performed
- GitHub source re-read after the commits for GenericRetarget,
  EventDispatch, their consumers, and all 25 modified test files.
- Structural check PASS: no private poolIfExists in audited public
  animation headers except inside ECS itself; no unprovided findNodePath;
  all 25 test files contain AlwaysCheck.h and active check expressions,
  and no remaining assert() expression in those files.
- NOT RUN: native clang/CMake compile, CTest or GPU runtime.
- Earlier pre-merge 115/115 CTest, Metal HDR and TAA passes do NOT apply
  to this current merged/modified source.

## Remaining limitations and next verification
- This static pass only covered relevant animation/View sources and test
  files, not all 5,908 repository files. Unknown generic integration,
  linker or shader failures might still be encountered by a real build.
- Next run should be ONE complete incremental all-target build, then
  ONE consolidated CTest on Mac; correct genuine newly exposed failures.
- No Mac filesystem/device/GPU attached to assistant tools; GitHub
  code readback cannot substitute native build or runtime validation.

```sh
cd ~/Projects/AIVERSE-prisma-motionweave-verify
git status --short
git fetch origin
git switch --detach origin/integration/prisma-motionweave-view
cmake --build build/merge-validation --parallel 4
ctest --test-dir build/merge-validation --output-on-failure --parallel 4
```
If any file is locally modified, inspect it before switching; do not
reset --hard. Run CTest only if build succeeds. Document exact compiler
diagnostics/CTest summary, then preserve a separate handoff for any fix.
