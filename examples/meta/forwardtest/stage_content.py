#!/usr/bin/env python3
"""Explicit public-content allowlist: never package the game project wholesale."""
from pathlib import Path
import shutil
root=Path(__file__).resolve().parent;out=root/'Builds/.temp/public-content';out.mkdir(parents=True,exist_ok=True)
(root/'Builds/.temp/evidence').mkdir(parents=True,exist_ok=True)
for name in ['game.json','rules.json','audio.json','branding','fonts','fx','lang','maps','models','scripts','ui']:
 src=root/name
 if src.is_dir():shutil.copytree(src,out/name,dirs_exist_ok=True)
 elif src.is_file():shutil.copy2(src,out/name)
print(out)
