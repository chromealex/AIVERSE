"""Unity TerrainData (class 156) -> neutral heightfield, splat layers, trees and detail density.

Reads the binary .asset through its type tree (serialized.py). Coordinates stay in Unity terrain
space: heights[z * res + x] in 0..1 of size[1]; tree positions are 0..1 of the terrain size.
If the asset cannot be decoded, the Unity editor exporter (unity_project/editor/AiverseTerrainExport.cs)
writes the same data as JSON + raw files, read by load_export().
"""
import json
import os
import struct
from array import array
from .serialized import load
from . import unity_yaml as uy

CLASS_TERRAIN_DATA = 156
CLASS_TEXTURE2D = 28
MAX_HEIGHT = 32766.0
# TextureFormat -> (bytes per pixel, channel offsets of R, G, B, A or None)
FORMATS = {1: (1, (None, None, None, 0)), 3: (3, (0, 1, 2, None)), 4: (4, (0, 1, 2, 3)), 5: (4, (1, 2, 3, 0)),
           14: (4, (2, 1, 0, 3)), 63: (1, (0, None, None, None))}


class Terrain:
    """Decoded tile: res, size (x, y, z metres), heights (array 'H'/'h', 0..32766), alpha (list of bytes per layer,
    alpha_res^2), layers [{name, diffuse, normal, tile}], trees, details, notes (what could not be decoded)."""

    def __init__(self):
        self.res, self.size, self.heights = 0, (0.0, 0.0, 0.0), array('h')
        self.alpha_res, self.alpha, self.layers = 0, [], []
        self.tree_prototypes, self.trees = [], []
        self.detail_prototypes, self.detail_res, self.detail = [], 0, []
        self.holes, self.notes = None, []

    def height01(self, u, v):
        """Bilinear height (0..1 of size[1]) at normalised terrain coordinates."""
        n = self.res - 1
        fx, fz = min(max(u, 0.0), 1.0) * n, min(max(v, 0.0), 1.0) * n
        x, z = min(int(fx), n - 1), min(int(fz), n - 1)
        tx, tz = fx - x, fz - z
        h, r = self.heights, self.res
        a, b = h[z * r + x], h[z * r + x + 1]
        c, d = h[(z + 1) * r + x], h[(z + 1) * r + x + 1]
        return ((a * (1 - tx) + b * tx) * (1 - tz) + (c * (1 - tx) + d * tx) * tz) / MAX_HEIGHT

    def weights(self, u, v):
        """Splat weights (0..255) of every layer at normalised coordinates (nearest texel)."""
        n = self.alpha_res
        if not n:
            return []
        x, z = min(int(u * n), n - 1), min(int(v * n), n - 1)
        x, z = max(x, 0), max(z, 0)
        return [layer[z * n + x] for layer in self.alpha]

    def summary(self):
        return dict(resolution=self.res, size=list(self.size), alphamapResolution=self.alpha_res,
                    layers=self.layers, treePrototypes=self.tree_prototypes, trees=len(self.trees),
                    detailPrototypes=self.detail_prototypes, detailResolution=self.detail_res,
                    holes=self.holes is not None, notes=self.notes)


def _pptr(sf, ref, assets):
    """(same-file path id, None) or (None, external asset path) for a binary PPtr."""
    fid, pid = ref.get('m_FileID', 0), ref.get('m_PathID', 0)
    if not pid:
        return None, None
    if fid == 0:
        return pid, None
    if 0 < fid <= len(sf.externals):
        guid = sf.externals[fid - 1][0]
        return None, assets.path(guid) if assets else None
    return None, None


def _ref_path(ref, assets):
    p = assets.path(ref) if assets and isinstance(ref, dict) else None
    return p


def _terrain_layer(path, assets):
    docs = assets.docs(path) if assets else []
    d = next((d for d in docs if d.type == 'TerrainLayer'), None)
    if d is None:
        return dict(name=os.path.splitext(os.path.basename(path or ''))[0], source=path, diffuse=None, normal=None, tile=[10.0, 10.0])
    data = d.data
    tile = data.get('m_TileSize') or {}
    off = data.get('m_TileOffset') or {}
    return dict(name=str(data.get('m_Name', '')), source=path, diffuse=_ref_path(data.get('m_DiffuseTexture'), assets),
                normal=_ref_path(data.get('m_NormalMapTexture'), assets),
                tile=[float(tile.get('x', 10) or 10), float(tile.get('y', 10) or 10)], offset=[float(off.get('x', 0)), float(off.get('y', 0))],
                normalScale=float(data.get('m_NormalScale', 1) or 1))


def _texture_channels(sf, tex, path):
    """Top mip of a Texture2D as one bytes object per channel (R, G, B, A)."""
    w, h, fmt = tex['m_Width'], tex['m_Height'], tex['m_TextureFormat']
    if fmt not in FORMATS:
        raise ValueError('splat texture format %d not supported' % fmt)
    bpp, offs = FORMATS[fmt]
    data = tex.get('image data') or b''
    stream = tex.get('m_StreamData') or {}
    if not data and stream.get('size'):
        rel = stream.get('path', '')
        rel = rel.split('/', 1)[1] if rel.startswith('archive:/') else rel
        side = os.path.join(os.path.dirname(path), os.path.basename(rel))
        with open(side, 'rb') as f:
            f.seek(stream.get('offset', 0))
            data = f.read(stream['size'])
    need = w * h * bpp
    if len(data) < need:
        raise ValueError('splat texture data truncated')
    data = data[:need]
    return w, h, [data[o::bpp] if o is not None else bytes(w * h) for o in offs]


def decode(path, assets=None):
    """Decode a TerrainData .asset; assets (unity_scene.Assets) resolves TerrainLayer / prefab GUIDs."""
    sf = load(path)
    ids = sf.by_class(CLASS_TERRAIN_DATA)
    if not ids:
        raise ValueError('no TerrainData object in ' + path)
    _, td = sf.read(ids[0])
    t = Terrain()
    hm = td['m_Heightmap']
    t.res = int(hm['m_Resolution'])
    sc = hm['m_Scale']
    t.size = (float(sc['x']) * (t.res - 1), float(sc['y']), float(sc['z']) * (t.res - 1))
    t.heights = hm['m_Heights']
    if len(t.heights) != t.res * t.res:
        raise ValueError('heightmap size %d does not match resolution %d' % (len(t.heights), t.res))
    holes = hm.get('m_Holes') or []
    if len(holes):
        t.holes = bytes(holes)
        t.notes.append('terrain holes are not converted')
    sd = td.get('m_SplatDatabase', {})
    t.alpha_res = int(sd.get('m_AlphamapResolution', 0))
    layer_refs = sd.get('m_TerrainLayers') or sd.get('m_Splats') or []
    for ref in layer_refs:
        if 'm_FileID' in ref:
            _, ext = _pptr(sf, ref, assets)
            t.layers.append(_terrain_layer(ext, assets) if ext else dict(name='unresolved', source=None, diffuse=None, normal=None, tile=[10.0, 10.0]))
        else:  # pre-2018.3 SplatPrototype
            t.layers.append(dict(name='splat', source=None, diffuse=None, normal=None, tile=[10.0, 10.0]))
            t.notes.append('legacy SplatPrototype textures are not resolved')
    channels = []
    for ref in sd.get('m_AlphaTextures') or []:
        pid, _ = _pptr(sf, ref, assets)
        if pid is None or pid not in sf.objects:
            t.notes.append('alphamap texture is external or missing')
            channels += [None] * 4
            continue
        _, tex = sf.read(pid)
        w, h, ch = _texture_channels(sf, tex, path)
        if w != t.alpha_res or h != t.alpha_res:
            t.alpha_res = w
        channels += ch
    n = t.alpha_res * t.alpha_res
    t.alpha = [(channels[i] if i < len(channels) and channels[i] is not None else bytes(n)) for i in range(len(t.layers))]
    dd = td.get('m_DetailDatabase', {})
    for p in dd.get('m_TreePrototypes') or []:
        _, ext = _pptr(sf, p.get('prefab', {}), assets)
        t.tree_prototypes.append(dict(prefab=ext, bendFactor=p.get('bendFactor', 0)))
    for i in dd.get('m_TreeInstances') or []:
        pos = i.get('position', {})
        t.trees.append(dict(position=[pos.get('x', 0), pos.get('y', 0), pos.get('z', 0)], width=i.get('widthScale', 1),
                            height=i.get('heightScale', 1), rotation=i.get('rotation', 0), prototype=i.get('index', 0)))
    for p in dd.get('m_DetailPrototypes') or []:
        _, prefab = _pptr(sf, p.get('prototype', {}), assets)
        _, tex = _pptr(sf, p.get('prototypeTexture', {}), assets)
        t.detail_prototypes.append(dict(prefab=prefab, texture=tex, mesh=bool(p.get('usePrototypeMesh', 0)),
                                        minWidth=p.get('minWidth', 1), maxWidth=p.get('maxWidth', 1),
                                        minHeight=p.get('minHeight', 1), maxHeight=p.get('maxHeight', 1),
                                        density=p.get('density', 1)))
    count, samples = int(dd.get('m_PatchCount', 0)), int(dd.get('m_PatchSamples', 0))
    patches = dd.get('m_Patches') or []
    if t.detail_prototypes and count and samples and len(patches) == count * count:
        res = count * samples
        t.detail_res = res
        grids = [bytearray(res * res) for _ in t.detail_prototypes]
        ss = samples * samples
        for pi, patch in enumerate(patches):
            px, pz = pi % count, pi // count
            values = patch.get('numberOfObjects')
            if values is None or not len(values):
                values = patch.get('coverage') or []
            for li, proto in enumerate(patch.get('layerIndices') or []):
                if proto >= len(grids):
                    continue
                g = grids[proto]
                base = li * ss
                for sz in range(samples):
                    row = (pz * samples + sz) * res + px * samples
                    for sx in range(samples):
                        k = base + sz * samples + sx
                        if k < len(values):
                            g[row + sx] = min(255, values[k])
        t.detail = [bytes(g) for g in grids]
    return t


def load_export(json_path):
    """Read the editor exporter's JSON (+ .r16 heights, .splat weights) into a Terrain."""
    with open(json_path, encoding='utf-8') as f:
        j = json.load(f)
    t = Terrain()
    base = os.path.dirname(json_path)
    t.res = int(j['resolution'])
    t.size = tuple(float(v) for v in j['size'])
    with open(os.path.join(base, j['heights']), 'rb') as f:
        raw = f.read()
    t.heights = array('h')
    t.heights.frombytes(raw[:t.res * t.res * 2])
    if struct.pack('=h', 1) != struct.pack('<h', 1):
        t.heights.byteswap()
    t.alpha_res = int(j.get('alphamapResolution', 0))
    t.layers = j.get('layers', [])
    if j.get('alphamaps'):
        with open(os.path.join(base, j['alphamaps']), 'rb') as f:
            raw = f.read()
        n = t.alpha_res * t.alpha_res
        t.alpha = [raw[i * n:(i + 1) * n] for i in range(len(t.layers))]
    root = os.path.dirname(os.path.dirname(base))  # <project>/AiverseExport/terrain -> <project>

    def absolute(p):
        return os.path.join(root, p) if p else None
    for layer in t.layers:
        for k in ('source', 'diffuse', 'normal'):
            layer[k] = absolute(layer.get(k))
    t.tree_prototypes = [dict(p, prefab=absolute(p.get('prefab'))) for p in j.get('treePrototypes', [])]
    t.trees = j.get('trees', [])
    t.detail_prototypes = [dict(p, prefab=absolute(p.get('prefab')), texture=absolute(p.get('texture'))) for p in j.get('detailPrototypes', [])]
    t.detail_res = int(j.get('detailResolution', 0))
    if j.get('details'):
        with open(os.path.join(base, j['details']), 'rb') as f:
            raw = f.read()
        n = t.detail_res * t.detail_res
        t.detail = [raw[i * n:(i + 1) * n] for i in range(len(t.detail_prototypes))]
    t.notes.append('read from the Unity editor export')
    return t
