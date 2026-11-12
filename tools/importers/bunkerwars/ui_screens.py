#!/usr/bin/env python3
"""Bunker Wars battle screens (UI.Windows 2.x / MW2 era) -> RmlUi screens, through tools/importers/unity_ui.

The unity_ui importer (agent-ui-import) reads UI.Windows 4 screens: `layouts.items[].windowLayout` +
`components[].component`, layout elements with `tagId`. Bunker Wars ships the older schema:

    layouts: {types: [Horizontal, Vertical], layouts: [{enabled, layoutResource: {guid, resourcePath},
              layoutPreferences, components: [{tag, componentResource: {guid, resourcePath}}]}]}
    WindowLayoutElement {tag, comment}

This module adapts that schema in memory (nothing in unity_ui is changed) and runs the importer on the
battle screens:

    python3 tools/importers/bunkerwars/ui_screens.py --source <bunker-wars> --out <game folder> [--only HUD] [--json]

Screens (folders under MW2Data/ResourcesScreens/MW2, see BATTLE_DIRS): Gameplay (HUD, SuddenDeath,
SuddenDeathCounter, TheEndCampaign; TheEndGame* hold no screens in the client), GameplayTutorial (tutorial windows,
finger tips, hints), BattlePreparing, and the battle windows kept with the meta screens: campaign Victory /
Defeat, multiplayer EndMatch / EndMatchResults, the in-battle PopupMenu, tutorial wait / tip / highlight windows.
Only the landscape ("Horizontal") layout is converted: BW plays in landscape only. Console / tvOS / notch
variants are skipped. The other meta screens (Menu, Mobile, Other) belong to the meta port (agent-bw-meta).
"""
import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
UI = os.path.join(os.path.dirname(HERE), 'unity_ui')
sys.path.insert(0, UI)
sys.path.insert(1, os.path.dirname(HERE))

import importer  # noqa: E402
import windows  # noqa: E402
from lib.unity.unity_yaml import get  # noqa: E402

_W = 'MW2Data/ResourcesScreens/MW2/'
BATTLE_DIRS = [_W + d for d in (
    'Gameplay', 'GameplayTutorial', 'BattlePreparing',
    # battle windows that live with the meta screens: results, in-battle menu, tutorial windows
    'Mobile/Campaign/Victory', 'Mobile/Campaign/Defeat', 'Mobile/Multiplayer/EndMatch', 'Mobile/Multiplayer/EndMatchResults',
    'Menu/PopupMenu', 'Other/TutorialWaitForClick', 'Other/SkillTipWindow', 'Other/TextTipWindow', 'Mobile/TutorialHighlight')]
# platform variants of the same window (consoles / tvOS / notch phones): the desktop + mobile ones are enough
SKIP_SUFFIXES = ('PS4', 'PS4-2', 'Switch', 'Switch2', 'TVOS', 'TVOS2', 'XboxOne', 'Consoles')


def _old_layouts(data):
    lay = data.get('layouts') if isinstance(data, dict) else None
    return lay.get('layouts') if isinstance(lay, dict) and isinstance(lay.get('layouts'), list) and 'items' not in lay else None


def adapt(data):
    """UI.Windows 2 screen data -> the UI.Windows 4 keys the converter reads (landscape layout only)."""
    old = _old_layouts(data)
    if old is None:
        return False
    types = get(data, 'layouts', 'types', default=[]) or []
    items = []
    for i, L in enumerate(old):
        if not isinstance(L, dict) or str(L.get('enabled', 1)) in ('0', 'False'):
            continue
        if i < len(types) and str(types[i]) == 'Vertical':
            continue
        res = L.get('layoutResource') or {}
        comps = []
        for c in L.get('components') or []:
            cr = c.get('componentResource') or {}
            comps.append({'tag': c.get('tag'), 'component': {'guid': cr.get('guid') or get(cr, 'directRef', 'guid')}})
        items.append({'windowLayout': {'guid': res.get('guid') or get(res, 'directRef', 'guid')},
                      'layoutPreferences': L.get('layoutPreferences') or {}, 'components': comps})
        break
    data['layouts']['items'] = items
    return True


def adapt_prefab(pf):
    for d in pf.docs:
        if not isinstance(d.data, dict):
            continue
        adapt(d.data)
        # WindowLayoutElement 2.x: `tag` (4.x: `tagId`); recognised by its `containsLayouts` / `comment` keys
        if 'tag' in d.data and 'tagId' not in d.data and 'containsLayouts' in d.data:
            d.data['tagId'] = d.data['tag']
    return pf


class BwBuilder(windows.Builder):
    def prefab(self, path):
        return adapt_prefab(super().prefab(path))


def find_screens(dirs, builder):
    out = []
    for d in dirs:
        for dp, dns, fns in os.walk(d):
            dns[:] = sorted(dns)
            for fn in sorted(fns):
                if not fn.endswith('.prefab') or os.path.basename(dp) != 'Screens':
                    continue
                stem = fn[:-7]
                if any(stem.endswith(s) for s in SKIP_SUFFIXES) or 'IPhoneX' in stem or 'iPhoneX' in stem:
                    continue
                p = os.path.join(dp, fn)
                with open(p, 'r', encoding='utf-8', errors='replace') as f:
                    text = f.read()
                if 'layoutResource:' in text and builder.is_screen(p):
                    out.append(p)
    return out


def run(source, out, only=(), copy=True, extra=()):
    """extra: more screen prefabs (absolute paths), e.g. the per-map tutorial windows the tutorial data shows."""
    src = importer.assets_dir(source)
    if os.path.isdir(os.path.join(src, 'Client', 'Assets')):
        src = os.path.join(src, 'Client', 'Assets')
    importer.Builder = BwBuilder
    dirs = [os.path.join(src, d) for d in BATTLE_DIRS if os.path.isdir(os.path.join(src, d))]
    orig = importer.find_screens
    importer.find_screens = lambda _dirs, b: find_screens(dirs, b) + [p for p in extra if b.is_screen(p)]
    try:
        # Bunker Wars has no Unity Localization tables (texts come from GameData / I2): l10n off
        return importer.run(src, out, dirs, (), only, l10n=False, copy=copy)
    finally:
        importer.find_screens = orig


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--source', required=True, help='Bunker Wars repository (or Client / Client/Assets)')
    ap.add_argument('--out', required=True, help='game folder')
    ap.add_argument('--only', action='append', default=[], help='only these screens')
    ap.add_argument('--no-copy', action='store_true')
    ap.add_argument('--json', action='store_true')
    a = ap.parse_args()
    res = run(a.source, a.out, a.only, not a.no_copy)
    if a.json:
        print(json.dumps(res, ensure_ascii=False, indent=1))
    else:
        for s in res['screens']:
            print('screen %-28s %2d blocks %3d controls  unsupported: %s' % (
                s['name'], len(s['blocks']), s['controls'],
                ', '.join('%s x%d' % (k, v['count']) for k, v in s['issues']['unsupported'].items()) or '-'))
        print('textures: %d, fonts: %d' % (res.get('textures', 0), len(res.get('fonts', []))))
    return 0 if res['ok'] else 2


if __name__ == '__main__':
    sys.exit(main())
