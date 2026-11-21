# AIVERSE Studio — Stage 74: Finder external file/folder drop into Assets

Date: 2026-11-20
Branch: feature/studio-html5
Parent: 0dee8da00298a3e1af9c6c1dedff76b353996160
Stage commit: see history.

## Implemented
- Added StudioFileDropWebView subclass implementing NSDraggingDestination for Finder's file URL pasteboard. Both normal and transparent-composite WKWebView hosts inherit the handler.
- Native receiver accepts a drop only when the current DOM point lies within an HTML5 Assets element `data-content-drop-root`. Destination comes from the hovered folder button/tile/breadcrumb `data-content-drop-target`, or the currently open content folder.
- External file URLs are supplied solely by NSDraggingDestination, NEVER accepted from untrusted JS parameters. The file/folder drop triggers StudioSession.previewExternalAssets; it does not directly copy anything.
- The preview appears in the same HTML5 conflict dialog as normal imports. Native errors are surfaced as `assets.importError`; native plans as `assets.importPreview`. Existing internal moves stay pure HTML drag with content-relative paths.
- Guarded max 64 items, file URL-only and native project-open requirements; no external network paths or browser file bytes.
- Fixed frontend TypeScript imports to use explicit DragEvent, removed unused component imports; added catalog stdint/climits includes.

## Files
apps/studio-native/mac/Main.mm
apps/studio-web/src/components/AssetsPanel.tsx
apps/studio-native/StudioAssetCatalog.cpp
docs/handoff/studio-html5/74-native-finder-drag-to-assets.md

## Verification
Implementation committed. macOS WKWebView DragDestination compile and live Finder drag NOT RUN; must be verified manually. This API depends on WebKit's delegation of fileURL drags to the WKWebView subclass and behavior may require a dedicated NSView overlay in a subsequent stage.
- No full npm build, no full Studio CTests executed in this connector session.

## Next
Run Native C++/ObjC++ compile and tests. Drag a real file and folder from Finder onto Assets root and nested folder; check conflict dialog, then verify keepBoth/skip/replace and GUID retention. Inspect temporary files on interruptions. Fix compiler/runtime issues with separate handoffs, preserve branch/main boundaries.
