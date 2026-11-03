#!/usr/bin/env python3
"""Publish packaged engine zips (tools/release/package_engine.py) to the `builds` branch on GitHub.

    python3 tools/release/publish_builds.py build/dist/RTSEngine-0.2.0-1a2b3c4d-windows.zip [more zips] [--keep 5]

The `builds` branch is an orphan branch (no shared history with main) with <platform>/<zip> files and a
README listing them. It always is ONE commit that is force-pushed: the newest --keep versions per
platform stay in it, older ones disappear from the branch, so the repository does not grow with every
build. Work happens in build/.temp/builds (git-ignored). Authorship as the repo's git config.
"""
import argparse, os, re, shutil, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
WORK = os.path.join(ROOT, "build", ".temp", "builds")
NAME = re.compile(r"^RTSEngine-([0-9.]+)-([0-9a-f]+)-(\w+)\.zip$")


def run(*args, cwd=None, check=True):
    return subprocess.run(list(args), cwd=cwd, check=check, capture_output=True, text=True).stdout.strip()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("zips", nargs="+")
    ap.add_argument("--keep", type=int, default=5)
    ap.add_argument("--remote", default="origin")
    ap.add_argument("--no-push", action="store_true")
    a = ap.parse_args()
    for z in a.zips:
        if not NAME.match(os.path.basename(z)) or not os.path.isfile(z):
            sys.exit(f"not a package from package_engine.py: {z}")
    shutil.rmtree(WORK, ignore_errors=True)
    os.makedirs(WORK)
    run("git", "init", "-q", "-b", "builds", cwd=WORK)
    for k in ("user.name", "user.email"):
        v = run("git", "-C", ROOT, "config", k, check=False)
        if v: run("git", "config", k, v, cwd=WORK)
    url = run("git", "-C", ROOT, "remote", "get-url", a.remote)
    run("git", "remote", "add", "origin", url, cwd=WORK)
    # previous contents of the branch (if any): keep their zips
    if subprocess.run(["git", "fetch", "-q", "--depth=1", "origin", "builds"], cwd=WORK, capture_output=True).returncode == 0:
        run("git", "checkout", "-q", "FETCH_HEAD", "--", ".", cwd=WORK)
    for z in a.zips:
        plat = NAME.match(os.path.basename(z)).group(3)
        os.makedirs(os.path.join(WORK, plat), exist_ok=True)
        shutil.copy2(z, os.path.join(WORK, plat, os.path.basename(z)))
    # order: index.txt keeps "<platform>/<zip>" newest first; the zips published now go on top
    idx = os.path.join(WORK, "index.txt")
    order = [l.strip() for l in open(idx, encoding="utf-8")] if os.path.exists(idx) else []
    new = [NAME.match(os.path.basename(z)).group(3) + "/" + os.path.basename(z) for z in a.zips]
    order = new + [o for o in order if o and o not in new and os.path.exists(os.path.join(WORK, o))]
    lines = ["# RTSEngine builds", "",
             "Ready-to-run editor + tools. Unpack the zip anywhere and start `RTSEngine/bin/rts_editor.exe` (Windows) "
             "or `RTSEngine/bin/RTSEditor.app` (macOS). The folder also holds the engine sources at that commit, "
             "which rtsbuild uses to build games.", "",
             "Published by tools/release/publish_builds.py; this branch is a single commit that is replaced on "
             f"every publish (the newest {a.keep} versions per platform are kept).", ""]
    kept = []
    for plat in sorted({o.split("/")[0] for o in order}):
        mine = [o for o in order if o.startswith(plat + "/")]
        for old in mine[a.keep:]:
            os.remove(os.path.join(WORK, old))
        lines += [f"## {plat}", ""]
        for o in mine[:a.keep]:
            ver, commit, _ = NAME.match(os.path.basename(o)).groups()
            size = os.path.getsize(os.path.join(WORK, o)) / 1048576
            lines.append(f"- [{os.path.basename(o)}]({o}) — {ver}, commit `{commit}`, {size:.0f} MB")
            kept.append(o)
        lines.append("")
    with open(idx, "w", encoding="utf-8") as fh:
        fh.write("\n".join(o for o in order if o in kept) + "\n")
    with open(os.path.join(WORK, "README.md"), "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines))
    run("git", "add", "-A", cwd=WORK)
    names = ", ".join(os.path.basename(z) for z in a.zips)
    run("git", "commit", "-q", "-m", f"Builds: {names}", cwd=WORK)
    if not a.no_push:
        run("git", "push", "-q", "--force", "origin", "builds", cwd=WORK)
    print("published:", names)


if __name__ == "__main__":
    main()
