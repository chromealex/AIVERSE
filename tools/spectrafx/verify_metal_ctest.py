#!/usr/bin/env python3
"""Summarize real MetalGPU CTest outcomes without confusing SKIPPED with PASS."""
import argparse
import re
from pathlib import Path

EXPECTED = ("spectrafx_metal_gpu", "spectrafx_metal_billboard",
            "spectrafx_metal_rhi", "spectrafx_metal_scene", "spectrafx_metal_allocator")
TEST_LINE = re.compile(
    r"^\s*\d+/\d+\s+Test\s+#\d+:\s+"
    r"(spectrafx_metal_(?:gpu|billboard|rhi|scene|allocator))\s+(.+)$"
)

def parse_results(log):
    results = {}
    for line in log.splitlines():
        match = TEST_LINE.match(line)
        if not match:
            continue
        tail = match.group(2)
        if re.search(r"\bPassed\b", tail):
            status = "PASS"
        elif re.search(r"Skipped|Not Run", tail):
            status = "SKIP"
        elif re.search(r"Failed|Timeout|Exception", tail):
            status = "FAIL"
        else:
            status = "UNKNOWN"
        results[match.group(1)] = status
    return {name: results.get(name, "MISSING") for name in EXPECTED}

def evaluate(log, strict=False):
    results = parse_results(log)
    passed = sum(status == "PASS" for status in results.values())
    skipped = sum(status == "SKIP" for status in results.values())
    missing = sum(status == "MISSING" for status in results.values())
    failed = sum(status in ("FAIL", "UNKNOWN") for status in results.values())
    detail = " ".join(f"{name}={status}" for name, status in results.items())
    print(f"[spectrafx] MetalGPU real PASS={passed}/{len(EXPECTED)} SKIP={skipped} "
          f"MISSING={missing} FAIL/UNKNOWN={failed}")
    print("[spectrafx] " + detail)
    if missing or failed:
        return 1
    return 1 if strict and skipped else 0

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path, help="Verbose ctest_metal.log path")
    parser.add_argument("--require-all", action="store_true",
                        help="fail on hardware skips as well as failures/missing tests")
    args = parser.parse_args()
    try:
        content = args.log.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        parser.error(str(exc))
    return evaluate(content, strict=args.require_all)

if __name__ == "__main__":
    raise SystemExit(main())
