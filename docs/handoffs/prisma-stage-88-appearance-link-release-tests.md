# PRISMA Stage 88 — AppearanceAuthoring ShaderGraph link repair and View Release test hardening

Date: 2026-11-20
Repo: chromealex/AIVERSE
Branch: integration/prisma-motionweave-view
Previous handoff: docs/handoffs/prisma-stage-87-integration-ci-handoff.md

## Real user report from Mac Studio full integrated build
At 63%, aiverse_appearance_authoring_tests failed to link:
```
Undefined symbols for architecture x86_64:
aiverse::render::graph::compile(aiverse::Json const&,std::string const&)
referenced from AppearanceAuthoringService::applyMaterial(...)
```
Warnings in tests/view/appearance_authoring.cpp and appearance_choice.cpp
were caused by Release NDEBUG disabling standard assert() expressions.

## Code changes
Commit 0c29a0431d923ba2eb1fd53dfaffac5ff2997d73:
CMakeLists.txt: explicitly link aiverse_shader_graph for all four
appearance-authoring test executables:
aiverse_appearance_authoring_tests,
aiverse_appearance_registry_tests,
aiverse_appearance_choice_tests,
aiverse_appearance_prepared_tests.
The existing engine/render/graph/ShaderGraph.cpp owns graph::compile.
No duplicate implementation, rendering change or new graph compiler.

Commit 1e439603007d273a1382ecdaa5499512df69c476:
Nine view test files including appearance_* and geometry tests use
../AlwaysCheck.h AIVERSE_TEST_CHECK instead of Release-stripped assert.

Commit 37e49d40eb62f9b636b25309dce795f2af202e1d:
Nine further view tests including scene packets, layering, rts preview,
surface and visibility use always-on assertions.
Together 18 tests/view files now preserve original assertions in
Release (original assert-only checks total 297).

## Verification actually performed
- Read CMake source, direct shader compiler target declaration at 1035,
  appearance test linkage at lines 768+ and AppearanceAuthoringService
  graph::compile call.
- Github commits for CMake and 18 tests/view translation units succeeded.
- Source code inspection only. No native link, tests or graphics execution
  by this agent. The user's reported linker failure belongs to older SHA.
- After converting assertions, some previously hidden test failures may
  now surface; do not declare PASS without actual CTest.

## Next step
On Mac:
```sh
cd ~/Projects/AIVERSE-prisma-motionweave-verify
git status --short
git fetch origin
git switch --detach origin/integration/prisma-motionweave-view
cmake -S . -B build/merge-validation -DCMAKE_BUILD_TYPE=Release -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_HDR_TESTS=ON -DAIVERSE_BUILD_EDITOR=ON
cmake --build build/merge-validation --parallel 4
```
Run consolidated CTest only after full build success. Preserve existing
build cache/worktrees; no changes to main/source agent branches.
