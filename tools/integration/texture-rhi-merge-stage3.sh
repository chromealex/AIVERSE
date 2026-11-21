#!/usr/bin/env bash
# Stage 3: merge a pinned main snapshot into the already accepted Texture RHI
# integration, never into the production main branch. Run in a fresh worktree.
set -euo pipefail
STAGE_BRANCH="integration/texture-rhi-main-stage3-20261120"
ACCEPTED_PARENT="d1c2df7ffe7b25b6fc14a415a8b8d63bfe73d5c6"
MAIN_SNAPSHOT="f9aed9a705982e2c6b77b29869e309995ce8b163"
die() { echo "FAIL: $*" >&2; exit 2; }
root="$(git rev-parse --show-toplevel)"
cd "$root"
branch="$(git branch --show-current)"
[[ "$branch" == "$STAGE_BRANCH" ]] || die "expected branch $STAGE_BRANCH, got $branch"
git merge-base --is-ancestor "$ACCEPTED_PARENT" HEAD ||
  die "accepted Texture RHI source SHA is not ancestor of current stage"
[[ -z "$(git status --porcelain)" ]] ||
  die "stage worktree is not clean; save local changes before merging"
if git rev-parse --verify -q MERGE_HEAD >/dev/null; then
  die "a merge is already in progress"
fi
# Fetch and prove the exact intended main object exists; a moving origin/main
# must not silently change which source revision is integrated.
git fetch origin main
git cat-file -e "${MAIN_SNAPSHOT}^{commit}" ||
  die "pinned main snapshot $MAIN_SNAPSHOT is unavailable"
echo "INFO: stage HEAD=$(git rev-parse HEAD)"
echo "INFO: pinned main=$MAIN_SNAPSHOT"
echo "INFO: origin/main=$(git rev-parse origin/main)"
set +e
git merge --no-ff --no-commit "$MAIN_SNAPSHOT"
merge_rc=$?
set -e
if [[ "$merge_rc" -eq 0 ]]; then
  echo "PASS: source merge staged with no conflicts (NOT committed)"
  git diff --cached --check || true
  echo "Next: run full engine + GPU acceptance, then commit and push stage branch"
  exit 0
fi
git rev-parse --verify -q MERGE_HEAD >/dev/null ||
  die "merge failed without an active MERGE_HEAD; inspect Git output"
echo "INFO: conflict paths:"
git diff --name-only --diff-filter=U
mkdir -p build/merge-stage3-diagnostics
git status --short > build/merge-stage3-diagnostics/status.txt
git diff --cc > build/merge-stage3-diagnostics/conflicts.diff || true
python3 - <<'PY'
from pathlib import Path
import subprocess
from zipfile import ZipFile, ZIP_DEFLATED
root = Path("build/merge-stage3-diagnostics")
paths = subprocess.check_output(
    ["git", "diff", "--name-only", "--diff-filter=U", "-z"]
).decode("utf-8").strip("\0").split("\0")
out = root / "aiverse-texture-stage3-conflicts.zip"
with ZipFile(out, "w", ZIP_DEFLATED) as arc:
    for path in paths:
        file = Path(path)
        if file.is_file():
            arc.write(file, f"merged/{path}")
        for stage, label in ((1, "base"), (2, "texture"), (3, "main")):
            content = subprocess.run(
                ["git", "show", f":{stage}:{path}"], capture_output=True
            )
            if content.returncode == 0:
                arc.writestr(f"stages/{label}/{path}", content.stdout)
    arc.write(root / "status.txt", "status.txt")
    arc.write(root / "conflicts.diff", "conflicts.diff")
print(f"INFO: saved {len(paths)} conflicted paths to {out}")
PY
echo "FAIL: $merge_rc from git merge; conflict archive generated. Do NOT commit"
exit 3
