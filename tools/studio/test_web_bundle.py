#!/usr/bin/env python3
"""Verify that the Vite build is self-contained and loads from the app scheme."""
from __future__ import annotations
from html.parser import HTMLParser
from pathlib import Path
import os
import sys
import os
import unittest
from urllib.parse import urlsplit, unquote

ROOT=Path(__file__).resolve().parents[2]

class AssetHtmlParser(HTMLParser):
    def __init__(self):
        super().__init__()
        self.refs: list[tuple[str,str]]=[]
        self.module_scripts=0
    def handle_starttag(self,tag,attributes):
        attrs=dict(attributes)
        if tag=="script" and attrs.get("type")=="module":
            self.module_scripts+=1
        if tag=="script" and "src" in attrs:
            self.refs.append(("script",attrs["src"]))
        if tag=="link" and attrs.get("rel") in ("stylesheet","modulepreload"):
            if "href" in attrs:self.refs.append(("link",attrs["href"]))

def verify_bundle(dist: Path) -> list[str]:
    index=dist/"index.html"
    if not index.is_file():return ["Missing built index.html: "+str(index)]
    parser=AssetHtmlParser()
    parser.feed(index.read_text(encoding="utf-8"))
    errors=[]
    if parser.module_scripts==0:errors.append("No ES module entry in built HTML5 bundle")
    if not parser.refs:errors.append("No linked JS/CSS assets in built HTML5 bundle")
    root=dist.resolve()
    for tag,url in parser.refs:
        parsed=urlsplit(url)
        if parsed.scheme or parsed.netloc or url.startswith(("/", "//", "\\")):
            errors.append(f"Non-relative {tag} asset URL: {url}")
            continue
        relative=unquote(parsed.path)
        if not relative or "\\" in relative or relative.startswith("/"):
            errors.append(f"Invalid {tag} asset path: {url}")
            continue
        destination=(root/relative).resolve()
        try:
            destination.relative_to(root)
            contained=True
        except ValueError:
            contained=False
        if not contained or not destination.is_file():
            errors.append(f"Missing or escaping {tag} asset: {url}")
    return errors

class StudioBundleTests(unittest.TestCase):
    def test_bundled_es_modules_resolve_to_local_files(self):
        dist=Path(os.environ.get("AIVERSE_STUDIO_WEB_DIST", ROOT/"apps/studio-web/dist"))
        if not (dist/"index.html").exists():
            self.skipTest("Vite dist unavailable in source-only CI; native build script validates it")
        self.assertEqual(verify_bundle(dist),[])

    def test_source_has_registered_bundled_scheme(self):
        main=(ROOT/"apps/studio-native/mac/Main.mm").read_text(encoding="utf-8")
        handler=(ROOT/"apps/studio-native/mac/StudioAssetSchemeHandler.mm").read_text(encoding="utf-8")
        self.assertIn('setURLSchemeHandler:assets_',main)
        self.assertIn('aiverse-studio://shell/index.html',main)
        self.assertIn('@"text/javascript"',handler)

if __name__=="__main__":
    unittest.main()
