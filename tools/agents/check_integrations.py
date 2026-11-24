"""Validate feature integration references; static Studio checks are not native QA."""
import json
import re
from pathlib import Path

def strings(value):
    return isinstance(value, list) and all(isinstance(x, str) and x.strip() for x in value) and len(set(value)) == len(value)

def validate_integrations(data, root, ready=False):
    links = data.get("integrations")
    keys = {"skills", "mcp_tools", "studio_panels", "studio_methods"}
    if not isinstance(links, dict) or set(links) != keys or not all(strings(v) for v in links.values()):
        return ["integrations must contain unique string lists: " + ", ".join(sorted(keys))]
    errors = []
    checks = data.get("checks")
    if not isinstance(checks, dict):
        return errors
    for dimension, key in (("skill", "skills"), ("mcp", "mcp_tools"), ("studio", "studio_panels"), ("studio", "studio_methods")):
        check = checks.get(dimension)
        if isinstance(check, dict) and check.get("status") == "pass" and not links[key]:
            errors.append(f"{dimension}: pass requires {key}")
    if any(not re.fullmatch(r"[a-zA-Z0-9_]+", n) for n in links["mcp_tools"]):
        errors.append("invalid MCP tool name")
    try:
        if links["skills"]:
            catalog = json.loads((root / "skills/catalog.json").read_text(encoding="utf-8"))
            by_id = {entry["id"]: entry for entry in catalog["skills"]}
            for skill in links["skills"]:
                entry = by_id.get(skill)
                if not entry:
                    errors.append(f"unknown skill: {skill}")
                    continue
                path = (root / entry["path"]).resolve()
                if not path.is_relative_to(root.resolve()) or not path.is_file():
                    errors.append(f"missing skill source: {skill}")
                elif not re.search(r"(?m)^name: " + re.escape(skill) + r"$", path.read_text(encoding="utf-8")):
                    errors.append(f"skill frontmatter mismatch: {skill}")
        if links["studio_panels"]:
            dock = (root / "apps/studio-web/src/nestedDock.ts").read_text(encoding="utf-8")
            renderer = (root / "apps/studio-web/src/panels/StudioPanelRenderer.tsx").read_text(encoding="utf-8")
            match = re.search(r"STUDIO_PANELS\s*=\s*\[(.*?)\]\s*as const", dock, re.S)
            panels = set(re.findall(r"['\"]([^'\"]+)['\"]", match.group(1))) if match else set()
            rendered = set(re.findall(r"\bcase\s+['\"]([^'\"]+)['\"]\s*:", renderer))
            for panel in links["studio_panels"]:
                if panel not in panels or panel not in rendered:
                    errors.append(f"Studio panel missing registration or render case: {panel}")
        if links["studio_methods"]:
            types = (root / "apps/studio-web/src/types.ts").read_text(encoding="utf-8")
            match = re.search(r"export interface StudioParams\s*\{(.*?)^\}", types, re.S | re.M)
            methods = set(re.findall(r"(?m)^\s*['\"](studio\.[^'\"]+)['\"]\s*:", match.group(1))) if match else set()
            for method in links["studio_methods"]:
                if method not in methods:
                    errors.append(f"unknown typed Studio method: {method}")
    except (OSError, ValueError, KeyError, TypeError) as exc:
        errors.append(f"integration source read failed: {exc}")
    return errors
