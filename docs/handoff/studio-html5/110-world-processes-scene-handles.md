# Studio HTML5 — Stage 110: interactive World Processes Scene handles

Date: 2026-11-23. Branch `feature/world-processes-experimental`, draft PR #112.
Extends earlier Studio World Processes Stage106 Live Inspector,
Stage107 authoring, Stage108 object picker, Stage109 linked deletion.

## Implemented source

The existing native Scene viewport, shared by macOS/Windows Studio, now
supports selection of **already-authored** World Processes handle points
on the selected stable map entity. Link endpoint sockets may be dragged
to a compatible map object; on release they call the existing strict
`studio.setProperty` RPC. The pointer move/preview never mutates
MapDoc. A successful release yields one native MapDoc Undo step and the
normal document/selection events; a rejected target leaves data intact.

EVOLVE visual radius and GRANULAR grid dimensions may also be dragged.
The new narrow native RPC `studio.worldProcessAreaSet` enforces
stable map IDs, type/range checks and one Undo step:
- EVOLVE radius (0.5..500 world metres) edits
  `DocEntity.extra.worldProcessGizmo.radiusMetres` **only**;
  it does NOT modify the actual native `EvolvePlantRegion` bounds.
- GRANULAR field width/height (1..128) are actual authored grid
  dimensions. The existing `GranularDocument::resize` preserves
  overlapping mass/obstacles and produces the canonical full-grid JSON
  schema, including when starting from a legacy simple 1×1 config.

Hit testing uses the Scene's actual camera projection; changes in
radius use world-space ground intersections. Active endpoint/area
handles and in-drag preview lines are drawn using
`StudioScene::OverlayShape`, not a second HTML canvas or ImGui.
Press Escape to cancel without a document change.

## Modified files

- `apps/studio-native/StudioWorldProcessesEditing.h`: typed
  component reference and area handles, screen hit testing, compatible
  target search, size calculations.
- `apps/studio-native/StudioSession.cpp`: area RPC and Undo labels;
  existing stable link field mutation remains authoritative.
- `apps/studio-native/StudioHost.h/.cpp`: transient drag state and
  normal native input handling for press/move/release/Escape; preview
  inside existing Scene overlay renderer.
- `apps/studio-native/StudioWorldProcessesGizmos.h`: world-space
  EVOLVE ring rendering matches the draggable radius.
- Agent handoff:
  `docs/handoffs/world-processes-stage28-interactive-studio-gizmos-2026-11-23.md`
  (exact SHA ledger, architecture, tests and outstanding limitations).

## Validation

These are committed source changes, **not native QA evidence**.
The user explicitly postponed tests. No actual macOS/Windows CMake
compile, Scene mouse drag, Undo/Redo runtime validation, save/reopen,
Meta/Box3D, browser build or screenshot was run. Never call Stage110
production-ready before executing those gates.

## Next work

Coordinate with `feature/studio-html5` owner; do not overwrite their
Scene handlers or style kit. Implement overlap cycling/labels and
native EVOLVE actual region-bounds authoring. Later run real Mac and
Windows Studio Scene captures, drag target validation, case-sensitive
map references, Escape, Undo/Redo, save/reopen, then full merged-tree
Mac quick suite and current CI; only then merge PR #112.
