"""Bunker Wars UI texts: the UI.Windows localization table (MW2Data/Resources/MW2.Modules/Localization/
LocalizationSettings.asset: data.keys + data.values[language].values, language = UI.Windows Language enum,
English 10, Russian 30). UI.Windows components name their text by key (`textLocalizationKey: {key: A|B|C}`).

    table = read_table(assets)            # {"newtutorial|tutor1|text26": {"ru": ..., "en": ...}}  (keys lower case)
    engine_key("NewTutorial|Tutor1|Text26")  -> "bw.ui.NewTutorial.Tutor1.Text26"
"""
import os
import re

LANGS = {"en": 10, "ru": 30}
_ESC = {"n": "\n", "t": "\t", '"': '"', "\\": "\\", "/": "/", "0": "\0", " ": " ", "r": "\r", "b": "\b", "e": "\x1b"}


def _scalar(lines):
    """A YAML scalar written over one or more lines (Unity style) -> str."""
    first = lines[0]
    if first.startswith('"'):
        text = first
        for ln in lines[1:]:
            text = (text[:-1] if text.endswith("\\") else text + " ") + ln.strip()   # folded, "\" joins without space
        body = text[1:text.rindex('"')] if text.rfind('"') > 0 else text[1:]
        out, i = [], 0
        while i < len(body):
            c = body[i]
            if c == "\\" and i + 1 < len(body):
                n = body[i + 1]
                if n == "u" and i + 5 < len(body) + 0:
                    out.append(chr(int(body[i + 2:i + 6], 16)))
                    i += 6
                    continue
                if n == "x":
                    out.append(chr(int(body[i + 2:i + 4], 16)))
                    i += 4
                    continue
                out.append(_ESC.get(n, n))
                i += 2
                continue
            out.append(c)
            i += 1
        s = "".join(out)
        # surrogate pairs written as two \u escapes
        return s.encode("utf-16", "surrogatepass").decode("utf-16", "replace") if any(0xD800 <= ord(ch) <= 0xDFFF for ch in s) else s
    if first.startswith("'"):
        text = " ".join([first] + [ln.strip() for ln in lines[1:]])
        return text[1:text.rindex("'")].replace("''", "'") if text.rfind("'") > 0 else text[1:]
    return " ".join([first] + [ln.strip() for ln in lines[1:]]).strip()


def _items(lines, start, indent):
    """List items `<indent>- x` from lines[start:] while the block lasts -> ([str], next index)."""
    pre = " " * indent + "- "
    out, cur, i = [], None, start
    while i < len(lines):
        ln = lines[i]
        if ln.startswith(pre):
            if cur is not None:
                out.append(_scalar(cur))
            cur = [ln[len(pre):]]
        elif cur is not None and ln.startswith(" " * (indent + 2)) and ln.strip():
            cur.append(ln)
        else:
            break
        i += 1
    if cur is not None:
        out.append(_scalar(cur))
    return out, i


def parse_settings(text):
    lines = text.splitlines()
    keys, values = [], {}
    i = 0
    lang = None
    while i < len(lines):
        ln = lines[i]
        if ln.startswith("    keys:"):
            keys, i = _items(lines, i + 1, 4)
            continue
        m = re.match(r"^      key: (\d+)$", ln)
        if m:
            lang = int(m.group(1))
        elif ln.startswith("      values:") and lang is not None:
            if ln.strip() == "values: []":
                i += 1
                continue
            vals, i = _items(lines, i + 1, 6)
            values[lang] = vals
            lang = None
            continue
        i += 1
    table = {}
    for code, idx in LANGS.items():
        vals = values.get(idx) or []
        for k, v in zip(keys, vals):
            if v:
                table.setdefault(k.lower(), {})[code] = v
    return table, keys


def read_table(assets):
    p = os.path.join(assets, "MW2Data", "Resources", "MW2.Modules", "Localization", "LocalizationSettings.asset")
    if not os.path.isfile(p):
        return {}
    with open(p, encoding="utf-8", errors="replace") as f:
        return parse_settings(f.read())[0]


def engine_key(key):
    k = re.sub(r"[|/ ]+", ".", key.strip())
    return "bw.ui." + re.sub(r"[^A-Za-z0-9._-]", "_", k)
