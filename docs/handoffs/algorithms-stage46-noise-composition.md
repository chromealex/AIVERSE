# Algorithms Stage 46 — Billow, vector/Jacobian, domain warp and curl

Date: 2026-11-24. Branch: `feature/world-processes-stage31-bidirectional-handles`.
Prior Stage 45 HEAD: `53a3bbe6a9599073ab1253c7fbd48cf9e4849a21`.
Delivery plan: `0c3e80b982a20dab59c7339d31b4c9ccae810fc2`.
Code SHA: `9954e0833c565105816248c3e48b23efcfe68750`.
Main at pre-stage check: `71aa5c93afa5993c4e45ba39f1bf5454343389ea`, already an ancestor. No force push or merge to main.

## Implementation
- `libraries/include/aiverselib/algorithms/Noise.h`: added Billow to the existing fractal modes (signed Q16, 2*abs(n)-1, zero derivative at cusp); existing enum values preserved. `NoiseVectorSample<N>`, `vectorNoiseSample`, `vectorNoise`, `NoiseWarpOptions`, `domainWarpNoiseSample`, `domainWarpNoise`, `curlNoise`. 2D/3D independent deterministic seed domains, derivative rows of vector Jacobian; warp evaluates p+strength*displacement and applies transposed Jacobian to carrier derivative. Signed strength limited to eight lattice units; zero strength ignores displacement options and exactly preserves carrier. Checked split Q16 multiplication bounds both operands at 2^38 and each output at 2^60, keeping sum of three terms in int64. Invalid coordinates, periods, options and overflow-risk requests fail closed. Curl is approximate analytical curl of scalar potential (2D) or vector potential (3D); no guarantee of exact discrete divergence or normalization. No heap, no floats, no new target or dependencies.
- `libraries/catalog.json`: noise 0.3.0; same AIVERSE::lib_noise and random dependency. `skills/core/aiverse-noise/SKILL.md`, `skills/catalog.json`, `tools/skills/catalog.py`: API, example, aliases and routing metadata. Catalog JSON parses; full generated catalog check NOT RUN.
- `tests/libraries/noise_tests.cpp`: extends existing registered fixture, no duplicate CMake registration.
- `docs/libraries.md` and `docs/handoffs/algorithms-stage46-delivery.md`: roadmap, native API and pre-code delivery plan. GenNoise.h/cpp reviewed and untouched; no saved-world migration, ECS/renderer/Studio/graph change.

## Actual verification
Linux isolated C++20, exact reconstructed source bytes verified against Git blob hashes at code SHA:
- Noise.h: 0493428866477648e23b976802d8dc43b2173de4
- Random.h: 6576f72e3d2548f2b6810012c6622d076597c287
- noise_tests.cpp: 36997027038e0f1daa107442e9b67abc7a6a85b2

Working directory /tmp/aiverse-noise-stage46 (include/aiverselib/algorithms populated). All three commands exited 0 and printed "Noise stage 46 checks passed":
```sh
clang++ -std=c++20 -Wall -Wextra -Werror -pedantic -O2 -DNDEBUG -Iinclude noise_tests.cpp -o clang-noise && ./clang-noise
g++ -std=c++20 -Wall -Wextra -Werror -pedantic -Wno-misleading-indentation -O2 -DNDEBUG -Iinclude noise_tests.cpp -o gcc-noise && ./gcc-noise
clang++ -std=c++20 -Wall -Wextra -Werror -pedantic -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Iinclude noise_tests.cpp -o san-noise && ./san-noise
```
Clang 17.0.0, GCC 14.2.0. Stage 45 regression fixture plus 32 seeds per dimension, Billow derivative and value, deterministic vector seed and row contract, exact positive/negative period shifts, warp chain-rule vs finite difference, zero-strength identity, curl equations, invalid inputs and checked fixed multiplication boundaries. No complete repository checkout: GitHub DNS inaccessible in the Linux test container.

## Delivery checklist
Implementation/architecture/native API/dependencies/docs/skill/example present; isolated API fixture PASS. Headless arithmetic PASS; full engine headless NOT RUN. Full skill generation/audit, root and standalone CMake/CTest, unified runner, native module/export, platform SDKs and merged-tree Mac gate NOT RUN. Performance allocations bounded by inspection, actual measurement NOT RUN. MCP, Studio, inspector, graphs, persistence and visual publishing NOT APPLICABLE (pure stateless arithmetic). No production-ready claim or site publication. Separate stage handoff. No main merge.

## Next stage 47
Add deterministic 2D/3D Simplex noise with analytical derivative samples and integration as an optional fractal basis. Explicitly distinguish skew-lattice repetition from axis-aligned periods; reject unsupported axis-aligned tiling instead of silently producing seams. Keep previous output bits, limit coordinate arithmetic safely, update 0.4.0 metadata and fixture. After that: allocation-free batch sampling, SIMD dispatch with scalar parity and actual performance measurement, and full acceptance gates.
