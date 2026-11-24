---
name: aiverse-noise
description: Deterministic scalar, simplex, fractal, vector, domain-warp, curl and batch noise for native C++ projects.
---
Use aiverselib/algorithms/Noise.h. Q16 coordinates and integer lattice periods (zero means nonperiodic). periodicValueNoise<2/3> returns signed Q16; cellularNoise<2/3> returns F1/F2 squared Q32, periodic feature images counted separately. Existing engine/game/gen/GenNoise.h remains authoritative for legacy gradient/FBM/ridge/warp sequences; do not replace saved-world generation. See registered `library_noise` fixture; Stage 44 alone had no tests.

Stage 45: link `AIVERSE::lib_noise`. `periodicValueNoiseSample<2/3>` preserves Stage 44 value bits and adds Q16 derivatives per lattice unit; `periodicGradientNoiseSample<2/3>` returns value and derivative; `periodicGradientNoise` returns value only. Derivatives approximate the analytical smoothstep interpolant, not the discontinuous integer quantization. Use `fractalNoiseSample` / `fractalNoise` with `NoiseFractalOptions`: FBm, Turbulence or Ridged, value or gradient basis, 1..16 octaves, integer lacunarity 1..8, gain Q16 0..65536. Fractals normalize by weight sum, scale periods per octave and apply chain rule. Ridge = 1-clamp(abs(value),0,1); at cusps use zero derivative. Entire request returns nullopt on invalid input or exceeded scaled limits (coordinate +/-2^46 Q16, period <=1e8, frequency <=65536). Stateless and allocation-free; no claim of cross-platform verification or performance benchmark yet.

Example:
```cpp
#include <aiverselib/algorithms/Noise.h>
using namespace aiverse::lib::algorithms;
auto s = fractalNoiseSample<2>({-32768, 98304}, 7, {16, 16});
if (s) { /* s->value and s->derivative are signed Q16 */ }
```

Stage 46: Billow mode = 2*abs(n)-1 (signed Q16, zero derivative at the cusp). Use vectorNoiseSample<N> for independently seeded fractal fields; result[i].value is component i and result[i].derivative[j] the Jacobian row. vectorNoise<N> returns the values. Use NoiseWarpOptions with carrier/displacement fractal options and signed strengthQ16 in [-8*65536,8*65536]; domainWarpNoiseSample<N> evaluates p + strength*displacement(p) and returns chain-rule derivatives, domainWarpNoise<N> returns the scalar. Zero strength exactly preserves the carrier and ignores displacement options. Requests outside supported bounds return nullopt; no clamping of coordinates. curlNoise<2> = (dF/dy,-dF/dx), curlNoise<3> = curl of vectorNoiseSample; not a discrete divergence-free guarantee and not normalized. All APIs share the original lattice period and deterministic seed domains; no allocation or legacy GenNoise changes.

```cpp
NoiseWarpOptions warp{};
warp.carrier.octaves = 4;
warp.displacement.octaves = 2;
auto warped = domainWarpNoiseSample<3>({-32768, 98304, 0}, 7, {16,16,16}, warp);
auto flow = curlNoise<3>({-32768, 98304, 0}, 7, {16,16,16});
```

Run `library_noise` and platform/native gates before claiming acceptance. SIMD Value batch interpolation is available in Stage 49.

Stage 47: use `simplexNoiseSample<N>(p, seed)` and `simplexNoise<N>(p, seed)` for Cartesian aperiodic simplex gradient noise in 2D/3D. Signed Q16 coordinates, values and derivatives; the scale is fixed at 48, not peak-normalized. The rational skew transform uses F=23988/65536 (2D) or 1/3 (3D), G=F/(1+N*F). Derivatives approximate the underlying continuous kernel, not the integer steps. `NoiseBasis::Simplex` is available in all fractal modes, vector, warp and curl; any nonzero Cartesian period is rejected for this basis. Existing bases and enum values are unchanged.

For exact skew-lattice repetition use `simplexSkewNoiseSample<N>(s, seed, period)`, where s is **already skewed** (s = p + F*sum(p)); derivatives are per skew-coordinate unit. Periods are lattice cells along skew axes, not Cartesian x/y/z periods. Period zero is aperiodic. Positive and negative shifts by period*65536 match value and derivatives bit-for-bit within coordinate bounds. No axis-aligned Cartesian simplex tiling or periodic simplex fractal wrapper is promised. All direct inputs are bounded to +/-2^46 Q16; invalid requests return nullopt. Do not replace GenNoise or change saved-world sequences.

```cpp
NoiseFractalOptions o{};
o.basis = NoiseBasis::Simplex;
auto f = fractalNoiseSample<3>({-32768, 98304, 0}, 7, {}, o);
auto tile = simplexSkewNoiseSample<2>({-32768, 98304}, 7, {8, 8});
```

Run the existing `library_noise` test; golden samples, derivative finite differences, seam and bound checks are included. Full platform and performance acceptance remains separate.

Stage 48: include `<aiverselib/algorithms/NoiseBatch.h>` using the same `AIVERSE::lib_noise` target. Create a borrowed `NoiseBatchPoints<N>::aos(span<const array<int64_t,N>>)` or `::soa(array<span<const int64_t>,N>)`; input axes must have equal lengths and may alias each other. Supply caller-owned contiguous typed output (AoS for compound samples); no writable output or mask may overlap any input or each other. Every point uses the same seed, period and options, and results are in input order. Scalar `noiseBatchSamples` returns NoiseSample<N>, `noiseBatchValues` returns int64 Q16; select Value, Gradient, Simplex, SimplexSkew, Fractal or DomainWarp via NoiseScalarBatchOptions.kind. `noiseBatchCellular` returns CellularDistance Q32; `noiseBatchVectorSamples`, `noiseBatchVectors`, `noiseBatchCurl` use the existing fractal options. Cartesian Simplex still rejects nonzero Cartesian periods.

`NoiseBatchPolicy::Atomic` (default) validates all elements in a read-only pass and evaluates again to write only if all are valid; optional full-length mask receives ones on success. `MarkInvalid` requires a full-length uint8_t mask for nonempty batches, evaluates once, writes only valid elements, and sets mask entries to 1/0. Invalid element output remains unchanged. Invalid shape/options/overlap writes nothing in either policy, including the mask. Report status, written, invalidCount, firstInvalid (size_t max if none); Atomic invalid-element report counts every invalid point, written=0. Empty valid batches succeed; config is still validated. Spans are synchronous and must remain alive and stable; no heap or threading guarantee. Partition into disjoint spans for independent jobs; no concurrent mutation of input. Compound output is AoS, not SoA output. The existing scalar kernels define all bits.

```cpp
#include <aiverselib/algorithms/NoiseBatch.h>
using namespace aiverse::lib::algorithms;
std::array<std::array<int64_t,2>,2> p{{{0,0},{32768,-32768}}};
std::array<int64_t,2> out{};
NoiseScalarBatchOptions o{}; o.kind=NoiseScalarKind::Gradient;
auto r=noiseBatchValues<2>(NoiseBatchPoints<2>::aos(p),out,7,{8,8},o);
// r.complete() and r.written==2 on success.
```

Run `library_noise` for scalar parity, both layouts, atomic/partial errors, overlap and regressions. No new CMake target or dependency.

Stage 49: `NoiseScalarBatchOptions.backend` is Auto (default), Scalar, SSE2, AVX2 or NEON. `noiseBatchBackendAvailable(backend)` reports compiled and CPU/OS-supported availability. Only `noiseBatchValues<N>` with kind Value uses SIMD interpolation; all derivative samples and other noise kernels remain scalar. Auto selects AVX2 for >=4 points, NEON or SSE2 for >=2, otherwise Scalar. Explicit unsupported or too-short preferences fall back to Scalar; invalid enum rejects without writes. AVX2 uses per-function target and runtime CPU/OS detection on GCC/Clang x86 with SSE2 baseline (not MSVC); SSE2 and AArch64 NEON require their compile-target baselines. No global ISA flags needed. Other targets use Scalar. Build-wide `AIVERSE_NOISE_DISABLE_SIMD` disables the SIMD paths (must agree in every translation unit). Full-width blocks only, scalar tail; report.backend is the SIMD kernel actually invoked or Scalar if none, including failed Atomic and all-invalid batches. It is not a promise every element ran SIMD. Only interpolation is vectorized; shared lattice preparation/hash/fade remain scalar. The SIMD signed Q16 quotient truncates toward zero exactly like scalar. Atomic Value validates point bounds without duplicating hashing. Batch masks, aliasing rules and result bits are unchanged. No measured throughput improvement is claimed yet; NEON not executed in the Linux x86 validation.

```cpp
o.kind=NoiseScalarKind::Value;
o.backend=NoiseBatchBackend::Auto;
auto result=noiseBatchValues<2>(NoiseBatchPoints<2>::aos(p),out,7,{8,8},o);
// result.backend is Scalar for a batch shorter than the selected SIMD width.
```

Run `library_noise` on each ISA, and separately with `-DAIVERSE_NOISE_DISABLE_SIMD` for the fallback. Package 0.6.0. Performance and full platform gates remain separate.
