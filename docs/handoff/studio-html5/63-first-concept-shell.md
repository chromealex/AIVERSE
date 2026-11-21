# AIVERSE HTML5 Studio — Stage 63: approved first-concept skin and new default layout

Date: 2026-11-20
Branch: feature/studio-html5
Parent SHA: ce8634483b6500eaea045581ba5dda8f00d6d1f7
Stage commit: see HEAD.

## Implemented
- Added CSS skin `apps/studio-web/src/ui/first-concept.css` using the *first* user-approved cinematic dark-navy concept. This is a real HTML5 style layer, not a generated scene image.
- Palette/semantics extended in `src/theme/tokens.css` with secondary periwinkle, blue active glow, toolbar/header gradients and elevated popover shadow. The existing `--studio-*` and `--au-*` tokens remain compatible.
- Styled top title bar, main toolbar, hierarchy selection, nested tabs, controls, Inspector, Lighting, Materials, Shader Graph and status bar with consistent deeper navy, raised surfaces, subtle blue/indigo accents and restrained focus.
- Updated default DockTree v3: Hierarchy left, Scene center, Inspector/Lighting right, **Assets and Console bottom** (Assets active). This matches the first concept's asset browser placement.
- Added assertions that the default dock has the right panel placement. Existing v3 localStorage preferences are never silently overwritten; Reset layout applies the new default.
- No changes to native C++ bridge, Metal renderer or the GPU viewport DOM shape/rect.

## Files
apps/studio-web/src/theme/tokens.css
apps/studio-web/src/ui/first-concept.css
apps/studio-web/src/main.tsx
apps/studio-web/src/nestedDock.ts
apps/studio-web/tests/nested-dock.mjs
docs/studio-visual-direction.md
docs/handoff/studio-html5/63-first-concept-shell.md

## Actual verification
Source changes committed. **Full npm typecheck/build, test:dock, and visual screenshots NOT RUN** in this tool environment; GitHub Actions status must be inspected. Current repository files and accepted design docs inspected.
Scene image in the first concept is illustrative; real Renderer3D scene from loaded project remains authoritative.

## Next
Fix the reported empty dock after drag and remove always-visible debug location dropdowns in Stage 64-65, with model tests. Verify on Mac after checkout. Do not alter `.viewport` selectors; maintain handoffs.
