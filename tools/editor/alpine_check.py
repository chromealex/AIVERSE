#!/usr/bin/env python3
"""PHYSIS + TERRA + NEREID together on AlpineValley (review copy: JSON
copied, heavy assets symlinked; nothing is saved). Opens maps/valley.json,
turns on the PHYSIS nature preview (grass, wind, fire), reads NEREID water
state, and captures river / forest / fire / overview shots with
geometry_stats per shot (alpine.json).

Drive a running aiverse_editor over its agent link (--listen PORT) and
capture beauty + debug screenshots of hierarchical wind masks on a
TerraWeave landscape: generated relief, procedural Arbor forest, PHYSIS
nature-preview grass and wind. Editor-only; the map is never saved.

usage: alpine_check.py PORT OUT_DIR
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

call("map_open", file="maps/valley.json")
# Entity ids near the river for the TERRA / NEREID gizmo shots.
import math
mapdoc = json.load(open(os.path.join(os.environ["TERRA_PROJECT"], "maps", "valley.json")))
def nearest(prefix, x, z):
    best = None
    for e in mapdoc["entities"]:
        if not e.get("type", "").startswith(prefix): continue
        px, pz = e["pos"]
        d = (px - x) ** 2 + (pz - z) ** 2
        if best is None or d < best[0]: best = (d, e["id"], px, pz)
    return best
tree = nearest("TerraTree", 275, 258)
tile = nearest("TerraTerrain", 300, 300)
river = nearest("nereid", 256, 128)
print("gizmo targets", tree, tile, river, flush=True)
def ui_shot(tag, **cam):
    for _ in range(5):
        r = call("view_screenshot", window="scene", clean=False, **cam)
        if "png_base64" in r:
            open(os.path.join(out, tag + ".png"), "wb").write(base64.b64decode(r["png_base64"]))
            return
        time.sleep(1)
info = call("map_info")
call("view_focus", x=256, z=256, distance=90)
time.sleep(15)   # 1391 entities, 48 terrain tiles, NEREID river upload
results = []
def measure(tag, **cam):
    shot(tag, **cam)
    time.sleep(2.5)
    shot(tag, **cam)
    time.sleep(1.0)
    st = min((call("geometry_stats") for _ in range(3)), key=lambda r: r.get("drawCalls", 1 << 30))
    row = {"view": tag}
    for k in ("drawCalls", "cameraModelTriangles", "sunShadowModelTriangles", "completedGpuFrameMs",
              "cameraLodNodes", "nereidConfiguredSurfaces"):
        if k in st: row[k] = st[k]
    results.append(row)
water = call("nereid_water_state")
with open(os.path.join(out, "nereid.json"), "w") as f:
    json.dump(water, f, indent=1)
# Before PHYSIS: authored scene.
measure("overview", x=256, y=8, z=256, distance=160, pitch=40, yaw=160)
measure("river-up", x=261, y=1.2, z=300, distance=60, pitch=35, yaw=180)
measure("river-down", x=258, y=1.2, z=200, distance=34, pitch=16, yaw=0)
measure("forest-edge", x=300, y=14, z=250, distance=90, pitch=40, yaw=250)
# PHYSIS nature preview: grass + wind, then a grass fire on the east bank.
call("physis_nature_preview", enabled=True, playing=True, grass=True, flames=True, sound=False,
     windSpeed=9, windDirection=20, humidity=0.1, cellMeters=4)
time.sleep(6)
measure("physis-wind", x=262, y=1.2, z=205, distance=45, pitch=35, yaw=0)
fire = call("physis_nature_preview", ignite={"x": 271, "z": 207}, advanceTicks=8)
time.sleep(3)
measure("physis-fire", x=262, y=1.2, z=205, distance=45, pitch=35, yaw=0)
fire2 = call("physis_nature_preview", advanceTicks=40)
time.sleep(3)
measure("physis-fire-later", x=262, y=1.2, z=205, distance=55, pitch=40, yaw=0)
fire = {"first": fire, "later": fire2}
measure("physis-overview", x=256, y=8, z=256, distance=120, pitch=24, yaw=110)
# Direction gizmos (viewport UI, so clean:false captures).
call("view_gizmos", physisWind=True, physisFire=True, nereidFlow=True, terraSlope=True,
     terraTreeWind=True, radius=60, spacing=6, select=river[1])
time.sleep(1.5)
ui_shot("gizmo-nereid-flow", x=258, y=1.2, z=230, distance=70, pitch=40, yaw=0)
call("view_gizmos", select=tree[1])
time.sleep(1.0)
ui_shot("gizmo-physis-wind-fire-tree", x=tree[2], y=2, z=tree[3], distance=60, pitch=45, yaw=220)
call("view_gizmos", select=river[1])
ui_shot("gizmo-fire-bank", x=262, y=1.2, z=205, distance=55, pitch=45, yaw=0)
# Erosion zones: authored areas where the ground erodes, with coefficients.
call("erosion_zone_set", id="outer-bend", shape="circle", x=250, z=212, radius=12, soil="sand", coefficient=2.5)
call("erosion_zone_set", id="clay-bank", shape="circle", x=272, z=196, radius=9, soil="clay", coefficient=0.6)
call("erosion_zone_set", id="rock-ledge", shape="polygon", points=[[266, 222], [276, 220], [278, 230], [268, 232]],
     soil="rock", coefficient=0)
zones = call("erosion_zones")
call("view_gizmos", physisWind=False, physisFire=False, erosionZones=True, select=river[1])
time.sleep(1.0)
ui_shot("gizmo-erosion-zones", x=262, y=1.2, z=212, distance=60, pitch=55, yaw=0)
call("undo"); call("undo"); call("undo")   # leave the review map as authored

with open(os.path.join(out, "alpine.json"), "w") as f:
    json.dump({"map": info, "fire": fire, "frames": results}, f, indent=1)
for r in results:
    print("  ", json.dumps(r), flush=True)
print("alpine-check: done", flush=True)
