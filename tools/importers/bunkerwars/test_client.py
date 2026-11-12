#!/usr/bin/env python3
"""Tests of the Bunker Wars client layer helpers (no BW project needed): UI.Windows 2.x adaptation, RML pools,
font effects, BW localization table, window time scales."""
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import bwloc  # noqa: E402
import client  # noqa: E402
import ui_screens  # noqa: E402


def test_adapt_old_layouts():
    data = {'layouts': {'types': ['Horizontal', 'Vertical'], 'layouts': [
        {'enabled': 1, 'layoutResource': {'guid': 'aa'}, 'layoutPreferences': {'guid': 'pp'},
         'components': [{'tag': 3, 'componentResource': {'guid': 'cc'}}, {'tag': 4, 'componentResource': {'guid': None}}]},
        {'enabled': 1, 'layoutResource': {'guid': 'vv'}, 'components': []}]}}
    assert ui_screens.adapt(data)
    items = data['layouts']['items']
    assert len(items) == 1 and items[0]['windowLayout'] == {'guid': 'aa'}, items   # landscape only
    assert items[0]['components'][0] == {'tag': 3, 'component': {'guid': 'cc'}}
    assert not ui_screens.adapt(data), 'already adapted'


def test_rml_helpers():
    rml = ('<div id="list" style="display: flex;"><div id="row" u-src="Row" style="position: absolute; left: 0%;">'
           '<div id="row-text" class="u-text" style="font-effect: shadow(0dp 1dp rgba(0, 0, 0, 255)), outline(1dp rgba(0, 0, 0, 255));">7</div>'
           '</div></div><div id="other"></div>')
    assert client.element_span(rml, 'row') == (rml.index('<div id="row"'), rml.index('</div></div><div id="other"') + 6)
    out = client.replicate(rml, 'row', 3, inflow=True)
    for k in (1, 2, 3):
        assert '<div id="row-%d"' % k in out
    assert out.count('position: relative; flex-shrink: 0') == 3
    moved = client.move_into(out, ['row-1'], 'other')
    a, b = client.element_span(moved, 'other')
    assert '<div id="row-1"' in moved[a:b]
    fx = client.one_font_effect(rml)
    assert 'font-effect: outline(1dp rgba(0, 0, 0, 255));' in fx and 'shadow(' not in fx


def test_texts_and_hud_doc():
    table = {'a|b': {'ru': 'Привет', 'en': 'Hello'}}
    lang = {'ru': {}, 'en': {}}
    rml, used = client.key_texts('<div class="u-text" style="x">Привет</div><div class="u-text">Тап</div>',
                                 {'Привет': 'A|B', 'Тап': 'NewTutorial|Common|TapToContinue'}, table, lang)
    assert '@bw.ui.A.B' in rml and '@bw.ui.NewTutorial.Common.TapToContinue' in rml
    assert lang['ru']['bw.ui.A.B'] == 'Привет' and lang['en']['bw.ui.A.B'] == 'Hello'
    assert lang['ru']['bw.ui.NewTutorial.Common.TapToContinue'] == 'Тап' and 'bw.ui.NewTutorial.Common.TapToContinue' not in lang['en']
    doc = ('<head></head><body class="u-body" onload="x"><div class="root" data-model="meta">'
           '<div id="ugui-canvas" u-screen="step1"></div></div></body>')
    hud = client.to_hud(doc, "Step1_Screen", True, True)
    assert 'data-model="script"' in hud and 'u-screen="Step1_Screen"' in hud and 'screens="playing"' in hud
    assert "send('bw_tut_click')" in hud and 'pointer-events: auto' in hud
    assert 'screens="playing paused"' in client.to_hud(doc, 'bw_hud', False, False)


def test_localization_table():
    text = ('  data:\n    values:\n    - enabled: 0\n      key: 0\n      values: []\n'
            '    - enabled: 1\n      key: 10\n      values:\n      - Hello\n      - "Two\n        lines \\u0410"\n'
            '    - enabled: 1\n      key: 30\n      values:\n      - "\\u041F\\u0440\\u0438\\u0432\\u0435\\u0442"\n      - \'it\'\'s\'\n'
            '    keys:\n    - Greet|Hi\n    - Other\n    keyHashes: 00\n')
    table, keys = bwloc.parse_settings(text)
    assert keys == ['Greet|Hi', 'Other'], keys
    assert table['greet|hi'] == {'en': 'Hello', 'ru': 'Привет'}, table
    assert table['other'] == {'en': 'Two lines А', 'ru': "it's"}, table
    assert bwloc.engine_key('NewTutorial|Tutor1|Text 2') == 'bw.ui.NewTutorial.Tutor1.Text.2'


def test_lua_numbers_and_wiring():
    assert client.lua_num(1.0) == '1' and client.lua_num(0.1) == 'fx("0.1")'
    with tempfile.TemporaryDirectory() as d:
        os.makedirs(os.path.join(d, 'maps'))
        os.makedirs(os.path.join(d, 'scripts', 'bw'))
        open(os.path.join(d, 'maps', 'm1.json'), 'w').write('// map\n{"size": [4, 4]}\n')
        open(os.path.join(d, 'scripts', 'bw', 'tut_m1.lua'), 'w').write('-- x\n')
        assert client.wire_tutorial_maps(d, ['m1', 'm2']) == ['m1']
        m = client.read_json(os.path.join(d, 'maps', 'm1.json'))
        assert m['scripts'] == ['scripts/bw/tut_m1.lua'] and m['viewScripts'] == ['scripts/view/bw/tutorial_view.lua']
        assert open(os.path.join(d, 'maps', 'm1.json')).readline().startswith('// map')
        open(os.path.join(d, 'rules.json'), 'w').write('{"scripts": ["a.lua"]}')
        open(os.path.join(d, 'game.json'), 'w').write('{"name": "x"}')
        client.wire(d, {})
        client.wire(d, {})   # re-runnable
        r = client.read_json(os.path.join(d, 'rules.json'))
        assert r['scripts'] == ['a.lua', 'scripts/bw/hud.lua'] and r['viewScripts'] == ['scripts/view/bw/hud_view.lua']
        assert client.read_json(os.path.join(d, 'game.json'))['gamepad']['scheme'] == 'bunkerwars'
        assert open(os.path.join(d, 'ui', 'skin.rcss')).read().count('#topwrap') == 1


if __name__ == '__main__':
    for name, fn in sorted(globals().items()):
        if name.startswith('test_') and callable(fn):
            fn()
    print('bw client layer: ok')
