"""Generate an inventory of registered feature manifests and detect declared regressions."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
from check_feature import ROOT, DIMENSIONS, validate
from legacy_inventory import inventory
from graph_inventory import inventory as graph_inventory

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

def matrix(registry):
    return {"scope": "registered manifests only; statuses are declared, not independent runtime proof",
            "count": len(registry), "features": [
                {"id": key, "owner": data.get("owner"), "checks": {
                    dim: data.get("checks", {}).get(dim, {}).get("status", "missing") for dim in DIMENSIONS},
                 "integrations": data.get("integrations", {})}
                for key, data in sorted(registry.items())]}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base")
    parser.add_argument("--graphs", action="store_true", help="Include known graph domain inventory")
    parser.add_argument("--legacy", action="store_true", help="Include catalog inventory without claiming runtime verification")
    parser.add_argument("--format", choices=["json", "markdown"], default="json")
    args = parser.parse_args()
    try:
        registry = load_registry(ROOT)
        errors = [f"{key}: {e}" for key, data in registry.items() for e in validate(data)]
        if args.base:
            errors.extend(regressions(baseline(ROOT, args.base), registry))
        report = matrix(registry)
        if args.legacy:
            report["legacy_inventory"] = inventory(ROOT)
            errors.extend(report["legacy_inventory"]["errors"])
        if args.graphs:
            report["graph_inventory"] = graph_inventory(ROOT)
            errors.extend(report["graph_inventory"]["errors"])
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
            for error in errors:
                print("- " + error)
        return int(bool(errors))
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as exc:
        print(f"Registry error: {exc}", file=sys.stderr)
        return 1

if __name__ == "__main__":
    sys.exit(main())
