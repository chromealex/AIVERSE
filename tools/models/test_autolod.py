import importlib.util
import json
import os
from pathlib import Path
import struct
import tempfile
import unittest

spec=importlib.util.spec_from_file_location('autolod',Path(__file__).with_name('autolod.py'))
autolod=importlib.util.module_from_spec(spec);spec.loader.exec_module(autolod)
WORKER=Path(os.environ.get('AUTOLOD_WORKER','/nonexistent'))

@unittest.skipUnless(WORKER.is_file(),'set AUTOLOD_WORKER to built aiverse_lod_simplify')
class AutoLodTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name)
        self.source=self.root/'source.gltf';self.output=self.root/'derived'/'lod.gltf'
        n=20
        pos=[(x/n,0,y/n) for y in range(n+1) for x in range(n+1)]
        indices=[]
        for y in range(n):
            for x in range(n):
                a=y*(n+1)+x;indices.extend((a,a+1,a+n+1,a+1,a+n+2,a+n+1))
        positions=struct.pack('<'+str(len(pos)*3)+'f',*(v for p in pos for v in p))
        raw=positions+struct.pack('<'+str(len(indices))+'I',*indices)
        (self.root/'source.bin').write_bytes(raw)
        self.doc={'asset':{'version':'2.0'},'buffers':[{'uri':'source.bin','byteLength':len(raw)}],
          'bufferViews':[{'buffer':0,'byteOffset':0,'byteLength':len(positions)}, {'buffer':0,'byteOffset':len(positions),'byteLength':len(raw)-len(positions)}],
          'accessors':[{'bufferView':0,'componentType':5126,'count':len(pos),'type':'VEC3','min':[0,0,0],'max':[1,0,1]}, {'bufferView':1,'componentType':5125,'count':len(indices),'type':'SCALAR'}],
          'materials':[{'name':'original material','pbrMetallicRoughness':{'baseColorFactor':[1,0,0,1]}}],
          'meshes':[{'primitives':[{'attributes':{'POSITION':0},'indices':1,'material':0}]}],
          'nodes':[{'mesh':0}],'scenes':[{'nodes':[0]}],'scene':0}
        self.save()
    def save(self):self.source.write_text(json.dumps(self.doc))
    def tearDown(self):self.tmp.cleanup()
    def run_tool(self):return autolod.generate(self.source,self.output,WORKER)
    def test_reduction_preserves_source_and_bounds(self):
        before=self.source.read_bytes();raw=(self.root/'source.bin').read_bytes()
        report=self.run_tool();out=autolod.Asset(self.output)
        self.assertEqual(self.source.read_bytes(),before);self.assertEqual((self.root/'source.bin').read_bytes(),raw)
        self.assertEqual(out.doc['meshes'][0]['primitives'],self.doc['meshes'][0]['primitives'])
        self.assertEqual(out.doc['materials'],self.doc['materials'])
        levels=report['meshes'][0]['levels'];self.assertGreaterEqual(len(levels),2)
        count=800;pixels=float('inf')
        for level in levels:
            self.assertLessEqual(level['triangles'],count*.8);count=level['triangles']
            self.assertLess(level['maxPixels'],pixels);pixels=level['maxPixels']
        for mesh in out.doc['meshes'][1:]:
            p=mesh['primitives'][0];pos=out.values(p['attributes']['POSITION']);indices=out.values(p['indices'])
            self.assertTrue(all(0<=i[0]<len(pos) for i in indices))
            self.assertEqual(set(v for v in pos if v[0] in (0,1) or v[2] in (0,1)),set(v for v in autolod.Asset(self.source).values(0) if v[0] in (0,1) or v[2] in (0,1)))
        first=self.output.read_bytes();first_bin=self.output.with_suffix('.lods.bin').read_bytes()
        self.run_tool();self.assertEqual(self.output.read_bytes(),first);self.assertEqual(self.output.with_suffix('.lods.bin').read_bytes(),first_bin)
    def test_uv_normals_and_normalized_colors_preserved(self):
        positions=autolod.Asset(self.source).values(0)
        raw=bytearray((self.root/'source.bin').read_bytes())
        for key,component,kind,values in (
            ('NORMAL',5126,'VEC3',[(0,1,0)]*len(positions)),
            ('TEXCOORD_0',5126,'VEC2',[(p[0],p[2]) for p in positions]),
            ('COLOR_0',5121,'VEC4',[(255,128,0,255)]*len(positions))):
            packed=struct.pack('<'+str(sum(map(len,values)))+('B' if component==5121 else 'f'),*(v for row in values for v in row))
            vi=len(self.doc['bufferViews']);self.doc['bufferViews'].append({'buffer':0,'byteOffset':len(raw),'byteLength':len(packed)})
            ai=len(self.doc['accessors']);a={'bufferView':vi,'componentType':component,'count':len(positions),'type':kind}
            if component==5121:a['normalized']=True
            self.doc['accessors'].append(a);self.doc['meshes'][0]['primitives'][0]['attributes'][key]=ai
            raw.extend(packed)
        self.doc['buffers'][0]['byteLength']=len(raw);(self.root/'source.bin').write_bytes(raw);self.save()
        self.run_tool();out=autolod.Asset(self.output)
        self.assertGreater(len(out.doc['meshes']),1)
        for mesh in out.doc['meshes'][1:]:
            attrs=mesh['primitives'][0]['attributes'];pos=out.values(attrs['POSITION'])
            self.assertEqual(out.values(attrs['NORMAL']),[(0,1,0)]*len(pos))
            self.assertEqual(out.values(attrs['TEXCOORD_0']),[(p[0],p[2]) for p in pos])
            self.assertEqual(out.values(attrs['COLOR_0']),[(1,128/255,0,1)]*len(pos))

    def test_small_mesh_bypass(self):
        result=autolod.generate(self.source,self.output,WORKER,1000)
        self.assertIn('skipped',result['meshes'][0]);self.assertEqual(len(json.loads(self.output.read_text())['meshes']),1)
    def test_skin_and_morph_bypass(self):
        self.doc['nodes'][0]['skin']=0;self.save()
        self.assertIn('skipped',self.run_tool()['meshes'][0])
        del self.doc['nodes'][0]['skin'];self.doc['meshes'][0]['primitives'][0]['targets']=[{'POSITION':0}];self.save()
        self.assertIn('skipped',self.run_tool()['meshes'][0])
    def test_bad_accessor_rejected(self):
        self.doc['accessors'][0]['count']=99999;self.save()
        with self.assertRaises(ValueError):self.run_tool()
        self.assertFalse(self.output.exists())
    def test_no_source_overwrite(self):
        with self.assertRaises(ValueError):autolod.generate(self.source,self.source,WORKER)
    def test_repeat_lod_rejected(self):
        self.run_tool()
        with self.assertRaises(ValueError):autolod.generate(self.output,self.root/'twice.gltf',WORKER)

if __name__=='__main__':unittest.main()
