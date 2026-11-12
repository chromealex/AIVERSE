"""Unity VFX Graph (.vfx, text serialized) -> a small node model with value evaluation.

A .vfx file is one MonoBehaviour per node (graph, contexts, blocks, operators, parameters, slots, data). The node
classes are only known by their script GUID (package scripts, not in the project), so `CLASSES` names the GUIDs of
com.unity.visualeffectgraph 17.x; nodes with an unknown GUID are classified by their serialized fields (`classify`)
and reported. Slot values are JSON strings (`m_SerializableObject`); links go from an input slot (or one of its
sub-slots: `x`, `r`, `radius`, ...) to an operator / parameter output slot (or one of its sub-slots).

Values are evaluated symbolically as far as the engine can use them: constants, per-particle random ranges (`Rand`),
curves / gradients over the particle life (`Life`), "per second" factors (`DeltaTime`), and `Unknown` for anything
else (time-varying expressions, attributes the engine has no equivalent for), which the mapper reports.
"""
import json
import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from lib.unity import unity_yaml as uy  # noqa: E402
from lib.unity.unity_yaml import get, num  # noqa: E402

# Script GUID (the first 8 hex digits are unique in the package) -> class. Contexts and blocks are also recognisable
# by their fields (classify); the binary operators share their fields, their names come from how graphs use them
# (index / 255 -> progress = Divide, position + offset = Add, speed * deltaTime = Multiply).
CLASSES = {
    '7d4c867f': 'VFXGraph', 'd01270ef': 'VFXUI',
    # contexts
    '73a13919': 'VFXBasicSpawner', '9dfea488': 'VFXBasicInitialize', '2dc09576': 'VFXBasicUpdate',
    'f42a6449': 'VFXBasicGPUEvent', 'a0b9e6b9': 'VFXPlanarPrimitiveOutput', 'e837ba02': 'VFXMeshOutput',
    '9956086b': 'VFXMeshOutput', '756b4278': 'VFXQuadStripOutput', '78dcade7': 'VFXQuadOutput',
    # data
    'd78581a9': 'VFXDataParticle', 'f6875907': 'VFXDataSpawner', '081ffb00': 'VFXDataEvent',
    # spawner blocks
    'f05c6884': 'VFXSpawnerConstantRate', '5e382412': 'VFXSpawnerBurst', '162e4a5d': 'VFXSpawnerVariableRate',
    '4bfc68be': 'VFXSpawnerCustomWrapper',
    # particle blocks
    'a971fa2e': 'SetAttribute', '01ec2c19': 'AttributeFromCurve', 'fb1f6794': 'PositionShape',
    'ee5e80e5': 'PositionMesh', '92b80e8c': 'VelocityDirection', '26096dfa': 'VelocityDirection',
    'b3c78ba9': 'VelocityDirection', '63716c0d': 'Turbulence', 'e5dce54a': 'Gravity', '3a58e066': 'ConformToSphere',
    '3f661efa': 'Collision', '9a7ad767': 'TriggerEvent', 'd16c6aea': 'Orient', '956b6887': 'ColorOverLife',
    'd03231f3': 'SubpixelAA',
    # operators
    '330e0fca': 'VFXParameter', 'ed61682d': 'Inline', '955b0c17': 'Inline', 'b8ee8a75': 'Multiply',
    'c7acf542': 'Add', '39201e37': 'Divide', '0155ae97': 'Subtract', 'c42128e1': 'Random', 'f8bcc906': 'SampleCurve',
    'a07d13b9': 'SampleGradient', 'ba941214': 'AgeOverLifetime', '486e063e': 'GetAttribute', 'a72fbb93': 'BuiltIn',
    '9717a5f0': 'Branch', 'e25f6e0f': 'Compare', '0a02ebe9': 'Remap', 'c6c2320b': 'ChangeSpace', '1fb2f8fd': 'Noise',
    'a30aeb73': 'Noise', 'efe55077': 'Rotate3D', 'f0fd9aaa': 'TransformPosition', '271b8023': 'PeriodicTotalTime',
    '73232fc6': 'MainCamera', '71f05d1d': 'SampleBezier', '299549bf': 'RandomSelectorWeighted', '3022e18c': 'Normalize',
    'c7b83980': 'SquareWave', '1e3ae21a': 'BinaryOperator', '1f6acec4': 'LogicalOperator', 'e1fe1c3a': 'CustomAttribute',
}
SLOT_GUIDS = {'f780aa28', 'b4c11ff2', 'ac39bd03', 'c117b74c', '1b605c02', '70a331b1', '388ad3b1', 'c82227d5', '76f778ff',
              'c52d920e', '1b2b7510', 'c499060c', '4d246e35', 'a9f9544b', '52656571', 'b47b8679', '3e3f628d', 'e8f2b4a8',
              'a47ff188', 'a4dabe49', 'd66abc5b', '99ac4f6a'}
CONTEXTS = {'VFXBasicSpawner', 'VFXBasicInitialize', 'VFXBasicUpdate', 'VFXBasicGPUEvent', 'VFXPlanarPrimitiveOutput',
            'VFXMeshOutput', 'VFXQuadStripOutput', 'VFXQuadOutput'}
OUTPUTS = {'VFXPlanarPrimitiveOutput', 'VFXMeshOutput', 'VFXQuadStripOutput', 'VFXQuadOutput'}
COMPONENT = {'x': 0, 'y': 1, 'z': 2, 'w': 3, 'r': 0, 'g': 1, 'b': 2, 'a': 3}


def classify(d):
    """Class of a node from its serialized fields, for GUIDs not in CLASSES (other package versions)."""
    k = d.data
    if 'm_MasterSlot' in k:
        return 'Slot'
    if 'm_ExposedName' in k:
        return 'VFXParameter'
    if 'loopDuration' in k:
        return 'VFXBasicSpawner'
    if 'integration' in k and 'reapParticles' in k:
        return 'VFXBasicUpdate'
    if 'blendMode' in k and 'm_InputFlowSlot' in k:
        return 'VFXMeshOutput' if 'MeshCount' in k else 'VFXQuadStripOutput' if 'tilingMode' in k else 'VFXPlanarPrimitiveOutput'
    if 'dataType' in k and 'capacity' in k:
        return 'VFXDataParticle'
    if 'attribute' in k and 'Source' in k:
        return 'SetAttribute'
    if 'attribute' in k and 'SampleMode' in k:
        return 'AttributeFromCurve'
    if 'attribute' in k and 'location' in k:
        return 'GetAttribute'
    if 'positionMode' in k and 'shape' in k:
        return 'PositionShape'
    if 'NoiseType' in k:
        return 'Turbulence'
    if 'repeat' in k and 'spawnMode' in k:
        return 'VFXSpawnerBurst'
    if 'speedMode' in k:
        return 'VelocityDirection'
    if 'faceRay' in k:
        return 'Orient'
    if 'm_BuiltInParameters' in k:
        return 'BuiltIn'
    return 'Unknown'


# ------------------------------------------------------------------------------------------ symbolic values

class Rand:
    """Uniform random per particle between lo and hi (numbers or equal-length tuples)."""

    def __init__(self, lo, hi):
        self.lo, self.hi = lo, hi

    def __repr__(self):
        return 'Rand(%r, %r)' % (self.lo, self.hi)


class Life:
    """A curve [(t, v)] (or a gradient [(t, (r, g, b, a))]) over the particle's age / lifetime, times k.
    keys == 'age' is the age / lifetime ratio itself."""

    def __init__(self, keys, k=1.0, gradient=False):
        self.keys, self.k, self.gradient = keys, k, gradient


class DeltaTime:
    """deltaTime * k: a value added every update is k per second."""

    def __init__(self, k=1.0):
        self.k = k


class AttrRef:
    """The particle's current value of an attribute (Get Attribute: position, ...)."""

    def __init__(self, name):
        self.name = name


class AttrXform:
    """attribute rotated about `center` by matrix M, then moved by `add` (Unity space): Rotate 3D / Add on the
    position the spawn shape produced."""

    def __init__(self, name, M, center, add=(0.0, 0.0, 0.0)):
        self.name, self.M, self.center, self.add = name, M, center, add


WRAPPERS = ('vector', 'direction', 'position')


def unwrap(v):
    """{'vector': (x, y, z)} / {'direction': ..} / {'position': ..} (VFX spaceable types) -> the inner value."""
    if isinstance(v, dict) and len(v) == 1 and next(iter(v)) in WRAPPERS:
        return unwrap(next(iter(v.values())))
    if isinstance(v, Rand):
        return Rand(unwrap(v.lo), unwrap(v.hi))
    return v


class Unknown:
    def __init__(self, why):
        self.why = why

    def __repr__(self):
        return 'Unknown(%s)' % self.why


def is_num(v):
    return isinstance(v, (int, float)) and not isinstance(v, bool)


def _vecop(a, b, f):
    if is_num(a) and is_num(b):
        return f(a, b)
    if isinstance(a, tuple) and is_num(b):
        return tuple(f(x, b) for x in a)
    if is_num(a) and isinstance(b, tuple):
        return tuple(f(a, y) for y in b)
    if isinstance(a, tuple) and isinstance(b, tuple):
        return tuple(f(a[i], b[i]) for i in range(min(len(a), len(b))))
    return None


OPS = {'Multiply': lambda x, y: x * y, 'Add': lambda x, y: x + y, 'Subtract': lambda x, y: x - y,
       'Divide': lambda x, y: x / y if y else 0.0}


def arith(op, a, b):
    """a op b over constants, Rand (bounds), Life (scaled by a constant) and DeltaTime."""
    f = OPS[op]
    a, b = unwrap(a), unwrap(b)
    if isinstance(a, Unknown):
        return a
    if isinstance(b, Unknown):
        return b
    if op in ('Add', 'Subtract') and isinstance(a, (AttrRef, AttrXform)) and isinstance(b, tuple):
        base = a if isinstance(a, AttrXform) else AttrXform(a.name, ((1, 0, 0), (0, 1, 0), (0, 0, 1)), (0.0, 0.0, 0.0))
        d = b if op == 'Add' else tuple(-x for x in b)
        return AttrXform(base.name, base.M, base.center, tuple(x + y for x, y in zip(base.add, d)))
    if op == 'Add' and isinstance(b, (AttrRef, AttrXform)) and isinstance(a, tuple):
        return arith(op, b, a)
    if op == 'Multiply' and isinstance(a, tuple) and isinstance(b, Life) and not b.gradient and b.keys != 'age':
        a, b = b, a
    if op == 'Multiply' and isinstance(a, Life) and not a.gradient and a.keys != 'age' and isinstance(b, tuple) and len(b) >= 3:
        c = tuple(b) + (1.0,) * (4 - len(b))
        return Life([(t, tuple(x * v * a.k for x in c[:3]) + (c[3],)) for t, v in a.keys], 1.0, True)

    def const(v):
        return is_num(v) or isinstance(v, tuple)

    if const(a) and const(b):
        r = _vecop(a, b, f)
        return r if r is not None else Unknown('%s of mismatched values' % op)
    if op in ('Multiply', 'Divide'):
        if isinstance(a, DeltaTime) and is_num(b):
            return DeltaTime(f(a.k, b))
        if isinstance(b, DeltaTime) and is_num(a) and op == 'Multiply':
            return DeltaTime(a * b.k)
        if isinstance(a, Life) and not a.gradient and a.keys != 'age' and is_num(b):
            return Life(a.keys, f(a.k, b))
        if isinstance(b, Life) and not b.gradient and b.keys != 'age' and is_num(a) and op == 'Multiply':
            return Life(b.keys, a * b.k)
        if isinstance(a, Life) and a.gradient and isinstance(b, tuple) and op == 'Multiply':
            return Life([(t, tuple(c[i] * (b[i] if i < len(b) else 1) for i in range(4))) for t, c in a.keys], 1.0, True)
        if isinstance(b, Life) and b.gradient and isinstance(a, tuple) and op == 'Multiply':
            return arith(op, b, a)
    if isinstance(a, Rand) and const(b):
        return Rand(_vecop(a.lo, b, f), _vecop(a.hi, b, f))
    if isinstance(b, Rand) and const(a):
        return Rand(_vecop(a, b.lo, f), _vecop(a, b.hi, f))
    return Unknown('%s of %s and %s' % (op, type(a).__name__, type(b).__name__))


class Composite:
    """A vector whose components are symbolic (one component linked to a random operator, ...)."""

    def __init__(self, parts):
        self.parts = parts

    def resolve(self):
        """-> tuple, Rand of tuples, or Unknown."""
        bad = next((p for p in self.parts if isinstance(p, Unknown)), None)
        if bad is not None:
            return bad
        if all(is_num(p) for p in self.parts):
            return tuple(self.parts)
        if all(is_num(p) or isinstance(p, Rand) for p in self.parts):
            return Rand(tuple(p.lo if isinstance(p, Rand) else p for p in self.parts),
                        tuple(p.hi if isinstance(p, Rand) else p for p in self.parts))
        if all(is_num(p) or isinstance(p, DeltaTime) for p in self.parts):
            return DeltaTime(tuple(p.k if isinstance(p, DeltaTime) else p for p in self.parts))
        if all(is_num(p) or (isinstance(p, Life) and not p.gradient and p.keys != 'age') for p in self.parts):
            return self   # components over the particle life (spin curves, ...): read per component
        return Unknown('vector with time-varying components')


def resolve(v):
    return v.resolve() if isinstance(v, Composite) else v


# ------------------------------------------------------------------------------------------ slot values

def _json(s):
    if s is None:
        return None
    if isinstance(s, (int, float)):
        return s
    try:
        return json.loads(s)
    except (ValueError, TypeError):
        return uy.scalar(str(s))


def plain(v):
    """Slot JSON -> python value: numbers, tuples for vectors / colours, dicts for structures."""
    if isinstance(v, bool):
        return 1.0 if v else 0.0
    if is_num(v):
        return float(v)
    if isinstance(v, dict):
        for keys in (('x', 'y', 'z', 'w'), ('x', 'y', 'z'), ('x', 'y'), ('r', 'g', 'b', 'a')):
            if set(v) == set(keys):
                return tuple(float(num(v[k])) for k in keys)
        return {k: plain(x) for k, x in v.items()}
    if isinstance(v, list):
        return [plain(x) for x in v]
    return v


def _set_path(v, path, x):
    """Replaces the component at `path` (sub-slot names) of a plain value."""
    if not path:
        return x
    head, rest = path[0], path[1:]
    if isinstance(v, (tuple, Composite)):
        parts = list(v.parts if isinstance(v, Composite) else v)
        idx = COMPONENT.get(head)
        if idx is None or idx >= len(parts):
            return v
        parts[idx] = _set_path(parts[idx], rest, x)
        return tuple(parts) if all(is_num(p) for p in parts) else Composite(parts)
    if isinstance(v, dict):
        v = dict(v)
        v[head] = _set_path(v.get(head), rest, x)
        return v
    return x


def _get_path(v, path):
    for p in path:
        if isinstance(v, (tuple, Composite)):
            parts = v.parts if isinstance(v, Composite) else v
            idx = COMPONENT.get(p)
            if idx is None or idx >= len(parts):
                return Unknown('component %s' % p)
            v = parts[idx]
        elif isinstance(v, Rand):
            v = Rand(_get_path(v.lo, [p]), _get_path(v.hi, [p]))
        elif isinstance(v, dict):
            v = v.get(p)
        else:
            return v
    return v


class Graph:
    """Nodes of one .vfx file + exposed-property overrides of the VisualEffect component that plays it."""

    def __init__(self, path, overrides=None):
        with open(path, 'r', encoding='utf-8', errors='replace') as f:
            text = f.read()
        if not text.startswith('%YAML'):
            raise ValueError('%s is not a text-serialized asset (Force Text serialization is needed)' % os.path.basename(path))
        self.path = path
        self.docs = uy.parse_documents(text)
        self.by = {d.file_id: d for d in self.docs}
        self.overrides = overrides or {}
        self.unknown = {}
        self.cls = {}
        for d in self.docs:
            if d.class_id != 114:
                continue
            g = str(get(d.data, 'm_Script', 'guid', default=''))[:8]
            c = CLASSES.get(g) or ('Slot' if g in SLOT_GUIDS or 'm_MasterSlot' in d.data else None)
            if c is None:
                c = classify(d)
                self.unknown[g] = c
            self.cls[d.file_id] = c
        self.graph = next((d for d in self.docs if self.cls.get(d.file_id) == 'VFXGraph'), None)
        self.resource = next((d for d in self.docs if d.class_id == 2058629511), None)
        self._cache = {}

    # nodes
    def node(self, ref):
        return self.by.get(get(ref, 'fileID') if isinstance(ref, dict) else ref)

    def kind(self, d):
        return self.cls.get(d.file_id, 'Unknown') if d is not None else None

    def children(self, d):
        return [c for c in (self.node(r) for r in d.data.get('m_Children') or []) if c is not None]

    def top(self):
        return self.children(self.graph) if self.graph is not None else []

    def contexts(self):
        return [d for d in self.top() if self.kind(d) in CONTEXTS]

    def _flow(self, ctx, key):
        out = []
        for fs in ctx.data.get(key) or []:
            for l in (fs or {}).get('link') or []:
                c = self.node(get(l, 'context'))
                if c is not None:
                    out.append(c)
        return out

    def flow_out(self, ctx):
        return self._flow(ctx, 'm_OutputFlowSlot')

    def flow_in(self, ctx):
        return self._flow(ctx, 'm_InputFlowSlot')

    def enabled(self, d):
        return not int(num(d.data.get('m_Disabled'), 0))

    def data(self, ctx):
        return self.node(get(ctx.data, 'm_Data'))

    # slots
    def slots(self, d, which='m_InputSlots'):
        return [s for s in (self.node(r) for r in d.data.get(which) or []) if s is not None]

    def slot_name(self, s):
        return str(get(s.data, 'm_Property', 'name', default=''))

    def slot(self, d, name):
        return next((s for s in self.slots(d) if self.slot_name(s) == name), None)

    def slot_values(self, d):
        return {self.slot_name(s): self.value(s) for s in self.slots(d)}

    def owner(self, s):
        master = self.node(get(s.data, 'm_MasterSlot')) or s
        return self.node(get(master.data, 'm_MasterData', 'm_Owner')), master

    def path_in_master(self, s):
        path, cur = [], s
        while True:
            parent = self.node(get(cur.data, 'm_Parent'))
            if parent is None or self.kind(parent) != 'Slot':
                break
            path.append(self.slot_name(cur))
            cur = parent
        return list(reversed(path))

    def linked(self, s):
        """True when the slot or one of its sub-slots is fed by a link."""
        if s.data.get('m_LinkedSlots'):
            return True
        return any(self.linked(c) for c in self.children(s) if self.kind(c) == 'Slot')

    def value(self, s):
        """Value of an input slot: its own (master) value with every linked sub-slot replaced by the link."""
        if s is None:
            return None
        key = ('in', s.file_id)
        if key in self._cache:
            return self._cache[key]
        self._cache[key] = Unknown('cycle')
        v = plain(_json(get(s.data, 'm_MasterData', 'm_Value', 'm_SerializableObject')))
        v = unwrap(resolve(self._links(s, v, [])))
        self._cache[key] = v
        return v

    def _links(self, s, v, path):
        links = s.data.get('m_LinkedSlots') or []
        if links:
            src = self.output(self.node(links[0]))
            return _set_path(v, path, src) if path else src
        for c in self.children(s):
            if self.kind(c) == 'Slot':
                v = self._links(c, v, path + [self.slot_name(c)])
        return v

    def output(self, s):
        """Value of an operator / parameter output slot (or of one of its sub-slots)."""
        if s is None:
            return Unknown('dangling link')
        op, master = self.owner(s)
        if op is None:
            return Unknown('output without owner')
        sub = self.path_in_master(s)
        idx = next((i for i, o in enumerate(self.slots(op, 'm_OutputSlots')) if o.file_id == master.file_id), 0)
        key = ('out', op.file_id, idx)
        if key not in self._cache:
            self._cache[key] = Unknown('cycle')
            self._cache[key] = resolve(self.evaluate(op, idx, master))
        return unwrap(resolve(_get_path(self._cache[key], sub)))

    def evaluate(self, op, idx, out_slot):
        k = self.kind(op)
        if k == 'VFXParameter':
            name = str(op.data.get('m_ExposedName') or '')
            if name in self.overrides:
                return self.overrides[name]
            return plain(_json(get(out_slot.data, 'm_MasterData', 'm_Value', 'm_SerializableObject')))
        if k == 'Inline':
            v = [self.value(s) for s in self.slots(op)]
            return v[0] if v else Unknown('empty inline operator')
        if k in OPS:
            v = [self.value(s) for s in self.slots(op)]
            if not v:
                return Unknown(k + ' without operands')
            acc = v[0]
            for x in v[1:]:
                acc = arith(k, acc, x)
            return acc
        if k == 'Random':
            v = self.slot_values(op)
            lo, hi = v.get('Min', 0.0), v.get('Max', 1.0)
            lo, hi = unwrap(lo), unwrap(hi)
            if not all(is_num(x) or isinstance(x, tuple) for x in (lo, hi)):
                return Unknown('random with non-constant bounds')
            return Rand(lo, hi)   # per evaluation: once per particle in Initialize, per frame in Update (approximated)
        if k in ('SampleCurve', 'SampleGradient'):
            v = self.slot_values(op)
            t = v.get('time')
            src = v.get('curve' if k == 'SampleCurve' else 'gradient')
            if not isinstance(t, Life) or t.keys != 'age':
                return Unknown('%s by something else than the particle age' % k)
            return Life(curve_points(src)) if k == 'SampleCurve' else Life(gradient_points(src), 1.0, True)
        if k == 'AgeOverLifetime':
            return Life('age')
        if k == 'GetAttribute':
            a = str(op.data.get('attribute') or '')
            if a == 'ageOverLifetime':
                return Life('age')
            if a == 'position' and int(num(op.data.get('location'), 0)) == 0:
                return AttrRef('position')
            return Unknown('attribute %s' % a)
        if k == 'Rotate3D':
            v = self.slot_values(op)
            pos, c, ax, ang = v.get('Position'), v.get('RotationCenter'), v.get('RotationAxis'), v.get('Angle')
            c = c.get('position') if isinstance(c, dict) else c
            ax = ax.get('direction') if isinstance(ax, dict) else ax
            if isinstance(pos, dict):
                pos = pos.get('position')
            if isinstance(pos, AttrRef) and isinstance(c, tuple) and isinstance(ax, tuple) and is_num(ang):
                return {'position': AttrXform(pos.name, axis_angle(ax, ang), c)}
            if isinstance(pos, tuple) and isinstance(c, tuple) and isinstance(ax, tuple) and is_num(ang):
                M = axis_angle(ax, ang)
                d = tuple(x - y for x, y in zip(pos, c))
                return {'position': tuple(sum(M[i][j] * d[j] for j in range(3)) + c[i] for i in range(3))}
            return Unknown('rotate 3D of a non-constant value')
        if k == 'Branch':
            v = self.slot_values(op)
            p = v.get('predicate')
            if is_num(p):
                return v.get('True') if p else v.get('False')
            return Unknown('branch on a non-constant condition')
        if k == 'Compare':
            v = self.slot_values(op)
            l, r = v.get('left'), v.get('right')
            if is_num(l) and is_num(r):
                c = int(num(op.data.get('condition'), 0))
                res = {0: l == r, 1: l != r, 2: l < r, 3: l <= r, 4: l > r, 5: l >= r}.get(c, False)
                return 1.0 if res else 0.0
            return Unknown('compare of non-constant values')
        if k == 'BuiltIn':
            p = int(num(op.data.get('m_BuiltInParameters'), 0))
            return DeltaTime(1.0) if p == 2 else Unknown('built-in time parameter %d' % p)
        return Unknown('operator %s' % k)


# ------------------------------------------------------------------------------------------ curves / gradients

def curve_points(c, samples=33):
    """VFX AnimationCurve JSON ({"frames": [...]}) -> [(t, v)] sampled over 0..1 and simplified."""
    if not isinstance(c, dict):
        v = float(c) if is_num(c) else 1.0
        return [(0.0, v), (1.0, v)]
    keys = []
    for fr in c.get('frames') or []:
        if isinstance(fr, dict):
            keys.append((num(fr.get('time')), num(fr.get('value')), num(fr.get('inTangent')), num(fr.get('outTangent')),
                         int(num(fr.get('weightedMode'), 0)), num(fr.get('inWeight'), 1 / 3.0), num(fr.get('outWeight'), 1 / 3.0)))
    keys.sort(key=lambda k: k[0])
    if not keys:
        return [(0.0, 1.0), (1.0, 1.0)]
    pts = [(i / (samples - 1), uy.curve_eval(keys, i / (samples - 1))) for i in range(samples)]
    lo, hi = min(p[1] for p in pts), max(p[1] for p in pts)
    return uy.simplify(pts, max(1e-4, (hi - lo) * 0.01))


def gradient_points(g):
    """VFX Gradient JSON -> [(t, (r, g, b, a))] at the union of colour and alpha key times."""
    if not isinstance(g, dict):
        return [(0.0, (1.0, 1.0, 1.0, 1.0)), (1.0, (1.0, 1.0, 1.0, 1.0))]
    def rgb(c):
        if isinstance(c, tuple):
            return tuple(c[:3]) + (1.0,) * (3 - len(c[:3]))
        return (num(get(c, 'r'), 1), num(get(c, 'g'), 1), num(get(c, 'b'), 1))

    ck = sorted(((num(k.get('time')), rgb(k.get('color'))) for k in g.get('colorKeys') or [] if isinstance(k, dict)),
                key=lambda x: x[0]) or [(0.0, (1.0, 1.0, 1.0))]
    ak = sorted(((num(k.get('time')), num(k.get('alpha'), 1)) for k in g.get('alphaKeys') or [] if isinstance(k, dict)),
                key=lambda x: x[0]) or [(0.0, 1.0)]
    fixed = int(num(g.get('gradientMode'), 0)) == 1

    def at(keys, t):
        if t <= keys[0][0]:
            return keys[0][1]
        for a, b in zip(keys, keys[1:]):
            if t <= b[0]:
                if fixed:
                    return b[1]
                f = (t - a[0]) / (b[0] - a[0]) if b[0] > a[0] else 1
                if isinstance(a[1], tuple):
                    return tuple(x + (y - x) * f for x, y in zip(a[1], b[1]))
                return a[1] + (b[1] - a[1]) * f
        return keys[-1][1]

    ts = sorted(set([0.0, 1.0] + [t for t, _ in ck] + [t for t, _ in ak]))
    return [(t, at(ck, t) + (at(ak, t),)) for t in ts]


def unity_gradient_json(g):
    """Unity YAML Gradient (key0..7, ctime0.., atime0.., m_NumColorKeys) -> the VFX JSON form."""
    if not isinstance(g, dict):
        return {}
    nc, na = int(num(g.get('m_NumColorKeys'), 2)), int(num(g.get('m_NumAlphaKeys'), 2))
    ck, ak = [], []
    for i in range(8):
        c = g.get('key%d' % i)
        if not isinstance(c, dict):
            continue
        if i < nc:
            ck.append({'color': {'r': num(c.get('r')), 'g': num(c.get('g')), 'b': num(c.get('b'))},
                       'time': num(g.get('ctime%d' % i)) / 65535.0})
        if i < na:
            ak.append({'alpha': num(c.get('a'), 1), 'time': num(g.get('atime%d' % i)) / 65535.0})
    return {'colorKeys': ck, 'alphaKeys': ak, 'gradientMode': int(num(g.get('m_Mode'), 0))}


def property_overrides(sheet):
    """VisualEffect m_PropertySheet -> {exposed name: plain value} (only overridden entries)."""
    out = {}
    if not isinstance(sheet, dict):
        return out
    for kind, arr in sheet.items():
        for e in get(arr, 'm_Array', default=[]) or []:
            if not isinstance(e, dict) or not int(num(e.get('m_Overridden'), 0)):
                continue
            v = e.get('m_Value')
            if kind == 'm_AnimationCurve':
                v = {'frames': [{'time': num(k.get('time')), 'value': num(k.get('value')), 'inTangent': num(k.get('inSlope')),
                                 'outTangent': num(k.get('outSlope'))} for k in get(v, 'm_Curve', default=[]) or []]}
            elif kind == 'm_Gradient':
                v = unity_gradient_json(v)
            elif kind == 'm_NamedObject':
                v = {'obj': v}
            elif kind == 'm_Bool':
                v = 1.0 if int(num(v, 0)) else 0.0
            else:
                v = plain(v)
            out[str(e.get('m_Name'))] = v
    return out


def axis_angle(axis, ang):
    """Rotation matrix about a (normalized) axis by ang radians (Rodrigues)."""
    n = v_len(axis) or 1.0
    x, y, z = (a / n for a in axis[:3])
    c, s, t = math.cos(ang), math.sin(ang), 1 - math.cos(ang)
    return ((t * x * x + c, t * x * y - s * z, t * x * z + s * y),
            (t * x * y + s * z, t * y * y + c, t * y * z - s * x),
            (t * x * z - s * y, t * y * z + s * x, t * z * z + c))


def v_len(v):
    return math.sqrt(sum(x * x for x in v))
