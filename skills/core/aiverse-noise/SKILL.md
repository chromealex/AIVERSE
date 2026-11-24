---
name: aiverse-noise
description: Deterministic periodic scalar, fractal, vector, domain-warp and curl noise for native C++ projects.
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

Run `library_noise` and platform/native gates before claiming acceptance. Simplex and SIMD are separate future stages.
