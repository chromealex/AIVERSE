"""Graph-domain inventory. Static references never establish runtime parity."""
import argparse
import json
from pathlib import Path
import sys
from legacy_inventory import ROOT, read
from capability_inventory import source_operations

def inventory(root=ROOT):
    data = read(root, "docs/agents/graph-inventory.json")
    skills = {s["id"] for s in read(root, "skills/catalog.json")["skills"]}
    operations = source_operations(root)
    import re
    dock = (root / "apps/studio-web/src/nestedDock.ts").read_text(encoding="utf-8")
    match = re.search(r"STUDIO_PANELS\s*=\s*\[(.*?)\]\s*as const", dock, re.S)
    panels = set(re.findall(r"['\"]([^'\"]+)['\"]", match.group(1))) if match else set()
    rows, errors, tasks = [], [], []
    seen = set()
    for graph in data["graphs"]:
        ident = graph["id"]
        if ident in seen:
            errors.append("duplicate graph: " + ident)
        seen.add(ident)
        source = (root / graph["source"]).resolve()
        if not source.is_relative_to(root.resolve()) or not source.is_file():
            errors.append(f"{ident}: missing source")
        if graph["skill"] not in skills:
            errors.append(f"{ident}: unknown skill")
        row = dict(graph, verification="not_run",
                   literal_mcp_matches=[n for n in graph["mcp_candidates"] if n in operations],
                   unmatched_mcp_candidates=[n for n in graph["mcp_candidates"] if n not in operations],
                   studio_matches=[n for n in graph["studio_candidates"] if n in panels])
        rows.append(row)
        acceptance = ["Identify graph schema, version, owner and public API",
                      "Check actual execution, validation, persistence and regression tests"]
        if graph["kind"] == "authoring":
            acceptance += ["Exercise native MCP read/edit/validate and save/reopen",
                           "Verify visual graph workflow in Studio, not just a generic form"]
        else:
            acceptance += ["Determine whether visual authoring is applicable; do not invent a node editor"]
        tasks.append(dict(id="graph-audit:" + ident, owner=graph["owner"], kind="investigate", acceptance=acceptance))
    return dict(scope="Known graph-related domains only; not exhaustive and no functional PASS", graphs=rows, tasks=tasks, errors=errors)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--owner")
    args = parser.parse_args()
    try:
        report = inventory()
        if args.owner:
            report["graphs"] = [r for r in report["graphs"] if r["owner"] == args.owner]
            report["tasks"] = [r for r in report["tasks"] if r["owner"] == args.owner]
        print(json.dumps(report, indent=2, ensure_ascii=False))
        return int(bool(report["errors"]))
    except (OSError, ValueError, KeyError, TypeError) as exc:
        print(f"Graph inventory error: {exc}", file=sys.stderr)
        return 1

if __name__ == "__main__":
    sys.exit(main())
