#!/usr/bin/env python3
"""Compile project meta definitions into one validated service configuration."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--project', type=Path, required=True)
p.add_argument('--validator', type=Path, required=True)
a = p.parse_args()
folder = a.project/'meta'
config = {'version': 1}
for name in ('economy', 'boards', 'chats', 'extensions'):
    config[name] = json.loads((folder/(name+'.json')).read_text())
fd, temp = tempfile.mkstemp(prefix='.service-', suffix='.json', dir=folder)
try:
    with os.fdopen(fd, 'w') as f:
        json.dump(config, f, ensure_ascii=False, indent=2)
        f.write('\n')
    subprocess.run([str(a.validator.resolve()), 'service', temp], check=True)
    Path(temp).replace(folder/'service.generated.json')
finally:
    Path(temp).unlink(missing_ok=True)
print(folder/'service.generated.json')
