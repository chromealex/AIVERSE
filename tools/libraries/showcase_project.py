#!/usr/bin/env python3
"""Create isolated showcase content from skirmish, preserving original template data."""
import argparse
import json
import shutil
import tempfile
from pathlib import Path
from gameplay import read_json
ROOT=Path(__file__).resolve().parents[2]
def create(output):
    if output.exists():raise ValueError('output already exists')
    output.parent.mkdir(parents=True,exist_ok=True)
    stage=Path(tempfile.mkdtemp(prefix='.showcase-',dir=str(output.parent)))
    try:
        shutil.copytree(ROOT/'templates/skirmish',stage/'content')
        content=stage/'content';(content/'lang').mkdir(exist_ok=True)
        for path in (ROOT/'examples/libraries/showcase/lang').glob('*.json'):
            target=content/'lang'/path.name;data=read_json(target) if target.exists() else {};data.update(read_json(path));target.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n')
        content.rename(output)
    finally:shutil.rmtree(stage)
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',required=True,type=Path);a=p.parse_args()
    try:create(a.output)
    except ValueError as e:p.error(str(e))
if __name__=='__main__':main()
