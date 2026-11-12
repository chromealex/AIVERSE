"""Unity uGUI / UI.Windows screens and Unity Localization tables -> AIVERSE game UI (RmlUi + Lua).

    python3 tools/importers/unity_ui/import.py --source <UnityProject | Assets folder> --out <game folder>
        [--screens <folder under Assets> ...] [--prefab <uGUI prefab> ...] [--only <name> ...]
        [--no-l10n] [--no-screens] [--key-prefix <p>] [--no-copy] [--json]

Writes into the game folder:
    ui/screens/<name>.rml       one document per UI.Windows screen (or uGUI prefab), slots + ids for Lua
    ui/ugui/{ugui,sprites}.rcss  shared styles, @spritesheet of every used texture (sprites, 9-slice)
    ui/ugui/runtime.lua         CanvasScaler + safe area + fonts + click routing (regenerated)
    ui/ugui/config.lua          fonts to load (regenerated)
    ui/ugui/screens/<name>.json slots, blocks, controls, logic components, localization keys of a screen
    ui/ugui/report.{json,md}    what was converted, approximated and not supported, per screen
    ui/logic/<name>.lua         logic stub per screen (written once, never overwritten)
    ui/img/*, ui/fonts/*        copied textures and TrueType fonts (external content of the source game)
    lang/<tag>.json             string tables;  game.json gets "languages" / "sourceLanguage"
Docs: docs/unity-ui-import.md."""
import json
import os
import re
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(1, os.path.dirname(HERE))

from lib.unity import unity_scene  # noqa: E402
from lib.unity.unity_scene import Assets, Report, project_roots  # noqa: E402

from l10n import Tables  # noqa: E402
from media import Media, ident  # noqa: E402
from scripts import Scripts  # noqa: E402
from windows import Builder  # noqa: E402

unity_scene.MAX_NESTING = 8   # UI prefabs nest deeper than effects


def assets_dir(source):
    s = os.path.abspath(source)
    if os.path.isdir(os.path.join(s, 'Assets')):
        return os.path.join(s, 'Assets')
    return s


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', encoding='utf-8') as f:
        f.write(text)


def find_screens(dirs, builder):
    out = []
    for d in dirs:
        for dp, dns, fns in os.walk(d):
            dns[:] = sorted(x for x in dns if x not in ('Library', 'Temp', '.git'))
            for fn in sorted(fns):
                if not fn.endswith('.prefab'):
                    continue
                p = os.path.join(dp, fn)
                try:
                    with open(p, 'r', encoding='utf-8', errors='replace') as f:
                        text = f.read()
                except OSError:
                    continue
                if 'layoutPreferences:' in text and 'windowLayout:' in text and builder.is_screen(p):
                    out.append(p)
    return out


def screen_name(path, taken):
    stem = os.path.splitext(os.path.basename(path))[0]
    base = ident(re.sub(r'Screen$', '', stem) or stem, 's')
    name = base
    if name in taken:
        parts = path.replace('\\', '/').split('/')
        group = parts[-3] if len(parts) >= 3 and parts[-2] == 'Screens' else parts[-2]
        name = ident(group + '-' + base, 's')
    k = 1
    while name in taken:
        k += 1
        name = '%s-%d' % (base, k)
    taken.add(name)
    return name


def rml_document(doc):
    s = doc.scaler
    c = doc.body
    c.attrs.update({'u-screen': doc.name, 'u-scale-mode': str(s['mode']), 'u-ref-w': '%g' % s['ref'][0],
                    'u-ref-h': '%g' % s['ref'][1], 'u-match': '%g' % s['match'], 'u-screen-match': str(s['screenMatch'])})
    c.style.update({'width': '%gdp' % s['ref'][0], 'height': '%gdp' % s['ref'][1]})
    head = ['<rml>', '<head>', '    <title>%s</title>' % doc.name,
            '    <link type="text/rcss" href="../ugui/ugui.rcss"/>', '    <link type="text/rcss" href="../ugui/sprites.rcss"/>']
    if doc.css:
        head += ['    <style>'] + ['        ' + r for r in doc.css] + ['    </style>']
    head += ['    <script>require(\'ugui.runtime\')</script>', '</head>',
             '<body class="u-body" onload="require(\'ugui.runtime\').mount(document)">',
             '<!-- converted from %s by tools/importers/unity_ui: edit ui/logic/%s.lua for behaviour -->' % (
                 os.path.basename(doc.source), doc.name),
             '<div class="root" data-model="meta">']
    body = c.render(1)
    return '\n'.join(head + body + ['</div>', '</body>', '</rml>', ''])


def logic_stub(doc):
    lines = ['-- Logic of the %s screen (converted from %s). Written once by the UI importer; yours to edit.' % (
        doc.name, os.path.basename(doc.source)),
        "-- Elements: document:GetElementById(id). Controls and blocks of this screen: ui/ugui/screens/%s.json." % doc.name,
        'local M = {on = {}}', '', '-- called when the document is loaded', 'function M.mount(document)', 'end', '']
    buttons = [s for s in doc.slots if s['kind'] in ('button', 'toggle')]
    if buttons:
        lines.append('-- buttons / toggles: M.on[id] = function(element, event) ... end')
        for b in buttons[:200]:
            lines.append("-- M.on['%s'] = function(element) end   -- %s" % (b['id'], b['source']))
        lines.append('')
    lines.append('return M')
    return '\n'.join(lines) + '\n'


def base_rcss(family, scaler):
    fam = ('    font-family: %s;\n' % family) if family else ''
    return ('/* generated by tools/importers/unity_ui: base styles of converted uGUI screens */\n'
            'body.u-body { display: block; position: absolute; left: 0; top: 0; right: 0; bottom: 0; margin: 0; padding: 0;\n'
            '%s    font-size: 24dp; color: #ffffff; }\n'
            'div { display: block; }\n'
            '.root { position: absolute; left: 0; top: 0; right: 0; bottom: 0; }\n'
            '.u-canvas { position: absolute; left: 0; top: 0; transform-origin: left top; overflow: hidden; }\n'
            '.u-text { display: block; }\n'
            '.u-disabled { opacity: 0.6; }\n' % fam)


def report_md(rep_json):
    out = ['# Unity UI import report', '']
    l = rep_json.get('localization')
    if l:
        out += ['Languages: %s (source `%s`); collections: %s.' % (', '.join(l['languages']), l['sourceLanguage'], ', '.join(l['collections'])), '']
        for code, st in sorted(l['stats'].items()):
            out.append('- `%s`: %d strings, %d plural, %d empty (fall back to the source language)' % (code, st['entries'], st['plural'], st['empty']))
        out.append('')
    for s in rep_json.get('screens', []):
        out += ['## %s' % s['name'], '', 'Source `%s`; layouts %s; %d blocks, %d controls, %d logic components, %d texts with keys.' % (
            s['source'], ', '.join(s['layouts']) or '-', len(s['blocks']), s['controls'], len(s['logic']), len(s['issues']['localizationKeys'])), '']
        for title, k in (('Not supported', 'unsupported'), ('Approximate', 'approximate')):
            items = s['issues'][k]
            if items:
                out.append('%s:' % title)
                for what, v in items.items():
                    out.append('- %s x%d (e.g. `%s`)' % (what, v['count'], v['examples'][0] if v['examples'] else ''))
                out.append('')
        if s['logic']:
            out.append('Logic components (need Lua): ' + ', '.join(sorted({x['component'] for x in s['logic']})))
            out.append('')
    for title, k in (('Warnings', 'warnings'), ('Notes', 'notes')):
        if rep_json.get(k):
            out += ['## ' + title, ''] + ['- ' + x for x in rep_json[k]] + ['']
    return '\n'.join(out)


def update_manifest(out, langinfo):
    path = os.path.join(out, 'game.json')
    if not os.path.exists(path):
        return False
    with open(path, 'r', encoding='utf-8') as f:
        text = f.read()
    try:
        m = json.loads(text)
    except ValueError:
        return False    # a manifest with comments: leave it, the report lists the languages
    langs = list(m.get('languages') or [])
    for c in langinfo['languages']:
        if c not in langs:
            langs.append(c)
    m['languages'] = langs
    m.setdefault('sourceLanguage', langinfo['sourceLanguage'])
    write(path, json.dumps(m, ensure_ascii=False, indent=2) + '\n')
    return True


def run(source, out, screens_dirs=(), prefabs=(), only=(), l10n=True, do_screens=True, key_prefix='', copy=True):
    src = assets_dir(source)
    if not os.path.isdir(src):
        raise SystemExit('source not found: %s' % source)
    out = os.path.abspath(out)
    proj = os.path.dirname(src) if os.path.basename(src) == 'Assets' else src
    if out == proj or out.startswith(proj + os.sep) or proj.startswith(out + os.sep):
        raise SystemExit('the output folder must not overlap the Unity project')
    rep = Report()
    roots = project_roots(src) if os.path.basename(src) != 'Assets' else [src]
    assets = Assets(roots, rep)
    scripts = Scripts(roots)
    tables = Tables(assets, roots, rep, key_prefix) if l10n else None
    result = {'source': src, 'out': out, 'screens': []}
    if tables is not None and tables.tables:
        result['localization'] = tables.write(out)
        result['manifestUpdated'] = update_manifest(out, result['localization'])
    media = Media(assets, out, rep, copy_files=copy)
    if do_screens:
        b = Builder(assets, scripts, media, tables, rep)
        dirs = [os.path.join(src, d) if not os.path.isabs(d) else d for d in screens_dirs] or [src]
        paths = find_screens(dirs, b) + [os.path.join(src, p) if not os.path.isabs(p) else p for p in prefabs]
        taken = set()
        for p in paths:
            name = screen_name(p, taken)
            if only and name not in only and os.path.splitext(os.path.basename(p))[0] not in only:
                continue
            doc = b.screen(p, name)
            write(os.path.join(out, 'ui', 'screens', name + '.rml'), rml_document(doc))
            stub = os.path.join(out, 'ui', 'logic', name + '.lua')
            if not os.path.exists(stub):
                write(stub, logic_stub(doc))
            info = {'name': name, 'source': os.path.relpath(p, os.path.dirname(src)), 'layouts': doc.layouts, 'blocks': doc.blocks,
                    'scaler': {'mode': doc.scaler['mode'], 'reference': list(doc.scaler['ref']), 'match': doc.scaler['match'],
                               'screenMatch': doc.scaler['screenMatch']},
                    'controls': sum(1 for s in doc.slots if s['kind'] != 'component'), 'logic': doc.iss.logic,
                    'issues': doc.iss.as_json()}
            write(os.path.join(out, 'ui', 'ugui', 'screens', name + '.json'),
                  json.dumps(dict(info, slots=doc.slots), ensure_ascii=False, indent=1) + '\n')
            result['screens'].append(info)
        # fonts: the most used family becomes the default of converted screens
        fams = {}
        for f in media.fonts.values():
            if f['family']:
                fams[f['family']] = fams.get(f['family'], 0) + 1
        main = max(sorted(fams), key=lambda k: fams[k]) if fams else ''
        write(os.path.join(out, 'ui', 'ugui', 'ugui.rcss'), base_rcss(main, None))
        write(os.path.join(out, 'ui', 'ugui', 'sprites.rcss'), '/* generated: sprites of the converted screens */\n' + media.spritesheets())
        fonts = sorted({'ui/' + f for f in media.font_files.values()})
        write(os.path.join(out, 'ui', 'ugui', 'config.lua'), '-- generated by tools/importers/unity_ui\nreturn {fonts = {%s}}\n' % (
            ', '.join("'%s'" % f for f in fonts)))
        shutil.copyfile(os.path.join(HERE, 'runtime', 'runtime.lua'), os.path.join(out, 'ui', 'ugui', 'runtime.lua'))
        result['fonts'] = [{'file': 'ui/' + f['file'] if f['file'] else None, 'family': f['family'], 'asset': f['name']}
                           for f in media.fonts.values()]
        result['textures'] = sum(1 for t in media.textures.values() if t)
    result.update({'approximate': rep.approx, 'warnings': rep.warnings, 'notes': rep.notes,
                   'ok': not any(w.startswith('error:') for w in rep.warnings)})
    write(os.path.join(out, 'ui', 'ugui', 'report.json'), json.dumps(result, ensure_ascii=False, indent=1) + '\n')
    write(os.path.join(out, 'ui', 'ugui', 'report.md'), report_md(result))
    ign = os.path.join(out, '.rpakignore')
    lines = []
    if os.path.exists(ign):
        with open(ign, encoding='utf-8') as f:
            lines = f.read().splitlines()
    for pat in ('ui/ugui/report.json', 'ui/ugui/report.md', 'ui/ugui/screens/*.json', 'ui/ugui/l10n-keys.json'):
        if pat not in lines:
            lines.append(pat)
    write(ign, '\n'.join(lines) + '\n')
    return result
