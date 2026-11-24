#!/usr/bin/env python3
"""Read-only cross-branch path audit. No checkout, merge, or ref writes."""
import argparse
import json
import subprocess
import sys


def git(*args):
    return subprocess.check_output(["git", *args])


def tree(ref):
    result = {}
    for entry in git("ls-tree", "-r", "-z", ref).split(b"\0"):
        if not entry:
            continue
        meta, path = entry.split(b"\t", 1)
        mode, kind, oid = meta.split()
        result[path.decode("utf-8", "surrogateescape")] = (mode, kind, oid)
    return result


def classify(base, left, right):
    groups = {key: [] for key in ("leftOnly", "rightOnly", "identical", "overlap")}
    for path in sorted(base.keys() | left.keys() | right.keys()):
        a, b, c = base.get(path), left.get(path), right.get(path)
        if a == b == c:
            continue
        if a == b:
            key = "rightOnly"
        elif a == c:
            key = "leftOnly"
        elif b == c:
            key = "identical"
        else:
            key = "overlap"
        groups[key].append(path)
    return groups


def self_test():
    result = classify(
        {"same": 1, "delete": 1, "conflict": 1, "bothDelete": 1},
        {"same": 1, "conflict": 2, "left": 1, "sameAdd": 3},
        {"same": 1, "delete": 1, "conflict": 3, "right": 1, "sameAdd": 3},
    )
    assert result == {
        "leftOnly": ["delete", "left"], "rightOnly": ["right"],
        "identical": ["bothDelete", "sameAdd"], "overlap": ["conflict"],
    }


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("left", nargs="?")
    p.add_argument("right", nargs="?")
    p.add_argument("--self-test", action="store_true")
    args = p.parse_args()
    if args.self_test:
        self_test()
        print("PASS classification self-test")
        return 0
    if not args.left or not args.right:
        p.error("two refs are required")
    left, right = [git("rev-parse", "--verify", ref + "^{commit}").decode().strip()
                   for ref in (args.left, args.right)]
    bases = git("merge-base", "--all", left, right).decode().splitlines()
    if len(bases) != 1:
        raise ValueError("expected one merge base; review criss-cross history manually")
    groups = classify(tree(bases[0]), tree(left), tree(right))
    print(json.dumps({
        "base": bases[0], "left": left, "right": right,
        "counts": {key: len(value) for key, value in groups.items()},
        "overlap": groups["overlap"],
        "studioOverlap": [p for p in groups["overlap"] if p.startswith("apps/studio-")],
        "note": "Path overlaps are review candidates, not proven textual conflicts. Rename analysis is separate.",
    }, indent=2, ensure_ascii=True))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (subprocess.CalledProcessError, ValueError) as exc:
        print(str(exc), file=sys.stderr)
        sys.exit(2)
