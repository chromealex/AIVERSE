# PHYSIS Stage 94 — guarded TerraWeave merge conflict resolver

Date: 2026-11-19
Repository: `chromealex/AIVERSE`
Isolated integration branch: `integration/physis-terraweave-fall-20261119`
Previous checkpoint: `docs/agent-handoffs/physis-stage92-git233-merge-recovery-2026-11-19.md`
Resolver implementation commit: `a26046faedb77f72d2b873df53af5e024a835127`
Handoff commit: `git log -1 -- docs/agent-handoffs/physis-stage94-merge-conflict-resolution-2026-11-19.md`

## Observed real merge on Mac Studio

User created local `physis-terraweave-local-merge` based on the dedicated integration branch and ran `git merge --no-commit --no-ff origin/feature/terraweave` with Git 2.33.0. Git auto-merged other changes and reported **exactly two unresolved paths**:

- `AGENTS.md` — content conflict
- `CMakeLists.txt` — content conflict

The displayed `git diff --name-only --diff-filter=U` contains only these two paths. The rest of the large TerraWeave source additions was staged by Git's incomplete merge, not yet committed.

## Three-way inspection and resolution decision

Inspected base `d71c6d47e532b25fb816a8bb1f96c41e5275a165`, PHYSIS integration source `80b35882da796e850555c67faaaea479d09634bb`, and TerraWeave source `bf21edf64f7f7a009e123168399bf9a4f58798e5` through the connected GitHub repository. Both overlaps are **independent additions at EOF**, not deletions or competing implementations:

- `AGENTS.md`: PHYSIS instructions/ownership matrix appended on one side; TerraWeave design and handoffs appended on the other. Preserve both instructions.
- `CMakeLists.txt`: PHYSIS test/feature registration block on one side, canonical TerraWeave and GPU timing test registration on the other. Preserve both sets; do not choose whole-file `--ours`/`--theirs`.

Created `tools/resolve_physis_terraweave_merge.py` **on the integration ref only**. It requires an active merge with exactly the two known unmerged files; checks one complete EOF conflict in each; validates PHYSIS and TerraWeave anchors in the correct sections; preserves both sides in PHYSIS-then-Terra order; saves source conflict-marker backups to a new temporary directory; stages only those two paths. It refuses unexpected conflicts/markers. It does not commit, push, checkout or reset other files/branches.

### How to run it on the conflicted Mac worktree

```bash
cd ~/Projects/AIVERSE-physis-terra-integration
git status --short | head -10
git fetch origin integration/physis-terraweave-fall-20261119
git show origin/integration/physis-terraweave-fall-20261119:tools/resolve_physis_terraweave_merge.py > /tmp/resolve_physis_terraweave_merge.py
python3 /tmp/resolve_physis_terraweave_merge.py
git diff --name-only --diff-filter=U
git diff --cached --check
```

If resolver refuses, keep the user's real conflict state unchanged and inspect markers using `git diff -- AGENTS.md CMakeLists.txt` and `git ls-files -u` before editing manually.

### Focused compiled check, after staging but BEFORE committing merge

```bash
python3 tools/aiverse-progress.py --mode all \
  --build-dir build/physis-terra-combined \
  --define AIVERSE_BUILD_TESTS=ON \
  --define AIVERSE_ENABLE_PHYSICS=ON \
  --target aiverse_physis_terraweave_fall_bridge_test \
  --target aiverse_physis_nature_fall_host_test \
  --test-regex '^physis_(terraweave_fall_bridge|nature_fall_host)$' \
  --jobs 4
```

This is the FIRST real build of `engine/world/PhysisTerraWeaveFallBridge.h` with canonical TerraWeave + Box3D. Fix concrete compile/runtime issues if any. Follow with full `^physis_` plus `^terraweave` and unfiltered AIVERSE regressions. Do not commit/merge to `main` until these checks.

## Actual tests and limitations

Stage94 is source-resolution tooling and handoff only; **the resolver has NOT run in the user's Mac worktree**, and the combined PHYSIS + TerraWeave source has NOT yet compiled or passed tests. Previously reported PHYSIS 29/29 applies to the PHYSIS-only checkout, not this uncommitted local merge. This stage does not establish actual Box3D fall correctness.

The active `feature/physis-terraweave-fracture-host`, `feature/terraweave`, `feature/physis-fracture-delivery`, and `main` remote refs remain untouched. Do not force-push or reset an agent worktree.

## Next handoff

Record actual resolver outcome, merged local index, exact CMake/compiler/CTest results, and any required host adapter changes. Resolve true integration errors rather than weakening assertions or masking them with a stub.
