#!/usr/bin/env python3
"""Report remaining legacy editor ImGui usage and enforce the new Studio boundary.

Pure standard-library tool, safe to run on every platform. Its counts measure
migration surface, NOT feature parity or editor completeness.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import re

IMGUI_CALL = re.compile(r"\bImGui::")
IMGUI_INCLUDE = re.compile(r'^\s*#\s*include\s*[<"][^>"]*imgui[^>"]*[>"]', re.I | re.M)
IMGUI_TYPE = re.compile(r"\b(ImGuiWindowFlags|ImVec2|ImVec4|ImDrawList|ImTextureID)\b")
SOURCE_EXT = {".cpp", ".h", ".hpp", ".mm"}


def scan(root: Path) -> dict:
    legacy = root / "apps" / "editor"
    native = root / "apps" / "studio-native"
    ui = root / "apps" / "studio-web" / "src"
    windows = sorted(p for p in (legacy / "windows").glob("*.cpp") if ".bak" not in p.name)
    legacy_sources = sorted(p for p in legacy.rglob("*") if p.is_file() and p.suffix in SOURCE_EXT and ".bak" not in p.name)
    violating_legacy = []
    total_calls = 0
    for path in legacy_sources:
        text = path.read_text(encoding="utf-8", errors="replace")
        calls = len(IMGUI_CALL.findall(text))
        total_calls += calls
        if calls or IMGUI_INCLUDE.search(text) or IMGUI_TYPE.search(text):
            violating_legacy.append(path.relative_to(root).as_posix())
    violations = []
    for path in sorted(native.rglob("*")):
        if not path.is_file() or path.suffix not in SOURCE_EXT:
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        if IMGUI_CALL.search(text) or IMGUI_INCLUDE.search(text) or IMGUI_TYPE.search(text):
            violations.append(path.relative_to(root).as_posix())
    panels = sorted(p.relative_to(root).as_posix() for p in (ui / "components").glob("*.tsx"))
    return {
        "legacy_window_cpp_files": len(windows),
        "legacy_imgui_source_files": len(violating_legacy),
        "legacy_imgui_calls": total_calls,
        "legacy_imgui_paths": violating_legacy,
        "new_react_components": panels,
        "new_native_imgui_violations": violations,
        "ready_to_remove_editor_imgui": not violating_legacy and not violations,
        "note": "Counts are static source metrics, not an indication of rendering or feature parity.",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--strict", action="store_true", help="Fail if new native Studio depends on ImGui")
    args = parser.parse_args()
    result = scan(args.root.resolve())
    if args.json:
        print(json.dumps(result, ensure_ascii=False, indent=2))
    else:
        for key in ("legacy_window_cpp_files", "legacy_imgui_source_files", "legacy_imgui_calls"):
            print(f"{key}: {result[key]}")
        print(f"new_react_component_count: {len(result['new_react_components'])}")
        print(f"new_native_imgui_violations: {', '.join(result['new_native_imgui_violations']) or 'none'}")
        print(result["note"])
    return 1 if args.strict and result["new_native_imgui_violations"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
