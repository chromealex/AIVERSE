# SpectraFX CI checkpoint restore fix — 2026-11-24

Branch: `fix/spectrafx-ci-20261124`, based on `feature/spectrafx-html5-data-interfaces-20261123` at `464f6e15d`. Implementation: `ae4e2af3c`.

`engine/fx/SpectraGpuParticles.cpp` now transfers the eight VM parameter and attribute lanes by value in both checkpoint directions. The previous `memcpy` into `ProgramParams` and `ProgramAttributes` failed Linux GCC `-Werror=class-memaccess` in PR #115. GPU buffer layout and checkpoint byte format are unchanged.

Executed on macOS with all build outputs and temporary compiler files on KINGSTON:

- AppleClang `-std=c++20 -Wall -Wextra -Werror -fsyntax-only` on `SpectraGpuParticles.cpp`: PASS.
- CMake Release configure using Unix Makefiles: PASS. Template package download failed due local Python certificate validation; CMake warned that sample-game tests need templates.
- `cmake --build ... --target aiverse_fx`: PASS.
- `cmake --build ... --target aiverse_spectrafx_contract_tests aiverse_vfx_graph_runtime_tests`: PASS.
- `ctest -R '^(spectrafx_contract|vfx_graph_runtime)$'`: 2/2 PASS.
- `git diff --check`: PASS before commit.

Not run: Linux GCC exact CI, full Mac quick gate, native Studio UI, Metal preview/capture, Windows/Vulkan, full repository CTest, and interactive visual QA. PR #115's prior Linux arm64 failure is addressed in source but needs a fresh CI run. PR #113 still has failing old checks and remains draft. Next: push this fix to PR #115, review its new CI, then address remaining errors and run the required merged-tree Mac gate before any main merge.
