# AIVERSE HTML5 Studio — Stage 56: shared editor UI design system v1

Date: 2026-11-20
Branch: feature/studio-html5
Parent: e16368ee9ad67e1bd35583c1e1fffb1e97019fe2
Stage commit: see branch history.

## Implemented
- Built a semantic, genre-independent visual token palette in `src/theme/tokens.css`. Existing `--studio-*` aliases preserved for current panels. Added reusable `--au-*` colors, spacing, typography, radii, focus, density and motion.
- Added `src/ui/studio-ui.css`: namespaced shared buttons, inputs, select, text area, tabs, list, card, section, alert, badge, empty/loading states, progress and dock-safe panel layout with no global viewport styling.
- Added type-safe React components in `src/ui/StudioUI.tsx` with controlled state and accessibility (button variants/loading, required icon label, field htmlFor, alerts/progress, tabs and rows).
- Wrote `docs/studio-design-system.md` with usage examples and agent rules. No new npm dependencies or ImGui usage.

## Changed files
apps/studio-web/src/theme/tokens.css
apps/studio-web/src/ui/studio-ui.css
apps/studio-web/src/ui/StudioUI.tsx
docs/studio-design-system.md
docs/handoff/studio-html5/56-ui-design-system.md

## Verification
- Commit written to GitHub. Full npm run typecheck/build and Mac WKWebView screenshot NOT executed in this environment.
- Stage 57 must add a barrel import, a focused contract test, generator integration and screen examples. Do not claim a full visual pass is complete.

## Invariants
- All new panels use shared UI classes/components; no hardcoded palette variants.
- CSS avoids .viewport and DOM/native host, and does not create or modify the Renderer3D Metal layer.
- Existing ImGui editor stays untouched; main branch not changed.
- Future stages must include a separate handoff and actual verification results.
