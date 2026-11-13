#!/usr/bin/env python3
"""Exercise the real C++ host CLI, including failure exit codes."""
import argparse
import json
import subprocess
import tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--binary',required=True,type=Path);args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='rts-app-cli-') as tmp:
        path=Path(tmp)/'input.json'
        def run(command,data,success=True):
            path.write_text(json.dumps(data))
            p=subprocess.run([str(args.binary.resolve()),command,str(path)],capture_output=True,text=True)
            assert (p.returncode==0)==success,(command,p.stdout,p.stderr)
            return p.stdout
        fixtures={name:json.loads((ROOT/'examples/libraries/app'/ (name+'.json')).read_text()) for name in ['balance','performance','plan','content','scenario','genre','screens']}
        for name,data in fixtures.items():run(name,data)
        result=json.loads(run('balance',fixtures['balance']));assert result['factions']['blue']['wins']==1
        result=json.loads(run('plan',fixtures['plan']));assert result['recipes']==['data','hud']
        result=json.loads(run('content',fixtures['content']));assert result['order']==['base','mod']
        run('performance',dict(samples=[1000]*30,maximumP95=999),False)
        run('performance',dict(samples=[100]*30,baseline=[50]*30),False)
        run('performance',dict(samples=[1]),False)
        run('performance',dict(samples=[1.5]*30),False)
        run('performance',dict(samples=[True]*30),False)
        run('performance',dict(samples=[1]*30,unknown=1),False)
        run('plan',dict(recipes=[],requested=['missing']),False)
        cycle=fixtures['content'];cycle['packages'][1]['dependencies']=[dict(id='mod',minimum=1,maximum=1)];run('content',cycle,False)
        scenario=fixtures['scenario'];scenario['checks'][0]['minimum']=2;run('scenario',scenario,False)
        screen=fixtures['screens'];screen['translations']['farm.title']='<script>';output=run('screens',screen);assert '&lt;script&gt;' in output and '<script>' not in output
        screen['rows'].append(screen['rows'][0]);run('screens',screen,False)
        run('genre',{'genre':'unknown'},False)
        assert json.loads(run('curve',{'points':[{'x':0,'y':10},{'x':10,'y':20}],'samples':[0,5,10,15]}))['values']==[10,15,20,20]
        run('curve',{'points':[{'x':0,'y':1},{'x':0,'y':2}],'samples':[1]},False)
        catalog=json.loads((ROOT/'libraries/catalog.json').read_text())
        ids={row['id'] for row in catalog['packages']}
        recipes=sorted((ROOT/'examples/genres/recipes').glob('*.json'))
        assert len(recipes)==16
        for recipe in recipes:
            result=json.loads(run('genre',json.loads(recipe.read_text())))
            assert result['coverage']=='recipe' and result['integration']
            assert set(result['packages'])<=ids, (recipe.name,result['packages'])
        assert json.loads(run('genre',{'genre':'tower-defense'}))['family']=='rts'
        assert json.loads(run('genre',{'genre':'rts'}))['id']=='classic-rts'
        assert json.loads(run('genre',{'genre':'rpg'}))['id']=='action-rpg'
        run('genre',{'genre':'unknown'},False)
        run('unknown',{},False)
    print('C++ application CLI checks passed')
if __name__=='__main__':main()
