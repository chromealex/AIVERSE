# PHYSIS concurrent branches merge — 3b600778b + f995ab5d9 (2026-11-18)

**Repository:** `chromealex/AIVERSE`  
**Destination branch:** `feature/physis-nature-simulation` (PHYSIS draft PR #25).  
**Actual merge commit:** `eb5afd7c5877428eb0353bf9e98fcb78eb799644`.  
**Parents, in exact order:**  
1. `b9c26382b215db19d34d9448719bdfadc0a96a9c` — canonical PHYSIS branch, including ongoing Stage 18 Scene overlay changes.  
2. `3b600778b899d642a65aea432468eda69b1e4bb4` — alternative agent's total spatial bucket memory budget work.  
3. `f995ab5d90f583a62ac457e286fb8e475c1c69a2` — separate agent's earlier 3D spatial broadphase implementation and handoff.

The destination branch was **successfully fast-forwarded to the merge commit** with an expected-head lease (no force push); the commit parents were re-fetched from GitHub after the ref update and verified.

## Resolution (exact files)

The two branches were **not** blindly overlaid onto the current editor work.

From the `3b600778b` lineage:
- `engine/physis/PhysisSpatialIndex.h` — adopt complete additional `FieldIndexSettings::maxTotalBucketReferences`, default 65,536 per field type, range [1, 1,048,576]. Every candidate still contributes: a per-type cap reached causes a stable ordered global-fallback visit instead of lost fields. Adds constant-time bucket-reference counting and checked tile-range multiplication.
- `tests/physis_spatial_index_test.cpp` — adopt `totalReferenceBudgetAndParity`: 800 fields (200 wind, shelters, gravity, storms), reference parity and aggressive 8-reference cap per type, rejected malformed budgets.
- `docs/agent-handoffs/physis-spatial-budget-2026-11-18.md` — preserve the original agent's complete implementation/test/limitations handoff.

From the `f995ab5d9` lineage:
- **Preserve the actual commit and its ancestors as a merge parent.** Its distinct `SpatialFieldIndex` used 3D buckets, explicit `rebuild`/`sameTick` and fallback-to-reference on stale tick. Those designs cannot safely replace the newer `FieldSpatialIndex` implementation which the canonical debug renderer/diagnostic APIs already use.
- Save its exact original handoff **unaltered** under `docs/agent-handoffs/physis-spatial-broadphase-alternative-2026-11-18.md`. Do not link it as the active public PHYSIS API; inspect its recorded old commit `78a00de...` through Git history when evaluating future 3D index enhancements.
- The active `engine/physis/PhysisSpatialIndex.h`, `PhysisDiagnostics.h`, CMake and tests continue to expose `FieldSpatialIndex` and `sampleDiagnosticRasterIndexed(FieldSpatialIndex const&, ...)`. No duplicate incompatible `SpatialFieldIndex` symbol or second competing broadphase header is installed.
- Unique future ideas retained for review: 3D buckets vs 2D X/Z, explicit same-tick guard and fallback; **same-tick does not detect mutations**, so a host-owned revision protocol would be needed before adopting that caching API.

Unrelated active Stage 17/18 editor files (`apps/editor/windows/PhysisWindow.cpp`, `apps/editor/PhysisSceneOverlay.h`, `SceneWindow.cpp`, `PhysisSceneGeometry.h`) were preserved as they existed on the first-parent branch. NEREID PR #23 and TerraWeave PR #24 were not edited.

## Architecture

- Canonical index remains transient and read-only with copy-on-write World snapshot and X/Z buckets. It does not mutate the physics world, ECS simulation or save schemas. `maxTilesPerVolume` limits per-volume candidates; `maxTotalBucketReferences` now caps aggregate stored bucket entries **per typed index** (WindZone, WindShelter, GravityField and StormEvent), not total shared memory across all types.
- Oversized candidate ranges are represented in the global list and visited in original ECS order; reference `windAt`, `stormsAt`, `gravityAt` semantics must be preserved.
- The archived 3D alternate is **not** a new production module, not registered with CMake and not compiled as part of PHYSIS.

## Verification — actual evidence

- PASS: GitHub API created merge commit with exactly three intended parents and moved the destination feature branch without force.
- PASS: GitHub API confirmed merged commit parents and destination ref update.
- **NOT RUN:** CMake configure, native compiler, CTest, editor build, spatial index parity execution, Box3D Scene, CI success, memory benchmarks. Do not claim those tests passed.
- Before merging draft PR #25, build and test the **actual merged tree**:

```sh
./run-tests.sh --filter '^physis_'
cmake -S . -B build/physis-physics -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_ENABLE_PHYSICS=ON
cmake --build build/physis-physics --target aiverse_physis_spatial_index_test aiverse_physis_editor_view_test aiverse_physis_box3d_test -j4
ctest --test-dir build/physis-physics --output-on-failure -R '^physis_(spatial_index|editor_view|box3d)$'
```

## Next explicit agent steps

1. Run real native CMake/CTest for `physis_spatial_index`, especially new 800-field total-budget test. Fix actual failures and record exact SHA, commands and logs.
2. Review the archived alternative 3D implementation only if profiling indicates advantages. Do not rename/switch the canonical index until compatibility, parity, COW/revision invalidation and performance are demonstrated.
3. Continue Stage 18 editor work from current first-parent tree: Scene viewport read-only overlay, map-authored PHYSIS settings with real Undo/Redo and common Inspector integration. No live PHYSIS mutations before safe command/replay/writeback rules exist.
4. Keep NEREID Water and TerraWeave terrain owned by their separate agents; PHYSIS remains genre-independent.
5. After each later implementation stage update root/PHYSIS AGENTS, the AI skill and a distinct agent handoff with commit hashes, actual tests and next steps.
