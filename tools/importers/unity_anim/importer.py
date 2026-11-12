#!/usr/bin/env python3
"""Unity character animation -> AIVERSE (clips, clip sidecars, controller graphs).

    python3 tools/importers/unity_anim/importer.py SOURCE --out DIR [--assets ASSETS_DIR]
            [--clipinfo build/<cfg>/aiverse_clipinfo] [--scripts DIR ...] [--json]

SOURCE is a Unity folder (any subfolder of Assets; GUIDs resolve against the enclosing Assets folder).
For every Animator in the prefabs and every .controller / .overrideController under SOURCE it writes

    DIR/models/<model>/<file>.fbx            the model the Animator's Avatar comes from (copied)
    DIR/models/<model>/<file>.fbx.clips.json clip sidecar: FBX take splits (ModelImporter clipAnimations,
                                             frame ranges, root motion) + external clips
    DIR/animations/clips/<clip>.glb          standalone .anim clips as glTF animations
    DIR/animations/<controller>.json         AnimationGraph with one "controller" node (Mecanim layers,
                                             states, blend trees, transitions, masks, parameters)
    DIR/unity_anim_report.{json,md}          what was converted, approximated, dropped, and which C#
                                             scripts drive each Animator parameter

Copied models are the user's own assets: keep DIR outside the repository. With --clipinfo every model is
loaded through the engine's real loader and every graph is compiled against it (dry run). Docs:
docs/unity-animation.md."""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))
import anim as A  # noqa: E402
import controller as CT  # noqa: E402
from lib.unity import unity_yaml as Y  # noqa: E402

MODEL_EXT = ('.fbx', '.obj', '.dae', '.gltf', '.glb', '.blend')


def safe(name):
    return re.sub(r'[^A-Za-z0-9_.-]+', '_', name).strip('_') or 'item'


class Index:
    """GUID -> asset path over an Assets folder (reads only the head of every .meta)."""

    def __init__(self, root):
        self.root, self.by_guid, self.guid_of = root, {}, {}
        for dp, dns, fns in os.walk(root):
            dns[:] = sorted(d for d in dns if d not in ('Library', 'Temp', 'Logs', '.git'))
            for fn in fns:
                if not fn.endswith('.meta'):
                    continue
                try:
                    with open(os.path.join(dp, fn), 'r', encoding='utf-8', errors='replace') as f:
                        head = f.read(512)
                except OSError:
                    continue
                m = re.search(r'^guid:\s*([0-9a-fA-F]{32})', head, re.M)
                if m:
                    p = os.path.join(dp, fn[:-5])
                    self.by_guid[m.group(1).lower()] = p
                    self.guid_of[os.path.abspath(p)] = m.group(1).lower()
        self.docs_cache = {}

    def path(self, guid):
        return self.by_guid.get((guid or '').lower())

    def docs(self, path):
        if path not in self.docs_cache:
            try:
                with open(path, 'r', encoding='utf-8', errors='replace') as f:
                    self.docs_cache[path] = Y.parse_documents(f.read())
            except (OSError, ValueError):
                self.docs_cache[path] = []
        return self.docs_cache[path]

    def meta(self, path):
        if not os.path.exists(path + '.meta'):
            return {}
        try:
            with open(path + '.meta', 'r', encoding='utf-8', errors='replace') as f:
                return Y.parse_block_text(f.read())
        except (OSError, ValueError):
            return {}

    def rel(self, path):
        return os.path.relpath(path, self.root).replace(os.sep, '/')


class ModelInfo:
    """A model file and its ModelImporter clip settings."""

    def __init__(self, index, path):
        self.path = path
        meta = index.meta(path)
        mi = meta.get('ModelImporter') or {}
        an = mi.get('animations') or {}
        self.motion_node = an.get('motionNodeName') or ''
        self.rig = {0: 'none', 1: 'legacy', 2: 'generic', 3: 'humanoid'}.get(int(Y.num(mi.get('animationType'), 2)), 'generic')
        self.import_animation = bool(mi.get('importAnimation', 1))
        self.splits = an.get('clipAnimations') or []
        self.ids = {}   # fileID -> clip name
        for e in mi.get('internalIDToNameTable') or []:
            first = e.get('first') or {}
            for cls, fid in first.items():
                if str(cls) == '74':
                    self.ids[CT.fid(fid)] = e.get('second') or ''
        recycle = mi.get('fileIDToRecycleName') or {}
        if isinstance(recycle, dict):
            for k, v in recycle.items():
                if str(k).startswith('74'):
                    self.ids[CT.fid(k)] = v
        for s in self.splits:
            if s.get('internalID') is not None:
                self.ids[CT.fid(s.get('internalID'))] = s.get('name') or ''

    def clip_name(self, fid):
        if fid in self.ids:
            return self.ids[fid]
        if 7400000 <= fid < 7400000 + 2 * max(1, len(self.splits)) and (fid - 7400000) % 2 == 0:
            if self.splits:
                return self.splits[(fid - 7400000) // 2].get('name')
            if fid == 7400000:
                return ''   # the first take of the file (name known only to the loader)
        return None

    def sidecar_entries(self):
        out = []
        for s in self.splits:
            e = {'name': s.get('name') or s.get('takeName') or 'Clip', 'take': s.get('takeName') or '',
                 'frames': [Y.num(s.get('firstFrame'), 0), Y.num(s.get('lastFrame'), 0)]}
            rm = root_motion(self.motion_node, bool(s.get('loopBlendOrientation')), bool(s.get('loopBlendPositionY')), bool(s.get('loopBlendPositionXZ')))
            if rm:
                e['rootMotion'] = rm
            out.append(e)
        return out

    def split(self, name):
        return next((s for s in self.splits if s.get('name') == name), None)


def root_motion(node, rot, y, xz):
    if not node or (rot and y and xz):
        return None
    return {'node': node, 'bakeRotation': rot, 'bakeY': y, 'bakeXZ': xz}


def scan_drivers(dirs):
    """Animator parameter / layer name -> C# files that set it (StringToHash / Set* / layer calls)."""
    pat = re.compile(r'(?:StringToHash|Set(?:Float|Bool|Integer|Trigger|LayerWeight)|ResetTrigger|GetLayerIndex|Play|CrossFade(?:InFixedTime)?)\s*\(\s*"([^"]+)"')
    out = {}
    for d in dirs:
        for dp, dns, fns in os.walk(d):
            dns[:] = [x for x in dns if x not in ('Library', 'Temp', '.git')]
            for fn in fns:
                if not fn.endswith('.cs'):
                    continue
                p = os.path.join(dp, fn)
                try:
                    with open(p, 'r', encoding='utf-8', errors='replace') as f:
                        text = f.read()
                except OSError:
                    continue
                if 'Animator' not in text:
                    continue
                for m in pat.finditer(text):
                    out.setdefault(m.group(1), set()).add(os.path.relpath(p, d).replace(os.sep, '/'))
    return {k: sorted(v) for k, v in out.items()}


class Importer:
    def __init__(self, source, out, assets=None, clipinfo=None, scripts=None):
        self.source = os.path.abspath(source)
        self.assets = os.path.abspath(assets or self._assets_root(self.source))
        self.out = os.path.abspath(out)
        if self.out.startswith(self.source + os.sep) or self.out == self.source:
            raise ValueError('output must not be inside the source folder')
        self.index = Index(self.assets)
        self.clipinfo = clipinfo
        self.scripts = scripts if scripts is not None else [self.assets]
        self.models = {}       # model path -> {'info', 'clips': {key: res}, 'names': set, 'entries': [...]}
        self.anim_cache = {}   # .anim path -> (clip dict, glb rel path or None)
        self.files = set()
        self.report = {'source': self.source, 'models': [], 'controllers': [], 'clips': [], 'skipped': [], 'warnings': []}

    @staticmethod
    def _assets_root(p):
        q = p
        while True:
            if os.path.basename(q) == 'Assets':
                return q
            parent = os.path.dirname(q)
            if parent == q:
                return p
            q = parent

    # ---------------------------------------------------------------- discovery
    def animators(self):
        """(prefab, controller guid, avatar guid or None, applyRootMotion) for every Animator under SOURCE."""
        found = []
        for dp, dns, fns in os.walk(self.source):
            dns.sort()
            for fn in sorted(fns):
                if not fn.endswith('.prefab'):
                    continue
                p = os.path.join(dp, fn)
                try:
                    with open(p, 'r', encoding='utf-8', errors='replace') as f:
                        text = f.read()
                except OSError:
                    continue
                ctrls = re.findall(r'm_Controller: \{fileID: -?\d+, guid: ([0-9a-f]{32})', text)
                ctrls += re.findall(r'propertyPath: m_Controller\s+value:[^\n]*\n\s+objectReference: \{fileID: -?\d+, guid: ([0-9a-f]{32})', text)
                if not ctrls:
                    continue
                avatars = self._avatars(text, 0)
                root = 'm_ApplyRootMotion: 1' in text
                for c in dict.fromkeys(ctrls):
                    found.append((p, c, avatars[0] if avatars else None, root))
        return found

    def _avatars(self, text, depth):
        av = re.findall(r'm_Avatar: \{fileID: 9000000, guid: ([0-9a-f]{32})', text)
        av += re.findall(r'propertyPath: m_Avatar\s+value:[^\n]*\n\s+objectReference: \{fileID: 9000000, guid: ([0-9a-f]{32})', text)
        if not av and depth < 5:   # nested prefab / variant: the base prefab or the model instance carries it
            for g in re.findall(r'm_SourcePrefab: \{fileID: \d+, guid: ([0-9a-f]{32})', text):
                src = self.index.path(g)
                if not src:
                    continue
                if src.lower().endswith(MODEL_EXT):
                    av.append(g)
                elif src.endswith('.prefab'):
                    try:
                        with open(src, 'r', encoding='utf-8', errors='replace') as f:
                            av += self._avatars(f.read(), depth + 1)
                    except OSError:
                        pass
        return list(dict.fromkeys(av))

    # ---------------------------------------------------------------- clips
    def model(self, path):
        if path not in self.models:
            self.models[path] = {'info': ModelInfo(self.index, path), 'clips': {}, 'names': set(), 'entries': []}
        return self.models[path]

    def anim(self, path):
        if path not in self.anim_cache:
            try:
                clip = A.load(path)
            except (OSError, ValueError) as e:
                self.report['warnings'].append(str(e))
                self.anim_cache[path] = None
                return None
            kind = A.kind_of(clip)
            rel, info = None, {}
            if kind == 'transform':
                data, info = A.to_glb(clip)
                if data:
                    rel = 'animations/clips/%s.glb' % self._unique_file(safe(clip['name']))
                    dst = os.path.join(self.out, rel)
                    os.makedirs(os.path.dirname(dst), exist_ok=True)
                    with open(dst, 'wb') as f:
                        f.write(data)
            self.report['clips'].append({'source': self.index.rel(path), 'name': clip['name'], 'kind': kind, 'file': rel,
                                         'loop': clip['loop'], 'duration': info.get('duration'), 'channels': info.get('channels', 0),
                                         'cubic': info.get('cubic', 0), 'resampled': info.get('resampled', 0),
                                         'unsupported': A.unsupported(clip), 'notes': info.get('notes', [])})
            self.anim_cache[path] = (clip, rel)
        return self.anim_cache[path]

    def _unique_file(self, base):
        name, i = base, 2
        while name.lower() in self.files:
            name, i = '%s_%d' % (base, i), i + 1
        self.files.add(name.lower())
        return name

    def resolver(self, model_path):
        """Motion reference -> {'name', 'loop'} registered in the model's clip sidecar."""
        M = self.model(model_path) if model_path else None

        def unique(name):
            if M is None:
                return name
            n, i = name, 2
            while n in M['names']:
                n, i = '%s_%d' % (name, i), i + 1
            M['names'].add(n)
            return n

        def relfile(rel):
            return os.path.relpath(os.path.join(self.out, rel), self.model_dir(model_path)).replace(os.sep, '/')

        def resolve(key):
            guid, fid = key
            src = self.index.path(guid)
            if not src:
                return None
            if M is not None and key in M['clips']:
                return M['clips'][key]
            ext = os.path.splitext(src)[1].lower()
            if ext == '.anim':
                got = self.anim(src)
                if not got or not got[1]:
                    return None
                clip, rel = got
                if M is None:
                    return {'name': clip['name'], 'loop': clip['loop']}
                res = {'name': unique(clip['name']), 'loop': clip['loop']}
                entry = {'name': res['name'], 'file': relfile(rel), 'clip': clip['name']}
                rm = root_motion(M['info'].motion_node, clip['bakeRotation'], clip['bakeY'], clip['bakeXZ'])
                if rm:
                    entry['rootMotion'] = rm
                M['entries'].append(entry)
                M['clips'][key] = res
                return res
            if ext in MODEL_EXT:
                info = ModelInfo(self.index, src)
                name = info.clip_name(fid)
                if name is None:
                    self.report['warnings'].append('clip fileID %d not found in %s.meta' % (fid, self.index.rel(src)))
                    return None
                split = info.split(name)
                loop = bool(split.get('loopTime')) if split else False
                if M is None:
                    return {'name': name or 'Take 001', 'loop': loop}
                if os.path.abspath(src) == os.path.abspath(model_path) and name:
                    res = {'name': name, 'loop': loop}
                    M['names'].add(name)
                else:   # a clip of another FBX: copy it with its own sidecar, reference it by file
                    rel = self.copy_model(src)
                    res = {'name': unique(name or os.path.splitext(os.path.basename(src))[0]), 'loop': loop}
                    entry = {'name': res['name'], 'file': relfile(rel)}
                    if name:
                        entry['clip'] = name
                    M['entries'].append(entry)
                M['clips'][key] = res
                return res
            return None
        return resolve

    def model_dir(self, model_path):
        return os.path.join(self.out, 'models', safe(os.path.splitext(os.path.basename(model_path))[0]))

    def copy_model(self, src):
        d = self.model_dir(src)
        os.makedirs(d, exist_ok=True)
        dst = os.path.join(d, os.path.basename(src))
        if not os.path.exists(dst):
            shutil.copyfile(src, dst)
        info = ModelInfo(self.index, src)
        if info.splits and src not in self.models:
            self.write_sidecar(dst, info.sidecar_entries(), keep=False)
        return os.path.relpath(dst, self.out).replace(os.sep, '/')

    @staticmethod
    def write_sidecar(dst, entries, keep):
        with open(dst + '.clips.json', 'w') as f:
            json.dump({'format': 'aiverse-clips', 'version': 1, 'keepSource': keep, 'clips': entries}, f, indent=1)

    # ---------------------------------------------------------------- controllers
    def mask_loader(self):
        def load(ref):
            p = self.index.path(ref.get('guid'))
            return CT.load_mask(self.index.docs(p)) if p else (None, '')
        return load

    def convert_controller(self, cpath, model_path, label):
        docs = self.index.docs(cpath)
        guid = self.index.guid_of.get(os.path.abspath(cpath), '')
        overrides, chain, base_path = {}, [], cpath
        for _ in range(6):   # override controllers may stack
            if not any(d.class_id == CT.C_OVERRIDE for d in docs):
                break
            base_ref, mp = CT.override_map(docs, guid)
            for k, v in mp.items():
                overrides.setdefault(k, v)
            chain.append(self.index.rel(base_path))
            base_path = self.index.path(base_ref.get('guid'))
            if not base_path:
                raise ValueError('%s: base controller not found' % self.index.rel(cpath))
            docs = self.index.docs(base_path)
            guid = self.index.guid_of.get(os.path.abspath(base_path), '')
        flat = {}
        for k in overrides:   # an override of an override maps the deepest base clip transitively
            v, n = overrides[k], 0
            while v in overrides and overrides[v] != v and n < 6:
                v, n = overrides[v], n + 1
            flat[k] = v
        conv = CT.Converter(base_path, docs, guid, self.resolver(model_path), self.mask_loader(), flat)
        graph, types = conv.convert()
        if model_path:
            graph['model'] = os.path.relpath(os.path.join(self.model_dir(model_path), os.path.basename(model_path)), self.out).replace(os.sep, '/')
        name = safe(os.path.splitext(os.path.basename(cpath))[0]) + ('_' + label if label else '')
        rel = 'animations/%s.json' % name
        graph['unity'] = {'controller': self.index.rel(cpath), 'base': self.index.rel(base_path) if chain else None,
                          'parameterTypes': types, 'notes': conv.notes}
        dst = os.path.join(self.out, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, 'w') as f:
            json.dump(graph, f, indent=1)
        return rel, graph, conv

    # ---------------------------------------------------------------- run
    def run(self):
        os.makedirs(self.out, exist_ok=True)
        pairs, seen = [], {}
        for prefab, cguid, avatar, root in self.animators():
            cpath = self.index.path(cguid)
            if not cpath or not cpath.endswith(('.controller', '.overrideController')):
                continue
            mpath = self.index.path(avatar) if avatar else None
            if mpath and not mpath.lower().endswith(MODEL_EXT):
                mpath = None
            key = (cpath, mpath)
            if key not in seen:
                seen[key] = len(pairs)
                pairs.append((cpath, mpath, [self.index.rel(prefab)], root))
            else:
                pairs[seen[key]][2].append(self.index.rel(prefab))
        used = {p[0] for p in pairs}
        for dp, dns, fns in os.walk(self.source):
            dns.sort()
            for fn in sorted(fns):
                p = os.path.join(dp, fn)
                if fn.endswith(('.controller', '.overrideController')) and p not in used:
                    pairs.append((p, None, [], False))
        drivers = scan_drivers(self.scripts)
        per_ctrl = {}
        for cpath, mpath, prefabs, root in pairs:
            label = ''
            if mpath:
                n = per_ctrl.setdefault(cpath, [])
                n.append(mpath)
                label = safe(os.path.splitext(os.path.basename(mpath))[0]) if len(n) > 1 else ''
                self.model(mpath)
                self.copy_model(mpath)
            try:
                rel, graph, conv = self.convert_controller(cpath, mpath, label)
            except (ValueError, KeyError, TypeError, AttributeError) as e:
                self.report['skipped'].append({'controller': self.index.rel(cpath), 'reason': str(e)})
                continue
            params = graph['parameters']
            layers = graph['nodes'][0]['controller']['layers']
            entry = {'controller': self.index.rel(cpath), 'graph': rel, 'model': graph.get('model'), 'prefabs': prefabs,
                     'applyRootMotion': root, 'layers': [L['name'] for L in layers], 'states': sum(len(L['states']) for L in layers),
                     'parameters': {k: {'default': v, 'type': graph['unity']['parameterTypes'].get(k, 'layer weight'),
                                        'drivers': drivers.get(k[len('layer.'):] if k.startswith('layer.') else k, [])} for k, v in params.items()},
                     'notes': list(conv.notes), 'crowd': crowd_hint(graph)}
            if root:
                entry['notes'].append('Animator.applyRootMotion is on: the engine keeps clips in place (the simulation moves units)')
            if not mpath:
                entry['notes'].append('no Avatar/model found for this controller: graph not bound to a model')
            self.report['controllers'].append(entry)
        for mpath, M in self.models.items():
            info = M['info']
            dst = os.path.join(self.model_dir(mpath), os.path.basename(mpath))
            entries = info.sidecar_entries() + M['entries']
            if entries:
                self.write_sidecar(dst, entries, keep=not info.splits)
            self.report['models'].append({'source': self.index.rel(mpath), 'file': os.path.relpath(dst, self.out).replace(os.sep, '/'),
                                          'rig': info.rig, 'motionNode': info.motion_node, 'splits': len(info.splits),
                                          'externalClips': len(M['entries'])})
            if info.rig == 'humanoid':
                self.report['warnings'].append('%s: humanoid rig: clips bind by bone path only (no muscle-space retargeting); see docs/unity-animation.md' % self.index.rel(mpath))
        if self.clipinfo:
            self.verify()
        with open(os.path.join(self.out, 'unity_anim_report.json'), 'w') as f:
            json.dump(self.report, f, indent=1)
        with open(os.path.join(self.out, 'unity_anim_report.md'), 'w') as f:
            f.write(markdown(self.report))
        return self.report

    def verify(self):
        """Loads every model through the engine loader + sidecar and compiles every graph against it."""
        for m in self.report['models']:
            r = self._clipinfo([os.path.join(self.out, m['file'])])
            m['engine'] = {'ok': 'error' not in r, 'error': r.get('error'), 'clips': [c['name'] for c in r.get('clips', [])],
                           'warnings': [w for w in r.get('warnings', []) if 'clips.json' in w]}
        for c in self.report['controllers']:
            if not c.get('model'):
                continue
            r = self._clipinfo([os.path.join(self.out, c['model']), '--graph', os.path.join(self.out, c['graph']), '--run', '1'])
            g = r.get('graph') or {}
            c['engine'] = {'ok': bool(g.get('ok')), 'error': g.get('error') or r.get('error'), 'layers': g.get('layers', [])}

    def _clipinfo(self, args):
        try:
            p = subprocess.run([self.clipinfo] + args, capture_output=True, text=True, timeout=120)
            return json.loads(p.stdout or '{}')
        except (OSError, ValueError, subprocess.TimeoutExpired) as e:
            return {'error': str(e)}


def first_clip(m):
    """The first clip of a motion (a nested blend tree contributes its first child)."""
    while m and 'tree' in m:
        m = next((c.get('motion') for c in m['tree']['children'] if c.get('motion')), None)
    return m.get('clip') if m else None


def crowd_hint(graph):
    """A `view.animations` block for the baked (instanced GPU) path: idle / move blend / attack / spawn / death
    by state and clip names. Mobs in crowds can use it instead of the controller graph."""
    hint = {}
    for L in graph['nodes'][0]['controller']['layers'][:1]:
        for st in L['states']:
            n, m = st['name'].lower(), st.get('motion') or {}
            if 'tree' in m and m['tree']['type'] == '1d' and 'move' not in hint:
                kids = [(first_clip(c.get('motion')), c['threshold']) for c in m['tree']['children']]
                kids = [k for k in kids if k[0]]
                if len(kids) >= 2:
                    hint['idle'] = kids[0][0]
                    hint['move'] = {'blend': [{'clip': c, 'speed': t} for c, t in kids[1:]]}
            elif 'clip' in m:
                for key, words in (('attack', ('attack', 'bite', 'shoot')), ('spawn', ('spawn', 'get_out')), ('death', ('death', 'die')), ('hit', ('damage', 'hit'))):
                    if key not in hint and any(w in n for w in words):
                        hint[key] = {'clip': m['clip'], 'loop': False}
    return hint


def markdown(rep):
    out = ['# Unity animation import', '', 'Source: `%s`' % rep['source'], '']
    out += ['## Models', '', '| model | rig | root node | splits | external clips | engine |', '|---|---|---|---|---|---|']
    for m in rep['models']:
        e = m.get('engine')
        ok = '' if e is None else ('ok, %d clips' % len(e['clips']) if e['ok'] else 'FAIL: %s' % e['error'])
        if e and e['warnings']:
            ok += '; %d sidecar warnings' % len(e['warnings'])
        out.append('| %s | %s | %s | %d | %d | %s |' % (m['source'], m['rig'], m['motionNode'] or '-', m['splits'], m['externalClips'], ok))
    out += ['', '## Controllers', '']
    for c in rep['controllers']:
        e = c.get('engine')
        out.append('### %s' % c['controller'])
        out.append('')
        out.append('- graph `%s`, model `%s`, layers: %s, %d states' % (c['graph'], c['model'] or '-', ', '.join(c['layers']), c['states']))
        if e:
            out.append('- engine dry run: %s' % (('compiled; ' + ', '.join('%s=%s' % (l['layer'], l['state']) for l in e['layers'])) if e['ok'] else 'FAIL: %s' % e['error']))
        if c['prefabs']:
            out.append('- prefabs: %s' % ', '.join(c['prefabs'][:6]) + (' ...' if len(c['prefabs']) > 6 else ''))
        for k, v in c['parameters'].items():
            out.append('- parameter `%s` (%s, default %s): %s' % (k, v['type'], v['default'], ', '.join(v['drivers']) or 'no C# driver found'))
        for n in c['notes']:
            out.append('- note: %s' % n)
        if c['crowd']:
            out.append('- crowd `view.animations` hint: `%s`' % json.dumps(c['crowd']))
        out.append('')
    out += ['## Clips', '', '| clip | kind | loop | channels (cubic / resampled) | not converted |', '|---|---|---|---|---|']
    for c in rep['clips']:
        out.append('| %s | %s | %s | %s | %s |' % (c['source'], c['kind'], 'yes' if c['loop'] else 'no',
                                                 '%s (%s / %s)' % (c['channels'], c['cubic'], c['resampled']), '; '.join(c['unsupported'] + c['notes'])))
    if rep['skipped'] or rep['warnings']:
        out += ['', '## Warnings', '']
        out += ['- skipped %s: %s' % (s['controller'], s['reason']) for s in rep['skipped']]
        out += ['- %s' % w for w in rep['warnings']]
    return '\n'.join(out) + '\n'


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('source')
    ap.add_argument('--out', required=True)
    ap.add_argument('--assets')
    ap.add_argument('--clipinfo')
    ap.add_argument('--scripts', action='append')
    ap.add_argument('--json', action='store_true')
    a = ap.parse_args(argv)
    try:
        rep = Importer(a.source, a.out, a.assets, a.clipinfo, a.scripts).run()
    except ValueError as e:
        print(json.dumps({'error': str(e)}))
        return 2
    summary = {'out': os.path.abspath(a.out), 'models': len(rep['models']), 'controllers': len(rep['controllers']), 'clips': len(rep['clips']),
               'skipped': len(rep['skipped']), 'warnings': len(rep['warnings']),
               'engineFailures': [c['controller'] for c in rep['controllers'] if c.get('engine') and not c['engine']['ok']] +
                                 [m['source'] for m in rep['models'] if m.get('engine') and not m['engine']['ok']]}
    print(json.dumps(rep if a.json else summary, indent=1))
    return 0


if __name__ == '__main__':
    sys.exit(main())
