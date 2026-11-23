#!/usr/bin/env python3
"""Audit the actual built AIVERSE MCP tools/list, not just source-code mentions.

Usage: python3 tools/mcp/audit_registry.py /path/to/aiversemcp [--engine /path/to/AIVERSE]
This checks registration and the mcp_registry tool; it is not a GPU/runtime parity test.
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path

# Stable, already-implemented families; expand this as new universal authoring areas land.
REQUIRED = {
    "core": ("open_game", "map_validate"),
    "scene": ("scene_entity_list", "scene_component_set"),
    "camera": ("lens_scene_get", "lens_camera_set"),
    "animation": ("animation_graph_list", "animation_graph_set"),
    "terrain": ("terrain_sculpt", "terrain_material"),
    "lighting": ("light_list", "scene_light_create"),
    "physis": ("physis_get", "physis_set"),
    "spectrafx": ("fx_graph_list", "fx_graph_set"),
    "postfx": ("postfx_get",),
    "geometry": ("geometry_catalog", "strata_capabilities"),
    "human": ("human_schema", "human_create"),
    "graphs": ("graph_catalog", "graph_list", "graph_get", "graph_node_catalog", "graph_validate", "graph_edit", "graph_save", "graph_diagnostics", "graph_dependencies", "graph_property_schema", "graph_asset_dependencies", "graph_runtime_capabilities", "graph_runtime_probe", "graph_live_snapshot", "graph_live_watch", "graph_live_profile", "graph_debug_session"),
    "mcp": ("mcp_registry",),
}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary")
    parser.add_argument("--engine")
    parser.add_argument("--feature-manifest", action="append", type=Path, default=[])
    args = parser.parse_args()
    feature_tools = set()
    for path in args.feature_manifest:
        try:
            names = json.loads(path.read_text(encoding="utf-8"))["integrations"]["mcp_tools"]
            if not isinstance(names, list) or not all(isinstance(n, str) and n for n in names):
                raise ValueError("invalid mcp_tools")
            feature_tools.update(names)
        except (OSError, ValueError, KeyError, TypeError) as exc:
            parser.error(f"{path}: {exc}")
    cmd = [args.binary, "--no-editor", "--port", "0"]
    if args.engine:
        cmd += ["--engine", args.engine]
    requests = [
        {"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"aiverse-audit","version":"1"}}},
        {"jsonrpc":"2.0","method":"notifications/initialized","params":{}},
        {"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}},
        {"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"mcp_registry","arguments":{}}},
    ]
    try:
        done = subprocess.run(cmd, input="\n".join(map(json.dumps, requests))+"\n",
                              text=True, capture_output=True, timeout=30, check=False)
    except (OSError, subprocess.TimeoutExpired) as exc:
        print("ERROR: MCP executable failed:", exc, file=sys.stderr)
        return 2
    responses = {}
    for line in done.stdout.splitlines():
        try:
            msg = json.loads(line)
            if "id" in msg:
                responses[msg["id"]] = msg
        except ValueError:
            continue
    if 2 not in responses or "result" not in responses[2]:
        print("ERROR: tools/list failed", done.stderr[-1000:], file=sys.stderr)
        return 2
    listed = {t["name"] for t in responses[2]["result"].get("tools", [])}
    missing = {group: [n for n in names if n not in listed]
               for group, names in REQUIRED.items()}
    missing = {k:v for k,v in missing.items() if v}
    print(f"registered={len(listed)}, required_families={len(REQUIRED)}")
    for group, names in REQUIRED.items():
        print(f"{'FAIL' if group in missing else 'PASS'} {group}: {', '.join(names)}")
    if feature_tools - listed:
        missing["feature_manifest"] = sorted(feature_tools - listed)
    if missing:
        print("MISSING:", json.dumps(missing, sort_keys=True))
    # This is a real tools/call smoke check, not merely static source matching.
    response = responses.get(3, {})
    content = response.get("result", {}).get("content", [])
    payload = None
    for item in content:
        if item.get("type") == "text":
            try:
                payload = json.loads(item["text"])
                break
            except (ValueError, KeyError):
                pass
    if not isinstance(payload, dict) or not isinstance(payload.get("tools"), list):
        print("ERROR: mcp_registry tools/call did not return a registry object", file=sys.stderr)
        return 2
    registry = {t["name"]:t for t in payload["tools"]}
    if registry.keys() != listed or payload.get("count") != len(listed):
        print("ERROR: tools/list and mcp_registry disagree", file=sys.stderr)
        return 2
    if any(not isinstance(t.get("guiOnly"), bool) for t in registry.values()):
        print("ERROR: missing guiOnly metadata", file=sys.stderr)
        return 2
    print("PASS tools/list == mcp_registry (count, names, guiOnly metadata)")
    print("NOTE: authoring and GPU validation still need dedicated per-system tests.")
    return 1 if missing else 0

if __name__ == "__main__":
    sys.exit(main())
