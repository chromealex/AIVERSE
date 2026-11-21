#!/usr/bin/env python3
"""Exercise PHYSIS over real MCP stdio, with no editor/project/file mutation."""
import json
import subprocess
import sys
import threading


def main():
    proc = subprocess.Popen([sys.argv[1], "--engine", sys.argv[2], "--no-editor", "--port", "0"],
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    timer = threading.Timer(25, proc.kill)
    timer.start()
    sequence = 0
    def request(method, params):
        nonlocal sequence
        sequence += 1
        proc.stdin.write(json.dumps(dict(jsonrpc="2.0", id=sequence, method=method, params=params)) + "\n")
        proc.stdin.flush()
        line = proc.stdout.readline()
        assert line, "MCP exited or timed out"
        response = json.loads(line)
        assert response.get("id") == sequence and "error" not in response, response
        return response["result"]
    def call(name, arguments, error=False):
        result = request("tools/call", dict(name=name, arguments=arguments))
        assert bool(result.get("isError")) == error, result
        return json.loads(result["content"][0]["text"])
    try:
        request("initialize", dict(protocolVersion="2025-06-18", capabilities={}, clientInfo=dict(name="physis-test", version="1")))
        listed = request("tools/list", {})["tools"]
        names = [t["name"] for t in listed if t["name"].startswith("physis_")]
        assert sorted(names) == ["physis_get", "physis_set", "physis_validate"], names
        first = call("physis_get", {})
        assert first["config"] is None and not first["liveApplied"]
        config = first["defaultConfig"]
        call("physis_validate", dict(config=config))
        changed = call("physis_set", dict(config=config, expectedRevision=first["revision"]))
        assert changed["changed"] and changed["config"] == config
        call("physis_set", dict(config=None, expectedRevision=first["revision"]), error=True)
        invalid = dict(config, floodMode=True)
        call("physis_validate", dict(config=invalid), error=True)
        call("physis_set", dict(config=invalid, expectedRevision=changed["revision"]), error=True)
        call("physis_set", dict(expectedRevision=changed["revision"]), error=True)
        assert call("physis_get", {})["config"] == config
        removed = call("physis_set", dict(config=None, expectedRevision=changed["revision"]))
        assert removed["changed"] and removed["config"] is None
        proc.stdin.close()
        assert proc.wait(timeout=5) == 0
        print("PHYSIS MCP stdio: PASS (discovery, validate, set/read, conflict, rejection atomicity, removal)")
    finally:
        timer.cancel()
        if proc.poll() is None:
            proc.kill()
        proc.wait()


if __name__ == "__main__":
    main()
