# AIVERSE world processes — Stage 05 focused acceptance (2026-11-23)

Branch: `feature/world-processes-experimental`; draft PR: https://github.com/chromealex/AIVERSE/pull/112. No merge to `main`.

## Exact work delivered

- `tests/world_processes_verify.sh` (commit `0c305623145e589899c5f50e2db23966776e62a5`) compiles and runs four self-contained native tests using the same real AIVERSE `Simulation.cpp`, `Fixed`, `Json` and ECS implementation:
  1. `tests/libraries/world_processes_test.cpp`: ten primitive policy smoke/edge cases
  2. `tests/ecs_echo_acoustics_test.cpp`: ECHO ECS propagation, save/COW rollback, entity-generation safety
  3. `tests/ecs_fracture_structure_test.cpp`: FRACTURE support/bonds, structural event, acknowledgement/repair, PHYSIS stress bridge, save/COW rollback
  4. `tests/ecs_aether_gas_test.cpp`: AETHER smoke/toxin/oxygen transport, flux, PHYSIS fire read adapter, native persistence/COW rollback
- `.github/workflows/world-processes.yml` (commit `b26b91ad9db3e3d456e7ee3dc7b985815dd062b5`) runs the focused script under GCC and Clang on Ubuntu; the Clang matrix job also runs `-fsanitize=address,undefined -fno-omit-frame-pointer`.
- This stage is **verification infrastructure and evidence**, not a new gameplay feature. Source ownership remains in `aiverselib/world`, not duplicated into `engine/`.

## Actual CI result — verified, not inferred

Workflow: **World processes focused ECS acceptance**, run ID `45207631125` at commit `b26b91ad9db3e3d456e7ee3dc7b985815dd062b5`.

- GCC job `137807534414`: **completed/success**. Log confirms RUN of four tests and PASS of ECHO, FRACTURE, AETHER, and final `PASS world-processes focused native acceptance`.
- Clang job `137807535139`: **completed/success**. Normal build/test **PASS** for all four; ASan/UBSan rerun **PASS** for all four, with no sanitizer diagnostic in the inspected log.
- Separately, the local Stage01 equivalent policy smoke test compiled and ran with C++20 `-Wall -Wextra -Werror -pedantic` and `-fsanitize=address,undefined`; this is additional Stage01-only evidence, not a substitute for CI.

The new focused acceptance suite therefore has **both jobs green**, with all four tests passing under GCC, Clang, and Clang ASan/UBSan. Tests are compiled directly from genuine repo source and link actual core simulation translation units.

## What this does NOT prove

- `./run-tests.sh --quick` on the merged Mac tree: **not performed**. No validated CTest invocation for the three new CMake targets inside a full configured repository build on macOS.
- Full GitHub Actions matrix `CI` (run `45207631147`) and other optional workflows were still incomplete when inspected; do not claim they passed.
- In earlier merge-CI, `tools/layers/check_layers.py` failed on 13 unrelated `engine/voxel -> libraries` edges, with no ECHO/FRACTURE/AETHER layer violation after the ECHO move.
- Editor UI/component metadata, actual PHYSIS wind-driven gas advection, Box3D structural falling/debris, audio mixer/AI hearing binding, real-game scenarios, replay parity against all legacy modules, web demos and hardware profiling are not part of this stage.
- The three modules are verified as **focused ECS building blocks**, not as complete production-ready systems. Seven other proposed mechanics remain experimental pure kernels.

## Agent follow-up / commands

On macOS, after reconciling with current main and before any merge:

```sh
git fetch origin
git switch feature/world-processes-experimental
git pull --ff-only origin feature/world-processes-experimental
bash tests/world_processes_verify.sh
cmake -S . -B build/world-processes -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=OFF
cmake --build build/world-processes --target aiverse_ecs_echo_acoustics_test aiverse_ecs_fracture_structure_test aiverse_ecs_aether_gas_test --parallel 4
ctest --test-dir build/world-processes -R '^ecs_(echo_acoustics|fracture_structure|aether_gas)_test$' --output-on-failure
./run-tests.sh --quick
```

Specific next work: finish full Mac/CTests and inspect active PR CI for blockers, coordinate existing voxel-layer debt with its owner instead of silently adding exceptions, then add generic Studio ECS metadata/authoring and small reproducible scenes for ECHO/FRACTURE/AETHER. Do not open a fourth system while the current three remain partial. Every implementation stage requires a new handoff showing SHA, changed paths, actual test results and next step.
