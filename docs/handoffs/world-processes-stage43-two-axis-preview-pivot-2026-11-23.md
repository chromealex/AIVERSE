# AIVERSE World Processes Stage43 — two-axis preview geometry and pivot overlay
Date: 2026-11-23
Branch: feature/world-processes-stage31-bidirectional-handles
Main unchanged. PR #112 remains draft.

## Source changes
- `apps/studio-native/StudioWorldProcessesEditing.h`: new pure `previewGroupScale2D` accepts stable map IDs, rational X/Z scale numerators and denominators, and explicit world-centimetre X/Z pivot. Calculates candidate rectangles, rejects invalid dimensions and world bounds; does not mutate MapDoc/ECS.
- `apps/studio-native/StudioHost.cpp`: adds an overlay showing a numeric X/Z pivot label, pivot marker, and 2D preview geometry during selected group edge dragging. The current overlay example uses the active entity position as a pivot and a one-axis scale derived from the drag; it demonstrates the shared 2D preview API without changing the Stage40/42 commit behavior.
- Existing `studio.worldProcessAreaBatchResize` Stage42 RPC is still the actual atomic authoring endpoint for explicit custom pivots and independent X/Z ratios.

## Scope / outstanding
- **No editable numeric Scene input has been added.** Existing numbers are read-only overlay labels and exact rational values remain available through RPC.
- **No draggable custom-pivot handle yet.** The overlay marker is visual only.
- The overlay does not yet represent an arbitrary user-entered two-axis scale, since the Scene input currently drives one axis. Full UI wiring remains next.
- Stage41 amber group preview and Stage43 blue preview may coexist; consolidate them in the next pass.
- Consider checking duplicate selected IDs and disabled components consistently between preview and RPC.

## Verification
GitHub accepted source commits. No native compile, CTest, Studio runtime, screenshot, Undo/Redo, save/reopen or ECS/TerraWeave QA was run, per user test deferral. Do not claim functional verification.

## Next
1. Add Studio numeric group scale X/Z and pivot X/Z input controls, calling Stage42 RPC.
2. Add draggable pivot handle and unify all group preview overlays.
3. Centralize exact integer arithmetic shared by RPC and preview.
4. Build and runtime-verify only when authorized; PR #112 stays draft.

## Commits
`b9d837b8a7823452c561d132f2234853db447854`
`c54fed8aec03b31301d3c77d20c730d8a5d6a53a`
