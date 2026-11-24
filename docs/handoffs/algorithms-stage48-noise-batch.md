# Algorithms Stage 48 — allocation-free noise batch sampling

Date: 2026-11-24. Branch: `feature/world-processes-stage31-bidirectional-handles`.
Prior Stage 47 HEAD: `bc34f52702f3cb845f37e2083fe3aa5bbc5e3af2`.
Delivery plan: `c6daec7ee0c6ee791b74ba52ba25e19766df7e29`.
Code SHA: `e88e470e012e98cbbeaf0fbcd1a3f19f19180714`. Main at pre-stage check `71aa5c93afa5993c4e45ba39f1bf5454343389ea` was already an ancestor; no force push or main merge.

## Implementation
- `libraries/include/aiverselib/algorithms/NoiseBatch.h`: borrowed `NoiseBatchPoints<N>` for AoS point arrays or equal-length SoA coordinate spans (2D/3D). Caller owns memory. Output is a typed contiguous span, AoS for compound results; shared seed, period, options; exact index order and scalar parity across partitions. No allocation, hidden state, threading or SIMD.
- `noiseBatchSamples` and `noiseBatchValues` expose value, gradient, Cartesian simplex, skew simplex, fractal and domain warp via `NoiseScalarBatchOptions`; `noiseBatchCellular` returns F1/F2 squared Q32; `noiseBatchVectorSamples`, `noiseBatchVectors`, `noiseBatchCurl` reuse scalar fractal APIs. The original Noise.h and Random.h are unchanged. No new solver, no GenNoise replacement or changed saved-world sequences.
- `NoiseBatchPolicy::Atomic` preflights every element, writes nothing if any is invalid, otherwise reevaluates and writes; optional full mask filled with ones on success. `MarkInvalid` requires full uint8 mask (except empty batch); one pass, valid values written, invalid output slots untouched, mask entries 0/1. Report status, written, invalidCount, firstInvalid. Atomic invalid report counts all invalid points, written=0. Invalid shape/options/overlap never write. SoA read-only axes may alias; no overlap between writable spans or between writable span and any input span. Inputs must be alive and stable for the synchronous call. Settings validation reuses scalar fractal validation at origin; point-dependent warp bounds are checked by each scalar call. No performance claim for double evaluation.
- `libraries/catalog.json`: noise 0.5.0, existing target and random dependency. Reviewed libraries/CMakeLists.txt: per-package INTERFACE target includes whole libraries/include; no separate target or source registration needed for new header.
- `skills/core/aiverse-noise/SKILL.md`, `skills/catalog.json`, `tools/skills/catalog.py`: API, example, routing and aliases. JSON parsed; full generator/audit not run.
- `tests/libraries/noise_tests.cpp`: extended existing library_noise test, no new CMake registration.
- `docs/libraries.md` and `docs/handoffs/algorithms-stage48-delivery.md`: roadmap and pre-code delivery scope.

## Actual verification
Linux isolated C++20 fixture under /tmp/aiverse-noise-stage48. Source blob hashes (new header and test verified against GitHub commit before updating branch, unchanged headers against Stage 47):
- NoiseBatch.h: 1ae8a1f58252fd099d91009bdbc8453d80cc127e
- Noise.h: e5377b3bbf1265b75a6a7220b8f72581c7f59572
- Random.h: 6576f72e3d2548f2b6810012c6622d076597c287
- noise_tests.cpp: 16e5d50566729818eed85cd22d646a2e5d4e9a9b

All three commands exited 0 and printed "Noise stage 48 checks passed" (Clang 17.0.0, GCC 14.2.0):
```sh
clang++ -std=c++20 -Wall -Wextra -Werror -pedantic -O2 -DNDEBUG -Iinclude noise_tests.cpp -o clang-noise && ./clang-noise
g++ -std=c++20 -Wall -Wextra -Werror -pedantic -Wno-misleading-indentation -O2 -DNDEBUG -Iinclude noise_tests.cpp -o gcc-noise && ./gcc-noise
clang++ -std=c++20 -Wall -Wextra -Werror -pedantic -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Iinclude noise_tests.cpp -o san-noise && ./san-noise
```
Standalone header example from skill, wrapped in main with success check, also compiled and exited 0:
```sh
clang++ -std=c++20 -Wall -Wextra -Werror -pedantic -Iinclude native_batch_example.cpp -o native-batch-example && ./native-batch-example
```
Fixture reruns Stage 45–47 regression and exercises 19 points per dimension, all six scalar kinds, both layouts and policies, scalar and derivative parity, arbitrary four-point partition and odd tail, cellular/vector/curl, mask and output sentinel preservation, invalid shape/enum/period/frequency, input/output and output/mask byte overlap, read-only axis alias, empty batches, invalid elements at two different positions, zero-strength warp contract. No end-to-end native module/export claim.

## Delivery checklist
Implementation/architecture/native API/dependencies/docs/skill/example present; isolated Linux fixture and standalone header example PASS. Full skill generation/audit, CMake/CTest, unified runner, actual native module/export, other platform SDKs, engine headless and merged-tree Mac gate NOT RUN. Allocation-free by implementation inspection; performance measurement NOT RUN. MCP, Studio, inspector, graphs, persistence and visual publishing NOT APPLICABLE (stateless arithmetic). No production-ready or site publication claim. Separate committed handoff.

## Next stage 49
Implement explicit scalar/SIMD dispatch for supported noise kernels; start with periodic value noise, preserving integer division truncation, negative floor, hash overflow semantics, wrapped periods, both input layouts and batch policies. Use compile/runtime feature gates and scalar fallback for unsupported targets/kernels. Verify bit-for-bit parity across lanes, tails, large coordinates, periods, seed and failure masks. Do not advertise NEON/SSE/AVX hardware as verified without real execution. Keep measured throughput and full platform integration separate; no changes to legacy GenNoise.
