# AIVERSE HTML5 Studio — Stage 25: persistent, bounded shell layout

Date: 2026-11-20
Branch: feature/studio-html5
Parent: ac3f99f0669540240bc248f53789222a9118405b
Stage commit: see branch history.

## Implemented
- Added apps/studio-web/src/layout.ts: versioned HTML5-only panel layout stored in browser localStorage, including selected scene tab, Assets/Hierarchy tab, console toggle and left/right widths.
- Values are parsed from unknown JSON and validated; widths clamped to 200..520, invalid tab names/bools ignored, private/restricted browser storage failures fall back to defaults.
- Main App now restores persisted layout and saves subsequent UI changes. The settings gear now resets the panel configuration instead of being an inert disabled control.
- Fixed Asset Browser's icon import from unrecognized FileBox2 to documented lucide-react FileBox.

## Changed files
apps/studio-web/src/layout.ts
apps/studio-web/src/App.tsx
apps/studio-web/src/components/AssetsPanel.tsx
docs/handoff/studio-html5/25-layout-persistence.md

## Validation
GitHub branch updated. TypeScript npm build, WKWebView visual/interaction tests, and macOS CMake/CTest NOT EXECUTED here. Current CI remains queued; no PASS claimed.

## Notes / limitations
- This persists the *existing resizable sidebars*, not arbitrary nested docking/tab detachment. Full dock manager is still to be implemented.
- No project data is stored in localStorage; only UI preferences.
- Default layouts for differing viewport sizes and multiple monitors are pending.
- Legacy ImGui editor remains production fallback.

## Next
Run the macOS CI build and fix any TypeScript issues. Integrate configurable nested HTML docking and drag-to-tab with native GPU viewport rectangle updates; then port EditorWindow classes and remove ImGui only after feature parity.
