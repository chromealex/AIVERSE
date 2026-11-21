# AIVERSE Studio HTML5 — Stage 30: nested docking model

Date: 2026-11-20
Branch: feature/studio-html5
Parent commit: f386bc91ce4601be6e917aadd00d7375d0edf9f3
Stage commit: see branch history

## Implemented
- Added a pure, genre-neutral TypeScript nested split-tree model (DockTree v3) separate from rendering and React.
- Supports unlimited-within-bounds nested horizontal/vertical dock splits, tab activation/reordering/move/close/reopen, bounded ratios and persisted layout.
- Validates storage: finite ratios, allowlisted panel IDs, duplicate tab elimination, node cap 48, depth cap 8, non-mutating edits.
- Migrates from the existing four-area v2 layout to the v3 split tree without storing project/ECS data.
- Added Node.js 22 tests in tests/nested-dock.mjs and included both suites in npm run test:dock.

## Files
apps/studio-web/src/nestedDock.ts
apps/studio-web/tests/nested-dock.mjs
apps/studio-web/package.json
docs/handoff/studio-html5/30-nested-dock-model.md

## Checks performed
- Locally authored v3 model was verified with Node 22.16 --experimental-strip-types: 11/11 model tests passed.
- tsc 5.8.3 --noEmit --strict --target ES2022 --module ESNext --moduleResolution Bundler --lib ES2022,DOM on the model: PASS.
- The existing React app's TypeScript/Vite build and macOS native target: NOT RUN. GitHub Actions jobs previously remained queued; no claimed macOS validation.

## Limitations
- The React UI still uses the v2 four-area DockWorkspace. Stage 31 must switch to a nested view component and migrate its state.
- The current model preserves empty stacks as drop targets instead of collapsing them.
- Panel identity is allowlisted; new AI-authored panel IDs still need a catalog entry and rendering component.
- No floating OS windows, detached monitor windows, drag-to-os-window or actual full ImGui feature parity.

## Next agent step
Build a recursive React renderer for DockTree v3 using existing Studio panel render callback. Implement splitters + edge drops and responsive constraints; move App from v2 to v3 with migration. Test both docking suites and React build, verify native 3D rectangle updates after moving Scene in WKWebView on Mac. Add the next handoff before merging anything to main.
