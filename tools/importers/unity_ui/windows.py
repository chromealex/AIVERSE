"""UI.Windows screens -> one RML document each.

A UI.Windows screen (WindowBase prefab) names its layouts (WindowLayout prefabs: structure + WindowLayoutElement
slots with a tagId) and, per slot tag, the component prefab that fills it. The document is the layout tree with
each component prefab converted inside its slot element (`u-slot`), so Lua logic can find blocks by slot name
and controls by id. ListComponent sources are converted once as a hidden template row."""
import os

from lib.unity.unity_yaml import get, num
from lib.unity.unity_scene import Prefab

from convert import Converter, Issues, build_tree
from dom import El
from media import ident

DEFAULT_SCALER = {'mode': 1, 'ref': (1920.0, 1080.0), 'match': 0.0, 'screenMatch': 0, 'ppu': 100.0}


def scaler_of(canvas_scaler=None, prefs=None):
    s = dict(DEFAULT_SCALER)
    if canvas_scaler:
        r = canvas_scaler.get('m_ReferenceResolution') or {}
        s.update({'mode': int(num(canvas_scaler.get('m_UiScaleMode'), 1)), 'ref': (num(r.get('x'), 1920), num(r.get('y'), 1080)),
                  'match': num(canvas_scaler.get('m_MatchWidthOrHeight')), 'screenMatch': int(num(canvas_scaler.get('m_ScreenMatchMode'))),
                  'ppu': num(canvas_scaler.get('m_ReferencePixelsPerUnit'), 100)})
    if prefs:
        r = prefs.get('referenceResolution') or {}
        s.update({'mode': int(num(prefs.get('uiScaleMode'), s['mode'])), 'ref': (num(r.get('x'), s['ref'][0]), num(r.get('y'), s['ref'][1])),
                  'match': num(prefs.get('matchWidthOrHeight'), s['match']), 'screenMatch': int(num(prefs.get('screenMatchMode'), s['screenMatch'])),
                  'ppu': num(prefs.get('referencePixelsPerUnit'), s['ppu'])})
    return s


def comp_with(prefab, key):
    for d in prefab.docs:
        if isinstance(d.data, dict) and key in d.data:
            return d
    return None


class ScreenDoc:
    def __init__(self, name, source):
        self.name, self.source = name, source
        self.iss = Issues()
        self.css, self.slots, self.layouts, self.blocks = [], [], [], []
        self.scaler = dict(DEFAULT_SCALER)
        self.body = None


class Builder:
    def __init__(self, assets, scripts, media, l10n, report):
        self.assets, self.scripts, self.media, self.l10n, self.rep = assets, scripts, media, l10n, report

    def prefab(self, path):
        return Prefab(path, self.assets, self.rep)

    def ref_path(self, ref):
        """UI.Windows Resource {type, address, guid, directRef} -> asset path."""
        if not isinstance(ref, dict):
            return None
        for g in (ref.get('guid'), get(ref, 'directRef', 'guid')):
            if g:
                p = self.assets.path(g)
                if p:
                    return p
        addr = ref.get('address')
        if addr:
            base = self.assets_root()
            if base:
                p = os.path.join(os.path.dirname(base), str(addr))
                if os.path.exists(p):
                    return p
        return None

    def assets_root(self):
        for p in self.assets.by_guid.values():
            i = p.find(os.sep + 'Assets' + os.sep)
            if i >= 0:
                return p[:i + 7]
        return None

    # ------------------------------------------------------------------ one prefab into an element
    def convert_prefab(self, doc, path, parent_el, prefix, shared, extra=None):
        """Converts a prefab's roots into parent_el. `shared` = the document's Converter (ids, issues)."""
        pf = self.prefab(path)
        for note in pf.nested_failed:
            doc.iss.near('nested prefab not flattened', note)
        roots = build_tree(pf, self.scripts)
        conv = Converter(self.media, self.l10n, doc.iss, prefix)
        conv.ids, conv.slots, conv.ref_ppu = shared.ids, shared.slots, shared.ref_ppu
        conv.index(roots)
        els = []
        for r in roots:
            el = conv.node(r, root=True, extra=extra(conv) if extra else None)
            parent_el.add(el)
            els.append((r, el))
        doc.css += conv.finish()
        return pf, els

    def list_templates(self, doc, shared):
        def factory(conv):
            def extra(n, el):
                lc = n.kind('ListBaseComponent')
                if not lc:
                    return
                src = lc['data'].get('source') or {}
                g = src.get('guid') or get(src, 'directRef', 'guid')
                if not g:
                    local = conv.by_comp.get(get(src, 'directRef', 'fileID'))
                    if local is not None and local.fid in conv.el_of:
                        tel = conv.el_of[local.fid]
                        el.attrs['u-template'] = tel.attrs['id']
                    return
                p = self.assets.path(g)
                if not p:
                    doc.iss.miss('list item prefab not found', n.path)
                    return
                root = conv.by_comp.get(get(lc['data'], 'customRoot', 'fileID'))
                holder = conv.el_of.get(root.fid) if root is not None else el
                holder = holder or el
                tpl = El('div')
                tpl.attrs.update({'class': 'u-template', 'u-template-of': el.attrs['id']})
                tpl.style.update({'display': 'none', 'position': 'relative'})
                _, els = self.convert_prefab(doc, p, tpl, ident(os.path.splitext(os.path.basename(p))[0]), shared, factory)
                for r, iel in els:
                    if r.rect is not None:    # a pooled row: authored size, in flow
                        iel.style = {k: v for k, v in iel.style.items() if k not in ('left', 'right', 'top', 'bottom', 'margin-left',
                                                                                  'margin-right', 'margin-top', 'margin-bottom')}
                        iel.style['position'] = 'relative'
                holder.add(tpl)
                el.attrs['u-template'] = tpl.attrs['u-template-of'] + '-row'
                tpl.attrs['id'] = el.attrs['u-template']
            return extra
        return factory

    # ------------------------------------------------------------------ screens
    def is_screen(self, path):
        pf = self.prefab(path)
        d = comp_with(pf, 'layouts')
        return bool(d and isinstance(get(d.data, 'layouts', 'items'), list))

    def screen(self, path, name=None):
        name = name or ident(os.path.splitext(os.path.basename(path))[0])
        doc = ScreenDoc(name, path)
        pf = self.prefab(path)
        wd = comp_with(pf, 'layouts')
        canvas = El('div')
        canvas.attrs.update({'id': 'ugui-canvas', 'class': 'u-canvas'})
        shared = Converter(self.media, self.l10n, doc.iss, '')
        shared.ids.add('ugui-canvas')
        doc.body = canvas
        if wd is None:
            # a plain uGUI prefab (Canvas root): convert it as the whole screen
            scaler = comp_with(pf, 'm_ReferenceResolution')
            doc.scaler = scaler_of(scaler.data if scaler else None)
            shared.ref_ppu = doc.scaler['ppu']
            self.convert_prefab(doc, path, canvas, '', shared, self.list_templates(doc, shared))
            doc.slots = shared.slots
            return doc
        script = self.scripts.name(get(wd.data, 'm_Script'))
        doc.window_class = script
        for item in get(wd.data, 'layouts', 'items', default=[]) or []:
            lref = get(item, 'windowLayout') or {}
            lp = self.assets.path(lref)
            if not lp:
                if get(lref, 'guid'):
                    doc.iss.miss('layout prefab not found', name)
                continue
            prefs = None
            pref_obj, _ = self.assets.object(get(item, 'layoutPreferences') or {}) if get(item, 'layoutPreferences', 'guid') else (None, None)
            if pref_obj is not None:
                prefs = get(pref_obj.data, 'canvasScalerData')
            lpf = self.prefab(lp)
            cs = comp_with(lpf, 'm_ReferenceResolution')
            doc.scaler = scaler_of(cs.data if cs else None, prefs)
            shared.ref_ppu = doc.scaler['ppu']
            by_tag = {}
            for c in get(item, 'components', default=[]) or []:
                p = self.ref_path(get(c, 'component') or {})
                if p:
                    by_tag[int(num(c.get('tag')))] = p
            doc.layouts.append(os.path.basename(lp))
            used = set()
            factory = self.list_templates(doc, shared)

            def slot_factory(conv, by_tag=by_tag, used=used):
                inner = factory(conv)

                def extra(n, el):
                    inner(n, el)
                    le = n.first('WindowLayoutElement') or n.kind('WindowLayoutElement')
                    if not le:
                        return
                    tag = int(num(le['data'].get('tagId')))
                    el.attrs['u-slot'] = ident(n.name)
                    el.attrs['class'] = (el.attrs.get('class', '') + ' u-slot').strip()
                    p = by_tag.get(tag)
                    if not p:
                        return
                    used.add(tag)
                    prefix = ident(n.name)
                    self.convert_prefab(doc, p, el, prefix, shared, factory)
                    doc.blocks.append({'slot': ident(n.name), 'tag': tag, 'component': os.path.basename(p), 'id': el.attrs['id']})
                return extra

            self.convert_prefab(doc, lp, canvas, '', shared, slot_factory)
            for tag, p in sorted(by_tag.items()):
                if tag not in used:
                    doc.iss.miss('component mapped to a missing layout slot', '%s (tag %d)' % (os.path.basename(p), tag))
        doc.slots = shared.slots
        return doc
