# PHYSIS Stage 17 — bounded TOTAL spatial bucket references (2026-11-18)

**Repository:** `chromealex/AIVERSE`  
**Branch:** `feature/physis-index-memory-budget`, forked from canonical PHYSIS `feature/physis-nature-simulation` head `fda801da926baa3b2d53d392edf2b7d021b8f9ae`. This is a focused supplement to its existing `FieldSpatialIndex` Stage 16, NOT a competing new broadphase. PR should target `feature/physis-nature-simulation` (draft PR #25).  
**Native test status:** **NOT RUN** — GitHub connector writes are confirmed, but no local repository clone/CMake/native CTest or completed CI PASS evidence is available. `git ls-remote` against github.com from the container failed DNS resolution.  
**Other agents:** Water/NEREID PR #23 and TerraWeave PR #24 remain untouched and own their canonical systems.

## Why

Canonical PHYSIS Stage 16 `FieldBuckets<T>::add` limited bucket assignments for each **single** volume via `maxTilesPerVolume`, but did not cap their aggregate for a large number of individually small spatial zones. 100k zones × 64 buckets each could allocate 6.4 million references even when individual volume budget was satisfied. This stage adds a total bucket-reference budget without dropping any environmental contributions.

## Exact implementation / docs commits

- `f482666fd0a617cc12591e99a9ac6c3edec3b6f7` — `engine/physis/PhysisSpatialIndex.h`: new `FieldIndexSettings::maxTotalBucketReferences` default 65536, validated [1,1048576]; O(1) `FieldBuckets<T>::bucketReferences()`; short-circuit dimensions before multiplication; track total assignments. If adding the next volume exceeds either per-volume or per-type total assignment cap, use the existing globally visited ordered fallback rather than dropping the field.
- `4b182976975f38ba69af7ec22f952b2019671c07` — `tests/physis_spatial_index_test.cpp`: new `totalReferenceBudgetAndParity` adds 200 each of WindZone, WindShelter, GravityField, StormEvent (800 volume components), with tiny 8-reference budgets for each type. Check `bucketReferences <= 8 × 4`, presence of global fallback and bit-exact Fixed parity against original `windAt`, `gravityAt`, `stormsAt`, `windWithStormsAt` over 81 spatial points. Check invalid zero and excessively large total budget settings.
- `8589a5893db3fe3429cff50f8f8899ca48200010` — `docs/physis.md`.
- `3c0a1ab2e7612695fd8545da2a3c133799d93229` — root `AGENTS.md`.
- `a035afde1c21906ba82a46f99ac4548fe79d2fc7` — `engine/physis/AGENTS.md`.
- `5489b84d46f51ebde83c1a50d048ce8bc7c71cd2` — `skills/world/aiverse-physis/SKILL.md`.

## Public API / backward compatibility

```cpp
aiverse::physis::FieldIndexSettings config;
config.tileSizeMeters = 256;
config.maxTilesPerVolume = 1024;
config.maxTotalBucketReferences = 65536; // budget for EACH typed index
aiverse::physis::FieldSpatialIndex snapshot(world, config);
auto stats = snapshot.stats();
```

- Defaults preserve canonical unindexed/reference sampling, including global oversized gravity/storm fields.
- No persisted schema changes: index is derived COW World snapshot, never authoritative or a new ECS resource.
- New configuration parameter is appended with default, so existing aggregate designated initializers retain behavior.
- Each typed index gets its own allocation budget; total across four indexes is up to 4× the configured limit, excluding snapshot ECS/components/map overhead. The global fallback list remains unbounded by this parameter because **correctness beats silently dropping environmental influences**.
- Once a type reaches its budget, its newly added fields are visited in exactly the original ECS ordinal order by merging bucket-local and global candidates; no change in Fixed calculation order.
- Does not change renderer, terrain, water, body physics, saving/networking, or any FPS/RTS/game-specific module.

## Test status / commands

New assertions are added to the existing CTest `physis_spatial_index`, which is already registered in CMake, focused PHYSIS Linux/macOS workflow and `./run-tests.sh`. No extra target was necessary.

```sh
./run-tests.sh --filter '^physis_'
# Focused:
cmake -S . -B build/physis-budget -DCMAKE_BUILD_TYPE=Debug \
  -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_ENABLE_PHYSICS=ON
cmake --build build/physis-budget --target aiverse_physis_spatial_index_test
ctest --test-dir build/physis-budget --output-on-failure -R '^physis_spatial_index$'
```

**Actually executed:** only GitHub connector file writes and remote repository inspection. **CMake, CTest, CI and benchmarks NOT RUN / UNVERIFIED.** No PASS or numeric performance improvement asserted.

## Limits

- This caps bucket *references* per field type, not the total number of volumes, map node allocations, COW World memory or global fallback visitor cost.
- At extreme overlap or tiny budgets the algorithm approaches the unindexed O(number of global fields) reference cost. Benchmark before changing default thresholds.
- Index snapshot must be rebuilt when original World content changes, including same-tick edits, rollback/restore/tick; no automatic revision/caching semantics were added.
- Ensure test no longer uses invalid fields; preserve canonical `FieldBuckets` and original helper visitors. No parallel worker execution introduced.

## Next AI agent

1. Inspect latest PHYSIS PR #25 and Stage 16 handoff; review this focused diff and conflict status (other agents are committing concurrently). Do not force push or merge blindly.
2. Run actual native CMake/CTest and fix any compiler/runtime parity issue, especially 800-volume/fallback test and invalid-settings exception path. Capture SHA, commands and logs in another handoff.
3. Measure bucket build/query memory and speed at realistic volumes (100, 1000, 10k), and choose default budget from evidence; no perf claims until measured.
4. Add optional editor diagnostics or deterministic authoritative front indexing only under separately coordinated responsibilities; do not replace NEREID water or TerraWeave.
5. After every new feature commit, write a **separate** handoff with exact changed paths, actual test results, limitations and next action.
