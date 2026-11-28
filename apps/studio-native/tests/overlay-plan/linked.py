"""Coordinate the MCP side of the native overlay-plan desktop test."""
import json
from pathlib import Path
import subprocess
import sys
import time

binary, root, port, log_path = sys.argv[1:]
process = subprocess.Popen([binary, "--no-editor", "--engine", root, "--port", port],
                           stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
sequence = 0


def rpc(method, params):
    global sequence
    sequence += 1
    process.stdin.write(json.dumps(dict(jsonrpc="2.0", id=sequence, method=method, params=params)) + "\n")
    process.stdin.flush()
    return json.loads(process.stdout.readline())["result"]


def preview(**params):
    response = rpc("tools/call", {"name": "overlay_preview", "arguments": params})
    return json.loads(response["content"][0]["text"])


def wait_for(message):
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        text = Path(log_path).read_text(errors="replace")
        if message in text:
            return
        if "CROSS main FAIL" in text:
            raise RuntimeError(text[-1500:])
        time.sleep(.1)
    raise RuntimeError(message)


try:
    rpc("initialize", {"protocolVersion": "2024-11-05", "capabilities": {},
                       "clientInfo": {"name": "studio-overlay-test", "version": "1"}})
    assert any(tool["name"] == "overlay_preview" for tool in rpc("tools/list", {})["tools"])
    result = preview(ids=["sensor_radius", "build_site"], placeType="outpost", x=4, z=4)
    assert result["ok"] and result["preview"] == ["sensor_radius", "build_site"], result
    wait_for("CROSS main LINK_LOCAL")
    local = preview()
    assert local["preview"] == ["build_site"] and local["x"] == 4
    assert "error" in preview(ids=[], placeType="unknown")
    assert "error" in preview(z=1000)
    assert preview() == local
    assert preview(ids=["hud_note"])["preview"] == ["hud_note"]
    assert preview(ids=["cursor_ping"])["preview"] == ["cursor_ping"]
    assert preview(ids=["banner","status_badge"])["preview"] == ["banner","status_badge"]
    cleared = preview(off=True)
    assert cleared["preview"] == [] and cleared["placeType"] == "" and "x" not in cleared
    wait_for("CROSS main LINK_OFF")
    assert preview(placeType="outpost")["ok"]
    print("MCP discovery, panel reveal, shared UI selection, atomic failure, partial update and off passed")
finally:
    process.stdin.close()
    process.wait(timeout=5)
