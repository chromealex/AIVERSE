#!/usr/bin/env python3
"""Create data presets; never modify engine code or overwrite project files."""
import argparse
import json
import shutil
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--preset', choices=['minimal', 'mobile', 'competitive', 'full'], required=True)
parser.add_argument('--out', type=Path, required=True, help='project directory')
parser.add_argument('--with-ui', action='store_true', help='copy reusable browser meta screens')
args = parser.parse_args()
source = Path(__file__).resolve().parents[2] / 'examples' / 'meta'
meta = json.loads((source / 'meta.json').read_text())
boards = json.loads((source / 'boards.json').read_text())
chats = json.loads((source / 'chats.json').read_text())
if args.preset == 'minimal':
    meta = {'version': 1, 'assets': {'gold': {'cap': 1000000}},
            'tracks': {'first_win': meta['tracks']['first_win']}}
    boards, chats = {}, {}
elif args.preset == 'competitive':
    meta.pop('energy')
    meta['assets'].pop('energy')
    meta['offers'] = {k: v for k, v in meta['offers'].items() if k in ('pass',)}
extensions = json.loads((source / 'service.json').read_text())['extensions'] if args.preset == 'full' else {}
if args.preset == 'full':
    extensions['liveops'] = json.loads((source / 'liveops.json').read_text())
folder = args.out / 'meta'
files = {'economy.json': meta, 'boards.json': boards, 'chats.json': chats, 'extensions.json': extensions}
if any((folder / name).exists() for name in files):
    parser.error('destination already has meta definitions; edit them instead')
ui = args.out / 'ui' / 'meta'
if args.with_ui and ui.exists():
    parser.error('destination UI already exists')
folder.mkdir(parents=True, exist_ok=True)
for name, data in files.items():
    with (folder / name).open('x') as stream:
        json.dump(data, stream, ensure_ascii=False, indent=2)
        stream.write('\n')
print(str(folder))

if args.with_ui:
    shutil.copytree(source / 'ui', ui)
