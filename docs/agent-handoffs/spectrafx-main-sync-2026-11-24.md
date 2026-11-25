# SpectraFX significance branch main sync — 2026-11-24

Branch `fix/spectrafx-base-sync-20261124`, based on PR #113 `feature/spectrafx-significance-data-20261123` at `e81a0c6f7`. Merge commit `0e2b52686` adds `origin/main` at `71aa5c93a` without conflicts. No SpectraFX source was edited in this stage.

Executed: `python3 tools/skills/catalog.py check` PASS (269 skills); `git diff --check origin/main...HEAD` PASS. `python3 tools/layers/check_layers.py` FAIL: 13 `engine/voxel` to `libraries` edges. `git diff --name-only origin/main..HEAD -- engine/voxel tools/layers` is empty, so this gate failure is already present in the merged main content and is outside this SpectraFX branch's changes. The old PR #113 Clang log showed missing `override` in `GameCore.h`; syncing main changes the interface context, but the exact CI job has not rerun yet.

Not run: full native build, CTest, Metal, Studio UI and visual QA on this merge. Next: push the updated PR #113 branch, merge it into stacked PR #115, then inspect fresh CI. The 13 layer violations need correction by their owning area before CI can be fully green; do not silence the gate or claim the SpectraFX PR is ready for main.
