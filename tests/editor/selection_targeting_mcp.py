"""Check authored targeting, selection, and persistence through the real MCP host."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def verify(binary, engine, project):
    scenario = {"seconds": 4, "seed": 17, "watch": [{"type": "target", "owner": 2}]}
    calls = [
        ("open_game", {"game": str(project)}),
        ("rules_lint", {}),
        ("scenario_run", scenario),
        ("rules_set", {"path": "targeting.acquireOrder", "value": "nearest"}),
        ("rules_set", {"path": "types.oak.selectable", "value": True}),
        ("rules_set", {"path": "targeting.acquireOrder", "value": "unknown"}),
        ("open_game", {"game": str(project)}),
        ("rules_get", {"path": "types.oak.selectable"}),
        ("rules_get", {"path": "targeting.acquireOrder"}),
        ("scenario_run", scenario),
    ]
    requests = [{"jsonrpc": "2.0", "id": 0, "method": "initialize", "params": {
        "protocolVersion": "2024-11-05", "capabilities": {},
        "clientInfo": {"name": "selection-targeting", "version": "1"}}}]
    for i, (name, arguments) in enumerate(calls, 1):
        requests.append({"jsonrpc": "2.0", "id": i, "method": "tools/call",
                         "params": {"name": name, "arguments": arguments}})
    response = subprocess.run([binary, "--engine", engine, "--no-editor", "--port", "0"],
                              input="".join(json.dumps(req) + "\n" for req in requests),
                              capture_output=True, text=True, check=True, timeout=60)
    replies = {r["id"]: r["result"] for r in map(json.loads, response.stdout.splitlines())}
    payloads = {}
    for i, (name, _) in enumerate(calls, 1):
        reply = replies[i]
        body = json.loads(reply["content"][0]["text"])
        if i == 6:
            assert "targeting.acquireOrder" in str(body) and (reply.get("isError") or "error" in body), body
        else:
            assert not reply.get("isError") and "error" not in body, (name, body)
        payloads[i] = body
    assert payloads[2]["ok"], payloads[2]
    assert payloads[8]["value"] is True and payloads[9]["value"] == "nearest"
    for i, expected in ((3, 14), (10, 11)):
        targets = payloads[i]["watch"]
        damaged = [unit["x"] for unit in targets if unit["hp"] < unit["maxHp"]]
        assert len(targets) == 2 and damaged == [expected], targets
        print(("rowMajor" if i == 3 else "nearest"), "damaged target at x =", damaged[0])


if __name__ == "__main__":
    executable, root = sys.argv[1:]
    with tempfile.TemporaryDirectory(prefix="selection-targeting-") as tmp:
        fixture = Path(tmp) / "game"
        shutil.copytree(Path(root) / "demos/selection-targeting", fixture)
        verify(executable, root, fixture)
    print("MCP rules lint, edit, validation rollback, reopen and both targeting policies passed")
