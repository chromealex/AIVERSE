"""Native stdio MCP acceptance for world persistence and retained job receipts."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time

server, engine = sys.argv[1:]
with tempfile.TemporaryDirectory(prefix="aiverse-world-mcp-") as tmp:
    root = Path(tmp)
    (root / "game.json").write_text(json.dumps({"name":"World persistence fixture","module":"actor","rules":"rules.json","maps":[{"name":"Map","file":"map.json"}]}))
    (root / "rules.json").write_text(json.dumps({"actor":{"tickRate":60,"maxPlayers":2,"mode":"deathmatch"},"regionOwnership":{}}))
    (root / "map.json").write_text(json.dumps({"players":[{"team":1},{"team":2}],"spawns":[[4,2,4],[8,2,4]],"colliders":[]}))
    proc = subprocess.Popen([server,"--no-editor","--port","0","--engine",engine],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    timer = threading.Timer(90,proc.kill)
    timer.start()
    seq = 0
    def request(method, params):
        global seq
        seq += 1
        proc.stdin.write(json.dumps({"jsonrpc":"2.0","id":seq,"method":method,"params":params})+"\n")
        proc.stdin.flush()
        line = proc.stdout.readline()
        assert line, "MCP exited or timed out"
        response = json.loads(line)
        assert response.get("id")==seq and "error" not in response, response
        return response["result"]
    def call(name,error=False,**args):
        result=request("tools/call",{"name":name,"arguments":args})
        assert bool(result.get("isError"))==error,result
        return json.loads(result["content"][0]["text"])
    def job(op, **args):
        ticket=call("world_job_start",operation=op,params=args)["id"]
        assert call("world_job_status")["id"]==ticket
        call("world_job_start",error=True,operation="world_save_list",params={})
        deadline=time.monotonic()+60
        while True:
            state=call("world_job_status",id=ticket)
            if state["state"]!="running":
                break
            assert time.monotonic()<deadline,state
            time.sleep(.02)
        assert state["state"]=="succeeded",state
        assert call("world_job_status",id=ticket)["result"]==state["result"]
        assert call("world_job_release",id=ticket)["released"]
        return state["result"]
    try:
        request("initialize",{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"world-test","version":"1"}})
        names={x["name"] for x in request("tools/list",{})["tools"]}
        required={"world_save_"+s for s in ("list","create","status","page","checkpoint","backups","retention_preview","retention_apply","fork")}
        required|={"world_job_"+s for s in ("start","status","cancel","release")}
        assert required<=names, required-names
        assert call("open_game",game=tmp).get("ok")
        assert call("world_save_list")["worlds"]==[]
        world="00000000000000000000000000000001"
        fork="00000000000000000000000000000002"
        args={"world":world,"seed":"1"}
        created=job("world_save_create",**args)
        assert created["generation"]=="1" and created["live"]==2,created
        assert job("world_save_status",**args)["revision"]==created["revision"]
        page=job("world_save_page",**args,bootstrap=True,expectedRevision=created["revision"],after="0",limit="100")
        assert len(page["records"])==2 and not page["resetRequired"],page
        checkpoint=job("world_save_checkpoint",**args,retainRevisions="0")
        assert checkpoint["generation"]=="2",checkpoint
        catalog=job("world_save_backups",**args,verify=True)
        assert catalog["complete"] and all(b["status"]=="recoverable" for b in catalog["backups"]),catalog
        assert job("world_save_fork",**args,generation="1",newWorld=fork)["ok"]
        assert job("world_save_status",world=fork,seed="1")["live"]==2
        plan=job("world_save_retention_preview",**args,keepPrevious="0",maxBytes="0")
        assert plan["valid"] and plan["removeGenerations"]==["1"],plan
        refused=job("world_save_retention_apply",**args,keepPrevious="0",maxBytes="0",expectedGenerations=["999"])
        assert not refused["ok"] and not refused["removed"],refused
        result=job("world_save_retention_apply",**args,keepPrevious="0",maxBytes="0",expectedGenerations=plan["removeGenerations"])
        assert result["ok"] and result["removed"]==["1"],result
        assert set(call("world_save_list")["worlds"])=={world,fork}
        proc.stdin.close()
        assert proc.wait(timeout=10)==0
        print("World persistence MCP stdio: PASS")
    finally:
        timer.cancel()
        if proc.poll() is None:
            proc.kill()
        proc.wait()
