"""Exercise native-module diagnostics through the standalone MCP server."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

server, engine = sys.argv[1:]
operations = ("match_run", "script_check", "scenario_run", "balance_batch", "balance_duel", "balance_matrix")


def call(name, **params):
    return "tools/call", {"name": name, "arguments": params}


def run(requests):
    messages = [dict(jsonrpc="2.0", id=i, method=m, params=p) for i, (m, p) in enumerate(requests)]
    proc = subprocess.run([server, "--no-editor", "--engine", engine, "--port", "0"],
                          input="".join(json.dumps(m) + "\n" for m in messages),
                          capture_output=True, text=True, check=True, timeout=60)
    responses = {r["id"]: r["result"] for r in map(json.loads, proc.stdout.splitlines()) if "id" in r}
    return [responses[i] for i in range(len(messages))]


def payload(response):
    assert not response.get("isError"), response
    return json.loads(response["content"][0]["text"])


with tempfile.TemporaryDirectory() as tmp:
    project = Path(tmp) / "outpost"
    shutil.copytree(Path(engine) / "demos/campaign-encounter", project)
    opening = call("open_game", game=str(project))
    rules_file = project / "rules.json"
    rules = json.loads(rules_file.read_text())
    rules["nativeModules"] = [{"id": "missing_alpha"}, {"id": "missing_beta"}]
    rules_file.write_text(json.dumps(rules))
    original = rules_file.read_bytes()
    params = {"seconds": 1, "a": "scout", "b": "guard", "runs": 1}
    replies = run([("initialize", {}), ("tools/list", {}), opening, call("match_run", seconds=1)] +
                  [call(op, reportMissingNativeModules=True, **params) for op in operations] +
                  [call("balance_stats")])
    schemas = {t["name"]: t["inputSchema"]["properties"] for t in replies[1]["tools"]}
    payload(replies[2])
    assert replies[3].get("isError") and "not compiled" in replies[3]["content"][0]["text"], replies[3]
    for op, response in zip(operations, replies[4:]):
        assert schemas[op]["reportMissingNativeModules"]["type"] == "boolean", op
        assert payload(response) == {"status": "missing_native_modules", "simulationRan": False,
                                     "missing": ["missing_alpha", "missing_beta"]}, (op, response)
    assert "reportMissingNativeModules" not in schemas["balance_stats"]
    assert "types" in payload(replies[-1])
    assert rules_file.read_bytes() == original
    del rules["nativeModules"]
    rules_file.write_text(json.dumps(rules))
    replies = run([("initialize", {}), opening, call("match_run", seconds=1),
                   call("match_run", seconds=1, reportMissingNativeModules=True)])
    regular, diagnostic = map(payload, replies[2:])
    assert regular["checksum"] and regular == diagnostic
print("Six simulation tools report both missing IDs. Default refusal, static stats, project integrity and real match parity passed.")
