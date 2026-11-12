"""Readers for the GTA2 data formats (GBH): .gmp maps and .sty style files. Standard library only.

Written from the published format descriptions ("GTA2 Map Format", "GTA2 Style Format"); no game data
is embedded here. Importers read the user's own copy and mark everything derived from it as external.

    m = gbh.read_gmp(open('wil.gmp', 'rb').read())
    m.column(x, y)        -> 8 block ids, bottom to top (None = empty air)
    m.blocks[id]          -> Block(left, right, top, bottom, lid, arrows, slope)
    m.zones, m.objects, m.lights, m.anims

    s = gbh.read_sty(open('wil.sty', 'rb').read())
    s.tile_rgba(n)        -> 64x64 RGBA bytes
    s.sprite_rgba(n)      -> (w, h, RGBA bytes)
    s.cars                -> car descriptions (size, sprite, doors, ...)

A side word: bits 0-9 tile, 10 wall (blocks cars / peds), 11 bullet wall, 12 flat (transparent),
13 flip, 14-15 rotation (x 90 deg). A lid word: bits 0-9 tile, 10-11 light level, 12 flat, 13 flip,
14-15 rotation. `slope`: bits 0-1 ground (GROUND), bits 2-7 slope type (see slope_kind).
"""
import struct
from collections import namedtuple

MAP_SIZE = 256
MAP_LEVELS = 8
TILE = 64
GROUND = ('air', 'road', 'pavement', 'field')
ZONE_TYPES = {0: 'general', 1: 'navigation', 2: 'traffic_light', 5: 'arrow_blocker', 6: 'railway_station',
              7: 'bus_stop', 8: 'general_trigger', 10: 'information', 11: 'railway_station_entry',
              12: 'railway_station_exit', 13: 'railway_stop', 14: 'gang', 15: 'local_navigation',
              16: 'restart', 20: 'arrest_restart'}

Block = namedtuple('Block', 'left right top bottom lid arrows slope')
Zone = namedtuple('Zone', 'type x y w h name')
MapObject = namedtuple('MapObject', 'x y rotation type')       # x, y in blocks (float), rotation in degrees
Light = namedtuple('Light', 'argb x y z radius intensity shape on_time off_time')
Anim = namedtuple('Anim', 'base frame_rate repeat tiles')
Car = namedtuple('Car', 'model sprite w h remaps passengers wreck rating front_wheel rear_wheel '
                        'front_window rear_window flags flags2 doors')
Side = namedtuple('Side', 'tile wall bullet_wall flat flip rotation')
Lid = namedtuple('Lid', 'tile light flat flip rotation')


class FormatError(Exception):
    pass


def side(word):
    return Side(word & 1023, bool(word & 1024), bool(word & 2048), bool(word & 4096), bool(word & 8192), word >> 14)


def lid(word):
    return Lid(word & 1023, (word >> 10) & 3, bool(word & 4096), bool(word & 8192), word >> 14)


def ground(block):
    return GROUND[block.slope & 3]


def slope_kind(block):
    """-> (kind, direction, step, steps). kind: 'none', 'ramp' (steps 2 = 26 deg, 8 = 7 deg, 1 = 45 deg),
    'diagonal' (half block, direction = the corner it faces), 'partial' (thin wall / corner / centre),
    'slope_above' (marker). direction: 'up' (towards y-), 'down', 'left', 'right' or a corner name."""
    t = block.slope >> 2
    d4 = ('up', 'down', 'left', 'right')
    if t == 0:
        return ('none', None, 0, 0)
    if t <= 8:
        return ('ramp', d4[(t - 1) // 2], (t - 1) % 2, 2)
    if t <= 40:
        return ('ramp', d4[(t - 9) // 8], (t - 9) % 8, 8)
    if t <= 44:
        return ('ramp', d4[t - 41], 0, 1)
    if t <= 48:
        return ('diagonal', ('up_left', 'up_right', 'down_left', 'down_right')[t - 45], 0, 0)
    if t <= 52:
        return ('diagonal3', ('up_left', 'up_right', 'down_left', 'down_right')[t - 49], 0, 0)
    if t <= 56:
        return ('partial', ('left', 'right', 'top', 'bottom')[t - 53], 0, 0)
    if t <= 60:
        return ('partial', ('top_left', 'top_right', 'bottom_right', 'bottom_left')[t - 57], 0, 0)
    if t == 61:
        return ('partial', 'centre', 0, 0)
    if t == 63:
        return ('slope_above', None, 0, 0)
    return ('none', None, 0, 0)


def chunks(data, magic, versions):
    """-> {tag: bytes}; checks the 6-byte file header."""
    if len(data) < 6 or data[:4] != magic:
        raise FormatError('not a %s file' % magic.decode())
    version = struct.unpack_from('<H', data, 4)[0]
    if version not in versions:
        raise FormatError('%s version %d is not supported (expected %s)' % (magic.decode(), version, versions))
    out, pos = {}, 6
    while pos + 8 <= len(data):
        tag = data[pos:pos + 4].decode('latin-1')
        size = struct.unpack_from('<I', data, pos + 4)[0]
        if pos + 8 + size > len(data):
            raise FormatError('chunk %s at %d runs past the end of the file (%d bytes missing)' %
                              (tag, pos, pos + 8 + size - len(data)))
        out[tag] = data[pos + 8:pos + 8 + size]
        pos += 8 + size
    return out


# ------------------------------------------------------------------ map

class GmpMap:
    def __init__(self):
        self.blocks = []        # Block
        self._cols = None       # 256*256 tuples of 8 block ids (None = air)
        self.zones, self.objects, self.lights, self.anims = [], [], [], []
        self.chunks = []        # tags present in the file

    def column(self, x, y):
        return self._cols[y * MAP_SIZE + x]

    def block_at(self, x, y, z):
        i = self.column(x, y)[z]
        return None if i is None else self.blocks[i]

    def top(self, x, y):
        """-> (z, Block) of the highest block that has a lid tile, or (-1, None)."""
        col = self.column(x, y)
        for z in range(MAP_LEVELS - 1, -1, -1):
            if col[z] is not None and self.blocks[col[z]].lid & 1023:
                return z, self.blocks[col[z]]
        return -1, None

    def stats(self):
        g = {k: 0 for k in GROUND}
        heights = [0] * (MAP_LEVELS + 1)
        for y in range(MAP_SIZE):
            for x in range(MAP_SIZE):
                z, b = self.top(x, y)
                heights[z + 1] += 1
                if b is not None:
                    g[ground(b)] += 1
        return {'blocks': len(self.blocks), 'topGround': g, 'topLevel': heights[1:], 'emptyColumns': heights[0],
                'zones': len(self.zones), 'objects': len(self.objects), 'lights': len(self.lights),
                'anims': len(self.anims), 'chunks': self.chunks}


def _read_blocks(buf, pos, count):
    if pos + count * 12 > len(buf):
        raise FormatError('map: block table is truncated')
    return [Block(*struct.unpack_from('<5H2B', buf, pos + i * 12)) for i in range(count)]


def _compressed(buf, wide):
    """DMAP (32-bit words) / CMAP (16-bit words): base[256][256], column words, unique blocks."""
    w, fmt = (4, 'I') if wide else (2, 'H')
    n = MAP_SIZE * MAP_SIZE
    if len(buf) < n * w + w:
        raise FormatError('map: compressed map chunk is truncated')
    base = struct.unpack_from('<%d%s' % (n, fmt), buf, 0)
    words = struct.unpack_from('<' + fmt, buf, n * w)[0]
    cpos = n * w + w
    if cpos + words * w + w > len(buf):
        raise FormatError('map: column data is truncated')
    col = struct.unpack_from('<%d%s' % (words, fmt), buf, cpos)
    bpos = cpos + words * w
    nblocks = struct.unpack_from('<' + fmt, buf, bpos)[0]
    blocks = _read_blocks(buf, bpos + w, nblocks)
    cache, cols = {}, []
    for b in base:
        c = cache.get(b)
        if c is None:
            if b >= words:
                raise FormatError('map: column offset %d is outside the column data (%d words)' % (b, words))
            height, offset = col[b] & 255, (col[b] >> 8) & 255
            if height > MAP_LEVELS or offset > height or b + 1 + height - offset > words:
                raise FormatError('map: bad column at word %d (height %d, offset %d)' % (b, height, offset))
            ids = [None] * MAP_LEVELS
            for k in range(height - offset):
                i = col[b + 1 + k]
                if i >= nblocks:
                    raise FormatError('map: block id %d out of range (%d blocks)' % (i, nblocks))
                ids[offset + k] = i
            c = cache[b] = tuple(ids)
        cols.append(c)
    return blocks, cols


def _uncompressed(buf):
    n = MAP_SIZE * MAP_SIZE
    blocks = _read_blocks(buf, 0, n * MAP_LEVELS)
    index, uniq = {}, []
    cols = [[None] * MAP_LEVELS for _ in range(n)]
    for z in range(MAP_LEVELS):
        for i in range(n):
            b = blocks[z * n + i]
            if not any(b):
                continue
            k = index.get(b)
            if k is None:
                k = index[b] = len(uniq)
                uniq.append(b)
            cols[i][z] = k
    return uniq, [tuple(c) for c in cols]


def read_gmp(data):
    ch = chunks(data, b'GBMP', (500,))
    m = GmpMap()
    m.chunks = sorted(ch)
    if 'DMAP' in ch:
        m.blocks, m._cols = _compressed(ch['DMAP'], True)
    elif 'CMAP' in ch:
        m.blocks, m._cols = _compressed(ch['CMAP'], False)
    elif 'UMAP' in ch:
        m.blocks, m._cols = _uncompressed(ch['UMAP'])
    else:
        raise FormatError('map: no DMAP / CMAP / UMAP chunk')
    buf, pos = ch.get('ZONE', b''), 0
    while pos + 6 <= len(buf):
        t, x, y, w, h, n = struct.unpack_from('<6B', buf, pos)
        m.zones.append(Zone(ZONE_TYPES.get(t, 'type%d' % t), x, y, w, h, buf[pos + 6:pos + 6 + n].decode('latin-1')))
        pos += 6 + n
    buf = ch.get('MOBJ', b'')
    for i in range(len(buf) // 6):
        x, y, rot, typ = struct.unpack_from('<HHBB', buf, i * 6)
        m.objects.append(MapObject(x / 128.0, y / 128.0, rot * 360.0 / 256.0, typ))
    buf = ch.get('LGHT', b'')
    for i in range(len(buf) // 16):
        argb, x, y, z, r, inten, shape, on, off = struct.unpack_from('<I4H4B', buf, i * 16)
        m.lights.append(Light(argb, x / 128.0, y / 128.0, z / 128.0, r / 128.0, inten, shape, on, off))
    buf, pos = ch.get('ANIM', b''), 0
    while pos + 6 <= len(buf):
        base, rate, rep, n = struct.unpack_from('<HBBB', buf, pos)
        if pos + 6 + n * 2 > len(buf):
            break
        m.anims.append(Anim(base, rate, rep, struct.unpack_from('<%dH' % n, buf, pos + 6)))
        pos += 6 + n * 2
    return m


# ------------------------------------------------------------------ style

class Style:
    def __init__(self, ch):
        self.chunks = sorted(ch)
        self._tile = ch.get('TILE', b'')
        self._sprg = ch.get('SPRG', b'')
        self._ppal = ch.get('PPAL', b'')
        palx = ch.get('PALX', b'')
        self.palx = struct.unpack_from('<%dH' % (len(palx) // 2), palx, 0)
        names = ('tile', 'sprite', 'car_remap', 'ped_remap', 'code_obj_remap', 'map_obj_remap', 'user_remap', 'font_remap')
        counts = struct.unpack_from('<8H', ch['PALB'], 0) if len(ch.get('PALB', b'')) >= 16 else (0,) * 8
        self.pal_base, acc = {}, 0
        for n, c in zip(names, counts):
            self.pal_base[n] = acc
            acc += c
        names = ('car', 'ped', 'code_obj', 'map_obj', 'user', 'font')
        counts = struct.unpack_from('<6H', ch['SPRB'], 0) if len(ch.get('SPRB', b'')) >= 12 else (0,) * 6
        self.sprite_base, acc = {}, 0
        for n, c in zip(names, counts):
            self.sprite_base[n] = acc
            acc += c
        sprx = ch.get('SPRX', b'')
        self.sprites = [struct.unpack_from('<IBB', sprx, i * 8) for i in range(len(sprx) // 8)]   # (ptr, w, h)
        self.tile_count = len(self._tile) // (TILE * TILE)
        self.cars = []
        buf, pos = ch.get('CARI', b''), 0
        while pos + 15 <= len(buf):
            model, sprite, w, h, nremap, passengers, wreck, rating = struct.unpack_from('<8B', buf, pos)
            fw, rw, fwin, rwin = struct.unpack_from('<4b', buf, pos + 8)
            flags, flags2 = struct.unpack_from('<2B', buf, pos + 12)
            pos += 14
            remaps = tuple(buf[pos:pos + nremap])
            pos += nremap
            if pos >= len(buf):
                break
            ndoors = buf[pos]
            pos += 1
            doors = tuple(struct.unpack_from('<2b', buf, pos + i * 2) for i in range(ndoors) if pos + i * 2 + 2 <= len(buf))
            pos += ndoors * 2
            self.cars.append(Car(model, sprite, w, h, remaps, passengers, wreck, rating, fw, rw, fwin, rwin, flags, flags2, doors))
        obji = ch.get('OBJI', b'')
        self.objects = [struct.unpack_from('<BB', obji, i * 2) for i in range(len(obji) // 2)]         # (model, sprites)

    def palette(self, virtual):
        """-> 256 (r, g, b) tuples of a virtual palette."""
        phys = self.palx[virtual] if virtual < len(self.palx) else 0
        page, p = phys // 64, phys % 64
        out = []
        for i in range(256):
            o = page * 65536 + (i * 64 + p) * 4
            if o + 4 > len(self._ppal):
                out.append((0, 0, 0))
            else:
                out.append((self._ppal[o + 2], self._ppal[o + 1], self._ppal[o]))
        return out

    @staticmethod
    def _rgba(indices, pal):
        out = bytearray(len(indices) * 4)
        for k, i in enumerate(indices):
            if i:
                r, g, b = pal[i]
                out[k * 4:k * 4 + 4] = bytes((r, g, b, 255))
        return bytes(out)

    def tile_indices(self, n):
        page, tx, ty = n // 16, (n % 4) * TILE, ((n % 16) // 4) * TILE
        rows = []
        for y in range(TILE):
            o = page * 65536 + (ty + y) * 256 + tx
            rows.append(self._tile[o:o + TILE])
        px = b''.join(rows)
        if len(px) != TILE * TILE:
            raise FormatError('style: tile %d is outside the TILE chunk' % n)
        return px

    def tile_rgba(self, n):
        return self._rgba(self.tile_indices(n), self.palette(self.pal_base['tile'] + n))

    def tile_colour(self, n):
        """Average (r, g, b, coverage 0..1) of a tile: enough to tell grass from water from asphalt."""
        px, pal = self.tile_indices(n), self.palette(self.pal_base['tile'] + n)
        r = g = b = c = 0
        for i in px:
            if i:
                pr, pg, pb = pal[i]
                r, g, b, c = r + pr, g + pg, b + pb, c + 1
        return (r // c, g // c, b // c, c / float(TILE * TILE)) if c else (0, 0, 0, 0.0)

    def sprite_rgba(self, n, remap=None):
        ptr, w, h = self.sprites[n]
        page, x0, y0 = ptr // 65536, ptr % 256, (ptr % 65536) // 256
        rows = []
        for y in range(h):
            o = page * 65536 + (y0 + y) * 256 + x0
            rows.append(self._sprg[o:o + w])
        px = b''.join(rows)
        if len(px) != w * h:
            raise FormatError('style: sprite %d is outside the SPRG chunk' % n)
        virtual = self.pal_base['sprite'] + n if remap is None else remap
        return w, h, self._rgba(px, self.palette(virtual))

    def stats(self):
        return {'tiles': self.tile_count, 'sprites': len(self.sprites), 'spriteBase': self.sprite_base,
                'palettes': len(self._ppal) // 1024, 'cars': len(self.cars), 'objects': len(self.objects),
                'chunks': self.chunks}


def read_sty(data):
    return Style(chunks(data, b'GBST', (700,)))


# ------------------------------------------------------------------ writers (tests and tools only)

def _file(magic, version, parts):
    return magic + struct.pack('<H', version) + b''.join(
        tag.encode('latin-1') + struct.pack('<I', len(body)) + body for tag, body in parts)


def build_gmp(columns, blocks, zones=(), objects=(), lights=(), default=(0, 0, ())):
    """Writes a DMAP map. columns: {(x, y): (height, offset, [block ids])}; others use `default`."""
    words, where, base = [], {}, []
    for y in range(MAP_SIZE):
        for x in range(MAP_SIZE):
            height, offset, ids = columns.get((x, y), default)
            key = (height, offset, tuple(ids))
            if key not in where:
                where[key] = len(words)
                words.append(height | (offset << 8))
                words.extend(ids)
            base.append(where[key])
    body = struct.pack('<%dI' % len(base), *base) + struct.pack('<I', len(words)) + struct.pack('<%dI' % len(words), *words)
    body += struct.pack('<I', len(blocks)) + b''.join(struct.pack('<5H2B', *b) for b in blocks)
    parts = [('DMAP', body)]
    if zones:
        parts.append(('ZONE', b''.join(struct.pack('<6B', t, x, y, w, h, len(n)) + n.encode('latin-1') for t, x, y, w, h, n in zones)))
    if objects:
        parts.append(('MOBJ', b''.join(struct.pack('<HHBB', *o) for o in objects)))
    if lights:
        parts.append(('LGHT', b''.join(struct.pack('<I4H4B', *l) for l in lights)))
    return _file(b'GBMP', 500, parts)
