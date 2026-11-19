# PHYSIS Stage 14 — renderer-neutral diagnostic heatmaps (2026-11-18)

**Repository:** `chromealex/AIVERSE`  
**Branch:** `feature/physis-nature-simulation` (draft PR #25; base `feature/universal-entity-inspector-fps`).  
**Implementation and test commits:**
- `73ec271864ed0699a2b681c4521b6244b0f327e1`: `engine/physis/PhysisDiagnostics.h`, read-only raster API.
- `8e4f4f6fcdeeb165b1e14e7d6979015668b3cf96`: `tests/physis_diagnostics_test.cpp`.
- `82b262a758b365430f6828c0d6fc14c663380ce2`: CMake target `aiverse_physis_diagnostics_test`, CTest name `physis_diagnostics`.
- `671da23fce97587cd559e5f6f85f036c428c801c`: focused GitHub Actions includes the diagnostic test target.
- `b1b494ba787a7e27a58fbf9af799626fdc469c58`: public PHYSIS documentation.
- `c2923f7a240d1666711ca0d624f5fff2ee04d4c1`, `4a2f29840ef556b036450bf8a707646e031c40ac`: root and subsystem AGENTS.md ownership notes.
- `cd65c63ab1e5ef21b75a977612c478bf5918ba1f`: explicit `<utility>` include for `std::move`.

## Delivered / design

`PhysisDiagnostics.h` depends only on `PhysisPresentation.h`, standard containers, fixed-point engine data; no Box3D, NEREID, TerraWeave, RHI, rendering, editor or genres.

- `DiagnosticLayer`: TemperatureCelsius, RelativeHumidity, PressureHpa, Rain, Snow, SurfaceWetness, SnowpackMeters, SurfaceIce, WindSpeedMetersPerSecond, VerticalWindMetersPerSecond, GravityMagnitude, Fire, Quake, LocalFloodMeters.
- `DiagnosticRequest`: climate cell-coordinate rectangle and downsample `stride`; read-only cap 65,536 samples per request, stride 1..1024.
- `DiagnosticRaster`: origin, output dimensions, tick, channel, `Fixed::raw` values, `valid` bit-like mask (byte array), min/max valid raw values, sample count. Invalid or out-of-world cells are not counted and not substituted as real zero samples.
- `sampleDiagnosticRaster(World const&, DiagnosticRequest const&, DiagnosticRaster&)`: uses one authoritative climate grid and `sampleNatureField` 3D sampling at cell centres/elevations; commits result to caller only on valid input. Does not modify World or its simulation clock.
- `diagnosticNormalized(DiagnosticRaster const&, index)`: neutral uint16 normalized scalar for external material/shader/ImGui heatmap mapping. No image/overlay rendering is implemented. A constant layer normalizes to 0, by convention.

## Integration example

```cpp
#include "engine/physis/PhysisDiagnostics.h"
physis::DiagnosticRequest request;
request.originX = -10;
request.originZ = -10;
request.width = 64;
request.height = 64;
request.stride = 2;
request.layer = physis::DiagnosticLayer::WindSpeedMetersPerSecond;
physis::DiagnosticRaster raster;
if (physis::sampleDiagnosticRaster(sim.world(), request, raster)) {
    // Diagnostic consumer colors/culls only cells whose raster.valid[i] is set.
    // No renderer or UI dependency in the PHYSIS API.
    auto heat = physis::diagnosticNormalized(raster, 0);
}
```

## Verification status (exact)

- GitHub connector writes to the branch: completed.
- Native C++ configure/build: **NOT RUN**.
- `./run-tests.sh --filter '^physis_'`: **NOT RUN**.
- `physis_diagnostics` CTest: **NOT RUN**.
- PHYSIS GitHub Actions: **no passing run inspected / NOT VERIFIED**.
- UI/editor and GPU visual preview: **NOT IMPLEMENTED**.
- Cross-branch NEREID/TerraWeave integration: **NOT IMPLEMENTED**.

The test source checks absent-world no mutation, negative cell origins, mixed in/out-of-grid validity, bounded invalid inputs, uniform/variable raster normalization, fire/hazard flagging, vertical wind, deterministic World copy and tick nonmutation. Do **not** report passing tests until CTest logs exist.

## Known limitations

- Large multi-layer heatmap requests recompute `sampleNatureField` per point, O(samples × (wind zones + storm events + gravity fields)), with no spatial index or multi-pass cache. This is intended for bounded debugging, not every-frame full-map uploads.
- The caller owns palette mapping; no ImGui, RHI, editor Window integration or GPU transfer exists yet.
- `LocalFloodMeters` shows PHYSIS optional standalone `HazardCell::waterDepth`, **not** canonical NEREID water-body level or water depth. NEREID owns water rendering and sampling.
- Edge probes are at climate cell centres in X/Z and `Cell::elevation` in Y; wind/storm values may change with height. No interpolated volumetric upsample.
- No new persistent authoritative ECS schemas; diagnostic output is disposable and must never be written into lockstep simulation.

## Next concrete agent tasks

1. Pull actual current PR #25 head, run PHYSIS full CMake test suite (including Box3D optionally), inspect CI and fix real compiler, numerics, Save/COW/rollback issues; record exact SHA, build flags and PASS/FAIL logs.
2. Add optional **editor-side PHYSIS debug overlay/window** that uses `DiagnosticRaster` and `NatureFieldSample` without modifying PHYSIS core or making editor universal systems depend on FPS/RTS. Instrument CPU cost at realistic 256x256 or 512x512 sample sizes.
3. Add spatial indexing/batching for WindZone/StormEvent/GravityField and bounded dirty updates. Never persist derived indexes in ECS; rebuild after rollback/save restore.
4. On an independent integration PR after water/Terra agents are coordinated, let Terra consume PHYSIS rainfall/wetness/snow and NEREID waterline masks; do not merge their branches or introduce duplicate water authority.
5. After every implementation, commit code first, then a separate agent handoff with implementation commit SHA, public API, tests **actually** run and next steps.
