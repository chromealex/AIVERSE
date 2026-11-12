"""Unity binary SerializedFile reader (format 9..22) driven by the embedded type tree.

Native assets such as TerrainData stay binary even under Force Text. Editor-saved files carry a
type tree, so every field is decoded by name without a Unity runtime. Files without a type tree
(player builds) are rejected: use tools/importers/unity_project/editor/AiverseTerrainExport.cs.
"""
import struct
from array import array
import sys

_COMMON = ('AABB', 'AnimationClip', 'AnimationCurve', 'AnimationState', 'Array', 'Base', 'BitField', 'bitset',
           'bool', 'char', 'ColorRGBA', 'Component', 'data', 'deque', 'double', 'dynamic_array', 'FastPropertyName',
           'first', 'float', 'Font', 'GameObject', 'Generic Mono', 'GradientNEW', 'GUID', 'GUIStyle', 'int', 'list',
           'long long', 'map', 'Matrix4x4f', 'MdFour', 'MonoBehaviour', 'MonoScript', 'm_ByteSize', 'm_Curve',
           'm_EditorClassIdentifier', 'm_EditorHideFlags', 'm_Enabled', 'm_ExtensionPtr', 'm_GameObject', 'm_Index',
           'm_IsArray', 'm_IsStatic', 'm_MetaFlag', 'm_Name', 'm_ObjectHideFlags', 'm_PrefabInternal',
           'm_PrefabParentObject', 'm_Script', 'm_StaticEditorFlags', 'm_Type', 'm_Version', 'Object', 'pair',
           'PPtr<Component>', 'PPtr<GameObject>', 'PPtr<Material>', 'PPtr<MonoBehaviour>', 'PPtr<MonoScript>',
           'PPtr<Object>', 'PPtr<Prefab>', 'PPtr<Sprite>', 'PPtr<TextAsset>', 'PPtr<Texture>', 'PPtr<Texture2D>',
           'PPtr<Transform>', 'Prefab', 'Quaternionf', 'Rectf', 'RectInt', 'RectOffset', 'second', 'set', 'short',
           'size', 'SInt16', 'SInt32', 'SInt64', 'SInt8', 'staticvector', 'string', 'TextAsset', 'TextMesh',
           'Texture', 'Texture2D', 'Transform', 'TypelessData', 'UInt16', 'UInt32', 'UInt64', 'UInt8',
           'unsigned int', 'unsigned long long', 'unsigned short', 'vector', 'Vector2f', 'Vector3f', 'Vector4f',
           'm_ScriptingClassIdentifier', 'Gradient', 'Type*', 'int2_storage', 'int3_storage', 'BoundsInt',
           'm_CorrespondingSourceObject', 'm_PrefabInstance', 'm_PrefabAsset', 'FileSize', 'Hash128',
           'RenderingLayerMask')
# Unity's built-in string table: type tree offsets with the high bit set index into it.


def _common_table():
    table, offset = {}, 0
    for n in _COMMON:
        table[offset] = n
        offset += len(n) + 1
    return table


COMMON = _common_table()
PRIM = {'bool': '?', 'char': 'B', 'SInt8': 'b', 'UInt8': 'B', 'SInt16': 'h', 'short': 'h', 'UInt16': 'H', 'unsigned short': 'H',
        'SInt32': 'i', 'int': 'i', 'UInt32': 'I', 'unsigned int': 'I', 'Type*': 'i', 'SInt64': 'q', 'long long': 'q',
        'UInt64': 'Q', 'unsigned long long': 'Q', 'FileSize': 'Q', 'float': 'f', 'double': 'd'}
ALIGN = 0x4000


class Node:
    __slots__ = ('type', 'name', 'size', 'flags', 'meta', 'level', 'children')

    def __init__(self, type_, name, size, flags, meta, level):
        self.type, self.name, self.size, self.flags, self.meta, self.level, self.children = type_, name, size, flags, meta, level, []


class Reader:
    def __init__(self, data, little=True):
        self.data, self.pos, self.e = data, 0, '<' if little else '>'

    def u(self, fmt):
        v = struct.unpack_from(self.e + fmt, self.data, self.pos)
        self.pos += struct.calcsize(self.e + fmt)
        return v[0] if len(v) == 1 else v

    def cstr(self):
        end = self.data.index(b'\0', self.pos)
        s = self.data[self.pos:end].decode('utf-8', 'replace')
        self.pos = end + 1
        return s

    def bytes(self, n):
        if n < 0 or self.pos + n > len(self.data):
            raise ValueError('truncated Unity serialized data')
        b = self.data[self.pos:self.pos + n]
        self.pos += n
        return b

    def align(self, n=4):
        self.pos = (self.pos + n - 1) // n * n


class SerializedType:
    def __init__(self):
        self.class_id, self.script_index, self.tree, self.ref = 0, -1, None, None


class SerializedFile:
    """objects: {pathID: (classID, typeIndex, offset, size)}; externals: [(guid hex, path)]."""

    def __init__(self, data):
        self.data = data
        h = Reader(data, little=False)
        if len(data) < 48:
            raise ValueError('not a Unity serialized file')
        meta_size, file_size, self.version, data_offset = h.u('IIII')
        if not 9 <= self.version <= 22:
            raise ValueError('unsupported Unity serialized file version %d' % self.version)
        little = h.u('B') == 0
        h.bytes(3)
        if self.version >= 22:
            meta_size, file_size, data_offset, _ = h.u('IqqQ')
        if file_size != len(data):
            raise ValueError('Unity serialized file size mismatch (truncated or not serialized data)')
        self.data_offset = data_offset
        r = Reader(data, little)
        r.pos = h.pos
        self.unity_version = r.cstr()
        self.platform = r.u('i')
        self.type_tree = bool(r.u('B')) if self.version >= 13 else True
        self.types = [self._type(r, False) for _ in range(r.u('i'))]
        if 7 <= self.version < 14:
            r.u('i')
        self.objects = {}
        for _ in range(r.u('i')):
            if self.version >= 14:
                r.align()
                path_id = r.u('q')
            else:
                path_id = r.u('i')
            start = r.u('q') if self.version >= 22 else r.u('I')
            size = r.u('I')
            type_index = r.u('i')
            if self.version < 16:
                class_id = r.u('H')
            else:
                class_id = self.types[type_index].class_id
            if self.version < 11: r.u('H')
            if 11 <= self.version < 17: r.u('h')
            if self.version in (15, 16): r.u('B')
            self.objects[path_id] = (class_id, type_index, data_offset + start, size)
        for _ in range(r.u('i')):
            r.u('i')
            r.align() if self.version >= 14 else None
            r.u('q') if self.version >= 14 else r.u('i')
        self.externals = []
        for _ in range(r.u('i')):
            if self.version >= 6: r.cstr()
            guid = r.bytes(16) if self.version >= 5 else b''
            if self.version >= 5: r.u('i')
            self.externals.append((_guid_hex(guid), r.cstr()))
        self.ref_types = [self._type(r, True) for _ in range(r.u('i'))] if self.version >= 20 else []

    def _type(self, r, is_ref):
        t = SerializedType()
        t.class_id = r.u('i')
        if self.version >= 16: r.u('B')
        if self.version >= 17: t.script_index = r.u('h')
        if self.version >= 13:
            if (is_ref and t.script_index >= 0) or (self.version < 16 and t.class_id < 0) or (self.version >= 16 and t.class_id == 114):
                r.bytes(16)
            r.bytes(16)
        if self.type_tree:
            if self.version >= 12 or self.version == 10:
                t.tree = self._blob(r)
            else:
                raise ValueError('legacy (pre-5.x) type trees are not supported')
            if self.version >= 21:
                if is_ref:
                    t.ref = (r.cstr(), r.cstr(), r.cstr())
                else:
                    for _ in range(r.u('i')): r.u('i')
        return t

    def _blob(self, r):
        count, strsize = r.u('i'), r.u('i')
        raw = []
        for _ in range(count):
            _ver, level, flags, tstr, nstr, size, _index, meta = r.u('HBBIIiii')
            if self.version >= 19: r.u('Q')
            raw.append((level, flags, tstr, nstr, size, meta))
        strings = r.bytes(strsize)

        def name(off):
            if off & 0x80000000:
                return COMMON.get(off & 0x7fffffff, '?%d' % (off & 0x7fffffff))
            end = strings.index(b'\0', off)
            return strings[off:end].decode('utf-8', 'replace')
        nodes = [Node(name(t), name(n), s, f, m, lv) for lv, f, t, n, s, m in raw]
        stack = []
        for n in nodes:
            while stack and stack[-1].level >= n.level:
                stack.pop()
            if stack:
                stack[-1].children.append(n)
            stack.append(n)
        return nodes[0] if nodes else None

    def read(self, path_id):
        class_id, type_index, start, size = self.objects[path_id]
        t = self.types[type_index]
        if t.tree is None:
            raise ValueError('object %d has no type tree (player build data); use the Unity editor export script' % path_id)
        r = Reader(self.data[start:start + size], self.e_little())
        value = _value(r, t.tree)
        return class_id, value

    def e_little(self):
        return self.data[16] == 0

    def by_class(self, class_id):
        return sorted(p for p, o in self.objects.items() if o[0] == class_id)


def _guid_hex(b):
    # Unity writes the GUID as four little-endian uint32; the text form prints nibbles of each byte swapped.
    return ''.join('%x%x' % (c & 15, c >> 4) for c in b)


def _value(r, node):
    align = bool(node.meta & ALIGN)
    t = node.type
    if t in PRIM:
        v = r.u(PRIM[t])
    elif t == 'string':
        v = r.bytes(r.u('i')).decode('utf-8', 'replace')
        if node.children and node.children[0].meta & ALIGN:
            align = True
    elif t == 'TypelessData':
        v = r.bytes(r.u('i'))
    elif node.children and node.children[0].type == 'Array':
        arr = node.children[0]
        if arr.meta & ALIGN: align = True
        count = r.u('i')
        elem = arr.children[1]
        if elem.type in PRIM and not elem.children:
            code = PRIM[elem.type]
            width = struct.calcsize(code)
            raw = r.bytes(count * width)
            if code == '?':
                v = [b != 0 for b in raw]
            else:
                a = array(code)
                if a.itemsize != width:
                    v = list(struct.unpack(r.e + code * count, raw))
                else:
                    a.frombytes(raw)
                    if (r.e == '<') != (sys.byteorder == 'little'): a.byteswap()
                    v = a
            if elem.meta & ALIGN: r.align()
        elif t == 'map':
            v = [(_value(r, elem.children[0]), _value(r, elem.children[1])) for _ in range(count)]
        else:
            v = [_value(r, elem) for _ in range(count)]
    elif t == 'Array' or node.flags & 1:
        count = r.u('i')
        v = [_value(r, node.children[1]) for _ in range(count)]
    else:
        v = {c.name: _value(r, c) for c in node.children}
    if align:
        r.align()
    return v


def load(path):
    with open(path, 'rb') as f:
        return SerializedFile(f.read())


def is_serialized(head):
    """Cheap test on the first 48 bytes."""
    if len(head) < 20 or head.lstrip(b'\xef\xbb\xbf').startswith(b'%YAML'):
        return False
    version = struct.unpack_from('>I', head, 8)[0]
    return 9 <= version <= 22
