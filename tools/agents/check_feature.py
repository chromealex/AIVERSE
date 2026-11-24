"""Structural feature evidence gate; no third-party dependencies."""
import argparse
import json
from pathlib import Path
import re
import sys
from check_integrations import validate_integrations
from evidence import validate_report

ROOT = Path(__file__).resolve().parents[2]
DIMENSIONS = ("implementation", "architecture", "native_api", "skill", "mcp",
              "studio", "inspector", "persistence", "graphs", "performance",
              "platforms", "dependencies", "headless", "examples", "tests",
              "handoff", "documentation", "publishing")
ALWAYS = {"implementation", "architecture", "dependencies", "tests", "handoff", "documentation"}

def validate(data, root=ROOT, ready=False, verify_git=False):
    errors = []
    if not isinstance(data, dict):
        return ["manifest must be an object"]
    if set(data) != {"schema", "id", "owner", "checks", "changed_paths", "traits", "integrations", "test_reports"}:
        errors.append("unexpected or missing manifest fields")
    if type(data.get("schema")) is not int or data["schema"] != 1:
        errors.append("schema must be 1")
    if not isinstance(data.get("id"), str) or not re.fullmatch(r"[a-z0-9][a-z0-9-]*", data["id"]):
        errors.append("invalid id")
    if not isinstance(data.get("owner"), str) or not data["owner"].strip():
        errors.append("missing owner")
    scope = data.get("changed_paths")
    if not isinstance(scope, list) or not scope or not all(isinstance(p, str) and p and not Path(p).is_absolute() and ".." not in Path(p).parts and p == Path(p).as_posix() for p in scope):
        errors.append("changed_paths must contain exact repository-relative paths")
    traits = data.get("traits")
    expected_traits = {"editable", "simulation", "visible", "runtime"}
    required = set(ALWAYS)
    if not isinstance(traits, dict) or set(traits) != expected_traits or any(type(v) is not bool for v in traits.values()):
        errors.append("traits must explicitly describe editable, simulation, visible and runtime")
    else:
        if traits["editable"]:
            required.update(("mcp", "studio", "inspector"))
        if traits["simulation"]:
            required.update(("native_api", "persistence", "headless"))
        if traits["visible"]:
            required.add("publishing")
        if traits["runtime"]:
            required.update(("performance", "platforms", "native_api"))
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
        if name in required and status == "not_applicable":
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
    reports = data.get("test_reports")
    if not isinstance(reports, list) or not all(isinstance(p, str) and p.startswith("docs/evidence/") and ".." not in Path(p).parts for p in reports):
        errors.append("test_reports must contain repository evidence paths")
    else:
        if isinstance(checks.get("tests"), dict) and checks["tests"].get("status") == "pass" and not reports:
            errors.append("tests: pass requires recorded test report")
        for report in reports:
            try:
                target = (root / report).resolve()
                if not target.is_relative_to(root.resolve()):
                    raise ValueError("external report")
                errors.extend(f"{report}: {e}" for e in validate_report(json.loads(target.read_text(encoding="utf-8")), root, verify_git))
            except (OSError, ValueError) as exc:
                errors.append(f"{report}: {exc}")
    errors.extend(validate_integrations(data, root, ready))
    return errors

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path, nargs="+")
    parser.add_argument("--ready", action="store_true")
    args = parser.parse_args()
    failures = 0
    for path in args.manifest:
        try:
            errors = validate(json.loads(path.read_text(encoding="utf-8")), ready=args.ready, verify_git=args.ready)
        except (OSError, ValueError) as exc:
            errors = [str(exc)]
        for error in errors:
            print(f"{path}: {error}", file=sys.stderr)
        failures += len(errors)
    print(f"Feature gate: {len(args.manifest)} manifest(s), {failures} error(s)")
    return int(bool(failures))

if __name__ == "__main__":
    sys.exit(main())
