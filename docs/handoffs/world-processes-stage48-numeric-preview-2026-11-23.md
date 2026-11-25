# AIVERSE World Processes Stage48 — numeric-to-Scene preview bridge and individual pivot parity
Date: 2026-11-23
Branch: feature/world-processes-stage31-bidirectional-handles
Main untouched; PR #112 draft.

## Implemented
- `apps/studio-native/StudioHost.h`: editor-only Json scratch `wpNumericPreview_`.
- `apps/studio-native/StudioHost.cpp`: host-owned read-only RPC `studio.worldProcessAreaPreview` accepts typed X/Z percent numerators, pivot mode and optional custom pivot world-centimetre coordinates, or `clear:true`. Valid input triggers `rebuildOverlay`, projecting group candidate rectangles in cyan. This does not touch MapDoc or ECS.
- `apps/studio-web/src/components/EvolveGroupNumeric.tsx`: React effect sends current valid numeric field values to preview RPC while panel is open, clears preview on invalid input, close, selection change or unmount. Apply still uses the existing Stage42 atomic batch resize RPC.
- `apps/studio-native/StudioWorldProcessesEditing.h`: `previewGroupScale2D` now supports individual centers explicitly; `apps/studio-native/StudioHost.cpp` uses it for Shift corner preview and numeric individual-pivot preview.

## Scope
Numeric X/Z scale percentages and custom pivot inputs now feed native Scene preview. Stage44 draggable custom pivot remains editor-only and is **not yet synchronized back to the HTML5 numeric fields**; custom numeric input controls its own pivot. This is an input-to-Scene bridge, not a bidirectional state sync.
Group preview remains a pure MapDoc projection, and commit remains a single validated MapDoc transaction.

## Risks / verification
- React effect can send overlapping asynchronous requests during rapid typing; latest-response ordering and preview cancellation need actual runtime verification.
- Preview validation and commit validation are not yet literally the same helper; potential boundary/rounding discrepancies remain.
- The native Scene preview should clear when the selection changes; current UI effect sends clear on selection changes, but the native scratch does not yet independently track the selection ID set.
- No TS typecheck, Vite build, C++ compile, native interaction, screenshot, Undo/Redo or ECS acceptance was run (user deferred tests). Source-only, not QA-verified.

## Next
1. Synchronize native draggable pivot into numeric fields and enforce last-update-wins for asynchronous preview.
2. Consolidate integer geometry and validation across preview/commit.
3. Check group corner preview and native host request routing on all shells.
4. Compile and run actual acceptance when authorized. Keep PR #112 draft.

## Commits
`ceed88c6` numeric preview state
`d5f12320` native preview RPC and overlay
`509a70e2` web numeric preview effect
`d2d4e306` individual pivot helper
`346ab6ac` native preview parity
