"""Bounded agent planning helpers. No agent execution, merges or release publication."""
import argparse
from collections import defaultdict
from difflib import SequenceMatcher
import json
from pathlib import Path
import re
import subprocess
import sys
from check_feature import ROOT
from feature_policy import repo_path

CONFIG = "docs/agents/feature-automation.json"

def read_config(root):
    data = json.loads((root / CONFIG).read_text(encoding="utf-8"))
    if (not isinstance(data, dict) or type(data.get("schema")) is not int or data["schema"] != 1
            or set(data) != {"schema", "agents", "releases", "architecture_exceptions"}):
        raise ValueError("invalid automation config")
    if not isinstance(data["agents"], list) or not isinstance(data["releases"], dict) or not isinstance(data["architecture_exceptions"], list):
        raise ValueError("invalid automation config collections")
    return data

def git(root, *args):
    return subprocess.check_output(["git", *args], cwd=root, text=True)

def discover_branches(root, base):
    """Local and fetched remote refs only. Exclude refs already merged into base."""
    refs = git(root, "for-each-ref", "--no-merged", base,
               "--format=%(refname)", "refs/heads/feature/", "refs/heads/integration/",
               "refs/remotes/origin/feature/", "refs/remotes/origin/integration/").splitlines()
    chosen = {}
    for ref in sorted(refs):
        name = ref.removeprefix("refs/heads/").removeprefix("refs/remotes/origin/")
        chosen.setdefault(name, ref)
    return sorted(chosen.values())

def impact_report(registry, policies, changed):
    changed = set(changed)
    direct = {}
    reverse = defaultdict(set)
    for ident, data in registry.items():
        policy = policies.get(ident, {})
        paths = set(data.get("changed_paths", [])) | set(policy.get("api_paths", []))
        hits = sorted(paths & changed)
        if hits:
            direct[ident] = hits
        for dep in policy.get("requires", []):
            reverse[dep].add(ident)
    affected = set(direct)
    queue = sorted(direct)
    while queue:
        ident = queue.pop(0)
        for dependent in sorted(reverse[ident] - affected):
            affected.add(dependent)
            queue.append(dependent)
    return {"direct": direct, "transitive": sorted(affected - direct.keys()),
            "unmapped_paths": sorted(changed - {p for paths in direct.values() for p in paths}),
            "scope": "declared paths and prerequisite closure; not a complete include or link dependency analysis"}

def release_scope(config, target, dependencies):
    ids = config["releases"].get(target)
    if not isinstance(ids, list) or not ids or not all(isinstance(x, str) for x in ids) or len(set(ids)) != len(ids):
        raise ValueError("release must declare a nonempty unique list of feature IDs")
    states = dependencies["features"]
    unknown = sorted(set(ids) - states.keys())
    if unknown:
        raise ValueError("unknown release features: " + ", ".join(unknown))
    closure = set(ids)
    pending = list(ids)
    while pending:
        for dep in states[pending.pop()]["requires"]:
            if dep not in closure:
                closure.add(dep)
                pending.append(dep)
    return {"target": target, "requested": sorted(ids), "features": sorted(closure),
            "implicit_prerequisites": sorted(closure - set(ids))}

def schedule_tasks(readiness, dependencies, agents, conflict_paths, registry):
    """Deterministic proposals, never grants ownership or dispatches external agents."""
    seen = set()
    slots = {}
    for row in agents:
        if not isinstance(row, dict) or set(row) != {"id", "owners", "capacity"}:
            raise ValueError("invalid agent entry")
        ident, owners, capacity = row["id"], row["owners"], row["capacity"]
        if (not isinstance(ident, str) or not ident or ident in seen or not isinstance(owners, list)
                or not owners or not all(isinstance(o, str) for o in owners)
                or type(capacity) is not int or not 0 <= capacity <= 5):
            raise ValueError("invalid agent capacity or ownership")
        seen.add(ident)
        slots[ident] = {"owners": set(owners), "remaining": capacity}
    states = dependencies["features"]
    rank = {name: i for i, name in enumerate(dependencies["order"])}
    priority = {"fix_manifest": 0, "implement_gap": 1, "investigate": 2}
    tasks = [dict(t, owner=b["owner"]) for b in readiness["owners"] for t in b["tasks"]]
    tasks.sort(key=lambda t: (rank.get(t.get("feature"), len(rank)),
                              priority.get(t["kind"], 3), t["id"]))
    out, claimed = [], set()
    for task in tasks:
        feature = task.get("feature")
        paths = set(registry.get(feature, {}).get("changed_paths", []))
        blocked = states.get(feature, {}).get("blocked_by", [])
        candidates = sorted(a for a, slot in slots.items() if slot["remaining"] and task["owner"] in slot["owners"])
        reason, agent = None, None
        if blocked:
            reason = "prerequisites: " + ", ".join(blocked)
        elif paths & (set(conflict_paths) | claimed):
            reason = "overlapping claimed paths"
        elif not candidates:
            reason = "no available matching agent"
        elif not paths and task["kind"] == "implement_gap":
            reason = "implementation paths require owner review"
        else:
            agent = min(candidates, key=lambda a: (-slots[a]["remaining"], a))
            slots[agent]["remaining"] -= 1
            claimed.update(paths)
        out.append({"task": task["id"], "owner": task["owner"], "agent": agent,
                    "state": "proposed" if agent else "blocked", "reason": reason})
    return out

def skill_similarity(root, skills, threshold=0.86):
    """Lexical review candidates, never automatic duplicate classification."""
    rows = []
    for entry in skills:
        path = (root / entry["path"]).resolve()
        if not path.is_relative_to(root.resolve()) or not path.is_file():
            continue
        body = path.read_text(encoding="utf-8")
        if body.startswith("---"):
            body = body.split("---", 2)[-1]
        words = set(re.findall(r"[a-z0-9_]{3,}", body.lower()))
        if words:
            rows.append((entry["id"], entry.get("domain"), words))
    pairs = []
    for i, (a, domain, aw) in enumerate(rows):
        for b, bd, bw in rows[i+1:]:
            if domain != bd:
                continue
            score = len(aw & bw) / len(aw | bw)
            if score >= threshold:
                pairs.append({"skills": sorted([a,b]), "similarity": round(score, 3)})
    return sorted(pairs, key=lambda p: (-p["similarity"], p["skills"]))

def architecture_report(root, changed, exceptions):
    """Conservative changed-file include guard. Findings require review."""
    findings = []
    excluded = set(exceptions)
    for rel in sorted(set(changed)):
        if not repo_path(rel) or rel in excluded:
            continue
        if not rel.startswith(("engine/", "libraries/")) or Path(rel).suffix not in (".h", ".hpp", ".cpp", ".cc"):
            continue
        path = (root / rel).resolve()
        if not path.is_relative_to(root.resolve()) or not path.is_file():
            continue
        for line, text in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            match = re.match(r'\s*#\s*include\s*[<"]([^>"]+)[>"]', text)
            if not match:
                continue
            include = match.group(1).replace("\\", "/")
            if re.search(r"(^|/)(apps/editor|apps/studio-native|tools/mcp|modules/rts|modules/fps)(/|$)", include):
                findings.append({"path": rel, "line": line, "include": include,
                                 "rule": "shared engine must not depend on editor or genre host"})
    return {"findings": findings, "scope": "lexical changed-file include check; not ECS semantic verification"}

def feature_history(root, ident, limit=30):
    if not re.fullmatch(r"[a-z0-9][a-z0-9-]*", ident) or not 1 <= limit <= 200:
        raise ValueError("invalid feature ID or history limit")
    path = f"docs/features/{ident}.json"
    shas = git(root, "log", "--format=%H", "-n", str(limit), "--", path).splitlines()
    rows = []
    for sha in shas:
        try:
            data = json.loads(git(root, "show", f"{sha}:{path}"))
        except subprocess.CalledProcessError:
            rows.append({"sha": sha, "state": "absent"})
            continue
        rows.append({"sha": sha, "checks": {k: v["status"] for k, v in data["checks"].items()},
                     "integrations": data["integrations"]})
    return rows

def task_context(root, report, registry, task_id):
    tasks = [t for owner in report["readiness"]["owners"] for t in owner["tasks"] if t["id"] == task_id]
    if len(tasks) != 1:
        raise ValueError("task missing or ambiguous")
    task = tasks[0]
    feature = registry.get(task.get("feature"), {})
    refs = ["AGENTS.md", "docs/agents/feature-contract.md",
            "skills/workflow/aiverse-new-feature/SKILL.md"]
    refs += [f"docs/features/{feature['id']}.json"] if "id" in feature else []
    if task.get("evidence"):
        refs.append(task["evidence"])
    return {"sha": report["sha"], "task": task, "paths": feature.get("changed_paths", []),
            "integrations": feature.get("integrations", {}), "read_first": list(dict.fromkeys(refs)),
            "rules": ["Do not infer coverage from catalog hints", "Use owning APIs and existing skills",
                      "Work in an isolated branch", "Record exact evidence and handoff; no unverified PASS"]}

def scaffold_gap(root, report, registry, task_id, output):
    context = task_context(root, report, registry, task_id)
    task = context["task"]
    if task["kind"] != "implement_gap":
        raise ValueError("only evidence-backed implementation gaps can be scaffolded")
    # Generate a review packet, not registered placeholder operations.
    packet = dict(context, draft=True, surface=task.get("surface"),
                  next_steps=["Locate the existing owner API and reuse it",
                              "Draft integration and regression tests",
                              "Update manifest and roadmap before implementation"])
    output.mkdir(parents=True, exist_ok=True)
    name = re.sub(r"[^a-zA-Z0-9_-]", "-", task_id)
    path = output / (name + ".json")
    with path.open("x", encoding="utf-8") as f:
        json.dump(packet, f, indent=2, ensure_ascii=False)
        f.write("\n")
    return path

def main():
    from feature_registry import load_registry
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path, default=ROOT / "logs/feature-readiness.json")
    sub = parser.add_subparsers(dest="command", required=True)
    history = sub.add_parser("history")
    history.add_argument("feature")
    history.add_argument("--limit", type=int, default=30)
    context = sub.add_parser("context")
    context.add_argument("task")
    draft = sub.add_parser("draft")
    draft.add_argument("task")
    draft.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == "history":
            result = feature_history(ROOT, args.feature, args.limit)
        else:
            report = json.loads(args.report.read_text(encoding="utf-8"))
            if report.get("sha") != git(ROOT, "rev-parse", "HEAD").strip():
                raise ValueError("report HEAD is stale")
            registry = load_registry(ROOT)
            if args.command == "context":
                result = task_context(ROOT, report, registry, args.task)
            else:
                result = {"draft": str(scaffold_gap(ROOT, report, registry, args.task, args.output))}
        print(json.dumps(result, indent=2, ensure_ascii=False))
        return 0
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as exc:
        print(f"Feature automation: {exc}", file=sys.stderr)
        return 1

if __name__ == "__main__":
    sys.exit(main())
