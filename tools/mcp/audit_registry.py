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
    "graphs": ("graph_catalog", "graph_list", "graph_get", "graph_node_catalog", "graph_validate", "graph_edit", "graph_save", "graph_diagnostics", "graph_dependencies", "graph_property_schema", "graph_asset_dependencies", "graph_runtime_capabilities", "graph_runtime_probe", "graph_live_snapshot", "graph_live_watch", "graph_live_profile", "graph_gpu_profile", "graph_debug_session", "texture_graph_catalog", "texture_graph_list", "texture_graph_get", "texture_graph_validate", "texture_graph_set", "texture_graph_preview", "texture_graph_bake", "texture_graph_templates", "texture_graph_bake_set", "texture_graph_cache", "texture_graph_material_variant", "texture_graph_gpu_preview"),
    "mcp": ("mcp_registry", "feature_readiness_get"),
}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary")
    parser.add_argument("--engine")
    parser.add_argument("--feature-manifest", action="append", type=Path, default=[])
    parser.add_argument("--feature-dir", type=Path, help="Audit every manifest in this directory")
    args = parser.parse_args()
    features = {}
    paths = list(args.feature_manifest)
    if args.feature_dir is not None:
        if not args.feature_dir.is_dir():
            parser.error("feature directory does not exist")
        discovered = sorted(args.feature_dir.glob("*.json"))
        if not discovered:
            parser.error("feature directory contains no manifests")
        paths.extend(discovered)
    for path in paths:
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
            ident = data["id"]
            if not isinstance(ident, str) or ident != path.stem:
                raise ValueError("feature id must match filename")
            names = data["integrations"]["mcp_tools"]
            if not isinstance(names, list) or not all(isinstance(n, str) and n.strip() for n in names) or len(set(names)) != len(names):
                raise ValueError("invalid or duplicate mcp_tools")
            status = data["checks"]["mcp"]["status"]
            if status not in ("pass", "fail", "not_run", "not_applicable"):
                raise ValueError("invalid MCP check status")
            if status == "pass" and not names:
                raise ValueError("MCP pass requires tool references")
            if ident in features:
                raise ValueError("duplicate feature id")
            features[ident] = (status, set(names))
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
    if done.returncode != 0:
        print("ERROR: MCP executable exited", done.returncode, done.stderr[-1000:], file=sys.stderr)
        return 2
    responses = {}
    for line in done.stdout.splitlines():
        try:
            msg = json.loads(line)
            if isinstance(msg, dict) and type(msg.get("id")) is int:
                if msg["id"] in responses:
                    print("ERROR: duplicate MCP response id", msg["id"], file=sys.stderr)
                    return 2
                responses[msg["id"]] = msg
        except ValueError:
            continue
    if not isinstance(responses.get(1, {}).get("result"), dict):
        print("ERROR: initialize failed", file=sys.stderr)
        return 2
    if not isinstance(responses.get(2, {}).get("result"), dict):
        print("ERROR: tools/list failed", done.stderr[-1000:], file=sys.stderr)
        return 2
    tools = responses[2]["result"].get("tools")
    if not isinstance(tools, list) or any(not isinstance(t, dict) or not isinstance(t.get("name"), str) or not t["name"] for t in tools):
        print("ERROR: malformed tools/list", file=sys.stderr)
        return 2
    listed = {t["name"] for t in tools}
    if len(listed) != len(tools) or responses[2]["result"].get("nextCursor"):
        print("ERROR: duplicate or incomplete tools/list", file=sys.stderr)
        return 2
    missing = {group: [n for n in names if n not in listed]
               for group, names in REQUIRED.items()}
    missing = {k:v for k,v in missing.items() if v}
    print(f"registered={len(listed)}, required_families={len(REQUIRED)}")
    for group, names in REQUIRED.items():
        print(f"{'FAIL' if group in missing else 'PASS'} {group}: {', '.join(names)}")
    # This is a real tools/call smoke check, not merely static source matching.
    response = responses.get(3, {})
    result = response.get("result")
    if not isinstance(result, dict) or result.get("isError") or "error" in response:
        print("ERROR: mcp_registry tools/call failed", file=sys.stderr)
        return 2
    content = result.get("content")
    if not isinstance(content, list):
        print("ERROR: malformed MCP content", file=sys.stderr)
        return 2
    payload = None
    for item in content:
        if isinstance(item, dict) and item.get("type") == "text":
            try:
                payload = json.loads(item["text"])
                break
            except (ValueError, KeyError, TypeError):
                pass
    if not isinstance(payload, dict) or not isinstance(payload.get("tools"), list):
        print("ERROR: mcp_registry tools/call did not return a registry object", file=sys.stderr)
        return 2
    if any(not isinstance(t, dict) or not isinstance(t.get("name"), str) or not t["name"] for t in payload["tools"]):
        print("ERROR: malformed registry entry", file=sys.stderr)
        return 2
    registry = {t["name"]:t for t in payload["tools"]}
    if len(registry) != len(payload["tools"]):
        print("ERROR: duplicate registry names", file=sys.stderr)
        return 2
    if registry.keys() != listed or payload.get("count") != len(listed):
        print("ERROR: tools/list and mcp_registry disagree", file=sys.stderr)
        return 2
    if any(not isinstance(t.get("guiOnly"), bool) for t in registry.values()):
        print("ERROR: missing guiOnly metadata", file=sys.stderr)
        return 2
    print("PASS tools/list == mcp_registry (count, names, guiOnly metadata)")
    for ident, (status, names) in sorted(features.items()):
        absent = sorted(names - listed)
        if absent:
            missing["feature:" + ident] = absent
        registration = ("missing" if absent else "registered") if names else (
            "not_applicable" if status == "not_applicable" else "undeclared")
        print("FEATURE", json.dumps({"id": ident, "declared_status": status,
                                     "registration": registration,
                                     "tools": sorted(names), "missing": absent,
                                     "runtime_verification": "not_run"}, sort_keys=True))
    if missing:
        print("MISSING:", json.dumps(missing, sort_keys=True))
    print("NOTE: authoring and GPU validation still need dedicated per-system tests.")
    return 1 if missing else 0

if __name__ == "__main__":
    sys.exit(main())
