# TerraWeave Stage70 + PHYSIS-validated main — two-parent integration handoff

**Date:** 2026-11-20. **Repo:** `chromealex/AIVERSE`.
**Integration branch:** `integration/terraweave-physis-stage70-20261120`.
**Source-only merge commit:** `97b4e165cae1ab74b907ba2b934665a530715432`.
**No change to:** `main`, `feature/terraweave` or
`integration/terraweave-main-20261120`.

## Why a second integration branch

A previous isolated staging merge combined TerraWeave up to Stage70
with main at `d7d9b3f4180dcba98b9238fd56eedc7ccb420bb1`.
Meanwhile current main gained independently validated PHYSIS +
TerraWeave + Box3D, new editor authoring, network improvements,
SpectraFX and updated batching tests. Continue with a NEW staging
branch instead of force-updating another agent's pending QA.

Merge parents, in order:

1. `e4129c5062a6ab47e7c70f3fdd483392fde1f679`
   (current main at integration time; Stage105 PHYSIS merge handoff).
2. `cbc7f0f6a91e99d7941678724eb21380a3df0c57`
   (previous Stage70 source integration with cursor regression tests).

Their most recent common ancestor was
`f53ab956e98157de96541a6ab0a954dd0837ab43`.
The synthesized result tree is
`eb714b81d8b8d3f7e031fd3365f82fcd1d0b1b0e`.
This is an actual two-parent Git merge commit, not a squash or
main overwrite.

## Content retention and conflict decisions

Compared by file path and blob SHA/mode across THREE recursive Git
trees (none truncated):

- 236 paths changed ONLY on current main: preserve exact main blobs.
- 32 paths changed ONLY on TerraWeave staging: preserve exact Terra blobs.
- 89 paths modified on BOTH sides to the SAME blob: no conflict.
- 14 paths differ after both sides changed: resolved individually.
- Read-back of synthesized tree: no main path missing; PHYSIS,
  SpectraFX, the Stage70 sparse nutrient headers and CMake all exist.
  The resulting merged tree has 5,998 recursive entries versus 5,972
  on main (including changed directory nodes). Source-tree check only.

Conflict selections:

| Path(s) | Resolution |
|---|---|
| `.github/workflows/terraweave.yml` | Terra: isolated Stage70 native tests included |
| `CMakeLists.txt` | Main in full, plus Terra Stage70 micro nutrient and optional Box3D/native smoke test target block (42 inserted lines) |
| `engine/render/Renderer3DModels.cpp` | Terra variant preserves special material-filtered draw and vertex-parameter packet/cache exclusions; main and Terra remainder of the file was near-identical by line comparison |
| `AGENTS.md` | Main; Terra-specific lines already contained in main, plus newer PHYSIS and SpectraFX ownership |
| `apps/editor/windows/SceneWindow.cpp` | Main; updated PHYSIS field marker input exclusion, overlay and authoring |
| `engine/net/replica/{Replica.h,ReplicaServer.cpp}` | Main; equivalent batched fields/viewer paths plus newer explanatory contracts |
| `engine/render/GlobalGeometryPool.cpp` | Main; dynamic vertex dedicated-buffer guard and PHYSIS/Geometry comment |
| `engine/render/StaticModelCachePolicy.h` | Main; strictly rejects static reuse for runtime vertex params / primitive material filter |
| `skills/catalog.json`, `tools/skills/catalog.py` | Main; includes Terra entries and newer PHYSIS skill |
| `tests/place/place_tests.cpp` | Main; more informative progress and phase-budget assertions |
| `tests/render/model_batching_policy.cpp` | Main; additional dynamic geometry and special draw regression |
| `tests/terraweave_model_test.cpp` | Main; signed/unsigned safe material index comparison |

**Risk:** Git tree parity and line-level inspection do not prove
semantic integration. PHYSIS headers now genuinely exist in the merged
checkout; Stage69/70 optional direct climate binding will compile
against real PHYSIS rather than its old fallback and must be tested.
The renderer's Terra filtered-draw branch must be compiled and checked
against current main static cache policies and GPU output. No
assertion about perfect cross-owner compatibility is yet justified.

## TerraWeave files and public runtime contracts

The 32 Terra-only paths include newer
`TerraWeaveForestDeadwood.h` (schema v3),
`TerraWeaveForestMicroNutrients.h`,
`TerraWeaveNereidMicroNutrients.h`,
`TerraWeavePhysisNutrientBridge.h`,
`TerraWeaveForestNutrientCycle.h`, NEREID junction,
ECS nutrient systems, native example, Stage70 tests, CI scripts and
individual stage handoffs. Major API and gameplay contracts remain:

- 32m sparse soil, separate persistent body-tagged 4m nutrient water,
  at most 65,536 4m cells, no dense world nutrient grid.
- Saved v1/v2 coarse dissolved nutrient mass remains unassigned until
  a verified, complete and atomic native claim; v3 save format is
  unchanged by the recent cursor fix.
- Current NEREID body identity and submerged Earthworks ground
  determine allowed water transport, PHYSIS owns weather, Box3D
  owns physics and Terra owns terrain/roots/ecology.
- `advanceForestMicroNutrients` uses immutable source mass and
  saved bounded cursors. Cursor compaction fix
  `34b557ad26c32926f5262831d34d50fe280aefc9` and tests
  `d6d03d7482a6adf3d978b59bd48ed997b579b1c5`
  are inherited as the second parent.
- **Replay caveat:** even without schema changes the cursor fix
  changes scheduler choice for an old affected snapshot. Do not
  mix host/client engine versions in authoritative replay.

## Actual verification at this checkpoint

**DONE:** API/Git tree inspection, 14 overlap decisions, preservation of
ALL main paths, construction/readback of new Git tree, creating
a real two-parent commit and independent branch.

**NOT RUN:** CMake configure or C++20 compilation, native
`terraweave_micro_nutrient` CTest, Stage70 v1/v2→v3 migration
runtime, all unfiltered CTest, PHYSIS/NEREID/Box3D coupled
acceptance, 65k nutrient stress, Metal/Vulkan/GLES hardware validation.
The old user's 136/136 PASS belongs to PRE-MERGE TerraWeave.
Earlier merged-main GitHub run #44621814322 was QUEUED at
integration inspection and cannot validate this new SHA.

The authoring container cannot clone GitHub via terminal (GitHub DNS
lookup unavailable); source change verification used the connected
GitHub API. Do NOT represent a GitHub accepted source commit as a
successful build. A new branch CI trigger and live results are
separate next stage.

## Next agent — exact commands and gates

On an isolated native Mac Studio worktree:

```bash
git fetch origin main integration/terraweave-physis-stage70-20261120
git worktree add --detach ~/Projects/AIVERSE-terraweave-physis-qa \
  origin/integration/terraweave-physis-stage70-20261120
cd ~/Projects/AIVERSE-terraweave-physis-qa
git rev-parse HEAD
cmake -S . -B build/terraweave-physis-qa \
  -DCMAKE_BUILD_TYPE=Debug -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_BUILD_EDITOR=ON -DAIVERSE_BUILD_HEADLESS=ON \
  -DAIVERSE_BUILD_TOOLS=ON -DAIVERSE_FBX=OFF \
  -DAIVERSE_ENABLE_PHYSICS=ON -DAIVERSE_PLACE_FAST_DEBUG=ON
cmake --build build/terraweave-physis-qa --parallel 4
ctest --test-dir build/terraweave-physis-qa \
  --output-on-failure --parallel 2
```

Proceed in this order:

1. Enable an isolated CI push/manual trigger on THIS new branch
   (do not cancel earlier branch checks). Compile both Linux and macOS.
2. Fix first concrete compiler error or failing test with exact
   diagnostic and commit a separate handoff.
3. Verify real PHYSIS weather and NEREID two-stream confluence with
   Stage70 v3 migration, no double authority, rollback, mass balance
   and 65k sparse performance/memory.
4. Validate renderer geometry pooling, packet safety and
   screenshots/backends before promotion.
5. If main moves again, recompare merge base and integrate in another
   reviewed QA stage; never force-push or merge this unverified
   staging commit into main directly.

Per user mandate: EVERY development/QA stage must leave a separate
committed handoff with changed paths, exact SHA, actual commands and
actual results. This file is the source-merge-stage handoff.
