#!/usr/bin/env python3
"""Forest draw-call A/B: TerraWeave near trees as one node per branch vs
merged whole-tree LOD tiers (ArborForestAdapter::setTreeGeometry), same map,
cameras and wind. Writes perf.json + one image per case.

Drive a running aiverse_editor over its agent link (--listen PORT) and
capture beauty + debug screenshots of hierarchical wind masks on a
TerraWeave landscape: generated relief, procedural Arbor forest, PHYSIS
nature-preview grass and wind. Editor-only; the map is never saved.

usage: terra_forest_perf.py PORT OUT_DIR
"""
import base64, json, os, socket, sys, time

port, out = int(sys.argv[1]), sys.argv[2]
os.makedirs(out, exist_ok=True)

def connect():
    for _ in range(240):
        try:
            s = socket.create_connection(("127.0.0.1", port), timeout=5)
            s.settimeout(120)
            return s
        except OSError:
            time.sleep(1)
    sys.exit("editor agent link not reachable")

sock = connect(); buf = b""; seq = 0
def _hello():
    # Agent link v2: authenticate with the token the editor writes per port.
    global buf
    path = os.path.expanduser("~/.aiverse/agent-link/%d.token" % port)
    for _ in range(120):
        if os.path.exists(path): break
        time.sleep(1)
    token = open(path).read().strip()
    sock.sendall((json.dumps({"jsonrpc": "2.0", "id": 0, "method": "hello", "token": token}) + "\n").encode())
    while b"\n" not in buf:
        chunk = sock.recv(1 << 20)
        if not chunk: sys.exit("editor closed the link during hello")
        buf += chunk
    line, buf = buf.split(b"\n", 1)
    print("hello:", line[:120].decode(errors="replace"), flush=True)
_hello()
def call(_tool, **args):
    global buf, seq
    seq += 1
    msg = {"jsonrpc": "2.0", "id": seq, "method": "call", "params": {"name": _tool, "arguments": args}}
    sock.sendall((json.dumps(msg) + "\n").encode())
    while True:
        while b"\n" not in buf:
            chunk = sock.recv(1 << 20)
            if not chunk: sys.exit("editor closed the link")
            buf += chunk
        line, buf = buf.split(b"\n", 1)
        reply = json.loads(line)
        if reply.get("id") == seq:
            r = reply.get("result", {})
            short = {k: v for k, v in r.items() if k != "png_base64"} if isinstance(r, dict) else r
            print(f"{_tool}: {json.dumps(short, ensure_ascii=False)[:300]}", flush=True)
            return r

def shot(name, **cam):
    for attempt in range(5):
        r = call("view_screenshot", window="scene", clean=True, width=1920, height=1080, **cam)
        if "png_base64" in r:
            with open(os.path.join(out, name + ".png"), "wb") as f:
                f.write(base64.b64decode(r["png_base64"]))
            return
        time.sleep(1)

def height(x, z):
    r = call("terrain_heights", points=[[x, z]])
    try: return float(r["heights"][0])
    except Exception: return 0.0

N = 192
call("map_new", width=N, height=N, name="TerraForestPerf")
call("terraweave_generate", seed=20261121, baseHeightCm=0, reliefCm=1400,
     basePeriodCm=9000, octaves=6, cellSizeCm=100, overwrite=True)
call("map_set", path="terrainPresentation", value="legacy")
call("lighting_set", time=0.4, dayLength=0, clouds=0.25)
views = [("near", 100, 90, 22, 7, 220), ("mid", 88, 104, 45, 12, 140), ("wide", 96, 96, 90, 18, 35), ("far", 60, 60, 120, 9, 45)]
results = []
for geometry, speed in (("perBranch", 12), ("merged", 12), ("merged", 0)):
    call("view_wind", forest=True, hierarchical=True, nature=True, grass=True, flames=False,
         sound=False, playing=True, windSpeed=speed, windDirection=25, lodScale=1,
         treeGeometry=geometry, debug="off")
    time.sleep(4)
    for tag, x, z, dist, pitch, yaw in views:
        cam = dict(x=x, y=height(x, z) + 5, z=z, distance=dist, pitch=pitch, yaw=yaw)
        shot(f"{geometry}-w{speed}-{tag}", **cam)
        time.sleep(2.0)
        samples = [call("geometry_stats") for _ in range(3) if not time.sleep(0.4)]
        best = min(samples, key=lambda r: r.get("drawCalls", 1 << 30))
        row = {"geometry": geometry, "wind": speed, "view": tag}
        for k in ("drawCalls", "cameraModelTriangles", "sunShadowModelTriangles",
                  "lodChains", "cameraLodNodes", "milliseconds", "completedGpuFrameMs"):
            if k in best: row[k] = best[k]
        info = call("view_wind")
        row["forestInstances"] = info.get("forestInstances")
        results.append(row)
with open(os.path.join(out, "perf.json"), "w") as f:
    json.dump(results, f, indent=1)
print("terra-forest-perf:")
for r in results:
    print("  ", json.dumps(r, ensure_ascii=False))
print("terra-forest-perf: done", flush=True)
