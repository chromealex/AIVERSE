# PHYSIS branch recovery audit — 2026-11-18

**Repository:** `chromealex/AIVERSE`  
**Canonical branch:** `feature/physis-nature-simulation`, draft PR #25, base `feature/universal-entity-inspector-fps`.  
**Canonical head at audit:** `b2be39b0f4a78ded68cdeb9663454c3d55ed2fa9` (may advance concurrently).  
**Nature of this stage:** repository inventory / history inspection; no physics/compiler changes, native compilation or CTest execution.

## Branch inventory, complete search for branch name "physis"

Search returned exactly 3 matching branches, and subsequent page with cursor `Mw` returned no more.

| Branch | PR | Merge status / comparison against canonical |
|---|---|---|
| `feature/physis-nature-simulation` | [#25](https://github.com/chromealex/AIVERSE/pull/25), open draft | Current production-facing PHYSIS development branch; review and validation pending |
| `feature/physis-index-memory-budget` | [#28](https://github.com/chromealex/AIVERSE/pull/28), closed/merged | GitHub compare canonical...branch: `behind`, ahead=0, behind=32; latest head `3b600778b899d642a65aea432468eda69b1e4bb4` is an ancestor of canonical |
| `feature/physis-spatial-broadphase` | [#27](https://github.com/chromealex/AIVERSE/pull/27), closed/unmerged PR | GitHub compare canonical...branch: `behind`, ahead=0, behind=41; latest head `f995ab5d90f583a62ac457e286fb8e475c1c69a2` is an ancestor of canonical via manual multi-parent merge |

Additional branch-name searches for `nature`, `weather`, `storm`, `gravity`, `climate`, `wind`, `environment`, `spatial`, `force`, `disaster` found no additional related branch (only the PHYSIS spatial branch also matched `spatial`). Search is a snapshot of currently discoverable branches, not a proof against deleted refs or unreachable commits.

## What was actually recovered

Merge commit: [`eb5afd7c5877428eb0353bf9e98fcb78eb799644`](https://github.com/chromealex/AIVERSE/commit/eb5afd7c5877428eb0353bf9e98fcb78eb799644) had three parents: canonical `b9c26382...`, budget `3b600778b...`, and alternate broadphase `f995ab5d9...`. It was applied to canonical without force push. `docs/agent-handoffs/physis-concurrent-merge-2026-11-18.md` records the conflict resolution.

- Budget branch **functional code retained**: `FieldIndexSettings::maxTotalBucketReferences` default 65536 per typed field index, O(1) bucket-ref tracking, stable global fallback for excessive volume references, and `totalReferenceBudgetAndParity` test covering 200 each of WindZone, WindShelter, GravityField and StormEvent. GitHub current-file inspection confirms these are present in canonical `engine/physis/PhysisSpatialIndex.h` and `tests/physis_spatial_index_test.cpp`.
- Alternative broadphase branch **Git history retained, its different implementation NOT activated**: uses `SpatialFieldIndex` 3D buckets, explicit `rebuild`, `sameTick` and stale-index fallback, conflicting with current `FieldSpatialIndex` COW snapshot API used by diagnostics/editor. The original handoff was preserved under `docs/agent-handoffs/physis-spatial-broadphase-alternative-2026-11-18.md` rather than replacing the current stage-16 handoff. PR #27 can be closed/unmerged in GitHub while its commit is still in canonical ancestry; this is intentional.
- Current canonical PHYSIS has additional Stage 17/18 editor work; do NOT overwrite it by checking out older PHYSIS heads or adopting their entire trees.

## Remaining engineering decisions (not "lost branches")

1. Consider the alternate **3D** bucket design only after comparing benchmark build cost, memory and query performance against active **2D** `FieldSpatialIndex` using a fixed fixture and actual CTest.
2. For automatic live cache reuse, a `sameTick` guard is insufficient to detect ECS same-tick edits; require explicit revision/invalidation. The active snapshot-based policy is safe only when rebuilt for the current desired World state.
3. There is no evidence from this branch inspection of any additional active PHYSIS branch awaiting merge. Avoid deleting branches or claiming all deleted/historical refs were searched.
4. Run `./run-tests.sh --filter '^physis_'` on the **current canonical checkout** and optional `AIVERSE_ENABLE_PHYSICS=ON` Box3D tests, plus editor build. **Actual status: NOT RUN**. No CI PASS result was proven by this branch audit.

## Next agent action

Continue unfinished PHYSIS Stage 18 (editor Scene overlay and map-authoring/Inspector undo integration), carefully inspect current branch head before writing. Check the current `AGENTS.md`, `docs/editor-unification-handoff.md`, and the two preserved spatial handoffs. Do not merge old PHYSIS branch trees blindly or duplicate NEREID Water/TerraWeave implementations. For every implementation stage write separate code and handoff commits with exact SHAs and true test status.
