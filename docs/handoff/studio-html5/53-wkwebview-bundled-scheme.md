# AIVERSE Studio HTML5 — Stage 53: fix blank WKWebView by serving ES modules from a bundled scheme

Date: 2026-11-20
Branch: feature/studio-html5
Parent: de4987221295060956fc5f0665a721d74b0cf40c
Commit: see branch history.

## Observed bug
Developer's Mac Studio has completed the native build and all 14 Studio CTests (Stage 52), but launching AiverseStudioHTML5.app shows an entirely white WKWebView. No header or React UI appears.

## Root cause hypothesis (grounded in source)
The native app loaded Vite's `dist/index.html` using `WKWebView.loadFileURL`. The compiled Vite app is an external `type="module"` JavaScript bundle. WebKit can block ES-module imports from opaque file:// origins, leaving the React #root empty even when HTML document navigation succeeds. Vite's already-correct `base: './'` option fixes only URL generation, not the file-origin module restriction.

## Implemented
- Added a dedicated, bundled, READ-ONLY `StudioAssetSchemeHandler` which serves `aiverse-studio://shell/index.html` and its relative JS/CSS/images/fonts. No local HTTP daemon, port or external network is opened.
- Handler enforces exact scheme+host, a resolved/symlink-safe path within the studio-web resource folder, path traversal rejection, file and MIME allowlists and max 32 MiB per asset.
- Main.mm registers the WKURLSchemeHandler before constructing WKWebView and loads the generated shell via `loadRequest`, preserving the separate opt-in Vite dev server `--dev`.
- Main navigation/message trust policy accepts only the exact bundled index or the existing loopback Vite dev origin. The handler does NOT expose arbitrary file:// paths or workspace files.
- Added WebKit navigation failure/termination logs and read-only `studio.webDiagnostic` messages in the native host.
- Added dark startup status and error/promise listeners to index.html, preventing a silent all-white UI if JavaScript fails. The startup status disappears when React renders.
- Vite `base: './'` remains unchanged.

## Files
apps/studio-native/mac/StudioAssetSchemeHandler.h
apps/studio-native/mac/StudioAssetSchemeHandler.mm
apps/studio-native/mac/Main.mm
apps/studio-native/CMakeLists.txt
apps/studio-web/index.html
docs/handoff/studio-html5/53-wkwebview-bundled-scheme.md

## Tests actually performed
- Source inspected and committed. A prior developer run of full CMake/AppleClang and 14/14 native CTests passed BEFORE this change.
- The new ObjC++ WKURLSchemeHandler needs recompilation on macOS: NOT yet compiled or visually verified. No PASS claim for startup.
- Previous Vite bundle had correct relative asset base. This fix addresses runtime scheme/module execution rather than changing base URLs.

## Next steps
Run:
```bash
cd ~/Projects/AIVERSE
git fetch origin
git switch feature/studio-html5
git pull --ff-only origin feature/studio-html5
bash tools/studio/verify_mac.sh
APP="$(find "$PWD/build/studio-html5-verify" -name AiverseStudioHTML5.app -type d -print -quit)"
test -n "$APP" || exit 1
open "$APP"
```
If still blank, run the Mach-O in Contents/MacOS from Terminal and capture [studio-web] log lines. Observe startup diagnostic in dark view. Inspect whether scheme module imports load; record screenshots and new handoff. Do not merge/remove ImGui until scene and UI work.
