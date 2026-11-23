"""Check PR path coverage and feature readiness against an explicit git base."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
from check_feature import ROOT, validate

PREFIXES = ("engine/", "libraries/", "modules/", "apps/", "tools/", "skills/", "platform/", "plugins/", "demos/", "tests/", ".github/workflows/", "docs/agents/contracts/")
EXACT = {"AGENTS.md", "CMakeLists.txt", "run-tests.sh"}
MANIFEST = "docs/features/"

def covered_paths(changed, manifests):
    required = {p for p in changed if p.startswith(PREFIXES) or p in EXACT}
    claimed = set()
    errors = []
    for path, data in manifests.items():
        if not isinstance(data, dict):
            errors.append(f"{path}: invalid manifest")
            continue
        scope = data.get("changed_paths")
        if not isinstance(scope, list) or not all(isinstance(p, str) for p in scope):
            errors.append(f"{path}: invalid changed_paths")
            continue
        claimed.update(scope)
        traits = data.get("traits", {})
        if isinstance(traits, dict):
            actual = set(scope) & changed
            if any(p.startswith(("engine/", "libraries/", "modules/")) for p in actual) and not traits.get("runtime"):
                errors.append(f"{path}: engine/library/module changes require runtime trait")
            if any(p.startswith(("engine/sim/", "engine/core/")) for p in actual) and not traits.get("simulation"):
                errors.append(f"{path}: simulation/core changes require simulation trait")
    for path in sorted(required - claimed):
        errors.append(f"uncovered changed path: {path}")
    return errors

def git_changed(base, head):
    result = subprocess.run(["git", "diff", "--no-renames", "--name-only", "-z", base + "..." + head, "--"],
                            cwd=ROOT, check=True, stdout=subprocess.PIPE)
    return set(result.stdout.decode("utf-8").split("\0")) - {""}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", required=True)
    parser.add_argument("--head", default="HEAD")
    parser.add_argument("--allow-partial", action="store_true")
    args = parser.parse_args()
    try:
        changed = git_changed(args.base, args.head)
    except (OSError, subprocess.CalledProcessError, UnicodeError) as exc:
        parser.error(str(exc))
    manifests = {}
    errors = []
    for path in sorted(changed):
        if not path.startswith(MANIFEST) or not path.endswith(".json"):
            continue
        try:
            data = json.loads((ROOT / path).read_text(encoding="utf-8"))
            manifests[path] = data
            errors.extend(f"{path}: {e}" for e in validate(data, ready=not args.allow_partial, verify_git=not args.allow_partial))
            if isinstance(data, dict) and Path(path).stem != data.get("id"):
                errors.append(f"{path}: filename must match id")
        except (OSError, ValueError) as exc:
            errors.append(f"{path}: {exc}")
    errors.extend(covered_paths(changed, manifests))
    for error in errors:
        print(error, file=sys.stderr)
    print(f"Feature compliance: {len(manifests)} manifest(s), {len(errors)} error(s)")
    return int(bool(errors))

if __name__ == "__main__":
    sys.exit(main())
