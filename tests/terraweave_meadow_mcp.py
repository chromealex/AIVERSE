"""Exercise meadow authoring, failure atomicity, undo and persistence over MCP."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def verify(binary, engine, project):
    requests = [{"jsonrpc": "2.0", "id": 0, "method": "initialize", "params": {
        "protocolVersion": "2024-11-05", "capabilities": {},
        "clientInfo": {"name": "meadow-test", "version": "1"}}}]
    calls = [
        ("open_game", {"game": str(project)}),
        ("terraweave_meadow_get", {}),
        ("terraweave_meadow_set", {"preset": "wetland"}),
        ("terraweave_meadow_set", {"config": {"species": [{"preset": "unknown"}]}}),
        ("terraweave_meadow_get", {}),
        ("undo", {}),
        ("terraweave_meadow_get", {}),
        ("redo", {}),
        ("map_save", {"file": "maps/saved.json"}),
        ("map_open", {"file": "maps/saved.json"}),
        ("terraweave_meadow_get", {}),
    ]
    for index, (tool, arguments) in enumerate(calls, 1):
        requests.append({"jsonrpc": "2.0", "id": index, "method": "tools/call",
                         "params": {"name": tool, "arguments": arguments}})
    result = subprocess.run([binary, "--engine", engine, "--no-editor", "--port", "0"],
                            input="".join(json.dumps(r) + "\n" for r in requests),
                            capture_output=True, text=True, check=True, timeout=60)
    replies = {r["id"]: r for line in result.stdout.splitlines() if line.startswith("{")
               for r in [json.loads(line)] if "id" in r}
    for i, (tool, _) in enumerate(calls, 1):
        reply = replies[i]["result"]
        payload = json.loads(reply["content"][0]["text"])
        if i == 4:
            assert reply.get("isError") or "error" in payload, payload
        else:
            assert not reply.get("isError") and "error" not in payload, (tool, payload)
        if tool == "terraweave_meadow_get":
            assert payload["meadow"] == (None if i in (2, 7) else "wetland"), payload
    assert json.loads((project / "maps/saved.json").read_text())["grass"]["meadow"] == "wetland"


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as tmp:
        project = Path(tmp)
        (project / "maps").mkdir()
        (project / "game.json").write_text('{"module":"game","rules":"rules.json","maps":[{"file":"maps/map.json"}]}')
        (project / "rules.json").write_text('{"types":{}}')
        (project / "maps/map.json").write_text('{"size":[16,16],"entities":[]}')
        verify(sys.argv[1], sys.argv[2], project)
    print("Meadow MCP authoring PASS")
