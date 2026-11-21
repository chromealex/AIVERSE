# AIVERSE Studio — Stage 73: Assets creation/import controls and conflict resolution window

Date: 2026-11-20
Branch: feature/studio-html5
Parent: 5df1e3f496c346dd83710132b353b2a3bfe164a2
Stage commit: see history.

## Implemented
- Added typed frontend methods: `studio.assetByGuid`, `studio.assetFolderCreate`, `studio.assetMovePreview`, `studio.assetImportPreview`, `studio.assetTransferCommit`.
- Added Native AppKit NSOpenPanel for trusted external source paths (files **and** folders, up to 64). Browser receives only preview of content-relative destinations and cannot pass arbitrary local source paths. Long-running pickers use extended UI bridge deadline.
- Asset Browser can create folders; import from system picker; move files and folders internally via HTML5 drag; choose target folder by dropping on folder tree, grid folder or breadcrumb.
- Every transfer first opens a conflict dialog listing filename/GUID conflicts, with explicit per-item Skip / Keep both / Replace file (retains target GUID) / Import with new GUID, when supported by backend. No silent overwrite.
- New folder records from native AssetIndex are rendered, including empty folders; files and folder operations trigger a refreshed GUID catalog. Existing Assets look remains in the first cinematic design system.
- Browser mock rejects native asset mutations rather than fabricating successful imports.

## Changed
apps/studio-web/src/types.ts
apps/studio-web/src/mock.ts
apps/studio-web/src/bridge.ts
apps/studio-web/src/components/AssetsPanel.tsx/.css
apps/studio-native/mac/Main.mm
apps/studio-native/StudioAssetCatalog.cpp (missing std::cctype/std::cstdint includes)
docs/handoff/studio-html5/73-assets-conflict-ui-native-picker.md

## Validation / limits
Code committed; full React typecheck / Vite / macOS ObjC++ and user UI screenshots NOT RUN yet. Needs immediate Mac compiler tests and complete product QA before production use.
External drag from Finder directly (without picker) still needs native NSDraggingDestination integration (Stage 74). The UI's internal drag handler only uses validated content-relative paths; it does not access arbitrary external filesystem paths.
Review C++ transfer engine atomicity, source GUID policy, symlink handling before enabling in a production project.

## Next
Implement Finder drag via native WKWebView subclass; use DOM data-content-drop-target for chosen folder. Update status/preview event bridging, add tests, and verify on Mac. Subsequent stage: native preview thumbnails and import option inspector.
