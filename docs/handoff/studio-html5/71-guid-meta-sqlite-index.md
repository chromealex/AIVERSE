# AIVERSE Studio HTML5 — Stage 71: GUID sidecars and SQLite AssetIndex

Date: 2026-11-20
Branch: feature/studio-html5
Parent SHA: 4912f702193b4277da68e1bb3da4e540e6316785
Stage commit: see git log.

## User decision / architecture
- AIVERSE adopts **Unity-like sidecar .meta** files for every file and subfolder under project/content. These contain immutable GUIDs and import settings, including compression/mipmaps/sRGB/maxSize and per-backend metal/vulkan/gles auto overrides.
- The **SQLite AssetIndex** stored under project/.aiverse-cache/asset-index.sqlite3 is rebuildable and never authoritative; .meta belongs in version control, cache/DB should be ignored.
- GUID is a random UUIDv4 represented as 32 lowercase hex chars; preserve GUID on source/meta move, never silently regenerate a duplicated GUID.
- A future build/import pipeline should convert source assets into platform-specific cached data using the settings. Stage 71 does NOT actually compress GPU assets.

## Implemented files
apps/studio-native/StudioAssetCatalog.h/.cpp — .meta default/read/create/validate, GUID v4, SQLite schema, atomic full-index rebuild, path/GUID lookups, folder records
apps/studio-native/StudioSession.cpp — studio.assets returns files+folders, guid, type; studio.assetByGuid resolves through SQLite
apps/studio-native/tests/StudioAssetCatalogTests.cpp — sidecar, SQLite, move with stable GUID, duplicate GUID conflict, invalid metadata
apps/studio-native/CMakeLists.txt — find_package(SQLite3 REQUIRED), links Studio and test targets
tools/studio/verify_mac.sh and .github/workflows/studio-html5-web.yml — new native CTest
docs/handoff/studio-html5/71-guid-meta-sqlite-index.md

## Validation
Source/CI wired and committed, but full CMake/CTest on Mac and Vite integration **NOT RUN** in this connector-only environment. Do not report success until native compiler and test logs confirm it.

## Technical boundaries
- `studio.assets` rebuilds the index from on-disk source and .meta. The DB is queried with prepared indexed SQL, not a linear file search, for `studio.assetByGuid`.
- Any existing .meta with invalid GUID/importer/settings is preserved and causes a visible error. Duplicate GUID conflicts block rebuild rather than overwrite an identity.
- Real asset files remain under content; compiled GPU formats are NOT stored in .meta, and texture/model thumbnails are NOT generated.
- Existing project and ImGui editor remain; only HTML5 Studio's asset service changes.
- If SQLite headers are unavailable on a platform, CMake will explicitly fail rather than silently substituting an in-memory map.

## Next
Stage 72: safe native file import and folder/move operations with **preview of conflicts** and explicit user decisions. Attach every newly imported file or folder to a .meta, then rebuild SQLite. Stage 73: HTML5 Assets conflict dialog + internal/external drag-and-drop via trusted native file URLs. After each stage, handoff and macOS checks.
