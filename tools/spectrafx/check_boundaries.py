#!/usr/bin/env python3
"""Fail if the engine's SpectraFX runtime acquires a genre/editor dependency."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
FILES = sorted((ROOT / "engine" / "fx").glob("SpectraFx*.h"))
FILES += [ROOT / "engine" / "fx" / "ParticleSystem.h",
          ROOT / "engine" / "fx" / "ParticleSystem.cpp"]
FILES += [ROOT / "engine" / "fx" / (name + suffix)
          for name in ("SpectraBatchWorld", "SpectraGpuPoolWorld",
                       "SpectraGpuParticles", "SpectraGraphRuntime")
          for suffix in (".h", ".cpp")]
REQUIRED = ROOT / "engine" / "fx" / "SpectraFxView.h"
errors = []
if REQUIRED not in FILES:
    errors.append("SpectraFxView.h missing")
for path in FILES:
    rel = path.relative_to(ROOT)
    content = path.read_text(encoding="utf-8")
    for line_no, line in enumerate(content.splitlines(), 1):
        stripped = line.strip()
        if stripped.startswith("//") or stripped.startswith("*"):
            continue
        if re.search(r"\bRTSCamera\b", line):
            errors.append(f"{rel}:{line_no}: concrete genre camera in core")
        match = re.match(r'\s*#\s*include\s*[<"]([^>"]+)[>"]', line)
        if match and re.search(r"(?:^|/)(?:modules|apps)/(?:fps|rts|editor|client)(?:/|$)", match.group(1)):
            errors.append(f"{rel}:{line_no}: genre/client/editor include: {match.group(1)}")
if errors:
    print("\n".join(errors), file=sys.stderr)
    sys.exit(1)
print(f"PASS SpectraFX independent of genre/editor headers ({len(FILES)} runtime files)")
