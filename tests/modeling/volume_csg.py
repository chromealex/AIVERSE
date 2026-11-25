"""Exercise the persisted source and generated GLB through the native Modeling workspace."""
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[2]
CLI = Path(sys.argv[1]).resolve()


def run(project, operation, arguments):
    request = json.dumps({"op": operation, "arguments": arguments}) + "\n"
    result = subprocess.run([CLI, project, ROOT], input=request, text=True,
                            capture_output=True, timeout=60, env=dict(os.environ, AIVERSE_TEMPLATES_DIR=str(project.parent)))
    if result.returncode:
        raise RuntimeError(result.stderr or result.stdout)
    return json.loads(result.stdout.splitlines()[-1])


def checked(project, op, args):
    result = run(project, op, args)
    if result.get("error") or result.get("ok") is False:
        raise RuntimeError(result)
    return result


def glb(project, revision):
    out = checked(project, "modeling_export", {"name": "sculpt", "expectedRevision": revision})
    data = (project / out["path"]).read_bytes()
    assert data[:4] == b"glTF" and struct.unpack_from("<II", data, 4) == (2, len(data))
    length, kind = struct.unpack_from("<II", data, 12)
    mesh = json.loads(data[20:20+length])
    assert kind == 0x4E4F534A and mesh["meshes"] and out["triangles"] > 0 and out["degenerateTriangles"] == 0
    return data, out["triangles"]


def main():
    with tempfile.TemporaryDirectory(prefix="aiverse-volume-csg-") as temp:
        workspace = Path(temp)
        project = workspace / "game"
        project.mkdir()
        (project / "game.json").write_text('{"name":"Volume CSG","id":"volume-csg","maps":[]}')
        (project / "rules.json").write_text('{"types":{"object":{}}}')
        document = json.loads((ROOT / "examples/modeling/volume-csg.json").read_text())
        texels = next(n for n in document["nodes"][0]["geometry"]["fields"] if n["op"] == "densityTexture")["texels"]
        raw = b"SPVT" + struct.pack("<IIII", 1, 4, 2, 128) + bytes(64) + bytes(texels)
        asset = project / "test.spvt"
        asset.write_bytes(raw + struct.pack("<I", zlib.crc32(raw)))
        imported = checked(project, "modeling_volume_texture_import", {"path": "test.spvt", "frame": 1})
        assert imported == {"op":"densityTexture", "dimension":4, "texels":texels}, imported
        assert run(project, "modeling_volume_texture_import", {"path":"test.spvt", "frame":2}).get("error")
        asset.write_bytes(raw + bytes(4))
        assert run(project, "modeling_volume_texture_import", {"path":"test.spvt"}).get("error")
        made = checked(project, "modeling_create", {"name": "sculpt", "document": document})
        assert made["revision"] == 1
        reopened = checked(project, "modeling_get", {"name": "sculpt"})
        assert reopened["document"] == document
        first, triangles = glb(project, 1)
        edit = {"name": "sculpt", "expectedRevision": 1, "commands": [{"op":"parameters","values":{"bore":.5}}]}
        updated = checked(project, "modeling_edit", edit)
        assert updated["revision"] == 2
        second, altered = glb(project, 2)
        assert first != second
        assert run(project, "modeling_edit", edit).get("error")
        invalid = json.loads(json.dumps(updated["document"]))
        invalid["nodes"][0]["geometry"]["fields"][-1]["a"] = "result"
        invalidEdit = {"name":"sculpt","expectedRevision":2,"commands":[{"op":"replace","document":invalid}]}
        assert run(project, "modeling_edit", invalidEdit).get("error")
        assert checked(project, "modeling_get", {"name":"sculpt"})["document"] == updated["document"]
        restored = checked(project, "modeling_undo", {"name":"sculpt","expectedRevision":2})
        assert restored["document"] == document and glb(project, 3)[0] == first
        print(f"PASS: reopened source, {triangles} -> {altered} triangles after bore edit, GLB changed, rejected invalid revision/cycle, undo restored identical GLB")


if __name__ == "__main__":
    main()
