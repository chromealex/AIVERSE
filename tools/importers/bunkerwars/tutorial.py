#!/usr/bin/env python3
"""Bunker Wars tutorials -> AIVERSE data: battle tutorials of the maps and the meta (menu) tutorials.

Battle: every map data asset (`*_Data.asset`) with a MapTutorialModule (beforeBattleChapters /
tutorialChapters of TutorialActions, Unity SerializeReference) becomes a Lua data module
`scripts/bw/tutorial_data/<map>.lua` plus a two-line map script `scripts/bw/tut_<map>.lua` that runs it
with the kit's runtime (scripts/bw/tutorial.lua: chapters, waits, actions on engine primitives,
the finger / highlight / windows drawn by scripts/view/bw/tutorial_view.lua).

Meta: TutorialData assets (_TutorialMeta: UI.Windows tutorials with conditions and actions such as
ShowScreen, WaitForClick, highlights, PushNotification, UnlockAchievement, SetKey, CounterAdd) become
`ui/bw_tutorials.json` for the meta runtime (ui/bw_tutorials.lua: runs them on UI events; pushes and
achievements go through the engine's push / social services).

    python3 tools/importers/bunkerwars/tutorial.py --unity <Client/Assets> --out <game folder>
        [--maps <dir with *_Data.asset under Assets>] [--rules <game>/rules.json] [--kit] [--json]

--kit copies the runtime (tools/importers/bunkerwars/kit) into the game; --rules adds the hidden
script commands the view sends (bw_tut_click, bw_tut_window, bw_tut_menu, bw_tut_percent, bw_tut_skip, bw_tut_off).
"""
import argparse
import json
import os
import re
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from lib.unity.unity_yaml import parse_documents  # noqa: E402

HERE = Path(__file__).resolve().parent
_KEY_VALUE = re.compile(r"^(\s*)(?:- )?[\w.]+: \S")


def unfold(text):
    """Joins wrapped plain scalars (Unity wraps long values; a continuation may start with "- ")."""
    out = []
    for line in text.splitlines():
        if out:
            m = _KEY_VALUE.match(out[-1])
            ind = len(line) - len(line.lstrip(" "))
            if m and line.strip() and ind > len(m.group(1)) + (2 if out[-1].lstrip().startswith("- ") else 0) \
                    and not re.match(r"^\s*[\w.]+:(\s|$)", line) and not out[-1].rstrip().endswith(("{", "[")):
                out[-1] = out[-1].rstrip() + " " + line.strip()
                continue
        out.append(line)
    return "\n".join(out)


_HEX_ARRAY = re.compile(r"^(\s+[A-Za-z]+s): ((?:[0-9a-f]{8})+)\s*$")


def quote_int_arrays(text):
    """Unity writes int[] as one hex blob ("fromPoints: 03000000"): keep it a string for hex_ints()."""
    return "\n".join(_HEX_ARRAY.sub(r'\1: "\2"', line) for line in text.splitlines())


def hex_ints(v):
    """"0300000001000000" -> [3, 1] (little-endian int32 per 8 hex digits)."""
    out = []
    for i in range(0, len(v), 8):
        n = int.from_bytes(bytes.fromhex(v[i:i + 8]), "little", signed=True)
        out.append(n)
    return out


def bits(value, names):
    v = int(value or 0)
    return [n for b, n in names if v & b]


BATTLE_FLAGS = [(0x1, "noHeroes"), (0x2, "noMorale"), (0x4, "noSkills"), (0x8, "noUpgrade"), (0x10, "noConvertToHouse"),
                (0x20, "noConvertToTower"), (0x40, "noConvertToSmithy"), (0x80, "showAllClouds"), (0x100, "noPercentChooser"),
                (0x200, "noSmithies"), (0x400, "showUpgradeArrows"), (0x800, "noMultiselectButton"), (0x1000, "showSos"),
                (0x2000, "showMyHero"), (0x4000, "showMySkills"), (0x8000, "ignoreDefaultPercent"), (0x10000, "noInput"),
                (0x20000, "noArtifacts")]
POINT_FLAGS = [(0x1, "moveIn"), (0x2, "moveOut"), (0x4, "upgradeMenu"), (0x8, "convert"), (0x10, "upgrade"),
               (0x20, "skills"), (0x40, "flames"), (0x80, "value")]
FINGER_ACTIONS = [(0x1, "show"), (0x2, "hide"), (0x4, "animate"), (0x8, "move")]
PLAYERS = ["player", "bot", "neutral"]
PERCENTS = [25, 50, 75, 100]


def resource_name(v):
    p = (v or {}).get("resourcePath") if isinstance(v, dict) else None
    return Path(str(p)).stem if p else None


def convert_step(cls, data):
    """One TutorialAction -> {"action": name, ...}; enums and bit masks become names."""
    d = dict(data or {})
    s = {"action": cls}
    for k, v in d.items():
        if isinstance(v, str) and re.fullmatch(r"(?:[0-9a-f]{8})+", v) and k.endswith("s"):
            s[k] = hex_ints(v)
        elif isinstance(v, dict) and "resourcePath" in v:
            s[k.replace("Source", "")] = resource_name(v)
        elif isinstance(v, dict) and set(v) >= {"x", "y"} and len(v) <= 3:
            s[k] = [v.get("x", 0), v.get("y", 0)]
        elif isinstance(v, dict) and ("m_Curve" in v or "serializedVersion" in v):
            continue   # AnimationCurve: the runtime eases
        else:
            s[k] = v
    if cls == "SetBattleFlags":
        s["method"] = "disable" if int(d.get("method") or 0) == 1 else "enable"
        s["flags"] = bits(d.get("flags"), BATTLE_FLAGS)
    elif cls == "SetPointFlags":
        s["selectionType"] = "allExcept" if int(d.get("selectionType") or 0) == 0 else "onlyInclude"
        s["flagState"] = "enabled" if int(d.get("flagState") or 0) == 0 else "disabled"
        s["modifyFlags"] = bits(d.get("modifyFlags"), POINT_FLAGS)
    elif cls in ("FingerTip",):
        s["action"] = "FingerTip"
        s["ops"] = bits(d.get("action"), FINGER_ACTIONS)
        s["fromType"] = "skill" if int(d.get("fromType") or 0) == 1 else "point"
        s["toType"] = "skill" if int(d.get("toType") or 0) == 1 else "point"
    elif cls == "SetMovePercent":
        s["percent"] = PERCENTS[min(3, max(0, int(d.get("percent") or 0)))]
    elif cls == "ShowHidePointComponent":
        s["layerType"] = "point" if int(d.get("layerType") or 0) == 1 else "hud"
    for key in ("player",):
        if key in d and cls != "MoveUnits" and isinstance(d[key], (int, float)):
            s[key] = PLAYERS[int(d[key])] if 0 <= int(d[key]) < 3 else "player"
    if isinstance(d.get("playerInfo"), dict):
        s["playerInfo"] = d["playerInfo"]
    return s


def read_map(path):
    docs = parse_documents(quote_int_arrays(unfold(Path(path).read_text(encoding="utf-8", errors="replace"))))
    for o in docs:
        data = o.data if isinstance(o.data, dict) else {}
        if "tutorialChapters" not in data:
            continue
        refs = {}
        for r in ((data.get("references") or {}).get("RefIds") or []):
            t = r.get("type") or {}
            refs[r.get("rid")] = (str(t.get("class") or ""), r.get("data") or {})

        def chapters(key):
            out = []
            for ch in data.get(key) or []:
                steps = []
                for st in (ch or {}).get("steps") or []:
                    rid = (st or {}).get("rid")
                    if rid not in refs or not refs[rid][0]:
                        steps.append({"action": "Missing"})
                        continue
                    steps.append(convert_step(*refs[rid]))
                out.append({"name": str(ch.get("name") or ""), "steps": steps})
            return out
        result = {"before": chapters("beforeBattleChapters"), "chapters": chapters("tutorialChapters")}
        if any(c["steps"] for c in result["chapters"]):
            return result
    return None


def lua_value(v, indent=0):
    pad = "  " * indent
    if v is None:
        return "nil"
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, float):
        if v.is_integer():
            return str(int(v))
        return 'fx("%s")' % repr(v)   # simulation scripts: no float literals (docs/scripting.md)
    if isinstance(v, int):
        return str(v)
    if isinstance(v, str):
        return json.dumps(v, ensure_ascii=False)
    if isinstance(v, list):
        if not v:
            return "{}"
        if all(not isinstance(x, (dict, list)) for x in v):
            return "{" + ", ".join(lua_value(x) for x in v) + "}"
        return "{\n" + ",\n".join(pad + "  " + lua_value(x, indent + 1) for x in v) + "\n" + pad + "}"
    if isinstance(v, dict):
        if not v:
            return "{}"
        parts = []
        for k, x in v.items():
            key = k if re.match(r"^[A-Za-z_][A-Za-z0-9_]*$", k) else "[" + json.dumps(k) + "]"
            parts.append(key + " = " + lua_value(x, indent + 1))
        line = "{" + ", ".join(parts) + "}"
        if len(line) < 140 and "\n" not in line:
            return line
        return "{\n" + ",\n".join(pad + "  " + p for p in parts) + "\n" + pad + "}"
    return "nil"


def map_id(path):
    stem = Path(path).stem.lstrip("!")
    stem = re.sub(r"_Data$", "", stem)
    return re.sub(r"[^A-Za-z0-9_]", "_", stem).lower()


def read_meta(assets):
    """_TutorialMeta TutorialData assets -> list of {name, forObject, startEvent, conditions, actions}."""
    out = []
    root = None
    for cand in Path(assets).rglob("_TutorialMeta"):
        if cand.is_dir():
            root = cand
            break
    if not root:
        return out
    for p in sorted(root.rglob("*.asset")):
        try:
            docs = parse_documents(unfold(p.read_text(encoding="utf-8", errors="replace")))
        except (OSError, ValueError):
            continue
        for o in docs:
            d = o.data if isinstance(o.data, dict) else {}
            if "actions" not in d or "conditions" not in d:
                continue
            refs = {}
            for r in ((d.get("references") or {}).get("RefIds") or []):
                t = r.get("type") or {}
                refs[r.get("rid")] = (str(t.get("class") or ""), r.get("data") or {})

            def items(block):
                res = []
                for it in ((d.get(block) or {}).get("items") or []):
                    rid = (it or {}).get("rid")
                    if rid not in refs:
                        continue
                    cls, data = refs[rid]
                    entry = {"type": cls.split("/")[-1], **{k: v for k, v in data.items() if not isinstance(v, dict) or "rid" not in v}}
                    for k, v in data.items():   # nested references (CustomAction -> CustomAction/ShowWelcomeStreakPopup)
                        if isinstance(v, dict) and "rid" in v and v["rid"] in refs:
                            entry[k] = refs[v["rid"]][0].split("/")[-1]
                    res.append(entry)
                return res
            fo = d.get("forObject") or {}
            out.append({"name": str(d.get("m_Name") or p.stem), "path": str(p.relative_to(assets)).replace(os.sep, "/"),
                        "forObject": str(fo.get("type") or "").split(",")[0], "startEvent": d.get("startEvent"),
                        "conditions": items("conditions"), "actions": items("actions")})
    return out


COMMANDS = ["bw_tut_click", "bw_tut_window", "bw_tut_menu", "bw_tut_percent", "bw_tut_skip", "bw_tut_off"]


def add_commands(rules_path):
    text = Path(rules_path).read_text(encoding="utf-8")
    rules = json.loads(re.sub(r"^\s*//.*$", "", text, flags=re.M))
    cmds = rules.setdefault("commands", {})
    for c in COMMANDS:
        cmds[c] = {"label": c, "card": False, "fn": "bw_tutorial_command"}
    Path(rules_path).write_text(json.dumps(rules, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--unity", required=True, help="the Unity project's Assets folder")
    ap.add_argument("--out", required=True, help="game folder")
    ap.add_argument("--maps", default=None, help="folder of map data assets (default: every *_Data.asset under Assets)")
    ap.add_argument("--rules", default=None, help="rules.json to add the hidden tutorial commands to")
    ap.add_argument("--kit", action="store_true", help="copy the tutorial runtime into the game folder")
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args(argv)
    assets = Path(a.unity)
    out = Path(a.out)
    maps_root = Path(a.maps) if a.maps else assets
    if a.maps and not maps_root.is_absolute():
        maps_root = assets / a.maps
    report = {"maps": [], "actions": {}, "meta": 0}
    data_dir = out / "scripts" / "bw" / "tutorial_data"
    for p in sorted(maps_root.rglob("*_Data.asset")):
        try:
            head = p.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        if "tutorialChapters" not in head:
            continue
        t = read_map(p)
        if not t:
            continue
        mid = map_id(p)
        if mid in report["maps"]:   # a variant of the same map (split-test folders): name it after its folder
            mid = mid + "_" + re.sub(r"[^A-Za-z0-9_]", "_", p.parent.name).lower()
        data_dir.mkdir(parents=True, exist_ok=True)
        (data_dir / (mid + ".lua")).write_text(
            "-- Bunker Wars tutorial of " + str(p.relative_to(assets)).replace(os.sep, "/") + " (tools/importers/bunkerwars/tutorial.py)\n"
            "return " + lua_value(t) + "\n", encoding="utf-8")
        (out / "scripts" / "bw" / ("tut_" + mid + ".lua")).write_text(
            "-- map script: list in the map's \"scripts\" (and scripts/view/bw/tutorial_view.lua in \"viewScripts\")\n"
            "require(\"bw.tutorial\")(require(\"bw.tutorial_data." + mid + "\"))\n", encoding="utf-8")
        for ch in t["before"] + t["chapters"]:
            for s in ch["steps"]:
                report["actions"][s["action"]] = report["actions"].get(s["action"], 0) + 1
        report["maps"].append(mid)
    meta = read_meta(assets)
    if meta:
        (out / "ui").mkdir(parents=True, exist_ok=True)
        (out / "ui" / "bw_tutorials.json").write_text(json.dumps({"tutorials": meta}, ensure_ascii=False, indent=1), encoding="utf-8")
        report["meta"] = len(meta)
    if a.kit:
        kit = HERE / "kit"
        for f in kit.rglob("*"):
            if not f.is_file():
                continue
            dst = out / f.relative_to(kit)
            dst.parent.mkdir(parents=True, exist_ok=True)
            if f.parent.name == "lang":   # merge the kit's keys into the game's language files
                cur = {}
                if dst.exists():
                    cur = json.loads(re.sub(r"^\s*//.*$", "", dst.read_text(encoding="utf-8"), flags=re.M))
                for k, v in json.loads(f.read_text(encoding="utf-8")).items():
                    cur.setdefault(k, v)
                dst.write_text(json.dumps(cur, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
            else:
                shutil.copyfile(f, dst)
    # which tutorial screens have their own HUD document (UI.Windows importer): the view opens those directly
    hud = out / "ui" / "hud"
    windows = set()
    for mid in report["maps"]:
        for line in (data_dir / (mid + ".lua")).read_text(encoding="utf-8").splitlines():
            m = re.search(r'window = "([^"]+)"', line)
            if m and (hud / (m.group(1) + ".rml")).exists():
                windows.add(m.group(1))
    if report["maps"]:
        vdir = out / "scripts" / "view" / "bw"
        vdir.mkdir(parents=True, exist_ok=True)
        (vdir / "tutorial_windows.lua").write_text(
            "-- tutorial screens with their own ui/hud/<name>.rml (tools/importers/bunkerwars/tutorial.py)\nreturn {" +
            ", ".join("[%s] = true" % json.dumps(w) for w in sorted(windows)) + "}\n", encoding="utf-8")
    if a.rules:
        add_commands(a.rules)
    if a.json:
        print(json.dumps(report, ensure_ascii=False))
    else:
        print("tutorials: %d maps, %d meta tutorials" % (len(report["maps"]), report["meta"]))
        for k, v in sorted(report["actions"].items(), key=lambda kv: -kv[1]):
            print("  %-30s %d" % (k, v))
    return 0


if __name__ == "__main__":
    sys.exit(main())
