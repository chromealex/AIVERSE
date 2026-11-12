"""Unity AnimationClip (.anim, Force Text YAML) -> glTF animation (glb.py) + clip settings.

Transform curves (m_RotationCurves / m_PositionCurves / m_ScaleCurves / m_EulerCurves) become TRS
channels of the node at the curve's path. Unity is left-handed: an FBX imported by Unity has X mirrored
relative to the same FBX loaded by the engine (verified on Drone Fall rigs: positions (-x, y, z),
rotations (x, -y, -z, w)), so values and tangents are mirrored back. Hermite keys with unweighted,
finite tangents map 1:1 onto glTF CUBICSPLINE (both store slopes per second); weighted / stepped
curves and Euler curves are resampled (30 fps + the original key times) to LINEAR / STEP.
Non-transform curves (blend shapes, material, IsActive, UI) are reported, not converted."""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
from lib.unity import unity_yaml as Y  # noqa: E402
import glb as G  # noqa: E402

FPS = 30.0
MIRROR_POS = (-1.0, 1.0, 1.0)
MIRROR_ROT = (1.0, -1.0, -1.0, 1.0)
CLASS_NAMES = {1: 'GameObject', 4: 'Transform', 23: 'MeshRenderer', 25: 'Renderer', 95: 'Animator', 114: 'MonoBehaviour',
               137: 'SkinnedMeshRenderer', 212: 'SpriteRenderer', 224: 'RectTransform', 225: 'CanvasGroup'}
UI_CLASSES = {224, 225, 222, 223}


def _strip_editor(text):
    """Editor-only curve copies are most of a .anim: drop them before parsing."""
    for key in ('\n  m_EditorCurves:', '\n  m_EulerEditorCurves:'):
        i = text.find(key)
        if i < 0:
            continue
        j = text.find('\n  m_', i + len(key))
        while j >= 0 and text.startswith('\n  m_EulerEditorCurves:', j):
            j = text.find('\n  m_', j + 5)
        text = text[:i] + (text[j:] if j >= 0 else '\n')
    return text


def load(path):
    with open(path, 'r', encoding='utf-8', errors='replace') as f:
        text = _strip_editor(f.read())
    docs = [d for d in Y.parse_documents(text) if d.class_id == 74]
    if not docs:
        raise ValueError('%s: no AnimationClip' % path)
    d = docs[0].data
    s = d.get('m_AnimationClipSettings') or {}
    clip = {
        'name': d.get('m_Name') or os.path.splitext(os.path.basename(path))[0],
        'legacy': bool(d.get('m_Legacy')), 'compressed': bool(d.get('m_Compressed')),
        'sampleRate': Y.num(d.get('m_SampleRate'), 30),
        'start': Y.num(s.get('m_StartTime'), 0), 'stop': Y.num(s.get('m_StopTime'), 0),
        'loop': bool(s.get('m_LoopTime')), 'loopBlend': bool(s.get('m_LoopBlend')),
        'cycleOffset': Y.num(s.get('m_CycleOffset'), 0), 'mirror': bool(s.get('m_Mirror')),
        'bakeRotation': bool(s.get('m_LoopBlendOrientation')), 'bakeY': bool(s.get('m_LoopBlendPositionY')),
        'bakeXZ': bool(s.get('m_LoopBlendPositionXZ')),
        'additiveReference': bool(s.get('m_HasAdditiveReferencePose')),
        'genericRoot': bool(d.get('m_HasGenericRootTransform')),
        'events': len(d.get('m_Events') or []),
        'tracks': {}, 'floats': [], 'pptr': len(d.get('m_PPtrCurves') or []),
    }
    for section, kind in (('m_RotationCurves', 'r'), ('m_PositionCurves', 't'), ('m_ScaleCurves', 's'), ('m_EulerCurves', 'e')):
        for c in d.get(section) or []:
            p = c.get('path') or ''
            curve = c.get('curve') or {}
            clip['tracks'].setdefault(p, {})[kind] = {'keys': curve.get('m_Curve') or [], 'order': int(Y.num(curve.get('m_RotationOrder'), 4))}
    for c in d.get('m_FloatCurves') or []:
        clip['floats'].append({'path': c.get('path') or '', 'class': int(Y.num(c.get('classID'), 0)), 'attribute': c.get('attribute') or '',
                               'keys': len((c.get('curve') or {}).get('m_Curve') or [])})
    if clip['compressed']:
        clip['tracks'] = {}
    return clip


def kind_of(clip):
    """'transform' (bones / objects), 'ui', 'property' (only non-transform curves) or 'empty'."""
    if any(f['class'] in UI_CLASSES for f in clip['floats']):
        return 'ui'
    if any(p for p in clip['tracks']):
        return 'transform'
    real = [f for f in clip['floats'] if f['attribute'] not in ('MotionT.x', 'MotionT.y', 'MotionT.z', 'MotionQ.x', 'MotionQ.y', 'MotionQ.z', 'MotionQ.w')]
    return 'property' if real else 'empty'


def _vec(v, n):
    if isinstance(v, dict):
        return [Y.num(v.get(k), 0) for k in 'xyzw'[:n]]
    x = Y.num(v, 0)
    return [x] * n


def _component_keys(keys, n, i):
    out = []
    for k in keys:
        out.append((Y.num(k.get('time')), _vec(k.get('value'), n)[i], _vec(k.get('inSlope'), n)[i], _vec(k.get('outSlope'), n)[i],
                    int(Y.num(k.get('weightedMode'), 0)), _vec(k.get('inWeight'), n)[i] if k.get('inWeight') is not None else 1 / 3.0,
                    _vec(k.get('outWeight'), n)[i] if k.get('outWeight') is not None else 1 / 3.0))
    out.sort(key=lambda k: k[0])
    return out


def _hermite_ok(keys, n):
    for k in keys:
        if int(Y.num(k.get('weightedMode'), 0)) != 0:
            return False
        for s in _vec(k.get('inSlope'), n) + _vec(k.get('outSlope'), n):
            if not math.isfinite(s):
                return False
    return True


def _sample_times(keys, start, stop):
    times = {round(start, 6), round(stop, 6)}
    n = max(1, int(math.ceil((stop - start) * FPS)))
    for i in range(n + 1):
        times.add(round(start + (stop - start) * i / n, 6))
    for k in keys:
        t = Y.num(k.get('time'))
        if start <= t <= stop:
            times.add(round(t, 6))
    return sorted(times)


def _stepped(comp_keys):
    return any(not math.isfinite(k[3]) or not math.isfinite(k[2]) for k in comp_keys)


def _euler_quat(e, order):
    """Unity Euler (degrees) with RotationOrder (0 XYZ .. 5 ZYX; 4 = ZXY default) -> quaternion xyzw."""
    def axis(i, deg):
        h = math.radians(deg) * 0.5
        q = [0.0, 0.0, 0.0, math.cos(h)]
        q[i] = math.sin(h)
        return q

    def mul(a, b):
        return [a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1], a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
                a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3], a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]]
    seq = {0: 'xyz', 1: 'xzy', 2: 'yzx', 3: 'yxz', 4: 'zxy', 5: 'zyx'}.get(order, 'zxy')  # first applied first
    q = [0.0, 0.0, 0.0, 1.0]
    for c in seq:
        i = 'xyz'.index(c)
        q = mul(axis(i, e[i]), q)
    return q


def channels(clip, glb, report):
    """Adds the clip's transform tracks to `glb` and returns its channel list and duration."""
    start = clip['start']
    stop = clip['stop']
    if stop <= start:   # no clip range stored: the keys define it
        for tr in clip['tracks'].values():
            for c in tr.values():
                for k in c['keys']:
                    stop = max(stop, Y.num(k.get('time')))
    out = []
    baked = cubic = 0
    for path, tr in sorted(clip['tracks'].items()):
        if not path:
            report.append('curves on the Animator object itself (path "") skipped: they move the whole object')
            continue
        node = glb.node(path)
        for kind, gpath, n, mirror in (('t', 'translation', 3, MIRROR_POS), ('r', 'rotation', 4, MIRROR_ROT), ('s', 'scale', 3, (1.0, 1.0, 1.0))):
            if kind not in tr or not tr[kind]['keys']:
                continue
            keys = sorted(tr[kind]['keys'], key=lambda k: Y.num(k.get('time')))
            comps = [_component_keys(keys, n, i) for i in range(n)]
            inside = all(start - 1e-6 <= Y.num(k.get('time')) <= stop + 1e-6 for k in keys)
            if _hermite_ok(keys, n) and inside and len(keys) > 1:
                times, values = [], []
                first, last = Y.num(keys[0].get('time')), Y.num(keys[-1].get('time'))
                if first > start + 1e-5:   # hold the first value from the clip start
                    times.append(0.0)
                    values += [0.0] * n + [comps[i][0][1] * mirror[i] for i in range(n)] + [0.0] * n
                for j, k in enumerate(keys):
                    times.append(max(0.0, Y.num(k.get('time')) - start))
                    values += [comps[i][j][2] * mirror[i] for i in range(n)]
                    values += [comps[i][j][1] * mirror[i] for i in range(n)]
                    values += [comps[i][j][3] * mirror[i] for i in range(n)]
                if last < stop - 1e-5:
                    times.append(stop - start)
                    values += [0.0] * n + [comps[i][-1][1] * mirror[i] for i in range(n)] + [0.0] * n
                if any(b <= a for a, b in zip(times, times[1:])):
                    pass  # duplicate key times: fall through to resampling
                else:
                    out.append((node, gpath, G.CUBIC, times, values))
                    cubic += 1
                    continue
            step = any(_stepped(c) for c in comps)
            times = _sample_times(keys, start, stop)
            values = []
            for t in times:
                v = [Y.curve_eval(comps[i], t) for i in range(n)]
                if n == 4:
                    ln = math.sqrt(sum(x * x for x in v)) or 1.0
                    v = [x / ln for x in v]
                values += [v[i] * mirror[i] for i in range(n)]
            out.append((node, gpath, G.STEP if step and len(keys) <= 2 else G.LINEAR, [t - start for t in times], values))
            baked += 1
        if 'e' in tr and tr['e']['keys'] and 'r' not in tr:
            keys = tr['e']['keys']
            comps = [_component_keys(keys, 3, i) for i in range(3)]
            times = _sample_times(keys, start, stop)
            values, prev = [], None
            for t in times:
                q = _euler_quat([Y.curve_eval(comps[i], t) for i in range(3)], tr['e']['order'])
                if prev and sum(a * b for a, b in zip(prev, q)) < 0:
                    q = [-x for x in q]
                prev = q
                values += [q[i] * MIRROR_ROT[i] for i in range(4)]
            out.append((node, 'rotation', G.LINEAR, [t - start for t in times], values))
            baked += 1
    return out, stop - start, {'cubic': cubic, 'resampled': baked}


def to_glb(clip, name=None):
    """(bytes or None, info) of one clip as an animation-only glb."""
    glb = G.Glb()
    notes = []
    chans, duration, stats = channels(clip, glb, notes)
    if not chans:
        return None, {'duration': duration, 'channels': 0, 'notes': notes, **stats}
    glb.animation(name or clip['name'], chans)
    return glb.bytes(), {'duration': round(duration, 4), 'channels': len(chans), 'notes': sorted(set(notes)), **stats}


def unsupported(clip):
    """Human-readable list of what the conversion leaves out."""
    out = []
    groups = {}
    for f in clip['floats']:
        if f['attribute'].startswith(('MotionT.', 'MotionQ.', 'RootT.', 'RootQ.')):
            continue
        key = '%s.%s' % (CLASS_NAMES.get(f['class'], 'class %d' % f['class']), f['attribute'].split('.')[0] if f['attribute'].startswith('m_') else f['attribute'])
        groups[key] = groups.get(key, 0) + 1
    for k, v in sorted(groups.items()):
        out.append('property curve %s (%d)' % (k, v))
    if clip['pptr']:
        out.append('object reference curves (sprite swaps) (%d)' % clip['pptr'])
    if clip['events']:
        out.append('animation events (%d): fire them from the game (view events / Lua)' % clip['events'])
    if clip['mirror']:
        out.append('mirror flag')
    if clip['compressed']:
        out.append('compressed clip (re-save with compression off)')
    if clip['loopBlend']:
        out.append('loop pose blending (Loop Pose)')
    if clip['cycleOffset']:
        out.append('clip cycle offset %.3g' % clip['cycleOffset'])
    return out
