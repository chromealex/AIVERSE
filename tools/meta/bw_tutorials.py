#!/usr/bin/env python3
"""Bunker Wars meta (menu) tutorials for the meta tutorial runner (engine/meta/bw/BwTutorials.h).

Reads the _TutorialMeta TutorialData assets with the BW importer (tools/importers/bunkerwars/tutorial.py
read_meta) and resolves what the runner needs by id instead of Unity references:
  - "guid" of every tutorial (its asset's .meta) so RunTutorial {nextTutorial: {guid}} can name it,
  - HasMissionStatus / HasCampaign map headers -> "mapId" (MapHeader asset "id"),
  - HasProfileFeatures feature -> "featureId" (feature asset "id" / name).
Writes <game>/ui/bw_tutorials.json ({"version": 2, "tutorials": [...]}).

    tools/meta/bw_tutorials.py --unity <bunker-wars>/Client/Assets [--game templates/bunkerwars]
"""
import argparse, json, re, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "importers"))
sys.path.insert(0, str(ROOT / "tools" / "importers" / "bunkerwars"))
import tutorial  # noqa: E402

GUID = re.compile(r"^guid:\s*([0-9a-f]{32})", re.M)


def guid_index(assets):
    out = {}
    for meta in Path(assets).rglob("*.asset.meta"):
        try:
            m = GUID.search(meta.read_text(encoding="utf-8", errors="replace"))
        except OSError:
            continue
        if m:
            out[m.group(1)] = meta.with_suffix("")   # strip .meta
    return out


def asset_field(path, names):
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return None
    for n in names:
        m = re.search(r"^\s*" + re.escape(n) + r":\s*(-?\d+)\s*$", text, re.M)
        if m:
            return int(m.group(1))
    return None


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--unity", required=True)
    ap.add_argument("--game", default=str(ROOT / "templates" / "bunkerwars"))
    a = ap.parse_args(argv)
    assets = Path(a.unity)
    meta = tutorial.read_meta(assets)
    guids = guid_index(assets)
    by_path = {str(p.relative_to(assets)).replace("\\", "/"): g for g, p in guids.items()}
    unresolved = 0
    for t in meta:
        t["guid"] = by_path.get(t["path"], "")
        for c in t["conditions"] + t["actions"]:
            for key in ("mapHeader", "campaignMapInfo", "feature", "nextTutorial"):
                ref = c.get(key)
                if not isinstance(ref, dict) or not ref.get("guid"):
                    continue
                g = ref["guid"]
                c[key + "Guid"] = g
                p = guids.get(g)
                if key == "mapHeader" and p:
                    c["mapId"] = asset_field(p, ["mapId"])   # MapHeader.mapId (the id the backend uses), not the database id
                elif key == "campaignMapInfo" and p:
                    c["campaignId"] = asset_field(p, ["id", "groupId", "clientId"])
                elif key == "feature" and p:
                    c["featureId"] = asset_field(p, ["id", "featureId", "flag"])
                    c["featureName"] = p.stem
                if p is None and key != "nextTutorial":
                    unresolved += 1
    out = Path(a.game) / "ui" / "bw_tutorials.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps({"version": 2, "source": "tools/meta/bw_tutorials.py", "tutorials": meta},
                              ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    print("wrote %s: %d tutorials, %d with guid, %d unresolved references" %
          (out, len(meta), sum(1 for t in meta if t["guid"]), unresolved))
    return 0


if __name__ == "__main__":
    sys.exit(main())
