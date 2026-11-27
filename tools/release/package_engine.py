#!/usr/bin/env python3
"""Package a built engine (editor + tools) for one platform as a zip anyone can unpack and run.

    python3 tools/release/package_engine.py --platform windows --bin build/.temp/windows [--out build/dist]
    python3 tools/release/package_engine.py --platform macos   --bin build/mac
    python3 tools/release/package_engine.py --platform linux   --bin build/linux-release

The zip holds AIVERSE/ = a snapshot of the sources at HEAD (git archive: engine assets, tools,
everything aiversebuild needs to build games) + AIVERSE/bin/ with the executables. The editor and the
tools find the engine next to themselves (engine/platform/EngineDir.h), so the folder works anywhere.
Intermediates go to <out>/.temp; <out> gets only AIVERSE-<version>-<commit>-<platform>.zip.
Prints the zip path. Publishing: tools/release/publish_builds.py.
"""
import argparse, io, os, re, shutil, stat, subprocess, sys, tarfile, zipfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

BINARIES = {
    "windows": ["AiverseStudio.exe", "aiversemcp.exe", "aiversebuild.exe", "aiverse_headless.exe", "AiverseGame.exe", "assetpipe.exe", "aiverse_modeling.exe", "aiverse_lod_simplify.exe", "aiverse_fps_server.exe"],
    "macos": ["AiverseStudioHTML5.app", "AiverseGame.app", "aiversemcp", "aiversebuild", "aiverse_headless", "assetpipe", "aiverse_modeling", "aiverse_lod_simplify", "aiverse_fps_server"],
    "linux": ["aiversemcp", "aiversebuild", "aiverse_headless", "AiverseGame", "assetpipe", "aiverse_modeling", "aiverse_lod_simplify", "aiverse_fps_server"],
}
# source paths left out of packages: benchmark screenshots are not needed to run the engine and
# would push a zip past GitHub's 100 MB file limit on the builds branch
EXCLUDE = ("docs/benchmarks/", "docs/devlog/media/",
           # source photos the bake scripts turn into engine/assets/materials; nothing reads them at run time
           "tools/terrain/pbr_sources/", "tools/vegetation/pbr_sources/")
STUDIO_ASSETS = ("engine/fonts/ui.ttf", "engine/fonts/LICENSE-ui.txt",
                 "engine/fx/physis/flame_16x4.png", "engine/fx/physis/smoke_8x8.png",
                 "engine/fx/physis/LICENSE-fx.txt", "studio-web/index.html")


def require_files(directory, paths):
    for path in paths:
        if not os.path.isfile(os.path.join(directory, path)):
            sys.exit(f"incomplete Studio build: missing {os.path.join(directory, path)}")


def git(*args):
    return subprocess.check_output(["git", "-C", ROOT] + list(args)).decode().strip()


def version():
    text = open(os.path.join(ROOT, "CMakeLists.txt"), encoding="utf-8").read()
    m = re.search(r"project\(AIVERSE VERSION ([0-9.]+)", text)
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
    binaries = list(BINARIES[a.platform])
    studio_bundle = "AiverseStudioHTML5.app"
    studio_files = {}
    if a.platform == "macos":
        for directory in (a.bin, os.path.join(a.bin, "apps", "studio-native")):
            if os.path.isdir(os.path.join(directory, studio_bundle)):
                contents = os.path.join(directory, studio_bundle, "Contents")
                require_files(contents, ("Info.plist", "MacOS/AiverseStudioHTML5", "MacOS/aiversemcp"))
                require_files(os.path.join(contents, "Resources"), STUDIO_ASSETS)
                studio_files[studio_bundle] = directory
                break
        else:
            sys.exit(f"missing {studio_bundle} in {a.bin}")
    if a.platform == "windows":
        nested = os.path.join(a.bin, "apps", "studio-native")
        candidates = [a.bin, os.path.join(nested, "Release"), nested]
        if os.path.basename(a.bin).lower() in ("release", "debug", "relwithdebinfo", "minsizerel"):
            candidates.append(os.path.join(os.path.dirname(a.bin), "apps", "studio-native", os.path.basename(a.bin)))
        for directory in candidates:
            if not os.path.isfile(os.path.join(directory, "AiverseStudio.exe")):
                continue
            require_files(directory, ("WebView2Loader.dll", "aiversemcp.exe") + STUDIO_ASSETS)
            resources = ["WebView2Loader.dll", "studio-web", "engine/fonts", "engine/fx/physis"]
            binaries.extend(resources)
            studio_files.update(dict.fromkeys(resources + ["AiverseStudio.exe", "aiversemcp.exe"], directory))
            if os.path.isfile(os.path.join(directory, "aiversebuild.exe")):
                studio_files["aiversebuild.exe"] = directory
            if directory == os.path.join(nested, "Release"):
                for binary in binaries:
                    if binary not in studio_files and os.path.isfile(os.path.join(a.bin, "Release", binary)):
                        studio_files[binary] = os.path.join(a.bin, "Release")
            break
        else:
            sys.exit(f"missing AiverseStudio.exe in {a.bin}")
    if a.platform == "linux":
        for directory in (a.bin, os.path.join(a.bin, "apps", "studio-native")):
            if not os.path.isfile(os.path.join(directory, "AiverseStudio")):
                continue
            require_files(directory, ("aiversemcp",) + STUDIO_ASSETS)
            resources = ["AiverseStudio", "studio-web", "engine/fonts", "engine/fx/physis"]
            if os.path.isfile(os.path.join(directory, "aiverse_client")):
                require_files(directory, ("content/engine/ui/theme.rcss",))
                resources.extend(["aiverse_client", "content/engine"])
            binaries.extend(resources)
            studio_files.update(dict.fromkeys(resources + ["aiversemcp"], directory))
            if os.path.isfile(os.path.join(directory, "aiversebuild")):
                studio_files["aiversebuild"] = directory
            break
        else:
            if not os.path.isfile(os.path.join(a.bin, "aiversemcp")):
                sys.exit(f"no Linux HTML Studio or MCP server in {a.bin}")
    ver, commit, date = version()
    name = f"AIVERSE-{ver}-{commit}-{a.platform}.zip"
    os.makedirs(os.path.join(a.out, ".temp"), exist_ok=True)
    tmp_zip = os.path.join(a.out, ".temp", name)
    src_tar = subprocess.check_output(["git", "-C", ROOT, "archive", "--format=tar", "HEAD"])
    with zipfile.ZipFile(tmp_zip, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        with tarfile.open(fileobj=io.BytesIO(src_tar)) as t:
            for m in t.getmembers():
                if m.name.startswith(EXCLUDE):
                    continue
                info = zipfile.ZipInfo("AIVERSE/" + m.name + ("/" if m.isdir() else ""))
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
        for b in binaries:
            p = os.path.join(studio_files.get(b, a.bin), b)
            if not os.path.exists(p) and b.startswith("aiverse_fps_"):
                p = os.path.join(a.bin, "engine", "actor", b)
            if os.path.exists(p):
                add_tree(z, p, "AIVERSE/bin/" + b)
        z.writestr("AIVERSE/BUILD.txt", f"AIVERSE {ver}\ncommit {commit} ({date})\nplatform {a.platform}\n")
    final = os.path.join(a.out, name)
    shutil.move(tmp_zip, final)
    print(final)


if __name__ == "__main__":
    main()
