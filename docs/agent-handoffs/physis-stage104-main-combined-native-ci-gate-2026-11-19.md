# PHYSIS Stage 104 — enable native one-pass CI for the current-main staging merge

Date: 2026-11-19
Repository: `chromealex/AIVERSE`
Branch: `integration/physis-terraweave-onto-main-20261119`
PR targeting main: https://github.com/chromealex/AIVERSE/pull/46 (Draft)
Previous stage: `docs/agent-handoffs/physis-stage103-current-main-merge-built-tree-2026-11-19.md`

## Real source state

The main-combined tree is a two-parent commit
`e3851a46ea2db22594348cf103c40c15b1017528`, whose parent1
contains main `d7d9b3f4180dcba98b9238fd56eedc7ccb420bb1`
and whose parent2 is the original native-validated feature
`658285a6ec22054ef9d382ef324ddd01ebe9d9b8`.

The tree was statically checked against all 4,981 source/main paths,
showing 0 Git blob/mode mismatches. There are 11 semantically merged
concurrent files, including CMake, editor, RHI/Renderer3D and skills.
This is source merge integrity, NOT a native C++ compiler PASS.

## Exact new workflow change

Commit `4e3808df83e28a221958c6d63b182914e753ebf0` changes only
`.github/workflows/physis-terraweave-onepass.yml`:

```yaml
on:
  push:
    branches:
      - integration/physis-terraweave-complete-20261119
      - integration/physis-terraweave-onto-main-20261119
```

The workflow runs the project's original
`bash tools/physis-terraweave-final-validation.sh` on
`macos-15` with CMake Debug, Box3D, editor and all native test targets,
then one **unfiltered** CTest. Artifacts contain configure/build/CTest/
JUnit evidence, whether the job succeeds or fails. This is the same
acceptance mechanism as the user's Mac Studio source-branch test.

The workflow trigger and code branch have been committed on staging,
**not on main**. GitHub CI may queue and is not assumed to have passed.
A full test suite for this merged-main tree is still **NOT RUN/UNKNOWN**
until real logs or a completed action explicitly show it.

## Manual Mac Studio fallback, preserving prior acceptance cache

There were only 11 GiB remaining at last measured disk utilization.
Check space and avoid deleting other agents' worktrees.
If the macos-15 CI runner remains unavailable, or real Mac Studio
render verification is desired, use the existing dedicated clean worktree
and its prior build cache rather than creating a second large cache:

```bash
cd ~/Projects/AIVERSE-physis-terra-final
git status --short
git fetch origin
git switch --detach origin/integration/physis-terraweave-onto-main-20261119
AIVERSE_CTEST_VERBOSE=1 bash tools/physis-terraweave-final-validation.sh
```

The new merged branch may register more CTests than the previous
source branch's 167 because main gained ECS/static scene tests. The
correct success criterion is **100% of registered tests**; do not force
an exact count or weaken the strict 5ms optimized-Debug Place gate.
Do not merge or fast-forward `main` until this new combined-main code
has a native compiler and test result, and fix each real failure on
staging with a Stage105 handoff if necessary.

## Independent upcoming Place work

Only after the merge gate: separate CTest names
`place_runtime` and `place_server_perf`, add streaming scalability
tiers (250 peers / 100,000 ECS objects, not 100k Box3D bodies), and
decouple 10–15Hz authoritative ticks/snapshots from 30Hz prediction
and 30–60FPS rendering. Implement on a new branch, with new
regressions for input loss, fixed-step reconciliation, remote entity
interpolation and late joiner correctness.
