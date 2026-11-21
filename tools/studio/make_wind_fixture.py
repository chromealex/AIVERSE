#!/usr/bin/env python3
"""Write a tiny Studio project with one rigged glTF tree for wind-mask QA.

Node tree: Trunk -> Branch_k -> Leaf_k_j (each node its own mesh), plus
content/tree.gltf.wind.json, content/wind/masks.json and profiles.json.
The map file is written separately (MapDoc) by the caller.
"""
import base64, json, math, struct, sys
from pathlib import Path

def cylinder(r0, r1, length, seg=10, rings=6):
    pos, nrm, idx = [], [], []
    for k in range(rings + 1):
        t = k / rings; r = r0 + (r1 - r0) * t; y = length * t
        for s in range(seg + 1):
            a = 2 * math.pi * s / seg
            pos += [r * math.cos(a), y, r * math.sin(a)]; nrm += [math.cos(a), 0, math.sin(a)]
    for k in range(rings):
        for s in range(seg):
            a = k * (seg + 1) + s; b = a + seg + 1
            idx += [a, b, a + 1, a + 1, b, b + 1]
    return pos, nrm, idx

def quad(w, h):
    pos = [-w, 0, 0, w, 0, 0, w, h, 0, -w, h, 0]
    nrm = [0, 0, 1] * 4
    return pos, nrm, [0, 1, 2, 0, 2, 3, 2, 1, 0, 3, 2, 0]

def quat_axis(ax, ay, az, angle):
    s = math.sin(angle / 2); l = math.sqrt(ax * ax + ay * ay + az * az)
    return [ax / l * s, ay / l * s, az / l * s, math.cos(angle / 2)]

def main(out: Path):
    content = out / "content"; (content / "wind").mkdir(parents=True, exist_ok=True)
    (out / "maps").mkdir(parents=True, exist_ok=True)
    blob = bytearray(); views = []; accessors = []; meshes = []; nodes = []
    def add_mesh(name, pos, nrm, idx, material):
        def view(data, target):
            while len(blob) % 4: blob.append(0)
            off = len(blob); blob.extend(data)
            views.append({"buffer": 0, "byteOffset": off, "byteLength": len(data), "target": target})
            return len(views) - 1
        vp = view(struct.pack("<%df" % len(pos), *pos), 34962)
        vn = view(struct.pack("<%df" % len(nrm), *nrm), 34962)
        vi = view(struct.pack("<%dI" % len(idx), *idx), 34963)
        xs, ys, zs = pos[0::3], pos[1::3], pos[2::3]
        accessors.append({"bufferView": vp, "componentType": 5126, "count": len(pos) // 3, "type": "VEC3",
                          "min": [min(xs), min(ys), min(zs)], "max": [max(xs), max(ys), max(zs)]})
        accessors.append({"bufferView": vn, "componentType": 5126, "count": len(nrm) // 3, "type": "VEC3"})
        accessors.append({"bufferView": vi, "componentType": 5125, "count": len(idx), "type": "SCALAR"})
        a = len(accessors)
        meshes.append({"name": name, "primitives": [{"attributes": {"POSITION": a - 3, "NORMAL": a - 2},
                                                     "indices": a - 1, "material": material}]})
        return len(meshes) - 1
    trunk = {"name": "Trunk", "mesh": add_mesh("Trunk", *cylinder(0.32, 0.12, 8.0), 0), "children": []}
    nodes.append(trunk)
    for k in range(5):
        h = 3.4 + k * 0.95; yaw = k * 2.4; tilt = 1.05 - k * 0.08; length = 3.2 - k * 0.35
        q_yaw = quat_axis(0, 1, 0, yaw); q_tilt = quat_axis(0, 0, 1, -tilt)
        # q = yaw * tilt
        x1, y1, z1, w1 = q_yaw; x2, y2, z2, w2 = q_tilt
        q = [w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2, w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2,
             w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2, w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2]
        branch = {"name": f"Branch_{k}", "mesh": add_mesh(f"Branch_{k}", *cylinder(0.11, 0.03, length, 8, 5), 0),
                  "translation": [0, h, 0], "rotation": q, "children": []}
        trunk["children"].append(len(nodes)); nodes.append(branch)
        bi = len(nodes) - 1
        for j in range(4):
            leaf = {"name": f"Leaf_{k}_{j}", "mesh": add_mesh(f"Leaf_{k}_{j}", *quad(0.45, 0.9), 1),
                    "translation": [0, length * (0.45 + 0.17 * j), 0],
                    "rotation": quat_axis(0, 1, 0, j * 1.3)}
            nodes[bi]["children"].append(len(nodes)); nodes.append(leaf)
    for n in nodes:
        if "children" in n and not n["children"]: del n["children"]
    gltf = {"asset": {"version": "2.0", "generator": "AIVERSE make_wind_fixture"},
            "scene": 0, "scenes": [{"nodes": [0]}], "nodes": nodes, "meshes": meshes,
            "materials": [{"name": "Bark", "pbrMetallicRoughness": {"baseColorFactor": [0.36, 0.22, 0.12, 1], "metallicFactor": 0, "roughnessFactor": 0.9}},
                          {"name": "Leaves", "doubleSided": True, "pbrMetallicRoughness": {"baseColorFactor": [0.25, 0.55, 0.18, 1], "metallicFactor": 0, "roughnessFactor": 0.8}}],
            "accessors": accessors, "bufferViews": views,
            "buffers": [{"byteLength": len(blob), "uri": "data:application/octet-stream;base64," + base64.b64encode(bytes(blob)).decode()}]}
    (content / "tree.gltf").write_text(json.dumps(gltf))
    (content / "tree.gltf.wind.json").write_text(json.dumps({
        "profile": "oak", "rules": [{"node": "Leaf*", "mask": "leaf", "flutter": 1}], "flutterMaterials": ["Leaves"]}, indent=1))
    (content / "wind" / "masks.json").write_text(json.dumps({"masks": {"oak_branch": [[0, 0], [0.2, 0.1], [0.6, 0.45], [1, 1]]}}, indent=1))
    (content / "wind" / "profiles.json").write_text(json.dumps({"profiles": {"oak": {"levels": [
        {"mask": "trunk", "stiffness": 0.85, "flex": 0.5}, {"mask": "oak_branch", "stiffness": 0.5, "flex": 1.0},
        {"mask": "leaf", "stiffness": 0.2, "flex": 1.4}]}}}, indent=1))
    (out / "game.json").write_text(json.dumps({"name": "Wind masks QA", "module": "rts", "rules": "rules.json",
                                               "maps": [{"file": "maps/scene.json"}]}))
    (out / "rules.json").write_text(json.dumps({"types": {"tree": {"health": {"max": 100},
                                                                   "view": {"model": "content/tree.gltf"}}}}))
    print("wrote", out)

if __name__ == "__main__":
    main(Path(sys.argv[1]))
