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
def ground(pts):
    return call("view_ground", points=[[x, z] for x, z in pts])["heights"]
def look(eye, target):
    """Orbit camera from an eye/target pair (target pulled within 110 m on the same ray)."""
    vx, vy, vz = (target[i] - eye[i] for i in range(3))
    L = math.sqrt(vx * vx + vy * vy + vz * vz); k = min(1.0, 110.0 / L)
    t = [eye[i] + (target[i] - eye[i]) * k for i in range(3)]
    dx, dy, dz = eye[0] - t[0], eye[1] - t[1], eye[2] - t[2]
    d = math.sqrt(dx * dx + dy * dy + dz * dz)
    return dict(x=t[0], y=t[1], z=t[2], distance=d, pitch=math.degrees(math.asin(dy / d)), yaw=math.degrees(math.atan2(dx, dz)))
def beauty(tag, **cam):
    shot(tag, **cam); time.sleep(2.5); shot(tag, **cam)
# Hero: standing in the river looking up and down the valley at several stations.
heights = ground([(x, z) for x, z, _ in centre])
for idx in (6, 14, 24, 40, 52):
    x, z, half = centre[idx]
    bed = heights[idx]
    for dirn, tag in ((+1, "up"), (-1, "down")):
        j = max(0, min(len(centre) - 1, idx + dirn * 12))
        tx, tz = centre[j][0], centre[j][1]
        eye = (x, bed + 3.2, z)
        target = (tx, bed + 3.2 + 14, tz)
        beauty(f"hero-{idx:02d}-{tag}", **look(eye, target))
# NEREID card: on the bank, low over the water, looking along the current.
x, z, half = centre[20]
bank = (x + half + 3, z)
gb = ground([bank])[0]
beauty("nereid-card", **look((bank[0], gb + 2.2, bank[1]), (centre[26][0], heights[26] + 0.5, centre[26][1])))
beauty("nereid-card-b", **look((centre[30][0] - half - 2, heights[30] + 1.6, centre[30][1]), (centre[35][0], heights[35] + 0.3, centre[35][1])))
# PHYSIS card: a grass fire front on the meadow, wind blowing along the valley.
call("physis_nature_preview", enabled=True, playing=False, grass=True, flames=True, sound=False,
     windSpeed=7, windDirection=90, humidity=0.06, cellMeters=2)
time.sleep(6)
mx, mz = 244, 200
gm = ground([(mx, mz), (mx + 26, mz)])
for k in range(-4, 5):
    call("physis_nature_preview", ignite={"x": mx - 6, "z": mz + k * 2.0})
call("physis_nature_preview", advanceTicks=8)
time.sleep(4)
beauty("physis-card", **look((mx + 26, gm[1] + 2.6, mz - 4), (mx - 6, gm[0] + 1.0, mz + 6)))
call("physis_nature_preview", advanceTicks=6)
time.sleep(4)
beauty("physis-card-b", **look((mx + 22, gm[1] + 4.0, mz - 14), (mx - 4, gm[0] + 0.8, mz + 10)))
print("alpine-beauty: done", flush=True)
