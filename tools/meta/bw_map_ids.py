#!/usr/bin/env python3
"""Bunker Wars map id table for battle nodes (agent-bw-meta, docs/meta-bunkerwars.md).

Reads the importer's sidecars <game>/bw/maps/<map>.json ("mapId", the BW MapHeader id the backend
sends as GameSpec.mapId / Match.mapId) and writes <game>/bw/map_ids.json:
  {"version": 1, "maps": {"2001": "maps/2players_2001.json", ...}, "variants": {"20001": [...]}}
Maps with several engine variants of one BW id (campaign _a/_b) take the first one in name order and
list all of them under "variants". Usage: tools/meta/bw_map_ids.py [--game templates/bunkerwars] [--check]
"""
import argparse, json, os, sys

def load(path):
    with open(path, encoding="utf-8") as f:
        text = "\n".join(l for l in f.read().splitlines() if not l.lstrip().startswith("//"))
    return json.loads(text)

def build(game):
    side = os.path.join(game, "bw", "maps")
    ids = {}
    for name in sorted(os.listdir(side)):
        if not name.endswith(".json") or not os.path.exists(os.path.join(game, "maps", name)):
            continue
        mid = load(os.path.join(side, name)).get("mapId")
        if isinstance(mid, int) and mid > 0:
            ids.setdefault(str(mid), []).append("maps/" + name)
    out = {"version": 1, "source": "tools/meta/bw_map_ids.py from bw/maps/*.json mapId",
           "maps": {k: v[0] for k, v in sorted(ids.items(), key=lambda kv: int(kv[0]))},
           "variants": {k: v for k, v in sorted(ids.items(), key=lambda kv: int(kv[0])) if len(v) > 1}}
    return json.dumps(out, indent=1, ensure_ascii=False) + "\n"

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--game", default="templates/bunkerwars")
    ap.add_argument("--check", action="store_true", help="fail if the table is out of date")
    a = ap.parse_args()
    text = build(a.game)
    dest = os.path.join(a.game, "bw", "map_ids.json")
    if a.check:
        cur = open(dest, encoding="utf-8").read() if os.path.exists(dest) else ""
        if cur != text:
            print(dest + " is out of date: run tools/meta/bw_map_ids.py --game " + a.game)
            return 1
        print("ok " + dest)
        return 0
    with open(dest, "w", encoding="utf-8") as f:
        f.write(text)
    print("wrote %s (%d ids)" % (dest, len(json.loads(text)["maps"])))
    return 0

if __name__ == "__main__":
    sys.exit(main())
