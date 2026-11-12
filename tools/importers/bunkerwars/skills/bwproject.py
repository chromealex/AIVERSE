"""Index of a Bunker Wars client checkout: asset guids, C# classes (bases, interfaces, interface-property
backing fields) and the GameData table (GDFloat / GDInt keys)."""
import glob
import os
import re

import unityyaml


class CsClass:
    def __init__(self, name, bases, props, fields):
        self.name = name
        self.bases = bases          # base class + interfaces as written
        self.props = props          # property / explicit interface member -> backing field path (dotted)
        self.fields = fields        # serialized field -> default literal (strings, as written)


_CLASS = re.compile(r'\bclass\s+(\w+)\s*(?:<[^>]*>)?\s*(?::\s*([^{]+))?\{')
_PROP = re.compile(r'(?:public\s+)?[\w\.<>\[\]]+\s+((?:I\w+(?:<\w+>)?\.)?\w+)\s*\{\s*get\s*(?:=>\s*|\{\s*return\s+)(?:\(\w+\))?\s*this\.([\w\.]+)', re.S)
_FIELD = re.compile(r'^\s*(?:\[[^\]]*\]\s*)*public\s+(?!static|const|override|virtual|abstract|readonly|enum|class|struct|interface|event)([\w\.<>\[\], ]+?)\s+(\w+)\s*(?:=\s*([^;]+))?;', re.M)


class Project:
    def __init__(self, assets):
        self.assets = assets                     # <client>/Client/Assets
        self.guid_path = {}                      # asset guid -> path
        self.script_class = {}                   # script guid -> class name
        self.classes = {}                        # class name -> CsClass
        self.gd = {}                             # GameData key (lower case) -> value string
        self._docs = {}

    # ------------------------------------------------------------------ indexing
    def index_assets(self, *dirs):
        for d in dirs:
            for meta in glob.glob(os.path.join(self.assets, d, '**', '*.meta'), recursive=True):
                try:
                    with open(meta, encoding='utf-8', errors='replace') as f:
                        m = re.search(r'^guid: (\w+)', f.read(), re.M)
                except OSError:
                    continue
                if m:
                    self.guid_path[m.group(1)] = meta[:-5]

    def index_scripts(self, *dirs):
        for d in dirs:
            for cs in glob.glob(os.path.join(self.assets, d, '**', '*.cs'), recursive=True):
                meta = cs + '.meta'
                if os.path.exists(meta):
                    with open(meta, encoding='utf-8', errors='replace') as f:
                        m = re.search(r'^guid: (\w+)', f.read(), re.M)
                    if m:
                        self.script_class[m.group(1)] = os.path.basename(cs)[:-3]
                with open(cs, encoding='utf-8-sig', errors='replace') as f:
                    src = f.read()
                self._read_classes(src)

    def _read_classes(self, src):
        heads = list(_CLASS.finditer(src))
        for m in heads:
            depth, j = 1, m.end()
            while j < len(src) and depth:   # the class body up to its matching brace (nested classes included)
                depth += {'{': 1, '}': -1}.get(src[j], 0)
                j += 1
            body = src[m.end(): j]
            name = m.group(1)
            bases = [b.strip().split('.')[-1] for b in (m.group(2) or '').split(',') if b.strip()]
            bases = [re.sub(r'<.*', '', b) if not b.startswith('IModifiableValue') else b for b in bases]
            props = {}
            for a, b in _PROP.findall(body):
                props[a] = b
            fields = {}
            for fm in _FIELD.finditer(body):
                fields[fm.group(2)] = (fm.group(3) or '').strip()
            if name in self.classes:   # nested classes share names (Modificator): keep the first one
                continue
            self.classes[name] = CsClass(name, bases, props, fields)

    def load_gamedata(self, path):
        if not os.path.exists(path):
            return
        with open(path, encoding='utf-8', errors='replace') as f:
            lines = f.read().split('\n')
        try:
            vi = next(i for i, l in enumerate(lines) if l.strip() == 'values:' and l.startswith('    values'))
            ki = next(i for i, l in enumerate(lines) if l.strip() == 'keys:')
        except StopIteration:
            return
        vals, keys = [], []
        for l in lines[vi + 1: ki]:
            s = l.strip()
            if s.startswith('- ') and l.startswith('      -'):
                vals.append(s[2:])
        for l in lines[ki + 1:]:
            s = l.strip()
            if not s.startswith('- '):
                break
            keys.append(s[2:])
        for k, v in zip(keys, vals):
            self.gd[k.lower()] = v

    # ------------------------------------------------------------------ queries
    def docs(self, path):
        if path not in self._docs:
            self._docs[path] = unityyaml.load(path)
        return self._docs[path]

    def path_of(self, ref):
        if isinstance(ref, dict):
            return self.guid_path.get(ref.get('guid', ''))
        return None

    def class_of(self, body):
        s = body.get('m_Script') if isinstance(body, dict) else None
        return self.script_class.get(s.get('guid', ''), '?') if isinstance(s, dict) else '?'

    def lineage(self, cls):
        """The class and its bases (classes and interfaces), nearest first."""
        out, todo = [], [cls]
        while todo:
            c = todo.pop(0)
            if c in out:
                continue
            out.append(c)
            info = self.classes.get(c)
            if info:
                todo.extend(info.bases)
        return out

    def implements(self, cls, iface):
        return iface in self.lineage(cls)

    def prop_field(self, cls, prop):
        """Backing field path of a property (searching bases), e.g. effectForce -> value.value."""
        for c in self.lineage(cls):
            info = self.classes.get(c)
            if not info:
                continue
            for k, v in info.props.items():
                if k == prop or k.endswith('.' + prop):
                    return v
        return None

    def gd_value(self, key, default=0.0):
        if not key:
            return default
        return unityyaml.num(self.gd.get(str(key).lower()), default)
