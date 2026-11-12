#!/usr/bin/env python3
"""Game audio "FMOD" banks -> AIVERSE audio.json (sounds, music playlists, event cues) + an event map.

Bunker Wars / Mushroom Wars 2 do not ship FMOD Studio banks: their "FMOD" is the UI.Windows audio
system (FMODChannels / FMODEventReference ScriptableObjects): a bank of groups -> items (key, clips,
SFX or Music, volume, play chance, max instances, music channel, loop, fades, triggerOnBegin /
triggerOnEnd chains) and ~800 event reference assets ({key} to play, {key, volume, delay, duration}
volume events, haptics). This importer reads them from the Unity project:

    python3 tools/importers/fmod/import_audio.py --unity <Client/Assets> --out <game folder>
        [--channels MW2Data/Resources/FMODChannels.asset] [--no-copy] [--json]

Writes into the game folder:
  audio/<path under Assets>       the clip files (WAV / OGG / MP3 as they are; aiversebuild compresses)
  audio.json                      sounds (SFX items), music playlists (Music items), events (engine cues);
                                  merged into an existing file (other keys kept, imported names replaced)
  audio_events.json               the event map: FMODEventReference guid / name -> {play, volume, haptic},
                                  every bank key -> {kind, sound | playlist}, extras the engine format has
                                  no key for (channel, fades, mute / stop other channels; play chance is
                                  also kept here, the sound gets "chance")

Mapping (docs/game-format.md § audio.json):
  SFX item    -> sounds[key]: files, volume, maxInstances = maxCount (steal "none": BW skips the play),
                 bus "ui" for menu groups, "sfx" otherwise; not positional (BW plays them 2D);
                 playChance < 1 -> "chance"
  Music item  -> music[key]: files, loop, volume. A triggerOnBegin chain (map-background ->
                 map-ambient -> war theme -> rain) plays on several channels at once: it becomes one
                 adaptive playlist with "layers"; a layer that volume events fade in gets an intensity.
                 A triggerOnEnd chain (main_menu -> main_menu_loop) becomes consecutive entries.
  Engine playlists: "menu" <- main_menu (or the first menu music), "match" <- map-background.
  Cues: click <- defaultClick, victory / defeat <- battleend-victory / battleend-defeat.

A real FMOD Studio project is not supported by this script: .bank files are FSB5 containers (often
encrypted); export the event audio from FMOD Studio (File > Export Audio) and map the files with a
--map file instead: {"sounds": {"name": {"files": [...]}}, "music": {...}} is merged verbatim.
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

FMOD_EVENT_REFERENCE_GUID = "1d422772112ee430fb8acc5d9ae2f832"   # FMODEventReference.cs.meta (UI.Windows)
UI_GROUP = re.compile(r"^(menu|matchmaking|ui)", re.I)


def _num(v, d=0.0):
    try:
        return float(v)
    except (TypeError, ValueError):
        return d


def _int(v, d=0):
    try:
        return int(float(v))
    except (TypeError, ValueError):
        return d


_KEY_VALUE = re.compile(r"^(\s*)(?:- )?[\w.]+: \S")


def unfold(text):
    """Joins wrapped plain scalars (Unity wraps long paths; a continuation may even start with "- ")."""
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


def read_channels(path):
    docs = parse_documents(unfold(Path(path).read_text(encoding="utf-8", errors="replace")))
    if not docs or not isinstance(docs[0].data, dict) or "groups" not in docs[0].data:
        raise ValueError("%s: not an FMODChannels asset (no groups)" % path)
    data = docs[0].data
    items = []
    for g in data.get("groups") or []:
        for it in (g or {}).get("items") or []:
            clips = []
            for c in it.get("clips") or []:
                p = (c or {}).get("resourcePath") or ""
                if p:
                    clips.append(str(p).strip())
            items.append({
                "group": str(g.get("groupName") or ""),
                "key": str(it.get("key") or ""),
                "enabled": _int(it.get("enabled"), 1) != 0,
                "clips": clips,
                "type": "music" if _int(it.get("type")) == 0 else "sfx",
                "volume": _num(it.get("volumeMultiplier"), 1.0),
                "chance": _num(it.get("playChance"), 1.0),
                "maxCount": _int(it.get("maxCount")),
                "channel": _int(it.get("channel")),
                "loop": _int(it.get("loop")) != 0,
                "stopOthers": _int(it.get("stopOtherChannels")) != 0,
                "muteOthers": _int(it.get("muteOtherChannels")) != 0,
                "unmuteAll": _int(it.get("unmuteAllChannels")) != 0,
                "onBegin": str(it.get("triggerOnBegin") or "").strip(),
                "onEnd": str(it.get("triggerOnEnd") or "").strip(),
                "fadeIn": _num(it.get("fadeInDuration")),
                "fadeOut": _num(it.get("fadeOutDuration")),
            })
    refs = {"defaultClick": data.get("defaultClick"), "defaultProgressClick": data.get("defaultProgressClick")}
    return items, refs


def scan_event_references(assets):
    """guid -> {name, path, play, volume, haptic} for every FMODEventReference asset."""
    out = {}
    for root, dirs, files in os.walk(assets):
        dirs[:] = [d for d in dirs if d not in (".worktrees", "Library", "Temp") and not d.startswith(".")]
        for f in files:
            if not f.endswith(".asset"):
                continue
            p = os.path.join(root, f)
            try:
                with open(p, "r", encoding="utf-8", errors="replace") as fh:
                    head = fh.read(4096)
                    if FMOD_EVENT_REFERENCE_GUID not in head:
                        continue
                    text = head + fh.read()
            except OSError:
                continue
            docs = parse_documents(unfold(text))
            if not docs:
                continue
            d = docs[0].data or {}
            guid = None
            try:
                meta = Path(p + ".meta").read_text(encoding="utf-8", errors="replace")
                m = re.search(r"^guid:\s*([0-9a-f]+)", meta, re.M)
                guid = m.group(1) if m else None
            except OSError:
                pass
            ae, ve = d.get("audioEvent") or {}, d.get("volumeEvent") or {}
            e = {"name": str(d.get("m_Name") or Path(f).stem), "path": os.path.relpath(p, assets).replace(os.sep, "/")}
            if _int(ae.get("enabled")) and ae.get("key"):
                e["play"] = str(ae["key"])
            if _int(ve.get("enabled")) and ve.get("key"):
                e["volume"] = {"key": str(ve["key"]), "value": _num(ve.get("targetValue")),
                               "delay": _num(ve.get("delay")), "duration": _num(ve.get("duration"))}
            if _int(d.get("vibrate")):
                e["haptic"] = _int(d.get("vibrateType"))
            out[guid or e["path"]] = e
    return out


def _target(rel):
    rel = rel.replace("\\", "/")
    if rel.startswith("Assets/"):
        rel = rel[len("Assets/"):]
    return "audio/" + rel


def convert(items, refs, events, assets, out_dir, copy=True, log=print):
    by_key = {}
    for it in items:
        if it["key"] and it["enabled"]:
            if it["key"] in by_key:
                log("warning: duplicate key '%s' (group %s): the first one wins, as in UI.Windows" % (it["key"], it["group"]))
                continue
            by_key[it["key"]] = it
    copied, missing = 0, []

    def files_of(it):
        nonlocal copied
        out = []
        for rel in it["clips"]:
            src = os.path.join(os.path.dirname(os.path.abspath(assets)), rel)
            if not os.path.isfile(src):
                src2 = os.path.join(assets, rel[len("Assets/"):] if rel.startswith("Assets/") else rel)
                src = src2 if os.path.isfile(src2) else src
            dst_rel = _target(rel)
            if not os.path.isfile(src):
                missing.append(rel)
                continue
            if copy:
                dst = os.path.join(out_dir, dst_rel)
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                if not (os.path.isfile(dst) and os.path.getsize(dst) == os.path.getsize(src)):
                    shutil.copyfile(src, dst)
                    copied += 1
            out.append(dst_rel)
        return out

    # volume events that raise a music key: those layers fade in with the fighting (intensity)
    raised = {}
    for e in events.values():
        v = e.get("volume")
        if v and v["value"] > 0:
            raised[v["key"]] = max(raised.get(v["key"], 0.0), v["value"])
    sounds, music, keymap = {}, {}, {}
    for key, it in by_key.items():
        files = files_of(it)
        if not files:
            log("warning: '%s' has no clip files" % key)
            continue
        extra = {k: it[k] for k in ("group", "chance", "channel", "fadeIn", "fadeOut", "stopOthers", "muteOthers", "unmuteAll",
                                     "onBegin", "onEnd") if it[k] not in ("", 0, 0.0, False) and not (k == "chance" and it[k] == 1.0)}
        if it["type"] == "sfx":
            s = {"files": files} if len(files) > 1 else {"file": files[0]}
            s["bus"] = "ui" if UI_GROUP.match(it["group"]) else "sfx"
            s["spatial"] = False
            if it["volume"] != 1.0:
                s["volume"] = round(it["volume"], 3)
            s["maxInstances"] = it["maxCount"]
            if it["maxCount"] > 0:
                s["steal"] = "none"
            if it["loop"]:
                s["loop"] = True
            if 0.0 <= it["chance"] < 1.0:
                s["chance"] = round(it["chance"], 4)
            sounds[key] = s
            keymap[key] = dict({"kind": "sound", "sound": key}, **extra)
        else:
            keymap[key] = dict({"kind": "music", "files": files}, **extra)
    # music playlists: layered (triggerOnBegin) and chained (triggerOnEnd)
    for key, km in keymap.items():
        if km["kind"] != "music":
            continue
        it = by_key[key]
        layers, seen, k = [], set(), key
        while k and k in by_key and k not in seen and by_key[k]["type"] == "music":
            seen.add(k)
            li = by_key[k]
            lf = keymap.get(k, {}).get("files") or []
            for f in lf[:1]:
                layer = {"file": f}
                vol = li["volume"]
                if k != key and k in raised:
                    layer["intensity"] = 0.5   # BW raises it with volume events: it fades in with the fighting
                    if vol <= 0:
                        vol = raised[k]
                if vol != 1.0:
                    layer["volume"] = round(vol, 3)
                layers.append(layer)
            k = li["onBegin"]
        if len(layers) > 1:
            pl = {"layers": layers}
        else:
            entries = list(km["files"])
            nxt, chain = it["onEnd"], {key}
            while nxt and nxt in keymap and nxt not in chain and keymap[nxt]["kind"] == "music":
                chain.add(nxt)
                entries += keymap[nxt]["files"]
                nxt = by_key[nxt]["onEnd"]
            pl = {"files": entries}
            if len(km["files"]) > 1 and len(entries) == len(km["files"]):
                pl["shuffle"] = True
        pl["loop"] = bool(it["loop"] or (it["onEnd"] and by_key.get(it["onEnd"], {}).get("loop")))
        if it["volume"] != 1.0:
            pl["volume"] = round(it["volume"], 3)
        music[key] = pl
        km["playlist"] = key
        km.pop("files", None)
    # engine playlists and cues
    def first(pred):
        for key in by_key:
            if key in music and pred(key, by_key[key]):
                return key
        return None
    menu = "main_menu" if "main_menu" in music else first(lambda k, it: UI_GROUP.match(it["group"]) is not None)
    match = "map-background" if "map-background" in music else first(lambda k, it: "map" in k)
    if menu:
        music["menu"] = dict(music[menu])
    if match:
        music["match"] = dict(music[match])
    cues = {}
    click_ref = (refs.get("defaultClick") or {}).get("guid") if isinstance(refs.get("defaultClick"), dict) else None
    click = (events.get(click_ref) or {}).get("play") if click_ref else None
    if not click and "ui-click" in sounds:
        click = "ui-click"
    for cue, key in (("click", click), ("victory", "battleend-victory"), ("defeat", "battleend-defeat")):
        if key and key in sounds:
            cues[cue] = key
    unknown = sorted({e["play"] for e in events.values() if "play" in e and e["play"] not in by_key})
    return {"sounds": sounds, "music": music, "events": cues}, keymap, {"copied": copied, "missing": missing, "unknownKeys": unknown}


def merge_audio(path, imported):
    try:
        with open(path, encoding="utf-8") as f:
            cur = json.load(f)
    except (OSError, ValueError):
        cur = {}
    for part in ("sounds", "music", "events"):
        block = cur.get(part) if isinstance(cur.get(part), dict) else {}
        block.update(imported[part])
        cur[part] = block
    cur.setdefault("volume", {"master": 1, "sfx": 1, "ui": 1, "music": 0.8})
    return cur


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--unity", required=True, help="the Unity project's Assets folder")
    ap.add_argument("--out", required=True, help="game folder (audio.json, audio/, audio_events.json)")
    ap.add_argument("--channels", default=None, help="FMODChannels asset (default: the first one found)")
    ap.add_argument("--map", default=None, help="extra JSON merged verbatim (FMOD Studio exported audio)")
    ap.add_argument("--no-copy", action="store_true", help="only write the JSON files")
    ap.add_argument("--json", action="store_true", help="print a JSON report")
    a = ap.parse_args(argv)
    assets = os.path.abspath(a.unity)
    channels = a.channels and os.path.join(assets, a.channels) if a.channels and not os.path.isabs(a.channels) else a.channels
    if not channels:
        for root, dirs, files in os.walk(assets):
            dirs[:] = [d for d in dirs if d not in (".worktrees",) and not d.startswith(".")]
            if "FMODChannels.asset" in files:
                channels = os.path.join(root, "FMODChannels.asset")
                break
    if not channels or not os.path.isfile(channels):
        print("error: no FMODChannels.asset under %s (pass --channels)" % assets, file=sys.stderr)
        return 1
    log = (lambda *_: None) if a.json else print
    items, refs = read_channels(channels)
    events = scan_event_references(assets)
    imported, keymap, report = convert(items, refs, events, assets, a.out, copy=not a.no_copy, log=log)
    if a.map:
        with open(a.map, encoding="utf-8") as f:
            extra = json.load(f)
        for part in ("sounds", "music", "events"):
            imported[part].update(extra.get(part) or {})
    os.makedirs(a.out, exist_ok=True)
    audio = merge_audio(os.path.join(a.out, "audio.json"), imported)
    with open(os.path.join(a.out, "audio.json"), "w", encoding="utf-8") as f:
        json.dump(audio, f, ensure_ascii=False, indent=1)
    with open(os.path.join(a.out, "audio_events.json"), "w", encoding="utf-8") as f:
        json.dump({"source": os.path.relpath(channels, assets).replace(os.sep, "/"), "keys": keymap,
                   "references": events}, f, ensure_ascii=False, indent=1)
    report.update({"sounds": len(imported["sounds"]), "playlists": len(imported["music"]), "cues": imported["events"],
                   "references": len(events), "items": len(items)})
    if a.json:
        print(json.dumps(report, ensure_ascii=False))
    else:
        print("audio: %d sounds, %d playlists, %d event references, %d files copied, %d missing" %
              (report["sounds"], report["playlists"], report["references"], report["copied"], len(report["missing"])))
        for m in report["missing"][:20]:
            print("  missing: " + m)
        if report["unknownKeys"]:
            print("  event references to keys not in the bank: " + ", ".join(report["unknownKeys"][:20]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
