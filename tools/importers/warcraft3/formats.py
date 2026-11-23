"""Read-only, bounds-checked Warcraft III map payload decoders.

Outputs neutral JSON data, not runtime World/ECS objects. Gameplay and TerraWeave
bridges must consume these explicitly; source structures remain lossless on disk.
"""
import base64
import re
import struct

MAX_POINTS = 1024 * 1024


class Reader:
    def __init__(self, data):
        self.data = data
        self.pos = 0

    def take(self, n):
        if n < 0 or self.pos + n > len(self.data):
            raise ValueError('truncated Warcraft III payload at byte %d' % self.pos)
        result = self.data[self.pos:self.pos + n]
        self.pos += n
        return result

    def unpack(self, fmt):
        return struct.unpack('<' + fmt, self.take(struct.calcsize('<' + fmt)))

    def u32(self):
        return self.unpack('I')[0]

    def tag(self):
        return self.take(4).decode('ascii', 'replace')

    def cstring(self, max_size=4096):
        end = self.data.find(b'\0', self.pos, min(len(self.data), self.pos + max_size + 1))
        if end < 0:
            raise ValueError('unterminated Warcraft III string at byte %d' % self.pos)
        result = self.data[self.pos:end].decode('utf-8', 'replace')
        self.pos = end + 1
        return result


def terrain(data):
    r = Reader(data)
    if r.tag() != 'W3E!':
        raise ValueError('not a Warcraft III W3E terrain')
    version = r.u32()
    if version not in (11, 12):
        raise ValueError('unsupported W3E version %d (expected 11 or 12)' % version)
    tileset = r.take(1).decode('ascii', 'replace')
    custom = r.u32()
    n_ground = r.u32()
    if n_ground > 64:
        raise ValueError('too many ground tile palettes')
    ground = [r.tag() for _ in range(n_ground)]
    n_cliff = r.u32()
    if n_cliff > 64:
        raise ValueError('too many cliff tile palettes')
    cliffs = [r.tag() for _ in range(n_cliff)]
    width, height = r.unpack('II')
    off_x, off_y = r.unpack('ff')
    if width < 2 or height < 2 or width * height > MAX_POINTS:
        raise ValueError('invalid W3E tilepoint dimensions')
    stride = 7 if version == 11 else 8
    if len(data) - r.pos != width * height * stride:
        raise ValueError('W3E tilepoint data length mismatch')
    vertices = []
    for _ in range(width * height):
        raw_ground, raw_water = r.unpack('HH')
        packed = r.unpack('B' if version == 11 else 'H')[0]
        variation, cliff_layer = r.unpack('BB')
        layer = cliff_layer & 0x0f
        ground_index = packed & (0x0f if version == 11 else 0x3f)
        flags_raw = packed & (0xf0 if version == 11 else 0xffc0)
        flags = flags_raw if version == 11 else (flags_raw >> 2) & 0xf0
        # The high water bits contain the boundary flags. Preserve original raw word.
        elevation = (raw_ground - 0x2000 + (layer - 2) * 0x200) / 4
        water = ((raw_water & 0x3fff) - 0x2000) / 4 - 89.6
        vertices.append([elevation, round(water, 4), flags, ground_index,
                         variation, cliff_layer >> 4, layer, raw_water, flags_raw])
    return {
        'format': 'warcraft3.w3e', 'version': version, 'mainTileset': tileset,
        'customTilesets': bool(custom), 'groundTilesets': ground, 'cliffTilesets': cliffs,
        'width': width, 'height': height, 'offsetX': off_x, 'offsetY': off_y,
        'warcraftUnitsPerAiverseUnit': 128,
        'vertexSchema': ['heightWar3Units', 'waterHeightWar3Units', 'flagsClassic',
                         'groundTilesetIndex', 'variationRaw', 'cliffTilesetIndex',
                         'cliffLayer', 'waterWordRaw', 'flagsWordRaw'],
        'verticesRowMajorBottomLeft': vertices,
    }


def pathing(data):
    r = Reader(data)
    if r.tag() != 'MP3W':
        raise ValueError('not a Warcraft III WPM pathing map')
    version, width, height = r.unpack('III')
    if version != 0 or width <= 0 or height <= 0 or width * height > MAX_POINTS * 16:
        raise ValueError('invalid WPM version or dimensions')
    flags = r.take(width * height)
    if r.pos != len(data):
        raise ValueError('WPM trailing data')
    return {'format': 'warcraft3.wpm', 'version': version,
            'width': width, 'height': height,
            'flagMasks': {'noWalk': 2, 'noFly': 4, 'noBuild': 8, 'blight': 32, 'noWater': 64},
            'flagsBase64': base64.b64encode(flags).decode('ascii'),
            'noWalkCells': sum(bool(x & 2) for x in flags),
            'noBuildCells': sum(bool(x & 8) for x in flags)}


def strings(data):
    """Parse WTS values without losing embedded braces, comments or indentation."""
    text = data.decode('utf-8-sig', 'replace')
    lines = text.splitlines(keepends=True)
    header = re.compile(r'^\s*STRING\s+(\d+)\s*$', re.I)
    opening = re.compile(r'^\s*\{\s*$')
    closing = re.compile(r'^\s*\}\s*$')
    result = {}
    i = 0
    while i < len(lines):
        match = header.fullmatch(lines[i].rstrip('\r\n'))
        if not match:
            i += 1
            continue
        key = 'TRIGSTR_%03d' % int(match.group(1))
        if key in result:
            raise ValueError('duplicate WTS key ' + key)
        i += 1
        if i >= len(lines) or not opening.fullmatch(lines[i].rstrip('\r\n')):
            raise ValueError('missing opening WTS brace: ' + key)
        i += 1
        values = []
        while i < len(lines) and not closing.fullmatch(lines[i].rstrip('\r\n')):
            values.append(lines[i])
            i += 1
        if i >= len(lines):
            raise ValueError('unterminated WTS value: ' + key)
        result[key] = ''.join(values).rstrip('\r\n')
        i += 1
    return {'format': 'warcraft3.wts', 'strings': result}


def imported(data):
    r = Reader(data)
    version, count = r.unpack('II')
    if version not in (0, 1) or count > 100000:
        raise ValueError('invalid Warcraft III IMP table')
    result = []
    for _ in range(count):
        flag = r.take(1)[0]
        path = r.cstring(1024)
        result.append({'flag': flag, 'path': path})
    if r.pos != len(data):
        raise ValueError('IMP trailing data')
    return {'format': 'warcraft3.imp', 'version': version, 'entries': result}
