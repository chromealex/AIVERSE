#!/usr/bin/env python3
"""Architecture guard: animation core must not depend on any game genre.

Checks preprocessor include edges and explicitly typed genre namespaces.
The FPS/RTS adapters MUST live under their own modules, not the core.
This is source-level validation, not a substitute for compilation or tests.
"""
from __future__ import annotations

import pathlib
import re
import sys

INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^">]+)[">]', re.MULTILINE)
# Exclude ordinary string literals, comments, names of animation functions.
FORBIDDEN_INCLUDE = re.compile(
    r'(^|/)(?:modules|apps|examples|projects)/|'
    r'(^|/)(?:fps|rts|rpg|genre)/', re.IGNORECASE
)
FORBIDDEN_TYPED_REFERENCE = re.compile(
    r'\b(?:aiverse::)?(?:fps|rts|game)::|'
    r'\b(?:FpsModule|GameCore|OrderType|FpsActor)\b'
)


def relevant_files(root: pathlib.Path) -> list[pathlib.Path]:
    # Public MotionWeave core and low-level skeletal graph sources.
    targets: list[pathlib.Path] = []
    for directory in (
        root / "libraries/include/aiverselib/animation",
        root / "engine/animation",  # core moved here 2026-11-21 (docs/architecture-layers.md)
    ):
        if directory.is_dir():
            targets.extend(directory.rglob("*.h"))
            targets.extend(directory.rglob("*.cpp"))
    render = root / "engine/render"
    targets.extend(render.glob("Animation*.h"))
    targets.extend(render.glob("Animation*.cpp"))
    return sorted(set(targets))


def check(root: pathlib.Path) -> list[str]:
    problems: list[str] = []
    for file in relevant_files(root):
        data = file.read_text(encoding="utf-8")
        relative = file.relative_to(root)
        for match in INCLUDE.finditer(data):
            header = match.group(1).replace("\\", "/")
            if FORBIDDEN_INCLUDE.search(header):
                line = data.count("\n", 0, match.start()) + 1
                problems.append(f"{relative}:{line}: forbidden include {header}")
        # Approximate typed references only after stripping C++ comments.
        scrubbed = re.sub(r'//[^\n]*|/\*.*?\*/', "", data, flags=re.DOTALL)
        for match in FORBIDDEN_TYPED_REFERENCE.finditer(scrubbed):
            line = scrubbed.count("\n", 0, match.start()) + 1
            problems.append(
                f"{relative}:{line}: genre type reference {match.group(0)}"
            )
    return problems


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print("usage: check_motionweave_dependencies.py REPO_ROOT", file=sys.stderr)
        return 2
    root = pathlib.Path(argv[1]).resolve()
    if not (root / "engine/animation/MotionWeave.h").is_file():
        print("MotionWeave public header not found", file=sys.stderr)
        return 2
    problems = check(root)
    if problems:
        print("\n".join(problems), file=sys.stderr)
        return 1
    print("MotionWeave: no genre dependencies in animation core")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
