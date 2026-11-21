# Universal View Release test enforcement — batch 2

Date: 2026-11-20
Branch: integration/prisma-motionweave-view

Replaced standard assert expressions in tests/view/ with always-active AIVERSE_TEST_CHECK from tests/AlwaysCheck.h. Changed files/counts: [{"file":"layer_providers.cpp","asserts":8},{"file":"layers.cpp","asserts":9},{"file":"packets.cpp","asserts":35},{"file":"rts_preview_presentation.cpp","asserts":25},{"file":"rts_selection_presentation.cpp","asserts":6},{"file":"surface.cpp","asserts":7},{"file":"triangle_facing.cpp","asserts":9},{"file":"view.cpp","asserts":40},{"file":"visibility_alias.cpp","asserts":1}].
Native Clang/CTest NOT RUN. Checks will now execute in Release; genuine failures must be fixed rather than suppressed.
