# AIVERSE Studio — Stage 77: real native conflict contract test + TypeScript DnD fix

Date: 2026-11-20
Branch: feature/studio-html5
Parent SHA: edbcc2f82b17c1deaafb81171bbc49b56f864098
Stage commit: see Git history.

## Implemented
- Added StudioAssetTransferTests.cpp to the macOS native CTest suite and build script. Scenarios: initial content folder creation; preview named conflicts and Skip; Keep both with new filename and GUID; explicit Replace retaining target GUID; internal move with meta/GUID retention; external cloned .meta GUID collision and New GUID; rejected stale preview token; nested external directory import.
- Corrected lingering React.DragEvent type in AssetsPanel to use explicit imported DragEvent.
- Reject an existing symlink/non-directory at the project/.aiverse-cache database path to prevent SQLite redirection through an attacker-controlled symlink. Cache stays regenerable.
- Updated CMake and macOS GitHub Actions to compile/run the new native test.

## Changed
apps/studio-native/tests/StudioAssetTransferTests.cpp
apps/studio-native/StudioAssetCatalog.cpp
apps/studio-native/CMakeLists.txt
apps/studio-web/src/components/AssetsPanel.tsx
tools/studio/verify_mac.sh
.github/workflows/studio-html5-web.yml
docs/handoff/studio-html5/77-asset-conflict-tests.md

## Test results
New native CTest is registered and source assertions authored, but compiler and test execution are **NOT RUN** here because the connected GitHub environment does not provide a macOS compiler/runtime. No claim of passing integration tests.

## Next
Run on Mac `bash tools/studio/verify_mac.sh` and `cd apps/studio-web && npm run typecheck && npm run test:web && npm run build`.
Verify drag/drop (Finder and internal), import dialog, metadata GUID retention and per-platform compression settings. Address any compiler or GUI bugs in new documented stages before merging to main.
