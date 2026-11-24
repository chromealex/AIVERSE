---
name: aiverse-noise
description: Deterministic periodic value, gradient, cellular and fractal noise for native C++ projects.
---
Use aiverselib/algorithms/Noise.h. Q16 coordinates and integer lattice periods (zero means nonperiodic). periodicValueNoise<2/3> returns signed Q16; cellularNoise<2/3> returns F1/F2 squared Q32, periodic feature images counted separately. Existing engine/game/gen/GenNoise.h remains authoritative for legacy gradient/FBM/ridge/warp sequences; do not replace saved-world generation. No tests.

Stage 45: link `AIVERSE::lib_noise`. `periodicValueNoiseSample<2/3>` preserves Stage 44 value bits and adds Q16 derivatives per lattice unit; `periodicGradientNoiseSample<2/3>` returns value and derivative; `periodicGradientNoise` returns value only. Derivatives approximate the analytical smoothstep interpolant, not the discontinuous integer quantization. Use `fractalNoiseSample` / `fractalNoise` with `NoiseFractalOptions`: FBm, Turbulence or Ridged, value or gradient basis, 1..16 octaves, integer lacunarity 1..8, gain Q16 0..65536. Fractals normalize by weight sum, scale periods per octave and apply chain rule. Ridge = 1-clamp(abs(value),0,1); at cusps use zero derivative. Entire request returns nullopt on invalid input or exceeded scaled limits (coordinate +/-2^46 Q16, period <=1e8, frequency <=65536). Stateless and allocation-free; no claim of cross-platform verification or performance benchmark yet.

Example:
```cpp
#include <aiverselib/algorithms/Noise.h>
using namespace aiverse::lib::algorithms;
auto s = fractalNoiseSample<2>({-32768, 98304}, 7, {16, 16});
if (s) { /* s->value and s->derivative are signed Q16 */ }
```
