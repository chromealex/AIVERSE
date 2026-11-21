# AIVERSE HTML5 Studio — Stage 58: visual polish of existing editor shell

Date: 2026-11-20
Branch: feature/studio-html5
Parent commit: 101f63497124abaf9b5e4b36124a6ee0b29d4504
Stage commit: see Git history.

## Implemented
- Added `src/ui/editor-shell.css` to align existing titlebar, toolbar, nested docking tabs, panel headings, Hierarchy, Inspector, Lighting, Materials, Shader Graph and footer with Studio UI v1 semantic colors and rhythm.
- Loaded this presentation adapter after the previous styles; it keeps the old HTML5 DOM structure and bridge unchanged.
- Existing PropertyField reuses shared `au-input`, `au-select`, `au-checkbox__input`, `au-field__hint/error` while retaining its validation and Undo behavior.
- Existing PanelTitle takes shared `au-toolbar` styling.
- Agent rules and design-system contracts updated for the shell adapter.

## Files
apps/studio-web/src/ui/editor-shell.css
apps/studio-web/src/main.tsx
apps/studio-web/src/components/PanelTitle.tsx
apps/studio-web/src/components/PropertyField.tsx
apps/studio-web/AGENTS.md
tools/studio/test_design_system.py
docs/handoff/studio-html5/58-shell-style-adapter.md

## Verification
Changes committed to GitHub. Full npm typecheck/build and native WKWebView screenshot not executed here. Shared UI style tests are included in Python discovery, to run on a complete worktree.
Metal viewport render/geometry, native bridge and selection unchanged by code. Full visual parity with ImGui is not claimed.

## Next
Run npm typecheck/build and Studio tests on Mac. Review appearance at 1440x900 and narrow dock panes; iteratively migrate remaining panels to actual Studio UI components, and record handoff for each stage. Do not merge main or remove ImGui until parity.
