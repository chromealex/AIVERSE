#!/usr/bin/env python3
"""Meadow grass QA: drive a running aiverse_editor (--listen PORT) and capture
before/after screenshots of TerraWeave grass in the PHYSIS nature preview:
legacy uniform blades vs meadow species/clumps, close-up at eye height, mid
field, field to the horizon, top view of clumps, wind on, wildfire.
Also records geometry_stats (draw calls, triangles, GPU ms) per camera.
Editor-only: the map is generated in memory and never saved.

usage: grass_fields_shots.py PORT OUT_DIR [modes,comma,separated]
"""
import base64, json, os, socket, sys, time

port, out = int(sys.argv[1]), sys.argv[2]
modes = (sys.argv[3] if len(sys.argv) > 3 else "legacy,meadow,dryMeadow,lushMeadow").split(",")
os.makedirs(out, exist_ok=True)

def connect():
    for _ in range(300):
        try:
            s = socket.create_connection(("127.0.0.1", port), timeout=5)
            s.settimeout(180)
            return s
        except OSError:
            time.sleep(1)
    sys.exit("editor agent link not reachable")

sock = connect(); buf = b""; seq = 0

def _hello():
    global buf
    directory = os.environ.get("AIVERSE_AGENT_LINK_DIR", os.path.expanduser("~/.aiverse/agent-link"))
    path = os.path.join(directory, "%d.token" % port)
    for _ in range(120):
        if os.path.exists(path): break
        time.sleep(1)
    if os.path.exists(path):
        token = open(path).read().strip()
        sock.sendall((json.dumps({"jsonrpc": "2.0", "id": 0, "method": "hello", "token": token}) + "\n").encode())
        while b"\n" not in buf:
            chunk = sock.recv(1 << 20)
            if not chunk: sys.exit("editor closed the link during hello")
            buf += chunk
        line, buf = buf.split(b"\n", 1)
        if not json.loads(line).get("result", {}).get("ok"):
            raise RuntimeError("Editor authentication failed")
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
            r = reply.get("result", reply.get("error", {}))
            if _tool != "view_screenshot" and (not isinstance(r, dict) or "error" in r):
                raise RuntimeError(f"{_tool}: {r}")
            short = {k: v for k, v in r.items() if k != "png_base64"} if isinstance(r, dict) else r
            print(f"{_tool}: {json.dumps(short, ensure_ascii=False)[:400]}", flush=True)
            return r if isinstance(r, dict) else {}

stats_log = open(os.path.join(out, "stats.jsonl"), "a")
def shot(name, **cam):
    for attempt in range(5):
        r = call("view_screenshot", window="scene", clean=True, width=1600, height=900, **cam)
        if "png_base64" in r:
            with open(os.path.join(out, name + ".png"), "wb") as f:
                f.write(base64.b64decode(r["png_base64"]))
            break
        time.sleep(1)
    else:
        raise RuntimeError(f"No scene screenshot for {name}")
    # counters of the frame just rendered with this camera
    time.sleep(0.4)
    st = call("geometry_stats")
    keep = {k: st.get(k) for k in ("drawCalls", "cameraModelTriangles", "cameraMeshTriangles", "completedGpuFrameMs")}
    keep["shot"] = name
    stats_log.write(json.dumps(keep) + "\n"); stats_log.flush()

def height(x, z):
    r = call("terrain_heights", points=[[x, z]])
    try: return float(r["heights"][0])
    except Exception: return 0.0

N = 256
call("map_new", width=N, height=N, name="GrassFields")
call("terraweave_generate", seed=20261122, baseHeightCm=0, reliefCm=500,
     basePeriodCm=16000, octaves=4, cellSizeCm=100, overwrite=True)
call("map_set", path="terrainPresentation", value="legacy")
call("lighting_set", time=0.38, dayLength=0, clouds=0.3)
call("map_set", path="postfx", value={"replace": True, "effects": [{"type": "fxaa"}]})
cx, cz = 128, 128
call("view_focus", x=cx, z=cz, distance=30)
time.sleep(3)

def cams():
    g = height(cx, cz)
    return [
        ("1-close", dict(x=cx, y=g + 0.55, z=cz, distance=3.2, pitch=19, yaw=35)),   # eye ~1.6 m
        ("2-mid", dict(x=cx, y=g + 0.3, z=cz, distance=14, pitch=16, yaw=35)),
        ("3-horizon", dict(x=cx, y=g + 1.45, z=cz, distance=1.5, pitch=6, yaw=35)),  # eye ~1.6 m, look far
        ("4-top", dict(x=cx, y=g, z=cz, distance=9, pitch=62, yaw=35)),
    ]

def wait_for_grass():
    previous = 0
    stable = 0
    for _ in range(60):
        time.sleep(3)
        count = call("view_wind")["grassPatches"]
        stable = stable + 1 if count and count == previous else 0
        if stable >= 4:
            return
        previous = count
    raise RuntimeError("Grass patch uploads did not settle")

for mode in modes:
    # Exercise the persisted map setting and the same auto mode used by old maps.
    if mode not in ("auto", "legacy"):
        call("terraweave_meadow_set", preset=mode)
        assert call("terraweave_meadow_get")["meadow"] == mode
    call("view_wind", forest=False, nature=True, grass=True, flames=False, sound=False,
         playing=True, windSpeed=0.5, windDirection=25, grassQuality=2, grassMeadow=("legacy" if mode == "legacy" else "auto"), debug="off")
    call("view_focus", x=cx, z=cz, distance=30)
    wait_for_grass()
    for tag, cam in cams():
        shot(f"{mode}-{tag}", **cam)
    call("view_wind", windSpeed=15)
    time.sleep(3)
    for tag, cam in cams()[:2]:
        shot(f"{mode}-{tag}-wind-a", **cam)
        time.sleep(0.6)
        shot(f"{mode}-{tag}-wind-b", **cam)
    call("view_wind", windSpeed=0.5)

# Wildfire over meadow grass (charred stubble + burning blades).
if "meadow" in modes:
    call("view_wind", forest=False, nature=True, grass=True, flames=True, sound=False, playing=False,
         windSpeed=6, windDirection=90, grassQuality=2, grassMeadow="meadow")
    call("physis_nature_preview", enabled=True, playing=False, grass=True, flames=True, sound=False,
         windSpeed=6, windDirection=90, humidity=0.06, cellMeters=2)
    time.sleep(6)
    for k in range(-3, 4):
        call("physis_nature_preview", ignite={"x": cx - 4, "z": cz + k * 2.0})
    for step in range(3):
        call("physis_nature_preview", advanceTicks=6)
        time.sleep(4)
        g = height(cx, cz)
        shot(f"fire-{step}", x=cx, y=g, z=cz, distance=16, pitch=30, yaw=180)
print("grass-fields-shots: done", flush=True)
