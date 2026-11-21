#!/usr/bin/env python3
"""Drive a running aiverse_editor over its agent link (--listen PORT) and
capture beauty + debug screenshots of hierarchical wind masks on a
TerraWeave landscape: generated relief, procedural Arbor forest, PHYSIS
nature-preview grass and wind. Editor-only; the map is never saved.

usage: terra_wind_shots.py PORT OUT_DIR
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
call("map_new", width=N, height=N, name="TerraWind")
call("terraweave_generate", seed=20261121, baseHeightCm=0, reliefCm=1400,
     basePeriodCm=9000, octaves=6, cellSizeCm=100, overwrite=True)
# map_new defaults to entity-presented terrain; show the generated heightfield surface.
call("map_set", path="terrainPresentation", value="legacy")
call("lighting_set", time=0.4, dayLength=0, clouds=0.25)
call("postfx_preset", preset="vivid")
call("view_focus", x=96, z=96, distance=80)
time.sleep(3)
shot("diag-terrain-only", x=96, y=height(96, 96) + 5, z=96, distance=80, pitch=25, yaw=35)
r = call("view_screenshot", window="all")
if "png_base64" in r:
    open(os.path.join(out, "diag-editor.png"), "wb").write(base64.b64decode(r["png_base64"]))
call("view_wind", forest=True, hierarchical=True, lodScale=3, nature=True, grass=True,
     flames=False, sound=False, playing=True, windSpeed=0, windDirection=25,
     grassQuality=2, debug="off")

# Wide establishing shot + two close tree shots. Camera (x,z) in cells.
views = [("wide", 96, 96, 80, 14, 35), ("grove", 88, 104, 34, 8, 140), ("tree", 100, 90, 22, 7, 220)]
def cams():
    for tag, x, z, dist, pitch, yaw in views:
        yield tag, dict(x=x, y=height(x, z) + 5, z=z, distance=dist, pitch=pitch, yaw=yaw)

call("view_focus", x=96, z=96, distance=70)
time.sleep(6)  # forest + grass build, PHYSIS frame
for tag, cam in cams():
    shot(f"{tag}-0-calm", **cam)

call("view_wind", windSpeed=16)
time.sleep(4)
for tag, cam in cams():
    shot(f"{tag}-1-wind-a", **cam)
    time.sleep(0.7)
    shot(f"{tag}-2-wind-b", **cam)

for view in ("parts", "depth", "weight"):
    call("view_wind", debug=view)
    time.sleep(1.5)
    for tag, cam in list(cams())[1:]:
        shot(f"{tag}-3-{view}", **cam)
call("view_wind", debug="off")
print("terra-wind-shots: done", flush=True)
