#!/usr/bin/env python3
"""Layer check: a source file may include only its own layer or a lower one.

    engine/  <  libraries/  <  modules/<name>/  <  apps/, tools/, tests/

- engine/ and libraries/ never include modules/ or apps/ (no genre host under shared code);
- one module never includes another module (modules/strategic -> modules/rts is debt);
- modules/ never include apps/.

Known debt is listed in tools/layers/allowlist.json as "<file> -> <included file>" with the
debt id from docs/no-genre-split-audit.md (G1-G9) or docs/architecture-layers.md (L1-...).
New edges fail. Allowlisted edges that no longer exist also fail, so the list only shrinks.

The build is checked too: an engine target (a library whose sources are all under engine/) never links a library
package (AIVERSE::lib_*, AIVERSE::libraries, ...) and never adds libraries/include to its include path; no allowlist.

Usage: tools/layers/check_layers.py [--update]   (--update rewrites the allowlist with the current edges, keeping ids)"""
import json, re, sys
from pathlib import Path

root = Path(__file__).resolve().parents[2]
allow_path = root / "tools/layers/allowlist.json"
INC = re.compile(r'^\s*#\s*include\s*["<]([^">]+)[">]', re.M)
EXT = {".h", ".hpp", ".cpp", ".cc", ".mm", ".m", ".inc"}
SEARCH = [root, root / "libraries/include", root / "engine", root / "modules"]


def layer(rel):
    p = rel.parts
    if p[0] == "engine":
        return (0, "engine")
    if p[0] == "libraries":
        return (1, "libraries")
    if p[0] == "modules" and len(p) > 1:
        return (2, "modules/" + p[1])
    if p[0] in ("apps", "tools", "tests", "examples", "platform", "plugins"):
        return (3, p[0])
    return None


def resolve(src, inc):
    for base in [src.parent] + SEARCH:
        cand = (base / inc).resolve()
        if cand.is_file():
            try:
                return cand.relative_to(root)
            except ValueError:
                return None
    return None


def edges():
    out = set()
    for top in ("engine", "libraries", "modules"):
        for f in (root / top).rglob("*"):
            if f.suffix not in EXT or not f.is_file() or "third_party" in f.parts or "node_modules" in f.parts:
                continue
            rel = f.relative_to(root)
            a = layer(rel)
            for inc in INC.findall(f.read_text(encoding="utf-8", errors="replace")):
                t = resolve(f, inc)
                if t is None:
                    continue
                b = layer(t)
                if a is None or b is None:
                    continue
                bad = b[0] > a[0] or (a[0] == b[0] == 2 and a[1] != b[1])
                if bad:
                    out.add(f"{rel.as_posix()} -> {t.as_posix()}")
    return out


def debt_id(edge):
    src, dst = edge.split(" -> ")
    if "Kingston" in dst or "/kingston/" in dst or "RtsBwAI" in dst:
        return "G3"
    if dst.startswith("modules/fps/"):
        return "G4"
    if dst.startswith("modules/rts/"):
        return "G3"
    if src.startswith("engine/") and dst.startswith("libraries/"):
        return "L1"
    return "L0"


LIB_TARGET = re.compile(r"AIVERSE::(lib_\w+|libraries|gameplay_native|mechanics|genre|strategy|navmesh|session|aiverse_bots|fps_bots|"
                        r"animation_ecs|MotionWeave|motionweave|InputFlow)\b|\baiverse_(lib_\w+|libraries|gameplay_native|mechanics_library|"
                        r"strategy_library|navmesh_library|session_library|animation_ecs|motionweave)\b")


def cmake_violations():
    """Engine targets that link a library package or include libraries/include (CMakeLists.txt, engine/**/CMakeLists.txt)."""
    files = [root / "CMakeLists.txt"] + sorted((root / "engine").rglob("CMakeLists.txt"))
    sources, calls = {}, []
    for f in files:
        text = f.read_text(encoding="utf-8", errors="ignore")
        base = f.parent.relative_to(root)
        for m in re.finditer(r"\b(add_library|target_sources)\((\w+)([^)]*)\)", text):
            for src in re.findall(r"[\w./${}-]+\.(?:cpp|cc|mm|c)\b", m.group(3)):
                src = src.replace("${PROJECT_SOURCE_DIR}/", "").replace("${CMAKE_CURRENT_SOURCE_DIR}/", "")
                path = (base / src) if not str(base) == "." else Path(src)
                sources.setdefault(m.group(2), []).append(Path(*[x for x in path.parts]).as_posix())
        for m in re.finditer(r"\btarget_(link_libraries|include_directories)\((\w+)([^)]*)\)", text, re.S):
            calls.append((f.relative_to(root).as_posix(), m.group(1), m.group(2), m.group(3)))
    def norm(p):
        parts = []
        for x in Path(p).parts:
            if x == "..":
                parts and parts.pop()
            else:
                parts.append(x)
        return "/".join(parts)
    engine_targets = {t for t, s in sources.items() if s and all(norm(x).startswith("engine/") for x in s)}
    engine_targets |= {"aiverse_engine", "aiverse_game", "aiverse_world", "aiverse_fps", "aiverse_vehicles", "aiverse_view",
                       "aiverse_motionweave_core", "aiverse_net", "aiverse_authority"}   # also INTERFACE and generated-source targets
    out = []
    for f, kind, target, body in calls:
        if target not in engine_targets:
            continue
        if kind == "link_libraries" and LIB_TARGET.search(body):
            out.append(f"{f}: {target} links {LIB_TARGET.search(body).group(0)}")
        if kind == "include_directories" and "libraries/include" in body:
            out.append(f"{f}: {target} includes libraries/include")
    return out


current = edges()
allow = json.loads(allow_path.read_text(encoding="utf-8")) if allow_path.exists() else {}
if "--update" in sys.argv:
    data = {e: allow.get(e, debt_id(e)) for e in sorted(current)}
    allow_path.write_text(json.dumps(data, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"layers allowlist: {len(data)} edges written")
    sys.exit(0)

new = sorted(current - allow.keys())
gone = sorted(allow.keys() - current)
for e in new:
    print("NEW layer violation:", e)
for e in gone:
    print("FIXED, remove from allowlist:", e)
summary = {}
for e in current:
    summary[allow.get(e, "new")] = summary.get(allow.get(e, "new"), 0) + 1
print("layer edges by debt id:", dict(sorted(summary.items())))
build = cmake_violations()
for v in build:
    print("ENGINE TARGET DEPENDS ON LIBRARIES:", v)
if build:
    print("Engine targets never link library packages (docs/architecture-layers.md): move the code into engine/.")
    sys.exit(1)
if new or gone:
    print("See docs/architecture-layers.md. Fix the include, or (debt only) run tools/layers/check_layers.py --update.")
    sys.exit(1)
print("layers: OK")
