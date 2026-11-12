"""World import: binary TerrainData, embedded / built-in meshes, spline roads, marker rules, map configs.
Synthetic fixtures only, written at test time (no Unity assets are committed)."""
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from lib.png import write_png
from lib.port.common import read_json
from lib.port.emit import emit
from lib.port import world as pworld
from lib.unity import serialized, terrain as uterrain, mesh as umesh
from unity_project import importer as unity
ROOT = Path(__file__).resolve().parents[3]

# ------------------------------------------------------------- minimal SerializedFile writer (format 22)


def N(type_, name, *children, align=False):
    return (type_, name, list(children), align)


def V(name, elem, align=False):
    return N('vector', name, N('Array', 'Array', N('int', 'size'), elem, align=align))


PPTR = lambda name: N('PPtr<Object>', name, N('int', 'm_FileID'), N('SInt64', 'm_PathID'))
VEC3 = lambda name: N('Vector3f', name, N('float', 'x'), N('float', 'y'), N('float', 'z'))
STR = lambda name: N('string', name, N('Array', 'Array', N('int', 'size'), N('char', 'data'), align=True))
SIZES = {'int': 4, 'SInt64': 8, 'float': 4, 'char': 1, 'UInt8': 1, 'SInt16': 2, 'unsigned int': 4}
CODES = {'int': 'i', 'SInt64': 'q', 'float': 'f', 'char': 'B', 'UInt8': 'B', 'SInt16': 'h', 'unsigned int': 'I'}

TEXTURE = N('Texture2D', 'Base', STR('m_Name'), N('int', 'm_Width'), N('int', 'm_Height'), N('int', 'm_TextureFormat'),
            N('TypelessData', 'image data', N('int', 'size'), N('UInt8', 'data')),
            N('StreamingInfo', 'm_StreamData', N('unsigned int', 'offset'), N('unsigned int', 'size'), STR('path')))
TERRAIN = N('TerrainData', 'Base', STR('m_Name'),
            N('SplatDatabase', 'm_SplatDatabase', V('m_TerrainLayers', PPTR('data')), V('m_AlphaTextures', PPTR('data')), N('int', 'm_AlphamapResolution')),
            N('DetailDatabase', 'm_DetailDatabase',
              V('m_Patches', N('DetailPatch', 'data', V('layerIndices', N('UInt8', 'data'), align=True), V('numberOfObjects', N('UInt8', 'data'), align=True))),
              V('m_DetailPrototypes', N('DetailPrototype', 'data', PPTR('prototype'), PPTR('prototypeTexture'), N('float', 'minWidth'),
                                        N('float', 'maxWidth'), N('int', 'usePrototypeMesh'))),
              N('int', 'm_PatchCount'), N('int', 'm_PatchSamples'),
              V('m_TreeInstances', N('TreeInstance', 'data', VEC3('position'), N('float', 'widthScale'), N('float', 'heightScale'),
                                     N('float', 'rotation'), N('int', 'index'))),
              V('m_TreePrototypes', N('TreePrototype', 'data', PPTR('prefab')))),
            N('Heightmap', 'm_Heightmap', V('m_Heights', N('SInt16', 'data'), align=True), V('m_Holes', N('UInt8', 'data'), align=True),
              N('int', 'm_Resolution'), VEC3('m_Scale')))


def blob(tree):
    nodes, strings, offsets = [], bytearray(), {}

    def off(s):
        if s not in offsets:
            offsets[s] = len(strings)
            strings.extend(s.encode() + b'\0')
        return offsets[s]

    def walk(n, level):
        t, name, children, align = n
        nodes.append(struct.pack('<HBBIIiiiQ', 1, level, 1 if t == 'Array' else 0, off(t), off(name), SIZES.get(t, -1), len(nodes),
                                 0x4000 if align else 0, 0))
        for c in children:
            walk(c, level + 1)
    walk(tree, 0)
    return struct.pack('<ii', len(nodes), len(strings)) + b''.join(nodes) + bytes(strings) + struct.pack('<i', 0)


def value(n, v, out):
    t, name, children, align = n
    if t in CODES:
        out += struct.pack('<' + CODES[t], v)
    elif t == 'string':
        b = v.encode()
        out += struct.pack('<i', len(b)) + b
        while len(out) % 4: out.append(0)
    elif t == 'TypelessData':
        out += struct.pack('<i', len(v)) + v
    elif children and children[0][0] == 'Array':
        arr = children[0]
        out += struct.pack('<i', len(v))
        for x in v:
            value(arr[2][1], x, out)
        if arr[3]:
            while len(out) % 4: out.append(0)
    else:
        for c in children:
            value(c, v[c[1]], out)
    if align:
        while len(out) % 4: out.append(0)


def guid_bytes(g):
    return bytes(int(g[i + 1], 16) << 4 | int(g[i], 16) for i in range(0, 32, 2))


def serialized_file(objects, externals):
    """objects: [(pathID, classID, tree, value)]; externals: [guid]. Little-endian, type tree, format 22."""
    trees = []
    for _, cid, tree, _ in objects:
        if (cid, tree) not in trees: trees.append((cid, tree))
    meta = bytearray(b'2022.3.0f1\0' + struct.pack('<iBi', -2, 1, len(trees)))
    for cid, tree in trees:
        meta += struct.pack('<iBh', cid, 0, -1) + bytes(16) + blob(tree)
    datas = []
    for pid, cid, tree, v in objects:
        b = bytearray()
        value(tree, v, b)
        datas.append(bytes(b))
    head = 48
    meta += struct.pack('<i', len(objects))
    entries, start = [], 0
    for (pid, cid, tree, _), d in zip(objects, datas):
        entries.append((pid, start, len(d), trees.index((cid, tree))))
        start = (start + len(d) + 7) // 8 * 8
    for pid, st, size, ti in entries:
        while (head + len(meta)) % 4: meta.append(0)
        meta += struct.pack('<qqIi', pid, st, size, ti)
    meta += struct.pack('<i', 0) + struct.pack('<i', len(externals))
    for g in externals:
        meta += b'\0' + guid_bytes(g) + struct.pack('<i', 2) + b'\0'
    meta += struct.pack('<i', 0) + b'\0'
    data_offset = (head + len(meta) + 15) // 16 * 16
    body = bytearray()
    for (pid, st, size, ti), d in zip(entries, datas):
        body += bytes(st - len(body)) + d
    header = struct.pack('>IIIIB3xIqqQ', 0, 0, 22, 0, 0, len(meta), data_offset + len(body), data_offset, 0)
    return header + bytes(meta) + bytes(data_offset - head - len(meta)) + bytes(body)


GUID_T, GUID_GRASS, GUID_DIRT, GUID_PNG, GUID_TREE, GUID_OBJ = ('1' * 32, '2' * 32, '3' * 32, '4' * 32, '5' * 32, '6' * 32)


def terrain_bytes():
    res, a = 5, 4
    alpha = bytearray()
    for z in range(a):
        for x in range(a):
            alpha += bytes([255, 0, 0, 0] if x < 2 else [0, 255, 0, 0])
    tex = dict(m_Name='SplatAlpha 0', m_Width=a, m_Height=a, m_TextureFormat=4, **{'image data': bytes(alpha)},
               m_StreamData=dict(offset=0, size=0, path=''))
    layer = lambda f: dict(m_FileID=f, m_PathID=8574412962073106934)
    nul = dict(m_FileID=0, m_PathID=0)
    td = dict(m_Name='T',
              m_SplatDatabase=dict(m_TerrainLayers=[layer(1), layer(2)], m_AlphaTextures=[dict(m_FileID=0, m_PathID=2800002)], m_AlphamapResolution=a),
              m_DetailDatabase=dict(m_Patches=[dict(layerIndices=[0], numberOfObjects=[1, 2, 3, 4])],
                                    m_DetailPrototypes=[dict(prototype=nul, prototypeTexture=nul, minWidth=1.0, maxWidth=2.0, usePrototypeMesh=0)],
                                    m_PatchCount=1, m_PatchSamples=2,
                                    m_TreeInstances=[dict(position=dict(x=0.5, y=0.0, z=0.5), widthScale=1.0, heightScale=1.0, rotation=0.0, index=0)],
                                    m_TreePrototypes=[dict(prefab=dict(m_FileID=3, m_PathID=100100000))]),
              m_Heightmap=dict(m_Heights=[x * 1000 for z in range(res) for x in range(res)], m_Holes=[], m_Resolution=res,
                               m_Scale=dict(x=5.0, y=10.0, z=5.0)))
    return serialized_file([(2800002, 28, TEXTURE, tex), (15600000, 156, TERRAIN, td)], [GUID_GRASS, GUID_DIRT, GUID_TREE])


def meta(path, guid):
    Path(str(path) + '.meta').write_text('fileFormatVersion: 2\nguid: %s\n' % guid)


LAYER = """%YAML 1.1
--- !u!1953259897 &8574412962073106934
TerrainLayer:
  m_Name: {name}
  m_DiffuseTexture: {{fileID: 2800000, guid: {png}, type: 3}}
  m_NormalMapTexture: {{fileID: 0}}
  m_TileSize: {{x: 4, y: 4}}
  m_TileOffset: {{x: 0, y: 0}}
"""


def go(fid, name, pos, comps):
    lines = ['--- !u!1 &%d' % fid, 'GameObject:', '  m_Name: ' + name, '  m_Component:']
    lines += ['  - component: {fileID: %d}' % (fid + 1)] + ['  - component: {fileID: %d}' % (fid + 2 + i) for i in range(len(comps))]
    lines += ['--- !u!4 &%d' % (fid + 1), 'Transform:', '  m_GameObject: {fileID: %d}' % fid,
              '  m_LocalPosition: {x: %s, y: %s, z: %s}' % pos, '  m_LocalRotation: {x: 0, y: 0, z: 0, w: 1}',
              '  m_LocalScale: {x: 1, y: 1, z: 1}', '  m_Father: {fileID: 0}', '  m_Children: []']
    for i, (cls, typ, body) in enumerate(comps):
        lines += ['--- !u!%d &%d' % (cls, fid + 2 + i), typ + ':', '  m_GameObject: {fileID: %d}' % fid] + ['  ' + b for b in body]
    return '\n'.join(lines) + '\n'


def mb(cls, *fields):
    return (114, 'MonoBehaviour', ['m_Script: {fileID: 0}', 'm_EditorClassIdentifier: Game::Game.' + cls] + list(fields))


MAP = '''%%YAML 1.1
--- !u!114 &11400000
MonoBehaviour:
  m_GameObject: {fileID: 0}
  m_Script: {fileID: 0}
  m_Name: GD_Map-World
  m_EditorClassIdentifier: ME.BECS::ME.BECS.EntityConfig
  references:
    version: 2
    RefIds:
    - rid: 1
      type: {class: GDCMapData, ns: Game, asm: Game}
      data:
        mapName:
          utf8LengthInBytes: %d
          bytes:
            offset0000:
%s
        mapSize:
          x:
            rawValue: %d
          y:
            rawValue: %d
        mapPos:
          x:
            rawValue: %d
          y:
            rawValue: %d
'''


def project(root):
    shutil.copytree(ROOT / 'tests/data/port/unity/ProjectSettings', root / 'ProjectSettings')
    a = root / 'Assets'
    (a / 'Terrain').mkdir(parents=True)
    (a / 'Maps').mkdir()
    (a / 'Terrain/T.asset').write_bytes(terrain_bytes()); meta(a / 'Terrain/T.asset', GUID_T)
    write_png(str(a / 'Terrain/grass.png'), 2, 2, bytes([40, 160, 40, 255] * 4)); meta(a / 'Terrain/grass.png', GUID_PNG)
    for name, guid in (('Grass', GUID_GRASS), ('Dirt', GUID_DIRT)):
        p = a / ('Terrain/%s.terrainlayer' % name)
        p.write_text(LAYER.format(name=name, png=GUID_PNG)); meta(p, guid)
    (a / 'Terrain/tree.obj').write_text('v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n'); meta(a / 'Terrain/tree.obj', GUID_OBJ)
    (a / 'Terrain/Tree.prefab').write_text('%YAML 1.1\n' + go(10, 'Tree', (0, 0, 0), [(33, 'MeshFilter', ['m_Mesh: {fileID: 10, guid: %s, type: 3}' % GUID_OBJ])]))
    meta(a / 'Terrain/Tree.prefab', GUID_TREE)
    scene = '%YAML 1.1\n'
    scene += go(100, 'Terrain', (-10, 0, -10), [(218, 'Terrain', ['m_TerrainData: {fileID: 15600000, guid: %s, type: 2}' % GUID_T])])
    scene += go(200, 'Road', (0, 0, 0), [mb('RoadLike', 'roadWidth: 2', 'splinePoints:', '- {x: -5, y: 0, z: 0}', '- {x: 5, y: 0, z: 0}')])
    scene += go(300, 'ExtractionFlag', (-5, 0, -5), [mb('ExtractionPointSceneEntityExt', 'config: {sourceId: 1}')])
    scene += go(400, 'SpawnFlag', (-8, 0, 8), [mb('SceneEntityExt', 'config: {sourceId: 2}')])
    scene += go(500, 'SpawnFlag (1)', (8, 0, -8), [mb('SceneEntityExt', 'config: {sourceId: 2}')])
    scene += go(600, 'Wall', (5, 0, 5), [mb('GraphMaskSceneEntityExt', 'bounds:', '  m_Center: {x: 0, y: 0, z: 0}', '  m_Extent: {x: 1, y: 1, z: 2}',
                                            'type: 0', 'cost: 0')])
    scene += go(700, 'Cube', (0, 0.5, 6), [(33, 'MeshFilter', ['m_Mesh: {fileID: 10202, guid: 0000000000000000e000000000000000, type: 0}']),
                                           (23, 'MeshRenderer', ['m_Materials: []'])])
    (a / 'World.unity').write_text(scene)
    name = 'World'
    body = '\n'.join('              byte%04d: %d' % (i, c) for i, c in enumerate(name.encode()))
    raw = lambda f: struct.unpack('<I', struct.pack('<f', f))[0]
    (a / 'Maps/GD_Map-World.asset').write_text(MAP % (len(name), body, raw(20.0), raw(20.0), raw(-10.0), raw(-10.0)))
    return root


class WorldTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(); self.root = Path(self.tmp.name); self.out = self.root / 'out'
        self.src = project(self.root / 'src')

    def tearDown(self): self.tmp.cleanup()

    def test_common_strings(self):
        inv = {v: k for k, v in serialized.COMMON.items()}
        self.assertEqual((inv['Texture2D'], inv['m_CorrespondingSourceObject'], inv['PPtr<Prefab>'], inv['SInt64']), (874, 1093, 646, 814))

    def test_terrain_decode(self):
        from lib.unity.unity_scene import Assets, Report
        t = uterrain.decode(str(self.src / 'Assets/Terrain/T.asset'), Assets([str(self.src / 'Assets')], Report()))
        self.assertEqual((t.res, t.size, t.alpha_res), (5, (20.0, 10.0, 20.0), 4))
        self.assertAlmostEqual(t.height01(1, 0), 4000 / 32766)
        self.assertAlmostEqual(t.height01(0.5, 0.3), 2000 / 32766)
        self.assertEqual([l['name'] for l in t.layers], ['Grass', 'Dirt'])
        self.assertTrue(t.layers[0]['diffuse'].endswith('grass.png'))
        self.assertEqual(t.weights(0.1, 0.9), [255, 0])
        self.assertEqual(t.weights(0.9, 0.1), [0, 255])
        self.assertEqual(len(t.trees), 1); self.assertTrue(t.tree_prototypes[0]['prefab'].endswith('Tree.prefab'))
        self.assertEqual((t.detail_res, list(t.detail[0])), (2, [1, 2, 3, 4]))

    def test_truncated_file_rejected(self):
        with self.assertRaisesRegex(ValueError, 'size mismatch|truncated|not a Unity'):
            serialized.SerializedFile(terrain_bytes()[:-10])

    def test_editor_export_fallback(self):
        exp = self.src / 'AiverseExport/terrain'; exp.mkdir(parents=True)
        (exp / (GUID_T + '.r16')).write_bytes(struct.pack('<4h', 0, 32766, 0, 32766))
        (exp / (GUID_T + '.json')).write_text(json.dumps(dict(resolution=2, size=[20, 5, 20], heights=GUID_T + '.r16', alphamapResolution=0,
                                                               layers=[dict(name='G', diffuse='Assets/Terrain/grass.png', tile=[4, 4])])))
        model = unity.extract(self.src)
        info = model['terrains']['Assets/Terrain/T.asset']
        self.assertTrue(info['ok']); self.assertEqual(info['resolution'], 2)
        self.assertEqual(info['layers'][0]['diffuse'], 'Assets/Terrain/grass.png')
        emit(model, self.out)

    def test_mesh_decode(self):
        verts = struct.pack('<9f', 0, 0, 0, 1, 0, 0, 0, 0, 1)
        data = dict(m_SubMeshes=[dict(firstByte=0, indexCount=3, topology=0, baseVertex=0)], m_IndexFormat=0,
                    m_IndexBuffer=struct.pack('<3H', 0, 1, 2).hex(),
                    m_VertexData=dict(m_VertexCount=3, m_Channels=[dict(stream=0, offset=0, format=0, dimension=3)], _typelessdata=verts.hex()))
        text, lo, hi = umesh.to_obj(umesh.decode(data))
        self.assertIn('v 0.00000 0.00000 -1.00000', text)   # Z flipped
        self.assertIn('f 1 3 2', text)                       # winding reversed
        self.assertEqual(len(umesh.builtin(10202)['positions']), 24)

    def test_world_import(self):
        model = unity.extract(self.src)
        emit(model, self.out, scale=1)
        rules = read_json(self.out / 'rules.json')
        self.assertEqual(rules['terrain']['roads'], False)
        layers = rules['terrain']['layers']
        self.assertEqual(len(layers), 1)  # both layers share one texture -> one palette entry
        self.assertTrue((self.out / layers[0]['texture']).is_file()); self.assertEqual(layers[0]['scale'], 4)
        m = read_json(next((self.out / 'maps').glob('*.json')))
        self.assertEqual(m['size'], [20, 20])
        corners = (self.out / m['terrain']['cornersFile']).read_bytes()
        self.assertEqual(len(corners), 21 * 21 * 2)
        c = struct.unpack('<%dh' % (21 * 21), corners)
        self.assertEqual(c[20], round(4000 / 32766 * 10 * 16))  # corner (20, 0): Unity x = 10 -> u = 1
        self.assertEqual(c[0], 0)
        d = m['terrain']['detail']
        self.assertEqual(d['scale'], 1)
        self.assertEqual(len((self.out / d['splat']).read_bytes()), 21 * 21 * 4)
        rows = m['terrain']['rows']
        self.assertEqual(rows[10][10], '+')                  # road along Unity z = 0 -> map row 10
        self.assertEqual(rows[5][15], '#')                   # wall at Unity (5, 5) -> map (15, 5)
        self.assertIn('extraction_1', m['areas'])
        self.assertEqual(m['areas']['map_bounds'], [0, 0, 20, 20])
        self.assertEqual([p.get('start') for p in m['players']], [[2.0, 2.0], [18.0, 18.0]])
        im = m['imported']
        self.assertEqual(im['roads'][0]['points'], [[5.0, 10.0], [15.0, 10.0]])
        self.assertEqual(im['trees'], 1)
        self.assertTrue((self.out / im['details'][0]['file']).is_file())
        tree_type = next(v for k, v in rules['types'].items() if k.startswith('terrain_tree_'))
        self.assertTrue(tree_type['invulnerable']); self.assertEqual(tree_type['view']['model'], 'content/Assets/Terrain/tree.obj')
        self.assertTrue(any(e['type'].startswith('terrain_tree_') for e in m['entities']))
        cube = [v for v in rules['types'].values() if v['view'].get('model', '').startswith('content/_meshes/')]
        self.assertEqual(len(cube), 1); self.assertTrue((self.out / cube[0]['view']['model']).is_file())
        dropped = {e['id'] for e in model['entities'] if e['name'].startswith(('ExtractionFlag', 'SpawnFlag'))}
        self.assertEqual(len(dropped), 3)
        self.assertFalse({e['type'] for e in m['entities']} & dropped)
        before = {p: p.read_bytes() for p in self.out.rglob('*') if p.is_file()}
        emit(model, self.out, scale=1)
        self.assertEqual(before, {p: p.read_bytes() for p in self.out.rglob('*') if p.is_file()})

    def test_custom_marker_rules(self):
        rules = self.root / 'mapping.json'
        rules.write_text(json.dumps({'markers': [{'match': '^Cube$', 'kind': 'area', 'area': 'cube', 'radius': 1, 'entity': 'drop'}]}))
        emit(unity.extract(self.src), self.out, scale=1, mapping=rules)
        m = read_json(next((self.out / 'maps').glob('*.json')))
        self.assertIn('cube_1', m['areas'])
        rules.write_text(json.dumps({'markers': [{'kind': 'teleport'}]}))
        with self.assertRaises(ValueError):
            pworld.load_rules(rules)

    def test_include_limits_scenes(self):
        self.assertEqual(unity.extract(self.src, ['Assets/Nothing/*'])['scenes'], [])

    @unittest.skipUnless(os.environ.get('AIVERSE_PORT_HEADLESS'), 'AIVERSE_PORT_HEADLESS not set')
    def test_headless_load(self):
        emit(unity.extract(self.src), self.out, scale=2)
        p = subprocess.run([os.environ['AIVERSE_PORT_HEADLESS'], '--project', str(self.out), '--seed', '3', '--max-seconds', '0'],
                           capture_output=True, text=True)
        self.assertEqual(p.returncode, 0, p.stdout + p.stderr)


if __name__ == '__main__':
    unittest.main()
