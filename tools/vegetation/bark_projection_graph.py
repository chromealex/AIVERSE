"""Shared smooth object-space PBR bark: two source images, six fragment samples.

Height lives in opaque albedo alpha. Surface-gradient bump uses world derivatives
so instance yaw/scale do not require per-instance material copies or UV atlases.
"""
import argparse
import json
import math
import shutil
from pathlib import Path


def material_graph(height_color, orm_path, metres=1.5, height_metres=.01225):
    if not math.isfinite(metres) or not .25 <= metres <= 16:
        raise ValueError("bark repeat metres .25..16")
    if not math.isfinite(height_metres) or not 0 <= height_metres <= .1:
        raise ValueError("bark relief metres0..0.1")
    nodes, memo = [], {}

    def n(op, *inputs, **properties):
        signature = json.dumps([op, inputs, properties], sort_keys=True)
        if signature in memo:
            return memo[signature]
        key = "n" + str(len(nodes)); memo[signature] = key
        node = dict(id=key, op=op, **properties)
        if inputs:
            node["inputs"] = list(inputs)
        nodes.append(node)
        return key

    def c(value):
        return n("constant", value=value)

    def total(values):
        return n("add", n("add", values[0], values[1]), values[2])

    obj, world = n("objectPosition"), n("worldPosition")
    normal = n("normalize3", n("normal"))
    # The generic normalize guard is absolute. Scale the derivative cross
    # before normalization so centimetre-close cameras do not darken weights.
    odx, ody = n("ddx", obj), n("ddy", obj)
    dpdx, dpdy = n("ddx", world), n("ddy", world)
    obj_cross = n("cross", odx, ody)
    obj_normal = n("normalize3", n("multiply", obj_cross, c(1000000)))
    # Recover the smooth object-space shading normal through corresponding
    # derivative frames. Uniformly scaled/rotated native tree instances share
    # one graph; geometric face normals caused visible triangular colour bands.
    def unit(v):
        return n("normalize3", n("multiply", v, c(1000000)))
    ot, wt = unit(odx), unit(dpdx)
    wf = unit(n("cross", dpdx, dpdy))
    ob, wb = n("cross", obj_normal, ot), n("cross", wf, wt)
    smooth = unit(total([n("multiply", basis, n("dot3", normal, w_basis))
                        for basis, w_basis in ((ot, wt), (ob, wb), (obj_normal, wf))]))
    weights = n("add", n("pow", n("abs", smooth), c(4)), c(.00001))
    axes = [n(channel, weights) for channel in ("red", "green", "blue")]
    divisor = n("max", total(axes), c(.00001))
    axes = [n("divide", a, divisor) for a in axes]
    colors, orms = [], []
    for axis, mask in enumerate(("bgra", "rbra", "rgra")):
        uv = n("multiply", n("swizzle", obj, mask=mask),
               c([1 / metres, (1 if axis == 1 else -1) / metres, 0, 0]))
        colors.append(n("multiply", n("sampleTexture", uv, texture=height_color, srgb=True), axes[axis]))
        orms.append(n("multiply", n("sampleTexture", uv, texture=orm_path), axes[axis]))
    color, orm = total(colors), total(orms)
    height = n("multiply", n("alpha", color), c(height_metres))
    r1, r2 = n("cross", dpdy, normal), n("cross", normal, dpdx)
    det = n("dot3", dpdx, r1)
    gradient = n("multiply", n("sign", det), n("add",
        n("multiply", n("ddx", height), r1), n("multiply", n("ddy", height), r2)))
    bumped = n("normalize3", n("multiply", n("subtract",
        n("multiply", n("abs", det), normal), gradient), c(1000000)))
    outputs = dict(color=n("setAlpha", color, c(1)), normal=bumped,
                   roughness=n("green", orm), occlusion=n("red", orm), metallic=c(0))
    if len(nodes) > 128:
        raise ValueError("bark graph exceeds native node budget")
    return dict(version=1, domain="material", minQuality="low", nodes=nodes, outputs=outputs)


def build_pack(output, asset_root):
    import numpy as np
    from PIL import Image
    output = Path(output)
    if output.exists():
        raise ValueError("output exists; refusing overwrite")
    source = Path(__file__).resolve().parents[2] / "engine/assets/materials/vegetation"
    catalog = json.loads((source / "catalog.json").read_text())
    output.mkdir(parents=True)
    materials, library = {}, {}
    for entry in catalog["materials"]:
        if entry["layout"] != "repeat":
            continue
        name = entry["id"]
        rgb = np.asarray(Image.open(source / "textures" / (name + "_basecolor.png")).convert("RGB"))
        relief = np.asarray(Image.open(source / "textures" / (name + "_height.png")))
        if relief.shape != rgb.shape[:2]:
            raise ValueError("aligned source albedo/height required")
        limit = np.iinfo(relief.dtype).max
        alpha = np.rint(relief.astype(np.float64) / limit * 255).astype(np.uint8)
        Image.fromarray(np.dstack([rgb, alpha])).save(output / (name + "_heightcolor.png"))
        shutil.copyfile(source / "textures" / (name + "_orm.png"), output / (name + "_orm.png"))
        graph = material_graph(asset_root + "/" + name + "_heightcolor.png",
                               asset_root + "/" + name + "_orm.png",
                               entry["repeatMeters"], entry["heightMeters"] * .35)
        (output / (name + ".json")).write_text(json.dumps(graph, indent=2))
        materials[name] = dict(graph=name + ".json", nodes=len(graph["nodes"]),
                               samples=6, sources=2, repeatMeters=entry["repeatMeters"])
        library[name + "_projected"] = dict(base=name,
            description="Shared object-space PBR bark with height-gradient normals",
            shaderGraph=graph)
    (output / "catalog.json").write_text(json.dumps(dict(version=1, materials=materials), indent=2))
    (output / "materials.json").write_text(json.dumps(library, indent=2))
    return materials


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--asset-root", required=True)
    args = parser.parse_args()
    print(json.dumps(build_pack(args.out, args.asset_root)))
