# Algorithms Stage 47 — deterministic Simplex noise

Date: 2026-11-24. Branch: `feature/world-processes-stage31-bidirectional-handles`.
Prior Stage 46 HEAD: `869f7530b504e7d5db8e36c989d2f869db19d146`.
Delivery plan: `11b980c4f36726703ea8af1412b53506fd04d15b`.
Code SHA: `4a7d23b51c4a75b7899290700ab732e1d947b396`. Main at pre-stage check `71aa5c93afa5993c4e45ba39f1bf5454343389ea` is already an ancestor. No force push or merge to main.

## Implementation
- `libraries/include/aiverselib/algorithms/Noise.h`: `simplexNoiseSample<N>` / `simplexNoise<N>` for 2D/3D Cartesian aperiodic input, `simplexSkewNoiseSample<N>` for exact skew-lattice repetition, and `NoiseBasis::Simplex` in the existing fractal, vector, warp and curl composition. No previous enum values, defaults, kernels or saved-world generation changed.
- Rational skew transform F=23988/65536 in 2D and 1/3 in 3D; G=F/(1+N*F) is used on local fractions, avoiding large-coordinate unskew drift. Fixed axis tie order, N+1 vertices, existing deterministic gradient/hash, radial t^4 kernel with radius squared slightly below 1/2 to omit opposing-face vertices safely. Q30 accumulation, fixed scale 48 (not peak-normalized), Q16 outputs and analytical first derivatives. No allocations or floating point. Input limits +/-2^46 Q16, period 0..100000000 in skew lattice cells. Nonzero Cartesian periods with Simplex basis are rejected rather than silently returning a non-tileable signal. Skew derivatives are transformed by the inverse skew matrix. Quantization means derivative is an approximation of the underlying continuous kernel. No Cartesian simplex tiling or skew-periodic fractal wrapper claimed.
- `libraries/catalog.json`: noise 0.4.0, same AIVERSE::lib_noise target and random dependency.
- `skills/core/aiverse-noise/SKILL.md`, `skills/catalog.json`, `tools/skills/catalog.py`: native example, coordinate and period contract, aliases. Both JSON files parsed successfully; full generation/audit not run.
- `tests/libraries/noise_tests.cpp`: extends existing registered library_noise fixture; no CMake duplication. `docs/libraries.md` and `docs/handoffs/algorithms-stage47-delivery.md`: roadmap and pre-code scope.

## Actual verification
Linux isolated C++20 source fixture, based on the exact Stage 46 blobs reconstructed locally in /tmp/aiverse-noise-stage47. The tested file hashes were verified against the new commit before updating the branch:
- Noise.h: e5377b3bbf1265b75a6a7220b8f72581c7f59572
- Random.h (unchanged): 6576f72e3d2548f2b6810012c6622d076597c287
- noise_tests.cpp: 82818c6cfe3dcb9a2148471c376c39895eebdcb3

All three commands exited 0 and printed "Noise stage 47 checks passed" (Clang 17.0.0, GCC 14.2.0):
```sh
clang++ -std=c++20 -Wall -Wextra -Werror -pedantic -O2 -DNDEBUG -Iinclude noise_tests.cpp -o clang-noise && ./clang-noise
g++ -std=c++20 -Wall -Wextra -Werror -pedantic -Wno-misleading-indentation -O2 -DNDEBUG -Iinclude noise_tests.cpp -o gcc-noise && ./gcc-noise
clang++ -std=c++20 -Wall -Wextra -Werror -pedantic -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Iinclude noise_tests.cpp -o san-noise && ./san-noise
```

Existing Stage 45/46 fixtures rerun. New checks cover golden value/derivative samples per dimension, 128 seeds per dimension, 1024 PCG-random coordinate/seed pairs per dimension, Cartesian and skew finite differences, exact positive and negative skew period shifts, continuity across rank and cell boundaries, extreme coordinate and invalid period/basis rejection, 6-octave high-frequency Billow and frequency-limit rejection, single-octave equivalence, all fractal modes, and integration with vector/warp/curl. Additional local diagnostic probe (not a benchmark) sampled 10000 coordinate/seed pairs per dimension; observed max Cartesian finite difference error 932 Q16 in 2D, 616 in 3D, max skew error 571/513. These are sampled observations, not global bounds.

## Delivery checklist
Implementation/architecture/native API/dependencies/docs/skill/example present; isolated headless API and regression fixture PASS on Linux Clang/GCC/ASan/UBSan. Skill catalog generation/audit, root and standalone CMake/CTest, unified runner, native module/export, other platform SDKs, full engine headless and mandatory merged-tree Mac gate NOT RUN. Performance work bounded by inspection (N+1 vertices, fixed stack), measurements NOT RUN. MCP, Studio, inspector, graph authoring, persistence and visual publishing NOT APPLICABLE (stateless arithmetic). No site publication or production-ready claim. Separate committed handoff.

## Next stage 48
Add allocation-free batch sampling with caller-owned spans, explicit invalid-element policy and no partial writes for invalid shape/options; cover scalar, derivative, vector, warp and curl APIs. Define AoS/SoA behavior and overlap rules. Then add portable SIMD dispatch with bit-for-bit scalar parity, benchmark measurements, platform acceptance and full CMake/catalog checks. Preserve all golden outputs; no legacy GenNoise replacement.
