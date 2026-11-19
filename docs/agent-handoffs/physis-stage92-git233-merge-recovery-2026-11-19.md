# PHYSIS Stage 92 — Git 2.33 merge-tree option mismatch diagnosed

Date: 2026-11-19
Repository: `chromealex/AIVERSE`
Integration-only branch: `integration/physis-terraweave-fall-20261119`
Prior handoff: `docs/agent-handoffs/physis-stage91-merge-preview-diagnostics-2026-11-19.md`.

## Verified root cause from Mac Studio

User supplied the exact native terminal output in the isolated
`~/Projects/AIVERSE-physis-terra-integration` worktree:

```text
git version 2.33.0
git status --short
# (no paths printed)
git merge-base HEAD origin/feature/terraweave
d71c6d47e532b25fb816a8bb1f96c41e5275a165
sed -n '1,100p' merge-preview.log
fatal: unknown rev --write-tree
```

The preview failure `exit 128` came from Git 2.33 not recognizing
`git merge-tree --write-tree`. **This is not evidence of a content merge
conflict**. No merge was attempted by the read-only command. The worktree
was clean at that point, and both histories share a merge base. Do not
attempt to resolve imaginary conflicts or report TerraWeave/PHYSIS integration
green. Nothing in the engine or tests changed as part of this diagnosis.

## Next safe user-native action

From the dedicated isolated worktree only, with a clean status, use local
branch and the supported Git 2.33 real merge command:

```bash
cd ~/Projects/AIVERSE-physis-terra-integration
git fetch origin
git switch -c physis-terraweave-local-merge
git merge --no-commit --no-ff origin/feature/terraweave
git status --short
git diff --name-only --diff-filter=U
```

This performs a real, reversible, non-committed merge **only in that
integration worktree**; it does not mutate `main`, PHYSIS feature, or
TerraWeave feature branches. If the local branch name already exists, use
another unique local name rather than overwriting a branch. Preserve the
first conflict list and avoid force push, hard reset, or unreviewed
ours/theirs replacements. The integration worktree's detached HEAD was
`49638372a` in the user's last snippet; a documentation-only Stage91
commit was added to the remote integration branch later. If preserving
that handoff in the eventual merge commit matters, start the local branch
from the refreshed remote integration ref instead of detached old HEAD.

An optional read-only legacy preview compatible with 2.33 is
`git merge-tree "$(git merge-base HEAD origin/feature/terraweave)" HEAD origin/feature/terraweave`,
but the legacy form differs from the modern merge algorithm and is not
proof that the real merge is clean.

## Verification state and next milestone

- Stage87 source: user's native PHYSIS suite **29/29 PASS** (Stage90).
- Combined PHYSIS/TerraWeave source: **NOT MERGED, NOT BUILT, NOT TESTED**.
- Stage92: diagnostic/docs only; no CMake, CTest, GUI, Metal or CI run.
- When conflicts (if any) are resolved within the integration worktree,
  compile and run the optional real
  `physis_terraweave_fall_bridge` with `AIVERSE_ENABLE_PHYSICS=ON`,
  then run broader PHYSIS/TerraWeave regressions. Record exact native
  source SHA, test list, results, and any remaining real Box3D pose rollback
  limitations in another per-stage handoff.
