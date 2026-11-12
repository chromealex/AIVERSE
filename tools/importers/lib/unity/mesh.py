"""Unity Mesh (class 43, Force Text) and built-in primitive meshes -> Wavefront OBJ text.

Vertices are transformed by the object's world rotation / scale (position stays in the entity), then
converted to the engine frame: Z flipped (Unity is left-handed) and the triangle winding reversed.
"""
import math
import struct
from .unity_scene import q_rot as _qrot

# VertexAttributeFormat -> (struct code, normaliser)
FORMATS = {0: ('f', None), 1: ('e', None), 2: ('B', 255.0), 3: ('b', 127.0), 4: ('H', 65535.0), 5: ('h', 32767.0),
           6: ('B', None), 7: ('b', None), 8: ('H', None), 9: ('h', None), 10: ('I', None), 11: ('i', None)}
BUILTIN_GUID = '0000000000000000e000000000000000'


def _hex(s):
    return bytes.fromhex(str(s or '').strip())


def decode(data):
    """Mesh document data -> dict(positions, normals, uvs, submeshes [[i0, i1, i2, ...]]) or raise ValueError."""
    if int(data.get('m_MeshCompression', 0) or 0):
        raise ValueError('compressed mesh (Mesh Compression enabled) is not supported')
    vd = data.get('m_VertexData') or {}
    count = int(vd.get('m_VertexCount', 0) or 0)
    raw = _hex(vd.get('_typelessdata'))
    channels = vd.get('m_Channels') or []
    if not count or not raw or not channels:
        raise ValueError('mesh has no readable vertex data (Read/Write disabled or streamed)')
    streams = {}
    for c in channels:
        dim = int(c.get('dimension', 0)) & 15
        if dim:
            code = FORMATS.get(int(c.get('format', 0)), ('f', None))[0]
            st = streams.setdefault(int(c.get('stream', 0)), 0)
            streams[int(c.get('stream', 0))] = max(st, int(c.get('offset', 0)) + dim * struct.calcsize(code))
    starts, at = {}, 0
    for s in sorted(streams):
        at = (at + 15) // 16 * 16 if s else at
        starts[s] = at
        at += streams[s] * count

    def channel(i):
        if i >= len(channels):
            return None
        c = channels[i]
        dim = int(c.get('dimension', 0)) & 15
        if not dim:
            return None
        code, norm = FORMATS.get(int(c.get('format', 0)), ('f', None))
        s = int(c.get('stream', 0))
        stride, base = streams[s], starts[s] + int(c.get('offset', 0))
        out = []
        for v in range(count):
            vals = struct.unpack_from('<' + code * dim, raw, base + v * stride)
            out.append([x / norm for x in vals] if norm else list(vals))
        return out
    positions, normals, uvs = channel(0), channel(1), channel(4)
    if positions is None:
        raise ValueError('mesh has no positions')
    index = _hex(data.get('m_IndexBuffer'))
    wide = int(data.get('m_IndexFormat', 0) or 0) == 1
    code = 'I' if wide else 'H'
    size = 4 if wide else 2
    subs = []
    for sm in data.get('m_SubMeshes') or []:
        if int(sm.get('topology', 0) or 0) != 0:
            continue
        first, n = int(sm.get('firstByte', 0)) // size, int(sm.get('indexCount', 0))
        base = int(sm.get('baseVertex', 0) or 0)
        idx = list(struct.unpack_from('<%d%s' % (n, code), index, first * size)) if n else []
        subs.append([i + base for i in idx])
    return dict(positions=positions, normals=normals, uvs=uvs, submeshes=subs)


def builtin(file_id):
    """Unity built-in meshes referenced with guid 0000000000000000e000000000000000."""
    if file_id == 10202:  # Cube: 1 x 1 x 1
        p, n, u, idx = [], [], [], []
        for axis in range(3):
            for sign in (1, -1):
                normal = [0, 0, 0]
                normal[axis] = sign
                a, b = [(axis + 1) % 3, (axis + 2) % 3]
                base = len(p)
                for du, dv in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                    v = [0.0, 0.0, 0.0]
                    v[axis] = 0.5 * sign
                    v[a], v[b] = 0.5 * du * sign, 0.5 * dv
                    p.append(v)
                    n.append(list(normal))
                    u.append([(du + 1) / 2, (dv + 1) / 2])
                # clockwise seen from outside (Unity front faces)
                idx += [base, base + 2, base + 1, base, base + 3, base + 2]
        return dict(positions=p, normals=n, uvs=u, submeshes=[idx])
    if file_id in (10209, 10210):  # Plane 10 x 10 (XZ) / Quad 1 x 1 (XY, facing -Z)
        s = 5.0 if file_id == 10209 else 0.5
        if file_id == 10209:
            p = [[-s, 0, -s], [s, 0, -s], [s, 0, s], [-s, 0, s]]
            n = [[0, 1, 0]] * 4
        else:
            p = [[-s, -s, 0], [s, -s, 0], [s, s, 0], [-s, s, 0]]
            n = [[0, 0, -1]] * 4
        return dict(positions=p, normals=n, uvs=[[0, 0], [1, 0], [1, 1], [0, 1]], submeshes=[[0, 2, 1, 0, 3, 2]])
    if file_id in (10206, 10207, 10208):  # Cylinder (h 2, r 0.5), Sphere (r 0.5), Capsule (approximated by a cylinder)
        seg, rings = 16, 8
        p, n, u, idx = [], [], [], []
        if file_id == 10207:
            for r in range(rings + 1):
                phi = math.pi * r / rings
                for s in range(seg + 1):
                    th = 2 * math.pi * s / seg
                    d = [math.sin(phi) * math.cos(th), math.cos(phi), math.sin(phi) * math.sin(th)]
                    p.append([0.5 * x for x in d])
                    n.append(d)
                    u.append([s / seg, 1 - r / rings])
            for r in range(rings):
                for s in range(seg):
                    a, b = r * (seg + 1) + s, (r + 1) * (seg + 1) + s
                    idx += [a, a + 1, b, a + 1, b + 1, b]
        else:
            for y in (-1.0, 1.0):
                for s in range(seg + 1):
                    th = 2 * math.pi * s / seg
                    p.append([0.5 * math.cos(th), y, 0.5 * math.sin(th)])
                    n.append([math.cos(th), 0, math.sin(th)])
                    u.append([s / seg, (y + 1) / 2])
            for s in range(seg):
                a, b = s, seg + 1 + s
                idx += [a, b, a + 1, a + 1, b, b + 1]
            for y in (-1.0, 1.0):
                c = len(p)
                p.append([0, y, 0])
                n.append([0, y, 0])
                u.append([0.5, 0.5])
                for s in range(seg):
                    th = 2 * math.pi * s / seg
                    p.append([0.5 * math.cos(th), y, 0.5 * math.sin(th)])
                    n.append([0, y, 0])
                    u.append([0.5 + 0.5 * math.cos(th), 0.5 + 0.5 * math.sin(th)])
                for s in range(seg):
                    a, b = c + 1 + s, c + 1 + (s + 1) % seg
                    idx += [c, b, a] if y > 0 else [c, a, b]
        return dict(positions=p, normals=n, uvs=u, submeshes=[idx])
    return None


def to_obj(mesh, rotation=(0.0, 0.0, 0.0, 1.0), scale=(1.0, 1.0, 1.0), name='mesh'):
    """OBJ text in the engine frame; returns (text, bounds min, bounds max) with bounds in engine axes."""
    lines = ['# converted from a Unity mesh by tools/importers (engine frame: Z flipped)', 'o ' + name]
    lo, hi = [1e30] * 3, [-1e30] * 3
    for v in mesh['positions']:
        w = _qrot(rotation, [v[0] * scale[0], v[1] * scale[1], v[2] * scale[2]])
        w = [w[0], w[1], -w[2]]
        for i in range(3):
            lo[i], hi[i] = min(lo[i], w[i]), max(hi[i], w[i])
        lines.append('v %.5f %.5f %.5f' % tuple(w))
    flip = (scale[0] * scale[1] * scale[2]) < 0
    for t in mesh.get('uvs') or []:
        lines.append('vt %.5f %.5f' % (t[0], t[1]))
    for nv in mesh.get('normals') or []:
        w = _qrot(rotation, [nv[0] / (scale[0] or 1), nv[1] / (scale[1] or 1), nv[2] / (scale[2] or 1)])
        length = math.sqrt(sum(c * c for c in w)) or 1
        lines.append('vn %.4f %.4f %.4f' % (w[0] / length, w[1] / length, -w[2] / length))
    has_t, has_n = bool(mesh.get('uvs')), bool(mesh.get('normals'))

    def ref(i):
        i += 1
        if has_t and has_n:
            return '%d/%d/%d' % (i, i, i)
        if has_n:
            return '%d//%d' % (i, i)
        if has_t:
            return '%d/%d' % (i, i)
        return str(i)
    for si, sub in enumerate(mesh['submeshes']):
        lines.append('g sub%d' % si)
        for k in range(0, len(sub) - 2, 3):
            a, b, c = sub[k], sub[k + 1], sub[k + 2]
            # Z flip keeps the screen-space winding: reverse it (clockwise -> counter-clockwise front faces)
            tri = (a, b, c) if flip else (a, c, b)
            lines.append('f ' + ' '.join(ref(i) for i in tri))
    if lo[0] > hi[0]:
        lo, hi = [0, 0, 0], [0, 0, 0]
    return '\n'.join(lines) + '\n', lo, hi
