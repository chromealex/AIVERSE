"""TrailRenderer / LineRenderer components -> engine emitters drawn as ribbons (engine/fx trail "mode": "ribbon").

TrailRenderer: Unity adds a point every `minVertexDistance` the object moves and drops points older than `time`.
The engine equivalent is an emitter of hidden particles spawned per metre moved (`perMeter`), living `time`, joined
into one ribbon: when the effect is moved (RtsFx trails, ParticleSystem::move) the ribbon follows, when it stops the
ribbon shrinks away in `time` like Unity's (autodestruct). Width curve / multiplier, colour gradient, material
(texture, blend), texture mode carry over.

LineRenderer: a polyline set in the editor (or, far more often, by a script at runtime). Straight lines and circles
(evenly spaced points, `loop`) become hidden particles spread evenly over a `line` / `ring` shape (arcMode "spread")
joined into a ribbon; other polylines are approximated by the line through their end points. Lines whose points a
script sets (aim lines, range circles) are better served by the engine overlays (range rings, rally lines): the
report says so.
"""
import math

import convert as cv
import unity_yaml as uy
from mapping import color_keys_json
from mapping_look import TEXTURE_MODES
from materials import read_material
from unity_scene import SysLog
from unity_yaml import get, num

CLASS_TRAIL, CLASS_LINE = 96, 120


def _width(params, log, scale):
    mult = num(get(params, 'widthMultiplier'), 1) * scale
    curve = get(params, 'widthCurve')
    keys = uy.curve_keys(curve) if isinstance(curve, dict) else []
    if not keys:
        return mult, [[0, 1], [1, 1]]
    pts = [(i / 32.0, uy.curve_eval(keys, i / 32.0)) for i in range(33)]
    peak = max(abs(v) for _, v in pts) or 1.0
    lo, hi = min(v for _, v in pts), max(v for _, v in pts)
    simple = uy.simplify(pts, max(1e-4, (hi - lo) * 0.01))
    return mult * peak, [[cv.rv(round(t, 3)), cv.rv(round(max(0.0, v / peak), 4))] for t, v in simple]


def _look(comp, assets, textures, log, slot):
    mats = comp.data.get('m_Materials') or []
    mat = read_material(mats[0] if mats else None, assets, log, slot)
    return mat, textures.get(mat)


def _ribbon(comp, params, mat, tex, log, scale, time):
    width, wkeys = _width(params, log, scale)
    grad = get(params, 'colorGradient')
    ckeys = cv.gradient_keys(grad, mat.tint) if isinstance(grad, dict) else [(0, mat.tint), (1, mat.tint)]
    t = {'mode': 'ribbon', 'time': cv.rv(min(10, max(0.01, time))), 'width': wkeys, 'color': color_keys_json(ckeys),
         'texture': tex, 'blend': mat.blend, 'inheritColor': False, 'sizeAffectsWidth': True}
    tm = TEXTURE_MODES.get(int(num(get(params, 'textureMode'), 0)), 'stretch')
    if tm != 'stretch':
        t['textureMode'] = tm
    if int(num(get(params, 'alignment'), 0)) == 1:
        log.approx('ribbon', 'alignment transform Z -> facing the camera')
    if int(num(get(params, 'numCornerVertices'), 0)) or int(num(get(params, 'numCapVertices'), 0)):
        log.approx('ribbon', 'rounded corners / caps dropped')
    return width, t


def map_trail_renderer(ctx, comp, path):
    """-> emitter dict (None when disabled)."""
    log = SysLog(ctx.rep, path)
    d = comp.data
    if not int(num(d.get('m_Enabled'), 1)):
        log.approx('trail renderer', 'disabled in the prefab (imported anyway: scripts usually enable it)')
    params = d.get('m_Parameters') or {}
    time = num(d.get('m_Time'), 5)
    mat, tex = _look(comp, ctx.assets, ctx.tex, log, 'trail renderer')
    width, ribbon = _ribbon(comp, params, mat, tex, log, ctx.scale, time)
    md = max(0.02, num(d.get('m_MinVertexDistance'), 0.1) * ctx.scale)
    em = {'comment': 'Unity TrailRenderer: ' + path, 'texture': tex, 'blend': mat.blend, 'hidden': True,
          'loop': True, 'duration': 1, 'perMeter': cv.rv(min(50, 1.0 / md)), 'life': cv.rv(min(60, max(0.01, time))),
          'speed': 0, 'spread': 0, 'size': cv.rv(max(0.001, width)), 'color': [[0, '#ffffffff'], [1, '#ffffffff']],
          'trail': ribbon, 'maxParticles': int(min(2000, max(8, math.ceil(time * 60))))}
    if not int(num(d.get('m_Autodestruct'), 0)) and not int(num(d.get('m_Emitting'), 1)):
        log.approx('trail renderer', 'not emitting in the prefab: imported emitting')
    sort = int(num(d.get('m_SortingOrder'), 0))
    if sort:
        em['sortOrder'] = sort
    log.ok('trail renderer', 'ribbon of particles spawned per %.3g m, living %.3g s' % (md, time))
    return em


def _fit(points):
    """('line', centre, direction, half length) or ('ring', centre, normal, radius) or ('poly', ...) for a polyline."""
    n = len(points)
    if n < 2:
        return None
    c = tuple(sum(p[i] for p in points) / n for i in range(3))
    a, b = points[0], points[-1]
    d = tuple(y - x for x, y in zip(a, b))
    L = math.sqrt(sum(x * x for x in d))
    if L > 1e-6:
        u = tuple(x / L for x in d)
        off = max(math.sqrt(max(0.0, sum((p[i] - a[i]) ** 2 for i in range(3)) - sum((p[i] - a[i]) * u[i] for i in range(3)) ** 2))
                  for p in points)
        if off <= max(1e-3, L * 0.01):
            return ('line', tuple((x + y) * 0.5 for x, y in zip(a, b)), u, L * 0.5)
    rs = [math.sqrt(sum((p[i] - c[i]) ** 2 for i in range(3))) for p in points]
    r = sum(rs) / n
    if n >= 6 and r > 1e-6 and max(abs(x - r) for x in rs) <= r * 0.03:
        # plane normal from two spread chords
        p0, p1, p2 = points[0], points[n // 3], points[2 * n // 3]
        v1 = tuple(y - x for x, y in zip(p0, p1))
        v2 = tuple(y - x for x, y in zip(p0, p2))
        nrm = (v1[1] * v2[2] - v1[2] * v2[1], v1[2] * v2[0] - v1[0] * v2[2], v1[0] * v2[1] - v1[1] * v2[0])
        nl = math.sqrt(sum(x * x for x in nrm)) or 1.0
        if nrm[1] < 0:   # either side of the plane draws the same circle: keep it unflipped
            nl = -nl
        return ('ring', c, tuple(x / nl for x in nrm), r)
    return ('poly', tuple((x + y) * 0.5 for x, y in zip(a, b)), d, L * 0.5)


def map_line_renderer(ctx, comp, path, wpos, wrot, wscale):
    log = SysLog(ctx.rep, path)
    d = comp.data
    params = d.get('m_Parameters') or {}
    pts = [uy.vec3(p) for p in d.get('m_Positions') or [] if isinstance(p, dict)]
    log.approx('line renderer', 'line points are usually set by a script at runtime: for aim lines / range circles prefer an '
                                'overlay (overlay_set: range ring, rally line); this is the editor-time shape')
    if len(pts) < 2:
        log.drop('line renderer', 'fewer than 2 points')
        return None
    if not int(num(d.get('m_UseWorldSpace'), 1)):
        pts = [tuple(a + b for a, b in zip(cv.mv(cv.q_mat(wrot), tuple(x * s for x, s in zip(p, wscale))), wpos)) for p in pts]
    pts = [ctx.eff_point(p) for p in pts]
    fit = _fit(pts)
    mat, tex = _look(comp, ctx.assets, ctx.tex, log, 'line renderer')
    width, ribbon = _ribbon(comp, params, mat, tex, log, 1.0, 60)
    width *= ctx.scale
    n = len(pts)
    loop = int(num(d.get('m_Loop'), 0))
    em = {'comment': 'Unity LineRenderer: ' + path, 'texture': tex, 'blend': mat.blend, 'hidden': True, 'loop': True,
          'duration': 60, 'bursts': [{'time': 0, 'count': n, 'cycles': 0, 'interval': 60}],
          'life': 60, 'speed': 0, 'spread': 0, 'size': cv.rv(max(0.001, width)), 'arcMode': 'spread',
          'color': [[0, '#ffffffff'], [1, '#ffffffff']], 'trail': ribbon, 'maxParticles': n}
    kind, c, axis, r = fit
    if kind == 'poly':
        L = vm_len(axis)
        axis = tuple(x / L for x in axis) if L > 1e-6 else (1.0, 0.0, 0.0)
    if vm_len(c) > 1e-5:
        em['offset'] = [cv.rv(round(x, 4)) for x in c]
    if kind == 'ring':
        em['shape'] = 'ring'
        em['radius'] = cv.rv(round(r, 4))
        M = cv.rot_between((0.0, 1.0, 0.0), axis)
        if not cv.is_identity(M):
            em['shapeRotation'] = [cv.rv(round(x, 3)) for x in cv.euler_zxy(M)]
        log.ok('line renderer', 'circle of %d points -> ring' % n)
        if loop:
            log.approx('line renderer', 'closed loop: the ribbon leaves out the closing segment')
    else:
        em['shape'] = 'line'
        # spread puts particle i of n at (2 i / n - 1) * radius along the line: stretch and shift it so that the
        # first and the last particle land on the end points
        k = n / max(1.0, n - 1.0)
        em['radius'] = cv.rv(round(r * k, 4))
        c = tuple(x + a * r / max(1.0, n - 1.0) for x, a in zip(c, axis))
        em['offset'] = [cv.rv(round(x, 4)) for x in c]
        M = cv.rot_between((1.0, 0.0, 0.0), axis if vm_len(axis) > 1e-6 else (1.0, 0.0, 0.0))
        if not cv.is_identity(M):
            em['shapeRotation'] = [cv.rv(round(x, 3)) for x in cv.euler_zxy(M)]
        if kind == 'poly':
            log.approx('line renderer', 'polyline of %d points -> the straight line between its ends' % n)
        else:
            log.ok('line renderer', 'straight line')
    return em


def vm_len(v):
    return math.sqrt(sum(x * x for x in v))
