#!/usr/bin/env python3
"""Install the official template packages of the catalog into one folder, named by their engine id.

    python3 tools/pkg/fetch_templates.py [--out DIR] [--only skirmish,mushrooms]

DIR defaults to $AIVERSE_TEMPLATES_DIR or ~/.aiverse/templates. Each package that declares
provides.engineTemplate goes to DIR/<engineTemplate>/ (the layout tests and CI used to read from
the engine's templates/ folder). Packages already present at the same version are left alone,
so a second run is offline and instant. Prints DIR on the last line.
"""
from __future__ import annotations

import argparse
import os
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import aiverse_pkg as pkg  # noqa: E402


def default_dir() -> Path:
    env = os.environ.get('AIVERSE_TEMPLATES_DIR')
    return Path(env) if env else pkg.home() / 'templates'


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--out', type=Path, default=None)
    ap.add_argument('--only', default='', help='comma-separated engine ids')
    a = ap.parse_args(argv)
    out = (a.out or default_dir()).resolve()
    only = {s for s in a.only.split(',') if s}
    out.mkdir(parents=True, exist_ok=True)
    index = pkg.load_index(max_age=pkg.TEMPLATE_CACHE_AGE)
    done = 0
    for item in index.get('packages', []):
        if item.get('category') != 'templates' or not item.get('official'):
            continue
        entry = pkg.package_entry(item, max_age=pkg.TEMPLATE_CACHE_AGE)
        version = pkg.pick_version(entry, None)
        engine_id = (entry.get('provides') or {}).get('engineTemplate')
        if not engine_id or (only and engine_id not in only):
            continue
        dest = out / engine_id
        stamp = dest / '.aiverse-package'
        if stamp.is_file() and stamp.read_text().strip() == f"{item['id']}@{version}":
            done += 1
            continue
        src = pkg.fetch(item['id'], version, log=lambda s: print(s, file=sys.stderr))
        if dest.exists():
            shutil.rmtree(dest)
        shutil.copytree(src, dest, ignore=shutil.ignore_patterns(pkg.MANIFEST))
        stamp.write_text(f"{item['id']}@{version}\n")
        done += 1
        print(f'{engine_id}: {item["id"]}@{version}', file=sys.stderr)
    if only and done < len(only):
        print(f'fetch_templates: only {done} of {len(only)} requested templates found', file=sys.stderr)
        return 1
    print(out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
