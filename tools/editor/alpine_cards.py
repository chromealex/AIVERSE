#!/usr/bin/env python3
"""Beauty shots of AlpineValley (review copy): hero vistas from the project's
renderbench cameras, plus PHYSIS (fire + wind on the meadow) and NEREID
(river) card frames. Scene-only 1920x1080, nothing saved.

PHYSIS + TERRA + NEREID together on AlpineValley (review copy: JSON
copied, heavy assets symlinked; nothing is saved). Opens maps/valley.json,
turns on the PHYSIS nature preview (grass, wind, fire), reads NEREID water
state, and captures river / forest / fire / overview shots with
geometry_stats per shot (alpine.json).

Drive a running aiverse_editor over its agent link (--listen PORT) and
capture beauty + debug screenshots of hierarchical wind masks on a
TerraWeave landscape: generated relief, procedural Arbor forest, PHYSIS
nature-preview grass and wind. Editor-only; the map is never saved.

usage: alpine_beauty.py PORT OUT_DIR
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

import math
call("map_open", file="maps/valley.json")
call("view_focus", x=256, z=256, distance=90)
time.sleep(15)
mapdoc = json.load(open(os.path.join(os.environ["TERRA_PROJECT"], "maps", "valley.json")))
river = next(e for e in mapdoc["entities"] if "NereidWater" in e.get("components", {}))
rx, rz = river["pos"]
centre = [(rx + p[0], rz + p[1], p[2]) for p in river["components"]["NereidWater"]["points"]]
trees = [e["pos"] for e in mapdoc["entities"] if e.get("type", "").startswith("TerraTree")]
grass = [e["pos"] for e in mapdoc["entities"] if e.get("type", "").startswith("TerraGrass")]
def ground(pts):
    return call("view_ground", points=[[x, z] for x, z in pts])["heights"]
def look(eye, target):
    vx, vy, vz = (target[i] - eye[i] for i in range(3))
    L = math.sqrt(vx * vx + vy * vy + vz * vz); k = min(1.0, 110.0 / L)
    t = [eye[i] + (target[i] - eye[i]) * k for i in range(3)]
    dx, dy, dz = eye[0] - t[0], eye[1] - t[1], eye[2] - t[2]
    d = math.sqrt(dx * dx + dy * dy + dz * dz)
    return dict(x=t[0], y=t[1], z=t[2], distance=d, pitch=math.degrees(math.asin(dy / d)), yaw=math.degrees(math.atan2(dx, dz)))
def beauty(tag, **cam):
    shot(tag, **cam); time.sleep(2.5); shot(tag, **cam)
def river_dist(x, z):
    best = (1e9, 0)
    for i, (cx, cz, h) in enumerate(centre):
        d = math.hypot(x - cx, z - cz) - h
        if d < best[0]: best = (d, i)
    return best
# Hero variants: camera in the river, tilted up toward the peaks.
heights = ground([(x, z) for x, z, _ in centre])
for idx in (8, 12, 16, 20, 26):
    x, z, half = centre[idx]
    j = min(len(centre) - 1, idx + 14)
    eye = (x, heights[idx] + 2.6, z)
    beauty(f"hero2-{idx:02d}", **look(eye, (centre[j][0], heights[idx] + 2.6 + 34, centre[j][1])))
# PHYSIS card: open meadow next to the river, no tree within 9 m.
cands = []
for gx, gz in grass:
    rd, ri = river_dist(gx, gz)
    if not (6 < rd < 30): continue
    if min(math.hypot(gx - tx, gz - tz) for tx, tz in trees) < 9: continue
    cands.append((rd, gx, gz, ri))
cands.sort()
print("meadows", cands[:5], flush=True)
call("physis_nature_preview", enabled=True, playing=False, grass=True, flames=True, sound=False,
     windSpeed=6, windDirection=0, humidity=0.06, cellMeters=2)
time.sleep(5)
for n, (rd, gx, gz, ri) in enumerate(cands[:2]):
    for k in range(-2, 3):
        call("physis_nature_preview", ignite={"x": gx + k * 1.5, "z": gz})
    call("physis_nature_preview", advanceTicks=6)
    time.sleep(4)
    cx, cz, half = centre[ri]
    g = ground([(gx, gz), (cx, cz)])
    eye = (cx, g[1] + 2.4, cz)
    beauty(f"physis-card-{n}", **look(eye, (gx, g[0] + 1.2, gz)))
    st = call("physis_nature_preview")
    print("card", n, st.get("flames"), st.get("burningAreaM2"), flush=True)
    call("physis_nature_preview", advanceTicks=5)
    time.sleep(4)
    beauty(f"physis-card-{n}b", **look((eye[0], eye[1] + 3, eye[2]), (gx, g[0] + 0.8, gz)))
print("alpine-cards: done", flush=True)
