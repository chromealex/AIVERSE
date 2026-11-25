# Algorithms Stage 50 — reproducible native Value Noise benchmark

Date: 2026-11-24. Branch: feature/world-processes-stage31-bidirectional-handles.
Benchmark source commit: 9df59497a83bde786c8ad5762370372b28c52cb4.
Adds standalone tests/libraries/noise_benchmark.cpp (C++20, existing headers only). No kernel, default dispatch, package version, ABI or serialized output changes.
Matrix: 2D/3D, AoS/SoA, 17/1024/65536 points, zero/nonzero periods, Atomic/MarkInvalid, Scalar/Auto/SSE2/AVX2/NEON. One warmup per available backend, seven rotated-order trials, at least one batch per trial. Fixed deterministic input and seed. Full scalar output and mask parity outside timing; checksum plus report and backend checks. CSV min/median/max ns/sample, actual backend, repeats, checksum; unavailable backends explicitly marked. Timing includes report checks and an observable output read, not full checksum. All points valid; mixed-invalid performance separate.

From repository root:
```sh
c++ -std=c++20 -O3 -DNDEBUG -I libraries/include tests/libraries/noise_benchmark.cpp -o /tmp/aiverse-noise-bench
/tmp/aiverse-noise-bench "$(uname -sm); $(sysctl -n machdep.cpu.brand_string 2>/dev/null || grep -m1 'model name' /proc/cpuinfo)" > noise-benchmark.csv
```
Run natively on Apple Silicon and x86; record CPU/compiler/flags, power mode and repeated runs. For scalar-only build add -DAIVERSE_NOISE_DISABLE_SIMD. Do not use global ISA flags for portable dispatch. Profile preparation/hash/fade versus interpolation before changing the kernel or Auto policy. Do not infer speedup from SIMD availability.
Verification: source/API inspection only; no build, execution, measured timings or profiling in this turn (runtime lacks repository network access). Next: run benchmark and existing library_noise test, compare CSV, optimize measured bottlenecks; preserve all output bits. No main merge.
