"""Unity materials import on the synthetic fixture tests/data/unity_materials (original test data).

With AIVERSE_GRAPHCHECK (path of aiverse_graphcheck) the converted graphs also go through the engine compiler.
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve()
sys.path.insert(0, str(HERE.parents[1]))
from lib.port.emit import emit  # noqa: E402
from unity_project import importer  # noqa: E402
from unity_materials import fbxinfo, shadergraph, slots, texops, textures, urp  # noqa: E402
from unity_materials.cli import graphs as all_graphs, survey  # noqa: E402
import images  # noqa: E402  (unity_vfx, put on sys.path by texops)

ROOT = HERE.parents[3]
DATA = ROOT / 'tests/data/unity_materials'


def read_png(path):
    return images.read_png(Path(path).read_bytes())


class Fixture(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.out = Path(cls.tmp.name) / 'out'
        cls.model = importer.extract(DATA)
        cls.report = emit(cls.model, cls.out)
        cls.rules = json.loads((cls.out / 'rules.json').read_text())
        cls.lib = json.loads((cls.out / 'materials.json').read_text())['materials']
        cls.game = json.loads((cls.out / 'game.json').read_text())

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def type_named(self, prefix):
        return next(t for k, t in self.rules['types'].items() if k.startswith(prefix))

    def key_of(self, name):
        return next(k for k in self.lib if k.startswith(name + '_'))

    def test_fbx_slots(self):
        info = fbxinfo.read_file(str(DATA / 'Assets/Models/Rover.fbx'))
        mesh = {m['name']: m['materials'] for m in info['models'] if m['mesh']}
        self.assertEqual(mesh, {'Body': ['M_Paint', 'M_Glass'], 'Wheel': ['M_Rubber']})

    def test_model_instance_slots(self):
        # Rover.prefab: hashed renderer id with 2 slots -> the only 2-submesh node; M_Rubber via externalObjects
        view = self.type_named('rover_')['view']
        self.assertEqual(view['materials'], {'M_Paint': self.key_of('paint'), 'M_Glass': self.key_of('glass'), 'M_Rubber': self.key_of('rubber')})
        self.assertEqual(view['model'], 'content/Assets/Models/Rover.fbx')
        # the scene instance of the prefab carries the nested model instance too
        self.assertEqual(self.type_named('battle_')['view']['materials'], view['materials'])

    def test_explicit_renderer_node(self):
        # Turret.prefab: MeshFilter mesh 4300002 = Wheel (internalIDToNameTable) -> its only slot M_Rubber
        self.assertEqual(self.type_named('turret_')['view']['materials'], {'M_Rubber': self.key_of('glow')})

    def test_resolve_ambiguous_is_reported(self):
        info = dict(models=[dict(name='A', materials=['x', 'y'], mesh=True), dict(name='B', materials=['z', 'w'], mesh=True)], materials=['x', 'y', 'z', 'w'])
        mats, assign, items = slots.resolve(info, [dict(node=None, slots=['k'], where='r', names={'k': 'unrelated'})])
        self.assertEqual(mats, {})
        self.assertTrue(any(s == 'missing' and 'ambiguous' in why for s, why in items))
        mats, _, _ = slots.resolve(info, [dict(node=None, slots=['k'], where='r', names={'k': 'Z'})])
        self.assertEqual(mats, {'z': 'k'})

    def test_lit_packed_maps(self):
        m = self.lib[self.key_of('paint')]
        self.assertEqual(m['metallic'], 1)
        self.assertEqual(m['roughness'], 1)
        self.assertEqual(m['normalScale'], 0.5)
        self.assertEqual(m['occlusionStrength'], 0.7)
        self.assertEqual(m['uvScale'], [2.0, 2.0])
        self.assertEqual(m['uvOffset'], [0.25, -1.0])            # V flipped: 1 - 2 - 0
        self.assertFalse(m['vertexColors'])
        self.assertEqual(m['color'][:2], [1.0, 0.214])            # sRGB 0.5 -> linear
        self.assertEqual(m['emissiveStrength'], 4.9538)          # HDR (2, 1, 0) keeps the hue in emissive
        self.assertEqual(m['emissive'], [1.0, 0.2019, 0.0])
        # metallic R / smoothness A -> G = 255 - A * 0.8, B = R
        src = read_png(DATA / 'Assets/Textures/paint_metal.png')
        mr = read_png(self.out / m['metallicRoughnessMap'])
        for i in (0, 9, 63):
            self.assertEqual(mr.rgba[i * 4 + 1], round(255 - src.rgba[i * 4 + 3] * 0.8))
            self.assertEqual(mr.rgba[i * 4 + 2], src.rgba[i * 4])
        ao_src = read_png(DATA / 'Assets/Textures/paint_ao.png')
        ao = read_png(self.out / m['occlusionMap'])
        self.assertEqual(ao.rgba[5 * 4], ao_src.rgba[5 * 4 + 1])  # G -> R
        # detail albedo: generated graph, detail UV relative to the base tiling
        g = m['shaderGraph']
        self.assertEqual(shadergraph.validate(g), [])
        off = next(n for n in g['nodes'] if n['id'] == 'off')['value']
        self.assertEqual(off[:2], [-0.5, -1.0])

    def test_transparent_and_simplelit(self):
        glass = self.lib[self.key_of('glass')]
        self.assertEqual((glass['alpha'], glass['doubleSided'], glass['castShadow']), ('blend', True, False))
        self.assertEqual(glass['color'][3], 0.4)
        rubber = self.lib[self.key_of('rubber')]
        self.assertEqual((rubber['alpha'], rubber['cutoff'], rubber['metallic']), ('mask', 0.3, 0))
        self.assertIn('roughness_from_alpha', rubber['metallicRoughnessMap'])

    def test_shader_graph_material(self):
        m = self.lib[self.key_of('glow')]
        g = m['shaderGraph']
        self.assertEqual(shadergraph.validate(g), [])
        self.assertEqual(g['parameters'], {'_Speed': 2.0, '_Tint': [0.214041, 1.0, 0.214041, 1.0]})
        samples = [n for n in g['nodes'] if n['op'] == 'sampleTexture']
        self.assertEqual([n['texture'] for n in samples], ['content/Assets/Textures/paint_albedo.png'])
        self.assertTrue(samples[0]['srgb'])
        # unset _Mask -> Unity's white default constant; scene depth replaced; custom function reported
        self.assertTrue(m['unlit'] and m['alpha'] == 'blend' and m['doubleSided'])
        self.assertIn('emission', g['outputs'])
        items = [i for i in self.report['items'] if 'Glow.shadergraph' in i['reason']]
        self.assertTrue(any(i['status'] == 'missing' and 'Wobble' in i['reason'] for i in items))
        self.assertTrue(any('scene depth' in i['reason'] for i in items))

    def test_uv_flip_exact(self):
        tpl = shadergraph.convert_text((DATA / 'Assets/Shaders/Glow.shadergraph').read_text(), 'material')
        nodes = {n['id']: n for n in tpl['graph']['nodes']}
        uv_flip = [n for n in nodes.values() if n['op'] == 'tilingOffset' and nodes[n['inputs'][0]]['op'] == 'uv']
        self.assertEqual(len(uv_flip), 1)
        self.assertEqual(nodes[uv_flip[0]['inputs'][1]]['value'], [1.0, -1.0, 1.0, 1.0])

    def test_particle_domain(self):
        tpl = shadergraph.convert_text((DATA / 'Assets/Shaders/Glow.shadergraph').read_text(), 'particle')
        self.assertEqual(shadergraph.validate(tpl['graph']), [])
        self.assertEqual(set(tpl['graph']['outputs']), {'color'})

    def test_texture_plans_and_packaging(self):
        pack = self.game['packaging']['targets']
        a = {r['match']: r for r in pack['android']['files']}
        i = {r['match']: r for r in pack['ios']['files']}
        albedo = 'content/Assets/Textures/paint_albedo.png'
        self.assertEqual(a[albedo]['compress'], 'astc4x4')       # explicit Android override (format 48)
        self.assertNotIn('maxSize', a[albedo])                  # 8 px source: nothing to shrink
        self.assertEqual(i[albedo]['compress'], 'astc6x6')       # Drone Fall rule for colour textures
        normal = 'content/Assets/Textures/paint_normal.png'
        self.assertEqual((i[normal]['compress'], i[normal]['colorSpace'], i[normal].get('normalMap')), ('astc4x4', 'linear', True))
        self.assertNotIn('content/Assets/Textures/paint_metal.png', i)   # data maps stay uncompressed
        self.assertTrue(all(not k.startswith('content/_derived') for k in i))

    def test_plan_rules(self):
        meta = {'TextureImporter': {'textureType': 8, 'mipmaps': {'sRGBTexture': 1, 'enableMipMap': 0}, 'maxTextureSize': 64,
                                    'platformSettings': [{'buildTarget': 'iPhone', 'overridden': 1, 'maxTextureSize': 32, 'textureFormat': 50}]}}
        p = textures.plan('Assets/UI/icon.png', meta, ('base',), size=(128, 128))
        self.assertEqual(p['kind'], 'ui')
        self.assertEqual(p['targets']['ios'], dict(maxSize=32, compress='astc6x6', source='override'))
        self.assertEqual(p['targets']['android']['compress'], 'astc4x4')
        lut = textures.plan('Assets/FX/color_lut.png', {'TextureImporter': {'mipmaps': {'sRGBTexture': 0}}}, ('base',))
        self.assertIsNone(lut['targets']['ios']['compress'])

    def test_texops(self):
        a = images.Image(2, 1, bytes([10, 20, 30, 255, 200, 100, 0, 0]))
        out = texops.apply('metallic_gloss', [a], {'smoothness': 1})
        self.assertEqual(list(out.rgba), [255, 0, 10, 255, 255, 255, 200, 255])
        out = texops.apply('occlusion', [a], {})
        self.assertEqual(list(out.rgba[:4]), [20, 20, 20, 255])

    def test_survey_and_graphcheck(self):
        s = survey(DATA)
        self.assertEqual(s['families']['urp-lit'], 2)
        self.assertEqual(s['shaderGraphs']['converted'], 1)
        self.assertEqual(s['shaderGraphs']['nodeTypes']['SceneDepthNode']['status'], 'replaced')
        exe = os.environ.get('AIVERSE_GRAPHCHECK')
        if not exe:
            self.skipTest('AIVERSE_GRAPHCHECK not set')
        with tempfile.TemporaryDirectory() as tmp:
            f = Path(tmp) / 'g.json'
            data = all_graphs(DATA)
            data['graphs']['paint#detail'] = self.lib[self.key_of('paint')]['shaderGraph']
            data['graphs']['glow#material'] = self.lib[self.key_of('glow')]['shaderGraph']
            f.write_text(json.dumps(data))
            proc = subprocess.run([exe, str(f)], capture_output=True, text=True)
            self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
            results = [json.loads(line) for line in proc.stdout.splitlines()]
            self.assertEqual(len(results), 4)
            self.assertTrue(all(r['ok'] for r in results))

    def test_repeat_import_is_stable(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / 'o'
            emit(importer.extract(DATA), out)
            first = (out / 'materials.json').read_bytes()
            emit(importer.extract(DATA), out)
            self.assertEqual(first, (out / 'materials.json').read_bytes())
            self.assertEqual(first, (self.out / 'materials.json').read_bytes())


if __name__ == '__main__':
    unittest.main()
