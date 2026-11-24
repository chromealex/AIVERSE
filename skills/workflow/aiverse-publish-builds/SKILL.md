---
name: aiverse-publish-builds
description: Publish AIVERSE editor packages for Windows and macOS to the builds branch as the MAIN agent - same main commit, packaging, publish script, index.json, versioning.
---

# Publish editor builds

Rules: AGENTS.md section 8. Only MAIN publishes; other agents write "republish due" in their report.

1. When: `main` got a verified change to the editor, engine or tools, and tests plus template matches passed on that exact commit.
2. Windows: cross-compile with mingw-w64 into `build/.temp/windows` (Ninja, Release, `AIVERSE_BUILD_TESTS=OFF`), targets `aiverse_editor aiversemcp aiversebuild aiverse_headless aiverse_client assetpipe`.
3. macOS: the Mac build (`build/mac`) of the same commit.
4. Package: `python3 tools/release/package_engine.py --platform windows|macos --bin <build dir>` -> `build/dist/*.zip` (Linux optional).
5. Publish: `python3 tools/release/publish_builds.py <zips>` - replaces the orphan `builds` branch with one commit keeping the newest 5 versions per platform (force-push) and writes `index.json`; keep its fields backward compatible. A zip over 95 MB is not committed: add `--release v<x.y.z>` and attach it to that GitHub release; index.json then points to the release asset.
6. Version: `project(AIVERSE VERSION x.y.z)` + short hash; bump for releases worth naming.
7. Push only with the user's go-ahead; report commit, versions, zip sizes, what was launched.

Runtime library: none. Repository release workflow.
