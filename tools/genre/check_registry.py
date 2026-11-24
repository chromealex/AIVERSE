#!/usr/bin/env python3
"""Checks docs/capability-status.json against the repository (CTest name: genre_registry).

- every area has id, area, acceptance, status (planned | partial | core), libraries, tests, gameReady;
- every library id exists in libraries/catalog.json;
- every test name is registered with add_test(NAME ...) in the root CMakeLists.txt, i.e. ./run-tests.sh runs it;
- a "core" area lists at least one test (its acceptance criterion is checked by those tests);
- a "planned" area lists no libraries (otherwise it is at least "partial").

Usage: check_registry.py [REPO_ROOT]"""
import json
import re
import sys
from pathlib import Path

root = Path(sys.argv[1] if len(sys.argv) > 1 else Path(__file__).resolve().parents[2])
registry = json.loads((root / "docs/capability-status.json").read_text(encoding="utf-8"))
catalog = {p["id"] for p in json.loads((root / "libraries/catalog.json").read_text(encoding="utf-8"))["packages"]}
cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
ctest = set(re.findall(r"add_test\(\s*NAME\s+([A-Za-z0-9_.-]+)", cmake))

errors = []
seen = set()
for area in registry["areas"]:
    aid = area.get("id", "?")
    for key in ("id", "area", "acceptance", "status", "libraries", "tests", "gameReady"):
        if key not in area:
            errors.append(f"{aid}: missing '{key}'")
    if aid in seen:
        errors.append(f"{aid}: duplicate id")
    seen.add(aid)
    status = area.get("status")
    if status not in ("planned", "partial", "core"):
        errors.append(f"{aid}: unknown status '{status}'")
    for lib in area.get("libraries", []):
        if lib not in catalog:
            errors.append(f"{aid}: library '{lib}' is not in libraries/catalog.json")
    for test in area.get("tests", []):
        if test not in ctest:
            errors.append(f"{aid}: test '{test}' is not registered in the root CMakeLists.txt (not run by ./run-tests.sh)")
    if status == "core" and not area.get("tests"):
        errors.append(f"{aid}: status 'core' needs the tests that check the acceptance criterion")
    if status == "planned" and area.get("libraries"):
        errors.append(f"{aid}: has libraries, so it is at least 'partial'")

for e in errors:
    print("FAIL:", e)
counts = {s: sum(a.get("status") == s for a in registry["areas"]) for s in ("core", "partial", "planned")}
print(f"genre registry: {len(registry['areas'])} areas, {counts}")
sys.exit(1 if errors else 0)
