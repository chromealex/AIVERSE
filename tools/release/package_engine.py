#!/usr/bin/env python3
"""Package a built engine (editor + tools) for one platform as a zip anyone can unpack and run.

    python3 tools/release/package_engine.py --platform windows --bin build/.temp/windows [--out build/dist]
    python3 tools/release/package_engine.py --platform macos   --bin build/mac

The zip holds RTSEngine/ = a snapshot of the sources at HEAD (git archive: templates, engine assets,
everything rtsbuild needs to build games) + RTSEngine/bin/ with the executables. The editor and the
tools find the engine next to themselves (engine/platform/EngineDir.h), so the folder works anywhere.
Intermediates go to <out>/.temp; <out> gets only RTSEngine-<version>-<commit>-<platform>.zip.
Prints the zip path. Publishing: tools/release/publish_builds.py.
"""
import argparse, io, os, re, shutil, stat, subprocess, sys, tarfile, zipfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

BINARIES = {
    "windows": ["rts_editor.exe", "rtsmcp.exe", "rtsbuild.exe", "rts_headless.exe", "RTSGame.exe", "assetpipe.exe"],
    "macos": ["RTSEditor.app", "RTSGame.app", "rtsmcp", "rtsbuild", "rts_headless", "assetpipe"],
    "linux": ["rts_editor", "rtsmcp", "rtsbuild", "rts_headless", "RTSGame", "assetpipe"],
}
REQUIRED = {"windows": "rts_editor.exe", "macos": "RTSEditor.app", "linux": "rts_editor"}


def git(*args):
    return subprocess.check_output(["git", "-C", ROOT] + list(args)).decode().strip()


def version():
    text = open(os.path.join(ROOT, "CMakeLists.txt"), encoding="utf-8").read()
    m = re.search(r"project\(RTSEngine VERSION ([0-9.]+)", text)
    return (m.group(1) if m else "0.0.0"), git("rev-parse", "--short=8", "HEAD"), git("log", "-1", "--format=%cs")


def add_tree(z, src, arc):
    if os.path.isfile(src) or os.path.islink(src):
        info = zipfile.ZipInfo(arc)
        st = os.lstat(src)
        info.date_time = (1980, 1, 1, 0, 0, 0)
        info.compress_type = zipfile.ZIP_DEFLATED
        if os.path.islink(src):   # macOS bundles may carry symlinks
            info.external_attr = (stat.S_IFLNK | 0o777) << 16
            z.writestr(info, os.readlink(src))
        else:
            info.external_attr = (stat.S_IFREG | (0o755 if st.st_mode & 0o111 else 0o644)) << 16
            with open(src, "rb") as f:
                z.writestr(info, f.read())
        return
    for name in sorted(os.listdir(src)):
        add_tree(z, os.path.join(src, name), arc + "/" + name)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--platform", required=True, choices=sorted(BINARIES))
    ap.add_argument("--bin", required=True, help="build dir with the executables")
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "dist"))
    a = ap.parse_args()
    if git("status", "--porcelain", "--untracked-files=no"):
        print("warning: uncommitted changes are not in the source snapshot (git archive HEAD)", file=sys.stderr)
    if not os.path.exists(os.path.join(a.bin, REQUIRED[a.platform])):
        sys.exit(f"no {REQUIRED[a.platform]} in {a.bin}")
    ver, commit, date = version()
    name = f"RTSEngine-{ver}-{commit}-{a.platform}.zip"
    os.makedirs(os.path.join(a.out, ".temp"), exist_ok=True)
    tmp_zip = os.path.join(a.out, ".temp", name)
    src_tar = subprocess.check_output(["git", "-C", ROOT, "archive", "--format=tar", "HEAD"])
    with zipfile.ZipFile(tmp_zip, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        with tarfile.open(fileobj=io.BytesIO(src_tar)) as t:
            for m in t.getmembers():
                info = zipfile.ZipInfo("RTSEngine/" + m.name + ("/" if m.isdir() else ""))
                info.date_time = (1980, 1, 1, 0, 0, 0)
                info.compress_type = zipfile.ZIP_DEFLATED
                if m.isdir():
                    info.external_attr = (stat.S_IFDIR | 0o755) << 16
                    z.writestr(info, b"")
                elif m.issym():
                    info.external_attr = (stat.S_IFLNK | 0o777) << 16
                    z.writestr(info, m.linkname)
                elif m.isfile():
                    info.external_attr = (stat.S_IFREG | (m.mode & 0o777)) << 16
                    z.writestr(info, t.extractfile(m).read())
        for b in BINARIES[a.platform]:
            p = os.path.join(a.bin, b)
            if os.path.exists(p):
                add_tree(z, p, "RTSEngine/bin/" + b)
        z.writestr("RTSEngine/BUILD.txt", f"RTSEngine {ver}\ncommit {commit} ({date})\nplatform {a.platform}\n")
    final = os.path.join(a.out, name)
    shutil.move(tmp_zip, final)
    print(final)


if __name__ == "__main__":
    main()
