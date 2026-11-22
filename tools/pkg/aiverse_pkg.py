#!/usr/bin/env python3
"""AIVERSE packages: content, project templates and authoring sources downloaded on demand.

The engine repository ships code and a minimal set of assets. Heavy optional content (PBR material
libraries, their source images, sample projects) lives in the package catalog
(https://github.com/chromealex/AIVERSE-PACKAGES): a short index.json ({id, category, path} per package),
each package folder with aiverse.package.json and versions.json, one .zip per package version
attached to a GitHub release. This tool finds, downloads, verifies and installs those packages.

  aiverse_pkg.py search [words] [--kind K] [--category C]
  aiverse_pkg.py info <id>
  aiverse_pkg.py fetch <id>[@range]            download into the shared cache, print its folder
  aiverse_pkg.py add <id>[@range] --project P  install into a project (game.json "packages" + aiverse.lock)
  aiverse_pkg.py remove <id> --project P
  aiverse_pkg.py install --project P           (re)install everything the project's lock / game.json lists
  aiverse_pkg.py status --project P            what is listed, installed, modified
  aiverse_pkg.py verify --project P            installed files against the lock (exit 1 on mismatch)
  aiverse_pkg.py skill <skill-id> [--project P]  packages a repository skill names (SKILL.md "packages:");
                                                 with --project installs them
  aiverse_pkg.py templates [words] [--all] [--refresh|--cached]  project templates: official (AIVERSE)
                                                 ones, with --all every template package of the catalog
  aiverse_pkg.py create <template-id> <folder>   new project folder from a template package

Packages are added to the catalog by pull request to AIVERSE-PACKAGES (its tools/catalog.py builds,
checks and publishes them); this tool only consumes the catalog.

Every command accepts --json (machine-readable output for MCP and agents).

Environment: AIVERSE_PACKAGES_HOME (cache, default ~/.aiverse/packages), AIVERSE_PACKAGES_INDEX
(index URL or local path, default the catalog's main branch), AIVERSE_PACKAGES_OFFLINE=1 (never touch
the network; only cached packages). Standard library only.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import sys
import tempfile
import time
import urllib.parse
import urllib.request
import zipfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

SCHEMA = 1        # aiverse.lock
INDEX_SCHEMA = 2  # catalog index.json: {id, category, path}; the rest is in the package folder
MANIFEST = 'aiverse.package.json'
LOCK = 'aiverse.lock'
CATALOG_REPO = 'chromealex/AIVERSE-PACKAGES'
DEFAULT_INDEX = f'https://raw.githubusercontent.com/{CATALOG_REPO}/main/index.json'
KINDS = ('content', 'template', 'source', 'script', 'plugin', 'tool')
ID_RE = re.compile(r'^[a-z0-9][a-z0-9-]{1,62}$')
VERSION_RE = re.compile(r'^(\d+)\.(\d+)\.(\d+)$')


class PkgError(Exception):
    pass


# ---------------------------------------------------------------- versions

def parse_version(text: str) -> tuple[int, int, int]:
    m = VERSION_RE.match(text or '')
    if not m:
        raise PkgError(f"bad version '{text}' (expected MAJOR.MINOR.PATCH)")
    return tuple(int(x) for x in m.groups())


def matches(version: str, spec: str | None) -> bool:
    """spec: None / '' / '*' / 'latest' / '1.2.3' / '^1.2' / '~1.2.3' / '>=1.0.0'."""
    if not spec or spec in ('*', 'latest'):
        return True
    v = parse_version(version)
    def full(s):
        parts = s.split('.')
        while len(parts) < 3:
            parts.append('0')
        return parse_version('.'.join(parts))
    if spec.startswith('>='):
        return v >= full(spec[2:])
    if spec.startswith('^'):
        b = full(spec[1:])
        upper = (b[0] + 1, 0, 0) if b[0] > 0 else ((0, b[1] + 1, 0) if b[1] > 0 else (0, 0, b[2] + 1))
        return b <= v < upper
    if spec.startswith('~'):
        b = full(spec[1:])
        return b <= v < (b[0], b[1] + 1, 0)
    return v == full(spec)


def split_ref(ref: str) -> tuple[str, str | None]:
    pkg, _, spec = ref.partition('@')
    if not ID_RE.match(pkg):
        raise PkgError(f"bad package id '{pkg}' (lowercase letters, digits and '-')")
    return pkg, (spec or None)


# ---------------------------------------------------------------- paths and io

def home() -> Path:
    env = os.environ.get('AIVERSE_PACKAGES_HOME')
    return Path(env).expanduser() if env else Path.home() / '.aiverse' / 'packages'


def offline() -> bool:
    return os.environ.get('AIVERSE_PACKAGES_OFFLINE', '') not in ('', '0')


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def read_json(path: Path, default=None):
    if not path.exists():
        return default
    try:
        return json.loads(path.read_text(encoding='utf-8'))
    except json.JSONDecodeError as e:
        raise PkgError(f'{path}: {e}')


def write_json(path: Path, data) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + '.tmp')
    tmp.write_text(json.dumps(data, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    os.replace(tmp, path)


def fetch_bytes(url: str) -> bytes:
    if '://' not in url or url.startswith('file://'):
        return Path(urllib.parse.urlparse(url).path if url.startswith('file://') else url).read_bytes()
    if offline():
        raise PkgError(f'offline (AIVERSE_PACKAGES_OFFLINE): cannot fetch {url}')
    req = urllib.request.Request(url, headers={'User-Agent': 'aiverse-pkg/1'})
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read()


def download(url: str, dest: Path, expect_sha: str, expect_size: int | None, log) -> None:
    if dest.exists() and sha256_file(dest) == expect_sha:
        return
    dest.parent.mkdir(parents=True, exist_ok=True)
    part = dest.with_name(dest.name + '.part')
    if '://' not in url or url.startswith('file://'):
        shutil.copyfile(urllib.parse.urlparse(url).path if url.startswith('file://') else url, part)
    else:
        if offline():
            raise PkgError(f'offline (AIVERSE_PACKAGES_OFFLINE): {dest.name} is not in the cache')
        req = urllib.request.Request(url, headers={'User-Agent': 'aiverse-pkg/1'})
        with urllib.request.urlopen(req, timeout=120) as r, open(part, 'wb') as out:
            total, last = 0, time.time()
            while True:
                block = r.read(1 << 20)
                if not block:
                    break
                out.write(block)
                total += len(block)
                if time.time() - last > 2 and expect_size:
                    log(f'  {total * 100 // expect_size}% of {expect_size // (1 << 20)} MB')
                    last = time.time()
    got = sha256_file(part)
    if got != expect_sha:
        part.unlink(missing_ok=True)
        raise PkgError(f'{url}: sha256 {got} does not match the index ({expect_sha})')
    os.replace(part, dest)


# ---------------------------------------------------------------- index

def index_source() -> str:
    return os.environ.get('AIVERSE_PACKAGES_INDEX', DEFAULT_INDEX)


def fresh(path: Path, max_age: float | None) -> bool:
    """A cached file younger than max_age seconds (None: never fresh enough)."""
    return max_age is not None and path.exists() and time.time() - path.stat().st_mtime < max_age


def load_index(refresh: bool = False, max_age: float | None = None) -> dict:
    """The catalog index; cached copy when offline, younger than max_age, or the network fails."""
    src = index_source()
    cached = home() / 'index-cache' / (hashlib.sha256(src.encode()).hexdigest()[:16] + '.json')
    if cached.exists() and ((not refresh and offline()) or fresh(cached, max_age)):
        return read_json(cached)
    try:
        data = json.loads(fetch_bytes(src).decode('utf-8'))
    except Exception as e:  # network or file error: fall back to the cached copy
        if cached.exists():
            return read_json(cached)
        raise PkgError(f'cannot read the package index {src}: {e}')
    if data.get('schema') != INDEX_SCHEMA:
        raise PkgError(f'{src}: unsupported index schema {data.get("schema")}')
    write_json(cached, data)
    return data


def catalog_url(path: str) -> str:
    """A file of the catalog repository, next to index.json (URL or local folder)."""
    src = index_source()
    if '://' in src and not src.startswith('file://'):
        return src.rsplit('/', 1)[0] + '/' + path
    base = Path(urllib.parse.urlparse(src).path if src.startswith('file://') else src).parent
    return str(base / path)


def package_entry(item: dict, refresh: bool = False, max_age: float | None = None) -> dict:
    """One package: its index line + <path>/aiverse.package.json + <path>/versions.json (cached for offline use)."""
    cached = home() / 'index-cache' / (hashlib.sha256(index_source().encode()).hexdigest()[:16] + f"-{item['id']}.json")
    if cached.exists() and ((not refresh and offline()) or fresh(cached, max_age)):
        return dict(read_json(cached), **item)
    try:
        manifest = json.loads(fetch_bytes(catalog_url(f"{item['path']}/{MANIFEST}")).decode('utf-8'))
        published = json.loads(fetch_bytes(catalog_url(f"{item['path']}/versions.json")).decode('utf-8'))
    except Exception as e:
        if cached.exists():
            return read_json(cached)
        raise PkgError(f"cannot read package {item['id']} from the catalog: {e}")
    entry = dict(manifest, **item)
    for k in ('latest', 'versions', 'previews', 'readme', 'viewer'):
        if k in published:
            entry[k] = published[k]
    write_json(cached, entry)
    return entry


def index_entry(index: dict, pkg: str, refresh: bool = False) -> dict:
    for e in index.get('packages', []):
        if e.get('id') == pkg:
            return package_entry(e, refresh)
    raise PkgError(f"package '{pkg}' is not in the catalog (aiverse_pkg.py search)")


def pick_version(entry: dict, spec: str | None) -> str:
    versions = sorted((v for v in entry.get('versions', {}) if matches(v, spec)), key=parse_version)
    if not versions:
        raise PkgError(f"package '{entry['id']}' has no version matching '{spec}'")
    return versions[-1]


# ---------------------------------------------------------------- cache

def cached_dir(pkg: str, version: str) -> Path:
    return home() / pkg / version


def installed_versions(pkg: str) -> list[str]:
    root = home() / pkg
    if not root.is_dir():
        return []
    out = [p.name for p in root.iterdir() if VERSION_RE.match(p.name) and (p / MANIFEST).is_file()]
    return sorted(out, key=parse_version)


def fetch(pkg: str, spec: str | None = None, log=lambda s: None) -> Path:
    """Make <pkg>@<best matching version> present in the cache; returns its folder."""
    local = [v for v in installed_versions(pkg) if matches(v, spec)]
    try:
        index = load_index()
    except PkgError:
        if local:
            return cached_dir(pkg, local[-1])
        raise
    entry = index_entry(index, pkg)
    version = pick_version(entry, spec)
    target = cached_dir(pkg, version)
    if (target / MANIFEST).is_file():
        return target
    info = entry['versions'][version]
    archive = home() / 'downloads' / f"{info['sha256']}.zip"
    log(f'downloading {pkg}@{version} ({info.get("size", 0) // (1 << 20)} MB)')
    download(info['url'], archive, info['sha256'], info.get('size'), log)
    tmp = Path(tempfile.mkdtemp(prefix=f'.{pkg}-', dir=home()))
    try:
        with zipfile.ZipFile(archive) as z:
            for name in z.namelist():
                p = Path(name)
                if p.is_absolute() or '..' in p.parts:
                    raise PkgError(f'{archive.name}: unsafe path {name}')
            z.extractall(tmp)
        manifest = read_json(tmp / MANIFEST)
        if not manifest or manifest.get('id') != pkg or manifest.get('version') != version:
            raise PkgError(f'{archive.name}: manifest does not describe {pkg}@{version}')
        target.parent.mkdir(parents=True, exist_ok=True)
        if target.exists():
            shutil.rmtree(target)
        os.replace(tmp, target)
    finally:
        if tmp.exists():
            shutil.rmtree(tmp, ignore_errors=True)
    for dep, dspec in (read_json(target / MANIFEST).get('dependencies') or {}).items():
        fetch(dep, dspec, log)
    return target


def package_dir(pkg: str, spec: str | None = None, log=None) -> Path:
    """For tools: the cached folder of a package, downloading it when needed."""
    return fetch(pkg, spec, log or (lambda s: print(s, file=sys.stderr)))


# ---------------------------------------------------------------- projects

def project_paths(project: Path) -> tuple[Path, Path]:
    game = project / 'game.json'
    if not game.is_file():
        raise PkgError(f'{project}: not a project folder (no game.json)')
    return game, project / LOCK


def package_files(src: Path) -> list[str]:
    out = []
    for p in sorted(src.rglob('*')):
        if p.is_file() and p.name != MANIFEST and not p.name.startswith('.'):
            out.append(p.relative_to(src).as_posix())
    return out


def install_into(project: Path, pkg: str, spec: str | None, force: bool, log, _chain: tuple = ()) -> dict:
    game_path, lock_path = project_paths(project)
    src = fetch(pkg, spec, log)
    manifest = read_json(src / MANIFEST)
    check_engine(manifest, log)
    inst = manifest.get('install') or {}
    mount = inst.get('mount')
    if not mount:
        raise PkgError(f"package '{pkg}' ({manifest.get('kind')}) is not installed into projects; "
                       f"use 'fetch' to get its folder")
    # content dependencies go into the project first: a model pack refers to the textures of the
    # material package it depends on instead of carrying copies (every texture once)
    deps = []
    for dep, dspec in (manifest.get('dependencies') or {}).items():
        if dep in _chain or dep == pkg:
            raise PkgError(f"dependency cycle: {' -> '.join(_chain + (pkg, dep))}")
        have = read_json(lock_path, {'packages': {}}).get('packages', {}).get(dep)
        if have and (not dspec or matches(have['version'], dspec)):
            continue
        dm = read_json(fetch(dep, dspec, log) / MANIFEST)
        if (dm.get('install') or {}).get('mount'):
            deps.append(install_into(project, dep, dspec, force, log, _chain + (pkg,)))
    lock = read_json(lock_path, {'schema': SCHEMA, 'packages': {}})
    previous = lock['packages'].get(pkg)
    if previous:
        uninstall_from(project, pkg, lock, keep_listing=True)
    target = project / mount
    files = {}
    for rel in package_files(src):
        dst = target / rel
        if dst.exists() and not force and not previous:
            if sha256_file(dst) != sha256_file(src / rel):
                raise PkgError(f'{dst} already exists and differs (use --force to overwrite)')
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(src / rel, dst)
        files[(Path(mount) / rel).as_posix()] = sha256_file(dst)
    merged = {}
    for m in inst.get('mergeJson') or []:
        into = project / m['into']
        add = read_json(src / m['from'])
        cur = read_json(into, {})
        clash = [k for k in add if k in cur and cur[k] != add[k]]
        if clash and not force:
            raise PkgError(f"{m['into']}: ids already used by the project: {', '.join(sorted(clash)[:8])}")
        cur.update(add)
        write_json(into, cur)
        merged[m['into']] = sorted(add)
    game = read_json(game_path)
    packages = game.setdefault('packages', {})
    if pkg not in packages or not packages[pkg]:
        packages[pkg] = '^' + manifest['version']
    write_json(game_path, game)
    entry = {'version': manifest['version'], 'kind': manifest.get('kind'), 'mount': mount,
             'files': files, 'merged': merged}
    try:
        info = index_entry(load_index(), pkg)['versions'][manifest['version']]
        entry['sha256'], entry['url'] = info['sha256'], info['url']
    except (PkgError, KeyError):
        pass
    lock['packages'][pkg] = entry
    lock['packages'] = dict(sorted(lock['packages'].items()))
    write_json(lock_path, lock)
    r = {'id': pkg, 'version': manifest['version'], 'mount': mount, 'files': len(files),
         'merged': {k: len(v) for k, v in merged.items()}}
    if deps:
        r['dependencies'] = deps
    return r


def uninstall_from(project: Path, pkg: str, lock: dict, keep_listing: bool = False) -> int:
    entry = lock['packages'].get(pkg)
    if not entry:
        raise PkgError(f"package '{pkg}' is not installed in {project}")
    removed = 0
    for rel, sha in entry.get('files', {}).items():
        p = project / rel
        if p.is_file():
            p.unlink()
            removed += 1
    mount = project / entry.get('mount', '')
    for d in sorted((p for p in mount.rglob('*') if p.is_dir()), key=lambda p: -len(p.parts)) if mount.is_dir() else []:
        if not any(d.iterdir()):
            d.rmdir()
    while mount != project and project in mount.parents and mount.is_dir() and not any(mount.iterdir()):
        mount.rmdir()
        mount = mount.parent
    for into, keys in entry.get('merged', {}).items():
        path = project / into
        cur = read_json(path, {})
        for k in keys:
            cur.pop(k, None)
        write_json(path, cur)
    if not keep_listing:
        del lock['packages'][pkg]
    return removed


# ---------------------------------------------------------------- engine version and skills

def engine_root() -> Path:
    return Path(__file__).resolve().parents[2]


def engine_version() -> str | None:
    m = re.search(r'project\(AIVERSE VERSION (\d+\.\d+\.\d+)', (engine_root() / 'CMakeLists.txt').read_text(encoding='utf-8')
                  if (engine_root() / 'CMakeLists.txt').is_file() else '')
    return m.group(1) if m else None


def check_engine(manifest: dict, log) -> None:
    need, have = manifest.get('minEngine'), engine_version()
    if need and have and parse_version(have) < parse_version(need):
        log(f"note: {manifest['id']} needs engine {need}+, this checkout reports {have} "
            f"(main between releases still reports the previous release)")


def skill_packages(skill: str) -> list[str]:
    hits = sorted((engine_root() / 'skills').glob(f'*/{skill}/SKILL.md'))
    if len(hits) != 1:
        raise PkgError(f"skill '{skill}' not found in skills/<category>/")
    text = hits[0].read_text(encoding='utf-8')
    head = text[4:text.find('\n---', 4)] if text.startswith('---\n') else ''
    for line in head.splitlines():
        if line.startswith('packages:'):
            return [x.strip() for x in line[9:].replace('[', '').replace(']', '').split(',') if x.strip()]
    return []


# ---------------------------------------------------------------- CLI

def cmd_search(a):
    index = load_index(refresh=True)
    words = [w.lower() for w in a.words]
    out = []
    for item in index.get('packages', []):
        if a.category and item.get('category') != a.category:
            continue
        e = package_entry(item, refresh=True)
        if a.kind and e.get('kind') != a.kind:
            continue
        texts = [v for k, v in e.items() if k.split('-', 1)[0] in ('title', 'summary') and isinstance(v, str)]
        hay = ' '.join([e['id'], e.get('category', ''), *texts, ' '.join(e.get('tags', [])),
                        json.dumps(e.get('provides', {}), ensure_ascii=False)]).lower()
        if all(w in hay for w in words):
            latest = e['versions'][e['latest']]
            out.append({'id': e['id'], 'kind': e.get('kind'), 'category': e.get('category'),
                        'title': e.get('title'), 'latest': e['latest'],
                        'minEngine': latest.get('minEngine'), 'size': latest.get('size'),
                        'cached': e['latest'] in installed_versions(e['id'])})
    return out


def strip_comments(text: str) -> str:
    """JSON with // and /* */ comments (the engine's own reader allows them) -> plain JSON."""
    out, i, n, in_str = [], 0, len(text), False
    while i < n:
        c = text[i]
        if in_str:
            out.append(c)
            if c == '\\' and i + 1 < n:
                out.append(text[i + 1]); i += 1
            elif c == '"':
                in_str = False
        elif c == '"':
            in_str = True; out.append(c)
        elif text.startswith('//', i):
            while i < n and text[i] != '\n':
                i += 1
            continue
        elif text.startswith('/*', i):
            end = text.find('*/', i + 2)
            i = n if end < 0 else end + 2
            continue
        else:
            out.append(c)
        i += 1
    return ''.join(out)


TEMPLATE_CACHE_AGE = 6 * 3600   # the start screen reads templates from the cache this long


def cmd_templates(a):
    """Project templates of the catalog for the editor's start screen. Official ones (the index marks them:
    made by the AIVERSE authors) by default, every template package with --all. Titles and summaries come
    with their translations (title-ru, ...); previews are URLs, the first one is also downloaded into the
    cache (previewFile) so native screens can show it offline. --cached never touches the network (the
    start screen opens instantly from what an earlier call saved; empty when nothing is cached yet)."""
    if a.cached:
        os.environ['AIVERSE_PACKAGES_OFFLINE'] = '1'
        try:
            index = load_index()
        except PkgError:
            return []
        age = None
    else:
        age = None if a.refresh else TEMPLATE_CACHE_AGE
        index = load_index(refresh=a.refresh, max_age=age)
    items = [it for it in index.get('packages', [])
             if it.get('category') == 'templates' and (a.all or it.get('official'))]

    def entry(item):
        try:
            return package_entry(item, refresh=a.refresh, max_age=age)
        except PkgError:
            return None  # one unreachable package does not hide the others
    with ThreadPoolExecutor(max_workers=8) as pool:   # one request per file: parallel on a cold cache
        entries = list(pool.map(entry, items))
    words = [w.lower() for w in a.words]
    out = []
    for item, e in zip(items, entries):
        if not e or e.get('kind') != 'template' or not e.get('latest'):
            continue
        texts = [v for k, v in e.items() if k.split('-', 1)[0] in ('title', 'summary') and isinstance(v, str)]
        hay = ' '.join([e['id'], *texts, ' '.join(e.get('tags', []))]).lower()
        if not all(w in hay for w in words):
            continue
        latest = e['versions'][e['latest']]
        provides = e.get('provides') or {}
        row = {k: v for k, v in e.items() if k.split('-', 1)[0] in ('title', 'summary', 'description')}
        row.update(id=e['id'], official=bool(item.get('official')), latest=e['latest'],
                   minEngine=latest.get('minEngine'), size=latest.get('size'), authors=e.get('authors', []),
                   tags=e.get('tags', []), previews=e.get('previews', []), readme=e.get('readme'),
                   maps=provides.get('maps', []), module=provides.get('module', ''),
                   engineTemplate=provides.get('engineTemplate'),
                   cached=e['latest'] in installed_versions(e['id']))
        if row['previews']:
            row['previewFile'] = cached_preview(e['id'], e['latest'], row['previews'][0])
        out.append(row)
    out.sort(key=lambda r: (not r['official'], r.get('title', r['id']).lower()))
    return out


def cached_preview(pkg: str, version: str, url: str) -> str | None:
    """Downloads a preview image once per package version; None when it cannot be fetched."""
    ext = Path(urllib.parse.urlparse(url).path).suffix.lower() or '.jpg'
    dest = home() / 'previews' / f'{pkg}-{version}{ext}'
    if dest.exists():
        return str(dest)
    if offline():
        return None
    try:
        data = fetch_bytes(url)
    except Exception:
        return None
    dest.parent.mkdir(parents=True, exist_ok=True)
    tmp = dest.with_suffix(dest.suffix + '.part')
    tmp.write_bytes(data)
    os.replace(tmp, dest)
    return str(dest)


def human(r, a):
    if isinstance(r, list):
        for e in r:
            mb = (e.get('size') or 0) / (1 << 20)
            print(f"{e['id']:<28} {e.get('kind', ''):<9} {e.get('latest', ''):<9} {mb:7.1f} MB  "
                  f"{'cached ' if e.get('cached') else ''}{e.get('title', '')}")
    elif isinstance(r, dict):
        print(json.dumps(r, indent=2, ensure_ascii=False))
    elif r is not None:
        print(r)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--json', action='store_true', help='machine-readable output')
    sub = ap.add_subparsers(dest='cmd', required=True)
    s = sub.add_parser('search'); s.add_argument('words', nargs='*'); s.add_argument('--kind', choices=KINDS)
    s.add_argument('--category')
    s = sub.add_parser('info'); s.add_argument('id')
    s = sub.add_parser('fetch'); s.add_argument('ref')
    s = sub.add_parser('path'); s.add_argument('ref')
    s = sub.add_parser('add'); s.add_argument('ref'); s.add_argument('--project', type=Path, required=True)
    s.add_argument('--force', action='store_true')
    s = sub.add_parser('remove'); s.add_argument('id'); s.add_argument('--project', type=Path, required=True)
    s = sub.add_parser('install'); s.add_argument('--project', type=Path, required=True)
    s = sub.add_parser('status'); s.add_argument('--project', type=Path, required=True)
    s = sub.add_parser('verify'); s.add_argument('--project', type=Path, required=True)
    s = sub.add_parser('skill'); s.add_argument('skill'); s.add_argument('--project', type=Path)
    s = sub.add_parser('templates'); s.add_argument('words', nargs='*'); s.add_argument('--all', action='store_true')
    s.add_argument('--refresh', action='store_true'); s.add_argument('--cached', action='store_true')
    s = sub.add_parser('create'); s.add_argument('ref'); s.add_argument('folder', type=Path)
    for k in sub.choices.values():
        k.add_argument('--json', action='store_true', default=argparse.SUPPRESS)
    a = ap.parse_args(argv)
    log = (lambda s: print(s, file=sys.stderr))
    try:
        r = run(a, log)
    except (PkgError, OSError, zipfile.BadZipFile, urllib.error.URLError) as e:
        if getattr(a, 'json', False):
            print(json.dumps({'ok': False, 'error': str(e)}, ensure_ascii=False))
        else:
            print(f'aiverse_pkg: {e}', file=sys.stderr)
        return 2
    if getattr(a, 'json', False):
        print(json.dumps({'ok': True, 'result': r}, indent=2, ensure_ascii=False))
    else:
        human(r, a)
    if a.cmd == 'verify' and r.get('mismatch'):
        return 1
    return 0


def run(a, log):
    if a.cmd == 'search':
        return cmd_search(a)
    if a.cmd == 'templates':
        return cmd_templates(a)
    if a.cmd == 'info':
        e = index_entry(load_index(refresh=True), a.id, refresh=True)
        return dict(e, cached=installed_versions(a.id))
    if a.cmd in ('fetch', 'path'):
        pkg, spec = split_ref(a.ref)
        return str(fetch(pkg, spec, log))
    if a.cmd == 'add':
        pkg, spec = split_ref(a.ref)
        r = install_into(a.project.resolve(), pkg, spec, a.force, log)
        if spec:
            game_path, _ = project_paths(a.project)
            game = read_json(game_path); game['packages'][pkg] = spec; write_json(game_path, game)
        return r
    if a.cmd == 'remove':
        project = a.project.resolve()
        game_path, lock_path = project_paths(project)
        lock = read_json(lock_path, {'schema': SCHEMA, 'packages': {}})
        n = uninstall_from(project, a.id, lock)
        if lock['packages']:
            write_json(lock_path, lock)
        else:
            lock_path.unlink(missing_ok=True)
        game = read_json(game_path)
        (game.get('packages') or {}).pop(a.id, None)
        if 'packages' in game and not game['packages']:
            del game['packages']
        write_json(game_path, game)
        return {'id': a.id, 'removedFiles': n}
    if a.cmd == 'install':
        project = a.project.resolve()
        game_path, lock_path = project_paths(project)
        wanted = dict((read_json(game_path).get('packages') or {}))
        lock = read_json(lock_path, {'schema': SCHEMA, 'packages': {}})
        out = []
        for pkg in sorted(set(wanted) | set(lock['packages'])):
            spec = lock['packages'][pkg]['version'] if pkg in lock['packages'] else wanted.get(pkg)
            out.append(install_into(project, pkg, spec, True, log))
        return out
    if a.cmd in ('status', 'verify'):
        project = a.project.resolve()
        game_path, lock_path = project_paths(project)
        wanted = read_json(game_path).get('packages') or {}
        lock = read_json(lock_path, {'schema': SCHEMA, 'packages': {}})
        rows, mismatch = [], 0
        for pkg in sorted(set(wanted) | set(lock['packages'])):
            e = lock['packages'].get(pkg)
            row = {'id': pkg, 'wanted': wanted.get(pkg), 'locked': e and e['version']}
            if e:
                missing = [f for f in e['files'] if not (project / f).is_file()]
                changed = [f for f, sha in e['files'].items()
                           if (project / f).is_file() and a.cmd == 'verify' and sha256_file(project / f) != sha]
                row.update(files=len(e['files']), missing=len(missing), changed=len(changed) if a.cmd == 'verify' else None)
                mismatch += len(missing) + len(changed)
                if wanted.get(pkg) and not matches(e['version'], wanted[pkg]):
                    row['outdated'] = True
                    mismatch += 1
            else:
                row['missing'] = 'not installed (aiverse_pkg.py install)'
                mismatch += 1
            rows.append(row)
        return {'project': str(project), 'packages': rows, 'mismatch': mismatch}
    if a.cmd == 'skill':
        refs = skill_packages(a.skill)
        if not a.project:
            return {'skill': a.skill, 'packages': refs}
        return {'skill': a.skill, 'installed': [install_into(a.project.resolve(), *split_ref(r), False, log) for r in refs]}
    if a.cmd == 'create':
        pkg, spec = split_ref(a.ref)
        src = fetch(pkg, spec, log)
        m = read_json(src / MANIFEST)
        if m.get('kind') != 'template':
            raise PkgError(f"'{pkg}' is a {m.get('kind')} package, not a project template")
        dest = a.folder.resolve()
        if dest.exists() and any(dest.iterdir()):
            raise PkgError(f'{dest} exists and is not empty')
        shutil.copytree(src, dest, dirs_exist_ok=True, ignore=shutil.ignore_patterns(MANIFEST))
        if not (dest / 'game.json').is_file():
            raise PkgError(f"template '{pkg}' has no game.json at its root")
        game = json.loads(strip_comments((dest / 'game.json').read_text(encoding='utf-8')))  # game.json may have // comments
        game['template'] = pkg
        write_json(dest / 'game.json', game)
        for dep, dspec in (game.get('packages') or {}).items():
            install_into(dest, dep, dspec, False, log)
        return {'project': str(dest), 'template': f"{pkg}@{m['version']}"}
    raise PkgError(f'unknown command {a.cmd}')


if __name__ == '__main__':
    sys.exit(main())
