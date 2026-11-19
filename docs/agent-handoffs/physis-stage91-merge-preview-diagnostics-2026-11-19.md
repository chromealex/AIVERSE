# PHYSIS Stage 91 — TerraWeave merge preview exit 128 (diagnostic checkpoint)

Date: 2026-11-19
Repository: chromealex/AIVERSE
Integration-only branch: `integration/physis-terraweave-fall-20261119`
Previous handoff: `docs/agent-handoffs/physis-stage90-full-29-green-2026-11-19.md`.
No implementation or resolved merge is claimed.

## User's actual macOS command and result

User fetched origin, created an isolated detached worktree at
`~/Projects/AIVERSE-physis-terra-integration` from integration commit
`49638372a577f13d0154a8fab20123337a9ba3ee`, and ran:

```sh
git merge-tree --write-tree HEAD origin/feature/terraweave \
  > merge-preview.log 2>&1
echo "Merge preview exit: $?"
grep -E 'CONFLICT|Auto-merging' merge-preview.log | head -80
```

Reported: `Merge preview exit: 128`, no matching grep output.
**The actual error text is NOT YET KNOWN:** both stdout and stderr were
redirected into the local `merge-preview.log`, whose contents were not
provided. Do not confuse this fatal status with a confirmed merge conflict,
do not assume an unsupported Git option without checking `git --version`,
and do not attempt a destructive reset of another agent's worktree.

## GitHub verification

GitHub compare of `integration/physis-terraweave-fall-20261119` vs
`feature/terraweave` reports diverged ancestry with merge base
`d71c6d47e532b25fb816a8bb1f96c41e5275a165`. Thus the remote
branches have a common ancestor. The remote TerraWeave head observed at the
last check: `78f37f4fdd86639d16b9df0978bcee256abecb55`; note that the
user's earlier fetch referred to `e8c1a7941`, so the remote has advanced.
Do not interpret the ahead/behind commit counts as the number of conflicts.

`git merge-tree` is intended as a read-only preview. The user did not
report any working tree modifications after the command. No source merge
was performed by the GitHub connector here.

## Exact non-destructive native diagnosis

```bash
cd ~/Projects/AIVERSE-physis-terra-integration
git --version
git status --short
git merge-base HEAD origin/feature/terraweave
sed -n '1,120p' merge-preview.log
```

If the log says `unknown option` or `--write-tree` is unsupported,
install/use a modern Git or try the older three-argument read-only form:

```bash
BASE=$(git merge-base HEAD origin/feature/terraweave) || exit 1
git merge-tree "$BASE" HEAD origin/feature/terraweave \
  > merge-preview-legacy.log
```

The legacy form is not necessarily an accurate simulation of the modern
merge strategy; do not treat a silent legacy preview as proof of a conflict-free
real merge. If the log reports missing objects, diagnose repository/shallow
status and fetch repair first; do not delete refs, force-push or reset.

## Next stage

Once the exact stderr is known, perform a real merge **only inside a fresh
isolated integration worktree**; resolve CMake/root ownership conflicts,
then compile and run the conditional `physis_terraweave_fall_bridge`
and full PHYSIS/TerraWeave native CTest. Keep `main`, PHYSIS feature and
TerraWeave active feature unchanged. New 29/29 PHYSIS standalone results
belong to the prior source, not the combined branch.
