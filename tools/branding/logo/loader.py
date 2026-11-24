#!/usr/bin/env python3
"""The animated AIVERSE mark used as the loading indicator on every web surface.

    python3 tools/branding/logo/loader.py           # rewrites the marked regions below
    python3 tools/branding/logo/loader.py --check   # fails when a copy differs (CTest branding_loader)

It plays the engine's default loading screen (engine/splash/SplashData.cpp, defaultBlock) in CSS:
the three blades slide in from their sides one after another, the diamond turns into place and keeps
pulsing while the load runs. Geometry comes from make_logo.py, so the mark matches the icon.

Colours follow the page: --aiv-mark-ink (blades, default #141210) and --aiv-mark-accent
(diamond, default #E9C3A4). Size it with CSS on .aiv-mark. With prefers-reduced-motion the mark
is drawn still. Restarting the animation: replace the element (or toggle the class off and on).

Copies live between `aiverse-loader:begin` and `aiverse-loader:end` markers in each TARGETS file,
as plain markup (HTML) or as a JS string (TS/JS/Python), because those files are served as is.
"""
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(__file__))
from make_logo import TOP, LEFT, RIGHT, DIAMOND  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))

STYLE = (
    ".aiv-mark{display:block;overflow:visible}"
    ".aiv-mark .aiv-b{fill:var(--aiv-mark-ink,#141210);opacity:0;animation:.55s cubic-bezier(.2,.7,.25,1) forwards}"
    ".aiv-mark .aiv-t{animation-name:aiv-t}"
    ".aiv-mark .aiv-l{animation-name:aiv-l;animation-delay:.1s}"
    ".aiv-mark .aiv-r{animation-name:aiv-r;animation-delay:.2s}"
    ".aiv-mark .aiv-d{fill:var(--aiv-mark-accent,#E9C3A4);opacity:0;transform-box:view-box;transform-origin:500px 588px;"
    "animation:aiv-d .5s .55s cubic-bezier(.2,.7,.25,1) forwards,aiv-p 1.6s 1.05s ease-in-out infinite}"
    "@keyframes aiv-t{from{opacity:0;transform:translate(0,-70px)}to{opacity:1;transform:none}}"
    "@keyframes aiv-l{from{opacity:0;transform:translate(-60px,32px)}to{opacity:1;transform:none}}"
    "@keyframes aiv-r{from{opacity:0;transform:translate(60px,32px)}to{opacity:1;transform:none}}"
    "@keyframes aiv-d{from{opacity:0;transform:rotate(-90deg) scale(.3)}to{opacity:1;transform:none}}"
    "@keyframes aiv-p{0%,100%{opacity:1;transform:none}50%{opacity:1;transform:scale(1.07)}}"
    "@media (prefers-reduced-motion:reduce){.aiv-mark .aiv-b,.aiv-mark .aiv-d{animation:none;opacity:1}}"
)


def pts(poly):
    return " ".join(f"{x},{y}" for x, y in poly)


SVG = (
    '<svg class="aiv-mark" viewBox="0 60 1000 880" role="img" aria-label="Loading">'
    f"<style>{STYLE}</style>"
    f'<polygon class="aiv-b aiv-t" points="{pts(TOP)}"/>'
    f'<polygon class="aiv-b aiv-l" points="{pts(LEFT)}"/>'
    f'<polygon class="aiv-b aiv-r" points="{pts(RIGHT)}"/>'
    f'<polygon class="aiv-d" points="{pts(DIAMOND)}"/>'
    "</svg>"
)

# file -> "html" (markup as is) or "js" (a JSON string literal)
TARGETS = {
    "apps/catalogview/shell.html": "html",
    "platform/web/shell.html": "html",
    "tools/site/render.py": "js",
    "apps/studio-web/src/ui/StudioLoader.tsx": "js",
}

MARK = re.compile(r"(aiverse-loader:begin[^\n]*\n)(.*?)(\n[^\n]*aiverse-loader:end)", re.S)


def body(kind):
    return SVG if kind == "html" else json.dumps(SVG)


def main():
    check = "--check" in sys.argv
    bad = 0
    for rel, kind in TARGETS.items():
        path = os.path.join(ROOT, rel)
        text = open(path, encoding="utf-8").read()
        m = MARK.search(text)
        if not m:
            print(f"{rel}: no aiverse-loader:begin/end markers")
            bad += 1
            continue
        want = body(kind)
        if m.group(2) == want:
            continue
        if check:
            print(f"{rel}: loader copy is out of date (run tools/branding/logo/loader.py)")
            bad += 1
        else:
            open(path, "w", encoding="utf-8").write(text[:m.start(2)] + want + text[m.end(2):])
            print(f"updated {rel}")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
