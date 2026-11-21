# AIVERSE Studio HTML5 — Stage 54: repair NUL path guard and validate packaged Vite assets

Date: 2026-11-20
Branch: feature/studio-html5
Parent: dc635a37538c8d46d0733d4b6f811242926af02e
Stage commit: see branch HEAD.

## Implemented
- Fixed a static-analysis issue caught after Stage 53: Objective-C NSString literal `@"\\0"` could behave as an empty substring instead of a real U+0000 sentinel, accidentally rejecting all requests.
- New scheme handler constructs an explicit one-character NSString containing NUL using `stringWithCharacters:length:`, then tests containment. Backslash/path traversal, symlink escape and MIME allowlists remain enforced.
- Removed legacy `file://` navigation trust branch. Production WKWebView now accepts only the bundled aiverse-studio://shell/index.html origin; opt-in localhost Vite dev mode remains unchanged.
- Added log lines for successfully served entry HTML and JavaScript bundles (only one line per asset), simplifying diagnosis if React does not boot.
- Added standalone Python bundle test: parse generated Vite HTML for module JS/CSS URLs, ensure URLs are relative and target files really exist under dist; source checks also assert registered scheme and JS MIME.
- Updated tools/studio/verify_mac.sh to verify all Vite asset paths before CMake configure, so a future missing resource fails clearly rather than producing a white app.

## Changed files
apps/studio-native/mac/StudioAssetSchemeHandler.mm
apps/studio-native/mac/Main.mm
tools/studio/verify_mac.sh
tools/studio/test_web_bundle.py
docs/handoff/studio-html5/54-bundled-origin-hardening.md

## Validation
- Source changes committed in GitHub.
- Full macOS build of the new ObjC++ URL handler and an actual startup screenshot are NOT yet verified.
- Stage 52 previously passed averse_studio target build and all 14 studio CTests before the Stage 53/54 runtime loader changes.
- New Python test is included in existing tools/studio unittest discovery; actual generated dist validation runs when Stage 54 verify_mac.sh is executed after npm build.

## Next
On Mac:
```bash
cd ~/Projects/AIVERSE && git fetch origin && git switch feature/studio-html5 && git pull --ff-only origin feature/studio-html5
bash tools/studio/verify_mac.sh
APP="$(find "$PWD/build/studio-html5-verify" -name AiverseStudioHTML5.app -type d -print -quit)"
test -n "$APP" && open "$APP"
```
If white screen persists, run the binary directly and capture `[studio-web]` diagnostic output. The HTML startup screen should now show a dark status with JS error text if modules fail. Add Stage 55 handoff with exact observed behavior.
