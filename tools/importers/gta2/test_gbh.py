#!/usr/bin/env python3
"""Self-test of tools/importers/lib/gbh.py on synthetic files (no game data needed).

    python3 tools/importers/gta2/test_gbh.py
"""
import os
import struct
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'lib'))
import gbh  # noqa: E402

fails = 0


def check(cond, what):
    global fails
    if not cond:
        fails += 1
        print('FAIL', what)


def test_map():
    road = (0, 0, 0, 0, 5, 0b0101, 1)                       # lid tile 5, arrows, ground road
    wall = (1024 | 7, 1024 | 7, 1024 | 7, 1024 | 7, 9 | (2 << 10), 0, 2)
    ramp = (0, 0, 0, 0, 6, 0, 1 | (2 << 2))                 # 26 deg up, high half
    blocks = [(0,) * 7, road, wall, ramp]
    cols = {(3, 4): (3, 0, [1, 2, 2]), (10, 20): (4, 2, [1, 3]), (255, 255): (1, 0, [1])}
    data = gbh.build_gmp(cols, blocks, zones=[(14, 1, 2, 30, 40, 'Zaibatsu')], objects=[(128 * 5 + 64, 128 * 6, 64, 9)],
                         lights=[(0xFFFF8040, 128, 256, 384, 512, 200, 0, 0, 0)])
    m = gbh.read_gmp(data)
    check(len(m.blocks) == 4, 'block count')
    check(m.column(3, 4) == (1, 2, 2, None, None, None, None, None), 'column 3,4')
    check(m.column(10, 20) == (None, None, 1, 3, None, None, None, None), 'column with offset')
    check(m.column(0, 0) == (None,) * 8, 'empty column')
    check(m.top(3, 4)[0] == 2 and gbh.ground(m.top(3, 4)[1]) == 'pavement', 'top of 3,4')
    check(m.top(0, 0) == (-1, None), 'top of empty')
    check(gbh.slope_kind(m.block_at(10, 20, 3)) == ('ramp', 'up', 1, 2), 'slope kind')
    check(gbh.side(m.blocks[2].left) == gbh.Side(7, True, False, False, False, 0), 'side bits')
    check(gbh.lid(m.blocks[2].lid) == gbh.Lid(9, 2, False, False, 0), 'lid bits')
    check(m.zones == [gbh.Zone('gang', 1, 2, 30, 40, 'Zaibatsu')], 'zones')
    check(m.objects == [gbh.MapObject(5.5, 6.0, 90.0, 9)], 'objects')
    check(m.lights[0].radius == 4.0 and m.lights[0].z == 3.0, 'lights')
    st = m.stats()
    check(st['topGround']['road'] == 2 and st['topGround']['pavement'] == 1 and st['emptyColumns'] == 65533, 'stats')
    for bad, what in ((b'XXXX' + data[4:], 'magic'), (data[:4] + b'\x01\x00' + data[6:], 'version'), (data[:-5], 'truncated')):
        try:
            gbh.read_gmp(bad)
            check(False, 'accepted bad file: ' + what)
        except gbh.FormatError:
            pass


def test_slopes():
    kinds = [gbh.slope_kind(gbh.Block(0, 0, 0, 0, 0, 0, t << 2)) for t in range(64)]
    check(kinds[1] == ('ramp', 'up', 0, 2) and kinds[8] == ('ramp', 'right', 1, 2), '26 deg')
    check(kinds[9] == ('ramp', 'up', 0, 8) and kinds[40] == ('ramp', 'right', 7, 8), '7 deg')
    check(kinds[41] == ('ramp', 'up', 0, 1) and kinds[44] == ('ramp', 'right', 0, 1), '45 deg')
    check(kinds[45][0] == 'diagonal' and kinds[53] == ('partial', 'left', 0, 0) and kinds[61][1] == 'centre', 'partial')
    check(kinds[63][0] == 'slope_above' and kinds[0][0] == 'none', 'markers')


def test_style():
    palx = struct.pack('<16384H', *([1, 2] + [0] * 16382))          # virtual 0 -> physical 1, 1 -> 2
    ppal = bytearray(65536)
    for p, (r, g, b) in ((1, (10, 20, 30)), (2, (200, 100, 50))):
        for i in range(256):
            o = (i * 64 + p) * 4
            ppal[o:o + 4] = bytes((b, g, r, 0))
    tiles = bytearray(65536)
    for y in range(64):
        for x in range(32):
            tiles[y * 256 + x] = 3                                  # tile 0: left half filled
    sprg = bytearray(65536)
    for y in range(2):
        for x in range(3):
            sprg[(5 + y) * 256 + 7 + x] = 9
    sprx = struct.pack('<IBBH', 5 * 256 + 7, 3, 2, 0)
    cari = struct.pack('<8B4b2B', 12, 1, 28, 60, 2, 3, 0, 4, 10, -12, 5, -6, 1, 0) + bytes((7, 8)) + bytes((1,)) + struct.pack('<2b', -8, 3)
    data = gbh._file(b'GBST', 700, [('PALX', palx), ('PPAL', bytes(ppal)), ('PALB', struct.pack('<8H', 1, 1, 0, 0, 0, 0, 0, 0)),
                                    ('TILE', bytes(tiles)), ('SPRG', bytes(sprg)), ('SPRX', sprx),
                                    ('SPRB', struct.pack('<6H', 1, 0, 0, 0, 0, 0)), ('CARI', cari), ('OBJI', bytes((3, 2)))])
    s = gbh.read_sty(data)
    px = s.tile_rgba(0)
    check(px[:4] == bytes((10, 20, 30, 255)) and px[40 * 4:40 * 4 + 4] == bytes(4), 'tile pixels')
    check(s.tile_colour(0) == (10, 20, 30, 0.5), 'tile colour')
    check(s.sprite_rgba(0) == (3, 2, bytes((200, 100, 50, 255)) * 6), 'sprite pixels')
    c = s.cars[0]
    check((c.model, c.w, c.h, c.remaps, c.doors, c.rear_wheel) == (12, 28, 60, (7, 8), ((-8, 3),), -12), 'car info')
    check(s.objects == [(3, 2)] and s.stats()['tiles'] == 16, 'objects, stats')


test_map()
test_slopes()
test_style()
print('gbh self-test:', 'OK' if not fails else '%d FAILED' % fails)
sys.exit(1 if fails else 0)
