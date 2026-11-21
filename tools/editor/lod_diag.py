#!/usr/bin/env python3
"""LOD diagnostics on AlpineValley (bank close-up like the user report, overview).
Was: AlpineValley review copy, after the TERRA detail/anti-tiling and NEREID
turbidity changes: ground close-ups (bank slopes, meadow), hero, NEREID and
PHYSIS card frames, a turbidity sweep (0 / 0.22 / 0.6, outside + diving)
and the PRISMA waterline bob through the real surface. Nothing is saved.

usage: alpine_review.py PORT OUT_DIR   (TERRA_PROJECT = review copy)
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


import math, copy
call("map_open", file="maps/valley.json")
call("view_focus", x=256, z=256, distance=90)
time.sleep(15)
proj = os.environ["TERRA_PROJECT"]
mapdoc = json.load(open(os.path.join(proj, "maps", "valley.json")))
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
def shot2(tag, w=1920, h=1080, **cam):
    for _ in range(5):
        r = call("view_screenshot", window="scene", clean=True, width=w, height=h, **cam)
        if "png_base64" in r:
            open(os.path.join(out, tag + ".png"), "wb").write(base64.b64decode(r["png_base64"])); return
        time.sleep(0.5)
def beauty(tag, **cam):
    shot2(tag, **cam); time.sleep(2.5); shot2(tag, **cam)
def river_dist(x, z):
    best = (1e9, 0)
    for i, (cx, cz, h) in enumerate(centre):
        d = math.hypot(x - cx, z - cz) - h
        if d < best[0]: best = (d, i)
    return best
def side(i):
    a = centre[max(0, i - 1)]; b = centre[min(len(centre) - 1, i + 1)]
    dx, dz = b[0] - a[0], b[1] - a[1]; L = math.hypot(dx, dz) or 1
    return (-dz / L, dx / L), (dx / L, dz / L)
beds = ground([(x, z) for x, z, _ in centre])
surf = [b + 1.55 for b in beds]
res = {}
def stats(tag, **cam):
    shot2(tag, 1280, 720, **cam); time.sleep(1.5); shot2(tag, 1280, 720, **cam)
    st = call("geometry_stats")
    res[tag] = {k: st.get(k) for k in ("drawCalls", "cameraModelTriangles", "cameraMeshTriangles", "sunShadowModelTriangles", "cameraLodNodes", "lodChains", "completedGpuFrameMs", "strata")}
    print("STATS", tag, json.dumps(res[tag]), flush=True)
idx = 10; x, z, half = centre[idx]; (nx, nz), (fx, fz) = side(idx)
bx, bz = x + nx * (half + 6), z + nz * (half + 6)
g = ground([(bx, bz)])[0]
stats("bank", **look((x - fx * 3, surf[idx] + 1.3, z - fz * 3), (bx + fx * 4, g + 0.3, bz + fz * 4)))
j = min(len(centre) - 1, 22)
stats("hero", **look((centre[8][0], surf[8] + 1.1, centre[8][1]), (centre[j][0], surf[8] + 35, centre[j][1])))
stats("overview", x=256, y=0, z=256, distance=260, pitch=40, yaw=200)
call("view_wind", forest=False, nature=False)
stats("bank-noflora", **look((x - fx * 3, surf[idx] + 1.3, z - fz * 3), (bx + fx * 4, g + 0.3, bz + fz * 4)))
json.dump(res, open(os.path.join(out, "lod.json"), "w"), indent=1)
print("lod-diag: done", flush=True)
