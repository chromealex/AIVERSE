#!/usr/bin/env python3
"""Self-test of tutorial.py on a synthetic BW map data asset (no game data needed)."""
import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))

MAP = """%YAML 1.1
%TAG !u! tag:unity3d.com,2011:
--- !u!114 &2673583075030234029
MonoBehaviour:
  m_Name: Tutorial
  beforeBattleChapters:
  - name:
    steps:
    - rid: 0
  tutorialChapters:
  - name:
    steps:
    - rid: 1
    - rid: 2
    - rid: 3
  - name: Move
    steps:
    - rid: 4
    - rid: 5
  references:
    version: 2
    RefIds:
    - rid: 0
      type: {class: SetBattleFlags, ns: MW2Data.Main.Database.Maps.TutorialActions, asm: Assembly-CSharp}
      data:
        method: 0
        flags: 65540
    - rid: 1
      type: {class: ShowWindow, ns: MW2Data.Main.Database.Maps.TutorialActions, asm: Assembly-CSharp}
      data:
        windowSource:
          guid: 9fd34371d6a8247eab2645b73adb75ed
          resourcePath: Assets/Screens/MPTutorial_1_Welcome
            Screen.prefab
    - rid: 2
      type: {class: Wait, ns: MW2Data.Main.Database.Maps.TutorialActions, asm: Assembly-CSharp}
      data:
        time: 0.5
    - rid: 3
      type: {class: GOTOChapter, ns: MW2Data.Main.Database.Maps.TutorialActions, asm: Assembly-CSharp}
      data:
        gotoChapters:
        - Move
        chaptersToDismiss: []
    - rid: 4
      type: {class: FingerTip, ns: MW2Data.Main.Database.Maps.TutorialActions, asm: Assembly-CSharp}
      data:
        action: 9
        fromType: 0
        indexFrom: 1
        shiftFrom: {x: 0, y: 2}
        toType: 1
        indexTo: 2
        moveDuration: 0.5
    - rid: 5
      type: {class: MoveUnits, ns: MW2Data.Main.Database.Maps.TutorialActions, asm: Assembly-CSharp}
      data:
        fromPoints: 0300000001000000
        toPoint: 4
        count: 50
"""


def main():
    with tempfile.TemporaryDirectory() as tmp:
        assets = os.path.join(tmp, "Assets")
        os.makedirs(os.path.join(assets, "Maps"))
        with open(os.path.join(assets, "Maps", "!Camp_10_Data.asset"), "w") as f:
            f.write(MAP)
        out = os.path.join(tmp, "game")
        os.makedirs(os.path.join(out, "ui", "hud"))
        open(os.path.join(out, "ui", "hud", "MPTutorial_1_Welcome Screen.rml"), "w").close()
        with open(os.path.join(out, "rules.json"), "w") as f:
            f.write('// rules\n{"types": {}}\n')
        r = subprocess.run([sys.executable, "-I", os.path.join(HERE, "tutorial.py"), "--unity", assets, "--out", out,
                            "--kit", "--rules", os.path.join(out, "rules.json"), "--json"], capture_output=True, text=True)
        assert r.returncode == 0, r.stderr
        rep = json.loads(r.stdout)
        assert rep["maps"] == ["camp_10"], rep
        lua = open(os.path.join(out, "scripts", "bw", "tutorial_data", "camp_10.lua")).read()
        assert 'flags = {"noSkills", "noInput"}' in lua, lua
        assert 'window = "MPTutorial_1_Welcome Screen"' in lua
        assert 'time = fx("0.5")' in lua and "0.5," not in lua.replace('fx("0.5")', "")
        assert 'gotoChapters = {"Move"}' in lua
        assert 'fromPoints = {3, 1}' in lua and 'ops = {"show", "move"}' in lua and 'toType = "skill"' in lua
        assert 'shiftFrom = {0, 2}' in lua
        main_script = open(os.path.join(out, "scripts", "bw", "tut_camp_10.lua")).read()
        assert 'require("bw.tutorial")(require("bw.tutorial_data.camp_10"))' in main_script
        assert os.path.isfile(os.path.join(out, "scripts", "bw", "tutorial.lua"))
        assert os.path.isfile(os.path.join(out, "scripts", "view", "bw", "tutorial_view.lua"))
        own = open(os.path.join(out, "scripts", "view", "bw", "tutorial_windows.lua")).read()
        assert '["MPTutorial_1_Welcome Screen"] = true' in own, own
        rules = json.load(open(os.path.join(out, "rules.json")))
        assert rules["commands"]["bw_tut_click"] == {"label": "bw_tut_click", "card": False, "fn": "bw_tutorial_command"}
        lang = json.load(open(os.path.join(out, "lang", "en.json")))
        assert lang["bw.tut.tap"]
        headless = os.environ.get("AIVERSE_HEADLESS")
        if headless:
            run_match(headless, assets, tmp)
    print("bw tutorial importer: ok")


def run_match(headless, assets, tmp):
    """The runtime in a real match (templates/mushrooms, test mode): the whole tutorial finishes, deterministically."""
    import re
    import shutil
    root = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
    game = os.path.join(tmp, "match")
    shutil.copytree(os.path.join(root, "templates", "mushrooms"), game)
    with open(os.path.join(assets, "Maps", "!Camp_10_Data.asset"), "w") as f:
        f.write(MAP.replace("toPoint: 4", "toPoint: 1").replace("count: 50", "count: 5"))
    r = subprocess.run([sys.executable, "-I", os.path.join(HERE, "tutorial.py"), "--unity", assets, "--out", game, "--kit",
                        "--rules", os.path.join(game, "rules.json"), "--json"], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    m = os.path.join(game, "maps", "duel.json")
    j = json.loads(re.sub(r"^\s*//.*$", "", open(m).read(), flags=re.M))
    j["scripts"] = j.get("scripts", []) + ["scripts/bw/tut_camp_10.lua"]
    j["viewScripts"] = j.get("viewScripts", []) + ["scripts/view/bw/tutorial_view.lua"]
    j.setdefault("initialVars", {})["bw_tutorial_auto"] = 1
    json.dump(j, open(m, "w"))
    outs = []
    for _ in range(2):
        p = subprocess.run([headless, "--project", game, "--map", "maps/duel.json", "--seed", "3"], capture_output=True, text=True, timeout=600)
        assert p.returncode == 0, p.stderr[-2000:]
        assert "bw tutorial: finished" in p.stdout, p.stdout[-3000:]
        assert "script error" not in p.stdout, p.stdout[-3000:]
        outs.append(re.search(r"Checksum: (\w+)", p.stdout).group(1))
    assert outs[0] == outs[1], outs


if __name__ == "__main__":
    main()
