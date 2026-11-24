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

## Stage 152 — dense GPU gas simulation (source; QA deferred)

GpuGasGrid and SpectraVolumeGpuShaders implement Metal/Vulkan native dense 8..64 gas simulation through the shared Device storage-uniform compute ABI, same authored Config/Source/Asset and obstacle sampling as CPU. 32-byte cells, triple state buffers, uint solid mask, double pressure and divergence (112 bytes/cell = 28 MiB at 64 cubed); exact logical buffer reservation via canonical Device ledger, fence-retiring shutdown. First-frame checked mask upload and clear. Fixed 60 Hz advection/reaction/forces/sources/walls, divergence/pressure reset, bounded Jacobi, projection/walls; successful candidate publication only, failed tick retains previous completed state and queued impulse. CPU impulse is immediate, GPU queued impulse applies after advection on next accepted tick. No automatic CPU fallback. Borrowed frame-scoped GPU field view and explicit diagnostic snapshot, no normal-step voxel readback. Native API public through SpectraFX.h. Actual GPU render, lighting, graph/Studio and checkpoint/package integration not installed yet. Metal/Vulkan allocator regression source compares CPU fields/velocity with tolerances, SDF mask, wind, impulse, invalid reinit, budget denial and clear. Shader compilation, CMake/CTest, hardware/parity/performance NOT RUN. Next Stage 153 real depth-aware HDR volume rendering.

## Stage 153 — native GPU HDR volume renderer (source; QA deferred)

SpectraGpuVolumeRenderer.h + SpectraVolumeRenderShaders.h consume current GpuGasGrid cells and solid mask directly, no CPU voxel readback. Compute uses existing TextureResources layout, separate RGBA16F storage output, matching opaque scene depth/color, inverse VP reconstruction and AABB clipping, camera-inside/ortho-compatible ray construction, trilinear fields, Beer-Lambert extinction, HDR temperature emission, ambient/sun Henyey-Greenstein scattering and bounded density self-shadowing, up to four explicit point lights. Bounds: 128 primary steps, 8 shadow steps, <=4,194,304 pixels; one volume per dispatch. Canonical Device ledger output (8 B/pixel) and fence retirement. PRISMA generic frame/view-scoped one-shot SceneHdrContributor callback after local fog before metering/bloom/tonemap; host owns renderer, no PRISMA volume shader or effect duplicate. HDR-capable native Metal/Vulkan only; no silent LDR fallback, no automatic host/Studio wiring yet, no scene shadow-map sampling, no unified overlapping media, no transparent-depth correctness or performance claims. Existing allocator fixture sources test HDR emission >1, opaque clipping, empty grid, allocation return and Vulkan shader compilation. CMake/CTest/shader/real GPU/visual acceptance NOT RUN. Next unified volume graph and Studio/scene host integration.

## Stage 154A — volume/hybrid Graph v2 and Studio preview (source; QA deferred)

SpectraVolumeGraph.h provides compilePresentationGraph splitting one independent ordered volume branch (VolumeDomain, repeated VolumeSource, VolumeAdvection, VolumeCombustion, VolumePressure, VolumeOutput) from existing particle Graph v2. Strict no cross-domain/value links, one output and ordered chain; native asset parser remains authoritative. Native presets fire/smoke/magic/fog, existing fx_graph_edit newVolume/addVolume, catalog, validation, save/get/draft; legacy particle bake rejects volume rather than omitting it. Canvas preset creation and selected volume property JSON editor, native reload; StudioSpectraPreview loads GPU grid/renderer alongside optional particle runtime with candidate-before-swap, native fixed ticks and HDR dock contribution (map contribution only if HDR enabled). Volume-only uses continuous sources; hybrid executes both, no exchange/coupling or cross-runtime transactional tick. Replay/checkpoint unsupported and rejected. Native status shows volume resolution, logical bytes and successful HDR command submission (not completion). No automatic gameplay instance, explosion impulse authoring, typed data ports, fully specialized inspector, sparse/LOD, scene shadow maps or package closure yet. Tests and examples source, actual CMake/TypeScript/Metal/Vulkan/Studio QA NOT RUN. Next Stage 154B finish game runtime, typed inspector and hybrid controls, then Stage 155 coupling/sparse.

## Stage 154B — scene volume sources and authored impulses (source; QA deferred)

Volume asset v1 now supports one optional impulse, duration .001..1 seconds and signed radial acceleration in Source. Graph VolumeSource mode continuous/impulse, one impulse per branch; Explosion preset injects heat/fuel/density and outward velocity once, not persistent fuel. CPU reference init injects immediately; GPU queued impulse applies on first successful tick. Native VolumeInstance owns placed grid + HDR renderer; local domain, source and SDF origins translated by scene anchor. Studio preview reuses instance; Restart rearms authored impulse, Clear cancels, Trigger explosion queues one pulse and paused preview requests a tick. SceneVolumeWorld admits at most four fixed-anchor volumes, four admission attempts/sync, four ticks/frame, revision/effect changes recreate, detach removes immediately; back-to-front approximate per-volume HDR composition, no unified overlap. Existing SpectraSource ECS identity/visible presentation reused in ClientFx and StudioSpectraSources; hybrid particle branch still existing presenter. Flat maps/unit scale only, source movement does not translate live volume, rotation and planet support pending. HDR must be active, no forced map quality change. Studio typed numeric/vector/mode/path fields replace manual JSON and expose optional settings; obstacle path typed manually (indexed picker pending). Tests source for preset/codec/radial impulse, anchor/revision/detach and admission; no actual builds, GPU, TS or UI acceptance. Next Stage 155: coupling, planet/moving volumes, PHYSIS, sparse/LOD, and scene diagnostics/authoring gaps.
