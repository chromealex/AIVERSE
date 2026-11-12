"""Synthetic Unity project for the tests: a UI.Windows-style screen (screen -> layout slots -> component prefabs),
plain uGUI widgets, sprites with 9-slice borders and Unity Localization string tables. Everything is generated
(original test data, no game assets)."""
import hashlib
import os
import sys

sys.path.insert(1, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from lib.png import write_png  # noqa: E402

IMAGE, TMP, BUTTON, HLG, SCROLL, MASK, TOGGLE, OUTLINE = (
    'fe87c0e1cc204ed48ad3b37840f39efc', 'f4688fdb7df04437aeb418b961361dc5', '4e29b1a8efbd4b44bb3f3716e73f07ff',
    '30649d3a9faa99c48a7b1166b86bf2a0', '1aa08ab6e0800fa44ae55d278d1423e3', '31a19414c41e5ae4aae2af33fee712f6',
    '9085046f02f69544eb97fd06b6048fe2', 'e19747de3f5aca642ab2be37e372fb86')
SCALER = '0cd44c1031e13a943bb63640046fad76'

# class name -> base class: written as C# files with .meta so the importer resolves GUID and base chain
SCRIPTS = {'WindowObject': 'MonoBehaviour', 'WindowComponent': 'WindowObject', 'GenericComponent': 'WindowComponent',
           'WindowLayout': 'WindowObject', 'WindowLayoutElement': 'WindowComponent', 'WindowLayoutSafeZone': 'WindowComponent',
           'TextComponent': 'GenericComponent', 'ImageComponent': 'GenericComponent', 'ButtonComponent': 'GenericComponent',
           'ListBaseComponent': 'WindowComponent', 'ListComponent': 'ListBaseComponent', 'WindowComponentModule': 'MonoBehaviour',
           'TextComponentModule': 'WindowComponentModule', 'TextLocalizationComponentModule': 'TextComponentModule',
           'TextComponentFormatModule': 'TextComponentModule', 'AnimationParameters': 'MonoBehaviour',
           'AlphaAnimationParameters': 'AnimationParameters', 'WindowBase': 'WindowObject', 'LayoutWindowType': 'WindowBase',
           'ShopScreen': 'LayoutWindowType', 'CoinsCounterComponent': 'WindowComponent', 'SparkleEffect': 'MonoBehaviour',
           'FancyImage': 'Image'}


def guid(name):
    return hashlib.md5(name.encode()).hexdigest()


def flowable(x):
    return isinstance(x, dict) and 0 < len(x) <= 4 and all(not isinstance(y, (dict, list)) for y in x.values())


def flow(x):
    return '{' + ', '.join('%s: %s' % (k, scalar(v)) for k, v in x.items()) + '}'


def emit(key, v, pad, out, dash=''):
    """Unity-style YAML: block mappings by two spaces, sequence dashes at the key's indentation."""
    head = pad + dash + (key + ':' if key is not None else '')
    if isinstance(v, dict) and not flowable(v):
        if not v:
            out.append(head + ' {}')
            return
        items = list(v.items())
        if key is None:      # a mapping as a sequence item: first key on the dash line
            k0, v0 = items[0]
            emit(k0, v0, pad, out, dash)
            for k, x in items[1:]:
                emit(k, x, pad + '  ', out)
            return
        out.append(head)
        for k, x in items:
            emit(k, x, pad + '  ', out)
    elif isinstance(v, list):
        if not v:
            out.append(head + ' []')
            return
        out.append(head)
        for x in v:
            emit(None, x, pad, out, '- ')
    else:
        val = flow(v) if isinstance(v, dict) else scalar(v)
        out.append(head + (' ' if key is not None else '') + val if key is not None else pad + dash + val)


def scalar(x):
    if isinstance(x, bool):
        return '1' if x else '0'
    if x is None:
        return ''
    if isinstance(x, str) and (x == '' or any(c in x for c in ':{}[]#<>\'"') or x != x.strip() or not x.isascii()):
        # like Unity: double quotes, non-ASCII as \uXXXX
        body = x.replace('\\', '\\\\').replace('"', '\\"')
        return '"' + ''.join(c if ord(c) < 128 else '\\u%04X' % ord(c) for c in body) + '"'
    return str(x)


class PrefabWriter:
    def __init__(self):
        self.docs, self.next = [], 1000

    def nid(self):
        self.next += 1
        return self.next

    def go(self, name, parent=None, rect=None, comps=(), active=True):
        """rect: (anchorMin, anchorMax, anchoredPosition, sizeDelta, pivot). Returns (go id, transform id)."""
        g, t = self.nid(), self.nid()
        amin, amax, pos, size, piv = rect or ((0, 0), (1, 1), (0, 0), (0, 0), (0.5, 0.5))
        comp_ids = []
        bodies = []
        for c in comps:
            cid = self.nid()
            comp_ids.append(cid)
            bodies.append((cid, c))
        self.docs.append((1, g, 'GameObject', {'m_Component': [{'component': {'fileID': t}}] + [{'component': {'fileID': c}} for c in comp_ids],
                                               'm_Name': name, 'm_IsActive': 1 if active else 0}))
        tr = {'m_GameObject': {'fileID': g}, 'm_LocalRotation': {'x': 0, 'y': 0, 'z': 0, 'w': 1}, 'm_LocalPosition': {'x': 0, 'y': 0, 'z': 0},
              'm_LocalScale': {'x': 1, 'y': 1, 'z': 1}, 'm_Children': [], 'm_Father': {'fileID': parent[1] if parent else 0},
              'm_AnchorMin': {'x': amin[0], 'y': amin[1]}, 'm_AnchorMax': {'x': amax[0], 'y': amax[1]},
              'm_AnchoredPosition': {'x': pos[0], 'y': pos[1]}, 'm_SizeDelta': {'x': size[0], 'y': size[1]}, 'm_Pivot': {'x': piv[0], 'y': piv[1]}}
        self.docs.append((224, t, 'RectTransform', tr))
        if parent:
            pdoc = next(d for d in self.docs if d[1] == parent[1])
            pdoc[3]['m_Children'].append({'fileID': t})
        for cid, (kind, data) in bodies:
            d = dict(data)
            if kind in (223, 225):
                self.docs.append((kind, cid, {223: 'Canvas', 225: 'CanvasGroup'}[kind], dict({'m_GameObject': {'fileID': g}, 'm_Enabled': 1}, **d)))
            else:
                self.docs.append((114, cid, 'MonoBehaviour', dict({'m_GameObject': {'fileID': g}, 'm_Enabled': 1,
                                                                    'm_Script': {'fileID': 11500000, 'guid': kind, 'type': 3}}, **d)))
        self.last_comps = comp_ids
        return g, t

    def text(self):
        out = ['%YAML 1.1', '%TAG !u! tag:unity3d.com,2011:']
        for cls, fid, name, data in self.docs:
            out.append('--- !u!%d &%d' % (cls, fid))
            emit(name, data, '', out)
        return '\n'.join(out) + '\n'

    def save(self, path):
        write_file(path, self.text())
        meta(path, guid(os.path.basename(path)))


def write_file(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', encoding='utf-8') as f:
        f.write(text)


def meta(path, g, extra=''):
    write_file(path + '.meta', 'fileFormatVersion: 2\nguid: %s\n%s' % (g, extra))


def sref(name):
    return {'fileID': 21300000, 'guid': guid(name), 'type': 3}


def ref(name, fid=11400000, typ=2):
    return {'fileID': fid, 'guid': guid(name), 'type': typ}


def full():
    return ((0, 0), (1, 1), (0, 0), (0, 0), (0.5, 0.5))


def tmp(text, size=32, align=(2, 512), color=(1, 1, 1, 1)):
    return (TMP, {'m_Color': {'r': color[0], 'g': color[1], 'b': color[2], 'a': color[3]}, 'm_text': text,
                  'm_fontAsset': ref('Main SDF.asset'), 'm_sharedMaterial': {'fileID': 2100000, 'guid': guid('Main SDF.asset'), 'type': 2},
                  'm_fontColor': {'r': color[0], 'g': color[1], 'b': color[2], 'a': color[3]}, 'm_fontSize': size, 'm_fontStyle': 1,
                  'm_HorizontalAlignment': align[0], 'm_VerticalAlignment': align[1], 'm_TextWrappingMode': 1, 'm_overflowMode': 0})


def image(sprite=None, typ=0, color=(1, 1, 1, 1), **kw):
    d = {'m_Color': {'r': color[0], 'g': color[1], 'b': color[2], 'a': color[3]}, 'm_Sprite': sprite or {'fileID': 0},
         'm_Type': typ, 'm_PreserveAspect': 0, 'm_FillCenter': 1, 'm_FillMethod': 0, 'm_FillAmount': 1, 'm_PixelsPerUnitMultiplier': 1}
    d.update(kw)
    return (IMAGE, d)


def uiw(name, **kw):
    return (guid(name + '.cs'), kw)


def png(path, w, h, rgba=(200, 120, 40, 255)):
    write_png(path, w, h, bytes(rgba) * (w * h))


def build(root, font_src):
    """Writes the synthetic project into root/Assets; returns the Assets path."""
    A = os.path.join(root, 'Assets')
    for name, base in SCRIPTS.items():
        p = os.path.join(A, 'Scripts', name + '.cs')
        write_file(p, 'public class %s : %s { }\n' % (name, base))
        meta(p, guid(name + '.cs'))
    # textures: a 9-slice panel and a two-sprite sheet
    png(os.path.join(A, 'Art', 'Panel.png'), 32, 32)
    meta(os.path.join(A, 'Art', 'Panel.png'), guid('Panel.png'),
         'TextureImporter:\n  spriteMode: 1\n  spritePixelsToUnits: 100\n  spriteBorder: {x: 8, y: 6, z: 8, w: 10}\n  textureType: 8\n')
    png(os.path.join(A, 'Art', 'Icons.png'), 64, 32, (40, 160, 220, 255))
    meta(os.path.join(A, 'Art', 'Icons.png'), guid('Icons.png'),
         'TextureImporter:\n  internalIDToNameTable:\n  - first:\n      213: 111\n    second: coin\n  - first:\n      213: 222\n    second: gem\n'
         '  spriteMode: 2\n  spritePixelsToUnits: 100\n  spriteSheet:\n    sprites:\n'
         '    - name: coin\n      rect:\n        serializedVersion: 2\n        x: 0\n        y: 0\n        width: 32\n        height: 32\n'
         '      border: {x: 0, y: 0, z: 0, w: 0}\n      internalID: 111\n'
         '    - name: gem\n      rect:\n        serializedVersion: 2\n        x: 32\n        y: 8\n        width: 32\n        height: 24\n'
         '      border: {x: 0, y: 0, z: 0, w: 0}\n      internalID: 222\n')
    # font: a TrueType file + a TMP font asset (with its material sub-asset) pointing at it
    with open(font_src, 'rb') as f:
        data = f.read()
    os.makedirs(os.path.join(A, 'Fonts'), exist_ok=True)
    with open(os.path.join(A, 'Fonts', 'Main.ttf'), 'wb') as f:
        f.write(data)
    meta(os.path.join(A, 'Fonts', 'Main.ttf'), guid('Main.ttf'))
    fa = os.path.join(A, 'Fonts', 'Main SDF.asset')
    write_file(fa, '%%YAML 1.1\n%%TAG !u! tag:unity3d.com,2011:\n--- !u!114 &11400000\nMonoBehaviour:\n  m_Name: Main SDF\n'
               '  m_FaceInfo:\n    m_FamilyName: Test\n    m_PointSize: 50\n  m_SourceFontFileGUID: %s\n  m_AtlasPadding: 5\n'
               '--- !u!21 &2100000\nMaterial:\n  m_Name: Main SDF Material\n  m_SavedProperties:\n    m_Floats:\n    - _OutlineWidth: 0.2\n'
               '    m_Colors:\n    - _OutlineColor: {r: 0, g: 0, b: 0, a: 1}\n' % guid('Main.ttf'))
    meta(fa, guid('Main SDF.asset'))
    localization(A)

    # ---- component prefabs
    loc = {'m_TableReference': {'m_TableCollectionName': 'GUID:' + guid('UI Shared Data.asset')},
           'm_TableEntryReference': {'m_KeyId': 101, 'm_Key': ''}}
    p = PrefabWriter()
    p.go('Title', comps=[uiw('TextComponent'), tmp('placeholder', 48), uiw('TextLocalizationComponentModule', key=loc)])
    p.save(os.path.join(A, 'UI', 'Components', 'Title.prefab'))

    p = PrefabWriter()
    b = p.go('BuyButton', comps=[uiw('ButtonComponent'), image(sref('Panel.png'), 1, (0.2, 0.6, 1, 1))])
    img_id = p.last_comps[1]
    btn = p.go('Click', b, full(), [(BUTTON, {'m_Interactable': 1, 'm_Transition': 1, 'm_TargetGraphic': {'fileID': img_id},
                                             'm_Colors': {'m_PressedColor': {'r': 0.5, 'g': 0.5, 'b': 0.5, 'a': 1}, 'm_ColorMultiplier': 1}})])
    p.go('Label', b, full(), [tmp('Buy <b>now</b> <sprite=1>', 30), (OUTLINE, {'m_EffectColor': {'r': 0, 'g': 0, 'b': 0, 'a': 1},
                                                                         'm_EffectDistance': {'x': 2, 'y': -2}})])
    p.save(os.path.join(A, 'UI', 'Components', 'BuyButton.prefab'))

    p = PrefabWriter()
    p.go('Row', rect=((0, 1), (0, 1), (0, 0), (120, 80), (0, 1)),
         comps=[uiw('WindowComponent'), image({'fileID': 222, 'guid': guid('Icons.png'), 'type': 3})])
    p.save(os.path.join(A, 'UI', 'Components', 'Row.prefab'))

    p = PrefabWriter()
    lst = p.go('ItemList', comps=[uiw('ListComponent', source={'type': 1, 'address': '', 'guid': guid('Row.prefab'),
                                                                     'directRef': {'fileID': 0}})])
    lc = p.last_comps[0]
    vp = p.go('Viewport', lst, full(), [(MASK, {'m_ShowMaskGraphic': 0}), image(None, 0, (1, 1, 1, 1))])
    content = p.go('Content', vp, ((0, 1), (1, 1), (0, 0), (0, 300), (0.5, 1)),
                   [(HLG, {'m_Padding': {'m_Left': 4, 'm_Right': 4, 'm_Top': 2, 'm_Bottom': 2}, 'm_ChildAlignment': 3, 'm_Spacing': 10,
                           'm_ChildForceExpandWidth': 0, 'm_ChildForceExpandHeight': 0, 'm_ChildControlWidth': 0, 'm_ChildControlHeight': 0})])
    scroll_doc = next(d for d in p.docs if d[1] == lc)
    scroll_doc[3]['customRoot'] = {'fileID': content[1]}
    p.go('Scroll', lst, full(), [(SCROLL, {'m_Content': {'fileID': content[1]}, 'm_Viewport': {'fileID': vp[1]},
                                           'm_Horizontal': 1, 'm_Vertical': 0})])
    p.save(os.path.join(A, 'UI', 'Components', 'ItemList.prefab'))

    p = PrefabWriter()
    c = p.go('Coins', comps=[uiw('CoinsCounterComponent'), uiw('SparkleEffect'), (225, {'m_Alpha': 0.5, 'm_Interactable': 1})])
    p.go('Amount', c, ((1, 0.5), (1, 0.5), (-10, 0), (200, 40), (1, 0.5)), [uiw('TextComponent'), tmp('0', 24, (4, 512)),
                                                                             uiw('TextComponentFormatModule', format='x{0}')])
    p.go('Glow', c, full(), [image(None, 0, (1, 1, 0, 0.5)), uiw('FancyImage')], active=False)
    p.save(os.path.join(A, 'UI', 'Components', 'Coins.prefab'))

    # ---- layout: canvas + scaler + a safe zone with slots
    p = PrefabWriter()
    lroot = p.go('ShopLayout', rect=((0, 0), (0, 0), (0, 0), (0, 0), (0.5, 0.5)),
                 comps=[uiw('WindowLayout'), (223, {'m_RenderMode': 0}),
                        (SCALER, {'m_UiScaleMode': 1, 'm_ReferenceResolution': {'x': 1080, 'y': 1920}, 'm_ScreenMatchMode': 0,
                                  'm_MatchWidthOrHeight': 1, 'm_ReferencePixelsPerUnit': 100})])
    safe = p.go('SafeZone', lroot, full(), [uiw('WindowLayoutSafeZone')])
    for tag, name, rect in ((1, 'Header', ((0, 1), (1, 1), (0, 0), (0, 200), (0.5, 1))),
                            (2, 'Body', ((0, 0), (1, 1), (0, -50), (-40, -500), (0.5, 0.5))),
                            (3, 'Footer', ((0.5, 0), (0.5, 0), (0, 40), (600, 160), (0.5, 0))),
                            (4, 'Unused', full())):
        p.go(name, safe, rect, [uiw('WindowLayoutElement', tagId=tag, hideInScreen=0)])
    p.save(os.path.join(A, 'UI', 'Layouts', 'ShopLayout.prefab'))

    # ---- screen
    p = PrefabWriter()
    lay = {'fileID': 1003, 'guid': guid('ShopLayout.prefab'), 'type': 3}

    def comp(tag, name, direct):
        res = {'type': 1 if direct else 2, 'objectType': 0, 'address': '' if direct else 'Assets/UI/Components/%s.prefab' % name,
               'guid': '' if not direct else guid(name + '.prefab'), 'directRef': {'fileID': 1001, 'guid': guid(name + '.prefab'), 'type': 3}
               if direct else {'fileID': 0}}
        return {'windowLayout': lay, 'tag': tag, 'component': res}

    p.go('ShopScreen', comps=[uiw('ShopScreen', layouts={'items': [{'windowLayout': lay, 'layoutPreferences': {'fileID': 0},
                                                                     'components': [comp(1, 'Title', True), comp(2, 'ItemList', False),
                                                                                    comp(3, 'BuyButton', True), comp(5, 'Coins', True)]}]})])
    p.save(os.path.join(A, 'UI', 'Screens', 'ShopScreen.prefab'))

    # ---- a plain uGUI prefab (no UI.Windows): canvas, toggle
    p = PrefabWriter()
    r = p.go('Popup', rect=((0, 0), (0, 0), (0, 0), (0, 0), (0.5, 0.5)),
             comps=[(223, {'m_RenderMode': 0}), (SCALER, {'m_UiScaleMode': 1, 'm_ReferenceResolution': {'x': 800, 'y': 600},
                                                           'm_ScreenMatchMode': 1, 'm_MatchWidthOrHeight': 0})])
    t = p.go('Toggle', r, ((0.5, 0.5), (0.5, 0.5), (0, 0), (60, 60), (0.5, 0.5)), [image(None, 0, (0.3, 0.3, 0.3, 1))])
    tog_doc_idx = len(p.docs)
    check = p.go('Check', t, full(), [image(None, 0, (0, 1, 0, 1))])
    check_img = p.last_comps[0]
    p.docs.append((114, 9999, 'MonoBehaviour', {'m_GameObject': {'fileID': t[0]}, 'm_Enabled': 1,
                                                 'm_Script': {'fileID': 11500000, 'guid': TOGGLE, 'type': 3},
                                                 'm_IsOn': 0, 'graphic': {'fileID': check_img}, 'm_Transition': 0}))
    next(d for d in p.docs if d[1] == t[0])[3]['m_Component'].append({'component': {'fileID': 9999}})
    p.save(os.path.join(A, 'UI', 'Plain', 'Popup.prefab'))
    return A


def localization(A):
    L = os.path.join(A, 'Loc')
    hdr = '%YAML 1.1\n%TAG !u! tag:unity3d.com,2011:\n--- !u!114 &11400000\nMonoBehaviour:\n'
    entries = [(101, 'Shop|Title'), (102, 'Keys'), (103, 'Coins.Format'), (104, 'Rich'), (105, 'Only.En')]
    shared = hdr + ('  m_Name: UI Shared Data\n  m_EditorClassIdentifier: Unity.Localization::UnityEngine.Localization.Tables.SharedTableData\n'
                    '  m_TableCollectionName: UI\n  m_TableCollectionNameGuidString: %s\n  m_Entries:\n' % guid('UI Shared Data.asset'))
    for i, k in entries:
        shared += '  - m_Id: %d\n    m_Key: %s\n    m_Metadata:\n      m_Items: []\n' % (i, k)
    write_file(os.path.join(L, 'UI Shared Data.asset'), shared)
    meta(os.path.join(L, 'UI Shared Data.asset'), guid('UI Shared Data.asset'))

    def table(code, values):
        t = hdr + ('  m_Name: UI_%s\n  m_EditorClassIdentifier: Unity.Localization::UnityEngine.Localization.Tables.StringTable\n'
                   '  m_LocaleId:\n    m_Code: %s\n  m_SharedData: {fileID: 11400000, guid: %s, type: 2}\n  m_TableData:\n') % (
            code, code, guid('UI Shared Data.asset'))
        for i, v in values:
            t += '  - m_Id: %d\n    m_Localized: %s\n    m_Metadata:\n      m_Items:%s\n' % (
                i, scalar(v), '\n      - rid: 77' if i == 102 else ' []')
        t += '  references:\n    version: 2\n    RefIds:\n    - rid: 77\n      type: {class: SmartFormatTag, ns: UnityEngine.Localization.Metadata, asm: Unity.Localization}\n      data:\n        m_Entries: \n'
        write_file(os.path.join(L, 'UI_%s.asset' % code), t)
        meta(os.path.join(L, 'UI_%s.asset' % code), guid('UI_%s.asset' % code))

    table('en', [(101, 'SHOP'), (102, 'NEED {0} {0:plural:KEY|KEYS}'), (103, 'x{0:N0}'), (104, 'In stock: <color=#17FF00>{0}</color>'),
                 (105, 'English only')])
    table('ru', [(101, 'МАГАЗИН'), (102, 'НУЖЕН {0} {0:plural:КЛЮЧ|КЛЮЧА|КЛЮЧЕЙ}'),
                 (103, ''), (104, '<b>{0}</b>'), (105, '')])
    for code, name in (('en', 'English (en)'), ('ru', 'Russian (ru)')):
        p = os.path.join(L, name + '.asset')
        write_file(p, hdr + '  m_Name: %s\n  m_EditorClassIdentifier: Unity.Localization::UnityEngine.Localization.Locale\n'
                   '  m_Identifier:\n    m_Code: %s\n  m_LocaleName: %s\n' % (name, code, name))
        meta(p, guid(name))
    p = os.path.join(L, 'Localization Settings.asset')
    write_file(p, hdr + '  m_Name: Localization Settings\n  m_EditorClassIdentifier: Unity.Localization::UnityEngine.Localization.Settings.LocalizationSettings\n'
               '  m_ProjectLocaleIdentifier:\n    m_Code: en\n  references:\n    version: 2\n    RefIds:\n    - rid: 1\n'
               '      type: {class: SpecificLocaleSelector, ns: UnityEngine.Localization.Settings, asm: Unity.Localization}\n'
               '      data:\n        m_LocaleId:\n          m_Code: en\n')
    meta(p, guid('Localization Settings'))
