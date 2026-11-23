#!/usr/bin/env python3
"""Validate real GLB2 image views + PBR channels from the building exporter.
Run against source-produced files, never fabricate screenshots or GPU results.
No third-party Python dependencies are required.
"""
import json
import pathlib
import struct
import sys

PNG = b"\x89PNG\r\n\x1a\n"
STYLES = (
    "medieval-fantasy", "dark-fantasy", "steampunk", "cyberpunk",
    "scifi-space", "alien-civilization", "post-apocalypse", "modern",
    "ancient-civilizations", "asian-historical", "solarpunk", "underwater",
)


def check_glb(path: pathlib.Path) -> dict:
    data = path.read_bytes()
    assert len(data) >= 28, f"{path}: truncated"
    magic, version, length = struct.unpack_from("<III", data)
    assert (magic, version, length) == (0x46546C67, 2, len(data)), path
    json_len, json_tag = struct.unpack_from("<II", data, 12)
    assert json_tag == 0x4E4F534A and json_len > 0 and json_len % 4 == 0, path
    doc = json.loads(data[20:20 + json_len])
    bin_offset = 20 + json_len
    bin_len, bin_tag = struct.unpack_from("<II", data, bin_offset)
    assert bin_tag == 0x004E4942 and bin_offset + 8 + bin_len == len(data), path
    binary = data[bin_offset + 8:]
    assert len(doc.get("meshes", [])) > 0, path
    assert len(doc.get("materials", [])) > 0, path
    images, views, textures = doc.get("images", []), doc.get("bufferViews", []), doc.get("textures", [])
    assert images and textures, f"{path}: generated PBR images are absent"
    for image in images:
        assert image["mimeType"] == "image/png", (path, image)
        v = views[image["bufferView"]]
        start, size = v.get("byteOffset", 0), v["byteLength"]
        assert start >= 0 and size > 32 and start + size <= len(binary), (path, v)
        payload = binary[start:start + size]
        assert payload.startswith(PNG), f"{path}: embedded image is not PNG"
        assert payload[12:16] == b"IHDR", f"{path}: PNG missing IHDR"
        w, h = struct.unpack_from(">II", payload, 16)
        assert (w, h) == (128, 128), (path, w, h)
    channels = set()
    for material in doc["materials"]:
        pbr = material.get("pbrMetallicRoughness", {})
        for name, obj in (
            ("baseColor", pbr.get("baseColorTexture")),
            ("metallicRoughness", pbr.get("metallicRoughnessTexture")),
            ("normal", material.get("normalTexture")),
        ):
            if obj is None:
                continue
            index = obj["index"]
            assert 0 <= index < len(textures), (path, name, index)
            channels.add(name)
    assert channels == {"baseColor", "metallicRoughness", "normal"}, (
        path, channels,
    )
    return {"meshes": len(doc["meshes"]), "images": len(images), "materials": len(doc["materials"])}


def main(root: pathlib.Path):
    totals = {"meshes": 0, "images": 0, "materials": 0}
    for name in STYLES:
        file = root / f"{name}.glb"
        assert file.exists(), f"Missing style GLB {file}"
        result = check_glb(file)
        for key in totals:
            totals[key] += result[key]
        print(f"PASS {name}: {result}")
    print(f"ALL 12 generated PBR GLB files: PASS {totals}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: verify_setting_glb.py <directory>")
    main(pathlib.Path(sys.argv[1]))
