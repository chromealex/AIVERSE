# PHYSIS Stage 102 — controlled integration of validated TerraWeave into current main

Date: 2026-11-19
Repository: `chromealex/AIVERSE`
NEW main-derived staging branch: `integration/physis-terraweave-onto-main-20261119`
Initial parent/base: `d7d9b3f4180dcba98b9238fd56eedc7ccb420bb1`
Fully tested PHYSIS + TerraWeave source: `658285a6ec22054ef9d382ef324ddd01ebe9d9b8`
Main-target tracking PR: https://github.com/chromealex/AIVERSE/pull/45 (Draft, **mergeable_state=dirty**, actual unresolved conflicts)
Source integration PR: https://github.com/chromealex/AIVERSE/pull/40 (Draft, targets old integration base)

## Native acceptance of the original integration branch — PASS

On owner's Mac Studio, single full acceptance execution:
`AIVERSE_CTEST_VERBOSE=1 bash tools/physis-terraweave-final-validation.sh`

```text
167/167 CTests passed; 0 failed
Total wall time 898.15 sec
Source commit 658285a6ec22054ef9d382ef324ddd01ebe9d9b8
Full report: /Users/aleksandrfeer/Projects/AIVERSE-physis-terra-final/build/physis-terraweave-onepass/onepass-reports/20261119-201008-18053
aiverse_tests: 273431 checks, 0 failures
aiverse_replica_tests: 35 internal tests, 57514 checks, 0 failures
place: 8 scenarios, 55 checks, 0 failures; 523 bodies, 16 clients + late join, 0.782ms avg / 2.291ms worst vs 5ms limit
skill_audit: 0 errors, 0 warnings
PHYSIS: 31 labelled CTests passed, including physis_terraweave_fall_bridge
```

Full native CTest covered combined PHYSIS authoritative fall ledger,
TerraWeave rooted plant state, Box3D bodies, repair, COW persistence and rollback.

**Important:** This 167/167 validates only the tested commit and its own
ancestors. It does **not** validate a merge with new main.

## Why the safe merge is not complete

The remote `main` is **467 commits ahead** of the common ancestor
`d71c6d47e532b25fb816a8bb1f96c41e5275a165` relative to tested
integration. These include independent ECS static/prefab/scene work,
editor code, renderer, GPU/Vulkan and batching changes.

GitHub's main-target PR #45 reports:
`mergeable=false`, `mergeable_state=dirty`; the new 399-file
source PR cannot be safely merged automatically. A prior
PHYSIS+TerraWeave merge already resolved different file-level conflicts,
but these are **new conflicts with latest main**. Never force-push,
reset, replace main's tree with the old tested tree, or discard other agents.

The NEW staging branch created for this stage is rooted at current main
and contains this document only. **The source merge has NOT been applied**
yet, and no combined-main CMake/CTest has run. This is an honest
handoff checkpoint for a conflict-aware continuation agent.

## Next step using an isolated Mac Studio worktree

Use source control to perform a standard three-way merge on the staging
branch, preserving both sets of edits and inspecting every conflict:

```bash
git -C ~/Projects/AIVERSE fetch origin

git -C ~/Projects/AIVERSE worktree add \
  -b integration/physis-terraweave-main-local \
  ~/Projects/AIVERSE-physis-main-integration \
  origin/integration/physis-terraweave-onto-main-20261119

cd ~/Projects/AIVERSE-physis-main-integration
git merge --no-commit --no-ff \
  origin/integration/physis-terraweave-complete-20261119

# The command is expected to report conflicts; do not commit the merge yet.
git status --short
git diff --name-only --diff-filter=U
```

The continuation agent must resolve each conflict *semantically*
(especially `AGENTS.md`, `CMakeLists.txt`, editor scene code
and skills metadata). Retain new main's ECS and renderer systems as
well as PHYSIS/NEREID/Terra. Re-run strict skill audit and all production
integration tests in the combined tree. Do not claim a pass from the
old branch; create a new handoff with the resolved tree SHA, merge
parents, exact commands, errors, and test report.

Final acceptance:
`AIVERSE_CTEST_VERBOSE=1 bash tools/physis-terraweave-final-validation.sh`
on the newly merged, clean integrated worktree, *after* resolving
conflicts and making a normal commit. Keep PR #45 Draft until native
combined-main acceptance is 100% green, then review and land without
replacing `main`.

## Parallel Place architecture design questions (NOT IMPLEMENTED here)

The user also asked about renaming the broad `place` test to separate
functional and performance CTests, scaling to 250 connected clients and
100,000 world entities, and decoupling server authority rates (10–15 Hz)
from client presentation (25–30 FPS). These are architectural follow-ups
and should be implemented in a **new dedicated branch**, not folded
unreviewed into this validated integration merge.

Current constraints at this stage:
- `replica::Server` uses uint8 slots 1..255; 250 clients are within the
  existing hard limit but leave only five slots for any other peers.
- `physics::SceneConfig` caps actual rigid bodies at 65,535 and
  defaults to 16,384, while `PlaceConfig.maxBodies` defaults to 4,000;
  100k ECS objects must not be conflated with 100k Box3D rigid bodies.
- `SimPlace::frame` runs one server tick and one update per client at
  virtual 60Hz; `PlaceServer` shares `Scene::dt` and replica input
  sequencing; `PredictionWorld::step` also assumes scene dt.
  A genuine 10/15Hz server with 30Hz client prediction requires independent
  frame clocks/input buffering, physics substep policy, snapshots,
  interpolation/reconciliation and tests for late joins and packet loss.
  Merely lowering JSON `tickRate` does NOT safely accomplish it.
- Keep a 16/500 rapid PR gate; introduce explicit 64/10k, 128/50k,
  250/100k *interest-streamed ECS* stress tiers, with active-physics
  density and bandwidth/memory budget recorded. A longer large-world
  soak belongs in dedicated performance/nightly CI, not a five-minute
  default CTest that mixes correctness and wall-clock performance.
