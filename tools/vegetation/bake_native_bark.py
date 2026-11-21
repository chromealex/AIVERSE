"""Author PBR bark UVs for rigid native Arbor glTF, preserving its geometry/LODs.

Uses the existing xatlas/object-space bark baker. No alternate tree generator.
Every woody LOD shares one atlas; foliage primitives remain byte-for-byte intact.
"""
import argparse
import copy
import json
import shutil
from pathlib import Path

import numpy as np
from bake_bark_atlas import bake_bark_atlas_parts


def bake_native_bark(source, output, resolution=2048, material="oak_bark"):
    source, output = Path(source).resolve(), Path(output)
    if output.exists():
        raise ValueError("output exists; refusing overwrite")
    doc = json.loads(source.read_text())
    if (doc.get("animations") or doc.get("skins") or doc.get("extensionsUsed") or
            doc.get("asset", {}).get("generator") != "AIVERSE modeling"):
        raise ValueError("only rigid native Arbor glTF is supported")

    def resource(uri):
        path = (source.parent / uri).resolve()
        if not path.is_relative_to(source.parent) or not path.is_file():
            raise ValueError("native external resources must stay in the pack")
        return path

    buffers = [resource(b["uri"]).read_bytes() for b in doc["buffers"]]

    def read(index):
        a = doc["accessors"][index]
        if a.get("sparse") or a.get("normalized"):
            raise ValueError("unsupported native accessor")
        view = doc["bufferViews"][a["bufferView"]]
        dtype = np.dtype({5126: "<f4", 5125: "<u4", 5123: "<u2"}[a["componentType"]])
        width = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[a["type"]]
        return np.ndarray((a["count"], width), dtype=dtype,
            buffer=buffers[view["buffer"]],
            offset=view.get("byteOffset", 0) + a.get("byteOffset", 0),
            strides=(view.get("byteStride", width * dtype.itemsize), dtype.itemsize)).copy()

    woody = [i for i, m in enumerate(doc["materials"]) if m["name"] == "TerraWeave Bark"]
    if len(woody) != 1:
        raise ValueError("exactly one native bark material required")
    bark = woody[0]
    primitives, parts = [], []
    for mesh in doc["meshes"]:
        for primitive in mesh["primitives"]:
            if primitive.get("material") != bark:
                continue
            attrs = primitive["attributes"]
            if primitive.get("mode", 4) != 4 or primitive.get("targets") or set(attrs) - {
                    "POSITION", "NORMAL", "TEXCOORD_0", "TANGENT"}:
                raise ValueError("unsupported native bark attributes")
            part = (read(attrs["POSITION"]), read(attrs["NORMAL"]),
                    read(attrs["TEXCOORD_0"]), read(primitive["indices"]).reshape(-1, 3))
            if not len(part[3]) or len(part[3]) > 100000:
                raise ValueError("bounded native bark triangle budget")
            primitives.append(primitive); parts.append(part)
    if not 1 <= len(parts) <= 4:
        raise ValueError("expected one to four rigid bark LODs")
    original_foliage = [copy.deepcopy(p) for m in doc["meshes"] for p in m["primitives"]
                        if p.get("material") != bark]
    output.mkdir(parents=True)
    (output / "textures").mkdir()
    results = bake_bark_atlas_parts(parts, output / "textures", source.stem,
                                    resolution, material)
    binary = bytearray()
    new_buffer = len(doc["buffers"])

    def append(values, kind, target):
        while len(binary) % 4:
            binary.append(0)
        values = np.asarray(values, dtype="<u4" if kind == "SCALAR" else "<f4")
        raw = values.tobytes()
        view = len(doc["bufferViews"])
        doc["bufferViews"].append(dict(buffer=new_buffer, byteOffset=len(binary),
                                     byteLength=len(raw), target=target))
        binary.extend(raw)
        a = dict(bufferView=view, componentType=5125 if kind == "SCALAR" else 5126,
                 count=values.size if kind == "SCALAR" else len(values), type=kind)
        if kind == "VEC3":
            a.update(min=values.min(axis=0).tolist(), max=values.max(axis=0).tolist())
        doc["accessors"].append(a)
        return len(doc["accessors"]) - 1

    for primitive, before, after in zip(primitives, parts, results):
        p, n, uv, faces = after
        # xatlas splits UV vertices only; preserve actual native triangles and
        # their orientation. Never simplify or silently replace tree geometry.
        if not np.array_equal(p[faces], before[0][before[3]]):
            raise ValueError("UV unwrap changed native triangle positions/order")
        if not all(np.isfinite(v).all() for v in (p, n, uv)):
            raise ValueError("nonfinite authored attributes")
        if not np.allclose(np.linalg.norm(n, axis=1), 1, atol=.002):
            raise ValueError("nonunit native normals")
        primitive["attributes"] = dict(POSITION=append(p, "VEC3", 34962),
            NORMAL=append(n, "VEC3", 34962), TEXCOORD_0=append(uv, "VEC2", 34962))
        primitive["indices"] = append(faces.reshape(-1), "SCALAR", 34963)
        # Tangents must be regenerated from the NEW UVs by the native loader.
    binary_name = source.stem + "-bark.bin"
    doc["buffers"].append(dict(uri=binary_name, byteLength=len(binary)))
    (output / binary_name).write_bytes(binary)
    for item in doc["buffers"][:-1] + doc.get("images", []):
        path = resource(item["uri"])
        destination = output / item["uri"]
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, destination)
    sampler = len(doc.setdefault("samplers", []))
    doc["samplers"].append(dict(magFilter=9729, minFilter=9987, wrapS=33071, wrapT=33071))
    textures = []
    for suffix in ("basecolor", "normal", "orm"):
        image = len(doc["images"])
        doc["images"].append(dict(uri=f"textures/{source.stem}_bark_{suffix}.png"))
        textures.append(len(doc["textures"]))
        doc["textures"].append(dict(source=image, sampler=sampler))
    m = doc["materials"][bark]
    m["pbrMetallicRoughness"].update(baseColorTexture=dict(index=textures[0]),
                                    metallicRoughnessTexture=dict(index=textures[2]))
    m["normalTexture"] = dict(index=textures[1])
    m["occlusionTexture"] = dict(index=textures[2])
    assert original_foliage == [p for mesh in doc["meshes"] for p in mesh["primitives"]
                               if p.get("material") != bark]
    doc.setdefault("extras", {})["nativeBarkAtlas"] = dict(
        material=material, resolution=resolution, lods=len(parts), geometry="unchanged")
    (output / source.name).write_text(json.dumps(doc, indent=2))
    return dict(lods=len(parts), barkTriangles=[len(p[3]) for p in parts],
                atlasResolution=resolution, source=str(source))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--resolution", type=int, default=2048)
    parser.add_argument("--material", default="oak_bark")
    args = parser.parse_args()
    print(json.dumps(bake_native_bark(args.input, args.out, args.resolution, args.material)))
