"""Exercise the ground overlay panel in macOS Studio. Pass a built app and a scratch directory."""
import argparse
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
