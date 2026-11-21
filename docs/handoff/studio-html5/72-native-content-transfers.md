# AIVERSE Studio — Stage 72: conflict-first native content transfer API

Date: 2026-11-20
Branch: feature/studio-html5
Parent SHA: 4b77bffd8d8ccde7c62428c0f667ea4c1be440fa
Stage commit: see branch history.

## Implemented
- Added project-local path validator, folder creation and **preview-before-commit** transfer planner in `StudioAssetTransfers.h/.cpp`.
- A transfer creates a unique token and itemized preview with `name`/ `guid` / `invalid-meta` conflict classifications. Sources for external imports can ONLY be supplied to `StudioSession::previewExternalAssets` by the native macOS picker, not JavaScript.
- Native bridge methods: `studio.assetFolderCreate({parent,name})`, `studio.assetMovePreview({paths,destination})`, `studio.assetTransferCommit({token,decisions})`. A caller cannot bypass preview; a new plan invalidates prior plan.
- Decision types: skip, keep-both (rename + new GUID on external copies), replace (existing FILE only; preserve destination GUID/.meta), new-guid (only external file with no name collision), import (no conflict). Internal move pairs file/folder with its .meta and preserves GUID. Folder import copies only real files/folders and generates fresh metadata.
- Rechecks destinations on commit; no implicit overwrite. Reports partial errors and rebuilds GUID SQLite index after successful file operations.
- StudioSession pending transfer reset when switching projects. No UI written yet, therefore import picker, drag, conflict UI and tests are the next bounded stage.

## Files
apps/studio-native/StudioAssetTransfers.h/.cpp
apps/studio-native/StudioSession.h/.cpp
apps/studio-native/CMakeLists.txt
docs/handoff/studio-html5/72-native-content-transfers.md

## Tests
Native compilation and integration tests **NOT RUN** in connector environment. This is a first implementation of the file operation backend and must be compiled/tested on Mac before being considered production safe. Unit tests for collision policy, symlinks and replace rollback are mandatory next.

## Known limitations
- A partial multi-file import is NOT an all-or-nothing transaction. Each item is handled independently; conflicts are checked before commit and errors reported.
- Folder replacement is deliberately refused rather than merging or destructively deleting a directory tree.
- External folder importing reassigns fresh identities for all descendants.
- Replace preserves target GUID; an internal moved source loses its original path/reference. Users must explicitly approve this policy.
- Deleting SQLite cache rebuilds GUID lookup on the next studio.assets refresh. Meta sidecars are source of truth.
- No external paths accepted from JS and no automatic flattening of source directory trees.

## Next
Stage 73 must add native AppKit file selection and HTML5 conflict resolution dialog; then internal/external drag and drop, manual Mac tests and full source CMake compile. Add a distinct handoff after every stage.
