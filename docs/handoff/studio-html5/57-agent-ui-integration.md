# AIVERSE HTML5 Studio — Stage 57: make Studio UI the agent default

Date: 2026-11-20
Branch: feature/studio-html5
Parent: bf6f52be88e7662e27dd8e1caf5f61065831ba43
Stage commit: see branch history.

## Implemented
- Added public `src/ui/index.ts` component exports and imported shared UI stylesheet exactly once in `src/main.tsx`, after tokens and before old styles. Scoped `au-*` selectors avoid changing the Metal viewport.
- Refactored AI panel generator `tools/studio/new_panel.py` to emit a real Studio UI starter screen using StudioPanel, StudioSection, StudioBadge, StudioAlert and StudioEmptyState, with Lucide icon. Screen CSS contains only local geometry, no duplicated colors/buttons.
- Updated `apps/studio-web/AGENTS.md` and README: shared UI v1 is the mandatory new-screen standard, using semantic --au-* tokens and docs/studio-design-system.md.
- Expanded generator regression test to assert the generated panel imports UI components, not legacy PanelTitle, and contains no raw hex CSS.
- Added `tools/studio/test_design_system.py` contract tests for public exports, CSS tokens/classes, global one-time import, GPU-viewport CSS isolation and agent documentation.

## Files
apps/studio-web/src/ui/index.ts
apps/studio-web/src/main.tsx
tools/studio/new_panel.py
tools/studio/test_new_panel.py
tools/studio/test_design_system.py
apps/studio-web/AGENTS.md
apps/studio-web/README.md
docs/handoff/studio-html5/57-agent-ui-integration.md

## Tests / verification
- GitHub source commit created; a full React/Vite macOS build has not run in this connector environment.
- Python unittest checks are registered via the existing `tools/studio/verify_headless.sh` unittest discovery. Must record actual pass result once run on complete worktree.
- The generator's existing 6-case suite is preserved and extended; compile generated TSX with npm typecheck/build before claiming a completed design-system migration.

## Important limitations
- Old panels still use some one-off CSS; migrate incrementally so layout/Metal rendering remains stable.
- Design system is for HTML5 editor windows only, not game runtime rendering.
- Do not merge to main or remove ImGui until complete editor parity and visual smoke pass.

## Next
Add Stage 58 adoption to existing screens and app shell. Inspect hover/focus, narrow dock panes, real Apple WKWebView and GPU composites; document handoff.
