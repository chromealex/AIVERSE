"""Emit world data of imported scenes: terrain heightfield / detail / splat, layer palette, trees, detail
density, spline roads (road cells), marker rules (starts, areas, blocked cells, regions, bounds, grid).

Engine frame: map x = Unity x, map z = -Unity z (cells = metres * scale), heights in 1/16 world units.
"""
import math
import re
import struct
import zlib
from array import array
from pathlib import Path
from .common import item, ident, read_json

HEIGHT_UNIT = 16


def uterrain_max():
    from lib.unity.terrain import MAX_HEIGHT
    return MAX_HEIGHT
MAX_SAMPLES = 1_200_000


def _png_gray(width, height, pixels):
    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data) & 0xFFFFFFFF)
    raw = b''.join(b'\0' + bytes(pixels[y * width:(y + 1) * width]) for y in range(height))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 0, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def _le(a):
    if struct.pack('=h', 1) != struct.pack('<h', 1):
        a = array(a.typecode, a)
        a.byteswap()
    return a.tobytes()


# ---------------------------------------------------------------- values of serialized fields

def number(v, default=None):
    """int/float, or a soft-float wrapper {rawValue: float32 bits} (ME.BECS sfloat)."""
    if isinstance(v, bool):
        return float(v)
    if isinstance(v, (int, float)):
        return float(v)
    if isinstance(v, dict) and 'rawValue' in v and isinstance(v['rawValue'], int):
        return struct.unpack('<f', struct.pack('<I', v['rawValue'] & 0xFFFFFFFF))[0]
    return default


def vector(v):
    if isinstance(v, dict):
        out = [number(v.get(k)) for k in ('x', 'y', 'z') if k in v]
        return out if out and None not in out else None
    return None


def fixed_string(v):
    """Unity.Collections FixedStringNBytes serialized as {utf8LengthInBytes, bytes {offsetNNNN {byteNNNN}} ...}."""
    if isinstance(v, str):
        return v
    if not isinstance(v, dict) or 'utf8LengthInBytes' not in v:
        return None
    at = {}

    def walk(d, base):
        for k, x in d.items():
            m = re.match(r'(offset|byte)(\d+)$', k)
            if not m:
                continue
            if isinstance(x, dict):
                walk(x, base + int(m[2]))
            elif isinstance(x, int):
                at[base + int(m[2])] = x & 255
    walk(v.get('bytes') or {}, 0)
    flat = [at.get(i, 0) for i in range(int(v['utf8LengthInBytes']))]
    return bytes(flat).decode('utf-8', 'replace')


def field(entity, path):
    """Dotted path(s) into the entity's fields; the first part may name a resolved link (local component)."""
    if not entity:
        return None
    for p in (path if isinstance(path, list) else [path]):
        node, parts = entity.get('fields', {}), p.split('.')
        link = entity.get('links', {}).get(parts[0])
        if link is not None and len(parts) > 1:
            node, parts = link['fields'], parts[1:]
        ok = True
        for part in parts:
            if isinstance(node, dict) and part in node:
                node = node[part]
            else:
                ok = False
                break
        if ok:
            return node
    return None


# ---------------------------------------------------------------- terrain tiles

class Tile:
    def __init__(self, terrain, position):
        self.t, self.x0, self.y0, self.z0 = terrain, position[0], position[1], position[2]
        self.sx, self.sy, self.sz = terrain.size
        self.fold = list(range(len(terrain.layers)))

    def contains(self, x, z):
        return self.x0 <= x <= self.x0 + self.sx and self.z0 <= z <= self.z0 + self.sz

    def uv(self, x, z):
        return (x - self.x0) / self.sx, (z - self.z0) / self.sz

    def dist(self, x, z):
        dx = max(self.x0 - x, 0, x - self.x0 - self.sx)
        dz = max(self.z0 - z, 0, z - self.z0 - self.sz)
        return dx * dx + dz * dz


class Terrains:
    """Decoded TerrainData cache + the global splat palette (the engine draws <= 4 layers per game)."""

    def __init__(self, model, root):
        self.model, self.root, self.cache, self.palette = model, root, {}, []

    def terrain(self, path):
        if path not in self.cache:
            from lib.unity import terrain as uterrain
            info = self.model.get('terrains', {}).get(path, {})
            self.cache[path] = None
            if info.get('ok'):
                try:
                    t = uterrain.load_export(str(self.root / info['export'])) if info.get('export') else uterrain.decode(str(self.root / path), None)
                    # layer / prototype paths were resolved (project-relative) at extraction time
                    t.layers = info['layers']
                    t.tree_prototypes, t.detail_prototypes = info['treePrototypes'], info['detailPrototypes']
                    self.cache[path] = t
                except (ValueError, OSError, KeyError) as e:
                    item(self.model, path, 'missing', 'TerrainData decode failed at emit: %s' % e, key='terrain-emit')
        return self.cache[path]

    def tiles(self, scene):
        out = []
        for ref in scene.get('terrains', []):
            t = self.terrain(ref['data'])
            if t is not None:
                p = ref['position']
                out.append(Tile(t, (p[0], p[1], p[2])))  # Unity world position of the Terrain object
        return out

    @staticmethod
    def layer_key(layer):
        return layer.get('diffuse') or layer.get('source') or layer.get('name')

    def choose_palette(self, scenes):
        usage = {}
        for scene in scenes:
            for tile in self.tiles(scene):
                t = tile.t
                area = tile.sx * tile.sz / max(t.alpha_res * t.alpha_res, 1)
                for layer, weights in zip(t.layers, t.alpha):
                    usage.setdefault(self.layer_key(layer), [0.0, layer])[0] += sum(weights[::7]) * 7 * area
        # layers whose textures resolved first, then by painted area
        ranked = sorted(usage.items(), key=lambda kv: (not kv[1][1].get('diffuse'), -kv[1][0], str(kv[0])))
        self.palette = [(k, v[1]) for k, v in ranked[:4] if v[0] > 0] or [(k, v[1]) for k, v in ranked[:1]]
        return self.palette

    def fold(self, tile):
        """Tile layer -> palette slot: identical layers map directly, others to the palette layer they overlap most."""
        keys = [k for k, _ in self.palette]
        t, slots = tile.t, []
        for i, layer in enumerate(t.layers):
            k = self.layer_key(layer)
            if k in keys:
                slots.append(keys.index(k))
                continue
            best, score, mine = 0, -1, t.alpha[i] if i < len(t.alpha) else b''
            for j, other in enumerate(t.layers):
                ok = self.layer_key(other)
                if ok in keys and j != i and j < len(t.alpha):
                    s = sum(o for m, o in zip(mine[::5], t.alpha[j][::5]) if m)
                    if s > score:
                        best, score = keys.index(ok), s
            slots.append(best)
        tile.fold = slots
        return slots


def palette_rules(model, writer, palette, scale):
    """rules.terrain.layers from the chosen palette; tiling textures copied to *_repeat names (kept out of atlases)."""
    layers, root = [], Path(model['sourceRoot'])
    for i, (key, layer) in enumerate(palette):
        entry = dict(id='layer%d' % i, scale=round(max(0.1, float(layer.get('tile', [10])[0]) * scale), 4))
        for src_key, dst_key in (('diffuse', 'texture'), ('normal', 'normal')):
            src = layer.get(src_key)
            if not src:
                continue
            ext = Path(src).suffix.lower()
            if ext not in ('.png', '.jpg', '.jpeg', '.tga'):
                item(model, src, 'missing', 'Terrain layer texture format %s is not converted.' % ext, key='terrain-texture')
                continue
            dst = 'content/_terrain/' + re.sub(r'[^A-Za-z0-9_]+', '_', Path(src).stem) + '_repeat' + ext
            writer.write(dst, (root / src).read_bytes())
            entry[dst_key] = dst
        layers.append(entry)
        item(model, layer.get('source') or key, 'approx', 'Terrain layer mapped to splat slot %d (tile size -> scale).' % i,
             'rules.json#terrain.layers.%d' % i, key='terrain-layer')
    return layers


# ---------------------------------------------------------------- marker rules

def load_rules(mapping_path=None):
    rules = read_json(Path(__file__).with_name('markers.json'))['markers']
    if mapping_path:
        extra = read_json(mapping_path).get('markers', [])
        if not isinstance(extra, list):
            raise ValueError('mapping "markers" must be a list')
        rules = extra + rules
    for r in rules:
        if not isinstance(r, dict) or r.get('kind') not in ('start', 'area', 'block', 'region', 'bounds', 'grid'):
            raise ValueError('invalid marker rule: ' + str(r))
    return rules


def _ref_types(entity):
    out = []
    for c in entity.get('components', []):
        for ref in ((c.get('data') or {}).get('references') or {}).get('RefIds') or []:
            if isinstance(ref, dict):
                out.append((str((ref.get('type') or {}).get('class', '')), ref.get('data') or {}))
    return out


def classify(entity, rules):
    """(rule, data) of the first rule matching the entity, else (None, None)."""
    classes = entity.get('classes') or []
    for r in rules:
        data = entity
        if 'refType' in r:
            hit = next((d for cls, d in _ref_types(entity) if cls == r['refType']), None)
            if hit is None:
                continue
            data = dict(fields=hit, links={})
        if 'class' in r and not any(c == r['class'] or c.endswith('.' + r['class']) for c in classes):
            continue
        if 'match' in r and not re.search(r['match'], str(entity.get('name', ''))):
            continue
        if any(number(field(data, k)) != number(v) and field(data, k) != v for k, v in (r.get('when') or {}).items()):
            continue
        return r, data
    return None, None


def bounds_markers(model, rules, report=True):
    """ScriptableObject map configs (e.g. Drone Fall GD_Map: mapName / mapSize / mapPos) by map name."""
    out = {}
    for e in model['entities']:
        if not e.get('asset'):
            continue
        r, data = classify(e, [x for x in rules if x['kind'] == 'bounds'])
        if r is None:
            continue
        name = fixed_string(field(data, r.get('mapName', 'mapName')))
        size, pos = field(data, r.get('size', 'mapSize')) or {}, field(data, r.get('position', 'mapPos')) or {}
        vals = [number(size.get('x')), number(size.get('y')), number(pos.get('x')), number(pos.get('y'))]
        if name and None not in vals:
            out[name] = dict(size=(vals[0], vals[1]), position=(vals[2], vals[3]), source=e['source'], name=e['name'])
            if report: item(model, e['source'], 'approx', 'Map config: bounds of scene "%s" -> map area map_bounds.' % name, key='map-config')
    return out


def scene_bounds(bounds_by_name, name):
    """Map config of a scene: exact map name, else the longest name the scene name extends ('World-FTUE' -> 'World-FTUE_Part_1')."""
    if name in bounds_by_name:
        return bounds_by_name[name]
    hits = sorted((n for n in bounds_by_name if name.startswith(n + '_')), key=len)
    return bounds_by_name[hits[-1]] if hits else None


def _area(areas, counters, name, x, z, w, h):
    counters[name] = counters.get(name, 0) + 1
    areas['%s_%d' % (name, counters[name])] = [round(x, 3), round(z, 3), round(max(w, 0.5), 3), round(max(h, 0.5), 3)]


def fill(cells, width, height, poly, value):
    """Cells whose centre lies inside the polygon (even-odd rule)."""
    zs = [p[1] for p in poly]
    for y in range(max(0, int(math.floor(min(zs)))), min(height, int(math.ceil(max(zs))) + 1)):
        cy, hits = y + 0.5, []
        for i in range(len(poly)):
            (x1, y1), (x2, y2) = poly[i], poly[(i + 1) % len(poly)]
            if (y1 <= cy) != (y2 <= cy):
                hits.append(x1 + (cy - y1) * (x2 - x1) / (y2 - y1))
        hits.sort()
        for a, b in zip(hits[::2], hits[1::2]):
            for x in range(max(0, int(math.ceil(a - 0.5))), min(width, int(math.floor(b - 0.5)) + 1)):
                cells[y * width + x] = value


def road_cells(cells, width, height, pts, half):
    half = max(half, 0.5)
    for (x1, z1), (x2, z2) in zip(pts, pts[1:]):
        lx, lz = x2 - x1, z2 - z1
        ll = lx * lx + lz * lz or 1e-9
        for y in range(max(0, int(min(z1, z2) - half)), min(height, int(max(z1, z2) + half) + 1)):
            for x in range(max(0, int(min(x1, x2) - half)), min(width, int(max(x1, x2) + half) + 1)):
                px, pz = x + 0.5, y + 0.5
                t = max(0.0, min(1.0, ((px - x1) * lx + (pz - z1) * lz) / ll))
                dx, dz = px - (x1 + t * lx), pz - (z1 + t * lz)
                if dx * dx + dz * dz <= half * half and cells[y * width + x] == ord('.'):
                    cells[y * width + x] = ord('+')


class SceneWorld:
    """World features of one scene; apply() writes them into the map once the origin is fixed."""

    def __init__(self, model, terrains, scene, scale, rules, by_id, bounds_by_name):
        self.model, self.scene, self.scale = model, scene, scale
        self.tiles = terrains.tiles(scene) if terrains else []
        for tile in self.tiles:
            terrains.fold(tile)
        self.markers, self.drop, self.grid = [], set(), None
        self.bounds = scene_bounds(bounds_by_name, scene['name'])
        for o in scene['objects']:
            e = by_id.get(o['entity'])
            r, data = classify(e, rules) if e else (None, None)
            if r is None:
                continue
            if r.get('entity') == 'drop':
                self.drop.add(e['id'])
            self.markers.append(self._marker(r, data, e))
            item(model, e['source'], 'approx', 'Scene marker mapped to map %s (%s).' % (r['kind'], r.get('area', r['kind'])),
                 'maps/' + scene['id'] + '.json#imported.markers', key='marker')

    def _marker(self, r, data, e):
        m = dict(kind=r['kind'], source=e['source'], name=e['name'], position=e['position'],
                 classes=e.get('classes', []), area=r.get('area', r['kind']))
        if 'radius' in r:
            m['radius'] = number(field(data, r['radius']), 1.0) if isinstance(r['radius'], str) else number(r['radius'])
        if 'bounds' in r:
            b = field(data, r['bounds']) or {}
            m['center'] = vector(b.get('m_Center')) or [0, 0, 0]
            m['extent'] = vector(b.get('m_Extent')) or [0.5, 0.5, 0.5]
            m['quaternion'] = e.get('quaternion', [0, 0, 0, 1])
        if 'points' in r:
            pts = field(data, r['points']) or []
            base = e['position']
            for link in e.get('links', {}).values():
                if 'points' in link['fields']:
                    base = link['position']
            m['polygon'] = [[base[0] + number(p.get('x'), 0), base[2] - number(p.get('y'), 0)] for p in pts if isinstance(p, dict)]
        if r['kind'] == 'grid':
            cfg = (e.get('refs') or {}).get(r.get('config', 'config'))
            so = next((x for x in self.model['entities'] if cfg and x.get('asset') == cfg), None)
            props = field(so, r.get('properties', 'graphProperties'))
            if isinstance(props, dict):
                node = number(props.get('nodeSize'), 1.0)
                pos = vector(props.get('position')) or [0, 0, 0]
                w = int(number(props.get('chunkWidth'), 0) * number(props.get('chunksCountX'), 0))
                h = int(number(props.get('chunkHeight'), 0) * number(props.get('chunksCountY'), 0))
                m['grid'] = dict(config=cfg, nodeSize=node, origin=[pos[0], -pos[2] - h * node], nodes=[w, h])
                self.grid = m['grid']
        m['fields'] = {k: v for k, v in e.get('fields', {}).items() if not isinstance(v, (dict, list)) or k in ('bounds', 'randomCount', 'range')}
        return m

    def extents(self):
        """(x0, z0, x1, z1) in engine-axis metres covered by terrain tiles / map bounds, or None."""
        rects = [(t.x0, -(t.z0 + t.sz), t.x0 + t.sx, -t.z0) for t in self.tiles]
        if self.bounds:
            (px, pz), (sx, sz) = self.bounds['position'], self.bounds['size']
            rects.append((px, -(pz + sz), px + sx, -pz))
        if not rects:
            return None
        return min(r[0] for r in rects), min(r[1] for r in rects), max(r[2] for r in rects), max(r[3] for r in rects)

    def box(self, m):
        """Rotated Bounds of a marker -> XZ polygon in engine-axis metres."""
        from lib.unity.unity_scene import q_rot
        c, e, q = m['center'], m['extent'], m['quaternion']
        px, pz = m['position'][0], -m['position'][2]
        out = []
        for dx, dz in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            v = q_rot(q, (c[0] + dx * e[0], c[1], c[2] + dz * e[2]))
            out.append((px + v[0], -(pz + v[2])))
        return out

    def apply(self, data, writer, rel_base, ox, oz, width, height, palette_size, types):
        s, imported = self.scale, {}
        cells = bytearray(b'.' * (width * height))

        def to_cell(x, z):
            return x * s - ox, z * s - oz
        if self.tiles:
            self._terrain(data, writer, rel_base, ox, oz, width, height, palette_size, imported)
            self._trees(data, writer, rel_base, ox, oz, types, imported)
        starts, areas, regions, counters = [], {}, [], {}
        for m in self.markers:
            cx, cz = to_cell(m['position'][0], m['position'][2])
            if m['kind'] == 'start':
                starts.append([round(cx, 3), round(cz, 3)])
            elif m['kind'] in ('area', 'block') and 'extent' in m:
                poly = [to_cell(x, z) for x, z in self.box(m)]
                if m['kind'] == 'block':
                    fill(cells, width, height, poly, ord('#'))
                else:
                    xs, zs = [p[0] for p in poly], [p[1] for p in poly]
                    _area(areas, counters, m['area'], min(xs), min(zs), max(xs) - min(xs), max(zs) - min(zs))
            elif m['kind'] == 'area':
                r = (m.get('radius') or 1.0) * s
                _area(areas, counters, m['area'], cx - r, cz - r, 2 * r, 2 * r)
            elif m['kind'] == 'region' and len(m.get('polygon', [])) >= 3:
                regions.append((m, [to_cell(x, z) for x, z in m['polygon']]))
        roads = self.scene.get('roads', [])
        for road in roads:
            road_cells(cells, width, height, [to_cell(p[0], p[2]) for p in road['points']], road['width'] * s / 2)
        if roads:
            imported['roads'] = [dict(name=r['name'], width=round(r['width'] * s, 3),
                                      points=[[round(a, 3), round(b, 3)] for a, b in (to_cell(p[0], p[2]) for p in r['points'])]) for r in roads]
        if regions:
            letters = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz'
            rows, names = bytearray(b'.' * (width * height)), {}
            for i, (m, poly) in enumerate(regions[:len(letters)]):
                fill(rows, width, height, poly, ord(letters[i]))
                names[letters[i]] = '%s_%d' % (m['area'], i + 1)
            data['regions'] = dict(rows=[rows[y * width:(y + 1) * width].decode() for y in range(height)], names=names)
            if len(regions) > len(letters):
                item(self.model, self.scene['source'], 'missing', 'More than 52 region polygons; extra regions dropped.', key='regions')
        if any(c != ord('.') for c in cells):
            data.setdefault('terrain', {})['rows'] = [cells[y * width:(y + 1) * width].decode() for y in range(height)]
        if self.bounds:
            (px, pz), (sx, sz) = self.bounds['position'], self.bounds['size']
            x0, z0 = to_cell(px, -(pz + sz))
            areas['map_bounds'] = [round(x0, 3), round(z0, 3), round(sx * s, 3), round(sz * s, 3)]
            imported['bounds'] = dict(source=self.bounds['source'], name=self.bounds['name'])
        if areas:
            data['areas'] = areas
        if starts:
            players = data['players']
            while len(players) < len(starts):
                players.append(dict(name='Player %d' % (len(players) + 1), team=len(players) + 1))
            for p, st in zip(players, starts):
                p['start'] = st
        if self.grid:
            g = self.grid
            gx, gz = to_cell(g['origin'][0], g['origin'][1])
            imported['grid'] = dict(g, cell=[round(gx, 3), round(gz, 3)], cellsPerNode=round(g['nodeSize'] * s, 4))
            if abs(g['nodeSize'] * s - 1) > 1e-6:
                item(self.model, self.scene['source'], 'approx', 'Pathfinding grid node %.3g m differs from the map cell; import with --scale %.6g '
                     'for one cell per node.' % (g['nodeSize'], 1 / g['nodeSize']), key='grid-scale')
        if self.markers:
            imported['markers'] = [self._marker_out(m, to_cell) for m in self.markers]
        if imported:
            data['imported'] = imported

    @staticmethod
    def _marker_out(m, to_cell):
        out = {k: v for k, v in m.items() if k not in ('position', 'polygon', 'quaternion')}
        cx, cz = to_cell(m['position'][0], m['position'][2])
        out['pos'] = [round(cx, 3), round(cz, 3)]
        if m.get('polygon'):
            out['polygon'] = [[round(a, 3), round(b, 3)] for a, b in (to_cell(x, z) for x, z in m['polygon'])]
        return out

    def _tile(self, x, uz):
        return next((t for t in self.tiles if t.contains(x, uz)), None) or min(self.tiles, key=lambda t: t.dist(x, uz))

    @staticmethod
    def _folded(tile):
        """Tile alphamap folded onto the palette: RGBA8 per texel, weights summing to 255 (layer 0 where empty)."""
        t, n = tile.t, tile.t.alpha_res
        if not n or not t.alpha:
            return 1, bytes([255, 0, 0, 0])
        acc = [[0] * (n * n) for _ in range(4)]
        for li, weights in enumerate(t.alpha):
            slot = acc[tile.fold[li]]
            acc[tile.fold[li]] = [a + b for a, b in zip(slot, weights)]
        out = bytearray(4 * n * n)
        for p, (a, b, c, d) in enumerate(zip(*acc)):
            tot = a + b + c + d
            if not tot:
                out[4 * p] = 255
                continue
            w = [a * 255 // tot, b * 255 // tot, c * 255 // tot, d * 255 // tot]
            m = max(range(4), key=lambda q: (a, b, c, d)[q])
            w[m] += 255 - sum(w)
            out[4 * p:4 * p + 4] = bytes(w)
        return n, bytes(out)

    def _terrain(self, data, writer, rel_base, ox, oz, width, height, palette_size, imported):
        s = self.scale
        per_cell = max((t.t.res - 1) / (t.sx * s) for t in self.tiles)
        k = int(max(1, min(4, math.floor(per_cell + 1e-6))))
        while k > 1 and (width * k + 1) * (height * k + 1) > MAX_SAMPLES:
            k -= 1
        sw, sh = width * k + 1, height * k + 1
        heights = array('h', bytes(2 * sw * sh))
        splat = bytearray(4 * sw * sh)
        covered = bytearray(sw * sh)
        for tile in self.tiles:
            t, res = tile.t, tile.t.res
            n, folded = self._folded(tile)
            hgt = t.heights
            # sample index ranges inside this tile (engine z = -Unity z)
            i0 = max(0, math.ceil(((tile.x0 * s) - ox) * k - 1e-9))
            i1 = min(sw - 1, math.floor(((tile.x0 + tile.sx) * s - ox) * k + 1e-9))
            j0 = max(0, math.ceil((-(tile.z0 + tile.sz) * s - oz) * k - 1e-9))
            j1 = min(sh - 1, math.floor((-tile.z0 * s - oz) * k + 1e-9))
            cols = []
            for i in range(i0, i1 + 1):
                u = min(max(((i / k + ox) / s - tile.x0) / tile.sx, 0.0), 1.0)
                fx = u * (res - 1)
                xi = min(int(fx), res - 2)
                cols.append((i, xi, fx - xi, 4 * min(int(u * n), n - 1)))
            hs = tile.sy * s * HEIGHT_UNIT / uterrain_max()
            base = tile.y0 * s * HEIGHT_UNIT
            for j in range(j0, j1 + 1):
                v = min(max((-((j / k + oz) / s) - tile.z0) / tile.sz, 0.0), 1.0)
                fz = v * (res - 1)
                zi = min(int(fz), res - 2)
                tz = fz - zi
                r0, r1 = zi * res, (zi + 1) * res
                arow = 4 * n * min(int(v * n), n - 1)
                orow = j * sw
                for i, xi, tx, ax in cols:
                    a0 = hgt[r0 + xi] + (hgt[r0 + xi + 1] - hgt[r0 + xi]) * tx
                    a1 = hgt[r1 + xi] + (hgt[r1 + xi + 1] - hgt[r1 + xi]) * tx
                    y = int(round(base + (a0 + (a1 - a0) * tz) * hs))
                    heights[orow + i] = -32768 if y < -32768 else 32767 if y > 32767 else y
                    o = 4 * (orow + i)
                    q = arow + ax
                    splat[o:o + 4] = folded[q:q + 4]
                    covered[orow + i] = 1
        if not all(covered):  # samples outside every tile: nearest tile edge
            for idx in (q for q, c in enumerate(covered) if not c):
                j, i = divmod(idx, sw)
                x, uz = (i / k + ox) / s, -((j / k + oz) / s)
                tile = self._tile(x, uz)
                u = min(max((x - tile.x0) / tile.sx, 0.0), 1.0)
                v = min(max((uz - tile.z0) / tile.sz, 0.0), 1.0)
                y = int(round((tile.y0 + tile.t.height01(u, v) * tile.sy) * s * HEIGHT_UNIT))
                heights[idx] = max(-32768, min(32767, y))
                splat[4 * idx] = 255
        corners = array('h', (heights[(j * k) * sw + i * k] for j in range(height + 1) for i in range(width + 1)))
        writer.write(rel_base + '.r16', _le(corners))
        writer.write(rel_base + '.detail.r16', _le(heights))
        writer.write(rel_base + '.detail.splat', bytes(splat))
        t = data.setdefault('terrain', {})
        t['cornersFile'] = rel_base + '.r16'
        t['detail'] = dict(scale=k, file=rel_base + '.detail.r16', splat=rel_base + '.detail.splat')
        imported['terrain'] = dict(tiles=[dict(source=r['data'], position=r['position']) for r in self.scene['terrains']], detailScale=k,
                                   minHeight=min(heights) / HEIGHT_UNIT, maxHeight=max(heights) / HEIGHT_UNIT)
        item(self.model, self.scene['source'], 'approx', 'Terrain tiles merged into one heightfield (corners + detail k=%d, %d splat layers); '
             'visual only unless rules terrain.model is "heightfield".' % (k, palette_size), rel_base + '.r16', key='terrain-map')

    def _trees(self, data, writer, rel_base, ox, oz, types, imported):
        s, trees, details = self.scale, 0, []
        for ti, tile in enumerate(self.tiles):
            t = tile.t
            for inst in t.trees:
                p = int(inst.get('prototype', 0))
                proto = t.tree_prototypes[p] if 0 <= p < len(t.tree_prototypes) else {}
                tid = 'terrain_tree_' + ident(proto.get('prefab') or 'tree%d' % p)
                if tid not in types:
                    ty = dict(name=Path(proto.get('prefab') or 'Tree').stem, ported=True, invulnerable=True, health={'max': 1}, view={'shape': 'tree'})
                    if proto.get('model'):
                        ty['view'] = dict(model='content/' + proto['model'], scale=s, fit=False)
                    types[tid] = ty
                x = tile.x0 + inst['position'][0] * tile.sx
                z = tile.z0 + inst['position'][2] * tile.sz
                data['entities'].append(dict(type=tid, owner=0, pos=[round(x * s - ox, 4), round(-z * s - oz, 4)]))
                trees += 1
            for pi, grid in enumerate(t.detail):
                n = t.detail_res
                # PNG row 0 = smallest map z = largest Unity z
                img = b''.join(grid[(n - 1 - r) * n:(n - r) * n] for r in range(n))
                rel = '%s.tile%d.detail%d.png' % (rel_base, ti, pi)
                writer.write(rel, _png_gray(n, n, img))
                x0, z0 = tile.x0 * s - ox, -(tile.z0 + tile.sz) * s - oz
                details.append(dict(file=rel, prototype=t.detail_prototypes[pi],
                                    rect=[round(x0, 3), round(z0, 3), round(tile.sx * s, 3), round(tile.sz * s, 3)]))
        if trees:
            imported['trees'] = trees
            item(self.model, self.scene['source'], 'approx', '%d terrain trees placed as invulnerable neutral entities; per-instance '
                 'rotation, width/height scale and colour are dropped.' % trees, key='trees')
        if details:
            imported['details'] = details
            item(self.model, self.scene['source'], 'missing', 'Terrain detail (grass) density exported as PNG per prototype; the engine has '
                 'no grass / detail-mesh renderer.', key='details')
