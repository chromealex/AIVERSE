"""Exercise native pose playback and independent Studio preview lifetimes over stdio."""
import base64
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile


def animated_triangle():
    # Three vertices, two key times and two local translations for the wrist.
    values = [0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0]
    data = struct.pack("<17f", *values)
    uri = "data:application/octet-stream;base64," + base64.b64encode(data).decode()
    return {
        "asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}],
        "nodes": [{"name": "Arm", "children": [1]}, {"name": "Wrist", "mesh": 0}],
        "buffers": [{"uri": uri, "byteLength": len(data)}],
        "bufferViews": [{"buffer": 0, "byteOffset": offset, "byteLength": length}
                        for offset, length in [(0, 36), (36, 8), (44, 24)]],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "type": "VEC3", "count": 3,
             "min": [0, 0, 0], "max": [1, 1, 0]},
            {"bufferView": 1, "componentType": 5126, "type": "SCALAR", "count": 2,
             "min": [0], "max": [1]},
            {"bufferView": 2, "componentType": 5126, "type": "VEC3", "count": 2}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0}}]}],
        "animations": [{"name": "Lift", "samplers": [{"input": 1, "output": 2}],
                        "channels": [{"sampler": 0, "target": {"node": 1, "path": "translation"}}]}],
    }


def verify(process):
    def command(panel, action, **arguments):
        params = {"name": "animation_graph_preview", "arguments": dict(arguments, panel=panel, action=action)}
        request = {"jsonrpc": "2.0", "id": 1, "method": "tools/call", "params": params}
        process.stdin.write(json.dumps(request) + "\n")
        process.stdin.flush()
        reply = json.loads(process.stdout.readline())
        return json.loads(reply["result"]["content"][0]["text"])

    graph = {"model": "content/arm.gltf", "nodes": [{"id": "lift", "kind": "clip", "clip": "Lift"}],
             "output": "lift"}
    live = command("graph", "open", graph=graph)
    rest = command("rig", "open", graph=graph, bindPose=True)
    wrist = next(i for i, bone in enumerate(live["bones"]) if bone["name"] == "Wrist")
    moved = command("graph", "frame", token=live["token"], dt=0.25)
    assert abs(moved["points"][wrist][1] - live["points"][wrist][1] - 0.25) < 1e-5, moved
    assert command("rig", "frame", token=rest["token"], dt=0.25)["points"] == rest["points"]
    assert "error" in command("graph", "frame", token=rest["token"], dt=0.1)
    assert command("graph", "close", token=live["token"]) == {}
    assert "error" in command("graph", "frame", token=live["token"], dt=0.1)
    assert command("rig", "close", token=rest["token"]) == {}


server, engine = sys.argv[1:]
with tempfile.TemporaryDirectory(prefix="studio-animation-preview-") as directory:
    root = Path(directory)
    (root / "game.json").write_text('{"name":"Preview","rules":"rules.json"}')
    (root / "rules.json").write_text('{"types":{"unit":{}}}')
    (root / "content").mkdir()
    (root / "content/arm.gltf").write_text(json.dumps(animated_triangle()))
    with subprocess.Popen([server, "--no-editor", "--port", "0", "--engine", engine, "--game", directory],
                          stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True) as process:
        try:
            verify(process)
        finally:
            process.stdin.close()
            process.wait(timeout=10)
