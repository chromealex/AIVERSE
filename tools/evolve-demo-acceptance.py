#!/usr/bin/env python3
"""Run only after building the native EVOLVE demo target."""
import argparse
import pathlib
import re
import subprocess
import tempfile
import xml.etree.ElementTree as ET

def main():
    p = argparse.ArgumentParser()
    p.add_argument("binary", type=pathlib.Path)
    p.add_argument("--map", type=pathlib.Path, default=pathlib.Path("examples/native/world_processes/evolve-forest-demo.json"))
    p.add_argument("--ticks", type=int, default=1024)
    args = p.parse_args()
    if not 1 <= args.ticks <= 500000:
        p.error("ticks must be 1..500000")
    binary, fixture = args.binary.resolve(), args.map.resolve()
    def run(ticks, *extra):
        result = subprocess.run([str(binary), str(fixture), str(ticks), *map(str, extra)],
                                check=True, capture_output=True, text=True, timeout=300)
        print(result.stdout, end="")
        match = re.search(r"tick=(\d+) checksum=(\d+)", result.stdout)
        if not match:
            raise RuntimeError("Missing tick/checksum diagnostics")
        return tuple(map(int, match.groups())), result.stdout
    with tempfile.TemporaryDirectory(prefix="evolve-acceptance-") as temp:
        save = pathlib.Path(temp) / "world.bin"
        svg = pathlib.Path(temp) / "ecology.svg"
        first, report = run(args.ticks, "--replay", "--save", save, "--svg", svg)
        if "rollback replay checksum matched" not in report:
            raise RuntimeError("Missing rollback evidence")
        if not save.stat().st_size:
            raise RuntimeError("Empty save")
        if ET.parse(svg).getroot().tag != "{http://www.w3.org/2000/svg}svg":
            raise RuntimeError("Invalid SVG root")
        restored, _ = run(0, "--load", save)
        if restored != first:
            raise RuntimeError("Disk roundtrip mismatch")
        resumed, _ = run(args.ticks, "--load", save)
        direct, _ = run(args.ticks * 2)
        if resumed != direct:
            raise RuntimeError("Resumed simulation diverged")
    print("PASS EVOLVE rollback, disk roundtrip, continuation and SVG")
if __name__ == "__main__":
    main()
