# AIVERSE Editor unification — agent handoff and living progress log

**Status:** IN PROGRESS — not verified, not ready to merge.
**PR:** https://github.com/chromealex/AIVERSE/pull/7
**Working branch:** `feature/universal-entity-inspector-fps` (base `main`)
**Owner goal:** The editor must treat RTS, FPS and future genres as ordinary entities with the same Scene, Hierarchy, Inspector, picking, Transform gizmo and outline. Genre-specific gameplay components are allowed; genre-specific editor selection/render/inspector stacks are not the target architecture.

> This file is the source of truth for the next coding agent. Read it with `AGENTS.md` before changes. **Update this file in the same branch after EVERY implementation/verification stage**, including unsuccessful stages. Never claim a feature complete based only on textual code inspection.

## Definition of done

- [ ] Shared entity identity and component/transform contract for both authored document entities and live ECS entities; no FPS-only editor selection state.
- [ ] Both RTS and FPS feed the same editor scene submission path without losing model/animation/material/skin support.
- [ ] Scene picking, hover, selection, Hierarchy, Inspector and undo/redo share identity and work for both genres.
- [ ] Editable native ECS component inspection, add/remove component support where safe, type-aware serialization/writeback. Document-only typed read inventory introduced; live ECS writeback still absent.
- [ ] Interactive Move/Rotate/Scale gizmo (XYZ, Local/World, handles, snapping, multi-selection, undo/redo). Where a legacy map schema cannot express rotation/scale, expose the limitation instead of silently dropping edits.
- [ ] Silhouette outline for selected/hovered authored models and primitive meshes, not just AABB wireframes.
- [ ] CI builds/tests green; macOS editor starts without crash; manual visual and interaction checks completed.
- [ ] PR reviewed, merged only after all gates pass.

## Current architecture and concrete files

| Responsibility | Current location | Notes |
| --- | --- | --- |
| Shared selection | `apps/editor/Editor.h` (`Selection`) | Added `Selection::FpsObject` with `ref`; `fpsSelectedSceneObject_` was removed. |
| Shared source adapter | `apps/editor/EditorEntityAdapter.h` | `DocumentEntityRef` for RTS map entities, FPS colliders/spawns; `readDocumentTransform`, `translateDocumentEntity(ies)`, `setDocumentPosition`, `setDocumentColliderBounds`. **Document adapter, not native ECS reflection.** |
| Common renderable | `apps/editor/SceneRenderable.h` | `SceneRenderable` can reference `Mesh` or `Model`, bounds, selected/hovered, ray/AABB picking. FPS currently uses primitive mesh. |
| FPS scene extraction | `apps/editor/EditorFps.cpp` | Reads `doc().data.fps["colliders"/"spawns"]`; creates box previews, handles FPS Inspector and adapter calls. |
| RTS scene extraction | `apps/editor/Editor.cpp`, `modules/rts/view/RtsViewDraw.cpp` | Still uses `RtsView` and its own model extraction. Must preserve existing RTS materials/animation. |
| Scene interaction | `apps/editor/windows/SceneWindow.cpp` | FPS dragging XZ / Shift-Y, X/Z axis constraints; RTS move calls common document translation. Colored axes are drawn for FPS but **not interactive handles**. |
| Scene picking | `apps/editor/EditorWorld.cpp` | `Editor::pickObject` dispatches FPS ray/AABB or RTS picker, returns shared `Selection`. |
| Hierarchy | `apps/editor/windows/HierarchyWindow.cpp` | FPS collider/spawn groups with shared selection and adapter-based camera focus; RTS still has distinct hierarchy path. |
| Inspector | `apps/editor/windows/InspectorWindow.cpp`, `apps/editor/DetailsPanel.cpp`, `apps/editor/EditorFps.cpp` | FPS currently has a dedicated `drawFpsSceneInspector` branch; RTS `DetailsPanel` has richer component presentation, much of it read-only. |
| Render infrastructure | `engine/render/Renderer3D.h`, `engine/render/Renderer3DModels.cpp` | `ModelDraw.outline` supports model inverted-hull outline; `drawMesh` primitive path has no equivalent silhouette outline. |
| RTS map data | `apps/editor/core/MapDoc.h` | `DocEntity` stores `x,z` (no generic Y/rotation/scale). FPS objects are JSON arrays/bounds. |

## Implemented, but not yet validated by a successful build

1. Shared `Editor::renderScene` renderer lifecycle; FPS preview submits through `SceneRenderable`.
2. Shared FPS picking and selection, hover, Hierarchy row highlighting and Inspector routing.
3. Common `DocumentEntityRef` and source adapters for RTS map entities / FPS collider / FPS spawn.
4. Read and write position; FPS collider bounds editing; RTS and FPS movement through common translation helpers.
5. Multi-selection prevalidation, deduplication, one document revision per batch; invalid collider extents rejected.
6. FPS scene XZ drag, Shift-Y drag, X/Z constrained movement; colored XYZ axis visualization (not a true gizmo).
7. RTS model outline; FPS selected/hovered primitive AABB edges (not a silhouette).
8. Latest known commits prior to this handoff include `169cf293` (FPS collider position Inspector) and `918db3cf` (collider Transform bounds validation).

**Validation evidence:** source-file presence checks only. GitHub Actions runs have repeatedly shown `queued`; there is NO confirmed passing editor build, no confirmed macOS launch and no verified visual test. Check latest CI rather than assuming it passed.

## Risks / likely defects to investigate immediately

- `EditorEntityAdapter.h` is a new header depending on `core/MapDoc.h`; verify all include paths and C++20 compilation. Check `MapDoc::data.fps` JSON methods against actual signatures.
- `SceneWindow.cpp` RTS group drag now uses `translateDocumentEntities`; compare with prior `worldedit::move` behavior, especially planet `at`, snap, locks and undo.
- FPS `Selection::FpsObject` is a document reference, not a runtime ECS entity. Do not label it native ECS support.
- `Editor::pickObject` FPS branch and mouse handling should be tested with scene viewport coordinates and selection modifiers.
- `drawFpsSceneInspector` edits JSON through adapter; ensure no invalid undo snapshots or pointer invalidation during ImGui drag.
- Primitive bounding box overlay must not be described as geometric silhouette outline.
- FPS source refs are based on array index; insertion/deletion/reorder can change identity. Plan stable GUIDs when schema allows.
- FPS camera zoom, off-screen culling, selection and drag should be tested at near-horizontal pitch.

## Next implementation stages (order matters)

**Stage A — Compile/test first.** Get GitHub CI outcome and/or build locally. Fix errors introduced by PR. Run targeted editor tests and launch on macOS if available. Record commands, exit codes and links to CI jobs here.

**Stage B — Common component adapter.** Define an editor-facing component schema/registry with type ID, read-only/editable capabilities, safe read/write, and serialization for native ECS and document entities. Do not fabricate generic reflection that cannot write back. Add tests for RTS/FPS data and selection.

**Stage C — Inspector consolidation.** Route FPS/RTS through a single Inspector panel backed by the common component adapter. Preserve specialized component drawers via registry; delete FPS-specific Inspector branch only after parity.

**Stage D — Scene extraction.** Extend the common renderable to preserve `ModelDraw` data (skin/pose/material overrides/geometry policy). Migrate RTS extraction carefully; add FPS authored models and stable identity.

**Stage E — Gizmo and outline.** Interactive handle picking, Move/Rotate/Scale, Local/World, snap, multi-edit, undo; true silhouette outline for `Mesh` and `Model`.

**Stage F — Release gates.** Full CI, macOS smoke test, Scene/Hierarchy/Inspector consistency, selection and undo tests, screenshot-based UI review, review and merge.

## Mandatory stage-update protocol

After EVERY stage or substantial commit, update the sections above **and append** an entry below. Include:
- Date/time (UTC if known), branch, commit SHA(s), exact files changed.
- What changed, what remains, and any schema/behavior limitations.
- Verification: actual build/test commands and results; link to GitHub workflow/job when available. If not run, say **NOT RUN**.
- New failures, follow-up owner, and next stage.
- Update checkbox statuses ONLY when verified; avoid vague “done” claims.
- Commit the handoff update to the same PR. If a stage is blocked, record the blocker instead of silently moving on.

## Progress log

### 2026-11-17 — Initial handoff document
- Branch: `feature/universal-entity-inspector-fps`; PR #7.
- Added this living agent handoff and the explicit acceptance criteria, architecture inventory, risks and execution plan.
- Code build/tests: **NOT RUN in this handoff stage**. Earlier GitHub workflow checks were observed queued.
- Next: obtain CI results and compile/fix, then implement component adapter/Inspector consolidation.

### 2026-11-17 — Transform validation hardening
- Branch: `feature/universal-entity-inspector-fps`.
- Commits: `ccc5c829` (`apps/editor/EditorEntityAdapter.h`), `27cf8262` (same file).
- Single-object FPS collider translation now rejects malformed/non-finite/inverted min/max extents **before** mutation. The shared bounds setter rejects arrays shorter than three coordinates.
- Validation: source edits committed; targeted code review by inspecting exact replacement blocks. **Build/tests NOT RUN**. GitHub Actions for prior handoff commit `47aa2192`: CI and Studio validation were **queued** at last check, not green.
- Limitations: No generic native ECS reflection, no common Inspector drawer registry, no full Move/Rotate/Scale gizmo or mesh silhouette outline.
- Next: obtain successful build/test results; implement common component adapter and migrate Inspector; verify interactions on macOS.

### 2026-11-17 — Document component inventory and FPS Inspector hookup
- Branch: `feature/universal-entity-inspector-fps`.
- Commits: `231f9890` (`apps/editor/EditorEntityAdapter.h`), `17cc48bb` (`apps/editor/EditorFps.cpp`).
- Added `DocumentComponentKind`, `DocumentComponentDescriptor`, `documentComponents()` and `readDocumentComponent()`. Supported document-backed views: RTS Transform/Renderable/Gameplay; FPS Transform/BoxCollider/SpawnPoint.
- FPS Inspector now renders a shared component inventory and read-only status while retaining existing specialized editable fields.
- Important: this is **not native ECS reflection**. No live-world component add/remove, no generalized writeback, and no RTS Inspector migration yet.
- Verification: source changes committed; **build/tests NOT RUN**. CI and Studio validation for previous `f0bb6d6a` were observed **queued**, not passed.
- Next: compile first; then introduce safe typed component writeback and migrate both genre inspectors to common component drawers; add targeted tests.

### 2026-11-17 — Typed document component writeback
- Branch: `feature/universal-entity-inspector-fps`.
- Commits: `b55c77a1` (`apps/editor/EditorEntityAdapter.h`), `7d3d3d4f` (`apps/editor/EditorFps.cpp`).
- Added `writeDocumentComponent()` with strict numeric vec3 validation and allowed kinds: document Transform, FPS Collider, FPS Spawn. Unsupported kinds (RTS gameplay/renderable, live ECS) return false instead of pretending to be writable.
- FPS Inspector Position XYZ and collider Min/Max fields now write through the typed component API. Undo snapshot creation remains in the existing ImGui activation path.
- Validation: source code inspected; **compilation and interactive tests NOT RUN**. Prior CI `649bb50e` was pending/queued.
- Remaining: native ECS component registry/writeback, common Inspector drawers for RTS and FPS, full gizmo and outline, passing CI.
- Next: obtain CI/build diagnostics; test typed writeback and undo on both FPS object types; then migrate RTS Inspector using typed component registry.

### 2026-11-17 — Shared component inventory UI for RTS and FPS
- Branch: `feature/universal-entity-inspector-fps`.
- Commits: `70e6e6af` (`apps/editor/EditorComponentPanel.h`), `dd8ff1ad` (`apps/editor/EditorFps.cpp`), `77d6c89f` (`apps/editor/DetailsPanel.cpp`).
- Introduced `drawDocumentComponentInventory()` as a common document-backed Inspector UI; FPS Inspector and RTS authored-entity DetailsPanel both call it.
- Live RTS ECS rows retain existing DetailsPanel; component inventory does not yet support native ECS reflection/writeback or shared editable field drawers. No claim of complete Inspector consolidation.
- Verification: changes committed and inspected via GitHub; **build/tests NOT RUN**. Last observed CI for `649bb50e` was pending/queued.
- Next: compile and fix errors; create common typed component field drawers, integrate runtime ECS component adapters and verify undo/UI behavior.

### 2026-11-17 — Shared editable Position XYZ drawer
- Branch: `feature/universal-entity-inspector-fps`.
- Commits: `ea38ef4e` (`apps/editor/EditorComponentPanel.h`), `80154699` (`apps/editor/EditorFps.cpp`), `68af0f2b` (`apps/editor/DetailsPanel.cpp`).
- Introduced `drawDocumentPositionField()` using `readDocumentTransform` and `writeDocumentComponent(Transform)`; connected to RTS authored DocEntity and FPS collider/spawn Inspector.
- Existing RTS DetailsPanel property fields remain; potential duplicated position presentation should be reviewed visually. RTS serialized Y is unsupported and rejected by the writer.
- Validation: source changes committed; **build/tests NOT RUN**. Latest prior CI `ac61f4fb` observed queued.
- Next: build/CI verification, consolidate duplicate RTS Transform drawers, add typed component-specific fields, then native ECS writeback.

### 2026-11-17 — RTS planar Transform UI correctness
- Branch: `feature/universal-entity-inspector-fps`.
- Commit: `f1385f13` (`apps/editor/EditorComponentPanel.h`).
- Shared Transform drawer now shows **Position XZ** with `DragFloat2` for RTS map entities, while FPS collider/spawn retain 3D `Position XYZ`. This avoids exposing an RTS Y edit that the serialized map schema cannot represent.
- Validation: source edit committed; inspected `engine/data/Json.h` to confirm `Json::operator==` exists. **Build/tests NOT RUN**. Prior `acdb57cd` CI and Studio validation observed queued.
- Next: verify build/CI, address duplicate RTS mapFields Transform display, then implement shared typed component drawers and native ECS adapter.

### 2026-11-17 — Remove duplicate RTS position editing and unify centroid translation
- Branch: `feature/universal-entity-inspector-fps`.
- Commit: `05ff3aff` (`apps/editor/DetailsPanel.cpp`).
- RTS single-selection Position XZ now appears only in shared `drawDocumentPositionField`, rather than also in legacy `mapFields`.
- Multi-selection retains the legacy centroid `DragFloat2` UI, but writes via `translateDocumentEntities` with source refs, replacing direct `worldedit::move` for this path.
- Validation: source change committed; **build/tests NOT RUN**. Need verify undo and multi-selection drag after switching between single and multi.
- Next: obtain actual CI/build results, inspect existing component drawer APIs, migrate more fields without breaking RTS runtime ECS DetailsPanel.

### 2026-11-17 — Extract collider bounds editor to shared typed component panel
- Branch: `feature/universal-entity-inspector-fps`.
- Commits: `dea12d99` (`apps/editor/EditorComponentPanel.h`), `456bfd7b` (`apps/editor/EditorFps.cpp`).
- Added `drawDocumentColliderBoundsField()`, which reads/writes the typed Collider component via `readDocumentComponent` / `writeDocumentComponent` and maintains ImGui activation-based undo snapshotting.
- FPS Inspector now calls this common drawer instead of keeping a local 1.5 KB bounds editor. Shared component drawers now include Transform position and Collider bounds.
- Validation: source changes committed; **build/tests NOT RUN**. CI for previous `69e99425` was observed queued.
- Next: CI/build verification; consolidate Inspector component card presentation; implement native ECS component read/write adapters without breaking RTS DetailsPanel.

### 2026-11-17 — Schema-driven editable Inspector cards for FPS
- Branch: `feature/universal-entity-inspector-fps`.
- Commits: `faf8396d` (`apps/editor/EditorComponentPanel.h`), `6d4d40ae` (`apps/editor/EditorFps.cpp`).
- Added `drawDocumentEditableComponents()` that iterates `documentComponents()` and draws typed Transform/Collider/Spawn cards. FPS Inspector now uses this shared dispatch rather than hand-coded per-object Transform section.
- This is a document-backed component UI, **not** native ECS reflection. RTS authored entities still use their existing DetailsPanel plus the shared Position drawer. Read-only component inventory remains separate.
- Validation: source edits committed; **build/tests NOT RUN**. GitHub Actions for `0e818076`: Studio validation queued (Ubuntu, Windows, macOS jobs), CI queued (multiple platform jobs). This is a CI availability blocker, not a passing result.
- Next: get actual build diagnostics, then migrate RTS document Inspector into the same card dispatcher without duplicating fields; build native ECS component registry and writeback.

### 2026-11-17 — RTS authored entities use the same editable component-card dispatcher
- Branch: `feature/universal-entity-inspector-fps`.
- Commit: `90262a21` (`apps/editor/DetailsPanel.cpp`).
- Replaced the RTS-only direct `drawDocumentPositionField` call with `drawDocumentEditableComponents`. Both RTS authored DocEntity and FPS collider/spawn now dispatch editable document components through the same `EditorComponentPanel.h` function.
- Existing RTS DetailsPanel `mapFields` and runtime ECS `sections` remain in place; this does **not** constitute a native ECS Inspector replacement.
- Validation: source change committed; **build/tests NOT RUN**. CI previously observed queued; no passing build claimed.
- Next: obtain CI diagnostics and test UI; implement typed runtime ECS component registry and writeback, then eliminate redundant document Inspector paths safely.

### 2026-11-17 — Document adapter defensive validation
- Branch: `feature/universal-entity-inspector-fps`.
- Commit: `71e586e1` (`apps/editor/EditorEntityAdapter.h`).
- Both mutable and const FPS entity accessors now explicitly accept only Collider/Spawn source kinds; invalid enum values no longer silently fall back to colliders. RTS Transform reads reject non-finite source X/Z values before exposing them to Inspector/Scene.
- Verification: GitHub source inspection only; **build/tests NOT RUN**. CI for previous `42cc0612` observed queued (both CI and Studio validation).
- Next: prioritize a real build and focused adapter tests before further native ECS editor work. Investigate long-running queued Actions and test typed Inspector edits/undo.

### 2026-11-17 — Collider midpoint numeric safety
- Branch: `feature/universal-entity-inspector-fps`.
- Commits: `1a400e7e`, `c1a653bc` (`apps/editor/EditorEntityAdapter.h`).
- Collider midpoint calculation now uses half-scaled endpoints instead of adding two potentially huge finite values, avoiding spurious overflow. FPS source-key index bounds comparison is explicitly 64-bit.
- Verification: source edits committed; **build/tests NOT RUN**. CI for `820ed35c` remained queued on latest check.
- Next: run actual editor build and targeted component adapter tests; inspect CI queue/runner availability before claiming functional readiness.

### 2026-11-17 — macOS build failure reported and fixed (awaiting rerun)
- User ran `cmake --build build-metal -j 8` on macOS; compiler failed at `apps/editor/EditorEntityAdapter.h:69`: cannot convert `const Json*` to `Json*` when using `fps.member(collection)[ref.index]`.
- Verified `engine/data/Json.h`: `operator[](size_t)` is const-only; `Json::at(size_t)` returns mutable `Json&`.
- Fix commit `9f4493e`: changed to `&fps.member(collection).at(ref.index)`.
- Verification: API signature inspected in source; **macOS rebuild NOT YET RUN after fix**. Ask user to pull branch and rerun `cmake --build build-metal -j 8`; collect subsequent errors.
- Next: fix remaining compiler diagnostics, then launch editor and test RTS/FPS Inspector, selection and Undo.

### 2026-11-17 — macOS compiler error in SceneWindow and Hierarchy warnings
- User's second `cmake --build build-metal -j 8` failed at `apps/editor/windows/SceneWindow.cpp:88`: undeclared identifier `hit`. Also reported unused `box` and `spawn` in `HierarchyWindow.cpp`.
- Fix commit `a6c8e700`: removed obsolete `if (hit < 0) pickSelect(ed, hovered, io);` after the shared selection path already invokes `pickSelect`.
- Cleanup commit `017a5731`: switched FPS Hierarchy collider/spawn iteration to indexed loops, removing unused variables.
- Validation: fixes committed; **macOS rebuild NOT YET RUN after these commits**. User should pull branch and rerun build.
- Next: resolve further build errors, launch editor, test selection/Inspector/Undo; record actual test results.

### 2026-11-17 — macOS clang warnings and FPS Hierarchy correctness
- User reported `HierarchyWindow.cpp:87` double increment of `index` (loop header and body), and `EditorFps.cpp:158` wrong `%d` format for `size_t`.
- Fix commits: `28c8d0c8` removes duplicate body increment (also prevents every second Spawn Point from being skipped); `fa50242c` uses `%zu` for source index.
- Verification: source fixes committed; **macOS rebuild NOT YET RUN after these commits**.
- Next: user pull and rebuild; launch editor and verify all FPS spawn entries appear in Hierarchy and Inspector displays correct indices.

### 2026-11-17 — Inspector UI redesign after user screenshot
- User reported duplicate bullet component inventory, 2D-only Transform, confusing `Карта (правка)` and unwanted Lua Custom Inspector. Screenshot showed Sedan authored RTS entity and native module unavailable warning.
- Commits: `35293b86` (`EditorComponentPanel.h`) removes bullet inventory and renders all document component types as collapsible cards with read-only fields; `27beb65d` (`DetailsPanel.cpp`) removes bullet inventory and renames map editor `RTS Object`; `fb6363f6` (`EditorFps.cpp`) removes duplicate list; `0958cc4f` (`InspectorWindow.cpp`) removes default Lua Custom Inspector; `2b95fc08` (`EditorComponentPanel.h`) introduces native C++ `InspectorDrawerRegistry` with whole-component and per-field callbacks.
- **Partial implementation, not feature complete.** RTS map serialization still stores XZ only; do not invent Y/rotation/scale fields. Native ECS Transform reflection and writeback remain to be implemented. RTS legacy `mapFields` may still duplicate `RTS Object` card: migrate it into the card instead of maintaining two UI sections. Generic recursive editing for arbitrary component fields is not implemented; current fallback is read-only for non-Transform/Collider.
- Validation: GitHub source edits only; **build/tests NOT RUN**. Need macOS rebuild and screenshot review. Lua file remains in repository for compatibility but is not rendered by default Inspector.
- Next: resolve duplicated RTS Object section; build native ECS component field descriptors (3D Transform, rotation, scale only when supported), generic type-driven field editing, then compile/test and inspect UI.

### 2026-11-17 — RTS Object card and editable display name (user screenshot feedback)
- User reported RTS object properties were still outside component cards; no editable display name and no object Enabled toggle.
- Commits: `2a4a0712` (`DetailsPanel.cpp`) wraps authored RTS map fields inside a collapsible `RTS Object` card; `d1f6a41d` (`EditorEntityAdapter.h`) removes duplicate read-only RTS Object card; `6e523726` (`DetailsPanel.cpp`) adds editable `displayName` stored in `DocEntity.extra` and shown in Inspector header, separate from stable script/road `id`.
- **Enabled toggle NOT IMPLEMENTED**: DocEntity schema has no enabled flag and no confirmed runtime disable semantics. Do not add a cosmetic checkbox that only changes JSON but leaves the entity active. Next agent must implement authoring serialization + preview/world instantiation behavior + editor Scene/Hierarchy state + undo tests as one coherent feature.
- Validation: GitHub edits only; **build/tests NOT RUN**. Verify `ui::inputString` overload and edited field persistence, including save/reload.
- Next: implement actual disable semantics end-to-end; migrate other RTS object properties to generic component field editor and validate UI/build.

### 2026-11-17 — Remove synthetic duplicate RTS Object card
- User screenshot showed two `RTS Object` cards: synthetic read-only `DocumentComponentKind::Gameplay` and legacy editable `DocEntity` metadata.
- Commits: `fbeb58e0` (`EditorEntityAdapter.h`) removes synthetic `RtsObject` descriptor from `documentComponents()`; `d8b5e768` (`DetailsPanel.cpp`) labels remaining authored-data editor `Map Entity Metadata` instead of presenting it as an ECS component.
- **Architectural clarification:** `RTS Object` was a UI placeholder, not a native ECS component. `DocEntity` is map-authoring data; the live runtime components are separate. Do not create fake components to display authoring metadata. Future Inspector must show actual runtime ECS component IDs and schema-backed values, with map metadata separated.
- Validation: source inspection and commits; **build/UI tests NOT RUN**. Verify no duplicate cards after rebuild.
- Next: native ECS component introspection and editable reflection, real Transform rotation/scale when present, header name/Enabled semantics with persistence and runtime behavior.

### 2026-11-17 — ECS snapshots become authoritative for RTS Inspector component cards
- User explicitly required REAL ECS component identities/values, not synthetic `DocEntity` components. Reviewed `apps/editor/core/WorldInspect.cpp`: `inspectEntity(Simulation&, RtsModule&, Entity, rulesJson)` iterates actual `World::has<T>` descriptors and serializes live ECS component values under `components`. `DetailsPanel::prepare()` already builds sections using those actual IDs.
- Commit `9d4c1a7c` (`apps/editor/DetailsPanel.cpp`): removes synthetic `drawDocumentEditableComponents()` from RTS Inspector; displays real ECS component `sections(ed)` first, then optional collapsed authoring-only `Map Entity Metadata` after them. Never call map fields real ECS components.
- **Critical limitations:** existing ECS inspection is currently read-only, since the extracted JSON is a snapshot and has no writeback. `WorldInspect.cpp` descriptor serialization is real but RTS-specific; FPS and native project modules still need adapters / reflection. Rotation and Scale must only be displayed when present in actual ECS Transform schema. An unavailable native module means no real native components to inspect; do not synthesize them.
- Verification: GitHub source read/commit only, **compile/UI tests NOT RUN**. Next agent must build and check UI with working native module; then implement typed per-component `World&` writeback with edit/undo policy and serialization persistence.

### 2026-11-17 — Typed field metadata and authored Transform editing on real ECS cards
- Branch: `feature/universal-entity-inspector-fps`.
- Commits: `1f8d7bb6` (`apps/editor/core/WorldInspect.h`), `8d8bc75f` (`apps/editor/core/WorldInspect.cpp`), `019d8fdb` and `62367c2a` (`apps/editor/DetailsPanel.cpp`).
- Exposed `WorldComponentField` and `worldComponentFields(componentId)` backed by typed metadata matching the **actual ECS descriptor serializers** for Transform, Owner and Unit. Unknown component fields continue to display via JSON fallback; this is not a complete native C++ reflection system.
- The RTS Inspector `Transform.pos` value now comes from the real ECS snapshot, with X/Z editable ONLY for a single map-authored entity and persisted via the shared document adapter, with Undo snapshot on gesture activation. The real ECS Y coordinate is displayed **read-only** because `DocEntity` has no serialized Y. Runtime/play objects remain read-only.
- `Transform.yaw` (stored in turns in the ECS) is rendered in degrees from its real value, read-only until there is a rotation persistence contract. Scale is not part of this RTS `Transform` struct and must not be fabricated.
- Limits: There is still no arbitrary native ECS field writeback, runtime ECS Undo, or FPS native component serializer. Persisted document X/Z changes trigger normal preview rebuild; never mutate the throwaway preview's World directly and call it saved.
- Verification: source changes committed via GitHub; **compiler, unit tests and UI test NOT RUN** in this stage. Next: macOS compile, verify ECS Transform vs map coordinates, Undo/redo and scene changes; then implement type-safe persisted field setters for further ECS components and native plugin schema registrations.

### 2026-11-17 — Persisted typed ECS-field bindings (Transform and Owner)
- Branch: `feature/universal-entity-inspector-fps`.
- Commits: `b3a3740e` (`apps/editor/EditorEcsWriteback.h`), `858e953b` (`apps/editor/DetailsPanel.cpp`).
- Introduced explicit allowlisted `canWriteAuthoredEcsField()` and `writeAuthoredEcsField()` functions, keyed by the **actual ECS component IDs and field IDs**, not placeholder document component names. Initial persisted bindings are `Transform.pos` (RTS map X/Z only) and `Owner.player` (map owner 0..N up to 255). All other fields are non-writable until a valid persistence contract exists.
- RTS Inspector field drawers for real `Transform.pos` and `Owner.player` now dispatch via the same typed writeback API. `Owner.player` uses `worldedit::setOwner`, including its document undo transaction; editing X/Z continues to use the shared document Transform setter and requires an activation-based beginEdit.
- **Distinction:** This is persisted map-authoring writeback for fields observed in actual ECS snapshots, not mutation of the running ECS. Editor preview rebuilds the live ECS state from the document. Runtime world entities, unknown native components, and fields lacking persisted schema remain read-only. Do not claim generic native runtime component editing or Undo is implemented.
- Verification: GitHub source re-read confirms new API and callsites. **Mac compiler and UI tests NOT RUN**. In particular test Owner slider, hostile owner (-1) read-only behavior, X/Z undo, preview rebuild and map save/reopen.
- Next: request macOS build results and fix any errors; extend typed schema/registration for actual native C++ ECS components with explicit safe editing policy and persistence.

### 2026-11-17 — Native ECS Inspector reader extension registry
- Branch: `feature/universal-entity-inspector-fps`.
- Commits: `abccd3ea`, `8455e68c`, `922dc9a1` (`apps/editor/core/WorldInspect.h`); `7d233bdd`, `fccf3996` (`apps/editor/core/WorldInspect.cpp`); `9f915abf` (`apps/editor/DetailsPanel.cpp`); `60618a9e` (`docs/editor-native-ecs-inspector.md`).
- Added `WorldInspectorExtension` with native C++ `has(const World&, Entity)`, `read(World&, Entity)`, owned field descriptions and a user-visible title; `registerWorldInspectorExtension` / `unregisterWorldInspectorExtension`. `inspectEntity` now includes only **present real ECS pools** supplied by extension readers; it does not infer components from `DocEntity`. The component UI reads registered names and field schemas, with a default JSON fallback.
- Registry access is guarded by mutex and callbacks are copied into an inspection snapshot. Dynamic module unloading requires an external synchronization barrier to avoid callbacks from unloaded code. Registration is **in-process C++ only**, not wired to the existing runtime native module loading/export ABI.
- C++ `World::tryGet<T>` does not expose a const overload, so `read` receives mutable `World&` but must be observational. Built-in `World::has<T>()` remains the authoritative component existence check.
- `worldComponentFields` now returns a copy and `WorldComponentField` owns string metadata to keep dynamic registration/unregistration from leaving dangling pointers; `DetailsPanel` uses `std::optional` to own the selected schema.
- Verification: examined ECS header signatures and inspected committed source through GitHub; **compiler/tests/native module integration/UI NOT RUN**. No native component is auto-registered by this stage. The world tree component bitset/filter still enumerates only built-in RTS types.
- Next: get macOS build result, add in-process registry tests and then connect project native-module lifecycle safely; implement writeback hooks with real project persistence and Undo before making native fields editable.

### 2026-11-17 — NativeModule lifecycle integration for ECS Inspector
- Branch: `feature/universal-entity-inspector-fps`. Commits: `260a9c62` (`modules/rts/RtsNative.h`), `c7dfa114` (`modules/rts/RtsNative.cpp`), `3227d1d1` (`apps/editor/core/WorldInspect.cpp`), `008b91b4` (`apps/editor/DetailsPanel.cpp`), `1e657929` (`docs/editor-native-ecs-inspector.md`), `0c1f4b29` (`docs/native-modules.md`).
- Architecture verified against `docs/native-modules.md`: AIVERSE C++ modules are **statically linked into project-specific editor builds**, not `dlopen`/hot-reloaded. `NativeModules::load` creates module instances from `compiledNativeModules` for `rules.json/nativeModules`; editor preview uses that Simulation.
- Added optional virtual `NativeModule::inspectComponents(World&, Entity, Json& components, Json& schemas) const` with a no-op default. `NativeModules::inspectComponents(Simulation&, Entity, ...)` calls the **loaded module instances only**; it aggregates real reported ECS components, refuses overwrite of existing builtin/component names, and forwards per-field schema descriptors.
- `inspectEntity` now merges the current Simulation's `mod.native().inspectComponents` output with existing ECS pool snapshots, optionally adding `componentSchemas`. `DetailsPanel` uses these scoped schema titles for component cards and field labels, without creating `DocEntity` placeholders. The earlier global C++ registration API remains optional for host-side extensions; module hooks avoid stale global callbacks on switching games.
- **Status: partial, read-only native ECS inspection.** No currently existing project module has been updated to implement this new optional method; native module-specific components will appear only once their owning modules opt in. A schema is not actual runtime component reflection and does not imply writeback. Native component writes, persistence, undo, complex field drawers, and generic FPS ECS inspection remain outstanding.
- **Validation: GitHub source edits only; compiler, tests, module integration, and UI NOT RUN.** Next step is `cmake --build build-metal -j 8` on macOS with an actual project's native module, followed by smoke checks: one present component produces exactly one card, missing component produces none, schema title is correct, switching projects does not retain old native component callbacks, existing RTS components remain correct.

### 2026-11-17 — macOS compiler error: WorldComponentField C-string misuse
- User's `cmake --build build-metal -j 8` failed at `apps/editor/core/WorldInspect.cpp:361`: `field.id.empty()` called on `const char*` (`WorldComponentField::id`).
- Root cause: field metadata was described as owned strings in the native extension design, but `WorldInspect.h` still declared `const char* id/title`.
- Fix commit `e70871e9` (`apps/editor/core/WorldInspect.h`): change field ID/title to `std::string` to match the registry validation and prevent dangling name pointers when native extensions are unregistered.
- Verification: verified old declaration in GitHub source and committed replacement. **macOS rebuild after fix NOT RUN**.
- Next: user pulls branch, reruns `cmake --build build-metal -j 8`, reports any subsequent compiler errors. After successful build, test native component inspector appearance, data correctness and project switching.

### 2026-11-17 — Fix cascading macOS compiler errors from unclosed namespace
- User's macOS build reported `DetailsPanel.cpp:83` ('cannot define or redeclare collect here because namespace editor does not enclose namespace DetailsPanel') followed by many cascading undefined `ed`, `items_`, `cacheKey_`, etc.
- Root cause: `apps/editor/EditorEcsWriteback.h` opened `namespace aiverse::editor {` but did not close it. Since `DetailsPanel.cpp` includes this header before opening its own namespace, the implementation ended up in a nested namespace.
- Fix commit `393e778f`: appended `} // namespace aiverse::editor` at the end of `apps/editor/EditorEcsWriteback.h`.
- Verification: exact header/source inspected in GitHub and fix committed. **Mac rebuild after fix NOT RUN**. The 20 reported diagnostics likely cascade from this one issue, but must check for further compiler errors.
- Next: user pulls branch and runs `cmake --build build-metal -j 8`; then launch editor and test ECS Inspector for real native components, Undo and scene selection.

### 2026-11-17 — Regression report: RTS Inspector component cards disappeared
- User screenshot shows no component cards after ECS-first conversion. Confirmed in source: `DetailsPanel::draw` no longer submits synthetic map-backed components; it displays cards ONLY from `DetailsPanel::collect` → `Editor::worldDetails` → `inspectEntity` of a valid ECS World preview.
- A missing native module, failed/stale preview, or missing `WorldRow` ↔ `DocEntity` mapping can make `sections_` empty. The prior `DetailsPanel::sections` displayed no feedback for the particular case `withData_ == 0` with an apparently ready preview; it looked as though components silently vanished. Inspect `EditorWorld.cpp::worldRow` (map selection only resolves when `worldFresh()`), `worldDetails`, `WorldPreview::error()`, and `linkMapEntities()` when debugging.
- Commit `9b6b9709` (`apps/editor/DetailsPanel.cpp`): add explicit ECS-preview-loading, native-module-error, missing world entity/mapping, and truly empty ECS diagnostics, plus `Обновить ECS предпросмотр` action. **Does not fabricate fake components or solve missing native module/mapping itself.**
- Verification: source reviewed and committed, **build, native module integration and UI NOT RUN**. User needs to rebuild, inspect the exact displayed warning, and check whether the project-specific editor includes native modules.
- Next high-priority correction: trace missing ECS entity mapping and preview errors on user's project; if a native module is absent, build the project editor using `-DAIVERSE_CONTENT_DIR=<project>`. If ECS exists but no row, fix `linkMapEntities` or the relevant runtime spawn code. Complete universal RTS/FPS audit: FPS still uses JSON-only Inspector and Hierarchy, and RTS still uses `RtsView`; conversion is not complete.

### 2026-11-17 — Consolidate FPS Inspector entry and shared authored scene node extraction
- Branch: `feature/universal-entity-inspector-fps`. Commits: `8b3d4e35` (`DetailsPanel.h`), `b1f5f0bb` (`DetailsPanel.cpp`), `d6e926dc` (`InspectorWindow.cpp`), `7bfd70f1` (`Editor.h`), `8b18916d` (`EditorFps.cpp`), `c5b9a08a` (`EditorSceneNodes.h`), `0d787b1a` (`HierarchyWindow.cpp`), `00d0b0e6` (`EditorFps.cpp`).
- FPS, RTS authored and RTS live selections enter **one `DetailsPanel::draw` API**; removed the separate FPS-only `Editor::drawFpsSceneInspector` and its routing. FPS's document component fields are still document-backed because its editor does not currently expose FPS runtime ECS entities.
- Added `enumerateDocumentSceneNodes(map, fpsProject)`, a single authored-source descriptor layer with source identity, label, group, and shared Transform read. FPS Hierarchy and FPS scene render extraction now both use the same authored node inventory. RTS authored entities are also supported by the descriptor, but RTS rich world Hierarchy still uses its own world-row stack.
- The source abstraction explicitly does NOT pretend document entries are live ECS components.
- Verification: GitHub source changes only; **macOS build/tests/UI NOT RUN**. Risks: FPS source enumeration now happens per Scene refresh; check FPS collider geometry, spawn selection, and FPS Hierarchy labels. Need confirm build and runtime behavior.
- Remaining major separate genre paths: RTS `RtsView` vs FPS primitive `SceneRenderable` extraction, RTS selection hit algorithms vs FPS ray/AABB, genre-specific Scene camera/drag interactions, RTS World Hierarchy vs FPS authored hierarchy, and FPS absence of a runtime editor ECS inspector. Generic gizmo/outline are still missing. This stage should not be described as complete universalization.

### 2026-11-17 — Shared document selection identity adapter
- Commits: `4cd2b410` (`apps/editor/EditorSelectionAdapter.h`), `58c81817` (`apps/editor/EditorFps.cpp`), `61d6822e` (`apps/editor/EditorWorld.cpp`), `5f7cbca3` (`apps/editor/DetailsPanel.cpp`).
- Added `selectionForDocumentEntity()` and `documentEntityFromSelection()`. RTS `Selection::Entity` and FPS `Selection::FpsObject` now have one shared compatibility conversion to `DocumentEntityRef`; the result is used for FPS scene picking, selection actions, dragging and Inspector.
- This is a compatibility layer, NOT yet one universal runtime entity ID. Legacy FPS and RTS selection enum values still exist. There are no FPS per-object ECS entities in the author's current world: `fps::State::actors` is a World **resource**, FPS authored colliders/spawns are JSON, while RTS uses real World entity/component pools.
- Verification: source edits committed, compile/GUI **NOT RUN**. Next: adapt all scene interaction handling to common handlers, keep FPS/RTS source adapter explicit, and implement a cross-genre component inspector provider without inventing native FPS ECS objects.

### 2026-11-17 — Regressions covered with CTest for shared scene adapters
- Commits: `a1694952` (`tests/editor/scene_entity_adapter_tests.cpp`), `40938b8b` (root `CMakeLists.txt`), `ff98fbde` (`apps/editor/EditorSceneNodes.h`).
- Added `aiverse_editor_scene_adapter_tests` / CTest `editor_scene_adapter`, exercising RTS and FPS authored scene enumeration, source IDs, Transform read/write, invalid RTS Y edits, FPS collider/spawn refs, deduplicated group movement, and no-partial-mutation validation.
- These are regression tests of **document scene adapters**, not end-to-end tests of FPS runtime ECS, Metal rendering, live RTS ECS or Inspector ImGui. Linking uses `aiverse_editor_core`, available for builds with editor/tests.
- Verification: test and CMake entries committed, test command **NOT RUN**; macOS build still required. Run `cmake -S . -B build-metal` only if CMake does not automatically regenerate, then `cmake --build build-metal --target aiverse_editor_scene_adapter_tests -j 8 && ctest --test-dir build-metal -R '^editor_scene_adapter$' --output-on-failure`.
- Next: resolve any compiler/test failure and continue universal component provider, scene interaction, and outline migration.

### 2026-11-17 — Shared RTS/FPS authored ray picking and regression coverage
- Commits: `31b202f8` (`apps/editor/Editor.cpp`), `46a0ba35` (`tests/editor/scene_picking_tests.cpp`), `0bdf1403` (root `CMakeLists.txt`).
- RTS authored objects' first-pass 3D ray/AABB intersection now creates generic `SceneRenderable` picking proxies and calls `pickSceneRenderable` — the same closest-hit helper as FPS. RTS source still computes its own bounds from its type/radius/terrain; planetary and visual screen-space/ground fallbacks remain for compatibility.
- Added `editor_scene_picking` CTest to exercise closest-hit ordering, parallel rays, degenerate/NaN ray rejection, non-pickable and invalid bounds. This tests the common picking geometry but not full Scene mouse event/UI pipeline.
- Verification: code review and GitHub commits; **macOS compile/tests NOT RUN**. Check with `cmake --build build-metal --target aiverse_editor_scene_picking_tests -j 8 && ctest --test-dir build-metal -R editor_scene_picking --output-on-failure` after CMake regenerate.
- Outstanding: RTS planet hit-test is specialized, runtime live ECS entity picking is separate, and FPS authored colliders/spawns remain document-backed. Single universal runtime entity/component provider across genre modules remains a large open architectural task. Render extraction (`RtsView` vs FPS), 3D gizmo and mask-based post-effect outline remain unfinished.

### 2026-11-17 — FPS multi-selection parity with RTS authored object dragging
- Commits: `89d50c96` (`apps/editor/EditorFps.cpp`), `39a2034f` (`apps/editor/windows/SceneWindow.cpp`).
- FPS Scene/Hierarchy now highlight **all** selected colliders/spawns from `selectionSet` instead of only the primary item; `moveSelectedFpsSceneObject()` now batches all selected FPS document sources through `translateDocumentEntities()` with one revision change and prevalidation.
- FPS Scene drag preserves a multi-selection when starting a drag on an already-selected member; clicking without moving selects only that member on release. Ctrl/Cmd/Shift selection stays routed through the shared editor `Selection` API.
- Validation: source edits committed, **macOS compile/UI tests NOT RUN**. Manual FPS regression checks: Shift-click mixed collider/spawn, drag group, Undo once, click member without dragging, confirm all selections/labels and no duplicate movement.
- Remaining: generic gizmo handles, snap/rotate/scale with schema-backed persistence, full FPS runtime ECS provider, post-process geometry silhouette and shared RTS/FPS world hierarchy.

### 2026-11-17 — Shared Move gizmo axis selection (RTS X/Z; FPS X/Y/Z)
- Commits: `282ec92e` (`apps/editor/EditorTransformGizmo.h`), `ef7356f1` and `0f34a491` (`apps/editor/windows/SceneWindow.cpp`), `c13a1508` and `e9b73dbf` (`apps/editor/EditorFps.cpp`).
- Added `pickMoveGizmoAxis()`: one screen-space projected-segment picker for all 3 world axes, constrained via mask by what the author's schema can persist. RTS now uses it for X/Z; FPS now uses it for X/Y/Z. FPS visible axes and hit tests both use the actual source's `readDocumentTransform` pivot; group gizmo remains on primary selected source.
- FPS clicking a move handle preserves the selection while dragging; X/Z constrained movement uses ground ray differences, Y uses vertical screen delta, and batch translation is shared with RTS. Snap, local/world gizmo axes, Rotate/Scale, true geometry outline and runtime component writeback remain pending.
- Verification: source reviewed and committed; **no successful macOS build, automated UI test or interactive gizmo test yet**. User should test Y handle, perspective/orthographic camera pitches, group translation/Undo and handle occlusion.

### 2026-11-17 — Move gizmo projected-handle regression test
- Commits: `92dc34c3` (`tests/editor/scene_gizmo_tests.cpp`), `832d2d64` (`CMakeLists.txt`).
- Registered CTest `editor_scene_gizmo`, checking projected axis handle hit selection, missing/disabled axes, invalid extent and offscreen cursor rejection. This exercises common mathematical gizmo picking, not full mouse gesture/Undo.
- Verification: committed; **NOT COMPILED/RUN**. Build with `cmake --build build-metal --target aiverse_editor_scene_gizmo_tests -j 8` and run `ctest --test-dir build-metal -R editor_scene_gizmo --output-on-failure`.

### 2026-11-17 — Static compile review and consistent map display names
- Commits: `e328df1d` (`apps/editor/EditorFps.cpp` adds explicit `<algorithm>` dependency for FPS multi-selection) and `3aaa41f8` (`apps/editor/core/WorldInspect.cpp`).
- World Hierarchy naming now respects `DocEntity.extra.displayName` when no explicit ECS script label is present. The display name remains authored metadata, not a fabricated ECS component.
- Attempted to reach GitHub from the model's isolated build container, but the container could not resolve `github.com`; no repository checkout or compilation was possible there. GitHub connector changes were reviewed statically.
- **Validation not completed:** full CMake/editor compile and all new CTest cases are still unverified by execution. User should pull and run `cmake --build build-metal --target aiverse_editor aiverse_editor_scene_adapter_tests aiverse_editor_scene_picking_tests aiverse_editor_scene_gizmo_tests -j 8` followed by `ctest --test-dir build-metal -R 'editor_scene_' --output-on-failure`. Check latest GitHub Actions independently (earlier runs were queued).
- **Not yet universal:** FPS uses external-client runtime and `fps::State::actors` (a World resource, not ECS entities), FPS authored colliders/spawns are JSON; RTS uses `World` ECS plus `RtsView`. Editor runtime entity inspection, hierarchy and render extraction still differ; Rotate/Scale, native writeback, true screen-space object-mask outline and end-to-end macOS UI validation are missing. Do NOT mark the PR done.

### 2026-11-17 — Ensure editor regression tests run in Release configurations
- Commits: `06de5e88`, `8adaa963`, `f273b398` (the three `tests/editor/scene_*_tests.cpp` test files).
- Replaced `assert(...)` with an always-active `require(...)` helper. On Release/NDEBUG builds, plain assert was compiled out, causing tests to falsely pass and skip side-effectful setup. Failed checks now call `std::abort()` regardless of build type.
- Validation: GitHub source edits committed; **CTest run still outstanding**.

### 2026-11-18 — FPS runtime resource actor provider (read-only)
- Commits: `ae877c37` (`Editor.h`), `95233127` and `9c9b2c25` and `a09febee` (`EditorFps.cpp`), `292bfc80` (`HierarchyWindow.cpp`), `ebf39145` (`DetailsPanel.cpp`), `c48d6fc5` (`InspectorWindow.cpp`), `693826b1` (`SceneRenderable.h`), `0272aadc` (`EditorWorld.cpp`).
- Important architectural fact: FPS actors are stored in `fps::State::actors` (World resource), NOT individual ECS entity pools. Added `Editor::fpsRuntimeSnapshot()` which runs actual `fps::Module::setup`, reads actor state from its World resource, and caches by document revision, project/map path and rules revision; errors are surfaced rather than replaced with fake components.
- FPS Hierarchy shows `Runtime Actors` after authored colliders/spawns. Shared DetailsPanel supports `Selection::FpsActor`, displaying actual runtime actor fields **read-only**; Scene draws selectable markers through `SceneRenderable::runtimeActor` without reusing FPS document source IDs. FPS actor selections can be made by Hierarchy or Scene picker; no writable snapshot-to-world storage contract exists yet.
- No generic structural ECS conversion was performed on `fps::State::actors`; reworking FPS deterministic netcode, snapshots and rollback requires a separate migration. Runtime snapshot is a provider/bridge, not per-actor ECS reflection.
- Verification: GitHub sources committed, **CMake build, tests and runtime scene check NOT RUN**. Check `fpsRuntimeSnapshot` with valid/invalid FPS map, rules change, map switching, runtime actor markers/selection; ensure expensive setup is cached and not called each frame.
- Further work: shared runtime provider interface for RTS ECS and FPS resource records; unified runtime Hierarchy/Inspector field adapters, persisted native ECS editing, Rotate/Scale, geometry mask-based post outline. Do NOT mark migration complete.

### 2026-11-18 — Scene post-effect selection outline (first integrated implementation, unverified)
- Commits: `d9f520d0` (`engine/render/Renderer3D.h`), `0f04b230` (`Renderer3DModels.cpp`), `6cde0a13` (`SelectionOutlineShaders.h`), `e18576d3`, `2e9fd85a` (`Renderer3D.cpp`), `d9c6577f` (`apps/editor/Editor.cpp`), `371d9ba9` (`apps/editor/SceneRenderable.h`).
- Scene now requests `setSceneSelectionOutline(true)` only around the editor Scene rendering; Game keeps the previous legacy renderer behavior. Renderer3D stores per-primitive selection tint and uses model `ModelDraw.outline` as a selection flag, but suppresses the legacy inverted-hull `renderModels(3)` phase in normal Scene color rendering.
- Added a separate RGBA8 selection mask pass that reuses the scene depth, disables depth clearing, renders selected model geometry via the existing model pipeline with **zero normal extrusion**, renders primitive selection geometry and composites **exterior mask edges** with a 2-pixel full-screen shader (GLSL + MSL). Main color/depth passes are resumed with load/no-clear. Selected models' materials and all submeshes are expected to contribute to one combined mask, not individual model wire edges.
- IMPORTANT: this change is **NOT confirmed working**. Shaders, Metal/GL pipelines, mask depth compatibility, HDR postfx interaction, alpha-masked/transparent geometry, occlusion, and final screenshot quality must be checked on macOS and other render backends. The mask is Scene-only; no X-Ray mode or configurable thickness yet. If the shader fails to initialize, editor selection outline can be absent.
- Validation: GitHub source edits and static inspection only. **CMake/Metal compilation and image tests NOT RUN**. Do not merge until green. Test representative Sedan with separate wheels, body and glass, overlapping geometry, distant camera, FPS primitives, transparent materials and custom PostFx enabled. Also ensure Game view unchanged and no extra full-screen pass when selection is absent.

### 2026-11-18 — Schema-backed FPS oriented box Rotation and Scale (Inspector + Scene Gizmo)
- Commits: `6a529bb8`, `7dd68317`, `5e3cef36` (`apps/editor/EditorEntityAdapter.h`); `badc909b`, `1655a2de` (`apps/editor/EditorFps.cpp`); `09284a25` (`apps/editor/EditorComponentPanel.h`); `daf3f07c` (`tests/editor/scene_oriented_collider_tests.cpp`); `1bc7cd5a` (root `CMakeLists.txt`); `8991f4ab` (`Editor.h`); `532ab6c1` (`EditorTransformGizmo.h`); `7d48e766`, `b2592f65`, `8c3e264d` (`SceneWindow.cpp`).
- FPS already supports real oriented colliders in its runtime schema `{id,center,half,yaw}`, with yaw in turns. Shared document adapter now reads/writes their true center, validates half extents and yaw, translates in multi-entity batch and exposes `Position XYZ`, `Rotation Y` (degrees UI) and `Scale XYZ` (twice half extents) only when present. The object remains in its original FPS map format; it is NOT an ECS component.
- FPS scene now draws an actual rotated oriented-box primitive and computes conservative world AABB for picking. The selected oriented collider gains Move, Rotate and Scale toolbar modes in Scene: projected yaw ring hit-testing and drag updates yaw; scale axes drag updates corresponding half extents. All edits use MapDoc undo and trigger preview rebuild. RTS and plain FPS colliders do not fabricate unsupported rotation/scale controls.
- Added `editor_oriented_collider` CTest for authentic oriented collider position, rotation, scale, duplicate batch movement, invalid nonpositive dimensions and retained collider id.
- These Rotate/Scale gizmos are **schema-specific initial implementations**, not full universal 3D rotation/scale for arbitrary ECS component types, world/local axes or multi-selection rotation/scale.
- Validation: GitHub source and test entries committed; **compilation, CTest and macOS visual verification NOT RUN**. Test selection ring handling at top-down/side camera angles, ensure rigid-box yaw matches runtime `orientedBoxTriangles`, confirm Undo/Redo/save/load. Fix any build errors before merge.

### 2026-11-18 — Native ECS field persistence hooks and type-aware default editors
- Commits: `a17de986` (`modules/rts/RtsNative.h`), `17a708cf` (`modules/rts/RtsNative.cpp`), `03bce05d` (`apps/editor/EditorEcsWriteback.h`), `bd39cb3b` (`apps/editor/DetailsPanel.cpp`).
- Loaded native module instances may implement `inspectWriteback(componentId, fieldId, value, currentExtra, updatedExtra)` to validate and map an **actual ECS field** back to authored `DocEntity.extra`. The editor commits the updated JSON with `MapDoc::touched()` and uses `MapDoc::beginEdit()` at ImGui control activation for Undo. No editor-owned arbitrary in-memory World writes or unsupported generic persistence are performed.
- `componentSchemas[componentId].fields[fieldId]` can opt into `editable:true` and supply a type (`integer`, scalar number, bool, string, numeric Vec3); the DetailsPanel draws a typed control and calls the loaded module's writer. Unknown/native-unregistered fields remain read-only, preserving safety when schemas change.
- Crucial contract: module writer must ensure changed authoring extras are consumed at next Simulation::setup to reconstruct the ECS component; a temporary runtime state is never treated as persistent. Only statically linked and actually loaded project modules can supply this callback. Build/integration/Undo behavior are NOT verified yet; require tests and documentation example before merge.

### 2026-11-18 — RTS facing and schema-backed Rotation Y
- Commits: `770832db` (`EditorEntityAdapter.h`), `e27c945f` (`EditorComponentPanel.h`), `53627d74` (`DetailsPanel.cpp`), `e0db4e10`, `fdde9a58`, `edb0b219`, `bcf54661` (`SceneWindow.cpp`), `eec6c1ab` (`tests/editor/scene_rotation_tests.cpp`), `4cafb432` (`CMakeLists.txt`).
- RTS game setup consumes `DocEntity.extra.facing` (degrees) to construct the real ECS `Unit.facing` direction vector. Shared Transform adapter now exposes optional Rotation Y only if the authoring facing property really exists, and persists edits back to that property. The real `Unit.facing` Inspector field renders an editable angle mapped through this authoring source rather than modifying an ephemeral ECS snapshot.
- Shared Scene Transform toolbar now offers Rotation for RTS entities with real facing; projected rotation-ring picking and mouse delta edit the same field with Undo. RTS Move shows only X/Z handles, not fake Y. RTS still has no per-object Scale in `DocEntity`; it is deliberately unavailable.
- Added CTest `editor_scene_rotation` for actual facing preservation, absent-field rejection and yaw ring picking. **Neither macOS build nor test run is verified**.

### 2026-11-18 — Source-level structural integrity check
- Fetched 18 affected C++ headers/translation units from PR branch and ran a lexical brace-balance pass that ignores C++ comments, ordinary string literals and character literals. All reported balanced braces with no premature namespace closing. This is **NOT a compilation, linkage, shader validation, CTest pass or runtime quality confirmation**; it only catches structural mistakes such as the earlier missing namespace brace.
- Latest confirmed GitHub Actions for the handoff revision were queued/pending. Local isolated container cannot resolve github.com for a checkout, so a full macOS/Metal build and visual testing require a project runner/user machine.
- Before merging: compile `aiverse_editor`, run `editor_scene_adapter`, `editor_scene_picking`, `editor_scene_gizmo`, `editor_oriented_collider`, `editor_scene_rotation` and platform suites; visually test selected Sedan (multiple submeshes) outline vs Game tab, transparent/alpha-masked meshes, FPS authored actor preview and oriented collider Rotate/Scale with Undo/Redo. The new modules should not be described as production-verified until these tests pass.

### 2026-11-18 — Common runtime entity view for RTS ECS rows and FPS World resource actors
- Commits: `12339ea7` and `d11b3ba8` (`apps/editor/EditorRuntimeEntityProvider.h`), `472297e5` (`HierarchyWindow.cpp`), `ce328c07` (`EditorFps.cpp`), `b48fbcc7` (`DetailsPanel.cpp`), `c4a4ac13` (`EditorFps.cpp` FPS snapshot refresh during drag), `247756f5` (`EditorEcsWriteback.h` include hygiene).
- `enumerateEditorRuntimeEntities(Editor&)` returns a common editor-facing `EditorRuntimeEntity` record for either genuine RTS ECS WorldRows (selection, name, world position) or FPS actors stored in World `fps::State` resource (selection, name, snapshot fields, world position). The source explicitly marks which entries represent actual ECS entities: an FPS actor record does NOT become a fake ECS `Entity`.
- FPS Hierarchy, Scene runtime actor markers, and the common Inspector now use this same provider rather than independently parsing the actor array. The existing RTS Hierarchy is still a richer grouped WorldModel renderer; provider unification is a data API, not yet a complete replacement for its specialized grouping UI.
- FPS actor preview creation is cached and drag-time rebuild is deferred until mouse release, preventing per-frame `Simulation::setup()` while moving colliders or spawn points. Cache also invalidates on project/map change and rules revision.
- Verification: GitHub source edits committed, **build/tests/UI NOT RUN**. Check real FPS project map actor creation, failed FPS setup, missing modules, selection from both Scene and Hierarchy, cursor focus and no repeated expensive preview setups.
- Remaining architecture: RTS rich Scene extraction is in `RtsView`, FPS uses primitive renderables; these are valid genre extractors into common Renderer3D but not a single `SceneRenderable` submission layer for every animated model. Generic native ECS writeback relies on real per-module persistence hooks and is not automatically available for arbitrary fields. Runtime FPS per-actor ECS pool migration would affect authoritative FPS save/rollback and has NOT been performed.

### 2026-11-18 — Final source integrity audit for current integration batch
- Commit `03687cd2` (`SceneWindow.cpp`): automatically resets unsupported Rotate/Scale mode to Move when changing selected entity type (e.g. FPS oriented collider → spawn, RTS entity without facing).
- Checked brace/namespace nesting in 13 updated C++ headers and source files by tokenizing ordinary comments/string literals, counting braces and testing for premature closing braces. All 13 were structurally balanced; this is **NOT a C++ compile or GPU shader test**.
- CI for earlier commits remains queued/pending. There is no confirmed passing PR build, CTest run, or visual verification for this integration batch.
- Current remaining hard limitations: FPS actors still live in an authoritative World **resource**, not per-actor ECS pools (full migration requires save/rollback/netcode redesign); RTS rich World Hierarchy is separate internally from FPS document grouping although FPS actors and RTS rows have a common view API; generic native component editing requires opt-in persistence hooks from the project's actual modules; game genres may have no persisted Rotation/Scale; model+primitive mask outline must be tested against hardware backends and transparent materials.
- Do NOT mark PR fully finished/merge until the user runs a successful build, all editor_scene_* and editor_oriented_collider CTests, and Scene visual checks. Follow-up agent should fix any errors found by the build before extending the renderer further.

### 2026-11-18 — Review and harden new screen-space selection outline
- Commits: `432f4a9d` (`engine/render/Renderer3D.h`), `c517f4d2` (`engine/render/Renderer3D.cpp`), `37c73832` (`engine/render/Renderer3DModels.cpp`), `2fdabbd3` (`engine/render/ModelShaders.h`).
- Inspected the branch's new `renderSelectionOutline` implementation. Selected-model primitives render a color mask against the current scene depth, then `SelectionOutlineShaders.h` composites a screen-space exterior border. It explicitly sets zero normal displacement for the mask (legacy game inverted-hull remains separate).
- Fixed mask primitive preview rendering to use a dedicated **depth-test ON, depth-write OFF** pipeline; the former reuse of `litPipe_` could overwrite scene depth while writing the selection mask, corrupting later depth-based visibility.
- The mask pass also now requests alpha-tested model shaders for `AlphaMode::Mask`; GLSL and Metal OUTLINE branches discard cutout fragments when generating the selection mask. This preserves transparent holes on fences/foliage and avoids painting their bounding hull.
- Verification: code inspection and GitHub commits only; **NO Metal/macOS build, shader compilation or screenshot confirmation**. Test selection/hover on whole multi-mesh Sedan, depth-occluded objects, cutout materials, FPS collider primitives, HDR PostFx on/off, and resize/recreate GPU resources.
- Remaining: explicit X-ray mode is not wired; visual QA, mask sampler orientation across Metal/GL, and robustness against shader compilation failure still need validation. Never mark outline finished until those pass.

### 2026-11-18 — Reuse custom field/component drawers for actual ECS Inspector cards
- Commit `7752488d` (`apps/editor/DetailsPanel.cpp`): the existing `InspectorDrawerRegistry` now also overrides real `WorldInspect`-derived ECS component cards and individual fields. Registration by true component ID or `Component.field` path; the fallback remains the generic schema/JSON view.
- Overrides are available **only for a single authored map-backed ECS entity in edit mode**; runtime entities, multi-selection, stats/rules sections remain guarded/read-only. Document source is an editing persistence target, not used to fake ECS component presence. Project-specific trusted C++ drawers must handle their own validation and undo semantics.
- Not an exported stable C ABI or a hot-reloadable editor plugin: this is an in-process C++ drawer registry. Native gameplay modules should expose data via `NativeModule::inspectComponents`, and use a project/editor bridge for ImGui-specific custom drawers if needed.
- Verification: source-level review and commit only; macOS build and runtime registration tests **NOT RUN**. Test component override, per-field override, default fallback, project change and undo.

### 2026-11-18 — Regression test for selection-mask GPU pipeline depth safety
- Commit `6d5f1f68` (`tests/tests.cpp`): added `selection_outline_mask_pipeline_preserves_scene_depth` using existing `entityfxtest::RecordingDevice`.
- The test inspects the pipeline created by `Renderer3D::initSelectionOutline()` and asserts it is RGBA8, color-writing, depth-tested, **depth-write disabled**. It is part of the existing `aiverse_tests` executable, so it runs through the standard CTest suite when executed.
- Source-level checks were performed, but **the test and macOS editor build have NOT been executed** in this environment. This test validates a pipeline descriptor, not Metal shader compilation, mask occlusion, or final visual quality.

### 2026-11-18 — FPS Hierarchy interaction parity with universal selection
- Commit `d9de10a5` (`apps/editor/windows/HierarchyWindow.cpp`): FPS document nodes and runtime actor rows now use the same `Selection` modifier handling as RTS (Cmd/Ctrl toggles, Shift adds, unmodified selects), common `ed.isSelected` highlighting, a search field over node/group labels, and double-click camera focus.
- This removes FPS-only `selectFpsSceneSource` branching from the Hierarchy click path without fabricating runtime ECS components. It keeps the RTS rich world-tree implementation intact to avoid losing its owner/type grouping, hide/lock, drag/drop and keyboard functionality.
- Verification: source-level inspection only; macOS compilation and UI tests **NOT RUN**. Check mixed FPS collider/spawn selection, Ctrl/Shift, search, double-click and runtime actor selection. A true shared visual row tree and unified backend provider across all genres are still open.

### 2026-11-18 — FPS real ECS entity migration, compatibility stage 1
- User requested moving ALL RTS/FPS runtime data to ECS. This stage starts with FPS without changing deterministic gameplay, network framing, or published save v9.
- Commits: `0b3ff8be` (`modules/fps/FpsModule.h`) and `1e885bdd` (`modules/fps/FpsModule.cpp`) add actual `World::Entity` identities and ECS component pools `fps::ActorEntityIdentity`, `fps::ActorEntityTransform`, `fps::ActorEntityVitals`, plus `ActorEntityIndex` (slot→Entity resource). Every FPS player is assigned a stable ECS Entity at `fps::Module::setup`. `fps.ecsMirror` runs after FPS tick and updates components, including when match is over. Save loading rehydrates ECS values; rollback snapshots clone ECS pools with the World.
- Critical compatibility: legacy `State::actors` is still authoritative and unchanged for gameplay, lag compensation (`HistoryFrame`), network packets and save v9. ECS components are a **real but derived** world view, not yet a source-of-truth migration. Never imply the gameplay uses ECS pools exclusively. No hash/checksum format changes were made.
- Old FPS save allocator compatibility: `e4e7db20` recreates missing actor ECS identities when a save predating these pools loads. `7f0f2167` extends the tests to exercise a pre-ECS empty allocator. Validation still pending real execution.
- Editor: `f0c084ac` (`apps/editor/EditorFps.cpp`) reads real ActorIdentity/Transform/Vitals from the runtime ECS World, placing each in the `components` snapshot rather than copying from `State::actors`. `d5de5c36` (`DetailsPanel.cpp`) draws these ECS component cards for FPS actors. Tests `3e8c073f` (`tests/fps/tests.cpp`) validate identity, pose, vitals, ticks, save/load, and `SimSnapshot` rollback.
- **Verification: source changes committed; compilation, FPS tests, network tests and UI not run in this session.** Run `cmake --build build-metal --target aiverse_fps_tests aiverse_editor -j 8`; `ctest --test-dir build-metal -R '^fps$|^fps_transport$|editor_scene_' --output-on-failure`. For gameplay parity compare baseline deterministic match checksums and old save v9 load tests.

### 2026-11-18 — FPS ECS runtime projectile/loot/vehicle identity migration
- Commits: `eca53c76`, `a040cf45`, `efaf4f7a`, `31fa4cf2` (`modules/fps/FpsModule.h/.cpp`). New real ECS components `ProjectileEntity`, `LootEntity`, `VehicleEntity`, with `DynamicEntityIndex` mapping stable kind/id keys to real entities. A deterministic post-tick sync creates/updates/destroys these ECS entities to mirror objects in `State`. Load/save and rollback preserve/recreate their ECS views.
- `934f4a4b` / `b259cda5` add FPS test coverage for creation, field values, removal and rollback of these dynamic ECS entities.
- Editor integration: `17fee21e` (`EditorFps.cpp`) gathers actual World ECS dynamic pools into runtime component snapshots; `9229761b` (`EditorRuntimeEntityProvider.h`) exposes them through the shared runtime provider; `51239e6a` (`SceneRenderable.h`), `4b90970c` (`EditorFps.cpp`), `0a425a74` (`EditorWorld.cpp`) add scene visualization, selection and ray picking; `15c777a5` / `ea05bb95` (`Editor.h`, `EditorWorld.cpp`) add stable `FpsRuntimeObject` selection and position lookup; `16cffefb` / `eb7f4776` (`DetailsPanel.cpp`, `InspectorWindow.cpp`) show real FPS object ECS cards; `0344eac4` / `b705ae80` (`HierarchyWindow.cpp`) group projectiles, loot and vehicles with 64-bit stable IDs; `5df30868` updates provider comments.
- **Important:** all three dynamic object arrays still live in `fps::State` as the authoritative simulation representation. The new ECS entities are real component pools with deterministic IDs but are a **compatibility mirror**. FPS authored JSON colliders/spawns remain map authoring sources, physics collision remains `SpatialWorld`, and FPS combat systems still update State rather than querying/mutating ECS pools. RTT/FPS whole-game migration is NOT complete.
- This staged approach avoids immediately breaking published FPS save v9, rewind history, prediction/replication and deterministic checksums. The follow-up required for full ECS authority is to refactor FPS gameplay systems to read/update the ECS pools as the source of truth, with a versioned save/network migration and exhaustive parity tests.
- **Verification: none of the new stage has compiled or been run on Metal in this session.** Compile `aiverse_fps_tests`, `aiverse_editor` and `aiverse_fps_transport_tests`, run CTest, and check visible ECS component names and actor/projectile/loot/vehicle scene selection. Never merge PR before passing tests.

### 2026-11-18 — Legacy FPS allocator restoration safety
- Commits `2b518d10` (`modules/fps/FpsModule.cpp`) and `4a08475f` (`tests/fps/tests.cpp`).
- `World::restoreEntityState` restores allocator generation/alive/free vectors **without clearing component pools**. Before this fix, loading a pre-ECS save could recycle a component-pool slot for a different derived FPS object, yielding incorrect mixed Actor/Projectile/Loot/Vehicle component membership.
- `syncActorEntities` removes stale dynamic FPS components on actor identities; `syncDynamicEntities` removes stale actor and wrong dynamic-kind components before assigning a fresh object's type. A new test simulates the legacy empty allocator and checks every dynamic ECS entity has only its correct component kind.
- This preserves game state and network save v9 while maintaining true ECS pool membership. **Build/tests not executed**. Particularly verify legacy save loading with map-initial loot/vehicles and full save↔reload across active projectiles.

### 2026-11-18 — Import ECS 2.0 source branch b75b873 into universal Editor PR #7
- User explicitly requested importing `feature/ecs-architecture-benchmarks` at `b75b873b1b2091a4a4a9bb386d39f3237f5d2585` into `feature/universal-entity-inspector-fps`, preserving ongoing editor/FPS migration.
- Source and target were divergent (185 source-only and 304 target-only commits at inspection); naive branch overwrite would discard editor ECS migration.
- **Core commit `f4cf94d1`**: transferred 42 source blob-exact files including `engine/core/ECS.h`, `engine/sim/{CommonComponents,Simulation,SceneHierarchy,SceneHierarchySystem}`, `engine/net/Rollback.*`, experimental scheduler/guarded world/snapshot code, render hierarchy adapter, benchmark/correctness tests/scripts, GitHub ECS workflow, and the two ECS docs. Used base-target Git tree and lease-checked branch update, not a replace of entire source branch.
- **Build integration `5866850a`**: wired opt-in ECS CTest suites, experimental RTS preprocessing flag (OFF), and COW rollback session test into our existing `CMakeLists.txt` without overwriting editor-specific CMake changes.
- **Overlap resolution commits `3cf9aee4`, `48fae9dd`, `a272d1c4`**: manually merged the source FPS experimental parallel-motor (speculative gather + canonical sequential commit, OFF by default) into our FPS actor ECS mirror code, retaining `syncActorEntities`, `syncDynamicEntities`, current authoritative `State`, save v9/rollback, and existing Editor runtime provider. Counters and opt-in CMake flag preserved.
- **Additional source ports `09644bf5` (RTS `RtsUnits.cpp`), `271ed678` (headless COW flags), `7d5876cf` (FPS tests incl. COW/motor parity)**.
- **Editor conflict resolution `5aacafd3`**: registered real `Parent`, `LocalTransform`, `WorldTransform` component dumps in the existing `WorldInspect.cpp` registry, preserving native module inspection and universal editor changes.
- **Agents guidance `b2b874e6`**: integrated ECS 2.0 coordination / determinism restrictions into existing `AGENTS.md`.
- **Audit:** among the 51 ECS-source-changed files, 44 are blob-identical to source commit and 7 are intentional merged versions: `AGENTS.md`, root and FPS `CMakeLists.txt`, `WorldInspect.cpp`, `FpsModule.cpp/.h`, `tests/fps/tests.cpp`. None omitted.
- **Verification: NOT COMPILED OR TESTED by this assistant.** Model container cannot resolve github.com for local git fetch; source ECS handoff warns the most recent COW/motor/hierarchy changes have not passed latest native Mac tests, despite prior older suites passing. Keep experimental flags OFF; do not merge main until Mac editor/FPS/RTS build, CTest/normal+sanitizer ECS suites, and gameplay parity comparison pass.
- **Commands:** `cmake -S . -B build-metal` (to refresh new test targets); `cmake --build build-metal -j 8`; `ctest --test-dir build-metal -R 'ecs_|fps|aiverse_tests|editor_scene_' --output-on-failure`; `bash tests/run_ecs_experiment_suite.sh` and `bash tests/run_ecs_gameplay_compare.sh` (see script help for args). Build with `-DAIVERSE_FPS_EXPERIMENTAL_PARALLEL_MOTOR=ON` and `-DAIVERSE_ECS_EXPERIMENTAL_RTS_TRANSFORM_JOBS=ON` only for separate parity runs.

### 2026-11-18 — FPS ECS authority migration: stage A (declaration)
- User explicitly requested continuing FPS runtime ECS migration on PR #7, with handoff updated **after every implementation action**. Keep PR #7 target branch `feature/universal-entity-inspector-fps` and preserve save v9/lockstep.
- Commit `b11fa4f`: `modules/fps/FpsModule.h` defines `ActorRuntime { Actor value; }` as the full gameplay payload for an ECS Entity; adds opt-in `Config::ecsAuthority=false`, `Module::actorEcsAuthority()`, and `hydrateActorsFromEcs` declaration. This stage declares the interface **only**, it is not active yet; no gameplay behavior change.
- Next: wire parsing, setup pool, command writes, tick hydrate/commit, save load rollback rehydration and exact same-hash parity tests. Old actor inspector components remain projections.
- **Verification:** not compiled/tested. No changes to main.

### 2026-11-18 — FPS ECS authority migration: stage B (command + tick bridge)
- Commit `e1a5da2` (`modules/fps/FpsModule.cpp`): parse `fps.ecsAuthority` (OFF by default). `syncActorEntities` now creates/updates full `ActorRuntime` components when enabled, while preserving Editor ActorIdentity/Transform/Vitals projections. `applyCommand` validates and writes the full ECS ActorRuntime first (then updates compatibility vector for callers). New deterministic system `fps.ecsHydrate` reads canonical ActorRuntime into the legacy contiguous actor working vector immediately before `fps` tick; `fps.ecsMirror` commits the tick results back to ECS afterwards.
- **Precise migration status:** canonical Actor data lives in ECS **between ticks**, and commands mutate the ECS component, but existing motor/combat/AI algorithms still access the `State::actors` tick workspace. This is a staged ECS authority bridge, **not** full direct component-based gameplay yet. Do not describe it as complete migration.
- Legacy `State` save v9, history/replay, network snapshots, editor runtime views remain unchanged with opt-in OFF. Deserialization already calls syncActorEntities to rebuild ECS ActorRuntime from save v9; SimSnapshot includes both since stored in World.
- Next stage: consistency and rollback parity tests, then migrate one system at a time to query/mutate ActorRuntime directly, remove mirror dependency, then migrate other object types. Validate with `fps.ecsAuthority=true` side-by-side checksums against old mode.
- **Verification:** not built/run. Keep this commit isolated from main until tested.

### 2026-11-18 — FPS ECS authority migration: stage C (parity regression)
- Commit `01055b4` adds `ecsActorAuthorityParity` to `tests/fps/tests.cpp`, invoked by the existing `aiverse_fps_tests` runner. Runs old vector-authoritative vs `fps.ecsAuthority=true` under the same 160-tick command stream, compares authoritative ECS ActorRuntime full Actor payload with the legacy workspace, checks legacy v9 save/reload and SimSnapshot rollback continuation, and exercises direct ECS mutation across a tick barrier.
- **Important finding:** `Module::setup` originally hashes the entire rules JSON into `contentHash_`. The opt-in implementation flag would incorrectly change checksum/save compatibility even when gameplay is byte-identical. Next commit must exclude only `fps.ecsAuthority` from the gameplay content hash, while preserving all actual game rules. This is a deterministic compatibility fix.
- Validation pending; the new test has not run on Mac Studio or CI and will expose any state divergence.

### 2026-11-18 — FPS ECS authority migration: stage D (content hash parity)
- Commit `cf561b1` in `modules/fps/FpsModule.cpp`: `fps.ecsAuthority` is removed from a temporary rules JSON copy **only** before computing `contentHash_`. The actual rules object remains unchanged. The toggle is an implementation/storage choice and must not change legacy save v9 content checks or authoritative cross-worker/network hashes. All other game-rule fields are still hashed exactly as before.
- No changes to the binary save format, command protocol, or actual gameplay rules. `ecsActorAuthorityParity` from stage C should now compare identical checksums across authority modes.
- **Validation:** compile/execute tests pending. Next migration target is full ECS dynamic object ownership (projectile, loot, vehicle), preserving their original order explicitly before changing gameplay algorithms.

### 2026-11-18 — FPS ECS authority migration: stage E (dynamic payload schema)
- Commit `1cb6283` (`modules/fps/FpsModule.h`): declares full payload components `ProjectileRuntime{Projectile value, uint32_t order}`, `LootRuntime{Loot value, order}`, and `VehicleRuntime{Vehicle value, order}`. The order is essential because existing projectile/loot/vehicle vectors are deterministic and gameplay behavior depends on iteration order. Declares `hydrateDynamicsFromEcs`.
- This commit is **only API/schema**, not a running authority migration yet. Next implementation commit will integrate it into `syncDynamicEntities` and `fps.ecsHydrate`, preserving save v9 and existing inspector projection components.
- **Verification:** not compiled or tested.

### 2026-11-18 — FPS ECS authority migration: stage F (dynamic objects live)
- Commit `8883e0a` (`modules/fps/FpsModule.cpp`): when `fps.ecsAuthority=true`, `syncDynamicEntities` commits the **full** Projectile, Loot and Vehicle value into their matching ECS runtime payload component and stores the original deterministic `order`. `hydrateDynamicsFromEcs` reconstructs the legacy contiguous gameplay arrays from ECS runtime components immediately before the FPS tick, sorting by `order` with deterministic ID tie-break. ECS object **membership, values, and order are authoritative between ticks**; vector arrays are compatibility workspaces during the FPS tick.
- On recycled Entity indices the sync removes mismatched runtime component kinds along with former inspector components; stale vanished dynamic objects are destroyed in deterministic source-ID order. On save v9 load, the existing `syncActorEntities/syncDynamicEntities` creates all authoritative payloads again from the deserialized legacy state; SimSnapshot includes the ECS pools.
- Does **not** yet rewrite projectile collision/damage, loot pickup or vehicle simulation algorithms to operate directly on ECS pools; these continue using State arrays in the tick. Does not change default behavior or FPS save format. This is **stage F**, not final complete migration.
- **Verification:** compilation, CI, FPS deterministic checks, save/restore parity pending. Next add direct ECS dynamics mutation+rollback tests, then eliminate gameplay dependency on temporary arrays system by system.

### 2026-11-18 — FPS ECS authority migration: stage G (canonical save/hash interface)
- Commit `8ca50de`: declares `Module::authoritativeStateCopy(Simulation&)` to construct legacy save v9 / checksum state **from authoritative ECS ActorRuntime/ProjectileRuntime/LootRuntime/VehicleRuntime components**, while preserving non-entity round/history/meta data from `State`.
- This commit is declaration only; next action must implement and route `hashState` and `serializeState` through the materialized canonical snapshot under `fps.ecsAuthority`.
- **Verification:** not run. Do not turn the storage option on by default until paired runtime and save/load checks pass.

### 2026-11-18 — FPS ECS authority migration: stage H (canonical hash and save v9)
- Commit `117b4e9` in `modules/fps/FpsModule.cpp`: implemented `authoritativeStateCopy`. In `fps.ecsAuthority`, it reads full `ActorRuntime` by deterministic slot, and builds projectile/loot/vehicle vectors from **ECS runtime component pools** sorted by explicit source order (+ID tie break). Non-entity data (round, rewind history, gear/bot state, match, royale) comes from compatibility `State`.
- `Module::hashState` now hashes the materialized canonical ECS payloads in authority mode, preserving the old wire/save layout and previous checksum algorithm; `Module::serializeState` writes the canonical materialized values in the existing FPS save v9 format, and on read the prior `syncActorEntities/syncDynamicEntities` creates the ECS payloads.
- Important: direct mutation of an authoritative ECS entity between ticks now affects hash/save immediately, whereas the temporary legacy working vector is ignored for entity payloads. Direct mutation through State vectors in `fps.ecsAuthority` mode is no longer a reliable write path: use ECS runtime components or move the algorithm into an ECS system.
- FPS physics/combat/AI still access ordered `State` working vectors *during* the tick; full elimination of these buffers remains. This commits a testable ECS canonical boundary rather than pretending full FPS gameplay ECS rewrite is done.
- **Verification:** no native build or CI PASS yet. Next stage: expand parity tests for dynamic objects and direct ECS edits, then migrate a selected gameplay subsystem.

### 2026-11-18 — FPS ECS authority migration: stage I (legacy load ordering fix)
- Commit `381b297`: in `Module::serializeState`, materialize canonical Actor/Dynamic ECS payloads **only when Archive is writing**. When reading save v9, `Simulation::deserialize` restores the saved allocator *before* calling module deserializer; a pre-ECS save may contain zero actor entities at that point. Read from the old State workspace first, validate save, then rebuild ECS identities and authoritative runtime payloads via existing sync functions.
- This prevents falsely rejecting legacy saves due to missing authoritative ECS entities during deserialization. `checksum()` following successful read still reads canonical ECS payloads, matching the old legacy v9 hash format (storage-only flag excluded from contentHash).
- **Verification:** not compiled/run; specifically run pre-ECS allocator restore, FPS legacy save validation, and authoritative-mode cross-snapshot tests.

### 2026-11-18 — FPS ECS authority migration: stage J (dynamic parity and lifecycle)
- Commit `f7325bb` adds `ecsDynamicAuthorityParity` to `tests/fps/tests.cpp`, comparing legacy State objects with opt-in ECS-authoritative Projectile/Loot/Vehicle runtime payloads. The test creates stable ECS entities and indexed source IDs, checks canonical checksum immediately (before hydration), steps 8 ticks, directly edits authoritative ECS LootRuntime with immediate hash visibility, saves/reloads v9, deletes an ECS LootRuntime entity and checks the legacy workspace reflects that membership change, and rolls back a pre-removal SimSnapshot.
- Commit `345d809` replaces an invalid full `std::vector<Projectile>` equality check (Projectile has no `operator==`) with size and ID verification; other payload equality is enforced by full deterministic checksum already checked every tick.
- This is an **unverified test**, not proof that FPS gameplay now directly uses ECS in combat systems. Actor and dynamic ECS payloads are authoritative at tick barriers; gameplay uses State vectors as a compatibility workspace until later direct ECS system migration.
- Next: validate compilation/tests, add Inspector coverage for full runtime components, then migrate one combat/AI system at a time.

### 2026-11-18 — FPS ECS authority migration: stage K (Inspector runtime fields)
- Commit `a160612` in `apps/editor/EditorFps.cpp`: shared FPS Runtime snapshot now includes actual `ActorRuntime` fields (kills, shots, hits, weapon index, reload/fire timers, input sequence, vehicle, etc.) alongside legacy ActorIdentity/Transform/Vitals projections; projectile/loot/vehicle runtime payload component cards show source order and additional authoritative fields if present. The existing EditorRuntimeEntityProvider/Hierarchy stable Entity IDs and selection logic remain unchanged.
- UI is read-only; mutations must go through ECS-authoritative gameplay APIs rather than direct editor-side mutation of simulation memory. For projects without `fps.ecsAuthority`, full runtime components are absent and existing mirror panels continue to work.
- Verification: source code committed, no Metal editor visual test or native build yet.

### 2026-11-18 — FPS ECS authority migration: stage L (clear ownership contract)
- Commits `a3772e9` (`modules/fps/FpsModule.h`) and `cf37e04` (`apps/editor/EditorRuntimeEntityProvider.h`) correct stale comments that said `State::actors` is *always* the gameplay authority. With `fps.ecsAuthority=false` (default), that remains true. With `fps.ecsAuthority=true`, the full `ActorRuntime` / `ProjectileRuntime` / `LootRuntime` / `VehicleRuntime` components are authoritative **between ticks**, and `State` vectors are temporary ordered gameplay workspaces.
- Stable ECS IDs and lightweight ActorEntity* / dynamic *Entity projections remain dedicated to Editor/Inspector. Legacy save/network stays v9; `authoritativeStateCopy` marshals ECS data into the old v9 wire/save layout.
- Key next step: remove remaining `State` gameplay workspace dependency one subsystem at a time; avoid parallelizing actor interactions/damage without canonical gather/apply barriers. No native build/CI PASS reported for recent commits.

### 2026-11-18 — FPS ECS authority migration: stage M (direct motor declaration)
- Commit `4dc4a2f` declares `Module::moveActorEcs(Simulation&,Actor&,const Input&)` in `FpsModule.h`. It is a dedicated entry to move actor motor calculation **onto the actual writable ECS ActorRuntime component during a tick**, then write only changed motor fields back into ordered State workspace for cross-actor combat and networking compatibility.
- Declaration only in this commit. Implementation and collision/determinism tests must follow. No change to default FPS mode.

### 2026-11-18 — FPS ECS authority migration: stage N (first direct gameplay system)
- Commit `f9a254e` (`modules/fps/FpsModule.cpp`) implements `Module::moveActorEcs`: in `fps.ecsAuthority=true` and when the speculative parallel-motor shortcut is not taking over, the actual fixed-point `moveActor` algorithm runs on **writable `ActorRuntime::value` in the ECS pool**, not on `State::actors[i]`. Only motor input/output state (position, velocity, yaw/pitch, health/vehicle/stance flags) crosses the bridge; no expensive whole Actor bag/melee copies per move. The result's motor fields are reflected into `State::actors[i]` for later canonical-order combat, target selection, network projection and old save v9 algorithms.
- Compatibility: OFF mode continues calling legacy `moveActor` directly. With experimental parallel motor ON, precomputed actor results are still applied through the old buffer and committed to ECS at the end-of-tick; the direct motor method is used on the safe sequential fallback. This is **a real migrated gameplay subsystem**, but shooting/AI/loot/projectile/vehicle logic still use workspace arrays.
- Existing `ecsActorAuthorityParity` drives movement/jump/sprint and compares complete 160-tick authoritative checksum histories; full native compilation and sanitizer checks remain pending. Preserve original actor iteration and combat ordering.

### 2026-11-18 — FPS ECS authority migration: stage O (direct combat damage and scoring)
- Commit `e59b298` (`modules/fps/FpsSystems.cpp`): `Module::damage` now calculates authoritative damage directly into `ActorRuntime::value` for the target and (if distinct) attacker when `fps.ecsAuthority=true`. Pre-damage actor workspaces are copied into ECS to preserve same-tick interaction order, and results are immediately reflected back into legacy State actor slots for subsequent targeting, knockdown/team queries, FPS snapshots and net prediction.
- Preserves existing damage sequence: friendly fire, gear/armor absorption, action cancel, lastAttacker/weapon, hit counter, knockdown, death/kill/money, drops and SimEvents. Lambda wrapping preserves early-return cases while ensuring authoritative ECS results are committed to the temporary workspace.
- Default `ecsAuthority=false` still operates exclusively on legacy State. The centralized damage method is one **direct gameplay** ECS subsystem, alongside direct actor motor and command validation; other FPS systems still use tick workspace arrays.
- Verification: compile/firing parity tests and ASan not yet run. Next add a two-team damage/kill regression under both authority modes, check same-tick event ordering, and update read-only editor schema as needed.

### 2026-11-18 — Universal mechanics audit: build preflight
- User requires proving that RTS/FPS mechanics are reusable by **any genre**, not merely present in the same repository. Universal ECS primitives and pure policy libraries exist, but RTS/FPS module-specific business logic still has coupling; comprehensive genre-agnostic integration is not yet complete.
- Commit `0b8ad00` fixed three invalid `//` comments in root `CMakeLists.txt` near ECS test/RTS option blocks. CMake uses `#` for comments, so previous imported PR #7 configuration could fail to parse before tests could run. This is a syntax-level fix; **native CMake configuration not yet executed**.
- Next: add explicit independent cross-genre ECS policy consumer tests + reusable adapter API; audit genre-bound `RtsModule&`/ `fps::Actor` dependencies; report remaining blockers without claiming complete universality.

### 2026-11-18 — FPS ECS authority migration: stage P (direct damage parity regression)
- Commit `2413b7a` (`tests/fps/tests.cpp`) adds `ecsDamageAuthorityParity`. Creates identical two-team deathmatch simulations in default and `fps.ecsAuthority=true` mode, sends 30 ticks of deterministic fire, compares `Simulation::checksum` after each tick, target health, attacker hits/kills, and the complete `ActorRuntime::value` of both participants against the compatibility workspace. Explicitly verifies the victim dies and the attacker is credited with a kill.
- This tests the newly migrated direct ECS `Module::damage` and earlier direct ECS motor/command paths together, while retaining canonical save v9/network semantics. No tested performance advantage is claimed; legacy API compatibility remains.
- **Verification status:** code pushed but still uncompiled on Mac Studio; run FPS CTest, FPS network transport, and ECS normal/ASan suites before merging or changing the default flag.

### 2026-11-18 — FPS ECS authority migration: stage Q (native direct ECS spawn)
- Commit `58808d8` (`modules/fps/FpsModule.cpp`): `hydrateDynamicsFromEcs` now enumerates all live `ProjectileRuntime`, `LootRuntime`, and `VehicleRuntime` components at the authoritative pre-tick barrier, registers each typed source ID in `DynamicEntityIndex` (kind+ID -> real Entity), and rejects zero IDs, duplicate IDs per kind, or conflicting live mappings.
- Native projects can now create an ECS Entity and `world.add<LootRuntime>/add<ProjectileRuntime>/add<VehicleRuntime>` directly **without writing legacy State vectors or manually populating index.byKey**. Hydration reconstructs deterministic ordered workspaces; existing post-tick `syncDynamicEntities` populates lightweight Inspector/Hierarchy component projections on the **same Entity**, not a duplicate.
- Important: new ECS dynamic mutations are applied at the next tick barrier, not mid-tick. Authoritative direct ECS creation in projects should be done outside concurrent job waves. Existing legacy default mode unchanged.
- Tests must verify native runtime spawn without pre-registered index and duplicate-ID failure. Native compile/CI not yet confirmed.

### 2026-11-18 — Cross-genre audit: stage 1, ECS policy adapter
- Commit `cb9a43e`: `libraries/include/aiverselib/genre/EcsMechanicSystems.h` introduces genre-independent `eachEcsMechanic<Component>(World&,policy)` and `addEcsMechanicSystem<Component>(Simulation&,name,policy)`. Game code chooses an arbitrary ECS component type, binds its existing policy, and gets deterministic dense-order iteration and COW write preparation. No dependency on `RtsModule` or `fps::Module`.
- This is **an adapter, not universalization of all FPS/RTS functions**. Any mechanic requiring `RtsModule&`, `fps::Actor`, global RNG, actor ownership, collision or format-specific state must be extracted/refactored separately. No native compilation yet.
- Next: compile and run the same pure policies against two different GameModules via this common adapter; add coverage and an explicit capability matrix for each mechanic.

### 2026-11-18 — FPS ECS authority migration: stage R (direct ECS spawn regression)
- Commit `201b75e` modifies `ecsDynamicAuthorityParity` to create ProjectileRuntime, LootRuntime and VehicleRuntime directly on fresh ECS Entity handles **without** modifying `DynamicEntityIndex`; verifies the index initially has zero entries, and after one FPS tick that it contains the exact three original Entity handles by typed source ID. The remaining test still checks state checksums, save/restore, deletion and snapshot rollback.
- This specifically guards against an accidentally duplicated ECS entity for a native-created object, which would break Inspector/Hierarchy identity and deterministic object ownership.
- **Verification:** test code committed; no native compile/CI PASS yet. Next add error-path duplicate-ID coverage and audit save v9 old allocator behavior.

### 2026-11-18 — FPS ECS authority migration: stage S (save-crossmode and duplicate-ID rejection)
- Commit `2e8de0c` adds two negative/compatibility regressions to `tests/fps/tests.cpp`: `ecsDuplicateDynamicIdRejected` creates two LootRuntime ECS entities with identical typed ID and asserts `sim.step` rejects the ambiguity before tick advances; `ecsActorAuthorityParity` now writes legacy FPS save v9 in default storage mode, loads it into a fresh `fps.ecsAuthority=true` simulation, and compares checksums.
- This explicitly covers the storage-only toggle's unchanged `contentHash_`, save version and Entity rehydration semantics. The replay contract is still ordered by original slot / dynamic object's explicit source order.
- Tests remain **unverified by native compilation/CI**. If they fail, fix within PR #7 and document exact failure/commit before considering main merge.

### 2026-11-18 — Cross-genre audit: stage 2, executable contract
- Commit `6dc7a89`: added `tests/ecs_cross_genre_mechanics_test.cpp`. It runs **identical** reusable `stepMotion`, `stepRecoil`, `stepCapture` policies through `addEcsMechanicSystem<Component>` in two independent GameModule types with distinct ECS components (`ShooterCharacter`, `StrategySoldier`). Checks matching deterministic checksums, rollback replay, and optional page COW state.
- Scope: this proves generic C++ policies can be bound by independently authored game modules **once compiled and executed**. It does **not** prove that every existing RTS/FPS gameplay function can be moved to a new genre unchanged; those still refer to `RtsModule&`, `fps::Actor`, domain-specific state and configuration.
- New test is not yet wired into build/test runners; no native compilation or passing CI observed.

### 2026-11-18 — Cross-genre audit: stage 3, public header contract
- Commit `561bafa`: cross-genre test imports `<aiverselib/genre/EcsMechanicSystems.h>`, `Locomotion.h`, `Weapons.h`, `Match.h` via published `AIVERSE::libraries` include conventions instead of repository-internal relative include paths. This makes the test a useful native-project consumer contract.
- Build and execution still pending. Next action: register the test in root CMake and standalone ECS runner.

### 2026-11-18 — Cross-genre audit: stage 4, CMake consumer target
- Commit `620920b`: root `CMakeLists.txt` now adds `aiverse_ecs_cross_genre_mechanics_test` after `add_subdirectory(libraries)`. Links only `aiverse_engine` and public `AIVERSE::libraries`, deliberately **not** `aiverse_fps` or `aiverse_game`. Registered as `ecs_cross_genre_mechanics_test` in CTest. This verifies that gameplay policies and ECS adapter do not require either genre module.
- Build/CTest has not been run on native toolchain. The absence of genre linkage is a source/CMake check, not proof of runtime correctness until CI passes.

### 2026-11-18 — FPS ECS authority migration: stage T (agent instructions)
- Commit `0c57aff` updates `AGENTS.md` with current integrated PR #7 branch, the staged `fps.ecsAuthority` source-of-truth contract, already-migrated direct ECS systems (command, motor, damage), and explicit acknowledgement that unconverted AI/projectiles/vehicles/loot still use `State` as a tick workspace.
- It instructs any successor agent to update **this exact handoff after every implementation commit**, preserve save v9/net compatibility and deterministic side-effect order, and avoid replacing merged PR7 Editor/FPS code with stale PR8 file copies.
- New code remains unverified by a completed native CI/Metal/FPS test run. Future tasks: compile, inspect any actual failures, migrate remaining FPS systems directly to ECS and eventually remove temporary ordered workspace vectors.

### 2026-11-18 — Cross-genre audit: stage 5, standalone regression runner
- Commit `835c24b`: `tests/run_ecs_experiment_suite.sh` runs a public-include cross-genre consumer alongside existing ECS correctness checks; compiles with `-Ilibraries/include` and Simulation sources, independent of RTS/FPS link libraries.
- **Verification:** not yet executed. Next: create an explicit reuse matrix/definition of done; review GitHub CI outcomes and any build errors. Run `bash tests/run_ecs_experiment_suite.sh` and `ctest -R '^ecs_cross_genre_mechanics_test$'` once a native toolchain is available.

### 2026-11-18 — Cross-genre mechanic contract and PR #17 integration

- Commit `5737403` added a mandatory **genre-independent ECS Components + Systems architecture contract** to `AGENTS.md`. Every mechanic/library must expose reusable ECS component definitions, configurable independently scheduled systems, explicit read/write sets, deterministic order/commands, persistence/rollback, native public C++ APIs and corresponding tests/AI skill/docs. Genre modules only adapt input/configuration/presentation; they must not duplicate shared algorithms.
- Examples: RTS and FPS ballistics/rockets use one reusable trajectory, collision, hit/penetration/splash/damage implementation with different configurations, not separate projectile solvers. Apply the same requirement to health, inventory, AI, steering, vehicles, economy and networking.
- User requested importing **PR #17** (`fix/pcss-penumbra-sampling-20261117`, temporal PCSS/HDR TAA) into `feature/universal-entity-inspector-fps`/PR #7 without overwriting existing editor/FPS changes. The PR has five renderer source-file patches and one new render validation document. Source-level dry-run of all five patches against PR #7 showed **unique exact context matches**; integration is the next action.
- **Verification:** policy/docs only; shaders/macOS build and real FPS runtime integration tests remain unverified. Keep PR #7 open and do not merge main.

### 2026-11-18 — Imported PR #17 temporal PCSS into this branch
- Explicit user request: bring GitHub PR #17 (`fix/pcss-penumbra-sampling-20261117`, head `e4f23850b0c123e8004232e49438143a5581d5f4`) into universal ECS/editor branch `feature/universal-entity-inspector-fps` (PR #7).
- **Merge commit `0a6abe5f114851ed6bf27b4a020ae9953b75ff95`** has the current PR #7 head as first parent and PR #17 head as second parent. Applied all six PR #17 file changes on top of the newer PR #7 versions using exact-context patching, without replacing existing renderer changes: `Renderer3D.cpp/.h`, `ScreenLighting.h`, `ScreenLightingShaders.h`, `Shaders.h`, `tests/render/temporal-pcss.md`.
- Changes: sample rotation for PCSS is gated by a live HDR temporal resolve; GLSL/MSL sample scrambling matches; history validation compares previous-camera receiver depth and bounds edge ghosting. The shader backend builds/visual checks **have NOT been performed**.
- This is a merge of PR #17 **into PR #7 feature branch**, NOT into `main`. Preserve the PCSS test matrix (`tests/render/temporal-pcss.md`) and run Metal/GLES + HDR/without-HDR captures and performance checks before merging PR #7 into main.
- User also ordered a stronger, genre-independent mechanic architecture. The required architecture will be documented in `AGENTS.md`, `docs/ecs-mechanic-composition.md` and `skills/workflow/aiverse-new-mechanic/SKILL.md`. These rules supersede old genre-specific new-mechanic instructions.

### 2026-11-18 — Architecture rule enforcement + PR #17 confirmed in PR #7

- `5737403` — `AGENTS.md` now mandates **genre-neutral ECS component contracts + independently configurable systems** for every mechanic. RTS/FPS must share ballistic/trajectory, collision, impact, penetration, splash and damage systems; apply this composition contract to AI, locomotion, vehicles, physics, economy, inventory and meta. Native C++ public API, tests, skill and persistence are mandatory.
- `985ae46` documented the PR #17 import plan. Concurrent agent completed the actual merge as **`0a6abe5f114851ed6bf27b4a020ae9953b75ff95`**; all six PCSS/TAA files were patched onto PR #7 while retaining its newer renderer tree, and PR #17 original head `e4f23850` is recorded as second parent. The assistant independently built an equivalent merge tree, but did **not** force an extra merge after detecting the destination had already advanced.
- GitHub compare `fix/pcss-penumbra-sampling-20261117...feature/universal-entity-inspector-fps` reported **ahead, behind=0**. PR #17 is included in the PR #7 branch by Git ancestry. This is **not** a merge to `main`.
- `7e75be8` — updated **`skills/workflow/aiverse-new-mechanic/SKILL.md`**. Removed old RTS-only implementation mandates (`RtsData.cpp`, `Rts<Mechanic>.cpp`) and replaced them with shared ECS Components+Systems, optional genre adapters, native-project linkage, deterministic serialization/rollback, cross-genre fixtures and mandatory per-step handoff.
- **Validation remains unverified:** latest Metal/GLES PCSS shader compile, shadow captures, FPS/RTS checksums and sanitizer tests have not been run in this session. Keep PCSS temporal sampling TAA-gated and ECS migration opt-in.
- **Next agent:** use the shared-library registry first; do not create duplicate RTS/FPS projectile/damage implementations. Follow the updated skill and `AGENTS.md` for all new game mechanics.

### 2026-11-18 — Universal ECS projectile + damage resolution, stage A
- Commit `3dfe73e`: added `libraries/include/aiverselib/genre/EcsDamageSystems.h`, a genre-neutral **Components + Systems** damage layer. Components: `HitPointsComponent`, `ProjectileDamageComponent`, `ColliderEntityIndex` (collision ID→generation-safe Entity), `DamageQueue`; event `DamageRequest`; applied report `DamageApplied`.
- `enqueueProjectileDamage` translates a `ProjectileImpactEvent` from the existing common `addProjectileFlightSystem` into an ordered damage request. `addDamageResolutionSystem` consumes requests at a later fixed system barrier in insertion order; callbacks can emit events but not mutate projectile traversal or structural state. Default policy applies direct damage; custom policies can add armor/factions/resistance without modifying the shared system.
- **No RTS/FPS includes**, public `AIVERSE::libraries` header. Supports any genre using ECS projectile/health components. This creates the shared capability; genre-specific implementations have NOT yet been ported and no tests/build have passed.
- Next: executable shooter/strategy host test using the SAME flight/damage systems, rollback/COW checks, CMake and standalone runner wiring, then measure parity before adapting actual RTS/FPS gameplay.

### 2026-11-18 — Universal ECS projectile + damage resolution, stage B
- Commit `d58435d`: `tests/ecs_cross_genre_projectile_test.cpp` defines two unrelated native `GameModule` types using `ShooterTag` and `ArtilleryTag`. Both install identical shared `addProjectileFlightSystem` + `addDamageResolutionSystem` to simulate an ECS projectile against the same collision AABB, convert impact to `DamageQueue`, and apply 35 damage to a `HitPointsComponent` in a deterministic order. Checks impact count, projectile lifetime/despawn, checksum parity, and full `SimSnapshot` rollback/replay with and without COW.
- This test deliberately links no FPS or RTS gameplay modules: it proves one mechanic combination can work for different genres once compiled.
- **Status:** source committed, not compiled or run yet. Next action: CMake/standalone ECS test runner integration and native build verification.

### 2026-11-18 — User-mandated universal ECS mechanic contract (all genres)
- User explicitly requires **every reusable gameplay mechanic** to be built from (1) persistent ECS **components**, (2) composable ECS **systems** implementing feature stages. Genre is a project preset, never a gate: a vehicle may shoot, RTS objects may use first-person weapons, inventory and AI systems mix freely.
- Commit `304e847` (`AGENTS.md`): high-priority normative rules require data-only ECS components, independent systems with explicit read/write/resource dependencies, pure policies, deterministic ordered effects, public native-project API, composability and cross-genre tests. Disallows duplicating trajectory/damage formulas in RTS/FPS and treats RtsModule/fps::Module code as adapters rather than universal APIs.
- Commit `76d7a69` (`docs/ecs-mechanic-composition.md`): detailed architecture, per-mechanic deliverables/acceptance gates and concrete launch -> flight -> collision -> damage -> effects/despawn design, plus candid migration status.
- Commit `035aae4` (`skills/workflow/aiverse-new-mechanic/SKILL.md`): rewrote older RTS-only instructions. All future features must follow shared ECS component + system + policy + host-adapter architecture.
- Commit `bc6ff19` (`skills/gameplay/aiverse-weapon-policies/SKILL.md`) requires shared ECS weapon/projectile systems for all genres. Commit `3cd63b6` created `skills/gameplay/aiverse-ecs-projectile-systems/SKILL.md` for the new shared flight API.
- Commit `8f36243` (`libraries/include/aiverselib/genre/EcsProjectileSystems.h`) adds reusable `ProjectileFlightComponent`, `BallisticFlightPolicy`, `ProjectileImpactEvent`, `addProjectileFlightSystem`: deterministic ECS ballistic updates, read-only collision source provided by ANY game, ordered impact sink after component iteration, deferred despawn. This is a reusable **flight stage**, not complete combat/damage/AI migration.
- Commit `68d92e0` (`tests/ecs_projectile_composition_test.cpp`) exercises the same flight ECS system in unrelated Shooter and Strategy GameModule hosts, with stable ordered impacts and deep-vs-page-COW rollback checksum comparison. Commits `bd94083`, `786941f` register it in CTest and the standalone ECS suite, without linking aiverse_game or aiverse_fps.
- Commit `741c292` registers package `ecs-projectile-systems` and its public `AIVERSE::lib_ecs_projectile_systems` target in `libraries/catalog.json`. Commit `41d6fb3` updates the composition guide to point to actual implementations.
- **Native build and shader test results are NOT confirmed.** These are code, test and guideline changes, NOT proof every legacy RTS/FPS mechanic is universal. Outstanding: replace genre-specific projectile/collision/damage logic with shared systems, extract RTS-only NativeModule hooks, FPS Actor State workspace migration, generic Inspector/persistence integration, and pass cross-genre/native/ASan/TSan tests.

### PR #17 integration proof (same feature branch)
- User requested import of PR #17 (`fix/pcss-penumbra-sampling-20261117`). Commit `0a6abe5f114851ed6bf27b4a020ae9953b75ff95` has PR #17 head `e4f23850b0c123e8004232e49438143a5581d5f4` as its **second parent**, previous PR #7 head as first. Imported five render source/shader changes through exact-context patching and added `tests/render/temporal-pcss.md`, preserving unrelated PR #7 renderer changes.
- Verified Git comparison: PR #7 branch is **ahead** of PR #17, **behind = 0**. This is an ancestry merge into PR #7, NOT a merge to main and does not necessarily close PR #17.
- Still unverified: actual Metal/GLES shader compilation, HDR TAA render captures, no-TAA stability, GPU performance. See `tests/render/temporal-pcss.md`.

### Immediate next actions
1. Run `bash tests/run_ecs_experiment_suite.sh` and CTest for `ecs_projectile_composition_test`, `ecs_cross_genre_mechanics_test`, FPS/RTS parity and page-COW rollback. Fix compiler or runtime failures without changing default genre behavior.
2. Run `python3 tools/libraries/catalog.py check` and public native consumer tests; validate new library and skill/catalog packaging.
3. Compile renderer/shaders on macOS Metal and GLES and inspect PCSS penumbra ghosting with/without HDR TAA.
4. Incrementally migrate RTS + FPS launch, projectile, hit/damage and weapon states to the same ECS systems. Document which exact adapters remain; do not claim all mechanics universal until cross-genre acceptance gates pass.
5. Update this handoff after every substantive code change (exact commit, result, next owner).


### 2026-11-18 — Universal ECS projectile + damage resolution, stage C
- Commit `c957986`: registered `aiverse_ecs_cross_genre_projectile_test` in root `CMakeLists.txt` (after `modules/fps` creates `aiverse_world`), with public `AIVERSE::libraries`, `aiverse_engine` and **collision-only** `aiverse_world` linkage. No `aiverse_fps` or `aiverse_game`. This complements the existing flight-only `ecs_projectile_composition_test` with health/damage queue coverage.
- **Verification:** no compiler/CTest result has yet been obtained. Next register standalone suite and add an explicit documented mechanics-composition recipe.

### 2026-11-18 — Universal ECS projectile + damage resolution, stage D
- Commit `c2e57ee`: added `ecs-cross-genre-projectile` to `tests/run_ecs_experiment_suite.sh` with public headers, `SpatialWorld/TriangleCollision`, `Simulation`, deterministic Fixed/Json source compilation. Keeps existing flight-only ECS composition test.
- Still not verified by execution. Next: inspect shell continuation formatting and add a new agent-facing component/system composition recipe.

### 2026-11-18 — Universal ECS projectile + damage resolution, stage E
- Commit `008f01c`: created `skills/gameplay/aiverse-ecs-impact-damage/SKILL.md` for agent reuse. It specifies data-only health/damage/collider-index/queue components, flight → impact staging → ordered damage resolve → optional project-specific policy, deterministic ownership and save/rollback, native C++ usage, tests, and explicit scope limits.
- This skill **does not** imply the existing RTS/FPS combat modules have been migrated; they still require adapters and parity tests.
- Next: add catalog entry to `libraries/catalog.json`, update normative composition docs and run the tests.

### 2026-11-18 — Universal ECS projectile + damage resolution, stage F
- Commit `259258b`: registered public package `ecs-impact-damage` in `libraries/catalog.json`, with target `AIVERSE::lib_ecs_impact_damage`, source `EcsDamageSystems.h`, workflow skill, CTest fixture and dependency on existing `ecs-projectile-systems`. The library catalog's generated `AIVERSE::lib_*` targets make it accessible to native C++ game modules without linking FPS/RTS combat modules.
- JSON structure was parsed and validated before commit (119 packages; package at end); **not** tested with the catalog CLI or a native CMake configure yet.
- Next: update normative composition guide/skill and verify build/results; do not advertise automatic integration into legacy `fps::Module` / `RtsModule`.

### 2026-11-18 — Universal ECS projectile + damage resolution, stage G
- Commit `95025b3`: updated normative `docs/ecs-mechanic-composition.md` with the actual `EcsDamageSystems.h` component schema, registration recipe, ordered flight→impact queue→damage resolution→deferred despawn barrier, public target `AIVERSE::lib_ecs_impact_damage`, and shooter/artillery rollback fixture.
- Explicit limits: default only implements direct hits; armor, AoE, penetration and status require new composable systems/policies. Existing RTS/FPS damage systems have **not** been migrated or equivalence-tested. No native compilation/visual checks passed yet.

### 2026-11-18 — Universal ECS projectile + damage resolution, stage H
- Commit `0f3c4d8`: replaced `assert`-only checks in `tests/ecs_cross_genre_projectile_test.cpp` with throwing `require` runtime assertions. C++ `NDEBUG` must not disable collider setup or make Release CTest silently pass if impact, damage, rollback, COW or cross-genre checks fail.
- **Verification:** source-level inspection only; native test builds still pending. Keep both CTest and standalone suite as required gates.

### 2026-11-18 — Universal ECS weapon launch, stage A
- Commit `3e146a3`: added shared `libraries/include/aiverselib/genre/EcsWeaponLaunchSystems.h`. Data-only ECS components: `WeaponParametersComponent` (Fixed speed/gravity/radius, lifetime, cooldown, damage), `WeaponStateComponent` (ammo/nextFireTick), `FireIntentComponent` (origin/direction/one-shot request), `ProjectileSpawnQueue` and `ProjectileSpawnCommand`.
- `addWeaponLaunchSystem` validates settings, cooldown/ammo, consumes requests in stable `WeaponState` dense order and emits spawn commands without structural changes during iteration. `addProjectileSpawnSystem` consumes them in a separate deterministic barrier and creates `ProjectileFlightComponent`/`ProjectileDamageComponent` pairs for the shared flight→impact→damage chain. Host genre adapters supply inputs and configuration; no `RtsModule` or `fps::Module` dependencies.
- **Status:** initial implementation only, not compiled/run or integrated with legacy RTS/FPS gameplay. Next: full cross-genre weapon→spawn→flight→damage test, CMake/standalone registration, catalog, skill and documentation. Keep existing host combat untouched until equivalence is proven.

### 2026-11-18 — Universal ECS weapon launch, stage B (full pipeline proof)
- Commit `95c4a08`: `tests/ecs_cross_genre_weapon_pipeline_test.cpp` installs identical **WeaponLaunch → ProjectileSpawn → ProjectileFlight → ImpactQueue → DamageResolution** ECS systems into unrelated ShooterTag/ArtilleryTag `GameModule` hosts. Checks one request consumes exactly one ammo, stable generated shot ID, 35 damage, projectile despawn, identical checksums and snapshot rollback with/without COW.
- The test depends on no FPS/RTS game module header or linkage, only `AIVERSE::libraries` + shared `aiverse_world` collision. It complements existing flight-only and flight-to-damage cross-genre tests.
- **Status:** new test not yet built/executed. Next wire CMake/standalone runner, register public package and skill, and preserve deterministic event barriers.

### 2026-11-18 — Universal ECS weapon launch, stage C (CMake)
- Commit `282a299`: registers `aiverse_ecs_cross_genre_weapon_pipeline_test` after `modules/fps` creates `aiverse_world`, linking only `aiverse_engine`, `aiverse_world` and `AIVERSE::libraries` (not `aiverse_fps` or `aiverse_game`). CTest case `ecs_cross_genre_weapon_pipeline_test` covers full shared shooting and damage composition.
- Not compiled on Mac/Linux yet; CI must confirm.

### 2026-11-18 — Universal ECS weapon launch, stage D (standalone suite)
- Commit `7e3fe6d`: `tests/run_ecs_experiment_suite.sh` now compiles and runs `ecs-cross-genre-weapon-pipeline` with public library includes, shared SpatialWorld/TriangleCollision and simulation sources. It checks the full weapon→flight→damage chain without linking any genre gameplay module, including normal/sanitized build modes.
- The source and shell continuation lines were assembled explicitly. Runtime tests remain unverified; check `bash -n` and execute suite on native toolchain.

### 2026-11-18 — Universal ECS weapon launch, stage E (agent skill)
- Commit `ac54b13`: created `skills/gameplay/aiverse-ecs-weapon-launch/SKILL.md`. Agents must assemble shared WeaponLaunch, Spawn, ProjectileFlight and DamageResolution components/systems, independent of RTS/FPS genre; state/queues/shot IDs in ECS, canonical side effects and COW/replay proof. New capabilities such as reload, guided rockets and spread are **separate systems**, not genre branches.
- Skill explicitly warns that legacy RTS/FPS combat modules still need stepwise migration and old save/network parity checks.
- Next: catalog public native target and update normative mechanics specification.

### 2026-11-18 — Universal ECS weapon launch, stage F (public native package)
- Commit `e1d286f`: registered `ecs-weapon-launch` package in `libraries/catalog.json`, exported as `AIVERSE::lib_ecs_weapon_launch`. It depends on `ecs-impact-damage` (and transitively on `ecs-projectile-systems` and collision), with public header/skill/test dependencies.
- Catalog parsed and validated as JSON (120 packages, new package last), but native `tools/libraries/catalog.py check` and CMake test **have not been run**. Native projects can request this package without importing FPS/RTS gameplay modules.
- Next: update composition spec + agent rules and inspect CI results.

### 2026-11-18 — Universal ECS weapon launch, stage G (normative composition doc)
- Commit `c458af0`: `docs/ecs-mechanic-composition.md` documents the complete public ECS `WeaponLaunch → ProjectileSpawn → Flight → Damage` component/system chain, order/barriers and native registration example. A single shared implementation works with any shooting Entity regardless of genre, with `AIVERSE::lib_ecs_weapon_launch` and `ecs_cross_genre_weapon_pipeline_test` as proof points.
- **Explicit non-completeness:** published FPS/RTS gameplay still has original combat implementations. The generic pipeline is reusable in independent hosts but requires migration of existing engines, tuning and legacy save/network parity tests. No native test success reported yet.

### 2026-11-18 — Universal ECS weapon launch, stage H (agent policy)
- Commit `8421961`: `AGENTS.md` explicitly names the required shared shooting packages `AIVERSE::lib_ecs_weapon_launch`, `ecs-projectile-systems` and `ecs-impact-damage`. Agents must reuse the same ECS projectile trajectory, collision and damage stages for FPS, RTS, vehicle turrets, RPG spells, etc., and extend systems through composable components/policies. Do not create new genre-specific duplicate solvers.
- The required cross-genre test cases are listed; published RTS/FPS save/rollback and real-game checksum migration remains a separate, unverified task.

### 2026-11-18 — Universal ECS weapon launch, stage I (skill cross-links)
- Commit `2363b7d`: updated the existing `skills/gameplay/aiverse-ecs-projectile-systems/SKILL.md` to direct agents to `aiverse-ecs-weapon-launch` and `aiverse-ecs-impact-damage`, the complete public header chain and three cross-genre regression tests. This prevents a future agent from implementing duplicate RTS/FPS weapon or damage stages when reading the older flight-only skill.
- Work in this checkpoint is limited to independent reusable packages and tests. **Existing** `RtsModule` and `fps::Module` shooting/AI/netcode remain to be migrated, one stage at a time with real checksum and legacy save parity checks. CI not confirmed green.


### 2026-11-18 — Shared weapon dry-fire regression checkpoint
- Commits `51d53d4553c5034f2a124b5511f107b97d377df2` and `a302652200d196b69b0f4057ae4071a52aa5653d`: extended `tests/ecs_cross_genre_weapon_pipeline_test.cpp` to request another shot after ammo reaches zero and after the initial 8-tick snapshot replay checksum parity checkpoint. It checks request consumption, unchanged ammo, unchanged nextShotId and empty pending spawn queue for both shooter and artillery hosts, including COW runs.
- The second commit places the dry-fire scenario **after** the original replay checksum assertion to avoid comparing different tick counts.
- Verification: GitHub source commits only; native compile, CTest, sanitizer and real FPS/RTS gameplay integration **not executed**. Next: compile and run the suite, address failures, then migrate real FPS/RTS launch paths to the shared weapon systems one at a time with save/network parity.


### 2026-11-18 — Agent continuity protocol (user instruction)
- User requires a durable handoff entry for **every completed implementation step**, not only at task completion. Record exact implementation SHA, touched paths, observable behavior, tests actually executed and their outcomes, limitations, and the next action. Commit this handoff entry separately immediately after the implementation commit.
- Keep unverified code changes explicitly marked as unverified; do not claim successful native build, sanitizer, Metal rendering or FPS/RTS parity without evidence.
- Current branch: `feature/universal-entity-inspector-fps`. Most recent code commits: `51d53d4553c5034f2a124b5511f107b97d377df2`, `a302652200d196b69b0f4057ae4071a52aa5653d`; preceding handoff commit: `6a780676d307b6121a76df23082d87088da064c4`.
- Next: run cross-genre weapon pipeline tests in native environment, then integrate actual FPS/RTS adapters in small, parity-checked increments.


### 2026-11-18 — Universal weapon dry-fire rollback replay (next checkpoint)
- Implementation SHA: `3c30e16a89897fb9f77c196dfa796686fb86de0c`.
- Changed: `tests/ecs_cross_genre_weapon_pipeline_test.cpp`. Saves a snapshot after the first shot and successful 8-tick replay, requests a dry fire with empty ammo, checks no projectile ID allocation or pending spawn and that the request is consumed; restores the post-shot snapshot, repeats the request and checks byte-stable simulation checksum. The template runs both ShooterTag and ArtilleryTag, with and without COW.
- Verification: GitHub file update succeeded. **No native compilation, CTest, sanitizer or gameplay runtime verification executed**. Do not mark test passed.
- Next action: run `bash tests/run_ecs_experiment_suite.sh`, targeted CTest and native consumer/catalog checks. Diagnose any failures before connecting real FPS/RTS weapon paths to the shared ECS systems.


### 2026-11-18 — Cross-host dry-fire damage and checksum regression
- Implementation SHA: `c75ff4e4f71da11f0bb8af2e55cb65e4797140c4`.
- Changed: `tests/ecs_cross_genre_weapon_pipeline_test.cpp`. After snapshot restore and dry-fire replay, assert target health remains 65 and damage event summary remains exactly one 35-point hit. Return post-dry-fire checksum from each template host so ShooterTag vs ArtilleryTag and deep vs COW paths compare the **full** scenario, not only the first firing checkpoint. Original first-shot replay checksum equality is still asserted within each host.
- Verification: GitHub source commit confirmed; C++ compilation and runtime test still **not verified**.
- CI observation for prior documentation commit `a07dc450d875f1438d707bf2dd211a463dea9e42`: four GitHub Actions runs were queued (ECS 2.0 experimental checks #318, CI #1168, AIVERSE Studio validation #835, GPU batching focused regression #439), with no conclusions at time of inspection. This is not a passing test report.
- Next: inspect the workflow outcomes and logs; run native suite/CTest, fix failures, and migrate FPS/RTS weapon adapters in isolated parity-checked increments.


### 2026-11-18 — FPS ECS authority post-tick invariant
- Implementation commits: `671c5e9680438bbf9aac4285cbc3d3bf66d7a292` and corrective `ec34d1c0b4735311b605da1f2518c1b8b3655f8f`.
- Changed: `modules/fps/FpsModule.cpp`, the `fps.ecsMirror` system. When `fps.ecsAuthority` is ON, after syncing actors and dynamic objects it now checks that every actor slot has an `ActorRuntime` component whose slot, position, velocity, health, armor, weapon index, ammo and reserve agree with the just-committed legacy tick workspace. A missing/divergent runtime throws `std::logic_error` instead of silently allowing authority drift. No extra checks run with the default OFF gate. The second commit corrects an initially mistaken `Actor::magazine` member to the real `Actor::ammo` member.
- Verification: checked `Actor` declaration in `modules/fps/FpsModule.h` and committed source; **no compilation or runtime test**. The invariant checks selected fields, not every nested actor field; it is a migration guard, **not** completion of direct ECS combat or actor-system migration.
- Next: verify `aiverse_fps_tests` with ecsAuthority both OFF and ON, save v9 and transport checksum parity; expand direct ECS systems and then remove the legacy tick workspace once complete. Check Editor regressions before merge.


### 2026-11-18 — Full actor payload ECS commit invariant
- Implementation SHA: `18104af8d6b2b3ee51ec06444bb03abbeb4af230`.
- Changed: `modules/fps/FpsModule.cpp`. Replaced the selective field checks in `fps.ecsMirror` with `Actor::operator==` against the complete `ActorRuntime::value`. `modules/fps/FpsModule.h` already declares defaulted `Actor::operator==`. This covers inventory, melee state, input, timers, weapon slots, actions and movement alongside the previous vitals, under the opt-in `fps.ecsAuthority` gate. No save/network wire-format changes.
- Verification: GitHub source inspection and commit only; native compile and runtime behavior **not verified**. Potential compile dependency: all nested Actor members must support equality, as required by its defaulted operator. Do not claim actual FPS/RTS migration is complete.
- Next: validate native build, replay and transport; migrate remaining tick algorithms to direct ECS components, while keeping versioned save/network compatibility.


### 2026-11-18 — FPS Editor ECS projection consistency guard
- Implementation SHAs: `75b3673b55ac10d6028130bf570d42277b9e718f`, correction `a1e6dacfc3af1db18dde1241944a4dc2f08c7745`.
- Changed `modules/fps/FpsModule.cpp`: after the full ActorRuntime equality guard, `fps.ecsMirror` now also verifies the derived `ActorEntityIdentity`, `ActorEntityTransform` and `ActorEntityVitals` projections against canonical Actor state (slot/team/life, position/velocity/yaw/pitch, health/armor/ammo/reserve and stance flags). This detects editor-facing stale projections while `fps.ecsAuthority` is enabled. Default OFF path unchanged. Corrective SHA uses actual `ActorEntityTransform::position` field, verified in `modules/fps/FpsModule.h`.
- Verification: source inspection and GitHub commits only. Native C++ compilation, runtime save/replay/network tests and visual editor regression **not run**.
- CI observation: at inspection, commit `858c575d31bc91f5efdf649600263ea9b6ffea22` had four queued Actions workflows (ECS 2.0, CI, Studio, GPU batching); no success reported.
- Next: run native build/CI and FPS ecsAuthority on/off replay tests. Complete actual direct ECS systems and RTS integration; Editor Inspector live ECS writeback, hierarchy/gizmo and rollback remain separate unfinished gates.


### 2026-11-18 — complete ECS feature inventory and migration roadmap (planning-only audit)
- **Planning document commit:** `0141efc431b5d852a633e627bb05b506fe7a43a7`; created `docs/ecs-feature-migration-plan.md`. It inventories all 120 registered packages (78 simulation, 21 presentation, 21 tools), describes real RTS/FPS host ownership and outlines cross-genre component/system migration epics E00–E15, execution stages, tests and agent boundaries. Categorization is a *proposed target architecture*, not a declaration that features are already converted.
- **Machine-readable tracker commit:** `be15ce4e0e6b01713a7fc7a35387e3e0c505b095`; created `docs/ecs-feature-migration-status.csv` with 120 package rows and explicit initial migration statuses. Follow-on agents must update per-feature status only when source and real-host tests support it.
- **Audited sources:** `libraries/catalog.json`, `AGENTS.md`, `docs/ecs-mechanic-composition.md`, `engine/core/ECS.h`, `engine/sim/Simulation.h`, `engine/sim/CommonComponents.h`, `modules/rts/RtsModule.cpp`, `RtsModule.h`, `RtsData.h`, `modules/fps/FpsModule.cpp`, `FpsModule.h`, `FpsSystems.h`, `engine/meta/Meta.h`, existing ECS and editor handoffs.
- **Finding:** independent shared ECS launch/flight/damage libraries exist but production RTS/FPS still use legacy gameplay; FPS ECS full-runtime payloads are canonical only between tick boundaries under an opt-in flag; RTS simulation registration is not itself genre-neutral component composition. Rendering, UI, offline tools and account meta are not simulation ECS migration targets.
- **Verification executed:** successful GitHub connector source reads, `libraries/catalog.json` JSON parse and GitHub file creation responses. **No** native CMake build, CTest, sanitizer, FPS/RTS gameplay, checksum parity, Metal/GLES validation, editor visual test or CI results were run/verified here.
- **Next action:** E00 baseline and golden RTS/FPS replay/event/save snapshots; coordinate ownership with active FPS and ECS agents before any implementation. Start incremental E01/E02/E03 integration, never mass-rewrite or replace live logic without parity proof. Keep this handoff updated after each subsequent implementation commit.


### 2026-11-18 — Universal ECS whole-engine migration register
- Implementation SHA: `0586878c1047c77401c870830330dfc32ab299b8`.
- Changed: `docs/ecs-all-features-migration-plan.md`; catalog-based review of all 120 packages (78 simulation, 21 presentation, 21 tools), 41 RTS `Simulation` system registrations, FPS opt-in `ecsAuthority` and the three existing generic weapon packages. Records waves, source-of-truth contract, host parity gates, native/API/editor requirements and per-package dispositions.
- Verification: read source via linked GitHub branch and parsed the catalog; **no native build, CTest or real RTS/FPS gameplay parity executed**. A migration plan is not a code migration. Library data may have changed since inspection; re-read before editing.
- Next: add reusable ECS gameplay components/systems (weapon reload, timers, stats) and independent native/cross-genre rollback fixtures; integrate actual RTS/FPS paths incrementally with legacy-save parity.


### 2026-11-18 — Generic ECS timed mechanics header
- Implementation SHA: `5907d5f893f91bd4e7989ed2eb8eaa3109ba2959`.
- Added `libraries/include/aiverselib/genre/EcsTimedMechanics.h`: `WeaponReloadComponent`/`ReloadIntentComponent` with reload and fire-intent gate stages; `HealthRegenerationComponent` over the canonical shared `HitPointsComponent`; `EnergyRegenerationComponent`; `TimedExpiryComponent`, all independently registered with `Simulation` and without RTS/FPS dependencies.
- Verification: source uploaded via GitHub; **native compile and CTest not run**, host integrations not yet switched. Shared health/weapon components remain additive/opt-in and are not asserted to replace legacy modules.
- Next: add two-host COW/save/rollback fixture, package registration, skill, catalog and CMake, then verify natively before any default host switch.


### 2026-11-18 — Shared timed mechanics cross-genre regression fixture
- Implementation SHA: `337cd0e9faea659b5ddedb75c6eb7eeecf0ce77a`.
- Added `tests/ecs_cross_genre_timed_mechanics_test.cpp`: shooter + strategy hosts exercise the identical reload/fire-gate, health/energy regeneration and timed expiry systems; verifies ammo/reserve, stable ticks, suppression/consumption, full reload, TTL and snapshot replay with/without COW.
- Verification: fixture committed through GitHub; **not compiled nor run** (no checked-out native toolchain in this environment). Assertions reflect expected semantics, not observed pass results.
- Next: register this test in CMake and package in catalog, add agent skill; then run compiler/CTest before integrating RTS/FPS gameplay.


### 2026-11-18 — Timed ECS package, skill and CTest wiring
- Commits: `939f8e89d039c443bb802ce66cd2cc93f60eb17c` (new agent skill), `9cb1be3ca61432eb98b60b9c8fd1b95764e643bf` (`libraries/catalog.json`, package #121), `a6aac3026dfc058de780baaaaab34a8708a5570c` (CTest registration in `CMakeLists.txt`).
- New public target `AIVERSE::lib_ecs_timed_mechanics` depends on `ecs-weapon-launch`; native registration example and compatibility requirements in `skills/gameplay/aiverse-ecs-timed-mechanics/SKILL.md`. Test executable `aiverse_ecs_cross_genre_timed_mechanics_test` added to CTest; test source already added in prior checkpoint.
- Verification: programmatically parsed `libraries/catalog.json` as 121 valid JSON entries at the catalog edit point. **No CMake configure/build, no CTest execution, no package catalog filesystem check, no FPS/RTS runtime parity**; avoid claims of readiness.
- Next: run `python3 tools/libraries/catalog.py check`, configure native CMake, build `aiverse_ecs_cross_genre_timed_mechanics_test` and run `ctest -R ecs_cross_genre_timed_mechanics_test --output-on-failure`. Investigate compile errors and cross-genre replay. Then integrate one legacy host (FPS or RTS) behind an explicit gate and compare tick/order/checksum/save/network before enabling.


### 2026-11-18 — Standalone ECS experiment suite wired
- Implementation SHA: `515536b3793c973b22a2b1e72e2dd33ecc64054e`.
- Changed `tests/run_ecs_experiment_suite.sh` to compile/run the cross-genre timed mechanics test from public headers with only shared engine/world link inputs; no RTS/FPS game library dependency.
- Verification: GitHub source edit succeeded. **No shell syntax check, native compilation, sanitizer or test execution available in this environment.** GitHub Actions on previous commit `3ad66dc1` returned all runs queued; not passing evidence.
- Next: check CI completion and target build errors; proceed to controlled legacy host integration only after baseline parity and source-of-truth mapping.


### 2026-11-18 — Universal ECS locomotion policy adapter
- Implementation SHA: `7e48fd23d31cc82fbd791c755f7301bb18635a48`.
- Added `libraries/include/aiverselib/genre/EcsLocomotionSystems.h`: `LocomotionParametersComponent`, `LocomotionStateComponent`, `LocomotionInputComponent`, `LocomotionOutputComponent` and `addLocomotionPolicySystem`. Uses the existing shared `stepMotion` fixed-point policy instead of duplicating movement math. Consumes jump edge, leaves persistent movement wishes, returns velocity; host collision/position integration is intentionally separate.
- Verification: GitHub source committed, **native compiler and behavioral tests not executed**. Does not replace FPS `moveActor` or RTS `sysMovement` yet.
- Next: native cross-genre tests and CMake/catalog/skill registration; then integrate collision sampling and movement into actual hosts behind parity gates.


### 2026-11-18 — Locomotion ECS two-host regression
- Implementation SHA: `9c7b593dad3755b89fec0186e2e03dfd2a92466e`.
- Added `tests/ecs_cross_genre_locomotion_test.cpp`: shooter and strategy modules use the exact same ECS locomotion system, verify jump edge, deterministic ground/air acceleration, output publishing, rollback/replay checksums and COW equivalence.
- Verification: source committed; **no native C++ compilation or runtime assertions executed**. Tests do not cover collision, ladders, mantling, swimming or legacy FPS/RTS integration.
- Next: publish native package target, skill, CMake/standalone test runner; add separate collider/transform integration and nontrivial locomotion mode tests before changing default games.


### 2026-11-18 — ECS locomotion package and test registration
- Commits: `278cd8384985c74519940f4d61cae1ecf0290c94` (skill), `40211d94b72739f3127bb58c1415ef417082d19a` (catalog #122), `c9ef4fe369e1fcc26aaabfb7fcbf321d4cb857cc` (CMake/CTest), `0bf6a9a7ac8f52fe5c6ff1fc7635ebcf24bc40a3` (standalone experiment runner).
- New public target `AIVERSE::lib_ecs_locomotion` depends on existing `character-locomotion`; public `EcsLocomotionSystems.h` and skill cover stable state/input/result policy binding without FPS/RTS module linkage. Two-host test checks replay and COW; `stepMotion` is reused, not duplicated.
- Verification: catalog parsed to 122 JSON entries during edit. **No native compile, native consumer build, CTest, nor runtime parity run**. Remaining collision, transformations and advanced locomotion modes are not yet ECS-integrated in built-in games.
- Next: run catalog/check, complete CMake/test suite, and convert one actual RTS/FPS consumer with exact saved-state and deterministic replay parity before deleting a legacy path.


### 2026-11-18 — Shared ECS inventory transaction system
- Implementation SHA: `bbda102d6635edf170d83fe5a79280351a53d159`.
- Added `libraries/include/aiverselib/genre/EcsInventorySystems.h`: per-Entity `InventoryComponent`; immutable `InventoryCatalog` world resource; ordered `InventoryCommandQueue`, `InventoryResultQueue`, generation-safe source/destination entities, and `addInventoryTransactionSystem`. Reuses transactional `gameplay::addItem/removeItem/transferItem` policies, preserving capacity and no-partial-write guarantees without FPS/RTS dependencies.
- Verification: source committed; **not compiled or run**, not yet integrated into legacy built-in FPS `Actor::bag` or RTS inventories.
- Next: cross-genre transaction/rollback tests, public package and CMake runner, skill; then migrate FPS loot/pickups and RTS item transfer by explicit authority cutover.


### 2026-11-18 — Cross-genre ECS inventory transaction fixture
- Implementation SHA: `b52131d3db13dc61c9a1c4ab23dc30454b4a3a25`.
- Added `tests/ecs_cross_genre_inventory_test.cpp`: same ECS inventory queue and transaction system in an RTS store and an FPS bag host; transfer, stack expansion, weight-cap rejection without partial mutation, ordered results, id sequence and COW snapshot rollback parity.
- Verification: source committed through GitHub. **No compiler execution or CTest results**. Built-in FPS and RTS gameplay still use their legacy inventory paths; test does not assert they have been migrated.
- Next: skill/catalog/CMake/standalone runner, then parity-checked native and built-in host adoption.


### 2026-11-18 — ECS inventory native package, skill and test registration
- Commits: `f59e7c888dd690ca4e76a9169f5556b89511e558` (agent skill), `30828e37f64e1d0f5d9794b8ec50ae9d77ee2fc3` (catalog #123), `0a311f24c98e9d63f212ccd4f380c1cd0d866ef3` (CTest), `513bffd59e84c4331a4399eb112fa2df18218343` (standalone experiment script).
- Published public native target `AIVERSE::lib_ecs_inventory` and `skills/gameplay/aiverse-ecs-inventory/SKILL.md`, reusing existing Inventory algorithms. Registered `ecs_cross_genre_inventory_test` on two unrelated game hosts, stable ordered commands, weight rejection, rollback and COW.
- Verification: catalog JSON parsed to 123 records in editor operation. **No compiler/CTest/CI success yet**. FPS/RTS built-in inventories are not migrated; authorization remains host responsibility.
- Next: run native CI, resolve issues, then migrate FPS loot/inventory and RTS gather/storehouses via explicit ECS owners and full save/network/checksum regression gates.


### 2026-11-18 — Agents routing updated for all-feature ECS migration
- Implementation SHA: `853a1f5d5f19c3aff06754622f7d74e838dc4d8f`.
- Changed `AGENTS.md` to link the 120-package baseline audit, newly published public ECS packages (timed mechanics, locomotion, inventory), their skills and required real-world RTS/FPS migration gates. Explicitly distinguishes library availability from production gameplay migration.
- Verification: GitHub source committed only. CI/build and user gameplay tests are still pending; latest workflow status check reported queued runs.
- Next: check compilation/CTests; migrate an actual built-in host with source-of-truth ownership and validate golden save/network/replay parity before turning on defaults.


### 2026-11-18 — Whole-engine ECS migration tracker
- Created persistent work tracker: https://github.com/chromealex/AIVERSE/issues/19 (epic with phased checklists, canonical authority gates and links to audit/AGENTS/this handoff).
- The epic marks reusable ECS packages as **implemented but unverified and not yet adopted by built-in RTS/FPS**. Do not close it merely because catalog entries exist.
- Next: obtain native build/CI evidence, then migrate actual FPS/RTS systems one behavior at a time, update the linked issue checklist and this handoff separately.


### 2026-11-18 — Shared ECS weapon cooldown snapshot regression
- Implementation SHA: `b703c3937a7397a1fe71987b29e7e90a5bf5e602`.
- Changed: `tests/ecs_cross_genre_weapon_pipeline_test.cpp`. New `exerciseCooldown<Tag>(cow)` fixture initializes two rounds, fires once, snapshots, requests another shot one tick later (inside the six-tick fire interval), and checks ammo remains 1, nextShotId remains 2, no pending spawn and fire intent is consumed. Restores the snapshot and compares deterministic checksum after replay. Compares ShooterTag/ArtilleryTag and deep/COW snapshot modes.
- Verification: GitHub commit succeeded. C++ compilation and test execution not performed; expected outcome is an assertion, not an observed pass.
- CI status observed for previous handoff SHA `4de1319dfad9589975ed885fdd7565ba1e8d2bf1`: CI #1178 was **cancelled**, while Studio #845, GPU batching #449 and ECS experimental #328 were queued. Cancelled is not a pass.
- Next: run the new fixture in native CTest, inspect Actions outcomes, then wire real FPS/RTS launch and projectile paths to shared systems behind opt-in gates with byte-compatible saves and rollback. Keep Editor regression testing on the acceptance checklist.


### 2026-11-18 — Shared ECS weapon cooldown expiry and replay
- Implementation SHA: `fbda37412977436a639c26230c1661f16478a008`.
- Changed: `tests/ecs_cross_genre_weapon_pipeline_test.cpp`, `exerciseCooldown<Tag>(cow)`. Following rejected early fire and snapshot replay, advances to tick 6 (six-tick fire interval), snapshots, requests the next shot, and checks the final ammo is consumed, nextShotId advances to 3, spawn queue is drained, and fire intent is consumed. Restores the ready snapshot, repeats the shot and compares checksum. All existing ShooterTag/ArtilleryTag and deep/COW variants exercise this path.
- Verification: GitHub source commit succeeded. **No native compilation or execution yet**; tick boundary semantics and test success must be confirmed by the native runner. This is a shared library test extension, not proof of actual FPS/RTS combat migration.
- Next: run `aiverse_ecs_cross_genre_weapon_pipeline_test`, inspect failures, then integrate real genre adapters with save v9, transport rollback and Editor regression gates. Preserve existing deterministic gameplay until parity proven.


### 2026-11-18 — First live FPS Editor projection read from ECS authority
- Implementation SHA: `b781438fd76808a24cda6de944bb1db97376c9c4`.
- Changed: `modules/fps/FpsModule.cpp::syncActorEntities`. After the authoritative `ActorRuntime` commit, `ActorEntityIdentity`, `ActorEntityTransform` and `ActorEntityVitals` are now populated from the ECS runtime payload when `fps.ecsAuthority` is enabled. Default legacy OFF mode still reads the legacy Actor. This removes a second read path for editor-facing actor projections and establishes ECS as their input source. The legacy gameplay tick and save v9 workspace are **still present**.
- Verification: source change pushed to GitHub; **not compiled or executed**. Existing post-mirror projection guard should catch divergence when ECS authority is enabled, but its actual test outcome is unverified.
- Next: migrate another actual gameplay stage (weapon launch / damage) to shared ECS components behind parity gate, preserving FPS-specific hitscan/rewind rules; do not replace those with ballistic flight without matching legacy semantics. Independently migrate RTS adapter, then validate save v9, network rollback and Editor.


### 2026-11-18 — FPS live fire accounting writes canonical ActorRuntime
- Implementation SHA: `0a17b92c808db73fc39877004bef3c2ed4e4e0d3`.
- Changed: `modules/fps/FpsModule.cpp::shoot`. Under opt-in `fps.ecsAuthority`, the actual gameplay firing path locates the shooter ECS Entity and its `ActorRuntime`, seeds its ammo/shots/nextFire from the current compatibility workspace (which may contain same-tick reload or other changes), applies the shot accounting to the runtime component, then writes the three resulting values back to the legacy workspace. Default OFF branch preserves the previous exact `--ammo; ++shots; nextFire=tick+interval` sequence. Existing hitscan/lag compensation, ballistic projectile creation, RNG, damage and save v9 remain untouched.
- This is **partial direct ECS mutation**, not adoption of the shared `EcsWeaponLaunchSystems` yet. The same-tick legacy workspace still carries the rest of combat, and `fps.ecsMirror` commits the full actor at the end. Do not describe this as universal RTS/FPS shooting.
- Verification: GitHub source commit only; no C++ compilation, replay checksum, FPS transport, save v9 or Editor test executed. Pay particular attention to same-tick reload and weapon switch ordering.
- Next: extract common weapon state/launch logic with real ECS systems and adapt FPS hitscan/rewind without changing behavior; add parity tests and integrate RTS. Run native build before production use.


### 2026-11-18 — FPS dynamic authority regression compile blocker fixed
- Implementation SHA: `cf9acb846fe018eb90198b0452bb1da48d9c6e0e`.
- Changed: `tests/fps/tests.cpp`. Moved accidental `if(i==0)` block (which referenced undefined `i`, `world`, `projectile`, `loot`, `vehicle`, `p`, `l`, `v`) out of `ecsActorAuthorityParity` and into the correct `ecsDynamicAuthorityParity` fixture, after its first synchronized tick. Added map `contains` checks before `at` lookups to report missing indexes as fixture assertions instead of uncaught out_of_range.
- Verification: static inspection of current branch + exact anchored GitHub edit; **native compile, CTest, sanitizer and gameplay parity still unverified**. Git clone from this execution environment failed DNS resolution. GitHub Actions check was previously queued/cancelled, not a pass.
- Next action: compile `aiverse_fps_tests` on native macOS/Linux; validate existing ECS actor/dynamic authority fixtures; inspect any additional malformed merged test blocks before replacing real gameplay authority.


### 2026-11-18 — Shared reload policy extracted for direct native host integration
- Implementation SHA: `930cdf4ae80aa0cc80a845d3c5d2a31f086119fa`.
- Changed `libraries/include/aiverselib/genre/EcsTimedMechanics.h` to extract `stepWeaponReload(WeaponStateComponent&,WeaponReloadComponent&,tick,request)`; `addWeaponReloadSystem` now calls the same deterministic function. Explicitly preserves normal FPS behavior including completion before a same-tick new request. No added system-level double-owner.
- Verification: static source modification only. **Native compiler/CTest unavailable so far**, no live FPS parity yet. The shared function uses the pre-existing FPS transfer arithmetic, so any invalid negative state handling must be checked in host validators.
- Next: convert the legacy FPS reload step to a temporary typed adapter invoking this policy with canonical fields, then add native golden parity fixtures covering weapon switching, ammo/reserve, replay and save v9.


### 2026-11-18 — First real built-in FPS mechanic uses the universal shared reload policy
- Implementation SHA: `a497f62f553680a456e1d7db645188204e677cad`.
- Changed `modules/fps/FpsModule.cpp`: the FPS actor's real per-tick reload completion, reserve transfer and reload-request decision now run via `lib::genre::stepWeaponReload` defined in `EcsTimedMechanics.h`, which is also used by generic `addWeaponReloadSystem`. Adapted `Actor::ammo/reserve/nextFire/reloadUntil` to temporary value structs in the same place inside tick, preserving weapon selection and shoot ordering and retaining legacy save v9 bytes. There is **no second mutable canonical reload ECS component** yet; under `fps.ecsAuthority`, `ActorRuntime` remains canonical at tick boundaries. This is actual shared-algorithm adoption, **not completion of full FPS ECS system extraction**.
- Verification: GitHub edit committed; **native compilation, FPS tests, direct reload parity, save/network and rollback not yet executed**. Generic policy used same legacy reload transfer expression and finish-then-request ordering; boundary cases near uint32 tick overflow remain a known parity gate.
- Next: add an FPS ECS-authority ON/OFF reload-fire-switch replay fixture, run native FPS/CTest and resolve any compiler behavior differences before switching authoritative weapon state to standalone ECS components.


### 2026-11-18 — Real FPS reload tick parity regression added
- Implementation SHA: `92a165d34c865a26955d551d2a1919946c9ea9b7`.
- Changed `tests/fps/tests.cpp`: new `ecsReloadAuthorityParity` drives 25 tick-accurate FPS inputs in two independent built-in modules, one legacy and one with `fps.ecsAuthority=true`. It checks authoritative ActorRuntime after every tick, source-state equality, shared reload start/completion/fire suppression and reserve accounting, weapon selection, v9 save cross-mode load, pre-reload-completion snapshot restoration, replay checksums and per-tick parity.
- Verification: static authored fixture/commit only. **No native compile or actual assertions executed**. Evaluate its tick-0 and weapon-switch expectations through the native test runner, and resolve any failure before claiming parity.
- Next: compile `aiverse_fps_tests` and run FPS tests; then continue separating the real FPS fire/damage and dynamic state systems from `State::actors` behind explicit per-feature gates.


### 2026-11-18 — Shared accepted-shot accounting extraction
- Implementation SHA: `f8b41cf113dc9042824716192073068fb429a9e8`.
- Changed `libraries/include/aiverselib/genre/EcsWeaponLaunchSystems.h`: extracted pure `commitAcceptedShot(ammo,nextFireTick,now,interval,shots?)` policy, used by generic ECS weapon launch after its existing acceptance checks. No RNG or structural writes, and no new validation inside the helper. Existing generic accepted-shot semantics remain decrement ammo and set nextFireTick.
- Verification: GitHub commit only; native C++ compilation and test execution **not run**.
- Next: call the same policy from real FPS `Module::shoot` while preserving its prior validation/hitscan/rewind and save v9 ordering; then document that separate commit.


### 2026-11-18 — Real FPS shot accounting now calls shared weapon policy
- Implementation SHA: `25917da11cf6cb5a2ea32c436ae891f6b4c2ee0d`.
- Changed `modules/fps/FpsModule.cpp`: includes the public shared weapon launch header and replaces duplicated post-acceptance ammo/cooldown/shots mutation in both `fps.ecsAuthority` ON and OFF paths of real `Module::shoot` with `lib::genre::commitAcceptedShot`. ON path still writes `ActorRuntime` first and copies three fields back to the legacy tick workspace. OFF path still mutates the legacy actor. The helper performs the same arithmetic as the removed inline statements; shot eligibility, RNG, pellets, hitscan, lag compensation, projectile creation, impact/damage, save v9 and network formats are unchanged.
- Verification: GitHub source commits only; C++ compile and runtime tests **not executed**. Shared policy may expose include/link issues; check native FPS target and cross-genre weapon test before merge.
- This is a **real shared gameplay algorithm**, but FPS/RTS are NOT yet fully consuming common ECS WeaponLaunch/Flight/Damage systems. Next: connect actual RTS fire accounting and/or extract shared collision/damage stages with parity fixtures; then migrate authoritative ECS state and test rollback.


### 2026-11-18 — User-directed projectile motion split
- Documentation SHA: `ed7cabb74b8278f83032aa023029e02e8b7bfb6a`.
- Changed `docs/ecs-mechanic-composition.md`: documented two distinct projectile-motion ECS systems, `KinematicProjectileSystem` (abstract deterministic motion, common RTS usage) and `PhysicsProjectileSystem` (physical trajectory/collision, common FPS usage), plus a separate instant `HitscanSystem` for ray/rewind weapons. Shared launch, impact and damage systems remain independent. Policy selection is per Entity/weapon configuration, **not** by RTS/FPS genre.
- Verification: documentation-only commit; no implementation, C++ build or tests for these proposed systems yet. Existing ballistic flight is only a starting point for physical mode.
- Next: implement kinematic motion as an independent shared ECS system with deterministic target loss and rollback tests, then adapt RTS; preserve existing FPS hitscan and implement physics/hitscan adapters only with parity proof.


### 2026-11-18 — Genre-independent fixed-point regen policy foundation
- Implementation SHA: `d6ae0b68fe23762b567735c8a613a91d58693d3e`.
- Added `libraries/include/aiverselib/genre/FixedRegeneration.h`: pure `stepFixedResourceRegeneration(current,maximum,rate,dt)`; exact fixed-point operation order matches existing RTS `Unit` regen. No ECS or genre-module dependency, so multiple ECS systems can reuse it without duplicating math.
- Verification: GitHub source commit only, **no native compiler or runtime executed**.
- Next: wire `addEnergyRegenerationSystem` and real RTS `sysEconomy` into the same policy while keeping `Unit` canonical; then add a focused cross-host policy smoke regression.


### 2026-11-18 — First kinematic projectile ECS system implementation
- Implementation SHA: `55a9ecc5c074b75e63907cd41db7cfb0af1d138d`.
- Added `libraries/include/aiverselib/genre/EcsKinematicProjectileSystems.h`: `KinematicProjectileComponent` (origin/current/destination, owner/target, shotId, elapsed/duration, alive), pure `stepKinematicProjectile` fixed-point endpoint interpolation per simulation tick, `KinematicArrivalEvent`, and `addKinematicProjectileSystem`. The system iterates the ECS pool in dense order, stages arrival events, emits them after iteration, and defers destruction. No collision world, physics forces or RNG. Target tracking is **not** implemented: the endpoint is fixed at launch, and the arrival sink must resolve stale/dead targets.
- Verification: GitHub source commit only, no native compilation or CTest. Not registered as a standalone library package or connected to RTS yet. Physics and hitscan systems remain separate existing/planned paths.
- Next: add independent host tests for travel duration, event ordering, expiry and snapshot rollback; register public package/skill and CMake; connect real RTS adapter behind parity gate.


### 2026-11-18 — ECS energy regeneration policy deduplication
- Implementation SHA: `9f1fd207014a4b223e47ab3df885b769da95d550`.
- Changed `libraries/include/aiverselib/genre/EcsTimedMechanics.h` to call the shared pure `stepFixedResourceRegeneration` rather than maintaining independent Fixed arithmetic. Its existing zero floor, valid rate gate and maximum cap are unchanged, and authoritative EnergyRegenerationComponent continues to live in ECS.
- Verification: static source reasoning/edit only; **not compiled/tested**. Existing `ecs_cross_genre_timed_mechanics_test` is the validation gate.
- Next: port direct RTS Unit health/energy policy calls without moving their existing authoritative component storage.


### 2026-11-18 — Kinematic projectile native cross-host regression source
- Implementation SHA: `a5bca3ddeb191613bc2af7033362a05861caf213`.
- Added `tests/ecs_kinematic_projectile_test.cpp`: unrelated strategy/shooter `GameModule` hosts install the same `addKinematicProjectileSystem`, spawn three fixed-endpoint projectiles with distinct durations, check stable arrival order, exact fixed-point destinations, deferred despawn, and snapshot replay checksum parity with both deep and COW snapshots. Assertions use throwing `check`, so Release/NDEBUG does not disable them.
- Verification: GitHub commit only. Test **not compiled or executed**, not registered in CMake or standalone test runner yet. Do not claim RTS gameplay integration or native test success.
- Next: wire test into CMake/standalone suite and library catalog, compile; then connect real RTS projectile adapters behind compatibility gate and preserve old target/death semantics.


### 2026-11-18 — Real RTS ECS Unit health/energy regeneration reuses generic policy
- Implementation SHA: `642bf57afec14ed3663ea404c13ddfef9df423af`.
- Changed `modules/rts/RtsEconomy.cpp`: within the real `sysEconomy` ECS `w.each<Unit,Owner>` loop, health regen and energy regen now call `lib::genre::stepFixedResourceRegeneration` instead of duplicating fixed-point arithmetic; `Unit.hp`/`Unit.energy` remain the sole authoritative ECS fields. Preserved original conditions, `Stat::MaxHp` cap, `Stat::EnergyRegen` modifier, multiplication order, stage timing and save/replay layout. No separate copy/mirror or genre-specific fork of the calculation.
- Verification: source change committed. **Native build, RTS scenario checksums and lockstep/rollback comparisons not executed**; those remain required before declaring parity. This is real shared-policy adoption inside an existing RTS ECS system, **not full RTS mechanic extraction**.
- Next: add focused pure regression test for rates, caps, positive/negative values, and continue migrating real combat/projectile paths after native validation.

### 2026-11-18 — Kinematic CMake registration
- Implementation SHA: `19e4cae29c8e6cb36aa5bfd847b163287c8af978`.
- Changed `CMakeLists.txt`: registered `aiverse_ecs_kinematic_projectile_test` and CTest `ecs_kinematic_projectile_test`, linked to `aiverse_engine`, `aiverse_world` and `AIVERSE::libraries` without RTS/FPS gameplay modules.
- Verification: source commit only; no CMake configure/build/CTest run.
- Next: wire standalone runner and run native build; check compile errors and test behavior.

### 2026-11-18 — Kinematic standalone runner registration
- Implementation SHA: `61f784a00b36eb8673704d2cda06d30e34e70062`.
- Changed `tests/run_ecs_experiment_suite.sh`: added `ecs-kinematic-projectile` compile/run entry with public headers and the existing simulation, fixed-point, JSON and collision sources. Matches the standalone cross-genre projectile link pattern.
- Verification: source commit only; shell syntax, compiler, executable and CTest **not run**.
- Next: validate both runners, register the package/skill and migrate the real RTS adapter after behavioral parity analysis.


### 2026-11-18 — Shared Fixed regen legacy parity fixture
- Implementation SHA: `f67c3ebe5e2177bfef772b45135c167bb4d0f7c2`.
- Added `tests/ecs_fixed_regeneration_policy_test.cpp`: compares the new generic policy to the exact original RTS Fixed expression across positive/negative rates, varying caps and fractional values; additionally checks saturation and negative energy regeneration behavior.
- Verification: GitHub commit succeeded; **no native compilation or test execution**. Next: register in CMake, standalone ECS runner, catalog test dependency, then check actual RTS scenario checksums.


### 2026-11-18 — Regen policy CTest registration
- Implementation SHA: `4612b8509791f535fd1e5b7d1caae15b243ef64d`.
- Registered `aiverse_ecs_fixed_regeneration_policy_test` under CMake CTest using public `AIVERSE::lib_ecs_timed_mechanics` and engine headers; independent of RTS/FPS module linkage.
- Verification: CMake text committed; **configure/compile/CTest not run**. Next: include fixture in standalone ECS suite, update catalog/skill, check native results.


### 2026-11-18 — Fixed regen parity standalone suite integration
- Implementation SHA: `ebe06f36e9f70efddb21dc366fb9ac06f2195bc8`.
- Changed `tests/run_ecs_experiment_suite.sh` to build and run the focused fixed-point regen parity fixture independently, using the public header and the engine Fixed implementation only.
- Verification: script committed through GitHub; **no bash -n, native compilation or execution performed** because a repository checkout was unavailable.
- Next: update native catalog's dependency list, skill and persistent issue; execute on an agent with repository checkout.


### 2026-11-18 — ECS timed package dependency tracking, formatting retained
- Implementation SHAs: `f2cde8e24c0a6f8d02307d78c39fff236e3509a0` (catalog policy/test dependency additions), `9bb95763a5af741a222ca18eb522251efc977ec4` (surgical reformat correction to avoid rewriting unrelated catalog entries).
- Changed `libraries/catalog.json`: the existing `ecs-timed-mechanics` package now explicitly tracks `FixedRegeneration.h` and its policy test; still 123 catalog entries. Preserves existing package source paths and public target.
- Verification: JSON parsed with 123 packages during the edit; **native catalog filesystem check/CTest not run**.
- Next: keep source pins/skills synchronized and finish engine-wide test validation before production enablement.


### 2026-11-18 — Timed ECS skill updated for live FPS/RTS adopters
- Implementation SHA: `373600d3047bbf6979c1b0ec7e4f6ba0e5a424fb`.
- Updated `skills/gameplay/aiverse-ecs-timed-mechanics/SKILL.md`: names real FPS `stepWeaponReload` and RTS `stepFixedResourceRegeneration` adoption; forbids parallel authoritative component copies and preserves tick-order parity requirements. Specifies `ecsReloadAuthorityParity` and `ecs_fixed_regeneration_policy_test` as incremental gates.
- Verification: docs-only commit; native execution remains unverified.
- Next: fix any native CI failure, then isolate full FPS/RTS subsystem stages with explicit component ownership.


### 2026-11-18 — Continuation static integration audit (no native execution)
- Read exact current branch source for `FpsModule.cpp`, `RtsEconomy.cpp`, `EcsTimedMechanics.h`, `FixedRegeneration.h`, FPS/regren fixtures, CMake, experiment suite, catalog and skill (10 files).
- Eight focused **static** assertions passed: both ECS+FPS call `stepWeaponReload`; both ECS+RTS use `stepFixedResourceRegeneration`; misplaced dynamic indexing fixture no longer appears in actor test; FPS reload parity fixture is invoked; fixed regeneration CTest and standalone script registration exist; catalog parses as 123 packages; agent skill includes live integration guidance.
- GitHub Actions examined for `92a165d3`: CI was **cancelled**, other three workflow runs were **queued**. Neither status establishes native compilation or passing gameplay tests. Git clone of the repo from execution environment is DNS-blocked; avoid claiming compiler/CTests passed.
- Next: on a native clone run `python3 tools/libraries/catalog.py check`, `bash tests/run_ecs_experiment_suite.sh`, build `aiverse_fps_tests` and run `ctest --output-on-failure -R 'fps|ecs_'` with RTS replay/save baseline; investigate any failures before merging or dropping legacy data.


### 2026-11-18 — Kinematic ECS adds nonphysical pursuit mode
- Implementation SHA: `58ca3b611a059b38146416a5d21f9a8bef211d55`.
- `libraries/include/aiverselib/genre/EcsKinematicProjectileSystems.h` now supports two **nonphysical** movement policies: existing deterministic `TimedEndpoint` interpolation and `Pursuit` travel by a fixed distance per tick toward a resolvable destination. Added `advanceKinematicPursuit` with existing `moveTowards` fixed-point semantics; `addKinematicProjectileSystem` accepts an optional destination resolver for moving targets and distinguishes hit arrival from lifetime expiry (expiry emits no hit event).
- Default TimedEndpoint call sites remain source-compatible. Target resolver receives `Simulation&` and `const KinematicProjectileComponent&` and returns `Vec3`; missed/dead targets can keep the last known destination by default.
- **Unverified:** GitHub source commit only, no C++ build, no actual RTS module integration or snapshot execution yet. Next: replace nonphysical RTS movement calls with this same shared policy, then test moving-target and timeout behavior.


### 2026-11-18 — Live RTS movement consumes shared nonphysical kinematic policy
- Implementation SHA: `83336082c896835643a0da78e1129f921d430c13`.
- Changed `modules/rts/RtsCombat.cpp::sysProjectiles`: both non-programmed projectile movement paths now call `lib::genre::advanceKinematicPursuit` from `EcsKinematicProjectileSystems.h`, using the existing `Transform::pos`, current target/aim and identical per-tick movement budget. The helper delegates to the exact prior fixed-point `moveTowards` operation. Standard weapon missiles continue homing toward the current target position; ability projectiles keep their arc, collision and `ProjectileProgram` branches untouched.
- **Actual RTS integration:** existing RTS `Projectile` and `Transform` are ECS components and the live `projectiles` system now consumes the shared library's deterministic motion policy; hit/damage/splash/onHit ordering is still unchanged within legacy `sysProjectiles`. This does **not** yet register `addKinematicProjectileSystem` for RTS or move its damage stage into standalone generic systems. Legacy save/checksum layout unaffected by this source change, but native parity is still unverified.
- Verification: pushed to GitHub only. Native compile, save v9, rollback and in-game projectile parity have **not** run.
- Next: fix invalid-pursuit input mutation behavior if needed, add moving-target/timeout parity tests and register them, run native builds/CTest. Refactor `sysProjectiles` by behavior only after parity evidence; preserve ability and program variants.


### 2026-11-18 — Non-mutating validation of pursuit projectile speed
- Code SHA: `58b3447e1a4ce439d52fda6a059e386d1af9016e`.
- `stepKinematicProjectile` now rejects invalid/nonpositive pursuit distance-per-tick *before* incrementing elapsed ticks or changing the projectile. TimedEndpoint semantics remain unchanged.
- Verification: source commit only, C++ build and execution not run. Follow up with moving-target/timeout/invalid-input snapshot tests.


### 2026-11-18 — Kinematic moving-target, timeout and rollback coverage
- Test SHA: `281cf92ac63c52a42ce1058c68780faf706d9f44`.
- Extended existing CMake/standalone `tests/ecs_kinematic_projectile_test.cpp` with one moving `MovingTarget` ECS entity; a `Pursuit` shot with target position resolved every tick; and another pursuit shot that expires before reaching its destination. Expected stable arrival sequence is `100,101,200,102`, and moving-target arrival is x=11/4 fixed-point, with no arrival for shot 201. Checks non-mutating zero-speed rejection, despawn, deep snapshot and COW rollback checksum parity in two unrelated host modules.
- Verification: source pushed only; **no C++ build, test execution or in-game RTS parity run**. This fixture is intended to validate the new generic pursuit logic. Note real RTS motion call site SHA `83336082` still delegates to exactly the previous fixed-point `moveTowards`, preserving original arithmetic.
- Next: run native CTest and standalone suite, repair actual compilation/runtime failures, add real RTS integration checks for moving and lost targets, then separate motion/impact scheduling when proven.


### 2026-11-18 — Kinematic library no longer depends on physics pipeline
- Implementation SHA: `184b186977d7fb00944ecb7613015c43b89bdfec`.
- Changed `EcsKinematicProjectileSystems.h` direct includes to `EcsMechanicSystems.h` + `engine/core/Fixed.h` instead of `EcsProjectileSystems.h`. RTS clients no longer pull the physical `Weapons.h` / `SpatialWorld.h` headers transitively just to run nonphysical kinematic pursuit; neither collision provider nor physics simulation is required. Generic `Simulation` and deterministic fixed-point movement remain required.
- Verification: GitHub commit only, compilation and existing RTS/FPS project validation **not run**. Previous test SHA `281cf92` had CI #1265, ECS experimental #415, Studio #932 and GPU batching #536 queued at inspection.


### 2026-11-18 — Public kinematic projectile AI skill
- Implementation SHA: `6f1f007f96cb9c4326e7d37c86e1de401fb9c62a`.
- New `skills/gameplay/aiverse-ecs-kinematic-projectiles/SKILL.md` documents TimedEndpoint and Pursuit modes, public native API, impact/damage composition, moving/dead target behavior, and RTS compatibility boundaries. It explicitly distinguishes shared movement policy reuse in live RTS from a full `addKinematicProjectileSystem` RTS integration.
- **Verification:** documentation commit only; skill not auto-registered through library catalog until follow-up commit. The skill's CMake package name is the planned catalog target, which must be registered next.


### 2026-11-18 — Published separate native ECS kinematic package
- Implementation SHA: `0fe1b61d7f2e85112e86e278eb20dac02c469a56`.
- Updated `libraries/catalog.json` with `ecs-kinematic-projectiles` at `AIVERSE::lib_ecs_kinematic_projectiles`; because the project creates per-skill INTERFACE targets from this catalog in `libraries/CMakeLists.txt`, no manual target edits are needed. Declared `aiverse_engine` dependency and zero physics-world dependencies. Associated AI skill SHA `6f1f007` and tests `ecs_kinematic_projectile_test`.
- Validated that the modified catalog is syntactically valid JSON and has unique package IDs in the connector update script. **No native CMake configure/build or test execution** has been performed; package registration in a real CMake run must still be confirmed.

### 2026-11-18 — Null-target guard in pursuit regression
- Code SHA: `c1575e21c6ed3119ccbcbf965a63921135e187ed`.
- The destination-resolver fixture checks `shot.target.valid()` before attempting an ECS target lookup. For a null or missing target it deterministically retains the shot's last known destination, including the timeout test projectile.
- Verification: source commit only, not built. The real RTS currently performs its own `w.alive(pr.target)` target validity checks, not this test's resolver.


### 2026-11-18 — Shared deterministic ordered projectile impact policy
- Implementation SHA: `c44f3638788e4b18d62e4724082bd58b9d9574db`.
- Added public `libraries/include/aiverselib/genre/EcsOrderedImpactSystems.h` with `OrderedProjectileImpact` (ECS projectile, shooter, target, Fixed damage, location and splash radius) and `resolveOrderedProjectileImpact` which preserves canonical per-projectile order: resolve direct target, stage direct-hit effects, visit eligible splash candidates in the host spatial query's order. Prevents applying splash twice to the primary target. All armor/status/faction/distance logic remains injected by caller callbacks. No `SpatialWorld`, RTS/FPS module, or rigid-body dependency.
- Intent: extract the live RTS direct+splash impact block without batching/reordering damage (which could change shields, callbacks and network replay). This generic resolver is a policy used from an ECS gameplay system, not yet a separately scheduled impact system.
- Verification: GitHub source commit only; no compilation or CTest observed. Next: integrate into `RtsCombat.cpp::sysProjectiles` and add portable callback-order regressions.


### 2026-11-18 — Live RTS direct/on-hit/splash damage routed through shared ordered impact policy
- Implementation SHA: `86b3da1e56489460b395f2ffd91417cc8ea7286a`.
- Changed `modules/rts/RtsCombat.cpp::sysProjectiles` for **ordinary non-programmed weapon projectiles** only: after shared kinematic pursuit reaches the current target aim, it calls `lib::genre::resolveOrderedProjectileImpact`. The generic helper dictates direct damage, on-hit payload staging, then splash candidate traversal, skipping the primary target. Injected RTS-specific callbacks preserve `w.alive/has<Unit>`, `dealDamage` (armor, native/script hooks, statuses/shields, faction effects), `gs.hash.query`, `hostile`, `dist2D` and payload scheduling, with the original `w.destroyLater` placement. `sysProjectilePrograms`, `impactProjectile` ability path, arcs, environmental collision and per-projectile order are untouched.
- This reduces duplicated impact ordering in live gameplay without introducing a second ECS state owner or delaying damage to another system barrier. It is a reusable ECS policy called from the existing RTS ECS tick, **not** yet a separately scheduled universal damage system. The generic `HitPointsComponent` uses integer HP and cannot naively replace RTS fixed-point/detailed `dealDamage`.
- Verification: GitHub source commit only, **not built/tested**. Mandatory parity cases: moving targets, direct kills, splash team filters, onHit payload sequencing, shield and status hooks, save/checksum/replay, editor and network rollback.
- Next: exercise this policy with independent host callback-order tests, publish reusable skill/catalog, run CI when available, and only then consider full separate commit system behind parity gate.


### 2026-11-18 — Regression source for shared ordered ECS impact dispatch
- Implementation SHA: `1cc7d9ba1fbe9de60b40ff8dd580fcc60e876958`.
- Extended `tests/ecs_kinematic_projectile_test.cpp` (already registered in CTest and standalone ECS suite) to include `EcsOrderedImpactSystems.h` and exercise `resolveOrderedProjectileImpact`. Fixture constructs actual ECS Entity handles, records callback order `direct, onHit, spatialQuery, splashEnemyA, splashEnemyB`, skips the direct target in the splash query, filters allied candidates, checks direct damage happens before splash, and confirms zero-splash impacts never run the spatial query. Existing cross-host motion and rollback tests remain.
- Verification: source committed in GitHub only. No native compiler / CTest run; assertions use `check` with exceptions so they are active in Release. The scenario covers **policy dispatch semantics**, not full RTS damage hook integration.
- Next: publish this API in the native library catalog/AI skill, and obtain native build plus gameplay/rollback parity results.

### 2026-11-18 — Shared ordered-impact native agent skill
- Code/documentation SHA: `26a829866a87ca90e2815df7b94115b8f7c8cba9`.
- Added `skills/gameplay/aiverse-ecs-ordered-impact/SKILL.md`: tells project agents to use reusable ECS impact ordering independently of projectile motion mode, preserve per-projectile direct/on-hit/splash sequencing, and inject project-specific armor, faction, Lua/status policies. Documents real RTS integration scope and pending FPS/ability adapters.
- Verification: skill file committed, not executed; catalog package target named by the skill must be registered in the next change.

### 2026-11-18 — Public native package registration for ordered ECS impact
- Implementation SHA: `3bf1227543d3f5758fca69a8a8423cb50a295dde`.
- Updated `libraries/catalog.json` to publish `ecs-ordered-impact` at `AIVERSE::lib_ecs_ordered_impact` with the public policy header and AI skill. The CMake catalog-driven INTERFACE target mechanism provides the root/include directories and `aiverse_engine` dependency without a collision-world library. The modified catalog was parsed with JSON.parse and duplicate IDs were rejected before commit.
- Native CMake configure, C++ build and test execution remain **not performed**. Use the existing `ecs_kinematic_projectile_test` registered target to exercise the ordered resolver.


### 2026-11-18 — Real RTS programmed weapon hits use shared ordered ECS impact policy
- Implementation SHA: `120a26b16c28cc4f5262cc61c84a3eb8b9ee529d`.
- Changed `modules/rts/RtsProjectilePrograms.cpp::sysProjectilePrograms`, the `impact` lambda's *weapon* branch (`shot.effList < 0`): routes fixed-point scaled direct damage, immediate on-hit payload and index-sorted splash through `lib::genre::resolveOrderedProjectileImpact`. The callback for splash querying retains **the original** `gs.hash.query` eligibility/distance test and explicitly sorts collected entities by `Entity::index` before applying damage; damage remains immediate for each candidate. The old `dealDamage` armor/shield/status/script behavior remains unchanged.
- **Important semantics:** ordinary weapon projectiles queue on-hit payloads and use query order; programmed weapon shots execute on-hit payloads immediately and sort splash by entity index. These are deliberately preserved as separate RTS adapters despite using the same generic dispatch policy. Programmed *ability* projectiles (`shot.effList >= 0`) remain on their original `applyPayload` path; this change does not alter their effects or projectile motion. No new ECS state pools or save data were introduced.
- Verification: source pushed via GitHub only; no native compile, CTest, cross-platform determinism, save v9, game replay, or network rollback verified. Next: add an explicit regression for host-provided splash ordering, then obtain native build/CI results before merging.


### 2026-11-18 — Programmed weapon impact ordering regression
- Implementation SHA: `e6ffbab29ff026d6342d5b309bb29b2423427416`.
- Extended `tests/ecs_kinematic_projectile_test.cpp` with `exerciseProgrammedImpactOrder`, using real ECS Entity handles to verify synchronous direct damage, *immediate* on-hit callback, and program-specific splash visiting eligible targets in Entity index order even when the mock spatial query returns them in reverse order. The primary target and out-of-range candidate are filtered, and callback order is asserted using exception-based checks. Both ordinary impact and kinematic/COW tests remain in the same CTest target.
- This is a policy-level fixture modelling the adapter's filtering/sorting, not a full in-game `RtsProjectilePrograms` simulation. Run native build and existing RTS projectile-program tests before accepting compatibility.
- Verification: GitHub source commit only; C++ compilation and execution not performed.


### 2026-11-18 — RTS target links public ECS kinematics and impact packages
- Implementation SHA: `81eae27a9594ab1d292b4affabe04a524478deb2`.
- `CMakeLists.txt` now explicitly links `aiverse_game` PRIVATE to catalog-generated `AIVERSE::lib_ecs_kinematic_projectiles` and `AIVERSE::lib_ecs_ordered_impact` after `add_subdirectory(libraries)`. This declares the runtime's native dependencies rather than relying only on the header paths inside the RTS source.
- The underlying libraries remain header-only, with no physics world requirement for the kinematic path and no new runtime state or persistence layout. External native projects can use the same public packages via the catalog.
- Verification: source commit only. Catalog syntax was validated when its packages were added; **CMake configure, native RTS compilation, linker and gameplay tests have not run**. A build is needed to confirm catalog-generated targets and ABI in the active toolchain.


### 2026-11-18 — Shared deterministic hitscan candidate query policy
- Implementation SHA: `ae9ee7b975139250674c952876a28994bddff5ca`.
- New public `libraries/include/aiverselib/genre/EcsHitscanSystems.h` exports `HitscanCandidate<T>` and `selectClosestHitscanCandidate`. Inputs: candidate range (live ECS-derived view or immutable rewind snapshot), origin, delta, maximum allowed fraction, eligible predicate and hitbox provider. Uses existing `world::intersect` fixed-point slab query and strict `fraction < best`, retaining original source range order for equal-distance ties. No projectile motion integration or extra RNG; real source and target identities remain the caller's.
- This is a **shared raycast selection policy**, not an independently registered `HitscanSystem` or a replacement for environment/vehicle obstruction. Next connect real FPS `traceShot` to this policy without touching rewind selection or damage order.
- Verification: connector GitHub commit only; native compilation/CTest have not run.


### 2026-11-18 — Live FPS hitscan and lag compensation use shared deterministic selection
- Implementation SHA: `bbd22a2e34d11a5600d08c742968cefe6e9e9b15`.
- Changed `modules/fps/FpsSystems.cpp::traceShot` to invoke `lib::genre::selectClosestHitscanCandidate` for victim Actor intersection instead of duplicating the per-target slab query/nearest-fraction loop. The caller supplies the original eligibility test (exclude source/dead actor or stale life), original `actorBounds(target,config_.motor)`, and *unchanged* `targets` collection, which may be FPS rewind history chosen earlier in `Module::shoot`. The helper retains strict `fraction < best`, equal-distance tie semantics, and best fraction initialization of `1_fx`.
- Unchanged: vehicle intersection order, static collider and material-penetration traversal, hit event emission, zone multipliers, armor, knockdown, lethal effects, and pre-existing `fps.ecsAuthority` direct `ActorRuntime` damage path. This makes live FPS use the same public hitscan policy that native projects of any genre can reuse; it is not yet a separately registered ECS hitscan system.
- Verification: GitHub source committed, C++ compilation and in-game rewind/network parity **not performed**. Next: add cross-host hitscan regression tests covering ties, invalid candidates, and no-hit, then expose package/skill and run native build.


### 2026-11-18 — Cross-genre shared hitscan candidate regression source
- Implementation SHA: `216cbfdd1163ed28cde5da5083a09172bdbfbc54`.
- Added `tests/ecs_hitscan_selection_test.cpp`: constructs candidate hitboxes independently of FPS/RTS modules, runs the same shared policy from FPS-style and RTS-style host instantiations, and checks fixed-point 3/10 nearest intersection, stable tie resolution, invalid/stale target eligibility, fallback to next valid snapshot pose, obstruction maximum-fraction cutoff and no-hit outcome. Exceptions enforce assertions even with NDEBUG.
- This is a pure hitscan candidate-selection test; it **does not** yet test a complete FPS rewind server, damage side effects or network replay. Native compile/run not performed. Next register with CMake/standalone runner, publish library metadata and skill, and verify build.

### 2026-11-18 — Registered native hitscan CTest
- Implementation SHA: `738ae12e49820e9a059d84f71aa2cd1e394557c9`.
- `CMakeLists.txt` registers `aiverse_ecs_hitscan_selection_test` and `ecs_hitscan_selection_test`, linking `aiverse_engine`, `aiverse_world` and `AIVERSE::libraries` without FPS or RTS gameplay modules. This exercises the shared public policy in unrelated host fixtures.
- Verification: GitHub source commit only; native CMake configure/compile/CTest not run.

### 2026-11-18 — Standalone hitscan selection build target
- Implementation SHA: `d2907c04f157f98bba70c4ee251cfe4f245da673`.
- `tests/run_ecs_experiment_suite.sh` now invokes `compile_run ecs-hitscan-selection` with public include path and the same Fixed/SpatialWorld source dependencies as other standalone projectile tests. No dependency on `modules/fps` or `modules/rts`.
- Verification: source commit only; shell script not executed in native repo.


### 2026-11-18 — Universal ECS ordered-impact queue system (new, opt-in)
- **Code SHA:** `bf80c4f272564a78c33af8fcfe6fde801a18c4ef`.
- Added `libraries/include/aiverselib/genre/EcsImpactQueueSystems.h`. New `OrderedImpactQueue` World resource (bounded pending list, monotonic sequence), `enqueueOrderedImpact` and `addOrderedImpactResolutionSystem` bridge the existing generic `resolveOrderedProjectileImpact` policy to a registered ECS Simulation stage. The queue is drained with a swap: requests submitted from damage callbacks execute at the **next barrier**, not recursively.
- All gameplay rules (friendly fire, armor, shields, statuses, target liveness, spatial radius and splash) remain host-injected callbacks, with no FPS/RTS dependency and no second authoritative HP/projectile state. It respects caller-controlled order, not a false global same-tick sort. **Compatibility warning:** the existing RTS immediate per-projectile damage path must not be wholesale replaced with a single end-of-tick barrier without proving identical effects ordering.
- **Verification:** GitHub file creation only, native C++ compile and tests **not executed**. This is a new public source header, not yet a catalog package or built-in FPS/RTS integration.
- **Next:** add shooter+strategy regression for callback/queue ordering and COW snapshots, publish the native catalog package and agent skill, register CTest and standalone runner. Verify native build before any legacy adoption.

### 2026-11-18 — AI skill for shared ECS hitscan
- Implementation SHA: `d1dbab0c76e66c134812614cac6a1c7790e756e0`.
- New `skills/gameplay/aiverse-ecs-hitscan/SKILL.md` describes shared fixed-point target selection, rewind snapshots, deterministic tie-breaking, native weapon/occlusion adapters, and composition with ordered impact/damage. It explicitly warns that the public policy is not yet a separately registered `HitscanSystem` and that real FPS integration still owns vehicle, penetration, damage and net rollback.
- Verification: GitHub documentation commit only; public library catalog target named in the skill is still pending registration.


### 2026-11-18 — Cross-genre ordered-impact ECS queue regression fixture
- **Code SHA:** `1f9d2c3235a960bd4203a3eb2007f13a6ab6b198`.
- Added `tests/ecs_cross_genre_impact_queue_test.cpp`, independently hosted by `ShooterTag` and `StrategyTag`. It constructs actual ECS entity health/team values and an `OrderedImpactQueue`, queues two impacts, checks synchronous direct -> on-hit -> area query -> eligible splash -> next direct order, prevents friendly/primary duplicate splash, and verifies a third impact enqueued from the on-hit callback remains pending until the next system barrier. Compares snapshot restore checksums for deep and COW World.
- **Verification:** GitHub file creation only. Test is not yet registered, C++ compilation/CTest/real FPS/RTS integration **not executed**. Need real native execution to establish these expected sequences.
- **Next:** publish skill/catalog/CMake target, run standalone and CTest, then separately integrate only compatible legacy motion/impact stages preserving exact live same-tick effects.

### 2026-11-18 — Public ECS hitscan package registered
- Implementation SHA: `1aaf349ba65147e37f894ae2c81f3513d15da464`.
- Published `ecs-hitscan` as `AIVERSE::lib_ecs_hitscan` in `libraries/catalog.json`, with `EcsHitscanSystems.h`, reusable AI skill and the independent `ecs_hitscan_selection_test` fixture. The library catalog was parsed as JSON and checked for duplicate package IDs before writing. Native games can now link this policy through the catalog-generated CMake INTERFACE target with `aiverse_world`.
- This does not imply full ECS-authoritative FPS hitscan or damage; real `FpsSystems.cpp::traceShot` consumes the policy while preserving FPS-specific rewind, penetration and damage.
- Verification: GitHub commit only; native CMake/configure/C++ tests not run. Next link FPS target to the public package and test cross-host parity.


### 2026-11-18 — Agent skill for queued ECS impacts
- **Implementation SHA:** `e4b5b30eb7ff0b02a5c051c1393cf7d5b9f247e7`.
- Added `skills/gameplay/aiverse-ecs-impact-queue/SKILL.md` describing public queue setup and registration, five policy callback stages, bounded event/sequence resource, deterministic processing and explicit legacy RTS immediate-side-effect warning. Cross-genre test and native linkage requirements are documented.
- **Verification:** document committed, not compiled. Catalog target in skill is not yet available until a following catalog update.
- **Next:** publish the catalog package, register CTest and standalone runner and verify native results.


### 2026-11-18 — Live FPS target explicitly links public hitscan package
- Implementation SHA: `4a8c483c049827e5044758b49cd0ad0f1ed95ca9`.
- `modules/fps/CMakeLists.txt` links `aiverse_fps` to catalog-generated `AIVERSE::lib_ecs_hitscan`, retaining `aiverse_world` and `aiverse_engine`. Root `CMakeLists.txt` loads `libraries` before `modules/fps`, so the CMake target is registered before use.
- Real FPS `traceShot` already uses shared deterministic target-selection API (`bbd22a2`). This declares its dependency explicitly; no gameplay state is added.
- Verification: source inspection and GitHub commit only; no native CMake configure/compile executed. Another agent is concurrently editing the handoff, so preserve all intervening entries before updating.


### 2026-11-18 — Native ECS impact queue package registered
- **Implementation SHA:** `ac5d8a776f543d1aad0491915f53d2710df6cc67`.
- Added `ecs-impact-queue` catalog package with public `AIVERSE::lib_ecs_impact_queue` INTERFACE target, depending on `ecs-ordered-impact`. This enables native project reuse without linking `RtsModule`/`fps::Module`. References header, skill and cross-genre test.
- **Verification:** parsed catalog JSON and verified IDs unique and count 127 at edit time. Native catalog filesystem check and CMake configure **not run**; source/test are additive only.
- **Next:** CMake CTest and standalone runner registration, executable compilation/rollback verification, then one real host adapter behind explicit parity gates.


### 2026-11-18 — Impact queue CTest native target
- **Implementation SHA:** `24ccbc7db168a2fcbc2985b1a7e87b6db252b527`.
- Added `aiverse_ecs_cross_genre_impact_queue_test` CMake executable and CTest case, linking only the generic engine and public `AIVERSE::lib_ecs_impact_queue` target. Built-in RTS/FPS modules are deliberately absent from this test's link dependencies.
- **Verification:** GitHub CMake edit committed, **native configure/compile/CTest not executed**. CI and static checks are separate gates.
- **Next:** register standalone experiment-runner command, then inspect native workflow output.


### 2026-11-18 — Live FPS hitscan direct actor damage uses shared ordered impact
- Implementation SHA: `7fd266c2aa6b66554d2239fa912a8fc5037f1213`.
- Changed `modules/fps/FpsSystems.cpp::traceShot` to call `lib::genre::resolveOrderedProjectileImpact` for an accepted direct Actor hit, after nearest historical target selection, vehicle occlusion, static-world/material penetration, hit event emission and FPS-specific head/limb multiplier. The direct callback still calls `Module::damage` exactly once with the original slot, head flag and amount; FPS armor/gear, knockdown, loot, kills, hit counters, projectile replay and opt-in `ActorRuntime` updates remain in the existing implementation. Instant hitscan has no splash or after-direct callbacks, so the generic policy only dispatches the direct callback; vehicles retain their existing separate branch.
- Both live RTS weapon projectiles and live FPS hitscan now use the same ordered impact policy, with different *game-specific* damage policies and trajectory/selection logic. This is **not** yet a standalone scheduled generic `DamageSystem` or complete FPS ECS-authoritative migration. No extra ECS component pools, state serialization or network messages introduced.
- Verification: GitHub source commit only; native compile, CTest and replay/network parity not performed. Critical scenarios: FPS shot with no source, same-team suppression, headshots and armor, multi-pellet, rewind pose, penetration and vehicle driver damage.


### 2026-11-18 — Standalone ECS impact queue regression runner
- **Implementation SHA:** `79eeca7b0d2668f67d2faf34cc13e0c4a46e5901`.
- Updated `tests/run_ecs_experiment_suite.sh` to compile/run the new two-host ordered-impact queue test with only generic `Simulation`, Fixed and Json sources and public library includes. This prevents accidental dependencies on `modules/fps` or `modules/rts`.
- **Verification:** source edit committed. `bash -n`, native build and actual execution **not run** in current environment (GitHub DNS unavailable for a full checkout).
- **Next:** run CTest, standalone script and sanitizer in native checkout; fix failures before enabling built-in gameplay paths.


### 2026-11-18 — Portable ECS impact queue capacity type
- **Implementation SHA:** `441bc126ec4f568bca99b826949fcce11dab5a3f`.
- In `EcsImpactQueueSystems.h`, explicitly included `<cstddef>` and used `std::size_t` for queue capacity instead of relying on an incidental global `size_t` declaration from another standard header. No gameplay semantics change.
- **Verification:** source edit only; no native compiler result yet. Run cross-genre impact queue CTest and standalone suite.

### 2026-11-18 — Cross-genre hitscan to ordered-impact integration regression
- Implementation SHA: `761beb341cd796b72cf7f9f3656d025c236bdaae`.
- `tests/ecs_hitscan_selection_test.cpp` now composes the shared `selectClosestHitscanCandidate` with `resolveOrderedProjectileImpact` in two independent host-style fixtures. The accepted nearest hit dispatches exactly one immediate damage callback against an ECS target entity; accidental on-hit/splash callbacks throw; the obstructed ray invokes no damage. Existing tie, stale-life and rewind-eligibility coverage stays intact. No RTS/FPS gameplay modules are linked.
- This is a standalone integration *test source*, not a native test pass or full authoritative client/server replay proof. Test registration was previously committed for CMake and standalone suite; validate link and CTest before accepting.


### 2026-11-18 — Impact queue overflow and capacity regression fixture
- **Implementation SHA:** `bef1f346551233b6bda0d352906bb94229892b11`.
- Extended `tests/ecs_cross_genre_impact_queue_test.cpp` with `exerciseQueueBoundaries`: rejects zero/overflowed sequence IDs and full (8192-event) queue submissions without allocating new IDs or changing existing pending events. Existing two-host ordering/COW regression remains.
- **Verification:** source committed only, assertions **not executed**. A native build must check both C++ compilation and full execution.


### 2026-11-18 — Whole-engine ECS migration plan updated for impact queue
- **Documentation SHA:** `1d5ce77437d7ab8acb9b4ca82d001902281e7aba`.
- Appended implementation status and compatibility caveats to `docs/ecs-all-features-migration-plan.md`; distinguishes the immutable 120-package audit baseline from the current catalog, and the new opt-in separately scheduled queue system from legacy RTS/FPS immediate hit implementations.
- **Verification:** docs-only source commit, native gameplay migration remains pending.
- **Next:** update AGENTS and issue tracker, perform static wiring audit, obtain compile/test evidence.

### 2026-11-18 — FPS native target now links both shared ECS impact and hitscan packages
- Implementation SHA: `2c52e9b5260fb9630bce76be9ec1745d3da8527a`.
- `modules/fps/CMakeLists.txt` links `AIVERSE::lib_ecs_hitscan` and `AIVERSE::lib_ecs_ordered_impact` explicitly. Live FPS `traceShot` now uses both generic policies but keeps its ECS ActorRuntime damage and vehicle, armor, rewind and penetration adapters.
- Verification: source commit only. Attempt to access GitHub from the available native container failed because DNS for github.com was not available; therefore no repository clone, compilation or tests were run. GitHub Actions status still needs a completed run; never treat queued/cancelled as passed.


### 2026-11-18 — Mandatory agent guidance for impact queue system
- **Implementation SHA:** `aea335dc94568437f45d3f53c4ea4b36f5fe18e0`.
- Updated `AGENTS.md` to direct subsequent agents to `AIVERSE::lib_ecs_impact_queue`, its skill and CTest, explicitly defining World-backed sequencer/pending impact ownership and the callback-next-barrier semantics. It prohibits replacing live RTS immediate direct/on-hit/splash effects with an end-of-tick batch without golden save/replay parity.
- **Verification:** docs-only commit, gameplay still not migrated to the queued barrier.
- **Next:** check package/test wiring, issue tracker and native CI; keep phased rollout gates.

### 2026-11-18 — New composable ECS hitscan request-resolution system
- Implementation SHA: `268518ca82f9efafe66376d028aa53cc1d059aac`.
- New `libraries/include/aiverselib/genre/EcsHitscanResolutionSystems.h` implements `HitscanIntentComponent` (origin/delta/maximumFraction/shotId/requested), `HitscanResolvedEvent` (source/target entity, shotId, point/fraction), and `addHitscanResolutionSystem`. Consumes one-shot ECS requests even when invalid, queries host-selected current or rewind candidates, checks eligibility/hitbox through shared nearest-ray policy, stages successful hits in stable source pool order and calls hit sink **after** traversal. No simulated projectile movement, new RNG, or internal damage application.
- This is a true separate ECS **system**, but it is not yet wired into the live FPS authoritative fire path. Existing FPS rewind hitscan stays on `FpsSystems.cpp::traceShot` using the shared policy; this new system's post-traversal damage callback order would differ from that legacy path if enabled without parity tests. No compiled/tested production guarantee.
- Next: add cross-host ECS fire-intent/snapshot tests; register catalog target and skill; then compare same-tick FPS and RTS hit ordering before changing live scheduling.


### 2026-11-18 — Versioned ECS impact queue persistence and full checksum
- **Implementation SHA:** `24083fb65f7633d0d6aa6d6090cf503991d77b90`.
- Added explicit Archive field IO for `OrderedProjectileImpact`, `QueuedProjectileImpact` and `OrderedImpactQueue` (format v1, fixed little-endian fields), bounded queue read validation, monotonic sequence validation, transactional `loadOrderedImpactQueue` that preserves the existing output on corrupt input, `saveOrderedImpactQueue`, and `hashOrderedImpactQueue` covering every authoritative event field including Entity generations, point, damage and radius.
- **Verification:** code committed through connector; **native compiler, serialization round-trip, truncated/invalid stream and actual rollback assertions NOT yet run**. Host must still invoke save/hash helpers from its GameModule for byte saves; World snapshot cloning covers the queued resource.
- **Next:** upgrade cross-genre fixture to call full hash and transactional save/load helpers; run native CTest and standalone suite before adopting as live FPS/RTS authority.

### 2026-11-18 — Native cross-host regression for schedulable ECS hitscan
- Implementation SHA: `437480afb61a680f706f82849519346a82f7d4df`.
- Added `tests/ecs_hitscan_resolution_test.cpp`: two unrelated ShooterStyle/StrategyStyle `GameModule` hosts install the real `addHitscanResolutionSystem` and create five ECS fire-intent entities. Two rays hit a fixed target in source ECS dense order (shot IDs 1 and 4), one is stopped by an occlusion fraction, one has invalid zero delta, and one is unrequested. Test asserts fixed-point intersection and hit event position, exactly-once intent consumption, no firing on the next tick, and deep/COW saveState/loadState replay checksums across both host styles.
- Verification: GitHub source commit only, no native compilation or CTest execution. No FPS production path switched to the new event-staging system; it changes same-tick damage ordering if used without a parity gate.
- Next: register the new test in CMake and the standalone ECS runner; publish a native package+AI skill; run compiler and replay tests.


### 2026-11-18 — ECS impact queue format/load/hash regression expansion
- **Implementation SHA:** `0349c8b7017c7ac4e5bbedd05cd3c6059f19792c`.
- `tests/ecs_cross_genre_impact_queue_test.cpp` now hashes every authoritative queue field using `hashOrderedImpactQueue` instead of a partial manual subset, and checks an in-flight pending queue's binary save/read exact round-trip, truncated input and invalid format rejection without replacing an already-good destination.
- **Verification:** GitHub edit committed, **actual native CTest and binary-format tests not executed**. Both unrelated host fixtures and COW snapshot assertions remain in this test.
- **Next:** update agent skill/schema documentation, then audit catalog/CTest/script registration and obtain native CI results.

### 2026-11-18 — Registered schedulable hitscan regression in native CTest
- Implementation SHA: `727d9ec271e258dac38787d300ca606f279c9eb0`.
- `CMakeLists.txt` now adds `aiverse_ecs_hitscan_resolution_test`, links `aiverse_engine aiverse_world AIVERSE::libraries`, and registers CTest `ecs_hitscan_resolution_test`. The new GameModule fixture uses only public ECS APIs, no real RTS or FPS module linkage.
- Verification: GitHub source commit only; no CMake configure/CTest or native runtime execution. Next: standalone runner registration and native library catalog package.


### 2026-11-18 — Native agent skill documents queued impact save/hash APIs
- **Implementation SHA:** `c5625676b797d516e55667470a295b12c1e61686`.
- Updated `skills/gameplay/aiverse-ecs-impact-queue/SKILL.md`: use provided `saveOrderedImpactQueue`, `loadOrderedImpactQueue` and `hashOrderedImpactQueue` from `GameModule` for stable versioned bytes and full checksum, while World snapshots already clone the resource. Explicitly warns that snapshots do not serialize the byte save format automatically.
- **Verification:** documentation only, native test remains unverified.

### 2026-11-18 — Standalone regression runner for ECS hitscan resolution
- Implementation SHA: `5588ca512d3866704363dc7ed006abb8a5612f7d`.
- `tests/run_ecs_experiment_suite.sh` now compiles/runs `ecs-hitscan-resolution` with the same public library and world/simulation/fixed-point sources as `ecs-hitscan-selection`. The test covers source order, invalid/occluded requests, consumed intent and deterministic deep/COW replay.
- Verification: GitHub code commit only. No native compiler or standalone suite executed here. Next: expose public package+skill and check outstanding CI.

### 2026-11-18 — Native AI skill for ECS hitscan resolution
- Implementation SHA: `22771304cc47862299f56c281bbd173c1986c33f`.
- New `skills/gameplay/aiverse-ecs-hitscan-resolution/SKILL.md` documents the component intent and five custom callback adapters for `addHitscanResolutionSystem`, choice of physics/kinematic/hitscan policies, independent snapshot tests, and the critical difference from live FPS synchronous same-tick damage ordering.
- Verification: skill file committed on the working branch. The described catalog target must still be published; no native compile/run.


### 2026-11-18 — ECS impact queue integration static audit (13/13)
- Inspected the working branch after package registration. **13 of 13 static wiring checks passed:** catalog IDs unique; `ecs-impact-queue` source/skill/target/dependency match; CMake CTest and standalone ECS runner reference the test; AGENTS and full migration plan reference package; the header exposes full versioned save/load/hash and bounded queue; the test exercises two unrelated hosts with COW and malformed-input cases.
- Catalog count at audit: 127 (other agents may add packages later; re-read before relying on this figure). Latest branch CI, Studio, ECS experiments and GPU batching workflow statuses were **queued**, not green.
- **Verification limitation:** static/source checks only; no successful native C++ compile, sanitizer, network rollback or gameplay parity evidence. **Do not enable as default in FPS/RTS or delete immediate-hit paths.**
- Next for a native-capable agent: `python3 tools/libraries/catalog.py check`, configure CMake, build `aiverse_ecs_cross_genre_impact_queue_test`, `ctest --output-on-failure -R ecs_cross_genre_impact_queue_test`, `bash tests/run_ecs_experiment_suite.sh`, then existing FPS/RTS regression and editor suite. Only adopt the queue in a live genre with proof of same-tick event-order parity.

### 2026-11-18 — Published native schedulable ECS hitscan resolution package
- Implementation SHA: `b12a198a090be09e11a90f8bc739dc00291c106a`.
- `libraries/catalog.json` now declares `ecs-hitscan-resolution` at `AIVERSE::lib_ecs_hitscan_resolution` and depends on the previously published `ecs-hitscan` policy. Source `EcsHitscanResolutionSystems.h`, public AI skill, independent test and composition doc are referenced in the catalog. JSON parsed and package IDs checked for uniqueness prior to push; per-package CMake target is generated by the existing `libraries/CMakeLists.txt` catalog loop.
- Verification: successful GitHub commit only, no CMake configure, native compilation or execution. No changes to authoritative FPS or RTS scheduling, network protocol or v9 saves. Keep new resolver opt-in pending gameplay parity.

### 2026-11-18 — Actual shared ECS hitscan -> damage pipeline regression
- Implementation SHA: `f8133ffed170cd9c5210872c8ffbaa2b30ff8f9d`.
- Updated `tests/ecs_hitscan_resolution_test.cpp` so each independent ShooterStyle/StrategyStyle host creates an authoritative `HitPointsComponent` on its ECS target and a `DamageQueue` resource. `addHitscanResolutionSystem` stages `DamageRequest` from successful ECS hit events; the next registered `addDamageResolutionSystem` applies two 25-point hits in stable source order. Test asserts `DamageApplied` shot IDs [1,4], remaining HP [75,50], target HP=50, drained queue, no duplicate damage after the next tick, and checksum parity through deep/COW snapshot restore.
- Both systems are actual shared ECS systems with no FPS/RTS module dependency. This validates intended end-to-end damage composition *in test source* but does not migrate authoritative FPS `traceShot` to staged damage; doing so would alter same-tick feedback without a parity gate.
- Verification: GitHub commit only; C++ compilation and test execution still not performed. CTest and standalone runner already include this test target.

### 2026-11-18 — Hitscan catalog dependency order hardened for CMake
- Implementation SHA: `9794f5583d014a933a683d5a29387514af711927`.
- Reordered catalog entries so `ecs-hitscan` is declared immediately before dependent `ecs-hitscan-resolution`. The per-package CMake INTERFACE target generation now encounters `AIVERSE::lib_ecs_hitscan` before `AIVERSE::lib_ecs_hitscan_resolution` links to it; this avoids any reliance on deferred namespaced target resolution. JSON parsed after the reorder and adjacent package ordering was validated.
- Verification: GitHub commit only; no CMake configure or actual native build.

### 2026-11-18 — Agent contract records schedulable hitscan and same-tick parity guard
- Documentation SHA: `d2bcde0fc629394ca38c64e766c2a6dac688d1e2`.
- Updated `AGENTS.md` with `AIVERSE::lib_ecs_hitscan_resolution` and `AIVERSE::lib_ecs_hitscan`, their public headers/skills, and the new cross-host `ecs_hitscan_resolution_test` combining fire intents with the actual shared `DamageQueue` and `DamageResolutionSystem`. Reinforces that FPS synchronous multi-hit/rewind/armor order cannot be changed without golden tests and network/checksum equivalence.
- Verification: documentation commit, no native game or library test run. The user requested continued implementation; remaining primary gate is native compile and migration of real FPS execution without reordering damage.


### 2026-11-18 — Shared integer health commit for ECS damage resolution
- Implementation SHA: `e9990503283b3bfe8ead7994a387f15f8551007b`.
- `libraries/include/aiverselib/genre/EcsDamageSystems.h` adds `applyIntegerHitPointDamage(int32_t&,int32_t)`, an immediate, allocation-free deterministic HP update after armor/faction/shield and other host policies are already applied. `addDamageResolutionSystem` now calls the same primitive. Handles nonpositive damage/current without mutation and large damage without signed subtraction overflow.
- Verification: GitHub source edit only; **native compiler, CTest, real FPS/RTS parity NOT run**. Next: adopt same helper in live FPS `damage` after knockdown resolution with the old event ordering unchanged; add edge-case regression and run FPS save/rollback checks.


### 2026-11-18 — Live FPS damage uses same HP commit as generic ECS system
- Implementation SHA: `8aebe28d5f0e95107bcf6b9eadee6558b1325c00`.
- Changed `modules/fps/FpsSystems.cpp::Module::damage` to invoke the shared `lib::genre::applyIntegerHitPointDamage(target.health,amount)` from `EcsDamageSystems.h` after the existing gear/armor and knockdown checks. The same primitive now executes inside the generic `addDamageResolutionSystem` and the live FPS path. Existing immediate health updates, source/hits/kill credit, loot, death events and authoritative `ActorRuntime` writeback are left in their original call order and save v9 layout.
- **Scope:** this is real cross-genre shared-algorithm use, **not** independent FPS `HitPointsComponent` authority or queued ECS damage adoption. Do not add a second mutable HP owner while `ActorRuntime` remains canonical. RTS fixed-point detailed `dealDamage` is intentionally unaffected by the integer primitive.
- Verification: GitHub commit only; **native compilation, FPS tests, rollback/save and real gameplay parity remain unverified**. Next: add edge-case policy tests, run native FPS ECS-authority ON/OFF and network suites, then incrementally split semantic stages without reordering effects.


### 2026-11-18 — Integer HP shared policy exhaustive regression fixture
- Implementation SHA: `47e9f8123a67d2b26812c62a21d51cc4afa34b63`.
- Added `tests/ecs_integer_health_commit_test.cpp` comparing the new common HP commit against the former FPS `std::max(0,hp-damage)` for valid 0..100 health and 0..1000 incoming damage; tests applied-damage amount, zero/dead/negative health behavior, nonpositive damage and INT32_MAX overflow safety. The header-only test intentionally links no built-in genre modules.
- Verification: GitHub test source committed only. **Compiler and test execution not yet performed**; register it in CMake/standalone test suite next and run alongside existing FPS ECS-authority and transport regressions.


### 2026-11-18 — Integer HP shared policy CMake/CTest integration
- Implementation SHA: `01b97620a62f1e1106330a22e72bcb9c35a5160e`.
- `CMakeLists.txt` now declares `aiverse_ecs_integer_health_commit_test` using public `AIVERSE::lib_ecs_impact_damage` and `aiverse_engine`, registered as CTest `ecs_integer_health_commit_test`. It does not link genre-specific RTS/FPS targets.
- Verification: GitHub CMake source edit succeeded; **native configure/build/CTest execution not yet verified**. Next: standalone suite registration and link check.


### 2026-11-18 — Standalone suite includes shared HP commit regression
- Implementation SHA: `ce2ab7f5cd95784c0a5845308f1edb2e2b29362d`.
- `tests/run_ecs_experiment_suite.sh` now compiles/runs `ecs_integer_health_commit_test.cpp` from the public library include with the engine Fixed implementation, independent of FPS/RTS modules.
- Verification: connector file update only; **native `bash -n`, compilation and execution still not run**. A full native test matrix including `aiverse_fps_tests` remains mandatory.


### 2026-11-18 — Real FPS programmable-projectile phase split out of monolithic tick
- **Implementation SHA:** `28218ca43083764103dc43ee4cfbc0b32435e0d5`.
- Changed `modules/fps/FpsModule.cpp`: registered `fps.compat.projectilePrograms` as an independent ordered `Simulation::addSystem` stage immediately after optional `fps.ecsHydrate` and before `fps` (which still calls `tickSystems`, royale, rewind history and actor movement). Removed duplicate `tickProjectilePrograms` call from the main `tick`. The new stage guards on winner like the previous early return; it shares the same `State` workspace and calls the **same projectile algorithm**, retaining its per-pellet collision, split, damage, events and exact update order. No save v9/network schema changes.
- **Scope limitation:** this is a real scheduling boundary extraction but remains an explicit **compatibility system** over legacy State vectors. Not yet a universal `ProjectileFlightComponent` ECS authority conversion. Built-in FPS will use canonical `ProjectileRuntime` pools between ticks only with `ecsAuthority` gate, hydrated before this stage and committed at the existing mirror stage.
- **Verification:** GitHub source commit only, no compiler, CTest, FPS save, replay, transport or rollback proof yet. Existing `tests/fps/projectiles.inc` and `ecsDynamicAuthorityParity` are required, plus a new cross-storage programmed projectile fixture.
- **Next:** add actual FPS parity test with ECS authority ON/OFF over projectile flight and target damage, snapshot restore and save v9 cross-load; then run native FPS suite and fix failures.


### 2026-11-18 — AI skill updated for shared FPS integer health commit
- Implementation SHA: `7009970b39775377e038f09cf19bc696d3b51f66`.
- Updated `skills/gameplay/aiverse-ecs-impact-damage/SKILL.md` to identify the shared `applyIntegerHitPointDamage` primitive in real FPS `Module::damage` and generic `addDamageResolutionSystem`, clarify its required post-mitigation placement, and forbid secondary live FPS health authority while `ActorRuntime` remains canonical.
- Verification: documentation commit only; native C++ and FPS save/rollback parity still pending.


### 2026-11-18 — FPS real programmed projectile phase parity regression source
- **Implementation SHA:** `5ce137a9aac331e4e23834cdd5dd77338e6f1f81`.
- Extended `tests/fps/projectiles.inc` with `ecsProjectileProgramStageParity`: executes a real four-player FPS programmed fly-through bullet after the newly separated `fps.compat.projectilePrograms` stage, compares actor and projectile state plus full checksums on every tick between default State and `ecsAuthority=true` hosts, confirms deterministic damage and despawn, saves a pre-flight snapshot, reloads v9 bytes across both storage configurations, then restores and replays to the exact original checksum.
- **Verification:** GitHub source committed; **native compile and test not yet executed**, test function not yet invoked by `tests/fps/tests.cpp` main until next implementation step. Source is grounded on existing `projectilePrograms` fixture's known flyThrough health expectations; verify at runtime.
- **Next:** call the fixture from existing FPS test main, then run native `aiverse_fps_tests`, `fps_transport` and relevant CTest before further authority cutover.


### 2026-11-18 — Generic ECS DamageQueue versioned bytes and complete checksum
- Implementation SHA: `fb9542e0474f2903b93776b438ead0e6fe9b9241`.
- Extended `libraries/include/aiverselib/genre/EcsDamageSystems.h` with explicit `Archive` IO for `DamageRequest` and `DamageQueue`, versioned v1 layout, bounded 16,384-request input, `saveDamageQueue`, transactional `loadDamageQueue` and `hashDamageQueue` covering both generation-safe Entity IDs, shot ID and damage amount in enqueue order. This completes a shared persistence primitive needed by real host rollback/save integrations.
- Verification: source committed only, **native C++ compilation, save round-trip and live game/save parity still not executed**. Adding a serializer alone does not automatically wire `GameModule::serializeState` or hash to this resource.
- Next: add CTest round-trip/truncation/checksum mutation checks in the existing integer-damage fixture, update AI skill and complete native test gates before using it as authoritative pending damage in FPS/RTS.


### 2026-11-18 — Shared synchronous hitscan intent policy for live FPS and scheduled ECS
- Implementation SHA: `801ad2a943031cf83cbd781c0c2f71664ccfe5d3`.
- Changed `libraries/include/aiverselib/genre/EcsHitscanResolutionSystems.h`: added `resolveHitscanIntent` pure generic policy taking `HitscanIntentComponent`, shooter entity, current or immutable historical target range, eligibility, hitboxes and live-entity mapping. Returns both `HitscanResolvedEvent` and a pointer to the matched immutable pose, preserving FPS historical head/limb checks. The scheduled `addHitscanResolutionSystem` now calls this **same** policy, copying a request before clearing `requested`, and still batches callbacks after ECS iteration; no change to intended order or one-shot semantics.
- **No gameplay migration claim yet**: live FPS still calls the shared nearest-candidate helper directly until its own `traceShot` is migrated in a separate implementation commit. This helper enables in-tick immediate resolution without postponing damage to the next system barrier.
- Verification: GitHub code commit only, not compiled or exercised. Follow-up tests must check invalid/blocked intents, rewind pose lifetime, snapshot/rollback and multiple sources.


### 2026-11-18 — FPS programmed-projectile parity fixture enabled in test main
- **Implementation SHA:** `bd4c7f43f43c17742741e18329149883bdc2c366`.
- Changed `tests/fps/tests.cpp` to execute `ecsProjectileProgramStageParity()` directly after existing `projectilePrograms()` during `aiverse_fps_tests`. This ensures the live `fps.compat.projectilePrograms` stage is covered against both ECS authority and legacy storage in normal native FPS test runs.
- **Verification:** source edit committed, **C++ compilation, actual FPS suite, COW/sanitizer and network tests unverified**. Tests may identify ordering or save parity differences that must be resolved before production status.
- **Next:** inspect CI, verify latest branch source, and run `ctest --output-on-failure -R 'fps|ecs_'` plus transport/Editor gates on a native checkout.


### 2026-11-18 — Real FPS rewind hitscan consumes the shared ECS intent query
- Implementation SHA: `00f2f94da747517aa13e063a1d39d3e027551df0`.
- Updated `modules/fps/FpsSystems.cpp::traceShot` to construct an ephemeral `lib::genre::HitscanIntentComponent` (origin/delta/current maximum fraction, requested) and synchronously call the same `lib::genre::resolveHitscanIntent` policy used by the registered ECS `addHitscanResolutionSystem`. The selected historical `Actor` pose pointer is retained for FPS head/limb detection; its semantic victim slot and fraction are used unchanged. The target-to-Entity callback uses existing `actorEntity` (generation-safe identity), with no new pool or persistent field.
- Preserves FPS source/target exclusion, dead targets and life validation, source-order tie behavior, vehicle and world occlusion, penetration, per-pellet RNG and immediate `damage`/kill/loot/event ordering in the current tick. This is a **real shared algorithm in the live FPS path**, but does **not** yet move the live FPS fire-intent state into a canonical ECS pool or make its damage asynchronous.
- Verification: Github source commit only; no native C++ build, gameplay checksum or rollback test pass claimed. Next: add a paired legacy-vs-ECS multi-shooter/fatal-damage fixture, and check v9 save parity with actual native runner.


### 2026-11-18 — DamageQueue persistence and generation-safe checksum regression
- Implementation SHA: `b49dbb32b4dfecb1509f418e1f583be548a28cd7`.
- Extended `tests/ecs_integer_health_commit_test.cpp`: two ordered `DamageRequest` entries with generation-safe source/target Entity IDs are byte-serialized, reloaded, byte-compared, and hashed; corrupt format and truncated bytes must reject without changing the existing destination; altering source Entity generation must change checksum. Oversized 16,385-event queues must reject serialization.
- Verification: GitHub test edit succeeded; **no native compilation or CTest execution**. Test target already registered in CMake/standalone ECS runner. Next: update skill, audit source wiring and check CI before production use.


### 2026-11-18 — Agent skill requires versioned ECS DamageQueue persistence
- Implementation SHA: `2bb9af0beb062fd49644de817cfcbccc25b187e8`.
- Updated `skills/gameplay/aiverse-ecs-impact-damage/SKILL.md`: document `saveDamageQueue/loadDamageQueue/hashDamageQueue`, v1 bounds-checked byte format, transactional load, generation-safe hash and the GameModule serializer/hash integration requirement. World COW snapshots are not enough for persistent saves.
- Verification: documentation commit, **native C++ execution not confirmed**. Next: execute CTest and regression suite; keep old FPS/RTS save paths untouched until migration parity evidence.


### 2026-11-18 — FPS programmed projectile stage parity checks deep and COW snapshots
- **Implementation SHA:** `d4fd11dc5984f4ceebbf4c427a4517c5ba113504`.
- Updated `tests/fps/projectiles.inc`: extracted `ecsProjectileProgramStageParityCase(bool cow)`; the `ecsProjectileProgramStageParity` entrypoint now tests both default deep snapshots and explicit ECS COW snapshots. Both validate tick-by-tick default-versus-`ecsAuthority` programmed projectile state/health, v9 cross-load and replay restore.
- **Verification:** source committed only; native compile, FPS CTest, COW and platform parity **not run**. COW enablement uses supported `World::enableCowSnapshots` API.
- **Next:** native compile/CTest and transport/Editor regression; resolve any differences before replacing `fps.compat.projectilePrograms` workspace with direct `ProjectileRuntime` ECS iteration.


### 2026-11-18 — Engine-wide ECS migration plan checkpoint
- Documentation SHA: `c05d2c4856216d6d372cf98e77290a0503b20a75`.
- Updated `docs/ecs-all-features-migration-plan.md` with current real FPS common HP primitive adoption, versioned `DamageQueue` byte/hash API, test coverage and explicit non-completeness of actual per-feature ECS health authority.
- Verification: source documentation only, no native build result.


### 2026-11-18 — Real FPS two-shooter rewind, save and rollback authority regression
- Implementation SHA: `c2ea67c2d089426e1fdcaa89012b4d9a4f40307b`.
- Extended `tests/fps/tests.cpp` with `ecsSimultaneousHitscanRollbackParity` and called it in the standard `fps` CTest suite. Configures two opposing players aiming at each other, an explicitly nonphysical hitscan weapon (3 pellets, 8 damage, zero bullet speed, one-tick interval), and both source inputs each tick with deliberately rewound `viewTick`. Runs 12 ticks under legacy and opt-in `fps.ecsAuthority`, compares checksums and entire Actor vectors each tick, verifies every authoritative `ActorRuntime` equals its compatibility view, and requires actual shooting/hits. Takes snapshots after tick 2, compares cross-mode v9 save reload in both directions, replays ticks 3..11 and asserts final checksums are identical.
- This fixture is intended to catch incorrect deferred same-tick hits, snapshot ownership mistakes, and lag-compensated damage regressions when changing live FPS. **No native compile or execution has occurred**, so expected target hit outcomes and all asserted equalities remain unverified.
- Next: run `ctest -R '^fps$'` on native debug+Release, confirm fixture, then compare real transport replay. Do not change the save v9 network wire or synchronously dispatched damage while this is unverified.


### 2026-11-18 — Agent instructions for live FPS health and pending ECS damage
- Implementation SHA: `1c6324e1728b358ecc31d04974f912ce84aeb03f`.
- Updated `AGENTS.md` to require common `applyIntegerHitPointDamage` after mitigation, versioned World `DamageQueue` persistence from GameModule, FPS ECS authority ON/OFF and save v9 parity, and prohibition against introducing an independent writable FPS `HitPointsComponent` while `ActorRuntime` remains canonical.
- Verification: docs-only edit. Next: native CMake/CTest and real FPS/RTS integration tests before any default cutover.


### 2026-11-18 — Whole-engine migration plan updated for real FPS projectile-stage extraction
- **Documentation SHA:** `c90338a46e977babd4f0063a83ade01e2b8be888`.
- Updated `docs/ecs-all-features-migration-plan.md` to record actual FPS stage execution order (`ecsHydrate -> fps.compat.projectilePrograms -> fps -> ecsMirror`), where legacy tick-workspace state remains, the precise scope of the existing deep/COW v9 parity fixture and the remaining ownership cutover from State arrays to shared ECS projectile components.
- **Verification:** documentation commit only. Native tests and FPS/RTS network/save parity remain outstanding.
- **Next:** after CI and FPS fixture pass, move the programmed projectile solver toward direct ECS-owned execution without losing immediate hit effects; do not enable new queued damage barrier until side-effect parity.


### 2026-11-18 — Live FPS vehicle raycast uses shared deterministic hitscan candidate selection
- Implementation SHA: `e4677f50d0217b011f0406919d71a8a25b03e10a`.
- Changed `modules/fps/FpsSystems.cpp::traceShot`: the vehicle AABB intersection loop now uses `lib::genre::selectClosestHitscanCandidate`, the same deterministic fixed-point nearest-hit policy used for actor current/rewound poses and by ECS `resolveHitscanIntent`. The host supplies the original eligibility (`v.health>0 && v.driver!=source`) and identical bounds (`pos-Vec3{1,0,2}` .. `pos+Vec3{1,2,2}`). Vehicle candidates still run AFTER actors, with strict `fraction < best`, so an equal-distance vehicle does not override an actor hit; a closer vehicle still overrides and takes the original vehicle damage/driver eject path.
- No changes to vehicle authority, stored ids, gear, static material penetration, event ordering, save v9 or network protocol. This is another live FPS shooting path consuming the generic hitscan policy, not a separate ECS vehicle-hit resolution barrier.
- Verification: code committed to GitHub only. Native C++ build/CTest and gameplay vehicle-firing parity not yet executed. Earlier attempt to query GitHub via shell failed to resolve github.com; connector source reads/writes remain available.


### 2026-11-18 — Shared FPS/ECS health and damage queue integration static audit
- Source re-read against current `feature/universal-entity-inspector-fps`. **14/14 static wiring assertions passed**: FPS uses `applyIntegerHitPointDamage`, generic ECS DamageResolution uses the same helper, versioned queue save/load/full hash and bounds are present, arithmetic/invalid-save/Entity-generation test cases exist, CMake CTest and standalone suite reference the fixture, skill/AGENTS/whole-engine migration plan document the current ownership.
- At inspection, PR #7 was open and GitHub Actions CI, ECS experiments, Studio and GPU batching were all **queued**, with no combined completed status checks. Not a passing build. Full repo checkout in the runtime is blocked by GitHub DNS resolution.
- **Do not merge or default-enable new ECS authority based on static results.** Next native-agent commands: `python3 tools/libraries/catalog.py check`; `bash tests/run_ecs_experiment_suite.sh`; CMake build `aiverse_ecs_integer_health_commit_test` and `aiverse_fps_tests`; `ctest --output-on-failure -R 'ecs_integer_health_commit_test|fps|fps_transport'`; full RTS replay, cross-genre golden checksums, sanitizers, save/network rollback.
- Current work is a *real shared FPS HP arithmetic replacement plus generic ECS queue persistence*, not an independent live FPS HitPointsComponent owner. Same-tick damage still executes synchronously to preserve current kill/loot/shield/knockdown semantics.


### 2026-11-18 — Shared swept projectile-target collision policy (physical path)
- Implementation SHA: `97ad4670832714ba2c324c88cf2f5f6224653a09`.
- New public `libraries/include/aiverselib/genre/EcsSweepQuerySystems.h` defines `SweptProjectileContact<Candidate>` (stable pointer/fraction/normal) and `selectSweptProjectileContact`. Performs deterministic fixed-point swept segment against AABB expanded by projectile extents with injectable bounds and stable tie preference. Unlike the strict legacy actor hitscan selector, endpoint fraction=1 contacts are accepted. No physics integration, RNG or damage; this is shared collision selection to compose with physical/ballistic projectile solver and impact policies. No genre check.
- Verification: connector GitHub source commit only; no C++ compilation, no gameplay usage yet. Next migrate the actual FPS programmed projectile sweep loop preserving its stable target-ID ordering and wall collision precedence, then add tests.


### 2026-11-18 — Live FPS programmed projectile collision uses shared sweep policy
- Implementation SHA: `6301d49ee6f8e438adc9a7f6b4a41bb92d3aeba1`.
- Changed `modules/fps/FpsProjectiles.cpp::tickProjectilePrograms`: the live programmed-projectile dynamic target sweep now delegates to `lib::genre::selectSweptProjectileContact`. Uses the identical candidate list (Actor/Vehicle mixed, prefiltered and sorted by semantic target ID), world AABB expanded by `Vec3{s.radius,s.radius,s.radius}`, the same `p.pos` and displacement, nearest `enter` fraction, and tie preference for smaller target ID. The selected candidate is copied back into the original `std::optional<Candidate>` and the existing subsequent terrain collision, material penetration, programmed contact, damage, effects and branch spawning remain in their prior order. Endpoint fraction=1 remains a hit.
- This is real FPS gameplay using a shared collision primitive, **not** a completed migration to the generic `addProjectileFlightSystem` or new ownership for State::projectiles. RTS kinematic no-physics remains separate; scripted RTS physics/flight programs are not changed.
- Verification: source commit only. Native C++ compile, moving-target/terrain/penetration tests, v9 save and rollback parity NOT run. Next expose shared sweep through the generic projectile package and add independent regression coverage including endpoint/tie behavior.


### 2026-11-18 — Shared sweep selection exported by physical projectile ECS package
- Implementation SHA: `7c030beb5143e085da6e786a1a98dd5ffad17ebb`.
- Updated `EcsProjectileSystems.h` to expose public `EcsSweepQuerySystems.h` transitively. Any native project already using `AIVERSE::lib_ecs_projectile_systems` can now compose deterministic dynamic-target swept contact selection with its physical flight and existing impact/damage stages without importing FPS or RTS. The header is not part of the nonphysical `EcsKinematicProjectileSystems` dependency closure.
- Verification: GitHub source commit only; native builds/tests pending. Next cover stable selection, extent expansion and endpoint contact in cross-genre projectile tests, update corresponding skill.


### 2026-11-18 — Swept physical-projectile shared collision regression
- Implementation SHA: `0ff656f20f961dc0d4cd1d76faa206fcc1a15de5`.
- Extended existing `tests/ecs_projectile_composition_test.cpp` (registered for ShooterWeaponSource and StrategyTurretSource hosts) to call `exerciseSharedSweptContacts` before its flight/snapshot checks. The new exception-based assertions cover deterministic nearest hit with out-of-order target IDs, exact tie preference for smaller semantic ID, endpoint contact at fraction=1, expansion of target AABB by projectile radius/extent and a no-hit case. It uses the public collision selector exported from `EcsProjectileSystems.h`, with no reference to FPS or RTS gameplay module implementations.
- Verification: GitHub source commit only. CTest/standalone suite execution not performed; expected 0.3, 1.0 and 0.2 fixed-point collision fractions must be confirmed against native `world::intersect` implementation. Next: update the public projectile AI skill and package manifest, check native builds and real FPS projectile-program parity.


### 2026-11-18 — Physical projectile sweep reuse documented for agents
- Implementation/documentation SHA: `ad99e919bcb367f50fbfd761abf7f82a9311c047`.
- Updated `skills/gameplay/aiverse-ecs-projectile-systems/SKILL.md` to document `selectSweptProjectileContact` from public `EcsSweepQuerySystems.h`, exact endpoint/tie/extent requirements, real FPS programmed-projectile adoption, and separation from independent no-physics kinematic and instantaneous hitscan systems. Points agents to `ecs_projectile_composition_test` regression and reminds them not to claim native passes without execution.
- No new gameplay code or wire format in this commit; test/build state unchanged.


### 2026-11-18 — FPS links physical ECS projectile package alongside hitscan/impact
- Implementation SHA: `aa3df049728df724f25f8c83f0698d2bdc31833b`.
- Updated `modules/fps/CMakeLists.txt` to explicitly link `AIVERSE::lib_ecs_projectile_systems` alongside `AIVERSE::lib_ecs_hitscan` and `AIVERSE::lib_ecs_ordered_impact`. This package exports the shared `EcsSweepQuerySystems.h` used by actual `FpsProjectiles.cpp`. Physical collision/ballistic dependency is now explicit and independent of the no-physics RTS kinematic package.
- Verification: GitHub source commit only; native CMake configure, C++ compile and CTest still not run. No save/network or per-tick gameplay code changes in the CMake commit.


### 2026-11-18 — Agent contract records real FPS shared hitscan and physical sweep migration
- Implementation/docs SHA: `62eb55759951fb9d20980c1122c48616bfa3dcf4`.
- Updated `AGENTS.md`: for new gameplay and agents, live FPS `FpsProjectiles.cpp` uses shared swept collision (`EcsSweepQuerySystems.h`) from physical ECS projectile package with endpoint/tie invariants; live FPS `traceShot` uses shared `resolveHitscanIntent` (same as separately schedulable ECS resolver) and generic vehicle ray selection but retains synchronous damage. Explicitly references existing v9/deep-COW programmed projectile tests and new two-shooter FPS rewind/save/rollback parity fixture; requires actual native execution before default-on or queued-impact changes. Motion policy is per weapon, never RTS/FPS.
- This is documentation-only; no compiled tests or gameplay runtime changed in this commit.


### 2026-11-18 — FPS per-projectile ECS executor declaration
- Code SHA: `b853ab8fd414375e76b99c40e5ac3b3a32df1c9a`.
- Declared private `Module::stepProgrammedProjectile(Simulation&,Projectile&,std::vector<Projectile>&,std::size_t liveCount)` in `modules/fps/FpsModule.h` as the shared per-projectile solver boundary for both legacy vector iteration and direct ECS `ProjectileRuntime` iteration. It will retain the legacy collision, movement, spawn, immediate damage, status and event order.
- Verification: source-only declaration; **native compilation not run**. The definition and callers are still pending in the next implementation step; do not build this intermediate commit.
- Next: extract old solver once, then implement ECS Runtime direct iteration and compatibility read projection, preserving `State::projectiles` only for later legacy mechanics.


### 2026-11-18 — Cross-genre ECS physical/hitscan migration static wiring audit
- Re-fetched current feature branch and evaluated **16/16 source-level assertions**: unique catalog package IDs, all required kinematic/physical/hitscan/impact package entries, explicit FPS native library links, live FPS use of shared sync hitscan/vehicle ray/ordered impact/HP/swept collision, RTS kinematic/ordinary/programmed impact reuse, real two-shooter FPS authority parity test call, shared sweep fixture, scheduled hitscan + damage fixture, CTest/standalone registration.
- This is **static inspection only**; it does NOT prove C++ compile, full correctness or runtime rollback. Direct `git ls-remote https://github.com/chromealex/AIVERSE.git HEAD` in the local runtime failed to resolve `github.com`, so the repository could not be cloned for native CMake/CTest. The GitHub connector worked for source reads/writes. GitHub Actions observed for recent implementations had CI runs **cancelled** while ECS experimental, Studio and GPU batching were **queued**; none can be reported as a pass.
- Remaining gates: real native Debug/Release FPS+RTS builds; `ctest --output-on-failure -R 'ecs_projectile_composition_test|ecs_hitscan_resolution_test|ecs_hitscan_selection_test|fps|fps_transport'`; actual FPS programmed projectile wall/penetration/branch parity, RTS moving-target ability/projectile impacts, replay and network rollback. Only then phase out compatibility `State::actors/projectiles` or change instant-damage timing.


### 2026-11-18 — Real FPS programmed projectiles now run directly on ECS ProjectileRuntime (experimental)
- **Code SHA:** `24d9ce958a56d6db179aa9b88471923772313b92`.
- Refactored `modules/fps/FpsProjectiles.cpp` into a single `Module::stepProgrammedProjectile(Simulation&,Projectile&,children,liveCount)` solver, reused by the default legacy vector loop and `fps.ecsAuthority` path. The **ECS-authoritative path** now enumerates live `ProjectileRuntime` entities in explicit saved `order`/ID order, advances the *component's* `Projectile value` in place, applies original immediate hit/damage/split/penetration callbacks, queues expired ECS Entity handles, and performs structural destroys only after traversal. It then creates child `ProjectileRuntime` entities in stable branch-spawn order and updates `DynamicEntityIndex` keys. Original vector order/capacity check is preserved through a per-iteration live-count tracked across logical expiration. External native projects can directly alter `ProjectileRuntime` before the stage.
- For compatibility, after the ECS traversal, rebuilds `State::projectiles` as a **projection** of ECS values. Other FPS grenade/round/loot systems are still legacy and may mutate this vector during the following main `fps` tick; existing `fps.ecsMirror` reconciles these mutations. Therefore **full exclusive ECS projectile authority for the entire frame is not yet complete**; this commit eliminates the legacy vector as the *programmable projectile flight stage's* mutable source.
- No game rule/config change, new component type or v9 save schema. Legacy mode keeps the original immediate source-vector loop, but invokes the same solver to avoid two divergent algorithms.
- **Verification:** GitHub source commit only; native compiler, `fps` tests, deep/COW rollback, dynamic Entity allocator ordering, network and save v9 parity **NOT executed**. Added method declaration in preceding SHA `b853ab8fd414375e76b99c40e5ac3b3a32df1c9a`. Check consistency of `DynamicEntityIndex` after projectile expired/recreated, especially snapshot restore and branch split budget.
- **Next:** add direct-ECS authority tests showing an in-place `ProjectileRuntime` change survives into the next stage, add deterministic mixed programmed/non-programmed order/branch tests and compile `aiverse_fps_tests` before any default flip.

### 2026-11-18 — Native ECS persistence registry on shared PR #7
- Added per-Simulation registered native ECS component/resource schemas with deterministic field-wise save/hash, strict ID/version/entity validation and v10 chunks. Existing no-native ECS projects retain save v9 bytes and hashes.
- Registered pools are permitted by RTS fixed-pool whitelist; unregistered pools still fail. World snapshot/COW includes registered state. New standalone and CTest fixture covers order, save/load, COW, legacy and schema failure.
- Native compilation, CTest, platform replay and Mac editor smoke tests are NOT YET RUN in this session. The older whole-Simulation load is not fully transactional: the custom schema chunk itself is staged.


### 2026-11-18 — FPS direct ProjectileRuntime authority/ID/COW regression fixture
- **Code SHA:** `61473e6af4cce8c34eae06516bbd3f89ac999db4`.
- Extended `tests/fps/projectiles.inc::ecsProjectileProgramStageParityCase`: every step now checks live `ProjectileRuntime` count equals legacy `State::projectiles`, each `DynamicEntityIndex` key maps to an alive runtime with matching ID/order/position/damage, and the two independent FPS simulations still match checksums. After golden deep/COW replay, restores pre-flight snapshots, writes new damage **directly to the ECS ProjectileRuntime**, deliberately poisons `State::projectiles` with a different damage value and verifies the next FPS tick takes canonical ECS data, produces the same gameplay as a legacy host with the new damage and preserves the 88-HP first victim outcome. COW mode uses the same mutation test.
- **Verification:** GitHub source commit only; native compiler/FPS/rollback test **not run**. Important follow-up: exercise source-ordered child spawns and mixed nonprogrammed projectiles, inspect World allocator reuse after removal, check save v9/checksum plus editor projections. Do not declare complete ECS projectile ownership until other main FPS systems stop writing State vector.


### 2026-11-18 — FPS ECS projectile branch-spawn and Entity order regression
- **Code SHA:** `d75012ec1f31b431ed242e90bcdaa8d5a53a5bb7`.
- Added `ecsProjectileBranchAuthorityParityCase(bool cow)` and runner to `tests/fps/projectiles.inc`: a real 4-player FPS shot uses the existing split/damage/flyThrough modifier chain, compares default State and `ProjectileRuntime` ECS-authoritative simulation on every tick, checks `DynamicEntityIndex`, stable `ProjectileRuntime.order` and ID, branch allocation (`nextObject`), final health, v9 save cross-loading and replay from a pre-flight deep/COW snapshot with identical ECS generations/order.
- **Verification:** test source committed only; not yet invoked by `aiverse_fps_tests`, **native compilation and assertions unverified**. The expected 90 HP for each target comes from the pre-existing legacy FPS projectile-program fixture; validate execution.
- **Next:** add test entrypoint to FPS suite and verify C++ compilation, COW + save/network replay.


### 2026-11-18 — FPS ECS branch split parity fixture enabled in test main
- **Code SHA:** `4374480daacdc1beb69bd531287dd4f131822bc5`.
- Updated `tests/fps/tests.cpp` to invoke `ecsProjectileBranchAuthorityParity()` after the existing `ecsProjectileProgramStageParity()` fixture. Native FPS tests now cover real programmed branch-spawn ordering, `ProjectileRuntime` source ownership, Entity identity/index, v9 cross-loading and deep/COW rollback by source.
- **Verification:** GitHub update only, **native compilation/CTest/assertions not yet executed**. Continue with static code review, then native build, FPS/RTS save and transport tests before merge.


### 2026-11-18 — Extract FPS ordinary projectile stage from round mechanics
- Implementation SHA: `030c814405beda3293967f1127547bcacd26d3c2`.
- `modules/fps/FpsSystems.cpp` split the previous `tickSystems` projectile iteration into `Module::tickOrdinaryProjectiles`, preserving the same per-projectile tick loop, traceShot collision/impact timing, grenade bounce/fuse/flash/smoke rules, order, and immediate effect callbacks. `tickSystems` now owns round/bomb logic only.
- **Transitional code state:** declaration and Simulation registration of `tickOrdinaryProjectiles` are still required in the following code commits; do not build the intermediate SHA alone. Preserve original order `fps.compat.projectilePrograms -> fps.compat.ordinaryProjectiles -> fps` (round mechanics, royale, actor tick). No native build run yet.

### 2026-11-18 — Ordinary FPS projectile system declared
- Implementation SHA: `de1c9842115cadfe716096773be642eed95fc8f1`.
- Declared `Module::tickOrdinaryProjectiles(Simulation&)` in `modules/fps/FpsModule.h`. The method is implemented by the previous `FpsSystems.cpp` commit, and still needs its own correctly ordered Simulation system registration. No native build executed.

### 2026-11-18 — Ordinary FPS bullets/grenades now registered as separate Simulation stage
- Implementation SHA: `9812d420052e5157d7bc03cdc41b71621143087a`.
- `modules/fps/FpsModule.cpp::setup` registers `fps.compat.ordinaryProjectiles` after the programmed projectile stage and before the main `fps` stage. When mechanics are enabled and no winner has been latched, it executes `tickOrdinaryProjectiles` from the extracted `FpsSystems.cpp` method. This preserves the original gameplay sequence: program flight/impacts -> ordinary bullet/grenade flight/impacts -> bomb/round rules -> royale -> actor update.
- This first extraction uses legacy `State::projectiles` as its workspace; it is a genuinely separate ECS Simulation stage, but still needs direct `ProjectileRuntime` ownership in `fps.ecsAuthority` to complete the next migration wave. No new fields or save/transport schema.
- Verification: GitHub source commits only; native build, FPS/RTS game tests, v9 rollback and editor checks not run.


### 2026-11-18 — Mixed physical program + nonprogrammed direct ECS projectile source test
- **Code SHA:** `54cd9fa586ac190f762603c391a562e62f47ae85`.
- Extended `ecsProjectileBranchAuthorityParityCase` in `tests/fps/projectiles.inc`: before the first FPS tick it inserts a passive, nonprogrammed projectile into the legacy `State` and **only into the ECS `ProjectileRuntime` pool** for the authoritative host, leaving its legacy `State::projectiles` projection intentionally stale. This checks `fps.ecsHydrate` index discovery, direct-ECS stage handling of mixed nonprogrammed/programmed bullets, original ordering of branch children, Entity IDs and projections across COW/deep snapshots and save v9, while ensuring the passive object remains after active branches expire.
- **Verification:** GitHub source committed; native compilation/CTest/assertions **not run**. Validate the runtime `DynamicEntityIndex` reconciliation before claiming fully authoritative ECS projectile ownership. Compatibility paths still mutate vector later in main tick.


### 2026-11-18 — Explicit std::size_t include for FPS direct projectile executor
- **Code SHA:** `5cc5039a3102ca3037b9321e969df4be13798e23`.
- Included `<cstddef>` explicitly in `modules/fps/FpsModule.h` for the new private `stepProgrammedProjectile(...,std::size_t)` declaration. Avoids relying on transitive C++ header contents.
- **Verification:** source commit only, native build/CTest not run.


### 2026-11-18 — Ordinary FPS projectiles now advance directly in ECS authority components
- Implementation SHA: `8e54368f80cf14319968f0e806521297e9fa388a`.
- `modules/fps/FpsSystems.cpp` extracts the existing bullet/grenade/smoke/flash impact behavior into shared `Module::stepOrdinaryProjectile(Simulation&,Projectile&)` (same logic, original immediate events and damage order). Default `fps.ecsAuthority=OFF` calls this policy over the original State vector and erases expired objects in place. With authority ON, `tickOrdinaryProjectiles` enumerates `ProjectileRuntime` ECS components sorted by stable `order,id`, mutates each real ECS payload directly, skips programmed projectiles (handled by the preceding system), stages expired entities, destroys/erases their dynamic keys only after iteration, and rebuilds State::projectiles as the ordered compatibility projection.
- This is the next **actual live FPS ECS ownership transfer**: both programmed and non-programmed shot flight systems now read/write ECS `ProjectileRuntime` with authority ON. The following `fps` tick still runs legacy round and actor subsystems that may modify State vectors; the `fps.ecsMirror` barrier reconciles them. Save v9 and command/network payloads unchanged.
- **Intermediate compilation gate:** add declaration `stepOrdinaryProjectile` to FpsModule.h in the next implementation commit. Native C++ compile, CTest, real v9 deep/COW and UDP/WebSocket rollback still unverified. Check grenade bounce, target death, smoke expiration and Source ID ordering.


### 2026-11-18 — FPS branch allocation fixture strengthened
- **Code SHA:** `6738e47bbb48648638aeb2d15ac3b51f44b69f81`.
- `tests/fps/projectiles.inc` now records `State::nextObject` at the pre-flight snapshot and requires subsequent branch child IDs to advance beyond that exact value. Replaces the weak fixed threshold `maxAllocated>3` which could be satisfied by unrelated preexisting objects.
- **Verification:** GitHub edit committed only; no native test execution. The existing direct ECS mixed and save/rollback fixture remains active in the main FPS test suite.

### 2026-11-18 — Shared FPS ordinary projectile step declaration completes header wiring
- Implementation SHA: `813fa4631d6480101fd57e7085fbcd744882e38b`.
- Added private `Module::stepOrdinaryProjectile(Simulation&,Projectile&)` declaration to `modules/fps/FpsModule.h`, matching the implementation introduced in SHA `8e54368`. Both legacy vector and direct ECS runtime paths call the same solver. The stage remains registered between programmed projectile and main round/actor systems.
- Verification: GitHub code update only; no native build or tests.


### 2026-11-18 — Whole-engine ECS plan acknowledges live direct FPS ProjectileRuntime flight ownership
- **Documentation SHA:** `7655c80b0530b5af136e4ff36f182f5ceff03ec0`.
- Updated `docs/ecs-all-features-migration-plan.md` with the new source-of-truth cutover: programmed flight now executes directly in the `ProjectileRuntime` ECS pool under `ecsAuthority`, preserving ordered ECS identities, split child creation and immediate impact; default legacy mode uses the same solver. Clearly marks remaining `State::projectiles` compatibility projection/writeback for grenades, round logic, spawn code, and v9 save/network.
- **Verification:** documentation commit only; native build and actual FPS gameplay/rollback tests unverified.
- **Next:** update AGENTS and projectile skill for the new ownership rule; update Epic, then inspect test harness/static code and CI.


### 2026-11-18 — Agent contract for direct ECS programmed-projectile authority
- **Code/docs SHA:** `0819547185c1f4fb4ed042237c480b03d83a98fa`.
- Updated `AGENTS.md` to require the real `fps.compat.projectilePrograms` stage to treat `ProjectileRuntime` as the authoritative owner in ECS mode, use the same `stepProgrammedProjectile` solver as legacy mode, preserve source `order` and immediate effects, and keep `State::projectiles` writable only for other yet-unmigrated FPS stages. Cites new deep/COW mixed projectile and v9 test fixtures.
- **Verification:** docs-only commit; native C++ compilation still unverified.


### 2026-11-18 — Native FPS parity fixture for direct ECS ordinary bullets, smoke and grenade
- Implementation SHA: `b93cda9c81569d4d48f9147c178bf2b132b27f72`.
- Added `ecsOrdinaryProjectileAuthorityParityCase(cow)` to `tests/fps/projectiles.inc`: real FPS bullet, persistent smoke and smoke-conversion grenade run in parallel legacy and canonical `ProjectileRuntime` modes. Tick-by-tick checksum and ECS projection/order/kind/position/velocity/expiry, body hit, deep/COW saveState rollback, v9 cross-mode save reload, direct native write to ECS smoke expiry against stale State projection.
- Still needs `ecsOrdinaryProjectileAuthorityParity()` invocation in `tests/fps/tests.cpp`; assertions and native C++ compilation NOT executed.


### 2026-11-18 — Projectile ECS skill documents live direct FPS runtime update
- **Implementation SHA:** `b401f7274d609538fc8b0f32eb26d4bcd93fde74`.
- Updated `skills/gameplay/aiverse-ecs-projectile-systems/SKILL.md` with the FPS `ProjectileRuntime` source-of-truth path, one physical solver shared with legacy mode, World Entity ordering/recycling, synchronous impact timing, transient State compatibility projection, and tests for COW/deep rollback, child branching and v9 cross-save.
- **Verification:** docs-only commit, native builds and gameplay tests pending. Next: track partial cutover in Epic #19, perform static audit and inspect CI.


### 2026-11-18 — FPS ordinary direct-ECS projectile parity test registered
- Implementation SHA: `5463a02005aef0873776980d9c8476bf2635ee3c`.
- The main `tests/fps/tests.cpp` suite now invokes `ecsOrdinaryProjectileAuthorityParity()` immediately after programmed projectile stage parity. The function is implemented in `tests/fps/projectiles.inc` and covers both deep and COW snapshots and cross-mode v9 save replay. Existing CMake target `aiverse_fps_tests` and CTest `fps` already compile the include through tests.cpp, so no new target is needed.
- **Verification:** code committed, not compiled or run. Native CTest `fps` must pass before releasing ECS-authoritative ordinary projectiles.


### 2026-11-18 — Direct FPS ProjectileRuntime integration source audit (20/20)
- Re-fetched the current branch's `FpsModule.h`, `FpsProjectiles.cpp`, `FpsModule.cpp`, FPS projectile and runner fixtures, AGENTS, public ECS projectile skill, whole-engine migration plan and this handoff (9 source/docs files).
- **20/20 static invariants matched source:** shared helper declaration and single collision/modifier implementation; default and ECS routes call the same physical solver; ECS updates the `ProjectileRuntime` in place; deterministic `order` sorting; expiration after traversal; stable child runtime spawn and retirement of old DynamicEntityIndex keys; projection only after the ECS stage; correct hydrate/stage ordering; registered tests for deep/COW, authoritative direct mutation, ECS-only nonprogrammed insertion and v9 branch replay; agent skill/plan/handoff references are present.
- **Verification scope strictly static:** these are source checks, **NOT** evidence of compilability or runtime parity. CMake, FPS tests, network transport, save round trip and Editor regressions remain unexecuted in this environment. Do not switch default ECS authority or delete compatibility writers.
- Next: run native `aiverse_fps_tests`, `fps_transport`, `ctest -R 'fps|ecs_'` and Editor regression on a checkout with full game assets. Triage any C++ build/runtime failures before migrating remaining grenade/spawn/round gameplay.


### 2026-11-18 — Reusable deterministic physical bouncing projectile policy
- Implementation SHA: `d2504f112e8455160e715f80cd9fa926f2f9db5b`.
- `libraries/include/aiverselib/genre/EcsProjectileSystems.h` now defines `BounceMotionConfig` (gravity, bounce velocity divisor, contact-fraction inset, collision box half-extent) and `stepBouncingProjectile`. This is a pure physics-motion policy over explicit position, velocity and a read-only `SpatialWorld` collision provider. It does not own a timer, apply damage, spawn/despawn or change projectile Entity ownership. Its default values intentionally match existing FPS grenade bounce: gravity 10, half-extent 0.1, contact inset 0.01, reflection divided by 2 exactly (fixed-point division, not multiplication by 0.5).
- Native result **unverified**; next implementation commit replaces only the real FPS ordinary grenade bounce branch with this shared policy, keeping effects/fuses and nonphysical RTS projectiles independent.

### 2026-11-18 — Native ECS migration integrity and failed-load rollback
- **Implementation SHA:** `acba58bf9f3c4d7cea69c039540f203aa99abbc3`.
- Extended `PersistentEcsRegistry` with optional explicit exact-version old-value readers for both custom components and resources; versioned chunks are validated and staged before commit.
- Original save checksum is verified from original schema IDs/versions and field bytes even after an upgrade, rather than silently skipping integrity verification on migration. This requires modules not to hash registered native fields independently.
- `Simulation::deserialize` now backs up Simulation-owned World/tick/RNG/players/commands/events and restores them on invalid payload/checksum or exception; GameModule private caches still require restore hooks.
- Expanded `ecs_native_persistence_test` with v1->v2 upgrade, re-save, unsupported version, corrupt-load unchanged state and simulated two-peer checksum/rollback parity. Standalone/CTest target already wired.
- Validation: GitHub source edits prepared, **no native compiler, CTest, real RTS integration, sanitizer or network socket run in this session**. Do not merge until validation.


### 2026-11-18 — Live FPS ordinary grenade motion uses common physical ECS policy
- Implementation SHA: `70b98ff0c80aa27d403e2e9422c4235215bf00e1`.
- `modules/fps/FpsSystems.cpp::stepOrdinaryProjectile` now calls public `lib::genre::stepBouncingProjectile` (default `BounceMotionConfig`) for non-programmed kind 1/2/3 grenades before fuse expiry. The shared policy matches the previous deterministic computation sequence: velocity.y -= 10*dt, cast swept box of half extent 0.1, position advance by max(0, hit.fraction-0.01), reflect normal and divide velocity by 2; otherwise full delta advance. Fuse conversions, smoke/flash/blast target effects, round systems and event order are unchanged. Works on direct `ProjectileRuntime` with ecsAuthority ON and legacy State baseline OFF via the same per-projectile caller.
- This is **live physical gameplay** sharing a general engine policy, unlike the no-physics RTS projectile path. No new serialized fields or network messages. Native compilation, exact replay hash and collision/penetration tests still NOT run.


### 2026-11-18 — Explicit std::move header for direct ECS projectile spawn
- **Code SHA:** `434ea5bf99d6705f940bd618bc9a2a5fc62871be`.
- Included `<utility>` explicitly in `modules/fps/FpsProjectiles.cpp` for direct child `std::move` / container operations. No gameplay semantics change.
- **Verification:** GitHub source commit only; native compilation and CTest pending.


### 2026-11-18 — Fixed-point grenade bounce equivalence regression fixture
- Implementation SHA: `e06ec4f168b72c8e6d1599b3ddf57368300a8ded`.
- Extended `tests/ecs_projectile_composition_test.cpp` with `exerciseSharedBounceMotion()` run by the existing projectile CTest and standalone ECS suite. Checks shared `stepBouncingProjectile` against the exact former FPS formula independently implemented in the test on a floor collider, non-colliding upward flight, and a near-ground contact. Compares full fixed-point position and velocity; preserves velocity-before-cast, swept half-extent 0.1, contact fraction inset 0.01, and reflection divided by 2 rather than multiplication by 0.5.
- Fixture source committed; **no native C++ build/test execution**. It is a parity expectation, not observed proof. Follow with native `ecs_projectile_composition_test`, `fps`, `fps_transport` tests.


### 2026-11-18 — Public AI projectile skill documents direct ECS ordinary FPS and physical bounce
- Documentation SHA: `6ec99f2955fb5a728cd37bf06b2ddf97001ffc85`.
- `skills/gameplay/aiverse-ecs-projectile-systems/SKILL.md` now teaches reuse of `BounceMotionConfig` and `stepBouncingProjectile`, independent physical vs RTS nonphysical kinematic policies, and the new real `fps.compat.ordinaryProjectiles` stage with direct `ProjectileRuntime` authority when `fps.ecsAuthority` is ON. Explicitly lists its FPS deep/COW/v9 and cross-genre formula-equivalence test gates, with native results not yet established.

### 2026-11-18 — Mandatory per-stage handoff checkpoint and native ECS ownership
- **Branch:** `feature/universal-entity-inspector-fps` (PR #7), sharing the working tree with other agents. This is a docs/process stage; prior code commits are `b416dd4c4ab9a72d43d7d9d805c44e49c4161d62` and `acba58bf9f3c4d7cea69c039540f203aa99abbc3`.
- **Files:** `AGENTS.md`, `docs/agent-handoffs/native-ecs-persistence.md`, `docs/native-ecs-persistence.md`, `docs/editor-unification-handoff.md`.
- **Change:** handoff requirement now applies after **every** stage, not just selected ECS implementation commits. The handoff must explicitly capture code SHA, paths, compatibility, actually executed tests, blocking failures, and next agent action; preserve concurrent entries and commit docs before next stage.
- **Native ECS source of truth:** `engine/sim/PersistentEcs.h`; public registration `engine/sim/Simulation.h`; save/hash/rollback `engine/sim/Simulation.cpp`; RTS fixed-pool boundary `modules/rts/RtsSave.cpp`; tests `tests/ecs_native_persistence_test.cpp`.
- **Verification:** repository source and workflow metadata inspected using GitHub connector. Container `git ls-remote` failed with `Could not resolve host: github.com`. **Native compile/CTest/macOS runtime and real network playback: NOT RUN.** Latest examined GitHub Actions for PR #7 were queued, not passing.
- **Unfinished:** real compiled/native RTT validation; check serializer migration of legacy modules, malformed payloads, exact checksum semantics, and full transaction boundary. Preserve old v9 format for projects without registered native ECS components.
- **Next action:** run `ctest --test-dir <build> -R ecs_native_persistence_test --output-on-failure` and `bash tests/run_ecs_experiment_suite.sh` on a full checkout; fix real diagnostics before switching production gameplay. Add exact result and commit to both handoff locations.


### 2026-11-18 — Agent coding contract updated for FPS ordinary projectile ECS authority
- Documentation SHA: `45ab2d8ab1deb3b115e66e027354c7142057e5bf`.
- Updated `AGENTS.md` with exact new stage order, both programmed and ordinary projectile direct `ProjectileRuntime` ownership under ECS Authority, temporary State projection/mirror limits, reuse of the physical `stepBouncingProjectile` and separate RTS kinematics. Requires new real-FPS direct-ECS ordinary projectile tests, existing programmed deep/COW tests, cross-genre physics parity, native FPS and transport tests before production.
- Docs-only; no native compiler or runtime result.


### 2026-11-18 — FPS direct ECS projectile spawn adapter declaration
- **Implementation SHA:** `a2ff11554b972e1dcf44d9c7dcc2225eccd4ab0a`.
- Added private `Module::spawnProjectile(Simulation&,Projectile)` declaration in `modules/fps/FpsModule.h`. The next implementation step will connect *actual built-in FPS weapon and grenade spawns* to this common adapter; with `fps.ecsAuthority` enabled it will create a canonical `ProjectileRuntime` immediately and keep legacy `State::projectiles` as a same-tick compatibility view.
- **Verification:** header source commit only; declaration intentionally requires follow-up definition before native build. No CMake/CTest execution yet. Preserve source ID generation, 256-capacity rules, legacy event order and v9 save format.


### 2026-11-18 — Real FPS weapon projectile spawns now create authoritative ECS components immediately
- **Implementation SHA:** `a2fb9481492f978c103f769e99ab4bb87c8cc4ea`.
- Implemented `Module::spawnProjectile(Simulation&,Projectile)` in `modules/fps/FpsModule.cpp` and replaced the actual weapon `shoot()` projectile-vector insertion with a call to it. In default mode the same legacy `State::projectiles` push preserves behavior. Under `fps.ecsAuthority`, the helper validates stable projectile ID, capacity (256) and `DynamicEntityIndex`, allocates an ECS Entity, attaches full authoritative `ProjectileRuntime` with the original sequential `order`, registers the kind/ID index, and updates `ProjectileEntity` editor projection **on spawn**, before pushing a same-tick legacy compatibility view. This removes the next-tick mirror as the original *creator* of weapon-projectile authority and makes the new ECS entity visible to later native game logic immediately.
- Legacy ordering unchanged: projectiles are created within the accepted shot loop, after existing ammo/fire accounting and weapon flight program initialization; no new RNG, network messages or save v9 fields. The projectile stage still executes earlier that tick; newly fired bullets are advanced next tick just as before. Remaining ECS mirror stages still reconcile writes by other FPS consumers.
- **Verification:** source push only; no C++ compiler, FPS tests, COW/deep snapshot, rollback or live parity executed. Next: route actual grenade action spawns through this helper, assert same-tick ECS runtime identity/order in FPS tests, and verify native compile + v9/transport.


### 2026-11-18 — Static integration audit of FPS ordinary ECS projectile migration
- Re-fetched active branch after implementation and checked **15/15 source-level conditions**: ordered stage registration, matching declarations, shared per-projectile solver, default legacy vs ECS authority branches, direct ECS runtime mutation, deferred expiration and dynamic-index cleanup, canonical projection reconstruction, removal of duplicate round-system projectile loop, public physics bounce policy, real FPS bounce adoption, unchanged direct ECS programmed stage, registered real FPS deep/COW/v9 fixture, shared bounce equivalence fixture, native library link and AGENTS guidance.
- This is *static verification*, not native compilation or gameplay test success. GitHub Actions for bounce integration SHA `70b98ff0c80aa27d403e2e9422c4235215bf00e1` showed Studio #1105, ECS experimental #588 and GPU batching #710 **queued**, while CI #1438 was **cancelled**. No passing result is available from that commit.
- Next required native commands: `python3 tools/libraries/catalog.py check`, `bash tests/run_ecs_experiment_suite.sh`, CMake build of `aiverse_fps_tests` and `aiverse_ecs_projectile_composition_test`, then `ctest --output-on-failure -R '^(fps|fps_transport|ecs_projectile_composition_test)$'`; also FPS ECS-authority ON/OFF golden checksums for grenades, smoke, bursts and network rollback. Do not merge or enable by default without actual results.


### 2026-11-18 — Real grenade action spawns now create authoritative ECS ProjectileRuntime
- **Implementation SHA:** `c121d7ef5e53bdf577fee5dcb1ebd328ea18e2b3`.
- Changed `modules/fps/FpsSystems.cpp::action` for actual `Frag`, `Smoke` and `Flash` commands to call the same `Module::spawnProjectile` as accepted weapon shots. Legacy inventory consumption, projectile ID allocation, fuse, initial velocity, damage, count limit and tick order are unchanged; under `fps.ecsAuthority`, grenades now receive a canonical ECS Entity and `ProjectileRuntime` as soon as the input action succeeds, not only during `fps.ecsMirror`. Default-off mode still appends the legacy vector.
- **Verification:** GitHub source commit only. No C++ compilation, grenade behavior/round parity, v9 save/load, network transport, snapshot rollback or Editor checks run. Next: native FPS regression for both weapon/throwable paths, plus round-reset/source-of-truth migration.


### 2026-11-18 — Live FPS weapon+throwable immediate ECS spawn parity fixture
- **Implementation SHA:** `91a80fbab26ddbe5f7a28e4a45b7f12e4866944d`.
- Added `tests/fps/projectile_spawn.inc` with `ecsProjectileSpawnAuthorityParity` run in both deep and COW snapshot modes. Uses production input commands to fire an ordinary bullet and throw Frag/Smoke/Flash actions, and compares legacy vs `ecsAuthority` state/checksums/actor fields and every `ProjectileRuntime` kind/ID/order/velocity/position/damage via `DynamicEntityIndex` after each tick. Verifies all three throwables existed, object ID counter advanced, v9 cross-mode save load, pre-fuse rollback, 10-tick continuation and replay checksum.
- **Verification:** GitHub test source added, **not yet referenced from `tests/fps/tests.cpp` main**, native C++ compilation and execution not performed. Requires normal FPS test data and CTest. The helper's immediate creation is verified in source; after-tick fixture alone cannot distinguish whether an entity was created before `fps.ecsMirror`.
- **Next:** include and invoke new fixture in `aiverse_fps_tests`, add a mid-tick spawn visibility invariant if needed, check native CI and test status.


### 2026-11-18 — FPS weapon/grenade ECS spawn parity fixture wired into built-in suite
- **Implementation SHA:** `538e370629d0647766cb5cde57802ae122da524e`.
- Updated `tests/fps/tests.cpp` to include `projectile_spawn.inc` and execute `ecsProjectileSpawnAuthorityParity()` alongside other actual FPS projectile source-ownership tests. This runs actual weapon shooting plus Frag/Smoke/Flash input actions in both legacy and ECS-authority hosts, with deep/COW, v9 cross-load and rollback coverage when the native FPS test binary executes.
- **Verification:** source commit only; native compilation, assertions, FPS transport/server integration **not run**. Next: inspect CI and address any build/test failures before changing production defaults.


### 2026-11-18 — C++20 libc++ build blocker: removed shared_ptr::unique()
- Implementation SHA: `31f1bef0b3b28d7436d06d9edc652fba48908451`.
- User's local macOS Clang/libc++ build failed at `engine/core/ECS.h` on `std::shared_ptr<T>::unique()` (13 usages total, including paged pool COW, resource detach, World copy paths, structural update and shared pool/resource counters). This C++20 standard-library method is no longer available in the toolchain.
- Replaced all 13 occurrences preserving the original boolean predicate: `shared.unique()` => `(shared.use_count() == 1)`, `!shared.unique()` => `(shared.use_count() != 1)`. This retains the previous behavior for empty handles and same single-owner vs shared-owner decisions. No gameplay component schema, save, network wire or scheduler changes.
- Verified by source transformation count: exactly 13 expressions replaced and zero `.unique()` occurrences remain in `engine/core/ECS.h`. **No native compilation or test run in this environment**; user's Mac build is the next verification step.
- Rebuild from branch: `git pull --ff-only origin feature/universal-entity-inspector-fps` then `cmake --build build/ecs-test --parallel 4 --target aiverse_fps_tests aiverse_ecs_projectile_composition_test aiverse_ecs_hitscan_resolution_test`. Run `ctest --test-dir build/ecs-test --output-on-failure -R '^(fps|ecs_projectile_composition_test|ecs_hitscan_resolution_test)$'` when compile succeeds.


### 2026-11-18 — Direct ECS projectile round-reset cleanup declaration
- **Implementation SHA:** `957fdd8d4e329df855b0aca59cbca474c12a7dba`.
- Added `Module::clearProjectiles(Simulation&)` declaration to `modules/fps/FpsModule.h`, intended to remove authoritative `ProjectileRuntime` ECS entities synchronously at bomb-round reset rather than leaving stale entities until `fps.ecsMirror`. The default legacy path must retain its existing vector reset behavior.
- **Verification:** declaration committed; definition + actual call are pending. **Native compilation not yet performed**. Next implementation must keep stable Entity destroy/source ID ordering and v9 compatibility.


### 2026-11-18 — ECS authoritative projectile round-reset cleanup implemented
- **Implementation SHA:** `b874140ca50336544cb47a66892812a5708cb17b`.
- Implemented `Module::clearProjectiles(Simulation&)` in `modules/fps/FpsModule.cpp`. Default FPS still clears legacy `State::projectiles`. Under `fps.ecsAuthority`, collects all live `ProjectileRuntime` entities, sorts by source projectile ID for deterministic retirement order, destroys them directly in World, removes indexed projectile kind/ID mappings while retaining loot/vehicle indices, then clears the same-tick State compatibility projection. Unlike the former mirror-only behavior, no stale projectile Runtime remains between the reset and `fps.ecsMirror`.
- **Verification:** code committed but **call site is not yet rewired**; C++ native build and gameplay tests not run. Destroying Entities earlier may change allocator reuse versus historical compatibility behavior, so verify save/replay and stable event order under both storage modes.
- **Next:** replace `tickSystems` round-reset vector clear with this helper, then add native round-reset regression across deep/COW and v9 saves before default-enabling ECS authority.


### 2026-11-18 — Live bomb-round reset now clears canonical projectile ECS components
- **Implementation SHA:** `6fb59b267c9ab882ca9b03a8a4f23474b8dc21b2`.
- Changed the actual `modules/fps/FpsSystems.cpp::tickSystems` bomb-round reset branch to invoke `clearProjectiles(sim)` rather than writing only `State::projectiles.clear()`. Under `fps.ecsAuthority`, all existing projectile Runtime entities and source-ID keys are retired at the round-reset barrier. Legacy mode retains its vector clear. Loot/vehicle reset logic and actor/bomb/round event ordering remain at their previous source location, no new save field.
- **Verification:** source committed; **native compilation, bomb round resets, same-tick player input, v9 cross-mode save, deep/COW rollback and network parity not yet run**. Full authority remains default-OFF, and external World allocator lifetime must be checked.
- **Next:** add FPS regression focused on immediate reset of Runtime/index, ordering, and cross-storage snapshot parity.


### 2026-11-18 — Built-in FPS bomb round reset ECS destruction/save/COW fixture
- **Implementation SHA:** `291ac6b9253190599c7eed173fd68ac6be2f10fe`.
- Extended `tests/fps/projectile_spawn.inc` with `ecsProjectileRoundResetParityCase(bool cow)`: runs real bomb-mode FPS to create a grenade by input action, explicitly ends the round at an identical tick in default/ECS hosts, verifies the actual `tickSystems` reset removes every `ProjectileRuntime` plus kind-1 ID index while preserving normal state/checksum parity, cross-loads save v9 in both storage modes, restores a pre-reset snapshot and repeats reset, then spawns a new grenade to verify increasing game object IDs after ECS Entity removal/reuse. Runs in deep and COW snapshot modes.
- **Verification:** source committed but **test entrypoint is not yet called by FPS main**; native compiler, gameplay assertions and transport rollback not run. Call from the built-in FPS suite and inspect actual failure modes, especially mandatory bomb sites and freeze/round event ordering.


### 2026-11-18 — FPS ECS projectile reset regression registered in native suite
- **Implementation SHA:** `f5f61276826a3a5f5fa32388413e3a47b5013d8d`.
- Updated `tests/fps/tests.cpp` main to execute `ecsProjectileRoundResetParity()` alongside weapon/grenade spawn parity. The new regression is called in the standard `aiverse_fps_tests` executable, not a standalone unused source fixture.
- **Verification:** source edit committed; native compile, CTest execution and actual bomb-mode checks remain unverified. Review both default and COW runs before making ECS authority default.

### 2026-11-18 — Real RTS NativeModule persistence integration fixture
- **Code SHA:** `a10a9a87e468465eeacaed113f61b58cea46f007` on PR #7 `feature/universal-entity-inspector-fps`.
- **Files:** `tests/ecs_native_rts_integration_test.cpp`, `CMakeLists.txt`.
- New CTest uses the actual RTS and NativeModules paths with `Counter` and `Ledger` persistent ECS state. It tests per-tick peer hashes, v10 save, COW rollback, replay, unregistered pool rejection, legacy v9, corrupt-save rejection.
- No gameplay defaults, v9-compatible games, or network packets were changed. Native compile and CTest were NOT RUN because the local container cannot resolve `github.com`. Workflow state: CI: queued (https://github.com/chromealex/AIVERSE/actions/runs/44263451051); AIVERSE Studio validation: queued (https://github.com/chromealex/AIVERSE/actions/runs/44263451024); ECS 2.0 experimental checks: queued (https://github.com/chromealex/AIVERSE/actions/runs/44263451070); GPU batching focused regression: queued (https://github.com/chromealex/AIVERSE/actions/runs/44263451007).
- **Next:** build/test the two `ecs_native_*` targets on a full checkout, report actual failures and fix before enabling production use. Full handoff: `docs/agent-handoffs/native-ecs-persistence.md`.



### 2026-11-18 — Whole-engine migration plan updated for live FPS projectile creation and reset
- **Documentation SHA:** `dd726cdd2cfb5d78aefc27da348202733b59c1e4`.
- Added explicit source-of-truth tracking to `docs/ecs-all-features-migration-plan.md` for live FPS weapon shots, Frag/Smoke/Flash actions and bomb-round projectile reset using direct ECS runtime entities at their original stage. Defines remaining transitional State projection writers, World Entity ID/allocator ordering gates, native test fixtures and v9/network acceptance requirements. Keeps legacy mode and default-OFF `fps.ecsAuthority`.
- **Verification:** documentation commit only; no native compilation/CTest.


### 2026-11-18 — Fix actual macOS sweep regression expectation
- Implementation SHA: `dfef673bf2384927a5c2e046d57fe077a9b164d3`.
- User's `ecs_projectile_composition_test` aborted with "shared projectile sweep lost nearest-hit tie semantics". `world::intersect` derives the fractional hit distance using `Fixed::operator/`, which **truncates** the 16-bit fractional quotient, while the test expected `Fixed::fromRatio(3,10)`, which **rounds**. At z=3 with displacement z=10 the raw outputs are 19660 versus 19661. Updated sweep fraction comparisons to `3_fx/10_fx` and `2_fx/10_fx` so they match the actual engine calculation; left tie-break and endpoint assertions strict.
- No production collision math or gameplay mechanics changed. Native CTest rerun **pending**; this change is not a proven fix until user runs it.


### 2026-11-18 — Agents contract updated for direct ECS weapon/grenade projectile producers
- **Implementation SHA:** `7d741c1407822cf49b84a84639f4042fe7cce963`.
- Updated `AGENTS.md` to reflect that weapon projectiles, throwable action projectiles and bomb-round cleanup are no longer legacy-only producers under `fps.ecsAuthority`. They now call a single `spawnProjectile` / `clearProjectiles` adapter to create/destroy canonical `ProjectileRuntime` ECS entities at their actual gameplay stage. The State vector remains for other legacy readers/writers until the complete migration, with exact game source ID, save v9, COW, network and event ordering gates.
- **Verification:** documentation committed, native build and FPS test execution not performed.


### 2026-11-18 — Fix actual macOS ECS hitscan resolution contact assertions
- Implementation SHA: `9e5263a6e8b028c85c731e3da80c8033ab2d248f`.
- User's `ecs_hitscan_resolution_test` aborted with "wrong closest hit fraction". `world::intersect` reports z=3 / dz=10 via truncating `Fixed::operator/`; the test expected nearest-rounded `Fixed::fromRatio(3,10)`. Also the emitted hit coordinate uses `origin + delta * fraction`, which is **not exactly** `3_fx` after quantization. The regression now checks `hit.fraction == 3_fx/10_fx` and `hit.point.z == 10_fx*(3_fx/10_fx)`, i.e. the actual deterministic engine arithmetic.
- No production hitscan, damage ordering, or transport format changed. Native test rerun remains **pending**.


### 2026-11-18 — Native ECS projectile skill updated for FPS creation/reset cutover
- **Implementation SHA:** `44feafc3837c00abc84b8008c1b68e08abb5c3a1`.
- Updated `skills/gameplay/aiverse-ecs-projectile-systems/SKILL.md` with real FPS `Module::spawnProjectile`/`clearProjectiles` adoption, direct `ProjectileRuntime` Entity/ID/order ownership and original same-tick location, unchanged default mode, bomb reset allocator ordering precautions, and new `ecsProjectileSpawnAuthorityParity` and `ecsProjectileRoundResetParity` deep/COW/v9 test gates.
- **Verification:** docs committed; no observed native compiler/test pass.


### 2026-11-18 — Prevent related unrun hitscan-selection CTest failure
- Implementation SHA: `940064fa6120bd3a0b8fec6e92cf75ce4ac38606`.
- `tests/ecs_hitscan_selection_test.cpp` also used the nearest-rounded `Fixed::fromRatio(3,10)` as the expected fraction. Corrected it to `3_fx/10_fx` to match the shared engine's truncated slab-intersection division. This is the same failure mode as the reported `ecs_hitscan_resolution_test`, and would affect the standalone/CTest sweep after the originally failed subset is rerun.
- Test expectation only; **not** a native test pass.

### 2026-11-18 — Reject malformed ECS allocator and RTS pool records
- **Code SHA:** `d8937196965a79c60d0832590f6a04b5afb9ec89`; current branch `feature/universal-entity-inspector-fps`.
- **Files:** `engine/core/ECS.h`, `engine/sim/Simulation.cpp`, `modules/rts/RtsSave.cpp`, `tests/ecs_native_persistence_test.cpp`.
- Added allocator invariants before byte-state application, plus generation/liveness/duplicate validation for RTS built-in pool entries. Source tests cover five malformed allocator states.
- No on-disk version or checksum update; no native compilation, sanitizers or live network test run here. See `docs/agent-handoffs/native-ecs-persistence.md` for exact proof limits and next command.
- **Next:** build targeted CTest, inspect legacy save compatibility and run real RTS malformed-pool regression.



### 2026-11-18 — Fix confirmed FPS COW snapshot detach during checksum
- Implementation SHA: `383b9c9099602d4fc2dfa1a4df2a1d6f354f3f85`.
- User's `fps` CTest failed at `tests/fps/tests.cpp:195` after a COW snapshot: `incremental->world().sharedResourceCount()==sharedResources` was false following `incremental->checksum()`. Root cause is `modules/fps/FpsModule.cpp::Module::hashState` calling mutable `state(sim)`, which calls `World::resource<State>()` and detaches a shared COW singleton during a read-only checksum. `authoritativeStateCopy` had the same read-side effect.
- Both methods now load the State through `World::resourceConst<State>()` with a missing-state guard, so ordinary and ECS-authority checksums do not change resource ownership. `hashState` uses an explicit const-cast **only** to supply Archive's legacy non-const bidirectional IO API on its output-only writer path; `stateIo` and nested IO write fields only when `!archive.writing()`. No deep copy is required for the default non-authority checksum; ECS-authority mode already materializes a State copy from canonical components.
- No gameplay step, save bytes, checksum algorithm or network format changed; fixes a snapshot-count side effect. Native FPS CTest rerun **not yet observed**.


### 2026-11-18 — Explicit ECS-owned projectile mirror API declaration
- **Implementation SHA:** `cc397b176f5a51a4dbe2cb0fdf49bf83c652d6e9`.
- Updated `Module::syncDynamicEntities(Simulation&,bool projectProjectilesFromEcs=false)` public declaration in `modules/fps/FpsModule.h`, preserving single-argument call sites for legacy setup and v9 deserialization while allowing the end-of-tick mirror to derive `State::projectiles` from canonical ECS Runtime rather than overwriting ECS components with legacy vector data.
- **Verification:** header commit only, implementation and mirror call migration pending. Native compilation not run at this intermediate commit.

### 2026-11-18 — Run native ECS persistence regressions in ECS gameplay CI
- **Code SHA:** `2f5163f0655b348ccaa0822f4c92949669b64405`; `tests/run_ecs_gameplay_compare.sh` now includes both native persistence CTest binaries in its `--full` OFF/ON builds and test selection. The ECS experimental workflow already invokes this mode.
- CI and actual native test execution are unverified; source wiring checked. See `docs/agent-handoffs/native-ecs-persistence.md` for follow-up commands.
- Next agent: obtain completed workflow logs and fix any failure, then record an exact handoff.



### 2026-11-18 — FPS end-of-tick projectile mirror converted to one-way ECS → State projection
- **Implementation SHA:** `bbf42ceaa8974216dda3a0a0404d33c2377b23ad`.
- Changed `modules/fps/FpsModule.cpp::syncDynamicEntities(sim,bool)` and the **live** `fps.ecsMirror` call. Under `fps.ecsAuthority`, the mirror now obtains **canonical live `ProjectileRuntime` components** (stable `order`/ID sort), validates unique nonzero source IDs and matching `DynamicEntityIndex` Entity links, refreshes the read-only `ProjectileEntity` Editor view, and derives `State::projectiles` as a compatibility presentation. It **never writes `ProjectileRuntime` from `State::projectiles` at this barrier**. Thus stale legacy vector mutations cannot silently overwrite the canonical ProjectileRuntime after the native flight or actor stages.
- Setup and existing legacy v9 `deserialize` still call the default `syncDynamicEntities(sim)` mode: State → ECS reconstruction remains mandatory for old saves. Default `ecsAuthority=false` still uses the original source-vector sync. Loot and vehicle mirror paths are unchanged and still legacy workspace writers; this change is limited to projectiles.
- This is the key **single-writer end-of-tick ownership** cutover for FPS projectile state. Actual shooting, grenade actions, programmed flight, ordinary flight and bomb-round reset have already been routed through direct `ProjectileRuntime` ECS paths when the gate is ON. Existing synchronous damage/hit/round stages and v9 format are retained.
- **Verification:** source committed only; **no successful native compiler, CTest, FPS simultaneous hits, serialized entity allocator replay, network rollback or Editor regression yet**. The one-way mirror may expose remaining undocumented native/legacy projectile writes: investigate failures rather than reverting ownership. Must add fixture deliberately poisoning the State vector then invoking `syncDynamicEntities(sim,true)` to prove a canonical ECS value survives.


### 2026-11-18 — Runtime-authoritative projectile mirror stale-State regression
- **Implementation SHA:** `7566853de76ca2096b52ee1614b653a186a90c12`.
- Extended the real FPS `ecsProjectileSpawnAuthorityParityCase(bool cow)` fixture in `tests/fps/projectile_spawn.inc`. Immediately after a weapon fires, changes `ProjectileRuntime.value.damage` to 17 and deliberately corrupts the ECS host's legacy `State::projectiles[0].damage` to 777; the control legacy FPS host receives 17. Calls public `Module::syncDynamicEntities(*ecs,true)`, requires the State view to be rebuilt as 17 and checks full state/checksum parity. The fixture then continues through real Frag/Smoke/Flash action spawns, v9 saves and deep/COW rollback. This exercises the actual new **one-way** mirror contract independently of the next-tick hydration process.
- **Verification:** source pushed but native C++ compilation/CTest/runtime checks are still **unverified**. Fix any source-of-truth violation rather than weakening this assertion.


### 2026-11-18 — Original native dynamic sync method retained for ABI
- **Implementation SHA:** `8e849661f4fc29b6b998e6bff135af2a7b5e93a0`.
- Updated `modules/fps/FpsModule.h` to declare both the **original** `syncDynamicEntities(Simulation&) const` signature and a new two-argument `syncDynamicEntities(Simulation&,bool) const` mirror-projection overload. This preserves source/binary symbol expectations for existing native project callers while keeping the ECS→State projection mode explicit.
- **Verification:** header committed; old-signature wrapper implementation still needed, native build not executed. Next commit defines the wrapper and logs it separately.


### 2026-11-18 — Backward-compatible FPS dynamic sync overload implemented
- **Implementation SHA:** `3d1d28401dd963be062197629e153b951149e07d`.
- Added the original `Module::syncDynamicEntities(Simulation&) const` native method body as an overload wrapper in `modules/fps/FpsModule.cpp`, forwarding to the extended `syncDynamicEntities(sim,false)`. Legacy v9 save/load, setup, Editor/native project invocations keep the old source and C++ symbol, while the actual `fps.ecsMirror` uses `syncDynamicEntities(s,true)` under `fps.ecsAuthority` to enforce one-way Runtime→State projectile projection.
- **Verification:** GitHub source committed; binary ABI/native compilation and gameplay fixtures remain unexecuted. Next: static wiring audit, documentation/epic update, native FPS and ECS tests.


### 2026-11-18 — Whole-engine migration plan records one-way projectile ECS mirror
- **Documentation SHA:** `96e09a7f304c56b6bbdbca2b6f83dec4e29618f4`.
- Updated `docs/ecs-all-features-migration-plan.md` with the concrete single-writer projectile cutover: live FPS mirror reads `ProjectileRuntime` and derives State/editor projection; default mode plus original public one-argument API preserve State→ECS legacy save v9 import; all known built-in projectile mutation sources use ECS on the authority gate. Lists still-legacy Actor/loot/vehicle ownership and comprehensive native regression gates.
- **Verification:** documentation committed. Static wiring audit 17/17 passed earlier, **not native gameplay pass**.


### 2026-11-18 — AGENTS mandate one-way FPS ProjectileRuntime writer and legacy v9 import
- **Implementation SHA:** `4de40df4c9194624595d6526e2ed27cacd6122be`.
- Updated `AGENTS.md` with a mandatory single-writer rule: all known live projectile lifecycle operations write canonical `ProjectileRuntime` with `fps.ecsAuthority=true`; end-of-tick `syncDynamicEntities(sim,true)` derives the State editor/save view only; only setup/legacy v9 deserialization retains the original `syncDynamicEntities(sim)` State→Runtime import. The stale-State 777 vs Runtime 17 mirror fixture is documented.
- **Verification:** source documentation committed, native compiler/CTest/Editor/network still not run.


### 2026-11-18 — Public projectile skill documents live FPS one-way ECS mirror
- **Implementation SHA:** `82b39ed022ba4ffad4790e0a57061b006dcb78f5`.
- Updated `skills/gameplay/aiverse-ecs-projectile-systems/SKILL.md` with the new live end-of-tick Runtime→State projection, unique source-ID validation, matching editor views, and the backward-compatible single-argument setup/v9 import overload. Requires agents not to write the legacy State vector as projectile authority, and cites stale-State override regression and native test gates.
- **Verification:** documentation only; native gameplay checks still unexecuted.


### 2026-11-18 — FPS one-way ProjectileRuntime lifecycle source audit (25/25)
- Re-fetched 10 current-branch files: FPS public header, main module, systems, programmed projectile solver, new spawn/reset fixture, FPS main test, AGENTS, ECS projectile skill, whole-engine migration plan and this handoff.
- **25/25 targeted static assertions passed**: old public `syncDynamicEntities(sim)` and new overload are both present; live mirror chooses the ECS→State mode; setup/v9 load still use old import; ECS mirror enumerates Runtime and has **no** Runtime-from-State assignment; IDs unique and indexed; weapon and grenade spawns, bomb reset, programmed and ordinary stages all use canonical Runtime when enabled; deep/COW + poisoned State + v9 fixture is registered; agent docs and plan match the cutover.
- At inspection, PR #7 head was `da1cbcf6885a64a8775882cb28095c7cd86ee910`; all four GitHub Actions (`CI`, `ECS 2.0 experimental checks`, `AIVERSE Studio validation`, `GPU batching focused regression`) were **queued**, so there is **no observed native compiler/test pass**.
- **Next native gate:** compile FPS and editor binaries, run `ctest --output-on-failure -R 'fps|ecs_'`, network `fps_transport`, sanitizer/replay tests and verify old v9 save byte compatibility and deterministic Entity allocator restoration. Do not enable ECS authority by default until results and any failures are recorded.

### 2026-11-18 — User-confirmed ECS/FPS selected tests on Mac Studio
- Owner's `build/ecs-test` CTest output reported **4/4 PASS (17.04s total)**: projectile composition (0.04s), hitscan selection (0.63s), hitscan resolution (0.04s) and FPS (16.32s).
- Only these four tests have verified runtime output. Exact local commit SHA not supplied; current PR branch CI was queued. The dedicated native persistence and real RTS NativeModule tests are still **NOT RUN / not present in this test output**.
- **Next:** run `git rev-parse HEAD`, build `aiverse_ecs_native_persistence_test` and `aiverse_ecs_native_rts_integration_test`, then `ctest --test-dir build/ecs-test --no-tests=error --output-on-failure -R '^ecs_native_(persistence|rts_integration)_test$'`. Record the result in `docs/agent-handoffs/native-ecs-persistence.md`.

### 2026-11-18 — Native ECS CTest NOT RUN due to missing binaries
- Owner's Mac Studio reported `make Error 2` while building `aiverse_ecs_native_persistence_test`. Both `ecs_native_persistence_test` (#56) and `ecs_native_rts_integration_test` (#55) were **Not Run**, because no executable files were present. No C++ compiler error text was supplied, so root cause is **not confirmed**; do not mark these tests as FAIL assertions.
- Remote PR #7 was inspected for relevant C++ files and workflows; no source changes made at this checkpoint. The first compiler diagnostic is needed before a safe correction. See `docs/agent-handoffs/native-ecs-persistence.md` for the exact single-thread verbose build command.
- Next: `cmake --build build/ecs-test --target aiverse_ecs_native_persistence_test -j1 --verbose 2>&1 | tee /tmp/aiverse-native-ecs-build.log`, then capture the first `error:` with preceding context. Fix and retest both executable targets.

### 2026-11-18 — Fix mixed-type auto declarations in native ECS test

- **Code SHA:** `bb9000396147f92aed0897c803a5610b4851df5d` on `feature/universal-entity-inspector-fps` (PR #7).
- **File:** `tests/ecs_native_persistence_test.cpp`. User's Mac Studio compiler reported two errors for mixed-type variables under one `auto` declaration (legacy/legacyBytes, before/originalHash). Split declarations and explicitly typed serialized data and checksum. No engine runtime changes.
- **Verified:** compiler output from user confirms the two pre-fix errors; source correction committed. **Post-fix native compile/CTest NOT RUN.** Both prior native ECS tests were `Not Run` due to missing binaries, not failed assertions.
- **Next:** build both native ECS test targets, then run the two CTest cases; write exact results to `docs/agent-handoffs/native-ecs-persistence.md`.

### 2026-11-18 — Real RTS/FPS ECS replay integration CTest (source stage)

- Shared branch `feature/universal-entity-inspector-fps`; code commits
  `d5a719fddc3f71d1b61f0518bf1b62cdf7b9c9d9` and
  `8c4fdf733a57d8b6413df4587abf6a367796ea31`.
- Registered `ecs_real_genre_replay_test` that runs real `RtsModule` and
  `fps::Module`: canonical commands with reversed peer arrival, v9 disk
  save/load at tick 20, COW/deep snapshots, corrupted-load recovery, replay
  comparison per tick, FPS default and `ecsAuthority` parity.
- No engine or public API changes; neither the default FPS authority mode
  nor save formats changed. The test complements but does NOT replace
  shared-library composition tests or the native RTS v10 persistence test.
- Native compile and CTest **NOT RUN** (container cannot resolve GitHub DNS;
  no local checkout). The user's older 4/4 CTest success predates this test.
- Full handoff and commands:
  `docs/agent-handoffs/ecs-real-genre-replay-2026-11-18.md`.
- Next: build `aiverse_ecs_real_genre_replay_test` on Mac Studio,
  run `ctest -R ecs_real_genre_replay_test`, fix any real save/replay
  regressions in this branch, record exact results in next handoff.

### 2026-11-18 — Single-command AIVERSE test runner

- **Code SHA:** `a2d9a428c5c01e177473962eb534ab654bc75a64` in `feature/universal-entity-inspector-fps` PR #7.
- **Files:** `run-tests.sh`, `tests/extra-test-suites.txt`,
  `docs/testing.md`. `./run-tests.sh` is now the user-facing entry
  point for the entire locally available desktop CTest suite plus ECS
  standalone/sanitizer/parity and other registered extras.
- **Registration contract:** normal tests use CMake `add_test` and get picked
  up automatically; standalone scripts go in the manifest, not root-script
  hard-coded commands. See `docs/agent-handoffs/unified-test-runner.md`.
- **Verification:** source/manifest structural checks only; the container
  could not resolve raw.githubusercontent.com for Bash syntax/native execution.
  **Full test execution NOT RUN**. The previous native ECS compile failure
  must still be independently re-tested after pulling new commits.
- **Next:** user runs `./run-tests.sh --quick` then `./run-tests.sh`
  on the Mac Studio, provides the first error from the timestamped report;
  fix and append handoff for each stage.

### 2026-11-18 — Test runner self-test coverage

- **Code SHA:** `c5c7001554a70a2626d7aebd8ac2ad48a593a49e`, branch `feature/universal-entity-inspector-fps`.
- **Files:** `tests/test_unified_runner.py`,
  `tests/extra-test-suites.txt`. Added six offline smoke checks covering
  syntax, CTest discovery, manifest execution, errors and build-stop
  semantics; these tests run automatically in full `./run-tests.sh`.
- **Verified:** source and manifest edited/published; native unittest
  execution **NOT RUN** in current environment due unavailable GitHub
  checkout. `docs/agent-handoffs/unified-test-runner.md` gives test commands.
- **Next:** `python3 tests/test_unified_runner.py`, then
  `./run-tests.sh --quick`, followed by full `./run-tests.sh`.

### 2026-11-18 — Unified runner build blocked by nonexistent Json::isBool
- **Code SHA:** `6d9f46c5ff64a3178be571f22d6b2980125ff358` on PR #7, file `engine/render/Model.cpp:185`.
- Owner's `./run-tests.sh`: CMake **PASS**, build **FAIL** exit 2 before running CTest; compiler: `Json` has no `isBool` method. Switched to `Json::type() != Json::Type::Bool`, preserving strict boolean validation for model batching sidecar `autoLod`.
- **Post-fix build/CTest NOT RUN** in connector-only environment. Follow up with full build. Review `RelWithDebInfo` potentially disabling `assert` in CTest binaries (many unused assert-only vars in owner build).
- Detailed handoff in `docs/agent-handoffs/unified-test-runner.md`. Next: pull, rerun one-command test script, fix further errors using first compiler diagnostic.


### 2026-11-18 — Unified CTest Debug assertions policy
- **Code SHA:** `9b44e6d540101df79ab42bb1aa6738c3967070dd` on PR #7, `run-tests.sh`, `tests/test_unified_runner.py`, `docs/testing.md`.
- Root unified runner now builds CTest in **Debug** (assertions enabled), and `--no-build` rejects optimized build caches. Added two test-runner smoke cases. Motivation: owner's RelWithDebInfo build disabled `assert` checks, visible through unused-variable warnings.
- **Post-fix tests NOT RUN**; next user commands `python3 tests/test_unified_runner.py`, `./run-tests.sh --quick`. Detailed handoff in `docs/agent-handoffs/unified-test-runner.md`.


### 2026-11-18 — Const-correct Simulation world for NativeModule CTest
- **Code SHA:** `4054a0eede2ec089a0327e36ed0aab7bde1d5292`; `engine/sim/Simulation.h` now exposes `const World& world() const` in addition to `World& world()`.
- **Owner's compile log:** `tests/ecs_native_rts_integration_test.cpp:60` could not call nonconst `Simulation::world()` from `marker(const Simulation&)` (unified build reached 58%); therefore no CTest run occurred. No cast or fixture weakening.
- **Post-fix native compilation/CTest NOT RUN** in remote-only environment. Next: pull and run `./run-tests.sh --quick`, then full `./run-tests.sh`; log any next diagnostics in `docs/agent-handoffs/native-ecs-persistence.md`.

### 2026-11-18 — EditorWorld const MapDoc accessor compile regression
- **Code SHA:** `508ee4dd346b8f7ff1e256db320b5733b328fe74` on PR #7 `feature/universal-entity-inspector-fps`.
- **User's actual Mac Studio build (Debug, `./run-tests.sh`):** `configure` PASS, `build` FAIL exit 2 at 93%, `apps/editor/EditorWorld.cpp:125:36`: `Editor::selectionPos(const Selection&, float&, float&) const` calls `readDocumentTransform(doc(), ref, transform)` but `Editor::doc()` only had a non-const overload. Compiler: `this argument ... has type const Editor, but function is not marked const`. Test stages **NOT RUN** because the build exited early.
- **Fix:** introduced `const MapDoc& doc() const { return ws_.map; }` beside the existing mutable accessor in `apps/editor/Editor.h`. Preserves full read-only document interface and editor mutation callers, no gameplay, save, network, or schema changes.
- **Other build evidence:** in the same log, `aiverse_ecs_native_persistence_test` and `aiverse_ecs_native_rts_integration_test` both compiled and linked, confirming the previous native ECS build errors were resolved for this reported HEAD. No test assertions executed yet. `aiverse_tests` linked successfully as well.
- **Verification:** GitHub source inspected and patch committed. Real compilation after this patch, CTest, full unified runner, sanitizer: **NOT RUN** by this agent; prior user's log predates the patch. Do not mark CI green based on this source fix.
- **Next:** user runs `git pull --ff-only` and `./run-tests.sh --quick` to reach all CTest; on success, run `./run-tests.sh` for extras. Capture exact failure and report directory. If another compile failure occurs, fix it in same branch and append handoff before the next stage.

### 2026-11-18 — Editor warning cleanup: selection loop, revision type, unused hover
- **Code SHA:** `26fa4b4ea6933ff52dfda64a4913cbcc93f0d923` on PR #7. User's `build(2).log` built the editor to 100% with three warnings (dangling else in `ModelWindow.cpp:112`, signed revision comparison in `EditorFps.cpp:31`, unused `hoveredKey` in `EditorFps.cpp:364`).
- Added braces to make the model type-vs-document-entity selection branch explicit (fixes real behavior), matched FPS cache revision type to `int rulesRevision_` and deleted unused hover variable. Tests **NOT RUN after the patch**; clean Debug warning audit pending. Other warning sources identified in earlier full Debug log.
- **Next:** remove `GamepadApple.mm` GNU ternary extension and unnecessary `[this]` in impact queue fixture, then test on Mac Studio and append separate stage handoff.

### 2026-11-18 — GameController and impact queue warning fixes; failed CTest names pending
- **Code SHA:** `effcc8f65d2d30370f07c35ac53a077778d14264`, files `engine/platform/gamepad/GamepadApple.mm`, `tests/ecs_cross_genre_impact_queue_test.cpp`. Replaced GNU-only elided `?:` with explicit standard pointer fallback; removed unused lambda `[this]` test capture. Code-only source verification, native post-fix build/CTest **NOT RUN**.
- User reports some tests failed during ongoing full suite, but no failing test log yet. Do **not** label full CTest green. Next agent needs `build/all-tests/test-reports/<latest>/ctest.log` or `build/all-tests/Testing/Temporary/LastTestsFailed.log`, then fix tests and record actual outcomes.

### 2026-11-18 — Extra standalone suites read CRLF registry erroneously
- **Code SHA:** `447bc13c999faea5d778adec61f3446816b58c51` shared PR #7; `run-tests.sh` now strips trailing CR from registry arguments. Added two CRLF smoke cases in `tests/test_unified_runner.py`.
- The owner's completed full run had **99/106 CTest pass, 7 fail**, plus 5 failing extra suites. Extra failures were all due to a terminal CR passed as a test argument from a CRLF checkout, not proven library failures. Real CTest seven failures are separate and must be fixed on their own.
- Local isolated Bash test of CRLF parsing **PASS**; post-change repo CI and Mac-native CTest **NOT RUN**. Detailed handoff: `docs/agent-handoffs/unified-test-runner.md`.

### 2026-11-18 — Release tools and ECS skill metadata regressions
- **Code SHA:** `2fbedf096f1b01c5a70b7eb37ba27a7b49a1ef34` on shared PR #7. Fixed publisher legacy-archive detection so AIVERSE zip binaries and architecture are found, added 11 missing ECS skills to `skills/catalog.json` from `libraries/catalog.json`, and corrected two skill docs' invalid backticked path/function suffixes.
- Addresses `release_tools`, `skill_catalog`, `skill_audit` from the owner's CTest 99/106 report. **Post-fix runtime tests NOT RUN**; local full checkout unavailable. See `docs/agent-handoffs/unified-test-runner.md`.
- **Next:** run `ctest --test-dir build/all-tests -R '^(release_tools|skill_catalog|skill_audit)$' --output-on-failure` and continue remaining ECS failures.


### 2026-11-18 — ECS failure fixes after full 99/106 CTest run
- **Code SHA:** `8a936fa116c1b3c984b4ada3fba6197ba1ea3562` on PR #7. Four remaining ECS/inspector regressions addressed: cycle rebuild now canonicalizes parent links by Entity index; generic kinematic projectile arrival callbacks stable-sort by Entity index; locomotion test uses Fixed-quantized dt rather than ideal floating fractions; inspector full-suite test accounts for 3 opt-in hierarchy pools excluded from legacy RTS v9.
- **No new save/network schema or ECS authority changes.** Changes committed but **post-fix tests NOT RUN**. Pre-fix Mac Studio CTest was 99/106 PASS and 7 FAIL; release/skills and CRLF extras fixes are in earlier commits.
- **Next:** run targeted CTest `ecs_scene_hierarchy_test|ecs_kinematic_projectile_test|ecs_cross_genre_locomotion_test`, then `aiverse_tests` and full `./run-tests.sh`. See `docs/agent-handoffs/unified-test-runner.md`.

### 2026-11-18 — Correct CRLF script edit (source replacement quoting issue)
- **Code SHA:** `1e98755c81db330781c5e619c5b760746cb3ba97` restored correct `run-tests.sh` after its earlier CRLF patch accidentally duplicated the file suffix because JS `String.replace` interpreted `$'` in the replacement string. This was discovered through source verification and corrected with callback replacement from last good version; no user test ran the malformed version.
- Structural checks of the published candidate passed, actual native `bash -n` and full tests **NOT RUN**. Next verify `bash -n run-tests.sh` and `python3 tests/test_unified_runner.py` before rerunning targeted CTest.

### 2026-11-18 — CI validates root test runner before ECS suites
- **Code SHA:** `3aca181ca15dcce2b0c4412c3242eb4ce21bc4cf`. `.github/workflows/ecs-experiments.yml` now runs `bash -n run-tests.sh` and `python3 tests/test_unified_runner.py` on macOS/Linux, with pull-request triggers for root runner, extras manifest and smoke fixture.
- **Actual CI results not yet known**; the change prevents repeating earlier source-edit corruption. Full handoff in `docs/agent-handoffs/unified-test-runner.md`.

### 2026-11-18 — Requested commit f90242ec4 unavailable for merge
- **Request:** merge abbreviated commit `f90242ec4` into shared `feature/universal-entity-inspector-fps` PR #7.
- **Investigation:** GitHub REST `GET /repos/chromealex/AIVERSE/commits/f90242ec4` returned 422 (`No commit found for SHA`); Git data commit lookup returned 404; compare in both directions returned 404; repository-scoped and global GitHub commit search returned no matches. Also checked `chromealex/gta`, unavailable there. Current remote branch head before this note was `dd711f5e2a7f66ed25bd7330875e6e87b7109e55`.
- **Outcome:** source commit is **not accessible** via the GitHub connection; no merge/cherry-pick, conflicts resolution, or code files changed. Do not claim merged. It may be an unpushed local commit or from a different repository.
- **Next:** publish the source commit to the AIVERSE remote (from a local checkout containing it): `git push origin f90242ec4:refs/heads/incoming-f90242ec4`; alternatively provide the remote repository and full SHA or an accessible branch. Then inspect the commit parents/changed files, do a conflict-aware merge into the latest shared branch and document the merge SHA, actual tests, and remaining issues.
- **Validation:** GitHub queries only, no local compile/CTest; tests **NOT RUN**.

### 2026-11-18 — Integrate published f90242ec4 (already applied, no code changes)
- Source `f90242ec4c02eaad89b02f2c92d61273db6366de` was found on `incoming-f90242ec4`; it changes only this handoff document, documenting namespace-closure fix `393e778f`.
- The exact historical note and the closed namespace are already present in PR #7. Recorded an empty cherry-pick `f5d9d65eeb1fc26a1b247a93230ad213849552ba` against the current target tree instead of merging 171 divergent source-branch commits. **No code or test changes; no native tests run.**
- Detailed resume notes: `docs/agent-handoffs/f90242ec4-integration.md`.

### 2026-11-18 — Mac Studio CTest 106/106 PASS, extras unverified
- Owner's uploaded `configure(3).log`, `build(3).log`, `ctest(2).log`, JUnit XML and `summary(3).txt` confirm local tested SHA `dd711f5e`: configure/build PASS, CTest 106 passed / 0 failed in 869.69s. All previous seven CTest failures are resolved. Current incremental build log has no warning/error diagnostics; not evidence for a clean rebuild.
- Summary ends immediately after `PASS ctest`; standalone `extra-*` results **NOT VERIFIED**. No game or test code changes. See `docs/agent-handoffs/unified-test-runner.md` for exact evidence and next validation.

### 2026-11-18 — PHYSIS Scene overlay and document Inspector extension (external PR #25)

- **Branch:** `feature/physis-nature-simulation` (draft PHYSIS PR #25), after main merge `810feba70f01cdc35003ac80ea8486f39dc16049`. This note coordinates PHYSIS editor changes; **do not mark any editor-unification PR #7 acceptance gate complete** on this basis.
- **Changed files:** `apps/editor/PhysisSceneOverlay.h` (read-only camera projection), `apps/editor/windows/SceneWindow.cpp` (optional debug draw call), `apps/editor/windows/PhysisWindow.cpp` (Scene overlay toggles), `apps/editor/PhysisAuthoring.h` (map authoring validation / Undo), and `apps/editor/windows/InspectorWindow.cpp` (MapProps PHYSIS dialog with JSON+structured configuration).
- **Genre independence:** Scene overlay consumes PHYSIS `State` through common `Simulation::world()`; no new FPS or RTS Inspector switch, no PHYSIS ECS writes from editor UI, no fake scene entities. The PHYSIS map editor supports both traditional `MapData.extra` and FPS verbatim map JSON using `MapDoc::beginEdit` for a single Undo step.
- **API:** `engine/physis/PhysisProjectConfig.h` supplies generic opt-in `installFromMap(Simulation&, Json map)` for native games only; editor itself does not auto-install or mutate PHYSIS runtime state.
- **Actual checks:** source review and GitHub commits only; `aiverse_editor` build, CTest, macOS launch/drag/click/screenshot **NOT RUN**. Studio validation workflow now builds PHYSIS authoring + Scene geometry fixtures, but no PASS log inspected.
- **Next editor-unification owner:** inspect the new Scene/Inspector side effects in the unified editor build, ensure UI collision-free with current Selection/MapProps/Undo/Redo and the InputFlow changes merged from main, and record exact failures/screenshots. PHYSIS retains ownership of its debug UI and generic runtime config parser; do not duplicate editor camera or entity selection stacks.

### 2026-11-18 — PHYSIS authored field Gizmos and shared Inspector properties (PR #25)
- **Ownership:** PHYSIS branch `feature/physis-nature-simulation` extends the common `SceneWindow` with opt-in map-authored PHYSIS W/S/G marker hit-testing/movement and `InspectorWindow::mapProps` with reusable field property UI; the field authorship code lives in `apps/editor/PhysisFieldEditor.h` and `PhysisAuthoring.h`. No new FPS- or RTS-specific editor stack.
- **Input safety:** markers receive left mouse events only when explicitly enabled and only after a projected marker hit; otherwise legacy RTS/FPS selection and camera input paths remain as before. The PHYSIS asset is a map-JSON asset, **not** an ECS entity selected in the native inspector. A mouse drag uses one MapDoc Undo snapshot (first movement), and remaining movement calls `touched` with no extra snapshots. Shift moves vertically. Direct live PHYSIS ECS mutation is forbidden.
- **Data source:** `engine/physis/PhysisFieldAssets.h` is a common genre-neutral map field parser. `installFromMap` creates WindZone/StormEvent/GravityField ECS entities sorted by stable unique JSON IDs after up-front validation; no new genre-native path or NEREID/Terra ownership collision.
- **Validation:** code committed, `physis_field_assets`/`physis_field_authoring` CTests authored and CMake/Studio CI registered. **Build, CTest, macOS interactive Scene pick/drag, Inspector Undo/Redo, screenshot/perf NOT RUN**. Stage 19 handoff: `docs/agent-handoffs/physis-field-assets-gizmos-2026-11-18.md`.
- **Next editor owner:** build the actual `aiverse_editor` after main merge, verify mouse-gizmo hit/drag/UI in both FPS and RTS maps and ensure duplicate PHYSIS MapProps panel IDs do not interfere with undo/redo in Studio. Preserve generic Selection ownership and document limitations.

## 2026-11-20 — Prisma/MotionWeave merge checkpoint

Integration branch `integration/prisma-motionweave-view`; merge `fc46059ee6523cbc1f04f8c412d44e4fdae9398a`.
See [complete merge handoff](handoffs/prisma-stage-82-motionweave-merge-completed.md).
Existing 21 resolutions preserved, final 10 reconciled; one Prisma postprocess
owner and one editor viewport target owner, both animation playback APIs and GPU
fence/retirement lifetimes retained. Build/CTest/GPU: **NOT RUN**, user requested
merge only. Remote documentation and latest MotionWeave merge/push remain next.


## 2026-11-20 — Inspector graph-v2 integration with current main

Implementation `9db6d2014137e41cd67ff82dc187cf1136f73bb3`; branch `feature/aiv20261120_perf_inspector_integration`.
Editor build PASS; final SpectraFX 20/20 PASS including five hardware Metal tests;
actual PRISMA frame checks 5/5 PASS. Source-compatible view adapters and GPU
preview retirement corrections preserve newer main ownership. No experimental
defaults enabled. Full repo/Release/Vulkan/GLES/manual editor UI NOT RUN.
Exact commands, earlier failures, warnings, APIs and next steps:
`docs/agent-handoffs/inspector-spectrafx-main-2026-11-20.md`. Separate documentation commit follows implementation.


## 2026-11-20 — lighting branch Light entities

Code d6ddd951b521f7a10aff5ddace7ebd76032eb8c7 adds stable document light identities, common Transform/Light components and component-targeted Studio Inspector writes. Canonical data remains legacy lights[] through an explicit compatibility adapter; no duplicate state or simulation ECS migration claim. Native RTS/FPS persistence and Release Metal/UI checks passed. See docs/agent-handoffs/lighting-main-stage34-light-entities-2026-11-20.md for commands, compatibility, remaining scope and exact evidence.


## 2026-11-20 — Lighting main integration Stage 36

Merge 9611d05d5aaf299f0892b1fe2f60b935a6c5bede on main preserves VT/LENS/Vulkan contracts and adds tested
GI coexistence. See docs/agent-handoffs/lighting-main-stage36-merge-2026-11-20.md
for exact builds/tests, initial failures and remaining platform gates.


## 2026-11-20 — Inspector revalidated with lighting main

Combined implementation `4acfb482742d2bb1d615c3eaae60308af09ffa1b` preserves incoming main `d84e958b1`.
Editor build PASS; SpectraFX 20/20 PASS (16.78s), five real Metal checks with
no skips; actual PRISMA frame review 5/5 PASS. Two unused GraphWindow helpers
removed; cleanup build has zero warnings. Full repository/platform/manual UI
coverage remains unrun. Exact commands and integration decisions: `docs/agent-handoffs/inspector-spectrafx-lighting-sync-2026-11-20.md`.
