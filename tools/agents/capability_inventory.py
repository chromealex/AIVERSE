"""Inventory individual catalog capabilities and source-declared authoring surfaces."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
from legacy_inventory import ROOT, read

SURFACES = ("native_api", "mcp", "studio", "skill", "graphs", "tests")

def source_operations(root):
    # Conservative source scan: literal registrations only, not built registry.
    pattern = re.compile(r'\b(?:add\s*\(\s*|registerOp\s*\(\s*\{\s*)"([a-z][a-z0-9_]+)"')
    result = {}
    for path in sorted((root / "apps/editor/core").glob("*.cpp")):
        for match in pattern.finditer(path.read_text(encoding="utf-8")):
            result.setdefault(match.group(1), []).append(path.relative_to(root).as_posix())
    return result

def capability_id(system, item):
    # Prefer canonical explicit IDs; otherwise stable across list reordering.
    name = item.get("nameEn") or item["name"]
    return system + ":" + (item.get("id") or hashlib.sha256(name.encode()).hexdigest()[:16])

def inventory(root=ROOT):
    systems = read(root, "docs/devlog/systems.json")["systems"]
    mapping = read(root, "docs/agents/legacy-integration-map.json")["systems"]
    skills = {s["id"] for s in read(root, "skills/catalog.json")["skills"]}
    operations = source_operations(root)
    panels_source = (root / "apps/studio-web/src/nestedDock.ts").read_text(encoding="utf-8")
    match = re.search(r"STUDIO_PANELS\s*=\s*\[(.*?)\]\s*as const", panels_source, re.S)
    panels = set(re.findall(r"['\"]([^'\"]+)['\"]", match.group(1))) if match else set()
    rows, tasks, errors = [], [], []
    seen = set()
    for system in systems:
        owner = system["id"]
        hints = mapping.get(owner, {})
        for item in system.get("includes", []):
            ident = capability_id(owner, item)
            if ident in seen:
                errors.append("duplicate capability: " + ident)
                continue
            seen.add(ident)
            # System-level candidates are NOT specific capability coverage.
            candidates = {
                "mcp": [n for n in hints.get("mcp", []) if n in operations],
                "studio": [n for n in hints.get("studio", []) if n in panels],
                "skill": [n for n in hints.get("skills", []) if n in skills],
            }
            row = dict(id=ident, owner=owner, title=item.get("nameEn") or item["name"],
                       description=item.get("textEn") or item.get("text", ""),
                       source="docs/devlog/systems.json", owner_doc=system.get("doc"),
                       coverage={s: "unknown" for s in SURFACES},
                       owner_candidates=candidates)
            rows.append(row)
            # One bounded investigation task per capability, not six speculative implementations.
            tasks.append(dict(id="audit:" + ident, owner=owner, capability=ident,
                title="Audit " + row["title"], kind="investigate",
                acceptance=["Identify owning public API and exact integration references",
                            "Determine applicability of each surface; document exclusions",
                            "Run relevant native, MCP, Studio and graph workflows",
                            "Record actual evidence or create a bounded implementation task for a confirmed gap"]))
    return dict(scope="Catalog capabilities and literal source declarations; no runtime parity claims",
                capabilities=rows, tasks=tasks, source_operations=operations,
                studio_panel_ids=sorted(panels), errors=errors)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--system")
    parser.add_argument("--format", choices=["json", "markdown"], default="json")
    args = parser.parse_args()
    try:
        report = inventory()
        if args.system:
            report["capabilities"] = [r for r in report["capabilities"] if r["owner"] == args.system]
            report["tasks"] = [r for r in report["tasks"] if r["owner"] == args.system]
            if not report["capabilities"]:
                parser.error("no catalog capabilities for system")
        if args.format == "json":
            print(json.dumps(report, ensure_ascii=False, indent=2))
        else:
            print(report["scope"])
            for row in report["capabilities"]:
                print(f"- {row['id']}: {row['title']} — audit required")
            for error in report["errors"]:
                print("- ERROR: " + error)
        return int(bool(report["errors"]))
    except (OSError, ValueError, KeyError, TypeError) as exc:
        print(f"Capability inventory error: {exc}", file=sys.stderr)
        return 1

if __name__ == "__main__":
    sys.exit(main())
