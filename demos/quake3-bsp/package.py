"""Assemble the AIVERSE WebGL2 catalog viewer and original OpenArena sample into a static site."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
from zipfile import ZipFile

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
SHA256 = 'b71f5a0c301a56d759f0cd7bfe072067b2f3a30cbb3d9159a20c36994cc52261'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('pk3', type=Path)
    parser.add_argument('viewer_build', type=Path)
    parser.add_argument('site', type=Path)
    args = parser.parse_args()
    if hashlib.sha256(args.pk3.read_bytes()).hexdigest() != SHA256:
        parser.error('expected z_oacmp-volume1-v3.pk3 from Debian openarena-oacmp1 3-17')
    view = json.loads((HERE / 'view.json').read_text())
    game = args.site / 'egyptian-trouble'
    game.mkdir(parents=True, exist_ok=True)
    with ZipFile(args.pk3) as archive:
        scripts = sorted(Path(n).stem for n in archive.namelist() if n.startswith('scripts/') and n.endswith('.shader'))
        for rel in view['items'][0]['files'] + ['COPYING', 'OACMP-Readme.txt']:
            output = game / rel
            output.parent.mkdir(parents=True, exist_ok=True)
            if rel == 'scripts/shaderlist.txt':
                output.write_text('\n'.join(scripts)+'\n')
            else:
                output.write_bytes(archive.read(rel))
    shutil.copy2(HERE / 'view.json', game)
    for name in ('index.html', 'index.js', 'index.wasm'):
        target = 'viewer.html' if name == 'index.html' else name
        shutil.copy2(args.viewer_build / name, args.site / target)
    shutil.copy2(HERE / 'README.md', args.site / 'README.md')
    shutil.copy2(REPO / 'LICENSE', args.site / 'ENGINE-LICENSE')
    shutil.copy2(args.viewer_build / 'notices/THIRD_PARTY_NOTICES.txt', args.site)
    (args.site / 'index.html').write_text('<!doctype html><meta charset="utf-8"><title>Egyptian Trouble · AIVERSE</title>'
        '<meta http-equiv="refresh" content="0; url=viewer.html?src=./egyptian-trouble">'
        '<a href="viewer.html?src=./egyptian-trouble">Open Egyptian Trouble</a>\n')
    print(args.site.resolve())


if __name__ == '__main__':
    main()
