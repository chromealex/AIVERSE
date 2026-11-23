# Handoff: runtime procedural building generation, stage 8 — 2026-11-23

Branch: feature/aiv20261123_building-grammar
PR: https://github.com/chromealex/AIVERSE/pull/114 (Draft).

## Implemented and changed files

- libraries/include/aiverselib/systems/RuntimeBuildingGenerator.h:
  portable header-only runtime catalog; registerStyleJson/registerStyleAsset
  (in-memory/package asset reader), RuntimeBuildingRequest, typed
  RuntimeBuildingResult/RuntimeBuildingError, generated mesh, material,
  collision box and gate anchor ownership.
- engine/buildings/RuntimeBuildingModel.h:
  build CPU render::Model directly from StyledMesh without a GLB
  intermediate. User/game render owner uploads with uploadModel and retires
  according to the existing GPU resource lifetime.
- tests/libraries/building_runtime_tests.cpp: registration, bad inputs,
  4 functions × multiple seeds, deterministic unsized, bounded explicit W/H/D,
  registry clear and result retention.
- tests/render/runtime_building_model_compile.cpp:
  compile-only contract for the CPU Renderer3D bridge.
- CMakeLists.txt: library_building_runtime CTest under AIVERSE_BUILD_TESTS.
- .github/workflows/building-style-focused.yml: compile and run runtime
  test, syntax-check renderer bridge.
- examples/world/runtime-building/README.md: example game integration and
  performance/threading/ownership requirements.

Commits: f4002a14 (API), 71551bd7 (Model adapter),
85673ab9 (packaged style reader/validation), a5395f5f (runtime tests),
2cfb2b87 (adapter test), bf061a43 (CMake), c69a4074 (workflow).

## Tests: actual evidence

Source changes were written to GitHub. Runtime C++ compilation,
CTest, Renderer3D upload, Metal/Vulkan/GLES, mobile, Web and gameplay
network replication have NOT RUN here and may require fixes.
Earlier passing Stage 4 focused tests at run 45210066318 do NOT validate
this later runtime interface.

## Constraints and exact next step

API is genre-independent; no direct editing, file path, process launch,
GPU mutation, gameplay ECS simulation state, or second scene-object store.
Do not mutate style registry concurrently with workers. Keep the original
building geometry/collision owner semantics; presentation output is
floating-point and not cross-architecture bit-exact.
Next: run focused C++ compiler/tests on Linux and Mac, syntax-check adapter,
upload an actual model in a native game scene with safe GPU retirement,
and verify packaging of style JSON inside a release (not dev path).
The stage-5 requested W/H/D solver still has unproven cases.
