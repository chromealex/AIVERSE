# Algorithms Stage 49 — deterministic SIMD Value Noise batch interpolation

Date: 2026-11-24. Branch: `feature/world-processes-stage31-bidirectional-handles`.
Prior Stage 48 HEAD: `cff56172e7fe62f28494c9ad916d0a44cafbe45b`.
Delivery plan: `817704eb99111ebe8f4252bd1bde432418a64cd9`.
Code SHA: `32fb965641cb9169c9b0c88f862aea455836baa4`. Main at pre-stage check `71aa5c93afa5993c4e45ba39f1bf5454343389ea` was already an ancestor. No force push or main merge.

## Implementation
- `libraries/include/aiverselib/algorithms/Noise.h`: extract canonical valueNoiseCorners helper (scalar floor, wrapping, hash, fade); old single-point API and interpolation order unchanged. No other kernels, seed domains, GenNoise or saved-world sequences changed.
- `libraries/include/aiverselib/algorithms/NoiseSimd.h`: actual SSE2 (2 lanes), AVX2 (4 lanes), AArch64 NEON (2 lanes) integer interpolation of prepared Value Noise lattice corners. Signed differences fit 32 bits, products use 64 bits; magnitude/sign on x86 or signed bias before shift on NEON preserves division truncation toward zero. No heap, floats, new solver or new dependencies. Hash/fade and gathering remain scalar. No claim of faster throughput before benchmarks.
- `NoiseBatch.h`: NoiseScalarBatchOptions.backend (Auto default, Scalar, SSE2, AVX2, NEON), noiseBatchBackendAvailable, NoiseBatchReport.backend records actually invoked SIMD backend or Scalar if none. Auto chooses AVX2 for >=4, NEON or SSE2 for >=2, scalar otherwise. Explicit unavailable/too-short preferences fallback to scalar; unknown enum rejects without writes. GCC/Clang x86 AVX2 uses per-function target attribute and builtin CPU/OS check, SSE2/NEON only for corresponding compile baselines. No global ISA flags required; build-wide AIVERSE_NOISE_DISABLE_SIMD disables all SIMD paths, must agree across translation units. MSVC x64 SSE2 source path, MSVC AVX2 not provided. Other architectures/targets scalar. Full blocks and scalar tails, invalid slots never written, both policies and layouts preserved. Atomic Value can preflight just point bounds (exact validity condition), no double hash evaluation. Shared validation handles shape, options, overlap. SIMD only in noiseBatchValues kind Value; other values, derivative samples, vector, cellular, warp and curl remain scalar. Report backend is not a per-element count. Aggregate report/options fields appended; consumers using structured bindings or compiled layout assumptions must rebuild.
- `libraries/catalog.json`: noise 0.6.0; same AIVERSE::lib_noise target and random dependency. `skills/core/aiverse-noise/SKILL.md`, `skills/catalog.json`, `tools/skills/catalog.py`: API and aliases; JSON parsed, full generation/audit not run. `docs/libraries.md` and `docs/handoffs/algorithms-stage49-delivery.md`: roadmap and pre-code scope.
- `tests/libraries/noise_tests.cpp` includes `tests/libraries/noise_simd_tests.inc`; existing library_noise registration unchanged.

## Actual verification
Linux x86_64 Intel Xeon Platinum 8573C, SSE2 and AVX2 available, Clang 17.0.0 and GCC 14.2.0. Isolated exact source fixture under /tmp/aiverse-noise-stage49, previous Stage 48 source blob hashes checked before changes; tested new source blobs matched commit before ref update:
- Noise.h: 14121ab0d92da994e9e2438dacddb09eef41270b
- NoiseSimd.h: 94dd1c1f6fbf1c178d832ea875fcf615d95938e3
- NoiseBatch.h: 8613ab62d65796e3fb579921cceb4b9a9c5cec06
- Random.h: 6576f72e3d2548f2b6810012c6622d076597c287
- noise_tests.cpp: a563a398d2e63a419f78ad187c4f580ae9b1db89
- noise_simd_tests.inc: d1e4e7381bd024416d48cf712bd6a61e9f98121c

All four commands exited 0:
```sh
clang++ -std=c++20 -Wall -Wextra -Werror -pedantic -O2 -DNDEBUG -Iinclude noise_tests.cpp -o clang-noise && ./clang-noise
g++ -std=c++20 -Wall -Wextra -Werror -pedantic -Wno-misleading-indentation -O2 -DNDEBUG -Iinclude noise_tests.cpp -o gcc-noise && ./gcc-noise
clang++ -std=c++20 -Wall -Wextra -Werror -pedantic -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Iinclude noise_tests.cpp -o san-noise && ./san-noise
clang++ -std=c++20 -Wall -Wextra -Werror -pedantic -O2 -DNDEBUG -DAIVERSE_NOISE_DISABLE_SIMD -Iinclude noise_tests.cpp -o scalar-noise && ./scalar-noise
```
First three printed "Noise stage 49 checks passed; SSE2=1 AVX2=1 NEON=0"; disabled build printed "Noise stage 49 checks passed; SSE2=0 AVX2=0 NEON=0". objdump of native Clang binary contains pmuludq xmm and vpmuludq ymm instructions; fixture asserts executed report backend for full valid blocks. 64 seeds x 65 points x 2 dimensions = 8320 base samples, repeated under both layouts/policies and each explicit/Auto preference; periods 0, 1, small, 100000000; extreme +/-2^46 Q16, negative coordinates, lattice boundaries, lengths 0..9, odd tails, mixed and all-invalid lanes, invalid enum, untouched atomic/mask buffers, derivative API scalar fallback, scalar value vs prior derivative sample parity, and Stage 45–48 regressions. No full repository build.
AArch64 NEON: extracted exact lerpNeon function into freestanding intrinsic probe with arm_neon.h and noiseOne definition, compiled via `clang++ -target aarch64-none-elf -ffreestanding -std=c++20 -Wall -Wextra -Werror -O2 -S neon_intrinsic_probe.cpp -o neon_intrinsic_probe.s`, exit 0; assembly contains smull and vector sub/and. This is only intrinsic syntax/codegen evidence, not whole-header target build or hardware execution. No aarch64 sysroot/emulator present. NEON parity and Apple Silicon hardware execution NOT RUN.

## Delivery checklist
Implementation/architecture/native API/dependencies/docs/skill/example present, Linux isolated fixture PASS (native SSE2/AVX2 and fallback). Full skill generation/audit, CMake/CTest, unified runner, native module/export, MSVC, ARM64 full compile/execution, Web/mobile SDKs, full engine headless, mandatory merged-tree Mac gate NOT RUN. Performance measurements NOT RUN; interpolation SIMD only, possible preparation overhead. MCP, Studio, inspector, graphs, persistence, visual publishing NOT APPLICABLE (stateless arithmetic). No production-ready claim, main merge or site publication. Separate committed handoff.

## Next stage 50
Add reproducible native benchmark for scalar vs Auto/forced SSE2/AVX2/NEON, AoS and SoA, 2D/3D, varied sizes and periods, both Atomic/MarkInvalid, checksum parity and actual executed backend; report warm-up, multiple trials and median ns/sample with compiler/CPU. Profile hash/wrap/fade/preparation vs SIMD interpolation and optimize only measured bottlenecks. If Auto regresses, use measured policy or keep SIMD opt-in until improved; never claim speedup without measurements. Expand SIMD beyond Value only after parity and profiling justify it. Then full platform and catalog/native acceptance gates; preserve all existing output bits.
