"""Scaffold a feature manifest and handoff; optional skill draft stays unregistered until reviewed."""
import argparse
import json
from pathlib import Path
import re
from check_feature import ROOT, DIMENSIONS

def scaffold(root, feature, owner, paths, traits, skill_draft=False):
    if not re.fullmatch(r"[a-z0-9][a-z0-9-]*", feature) or not owner.strip():
        raise ValueError("invalid feature ID or owner")
    if not paths or any(not p or Path(p).is_absolute() or ".." in Path(p).parts or p != Path(p).as_posix() or any(c in p for c in "*?[]") for p in paths):
        raise ValueError("provide exact repository-relative changed paths")
    manifest = {"schema": 1, "id": feature, "owner": owner, "changed_paths": sorted(set(paths)),
                "traits": traits, "checks": {d: {"status": "not_run", "note": "Pending implementation and verification.", "evidence": []} for d in DIMENSIONS},
                "integrations": {k: [] for k in ("skills", "mcp_tools", "studio_panels", "studio_methods")}, "test_reports": []}
    files = {
        f"docs/features/{feature}.json": json.dumps(manifest, indent=2) + "\n",
        f"docs/agent-handoffs/{feature}-initial.md": f"# {feature} — initial handoff\n\nOwner: {owner}\nBranch/SHA: pending\nArchitecture/API: pending\nChanged paths: pending\nTests: NOT RUN\nRisks: pending\nNext: confirm owner, update roadmap and implement.\n"}
    if skill_draft:
        files[f"docs/features/{feature}-skill-draft.md"] = f"---\nname: aiverse-{feature}\ndescription: TODO\n---\n# {feature}\n\nTODO: responsibility, API, target, dependencies, example and validation. Search existing skills before registering.\n"
    for path in files:
        target = root / path
        if target.exists() or not target.resolve().is_relative_to(root.resolve()):
            raise ValueError(f"refusing to overwrite or escape root: {path}")
    for path, content in files.items():
        target = root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        with target.open("x", encoding="utf-8") as out:
            out.write(content)
    return list(files)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("id")
    parser.add_argument("--owner", required=True)
    parser.add_argument("--path", action="append", required=True)
    for trait in ("editable", "simulation", "visible", "runtime"):
        parser.add_argument("--" + trait, action="store_true")
    parser.add_argument("--skill-draft", action="store_true")
    args = parser.parse_args()
    traits = {k: getattr(args, k) for k in ("editable", "simulation", "visible", "runtime")}
    if traits["simulation"]:
        traits["runtime"] = True
    try:
        for path in scaffold(ROOT, args.id, args.owner, args.path, traits, args.skill_draft):
            print(path)
    except (OSError, ValueError) as exc:
        parser.error(str(exc))

if __name__ == "__main__":
    main()
