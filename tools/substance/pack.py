#!/usr/bin/env python3
"""Pack explicitly mapped Substance PNG outputs into a Studio material bundle."""
import argparse
import json
from pathlib import Path
import tempfile
try:
    from PIL import Image, ImageOps
except ImportError:
    raise SystemExit('Install Pillow in your tooling environment: pip install Pillow')


def pack(source, mapping, output, directx=False):
    source, output = Path(source).resolve(), Path(output).resolve()
    allowed = {'baseColor', 'normal', 'roughness', 'metallic', 'occlusion', 'emissive'}
    if not isinstance(mapping, dict) or not mapping or set(mapping) - allowed:
        raise ValueError('Expected explicit semantic-to-PNG mapping')
    if output.exists():
        raise ValueError('Output directory already exists')
    images = {}
    for semantic, filename in mapping.items():
        path = (source / filename).resolve()
        if not path.is_relative_to(source) or path.suffix.lower() != '.png':
            raise ValueError('Maps must be PNG files inside the source directory')
        with Image.open(path) as image:
            if image.mode not in ('L', 'RGB', 'RGBA'):
                raise ValueError('Only 8-bit L/RGB/RGBA maps are supported')
            image.load()
            images[semantic] = image.copy()
    sizes = {image.size for image in images.values()}
    if len(sizes) != 1:
        raise ValueError('All maps must have identical dimensions')
    size = next(iter(sizes))
    if max(size) > 4096:
        raise ValueError('Maps exceed the 4096 stage budget')
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.pack-', dir=output.parent) as temporary:
        staging = Path(temporary)
        material = {'metallic': 1 if 'metallic' in images else 0, 'roughness': 1}
        spaces = {}
        for semantic, field in [('baseColor', 'texture'), ('normal', 'normalMap'),
                                ('emissive', 'emissiveMap')]:
            if semantic not in images:
                continue
            image = images[semantic].convert('RGBA')
            if semantic == 'normal' and directx:
                r, g, b, a = image.split()
                image = Image.merge('RGBA', (r, ImageOps.invert(g), b, a))
            name = semantic + '.png'
            image.save(staging / name)
            material[field] = name
            spaces[name] = 'linear' if semantic == 'normal' else 'sRGB'
            if semantic == 'emissive':
                material['emissiveStrength'] = 1
        if any(k in images for k in ('roughness', 'metallic', 'occlusion')):
            channels = []
            for semantic, default in [('occlusion', 255), ('roughness', 255), ('metallic', 0)]:
                image = images.get(semantic)
                # Scalar outputs must really be grayscale; never silently use luminance.
                if image is not None:
                    rgb = image.convert('RGB').split()
                    if rgb[0].tobytes() != rgb[1].tobytes() or rgb[0].tobytes() != rgb[2].tobytes():
                        raise ValueError(f'{semantic} must be grayscale')
                    channels.append(rgb[0])
                else:
                    channels.append(Image.new('L', size, default))
            Image.merge('RGB', channels).save(staging / 'orm.png')
            material['metallicRoughnessMap'] = 'orm.png'
            if 'occlusion' in images:
                material['occlusionMap'] = 'orm.png'
            spaces['orm.png'] = 'linear'
        (staging / 'material.json').write_text(json.dumps(material, indent=2) + '\n')
        (staging / 'bundle.json').write_text(json.dumps({
            'schema': 1, 'colorSpaces': spaces, 'normalConvention': 'OpenGL',
            'width': size[0], 'height': size[1], 'sourceMapping': mapping,
            'note': 'Texture paths are bundle-relative; rebase on project import.'}, indent=2) + '\n')
        staging.rename(output)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--mapping', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--normal-directx', action='store_true')
    args = parser.parse_args()
    try:
        print(pack(args.source, json.loads(args.mapping.read_text()), args.output, args.normal_directx))
    except (ValueError, OSError, TypeError) as error:
        parser.exit(1, f'Pack failed: {error}\n')


if __name__ == '__main__':
    main()
