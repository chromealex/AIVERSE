#!/usr/bin/env python3
"""Tests of the Unity UI / localization importer on a generated synthetic project (fixture.py).
Run: python3 tools/importers/unity_ui/test_import.py
With AIVERSE_UI_IMPORT_CLIENT=<AiverseGame> (and a display) the converted screen is also rendered by the client."""
import json
import os
import re
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(1, os.path.dirname(HERE))
import fixture  # noqa: E402
import importer  # noqa: E402
import l10n  # noqa: E402
import richtext  # noqa: E402
from media import image_size, ttf_family  # noqa: E402
from rect import Rect  # noqa: E402

ROOT = os.path.normpath(os.path.join(HERE, '..', '..', '..'))
FONT = os.path.join(ROOT, 'engine', 'assets', 'fonts', 'ui.ttf')


def rt(amin, amax, pos, size, pivot):
    return Rect({'m_AnchorMin': {'x': amin[0], 'y': amin[1]}, 'm_AnchorMax': {'x': amax[0], 'y': amax[1]},
                 'm_AnchoredPosition': {'x': pos[0], 'y': pos[1]}, 'm_SizeDelta': {'x': size[0], 'y': size[1]},
                 'm_Pivot': {'x': pivot[0], 'y': pivot[1]}})


class RectTests(unittest.TestCase):
    def test_point_anchor(self):
        # anchored to the bottom-right corner, pivot bottom-right, 40 px in from both edges
        b = rt((1, 0), (1, 0), (-40, 40), (200, 100), (1, 0)).box()
        self.assertEqual((b['left'], b['width'], b['margin-left']), ('100%', '200dp', '-240dp'))
        self.assertEqual((b['top'], b['height'], b['margin-top']), ('100%', '100dp', '-140dp'))

    def test_stretch(self):
        # stretched with offsets: left 10, right 20, top 30, bottom 40
        b = rt((0, 0), (1, 1), (-5, 5), (-30, -70), (0.5, 0.5)).box()
        self.assertEqual((b['left'], b['right'], b['margin-left'], b['margin-right']), ('0%', '0%', '10dp', '20dp'))
        self.assertEqual((b['top'], b['bottom'], b['margin-top'], b['margin-bottom']), ('0%', '0%', '30dp', '40dp'))

    def test_partial_anchors_and_transform(self):
        r = rt((0.25, 0.5), (0.75, 0.5), (0, 0), (0, 80), (0.5, 0.5))
        r.rot, r.scale = 90.0, (2.0, 2.0)
        b = r.box()
        self.assertEqual((b['left'], b['right'], b['top'], b['height'], b['margin-top']), ('25%', '25%', '50%', '80dp', '-40dp'))
        self.assertEqual(b['transform'], 'rotate(-90deg) scale(2, 2)')


class TextTests(unittest.TestCase):
    def test_rich_text(self):
        dropped = set()
        out = richtext.to_rml('A <b>B</b> <color=#ff000080>C</color><br><size=20>D</size> <sprite=3> & <wave>E</wave>', dropped=dropped)
        self.assertEqual(out, 'A <span style="font-weight: bold;">B</span> <span style="color: rgba(255, 0, 0, 128);">C</span><br/>'
                              '<span style="font-size: 20dp;">D</span>  &amp; E')
        self.assertEqual(dropped, {'<sprite>', '<wave>', '</wave>'})

    def test_smart_strings(self):
        c = l10n.Converter()
        self.assertEqual(c.convert('Lv.{0} of {1}', 'k', False), 'Lv.{arg0} of {arg1}')
        self.assertEqual(c.convert('{0:plural:one key|{0} keys}', 'k', True), {'one': 'one key', 'other': '{arg0} keys'})
        self.assertEqual(c.convert('{0:p:a|b|c}', 'k', True), {'one': 'a', 'few': 'b', 'many': 'c', 'other': 'c'})
        self.assertEqual(c.convert('{name} has {0:N0}', 'k', True), '{name} has {arg0}')
        self.assertEqual(c.convert('{0:cond:yes|no}', 'k', True), 'yes')
        self.assertEqual(len(c.issues), 2)

    def test_font_family_and_image_size(self):
        self.assertEqual(ttf_family(FONT), 'noto sans')
        with tempfile.TemporaryDirectory() as t:
            fixture.png(os.path.join(t, 'a.png'), 7, 5)
            self.assertEqual(image_size(os.path.join(t, 'a.png')), (7, 5))


class ImportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.src = os.path.join(cls.tmp.name, 'Unity')
        cls.out = os.path.join(cls.tmp.name, 'game')
        fixture.build(cls.src, FONT)
        os.makedirs(cls.out)
        with open(os.path.join(cls.out, 'game.json'), 'w') as f:
            json.dump({'name': 'Fixture', 'module': 'rts', 'languages': ['en']}, f)
        cls.res = importer.run(cls.src, cls.out, prefabs=['UI/Plain/Popup.prefab'])
        cls.shop = cls.read('ui/screens/shop.rml')

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    @classmethod
    def read(cls, rel):
        with open(os.path.join(cls.out, rel), encoding='utf-8') as f:
            return f.read()

    def screen(self, name):
        return next(s for s in self.res['screens'] if s['name'] == name)

    def test_screens_found(self):
        self.assertEqual(sorted(s['name'] for s in self.res['screens']), ['popup', 'shop'])
        s = self.screen('shop')
        self.assertEqual(s['scaler'], {'mode': 1, 'reference': [1080.0, 1920.0], 'match': 1.0, 'screenMatch': 0})
        self.assertEqual(sorted(b['slot'] for b in s['blocks']), ['body', 'footer', 'header'])

    def test_slots_and_blocks(self):
        self.assertIn('u-slot="header"', self.shop)
        self.assertIn('class="u-safe"', self.shop)
        self.assertIn('id="ugui-canvas" class="u-canvas" u-screen="shop" u-scale-mode="1" u-ref-w="1080" u-ref-h="1920" u-match="1"',
                      self.shop)
        # the footer slot: bottom-centre anchor, pivot bottom, 40 up, 600x160
        self.assertRegex(self.shop, r'id="footer".*left: 50%; width: 600dp; margin-left: -300dp; top: 100%; height: 160dp; margin-top: -200dp;')
        self.assertIn('@UI.Shop.Title', self.shop)

    def test_graphics(self):
        # 9-slice: border (l 8, b 6, r 8, t 10) -> edges top right bottom left; tint; pressed tint rule
        self.assertIn('decorator: ninepatch(panel, panel-in, 10dp 8dp 6dp 8dp)', self.shop)
        self.assertIn('image-color: rgba(51, 153, 255, 255)', self.shop)
        self.assertIn('#footer-click:active #footer-buybutton { image-color: rgba(26, 76, 128, 255); }', self.shop)
        sheets = self.read('ui/ugui/sprites.rcss')
        self.assertIn('panel-in: 8px 10px 16px 16px;', sheets)
        self.assertIn('gem: 32px 0px 32px 24px;', sheets)     # bottom-left Unity rect -> top-left
        self.assertTrue(os.path.exists(os.path.join(self.out, 'ui', 'img', 'panel.png')))

    def test_text(self):
        self.assertIn('font-family: noto sans', self.shop)
        self.assertIn('font-size: 48dp', self.shop)
        self.assertIn('Buy <span style="font-weight: bold;">now</span>', self.shop)
        self.assertIn('outline(2dp rgba(0, 0, 0, 255))', self.shop)           # uGUI Outline effect
        self.assertIn("return {fonts = {'ui/fonts/main.ttf'}}", self.read('ui/ugui/config.lua'))

    def test_controls_list_scroll(self):
        self.assertIn('u-kind="button" onclick="require(\'ugui.runtime\').click(element, event)"', self.shop)
        self.assertRegex(self.shop, r'id="body-viewport"[^>]*overflow-x: auto; overflow-y: hidden;')
        self.assertRegex(self.shop, r'id="body-content"[^>]*position: relative; width: auto; height: 300dp;')
        self.assertRegex(self.shop, r'id="body-content"[^>]*flex-direction: row')
        self.assertIn('u-template="body-itemlist-row"', self.shop)
        self.assertRegex(self.shop, r'id="row-row"[^>]*position: relative; width: 120dp; height: 80dp; decorator: image\(gem fill\)')
        popup = self.read('ui/screens/popup.rml')
        self.assertRegex(popup, r'u-kind="toggle"[^>]*u-on="false" u-check-id="check"')
        self.assertRegex(popup, r'id="check"[^>]*visibility: hidden;')

    def test_report(self):
        iss = self.screen('shop')['issues']
        self.assertIn('component mapped to a missing layout slot', iss['unsupported'])
        self.assertIn('rich text tag <sprite>', iss['approximate'])
        self.assertEqual(iss['localizationKeys'], ['UI.Shop.Title'])
        # Coins (tag 5) has no slot: its logic component and unknown script are not part of the screen
        report = self.read('ui/ugui/report.md')
        self.assertIn('## shop', report)
        slots = json.loads(self.read('ui/ugui/screens/shop.json'))['slots']
        self.assertIn({'id': 'footer-click', 'kind': 'button', 'source': 'BuyButton/Click'}, slots)

    def test_unsupported_and_logic(self):
        # the Coins block in a plain prefab: logic component, unknown script, Image subclass, CanvasGroup, inactive child
        out = os.path.join(self.tmp.name, 'coins')
        res = importer.run(self.src, out, prefabs=['UI/Components/Coins.prefab'], l10n=False, screens_dirs=['UI/Plain'])
        s = next(x for x in res['screens'] if x['name'] == 'coins')
        self.assertEqual(s['issues']['unsupported'], {'SparkleEffect': {'count': 1, 'examples': ['Coins']}})
        self.assertIn('FancyImage drawn as Image', s['issues']['approximate'])
        self.assertEqual([x['component'] for x in s['logic']], ['CoinsCounterComponent'])
        with open(os.path.join(out, 'ui', 'screens', 'coins.rml'), encoding='utf-8') as f:
            rml = f.read()
        self.assertIn('opacity: 0.5', rml)
        self.assertIn('u-format="x{0}"', rml)
        self.assertRegex(rml, r'id="glow"[^>]*u-display="block"[^>]*display: none;')

    def test_localization(self):
        en = json.loads(self.read('lang/en.json'))
        ru = json.loads(self.read('lang/ru.json'))
        self.assertEqual(en['@language'], 'English')
        self.assertEqual(en['UI.Keys'], {'one': 'NEED {arg0} KEY', 'other': 'NEED {arg0} KEYS'})
        self.assertEqual(en['UI.Coins.Format'], 'x{arg0}')
        self.assertEqual(en['UI.Rich'], 'In stock: <span style="color: #17FF00;">{arg0}</span>')
        self.assertEqual(ru['UI.Keys'], {'one': 'НУЖЕН {arg0} КЛЮЧ', 'few': 'НУЖЕН {arg0} КЛЮЧА', 'many': 'НУЖЕН {arg0} КЛЮЧЕЙ',
                                         'other': 'НУЖЕН {arg0} КЛЮЧЕЙ'})
        self.assertNotIn('UI.Only.En', ru)                       # empty: falls back to the source language
        loc = self.res['localization']
        self.assertEqual((loc['sourceLanguage'], loc['languages']), ('en', ['en', 'ru']))
        self.assertEqual(json.loads(self.read('game.json'))['languages'], ['en', 'ru'])
        keys = json.loads(self.read('ui/ugui/l10n-keys.json'))['keys']
        self.assertIn({'table': 'UI', 'key': 'Shop|Title', 'id': 101, 'engine': 'UI.Shop.Title'}, keys)

    def test_reimport_keeps_logic(self):
        stub = os.path.join(self.out, 'ui', 'logic', 'shop.lua')
        with open(stub, 'a', encoding='utf-8') as f:
            f.write('-- mine\n')
        importer.run(self.src, self.out, prefabs=['UI/Plain/Popup.prefab'])
        with open(stub, encoding='utf-8') as f:
            self.assertTrue(f.read().endswith('-- mine\n'))
        self.assertEqual(self.read('ui/screens/shop.rml'), self.shop)    # deterministic output

    def test_overlap_refused(self):
        with self.assertRaises(SystemExit):
            importer.run(self.src, os.path.join(self.src, 'Assets', 'out'))

    @unittest.skipUnless(os.environ.get('AIVERSE_UI_IMPORT_CLIENT'), 'set AIVERSE_UI_IMPORT_CLIENT=<AiverseGame> to render')
    def test_client_renders(self):
        game = os.path.join(self.tmp.name, 'render')
        importer.run(self.src, game, prefabs=['UI/Plain/Popup.prefab'])
        with open(os.path.join(game, 'game.json'), 'w') as f:
            json.dump({'name': 'Fixture', 'module': 'rts', 'languages': ['en', 'ru'], 'sourceLanguage': 'en', 'rules': 'rules.json',
                       'maps': []}, f)
        shot = os.path.join(self.tmp.name, 'shot.ppm')
        env = dict(os.environ, AIVERSE_CONTENT=game, AIVERSE_SCREENSHOT=shot, AIVERSE_EXIT_AFTER_FRAMES='20', AIVERSE_SPLASH='off',
                   AIVERSE_AUDIO='off', AIVERSE_UI_SAVE_DIR=os.path.join(self.tmp.name, 'save'))
        r = subprocess.run([os.environ['AIVERSE_UI_IMPORT_CLIENT'], '--size', '360x640', '--ui-screen', 'shop'], env=env,
                           capture_output=True, text=True, timeout=160, cwd=os.environ.get('AIVERSE_UI_IMPORT_CWD') or None)
        log = r.stdout + r.stderr
        self.assertTrue(os.path.exists(shot), log)
        self.assertNotRegex(log, r'\[lua\].*error|\[ui error\]')


if __name__ == '__main__':
    unittest.main()
