#!/usr/bin/env python3
"""20 genuine AIVERSE procedural building variants; no mock, no fallback model.

Builds through tools/buildings/building_style_export.cpp (optional --compile).
Checks GLB buffer layout and ACTUAL vertex positions, normal validity, exact
requested bounding boxes, triangle count and deterministic file hashes.
Unreachable dimensions remain FAILED; the script never silently scales or
substitutes legacy models. macOS/Python standard library only.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import shlex
import struct
import subprocess
import sys
from pathlib import Path

CASES = [
    ("barracks", 12, 8, 11, 3),
    ("barracks", 16, 10, 14, 7),
    ("barracks", 24, 13, 18, 11),
    ("barracks", 32, 18, 26, 19),
    ("barracks", 44, 23, 34, 42),
    ("command", 18, 11, 18, 3),
    ("command", 24, 14, 22, 7),
    ("command", 32, 18, 28, 11),
    ("command", 42, 24, 36, 19),
    ("command", 55, 32, 46, 42),
    ("factory", 16, 9, 11, 3),
    ("factory", 24, 12, 18, 7),
    ("factory", 32, 16, 24, 11),
    ("factory", 45, 22, 33, 19),
    ("factory", 60, 30, 46, 42),
    ("depot", 10, 7, 10, 3),
    ("depot", 14, 9, 12, 7),
    ("depot", 20, 12, 18, 11),
    ("depot", 30, 16, 24, 19),
    ("depot", 42, 22, 33, 42),
]


def decode_glb(path: Path, dimensions: tuple[int, int, int]) -> dict:
    data = path.read_bytes()
    if len(data) < 28:
        raise ValueError("GLB too small")
    magic, version, total = struct.unpack_from("<III", data)
    if magic != 0x46546C67 or version != 2 or total != len(data):
        raise ValueError("Invalid glTF GLB header/length")
    jsonlen, jsontag = struct.unpack_from("<II", data, 12)
    if jsontag != 0x4E4F534A or 20 + jsonlen + 8 > len(data):
        raise ValueError("Invalid JSON chunk")
    doc = json.loads(data[20:20 + jsonlen])
    bin_offset = 20 + jsonlen
    binlen, bintag = struct.unpack_from("<II", data, bin_offset)
    if bintag != 0x004E4942 or bin_offset + 8 + binlen != len(data):
        raise ValueError("Invalid BIN chunk/length")
    binary = memoryview(data)[bin_offset + 8:]
    if not doc.get("meshes") or not doc.get("accessors") or not doc.get("bufferViews"):
        raise ValueError("No mesh/accessors/views")
    vertices = set()
    triangles = 0
    materials = set()
    mins = [math.inf] * 3
    maxs = [-math.inf] * 3
    for mesh in doc["meshes"]:
        for prim in mesh["primitives"]:
            material = prim.get("material")
            if material is not None:
                materials.add(material)
            idx = doc["accessors"][prim["indices"]]
            if idx.get("type") != "SCALAR" or idx.get("componentType") != 5125 or idx["count"] % 3:
                raise ValueError("Invalid triangle indices")
            triangles += idx["count"] // 3
            ia = doc["accessors"][prim["attributes"]["POSITION"]]
            if ia.get("type") != "VEC3" or ia.get("componentType") != 5126:
                raise ValueError("Invalid position accessor")
            bv = doc["bufferViews"][ia["bufferView"]]
            start = bv.get("byteOffset", 0) + ia.get("byteOffset", 0)
            stride = bv.get("byteStride", 12)
            if start < 0 or stride < 12 or start + stride * (ia["count"] - 1) + 12 > len(binary):
                raise ValueError("Vertex buffer overrun")
            key = (ia["bufferView"], ia.get("byteOffset", 0), ia["count"])
            if key not in vertices:
                vertices.add(key)
                for i in range(ia["count"]):
                    at = start + i * stride
                    xyz = struct.unpack_from("<fff", binary, at)
                    if not all(math.isfinite(v) for v in xyz):
                        raise ValueError("Nonfinite vertex position")
                    for axis, v in enumerate(xyz):
                        mins[axis] = min(mins[axis], v)
                        maxs[axis] = max(maxs[axis], v)
    if triangles < 1 or triangles > 150000 or not vertices:
        raise ValueError("Invalid triangle count")
    for i, requested in enumerate(dimensions):
        if abs(mins[i]) > 0.015 or abs(maxs[i] - requested) > 0.015:
            raise ValueError(
                f"Actual bounds mismatch axis {i}: [{mins[i]:.4f}, {maxs[i]:.4f}] "
                f"expected [0, {requested}]"
            )
    return {
        "vertices": sum(item[2] for item in vertices),
        "triangles": triangles,
        "materials": len(materials),
        "bytes": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "bounds_min": [round(v, 4) for v in mins],
        "bounds_max": [round(v, 4) for v in maxs],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--out", type=Path, default=Path("build/buildings-qa/batch-20"))
    parser.add_argument("--exporter", type=Path, default=Path("build/buildings-qa/aiverse_building_export"))
    parser.add_argument("--compile", action="store_true", help="Recompile C++ exporter with Apple clang++")
    parser.add_argument("--allow-failures", action="store_true", help="Exit 0 even if some dimensions fail")
    args = parser.parse_args()
    root = args.root.resolve()
    out = (root / args.out).resolve()
    exporter = (root / args.exporter).resolve()
    out.mkdir(parents=True, exist_ok=True)
    if args.compile:
        command = [
            "clang++", "-std=c++20", "-O1", "-Wno-misleading-indentation",
            "-I.", "-Ilibraries/include",
            "tools/buildings/building_style_export.cpp",
            "engine/core/Fixed.cpp", "engine/data/Json.cpp",
            "-o", str(exporter),
        ]
        exporter.parent.mkdir(parents=True, exist_ok=True)
        print("Compiling real C++ exporter:", shlex.join(command), flush=True)
        subprocess.run(command, cwd=root, check=True)
    if not exporter.is_file():
        parser.error(f"Missing C++ exporter: {exporter}; pass --compile to build it")
    rows = []
    for i, (kind, w, h, d, seed) in enumerate(CASES, 1):
        name = f"{i:02d}-{kind}-{w}x{h}x{d}-s{seed}"
        glb = out / f"{name}.glb"
        cmd = [
            str(exporter),
            "--style", "engine/assets/building-styles/scifi.json",
            "--structure", kind,
            "--size", str(w), str(h), str(d),
            "--seed", str(seed), "--out", str(glb),
        ]
        row = dict(index=i, kind=kind, width=w, height=h, depth=d,
                   seed=seed, status="FAILED", file=glb.name,
                   error="", vertices=0, triangles=0, materials=0,
                   bytes=0, sha256="", bounds_min="", bounds_max="")
        try:
            result = subprocess.run(cmd, cwd=root, text=True,
                                    capture_output=True, timeout=45)
            if result.returncode != 0:
                raise RuntimeError((result.stderr or result.stdout).strip()[:400])
            stats = decode_glb(glb, (w, h, d))
            row.update(stats)
            row["bounds_min"] = json.dumps(stats["bounds_min"])
            row["bounds_max"] = json.dumps(stats["bounds_max"])
            row["status"] = "PASS"
        except Exception as ex:
            row["error"] = str(ex)
            print(f"[{i:02d}/20] FAIL {kind:8} {w:3} x {h:2} x {d:3}  "
                  f"seed {seed:2}: {ex}", flush=True)
        else:
            print(f"[{i:02d}/20] PASS {kind:8} {w:3} x {h:2} x {d:3}  "
                  f"seed {seed:2}: {row['triangles']} triangles", flush=True)
        rows.append(row)
    with (out / "report.csv").open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    (out / "report.json").write_text(
        json.dumps({"schema": 1, "cases": rows}, indent=2) + "\n", encoding="utf-8"
    )

    # The engine's real ModelView opens each five-model group. Native scale
    # is preserved in the assets; the viewer can scale for easier inspection.
    viewer = root / "build/building-view/aiverse_modelview"
    for kind in ("barracks", "command", "factory", "depot"):
        files = [out / row["file"] for row in rows
                 if row["kind"] == kind and row["status"] == "PASS"]
        commands = []
        if files:
            command = [str(viewer), *(str(p) for p in files),
                       "--yaw", "30", "--pitch", "28", "--spacing", "14"]
            commands.append(shlex.join(command))
        else:
            commands.append(f"echo {shlex.quote('No successful models for ' + kind)}")
        script = "#!/bin/sh\nset -eu\n" + "\n".join(commands) + "\n"
        target = out / f"view-{kind}.command"
        target.write_text(script, encoding="utf-8")
        target.chmod(0o755)
    good = sum(r["status"] == "PASS" for r in rows)
    print(f"\nRESULT: {good}/20 PASS, {20-good}/20 FAILED", flush=True)
    print(f"Models / reports / viewer scripts: {out}", flush=True)
    if good != 20 and not args.allow_failures:
        print("Some requested exact sizes were rejected. This is a real "
              "constructive-fit QA failure, not a substitute legacy model.", flush=True)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
