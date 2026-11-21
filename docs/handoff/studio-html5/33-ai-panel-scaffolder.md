# AIVERSE HTML5 Studio — Stage 33: one-command AI panel scaffolder

Date: 2026-11-20
Branch: feature/studio-html5
Parent commit: 778fbbd96f0948b74bca556d48ec3b1e92701b63
Stage commit: see branch history.

## Implemented
- Added tools/studio/new_panel.py with --id / --title / --area / --dry-run and optional --root (test fixtures).
- Generates TSX + CSS under apps/studio-web/src/panels/generated/ and updates the typed nestedDock.ts catalog and StudioPanelRenderer case.
- The code generator pre-validates IDs, duplicate registrations, file collisions, insertion anchors and title size; uses atomic temporary-file replacement with best-effort rollback.
- Generated UI is semantic, CSS-token-driven and has a typed StudioSnapshot prop with clear native bridge guidance; it never writes project files from JS or calls ImGui.
- Documented workflow in apps/studio-web/AGENTS.md and README.md.
- Added standard-library Python unit tests in tools/studio/test_new_panel.py; automatically picked up by existing studio-html5-web.yml boundary test discovery.

## Files changed
tools/studio/new_panel.py
tools/studio/test_new_panel.py
apps/studio-web/AGENTS.md
apps/studio-web/README.md
docs/handoff/studio-html5/33-ai-panel-scaffolder.md

## Checks actually performed
- Python 3.13 unittest discover against isolated temporary fixture: 6 tests PASS.
- Covers one panel generation/metadata, invalid ID rejection, duplicate protection, --dry-run, multiple insertions and escaping quoted titles.
- Full npm Vite typecheck/build, macOS native WKWebView/Metal verification and user-visible window screenshots: NOT RUN here. GitHub CI is queued, not PASS.

## Try it
python3 tools/studio/new_panel.py --id lighting_debug --title "Lighting Debug" --area right --dry-run
python3 tools/studio/new_panel.py --id lighting_debug --title "Lighting Debug" --area right
cd apps/studio-web && npm run typecheck && npm run build && npm run test:dock

## Remaining issues / next stage
- The generator is a source editing tool; AI still implements the actual feature inside the generated panel.
- Add optional declarative schema-to-form renderer for common Inspector-style screens to reduce manual TSX.
- Test generator in a real checked-out AIVERSE tree, then run full frontend and native CI and fix failures before merging.
- The nested dock remains experimental, and Metal scene/full editor parity are incomplete. Do not remove the legacy ImGui editor.
- Next handoff must record actual tests and constraints.
