"""Exercise local order and structure upgrade authoring over the native MCP protocol."""

import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def verify(binary, engine, project):
    path = "types.outpost.view.showUpgradeTarget"
    calls = [
        ("open_game", {"game": str(project)}),
        ("overlay_set", {"template": "local_order_ring", "id": "acceptance_ring"}),
        ("overlay_get", {"id": "acceptance_ring"}),
        ("rules_set", {"path": path, "value": False}),
        ("rules_set", {"path": path, "value": "invalid"}),
        ("open_game", {"game": str(project)}),
        ("rules_get", {"path": path}),
        ("overlay_get", {"id": "acceptance_ring"}),
    ]
    requests = [{"jsonrpc": "2.0", "id": 0, "method": "initialize", "params": {}}]
    for index, (name, arguments) in enumerate(calls, 1):
        requests.append({"jsonrpc": "2.0", "id": index, "method": "tools/call",
                         "params": {"name": name, "arguments": arguments}})
    result = subprocess.run([binary, "--no-editor", "--engine", engine, "--port", "0"],
                            input="".join(json.dumps(r) + "\n" for r in requests),
                            text=True, capture_output=True, check=True, timeout=45)
    replies = {}
    for line in result.stdout.splitlines():
        if line.startswith("{"):
            reply = json.loads(line)
            if "id" in reply:
                replies[reply["id"]] = reply["result"]
    for index, (name, arguments) in enumerate(calls, 1):
        reply = replies[index]
        invalid = arguments.get("value") == "invalid"
        assert bool(reply.get("isError")) == invalid, (name, reply)
        if invalid:
            assert "showUpgradeTarget" in reply["content"][0]["text"], reply
        else:
            data = json.loads(reply["content"][0]["text"])
            if name == "overlay_get":
                assert data["rules"]["event"]["on"] == "order", data
            if name == "rules_get":
                assert data["value"] is False, data
    persisted = json.loads((project / "rules.json").read_text())
    assert persisted["types"]["outpost"]["view"]["showUpgradeTarget"] is False
    assert any(o["id"] == "acceptance_ring" for o in persisted["overlays"])


if __name__ == "__main__":
    binary, engine = sys.argv[1:]
    with tempfile.TemporaryDirectory() as tmp:
        project = Path(tmp) / "courtyard"
        shutil.copytree(Path(engine) / "demos/order-upgrade", project)
        verify(binary, engine, project)
    print("PASS local order template and upgrade appearance authoring, rollback and project reopen")
