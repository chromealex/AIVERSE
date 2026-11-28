# AIVERSE builds

Ready-to-run editor + tools. Unpack the zip anywhere and launch the editor at the path listed in index.json for your package. The folder also holds the engine sources at that commit, which aiversebuild and the MCP server (`AIVERSE/bin/aiversemcp`) use.

Programs that download a build read [index.json](index.json): every package with platform, arch, version, commit, URL, size, sha256 and the paths of aiversemcp / the editor inside the zip.

Published by tools/release/publish_builds.py; this branch is a single commit that is replaced on every publish (the newest 5 versions per platform are kept).

## macos

- [AIVERSE-3.1.0-c65a0b75-macos.zip](https://github.com/chromealex/AIVERSE/releases/download/v3.1.0/AIVERSE-3.1.0-c65a0b75-macos.zip) — 3.1.0, commit `c65a0b75`, 323 MB (release asset)
- [AIVERSE-3.0.0-9e7e3b80-macos.zip](https://github.com/chromealex/aiverse/releases/download/v3.0.0/AIVERSE-3.0.0-9e7e3b80-macos.zip) — 3.0.0, commit `9e7e3b80`, 240 MB (release asset)
- [AIVERSE-2.1.0-082f4d63-macos.zip](macos/AIVERSE-2.1.0-082f4d63-macos.zip) — 2.1.0, commit `082f4d63`, 76 MB
- [AIVERSE-0.2.0-16d43acf-macos.zip](macos/AIVERSE-0.2.0-16d43acf-macos.zip) — 0.2.0, commit `16d43acf`, 83 MB
- [AIVERSE-0.2.0-103420e6-macos.zip](macos/AIVERSE-0.2.0-103420e6-macos.zip) — 0.2.0, commit `103420e6`, 84 MB

## windows

- [AIVERSE-3.1.0-c65a0b75-windows.zip](https://github.com/chromealex/AIVERSE/releases/download/v3.1.0/AIVERSE-3.1.0-c65a0b75-windows.zip) — 3.1.0, commit `c65a0b75`, 268 MB (release asset)
- [AIVERSE-3.0.0-9e7e3b80-windows.zip](https://github.com/chromealex/aiverse/releases/download/v3.0.0/AIVERSE-3.0.0-9e7e3b80-windows.zip) — 3.0.0, commit `9e7e3b80`, 257 MB (release asset)
- [AIVERSE-2.1.0-082f4d63-windows.zip](windows/AIVERSE-2.1.0-082f4d63-windows.zip) — 2.1.0, commit `082f4d63`, 88 MB
- [AIVERSE-2.0.0-ad6025b5-windows.zip](windows/AIVERSE-2.0.0-ad6025b5-windows.zip) — 2.0.0, commit `ad6025b5`, 93 MB
- [AIVERSE-0.2.0-16d43acf-windows.zip](windows/AIVERSE-0.2.0-16d43acf-windows.zip) — 0.2.0, commit `16d43acf`, 92 MB
