"""Synthetic projects only; run with RTS_PORT_HEADLESS for the real engine loader check."""
import copy
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from lib.port.common import read_json, safe
from lib.port.emit import emit
from unity_project import importer as unity
from godot_project import importer as godot
from unreal_export import importer as unreal
ROOT=Path(__file__).resolve().parents[3]
DATA=ROOT/'tests/data/port'
CLI=Path(__file__).with_name('rts_port.py')

class PortTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(); self.root=Path(self.tmp.name); self.out=self.root/'out'
    def tearDown(self): self.tmp.cleanup()
    def test_force_text_and_override(self):
        self.assertTrue(unity.scan(DATA/'unity')['ok'])
        model=unity.extract(DATA/'unity')
        e=next(e for e in model['entities'] if '.unity#' in e['source'])
        self.assertEqual(e['fields']['health'],250)
        self.assertEqual(e['position'],[-3,0,-5])
        self.assertEqual(len(model['scripts'][0]['uses']),3)
    def test_binary_rejected(self):
        src=self.root/'source'; shutil.copytree(DATA/'unity',src)
        (src/'Assets/Unit.prefab').write_bytes(b'\0binary')
        self.assertFalse(unity.scan(src)['ok'])
        with self.assertRaisesRegex(ValueError,'Force Text'): unity.extract(src)
    def test_emit_units_and_edit_preservation(self):
        model=unity.extract(DATA/'unity'); emit(model,self.out,scale=2)
        rules=read_json(self.out/'rules.json'); key=next(iter(rules['types']))
        self.assertEqual(rules['types'][key]['move']['speed'],6)
        self.assertEqual(rules['types'][key]['weapons'][0]['cooldown'],0.5)
        rules['types'][key]['health']['max']=999
        (self.out/'rules.json').write_text(json.dumps(rules))
        mapfile=next((self.out/'maps').glob('*.json')); mapfile.write_text('{"edited":true}')
        emit(model,self.out)
        current=read_json(self.out/'rules.json')['types'][key]
        self.assertEqual(current['health']['max'],999); self.assertNotIn('ported',current)
        self.assertEqual(read_json(mapfile),{'edited':True})
    def test_idempotent(self):
        model=unity.extract(DATA/'unity'); emit(model,self.out)
        before={p.relative_to(self.out):p.read_bytes() for p in self.out.rglob('*') if p.is_file()}
        emit(model,self.out)
        after={p.relative_to(self.out):p.read_bytes() for p in self.out.rglob('*') if p.is_file()}
        self.assertEqual(before,after)
        self.assertIn('port.json', (self.out/'.rpakignore').read_text())
    def test_paths(self):
        with self.assertRaises(ValueError): safe(self.root,'../escape')
        with self.assertRaises(ValueError): safe(self.root,'/tmp/escape')
        with self.assertRaises(ValueError): emit(unity.extract(DATA/'unity'),DATA/'unity'/'output')
    def test_godot(self):
        model=godot.extract(DATA/'godot'); emit(model,self.out)
        e=next(e for e in model['entities'] if e['name']=='Unit')
        self.assertEqual(e['position'],[3,0,5]); self.assertEqual(e['fields']['health'],120)
    def test_godot_2d(self):
        emit(godot.extract(DATA/'godot2d'),self.out)
        self.assertEqual(read_json(self.out/'game.json')['camera']['projection'],'ortho')
        types=read_json(self.out/'rules.json')['types']
        self.assertTrue(any(t.get('sprite',{}).get('frame')==[2,2] for t in types.values()))
    def test_unreal(self):
        model=unreal.extract(DATA/'unreal'); emit(model,self.out)
        self.assertEqual(read_json(self.out/'rules.json')['types']['unit']['health']['max'],100)
    def test_cli_and_resolution(self):
        def cli(*args):
            p=subprocess.run([sys.executable,str(CLI),*map(str,args)],capture_output=True,text=True)
            self.assertEqual(p.returncode,0,p.stdout+p.stderr); return json.loads(p.stdout)
        cli(DATA/'unity','--out',self.out)
        report=cli('report',self.out); key=report['items'][0]['id']
        cli('resolve',self.out,'--id',key,'--how','dropped','--reason','Synthetic test')
        self.assertEqual(cli('item',self.out,'--id',key)['item']['status'],'resolved')
        cli(DATA/'unity','--out',self.out)
        self.assertEqual(cli('item',self.out,'--id',key)['item']['status'],'resolved')
    def test_vfx_staging_preserves_edits(self):
        src=self.root/'fx-source'; shutil.copytree(DATA/'unity',src)
        shutil.copytree(ROOT/'tests/data/unity_vfx/Assets/FX',src/'Assets/FX')
        model=unity.extract(src); emit(model,self.out)
        effects=list((self.out/'fx').glob('*.json'))
        self.assertTrue(effects)
        effects[0].write_text('{"edited":true}')
        emit(model,self.out)
        self.assertEqual(read_json(effects[0]),{'edited':True})
    def test_tiles_and_sprites(self):
        model=unity.extract(DATA/'unity2d'); emit(model,self.out)
        scene=read_json(next((self.out/'maps').glob('*.json')))
        self.assertEqual(len(scene['tiles']['fixed']),1)
        self.assertEqual(read_json(self.out/'game.json')['camera']['projection'],'ortho')
        types=read_json(self.out/'rules.json')['types']
        self.assertTrue(any(t.get('sprite',{}).get('frame')==[2,2] for t in types.values()))
    def test_changed_source_reopens_resolution(self):
        from lib.port.emit import save_report
        model=unity.extract(DATA/'unity'); report=emit(model,self.out)
        entry=next(i for i in report['items'] if i['source'].endswith('Unit.cs'))
        entry.update(status='resolved',resolution={'how':'lua','reason':'done'})
        save_report(self.out,report)
        model['scripts'][0]['source']+='// changed behavior'
        report=emit(model,self.out)
        self.assertEqual(next(i for i in report['items'] if i['id']==entry['id'])['status'],'missing')
    def test_invalid_model_writes_nothing(self):
        model=unity.extract(DATA/'unity'); model['scenes'][0]['objects'][0]['entity']='unknown'
        with self.assertRaisesRegex(ValueError,'unknown scene entity'): emit(model,self.out)
        self.assertFalse(self.out.exists())
    def test_emit_generated_intermediate_is_stable(self):
        model=unity.extract(DATA/'unity'); first=emit(model,self.out)
        second=emit(read_json(self.out/'port.json'),self.out)
        self.assertEqual(first,second)
    @unittest.skipUnless(os.environ.get('RTS_PORT_HEADLESS'),'RTS_PORT_HEADLESS not set')
    def test_headless_load(self):
        for name,module in [('unity',unity),('unity2d',unity),('godot',godot),('godot2d',godot),('unreal',unreal)]:
            out=self.root/name; emit(module.extract(DATA/name),out)
            p=subprocess.run([os.environ['RTS_PORT_HEADLESS'],'--project',str(out),'--seed','3','--max-seconds','2'],capture_output=True,text=True)
            self.assertEqual(p.returncode,0,p.stdout+p.stderr)
            if name=='unity': self.assertIn('winner=Player 1',p.stdout)

if __name__=='__main__': unittest.main()
