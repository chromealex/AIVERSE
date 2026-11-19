#!/usr/bin/env python3
"""Resolve exactly the two PHYSIS/TerraWeave EOF-addition conflicts.

Run ONLY in the isolated, currently conflicted integration worktree.
Requires Git merge in progress, with exactly AGENTS.md and CMakeLists.txt
unmerged. Preserves *both* PHYSIS and TerraWeave additions verbatim.
Creates backup copies in a temporary folder. Does NOT commit or push.
"""
import pathlib
import subprocess
import sys
import tempfile

EXPECTED = {"AGENTS.md", "CMakeLists.txt"}
REQUIRED = {
    "AGENTS.md": (
        ("## PHYSIS", "engine/physis/"),
        ("## TerraWeave", "engine/world/TerraWeave.h"),
    ),
    "CMakeLists.txt": (
        ("aiverse_physis_nature_fall_host_test",
         "aiverse_physis_terraweave_fall_bridge_test"),
        ("add_executable(aiverse_terraweave_test",
         "aiverse_gpu_timing_router_test"),
    ),
}

def git(*args):
    return subprocess.run(
        ["git", *args], text=True, capture_output=True, check=False
    )

def parse_one_eof_conflict(text, name):
    lines = text.splitlines(keepends=True)
    matches = [i for i, line in enumerate(lines)
               if line.startswith("<<<<<<< ")]
    if len(matches) != 1:
        raise ValueError(
            "{}: expected exactly one conflict, found {}".format(name, len(matches))
        )
    start = matches[0]
    mode, ours, base, theirs = "ours", [], [], []
    end = None
    for i in range(start + 1, len(lines)):
        line = lines[i]
        if mode == "ours" and line.startswith("||||||| "):
            mode = "base"
            continue
        if line.rstrip("\r\n") == "=======" and mode in ("ours", "base"):
            mode = "theirs"
            continue
        if mode == "theirs" and line.startswith(">>>>>>> "):
            end = i + 1
            break
        if line.startswith("<<<<<<< ") or line.startswith(">>>>>>> "):
            raise ValueError("{}: nested or malformed conflict".format(name))
        if mode == "ours":
            ours.append(line)
        elif mode == "base":
            base.append(line)
        elif mode == "theirs":
            theirs.append(line)
    if end is None or mode != "theirs" or not ours or not theirs:
        raise ValueError("{}: incomplete conflict markers".format(name))
    if "".join(lines[end:]).strip():
        raise ValueError("{}: conflict is not at EOF; refusing".format(name))
    left, right = "".join(ours), "".join(theirs)
    for anchor in REQUIRED[name][0]:
        if anchor not in left:
            raise ValueError("{}: missing PHYSIS-side anchor {}".format(name, anchor))
    for anchor in REQUIRED[name][1]:
        if anchor not in right:
            raise ValueError("{}: missing TerraWeave-side anchor {}".format(name, anchor))
    replacement = left
    if not replacement.endswith("\n"):
        replacement += "\n"
    replacement += "\n" + right.lstrip("\n")
    result = "".join(lines[:start]) + replacement + "".join(lines[end:])
    if any(marker in result for marker in (
            "\n<<<<<<< ", "\n>>>>>>> ", "\n||||||| ", "\n=======\n")):
        raise ValueError("{}: conflict markers remain".format(name))
    return result

def main():
    root = git("rev-parse", "--show-toplevel")
    merge = git("rev-parse", "-q", "--verify", "MERGE_HEAD")
    unmerged = git("diff", "--name-only", "--diff-filter=U")
    if root.returncode or merge.returncode or unmerged.returncode:
        raise ValueError("Run in an active local Git merge worktree")
    files = set(unmerged.stdout.splitlines())
    if files != EXPECTED:
        raise ValueError("Unexpected conflicted paths: {}".format(sorted(files)))
    root_path = pathlib.Path(root.stdout.strip())
    originals, resolved = {}, {}
    for name in sorted(EXPECTED):
        original = (root_path / name).read_text(encoding="utf-8")
        originals[name] = original
        resolved[name] = parse_one_eof_conflict(original, name)

    # Stage the resolution only after both files have passed every guard.
    backup = pathlib.Path(tempfile.mkdtemp(prefix="aiverse-physis-merge-"))
    for name, text in originals.items():
        (backup / name).write_text(text, encoding="utf-8")
    for name, text in resolved.items():
        (root_path / name).write_text(text, encoding="utf-8")
    added = git("add", "--", *sorted(EXPECTED))
    if added.returncode:
        raise RuntimeError("git add failed: {}".format(added.stderr.strip()))
    rest = git("diff", "--name-only", "--diff-filter=U")
    if rest.returncode or rest.stdout.strip():
        raise RuntimeError("Unmerged paths remain: {}".format(rest.stdout))
    print("PHYSIS + TerraWeave: both EOF additions preserved and staged")
    print("Backups:", backup)
    print("Merge still NOT committed. Run CMake/CTest before committing.")
    print("Review: git diff --cached -- AGENTS.md CMakeLists.txt")

if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print("Refusing merge resolution:", exc, file=sys.stderr)
        sys.exit(1)
