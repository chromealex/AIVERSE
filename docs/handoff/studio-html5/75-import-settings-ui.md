# AIVERSE Studio — Stage 75: per-asset .meta importer settings UI

Date: 2026-11-20
Branch: feature/studio-html5
Parent SHA: b828a641f165f7c7f12f2c6d15f954cedd823280
Stage commit: see history.

## Implemented
- Added `StudioAssetCatalog::patchSettings(guid,key,value)`: whitelisted JSON import settings with strong type/range checks, GUID and importer identity validation, symlink checks, temp-write/rename, followed by SQLite index reconciliation.
- C++ native bridge `studio.assetMetaPatch` exposes only (GUID, allowed key, primitive value); never browser-provided filesystem paths.
- Texture settings: compression (auto/none/fast/high/lossless), mipmaps, sRGB, maxSize power-of-two, and format hints for Metal/Vulkan/GLES. Model settings: meshCompression and generateLODs. Audio settings: compression and quality.
- Added `AssetImportSettings.tsx` to inspect selected real asset metadata with read/update by GUID through native typed bridge. UI clearly says these are metadata hints, NOT proof of an actual GPU compression/transcode pipeline.
- Updated tests to check GUID retention across setting changes, invalid maxSize rejection and per-platform override persistence.

## Files
apps/studio-native/StudioAssetCatalog.h/.cpp
apps/studio-native/StudioSession.cpp
apps/studio-native/tests/StudioAssetCatalogTests.cpp
apps/studio-web/src/types.ts
apps/studio-web/src/mock.ts
apps/studio-web/src/components/AssetImportSettings.tsx
apps/studio-web/src/components/AssetsPanel.tsx/.css
docs/handoff/studio-html5/75-import-settings-ui.md

## Validation
Code committed. Full Mac compilation, CTest and Vite typecheck/build NOT RUN in this tool environment. Must verify UI, per-file metadata saving, GUID stability and rollback failures before production use.

## Boundaries
- .meta remains authoritative; .aiverse-cache/asset-index.sqlite3 is a rebuildable index, not the source of truth.
- These settings only configure future import/build conversion. Texture GPU compression for each platform and model thumbnail generation remain separate engine tasks.
- Not a Unity YAML .meta clone, but JSON schema v1 native to AIVERSE. Do not silently overwrite malformed .meta or duplicated GUIDs.
- No change to old ImGui editor or main.

## Next
Run full Mac CMake/CTests, verify drag-import/conflict/metadata and profile. Fix any compile issues with separate handoff. Implement actual thumbnail processing and add conflict resolution for duplicated GUIDs discovered during background rescan.
