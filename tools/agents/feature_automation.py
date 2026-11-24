"""Bounded agent planning helpers. No agent execution, merges or release publication."""
import argparse
import hashlib
from collections import defaultdict
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
    for current, older in zip(rows, rows[1:]):
        current["regressions"] = sorted(k for k,v in older.get("checks", {}).items()
                                         if v == "pass" and current.get("checks", {}).get(k) != "pass")
    return rows

def coverage_history(root, capability, limit=30):
    """Historical declarations only; a historical PASS is not current evidence."""
    if not isinstance(capability, str) or not capability or not 1 <= limit <= 200:
        raise ValueError("invalid capability or limit")
    from capability_coverage import LEDGER
    rows = []
    previous = None
    for sha in git(root, "log", "--format=%H", "-n", str(limit), "--", LEDGER).splitlines():
        try:
            data = json.loads(git(root, "show", f"{sha}:{LEDGER}"))
        except subprocess.CalledProcessError:
            data = {"records": []}
        values = {r["surface"]: r["status"] for r in data["records"] if r["capability"] == capability}
        if values != previous:
            rows.append({"sha": sha, "coverage": values})
            previous = values
    for current, older in zip(rows, rows[1:]):
        current["regressions"] = sorted(k for k,v in older["coverage"].items()
                                         if v == "pass" and current["coverage"].get(k) != "pass")
    return rows

def task_context(root, report, registry, task_id):
    tasks = [dict(t, owner=b["owner"]) for b in report["readiness"]["owners"]
             for t in b["tasks"] if t["id"] == task_id]
    if len(tasks) != 1:
        raise ValueError("task missing or ambiguous")
    task = tasks[0]
    feature = registry.get(task.get("feature"), {})
    refs = ["AGENTS.md", "docs/agents/feature-contract.md",
            "docs/agents/contracts/architecture-common.md",
            "docs/agents/contracts/workflow-parallel.md",
            "skills/workflow/aiverse-new-feature/SKILL.md"]
    paths = list(feature.get("changed_paths", []))
    refs += [f"docs/features/{feature['id']}.json"] if "id" in feature else []
    systems = json.loads((root / "docs/devlog/systems.json").read_text(encoding="utf-8"))["systems"]
    owner = next((row for row in systems if row["id"] == task["owner"]), {})
    if repo_path(owner.get("doc")):
        refs.append(owner["doc"])
    if repo_path(task.get("evidence")):
        refs.append(task["evidence"])
    surface = task.get("surface")
    if surface == "studio":
        refs.extend(["docs/agents/contracts/studio-contract.md", "docs/studio-ui-agent-guide.md",
                     "skills/editor/studio-ui/SKILL.md"])
    if surface == "mcp":
        refs.append("docs/agents/mcp-coverage.md")
    if surface == "skill":
        refs.append("docs/agents/contracts/reuse-skills.md")
    skills = json.loads((root / "skills/catalog.json").read_text(encoding="utf-8"))["skills"]
    wanted = set(feature.get("integrations", {}).get("skills", []))
    refs += [entry["path"] for entry in skills if entry["id"] in wanted]
    dependencies = report["policy"]["dependencies"]["features"].get(task.get("feature"), {})
    refs += [f"docs/features/{dep}.json" for dep in dependencies.get("requires", [])]
    existing = [p for p in dict.fromkeys(refs) if repo_path(p) and
                (root / p).resolve().is_relative_to(root.resolve()) and (root / p).is_file()]
    return {"sha": report["sha"], "task": task, "paths": paths,
            "integrations": feature.get("integrations", {}), "read_first": existing,
            "missing_references": sorted(set(refs) - set(existing)),
            "dependencies": dependencies.get("requires", []),
            "rules": ["Do not infer coverage from catalog hints", "Use owning APIs and existing skills",
                      "Work in an isolated branch", "Record exact evidence and handoff; no unverified PASS"]}

def write_new_json(path, data):
    with path.open("x", encoding="utf-8") as f:
        json.dump(data, f, indent=2, ensure_ascii=False)
        f.write("\n")

def packet_name(task_id):
    return re.sub(r"[^a-zA-Z0-9_-]", "-", task_id)[:80] + "-" + hashlib.sha256(task_id.encode()).hexdigest()[:12]

def scaffold_gap(root, report, registry, task_id, output):
    context = task_context(root, report, registry, task_id)
    task = context["task"]
    if task["kind"] != "implement_gap":
        raise ValueError("only evidence-backed implementation gaps can be scaffolded")
    folder = output / packet_name(task_id)
    folder.mkdir(parents=True, exist_ok=False)
    surface = task.get("surface")
    packet = dict(context, draft=True, surface=surface,
                  next_steps=["Locate the existing owner API and reuse it",
                              "Complete implementation and regression tests",
                              "Update manifest and roadmap before registration"])
    write_new_json(folder / "task.json", packet)
    if surface == "mcp":
        draft = '''// UNREGISTERED DRAFT. Replace placeholders after resolving the owning API.
#include "Workspace.h"
namespace aiverse::editor {
inline void registerReviewedCapabilityDraft(Workspace& ws) {
    ws.registerOp({"REVIEW_OPERATION_ID", "REVIEW_DESCRIPTION",
        Json::parse(R"({"type":"object","additionalProperties":false})"),
        [](const Json&) -> Json {
            throw std::runtime_error("Draft integration: owner implementation required");
        }, false});
}
}
'''
        name = "CapabilityOps.h.in"
    elif surface == "studio":
        draft = '''// UNREGISTERED DRAFT. Bind typed native owner operation before registration.
import { StudioPanel, StudioSection, StudioAlert } from '../ui';
export default function CapabilityDraftPanel() {
  return <StudioPanel title="Capability draft">
    <StudioSection title="Integration">
      <StudioAlert tone="warning">Owner API not wired. No runtime evidence.</StudioAlert>
    </StudioSection>
  </StudioPanel>;
}
'''
        name = "CapabilityPanel.tsx.in"
    elif surface == "skill":
        draft = '''---
name: REVIEW_SKILL_ID
description: REVIEW_OWNING_CAPABILITY
---
# Draft: not catalog registered
- Owner and public API:
- Target/dependencies:
- Minimal usage:
- Persistence and deterministic state:
- MCP/Studio integration:
- Verification command and evidence:
'''
        name = "SKILL.md.in"
    else:
        draft = "# Owner integration draft\n\nResolve the reviewed gap using the existing public owner API. Add no registration until implementation and evidence exist.\n"
        name = "integration.md"
    (folder / name).write_text(draft, encoding="utf-8")
    return folder

def export_assignments(root, report, registry, output):
    """Export deterministic per-agent proposals; consumers must accept ownership separately."""
    output.mkdir(parents=True, exist_ok=True)
    by_agent = defaultdict(list)
    for row in report["automation"]["schedule"]:
        if row["state"] == "proposed" and row["agent"]:
            by_agent[row["agent"]].append(task_context(root, report, registry, row["task"]))
    files = []
    for agent, tasks in sorted(by_agent.items()):
        path = output / (packet_name(agent) + ".json")
        write_new_json(path, {"schema": 1, "sha": report["sha"], "agent": agent,
                              "state": "proposed", "tasks": tasks})
        files.append(str(path))
    return {"assignments": files, "agents": len(by_agent), "dispatch": "external_acceptance_required"}

def main():
    from feature_registry import load_registry
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path, default=ROOT / "logs/feature-readiness.json")
    sub = parser.add_subparsers(dest="command", required=True)
    history = sub.add_parser("history")
    history.add_argument("feature")
    history.add_argument("--limit", type=int, default=30)
    history.add_argument("--capability", action="store_true")
    context = sub.add_parser("context")
    context.add_argument("task")
    draft = sub.add_parser("draft")
    draft.add_argument("task")
    draft.add_argument("--output", type=Path, required=True)
    assignments = sub.add_parser("assignments")
    assignments.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == "history":
            result = (coverage_history if args.capability else feature_history)(ROOT, args.feature, args.limit)
        else:
            report = json.loads(args.report.read_text(encoding="utf-8"))
            if report.get("sha") != git(ROOT, "rev-parse", "HEAD").strip():
                raise ValueError("report HEAD is stale")
            registry = load_registry(ROOT)
            if args.command == "context":
                result = task_context(ROOT, report, registry, args.task)
            elif args.command == "draft":
                result = {"draft": str(scaffold_gap(ROOT, report, registry, args.task, args.output))}
            else:
                result = export_assignments(ROOT, report, registry, args.output)
        print(json.dumps(result, indent=2, ensure_ascii=False))
        return 0
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as exc:
        print(f"Feature automation: {exc}", file=sys.stderr)
        return 1

if __name__ == "__main__":
    sys.exit(main())
