# PHYSIS Stage 16 — bounded transient spatial broadphase (2026-11-18)

**Repository:** `chromealex/AIVERSE`  
**Implementation branch:** `feature/physis-spatial-broadphase`, forked from PHYSIS head `44b4fe74de7ad0947dca6dc78c6f2f6c2a13b256`. **PR target:** `feature/physis-nature-simulation` (upstream draft PR #25), NOT `main`. Other agents may continue changing the PHYSIS source branch. No changes made to NEREID PR #23 or TerraWeave PR #24.  
**Status:** code, CTest source and CI build registration committed. **NO actual native build, CTest, CI pass, editor or Box3D execution validated.** GitHub repository clone from local container failed DNS lookup for `github.com`.

## Exact implementation/test commits
- `78a00de6612b56590a22f18df44347c0e1fafb36` — new `engine/physis/PhysisSpatialIndex.h` transient index and indexed nature queries.
- `9f0fa5baa5deb27892d22175196a304dc91b72cc` — `tests/physis_spatial_index_test.cpp`, indexed/reference parity.
- `47f856b3486d579c7e65714177f164d22a29d03a` — `PhysisDiagnostics.h` shared callback-based `sampleDiagnosticRasterWith`; existing function delegates to reference `sampleNatureField`.
- `80989e2c03a7616d2323872a18157795a78b2d0c` — `sampleDiagnosticRasterIndexed` optional overload.
- `3a71361ccfcaf923faae7b58a160f14020b57d95` — all 14 diagnostic layers compared to original.
- `bd8d366ecd6401db5d5d626547fe402fafd22452` — CMake `aiverse_physis_spatial_index_test` registered under CTest `physis_spatial_index`.
- `e069de32b8242e5cf8bf788d413c577f544c3cc3` — focused Linux/macOS `.github/workflows/physis.yml` builds new test.
- `1c47b8ab29cbbaf2a08ba621d92ba50d028aaa29` — `docs/physis.md` public API.
- `a8c4725763f83ad53a76155bdbd29528ec65a121`, `831f97c78d47f98245770ac6706a2a63b5e96f73` — root/subsystem `AGENTS.md` ownership, invalidation and no-persistence.
- `8260db1350fda1a0478eb7c038805073c53ea34a` — reusable `skills/world/aiverse-physis/SKILL.md` guidance.

## Architecture and API

`engine/physis/PhysisSpatialIndex.h` depends on read-only PHYSIS `PhysisDiagnostics.h` and fixed-point ECS; **no** genre, Box3D, graphics, Terra, NEREID or platform dependency.

- `SpatialFieldIndex::rebuild(World const&)` materializes copies of `WindZone`, `WindShelter`, `GravityField`, `StormEvent` in per-type `SpatialVolumeIndex<T>`. It is strictly **derived/transient**, never ECS state or serialized data.
- `SpatialVolumeIndex<T>::add` uses signed 256-metre 3D buckets, conservative finite bounds and a bounded map. Up to 256 assignments per finite volume and 65,536 assignments per type; larger/uniform/global/overflow-risk fields use a single global fallback list, never silently discarded.
- `SpatialVolumeIndex<T>::visit` stable-merges per-bucket candidates with globals in the **original ECS traversal order**, preserving Fixed summation, priority ties and shelter sequencing. Query consumes no temporary allocation.
- Public `indexedWindAt`, `indexedGravityAt`, `indexedStormsAt`, `indexedWindWithStormsAt`, `sampleNatureFieldIndexed` and `sampleDiagnosticRasterIndexed` consume the snapshot. Original unindexed APIs remain reference and unchanged. 3D diagnostic layers use indexed volume sampling; direct ECS scalar layers keep their Stage 15 fast path.
- `SpatialFieldIndex::sameTick(World const&)` checks PHYSIS committed tick. Indexed queries fall back to reference when tick differs. **Cannot detect same-tick ECS edits**; callers must rebuild after any changes even without advancing tick. Rebuild after save/load, COW rollback and World replacement.

### Example

```cpp
#include "engine/physis/PhysisSpatialIndex.h"
physis::SpatialFieldIndex index;
index.rebuild(sim.world()); // after committed tick & all ECS writes
physis::NatureFieldSample sample;
physis::sampleNatureFieldIndexed(sim.world(), index, position, sample);

physis::DiagnosticRaster raster;
physis::sampleDiagnosticRasterIndexed(sim.world(), index, request, raster);
// Cache discarded/rebuilt whenever authoritative PHYSIS World changes.
```

No host/editor/Box3D integration is automatically installed; a native project owns scheduling and body-to-entity mapping. No genre switch or duplicate water authority is introduced.

## Tests added (authored, **NOT RUN**)

`tests/physis_spatial_index_test.cpp` checks:
- indexed `Wind`, `Gravity`, `Storm`, combined wind and `NatureFieldSample` against reference at mixed positive/negative/out-of-climate-grid points;
- 20 local wind zones, multiple shelters, additive and override gravity plus global uniform fallback, huge hurricane fallback, tornado;
- preserves original visiting order and avoids visiting all distant zones in an isolated bucket;
- all 14 `DiagnosticLayer` rasters identical in raw values/valid mask/min/max/count/tick;
- stale tick fallback and rebuild, COW snapshot rollback/rebuild, missing PHYSIS state and oversized volume assignment budgets.

### Required actual execution

```sh
./run-tests.sh --filter '^physis_'
# Optional dedicated focused build:
cmake -S . -B build/physis-spatial -DCMAKE_BUILD_TYPE=Debug \
    -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_ENABLE_PHYSICS=ON
cmake --build build/physis-spatial --target aiverse_physis_spatial_index_test
ctest --test-dir build/physis-spatial --output-on-failure \
    -R '^physis_(spatial_index|diagnostics|wind|gravity|storm|presentation|integration|box3d)$'
```

**Actual results:** NOT RUN, no PASS logs. There was no local clone (DNS failure) and no completed GitHub CI run inspected. Do not merge until native compilation plus reference parity passes; attach exact error log/test SHA in next handoff.

## Limitations / risks

- Index construction is O(number of ECS volumes + candidate bucket assignments) per rebuild, with `std::map` overhead, and may cost more than scanning if there are only a few volumes. No benchmark results available.
- Global uniform/oversized volumes still require an O(global fallback count) visit; this is intentional correctness behavior.
- Query is valid only against **same world content**. Same tick is insufficient to detect structural/field edits; explicit rebuild at host barrier is mandatory.
- Generic World `eachExistingConst<T>` has stable ECS pool iteration ordering; stored candidate ordinal keeps this same order for indexed+fallback merging. If ECS iteration semantics change, update parity tests.
- This is a read-side optimization only; PHYSIS climate tick remains serial O(grid × weather fronts) and hazards still compute serially. It is not a climate-field spatial index, GPU job implementation, world streaming, or a rendered editor panel.
- No authoritative schema format changes; no tracked caches or GPU handles in World; no automatic NEREID/Terra feature adoption.

## Next agent — concrete actions

1. Inspect this stage branch and PHYSIS PR #25 HEAD. Run actual builds/CI and fix **real** compiler or reference parity failures first, including native persistence and optional Box3D tests. Preserve unindexed test truth.
2. Review same-tick invalidation contract in any host integration. If required, add an explicit World field revision/barrier event before adopting a reusable long-lived cache; do not attempt to cache across rollback or host writes unnoticed.
3. Benchmark `sampleNatureFieldIndexed` vs `sampleNatureField` and repeated 3D diagnostics on realistic scenes (e.g. 100, 1000+ spatial volumes); profile build cost, query count, memory. Choose adaptive thresholds based on observed evidence, not guessed gains.
4. After verification, implement opt-in Editor Scene diagnostic overlays or streamed grid updates in an editor-owned branch; respect concurrent editor PR.
5. Leave Water/NEREID and TerraWeave to their separate agents. Integration requires its own host adapter (no PHYSIS core dependencies). After every new stage, commit code first and separate handoff with source SHA, tested results, limitations and exact next step.
