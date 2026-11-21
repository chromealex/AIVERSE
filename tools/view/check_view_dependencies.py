#!/usr/bin/env python3
"""Guard the canonical AIVERSE View against genre/game dependencies."""
from __future__ import annotations

import pathlib
import re
import sys

INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^">]+)[">]', re.MULTILINE)
FORBIDDEN = re.compile(
    r'(^|/)(?:modules|apps|examples|projects)/|'
    r'(^|/)(?:fps|rts|rpg|genre)/', re.IGNORECASE
)
GAME_NAMESPACE = re.compile(
    r'\b(?:aiverse::)?(?:fps|rts|game)::|'
    r'\b(?:FpsModule|RtsModule|RtsScene|RtsView|OrderType)\b'
)


def check(root: pathlib.Path) -> list[str]:
    results: list[str] = []
    for path in sorted((root / "engine/view").rglob("*")):
        if path.suffix not in {".h", ".cpp"}:
            continue
        content = path.read_text("utf-8")
        relative = path.relative_to(root)
        for match in INCLUDE.finditer(content):
            if FORBIDDEN.search(match.group(1).replace("\\", "/")):
                results.append(
                    f"{relative}: forbidden include {match.group(1)}"
                )
        # Strip comments and string literals before scanning for typed
        # references. "RtsView" in a migration comment is not a dependency.
        stripped = re.sub(
            r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"',
            "", content, flags=re.DOTALL,
        )
        for match in GAME_NAMESPACE.finditer(stripped):
            results.append(
                f"{relative}: genre type reference {match.group(0)}"
            )
    return results


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("usage: check_view_dependencies.py REPOSITORY_ROOT", file=sys.stderr)
        raise SystemExit(2)
    root = pathlib.Path(sys.argv[1]).resolve()
    if not (root / "engine/view/View.h").is_file():
        print("View public header not found", file=sys.stderr)
        raise SystemExit(2)
    violations = check(root)
    if violations:
        print("\n".join(violations), file=sys.stderr)
        raise SystemExit(1)
    print("AIVERSE View has no genre dependencies")
