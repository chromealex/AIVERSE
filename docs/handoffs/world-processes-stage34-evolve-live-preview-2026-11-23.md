# AIVERSE World Processes Stage34 — EVOLVE live rectangle preview
Date: 2026-11-23
Branch: feature/world-processes-stage31-bidirectional-handles
Main unchanged; draft PR #112 not merged.

## Implemented
- `apps/studio-native/StudioWorldProcessesEditing.h`: pure `previewArea(owner,mode,valueCm,symmetric,result)` derives center and half-extents for a proposed edit. Uses typed authored offsets and opposite-fixed-edge calculation, even-centimetre span rounding, and world budget checks. Does not write MapDoc or ECS.
- `apps/studio-native/StudioHost.cpp`: during a valid EVOLVE drag, overlays the prospective rectangle in green using existing native `worldprocess_gizmos::rectangle` shapes, while preserving the existing point/line/field label. The overlay is transient and rebuilt on mouse movement; the existing single-RPC mouse-up commit is unchanged.
- No new renderer, ECS schema, or simulation state.

## Notes
- The helper includes a `symmetric` argument to support a future resize-mode UI, but the host currently passes false: fixed-opposite-edge resize. **No user-facing symmetric/asymmetric mode toggle implemented yet.**
- Center handles use the same preview helper. Existing maps without offset fields use zeros.
- Preview is intentionally not saved and is removed on release/cancel.

## Actual verification
GitHub accepted source commits. No C++ compilation, runtime Studio drag, screenshots, Undo/Redo, save/reopen or gameplay tests were executed (user defers tests). This stage is source-only.

## Remaining
1. Implement explicit symmetric vs opposite-fixed resize mode in Scene input, persisted only as editor preference (not ECS).
2. Share exact integer arithmetic between preview and session commit to avoid divergence at coordinate limits.
3. Verify compile, platform rendering and real interactive drag/ESC/Undo/Redo when tests are authorized.
4. Continue dedicated skill and handoff updates after each stage; do not merge draft PR #112 before QA.

## Commits
`aed629c552432e056d9cd891b9d828ed812c4221` preview geometry
`5f4f413a0df34caef196ab5e5a074fade72bdb4f` overlay integration
