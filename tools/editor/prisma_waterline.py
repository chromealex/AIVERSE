#!/usr/bin/env python3
"""PRISMA lensWater waterline on AlpineValley: the camera bobs through the
river surface; frames for a comparison video. Then the same without the effect.

Beauty shots of AlpineValley (review copy): hero vistas from the project's
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
def look(eye, target):
    vx, vy, vz = (target[i] - eye[i] for i in range(3))
    L = math.sqrt(vx * vx + vy * vy + vz * vz); k = min(1.0, 110.0 / L)
    t = [eye[i] + (target[i] - eye[i]) * k for i in range(3)]
    dx, dy, dz = eye[0] - t[0], eye[1] - t[1], eye[2] - t[2]
    d = math.sqrt(dx * dx + dy * dy + dz * dz)
    return dict(x=t[0], y=t[1], z=t[2], distance=d, pitch=math.degrees(math.asin(dy / d)), yaw=math.degrees(math.atan2(dx, dz)))
def frame(tag, **cam):
    for _ in range(5):
        r = call("view_screenshot", window="scene", clean=True, width=1280, height=720, **cam)
        if "png_base64" in r:
            open(os.path.join(out, tag + ".png"), "wb").write(base64.b64decode(r["png_base64"])); return
        time.sleep(0.5)
print(call("postfx_add", type="lensWater", enabled=True, amount=0.85, dropSize=14, density=0.6, distortion=14,
           flowSpeed=30, dryTime=5, film=0.25, streaks=0.6, glint=0.7, waterline=True, meniscus=7,
           waveHeight=0.07, waveLength=0.9, waveSpeed=1.3, lensDistance=0.25), flush=True)
idx = 14
x, z, half = centre[idx]
bed = call("view_ground", points=[[x, z]])["heights"][0]
water = call("nereid_water_state")
surf = water["surfaces"][0]
print("bed", bed, "surfaceY", surf.get("surfaceY"), flush=True)
tx, tz = centre[idx + 10][0], centre[idx + 10][1]
# Scan heights once to find the waterline (frames named by height), then bob.
for i in range(36):
    t = i / 36.0
    y = bed + 0.9 + 0.75 * math.sin(t * 2 * math.pi * 2)   # two bobs through the surface
    frame(f"bob-{i:02d}", **look((x, y, z), (tx, bed + 6 + 8 * t, tz)))
    time.sleep(0.15)
call("postfx_add", type="lensWater", enabled=False)
frame("bob-off-under", **look((x, bed + 0.5, z), (tx, bed + 6, tz)))
frame("bob-off-above", **look((x, bed + 1.6, z), (tx, bed + 6, tz)))
print("prisma-waterline: done", flush=True)
