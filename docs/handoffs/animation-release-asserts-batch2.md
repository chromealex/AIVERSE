# Animation Release test hardening — batch 2

Date: 2026-11-20
Branch: integration/prisma-motionweave-view
Paths and old assertions: [{"name":"hot_reload.cpp","asserts":13},{"name":"humanoid_profile.cpp","asserts":4},{"name":"humanoid_retarget.cpp","asserts":4},{"name":"jobs.cpp","asserts":4},{"name":"model_dependencies.cpp","asserts":15},{"name":"motionweave_api.cpp","asserts":1},{"name":"motionweave_backend.cpp","asserts":9}].
All standard asserts replaced with tests/AlwaysCheck.h AIVERSE_TEST_CHECK, active under Release/NDEBUG.
Actual: GitHub source updates only. Native compiler and CTest NOT RUN. Preserve all source test expressions.
