#!/usr/bin/env python3
"""Create a runnable animation-graph project from the engine's original showcase assets."""
import json
from pathlib import Path
import shutil
import sys

root = Path(__file__).resolve().parents[2]
out = Path(sys.argv[1]).resolve()
if out.exists():
    raise SystemExit('Destination must not exist')
shutil.copytree(root / 'templates/showcase', out)
shutil.copytree(root / 'engine/assets', out / 'engine')
# Preserve JSON-with-comments; insert an override into the golem view object.
p = out / 'rules.json'
s = p.read_text()
needle = '"model": "models/golem.glb",'
if needle not in s:
    raise SystemExit('Showcase golem view changed; inspect the source template')
graph = {
    'parameters': {'speed': 0, 'moving': 0, 'dead': 0, 'blend': 0.5, 'breath': 0.3},
    'nodes': [
        {'id': 'idle', 'kind': 'clip', 'clip': 'Idle'},
        {'id': 'walk', 'kind': 'clip', 'clip': 'Walk'},
        {'id': 'run', 'kind': 'clip', 'clip': 'Run'},
        {'id': 'locomotion', 'kind': 'blend', 'a': 'walk', 'b': 'run', 'parameter': 'blend'},
        {'id': 'breath', 'kind': 'clip', 'clip': 'Breathe'},
        {'id': 'alive', 'kind': 'additive', 'a': 'locomotion', 'b': 'breath', 'mask': 'spine', 'parameter': 'breath'},
        {'id': 'death', 'kind': 'clip', 'clip': 'Death', 'loop': False},
    ],
    'initial': 'walk',
    'states': [
        {'id': 'walk', 'output': 'alive', 'transitions': [
            {'to': 'dead', 'fade': 0.1, 'when': [{'parameter': 'dead', 'op': 'eq', 'value': 1}]},
            {'to': 'idle', 'fade': 0.2, 'after': 2, 'when': []}]},
        {'id': 'idle', 'output': 'idle', 'transitions': [
            {'to': 'dead', 'fade': 0.1, 'when': [{'parameter': 'dead', 'op': 'eq', 'value': 1}]},
            {'to': 'walk', 'fade': 0.2, 'after': 2, 'when': []}]},
        {'id': 'dead', 'output': 'death'}
    ]
}
s = s.replace(needle, needle + '\n"animationGraph": ' + json.dumps(graph) + ',', 1)
p.write_text(s)
print(out)
