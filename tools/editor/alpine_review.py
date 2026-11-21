#!/usr/bin/env python3
"""AlpineValley review copy, after the TERRA detail/anti-tiling and NEREID
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
surf = [b + 1.55 for b in beds]      # surface ~1.55 m above the carved bed
call("physis_nature_preview", enabled=True, playing=False, grass=True, flames=True, sound=False,
     windSpeed=4, windDirection=0, humidity=0.06, cellMeters=2)
time.sleep(6)
# A. TERRA ground close-ups: bank slopes seen from the river, like the report.
for idx in (10, 18, 24):
    x, z, half = centre[idx]; (nx, nz), (fx, fz) = side(idx)
    for s, tag in ((1, "L"), (-1, "R")):
        bx, bz = x + nx * s * (half + 6), z + nz * s * (half + 6)
        g = ground([(bx, bz)])[0]
        eye = (x - fx * 3, surf[idx] + 1.3, z - fz * 3)
        beauty(f"ground-{idx}-{tag}", **look(eye, (bx + fx * 4, g + 0.3, bz + fz * 4)))
# Meadow close-up from 2 m, looking down at the grass/ground mix.
mead = sorted((river_dist(gx, gz)[0], gx, gz) for gx, gz in grass if 10 < river_dist(gx, gz)[0] < 60 and min(math.hypot(gx - tx, gz - tz) for tx, tz in trees) > 9)[:1]
for _, gx, gz in mead:
    g = ground([(gx, gz)])[0]
    beauty("ground-meadow", **look((gx - 6, g + 2.2, gz - 6), (gx, g, gz)))
# B. Hero (low camera in the river, toward the peaks).
for idx in (8, 12):
    x, z, _ = centre[idx]; j = min(len(centre) - 1, idx + 14)
    eye = (x, surf[idx] + 1.1, z)
    beauty(f"hero-{idx:02d}", **look(eye, (centre[j][0], eye[1] + 34, centre[j][1])))
# C. NEREID card: from the bank, river curving downstream.
for idx in (12, 20):
    x, z, half = centre[idx]; (nx, nz), (fx, fz) = side(idx)
    ex, ez = x + nx * (half - 1.0), z + nz * (half - 1.0)
    j = min(len(centre) - 1, idx + 6)
    beauty(f"nereid-card-{idx}", **look((ex, surf[idx] + 2.4, ez), (centre[j][0] - nx * half * 0.4, surf[j] - 0.5, centre[j][1] - nz * half * 0.4)))
# D. Turbidity sweep: same frames outside and diving, 0 / 0.22 / 0.6.
comp = river["components"]
for tval in (0.0, 0.25, 0.6):
    c2 = copy.deepcopy(comp); c2["NereidWater"]["style"]["turbidity"] = tval
    call("entity_update", id=river["id"], set={"components": c2})
    time.sleep(4)
    idx = 16; x, z, half = centre[idx]; (nx, nz), (fx, fz) = side(idx)
    ex, ez = x + nx * (half - 1.0), z + nz * (half - 1.0)
    tag = f"turbidity-{int(tval*100):02d}"
    beauty(tag + "-outside", **look((ex, surf[idx] + 1.8, ez), (x + fx * 5 - nx * 1.5, beds[idx], z + fz * 5 - nz * 1.5)))
    j = min(len(centre) - 1, idx + 6)
    beauty(tag + "-under", **look((x, surf[idx] - 0.6, z), (centre[j][0], beds[j] + 0.4, centre[j][1])))
c2 = copy.deepcopy(comp); call("entity_update", id=river["id"], set={"components": c2}); time.sleep(3)
# E. PHYSIS card: fire on an open meadow by the river, camera 8 m away.
cands = []
for gx, gz in grass:
    rd, ri = river_dist(gx, gz)
    if not (6 < rd < 60): continue
    if min(math.hypot(gx - tx, gz - tz) for tx, tz in trees) < 7: continue
    cands.append((rd, gx, gz, ri))
cands.sort()
print("meadows", len(cands), cands[:3], flush=True)
for n, (rd, gx, gz, ri) in enumerate(cands[:2]):
    for k in range(-3, 4):
        call("physis_nature_preview", ignite={"x": gx + k * 1.2, "z": gz})
    call("physis_nature_preview", advanceTicks=16)
    time.sleep(4)
    # Of 8 directions, the eye point with the most room from trees.
    best = None
    for k in range(8):
        a = k * math.pi / 4
        px_, pz_ = gx + math.cos(a) * 9, gz + math.sin(a) * 9
        clear = min(math.hypot(px_ - tx, pz_ - tz) for tx, tz in trees)
        if best is None or clear > best[0]: best = (clear, px_, pz_)
    ex, ez = best[1], best[2]
    g = ground([(gx, gz), (ex, ez)])
    st = call("physis_nature_preview")
    print("card", n, st.get("flames"), st.get("burningAreaM2"), flush=True)
    call("physis_nature_preview", playing=True)
    time.sleep(3)
    beauty(f"physis-card-{n}p", **look((ex, g[1] + 1.4, ez), (gx, g[0] + 0.7, gz)))
    call("physis_nature_preview", playing=False)
    beauty(f"physis-card-{n}", **look((ex, g[1] + 1.4, ez), (gx, g[0] + 0.7, gz)))
call("physis_nature_preview", enabled=False)
# F. PRISMA waterline bob, centred on the real surface so it surfaces twice.
call("postfx_add", type="lensWater", enabled=True, amount=0.85, dropSize=14, density=0.6, distortion=14,
     flowSpeed=30, dryTime=5, film=0.25, streaks=0.6, glint=0.7, waterline=True, meniscus=7,
     waveHeight=0.07, waveLength=0.9, waveSpeed=1.3, lensDistance=0.25)
idx = 14; x, z, _ = centre[idx]
tx, tz = centre[idx + 10][0], centre[idx + 10][1]
# Two dives: 6 frames under, surface, 18 frames dry while the film drains.
frames = []
for cyc in range(2):
    frames += [surf[idx] - 0.45 + 0.05 * k for k in range(6)]
    frames += [surf[idx] + 0.18 + 0.004 * k for k in range(18)]
for i, y in enumerate(frames):
    t = i / len(frames)
    shot2(f"bob-{i:02d}", 1280, 720, **look((x, y, z), (tx, surf[idx] + 4 + 6 * t, tz)))
    time.sleep(0.08)
print("alpine-review: done", flush=True)
