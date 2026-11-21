# AIVERSE HTML5 Studio — Stage 67: project-local content directory and scoped Asset listing

Date: 2026-11-20
Branch: feature/studio-html5
Parent SHA: 5f644134905b37bed783a0a8d28b27647584692b
Stage commit: see Git history.

## Feature contract
A project opened in the **HTML5 Studio** now owns a standard lower-case `content/` directory directly beneath its project root (`game.json` remains in the root). Opening a project through the file picker or passing `--project` on the native host creates this directory if missing. Creation is idempotent and refuses symlinks or a non-directory at that path.

The `studio.assets` typed bridge ignores all browser-provided directory values and returns ONLY direct/nested regular files in `project/content`; paths in the JSON response are **relative to the content root**, e.g. `Models/Ship.glb`. The reply includes `root:"content"`. No project service files, scenes, root `game.json`, or paths outside content are exposed.

## Architecture
- `apps/studio-native/StudioContentDirectory.h`: C++17/standard-library helper `studioContentRoot(projectDir)` (create/check) and `studioContentFiles(projectDir, limit=2000)` (read-only regular-file listing, ordered). Explicitly rejects root symlink, ignores symlinked files/directories, uses lexically relative paths. No game genres, renderers, platform APIs or ImGui.
- `StudioSession::StudioSession(engine, project)` and `StudioSession::openProject` call `studioContentRoot` after successful native Workspace.openGame.
- `StudioSession::dispatch` handles `studio.assets` directly through the dedicated scoped helper, not generic `Workspace.asset_list`, which intentionally lists whole projects for other editor/tools.
- No API for arbitrary directory browsing, file deletion or moving existing game files is added.
- Projects opened by *other clients* of Workspace are not mutated by this Studio-only feature; extending project scaffolding across the engine requires separate coordination with the agent owning Workspace/Templates.

## Files
apps/studio-native/StudioContentDirectory.h
apps/studio-native/tests/StudioContentDirectoryTests.cpp
apps/studio-native/StudioSession.cpp
apps/studio-native/CMakeLists.txt
tools/studio/verify_mac.sh
.github/workflows/studio-html5-web.yml
docs/handoff/studio-html5/67-project-content-native.md

## Validation
Added `studio_content_directory` CTest and CMake target, included in Mac script and GitHub macOS job. Tests check idempotent creation, nested listing, exclusion of game.json/rules.json, content-relative paths, symlink rejection, and file-vs-dir collision. Native macOS build and tests NOT RUN in this connector environment. Do not claim runtime success.

## Important compatibility
- Existing assets in project root or uppercase `Content/` **are not moved or copied**. They remain on disk but are intentionally invisible in the HTML5 Assets view until the user moves/imports them into lower-case `content/`.
- Other code referring to legacy assets outside content is unchanged. This stage does not rewrite asset references in maps/materials/shaders or change build/packaging code.
- If project/content cannot be created (permissions, conflicting file, unsafe symlink), Studio reports an explicit error. No destructive repair is performed.
- No project content files or image thumbnails are generated automatically.

## Next stage
Stage 68: adapt HTML5 Asset Browser to label the root **content**, show its contents directly, reflect optional `root` metadata, and add frontend tests. Then test new and legacy project workflows on Mac. Commit a separate handoff with actual results.
