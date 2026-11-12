"""Routing should select responsibilities/dependencies, not unrelated recipes."""
import copy
import importlib.util
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('skills',ROOT/'tools/skills/catalog.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
data=m.generate();entries=m.validate(data)
roots=[e['id'] for score,e in m.search(entries,'магазин чат','meta') if score>=4]
assert set(roots)=={'meta-economy','meta-social'}
server=m.plan(entries,roots,'server')
assert set(e['id'] for e in server['selection'])=={'meta-economy','meta-social','meta-server'}
client=m.plan(entries,roots,'client')
assert [e['id'] for e in client['selection']]==['meta-client']
assert client['serverModules']==[]
war=m.plan(entries,['meta-wars'],'server')
assert set(war['serverModules'])=={'economy','social','clans','wars'}
assert all('rating' not in s and 'matchmaking' not in s for s in war['read'])
assert m.search(entries,'unrecognized-xyz-123')==[]
for bad in ('missing','cycle','client'):
    changed=copy.deepcopy(data)
    target=next(e for e in changed['skills'] if e['id']=='meta-client')
    target['dependencies']={'missing':['does-not-exist'],'cycle':['meta-client'],'client':['meta-server']}[bad]
    try:m.validate(changed)
    except ValueError:pass
    else:raise AssertionError('invalid dependency accepted: '+bad)
print('Skill routing, ownership and dependency validation passed')
