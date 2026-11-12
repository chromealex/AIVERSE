#!/usr/bin/env python3
"""Publish packaged engine zips (tools/release/package_engine.py) to the `builds` branch on GitHub.

    python3 tools/release/publish_builds.py build/dist/AIVERSE-0.2.0-1a2b3c4d-windows.zip [more zips] [--keep 5]

The `builds` branch is an orphan branch (no shared history with main) with <platform>/<zip> files and a
README listing them, plus index.json — the same list for programs that download a build (format:
docs/overview.md § Готовые сборки движка). It always is ONE commit that is force-pushed: the newest --keep versions per
platform stay in it, older ones disappear from the branch, so the repository does not grow with every
build. Work happens in build/.temp/builds (git-ignored). Authorship as the repo's git config.
"""
import argparse, hashlib, json, os, re, shutil, struct, subprocess, sys, zipfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
WORK = os.path.join(ROOT, "build", ".temp", "builds")
NAME = re.compile(r"^(?:AIVERSE|RTSEngine)-([0-9.]+)-([0-9a-f]+)-(\w+)\.zip$")
INDEX_SCHEMA = 1
# executables inside a package, by platform (package_engine.py: AIVERSE/bin/...)
EXE = {"windows": {"rtsmcp": "aiversemcp.exe", "editor": "aiverse_editor.exe", "headless": "aiverse_headless.exe"},
       "macos": {"rtsmcp": "aiversemcp", "editor": "AiverseEditor.app", "headless": "aiverse_headless"},
       "linux": {"rtsmcp": "aiversemcp", "editor": "aiverse_editor", "headless": "aiverse_headless"}}


def binary_arch(head):
    """CPU of an executable from its first bytes: x86_64, arm64, universal (Mach-O fat) or None."""
    if head[:2] == b"MZ" and len(head) >= 0x40:   # PE: machine after the PE signature
        off = struct.unpack_from("<I", head, 0x3C)[0]
        if head[off:off + 4] == b"PE\0\0":
            return {0x8664: "x86_64", 0xAA64: "arm64", 0x14C: "x86"}.get(struct.unpack_from("<H", head, off + 4)[0])
    if head[:4] == b"\x7fELF":
        return {62: "x86_64", 183: "arm64"}.get(struct.unpack_from("<H", head, 18)[0])
    if head[:4] == b"\xca\xfe\xba\xbe":
        return "universal"
    if head[:4] == b"\xcf\xfa\xed\xfe":
        return {0x01000007: "x86_64", 0x0100000C: "arm64"}.get(struct.unpack_from("<I", head, 4)[0])
    return None


def package_entry(work, rel, repo, branch):
    """One index.json record for <work>/<rel> (a zip from package_engine.py)."""
    ver, commit, plat = NAME.match(os.path.basename(rel)).groups()
    path = os.path.join(work, rel)
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    legacy = os.path.basename(rel).startswith("RTSEngine-")
    package_root = "RTSEngine" if legacy else "AIVERSE"
    names = dict(EXE.get(plat, EXE["linux"]))
    if legacy:
        names = {k: v.replace("aiversemcp", "rtsmcp").replace("aiverse_", "rts_").replace("AiverseEditor", "RTSEditor") for k, v in names.items()}
    found, arch = {}, None
    with zipfile.ZipFile(path) as z:
        listed = set(z.namelist())
        for key, exe in names.items():
            inner = package_root + "/bin/" + exe
            if inner in listed or inner + "/" in listed or any(n.startswith(inner + "/") for n in listed):
                found[key] = inner
        for key in ("rtsmcp", "headless"):   # arch from a plain executable (the editor may be an .app)
            if key in found and arch is None:
                with z.open(found[key]) as f:
                    arch = binary_arch(f.read(4096))
    return {"platform": plat, "arch": arch, "version": ver, "commit": commit, "file": rel,
            "url": f"https://raw.githubusercontent.com/{repo}/{branch}/{rel}",
            "size": os.path.getsize(path), "sha256": h.hexdigest(),
            "root": package_root, "rtsmcp": found.get("rtsmcp"), "editor": found.get("editor"),
            "headless": found.get("headless")}


def write_index(work, kept, repo, branch="builds"):
    """index.json next to README.md: every kept package, newest first (the order of index.txt)."""
    index = {"schema": INDEX_SCHEMA, "repository": repo, "branch": branch,
             "packages": [package_entry(work, rel, repo, branch) for rel in kept]}
    with open(os.path.join(work, "index.json"), "w", encoding="utf-8") as fh:
        json.dump(index, fh, indent=2)
        fh.write("\n")
    return index


def repo_slug(url):
    """owner/name from a GitHub remote URL (https or ssh)."""
    m = re.search(r"github\.com[:/]([^/]+/[^/]+?)(?:\.git)?/?$", url)
    return m.group(1) if m else url


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
    lines = ["# AIVERSE builds", "",
             "Ready-to-run editor + tools. Unpack the zip anywhere and start `AIVERSE/bin/aiverse_editor.exe` (Windows), "
             "`AIVERSE/bin/AiverseEditor.app` (macOS) or `AIVERSE/bin/aiverse_editor` (Linux, when the package has it). "
             "The folder also holds the engine sources at that commit, which aiversebuild and the MCP server "
             "(`AIVERSE/bin/aiversemcp`) use.", "",
             "Programs that download a build read [index.json](index.json): every package with platform, arch, "
             "version, commit, URL, size, sha256 and the paths of aiversemcp / the editor inside the zip.", "",
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
    write_index(WORK, [o for o in order if o in kept], repo_slug(url))
    run("git", "add", "-A", cwd=WORK)
    names = ", ".join(os.path.basename(z) for z in a.zips)
    run("git", "commit", "-q", "-m", f"Builds: {names}", cwd=WORK)
    if not a.no_push:
        run("git", "push", "-q", "--force", "origin", "builds", cwd=WORK)
    print("published:", names)


if __name__ == "__main__":
    main()
