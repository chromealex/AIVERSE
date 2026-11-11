#!/usr/bin/env python3
import importlib.util,json,os,shutil,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('gameplay',ROOT/'tools/libraries/gameplay.py');gp=importlib.util.module_from_spec(spec);spec.loader.exec_module(gp)
class Tools(unittest.TestCase):
    def test_settings(self):
        self.assertEqual(gp.settings({'schemaVersion':1})['inventory']['slots'],20)
        for obj in ({'schemaVersion':True},{'schemaVersion':1,'input':{'deadzone':True}},{'schemaVersion':1,'input':{'typo':3}},{'schemaVersion':1,'camera':{'minZoom':10,'maxZoom':2}}):
            with self.assertRaises(ValueError):gp.settings(obj)
    def test_audit_and_backups(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);project=root/'game';(project/'textures').mkdir(parents=True);(project/'lang').mkdir()
            (project/'textures/bad.png').write_bytes(b'bad');(project/'lang/en.json').write_text('{"a":"A"}');(project/'lang/ru.json').write_text('{}')
            cfg=gp.settings({'schemaVersion':1,'content':{'requireMatchingLocales':True}});result=gp.audit(project,cfg);self.assertFalse(result['ok']);self.assertEqual(len(result['errors']),2)
            state=root/'state.json';save=root/'save.json';restored=root/'restored.json';state.write_text('{"v":3}')
            gp.snapshot(state,save,cfg);state.write_text('{"v":4}');gp.snapshot(state,save,cfg)
            self.assertTrue(save.with_name('save.json.bak1').exists());gp.restore(save,restored,cfg);self.assertEqual(json.loads(restored.read_text()),{'v':4})
            broken=json.loads(save.read_text());broken['state']['v']=5;save.write_text(json.dumps(broken))
            with self.assertRaises(ValueError):gp.restore(save,restored,cfg)
            self.assertEqual(json.loads(restored.read_text()),{'v':4})
    def test_recipe(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);(root/'rules.json').write_text('// comment\n{"types":{"worker":{"builds":[]}}}')
            payload={'id':'tower','definition':{'structure':{'footprint':[1,1]}},'builders':['worker']}
            plan=gp.recipe(root,'building',payload);self.assertEqual(plan['rules']['types']['worker']['builds'],['tower'])
            self.assertNotIn('tower',gp.read_json(root/'rules.json')['types'])
            (root/'rules.json').write_text('{}')
            with self.assertRaises(ValueError):gp.apply_plan(root,plan,Path('/missing'))
    def test_engine_apply(self):
        configured=os.environ.get('AIVERSE_HEADLESS')
        candidates=[ROOT/path for path in ('build/.temp/library-aiverse/aiverse_headless','build/.temp/library-native/aiverse_headless','build/linux-release/aiverse_headless')]
        binary=Path(configured).resolve() if configured else next((p for p in candidates if p.is_file()),None)
        if configured:self.assertTrue(binary.is_file(),'AIVERSE_HEADLESS must point to a built executable')
        if binary is None:self.skipTest('build aiverse_headless or set AIVERSE_HEADLESS')
        with tempfile.TemporaryDirectory() as temp:
            project=Path(temp)/'game';shutil.copytree(ROOT/'templates/skirmish',project)
            for name,payload in [
                ('resource',{'id':'copper','definition':{'kind':'stock','start':0}}),
                ('building',{'id':'store','definition':{'health':{'max':100},'structure':{'footprint':[2,2]},'cost':{'gold':10}},'builders':['peasant']}),
                ('ability',{'id':'mend','owner':'footman','definition':{'target':'none','cooldown':10,'effects':[{'type':'heal','amount':5}]}}),
                ('faction',{'id':'test','definition':{'name':'Test','start':[{'type':'townhall','count':1,'offset':[0,0]}]}})]:
                self.assertTrue(gp.apply_plan(project,gp.recipe(project,name,payload),binary)['applied'])
            before=(project/'rules.json').read_bytes();bad=gp.recipe(project,'faction',{'id':'bad','definition':{'start':[{'type':'missing_type'}]}})
            with self.assertRaises(ValueError):gp.apply_plan(project,bad,binary)
            self.assertEqual((project/'rules.json').read_bytes(),before)
if __name__=='__main__':unittest.main()
