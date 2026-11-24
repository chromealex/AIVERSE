# Algorithms Stage 45 — periodic gradient, derivatives and fractals

Date: 2026-11-24. Branch: `feature/world-processes-stage31-bidirectional-handles`.
Prior Stage 44 HEAD: `0ab53d54716bee5a41eb45be265b1535997337c9`.
Stage plan: `561a87bbe929ce718e4b5ca3ad571f5d208ffa6a`.
Code SHA: `9a565026a826ec3998c07a56cd5c9529ecdf0820`. Main was already an ancestor at the pre-stage check; no force push or main merge.

## Implementation and API
- `libraries/include/aiverselib/algorithms/Noise.h`: `NoiseSample<N>`, periodicValueNoiseSample, periodicGradientNoiseSample, periodicGradientNoise, NoiseFractalOptions, fractalNoiseSample, fractalNoise. Two/three dimensions, explicit seed and integer lattice periods. Signed Q16 derivatives approximate the smoothstep interpolant; at turbulence/ridge cusps choose zero derivative. No floating-point, allocations or mutable state.
- Normalized fBm, turbulence and ridge with value/gradient basis, 1..16 octaves, gain in [0,65536], lacunarity 1..8; checked scaled coordinate, period and frequency bounds before multiplication. Scaled periods preserve seams. Existing periodic value kernel, cellular distances and legacy engine/game/gen/GenNoise remain unchanged. No save format changes.
- `libraries/catalog.json`: noise 0.2.0, retains random dependency and AIVERSE::lib_noise; verifiedPlatforms unchanged pending platform gates.
- `skills/core/aiverse-noise/SKILL.md`, `skills/catalog.json`, `tools/skills/catalog.py`: update API recipe, example and search metadata.
- `docs/libraries.md`, `docs/handoffs/algorithms-stage45-delivery.md`: roadmap and delivery scope.
- `tests/libraries/noise_tests.cpp`, root and standalone library CMakeLists: registered library_noise test; always-on CHECK even in Release.

## Actual verification
Linux isolated C++20 test, using byte-identical source reconstructed from fetched files. Git blob hashes checked against exact code SHA:
- Noise.h: e449d6cdc0f51f22b74fdd87d5dc1d58e04cb951
- Random.h: 6576f72e3d2548f2b6810012c6622d076597c287
- noise_tests.cpp: 7067b58e05575f4c8838f53d7cecfee5c3ec07b0

In /tmp/aiverse-noise-stage45, with include/aiverselib/algorithms populated, all three commands exited 0 and printed "Noise stage 45 checks passed":
```sh
clang++ -std=c++20 -Wall -Wextra -Werror -pedantic -O2 -DNDEBUG -Iinclude noise_tests.cpp -o clang-noise && ./clang-noise
g++ -std=c++20 -Wall -Wextra -Werror -pedantic -Wno-misleading-indentation -O2 -DNDEBUG -Iinclude noise_tests.cpp -o gcc-noise && ./gcc-noise
clang++ -std=c++20 -Wall -Wextra -Werror -pedantic -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Iinclude noise_tests.cpp -o san-noise && ./san-noise
```
Clang 17.0.0, GCC 14.2.0. Tests exercise 64 seeds in each dimension, periodic value bit parity, exact positive/negative period shifts, analytical gradient vs finite difference, all fractal modes, single-octave equivalence, negative coordinates, invalid options, scaled limits and deterministic repetition. These are isolated source tests, not a full repo build.

NOT RUN: root CMake/CTest, standalone library suite, full ./run-tests.sh, native project/export consumer, generated catalog check across entire tree, other platform SDKs, performance measurements, mandatory merged-tree Mac quick gate. No site publication or main merge.

## Feature delivery checklist
Implementation/architecture/native API/dependencies/documentation/skill/example: implementation present; isolated C++ API checks PASS. Skill and library catalog end-to-end validator: NOT RUN. Tests: isolated Linux PASS; unified runner registration present but full CTest NOT RUN. Headless arithmetic fixture PASS; engine headless NOT RUN. Performance: fixed bounded work, no heap by inspection, measurements NOT RUN. Platform: Linux Clang/GCC isolated PASS; Mac/Windows/mobile/Web NOT RUN. MCP, Studio, inspector, graphs, persistence and visual publishing: NOT APPLICABLE (stateless arithmetic only; no authorable scene entity or visible product). Internal library roadmap updated. Handoff committed separately. Feature not production-verified.

## Next stage 46
Implement bounded periodic domain warp and curl/vector noise on top of the shared NoiseSample API; chain-rule Jacobian, stable seed derivation, explicit bounds, no duplicate engine solver. Preserve Stage 44/45 outputs; update package version, recipe, catalog, test fixture and next handoff. Audit existing vector fields and GenNoise warp before adding functionality.
