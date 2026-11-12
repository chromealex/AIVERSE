#!/usr/bin/env python3
"""Compile all public headers independently, then run the C++ tests."""
import os
import subprocess
import tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='rts-cpp-libraries-') as temp:
    tmp=Path(temp)
    for header in sorted((ROOT/'libraries/include/aiverselib').rglob('*.h')):
        source=tmp/'header.cpp'
        source.write_text('#include <aiverselib/'+header.relative_to(ROOT/'libraries/include/aiverselib').as_posix()+'>\n')
        subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-Wall','-Wextra','-Werror','-pedantic','-fsyntax-only','-I'+str(ROOT/'libraries/include'),'-I'+str(ROOT),str(source)],check=True)
    # Genre/bot packages share namespaces: every header must also compile together with all the others.
    together=tmp/'together.cpp'
    together.write_text(''.join('#include <aiverselib/'+h.relative_to(ROOT/'libraries/include/aiverselib').as_posix()+'>\n'
        for d in ('genre','bots') for h in sorted((ROOT/'libraries/include/aiverselib'/d).glob('*.h'))))
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-Wall','-Wextra','-Werror','-pedantic','-fsyntax-only','-I'+str(ROOT/'libraries/include'),'-I'+str(ROOT),str(together)],check=True)
    subprocess.run(['cmake','-S',str(ROOT/'tests/libraries'),'-B',str(tmp/'build'),'-DCMAKE_BUILD_TYPE=Release'],check=True)
    subprocess.run(['cmake','--build',str(tmp/'build'),'--parallel','2'],check=True)
    subprocess.run(['ctest','--test-dir',str(tmp/'build'),'--output-on-failure'],check=True)
