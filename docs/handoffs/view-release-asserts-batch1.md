# Universal View Release test enforcement — batch 1

Date: 2026-11-20
Branch: integration/prisma-motionweave-view

Replaced Release-disabled standard assert expressions in tests/view/ with always-active AIVERSE_TEST_CHECK from tests/AlwaysCheck.h. Changed files/counts: [{"file":"appearance_authoring.cpp","asserts":13},{"file":"appearance_choice.cpp","asserts":32},{"file":"appearance_packet.cpp","asserts":12},{"file":"appearance_prepared.cpp","asserts":22},{"file":"appearance_registry.cpp","asserts":42},{"file":"frustum.cpp","asserts":8},{"file":"geometry_bounds.cpp","asserts":17},{"file":"grid_geometry.cpp","asserts":3},{"file":"horizon.cpp","asserts":8}].
Native Clang/CTest NOT RUN. GitHub static inspection only. CMake link correction for ShaderGraph appearance tests tracked separately.
