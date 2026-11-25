# TerraWeave Caverns — stage 31 handoff (2026-11-23)

## Provenance

Repository `chromealex/AIVERSE`, feature branch `feature/terraweave-caverns-core-20261123`. **Not merged into main**.

Changes actually published:
- `9cfc42d5bf6a515fc4ffcbd003e51a18bb1c83c9` — `engine/world/TerraWeaveCavernsEditRuntime.h`: read-only, seam-certified visual preview candidate extraction without gameplay dirty ACK.
- `d087c44b821ac7e3b8d6307534d4dc58b46d773f` — `apps/editor/Editor.h`: explicit default-off visual preview option.
- `6da18947418ba874d8ee60f17f5935af387b1e68` — `apps/editor/Editor.cpp`: actual native Scene integration with GPU staging, drawing and GPU-idle teardown.
- `ddf1611c3780b8d8aa0fb6f522fbbc70f7025470` — `apps/editor/windows/TerrainWindow.cpp`: opt-in visual-only mode and warning that Box3D, SpatialWorld and NavMesh remain unchanged.
- `e28f4665327d63fb019d62050a932f179ad51dd2` — `engine/render/TerraWeaveCavernsShardGpuHost.h`: explicit render-only `activateVisualOnly` API distinct from the gameplay `activateGroup` path.
- `f4ef7acbf17ef2e754bbe5700aa150528e1aac27` — Editor now uses the render-only activation instead of calling gameplay owner commit callback.
- `340b683f61744575fda88c84ec1b79ea3e7656b5` — `tests/terraweave_caverns_editor_visual_preview_test.cpp`: source test for no gameplay ACK and stale revision invalidation.
- `9d6eac98a71a16f77967efe5fd2362bd77afe46a` — refuse staging/drawing old-project preview after a project switch.
- `1f7629bfa5f06d5e81d548c21b51f9cc72f8698d` — mandatory update to `skills/world/aiverse-terraweave-caverns/SKILL.md`.
- This dated handoff is a subsequent commit; inspect feature HEAD for current SHA.

## Implementation

The TerrainWindow Caverns panel now has `3D GPU-предпросмотр пещер (только визуально)`, deliberately off by default. The native Editor owns the option in `Editor::cavernsVisualPreviewEnabled`. The user is warned that rendered cave edits are NOT automatically applied to the physics world or NavMesh.

`CaveEditRuntimeBridge::visualPreviewCandidates()` builds a const-friendly copy of all currently prepared dirty CPU meshes, requiring: no active workers, full dirty candidate coverage, current revision/serial, prospective Stage16 seam certification for every loaded face. It does **not** acknowledge gameplay dirty messages or publish the game's current gate. A newer source revision invalidates old candidates until regenerated.

`Editor::CavernsEditHost` owns an independent GPU `CaveShardGpuHost`. The editor runs `stageVisualPreview` from `renderScene` before `r3d_.beginScene`, converting current `CavePreparedEdit` data into complete signed spatial shards and loading candidate `GeometryScene` objects. `activateVisualOnly` commits those drawings for editor viewing ONLY. This avoids abusing `CaveEditedSceneTransaction`'s four-owner gameplay commit API. The visual revision and author epoch are used to avoid repeated GPU uploads in the same observed state. Editor Scene then calls `drawVisualPreview(r3d_)` once in the normal model submission phase. The rest of the terrain continues to render. No second cave draw path is introduced here.

To avoid GPU use-after-free, `stopPreview(device)` calls `CaveShardGpuHost::shutdownAfterGpuIdle` on project close/switch and at native Editor shutdown, retaining GPU resources if the required barrier fails. Scene rendering and staging explicitly require project identity match; stale preview is not submitted when a different project is opening.

Visual preview enables **GeometryScene renderer fallback**, not STRATA GPU selection (`CaveBatchSpec.enableStrata=false`). The existing STRATA staging API remains available elsewhere, but no GPU cluster rendering proof is claimed for this stage. The editor preview does not alter `Editor::cavernsBuildStats.visualCommitConnected` (this flag refers to missing gameplay owner commit, not whether preview GPU can render).

## Verification performed

- Inspected actual `Editor::renderScene`, the native `Renderer3D::beginScene` and `view_.draw` sequencing, Editor shutdown device-idle barrier, existing `CavernsEditHost`, Stage30 GPU shard host and Stage22 CPU edit bridge on the feature branch.
- GitHub connector acknowledged all source, test and skill update commits above.
- `tests/terraweave_caverns_editor_visual_preview_test.cpp` was authored to check CPU candidate access does not ACK gameplay dirty state and that a later source revision invalidates old preview candidates.
- **NOT RUN:** full AIVERSE C++ compile, CMake/CTest, native Editor Scene interaction, Visual GPU screenshot on Metal/Vulkan/GLES, real GeometryScene GPU uploads, shader and STRATA selection, GPU idle failure injection, actual Box3D/SpatialWorld/NavMesh gameplay publishing. The source-level regression test has not been executed. Do not claim the editor visually works until native validation.

## Known limitations

1. **Visual-only mode** is not a game physics update. `CaveEditRuntimeBridge::publishReady` is deliberately NOT invoked, hence game dirty records remain pending. The editor may show a new hole while the simulation still collides with the old scene; disable preview for collision-sensitive gameplay testing.
2. GPU staging uses full shard GeometryScene uploads, and old GeometryScene/STRATA owners still may require full GPU idle for safe retirement. This can cause pauses, particularly when changing projects. The opt-in editor mode currently has no frame-budgeted incremental GPU upload scheduler.
3. If `stagePreparedEdits` is refused due to GPU budget/backpressure, the previous visual state remains and the editor can retry when subsequent Scene draws request the current author revision. Errors are surfaced via `cavernsBuildStats.diagnostic`.
4. Native editor code is not compiled in this execution environment. Check type/API compatibility and real Scene framebuffer ordering. The material sources and default cave model still need visual quality acceptance and cave depth testing.
5. A cave must be present in the full authored snapshot; this editor path does not automatically generate arbitrary base cave terrain across the full camera-visible world. It only stages chunks invalidated by tracked SDF authoring changes, with Stage23 broad old/new stamp influence union.
6. The full all-owner staged Box3D/SpatialWorld/NavMesh commit remains unsolved: Box3D creates active bodies immediately on `Scene::create`, and Stage25 ownership readiness is bookkeeping rather than a rollback-safe transaction. Do not route this editor-only visual commit to gameplay.
7. The default-on camera input / brush interaction still needs real visual acceptance: walls, negative Y, ceiling picks, projects with no prior cave content and very thin surfaces.

## Concrete next stage

1. **High priority:** compile the complete feature branch, register every Caverns Stage1–31 CTest executable and fix real C++ errors. Run native editor UI and GPU captures, including mixed-shard fill, Ctrl+Z undo, repeated paint, project switch and device loss.
2. Add explicit visual-preview status in TerrainWindow separate from gameplay dirty metrics: GPU staged/active revision, failed stages and retirement queue length. Implement frame-budget and GPU owner-pressure handling to prevent stutters while drawing.
3. Build a real gameplay-exclusive owner transaction with rollback-safe Box3D/body replacement, reserved SpatialWorld IDs, versioned NavMesh shadow and renderer source swap. Only then let `CaveEditRuntimeBridge::publishReady` ACK source revisions and make gameplay world updates automatic.
4. Implement genuinely asynchronous fence-backed GeometryScene/STRATA sidecar retirement, or preserve the documented full-idle device barrier for compatibility until it is safe.
5. Continue skill append and standalone dated handoff after each stage; do not merge into main without native tests.
