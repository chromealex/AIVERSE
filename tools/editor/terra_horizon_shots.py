#!/usr/bin/env python3
"""Forest to the horizon: a 1 km TerraWeave map with the editor forest
overlay in Horizon mode (merged near trees, per-tree impostors to 250 m,
TerraWeave Horizon cluster cards to ~6 km). Hilltop cameras, scene-only
shots + geometry_stats per shot in horizon.json.

Drive a running aiverse_editor over its agent link (--listen PORT) and
capture beauty + debug screenshots of hierarchical wind masks on a
TerraWeave landscape: generated relief, procedural Arbor forest, PHYSIS
nature-preview grass and wind. Editor-only; the map is never saved.

usage: terra_horizon_shots.py PORT OUT_DIR
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

N = 512
call("map_new", width=N, height=N, name="TerraHorizon")
call("terraweave_generate", seed=7, baseHeightCm=0, reliefCm=4200,
     basePeriodCm=32000, octaves=6, cellSizeCm=100, overwrite=True)
call("map_set", path="terrainPresentation", value="legacy")
call("lighting_set", time=0.36, dayLength=0, clouds=0.3)
call("map_set", path="lighting.fogDensity", value=0.00045)
call("postfx_preset", preset="vivid")
# Highest point near the centre for a lookout.
grid = call("terrain_heights", grid=16)
best = (N // 2, N // 2, -1e9)
for zi, row in enumerate(grid.get("rows", [])):
    for xi, h in enumerate(row):
        x, z = xi * 16 + 8, zi * 16 + 8
        if abs(x - N / 2) < N * 0.25 and abs(z - N / 2) < N * 0.25 and h > best[2]:
            best = (x, z, h)
hx, hz, _ = best
print("lookout", best, flush=True)
call("view_wind", forest=True, hierarchical=True, horizon=True, nature=True, grass=True, flames=False,
     sound=False, playing=True, windSpeed=6, windDirection=30, lodScale=1, treeGeometry="auto", debug="off")
call("view_focus", x=hx, z=hz, distance=40)
time.sleep(12)   # 17x17 tiles of trees + Horizon region bake
results = []
views = [("lookout-n", 30, 4, 0), ("lookout-e", 30, 4, 90), ("lookout-sw", 30, 6, 225),
         ("aerial", 110, 22, 45), ("low-forest", 12, 2, 160)]
for tag, dist, pitch, yaw in views:
    cam = dict(x=hx, y=height(hx, hz) + 3, z=hz, distance=dist, pitch=pitch, yaw=yaw)
    shot(tag, **cam)
    time.sleep(3)
    shot(tag, **cam)   # second capture after Horizon re-bake for this camera
    time.sleep(1.5)
    st = min((call("geometry_stats") for _ in range(3)), key=lambda r: r.get("drawCalls", 1 << 30))
    info = call("view_wind")
    row = {"view": tag, "forestInstances": info.get("forestInstances")}
    for k in ("drawCalls", "cameraModelTriangles", "sunShadowModelTriangles", "completedGpuFrameMs", "cameraLodNodes"):
        if k in st: row[k] = st[k]
    results.append(row)
# TERRA slope + PHYSIS wind gizmos on the TerraWeave heightfield (viewport UI).
call("view_gizmos", terraSlope=True, terraSlopeAtTarget=True, physisWind=True, radius=70, spacing=7)
time.sleep(1.5)
for _ in range(5):
    r = call("view_screenshot", window="scene", clean=False, x=hx, y=height(hx, hz), z=hz, distance=110, pitch=50, yaw=30)
    if "png_base64" in r:
        open(os.path.join(out, "gizmo-terra-slope-wind.png"), "wb").write(base64.b64decode(r["png_base64"])); break
    time.sleep(1)
with open(os.path.join(out, "horizon.json"), "w") as f:
    json.dump(results, f, indent=1)
for r in results:
    print("  ", json.dumps(r), flush=True)
print("terra-horizon-shots: done", flush=True)
