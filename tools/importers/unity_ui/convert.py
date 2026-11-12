"""uGUI hierarchy (a flattened prefab) -> RML element tree.

Each GameObject becomes a <div> positioned from its RectTransform (rect.py). Graphics become decorators /
text, layout groups flexbox, controls get `u-kind` markers and ids for Lua logic. Everything that is
not converted is counted per screen (Issues)."""
import os

from lib.unity.unity_yaml import color, get, num
from lib.unity.unity_scene import CLASS_GAMEOBJECT, CLASS_MONO, CLASS_RECT_TRANSFORM, CLASS_TRANSFORM

from dom import El, Raw
from media import ident, ttf_family
from rect import Rect, dp, fmt
import richtext
from scripts import SILENT

CLASS_CANVAS_RENDERER, CLASS_CANVAS, CLASS_CANVAS_GROUP = 222, 223, 225
NATIVE = {20: 'Camera', 82: 'AudioSource', 95: 'Animator', 111: 'Animation', 198: 'ParticleSystem', 199: 'ParticleSystemRenderer',
          23: 'MeshRenderer', 33: 'MeshFilter', 137: 'SkinnedMeshRenderer', 108: 'Light', 212: 'SpriteRenderer',
          328: 'VideoPlayer', 210: 'SortingGroup', 120: 'LineRenderer', 96: 'TrailRenderer'}

GRAPHICS = ('Image', 'RawImage', 'Text', 'TextMeshProUGUI')
CONTROLS = ('Button', 'Toggle', 'Slider', 'Scrollbar', 'ScrollRect', 'InputField', 'TMP_InputField', 'Dropdown', 'TMP_Dropdown')
LAYOUTS = ('HorizontalLayoutGroup', 'VerticalLayoutGroup', 'GridLayoutGroup')
UIW_VIEW = ('ImageComponent', 'TextComponent', 'ButtonComponent', 'CheckboxComponent', 'ProgressComponent', 'ListComponent',
            'ListBaseComponent', 'InputFieldComponent', 'GenericComponent', 'WindowComponent', 'WindowLayoutElement',
            'WindowLayout', 'WindowLayoutSafeZone', 'WindowObject')
FULL = {'position': 'absolute', 'left': '0', 'top': '0', 'right': '0', 'bottom': '0'}


def rgba(c, mul=1.0):
    r, g, b, a = c
    return 'rgba(%d, %d, %d, %d)' % (round(r * 255), round(g * 255), round(b * 255), round(max(0.0, min(1.0, a * mul)) * 255))


def mulc(a, b):
    return tuple(x * y for x, y in zip(a, b))


class Issues:
    """What a screen could not take over exactly."""

    def __init__(self):
        self.unsupported, self.approx, self.logic, self.animations, self.keys = {}, {}, [], 0, set()

    def miss(self, what, path):
        self.unsupported.setdefault(what, []).append(path)

    def near(self, what, path):
        self.approx.setdefault(what, []).append(path)

    def as_json(self):
        return {'unsupported': {k: {'count': len(v), 'examples': v[:3]} for k, v in sorted(self.unsupported.items())},
                'approximate': {k: {'count': len(v), 'examples': v[:3]} for k, v in sorted(self.approx.items())},
                'showHideAnimations': self.animations, 'localizationKeys': sorted(self.keys)}


class Node:
    __slots__ = ('go', 'name', 'active', 'rect', 'rt', 'comps', 'children', 'path', 'fid')

    def __init__(self):
        self.children, self.comps = [], []

    def first(self, *names):
        return next((c for c in self.comps if c['class'] in names), None)

    def kind(self, *names):
        """First component whose class chain contains one of `names`."""
        return next((c for c in self.comps if any(n in c['chain'] for n in names)), None)


def build_tree(prefab, scripts):
    """Prefab -> list of root Nodes (children in sibling order)."""

    def mk(tr, parent_path, depth):
        n = Node()
        n.fid = tr.file_id
        n.rt = tr.data if tr.class_id == CLASS_RECT_TRANSFORM else None
        n.rect = Rect(tr.data) if n.rt is not None else None
        n.go = get(tr.data, 'm_GameObject', 'fileID')
        god = prefab.gos.get(n.go)
        n.name = prefab.name(n.go)
        n.active = int(num(get(god.data, 'm_IsActive') if god else 1, 1)) != 0
        n.path = (parent_path + '/' if parent_path else '') + n.name
        for d in prefab.comp_by_go.get(n.go, []):
            if d.class_id in (CLASS_TRANSFORM, CLASS_RECT_TRANSFORM, CLASS_GAMEOBJECT):
                continue
            if d.class_id == CLASS_MONO:
                cls = scripts.name(get(d.data, 'm_Script'))
                ec = str(get(d.data, 'm_EditorClassIdentifier', default='') or '')
                if not cls and '::' in ec:
                    cls = ec.split('.')[-1]
                cls = cls or 'MissingScript'
                chain = scripts.chain(cls)
            else:
                cls = {CLASS_CANVAS_RENDERER: 'CanvasRenderer', CLASS_CANVAS: 'Canvas', CLASS_CANVAS_GROUP: 'CanvasGroup'}.get(
                    d.class_id, NATIVE.get(d.class_id, 'class%d' % d.class_id))
                chain = [cls]
            n.comps.append({'class': cls, 'chain': chain, 'data': d.data or {}, 'id': d.file_id,
                            'enabled': int(num(get(d.data, 'm_Enabled'), 1)) != 0})
        if depth < 64:
            n.children = [mk(ch, n.path, depth + 1) for ch in prefab.children(tr)]
        return n

    return [mk(t, '', 0) for t in prefab.roots()]


class Converter:
    """One converter per screen (ids, extra style rules and issues are per document)."""

    def __init__(self, media, l10n, issues, prefix=''):
        self.media, self.l10n, self.iss, self.prefix = media, l10n, issues, prefix
        self.ids, self.rules, self.slots = set(), [], []
        self.ref_ppu = 100.0
        self.by_comp = {}   # component fileID -> Node (Button.m_TargetGraphic, ScrollRect.m_Content, ...)
        self.el_of = {}     # node fid -> El

    def uid(self, name):
        base = ident(self.prefix + '-' + name if self.prefix else name, 'n')
        u, k = base, 1
        while u in self.ids:
            k += 1
            u = '%s-%d' % (base, k)
        self.ids.add(u)
        return u

    def index(self, roots):
        stack = list(roots)
        while stack:
            n = stack.pop()
            self.by_comp[n.fid] = n     # references to the RectTransform (ScrollRect.m_Content, customRoot, ...)
            for c in n.comps:
                self.by_comp[c['id']] = n
            stack += n.children

    # ------------------------------------------------------------------ nodes
    def node(self, n, flex=None, root=False, extra=None):
        """Node -> El. `flex` is the parent's layout group component (children are flex items).
        `extra(node, el)` lets the caller fill UI.Windows slots."""
        el = El('div')
        el.attrs['id'] = self.uid(n.name)
        el.attrs['u-src'] = n.name
        self.el_of[n.fid] = el
        st = el.style
        if n.rect is None:
            st.update(FULL)
        elif root and n.rect.size == (0.0, 0.0) and n.rect.amin == n.rect.amax:
            st.update(FULL)    # a Canvas root: the canvas is the screen
        elif flex is not None and not self.ignores_layout(n):
            self.flex_item(n, flex, st)
        else:
            st.update(n.rect.box())
        if n.rect is not None and n.rect.tilt:
            self.iss.near('3D rotation (x/y) flattened', n.path)
        for c in n.comps:
            self.component(n, c, el, root)
        self.effects(n, el)
        arf = n.first('AspectRatioFitter')
        if arf and arf['enabled'] and n.rect is not None:
            self.aspect(n, arf['data'], el)
        if not n.active:
            el.attrs['u-display'] = st.get('display', 'block')
            st['display'] = 'none'
        group = n.kind(*LAYOUTS)
        if group is not None and not group['enabled']:
            group = None
        for ch in n.children:
            el.add(self.node(ch, flex=group, extra=extra))
        if extra:
            extra(n, el)
        return el

    def component(self, n, c, el, root):
        cls, chain, st = c['class'], c['chain'], el.style
        if cls in SILENT or cls == 'CanvasRenderer':
            return
        if cls == 'CanvasGroup':
            a = num(c['data'].get('m_Alpha'), 1.0)
            if a < 0.999:
                st['opacity'] = fmt(a)
            if not int(num(c['data'].get('m_Interactable'), 1)):
                el.attrs['u-interactable'] = 'false'
        elif cls == 'Canvas':
            if int(num(c['data'].get('m_OverrideSorting'))) and not root:
                self.iss.near('nested Canvas sorting order (document order is used)', n.path)
        elif 'RawImage' in chain:
            self.raw_image(n, c, el)
        elif 'Image' in chain:
            if cls != 'Image':
                self.iss.near('%s drawn as Image' % cls, n.path)
            self.image(n, c, el)
        elif 'TextMeshProUGUI' in chain or 'TMP_Text' in chain:
            self.tmp_text(n, c, el)
        elif 'Text' in chain:
            self.legacy_text(n, c, el)
        elif 'RectMask2D' in chain or 'Mask' in chain:
            if c['enabled']:
                st['overflow'] = 'hidden'
            if 'Mask' in chain and not int(num(c['data'].get('m_ShowMaskGraphic'), 1)):
                st.pop('decorator', None)
                st.pop('background-color', None)
                el.attrs['u-hide-graphic'] = '1'
            img = n.kind('Image')
            if 'Mask' in chain and img and get(img['data'], 'm_Sprite', 'guid'):
                self.iss.near('Mask with a sprite clips to its rectangle', n.path)
        elif any(x in chain for x in LAYOUTS):
            self.layout(n, c, el)
        elif cls == 'ContentSizeFitter':
            hf, vf = int(num(c['data'].get('m_HorizontalFit'))), int(num(c['data'].get('m_VerticalFit')))
            if hf and 'right' not in st:
                st['width'] = 'auto'
            if vf and 'bottom' not in st:
                st['height'] = 'auto'
            if hf or vf:
                self.iss.near('ContentSizeFitter (auto size)', n.path)
        elif cls == 'AspectRatioFitter':
            pass   # after the graphics: aspect()
        elif cls in ('Shadow', 'Outline') or 'Shadow' in chain:
            pass   # applied to the text in effects()
        elif any(x in chain for x in CONTROLS):
            self.control(n, c, el)
        elif any(x.endswith('AnimationParameters') for x in chain):
            self.iss.animations += 1
        elif any(x.endswith('Module') for x in chain[1:]) or cls.endswith('ComponentModule'):
            self.module(n, c, el)
        elif any(x in chain for x in UIW_VIEW):
            self.windows_component(n, c, el)
        elif 'BaseMeshEffect' in chain:
            self.iss.miss('mesh effect %s' % cls, n.path)
        elif cls == 'EventTrigger':
            el.attrs['u-events'] = '1'
            self.iss.near('EventTrigger (pointer callbacks need Lua)', n.path)
        else:
            self.iss.miss(cls, n.path)

    def aspect(self, n, d, el):
        mode, ratio, st = int(num(d.get('m_AspectMode'))), num(d.get('m_AspectRatio'), 1.0) or 1.0, el.style
        w, h = n.rect.size
        if mode in (3, 4):
            # Fit In / Envelope Parent: the driven rect is the parent rect scaled to the aspect; the graphic keeps it
            for k in ('width', 'height', 'margin-left', 'margin-right', 'margin-top', 'margin-bottom'):
                st.pop(k, None)
            st.update({'left': '0', 'right': '0', 'top': '0', 'bottom': '0'})
            dec = st.get('decorator', '')
            if dec.startswith('image(') and dec.endswith(' fill)'):
                st['decorator'] = dec[:-5] + ('cover)' if mode == 4 else 'contain)')
            else:
                self.iss.near('AspectRatioFitter %s (children use the whole parent)' % ('envelope' if mode == 4 else 'fit'), n.path)
        elif mode == 1 and 'width' in st:
            st['height'] = dp(w / ratio)
        elif mode == 2 and 'height' in st:
            st['width'] = dp(h * ratio)
        elif mode in (1, 2):
            self.iss.near('AspectRatioFitter on a stretched rect (authored size kept)', n.path)

    @staticmethod
    def ignores_layout(n):
        le = n.first('LayoutElement')
        return bool(le and int(num(le['data'].get('m_IgnoreLayout'))))

    # ------------------------------------------------------------------ layout groups
    def layout(self, n, c, el):
        d, st, cls = c['data'], el.style, c['class']
        if not c['enabled']:
            return
        pad = d.get('m_Padding') or {}
        p = [num(pad.get(k)) for k in ('m_Top', 'm_Right', 'm_Bottom', 'm_Left')]
        if any(p):
            st['padding'] = ' '.join(dp(x) for x in p)
        st['display'] = 'flex'
        align = int(num(d.get('m_ChildAlignment')))
        row, col = align // 3, align % 3
        main_of = ['flex-start', 'center', 'flex-end']
        if 'GridLayoutGroup' in c['chain']:
            st.update({'flex-direction': 'row', 'flex-wrap': 'wrap', 'justify-content': main_of[col], 'align-content': main_of[row]})
            if int(num(d.get('m_StartAxis'))) == 1:
                self.iss.near('GridLayoutGroup vertical start axis (filled by rows)', n.path)
            if int(num(d.get('m_Constraint'))):
                self.iss.near('GridLayoutGroup column/row constraint (wraps by width)', n.path)
            if cls != 'GridLayoutGroup':
                self.iss.near('%s laid out as GridLayoutGroup' % cls, n.path)
            return
        horiz = 'HorizontalLayoutGroup' in c['chain']
        rev = int(num(d.get('m_ReverseArrangement')))
        st['flex-direction'] = ('row' if horiz else 'column') + ('-reverse' if rev else '')
        st['justify-content'] = main_of[col] if horiz else main_of[row]
        cross = main_of[row] if horiz else main_of[col]
        ctrl = int(num(d.get('m_ChildControlHeight' if horiz else 'm_ChildControlWidth')))
        expand = int(num(d.get('m_ChildForceExpandHeight' if horiz else 'm_ChildForceExpandWidth'), 1))
        st['align-items'] = 'stretch' if (ctrl and expand) else cross
        if cls not in ('HorizontalLayoutGroup', 'VerticalLayoutGroup'):
            self.iss.near('%s laid out as %s' % (cls, 'HorizontalLayoutGroup' if horiz else 'VerticalLayoutGroup'), n.path)

    def flex_item(self, n, group, st):
        d, chain = group['data'], group['chain']
        st['position'] = 'relative'
        st['flex-shrink'] = '0'
        le = (n.first('LayoutElement') or {}).get('data') or {}
        w, h = n.rect.size
        if 'GridLayoutGroup' in chain:
            cs, sp = d.get('m_CellSize') or {}, d.get('m_Spacing') or {}
            st['width'], st['height'] = dp(num(cs.get('x'), 100)), dp(num(cs.get('y'), 100))
            if num(sp.get('x')):
                st['margin-right'] = dp(num(sp.get('x')))
            if num(sp.get('y')):
                st['margin-bottom'] = dp(num(sp.get('y')))
            return
        horiz = 'HorizontalLayoutGroup' in chain
        main, cross = ('width', 'height') if horiz else ('height', 'width')
        mw, mh = (w, h) if horiz else (h, w)
        ax, cx = ('Width', 'Height') if horiz else ('Height', 'Width')
        ctrl_main, ctrl_cross = int(num(d.get('m_ChildControl' + ax))), int(num(d.get('m_ChildControl' + cx)))
        exp_main, exp_cross = int(num(d.get('m_ChildForceExpand' + ax), 1)), int(num(d.get('m_ChildForceExpand' + cx), 1))
        pref, minv, flexv = (num(le.get('m_Preferred' + ax), -1), num(le.get('m_Min' + ax), -1), num(le.get('m_Flexible' + ax), -1))
        if ctrl_main:
            st[main] = dp(max(pref if pref >= 0 else (minv if minv >= 0 else mw), 0))
            if flexv > 0 or exp_main:
                st['flex-grow'] = fmt(flexv if flexv > 0 else 1)
                st['flex-shrink'] = '1'
        else:
            st[main] = dp(mw)
            if exp_main:
                self.iss.near('force-expand without child size control (spacing differs)', n.path)
        if not (ctrl_cross and exp_cross):
            prefc = num(le.get('m_Preferred' + cx), -1) if ctrl_cross else -1
            st[cross] = dp(prefc if prefc >= 0 else mh)
        spacing = num(d.get('m_Spacing'))
        if spacing and n is not None:
            st['margin-left' if horiz else 'margin-top'] = dp(spacing)   # the first child's margin is removed in finish()
            self.rules.append(('first-spacing', group['id'], n, 'margin-left' if horiz else 'margin-top'))

    # ------------------------------------------------------------------ graphics
    def image(self, n, c, el):
        d, st = c['data'], el.style
        if not c['enabled'] or el.attrs.get('u-hide-graphic'):
            return
        col = color(d.get('m_Color'))
        if get(d.get('m_Material') or {}, 'guid'):
            self.iss.near('custom UI material ignored', n.path)
        sref = d.get('m_Sprite') or {}
        spr = self.media.sprite(sref)
        typ = int(num(d.get('m_Type')))
        if get(sref, 'guid') and not spr:
            self.iss.miss('sprite not found / unsupported texture', n.path)
        if spr and 'builtin' in spr:
            # Unity's default UI sprites: rounded rectangles / a circle, drawn with border-radius
            if col[3] > 0.001 and spr['builtin'] not in ('Checkmark', 'DropdownArrow'):
                st['background-color'] = rgba(col)
                w, h = n.rect.size if n.rect else (0, 0)
                st['border-radius'] = dp(min(w, h) / 2) if spr['builtin'] == 'Knob' and min(w, h) > 0 else '6dp'
            self.iss.near('built-in sprite %s drawn with border-radius' % spr['builtin'], n.path)
            return
        if not spr:
            if col[3] > 0.001:
                st['background-color'] = rgba(col)
            return
        if rgba(col) != 'rgba(255, 255, 255, 255)':
            st['image-color'] = rgba(col)
        if typ == 1 and spr['inner']:
            mult = num(d.get('m_PixelsPerUnitMultiplier'), 1.0) or 1.0
            k = self.ref_ppu / spr['ppu'] / mult    # Unity: border px / (sprite ppu / canvas reference ppu) / multiplier
            l, b, r, t = spr['border']
            st['decorator'] = 'ninepatch(%s, %s, %s %s %s %s)' % (spr['sheet'], spr['inner'], dp(t * k), dp(r * k), dp(b * k), dp(l * k))
            if not int(num(d.get('m_FillCenter'), 1)):
                self.iss.near('sliced image without centre fill', n.path)
        elif typ == 2:
            st['decorator'] = 'image(%s repeat)' % spr['sheet']
            self.iss.near('tiled image (whole sprite repeated)', n.path)
        else:
            st['decorator'] = 'image(%s %s)' % (spr['sheet'], 'contain' if int(num(d.get('m_PreserveAspect'))) else 'fill')
            if typ == 3:
                self.fill(n, d, el)

    def fill(self, n, d, el):
        amount = num(d.get('m_FillAmount'), 1.0)
        method = int(num(d.get('m_FillMethod')))
        el.attrs['u-fill'] = fmt(amount)
        el.attrs['u-fill-method'] = ['horizontal', 'vertical', 'radial90', 'radial180', 'radial360'][max(0, min(4, method))]
        el.attrs['u-fill-origin'] = str(int(num(d.get('m_FillOrigin'))))
        if amount < 0.999:
            if method in (0, 1):
                self.iss.near('filled image drawn whole (ugui.runtime fill() resizes it)', n.path)
            else:
                self.iss.miss('radial image fill', n.path)

    def raw_image(self, n, c, el):
        d, st = c['data'], el.style
        if not c['enabled']:
            return
        col = color(d.get('m_Color'))
        tex = d.get('m_Texture') or {}
        f = self.media.raw(tex) if get(tex, 'guid') else None
        if f:
            st['decorator'] = 'image(../%s fill)' % f
            if rgba(col) != 'rgba(255, 255, 255, 255)':
                st['image-color'] = rgba(col)
            uv = d.get('m_UVRect') or {}
            if (num(uv.get('x')), num(uv.get('y')), num(uv.get('width'), 1), num(uv.get('height'), 1)) != (0, 0, 1, 1):
                self.iss.near('RawImage uvRect', n.path)
            return
        if get(tex, 'fileID'):
            el.attrs['u-texture'] = 'runtime'
            self.iss.near('RawImage render texture (filled at run time)', n.path)
        if col[3] > 0.001 and not get(tex, 'fileID'):
            st['background-color'] = rgba(col)

    def text_key(self, n):
        """Engine key of the localization module on this object, if any."""
        for c in n.comps:
            if 'TextLocalizationComponentModule' in c['chain'] or c['class'] == 'LocalizeStringEvent':
                ref = c['data'].get('key') or c['data'].get('m_StringReference') or {}
                k = self.l10n.engine_key(ref) if self.l10n else None
                if k:
                    self.iss.keys.add(k)
                    return k
                if get(ref, 'm_TableReference', 'm_TableCollectionName'):
                    self.iss.miss('localized string with an unknown table / key', n.path)
        return None

    def text_box(self, st, halign, valign, wrap, clip):
        """Text sits in an inner block so vertical alignment can use flexbox."""
        st['display'] = 'flex'
        st['flex-direction'] = 'column'
        st['justify-content'] = valign
        if clip:
            st['overflow'] = 'hidden'
        inner = El('div')
        inner.attrs['class'] = 'u-text'
        inner.style['text-align'] = halign
        if not wrap:
            inner.style['white-space'] = 'nowrap'
        return inner

    def tmp_text(self, n, c, el):
        d, st = c['data'], el.style
        if not c['enabled']:
            return
        font = self.media.font(d.get('m_fontAsset'))
        size = num(d.get('m_fontSize'), 36)
        if int(num(d.get('m_enableAutoSizing'))):
            self.iss.near('TMP auto size (current size kept)', n.path)
        col = color(d.get('m_fontColor'), color(d.get('m_Color')))
        style = int(num(d.get('m_fontStyle')))
        h, v = int(num(d.get('m_HorizontalAlignment'), 1)), int(num(d.get('m_VerticalAlignment'), 256))
        halign = {1: 'left', 2: 'center', 4: 'right', 8: 'justify', 16: 'justify', 32: 'center'}.get(h, 'left')
        valign = {256: 'flex-start', 512: 'center', 1024: 'flex-end', 2048: 'flex-end', 4096: 'center', 8192: 'flex-start'}.get(v, 'flex-start')
        wrap = int(num(d.get('m_TextWrappingMode'), num(d.get('m_enableWordWrapping'), 1))) not in (0, 2)
        inner = self.text_box(st, halign, valign, wrap, int(num(d.get('m_overflowMode'))) in (1, 3, 4, 6))
        ist = inner.style
        ist['font-size'] = dp(size)
        ist['color'] = rgba(col)
        if font and font['family']:
            ist['font-family'] = font['family']
        if style & 1:
            ist['font-weight'] = 'bold'
        if style & 2:
            ist['font-style'] = 'italic'
        if style & 16:
            ist['text-transform'] = 'uppercase'
        elif style & 8:
            ist['text-transform'] = 'lowercase'
        if style & (4 | 32 | 64):
            self.iss.near('TMP underline / small caps / strikethrough', n.path)
        if num(d.get('m_lineSpacing')):
            ist['line-height'] = fmt(1.2 + num(d.get('m_lineSpacing')) / 100.0)
        if num(d.get('m_characterSpacing')):
            self.iss.near('TMP character spacing', n.path)
        m = d.get('m_margin') or {}
        mg = [num(m.get(k)) for k in ('y', 'z', 'w', 'x')]   # TMP margin: x left, y top, z right, w bottom
        if any(mg):
            st['padding'] = ' '.join(dp(x) for x in mg)
        effects = self.tmp_material(font, d, size)
        if effects:
            ist['font-effect'] = ', '.join(effects)
        if int(num(d.get('m_enableVertexGradient'))):
            self.iss.near('TMP vertex colour gradient (base colour used)', n.path)
        self.put_text(n, inner, str(d.get('m_text') or ''))
        el.add(inner)

    def tmp_material(self, font, d, size):
        props = self.media.material(font, d.get('m_sharedMaterial')) if font else {}
        if not props:
            return []
        unit = (font['padding'] / font['point']) * size    # one SDF spread unit in canvas units
        out = []
        w, oc = num(props.get('_OutlineWidth')), props.get('_OutlineColor')
        if w > 0.001 and isinstance(oc, dict) and num(oc.get('a'), 1) > 0.01:
            out.append('outline(%s %s)' % (dp(max(1.0, w * unit)), rgba(color(oc))))
        uc = props.get('_UnderlayColor')
        if 'UNDERLAY' in props.get('_keywords', '') and isinstance(uc, dict) and num(uc.get('a'), 1) > 0.01:
            ox, oy = num(props.get('_UnderlayOffsetX')), num(props.get('_UnderlayOffsetY'))
            out.append('shadow(%s %s %s)' % (dp(ox * unit), dp(-oy * unit), rgba(color(uc))))
        return out

    def legacy_text(self, n, c, el):
        d, st = c['data'], el.style
        if not c['enabled']:
            return
        fd = d.get('m_FontData') or {}
        size = num(fd.get('m_FontSize'), 14)
        a = int(num(fd.get('m_Alignment')))
        inner = self.text_box(st, ['left', 'center', 'right'][a % 3], ['flex-start', 'center', 'flex-end'][min(2, a // 3)],
                              int(num(fd.get('m_HorizontalOverflow'))) == 0, int(num(fd.get('m_VerticalOverflow'))) == 0)
        ist = inner.style
        ist['font-size'] = dp(size)
        ist['color'] = rgba(color(d.get('m_Color')))
        fs = int(num(fd.get('m_FontStyle')))
        if fs in (1, 3):
            ist['font-weight'] = 'bold'
        if fs in (2, 3):
            ist['font-style'] = 'italic'
        path = self.media.assets.path(fd.get('m_Font')) if isinstance(fd.get('m_Font'), dict) else None
        if path and os.path.splitext(path)[1].lower() in ('.ttf', '.otf'):
            fam = ttf_family(path)
            if fam:
                ist['font-family'] = fam
            self.iss.near('legacy Text font (family name only, file not copied)', n.path)
        self.put_text(n, inner, str(d.get('m_Text') or ''))
        el.add(inner)

    def put_text(self, n, inner, s):
        key = self.text_key(n)
        if key:
            inner.add(Raw('@' + key))
            inner.attrs['u-key'] = key
            return
        dropped = set()
        inner.add(Raw(richtext.to_rml(s, dropped=dropped)))
        for t in sorted(dropped):
            self.iss.near('rich text tag %s' % t, n.path)

    def effects(self, n, el):
        """uGUI Shadow / Outline mesh effects on text -> font-effect."""
        inner = next((c for c in el.children if isinstance(c, El) and c.attrs.get('class') == 'u-text'), None)
        for c in n.comps:
            if c['class'] not in ('Shadow', 'Outline') or not c['enabled']:
                continue
            dist = c['data'].get('m_EffectDistance') or {}
            dx, dy = num(dist.get('x'), 1), num(dist.get('y'), -1)
            col = rgba(color(c['data'].get('m_EffectColor'), (0, 0, 0, 0.5)))
            if inner is None:
                self.iss.miss('%s effect on an image' % c['class'], n.path)
                continue
            e = ('outline(%s %s)' % (dp(max(abs(dx), abs(dy))), col)) if c['class'] == 'Outline' else (
                'shadow(%s %s %s)' % (dp(dx), dp(-dy), col))
            prev = inner.style.get('font-effect')
            inner.style['font-effect'] = (prev + ', ' if prev else '') + e

    # ------------------------------------------------------------------ controls / UI.Windows
    def control(self, n, c, el):
        d, chain = c['data'], c['chain']
        eid = el.attrs['id']
        if 'Button' in chain or 'Toggle' in chain:
            kind = 'toggle' if 'Toggle' in chain else 'button'
            el.attrs['u-kind'] = kind
            el.attrs['onclick'] = "require('ugui.runtime').click(element, event)"
            el.attrs['class'] = (el.attrs.get('class', '') + ' u-' + kind).strip()
            if not int(num(d.get('m_Interactable'), 1)):
                el.attrs['class'] += ' u-disabled'
            if kind == 'toggle':
                el.attrs['u-on'] = 'true' if int(num(d.get('m_IsOn'))) else 'false'
                g = self.by_comp.get(get(d, 'graphic', 'fileID'))
                if g is not None:
                    self.rules.append(('check', el, g))
            tr = int(num(d.get('m_Transition'), 1))
            if tr == 1:
                cols = d.get('m_Colors') or {}
                pressed = color(cols.get('m_PressedColor'), (0.78, 0.78, 0.78, 1))
                mult = num(cols.get('m_ColorMultiplier'), 1)
                tg = self.by_comp.get(get(d, 'm_TargetGraphic', 'fileID'))
                if tg is not None:
                    self.rules.append(('press', el, tg, tuple(min(1.0, x * mult) for x in pressed)))
            elif tr in (2, 3):
                self.iss.near('button sprite swap / animation transition', n.path)
            self.slots.append({'id': eid, 'kind': kind, 'source': n.path})
        elif 'Slider' in chain or 'Scrollbar' in chain:
            kind = 'slider' if 'Slider' in chain else 'scrollbar'
            el.attrs.update({'u-kind': kind, 'u-min': fmt(num(d.get('m_MinValue'))), 'u-max': fmt(num(d.get('m_MaxValue'), 1)),
                             'u-value': fmt(num(d.get('m_Value'))), 'u-direction': str(int(num(d.get('m_Direction'))))})
            fill = self.by_comp.get(get(d, 'm_FillRect', 'fileID')) or self.by_comp.get(get(d, 'm_HandleRect', 'fileID'))
            if fill is not None:
                self.rules.append(('ref', el, 'u-fill-id', fill))
            self.slots.append({'id': eid, 'kind': kind, 'source': n.path})
        elif 'ScrollRect' in chain:
            el.attrs['u-kind'] = 'scroll'
            vp = self.by_comp.get(get(d, 'm_Viewport', 'fileID'))
            content = self.by_comp.get(get(d, 'm_Content', 'fileID'))
            self.rules.append(('scroll', el, vp, content, int(num(d.get('m_Horizontal'), 1)), int(num(d.get('m_Vertical'), 1))))
            self.slots.append({'id': eid, 'kind': 'scroll', 'source': n.path})
        elif 'InputField' in chain or 'TMP_InputField' in chain:
            el.attrs['u-kind'] = 'input'
            self.iss.near('input field (marked; bind an <input> in Lua)', n.path)
            self.slots.append({'id': eid, 'kind': 'input', 'source': n.path})
        else:
            el.attrs['u-kind'] = 'dropdown'
            self.iss.miss(c['class'], n.path)
            self.slots.append({'id': eid, 'kind': 'dropdown', 'source': n.path})

    def module(self, n, c, el):
        cls, d = c['class'], c['data']
        if 'TextLocalizationComponentModule' in c['chain']:
            return    # read with the text
        if cls == 'TextComponentFormatModule' and d.get('format'):
            el.attrs['u-format'] = str(d.get('format'))
            return
        mods = el.attrs.get('u-modules')
        el.attrs['u-modules'] = (mods + ' ' if mods else '') + cls

    def windows_component(self, n, c, el):
        cls, chain = c['class'], c['chain']
        if cls in ('WindowLayout', 'WindowLayoutElement', 'WindowObject'):
            return
        if cls == 'WindowLayoutSafeZone':
            el.attrs['class'] = (el.attrs.get('class', '') + ' u-safe').strip()
            return
        if 'ListBaseComponent' in chain:
            el.attrs['u-kind'] = 'list'
            if el.attrs.get('u-kind') == 'list':
                self.rules.append(('list', el, n, c))
        prev = el.attrs.get('u-component')
        el.attrs['u-component'] = (prev + ' ' if prev else '') + cls
        base = next((x for x in chain if x in UIW_VIEW), 'WindowComponent')
        if cls not in UIW_VIEW:
            self.iss.logic.append({'component': cls, 'base': base, 'id': el.attrs['id'], 'source': n.path})
        self.slots.append({'id': el.attrs['id'], 'kind': 'component', 'component': cls, 'base': base, 'source': n.path})

    # ------------------------------------------------------------------ after the tree is built
    def finish(self):
        """Rules that need the elements of referenced objects. Returns extra RCSS rules."""
        css, first_done = [], set()
        for r in self.rules:
            kind = r[0]
            if kind == 'first-spacing':
                _, gid, node, prop = r
                if gid not in first_done:
                    first_done.add(gid)
                    e = self.el_of.get(node.fid)
                    if e is not None:
                        e.style.pop(prop, None)
            elif kind == 'press':
                _, btn, tg, pressed = r
                tel = self.el_of.get(tg.fid)
                img = tg.kind('Image', 'RawImage')
                if tel is None or img is None:
                    continue
                base_c = color(img['data'].get('m_Color'))
                sel = '#%s:active' % btn.attrs['id'] if tel is btn else '#%s:active #%s' % (btn.attrs['id'], tel.attrs['id'])
                prop = 'image-color' if 'decorator' in tel.style else 'background-color' if 'background-color' in tel.style else None
                if prop:
                    css.append('%s { %s: %s; }' % (sel, prop, rgba(mulc(base_c, pressed))))
            elif kind == 'check':
                _, el, g = r
                gel = self.el_of.get(g.fid)
                if gel is not None:
                    el.attrs['u-check-id'] = gel.attrs['id']
                    if el.attrs.get('u-on') == 'false':
                        gel.style['visibility'] = 'hidden'
            elif kind == 'ref':
                _, el, attr, node = r
                e = self.el_of.get(node.fid)
                if e is not None:
                    el.attrs[attr] = e.attrs['id']
            elif kind == 'scroll':
                self.scroll(*r[1:])
        return css

    def scroll(self, el, vp, content, hor, ver):
        vel = self.el_of.get(vp.fid) if vp is not None else el
        if vel is None:
            return
        vel.style.pop('overflow', None)
        vel.style['overflow-x'] = 'auto' if hor else 'hidden'
        vel.style['overflow-y'] = 'auto' if ver else 'hidden'
        cel = self.el_of.get(content.fid) if content is not None else None
        if cel is None or content.rect is None:
            return
        # scrolled content must be in flow to extend the scroll area (docs/ui-scripting.md, clipping)
        keep = {k: v for k, v in cel.style.items() if k in ('display', 'flex-direction', 'flex-wrap', 'justify-content', 'align-items',
                                                             'align-content', 'padding', 'opacity')}
        keep['position'] = 'relative'
        w, h = content.rect.size
        keep['width'] = 'auto' if content.rect.stretch_x() or cel.style.get('width') == 'auto' else dp(max(w, 0))
        keep['height'] = 'auto' if content.rect.stretch_y() or cel.style.get('height') == 'auto' else dp(max(h, 0))
        if keep['height'] == 'auto' and not ver and content.rect.stretch_y():
            keep['height'] = '100%'
        cel.style = keep
