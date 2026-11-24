"""Exercise the native recurring work planner over real MCP stdio."""
import json
import subprocess
import sys

server, engine = sys.argv[1:]
def call(name, **arguments):
    return ("tools/call", {"name": name, "arguments": arguments})

requests = [
    ("initialize", {}),
    ("tools/list", {}),
    call("simulation_budget_plan", count=200000, periodTicks=120),
    call("simulation_budget_plan", count=200000, periodTicks=120, maxVisitsPerTick=1000),
    call("simulation_budget_plan", count=0, periodTicks=120),
    call("simulation_budget_plan", count=20, periodTicks=0),
    call("simulation_budget_plan", count=2.5, periodTicks=20),
    call("simulation_budget_plan", count=-1, periodTicks=20),
]
data = "".join(json.dumps(dict(jsonrpc="2.0", id=i, method=m, params=p)) + "\n"
               for i, (m, p) in enumerate(requests))
result = subprocess.run([server, "--no-editor", "--engine", engine, "--port", "0"],
                        input=data, text=True, capture_output=True, timeout=30, check=True)
replies = {r["id"]: r["result"] for r in map(json.loads, result.stdout.splitlines()) if "id" in r}
assert "simulation_budget_plan" in {r["name"] for r in replies[1]["tools"]}
def payload(i):
    assert not replies[i].get("isError"), replies[i]
    return json.loads(replies[i]["content"][0]["text"])
assert payload(2) == dict(visitsPerTick=1667, maxRevisitTicks=120, meetsPeriod=True)
assert payload(3) == dict(visitsPerTick=1000, maxRevisitTicks=200, meetsPeriod=False)
assert payload(4) == dict(visitsPerTick=0, maxRevisitTicks=0, meetsPeriod=True)
for i in (5, 6, 7):
    assert replies[i].get("isError"), replies[i]
print("simulation budget planner: stdio discovery, exact plans and invalid input refusal")
