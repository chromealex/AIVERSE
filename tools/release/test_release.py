#!/usr/bin/env python3
"""Tests of the builds-branch tooling.

    python3 tools/release/test_release.py [path/to/aiversemcp]

1. index.json (publish_builds.write_index): fake packages for windows / macos / linux, every field checked.
2. With an aiversemcp path: package_engine.py --platform linux from a folder holding only that aiversemcp (a
   tools-only package), unpack it to a temp folder outside the repo and drive the unpacked aiversemcp over
   MCP stdio WITHOUT --engine: tools/list, create_project from a template, map_generate, map_validate,
   map_ascii, rules_schema. The source path compiled into that copy is overwritten with a missing one
   (as on a machine that never had the sources), so it must find the engine next to itself; the
   templates it lists must come from the unpacked folder. HOME is a temp folder (recent projects).
"""
import hashlib, json, os, shutil, stat, struct, subprocess, sys, tempfile, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
import publish_builds  # noqa: E402


def fake_pe(machine):
    head = bytearray(0x100)
    head[:2] = b"MZ"
    struct.pack_into("<I", head, 0x3C, 0x80)
    head[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<H", head, 0x84, machine)
    return bytes(head)


def fake_elf(machine):
    head = bytearray(64)
    head[:4] = b"\x7fELF"
    struct.pack_into("<H", head, 18, machine)
    return bytes(head)


def fake_macho(cpu):
    return b"\xcf\xfa\xed\xfe" + struct.pack("<I", cpu) + bytes(56)


def write_zip(path, files):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with zipfile.ZipFile(path, "w") as z:
        for name, data in files.items():
            z.writestr(name, data)


def test_index():
    with tempfile.TemporaryDirectory(prefix="rts-index-") as work:
        pkgs = {
            "windows/AIVERSE-0.2.0-aaaaaaaa-windows.zip": {
                "AIVERSE/bin/aiversemcp.exe": fake_pe(0x8664), "AIVERSE/bin/aiverse_editor.exe": b"x",
                "AIVERSE/bin/aiverse_headless.exe": fake_pe(0x8664), "AIVERSE/templates/skirmish/game.json": b"{}"},
            "macos/AIVERSE-0.2.0-aaaaaaaa-macos.zip": {
                "AIVERSE/bin/aiversemcp": b"\xca\xfe\xba\xbe" + bytes(60),
                "AIVERSE/bin/AiverseEditor.app/Contents/MacOS/AiverseEditor": b"x"},
            "macos/AIVERSE-0.1.9-bbbbbbbb-macos.zip": {"AIVERSE/bin/aiversemcp": fake_macho(0x0100000C)},
            "linux/AIVERSE-0.2.0-aaaaaaaa-linux.zip": {
                "AIVERSE/bin/aiversemcp": fake_elf(62), "AIVERSE/bin/aiverse_headless": fake_elf(62)},
        }
        for rel, files in pkgs.items():
            write_zip(os.path.join(work, rel), files)
        order = list(pkgs)
        idx = publish_builds.write_index(work, order, "owner/repo")
        with open(os.path.join(work, "index.json"), encoding="utf-8") as fh:
            assert json.load(fh) == idx, "index.json differs from the returned index"
        assert idx["schema"] == 1 and idx["repository"] == "owner/repo" and idx["branch"] == "builds", idx
        assert [p["file"] for p in idx["packages"]] == order, "packages keep the newest-first order"
        by = {p["file"]: p for p in idx["packages"]}
        w = by[order[0]]
        assert (w["platform"], w["arch"], w["version"], w["commit"]) == ("windows", "x86_64", "0.2.0", "aaaaaaaa"), w
        assert w["rtsmcp"] == "AIVERSE/bin/aiversemcp.exe" and w["editor"] == "AIVERSE/bin/aiverse_editor.exe", w
        assert w["headless"] == "AIVERSE/bin/aiverse_headless.exe" and w["root"] == "AIVERSE", w
        assert w["url"] == "https://raw.githubusercontent.com/owner/repo/builds/" + order[0], w
        data = open(os.path.join(work, order[0]), "rb").read()
        assert w["size"] == len(data) and w["sha256"] == hashlib.sha256(data).hexdigest(), w
        m = by[order[1]]
        assert m["arch"] == "universal" and m["editor"] == "AIVERSE/bin/AiverseEditor.app" and m["headless"] is None, m
        assert by[order[2]]["arch"] == "arm64" and by[order[2]]["version"] == "0.1.9", by[order[2]]
        l = by[order[3]]
        assert l["arch"] == "x86_64" and l["editor"] is None and l["rtsmcp"] == "AIVERSE/bin/aiversemcp", l
    for url, slug in [("https://github.com/chromealex/rts-engine", "chromealex/rts-engine"),
                      ("https://github.com/chromealex/rts-engine.git", "chromealex/rts-engine"),
                      ("git@github.com:chromealex/rts-engine.git", "chromealex/rts-engine")]:
        assert publish_builds.repo_slug(url) == slug, (url, publish_builds.repo_slug(url))
    print("index.json: ok")


def unzip_with_modes(path, dest):
    """zipfile.extractall drops the executable bits package_engine.py stored; restore them."""
    with zipfile.ZipFile(path) as z:
        for info in z.infolist():
            out = z.extract(info, dest)
            mode = (info.external_attr >> 16) & 0o777
            if mode and not info.is_dir():
                os.chmod(out, mode)


def forget_source_dir(src, dst, source_dir):
    """Copy of an executable whose compiled-in AIVERSE_SOURCE_DIR points nowhere (same length, so the
    binary stays valid): what a package built on another machine sees."""
    data = open(src, "rb").read()
    old = source_dir.encode()
    new = ("/nonexistent-rts-src" + "x" * len(old))[:len(old)].encode()
    assert len(old) >= 8 and old in data, f"{source_dir} is not compiled into {src}"
    with open(dst, "wb") as f:
        f.write(data.replace(old, new))
    os.chmod(dst, 0o755)


class Mcp:
    def __init__(self, exe, cwd, home):
        # no --engine: the packaged aiversemcp must find AIVERSE/ next to itself
        env = dict(os.environ, HOME=home, USERPROFILE=home)
        env.pop("AIVERSE_ENGINE_DIR", None)
        self.p = subprocess.Popen([exe, "--no-editor", "--port", "1"], cwd=cwd, stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, text=True, env=env)
        self.seq = 0

    def request(self, method, params=None):
        self.seq += 1
        self.p.stdin.write(json.dumps({"jsonrpc": "2.0", "id": self.seq, "method": method, "params": params or {}}) + "\n")
        self.p.stdin.flush()
        msg = json.loads(self.p.stdout.readline())
        assert msg.get("id") == self.seq and "error" not in msg, msg
        return msg["result"]

    def call(self, name, args=None):
        r = self.request("tools/call", {"name": name, "arguments": args or {}})
        text = r["content"][0]["text"]
        assert not r.get("isError"), f"{name}: {text}"
        return text

    def close(self):
        self.p.stdin.close()
        assert self.p.wait(timeout=60) == 0


def test_package(aiversemcp):
    with tempfile.TemporaryDirectory(prefix="rts-tools-pkg-") as tmp:
        bin_dir = os.path.join(tmp, "bin")
        os.makedirs(bin_dir)
        forget_source_dir(aiversemcp, os.path.join(bin_dir, "aiversemcp"), ROOT)
        zpath = subprocess.check_output([sys.executable, os.path.join(HERE, "package_engine.py"), "--platform", "linux",
                                         "--bin", bin_dir, "--out", os.path.join(tmp, "dist")], text=True).strip()
        assert os.path.basename(zpath).endswith("-linux.zip"), zpath
        unpacked = os.path.join(tmp, "unpacked")
        unzip_with_modes(zpath, unpacked)
        os.remove(zpath)
        exe = os.path.join(unpacked, "AIVERSE", "bin", "aiversemcp")
        assert os.stat(exe).st_mode & stat.S_IXUSR, "aiversemcp lost its executable bit"
        work, home = os.path.join(tmp, "work"), os.path.join(tmp, "home")
        os.makedirs(work)
        os.makedirs(home)
        mcp = Mcp(exe, work, home)
        try:
            init = mcp.request("initialize", {"protocolVersion": "2025-06-18"})
            assert init["serverInfo"]["name"] == "aiverse", init
            tools = {t["name"] for t in mcp.request("tools/list")["tools"]}
            for need in ("create_project", "map_generate", "map_validate", "map_ascii", "match_run", "rules_schema"):
                assert need in tools, f"tools/list lacks {need}"
            engine = os.path.realpath(os.path.join(unpacked, "AIVERSE"))
            templates = json.loads(mcp.call("list_projects"))["templates"]
            dirs = [t["dir"] for t in templates if "dir" in t]
            assert dirs and all(os.path.realpath(d).startswith(engine + os.sep) for d in dirs), dirs
            created = json.loads(mcp.call("create_project", {"name": "Bundle Check", "template": "skirmish",
                                                              "location": os.path.join(tmp, "projects")}))
            assert created.get("ok") and os.path.isfile(os.path.join(created["dir"], "game.json")), created
            mcp.call("map_generate", {"width": 48, "height": 40, "players": 2, "seed": 7})
            valid = json.loads(mcp.call("map_validate"))
            assert not valid.get("errors"), valid
            ascii_map = mcp.call("map_ascii", {"maxWidth": 64})
            assert len(ascii_map.splitlines()) >= 20, ascii_map
            schema = mcp.call("rules_schema")   # reads docs/ from the packaged engine
            assert len(schema) > 1000, schema[:200]
        finally:
            mcp.close()
    print(f"packaged aiversemcp: ok ({len(tools)} tools)")


if __name__ == "__main__":
    test_index()
    if len(sys.argv) > 1:
        test_package(os.path.abspath(sys.argv[1]))
