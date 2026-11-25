"""Generate an inventory of registered feature manifests and detect declared regressions."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
from check_feature import ROOT, DIMENSIONS, validate
from legacy_inventory import inventory
from graph_inventory import inventory as graph_inventory
from capability_inventory import inventory as capability_inventory

PREFIX = "docs/features/"

def load_registry(root):
    result = {}
    for path in sorted((root / PREFIX).glob("*.json")):
        data = json.loads(path.read_text(encoding="utf-8"))
        if not isinstance(data, dict) or data.get("id") != path.stem:
            raise ValueError(f"invalid manifest identity: {path}")
        result[path.stem] = data
    return result

def baseline(root, ref):
    paths = subprocess.check_output(["git", "ls-tree", "-r", "--name-only", "-z", ref, "--", PREFIX], cwd=root).decode().split("\0")
    result = {}
    for path in paths:
        if path.endswith(".json"):
            data = json.loads(subprocess.check_output(["git", "show", f"{ref}:{path}"], cwd=root))
            result[Path(path).stem] = data
    return result

def regressions(old, new):
    errors = []
    for key, before in sorted(old.items()):
        after = new.get(key)
        if after is None:
            errors.append(f"{key}: registered feature removed")
            continue
        for dimension, check in before.get("checks", {}).items():
            if check.get("status") == "pass" and after.get("checks", {}).get(dimension, {}).get("status") != "pass":
                errors.append(f"{key}: lost PASS for {dimension}")
        for kind, names in before.get("integrations", {}).items():
            for name in sorted(set(names) - set(after.get("integrations", {}).get(kind, []))):
                errors.append(f"{key}: removed {kind} reference {name}")
        for trait, enabled in before.get("traits", {}).items():
            if enabled and not after.get("traits", {}).get(trait):
                errors.append(f"{key}: removed trait {trait}")
    return errors

def integration_gaps(registry, root):
    """List declared integration gaps, never infer runtime coverage from references."""
    tasks = []
    dimensions = ("native_api", "skill", "mcp", "studio", "inspector", "graphs")
    for ident, data in sorted(registry.items()):
        for dimension in dimensions:
            check = data.get("checks", {}).get(dimension, {})
            status = check.get("status", "missing")
            if status not in ("pass", "not_applicable"):
                tasks.append({"feature": ident, "surface": dimension, "status": status,
                              "kind": "investigate", "note": check.get("note", "")})
        for error in validate(data, root):
            tasks.append({"feature": ident, "surface": "manifest", "status": "invalid",
                          "kind": "fix_manifest", "note": error})
    return tasks

def matrix(registry):
    return {"scope": "registered manifests only; statuses are declared, not independent runtime proof",
            "count": len(registry), "features": [
                {"id": key, "owner": data.get("owner"), "checks": {
                    dim: data.get("checks", {}).get(dim, {}).get("status", "missing") for dim in DIMENSIONS},
                 "integrations": data.get("integrations", {})}
                for key, data in sorted(registry.items())]}

def owner_readiness(registry, root, capabilities, graphs, owner=None):
    """Group independent inventory views; do not count overlapping rows as unique features."""
    owners = {}
    def group(name):
        return owners.setdefault(name, {"owner": name, "manifests": [], "capabilities": [],
                                        "graphs": [], "tasks": []})
    for ident, data in sorted(registry.items()):
        bucket = group(data["owner"])
        checks = {dim: data.get("checks", {}).get(dim, {}).get("status", "missing")
                  for dim in DIMENSIONS}
        errors = validate(data, root)
        pending = sorted(dim for dim, status in checks.items()
                         if status not in ("pass", "not_applicable"))
        state = "invalid" if errors else "pending" if pending else "declared_ready"
        bucket["manifests"].append({"id": ident, "state": state, "checks": checks})
        if errors or pending:
            bucket["tasks"].append({"id": "manifest-audit:" + ident, "owner": data["owner"],
                                    "kind": "fix_manifest" if errors else "investigate",
                                    "feature": ident, "pending": pending, "errors": errors,
                                    "acceptance": ["Resolve listed manifest errors",
                                                   "Verify pending checks or document justified exclusions",
                                                   "Record evidence and update the manifest"]})
    for report, key, label in ((capabilities, "capabilities", "capabilities"),
                                (graphs, "graphs", "graphs")):
        for row in report[key]:
            group(row["owner"])[label].append(row["id"])
        for task in report["tasks"]:
            group(task["owner"])["tasks"].append(task)
    result = []
    seen = set()
    errors = list(capabilities["errors"]) + list(graphs["errors"])
    for name, bucket in sorted(owners.items()):
        for task in bucket["tasks"]:
            if task["id"] in seen:
                errors.append("duplicate audit task: " + task["id"])
            seen.add(task["id"])
        if owner is not None and name != owner:
            continue
        for key in ("capabilities", "graphs"):
            bucket[key].sort()
        priorities = {"fix_manifest": 0, "implement_gap": 1, "investigate": 2}
        bucket["tasks"].sort(key=lambda task: (priorities.get(task["kind"], 3), task["id"]))
        bucket["counts"] = {key: len(bucket[key]) for key in
                            ("manifests", "capabilities", "graphs", "tasks")}
        result.append(bucket)
    if owner is not None and not result:
        errors.append("unknown owner: " + owner)
    return {"scope": "Overlapping manifest, catalog capability and graph views; no runtime readiness claim",
            "owners": result, "errors": errors,
            "counts": {key: sum(row["counts"][key] for row in result) for key in
                       ("manifests", "capabilities", "graphs", "tasks")}}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base")
    parser.add_argument("--readiness", action="store_true", help="Unified owner-scoped audit queue")
    parser.add_argument("--owner", help="Filter unified readiness by exact owner ID")
    parser.add_argument("--gaps", action="store_true", help="List declared integration gaps and invalid references")
    parser.add_argument("--graphs", action="store_true", help="Include known graph domain inventory")
    parser.add_argument("--legacy", action="store_true", help="Include catalog inventory without claiming runtime verification")
    parser.add_argument("--format", choices=["json", "markdown"], default="json")
    args = parser.parse_args()
    if args.owner is not None and not args.readiness:
        parser.error("--owner requires --readiness")
    try:
        registry = load_registry(ROOT)
        errors = [f"{key}: {e}" for key, data in registry.items() for e in validate(data)]
        if args.base:
            errors.extend(regressions(baseline(ROOT, args.base), registry))
        report = matrix(registry)
        if args.gaps:
            report["integration_gaps"] = integration_gaps(registry, ROOT)
        if args.legacy:
            report["legacy_inventory"] = inventory(ROOT)
            errors.extend(report["legacy_inventory"]["errors"])
        if args.graphs:
            report["graph_inventory"] = graph_inventory(ROOT)
            errors.extend(report["graph_inventory"]["errors"])
        if args.readiness:
            report["readiness"] = owner_readiness(registry, ROOT, capability_inventory(ROOT),
                report.get("graph_inventory") or graph_inventory(ROOT), args.owner)
            errors.extend(report["readiness"]["errors"])
        report["errors"] = errors
        if args.format == "json":
            print(json.dumps(report, indent=2))
        else:
            columns = ("native_api", "mcp", "studio", "skill", "tests")
            print(report["scope"])
            print("| Feature | " + " | ".join(columns) + " |")
            print("|---|" + "---|" * len(columns))
            for row in report["features"]:
                print("| " + row["id"] + " | " + " | ".join(row["checks"][c] for c in columns) + " |")
            if args.legacy:
                print("\nLegacy catalog counts: " + json.dumps(report["legacy_inventory"]["counts"]))
                print("Legacy rows are catalog references, not verified feature manifests.")
            if args.graphs:
                print("\nKnown graph domains: " + str(len(report["graph_inventory"]["graphs"])))
                print("| Graph | Kind | MCP source matches | Studio IDs | Verification |")
                print("|---|---|---|---|---|")
                for row in report["graph_inventory"]["graphs"]:
                    print("| " + " | ".join([row["id"], row["kind"], str(len(row["literal_mcp_matches"])), ", ".join(row["studio_matches"]) or "unknown", row["verification"]]) + " |")
            if args.gaps:
                print("\nIntegration audit queue (declared, not runtime proof):")
                for task in report["integration_gaps"]:
                    print(f"- {task['feature']} / {task['surface']}: {task['status']} — {task['note']}")
            if args.readiness:
                readiness = report["readiness"]
                print("\n" + readiness["scope"])
                print("| Owner | Manifests | Catalog capabilities | Graphs | Audit tasks |")
                print("|---|---:|---:|---:|---:|")
                for bucket in readiness["owners"]:
                    counts = bucket["counts"]
                    print("| " + bucket["owner"] + " | " + " | ".join(str(counts[key]) for key in
                          ("manifests", "capabilities", "graphs", "tasks")) + " |")
                for bucket in readiness["owners"]:
                    print("\n### " + bucket["owner"])
                    for task in bucket["tasks"]:
                        print("- " + task["id"] + " (" + task["kind"] + ")")
            for error in errors:
                print("- " + error)
        return int(bool(errors))
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as exc:
        print(f"Registry error: {exc}", file=sys.stderr)
        return 1

if __name__ == "__main__":
    sys.exit(main())
