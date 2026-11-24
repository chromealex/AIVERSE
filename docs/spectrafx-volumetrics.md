# SpectraFX Volumetrics — native CPU reference

SpectraFX owns **world-space visual** gas, smoke and combustion. PHYSIS and NEREID remain authoritative for environmental gameplay; PRISMA remains responsible for postprocessing and final HDR composition. The existing local fog system is analytic, not a dynamic gas solver. Do not use visual gas density for deterministic damage, navigation or visibility.

## Native API (Stage 151)

```cpp
#include "engine/fx/SpectraFX.h"
using namespace aiverse::spectra::volume;

Config cfg;
GasGrid grid;
if (!grid.init(cfg)) return;
Source source;
source.position = {0, -1, 0};
source.temperature = 8;
source.fuel = 4;
grid.setSources({source});
// Call once per visual 1/60-second tick, outside gameplay authority.
grid.step();
RayResult result;
// Opaque distance is supplied by the host, in world units.
grid.march({0, 0, -5}, {0, 0, 1}, 10, Material{}, result);
```

Dense cell-centred 8..64 cubic grid, smoke density, temperature, fuel and velocity. Continuous spherical sources or explicit impulse, first-order semi-Lagrangian transport, temperature/fuel reaction, decay, buoyancy relative to the supplied gravity vector, wind acceleration, approximate collocated Jacobi pressure projection (0..24 iterations). SDF obstacle resampling uses the existing signed-distance lattice; negative or zero cells become solid. SDF outside is fluid. Boundary velocity is constrained; backtrace is not a conservative solid sweep, numerical diffusion and leakage across thin obstacles remain possible. No conservative mass/energy or physically calibrated combustion claim. No dynamic liquid simulation.

The grid preallocates double cell buffers, solid mask and three pressure/divergence buffers. At 64 cubed, logical arrays total about 15.25 MiB (assuming Cell is 24 bytes); `memoryBytes()` reports actual vector capacities, not all object/allocator overhead. Work scales with cells, sources and pressure iterations. No per-tick allocation in the solver. This is a reference implementation, not a measured real-time CPU budget.

`march` computes scene-linear emission/absorption and opacity along a bounded ray, clipped to supplied opaque depth, with 1..256 steps. It does not draw a frame or implement light scattering/shadows. A later GPU world-space renderer will composite into scene HDR before PRISMA. No GPU volume backend, full viewport rendering or hybrid execution is installed yet.

## Authored volume asset

See `examples/spectrafx/volumes/fire.volume.json` and `smoke.volume.json`. Format `aiverse.spectrafx.volume`, version 1. Use `readAsset`, `writeAsset`, `initAsset` (optional explicit SDF reader for `content/*.sdfbin`). Strict field validation, atomic initialization, maximum 16 sources. Particle-only graph compiler remains unchanged and does not accept this document as a particle graph. Unified graph nodes, Studio authoring and hybrid runtime are future stages.

MCP/Workspace operations `fx_volume_validate` and `fx_volume_probe` use the same native reference, check active project and resolve optional obstacle through the bounded project SDF reader. Probe runs at most eight ticks with an additional cell-work cap, returns aggregate field sums and optional ray sample. These operations do not write files, produce an image or mutate scene state. No persistent runtime checkpoint yet; authored JSON is a configuration, not a savegame.

## Verification

Fixture source in existing `vfx_graph_runtime` and `vfx_graph_ops` targets. Build, CTest, GPU, native visual capture and performance verification NOT RUN. No hardware or production-readiness claim.
