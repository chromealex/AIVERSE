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


def check(package):
    if package.suffix == ".app":
        host = package / "Contents/MacOS/AiverseStudioHTML5"
        server = host.with_name("aiversemcp")
        web = package / "Contents/Resources/studio-web"
    else:
        host = package / "AiverseStudio.exe"
        server = package / "aiversemcp.exe"
        web = package / "studio-web"

    if not host.is_file():
        raise FileNotFoundError(host)
    page = PageAssets()
    page.feed((web / "index.html").read_text(encoding="utf-8"))
    if not page.urls:
        raise ValueError("Studio page has no bundled entry assets")
    for url in page.urls:
        asset = web / unquote(urlsplit(url).path).lstrip("/")
        if not asset.is_file():
            raise FileNotFoundError(asset)

    calls = [{"jsonrpc": "2.0", "id": index, "method": method, "params": {}}
             for index, method in enumerate(["initialize", "tools/list"], start=1)]
    response = subprocess.run([str(server), "--no-editor"], check=True, text=True,
                              input="".join(json.dumps(call) + "\n" for call in calls),
                              capture_output=True, timeout=30)
    info, catalog = [json.loads(line) for line in response.stdout.splitlines()]
    names = {tool["name"] for tool in catalog["result"]["tools"]}
    if info["result"]["serverInfo"]["name"] != "aiverse" or "game_file_read" not in names:
        raise ValueError("Bundled MCP server did not expose Studio project operations")
    print(f"Found Studio, {len(page.urls)} web entry assets and {len(names)} MCP tools")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path, help="macOS .app or Windows executable directory")
    check(parser.parse_args().package.resolve())
