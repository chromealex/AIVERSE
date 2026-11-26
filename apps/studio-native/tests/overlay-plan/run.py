"""Exercise the ground overlay panel in macOS Studio. Pass a built app and a scratch directory."""
import argparse
import struct
import zlib
import json
import re
import sys
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

parser = argparse.ArgumentParser()
parser.add_argument("bundle", type=Path)
parser.add_argument("scratch", type=Path)
args = parser.parse_args()
here = Path(__file__).resolve().parent
root = here.parents[3]


def stack(region, panel):
    return dict(id="dock-" + region, kind="stack", tabs=[panel], active=panel)


def split(node, ratio, first, second):
    return dict(id=node, kind="split", axis="horizontal", ratio=ratio, first=first, second=second)


with tempfile.TemporaryDirectory(dir=args.scratch) as temp:
    work = Path(temp)
    project = work / "project"
    shutil.copytree(root / "demos/order-upgrade", project)
    map_file = project / "maps/courtyard.json"
    map_data = json.loads(map_file.read_text())
    rows = map_data["terrain"]["rows"]
    rows[4] = rows[4][:3] + "~~" + rows[4][5:]
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    background = project / "minimap.png"
    background.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 6, 0, 0, 0)) +
                           chunk(b"IDAT", zlib.compress(bytes([0,26,53,65,128]))) + chunk(b"IEND", b""))
    def badge(x, y):
        if abs(x-5.5)+abs(y-5.5)>7: return bytes(4)
        return bytes([200,96,42,255] if x<6 else [50,172,220,255])
    def badge_png(size, inset):
        scanlines = b"".join(b"\0"+b"".join(badge(x+inset,y+inset) for x in range(size)) for y in range(size))
        return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size,size,8,6,0,0,0))
                + chunk(b"IDAT", zlib.compress(scanlines)) + chunk(b"IEND", b""))
    (project / "beacon.png").write_bytes(badge_png(12,0))
    atlas_dir = project / "_atlas"
    atlas_dir.mkdir(exist_ok=True)
    (atlas_dir / "icons.png").write_bytes(badge_png(8,2))
    (atlas_dir / "atlas.json").write_text(json.dumps(dict(version=1,
        pages=[dict(file="_atlas/icons.png",w=8,h=8)],
        textures={"hud/badge.png":[0,0,0,8,8,2,2,12,12,0]})))
    map_data["minimapBackground"] = background.name
    original = json.dumps(map_data)
    map_file.write_text(original)
    (map_file.parent / "second.json").write_text(original)
    rules = project / "rules.json"
    data = json.loads(rules.read_text())
    data["overlays"] = [
        dict(id="sensor_radius", when="never", anchor="selected", shape=dict(type="ring", radius=2.5),
             style=dict(color="#16eaafff", thickness=.15, fadeIn=.4)),
        dict(id="build_site", anchor="ghost", shape=dict(type="footprint", predicate="ok"),
             style=dict(fill=True, outline=False, fadeIn=.4, colors={"true": "#f6bf4aff", "false": "#db3854ff"})),
        dict(id="beacon", order=10, anchor=dict(type="point", at=[5,12]), shape=dict(type="icon",size=3,image="beacon.png"),
             style=dict(space="ground",rotate=1.5707963,color="#ffffffff")),
        dict(id="beacon_center", order=11, anchor=dict(type="point", at=[5,12]), shape=dict(type="circle",radius=.3),
             style=dict(color="#ceb7ffff")),
        dict(id="banner", anchor=dict(type="point", at=[18,4]), shape=dict(type="icon",size=3,image="beacon.png"),
             style=dict(space="billboard",rotate=1.5707963,color="#ffffffff")),
        dict(id="status_badge", anchor=dict(type="screen", at=[58,38]), shape=dict(type="icon",size=30,image="hud/badge.png"),
             style=dict(color="#80ffffff")),
        dict(id="banner_frame", anchor=dict(type="point", at=[18,4]), shape=dict(type="rect",size=[3.5,4]),
             style=dict(space="billboard",fill=False,outline=True,thickness=.25,color="#99bd88ff")),
        dict(id="missing_badge", anchor="cursor", shape=dict(type="icon",size=2,image="missing.png")),
        dict(id="patrol_signal", anchor=dict(type="point", at=[5,9]), shape=dict(type="ring", radius=2),
             style=dict(space="minimap", color="#ffbd59ff")),
        dict(id="patrol_zone", anchor=dict(type="point", at=[16,3]), shape=dict(type="rect", size=[2,4]),
             style=dict(space="minimap", color="#68cfdeff")),
        dict(id="cursor_ping", anchor="cursor", shape=dict(type="circle", radius=1),
             style=dict(space="minimap", color="#e879a1ff")),
        dict(id="map_marker", anchor=dict(type="screen", at=[15,18]), shape=dict(type="circle", radius=6),
             style=dict(color="#8ac97dff", segments=8)),
        dict(id="map_badge", anchor=dict(type="screen", at=[32,18]), shape=dict(type="rect", size=[12,8]),
             style=dict(fill=True, outline=False, rounding=2, color="#efa087ff")),
        dict(id="hud_note", when="never", anchor=dict(type="screen", at=[-12,12]),
             shape=dict(type="text", text="Patrol · {selected} selected", size=1.1),
             style=dict(color="#ffffffff", background="#4a315cff", padding=[7,4], rounding=4, shadow=False)),
        dict(id="vitality", when="never", anchor="selected",
             shape=dict(type="bar", value="hp", width=20, widthStructure=28, height=4),
             style=dict(stops=[[1,"#e5697aff"]], background="#211422ff", offset=[0,-9])),
    ]
    rules.write_text(json.dumps(data))
    bundle = work / "PreviewTest.app"
    subprocess.run(["ditto", args.bundle, bundle], check=True)
    index = bundle / "Contents/Resources/studio-web/index.html"
    layout = dict(version=3, root=split("root", .22, stack("left", "hierarchy"),
                  split("body", .65, stack("center", "console"), stack("right", "inspector"))))
    setup = '''
localStorage.setItem('aiverse.studio.workspace','scene');
localStorage.setItem('aiverse.studio.ws.added.scene.agent','1');
localStorage.setItem('aiverse.studio.ws.added.scene.history','1');
localStorage.setItem('aiverse.studio.dock.v3.scene',LAYOUT);
window.testSceneWasPresent=false;
new MutationObserver(()=>{if(document.querySelector('.viewport'))window.testSceneWasPresent=true;})
.observe(document.documentElement,{childList:true,subtree:true});
'''.replace("LAYOUT", json.dumps(json.dumps(layout)))
    content = index.read_text().replace("<head>", "<head><script>" + setup + "</script>", 1)
    index.write_text(content.replace("</body>", "<script>" + (here / "check.js").read_text() + "</script></body>"))
    subprocess.run(["/usr/libexec/PlistBuddy", "-c", "Set :CFBundleIdentifier com.aiverse.studio.overlayplantest",
                    bundle / "Contents/Info.plist"], check=True)
    for target in (bundle / "Contents/MacOS/aiversemcp", bundle):
        subprocess.run(["codesign", "--force", "--sign", "-", target], check=True)
    log = work / "studio.log"
    with log.open("w") as output:
        host = subprocess.Popen([bundle / "Contents/MacOS/AiverseStudioHTML5", "--project", project],
                                stdout=output, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 120
            linked = False
            while host.poll() is None and time.monotonic() < deadline:
                messages = log.read_text(errors="replace")
                match = re.search(r"CROSS main LINK_PORT (\d+)", messages)
                if match and not linked:
                    subprocess.run([sys.executable, here / "linked.py", bundle / "Contents/MacOS/aiversemcp",
                                    root, match.group(1), log], timeout=55, check=True)
                    linked = True
                if "CROSS main FAIL" in messages or "CROSS main DONE_1" in messages:
                    break
                time.sleep(.15)
            messages = log.read_text(errors="replace")
            if "CROSS main DONE_1" not in messages or "CROSS main FAIL" in messages:
                raise RuntimeError(messages[-4000:])
            print(next(line for line in messages.splitlines() if "CROSS main VERIFIED" in line))
        finally:
            if host.poll() is None:
                host.terminate()
            host.wait(timeout=10)
    if map_file.read_text() != original:
        raise RuntimeError("Preview changed the authored map")
