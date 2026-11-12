"""Unity Localization string tables -> engine/i18n tables (lang/<tag>.json).

Reads SharedTableData (key ids and names), StringTable (per locale), Locale assets and LocalizationSettings
(project / startup locale). Smart strings: `{0}` -> `{arg0}`, `{name}` stays, one `{n:plural:a|b}` (or `p:`)
turns the entry into an engine plural object; other formatters (choose / cond / list / format specs) are
reported. Rich-text tags become RML spans (RmlUi parses translated text that contains markup)."""
import os
import re

from lib.unity.unity_yaml import get

import richtext

PLURAL_ORDER = {  # SmartFormat plural form order (count of forms) -> engine plural categories
    1: ['other'], 2: ['one', 'other'], 3: ['one', 'few', 'many'], 4: ['one', 'two', 'few', 'other'],
    5: ['one', 'two', 'few', 'many', 'other'], 6: ['zero', 'one', 'two', 'few', 'many', 'other']}
SELF_NAMES = {'en': 'English', 'ru': 'Русский', 'de': 'Deutsch', 'fr': 'Français', 'es': 'Español', 'it': 'Italiano',
              'pt': 'Português', 'pt-BR': 'Português (Brasil)', 'ja': '日本語', 'ko': '한국어', 'zh': '中文', 'zh-CN': '简体中文',
              'zh-TW': '繁體中文', 'tr': 'Türkçe', 'pl': 'Polski', 'uk': 'Українська', 'ar': 'العربية', 'th': 'ไทย', 'vi': 'Tiếng Việt',
              'id': 'Bahasa Indonesia', 'nl': 'Nederlands', 'sv': 'Svenska', 'cs': 'Čeština', 'hi': 'हिन्दी'}


def key_part(s):
    s = re.sub(r'[|/\\ ]+', '.', str(s)).strip('.')
    return re.sub(r'[^A-Za-z0-9._-]', '_', s) or '_'


def split_top(s, sep):
    """Split on `sep` outside braces."""
    out, depth, cur = [], 0, []
    for ch in s:
        if ch == '{':
            depth += 1
        elif ch == '}':
            depth -= 1
        if ch == sep and depth == 0:
            out.append(''.join(cur))
            cur = []
        else:
            cur.append(ch)
    out.append(''.join(cur))
    return out


def parse_smart(s):
    """-> list of str | ('ph', selector, formatter, options, format-text)."""
    out, i, buf = [], 0, []
    while i < len(s):
        ch = s[i]
        if ch == '{':
            if s.startswith('{{', i):
                buf.append('{{')
                i += 2
                continue
            depth, j = 0, i
            while j < len(s):
                if s[j] == '{':
                    depth += 1
                elif s[j] == '}':
                    depth -= 1
                    if depth == 0:
                        break
                j += 1
            if j >= len(s):
                buf.append(s[i:])
                break
            if buf:
                out.append(''.join(buf))
                buf = []
            body = s[i + 1:j]
            parts = body.split(':', 1)
            sel = parts[0].strip()
            fmt_name, opts, text = '', '', None
            if len(parts) > 1:
                rest = parts[1]
                m = re.match(r'([A-Za-z]+)(?:\(([^)]*)\))?:(.*)$', rest, re.S)
                if m:
                    fmt_name, opts, text = m.group(1), m.group(2) or '', m.group(3)
                else:
                    fmt_name, text = 'spec', rest
            out.append(('ph', sel, fmt_name, opts, text))
            i = j + 1
            continue
        if ch == '}' and s.startswith('}}', i):
            buf.append('}}')
            i += 2
            continue
        buf.append(ch)
        i += 1
    if buf:
        out.append(''.join(buf))
    return out


def ph_name(sel):
    return 'arg%s' % sel if re.fullmatch(r'\d+', sel) else sel


class Converter:
    """Smart string -> engine pattern (str) or plural object; `issues` collects what changed meaning."""

    def __init__(self):
        self.issues = []

    def flat(self, parts, form=None, key=''):
        out = []
        for p in parts:
            if isinstance(p, str):
                out.append(p)
                continue
            _, sel, f, opts, text = p
            name = ph_name(sel)
            if f in ('plural', 'p') and text is not None:
                forms = split_top(text, '|')
                pick = forms[min(form, len(forms) - 1)] if form is not None else forms[-1]
                out.append(self.flat(parse_smart(pick), form, key))
            elif f in ('', None):
                out.append('{%s}' % name)
            elif f == 'spec':
                self.issues.append('%s: number format {%s:%s} dropped (format the value before passing it)' % (key, sel, text))
                out.append('{%s}' % name)
            elif f in ('cond', 'conditional', 'choose', 'c') and text is not None:
                first = split_top(text, '|')[0]
                self.issues.append('%s: {%s:%s:...} kept only its first branch' % (key, sel, f))
                out.append(self.flat(parse_smart(first), form, key))
            else:
                self.issues.append('%s: smart formatter "%s" is not supported (placeholder kept)' % (key, f))
                out.append('{%s}' % name)
        return ''.join(out)

    def convert(self, s, key, smart):
        if not smart:
            # String.Format style: positional only
            out = re.sub(r'\{(\d+)(?::[^}]*)?\}', lambda m: '{arg%s}' % m.group(1), s)
            if re.search(r'\{\d+:[^}]*\}', s):
                self.issues.append('%s: number format dropped' % key)
            return out
        parts = parse_smart(s)
        plurals = [p for p in parts if not isinstance(p, str) and p[2] in ('plural', 'p')]
        nested = [p for p in parts if not isinstance(p, str) and p[4] and re.search(r'\{[^}]*:(plural|p):', p[4])]
        if not plurals and not nested:
            return self.flat(parts, None, key)
        sels = {p[1] for p in plurals}
        if len(sels) > 1:
            self.issues.append('%s: plurals of several values; the form follows the first numeric argument' % key)
        n = max(len(split_top(p[4], '|')) for p in plurals) if plurals else 2
        cats = PLURAL_ORDER.get(n, PLURAL_ORDER[2])
        obj = {}
        for i, cat in enumerate(cats):
            obj[cat] = self.flat(parts, i, key)
        if 'other' not in obj:
            obj['other'] = obj[cats[-1]]
        return obj


def _class(d):
    return str(get(d.data, 'm_EditorClassIdentifier', default='') or '')


class Tables:
    """All string tables of a Unity project."""

    def __init__(self, assets, roots, report, prefix=''):
        self.assets, self.rep, self.prefix = assets, report, prefix
        self.shared = {}    # shared guid -> {'name', 'entries': {id: key}, 'collection_guid'}
        self.by_name = {}
        self.tables = []    # {'code', 'shared', 'entries': {id: (text, smart)}}
        self.locales = {}   # code -> name
        self.project_locale, self.startup = None, []
        self.conv = Converter()
        for path in sorted(p for p in assets.by_guid.values() if p.endswith('.asset')):
            try:
                with open(path, 'r', encoding='utf-8', errors='replace') as f:
                    head = f.read(1500)
            except OSError:
                continue
            if 'UnityEngine.Localization' not in head:
                continue
            for d in assets.docs(path):
                self._read(path, d)

    def _read(self, path, d):
        ec, data = _class(d), d.data or {}
        if ec.endswith('SharedTableData') or ('m_TableCollectionName' in data and 'm_Entries' in data):
            g = self.assets.guid_of(path)
            entries = {get(e, 'm_Id'): str(get(e, 'm_Key', default='')) for e in data.get('m_Entries') or [] if isinstance(e, dict)}
            info = {'name': str(data.get('m_TableCollectionName') or os.path.basename(path)), 'entries': entries, 'guid': g}
            self.shared[g] = info
            self.by_name[info['name']] = info
            cg = str(data.get('m_TableCollectionNameGuidString') or '')
            if cg:
                self.shared.setdefault(cg.lower(), info)
        elif ec.endswith('.StringTable') or ('m_TableData' in data and 'm_SharedData' in data):
            code = str(get(data, 'm_LocaleId', 'm_Code', default='') or '')
            smart_ids = set()
            refs = {r.get('rid'): r for r in get(data, 'references', 'RefIds', default=[]) or [] if isinstance(r, dict)}
            for r in refs.values():
                if get(r, 'type', 'class') == 'SmartFormatTag':
                    smart_ids |= {get(e, 'id') for e in get(r, 'data', 'm_SharedEntries', default=[]) or [] if isinstance(e, dict)}
            entries = {}
            for e in data.get('m_TableData') or []:
                if not isinstance(e, dict):
                    continue
                eid = get(e, 'm_Id')
                smart = eid in smart_ids or any(get(refs.get(get(it, 'rid')), 'type', 'class') == 'SmartFormatTag'
                                                for it in get(e, 'm_Metadata', 'm_Items', default=[]) or [] if isinstance(it, dict))
                text = e.get('m_Localized')
                entries[eid] = ('' if text is None else str(text), smart)
            self.tables.append({'code': code, 'shared': str(get(data, 'm_SharedData', 'guid', default='')).lower(), 'entries': entries,
                                'file': os.path.basename(path)})
        elif ec.endswith('.Locale') or ('m_Identifier' in data and 'm_LocaleName' in data):
            code = str(get(data, 'm_Identifier', 'm_Code', default='') or '')
            if code:
                self.locales[code] = str(data.get('m_LocaleName') or code)
        elif ec.endswith('LocalizationSettings') or 'm_ProjectLocaleIdentifier' in data:
            self.project_locale = str(get(data, 'm_ProjectLocaleIdentifier', 'm_Code', default='') or '') or None
            for r in get(data, 'references', 'RefIds', default=[]) or []:
                if isinstance(r, dict) and get(r, 'type', 'class') == 'SpecificLocaleSelector':
                    c = get(r, 'data', 'm_LocaleId', 'm_Code')
                    if c:
                        self.startup.append(str(c))

    # ------------------------------------------------------------------ keys
    def engine_key_of(self, coll, key):
        return '%s%s.%s' % (self.prefix, key_part(coll), key_part(key))

    def collection(self, table_ref):
        name = str(get(table_ref, 'm_TableCollectionName', default='') or '')
        if name.startswith('GUID:'):
            return self.shared.get(name[5:].lower())
        return self.by_name.get(name)

    def engine_key(self, ref):
        """LocalizedString {m_TableReference, m_TableEntryReference} -> engine key or None."""
        coll = self.collection(get(ref, 'm_TableReference') or {})
        if not coll:
            return None
        er = get(ref, 'm_TableEntryReference') or {}
        kid, kname = get(er, 'm_KeyId'), str(get(er, 'm_Key', default='') or '')
        key = coll['entries'].get(kid) if kid else None
        if key is None and kname and kname in coll['entries'].values():
            key = kname
        return self.engine_key_of(coll['name'], key) if key else None

    # ------------------------------------------------------------------ output
    def languages(self):
        codes = sorted({t['code'] for t in self.tables if t['code']} | set(self.locales))
        src = (self.startup[0] if self.startup else None) or self.project_locale or ('en' if 'en' in codes else (codes[0] if codes else 'en'))
        return src, [src] + [c for c in codes if c != src]

    def build(self):
        """-> ({code: table json}, keymap rows, stats)."""
        out, keymap, stats = {}, [], {}
        tagged = 0
        for t in sorted(self.tables, key=lambda t: (t['code'], t['file'])):
            sh = self.shared.get(t['shared'])
            if not sh:
                self.rep.warnings.append('string table %s: shared table data not found' % t['file'])
                continue
            dst = out.setdefault(t['code'], {})
            st = stats.setdefault(t['code'], {'entries': 0, 'empty': 0, 'plural': 0})
            for eid, (text, smart) in sorted(t['entries'].items(), key=lambda kv: str(sh['entries'].get(kv[0], ''))):
                key = sh['entries'].get(eid)
                if key is None:
                    self.rep.warnings.append('%s: entry %s has no key in the shared table' % (t['file'], eid))
                    continue
                if text == '':
                    st['empty'] += 1
                    continue
                ek = self.engine_key_of(sh['name'], key)
                val = self.conv.convert(text, ek, smart)
                if richtext.has_tags(text):
                    tagged += 1
                    val = {k: richtext.to_rml(v) for k, v in val.items()} if isinstance(val, dict) else richtext.to_rml(val)
                if isinstance(val, dict):
                    st['plural'] += 1
                dst[ek] = val
                st['entries'] += 1
        for g, sh in sorted(self.shared.items()):
            if g != sh['guid']:
                continue
            for eid, key in sorted(sh['entries'].items(), key=lambda kv: kv[1]):
                keymap.append({'table': sh['name'], 'key': key, 'id': eid, 'engine': self.engine_key_of(sh['name'], key)})
        if tagged:
            self.rep.notes.append('%d translations have rich-text tags: converted to RML spans' % tagged)
        self.rep.approx += self.conv.issues
        return out, keymap, stats

    def write(self, out_dir, merge=True):
        import json
        tables, keymap, stats = self.build()
        os.makedirs(os.path.join(out_dir, 'lang'), exist_ok=True)
        src, langs = self.languages()
        written = []
        for code, entries in sorted(tables.items()):
            path = os.path.join(out_dir, 'lang', '%s.json' % code)
            data = {}
            if merge and os.path.exists(path):
                with open(path, 'r', encoding='utf-8') as f:
                    data = json.load(f)
            data.setdefault('@language', SELF_NAMES.get(code, self.locales.get(code, code)))
            data.update(entries)
            with open(path, 'w', encoding='utf-8') as f:
                json.dump(data, f, ensure_ascii=False, indent=1, sort_keys=False)
                f.write('\n')
            written.append('lang/%s.json' % code)
        os.makedirs(os.path.join(out_dir, 'ui', 'ugui'), exist_ok=True)
        with open(os.path.join(out_dir, 'ui', 'ugui', 'l10n-keys.json'), 'w', encoding='utf-8') as f:
            json.dump({'sourceLanguage': src, 'languages': langs, 'keys': keymap}, f, ensure_ascii=False, indent=1)
            f.write('\n')
        return {'sourceLanguage': src, 'languages': langs, 'files': written, 'stats': stats,
                'startupSelector': self.startup, 'collections': sorted({s['name'] for s in self.shared.values()})}
