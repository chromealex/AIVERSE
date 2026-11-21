# AIVERSE HTML5 Studio — Stage 29: keyboard-accessible dock splitters

Date: 2026-11-20
Branch: feature/studio-html5
Parent: 662c2cc1d7f451bfc3393d5027ca4a5ff47e86d0
Stage commit: see branch HEAD.

## Implemented
- Corrected React-vs-DOM pointer event typing by using explicit ReactPointerEvent and DOM PointerEvent for window listeners.
- Dock resizer handles are keyboard-focusable, expose appropriate separator orientation and ARIA values.
- Arrow keys resize 8px, Shift+Arrow 32px, Home/End go to minimum/maximum. All values remain clamped.
- Hardened drag-leave event handling when relatedTarget is not a DOM Node.
- Added focus-visible styling to splitters and dock tab controls.

## Changed files
apps/studio-web/src/components/DockWorkspace.tsx
apps/studio-web/src/components/DockWorkspace.css
docs/handoff/studio-html5/29-accessible-dock-controls.md

## Validation
Source reviewed and committed. React TypeScript/Vite build, real Safari/WKWebView interaction and native C++ build NOT RUN here. CI is queued. No passing test result claimed.

## Limitations
- Keyboard focus, window navigation and VoiceOver semantics still require manual accessibility testing.
- Native Metal surface interaction and transparent compositing require a Mac visual run.
- Fixed four-zone dock does not yet support nested split trees or floating windows.

## Next
Check GitHub Actions output and run \`npm run test:dock && npm run typecheck && npm run build\`. Test dragging, resizing and rebinding native viewport manually. Continue to nested docking and actual editor screen parity after baseline tests.
