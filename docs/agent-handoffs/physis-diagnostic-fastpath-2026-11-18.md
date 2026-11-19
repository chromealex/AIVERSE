# PHYSIS Stage 15 — direct ECS diagnostic layers / reference parity (2026-11-18)

**Repository** `chromealex/AIVERSE` · **branch** `feature/physis-nature-simulation` · draft PR #25.  
**Implementation SHA** `99642e39f0bbdd6e6ff45d95ec414dff34941d24`; **test SHA** `55f1cc735343cd56d2a6e1db5777926e8a1d2a28`.  
**Verification**: GitHub write completed. **NOT RUN** CMake configure, native build, CTest, CI, Box3D, editor profiling. GitHub checkout unavailable to the current container due `Could not resolve host: github.com`; do not claim any PASS.

## Problem / improvement

Stage 14 `sampleDiagnosticRaster` invoked `sampleNatureField` for **every** raster pixel and layer. That read-only API evaluates all WindZone, WindShelter, StormEvent, GravityField components even for a temperature/rain/snow/hazard heatmap. This was unnecessary work proportional to the number of environmental volumes per cell.

## Changed files / public API

- `engine/physis/PhysisDiagnostics.h`: add `diagnosticCellValue(const World&, Entity, DiagnosticLayer, Fixed&)` and `diagnosticNeeds3DField(DiagnosticLayer)` and replace per-cell read path. Weather/surface/hazard layers now sample their exact authoritative `Atmosphere`, `Surface`, `HazardCell` components directly, checking required component presence. 3D WindSpeed, VerticalWind, GravityMagnitude still use `sampleNatureField` at cell centres and existing `diagnosticValue`.
- `tests/physis_diagnostics_test.cpp`: parity fixture for **all 14** scalar layers against previous complete `NatureFieldSample` at identical cell centres, including negative coordinates, wind zone, radial gravity and a storm. The unchanged Stage 14 test remains registered in CMake and CI.

No public function signature, persistence layout, CTest registry or gameplay ECS changed. `DiagnosticRaster` output format, min/max, valid mask, normalization, error behavior and declared stride are unchanged. No Box3D/RTS/FPS/NEREID/Terra/renderer dependency introduced.

## Complexity (not measured yet)

- Temperature/humidity/pressure/precipitation/snow/surface/hazard layers now have **O(number of sampled cells)** reads instead of O(cells × environmental volumes).
- Wind speed/vertical wind/gravity still traverse environmental volumes per sampled point. These need a derived spatial index; this stage makes no claim of large-world spatial indexing, worker parallelism or actual measured performance improvement.

## Native verification (NOT RUN)

```sh
./run-tests.sh --filter '^physis_'
# Optional native physical-scene validation:
cmake -S . -B build/physis-physics -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_ENABLE_PHYSICS=ON
cmake --build build/physis-physics --target aiverse_physis_box3d_test
ctest --test-dir build/physis-physics -R '^physis_(diagnostics|box3d)$' --output-on-failure
```

Confirm fast-path parity for every layer, then test negative coordinates, missing hazards, stale snapshots and `sampleDiagnosticRaster` error-output invariants. Record the actual command/result and Git SHA in a follow-up handoff.

## Next bounded stage / owner

**PHYSIS agent:** add a transient per-tick spatial broadphase for `WindZone`, `WindShelter`, `GravityField`, `StormEvent`, preserving stable accumulation order and priority ties. It must be derived (not persisted), bounded on giant volumes, and demonstrably match unindexed reference for mixed/negative positions. Consumer API only; do not put Box3D, TerraWeave, NEREID or genre dependencies in PHYSIS core.

**Editor agent:** once native CTest passes, add an optional PHYSIS debug window/Scene overlay that consumes `DiagnosticRaster` on a bounded visible patch and has no authoritative ECS writes. Review editor-unification work before wiring UI.

**Water/Terra agents:** remain independently responsible for NEREID water and TerraWeave land/foliage; see `docs/physis-nereid-terraweave-integration.md`.

Every follow-up implementation must be followed by its own handoff commit, not an unverified PASS claim.
