# TerraWeave integration QA — Stage70 sparse scheduler cursor correctness

**Date:** 2026-11-20. **Branch:** `integration/terraweave-main-20261120`.
**Baseline:** staging main/TerraWeave two-parent merge; original TerraWeave
`feature/terraweave` and `main` are NOT changed by this QA stage.

## Exact source commits

- `34b557ad26c32926f5262831d34d50fe280aefc9` —
  `engine/world/TerraWeaveForestMicroNutrients.h`.
  Both bounded Stage70 scheduler cursors now resume using the next
  **unprocessed sorted key** across sparse vector compaction, instead of
  applying modulo arithmetic from the former vector size.
- `d6d03d7482a6adf3d978b59bd48ed997b579b1c5` —
  `tests/terraweave_micro_nutrient_test.cpp`.
  Added regression fixtures for (1) signed negative 4m-to-32m floor mapping,
  (2) all-or-nothing legacy v2 coarse-water claim, including refusal on
  wrong spatial membership and unavailable verified wet body,
  (3) v3 archive round-trip after valid two-body negative-coordinate claim,
  (4) fair soil cursor progression when a processed patch disappears, and
  (5) fair water cursor progression after a transported source disappears.

## Root cause, behavior and API

Previously, for four sorted soil cells `A,B,C,D`, a cursor at `B` and
a two-cell work budget would process `B,C`. If `B` was depleted and
removed, the old next offset `(1+2)%4=3`, modulo the new size of 3,
became 0 (cell `A`) instead of resuming at `D`. The same bug applied
to the 4m sparse water cursor after advection removed a processed tile.
With large worlds and bounded per-tick budgets this caused unfair
scheduling and delayed coverage.

Both cursors now save the next original sorted key before compaction,
then lower-bound that key in the new sorted vector (wrapping when no
higher/equal key remains). This is deterministic, bounded, and does
not add dense grids or per-plant ECS entities. The 32m soil and
body-tagged 4m water storage, `ForestDeadwoodResource` v3 binary
layout, ledger mass authority, public function signatures and defaults
are unchanged.

**Replay compatibility caveat:** scheduler selection for an existing
snapshot can differ from prior affected builds when a processed cell
is removed. The saved index and archive schema are unchanged, but exact
simulation replay across mixed engine versions with this bug fix is
NOT guaranteed. Multiplayer hosts/clients should run the same build;
recheck rollback and replay determinism before promotion.

## Actual verification / outstanding QA

- DONE: both code/test changes committed through GitHub Contents API.
- DONE: GitHub readback of branch head
  `d6d03d7482a6adf3d978b59bd48ed997b579b1c5` at time of this
  handoff authoring.
- NOT RUN: Clang/GCC compile of changed AIVERSE header, CMake configure,
  micro nutrient CTest, full CTest, Mac Metal/Vulkan/GLES GPU render, live
  NEREID/PHYSIS/Box3D multi-owner integration, large-world performance.
  Full local checkout is unavailable in this authoring container:
  `git ls-remote` fails DNS lookup of github.com.
- GitHub Actions merged-main acceptance latest run:
  [#44621814322](https://github.com/chromealex/AIVERSE/actions/runs/44621814322)
  was **queued** at this checkpoint. Previous #44619590001 and
  #44621671073 were **cancelled by branch push concurrency**, not pass/fail
  evidence. Do not attribute earlier user's 136/136 PASS to the new head.
- Main advanced independently to
  `e4129c5062a6ab47e7c70f3fdd483392fde1f679` by last read.
  This integration branch was based on an earlier main commit;
  update main integration only after the current QA has a recorded result.

## Independent QA commands — NOT EXECUTED here

Use a clean worktree; do not overwrite other agent clones:

```bash
git fetch origin integration/terraweave-main-20261120
git worktree add --detach ~/Projects/AIVERSE-terraweave-cursor-qa \
  origin/integration/terraweave-main-20261120
cd ~/Projects/AIVERSE-terraweave-cursor-qa
git rev-parse HEAD
cmake -S . -B build/terraweave-cursor-qa \
  -DCMAKE_BUILD_TYPE=Debug -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_PLACE_FAST_DEBUG=ON -DAIVERSE_ENABLE_PHYSICS=ON
cmake --build build/terraweave-cursor-qa --parallel 4 \
  --target aiverse_terraweave_micro_nutrient_test
ctest --test-dir build/terraweave-cursor-qa \
  -R '^terraweave_micro_nutrient$' --output-on-failure
bash tools/terraweave-main-integration-acceptance.sh
```

## Next agent / acceptance

1. Wait for a **completed** latest CI run (or perform fresh native build),
   fix first actual compiler/CTest diagnostic and record the logs.
2. Confirm negative-cell archive migration and both cursor tests pass.
   Add 65k sparse tile stress/budget measurements and cross-build
   rollback/snapshot tests; do not claim these from the new small fixtures.
3. Integrate the now-newer `main` and PHYSIS owner branch in a fresh
   staging merge, preserving both histories and manually validating the
   renderer overlap files. Do not force-push or bypass unresolved conflicts.
4. Run one entire unfiltered CTest on the new integrated SHA, real
   NEREID erosion + Box3D fall + NavMesh + PHYSIS + ecology world acceptance,
   and available Metal/Vulkan/GLES checks. Only then consider merge to main.
5. Backport the scheduler fix to `feature/terraweave` if the QA
   succeeds or decide on a single branch of integration ownership.

Each bounded QA stage needs its own committed handoff and accurate
pass/fail status.
