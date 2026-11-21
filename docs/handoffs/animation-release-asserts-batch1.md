# Animation Release test hardening — batch 1

Date: 2026-11-20
Branch: integration/prisma-motionweave-view

Source paths: tests/animation/action_edges.cpp, tests/animation/asset_loader.cpp, tests/animation/backend_phase_transfer.cpp, tests/animation/crowd.cpp, tests/animation/cues.cpp, tests/animation/editor_visual_asset_service.cpp, tests/animation/graph_notifies.cpp.
Replace NDEBUG-stripped assert() with always-on AIVERSE_TEST_CHECK() from tests/AlwaysCheck.h, preserving expressions and side effects.
Actual verification: source text transformations and GitHub writes only. Native CMake/CTest NOT RUN. Continue with remaining test files and full Mac all-target rebuild.
