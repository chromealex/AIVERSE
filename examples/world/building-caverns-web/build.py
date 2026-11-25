"""Build the standalone WebAssembly demo into an external build directory."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_directory", type=Path)
    args = parser.parse_args()
    source = Path(__file__).resolve().parent
    root = source.parents[2]
    build = args.build_directory.resolve()
    drive = Path("/Volumes/KINGSTON")
    if sys.platform == "darwin" and not (drive.is_mount() and drive in build.parents):
        parser.error("On macOS choose a build directory on the mounted KINGSTON volume.")
    deps = build / "dependencies"
    deps.mkdir(parents=True, exist_ok=True)
    dependency_files = ("package.json", "package-lock.json")
    digest = hashlib.sha256()
    for name in dependency_files:
        digest.update((source / name).read_bytes())
    marker = deps / ".installed"
    if not (marker.is_file() and marker.read_text() == digest.hexdigest()
            and (deps / "node_modules/esbuild/package.json").is_file()
            and (deps / "node_modules/three/package.json").is_file()):
        for name in dependency_files:
            shutil.copy(source / name, deps / name)
        subprocess.run(["npm", "ci", "--cache", str(build / "npm-cache"), "--no-audit", "--no-fund"],
                       cwd=deps, check=True)
        marker.write_text(digest.hexdigest())
    subprocess.run(["emcmake", "cmake", "-S", str(source), "-B", str(build),
                    "-DCMAKE_BUILD_TYPE=Release"], check=True)
    subprocess.run(["cmake", "--build", str(build), "--parallel", "4"], check=True)
    web = build / "web"
    modules = deps / "node_modules"
    bundle = modules / ".bin" / ("esbuild.cmd" if os.name == "nt" else "esbuild")
    subprocess.run([str(bundle), str(source / "viewer.js"), "--bundle", "--minify",
                    "--outfile=" + str(web / "viewer.js")], env={**os.environ, "NODE_PATH": str(modules)}, check=True)
    styles = {}
    for path in sorted((root / "engine/assets/building-styles").glob("*.json")):
        style = json.loads(path.read_bytes())
        styles[style["id"]] = list(style["structures"])
    catalog = {"styleRoles": styles, "buildingRecipe": json.loads((source / "building.recipe.json").read_bytes()),
               "buildingLayouts": json.loads((source / "layouts.json").read_bytes())}
    (web / "catalog.js").write_text("Object.assign(window," + json.dumps(catalog) + ");\n", encoding="utf-8")
    for name in ("index.html", "building.recipe.json"):
        shutil.copy(source / name, web / name)
    shutil.copy(modules / "three/LICENSE", web / "THREE-LICENSE.txt")
    shutil.copy(root / "LICENSE", web / "AIVERSE-LICENSE.txt")
    shutil.copy(root / "third_party/zstd/LICENSE", web / "ZSTD-LICENSE.txt")
    shutil.copy(root / "third_party/stb/LICENSE.txt", web / "STB-LICENSE.txt")
    print(web / "index.html")


if __name__ == "__main__":
    main()
