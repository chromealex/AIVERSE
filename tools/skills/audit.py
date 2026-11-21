#!/usr/bin/env python3
"""Audit repository skills against the code: every referenced path exists and every MCP tool name is real.

  python3 tools/skills/audit.py [--skills id,id] [--strict]

Scans skills/<category>/<id>/SKILL.md. In backticks it checks
  - repository paths (`modules/rts/RtsAI.cpp`, `docs/x.md`, `skills/ai/...`, `tools/...`; globs like `Rts*.cpp` must
    match something; placeholders with <...> are skipped);
  - MCP tool names: a backticked word that starts like a tool (`map_`, `rules_`, ...) must be registered by the
    editor or MCP server (`add("name"` / `registerOp({"name"` in apps/ and tools/mcp).
Missing paths are errors; unknown tool names are errors with --strict, warnings otherwise. Exit 1 on errors.
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOP = ('apps', 'engine', 'modules', 'tools', 'docs', 'libraries', 'tests', 'templates', 'skills', 'examples',
       'platform', 'plugins', 'third_party', 'games')
ROOT_FILES = ('AGENTS.md', 'README.md', 'CMakeLists.txt', 'CLAUDE.md', 'CMakePresets.json')
TICK = re.compile(r'`([^`\n]+)`')
REG = re.compile(r'(?:registerOp\(\{|\badd\(|ed\.ws\(\)\.registerOp\(\{)"([a-z][a-z0-9_]+)"')
# snake_case words that are CMake targets or test names, not MCP tools
NOT_TOOLS = {'game_meta', 'game_meta_rpc', 'game_client', 'road_network_geometry_and_decks', 'traffic_drives_spline_roads', 'animation_assets', 'animation_budget_policy', 'animation_ecs_presentation', 'animation_generic_retarget', 'animation_graphs', 'animation_inertialization', 'animation_montage', 'animation_motion_warp', 'animation_physical_pose', 'animation_playback'}
TOOL = re.compile(r'^([a-z][a-z0-9]*(?:_[a-z0-9]+)+)(?:\s*\{.*\})?$')


def mcp_tools(root=ROOT):
    names = set()
    for base in (root / 'apps', root / 'tools' / 'mcp'):
        for p in base.rglob('*.cpp'):
            names.update(REG.findall(p.read_text(encoding='utf-8', errors='replace')))
    return names


def path_candidates(token):
    token = token.strip().split()[0].rstrip('.,;:)')
    token = re.sub(r':\d+$', '', token)
    if token.startswith('./'):
        token = token[2:]
    if any(c in token for c in '<>$') or token.startswith(('-', 'http')):
        return None
    head = token.split('/')[0]
    if token in ROOT_FILES or (head in TOP and '/' in token):
        return token
    return None


def exists(root, token):
    t = token.rstrip('/')
    if '{' in t:   # brace list: a/{B,C}.h
        m = re.match(r'(.*)\{([^}]*)\}(.*)', t)
        return all(exists(root, m.group(1) + part + m.group(3)) for part in m.group(2).split(','))
    if any(c in t for c in '*?['):
        return any(root.glob(t))
    return (root / t).exists()


def audit(root=ROOT, only=None, strict=False):
    tools = mcp_tools(root)
    prefixes = {n.split('_')[0] for n in tools}
    # CTest identifiers in validation instructions are not MCP operations.
    cmake = (root / 'CMakeLists.txt').read_text(encoding='utf-8')
    test_names = set(re.findall(r'add_test\s*\(\s*NAME\s+([a-z][a-z0-9_]+)', cmake))
    errors, warnings = [], []
    for skill in sorted((root / 'skills').glob('*/*/SKILL.md')):
        sid = skill.parent.name
        if only and sid not in only:
            continue
        rel = skill.relative_to(root).as_posix()
        for token in sorted(set(TICK.findall(skill.read_text(encoding='utf-8')))):
            p = path_candidates(token)
            if p is not None:
                if not exists(root, p):
                    errors.append(f'{rel}: missing path `{p}`')
                continue
            m = TOOL.match(token.strip())
            if m and m.group(1).split('_')[0] in prefixes and m.group(1) not in tools and m.group(1) not in NOT_TOOLS and m.group(1) not in test_names:
                (errors if strict else warnings).append(f'{rel}: unknown MCP tool `{m.group(1)}`')
    return errors, warnings


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--skills', help='comma-separated skill ids')
    ap.add_argument('--strict', action='store_true')
    a = ap.parse_args(argv)
    only = {s.strip() for s in a.skills.split(',')} if a.skills else None
    errors, warnings = audit(ROOT, only, a.strict)
    for w in warnings:
        print('warning:', w)
    for e in errors:
        print('error:', e)
    print(f'{len(errors)} error(s), {len(warnings)} warning(s)')
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
