"""Check the Studio distribution directory without launching the GUI."""
import argparse
import json
import subprocess
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import unquote, urlsplit


class PageAssets(HTMLParser):
    def __init__(self):
        super().__init__()
        self.urls = []

    def handle_starttag(self, tag, attrs):
        attributes = dict(attrs)
        if tag == "script" and attributes.get("src"):
            self.urls.append(attributes["src"])
        if tag == "link" and attributes.get("rel") == "stylesheet":
            self.urls.append(attributes["href"])


def check_files(package, require_build_tool, require_player=False):
    required = []
    if package.suffix == ".app":
        host = package / "Contents/MacOS/AiverseStudioHTML5"
        server = host.with_name("aiversemcp")
        resources = package / "Contents/Resources"
        player_bundle = package / "Contents/Helpers/AiverseGame.app/Contents"
        player = player_bundle / "MacOS/AiverseGame"
        player_resources = player_bundle / "Resources"
    elif (package / "AiverseStudio").is_file():
        host = package / "AiverseStudio"
        server = package / "aiversemcp"
        resources = player_resources = package
        player = package / "aiverse_client"
    else:
        host = package / "AiverseStudio.exe"
        server = package / "aiversemcp.exe"
        resources = player_resources = package
        player = package / "AiverseGame.exe"
        required.append(package / "WebView2Loader.dll")

    required.extend([host, server, resources / "engine/fonts/ui.ttf"])
    if require_player:
        required.extend([player, player_resources / "content/engine/fonts/ui.ttf"])
    if require_build_tool:
        required.append(server.with_name("aiversebuild" + server.suffix))
    for relative in ("flame_16x4.png", "smoke_8x8.png"):
        required.append(resources / "engine/fx/physis" / relative)
    web = resources / "studio-web"
    page = PageAssets()
    page.feed((web / "index.html").read_text(encoding="utf-8"))
    if not page.urls:
        raise ValueError("Studio page has no bundled entry assets")
    for url in page.urls:
        asset = web / unquote(urlsplit(url).path).lstrip("/")
        required.append(asset)
    for path in required:
        if not path.is_file():
            raise FileNotFoundError(path)
        if path.stat().st_size == 0:
            raise ValueError(f"Empty bundled file: {path}")
    return server, len(page.urls)


def check(package, files_only=False, require_build_tool=False, require_player=False):
    server, entries = check_files(package, require_build_tool, require_player)
    if files_only:
        print(f"Verified Studio distribution files and {entries} web entry assets")
        return

    calls = [{"jsonrpc": "2.0", "id": index, "method": method, "params": {}}
             for index, method in enumerate(["initialize", "tools/list"], start=1)]
    response = subprocess.run([str(server), "--no-editor"], check=True, text=True,
                              input="".join(json.dumps(call) + "\n" for call in calls),
                              capture_output=True, timeout=30)
    info, catalog = [json.loads(line) for line in response.stdout.splitlines()]
    names = {tool["name"] for tool in catalog["result"]["tools"]}
    if info["result"]["serverInfo"]["name"] != "aiverse" or "game_file_read" not in names:
        raise ValueError("Bundled MCP server did not expose Studio project operations")
    print(f"Found Studio, {entries} web entry assets and {len(names)} MCP tools")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path, help="macOS .app or Linux/Windows executable directory")
    parser.add_argument("--files-only", action="store_true", help="Inspect a cross-built package without running its MCP server")
    parser.add_argument("--require-build-tool", action="store_true", help="Verify the packager is bundled")
    parser.add_argument("--require-player", "--require-windows-player", action="store_true", help="Verify the standalone player and its engine resources")
    args = parser.parse_args()
    check(args.package.resolve(), args.files_only, args.require_build_tool, args.require_player)
