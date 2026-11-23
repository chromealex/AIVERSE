"""Structural feature evidence gate; no third-party dependencies."""
import argparse
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
DIMENSIONS = ("implementation", "architecture", "native_api", "skill", "mcp",
              "studio", "inspector", "persistence", "graphs", "performance",
              "platforms", "dependencies", "headless", "examples", "tests",
              "handoff", "documentation", "publishing")
ALWAYS = {"implementation", "architecture", "dependencies", "tests", "handoff", "documentation"}

def validate(data, root=ROOT, ready=False):
    errors = []
    if not isinstance(data, dict):
        return ["manifest must be an object"]
    if set(data) != {"schema", "id", "owner", "checks"}:
        errors.append("unexpected or missing manifest fields")
    if type(data.get("schema")) is not int or data["schema"] != 1:
        errors.append("schema must be 1")
    if not isinstance(data.get("id"), str) or not re.fullmatch(r"[a-z0-9][a-z0-9-]*", data["id"]):
        errors.append("invalid id")
    if not isinstance(data.get("owner"), str) or not data["owner"].strip():
        errors.append("missing owner")
    checks = data.get("checks")
    if not isinstance(checks, dict):
        return errors + ["checks must be an object"]
    if set(checks) != set(DIMENSIONS):
        errors.append("missing/unknown checks: " + ", ".join(sorted(set(checks) ^ set(DIMENSIONS))))
    for name, check in checks.items():
        if not isinstance(check, dict) or set(check) != {"status", "note", "evidence"}:
            errors.append(f"{name}: invalid check fields")
            continue
        status, note, evidence = check["status"], check["note"], check["evidence"]
        if status not in ("pass", "fail", "not_run", "not_applicable"):
            errors.append(f"{name}: invalid status")
        if not isinstance(note, str) or not note.strip():
            errors.append(f"{name}: missing verification note or exclusion reason")
        if name in ALWAYS and status == "not_applicable":
            errors.append(f"{name}: cannot be excluded")
        if ready and status not in ("pass", "not_applicable"):
            errors.append(f"{name}: not ready ({status})")
        if not isinstance(evidence, list):
            errors.append(f"{name}: evidence must be a list")
            continue
        if status == "pass" and not evidence:
            errors.append(f"{name}: pass requires evidence")
        for item in evidence:
            if not isinstance(item, str) or not item or Path(item).is_absolute() or ".." in Path(item).parts:
                errors.append(f"{name}: invalid evidence path")
                continue
            target = (root / item).resolve()
            if not target.is_relative_to(root.resolve()) or not target.is_file():
                errors.append(f"{name}: missing or external evidence {item}")
    return errors

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path, nargs="+")
    parser.add_argument("--ready", action="store_true")
    args = parser.parse_args()
    failures = 0
    for path in args.manifest:
        try:
            errors = validate(json.loads(path.read_text(encoding="utf-8")), ready=args.ready)
        except (OSError, ValueError) as exc:
            errors = [str(exc)]
        for error in errors:
            print(f"{path}: {error}", file=sys.stderr)
        failures += len(errors)
    print(f"Feature gate: {len(args.manifest)} manifest(s), {failures} error(s)")
    return int(bool(failures))

if __name__ == "__main__":
    sys.exit(main())
