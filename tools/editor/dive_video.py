#!/usr/bin/env python3
"""PRISMA lensWater dive video on the AlpineValley review copy: the camera
sinks through the river surface, stays under, surfaces and holds while the
water film drains and rivulets/drops remain. Frames dive-NNN.png (1280x720);
assemble with ffmpeg. Nothing is saved.

usage: dive_video.py PORT OUT_DIR   (TERRA_PROJECT = review copy)
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

call("postfx_add", type="lensWater", enabled=True, amount=0.9, dropSize=14, density=0.6, distortion=12,
     flowSpeed=30, dryTime=7, film=0.2, streaks=0.6, glint=0.7, waterline=True, meniscus=7,
     waveHeight=0.06, waveLength=0.9, waveSpeed=1.2, lensDistance=0.25, sheet=0.9, sheetTime=2.8)
# Clearer river for the dive (turbidity 0.1): the bed stays readable under water.
comp = json.loads(json.dumps(river["components"]))
comp["NereidWater"]["style"]["turbidity"] = float(os.environ.get("DIVE_TURBIDITY", "0.1"))
call("entity_update", id=river["id"], set={"components": comp})
time.sleep(3)
idx = 14
x, z, half = centre[idx]
bed = call("view_ground", points=[[x, z]])["heights"][0]
surf = bed + 1.55
tx, tz = centre[idx + 10][0], centre[idx + 10][1]
def ease(t): return t * t * (3 - 2 * t)
path = []
path += [surf + 0.5] * 4                                              # above
path += [surf + 0.5 - 0.42 * ease(k / 8) for k in range(9)]           # slow approach
path += [surf + 0.08 * math.sin(k / 24 * 2 * math.pi * 2) for k in range(24)]  # at the line
path += [surf - 0.08 - 0.6 * ease(k / 8) for k in range(9)]           # dive under
path += [surf - 0.68] * 8                                             # under water
path += [surf - 0.68 + 0.66 * ease(k / 8) for k in range(9)]          # rise to the line
path += [surf + 0.06 * math.sin(k / 14 * 2 * math.pi) for k in range(14)]       # at the line
path += [surf + 0.06 + 0.3 * ease(k / 4) for k in range(5)]           # emerge
path += [surf + 0.36] * 40                                            # film drains, drops remain
for i, y in enumerate(path):
    pitch_t = surf + 3.5
    frame(f"dive-{i:03d}", **look((x, y, z), (tx, pitch_t, tz)))
    time.sleep(0.09)
print("dive-video: done", len(path), flush=True)
