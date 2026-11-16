# AIVERSE builds

Ready-to-run editor + tools. Unpack the zip anywhere and start `AIVERSE/bin/aiverse_editor.exe` (Windows), `AIVERSE/bin/AiverseEditor.app` (macOS) or `AIVERSE/bin/aiverse_editor` (Linux, when the package has it). The folder also holds the engine sources at that commit, which aiversebuild and the MCP server (`AIVERSE/bin/aiversemcp`) use.

Programs that download a build read [index.json](index.json): every package with platform, arch, version, commit, URL, size, sha256 and the paths of aiversemcp / the editor inside the zip.

Published by tools/release/publish_builds.py; this branch is a single commit that is replaced on every publish (the newest 5 versions per platform are kept).

## macos

- [AIVERSE-2.1.0-082f4d63-macos.zip](macos/AIVERSE-2.1.0-082f4d63-macos.zip) — 2.1.0, commit `082f4d63`, 76 MB
- [AIVERSE-0.2.0-16d43acf-macos.zip](macos/AIVERSE-0.2.0-16d43acf-macos.zip) — 0.2.0, commit `16d43acf`, 83 MB
- [AIVERSE-0.2.0-103420e6-macos.zip](macos/AIVERSE-0.2.0-103420e6-macos.zip) — 0.2.0, commit `103420e6`, 84 MB
- [AIVERSE-0.2.0-55aa9353-macos.zip](macos/AIVERSE-0.2.0-55aa9353-macos.zip) — 0.2.0, commit `55aa9353`, 87 MB
- [AIVERSE-0.2.0-413cec3a-macos.zip](macos/AIVERSE-0.2.0-413cec3a-macos.zip) — 0.2.0, commit `413cec3a`, 76 MB

## windows

- [AIVERSE-2.1.0-082f4d63-windows.zip](windows/AIVERSE-2.1.0-082f4d63-windows.zip) — 2.1.0, commit `082f4d63`, 88 MB
- [AIVERSE-2.0.0-ad6025b5-windows.zip](windows/AIVERSE-2.0.0-ad6025b5-windows.zip) — 2.0.0, commit `ad6025b5`, 93 MB
- [AIVERSE-0.2.0-16d43acf-windows.zip](windows/AIVERSE-0.2.0-16d43acf-windows.zip) — 0.2.0, commit `16d43acf`, 92 MB
- [AIVERSE-0.2.0-103420e6-windows.zip](windows/AIVERSE-0.2.0-103420e6-windows.zip) — 0.2.0, commit `103420e6`, 100 MB
- [AIVERSE-0.2.0-55aa9353-windows.zip](windows/AIVERSE-0.2.0-55aa9353-windows.zip) — 0.2.0, commit `55aa9353`, 65 MB
