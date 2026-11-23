"""Read existing capability catalogs without upgrading catalog claims to verified PASS."""
import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]

def read(root, path):
    return json.loads((root / path).read_text(encoding="utf-8"))

def inventory(root=ROOT):
    systems = read(root, "docs/devlog/systems.json")["systems"]
    packages = read(root, "libraries/catalog.json")["packages"]
    skills = read(root, "skills/catalog.json")["skills"]
    links = read(root, "docs/agents/legacy-integration-map.json")
    by_skill = {s["id"]: s for s in skills}
    errors = []
    rows = []
    def add(kind, ident, name, source, skill_ids, hints=None, extra=None):
        hints = hints or {}
        unknown = [s for s in skill_ids if s not in by_skill]
        errors.extend(f"{kind}:{ident}: unknown skill {s}" for s in unknown)
        rows.append(dict(id=f"{kind}:{ident}", name=name, source=source,
            skill_ids=sorted(set(skill_ids)), mcp_candidates=hints.get("mcp", []),
            studio_candidates=hints.get("studio", []),
            verification="not_run", catalog_metadata=extra or {}))
    system_ids = {s["id"] for s in systems}
    errors.extend(f"unknown system mapping: {s}" for s in sorted(set(links["systems"]) - system_ids))
    for s in systems:
        hint = links["systems"].get(s["id"], {})
        add("system", s["id"], s.get("nameEn", s["name"]), s.get("doc"), hint.get("skills", []), hint,
            {"release": s.get("release"), "roadmap": s.get("roadmap", []), "includes": s.get("includes", [])})
    for p in packages:
        ids = [s["id"] for s in skills if s["path"] == p.get("skill")]
        add("library", p["id"], p["id"], p.get("source"), ids,
            extra={"catalog_status": p.get("status"), "target": p.get("cmakeTarget"),
                   "reported_platforms": p.get("verifiedPlatforms", []), "runtime": p.get("runtime")})
        if not ids:
            errors.append(f"library:{p['id']}: missing catalog skill reference")
    for s in skills:
        add("skill", s["id"], s["id"], s.get("source"), [s["id"]],
            extra={"domain": s.get("domain"), "runtime": s.get("runtime"), "target": s.get("target")})
    return {"scope": "Catalog inventory; overlapping system/library/skill views, not distinct feature count or runtime parity.",
            "counts": {"systems": len(systems), "libraries": len(packages), "skills": len(skills)},
            "errors": errors, "entries": rows}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--format", choices=["json", "markdown"], default="json")
    args = parser.parse_args()
    try:
        data = inventory()
        if args.format == "json":
            print(json.dumps(data, ensure_ascii=False, indent=2))
        else:
            print(data["scope"])
            print(data["counts"])
            print("| System | Skills | MCP candidates | Studio candidates | Verification |")
            print("|---|---|---|---|---|")
            for row in data["entries"]:
                if row["id"].startswith("system:"):
                    print("| " + " | ".join([row["id"], ", ".join(row["skill_ids"]) or "unknown", ", ".join(row["mcp_candidates"]) or "unknown", ", ".join(row["studio_candidates"]) or "unknown", row["verification"]]) + " |")
            for error in data["errors"]:
                print("- " + error)
        return int(bool(data["errors"]))
    except (OSError, ValueError, KeyError, TypeError) as exc:
        print(f"Inventory error: {exc}", file=sys.stderr)
        return 1

if __name__ == "__main__":
    sys.exit(main())
