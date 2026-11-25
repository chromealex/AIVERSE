#!/usr/bin/env python3
"""Exercise Building diagnostics via a real standalone MCP process, not a mock."""
import json
import subprocess
import sys

def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_building_diagnostics_stdio.py MCP ENGINE")
    binary, engine = sys.argv[1:]
    def call(ident, scene_id):
        return {"jsonrpc":"2.0","id":ident,"method":"tools/call","params":{
            "name":"building_graph_diagnostics","arguments":{"sceneId":scene_id}}}
    requests = [
        {"jsonrpc":"2.0","id":1,"method":"initialize","params":{
            "protocolVersion":"2024-11-05","capabilities":{},
            "clientInfo":{"name":"building-stage37","version":"1"}}},
        {"jsonrpc":"2.0","method":"notifications/initialized","params":{}},
        call(2,"1"), call(3,"0"), call(4,"9223372036854775808"),
    ]
    done = subprocess.run([binary,"--no-editor","--port","0","--engine",engine],
        input="\n".join(map(json.dumps,requests))+"\n",
        text=True,capture_output=True,timeout=30,check=True)
    responses = {}
    for line in done.stdout.splitlines():
        try:
            row = json.loads(line)
        except ValueError:
            continue
        if isinstance(row,dict) and isinstance(row.get("id"),int):
            if row["id"] in responses:
                raise RuntimeError("duplicate response")
            responses[row["id"]] = row
    def require(ok, message):
        if not ok:
            raise RuntimeError(message + ": " + json.dumps(responses,ensure_ascii=False))
    require(isinstance(responses.get(1,{}).get("result"),dict),"initialize failed")
    result = responses.get(2,{}).get("result",{})
    require(not result.get("isError") and bool(result.get("content")),"valid call failed")
    payload = json.loads(next(x["text"] for x in result["content"] if x.get("type")=="text"))
    require(payload.get("available") is False and bool(payload.get("reason")) and
            "snapshot" not in payload,"standalone must report missing live owner")
    print("PASS valid scene ID: unavailable without live runtime owner")
    for ident in (3,4):
        row = responses.get(ident,{})
        require("error" in row or row.get("result",{}).get("isError") is True,
                "invalid scene ID accepted")
    print("PASS zero and overflowing scene IDs rejected")
    print("Live attached-runtime diagnostics NOT RUN")
if __name__ == "__main__":
    main()
