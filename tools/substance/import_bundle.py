#!/usr/bin/env python3
"""Import a packed Substance bundle into an offline AIVERSE project."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import tempfile
from PIL import Image


def import_bundle(bundle, project, name):
    bundle, project = Path(bundle).resolve(strict=True), Path(project).resolve(strict=True)
    if not re.fullmatch(r'[A-Za-z0-9_-]+', name):
        raise ValueError('Material name must contain only letters, digits, _ or -')
    material = json.loads((bundle / 'material.json').read_text())
    metadata = json.loads((bundle / 'bundle.json').read_text())
    maps = {'texture': 'sRGB', 'normalMap': 'linear', 'emissiveMap': 'sRGB',
            'metallicRoughnessMap': 'linear', 'occlusionMap': 'linear'}
    if metadata.get('schema') != 1 or metadata.get('normalConvention') != 'OpenGL':
        raise ValueError('Unsupported bundle schema or normal convention')
    if not isinstance(material, dict) or set(material) - set(maps) - {'metallic', 'roughness', 'emissiveStrength'}:
        raise ValueError('Unsupported material fields')
    for key in ('metallic', 'roughness', 'emissiveStrength'):
        if key in material and (type(material[key]) not in (float, int) or
                                not 0 <= material[key] <= 1):
            raise ValueError('Invalid material factor')
    payload = {}
    for field, space in maps.items():
        if field not in material:
            continue
        filename = material[field]
        if not isinstance(filename, str) or Path(filename).name != filename:
            raise ValueError('Texture must be a bundle-local filename')
        path = (bundle / filename).resolve(strict=True)
        if not path.is_relative_to(bundle) or path.suffix != '.png':
            raise ValueError('Texture escapes bundle or is not PNG')
        if metadata.get('colorSpaces', {}).get(filename) != space:
            raise ValueError('Incorrect texture color space')
        data = path.read_bytes()
        with Image.open(path) as image:
            image.load()
            if image.size != (metadata['width'], metadata['height']) or max(image.size) > 4096:
                raise ValueError('Invalid image dimensions')
        payload[filename] = data
    if not payload:
        raise ValueError('Bundle contains no textures')
    identity = hashlib.sha256(json.dumps(material, sort_keys=True).encode())
    for filename, data in sorted(payload.items()):
        identity.update(filename.encode() + b'\0' + data)
    relative = Path('assets/substance') / identity.hexdigest()
    destination = project / relative
    if not destination.resolve().is_relative_to(project):
        raise ValueError('Asset directory escapes project')
    library = project / 'materials.json'
    if library.is_symlink():
        raise ValueError('Symlinked material library is unsupported')
    # Cooperating importers serialize; Studio must be closed (it does not share this lock).
    lock = project / '.substance-import.lock'
    descriptor = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
    try:
        before = library.read_bytes() if library.exists() else None
        document = json.loads(before) if before is not None else {'materials': {}}
        if not isinstance(document, dict) or not isinstance(document.get('materials'), dict):
            raise ValueError('Invalid materials.json library')
        if name in document['materials']:
            raise ValueError('Material already exists; choose another name')
        destination.parent.mkdir(parents=True, exist_ok=True)
        if destination.exists():
            if any(not (destination / f).is_file() or (destination / f).read_bytes() != d
                   for f, d in payload.items()):
                raise ValueError('Existing content-addressed textures are damaged')
        else:
            with tempfile.TemporaryDirectory(prefix='.import-', dir=destination.parent) as temporary:
                staging = Path(temporary)
                for filename, data in payload.items():
                    (staging / filename).write_bytes(data)
                staging.rename(destination)
        for field in maps:
            if field in material:
                material[field] = (relative / material[field]).as_posix()
        document['materials'][name] = material
        with tempfile.NamedTemporaryFile(dir=project, prefix='.materials-', delete=False) as stream:
            pending = Path(stream.name)
            stream.write((json.dumps(document, indent=2, allow_nan=False) + '\n').encode())
        try:
            if (library.read_bytes() if library.exists() else None) != before:
                raise ValueError('Material library changed during import; retry with Studio closed')
            pending.replace(library)
        finally:
            pending.unlink(missing_ok=True)
    finally:
        os.close(descriptor)
        lock.unlink()
    return material


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bundle', type=Path)
    parser.add_argument('--project', type=Path, required=True)
    parser.add_argument('--name', required=True)
    args = parser.parse_args()
    try:
        print(json.dumps(import_bundle(args.bundle, args.project, args.name), indent=2))
    except (ValueError, OSError, KeyError, TypeError) as error:
        parser.exit(1, f'Import failed: {error}\n')
