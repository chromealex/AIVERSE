"""Unpack the redistributable OACMP1 optical regression fixture from its Debian PK3."""
import argparse
import hashlib
from pathlib import Path
from zipfile import ZipFile

PACKAGE_SHA256 = 'b71f5a0c301a56d759f0cd7bfe072067b2f3a30cbb3d9159a20c36994cc52261'
MAP = 'maps/oacmpdm3.bsp'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('pk3', type=Path)
    parser.add_argument('destination', type=Path)
    args = parser.parse_args()
    if hashlib.sha256(args.pk3.read_bytes()).hexdigest() != PACKAGE_SHA256:
        parser.error('expected z_oacmp-volume1-v3.pk3 from Debian openarena-oacmp1 3-17')
    scripts = []
    with ZipFile(args.pk3) as archive:
        for name in archive.namelist():
            if name.endswith('/'):
                continue
            if name.startswith('scripts/') and name.endswith('.shader'):
                scripts.append(Path(name).stem)
            elif name != MAP and not name.startswith('textures/') and name != 'OACMP-Readme.txt':
                continue
            dest = args.destination / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(archive.read(name))
    (args.destination / 'scripts/shaderlist.txt').write_text('\n'.join(sorted(scripts))+'\n')
    print((args.destination / MAP).resolve())


if __name__ == '__main__':
    main()
