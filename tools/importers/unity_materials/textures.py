"""Unity TextureImporter settings -> AIVERSE texture plan and per-target packaging rules.

The plan honours the importer: colour space (sRGBTexture), normal maps (textureType 1, or
convertToNormalMap which is reported: heights are not converted), alpha source, max size (default
and Android / iPhone overrides) and mipmaps. Mobile compression follows the Drone Fall rules
(Ext-Client AGENTS.md): an explicit Android/iOS override with an ASTC format wins; otherwise colour
textures of models / environment and large backgrounds get ASTC 6x6, UI icons and visually
sensitive masks ASTC 4x4, and numeric/data textures (linear, LUTs, ramps, gradients, font atlases,
dynamic textures) stay uncompressed. aiversebuild applies the rules (game.json `packaging`).
"""
import re
from pathlib import Path

from lib.unity.unity_yaml import get

# TextureImporterFormat values of the ASTC formats -> block size
ASTC_FORMATS = {48: '4x4', 49: '5x5', 50: '6x6', 51: '8x8', 52: '10x10', 53: '12x12',
                54: '4x4', 55: '5x5', 56: '6x6', 57: '8x8', 58: '10x10', 59: '12x12'}
SUPPORTED_BLOCKS = ('4x4', '6x6')
TARGETS = {'Android': 'android', 'iOS': 'ios', 'iPhone': 'ios'}
# uncompressed TextureImporterFormat values (Alpha8, RGB24, RGBA32, ARGB32, RGB565, R16, RGBA4444, RGBAHalf, R8, ...)
UNCOMPRESSED = (1, 2, 3, 4, 5, 7, 9, 13, 14, 15, 16, 17, 18, 19, 20, 62, 63)
DATA_NAME = re.compile(r'(^|[_\- ])(lut|ramp|gradient|noise|flow|height|depth|mask_data|curve|sdf)([_\- .]|$)', re.I)
MASK_NAME = re.compile(r'(mask|_m$|alpha)', re.I)


def importer(meta):
    return meta.get('TextureImporter') if isinstance(meta, dict) else None


def _int(v, default=0):
    return v if isinstance(v, int) and not isinstance(v, bool) else default


def plan(rel, meta, roles=(), size=None):
    """-> dict(path, kind, srgb, normal, alpha, mipmaps, maxSize, targets{android,ios: {compress, maxSize, source}}, notes)

    rel: path relative to the project; meta: parsed .meta; roles: material roles (base, normal,
    emission, data, detail, mask, ui); size: (w, h) of the source when known."""
    ti = importer(meta) or {}
    notes = []
    ttype = _int(ti.get('textureType'), 0)
    srgb = _int(get(ti, 'mipmaps', 'sRGBTexture', default=ti.get('sRGBTexture', 1)), 1) == 1
    normal = ttype == 1 or 'normal' in roles
    if _int(get(ti, 'bumpmap', 'convertToNormalMap', default=0)) == 1:
        notes.append('Unity generates this normal map from a height map (convertToNormalMap); the engine gets the height image.')
    alpha_source = _int(ti.get('alphaSource'), 1)
    mip = _int(get(ti, 'mipmaps', 'enableMipMap', default=1), 1) == 1
    name = Path(rel).stem
    ui = ttype in (2, 8) or '/UI/' in '/' + rel or 'ui' in roles
    sprite = ttype == 8
    if ttype == 8 and not mip:
        mip = False
    data = (not srgb and not normal) or ttype == 10 or bool(DATA_NAME.search(name)) or 'data' in roles and not any(
        r in roles for r in ('base', 'emission'))
    if ttype in (6, 11, 12):
        notes.append('Lightmap / shadowmask texture: not used by imported materials.')
    kind = 'normal' if normal else 'ui' if ui else 'data' if data else 'color'
    if normal and srgb and ttype != 1:
        notes.append('Used as a normal map but imported as sRGB colour in Unity; the engine reads it as linear data.')
    if 'base' in roles and not srgb:
        notes.append('Base colour texture imported as linear in Unity (sRGBTexture off); the engine decodes base colour as sRGB.')
    default_max = _int(ti.get('maxTextureSize'), 2048)
    platforms = {}
    for ps in ti.get('platformSettings') or []:
        if isinstance(ps, dict):
            platforms[str(ps.get('buildTarget') or ps.get('m_BuildTarget') or '')] = ps
    dflt = platforms.get('DefaultTexturePlatform') or {}
    if dflt:
        default_max = _int(dflt.get('maxTextureSize'), default_max)
    targets = {}
    compression = _int(dflt.get('textureCompression'), _int(ti.get('textureCompression'), 1))
    for unity_name, target in TARGETS.items():
        if target in targets and not platforms.get(unity_name):
            continue
        ps = platforms.get(unity_name) or {}
        over = _int(ps.get('overridden'), 0) == 1
        tmax = _int(ps.get('maxTextureSize'), default_max) if over else default_max
        fmt = _int(ps.get('textureFormat'), -1) if over else -1
        entry = dict(maxSize=tmax)
        if over and fmt in ASTC_FORMATS:
            block = ASTC_FORMATS[fmt]
            if block not in SUPPORTED_BLOCKS:
                notes.append('%s override ASTC %s: the engine encodes 4x4 / 6x6; %s used.' % (unity_name, block, '6x6' if block != '5x5' else '4x4'))
                block = '6x6' if block != '5x5' else '4x4'
            entry.update(compress='astc' + block, source='override')
        elif over and fmt not in (-1,) + UNCOMPRESSED:  # explicit non-ASTC compressed formats (ETC, PVRTC, ...)
            entry.update(compress='astc6x6' if kind == 'color' else 'astc4x4' if kind in ('ui', 'normal') else None,
                         source='override-format-%d' % fmt)
            notes.append('%s override format %d replaced by the Drone Fall ASTC rule.' % (unity_name, fmt))
        elif over and fmt in UNCOMPRESSED:   # RGBA32 / RGB24 / Alpha8 ... : uncompressed on purpose
            entry.update(compress=None, source='override-uncompressed')
        else:
            if kind == 'color':
                compress = 'astc4x4' if MASK_NAME.search(name) else 'astc6x6'
            elif kind in ('ui', 'normal'):
                compress = 'astc4x4'
            else:
                compress = None
            if compression == 0 and not over:
                compress, src = None, 'importer-uncompressed'
            else:
                src = 'drone-fall-rule'
            entry.update(compress=compress, source=src)
        if size and max(size) <= tmax:
            entry['maxSize'] = 0   # nothing to shrink
        targets[target] = entry
    return dict(path=rel, kind=kind, srgb=srgb and not normal, normal=normal, sprite=sprite,
                alpha=alpha_source != 0, mipmaps=mip and not sprite, maxSize=default_max if not size or max(size) > default_max else 0,
                targets=targets, notes=notes)


def packaging(plans, prefix='content/'):
    """game.json `packaging` block with one exact-path rule per texture and target."""
    out = {'files': [], 'targets': {}}
    for p in sorted(plans, key=lambda x: x['path']):
        path = prefix + p['path']
        if p['maxSize']:
            out['files'].append(dict(match=path, maxSize=p['maxSize']))
        for target, t in sorted(p['targets'].items()):
            rule = dict(match=path)
            if t.get('maxSize'):
                rule['maxSize'] = t['maxSize']
            if t.get('compress'):
                rule.update(compress=t['compress'], colorSpace='srgb' if p['srgb'] else 'linear', mipmaps=p['mipmaps'])
                if p['normal']:
                    rule['normalMap'] = True
                if not p['alpha']:
                    rule['alpha'] = False
            if len(rule) > 1:
                out['targets'].setdefault(target, {'files': []})['files'].append(rule)
    if not out['files']:
        del out['files']
    return out
