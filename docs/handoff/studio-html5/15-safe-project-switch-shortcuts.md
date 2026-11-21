# AIVERSE Studio HTML5 — Stage 15: safe project switching and global shortcuts

Date: 2026-11-20
Branch: feature/studio-html5
Parent: 7c7effcc21b3d5edd76e4496289226b04dcf3044
Stage commit: see current branch head.

## Done
- Native project picker now checks MapDoc, rules and manifest dirty flags. If any are dirty, macOS NSAlert sheet asks Discard and Open / Cancel before invoking the project picker.
- Cancelling either sheet returns {cancelled:true,snapshot:null}, so the JS request can complete without timeout and the original Workspace remains open.
- TypeScript shortcuts: Ctrl/Cmd+S Save, Ctrl/Cmd+Z Undo, Ctrl/Cmd+Shift+Z or Ctrl+Y Redo. Do not hijack native text field undo/redo.
- Shortcut commands respect backend availability, canUndo/canRedo and busy state. No direct disk/engine access from JS.

## Changed files
apps/studio-native/mac/Main.mm
apps/studio-web/src/App.tsx
docs/handoff/studio-html5/15-safe-project-switch-shortcuts.md

## Tests
Changes committed through GitHub API. npm typecheck, macOS app build, CTest, interactive NSAlert/NSOpenPanel behavior NOT RUN.
GitHub Actions was queued when last inspected. No PASS claimed.

## Remaining
- Save As for untitled maps; no full projects/new game wizard.
- Native image and texture tests, toolchain build, keyboard shortcut interaction testing.
- Native viewport still primitive MapDoc preview, not full renderer parity. ImGui editor remains the fallback.
- Follow up: verify build/test on Mac, then handle the rest of the editor windows and cross-platform WebViews.
