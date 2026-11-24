# SPIRV-Tools (vendored)

- Upstream: https://github.com/KhronosGroup/SPIRV-Tools, commit ef96ed763b43b59b33b31b362f09a02b729fa1c9
  (the commit glslang 16.6.0 lists in its known_good.json). Headers: ../spirv-headers,
  KhronosGroup/SPIRV-Headers 496543121ce6419f23d6fa5d7194ba66c36212d2.
- License: Apache-2.0 (LICENSE). SPIRV-Headers: MIT-style Khronos license (../spirv-headers/LICENSE).
- Kept: CMake, cmake/, external/CMakeLists.txt, include/, source/, utils/ (grammar table generators run by
  CMake, Python >= 3.9 on the build host), tools/ and examples/ (skipped by SPIRV_SKIP_EXECUTABLES), test/CMakeLists.txt
  only (skipped by SPIRV_SKIP_TESTS). Removed: tests, docs, CI, Bazel/GN files.
- Used by: the Vulkan backend through glslang (ENABLE_OPT): every program's SPIR-V goes through the
  performance passes (engine/gfx/vulkan/VkShaderCompiler.cpp). Root option AIVERSE_SPIRV_OPT (ON).
