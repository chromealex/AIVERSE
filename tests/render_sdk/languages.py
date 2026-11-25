"""Compare RGBA frames from the actual Python, .NET and Java SDK bindings."""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SDK = ROOT / "sdk/render"
sys.path.insert(0, str(SDK / "python"))
from aiverse_render import Renderer


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("library", type=Path)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    native = args.library.resolve()
    fixture = Path(__file__).resolve().parent
    args.work.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=args.work) as directory:
        temp = Path(directory)
        model = temp / "модель.obj"
        model.write_text("v -1 -1 0\nv 1 -1 0\nv 0 1 0\nf 1 2 3\n")
        lib = Renderer(native)
        with lib.open(model) as opened:
            reference = opened.render(96, 72)
            if reference == opened.render(96, 72, yaw=1.4):
                raise RuntimeError("Changing the camera did not change the frame")
            try:
                opened.render(96, 72, backend="unknown")
            except RuntimeError:
                pass
            else:
                raise RuntimeError("Unsupported renderer accepted")
        try:
            opened.render(96, 72)
        except RuntimeError:
            pass
        else:
            raise RuntimeError("Closed model accepted")
        background = reference[:4]
        foreground = sum(reference[i:i+4] != background for i in range(0, len(reference), 4))
        if not 100 < foreground < 96*72:
            raise RuntimeError("Expected visible geometry and background")
        env = os.environ.copy()
        key = "PATH" if os.name == "nt" else "DYLD_LIBRARY_PATH" if sys.platform == "darwin" else "LD_LIBRARY_PATH"
        env[key] = str(native.parent) + os.pathsep + env.get(key, "")
        env["DOTNET_CLI_HOME"] = str(temp / "dotnet")
        env["NUGET_PACKAGES"] = str(temp / "nuget")
        env["DOTNET_CLI_TELEMETRY_OPTOUT"] = "1"
        env["DOTNET_NOLOGO"] = "1"
        for src in (fixture/"RenderFrame.csproj", fixture/"RenderFrame.cs", SDK/"csharp/RenderModel.cs"):
            shutil.copy(src, temp/src.name)
        # Only reference packs installed with the .NET SDK are needed.
        (temp/"NuGet.Config").write_text("<configuration><packageSources><clear/></packageSources></configuration>")
        (temp/"global.json").write_text('{"sdk":{"version":"8.0.100","rollForward":"latestFeature"}}')
        def run(*command):
            subprocess.run(command, cwd=temp, env=env, check=True)
        run("dotnet", "build", "RenderFrame.csproj", "--nologo", "-v:q")
        run("dotnet", "bin/Debug/net8.0/RenderFrame.dll", model, "csharp.rgba")
        run("javac", "-d", temp, SDK/"java/org/aiverse/render/RenderModel.java", fixture/"RenderFrame.java")
        run("java", "-Djava.library.path="+str(native.parent), "-cp", temp, "RenderFrame", model, "java.rgba")
        for lang in ("csharp", "java"):
            if (temp/(lang+".rgba")).read_bytes() != reference:
                raise RuntimeError(lang+" frame differs from Python")
        print("Python, C# and Java RGBA8 match:", len(reference), "bytes;",
              foreground, "foreground pixels; sha256", hashlib.sha256(reference).hexdigest())


if __name__ == "__main__":
    main()
