"""Explicit feature policy: no inferred dependencies or invented API ownership."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
from collections import Counter
from check_feature import ROOT, DIMENSIONS, validate

POLICY = "docs/agents/feature-policy.json"

def read(root, path):
    return json.loads((root / path).read_text(encoding="utf-8"))

def repo_path(path):
    return isinstance(path, str) and bool(path) and path == Path(path).as_posix() and not Path(path).is_absolute() and ".." not in Path(path).parts and path != "."

def dependency_report(registry, policies, root):
    errors, unknown, edges = [], [], {}
    invalid_deps = set()
    for ident in sorted(registry):
        if ident not in policies:
            unknown.append(ident)
            edges[ident] = []
            continue
        deps = policies[ident].get("requires")
        if not isinstance(deps, list) or not all(isinstance(d, str) for d in deps) or len(set(deps)) != len(deps):
            errors.append(f"{ident}: invalid dependencies")
            invalid_deps.add(ident)
            deps = []
        edges[ident] = sorted(d for d in deps if d in registry)
        for dep in sorted(set(deps) - registry.keys()):
            errors.append(f"{ident}: unknown dependency {dep}")
            invalid_deps.add(ident)
    order, active, done, cyclic = [], [], set(), set()
    def visit(ident):
        if ident in active:
            cycle = active[active.index(ident):] + [ident]
            cyclic.update(cycle)
            errors.append("dependency cycle: " + " -> ".join(cycle))
            return
        if ident in done:
            return
        active.append(ident)
        for dep in edges[ident]:
            visit(dep)
        active.pop()
        done.add(ident)
        order.append(ident)
    for ident in sorted(edges):
        visit(ident)
    states = {}
    for ident in order:
        data = registry[ident]
        pending = [d for d in DIMENSIONS if data.get("checks", {}).get(d, {}).get("status") not in ("pass", "not_applicable")]
        invalid = validate(data, root)
        blocked = [d for d in edges[ident] if states.get(d, {}).get("state") != "declared_ready"]
        state = ("invalid" if invalid or ident in cyclic or ident in invalid_deps else
                 "unknown_dependencies" if ident in unknown else
                 "blocked" if blocked else "pending" if pending else "declared_ready")
        states[ident] = {"state": state, "requires": edges[ident], "blocked_by": blocked,
                         "pending": pending, "errors": invalid}
    return {"order": order, "features": states, "undeclared": unknown, "errors": errors}

def roadmap_report(registry, policies, systems):
    errors, unknown, rows = [], [], []
    by_system = {s["id"]: s for s in systems}
    for ident in sorted(registry):
        link = policies.get(ident, {}).get("roadmap")
        if link is None:
            unknown.append(ident)
            continue
        if not isinstance(link, dict) or set(link) != {"system", "id"} or not all(isinstance(v, str) for v in link.values()):
            errors.append(f"{ident}: invalid roadmap link")
            continue
        matches = [r for r in by_system.get(link["system"], {}).get("roadmap", []) if r.get("id") == link["id"]]
        if len(matches) != 1:
            errors.append(f"{ident}: roadmap link missing or ambiguous")
            continue
        rows.append({"feature": ident, "system": link["system"], "roadmap": link["id"], "status": matches[0].get("status")})
    counts = Counter((r["system"], r["roadmap"]) for r in rows)
    for key, count in sorted(counts.items()):
        if count > 1:
            errors.append("duplicate roadmap mapping: " + "/".join(key))
    return {"links": rows, "unmapped": unknown, "errors": errors}

def skills_report(root, skills):
    errors, candidates = [], []
    ids = Counter(s["id"] for s in skills)
    paths = Counter(s["path"] for s in skills)
    errors.extend(f"duplicate skill ID: {k}" for k,v in sorted(ids.items()) if v > 1)
    errors.extend(f"duplicate skill path: {k}" for k,v in sorted(paths.items()) if v > 1)
    sources = {}
    for s in skills:
        ident, rel = s["id"], s["path"]
        path = (root / rel).resolve()
        if not repo_path(rel) or not path.is_relative_to(root.resolve()) or not path.is_file():
            errors.append(f"{ident}: missing skill source file")
        elif not re.search(r"(?m)^name: " + re.escape(ident) + r"$", path.read_text(encoding="utf-8")):
            errors.append(f"{ident}: skill frontmatter mismatch")
        for dep in s.get("dependencies", []):
            if dep not in ids:
                errors.append(f"{ident}: unknown skill dependency {dep}")
        source = s.get("source")
        if isinstance(source, str) and source:
            sources.setdefault(source, []).append(ident)
            if repo_path(source) and not (root / source).exists():
                candidates.append({"skill": ident, "reason": "source path unavailable", "source": source})
    shared = [{"source": src, "skills": sorted(names)} for src,names in sorted(sources.items()) if len(names) > 1]
    return {"errors": errors, "review_candidates": candidates, "shared_sources": shared}

def api_report(policies, changed):
    errors, tracked, affected = [], {}, []
    for ident, policy in sorted(policies.items()):
        paths = policy.get("api_paths")
        if not isinstance(paths, list) or not all(repo_path(p) for p in paths) or len(set(paths)) != len(paths):
            errors.append(f"{ident}: invalid API paths")
            continue
        for path in paths:
            tracked.setdefault(path, []).append(ident)
    for path in sorted(set(changed) & tracked.keys()):
        for ident in sorted(tracked[path]):
            manifest = f"docs/features/{ident}.json"
            affected.append({"feature": ident, "path": path, "manifest_changed": manifest in changed,
                             "review": ["native_api", "mcp", "studio", "skill", "tests"]})
            if manifest not in changed:
                errors.append(f"{path}: API change requires updated {manifest}")
    return {"tracked_paths": tracked, "affected": affected, "errors": errors}

def ownership_report(claims):
    errors, conflicts, by_path, seen = [], [], {}, set()
    for claim in claims:
        if not isinstance(claim, dict) or set(claim) != {"agent", "branch", "paths"} or not isinstance(claim["agent"], str) or not claim["agent"].strip() or not isinstance(claim["branch"], str) or not claim["branch"].strip() or not isinstance(claim["paths"], list) or not all(repo_path(p) for p in claim["paths"]):
            errors.append("invalid ownership claim")
            continue
        key = (claim["agent"], claim["branch"])
        if key in seen:
            errors.append("duplicate ownership claim: " + "/".join(key))
        seen.add(key)
        for path in set(claim["paths"]):
            by_path.setdefault(path, set()).add(key)
    for path, owners in sorted(by_path.items()):
        if len(owners) > 1:
            conflicts.append({"path": path, "claims": [{"agent": a, "branch": b} for a,b in sorted(owners)]})
    return {"conflicts": conflicts, "errors": errors, "scope": "explicit active claims only; no remote worktree discovery"}

def build_report(root, registry, changed=()):
    data = read(root, POLICY)
    if not isinstance(data, dict) or set(data) != {"schema", "features", "claims"} or type(data["schema"]) is not int or data["schema"] != 1 or not isinstance(data["features"], dict) or not isinstance(data["claims"], list):
        raise ValueError("invalid feature policy")
    policies = data["features"]
    if any(not isinstance(v, dict) or set(v) != {"requires", "roadmap", "api_paths"} for v in policies.values()):
        raise ValueError("invalid feature policy entry")
    errors = [f"unknown feature policy: {key}" for key in sorted(policies.keys() - registry.keys())]
    dependencies = dependency_report(registry, policies, root)
    roadmap = roadmap_report(registry, policies, read(root, "docs/devlog/systems.json")["systems"])
    skills = skills_report(root, read(root, "skills/catalog.json")["skills"])
    api = api_report(policies, set(changed))
    ownership = ownership_report(data["claims"])
    for section in (dependencies, roadmap, skills, api, ownership):
        errors.extend(section["errors"])
    return {"dependencies": dependencies, "roadmap": roadmap, "skills": skills,
            "api": api, "ownership": ownership, "errors": errors}

def readiness_errors(root, registry, selected, changed=()):
    """Strict readiness for selected manifests and their transitive prerequisites."""
    report = build_report(root, registry, changed)
    errors = list(report["dependencies"]["errors"]) + list(report["api"]["errors"])
    roadmap = {row["feature"]: row for row in report["roadmap"]["links"]}
    errors.extend(report["roadmap"]["errors"])
    states = report["dependencies"]["features"]
    visited = set()
    def visit(ident):
        if ident in visited:
            return
        visited.add(ident)
        if ident not in states:
            errors.append(f"{ident}: missing feature")
            return
        state = states[ident]
        if state["state"] != "declared_ready":
            errors.append(f"{ident}: dependency readiness {state['state']}")
        if ident not in roadmap:
            errors.append(f"{ident}: missing roadmap mapping")
        errors.extend(f"{ident}: {error}" for error in
                      validate(registry[ident], root, ready=True, verify_git=True))
        for dep in state["requires"]:
            visit(dep)
    for ident in sorted(selected):
        visit(ident)
    return errors

def release_report(root, registry, policy, readiness):
    blockers = []
    for ident, state in sorted(policy["dependencies"]["features"].items()):
        if state["state"] != "declared_ready":
            blockers.append({"feature": ident, "reason": state["state"]})
        else:
            for error in validate(registry[ident], root, ready=True, verify_git=True):
                blockers.append({"feature": ident, "reason": error})
    blockers.extend({"feature": None, "reason": e} for e in policy["errors"] + readiness["errors"])
    blockers.extend({"feature": ident, "reason": "roadmap mapping unreviewed"} for ident in policy["roadmap"]["unmapped"])
    blockers.extend({"feature": None, "reason": "ownership conflict: " + c["path"]} for c in policy["ownership"]["conflicts"])
    gaps = [task["id"] for owner in readiness["owners"] for task in owner["tasks"] if task["kind"] == "implement_gap"]
    blockers.extend({"feature": None, "reason": "reviewed gap: " + gap} for gap in gaps)
    return {"scope": "repository checks only; independent CI, Mac merged-tree and release approval still required",
            "state": "blocked" if blockers else "requires_integrator_review", "blockers": blockers,
            "inventory_counts": readiness["counts"]}

def main():
    from feature_registry import load_registry, owner_readiness
    from capability_inventory import inventory as capabilities
    from graph_inventory import inventory as graphs
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base")
    parser.add_argument("--output", type=Path, help="Optional report file; use ignored build output")
    parser.add_argument("--require-release-ready", action="store_true", help="Exit nonzero when release has blockers")
    args = parser.parse_args()
    try:
        registry = load_registry(ROOT)
        changed = []
        if args.base:
            base = subprocess.check_output(["git", "rev-parse", "--verify", "--end-of-options", args.base + "^{commit}"], cwd=ROOT, text=True).strip()
            changed = subprocess.check_output(["git", "diff", "--name-only", "--no-renames", "-z", base + "...HEAD"], cwd=ROOT).decode().split("\0")
        policy = build_report(ROOT, registry, changed)
        readiness = owner_readiness(registry, ROOT, capabilities(ROOT), graphs(ROOT))
        sha = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        report = {"schema": 1, "sha": sha, "policy": policy, "readiness": readiness,
                  "release": release_report(ROOT, registry, policy, readiness)}
        output = json.dumps(report, ensure_ascii=False, indent=2) + "\n"
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(output, encoding="utf-8")
        else:
            print(output, end="")
        return int(bool(policy["errors"] or readiness["errors"] or policy["ownership"]["conflicts"] or
                        (args.require_release_ready and report["release"]["blockers"])))
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as exc:
        print(f"Feature policy error: {exc}", file=sys.stderr)
        return 1

if __name__ == "__main__":
    sys.exit(main())
