#!/usr/bin/env python3
"""Generate original terrain/arch fixtures for the asynchronous world runtime."""
import hashlib
import json
import struct
import zlib
from pathlib import Path

root = Path(__file__).resolve().parent / 'streamed'
(root / 'chunks').mkdir(parents=True, exist_ok=True)

def write(path, value):
    (root / path).write_bytes((json.dumps(value, indent=2) + '\n').encode('utf-8'))

def box(lo, hi):
    vertices = [[hi[0] if i & 1 else lo[0], hi[1] if i & 2 else lo[1], hi[2] if i & 4 else lo[2]] for i in range(8)]
    indices = [0,2,3,0,3,1,4,5,7,4,7,6,0,4,6,0,6,2,1,3,7,1,7,5,2,6,7,2,7,3,0,1,5,0,5,4]
    return vertices, indices

vertices, indices = [], []
for lo, hi in [([-8,0,-2],[-5,12,2]),([5,0,-2],[8,12,2]),([-5,9,-2],[5,12,2])]:
    v, idx = box(lo, hi)
    indices.extend(i + len(vertices) for i in idx)
    vertices.extend(v)
obj = 'mtllib arch.mtl\no OriginalArch\nusemtl stone\n'
obj += ''.join('v %s %s %s\n' % tuple(v) for v in vertices)
obj += ''.join('f %d %d %d\n' % tuple(i + 1 for i in indices[n:n+3]) for n in range(0,len(indices),3))
(root / 'arch.obj').write_bytes(obj.encode('utf-8'))
(root / 'arch.mtl').write_bytes(b'newmtl stone\nKd 0.62 0.47 0.31\n')
# Original small tiling ground texture, no external artwork.
def png_chunk(tag, data):
    return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff)
rows = bytearray()
for y in range(16):
    rows.append(0)
    for x in range(16):
        variation = ((x * 17 + y * 29 + x * y * 7) % 17) - 8
        rows.extend((100 + variation, 135 + variation, 67 + variation, 255))
image = b'\x89PNG\r\n\x1a\n' + png_chunk(b'IHDR', struct.pack('>IIBBBBB', 16, 16, 8, 6, 0, 0, 0))
image += png_chunk(b'IDAT', zlib.compress(bytes(rows))) + png_chunk(b'IEND', b'')
(root / 'ground.png').write_bytes(image)
chunks = []
for x in range(4):
    for z in range(4):
        height = lambda px, pz: (x * 128 + px) / 256 + (z * 128 + pz) / 512
        base = [32, height(32,64), 64]
        data = {'terrain': {'resolution': 9, 'material': {'color':[1,1,1], 'baseTexture':'ground.png'}, 'uvScale':0.2, 'heights': [height(px*16,pz*16) for pz in range(9) for px in range(9)]},
                'meshes': [{'file':'arch.obj','position':base}],
                'collision': {'vertices': [[p[i]+base[i] for i in range(3)] for p in vertices], 'indices':indices}}
        file = f'chunks/{x}_{z}.json'
        write(file, data)
        chunks.append({'cell':[x,0,z], 'file':file, 'reservedBytes':2097152})
assets = {p.relative_to(root).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(root.rglob('*')) if p.is_file() and (p.suffix in ('.obj','.mtl','.png') or p.parent.name=='chunks')}
world = {'async':True,'cellSize':128,'maxResidentBytes':41943040,'maxResidentChunks':20,'maxStagingBytes':8388608,'maxUploadBytes':4194304,'maxJobs':2,'assets':assets,'chunks':chunks}
write('map.json', {'players':[{'team':1},{'team':2,'bot':True}],'spawns':[[32,2,32],[420,5,420]],'colliders':[], 'world':world})
write('rules.json', {'fps':{'mode':'deathmatch','tickRate':60,'maxPlayers':2,'worldLimit':1024,'roundTicks':3600,'weapon':{'range':60}}})
write('game.json', {'id':'aiverse.streamed-world','name':'Streamed terrain and arches','module':'fps','genre':'fps','rules':'rules.json','maps':[{'file':'map.json'}]})
print(root)
