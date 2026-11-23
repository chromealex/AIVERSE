"""Versioned read-only header of Warcraft III W3I map info.

Deliberately stops at the tileset byte. Remaining player/force/fog/etc records
are retained verbatim in the source .w3i rather than wrongly decoded.
"""
import math
from formats import Reader


def header(data):
    r = Reader(data)
    version = r.u32()
    if version not in (18, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 39):
        raise ValueError('unsupported W3I version %d (header supports 18/23-33/39)' % version)
    saves, editor_version = r.unpack('II')
    game_version = list(r.unpack('IIII')) if version >= 27 else None
    name = r.cstring(109)
    author = r.cstring(97)
    description = r.cstring(769)
    recommended = r.cstring(49)
    camera = list(r.unpack('ffffffff'))
    if not all(math.isfinite(x) for x in camera):
        raise ValueError('non-finite W3I camera bounds')
    complements = list(r.unpack('iiii')) if version >= 14 else [0, 0, 0, 0]
    width, height, flags = r.unpack('III')
    if not 1 <= width <= 2048 or not 1 <= height <= 2048:
        raise ValueError('W3I playable size is invalid')
    tileset = r.take(1).decode('ascii', 'replace')
    return {'format': 'warcraft3.w3i', 'version': version,
            'mapVersion': saves, 'editorVersion': editor_version,
            'gameVersion': game_version, 'name': name, 'author': author,
            'description': description, 'recommendedPlayers': recommended,
            'cameraBoundsWar3': camera, 'cameraComplements': complements,
            'playableWidth': width, 'playableHeight': height,
            'flagsRaw': flags, 'tileset': tileset,
            'parsedHeaderBytes': r.pos, 'unparsedTailBytes': len(data) - r.pos,
            'parsingScope': 'header-only'}
