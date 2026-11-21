# PRISMA Stage 89 — Mac log vs integrated GitHub source mismatch

Date: 2026-11-20
Branch: integration/prisma-motionweave-view
Previous: docs/handoffs/prisma-stage-88-appearance-link-release-tests.md

## Reports received
Mac build logged undefined `particles` at tests/vfx_ecs_presentation_test.cpp:87-88, and numerous unused variables from tests/ecs_* compiled in Release/NDEBUG. Another warning cites tests/render/exr_texture.cpp.

## Source verification performed
At integrated GitHub HEAD b6a6076499b3faa43db84d6087f5d27af1ca2c9d:
- tests/vfx_ecs_presentation_test.cpp blob 569604dde59cfd624c19b2aa4c09a20312c7fe1d already declares `std::vector<fx::FxParticleInfo> particles;` at line 74 before the snapshot uses near end of scenario().
- tests/ecs_scene_hierarchy_simulation_test.cpp, ecs_guarded_failure_test.cpp, ecs_simulation_bridge_test.cpp, ecs_cross_genre_mechanics_test.cpp, ecs_projectile_composition_test.cpp and ecs_cow_rollback_session_test.cpp already contain AIVERSE_TEST_CHECK and no standard assert() macros.
- tests/render/exr_texture.cpp does not exist in the currently fetched integration Git tree.
Therefore the incoming build diagnostics appear inconsistent with this GitHub branch revision. This may be a stale checkout, different worktree, local edits, or logs from different builds; none has been established conclusively.

## Changes and tests
No source change made; avoid duplicating existing fixes or incorrectly deleting test coverage.
GitHub source readback: PASS. Mac build verification: NOT RUN here.
No new compile/CTest/GPU result claimed.

## Next exact Mac diagnosis
```bash
cd ~/Projects/AIVERSE-prisma-motionweave-verify
git status --short
git rev-parse HEAD
git fetch origin
git rev-parse origin/integration/prisma-motionweave-view
git diff --name-status HEAD..origin/integration/prisma-motionweave-view -- tests/vfx_ecs_presentation_test.cpp tests/ecs_scene_hierarchy_simulation_test.cpp
nl -ba tests/vfx_ecs_presentation_test.cpp | sed -n '70,100p'
git ls-files '*exr_texture.cpp'
```
If clean, switch detached to latest branch SHA, run CMake reconfigure
and a single full build, capturing first real error. Do not discard local
uncommitted work or use reset --hard.
