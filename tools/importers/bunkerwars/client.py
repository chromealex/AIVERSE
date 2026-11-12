#!/usr/bin/env python3
"""Bunker Wars client layer of a game folder made by import.py: battle screens, HUD, tutorials, audio, pads.

    python3 tools/importers/bunkerwars/client.py --source <bunker-wars> --out <game folder> [--no-copy] [--json]

Run after import.py (it writes game.json / rules.json / maps / lang); every step is additive and re-runnable:

  1. UI screens (ui_screens.py over tools/importers/unity_ui): BW's UI.Windows battle screens -> ui/screens, ui/ugui,
     ui/img, ui/fonts; then
       HUDScreen               -> ui/hud/bw_hud.rml (script UI document, logic ui/logic/bw_hud.lua from the kit)
       tutorial windows        -> ui/hud/<BW screen prefab>.rml (the names the tutorial data uses), clickable ones
                                  (TapToContinue) send bw_tut_click / bw_tut_window
       SuddenDeath(Counter)    -> ui/hud/bw_suddendeath(counter).rml
       results / pause menu    stay in ui/screens (victory, defeat, endmatch, endmatchresults, popupmenuscreenmobile,
                                  campaignend): the meta port (agent-bw-meta) wires them to its screens.json
  2. Texts: TextComponent.textLocalizationKey of the screens' prefabs -> "@bw.ui.<key>" text nodes; lang/{ru,en}.json
     get the UI.Windows localization table value (LocalizationSettings.asset), else the text authored in the prefab
     (BW's NewTutorial texts ship in the prefabs only; the live table comes from the CDN, l10n.csv)
  3. Tutorial time: the GameplayTimeScale / SetBattleTimeScale window modules and HintsScreen's pause ->
     scripts/bw/tutorial_time.lua (the tutorial runtime tweens the engine variable "gameplay_scale")
  4. Tutorials: tutorial.py --kit --rules (battle tutorial data per map, the kit runtime, hidden commands);
     tutorial maps of the game (maps/<id>.json with the same id) get their map script and the tutorial view
  5. Audio: tools/importers/fmod/import_audio.py (FMODChannels bank -> audio.json, audio/)
  6. rules.json "scripts" / "viewScripts" += the HUD scripts; ui/skin.rcss hides the engine HUD parts BW draws
     itself; game.json "gamepad": {"scheme": "bunkerwars"} (ClientGamepad: BW InputConfig layout)
Art, texts and sounds stay external content of the user's BW copy (never committed with the template).
"""
import argparse
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(1, os.path.dirname(HERE))

import bwloc  # noqa: E402
import ui_screens  # noqa: E402
from lib.unity.unity_yaml import parse_documents  # noqa: E402

TUTORIAL_PARTS = ('/GameplayTutorial/', '/TutorialWaitForClick/', '/TutorialHighlight/')
HUD_RENAME = {'hud': 'bw_hud', 'suddendeath': 'bw_suddendeath', 'suddendeathcounter': 'bw_suddendeathcounter'}
TAP_KEYS = ('newtutorial|common|taptocontinue',)

# engine HUD parts Bunker Wars draws itself (engine/assets/ui/hud.rml ids); the pause / result modal stays
SKIN = '''/* bunkerwars client.py: BW's own HUD (ui/hud/bw_hud.rml) replaces these engine HUD parts */
#topwrap, #tools, #card, #minimap, #dispatch-badge { display: none; }
'''


def strip_comments(text):
    return re.sub(r'^\s*//.*$', '', text, flags=re.M)


def read_json(path, default=None):
    if not os.path.exists(path):
        return default
    with open(path, encoding='utf-8') as f:
        return json.loads(strip_comments(f.read()))


def write_json(path, value, header=None):
    os.makedirs(os.path.dirname(path) or '.', exist_ok=True)
    with open(path, 'w', encoding='utf-8') as f:
        if header:
            f.write(header.rstrip('\n') + '\n')
        json.dump(value, f, ensure_ascii=False, indent=1)
        f.write('\n')


def first_comment(path):
    if not os.path.exists(path):
        return None
    with open(path, encoding='utf-8') as f:
        line = f.readline()
    return line if line.lstrip().startswith('//') else None


# ------------------------------------------------------------------ prefabs: texts and window modules
def prefab_docs(path, cache={}):
    if path not in cache:
        try:
            with open(path, encoding='utf-8', errors='replace') as f:
                cache[path] = parse_documents(f.read())
        except Exception:
            cache[path] = []
    return cache[path]


def text_keys(prefabs):
    """(authored text, key) of every TextComponent with a textLocalizationKey in these prefabs."""
    out = {}
    for p in prefabs:
        docs = prefab_docs(p)
        by_id = {d.file_id: d for d in docs}
        for d in docs:
            data = d.data if isinstance(d.data, dict) else None
            key = ((data or {}).get('textLocalizationKey') or {}).get('key') if data else None
            if not key:
                continue
            t = by_id.get(((data.get('text') or {}).get('fileID')))
            txt = None
            if t is not None and isinstance(t.data, dict):
                txt = t.data.get('m_Text', t.data.get('m_text'))
            if isinstance(txt, str) and txt.strip():
                out.setdefault(txt.strip(), str(key).strip())
    return out


def window_times(assets, screens):
    """Tutorial window prefab stem -> {scale, dur} from its GameplayTimeScale / SetBattleTimeScale modules."""
    mods = {}
    for dp, _, fns in os.walk(os.path.join(assets, 'MW2Data', 'ResourcesScreens', 'MW2', 'GameplayTutorial', 'Modules')):
        for fn in fns:
            if not fn.endswith('.prefab'):
                continue
            p = os.path.join(dp, fn)
            meta = p + '.meta'
            if not os.path.exists(meta):
                continue
            g = re.search(r'^guid:\s*([0-9a-f]{32})', open(meta, encoding='utf-8', errors='replace').read(), re.M)
            for d in prefab_docs(p):
                data = d.data if isinstance(d.data, dict) else {}
                if 'targetTimeScale' in data:
                    mods[g.group(1)] = {'scale': float(data.get('targetTimeScale') or 0), 'dur': float(data.get('switchScaleDuration') or 0)}
                elif 'timeScale' in data and 'group' in data and int(data.get('group') or 0) == 1:   # ModuleGroup.Gameplay
                    mods[g.group(1)] = {'scale': float(data.get('timeScale') or 0), 'dur': 0.0}
    out = {}
    for p in screens:
        stem = os.path.splitext(os.path.basename(p))[0]
        if stem == 'HintsScreen':   # HintsScreen.OnShowBegin: Battle.Pause(forced)
            out[stem] = {'scale': 0.0, 'dur': 0.0}
            continue
        with open(p, encoding='utf-8', errors='replace') as f:
            text = f.read()
        for g in re.findall(r'moduleSourceResource:\s*\n\s*guid: ([0-9a-f]{32})', text):
            if g in mods:
                out[stem] = mods[g]
    return out


def lua_num(v):
    v = round(float(v), 4)
    return str(int(v)) if v == int(v) else 'fx("%s")' % ('%g' % v)


# ------------------------------------------------------------------ RML transforms
def to_hud(rml, screen, tutorial, clickable):
    rml = rml.replace('data-model="meta"', 'data-model="script"', 1)
    rml = re.sub(r'u-screen="[^"]*"', 'u-screen="%s"' % screen, rml, count=1)
    body_style = 'body.u-body { pointer-events: none; } .u-button, [u-kind] { pointer-events: auto; }'
    if clickable:
        body_style = 'body.u-body { pointer-events: auto; }'
    rml = rml.replace('</head>', '    <style>%s</style>\n</head>' % body_style, 1)
    screens = 'playing' if tutorial else 'playing paused'
    rml = rml.replace('<body class="u-body"', '<body class="u-body" screens="%s"' % screens, 1)
    if clickable:
        ev = "send('bw_tut_click'); send('bw_tut_window', {window = '%s'})" % screen.replace("'", '')
        rml = rml.replace('<div class="root" data-model="script">', '<div class="root" data-model="script" data-event-click="%s">' % ev, 1)
    return rml


_TEXT = re.compile(r'(<div class="u-text"[^>]*>)([^<]+)(</div>)')
_EFFECTS = re.compile(r'font-effect: ([^;"]+);')


def one_font_effect(rml):
    """RmlUi takes one font effect per declaration: keep the outline (else the first) of 'shadow(..), outline(..)'."""
    def sub(m):
        parts = re.findall(r'\w+\([^()]*(?:\([^()]*\)[^()]*)*\)', m.group(1))
        if len(parts) <= 1:
            return m.group(0)
        pick = next((x for x in parts if x.startswith('outline')), parts[0])
        return 'font-effect: %s;' % pick
    return _EFFECTS.sub(sub, rml)


def element_span(rml, el_id):
    """(start, end) of the element <div id="el_id" ...>...</div> (balanced divs), or None."""
    i = rml.find('<div id="%s"' % el_id)
    if i < 0:
        return None
    depth, pos = 0, i
    for m in re.compile(r'<div\b[^>]*?(/?)>|</div>').finditer(rml, i):
        if m.group(0).startswith('</div'):
            depth -= 1
        elif not m.group(1):
            depth += 1
        if depth == 0:
            return i, m.end()
    return None


def replicate(rml, el_id, n, inflow=False):
    """n copies of a template element right after it, ids el_id-1..n (BW pooled list rows: players, skill cards)."""
    span = element_span(rml, el_id)
    if not span:
        return rml
    a, b = span
    tpl = rml[a:b]
    copies = []
    for k in range(1, n + 1):
        c = tpl.replace('<div id="%s"' % el_id, '<div id="%s-%d"' % (el_id, k), 1)
        if inflow:
            c = re.sub(r'(<div id="%s-%d"[^>]*style=")[^"]*"' % (re.escape(el_id), k),
                       r'\1position: relative; flex-shrink: 0; width: 97dp; height: 134dp; margin-right: 21dp; display: none;"', c, count=1)
        copies.append(c)
    return rml[:b] + '\n' + '\n'.join(copies) + rml[b:]


def move_into(rml, el_ids, parent_id):
    """Moves elements (already in the document) to the end of another element's children."""
    for el_id in el_ids:
        span = element_span(rml, el_id)
        if not span:
            continue
        block = rml[span[0]:span[1]]
        rml = rml[:span[0]] + rml[span[1]:]
        p = element_span(rml, parent_id)
        if not p:
            return rml
        end = p[1] - len('</div>')
        rml = rml[:end] + block + '\n' + rml[end:]
    return rml


def hud_pools(rml):
    """BW HUD lists filled at run time: PlayersBar rows (8 players) and HeroSkillsComponent cards (5 slots)."""
    rml = replicate(rml, 'playersbar-player', 8)
    rml = replicate(rml, 'skillspanel1player-skill', 5, inflow=True)
    return move_into(rml, ['skillspanel1player-skill-%d' % k for k in range(1, 6)], 'skillspanel1player-content-3')


def key_texts(rml, text2key, table, lang):
    used = []

    def sub(m):
        txt = m.group(2).strip()
        key = text2key.get(txt)
        if not key:
            return m.group(0)
        ek = bwloc.engine_key(key)
        row = table.get(key.lower()) or {}
        lang['ru'].setdefault(ek, row.get('ru') or txt)
        if row.get('en'):
            lang['en'].setdefault(ek, row['en'])
        used.append(key)
        return m.group(1) + '@' + ek + m.group(3)
    return _TEXT.sub(sub, rml), used


def merge_lang(out, lang):
    for code, rows in lang.items():
        if not rows:
            continue
        p = os.path.join(out, 'lang', code + '.json')
        cur = read_json(p, {}) or {}
        for k, v in rows.items():
            cur.setdefault(k, v)
        write_json(p, cur)


def merge_list(obj, key, items):
    cur = obj.get(key)
    cur = list(cur) if isinstance(cur, list) else []
    for i in items:
        if i not in cur:
            cur.append(i)
    obj[key] = cur


# ------------------------------------------------------------------ steps
def find_prefabs(assets, stems):
    """Screen prefabs by file stem anywhere under Assets (the tutorial data names its windows by prefab)."""
    want, found = set(stems), {}
    for dp, dns, fns in os.walk(assets):
        dns[:] = [d for d in dns if d not in ('Library', 'Temp', 'Plugins', 'StreamingAssets', 'Editor')]
        for fn in fns:
            if fn.endswith('.prefab') and fn[:-7] in want and fn[:-7] not in found:
                found[fn[:-7]] = os.path.join(dp, fn)
    return found


def tutorial_windows(out):
    names = set()
    d = os.path.join(out, 'scripts', 'bw', 'tutorial_data')
    if os.path.isdir(d):
        for fn in os.listdir(d):
            with open(os.path.join(d, fn), encoding='utf-8') as f:
                names.update(re.findall(r'window = "([^"]+)"', f.read()))
    return names


def install_screens(assets, out, copy, report, windows=()):
    extra = find_prefabs(assets, [w for w in windows if w != 'HUDScreen'])
    res = ui_screens.run(assets, out, copy=copy, extra=sorted(extra.values()))
    extra_paths = {os.path.normpath(p) for p in extra.values()}
    table = bwloc.read_table(assets)
    prefabs = []
    for d in ui_screens.BATTLE_DIRS + ['MW2Data/Components/Resources/CurUI/Gameplay', 'MW2Data/Components/Resources/UI/Gameplay']:
        for dp, _, fns in os.walk(os.path.join(assets, d)):
            prefabs += [os.path.join(dp, f) for f in fns if f.endswith('.prefab')]
    text2key = text_keys(sorted(prefabs))
    lang = {'ru': {}, 'en': {}}
    tutorial_screens, placed = [], {}
    for s in res['screens']:
        src = os.path.join(os.path.dirname(assets), s['source'])
        name = s['name']
        doc = os.path.join(out, 'ui', 'screens', name + '.rml')
        with open(doc, encoding='utf-8') as f:
            rml = f.read()
        rml, used = key_texts(rml, text2key, table, lang)
        rml = one_font_effect(rml)
        if name == 'hud':
            rml = hud_pools(rml)
        tutorial = any(t in src.replace('\\', '/') for t in TUTORIAL_PARTS) or os.path.normpath(src) in extra_paths
        if tutorial:
            tutorial_screens.append(src)
            target = os.path.splitext(os.path.basename(src))[0]
        else:
            target = HUD_RENAME.get(name)
        if target is None:
            with open(doc, 'w', encoding='utf-8') as f:
                f.write(rml)
            placed[name] = 'ui/screens/%s.rml' % name
            continue
        clickable = tutorial and any(k.lower() in TAP_KEYS for k in used)
        dst = os.path.join(out, 'ui', 'hud', target + '.rml')
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, 'w', encoding='utf-8') as f:
            f.write(to_hud(rml, target, tutorial, clickable))
        os.remove(doc)
        placed[name] = 'ui/hud/%s.rml' % target
    merge_lang(out, lang)
    times = window_times(assets, tutorial_screens)
    p = os.path.join(out, 'scripts', 'bw', 'tutorial_time.lua')
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, 'w', encoding='utf-8') as f:
        f.write('-- tutorial windows that change the gameplay time scale on show (tools/importers/bunkerwars/client.py):\n'
                '-- BW GameplayTimeScale / SetBattleTimeScale window modules, HintsScreen pauses the battle\nreturn {\n')
        for k in sorted(times):
            f.write('  [%s] = {scale = %s, dur = %s},\n' % (json.dumps(k), lua_num(times[k]['scale']), lua_num(times[k]['dur'])))
        f.write('}\n')
    hud = os.path.join(out, 'ui', 'hud')
    own = {w: w for w in windows if os.path.exists(os.path.join(hud, w + '.rml'))}
    if 'HUDScreen' in windows:
        own['HUDScreen'] = 'bw_hud'   # the battle HUD is always open (scripts/view/bw/hud_view.lua)
    p = os.path.join(out, 'scripts', 'view', 'bw', 'tutorial_windows.lua')
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, 'w', encoding='utf-8') as f:
        f.write('-- tutorial windows -> their ui/hud document (tools/importers/bunkerwars/client.py)\nreturn {\n')
        for k in sorted(own):
            f.write('  [%s] = %s,\n' % (json.dumps(k), json.dumps(own[k])))
        f.write('}\n')
    report['tutorialWindows'] = {'used': len(windows), 'converted': len(own), 'missing': sorted(set(windows) - set(own))[:30]}
    report.update({'screens': placed, 'texts': len(lang['ru']), 'textsEn': len(lang['en']), 'windowTimes': times,
                   'textures': res.get('textures', 0), 'importWarnings': res.get('warnings', [])[:20]})


def install_tutorials(assets, out, report):
    import tutorial
    rules = os.path.join(out, 'rules.json')
    argv = ['--unity', assets, '--out', out, '--kit', '--json'] + (['--rules', rules] if os.path.exists(rules) else [])
    maps = os.path.join('ME.TexturesGenerator', '_Common', 'Maps')
    if os.path.isdir(os.path.join(assets, maps)):
        argv += ['--maps', maps]   # where BW keeps the map data assets (a walk of all Assets takes minutes)
    import io
    import contextlib
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        tutorial.main(argv)
    try:
        rep = json.loads(buf.getvalue().strip().splitlines()[-1])
    except (ValueError, IndexError):
        rep = {}
    wired = wire_tutorial_maps(out, rep.get('maps', []))
    report['tutorials'] = {'maps': len(rep.get('maps', [])), 'meta': rep.get('meta', 0), 'wiredMaps': wired}


def wire_tutorial_maps(out, mids):
    """Tutorial maps the game has (maps/<id>.json): their map script and the tutorial view."""
    wired = []
    for mid in mids:
        mp = os.path.join(out, 'maps', mid + '.json')
        m = read_json(mp)
        if not isinstance(m, dict) or not os.path.exists(os.path.join(out, 'scripts', 'bw', 'tut_%s.lua' % mid)):
            continue
        merge_list(m, 'scripts', ['scripts/bw/tut_%s.lua' % mid])
        merge_list(m, 'viewScripts', ['scripts/view/bw/tutorial_view.lua'])
        write_json(mp, m, first_comment(mp))
        wired.append(mid)
    return wired


def install_audio(assets, out, copy, report):
    sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'fmod'))
    import import_audio
    import io
    import contextlib
    argv = ['--unity', assets, '--out', out, '--json'] + ([] if copy else ['--no-copy'])
    buf = io.StringIO()
    try:
        with contextlib.redirect_stdout(buf):
            import_audio.main(argv)
        report['audio'] = json.loads(buf.getvalue().strip().splitlines()[-1]).get('counts', 'ok')
    except Exception as e:   # no FMODChannels in this copy: the game keeps its audio
        report['audio'] = 'skipped: %s' % e


def wire(out, report):
    rp = os.path.join(out, 'rules.json')
    rules = read_json(rp)
    if isinstance(rules, dict):
        merge_list(rules, 'scripts', ['scripts/bw/hud.lua'])
        merge_list(rules, 'viewScripts', ['scripts/view/bw/hud_view.lua'])
        write_json(rp, rules, first_comment(rp))
    skin = os.path.join(out, 'ui', 'skin.rcss')
    cur = open(skin, encoding='utf-8').read() if os.path.exists(skin) else ''
    if 'bunkerwars client.py' not in cur:
        os.makedirs(os.path.dirname(skin), exist_ok=True)
        with open(skin, 'w', encoding='utf-8') as f:
            f.write(cur + ('\n' if cur and not cur.endswith('\n') else '') + SKIN)
    gp = os.path.join(out, 'game.json')
    game = read_json(gp)
    if isinstance(game, dict):
        pad = game.get('gamepad') if isinstance(game.get('gamepad'), dict) else {}
        pad.setdefault('scheme', 'bunkerwars')
        pad.setdefault('snap', {'targets': 'structures', 'radius': 80, 'flick': True})
        game['gamepad'] = pad
        write_json(gp, game, first_comment(gp))
    report['wired'] = True


def run(source, out, copy=True, audio=True):
    assets = ui_screens.importer.assets_dir(source)
    if os.path.isdir(os.path.join(assets, 'Client', 'Assets')):
        assets = os.path.join(assets, 'Client', 'Assets')
    out = os.path.abspath(out)
    report = {}
    install_tutorials(assets, out, report)   # first: the tutorial data names the windows to convert
    install_screens(assets, out, copy, report, sorted(tutorial_windows(out)))
    if audio:
        install_audio(assets, out, copy, report)
    wire(out, report)
    return report


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--source', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--no-copy', action='store_true')
    ap.add_argument('--no-audio', action='store_true')
    ap.add_argument('--json', action='store_true')
    a = ap.parse_args()
    rep = run(a.source, a.out, not a.no_copy, not a.no_audio)
    if a.json:
        print(json.dumps(rep, ensure_ascii=False, indent=1))
    else:
        print('screens: %d (%s)' % (len(rep['screens']), ', '.join('%s -> %s' % kv for kv in sorted(rep['screens'].items())[:8])))
        print('texts: %d keyed (%d with English)' % (rep['texts'], rep['textsEn']))
        print('tutorial windows with a time scale: %s' % ', '.join(sorted(rep['windowTimes'])))
        print('tutorials: %s' % rep.get('tutorials'))
        print('audio: %s' % rep.get('audio'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
