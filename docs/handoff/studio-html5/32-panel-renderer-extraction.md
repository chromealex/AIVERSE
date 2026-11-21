# AIVERSE HTML5 Studio — Stage 32: AI-friendly panel renderer separation

Date: 2026-11-20
Branch: feature/studio-html5
Parent commit: ddcd74c9d11c7ad9c4363e86986e11e1a194fce6
Stage commit: see branch history.

## Implemented
- Extracted the complete per-panel render switch (Hierarchy, Assets, Scene, Inspector, Console and unported placeholders) from App.tsx into src/panels/StudioPanelRenderer.tsx.
- Main App.tsx now owns only application state and actions; passes an explicit typed StudioPanelContext to a reusable panel renderer.
- Moved panel titles and preferred dock stack metadata out of NestedDockWorkspace.tsx into the pure nestedDock.ts model (STUDIO_PANEL_TITLES / STUDIO_PANEL_HOME).
- New AI-authored panel changes are localized to a panel component, a registry render case and a catalog ID + metadata; native Workspace operations still require a separate allowlisted C++ bridge method.
- Added metadata completeness test to nested docking tests. Existing panel functions unchanged.

## Files
apps/studio-web/src/App.tsx
apps/studio-web/src/nestedDock.ts
apps/studio-web/src/components/NestedDockWorkspace.tsx
apps/studio-web/src/panels/StudioPanelRenderer.tsx
apps/studio-web/tests/nested-dock.mjs
docs/handoff/studio-html5/32-panel-renderer-extraction.md

## Checks
GitHub source committed; frontend npm/Vite/typecheck and macOS GPU build are NOT RUN in this environment.
The Stage 30 pure dock model passed 11/11 Node tests locally before this metadata extraction; rerun tests after this change (new 12th test).
No visual confirmation of React integration or native Scene compositor.

## Limits
- The panel catalog is type-safe but still requires explicit IDs (not a filesystem-based automatic import).
- Creating a new panel requires a metadata entry and a render case, though App.tsx no longer needs modification.
- Future Stage 33 can provide a generator to automate these edits.
- React/Vite CI is still queued; no PASS claimed. Full legacy editor features are not migrated.

## Next
Provide a safe AI panel generator for screen scaffolding with tests, then run browser build, macOS compile/visual, and complete the rest of editor windows with handoffs. Keep ImGui until parity.
