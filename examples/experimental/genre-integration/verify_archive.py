#!/usr/bin/env python3
"""Verify the inert recovery archive; never apply historical patches."""
import ast
import hashlib
import json
from pathlib import Path
root=Path(__file__).resolve().parent
manifest=json.loads((root/'source-manifest.json').read_text())
for item in manifest['files']:
    path=root/item['path']
    assert path.is_relative_to(root) if hasattr(path,'is_relative_to') else '..' not in Path(item['path']).parts
    data=path.read_bytes().replace(b'\r\n',b'\n')
    assert hashlib.sha256(data).hexdigest()==item['sha256'],item['path']
    if path.suffix=='.py':ast.parse(data.decode())
index=json.loads((root/'source/examples/genres/index.json').read_text())
assert len(index)==16 and len({entry['genre'] for entry in index})==16
translations=[json.loads((root/('genres.'+lang+'.json')).read_text()) for lang in ['en','ru']]
assert translations[0].keys()==translations[1].keys() and translations[0]
assert all(key.startswith('genres.') for key in translations[0])
release=(root/'release-addition.review.diff').read_text()
assert 'aiverse_genre_client' in release
for line in release.splitlines():
    if line.startswith('-') and not line.startswith('---'):
        if 'aiverse_modeling' in line:assert 'aiverse_modeling' in next(l for l in release.splitlines() if l.startswith('+'+line[1:].split(':',1)[0]+':'))
print('Archive hashes, 16 launch routes, Python syntax and translation parity passed')
