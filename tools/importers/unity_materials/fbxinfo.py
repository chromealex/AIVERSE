"""FBX node / material slot listing (binary 6.x-7.x and ASCII) for Unity submesh slot matching.

Unity builds one submesh per material connected to a mesh node, in connection order, and a
renderer's m_Materials[i] fills submesh i. The engine's FBX loader (ufbx) names its primitives'
materials after the same FBX materials, so slot i of node N maps to the engine material name
``models[N].materials[i]``. Only names and connections are read; geometry is skipped.
"""
import re
import struct
import zlib

MAGIC = b'Kaydara FBX Binary  \x00'


class FbxError(ValueError):
    pass


def _binary_nodes(data):
    if len(data) < 27:
        raise FbxError('truncated FBX header')
    version = struct.unpack_from('<I', data, 23)[0]
    wide = version >= 7500
    head = struct.Struct('<QQQB' if wide else '<IIIB')

    def props(pos, count):
        out = []
        for _ in range(count):
            t = data[pos:pos + 1]
            pos += 1
            if t in (b'S', b'R'):
                n = struct.unpack_from('<I', data, pos)[0]
                raw = data[pos + 4:pos + 4 + n]
                out.append(raw.decode('utf-8', 'replace') if t == b'S' else raw)
                pos += 4 + n
            elif t in (b'f', b'd', b'l', b'i', b'b'):
                n, enc, clen = struct.unpack_from('<III', data, pos)
                pos += 12
                if t == b'i' or t == b'l':
                    raw = data[pos:pos + clen]
                    raw = zlib.decompress(raw) if enc == 1 else raw
                    out.append(list(struct.unpack('<%d%s' % (n, 'i' if t == b'i' else 'q'), raw[:n * (4 if t == b'i' else 8)])))
                else:
                    out.append(None)   # float/bool arrays: geometry, not needed
                pos += clen
            else:
                size = {b'Y': 2, b'C': 1, b'I': 4, b'F': 4, b'D': 8, b'L': 8}.get(t)
                if size is None:
                    raise FbxError('unknown FBX property type %r' % t)
                fmt = {b'Y': '<h', b'C': '<?', b'I': '<i', b'F': '<f', b'D': '<d', b'L': '<q'}[t]
                out.append(struct.unpack_from(fmt, data, pos)[0])
                pos += size
        return out

    def node(pos, depth):
        if pos + head.size > len(data):
            raise FbxError('truncated FBX node')
        end, count, plen, nlen = head.unpack_from(data, pos)
        if end == 0:
            return None, pos + head.size
        if end > len(data) or depth > 64:
            raise FbxError('corrupt FBX node')
        name = data[pos + head.size:pos + head.size + nlen].decode('ascii', 'replace')
        p = pos + head.size + nlen
        values = props(p, count)
        p += plen
        children = []
        while p < end:
            child, p = node(p, depth + 1)
            if child is None:
                break
            children.append(child)
        return (name, values, children), end

    pos, top = 27, []
    while pos < len(data):
        n, pos = node(pos, 0)
        if n is None:
            break
        top.append(n)
    return top


def _split_name(s):
    # binary "Name\x00\x01Class"; ASCII "Class::Name"
    if '\x00\x01' in s:
        return s.split('\x00\x01')[0]
    return s.split('::', 1)[1] if '::' in s else s


def _from_binary(data):
    top = _binary_nodes(data)
    objects, links = {}, []
    for name, _, children in top:
        if name == 'Objects':
            for kind, values, _ in children:
                if kind in ('Model', 'Material', 'Geometry') and len(values) >= 2:
                    oid = values[0]
                    objects[oid] = (kind, _split_name(values[1]), values[2] if len(values) > 2 else '')
        elif name == 'Connections':
            for kind, values, _ in children:
                if kind == 'C' and len(values) >= 3 and values[0] == 'OO':
                    links.append((values[1], values[2]))
    return objects, links


def _from_ascii(text):
    objects, links = {}, []
    for m in re.finditer(r'^\s*(Model|Material|Geometry):\s*(-?\d+)\s*,\s*"([^"]*)"\s*,\s*"([^"]*)"', text, re.M):
        objects[int(m[2])] = (m[1], _split_name(m[3]), m[4])
    for m in re.finditer(r'^\s*C:\s*"OO"\s*,\s*(-?\d+)\s*,\s*(-?\d+)', text, re.M):
        links.append((int(m[1]), int(m[2])))
    if not objects:   # FBX 6.x ASCII: names instead of ids
        for m in re.finditer(r'^\s*(Model|Material):\s*"([^"]*)"\s*,\s*"([^"]*)"', text, re.M):
            objects[m[2]] = (m[1], _split_name(m[2]), m[3])
        for m in re.finditer(r'^\s*Connect:\s*"OO"\s*,\s*"([^"]*)"\s*,\s*"([^"]*)"', text, re.M):
            links.append((m[1], m[2]))
    return objects, links


def read(data):
    """{'models': [{'name', 'type', 'materials': [...], 'mesh': bool}], 'materials': [unique names]}"""
    if data.startswith(MAGIC):
        objects, links = _from_binary(data)
    else:
        objects, links = _from_ascii(data.decode('utf-8', 'replace'))
    models = {oid: dict(name=o[1], type=o[2], materials=[], mesh=o[2] == 'Mesh') for oid, o in objects.items() if o[0] == 'Model'}
    for child, parent in links:
        c, p = objects.get(child), models.get(parent)
        if p is None or c is None:
            continue
        if c[0] == 'Material':
            p['materials'].append(c[1])
        elif c[0] == 'Geometry':
            p['mesh'] = True
    names = []
    for o in objects.values():
        if o[0] == 'Material' and o[1] not in names:
            names.append(o[1])
    return dict(models=[m for m in models.values()], materials=names)


def read_file(path):
    with open(path, 'rb') as f:
        return read(f.read())
