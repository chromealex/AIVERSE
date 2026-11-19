# PHYSIS Stage 16 — transient COW spatial index for wind, gravity, storms (2026-11-18)

**Repository:** `chromealex/AIVERSE`  
**Branch:** `feature/physis-nature-simulation`, draft PR #25, base `feature/universal-entity-inspector-fps`.  
**Status:** implementation, tests, CMake/CI and docs committed. **NATIVE COMPILATION, CTEST, CI PASS, GPU/EDITOR PREVIEW AND PERFORMANCE BENCHMARKS NOT RUN / UNVERIFIED.** The local shell cannot clone GitHub (`Could not resolve host: github.com`); GitHub connector writes succeed but do not constitute a compiled test.

## Implementation commits / changed files

- `e06878166f98939f924ef8ca26f07236557cede0` — `engine/physis/PhysisWind.h`: shared exact Fixed wind/shelter evaluation through `localWindAtWith` / `windAtWith` ordered candidate visitors; original unindexed `localWindAt` / `windAt` wrappers preserved.
- `36d411126712b7ef28b6d1dc065be9285989f63b` — `engine/physis/PhysisGravity.h`: shared gravity policy `gravityAtWith`, existing `gravityAt` unchanged externally; same additive Fixed accumulation and generation-safe Entity tie-break.
- `44b4fe74de7ad0947dca6dc78c6f2f6c2a13b256` — `engine/physis/PhysisStorm.h`: shared `stormsAtWith`, existing `stormsAt` remains.
- `d75f777d1d61727e6b9811108cde66b07ef1c255` — small warning-hygiene follow-up: explicitly mark the `World` argument of storm visitor helper unused.
- `7c6e28f2bc37e1e2fb818f02ade9b58c7e886eb3`, `9156ae13eb429085712b11ca48dd7d411820b2f6` — new `engine/physis/PhysisSpatialIndex.h`, signed-bound overflow guard fix.
- `aab95b15cf6b1f924612fb8ef20c86010a20ca8d` — `tests/physis_spatial_index_test.cpp`: ~400 spatial queries against old unindexed reference, all Fixed wind/gravity/storm components, negative positions, shelters, large-volume fallback, COW snapshot pinning and rebuild after mutation, invalid settings.
- `0d9b9b621a30c8ce4a6902ca6d18a26610da5e93` — `engine/physis/PhysisDiagnostics.h`: `sampleDiagnosticRasterIndexed` shares same raster loop with old `sampleDiagnosticRaster`, using indexed 3D vector field lookups and Stage 15 direct ECS scalar layers.
- `7ba37ee3e04691caf09f7966e19a55df72eaee8e` — `tests/physis_diagnostics_test.cpp`: indexed/unindexed raster bit-identical test on 14 channels, including wind/gravity/storm.
- `ffd9373e1e02ab02ed655653b6566bff07329b46`, `4f1cedc017d126cde882085ee3bc37a55bb3ed89` — `CMakeLists.txt` registers `physis_spatial_index` and `.github/workflows/physis.yml` builds the new target on Ubuntu/macOS with opt-in Box3D.
- `45d8efc58283444cd5e6190c6601d7ffdcc44924` — `docs/physis.md`, Stage 12–16 index and API examples, duplicate Stage 12 entry removed.
- `ad3d0df6f1da6c23ad6017b6100c1704998a08bd` — `skills/world/aiverse-physis/SKILL.md` updated for AI reuse.
- `bd61e29a64ebe9af74b1f6c3644203bf42c85bd9`, `b91d743d962b1c6b7527546e166e2f6f0b54fcea` — root and `engine/physis/AGENTS.md` ownership and snapshot rules.

This handoff is committed **after** those code/test commits, as required.

## Architecture and API

`FieldSpatialIndex(const World&, FieldIndexSettings = {})` copies the current authoritative `aiverse::World` using COW/snapshot semantics; it does not write any PHYSIS/World resource. It constructs 2D X/Z fixed-metre buckets for `WindZone`, `WindShelter`, `GravityField`, `StormEvent`; each candidate volume has the original ECS iteration ordinal. Queries merge tile-local/global candidates in that exact order to preserve Fixed addition sequence and wind shelter attenuation, and still run the **original precise 3D/distance/time physics policies**.

Unbounded uniform gravity and very large/boundary-safe volumes use a global fallback list, so bucket growth is bounded by `FieldIndexSettings.maxTilesPerVolume` (default 1024; validated 1..4096) per finite volume. `tileSizeMeters` defaults 256 (validated 1..1,000,000). All buckets are derived; they are neither serialized nor hashed and never enter authoritative replay.

Existing unindexed APIs remain unchanged. New optional API:

```cpp
#include "engine/physis/PhysisSpatialIndex.h"
#include "engine/physis/PhysisDiagnostics.h"

aiverse::physis::FieldSpatialIndex index(world, {
    .tileSizeMeters = 256,
    .maxTilesPerVolume = 1024
});
const auto wind = index.windAt(position);              // Vec3 m/s
const auto storms = index.stormsAt(position);          // StormInfluence
const auto gravity = index.gravityAt(position);        // Vec3 m/s²
const auto mixedWind = index.windWithStormsAt(position);
aiverse::physis::NatureFieldSample sample;
const bool ok = index.sample(position, sample);
const auto stats = index.stats();
const auto candidates = index.candidateCount(position);

aiverse::physis::DiagnosticRequest diagnostic;
diagnostic.layer = aiverse::physis::DiagnosticLayer::WindSpeedMetersPerSecond;
aiverse::physis::DiagnosticRaster raster;
aiverse::physis::sampleDiagnosticRasterIndexed(index, diagnostic, raster);
```

### Snapshot semantics (mandatory)

The index contains its own COW `World` snapshot and copied volume entries. It does **not** observe mutations, storm drift or subsequent ticks of the original world; rebuild a fresh index at the required committed tick or after save-load/rollback. Old indexes are deliberately safe, pinned to their old snapshots, but retaining many of them may keep large COW pages alive. Do not build once and use forever as if it were a live ECS view. Do not make PHYSIS simulation depend on the presentation index.

### Complexity and limits

Queries find only volumes in their X/Z tile plus globally indexed volumes, then execute the original Fixed radial/height/time checks. For many small localized volumes and many queries per tick this avoids O(all fields) work per query; build is O(volumes × bounded tile placements), memory O(bucket references), global fallback may still be O(very large/global volumes). No measured runtime improvement is claimed. Current implementation does not add worker jobs, streaming/chunk residency, indexed WeatherFront/QuakeEvent authoritative system updates, 3D octrees, RHI/editor heatmap drawing, NEREID water, or TerraWeave foliage. Scanning scalar weather diagnostics still uses Stage 15 O(sampled cells) direct ECS reads.

No authoritative schemas changed. No dependencies from PHYSIS core to Box3D, RTS, FPS, TerraWeave, NEREID, gfx/render/editor or OS clocks. Existing genus-independent engine mechanics remain in common ECS.

## Verification instructions (all unexecuted here)

```sh
./run-tests.sh --filter '^physis_'

# Optional real Box3D validation:
cmake -S . -B build/physis-physics \
  -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_ENABLE_PHYSICS=ON \
  -DAIVERSE_BUILD_PHYSICS_DEMO=OFF
cmake --build build/physis-physics \
  --target aiverse_physis_spatial_index_test aiverse_physis_diagnostics_test aiverse_physis_box3d_test
ctest --test-dir build/physis-physics \
  --output-on-failure -R '^physis_(spatial_index|diagnostics|box3d)$'
```

**Actual result:** NOT RUN / no CTest output or compiler log available. Do not assert successful build, exact parity or performance until those commands run. Preserve `.github/workflows/physis.yml` logs and any first errors. The source tests are assertions waiting to be compiled/executed.

## Risks / next concrete work

1. **Verification agent**: run focused PHYSIS build/test suite and CI, fix discovered compiler/runtime/serialization issues before merging draft PR #25. Validate `FieldSpatialIndex` COW snapshots, visitor template instantiation on GCC/Clang/MSVC, 14-layer indexed diagnostics, negative coordinates, bounded extreme tile indexing and Box3D tests.
2. **Editor agent**, after reading `docs/editor-unification-handoff.md`: build an opt-in PHYSIS debug window/Scene overlay driven by `DiagnosticRaster` (prefer `sampleDiagnosticRasterIndexed` for vector layers) with bounded visible patch and no authoritative writes. Test in actual editor with a known world.
3. **PHYSIS performance agent**: profile field index construction/query on realistic 256²/512² rasters and many overlapping global volumes; add LRU/cache invalidation at host boundary if needed (not persisted). Then optimize authoritative WeatherFront/QuakeEvent passes separately with deterministic spatial indexing/partitioning, eventually streaming and job workers.
4. **Water/Terra owners**: NEREID PR #23 owns water, TerraWeave PR #24 terrain/foliage. Create separate optional integration PR for waterline/shore/masks and precipitation, not a PHYSIS branch duplicate; use `FloodMode::ExternalAuthority`.
5. After **every** new implementation stage: commit code/tests, create independent handoff with exact SHA, actual test PASS/FAIL/NOT RUN, limitations and specific next action. No force push, no blind merges.
