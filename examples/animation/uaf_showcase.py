#!/usr/bin/env python3
"""Make a standalone UAF-style demo with original golem assets and authored root trajectories."""
import json
from pathlib import Path
import shutil
import struct
import sys
root = Path(__file__).resolve().parents[2]
out = Path(sys.argv[1]).resolve()
if out.exists():
    raise SystemExit('Destination must not exist')
shutil.copytree(root / 'templates/showcase', out)
shutil.copytree(root / 'engine/assets', out / 'engine')
# The showcase clips are in-place. Add demo-authored root displacement tracks so the
# motion database can measure real clip trajectories; playback strips them from the pose.
p = out / 'models/golem.glb'
data = p.read_bytes()
json_len = struct.unpack_from('<I', data, 12)[0]
asset = json.loads(data[20:20+json_len])
offset = 20 + json_len
bin_len = struct.unpack_from('<I', data, offset)[0]
binary = bytearray(data[offset+8:offset+8+bin_len])
for animation in asset['animations']:
    speed = {'Walk': 1.0, 'Run': 3.0}.get(animation['name'])
    if speed is None:
        continue
    duration = max(asset['accessors'][sampler['input']]['max'][0] for sampler in animation['samplers'])
    def accessor(values, count, kind, minimum, maximum):
        while len(binary) % 4:
            binary.append(0)
        start = len(binary)
        binary.extend(struct.pack('<' + 'f'*len(values), *values))
        view = len(asset['bufferViews'])
        asset['bufferViews'].append({'buffer':0,'byteOffset':start,'byteLength':len(binary)-start})
        index = len(asset['accessors'])
        asset['accessors'].append({'bufferView':view,'componentType':5126,'count':count,'type':kind,'min':minimum,'max':maximum})
        return index
    times = accessor([0,duration],2,'SCALAR',[0],[duration])
    translations = accessor([0,0,0,speed*duration,0,0],2,'VEC3',[0,0,0],[speed*duration,0,0])
    sampler = len(animation['samplers'])
    animation['samplers'].append({'input':times,'output':translations,'interpolation':'LINEAR'})
    animation['channels'].append({'sampler':sampler,'target':{'node':0,'path':'translation'}})
asset['buffers'][0]['byteLength'] = len(binary)
header = json.dumps(asset,separators=(',',':')).encode()
header += b' ' * (-len(header) % 4)
binary.extend(b'\0' * (-len(binary) % 4))
p.write_bytes(struct.pack('<III',0x46546c67,2,28+len(header)+len(binary))+struct.pack('<II',len(header),0x4e4f534a)+header+struct.pack('<II',len(binary),0x004e4942)+binary)
p = out / 'rules.json'
s = p.read_text()
s = s.replace('"model": "models/golem.glb",', '"model": "models/golem.glb", "programmableAnimation": true,', 1)
# JSON comments are kept intact; append the root viewScripts field.
end = s.rfind('}')
s = s[:end].rstrip() + ',\n"viewScripts":["scripts/view/uaf.lua"]\n' + s[end:]
p.write_text(s)
target=out/'scripts/view/uaf.lua'
target.parent.mkdir(parents=True,exist_ok=True)
shutil.copy2(root/'examples/animation/uaf.lua',target)
print(out)
