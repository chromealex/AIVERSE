#!/usr/bin/env python3
"""Safely scaffold a typed, genre-neutral HTML5 Studio panel for AI agents."""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import re
import tempfile

NAME = re.compile(r"[a-z][a-z0-9_]{1,31}\Z")
AREA = {"left": "dock-left", "center": "dock-center", "right": "dock-right", "bottom": "dock-bottom"}


def _insert_record(source: str, name: str, key: str, value: str) -> str:
    marker = f"export const {name}: Record<StudioPanel,string> = {{"
    start = source.find(marker)
    if start < 0:
        raise ValueError(f"Catalog anchor missing: {name}")
    stop = source.find("\n};", start)
    if stop < 0:
        raise ValueError(f"Catalog end missing: {name}")
    before = source[:stop].rstrip()
    if not before.endswith(","):
        before += ","
    return before + f"\n  {key}: {json.dumps(value, ensure_ascii=False)}" + source[stop:]


def _atomic(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(dir=path.parent, prefix=".aiverse-")
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as output:
            output.write(content)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def generate(root: Path, panel_id: str, title: str, area: str, dry_run: bool = False) -> list[Path]:
    """Build the full change plan before any filesystem mutation."""
    root = root.resolve()
    if not NAME.fullmatch(panel_id):
        raise ValueError("Panel ID must be 2–32 lowercase ASCII characters, digits or underscores, starting with a letter")
    if not title.strip() or len(title) > 80 or any(ord(ch) < 32 for ch in title):
        raise ValueError("Title must be nonempty printable text up to 80 characters")
    if area not in AREA:
        raise ValueError("Unknown dock area")

    src = root / "apps/studio-web/src"
    catalog = src / "nestedDock.ts"
    registry = src / "panels/StudioPanelRenderer.tsx"
    model = catalog.read_text(encoding="utf-8")
    renderer = registry.read_text(encoding="utf-8")
    names = re.search(r"export const STUDIO_PANELS = \[([^\]]*)\] as const;", model, re.S)
    if not names:
        raise ValueError("Typed Studio panel catalog declaration not found")
    existing = re.findall(r"['\"]([a-z][a-z0-9_]*)['\"]", names.group(1))
    if panel_id in existing:
        raise ValueError("Panel already registered: " + panel_id)
    if "// AI_PANEL_CASES" not in renderer:
        raise ValueError("Renderer insertion marker missing")

    component_name = "".join(s.capitalize() for s in panel_id.split("_")) + "Panel"
    component = src / "panels/generated" / (component_name + ".tsx")
    css = src / "panels/generated" / (component_name + ".css")
    if component.exists() or css.exists():
        raise ValueError("Generated component filename already exists")

    raw = names.group(1).rstrip()
    updated = raw + ("," if raw and not raw.endswith(",") else "") + f"'{panel_id}'"
    model = model[:names.start(1)] + updated + model[names.end(1):]
    model = _insert_record(model, "STUDIO_PANEL_TITLES", panel_id, title)
    model = _insert_record(model, "STUDIO_PANEL_HOME", panel_id, AREA[area])
    renderer = renderer.replace("export interface StudioPanelContext {",
        f"import {component_name} from './generated/{component_name}';\n\nexport interface StudioPanelContext {{", 1)
    renderer = renderer.replace("      // AI_PANEL_CASES",
        f"      case '{panel_id}': return <{component_name} snapshot={{snapshot}}/>;\n      // AI_PANEL_CASES", 1)

    title_js = json.dumps(title, ensure_ascii=False)
    class_name = "studio-panel-" + panel_id
    component_code = f'''import type {{ StudioSnapshot }} from '../../types';
import {{ Layers3 }} from 'lucide-react';
import {{
  StudioPanel, StudioSection, StudioBadge, StudioAlert, StudioEmptyState
}} from '../../ui';
import './{component_name}.css';

/** Generated with AIVERSE Studio UI v1. Never touch game/project files from React. */
export default function {component_name}({{ snapshot }}: {{ snapshot: StudioSnapshot | null }}) {{
  const title = {title_js};
  const projectLoaded = Boolean(snapshot?.project.path);
  return <StudioPanel className="{class_name}" title={{title}}
    eyebrow="TOOLS / CUSTOM"
    subtitle={{snapshot?.project.name ?? 'No project open'}}
    actions={{<StudioBadge tone={{projectLoaded ? 'success' : 'neutral'}}>
      {{projectLoaded ? 'Project loaded' : 'No project'}}
    </StudioBadge>}}>
    <div className="au-stack">
      <StudioSection title="Overview">
        <StudioAlert title="Native bridge integration">
          Read data using typed src/bridge.ts methods. Implement any write
          through allowlisted C++ StudioSession and the engine Workspace.
        </StudioAlert>
      </StudioSection>
      <StudioEmptyState
        icon={{<Layers3 size={{22}}/>}}
        title="Ready for this panel"
        description="Build this screen with Studio UI components and semantic tokens.
        Loading, empty, error and disabled states belong to each feature."
      />
    </div>
  </StudioPanel>;
}}
'''
    # Use only scoped layout overrides. Appearance stays in shared UI tokens.
    css_code = f'''.{class_name} .au-empty{{min-height:240px}}
'''
    plan = {catalog: model, registry: renderer, component: component_code, css: css_code}
    if dry_run:
        return list(plan)
    originals = {path: path.read_text(encoding="utf-8") if path.exists() else None for path in plan}
    written: list[Path] = []
    try:
        # New components are written before the catalog announces their existence.
        for path in (component, css, catalog, registry):
            _atomic(path, plan[path])
            written.append(path)
    except Exception:
        for path in reversed(written):
            original = originals[path]
            if original is None:
                path.unlink(missing_ok=True)
            else:
                _atomic(path, original)
        raise
    return list(plan)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--id', required=True)
    parser.add_argument('--title', required=True)
    parser.add_argument('--area', choices=list(AREA), default='right')
    parser.add_argument('--dry-run', action='store_true')
    args = parser.parse_args()
    try:
        changed = generate(args.root, args.id, args.title, args.area, args.dry_run)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    for path in changed:
        print(('Would modify/create: ' if args.dry_run else 'Created/updated: ') + str(path))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
