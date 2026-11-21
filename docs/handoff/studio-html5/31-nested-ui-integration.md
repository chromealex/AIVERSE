# AIVERSE HTML5 Studio — Stage 31: nested dock UI wired to editor

Date: 2026-11-20
Branch: feature/studio-html5
Parent: 0e310a15d128041fecc110a6eab1f658a3a7374a
Stage commit: see branch history.

## Implemented
- Added recursive NestedDockWorkspace rendering the split-tree model from Stage 30.
- Drag tabs onto a stack to join/reorder, or onto its left/right/top/bottom edge to create a nested split.
- Keyboard-accessible per-split resize handles (arrows, shift-arrows, Home/End), mouse split resizing, tab close/reopen and Panels menu.
- Replaced the four-zone DockWorkspace in App.tsx with the nested system while preserving the existing renderPanel callback for Scene, Hierarchy, Inspector, Assets, Console and the explicitly-unported tabs.
- Layout now uses v3 persistence, automatically migrating previous v2 layout preferences.
- Browser Scene viewport is mounted within its actual dock stack; its existing ResizeObserver sends native C++ surface rectangles when moved/hidden.
- CSS has an opt-in rule for native-composite mode, keeping the Scene stack transparent behind the WKWebView when it sits above Metal.

## Files
apps/studio-web/src/components/NestedDockWorkspace.tsx
apps/studio-web/src/components/NestedDockWorkspace.css
apps/studio-web/src/App.tsx
docs/handoff/studio-html5/31-nested-ui-integration.md

## Checks
- Stage 30 model tests (11/11) and strict TypeScript check of the pure model passed locally.
- Parsed/checked the new recursive JSX source locally using TypeScript 5.8 with minimal React type stubs; this is NOT an actual React/Vite build.
- Full npm typecheck/build and macOS native/visual checks NOT EXECUTED in this environment; inspect GitHub CI before claiming success.

## Limitations
- No floating OS windows, editor window detachment, persisted multi-monitor layouts or real Game/Materials/Animation panels.
- Native 3D compositor and input must be verified in WKWebView Safari on macOS.
- DOM tab drag may behave differently in native Safari; an accessible select fallback is provided.
- Deeply nested splits can make very small panes; UI has bounded ratios and node depth, not complex min-size constraints.
- Old four-area DockWorkspace remains in sources as a fallback but is no longer used by App.
- Existing ImGui editor remains the production fallback.

## Next
Run the web CI suite, fix any TypeScript/React errors; execute tools/studio/verify_mac.sh on macOS. Test nested Scene moves with Metal resize and inspector selection. Follow with AI-extensible panel registry and real editor window migrations. Document every stage.
