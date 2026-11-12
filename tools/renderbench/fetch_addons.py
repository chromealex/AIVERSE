#!/usr/bin/env python3
"""Fetch the five official Intel Sponza additions without downloading source DCC archives."""
import argparse
import json
import shutil
import urllib.parse
import zipfile
import zlib
from pathlib import Path, PurePosixPath
from fetch_sponza import RemoteZip, safe_name, digest

PACKAGES = {
    'curtains': (726650, 'pkg_a_curtains/NewSponza_Curtains_glTF.gltf'),
    'ivy': (726656, 'pkg_b_ivy/NewSponza_IvyGrowth_glTF.gltf'),
    'trees': (726662, 'pkg_c_trees/NewSponza_CypressTree_glTF.gltf'),
    'candles': (726676, 'pkg_d_10k_candles/NewSponza_4_Combined_glTF.gltf'),
    'knight': (763175, 'pkg_e_knight_anim/Exports/FBX/Knight_USD_002.fbx'),
}

def fetch(dest, package):
    number, model = PACKAGES[package]
    url = f'https://cdrdv2.intel.com/v1/dl/getContent/{number}'
    remote = RemoteZip(url)
    with zipfile.ZipFile(remote) as archive:
        names = [model]
        data = archive.read(model)
        if model.endswith('.gltf'):
            gltf = json.loads(data)
            for item in gltf.get('buffers', []) + gltf.get('images', []):
                uri = item.get('uri', '')
                if uri.startswith('data:'): continue
                if not uri or urllib.parse.urlsplit(uri).scheme: raise ValueError('invalid resource URI')
                names.append(str(PurePosixPath(model).parent / safe_name(urllib.parse.unquote(uri))))
        else:
            names += [n for n in archive.namelist() if n.endswith('.png') or n.endswith('Knight_Animation_Data_Only_002.fbx') or n.endswith('Scenes/knight_USD_PREVIEW_SURFACE_002.rnd.ma')]
        names += [n for n in archive.namelist() if n.endswith(('credits_license.txt', 'README.txt'))]
        names = sorted(set(names))
        dest.mkdir(parents=True, exist_ok=True)
        needed = sum(archive.getinfo(n).file_size for n in names if not (dest/n).exists())
        if shutil.disk_usage(dest).free < needed + 200*1024**2: raise RuntimeError('insufficient disk space')
        print(f'{package}: {len(names)} files, {needed/1e9:.3f} GB missing', flush=True)
        records = []
        for index, name in enumerate(names):
            safe_name(name)
            target = dest/name
            info = archive.getinfo(name)
            if target.exists():
                content = target.read_bytes()
                if len(content) != info.file_size or zlib.crc32(content) != info.CRC: raise RuntimeError(f'modified asset: {target}')
            else:
                content = data if name == model else archive.read(name)
                target.parent.mkdir(parents=True, exist_ok=True)
                temporary = target.with_suffix(target.suffix+'.part')
                temporary.write_bytes(content)
                temporary.replace(target)
            records.append(dict(path=name, bytes=len(content), sha256=digest(content)))
            print(f'{package} {index+1}/{len(names)} {name}', flush=True)
        manifest = dict(source=url, archiveBytes=remote.size, archiveETag=remote.etag, model=model, files=records,
                        fingerprint=digest(json.dumps(records, sort_keys=True, separators=(',', ':')).encode()))
        lock = Path(__file__).with_name(f'sponza-{package}-lock.json')
        if lock.exists() and json.loads(lock.read_text())['fingerprint'] != manifest['fingerprint']:
            raise RuntimeError('upstream assets differ from repository lock')
        (dest/f'{package}-manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
        return manifest

def assemble(dest):
    tools = Path(__file__).parent
    manifest = json.loads((tools/'sponza-full-lock.json').read_text())
    for record in manifest['files']:
        path = dest/record['path']
        if not path.is_file() or path.stat().st_size != record['bytes']:
            raise RuntimeError(f'missing/truncated asset: {path}; fetch base Sponza into the same destination first')
    (dest/'asset-manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    shutil.copyfile(tools/'scene-full.json', dest/'scene.json')
    print(f'Ready: {dest / "scene.json"}', flush=True)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('destination', type=Path)
    parser.add_argument('--package', choices=list(PACKAGES))
    args = parser.parse_args()
    for package in ([args.package] if args.package else PACKAGES): fetch(args.destination, package)
    if not args.package: assemble(args.destination)
