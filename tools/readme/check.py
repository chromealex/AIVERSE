#!/usr/bin/env python3
"""Check README.md and its translations README.<lang>.md.

README.md (English) is the source of truth. Every translation must have the same structure:
the same sections, the same table rows, the same code blocks, the same images and the same
repository links, in the same order. Relative links and images must exist.

Usage: python3 tools/readme/check.py            # exit 1 on problems
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LANGS = ["ru", "zh-CN", "ja", "ko", "es", "pt-BR", "de", "fr"]


def strip_code(text):
    return re.sub(r"```.*?```", "", text, flags=re.S)


def shape(text):
    """Language-independent skeleton of a README."""
    body = strip_code(text)
    return {
        "sections": len(re.findall(r"^## ", body, re.M)),
        "table rows": len(re.findall(r"^\|", body, re.M)),
        # commands only: comments and quoted names (project titles) may be translated
        "code blocks": [[re.sub(r'"[^"]*"', '""', ln.split("#")[0]).strip() for ln in b.splitlines()]
                        for b in re.findall(r"```(?:bash|sh)\n(.*?)```", text, re.S)],
        "images": [i for i in re.findall(r'<img[^>]+src="([^"]+)"', body) if "shields.io" not in i]
                  + re.findall(r"!\[[^\]]*\]\(([^)]+)\)", body),
        "repo links": [l for l in re.findall(r'\]\(([^)#\s]+)', body) if not l.startswith("http")],
    }


def local_targets(text):
    body = strip_code(text)
    out = re.findall(r'\]\(([^)\s]+)\)', body) + re.findall(r'(?:href|src)="([^"]+)"', body)
    return sorted({t.split("#")[0] for t in out if not t.startswith(("http", "#", "mailto:")) and t.split("#")[0]})


def main():
    problems = []
    src_path = os.path.join(ROOT, "README.md")
    src = open(src_path, encoding="utf-8").read()
    ref = shape(src)
    files = {"README.md": src}
    for lang in LANGS:
        name = f"README.{lang}.md"
        path = os.path.join(ROOT, name)
        if not os.path.exists(path):
            problems.append(f"{name}: missing")
            continue
        text = open(path, encoding="utf-8").read()
        files[name] = text
        got = shape(text)
        for key, want in ref.items():
            if got[key] != want:
                if isinstance(want, int):
                    problems.append(f"{name}: {key} {got[key]} != README.md {want}")
                else:
                    extra = [x for x in got[key] if x not in want]
                    lost = [x for x in want if x not in got[key]]
                    problems.append(f"{name}: {key} differ from README.md"
                                    + (f"; missing {lost[:3]}" if lost else "")
                                    + (f"; extra {extra[:3]}" if extra else "")
                                    + ("; order differs" if not lost and not extra else ""))
        for other in ["README.md"] + [f"README.{l}.md" for l in LANGS]:
            if other != name and f'href="{other}"' not in text:
                problems.append(f"{name}: language bar has no link to {other}")
    for name, text in files.items():
        for t in local_targets(text):
            if not os.path.exists(os.path.join(ROOT, t)):
                problems.append(f"{name}: broken link {t}")
    for p in problems:
        print(p)
    print(f"{len(files)} README files checked, {len(problems)} problem(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
