"""Validate every selectable kit through C++, selective descriptions and non-destructive writes."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/meta'))
import kits
validator=Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory() as directory:
    root=Path(directory)
    for feature in kits.DEPENDENCIES:
        project=root/feature
        recipe=kits.apply(project,[feature],validator)
        data=json.loads((project/'meta/service.generated.json').read_text())
        assert feature in recipe['resolved']
        if 'social' not in recipe['resolved']:assert data['boards']=={}
        before=(project/'meta/economy.json').read_bytes()
        try:kits.apply(project,[feature],validator)
        except ValueError:pass
        else:raise AssertionError('overwrote existing project')
        assert before==(project/'meta/economy.json').read_bytes()
        subprocess.run([sys.executable,str(ROOT/'tools/meta/catalog.py'),'describe',feature],check=True,stdout=subprocess.DEVNULL)
    kits.apply(root/'full',kits.PRESETS['full'],validator)
    assert (root/'full/ui/meta/app.js').is_file()
    try:kits.apply(root/'invalid',['typo'],validator)
    except ValueError:pass
    else:raise AssertionError('unknown feature accepted')
    assert not (root/'invalid').exists()
    try:kits.apply(root/'failed',['social'],root/'missing-validator')
    except OSError:pass
    else:raise AssertionError('missing validator accepted')
    assert not (root/'failed').exists()
print('All selected meta packages validated; guards preserve existing files')

with tempfile.TemporaryDirectory() as directory:
    root=Path(directory)
    kits.apply(root/'client',['economy','social','database','ui'],validator,'client')
    assert (root/'client/meta/client/CMakeLists.txt').exists()
    assert not list((root/'client').rglob('*.sql'))
    assert not (root/'client/meta/service.generated.json').exists()
    assert not (root/'client/meta/economy.json').exists()
    assert not (root/'client/meta/server').exists()
    kits.apply(root/'server',['economy','database','ui'],validator,'server')
    assert (root/'server/meta/server/db/001_initial.sql').exists()
    assert not (root/'server/meta/client').exists()
    assert not (root/'server/ui').exists()
print('Client/server generation does not leak private JSON or SQL to client-only output')
