#!/usr/bin/env python3
"""AIVERSE website: devlog entries, flagship systems, showcase frame and the static build (stdlib only).

  python3 tools/site/build.py check                      validate docs/devlog
  python3 tools/site/build.py add --system render --title "..." [--media a.png ...]
  python3 tools/site/build.py build [--out build/site]   site/ + devlog.json + media
  python3 tools/site/build.py publish [--dry-run]         after pushing main: check, build, push gh-pages

Rules for agents: AGENTS.md "Ход разработки на сайте", skill skills/workflow/aiverse-devlog.
"""
import argparse
import datetime as dt
import html
import hashlib
import json
import os
from pathlib import Path
import posixpath
import re
import shutil
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
ROOT = Path(__file__).resolve().parents[2]
DEVLOG = ROOT / 'docs/devlog'
ENTRIES = DEVLOG / 'entries'
MEDIA = DEVLOG / 'media'
SYSTEMS = DEVLOG / 'systems.json'
FEATURES = DEVLOG / 'features'  # features/<system>/<include id>.md (+ .en.md): mini-articles
SHOWCASE = DEVLOG / 'showcase.json'
RELEASES = DEVLOG / 'releases.json'
SITE = ROOT / 'site'

STATUSES = ('done', 'in-progress', 'experimental')
REQUIRED = ('id', 'date', 'system', 'title', 'summary', 'body', 'status', 'verified', 'media')
OPTIONAL = ('limits', 'branch', 'commit', 'links', 'author',
            'titleEn', 'summaryEn', 'bodyEn', 'verifiedEn', 'limitsEn')
IMAGE_EXT = {'.png', '.jpg', '.jpeg', '.webp', '.gif'}
STILL_EXT = {'.png', '.jpg', '.jpeg'}
VIDEO_EXT = {'.mp4', '.webm'}
MAX_IMAGE = 3 * 1024 * 1024
MAX_VIDEO = 15 * 1024 * 1024
MAX_FRAME = 6 * 1024 * 1024
MIN_SHOWCASE_W = 1600
MIN_COVER_W = 1280
ID_RE = re.compile(r'^\d{4}-\d{2}-\d{2}-[a-z0-9]+(?:-[a-z0-9]+)*$')
WHERE = {'main': 'в main', 'branch': 'в разработке'}
ROADMAP_STATUS = {'done': 'Готово', 'in-progress': 'В работе', 'planned': 'Запланировано'}
MONTHS = ['январь', 'февраль', 'март', 'апрель', 'май', 'июнь', 'июль', 'август', 'сентябрь', 'октябрь', 'ноябрь', 'декабрь']
_ARM = '-72,-220 0,-262 71,-220 71,-58 0,-100 -72,-58'
MARK_LIGHT = ('<svg width="72" height="68" viewBox="-280 -280 560 530"><g fill="#F3EFE8">'
              f'<polygon points="{_ARM}"/><polygon points="{_ARM}" transform="rotate(120)"/>'
              f'<polygon points="{_ARM}" transform="rotate(240)"/></g>'
              '<polygon points="0,-78 78,0 0,78 -78,0" fill="#E9C3A4"/></svg>')


def load_systems():
    data = json.loads(SYSTEMS.read_text(encoding='utf-8'))
    return {s['id']: s for s in data['systems']}, data['systems']


def repo_file(rel):
    """A repository-relative path that must stay inside the repository."""
    if not isinstance(rel, str) or not rel or rel.startswith('/') or '\\' in rel or '..' in Path(rel).parts:
        return None
    return ROOT / rel


def image_size(f):
    """(width, height) of a PNG or JPEG, or None."""
    data = f.read_bytes()
    if data[:8] == b'\x89PNG\r\n\x1a\n':
        return struct.unpack('>II', data[16:24])
    if data[:2] == b'\xff\xd8':
        i = 2
        while i + 9 < len(data):
            if data[i] != 0xFF:
                i += 1
                continue
            marker = data[i + 1]
            if marker in (0xC0, 0xC1, 0xC2):
                h, w = struct.unpack('>HH', data[i + 5:i + 9])
                return w, h
            i += 2 + struct.unpack('>H', data[i + 2:i + 4])[0]
    return None


def check_frame(p, fr, min_w, keys):
    """A showcase frame or a system cover: real still, landscape, wide enough, captioned."""
    errors = []
    if not isinstance(fr, dict):
        return [f'{p}: must be an object']
    for key in keys:
        if key not in fr:
            errors.append(f'{p}: missing "{key}"')
    if not isinstance(fr.get('approved'), bool):
        errors.append(f'{p}: "approved" must be true or false; only the user sets true')
    f = repo_file(fr.get('src'))
    if f is None or not f.is_file():
        return errors + [f'{p}: image not found: {fr.get("src")!r}']
    if f.suffix.lower() not in STILL_EXT:
        return errors + [f'{p}: must be a PNG or JPG still']
    if f.stat().st_size > MAX_FRAME:
        errors.append(f'{p}: larger than 6 MB; save a JPG')
    size = image_size(f)
    if size is None or size[0] < min_w or size[0] < size[1]:
        errors.append(f'{p}: must be landscape and at least {min_w} px wide, got {size}')
    return errors


def check_entry(path, systems):
    errors = []
    try:
        e = json.loads(path.read_text(encoding='utf-8'))
    except Exception as exc:  # noqa: BLE001 - report any parse error
        return None, [f'{path.name}: invalid JSON: {exc}']
    p = path.name
    for key in REQUIRED:
        if key not in e:
            errors.append(f'{p}: missing "{key}"')
    for key in e:
        if key not in REQUIRED and key not in OPTIONAL:
            errors.append(f'{p}: unknown key "{key}"')
    if errors:
        return None, errors
    if not ID_RE.match(e['id']):
        errors.append(f'{p}: id must be YYYY-MM-DD-system-slug in lowercase latin')
    if path.stem != e['id']:
        errors.append(f'{p}: file name must be <id>.json')
    try:
        dt.date.fromisoformat(e['date'])
    except ValueError:
        errors.append(f'{p}: date must be YYYY-MM-DD')
    if not e['id'].startswith(e['date'] + '-'):
        errors.append(f'{p}: id must start with the date')
    if e['system'] not in systems:
        errors.append(f'{p}: unknown system "{e["system"]}" (see docs/devlog/systems.json)')
    if e['status'] not in STATUSES:
        errors.append(f'{p}: status must be one of {STATUSES}')
    for key in ('title', 'summary', 'verified'):
        if not isinstance(e[key], str) or not e[key].strip():
            errors.append(f'{p}: "{key}" must be non-empty text')
    if len(e.get('summary') or '') > 280:
        errors.append(f'{p}: summary longer than 280 characters')
    if not isinstance(e['body'], list) or not all(isinstance(x, str) and x.strip() for x in e['body']):
        errors.append(f'{p}: body must be a list of paragraphs')
    media = e['media']
    flagship = bool((systems.get(e['system']) or {}).get('flagship'))
    if not isinstance(media, list) or (flagship and not media):
        # flagship diaries always show a frame; blog posts about other topics may be text only
        errors.append(f'{p}: at least one screenshot or video in "media"')
        media = []
    for m in media:
        src = m.get('src') if isinstance(m, dict) else None
        f = repo_file(src)
        if f is None:
            errors.append(f'{p}: media src must be a repository-relative path: {src!r}')
            continue
        if not f.is_file():
            errors.append(f'{p}: media file not found: {src}')
            continue
        ext = f.suffix.lower()
        if ext in IMAGE_EXT:
            if f.stat().st_size > MAX_IMAGE:
                errors.append(f'{p}: {src} is larger than 3 MB; save a JPG/WebP')
        elif ext in VIDEO_EXT:
            if f.stat().st_size > MAX_VIDEO:
                errors.append(f'{p}: {src} is larger than 15 MB')
        else:
            errors.append(f'{p}: {src}: only {sorted(IMAGE_EXT | VIDEO_EXT)}')
        if not (m.get('caption') or '').strip():
            errors.append(f'{p}: {src}: caption is required')
    for link in e.get('links') or []:
        href = link.get('href', '') if isinstance(link, dict) else ''
        if not href.startswith('https://'):
            f = repo_file(href)
            if f is None or not f.exists():
                errors.append(f'{p}: link not found: {href!r}')
    return e, errors


def feature_article(sid, fid, lang='ru'):
    f = FEATURES / sid / (f'{fid}.en.md' if lang == 'en' else f'{fid}.md')
    return f if f.is_file() else None


def check_feature(p, sid, it, ids):
    """An includes item with an "id" gets its own page: a mini-article and/or screenshots."""
    errors = []
    fid = it.get('id')
    if fid is None:
        if it.get('media'):
            errors.append(f'{p}: include "{it["name"]}" has media but no "id"')
        return errors
    q = f'{p} include {fid!r}'
    if not re.match(r'^[a-z0-9]+(-[a-z0-9]+)*$', str(fid)):
        return [f'{q}: id must be a lowercase slug']
    if fid in ids:
        errors.append(f'{q}: duplicate id')
    ids.add(fid)
    md = feature_article(sid, fid)
    if md is None and feature_article(sid, fid, 'en') is not None:
        errors.append(f'{q}: {fid}.en.md without the Russian {fid}.md')
    media = it.get('media') or []
    if not isinstance(media, list):
        return errors + [f'{q}: media must be a list']
    if md is None and not media:
        errors.append(f'{q}: needs docs/devlog/features/{sid}/{fid}.md or media (or drop the id)')
    if md is not None and len(md.read_text(encoding='utf-8').strip()) < 200:
        errors.append(f'{q}: the article is shorter than 200 characters; keep it in "text" instead')
    for i, m in enumerate(media):
        mq = f'{q} media[{i}]'
        if not isinstance(m, dict) or not (m.get('caption') or '').strip():
            errors.append(f'{mq}: needs "src" and "caption"')
            continue
        f = repo_file(m.get('src'))
        if f is None or not f.is_file():
            errors.append(f'{mq}: image not found: {m.get("src")!r}')
        elif f.suffix.lower() not in STILL_EXT:
            errors.append(f'{mq}: must be a PNG or JPG')
        elif f.stat().st_size > MAX_FRAME:
            errors.append(f'{mq}: larger than 6 MB; save a JPG')
    if md is not None:
        for ref in re.findall(r'!\[[^\]]*\]\(([^)\s]+)', md.read_text(encoding='utf-8')):
            if not re.match(r'^https?:', ref) and not (md.parent / ref).resolve().is_file():
                errors.append(f'{q}: article image not found: {ref}')
    return errors


def check_roadmap(p, items):
    """Roadmap items of a flagship or core system: {status, title, text?, date (done) | target (YYYY-MM or null)}."""
    errors = []
    for i, it in enumerate(items):
        q = f'{p} roadmap[{i}]'
        if not isinstance(it, dict) or not (it.get('title') or '').strip():
            errors.append(f'{q}: needs a title')
            continue
        st = it.get('status')
        if st not in ROADMAP_STATUS:
            errors.append(f'{q}: status must be one of {list(ROADMAP_STATUS)}')
        if st == 'done':
            try:
                dt.date.fromisoformat(it.get('date') or '')
            except ValueError:
                errors.append(f'{q}: a done item needs "date" YYYY-MM-DD')
        elif it.get('target') is not None and not re.match(r'^\d{4}-(0[1-9]|1[0-2])$', str(it['target'])):
            errors.append(f'{q}: target is YYYY-MM or null')
    return errors


def check_systems(systems):
    errors = []
    for s in systems.values():
        p = f'systems.json {s["id"]}'
        if s.get('doc') and not (ROOT / s['doc']).is_file():
            errors.append(f'{p}: doc not found: {s["doc"]}')
        if s.get('core'):
            for key in ('tagline', 'essence', 'taglineEn', 'essenceEn'):
                if not (s.get(key) or '').strip():
                    errors.append(f'{p}: core system needs "{key}"')
            for key in ('includes', 'enables', 'enablesEn'):
                if not isinstance(s.get(key), list) or not s[key]:
                    errors.append(f'{p}: core system needs a non-empty "{key}" list')
            if s.get('cover') and not (repo_file(s['cover']['src']) or ROOT / '_').is_file():
                errors.append(f'{p}: cover not found: {s["cover"]["src"]}')
            if s.get('roadmap') is not None and not isinstance(s['roadmap'], list):
                errors.append(f'{p}: "roadmap" must be a list')
            errors += check_roadmap(p, s.get('roadmap') or [])
            continue
        if not s.get('flagship'):
            continue
        for key in ('tagline', 'essence', 'coverBrief', 'where'):
            if not (s.get(key) or '').strip():
                errors.append(f'{p}: flagship system needs "{key}"')
        if s.get('where') not in WHERE:
            errors.append(f'{p}: where must be one of {list(WHERE)}')
        art = s.get('article')
        if art and not art.startswith('https://') and (repo_file(art) is None or not repo_file(art).is_file()):
            errors.append(f'{p}: article not found: {art!r}')
        try:
            dt.date.fromisoformat(s.get('started') or '')
        except ValueError:
            errors.append(f'{p}: "started" must be YYYY-MM-DD (first commit of the system)')
        for key in ('includes', 'enables', 'roadmap'):
            if not isinstance(s.get(key), list) or not s[key]:
                errors.append(f'{p}: flagship system needs a non-empty "{key}" list')
        ids = set()
        for it in s.get('includes') or []:
            if not (isinstance(it, dict) and (it.get('name') or '').strip() and (it.get('text') or '').strip()):
                errors.append(f'{p}: includes items are {{"name", "text"}}')
                continue
            errors += check_feature(p, s['id'], it, ids)
        errors += check_roadmap(p, s.get('roadmap') or [])
        if s.get('cover') is not None:
            errors += check_frame(f'{p} cover', s['cover'], MIN_COVER_W, ('src', 'caption', 'approved'))
            video = s['cover'].get('video')
            if video is not None:
                f = repo_file(video)
                if f is None or not f.is_file() or f.suffix.lower() not in VIDEO_EXT or f.stat().st_size > MAX_VIDEO:
                    errors.append(f'{p} cover: video must be an MP4/WebM in the repository, at most 15 MB: {video!r}')
    return errors


def check_showcase(systems, ids):
    data = json.loads(SHOWCASE.read_text(encoding='utf-8'))
    frames, candidates = data.get('frames'), data.get('candidates', [])
    if not isinstance(frames, list) or not frames:
        return ['showcase.json: "frames" must be a non-empty list']
    if not isinstance(candidates, list):
        return ['showcase.json: "candidates" must be a list']
    errors = []
    keys = ('src', 'title', 'caption', 'system', 'date', 'approved')
    for kind, items in (('frames', frames), ('candidates', candidates)):
        for i, fr in enumerate(items):
            p = f'showcase.json {kind}[{i}]'
            errors += check_frame(p, fr, MIN_SHOWCASE_W, keys)
            if fr.get('system') not in systems:
                errors.append(f'{p}: unknown system {fr.get("system")!r}')
            if fr.get('entry') and fr['entry'] not in ids:
                errors.append(f'{p}: devlog entry not found: {fr["entry"]}')
            if kind == 'candidates' and fr.get('approved'):
                errors.append(f'{p}: an approved frame belongs in "frames"')
    return errors


def check():
    systems, _ = load_systems()
    errors, entries, ids = check_systems(systems), [], set()
    for path in sorted(ENTRIES.glob('*.json')):
        e, errs = check_entry(path, systems)
        errors += errs
        if e:
            if e['id'] in ids:
                errors.append(f'{path.name}: duplicate id')
            ids.add(e['id'])
            entries.append(e)
    for path in ENTRIES.iterdir() if ENTRIES.is_dir() else []:
        if path.suffix != '.json':
            errors.append(f'entries/{path.name}: only <id>.json files belong here; media go to docs/devlog/media/<id>/')
    errors += check_showcase(systems, ids)
    errors += check_releases()
    return entries, errors


def load_releases():
    if not RELEASES.is_file():
        return {'releases': [], 'builds': {'index': '', 'branch': ''}}
    return json.loads(RELEASES.read_text(encoding='utf-8'))


def shipped_entries(rel_data):
    """Ids of devlog entries already in the latest release (present in its tagged commit).

    Entries written on the release day after the tag are new work, so the date alone cannot tell
    them apart. None when git or the tag is unavailable (then the page falls back to dates)."""
    rels = rel_data.get('releases') or []
    if not rels:
        return None
    ref = rels[0].get('tag') or rels[0].get('commit')
    try:
        out = subprocess.run(['git', 'ls-tree', '--name-only', ref, 'docs/devlog/entries/'],
                             cwd=ROOT, capture_output=True, text=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError):
        try:
            out = subprocess.run(['git', 'ls-tree', '--name-only', rels[0].get('commit', ''),
                                  'docs/devlog/entries/'], cwd=ROOT, capture_output=True, text=True,
                                 check=True).stdout
        except (OSError, subprocess.CalledProcessError):
            return None
    return {Path(p).stem for p in out.split() if p.endswith('.json')}


def check_releases():
    p = 'releases.json'
    try:
        data = load_releases()
    except ValueError as err:
        return [f'{p}: {err}']
    errors = []
    b = data.get('builds') or {}
    for k in ('index', 'branch'):
        if not str(b.get(k, '')).startswith('https://'):
            errors.append(f'{p}: builds.{k} must be an https:// URL')
    seen, prev = set(), None
    for i, r in enumerate(data.get('releases') or []):
        q = f'{p} releases[{i}]'
        for k in ('version', 'tag', 'date', 'commit', 'title', 'summary'):
            if not isinstance(r.get(k), str) or not r[k].strip():
                errors.append(f'{q}: "{k}" is required')
        if not re.match(r'^\d{4}-\d{2}-\d{2}$', str(r.get('date', ''))):
            errors.append(f'{q}: date must be YYYY-MM-DD')
        if not re.match(r'^[0-9a-f]{7,40}$', str(r.get('commit', ''))):
            errors.append(f'{q}: commit must be a hex SHA')
        if r.get('version') in seen:
            errors.append(f'{q}: duplicate version {r.get("version")}')
        seen.add(r.get('version'))
        if prev and str(r.get('date', '')) > prev:
            errors.append(f'{q}: releases go newest first')
        prev = str(r.get('date', ''))
        for j, h in enumerate(r.get('highlights') or []):
            if not h.get('title') or not h.get('text'):
                errors.append(f'{q} highlights[{j}]: "title" and "text" are required')
            for k, m in enumerate(h.get('media') or []):
                mq = f'{q} highlights[{j}] media[{k}]'
                f = repo_file(m.get('src')) if isinstance(m, dict) else None
                if f is None or not f.is_file():
                    errors.append(f'{mq}: image not found: {m.get("src") if isinstance(m, dict) else m!r}')
                elif f.suffix.lower() not in STILL_EXT:
                    errors.append(f'{mq}: must be a PNG or JPG')
                elif not (m.get('caption') or '').strip():
                    errors.append(f'{mq}: needs a caption')
    nxt = (data.get('next') or {}).get('version')
    if nxt is not None and (not isinstance(nxt, str) or nxt in seen):
        errors.append(f'{p}: next.version must be a version that is not released yet')
    try:
        _, systems = load_systems()
    except (OSError, ValueError, KeyError):
        systems = []
    for s in systems:
        r = s.get('release')
        if r is not None and r not in seen and r != nxt:
            errors.append(f'systems.json {s.get("id")}: release "{r}" is neither in releases nor next.version')
    return errors


from render import (Ctx, ROOT_LANG, MENU_SPY_JS, roadmap_lines, FEATURE_JS, DOCS_SEARCH_JS, doc_sections, GALLERY_JS, gallery_page, CATALOG_JS, catalog_page, MARK_LIGHT, RELEASES_JS, ROADMAP_JS, TRACK_JS, BLOG_JS, Converter, blog_page, devblog_page, feature_page, docs_index, doc_page, esc, localized_entries,  # noqa: E402
                    release_chip, releases_page, render_page, roadmap_page, showcase_html, system_page,
                    systems_html, core_html, fill_tokens)

LANGS = ('ru', 'en')
DOCS_MANIFEST = SITE / 'docs.json'


def cmd_check(_args):
    entries, errors = check()
    for err in errors:
        print('error:', err)
    print(f'{len(entries)} entries, {len(errors)} errors')
    return 1 if errors else 0


def slugify(text):
    text = re.sub(r'[^a-z0-9]+', '-', text.lower()).strip('-')
    return text[:48].strip('-') or 'update'


def cmd_add(args):
    systems, _ = load_systems()
    if args.system not in systems:
        sys.exit(f'unknown system {args.system}; known: {", ".join(systems)}')
    date = args.date or dt.date.today().isoformat()
    eid = f'{date}-{args.system}-{slugify(args.slug or args.title)}'
    target = ENTRIES / f'{eid}.json'
    if target.exists():
        sys.exit(f'{target.relative_to(ROOT)} already exists; edit it instead')
    media = []
    for i, src in enumerate(args.media or []):
        s = Path(src).resolve()
        if not s.is_file():
            sys.exit(f'media not found: {src}')
        try:
            rel = s.relative_to(ROOT)
        except ValueError:
            dest = MEDIA / eid / f'{i + 1:02d}{s.suffix.lower()}'
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(s, dest)
            rel = dest.relative_to(ROOT)
        media.append({'src': rel.as_posix(), 'caption': 'TODO: что на снимке'})
    entry = {
        'id': eid, 'date': date, 'system': args.system, 'title': args.title,
        'summary': 'TODO: одно-два предложения о том, что стало возможно',
        'body': ['TODO: как это работает и где лежит'],
        'status': args.status, 'verified': 'TODO: какие тесты и снимки реально запускались',
        'limits': None, 'branch': args.branch, 'commit': None,
        'media': media, 'links': [], 'author': args.author,
    }
    ENTRIES.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(entry, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(target.relative_to(ROOT))
    return 0


def check_docs():
    errors = []
    manifest = json.loads(DOCS_MANIFEST.read_text(encoding='utf-8'))
    ids = set()
    for sec in manifest.get('sections', []):
        for pg in sec.get('pages', []):
            if not re.match(r'^[a-z0-9-]+$', pg.get('id', '')) or pg['id'] in ids:
                errors.append(f'site/docs.json: bad or duplicate page id {pg.get("id")!r}')
            ids.add(pg.get('id'))
            f = repo_file(pg.get('source'))
            if f is None or not f.is_file():
                errors.append(f'site/docs.json: source not found: {pg.get("source")!r}')
    return manifest, errors


def cmd_build(args):
    entries, errors = check()
    manifest_docs, doc_errors = check_docs()
    errors += doc_errors
    if errors:
        for err in errors:
            print('error:', err)
        return 1
    _, systems = load_systems()
    by_id = {s['id']: s for s in systems}
    out = Path(args.out).resolve()
    if out == ROOT or (ROOT in out.parents and out.relative_to(ROOT).parts[0] not in ('build', 'dist')):
        sys.exit('--out must be outside the repository or under build/ or dist/')
    if out.exists():
        shutil.rmtree(out)
    shutil.copytree(SITE, out, ignore=shutil.ignore_patterns('manifest.json', 'docs.json', '_layout.html', 'i18n', '.DS_Store'))
    manifest = json.loads((SITE / 'manifest.json').read_text(encoding='utf-8'))
    copied = set()

    def copy_media(rel):
        if rel in copied:
            return
        f = repo_file(rel)
        if f is None or not f.is_file():
            sys.exit(f'site media not found: {rel}')
        dest = out / 'media' / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(f, dest)
        copied.add(rel)

    for rel in manifest.get('media', []):
        copy_media(rel)
    frame = json.loads(SHOWCASE.read_text(encoding='utf-8'))['frames'][0]
    copy_media(frame['src'])
    flagships = [s for s in systems if s.get('flagship')]
    paged = flagships + [s for s in systems if s.get('core')]
    for s in paged:
        if s.get('cover'):
            copy_media(s['cover']['src'])
            if s['cover'].get('video'):
                copy_media(s['cover']['video'])
        for it in s['includes']:
            for m in it.get('media') or []:
                copy_media(m['src'])
            if it.get('id'):
                it['article'] = bool(feature_article(s['id'], it['id']))
    entries.sort(key=lambda e: (e['date'], e['id']), reverse=True)
    for e in entries:
        for m in e['media']:
            copy_media(m['src'])
            m['src'] = 'media/' + m['src']
        for link in e.get('links') or []:
            if not link['href'].startswith('https://'):
                link['href'] = f'{manifest["repoBlob"]}/{link["href"]}'
    layout = (SITE / '_layout.html').read_text(encoding='utf-8')
    rel_data = load_releases()
    rel_data['_shipped'] = shipped_entries(rel_data)
    for r in rel_data.get('releases') or []:
        for h in r.get('highlights') or []:
            for m in h.get('media') or []:
                copy_media(m['src'])
    doc_pages = {p['source']: p['id'] for sec in manifest_docs['sections'] for p in sec['pages']}
    generated = dt.datetime.now(dt.timezone.utc).isoformat(timespec='seconds')
    pages = 0
    for lang in LANGS:
        T = json.loads((SITE / 'i18n' / f'{lang}.json').read_text(encoding='utf-8'))
        ctx = Ctx(lang, T, LANGS)
        ctx.rel = {'released': {r['version'] for r in rel_data.get('releases') or []},
                   'next': (rel_data.get('next') or {}).get('version', '')}
        d = out if lang == ROOT_LANG else out / lang
        d.mkdir(parents=True, exist_ok=True)
        loc_entries = localized_entries(entries, ctx)
        # static pages per language: site/<page>.html (English, the root) and site/<lang>/<page>.html
        for name in ('index.html', 'devlog.html'):
            f = d / name
            if not f.is_file():
                sys.exit(f'site page missing: {f.relative_to(out)}')
            text = fill_tokens(f.read_text(encoding='utf-8'), ctx, name)
            # One menu for every page: the static pages take the header of _layout.html, so a new
            # menu item (gallery, blog, ...) cannot go missing from them.
            head_re = re.compile(r'<header class="top">.*?</header>', re.S)
            shared = head_re.search(fill_tokens(layout, ctx, name)).group(0)
            nav_id = 'devlog' if name == 'devlog.html' else None
            if nav_id:
                shared = shared.replace(f'data-nav="{nav_id}"', f'data-nav="{nav_id}" aria-current="page"')
            text, n = head_re.subn(lambda _m: shared, text, count=1)
            if n != 1:
                sys.exit(f'{lang}/{name} lost its <header class="top">')
            if name == 'index.html':
                # The main page is one long page: the menu item of the section in view is current.
                text = text.replace('</body>', MENU_SPY_JS + '</body>', 1)
            if name == 'index.html':
                for mark in ('<!--SHOWCASE-->', '<!--SYSTEMS-->', '<!--CORE_SYSTEMS-->', '<!--ROADMAP_LINES-->'):
                    if mark not in text:
                        sys.exit(f'{lang}/index.html lost the {mark} placeholder')
                text = text.replace('<!--SHOWCASE-->', showcase_html(frame, by_id, ctx))
                text = text.replace('<!--SYSTEMS-->', systems_html(systems, ctx))
                text = text.replace('<!--CORE_SYSTEMS-->', core_html(systems, ctx))
                text = text.replace('<!--ROADMAP_LINES-->', roadmap_lines(flagships, ctx))
                text = text.replace('<!--RELEASE_CHIP-->', release_chip(rel_data['releases'], ctx))
            f.write_text(text, encoding='utf-8')
        for s in paged:
            body = system_page(s, paged, loc_entries, ctx, manifest['repoBlob'], doc_pages)
            (d / f'system-{s["id"]}.html').write_text(render_page(
                layout, ctx, f'system-{s["id"]}.html', f'{ctx.loc(s, "name")} — {ctx.loc(s, "tagline")} · AIVERSE',
                ctx.loc(s, 'essence'), body, nav='systems', script=TRACK_JS), encoding='utf-8')
            pages += 1
            for it in s['includes']:
                if not (it.get('id') and (it.get('article') or it.get('media'))):
                    continue
                md = feature_article(s['id'], it['id'], lang) or feature_article(s['id'], it['id'])
                body_html = ''
                if md is not None:
                    text = md.read_text(encoding='utf-8')
                    rel_dir = md.parent.relative_to(ROOT).as_posix()

                    def image(href, rel_dir=rel_dir):
                        if re.match(r'^https?:', href):
                            return href
                        rel = posixpath.normpath(posixpath.join(rel_dir, href))
                        copy_media(rel)
                        return ctx.media(rel)

                    def link(href, rel_dir=rel_dir):
                        if re.match(r'^(https?:|mailto:|#)', href) or href.endswith('.html'):
                            return href
                        path, _, anchor = href.partition('#')
                        target = posixpath.normpath(posixpath.join(rel_dir, path))
                        tail = f'#{anchor}' if anchor else ''
                        if target in doc_pages:
                            return f'docs-{doc_pages[target]}.html{tail}'
                        return f'{manifest["repoBlob"]}/{target}{tail}'
                    body_html = Converter(link, image).convert(text)
                    if lang == 'en' and md.name.endswith('.md') and not md.name.endswith('.en.md'):
                        body_html = f'<p class="doc-note">{esc(T["incRuOnly"])}</p>' + body_html
                name = f'system-{s["id"]}-{it["id"]}.html'
                (d / name).write_text(render_page(
                    layout, ctx, name, f'{ctx.loc(it, "name")} — {s["name"]} · AIVERSE', ctx.loc(it, 'text'),
                    feature_page(s, it, body_html, ctx), nav='systems', script=FEATURE_JS), encoding='utf-8')
                pages += 1
        flag_ids = {s['id'] for s in flagships}
        blog = [e for e in loc_entries if e['system'] not in flag_ids]
        (d / 'blog.html').write_text(render_page(
            layout, ctx, 'blog.html', f'{T["blogHubTitle"]} · AIVERSE', T['blogHubLead'],
            blog_page(blog, systems, ctx), nav='blog', script=FEATURE_JS + BLOG_JS), encoding='utf-8')
        pages += 1
        for s in systems:
            mine = [e for e in loc_entries if e['system'] == s['id']]
            if not mine:
                continue
            name = f'devblog-{s["id"]}.html'
            (d / name).write_text(render_page(
                layout, ctx, name, f'{T["blogTitle"]}: {s["name"]} · AIVERSE', T['blogLead'].format(s=s['name']),
                devblog_page(s, mine, ctx, s['id'] in {p['id'] for p in paged}), nav='devlog', script=FEATURE_JS), encoding='utf-8')
            pages += 1
        (d / 'gallery.html').write_text(render_page(
            layout, ctx, 'gallery.html', f'{T["galTitle"]} · AIVERSE', T['galLead'],
            gallery_page(loc_entries, systems, ctx), nav='gallery', script=FEATURE_JS + GALLERY_JS), encoding='utf-8')
        pages += 1
        (d / 'roadmap.html').write_text(render_page(layout, ctx, 'roadmap.html', T['rmPageTitle'], T['rmDesc'],
                                                    roadmap_page(flagships + [s for s in systems if s.get('core') and s.get('roadmap')], ctx), nav='roadmap', script=ROADMAP_JS),
                                        encoding='utf-8')
        rel_cfg = {'index': rel_data['builds']['index'], 'none': T['relDlNone'],
                   'os': {'windows': 'Windows', 'macos': 'macOS', 'linux': 'Linux'}}
        rel_script = f'<script>window.REL_CFG={json.dumps(rel_cfg, ensure_ascii=False)};</script>' + RELEASES_JS
        (d / 'releases.html').write_text(render_page(
            layout, ctx, 'releases.html', T['relPageTitle'], T['relLead'],
            releases_page(rel_data, loc_entries, systems, ctx, manifest['repo']), nav='releases', script=rel_script),
            encoding='utf-8')
        cat = manifest['catalog']
        cat_cfg = {'index': cat['index'], 'lang': lang, 't': T['catJs'], 'cats': T['catNames'], 'kinds': T['catKinds']}
        cat_script = f'<script>window.CAT_CFG={json.dumps(cat_cfg, ensure_ascii=False)};</script>' + CATALOG_JS
        (d / 'catalog.html').write_text(render_page(layout, ctx, 'catalog.html', T['catPageTitle'], T['catLead'],
                                                    catalog_page(cat, ctx), nav='catalog', script=cat_script),
                                        encoding='utf-8')
        ds_script = (f'<script>window.DS_L={json.dumps({"none": T["docsSearchNone"]}, ensure_ascii=False)};</script>'
                     + DOCS_SEARCH_JS)
        (d / 'docs.html').write_text(render_page(layout, ctx, 'docs.html', T['docsPageTitle'], T['docsLead'],
                                                 docs_index(manifest_docs, ctx), nav='docs', script=ds_script), encoding='utf-8')
        search_index = []
        for sec in manifest_docs['sections']:
            for pg in sec['pages']:
                body, heads = doc_page(pg, manifest_docs, ctx, ROOT, manifest['repo'], doc_pages)
                title = ctx.loc(pg, 'title')
                art = re.search(r'<article class="doc"[^>]*>(.*)</article>', body, re.S)
                search_index += doc_sections(pg['id'], title, art.group(1) if art else body)
                (d / f'docs-{pg["id"]}.html').write_text(render_page(
                    layout, ctx, f'docs-{pg["id"]}.html', f'{title} — AIVERSE', ctx.loc(pg, 'lead') or title,
                    body, nav='docs', main_class=' docs-main', script=ds_script), encoding='utf-8')
                pages += 1
        (d / 'docs-search.json').write_text(json.dumps(search_index, ensure_ascii=False, separators=(',', ':')),
                                            encoding='utf-8')
        public = [{'id': s['id'], 'name': ctx.loc(s, 'name')} for s in systems]
        data = {'generated': generated, 'lang': lang, 'repo': manifest['repo'], 'systems': public,
                'entries': loc_entries}
        (d / 'devlog.json').write_text(json.dumps(data, ensure_ascii=False, indent=1), encoding='utf-8')
        pages += 6
    (out / '.nojekyll').write_text('', encoding='utf-8')
    # English moved from en/ to the site root: old en/<page> links redirect to the same page.
    legacy = out / 'en'
    legacy.mkdir(exist_ok=True)
    for page in sorted(out.glob('*.html')):
        (legacy / page.name).write_text(
            f'<!doctype html><meta charset="utf-8"><title>AIVERSE</title>'
            f'<link rel="canonical" href="../{page.name}">'
            f'<meta http-equiv="refresh" content="0; url=../{page.name}">'
            f'<script>location.replace("../{page.name}"+location.hash)</script>', encoding='utf-8')
    # Cache busting: the CDN and browsers keep style.css / devlog.js for minutes, so a fresh page
    # could meet a stale stylesheet. Every page asks for the exact version it was built with.
    for asset in ('style.css', 'devlog.js'):
        f = out / asset
        if f.is_file():
            v = hashlib.sha1(f.read_bytes()).hexdigest()[:10]
            for page in out.rglob('*.html'):
                t = page.read_text(encoding='utf-8')
                t2 = re.sub(r'(href|src)="((?:\.\./)?)' + re.escape(asset) + r'(\?v=[0-9a-f]+)?"', rf'\1="\2{asset}?v={v}"', t)
                if t2 != t:
                    page.write_text(t2, encoding='utf-8')
    print(f'{out}: {len(LANGS)} languages, {pages} pages, {len(entries)} entries, {len(copied)} media files')
    return 0


def git(*args, capture=True, env=None):
    r = subprocess.run(['git', *args], cwd=ROOT, capture_output=capture, text=True, env=env)
    if r.returncode != 0:
        raise SystemExit(f'git {" ".join(args)} failed: {(r.stderr or "").strip()}')
    return (r.stdout or '').strip()


def cmd_publish(args):
    """Publish the site of origin/main to gh-pages (the Site workflow is not installed: this is the way).

    Refuses when the checkout is not exactly origin/main or has uncommitted site sources, so the
    published pages always match main. One commit on top of gh-pages, nothing else is touched."""
    git('fetch', '-q', 'origin', '+refs/heads/main:refs/remotes/origin/main',
        '+refs/heads/gh-pages:refs/remotes/origin/gh-pages')
    head, main_tip = git('rev-parse', 'HEAD'), git('rev-parse', 'origin/main')
    if head != main_tip:
        print(f'publish: HEAD {head[:9]} is not origin/main {main_tip[:9]}. Merge and push main first '
              f'(git push origin HEAD:main), then check out origin/main and run publish again.')
        return 1
    dirty = git('status', '--porcelain', '--', 'site', 'docs', 'tools/site', 'README.md')
    if dirty:
        print('publish: uncommitted site sources (commit them to main first):\n' + dirty)
        return 1
    if cmd_check(args):
        return 1
    out = Path(tempfile.mkdtemp(prefix='aiverse-site-'))
    try:
        b = argparse.Namespace(out=str(out))
        if cmd_build(b):
            return 1
        index = out.parent / (out.name + '.index')
        env = dict(os.environ, GIT_INDEX_FILE=str(index))
        git('read-tree', 'origin/gh-pages', env=env)
        git(f'--work-tree={out}', 'add', '--sparse', '-A', env=env)
        tree = git('write-tree', env=env)
        if tree == git('rev-parse', 'origin/gh-pages^{tree}'):
            print('publish: gh-pages already matches main, nothing to push')
            return 0
        commit = git('commit-tree', tree, '-p', 'origin/gh-pages', '-m', f'Site: publish main {main_tip[:9]}')
        if args.dry_run:
            print(f'publish (dry run): would push {commit[:9]} to gh-pages')
            return 0
        git('push', '-q', 'origin', f'{commit}:refs/heads/gh-pages', capture=False)
        print(f'publish: gh-pages {commit[:9]} pushed. Pages deploys in ~1 min: '
              f'gh run list -R chromealex/AIVERSE --workflow pages-build-deployment -L 1')
        return 0
    finally:
        shutil.rmtree(out, ignore_errors=True)
        (out.parent / (out.name + '.index')).unlink(missing_ok=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    sub.add_parser('check').set_defaults(fn=cmd_check)
    a = sub.add_parser('add')
    a.add_argument('--system', required=True)
    a.add_argument('--title', required=True)
    a.add_argument('--slug')
    a.add_argument('--date')
    a.add_argument('--status', default='in-progress', choices=STATUSES)
    a.add_argument('--branch')
    a.add_argument('--author')
    a.add_argument('--media', nargs='*')
    a.set_defaults(fn=cmd_add)
    b = sub.add_parser('build')
    b.add_argument('--out', default=str(ROOT / 'build/site'))
    b.set_defaults(fn=cmd_build)
    pb = sub.add_parser('publish')
    pb.add_argument('--dry-run', action='store_true')
    pb.set_defaults(fn=cmd_publish)
    args = ap.parse_args()
    return args.fn(args)


if __name__ == '__main__':
    sys.exit(main())
