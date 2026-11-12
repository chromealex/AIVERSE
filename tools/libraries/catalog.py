#!/usr/bin/env python3
"""Discover C++ capabilities and pin/verify the engine headers used by native projects."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['list', 'search', 'info', 'check', 'pin', 'verify'])
    parser.add_argument('query', nargs='?', default='')
    parser.add_argument('--project', type=Path)
    args = parser.parse_args()
    packages = json.loads((ROOT/'libraries/catalog.json').read_text())['packages']
    if args.command == 'check':
        ids = set()
        for p in packages:
            assert p['id'] not in ids, 'duplicate id'
            ids.add(p['id'])
            assert p['layer'] in ('simulation', 'presentation', 'tools')
            assert p['status'] in ('cpp-foundation', 'engine-reference', 'cpp-subsystem', 'tool-workflow')
            for field in ('source', 'skill'):
                assert (ROOT/p[field]).is_file(), p[field]
            assert f"name: {Path(p['skill']).parent.name}\n" in (ROOT/p['skill']).read_text()
        print(f'Validated {len(packages)} catalog entries')
        return
    selected = [p for p in packages if args.query.lower() in json.dumps(p).lower()]
    if args.command in ('info', 'pin', 'verify'):
        selected = [p for p in packages if p['id'] == args.query]
        if not selected:
            parser.error('unknown package')
    if args.command not in ('pin', 'verify'):
        print(json.dumps(selected, indent=2))
        return
    p = selected[0]
    if p['status'] not in ('cpp-foundation', 'cpp-subsystem'):
        parser.error('reference entries are not independently pinned; pin the engine revision')
    if not args.project or not (args.project/'game.json').is_file():
        parser.error('--project must point to a game project with game.json')
    # Include all library-local transitive headers and explicit metadata dependencies.
    import re
    files=set([p['source'],'libraries/CMakeLists.txt',*p.get('dependencies',[])])
    pending=list(files)
    while pending:
        file=pending.pop()
        if not file.endswith('.h'):continue
        for inc in re.findall(r'^#include "([^"]+)"', (ROOT/file).read_text(), re.M):
            resolved=(ROOT/file).parent/inc
            if not resolved.is_file():continue
            relative=resolved.resolve().relative_to(ROOT.resolve()).as_posix()
            if relative not in files:files.add(relative);pending.append(relative)
    files=sorted(files)
    pinned=dict(version=p['version'],layer=p['layer'],files={f:hashlib.sha256((ROOT/f).read_bytes().replace(b'\r\n', b'\n')).hexdigest() for f in files})
    lock_path = args.project/'libraries.lock.json'
    lock = json.loads(lock_path.read_text()) if lock_path.exists() else {'schema': 2, 'packages': {}}
    if lock.get('schema')!=2:
        parser.error('old lock schema; explicit migration required')
    previous=lock['packages'].get(p['id'])
    if args.command=='verify':
        if previous!=pinned:
            parser.error('missing or changed pin; review engine/library version before updating')
        print(f"Verified {p['id']} {p['version']}")
        return
    if previous is not None and previous!=pinned:
        parser.error('lock differs; explicit migration required')
    lock['packages'][p['id']]=pinned
    temp_path=lock_path.with_suffix('.json.tmp')
    temp_path.write_text(json.dumps(lock,indent=2,sort_keys=True)+'\n')
    temp_path.replace(lock_path)
    print(f"Pinned {p['id']} {p['version']}; link AIVERSE::libraries")

if __name__ == '__main__':
    main()
