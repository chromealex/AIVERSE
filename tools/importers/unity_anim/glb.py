"""Minimal glTF 2.0 binary writer for animation-only files (node hierarchy + animations).

The engine's glTF loader reads them (engine/render/ModelGltf.cpp) and a model's clip sidecar binds
their channels to the model's nodes by path (engine/render/ModelClips.h)."""
import json
import struct

LINEAR, STEP, CUBIC = 'LINEAR', 'STEP', 'CUBICSPLINE'


class Glb:
    def __init__(self):
        self.nodes, self.roots, self.by_path = [], [], {}
        self.bin = bytearray()
        self.accessors, self.views = [], []
        self.animations = []

    def node(self, path):
        """Node index of a Unity transform path ("a/b/c"), creating its ancestors."""
        if path in self.by_path:
            return self.by_path[path]
        parent = None
        if '/' in path:
            parent = self.node(path.rsplit('/', 1)[0])
        idx = len(self.nodes)
        self.nodes.append({'name': path.rsplit('/', 1)[-1]})
        if parent is None:
            self.roots.append(idx)
        else:
            self.nodes[parent].setdefault('children', []).append(idx)
        self.by_path[path] = idx
        return idx

    def _accessor(self, values, kind, minmax=False):
        comps = {'SCALAR': 1, 'VEC3': 3, 'VEC4': 4}[kind]
        while len(self.bin) % 4:
            self.bin.append(0)
        offset = len(self.bin)
        self.bin += struct.pack('<%df' % len(values), *values)
        self.views.append({'buffer': 0, 'byteOffset': offset, 'byteLength': len(values) * 4})
        acc = {'bufferView': len(self.views) - 1, 'componentType': 5126, 'count': len(values) // comps, 'type': kind}
        if minmax:
            acc['min'], acc['max'] = [min(values)], [max(values)]
        self.accessors.append(acc)
        return len(self.accessors) - 1

    def animation(self, name, channels):
        """channels: [(node, 'translation'|'rotation'|'scale', interpolation, times, flat values)].
        CUBICSPLINE values are (in-tangent, value, out-tangent) per key, tangents in units / second."""
        samplers, out = [], []
        for node, path, interp, times, values in channels:
            if not times:
                continue
            kind = 'VEC4' if path == 'rotation' else 'VEC3'
            samplers.append({'input': self._accessor(times, 'SCALAR', True), 'output': self._accessor(values, kind), 'interpolation': interp})
            out.append({'sampler': len(samplers) - 1, 'target': {'node': node, 'path': path}})
        if out:
            self.animations.append({'name': name, 'samplers': samplers, 'channels': out})

    def bytes(self):
        doc = {'asset': {'version': '2.0', 'generator': 'AIVERSE unity_anim importer'},
               'scene': 0, 'scenes': [{'nodes': self.roots}], 'nodes': self.nodes,
               'animations': self.animations, 'accessors': self.accessors, 'bufferViews': self.views,
               'buffers': [{'byteLength': len(self.bin)}]}
        js = json.dumps(doc, separators=(',', ':')).encode('utf-8')
        js += b' ' * ((4 - len(js) % 4) % 4)
        binary = bytes(self.bin) + b'\0' * ((4 - len(self.bin) % 4) % 4)
        total = 12 + 8 + len(js) + 8 + len(binary)
        return (struct.pack('<III', 0x46546C67, 2, total) + struct.pack('<II', len(js), 0x4E4F534A) + js +
                struct.pack('<II', len(binary), 0x004E4942) + binary)
