#!/usr/bin/env python3
"""Self-test of import_audio.py on a synthetic UI.Windows audio bank (no game data needed)."""
import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))

CHANNELS = """%YAML 1.1
%TAG !u! tag:unity3d.com,2011:
--- !u!114 &11400000
MonoBehaviour:
  m_Name: FMODChannels
  defaultClick: {fileID: 11400000, guid: aaaa0000000000000000000000000001, type: 2}
  groups:
  - groupName: menu-sfx
    items:
    - key: ui-click
      enabled: 1
      clips:
      - guid: 01
        resourcePath: Assets/Audio/UI/click
          - one.wav
      - guid: 02
        resourcePath: Assets/Audio/UI/click2.wav
      type: 1
      volumeMultiplier: 0.7
      playChance: 1
      maxCount: 2
      channel: 0
      loop: 0
      triggerOnBegin:
      triggerOnEnd:
  - groupName: gameplay-maps
    items:
    - key: battleend-victory
      enabled: 1
      clips:
      - resourcePath: Assets/Audio/win.wav
      type: 1
      volumeMultiplier: 1
      playChance: 0.5
      maxCount: 0
    - key: map-background
      enabled: 1
      clips:
      - resourcePath: Assets/Audio/light.ogg
      type: 0
      volumeMultiplier: 1
      channel: 1
      loop: 1
      triggerOnBegin: war
      triggerOnEnd:
    - key: war
      enabled: 1
      clips:
      - resourcePath: Assets/Audio/hard.ogg
      type: 0
      volumeMultiplier: 0
      channel: 10
      loop: 1
    - key: main_menu
      enabled: 1
      clips:
      - resourcePath: Assets/Audio/intro.ogg
      type: 0
      volumeMultiplier: 1
      loop: 0
      triggerOnEnd: main_menu_loop
    - key: main_menu_loop
      enabled: 1
      clips:
      - resourcePath: Assets/Audio/loop.ogg
      type: 0
      volumeMultiplier: 1
      loop: 1
"""

REF = """%%YAML 1.1
%%TAG !u! tag:unity3d.com,2011:
--- !u!114 &11400000
MonoBehaviour:
  m_Script: {fileID: 11500000, guid: 1d422772112ee430fb8acc5d9ae2f832, type: 3}
  m_Name: %s
  audioEvent:
    enabled: %d
    key: %s
  volumeEvent:
    enabled: %d
    key: %s
    targetValue: %s
    delay: 0
    duration: 1
  vibrate: 0
"""


def main():
    with tempfile.TemporaryDirectory() as tmp:
        assets = os.path.join(tmp, "Assets")
        for rel in ("Audio/UI/click - one.wav", "Audio/UI/click2.wav", "Audio/win.wav", "Audio/light.ogg", "Audio/hard.ogg",
                    "Audio/intro.ogg", "Audio/loop.ogg"):
            p = os.path.join(assets, rel)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, "wb") as f:
                f.write(b"RIFF0000WAVE")
        os.makedirs(os.path.join(assets, "Resources"), exist_ok=True)
        with open(os.path.join(assets, "Resources", "FMODChannels.asset"), "w") as f:
            f.write(CHANNELS)
        for name, guid, body in (("Click", "aaaa0000000000000000000000000001", REF % ("Click", 1, "ui-click", 0, "", "0")),
                                 ("WarOn", "aaaa0000000000000000000000000002", REF % ("WarOn", 0, "", 1, "war", "0.8"))):
            with open(os.path.join(assets, name + ".asset"), "w") as f:
                f.write(body)
            with open(os.path.join(assets, name + ".asset.meta"), "w") as f:
                f.write("fileFormatVersion: 2\nguid: %s\n" % guid)
        out = os.path.join(tmp, "game")
        os.makedirs(out)
        with open(os.path.join(out, "audio.json"), "w") as f:
            json.dump({"sounds": {"keep": {"file": "x.ogg"}}, "volume": {"master": 0.5}}, f)
        r = subprocess.run([sys.executable, "-I", os.path.join(HERE, "import_audio.py"), "--unity", assets, "--out", out, "--json"],
                           capture_output=True, text=True)
        assert r.returncode == 0, r.stderr
        rep = json.loads(r.stdout)
        a = json.load(open(os.path.join(out, "audio.json")))
        ev = json.load(open(os.path.join(out, "audio_events.json")))
        s = a["sounds"]
        assert s["keep"] == {"file": "x.ogg"} and a["volume"]["master"] == 0.5, "merge keeps other keys"
        assert s["ui-click"]["files"] == ["audio/Audio/UI/click - one.wav", "audio/Audio/UI/click2.wav"], s["ui-click"]
        assert s["ui-click"]["bus"] == "ui" and s["ui-click"]["maxInstances"] == 2 and s["ui-click"]["steal"] == "none"
        assert s["battleend-victory"]["bus"] == "sfx" and ev["keys"]["battleend-victory"]["chance"] == 0.5
        assert s["battleend-victory"]["chance"] == 0.5 and "chance" not in s["ui-click"]
        m = a["music"]
        assert m["map-background"]["layers"][1] == {"file": "audio/Audio/hard.ogg", "intensity": 0.5, "volume": 0.8}, m["map-background"]
        assert m["main_menu"]["files"] == ["audio/Audio/intro.ogg", "audio/Audio/loop.ogg"] and m["main_menu"]["loop"]
        assert m["menu"] == m["main_menu"] and m["match"] == m["map-background"]
        assert a["events"] == {"click": "ui-click", "victory": "battleend-victory"}, a["events"]
        assert ev["references"]["aaaa0000000000000000000000000002"]["volume"]["key"] == "war"
        assert os.path.isfile(os.path.join(out, "audio/Audio/UI/click - one.wav")) and rep["copied"] == 7
    print("import_audio: ok")


if __name__ == "__main__":
    main()
