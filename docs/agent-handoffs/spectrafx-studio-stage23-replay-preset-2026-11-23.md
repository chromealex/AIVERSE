# SpectraFX Studio Stage 23 — replay preset authoring (2026-11-23)

## Provenance
Branch: feature/spectrafx-html5-data-interfaces-20261123. Implementation commit: ebdb3b622ea86265c23674e72617aeb57dc91e0f. Not merged to main.

## Implemented
Changed apps/studio-web/src/panels/SpectraNativePreview.tsx.
Save replay preset stores a bounded in-memory snapshot of the native preview's last polled camera, simulation tick, playback speed/loop, stage background/grid/axes, project path and graph JSON signature. Restore replay preset rejects changed project/graph and serially invokes existing native pause, stage, playback, cameraRestore, seek operations, stopping on reported errors. UI shows the stored tick and playback speed. Preset clears on project switch. This reuses the Stage 22 camera restore and existing Stage 15 playback controls; no new GPU capture, no ECS writes and no filesystem persistence.

## Tests executed
NONE. No TS/Vite build, native C++/Metal tests, manual Studio QA or cross-platform check on this exact commit. Source implementation only.

## Important limitations
- Not deterministic simulation: no explicit random seed, particle state snapshot, post-process state, native GPU capture fence or exact frame provenance. Replay only restores requested authoring controls and tick.
- Graph signature uses JSON.stringify and matches only the current in-memory graph; edits are rejected rather than silently replayed against a different graph.
- No persisted replay preset, import/export, scalar parameter snapshot or native atomic transaction. The user may need to reapply live scalar values.
- UI status may lag the render-owner state; capture A/B may not match exactly. Pause/seek semantics and cameraRestore require hardware QA.
- Stage 22 native camera code must compile on target platforms before claiming readiness.

## Next
1. Run exact SHA npm typecheck/build and focused native Studio/Metal CTests, fix errors.
2. Add typed parameter snapshots, validated JSON preset export/import, and explicit seed only if supported by native runtime.
3. Add render-owner atomic capture provenance (actual tick, camera, graph revision, GPU fence).
4. Add split dimension guard, drag handle and tests; then update skill/roadmap and real screenshot devlog.
