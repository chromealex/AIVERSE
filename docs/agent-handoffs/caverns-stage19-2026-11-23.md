# TerraWeave Caverns — Stage 19 handoff (2026-11-23)

## Provenance
Repository `chromealex/AIVERSE`. Branch `feature/terraweave-caverns-core-20261123`, originally branched from main; **NOT merged into main**.
Stage 19 commits:
- `1364b9a150ff232e25ce809a5b0b4c0f0ac170aa`: `engine/world/TerraWeaveCavernsArchive.h` — versioned Json codec for settings/entrances/stamps.
- `1fd2a013bca36e6dcc05650639d0b1fe8d5ddffe`: `apps/editor/core/TerraWeaveCavernsOps.h` — shared editor/MCP operation.
- `412515c75605a7aa44f8ffda434f1b11983a2b0f`: `apps/editor/core/TerraWeaveOps.h` — register new operation.
- `bb02a8aa08ca11f3f4acd3ed8a8f372ac03a1d7e`: `apps/editor/windows/TerrainWindow.cpp` — native GUI tool.
- `1d465d087a2548a6e0e812412d7bb5eb26fb4eb1`: `tests/terraweave_caverns_editor_test.cpp` — contract coverage.
- `81a0ba892ab2c8ce0722d5688c299f164d38772d`: avoid routine reused stamp IDs after deletion.
- `0d7906247c2bbc80236d40da136a9f6af1838a35`: updated `skills/world/aiverse-terraweave-caverns/SKILL.md`.
- This dated handoff is committed subsequently. Read branch HEAD for latest revision.

## Existing interfaces inspected
- `apps/editor/core/TerraWeaveSparseOps.h`: Workspace op + `map.beginEdit()`, `map.touched()`, `map.data.extra` storage, `Change::Map` and `withinStroke`.
- `apps/editor/windows/TerrainWindow.cpp`: existing `Editor::groundAt` cursor projection and native Terrain GUI, drawing in `update` when editor owns `Tool::Count`.
- `tests/terraweave_sparse_editor_test.cpp`: existing MapDoc save/restore and undo/redo convention.
- Stage 18 `CaveVolumeSnapshot` is the canonical authored density input for worker rebuild.

## Implemented
1. `caveSnapshotToJson` / `caveSnapshotFromJson`: stable schema v1 in `MapDoc::extra["terraweaveCaverns"]`, validates/canonicalizes generator settings, entrance and stamp IDs, preserves source revision and deterministic ordered author inputs. No GPU mesh serialization.
2. `terraweave_caverns_paint` op accessible through native editor Workspace and shared MCP/HTML5 command bridge. `carve`/`fill` take signed world centimetres XYZ and brush radius; `erase` removes a stable ID; `get` returns canonical saved snapshot. Revisions bump on actual changes. Each call normally creates one MapDoc undo unit; `withinStroke` groups mouse dabs into the current stroke. Invalid parameters never partially mutate map data.
3. TerrainWindow Caverns panel: toggle scene brush, Carve/Fill selector, radius in centimetres, depth below existing scene plane, Shift inverses action, drag-to-paint and a camera-center stamp button. Deactivates sparse terrain mode when selected.
4. Test fixture includes carve, fill, erase, invalid brush, undo/redo, normal MapDoc serialization and restored document equality.

## Limitations
- **This is a real editor operation and GUI source change, but it has not been compiled or run in a full AIVERSE checkout.** No assertion that GUI painting works on hardware or that editor/MSVC compiles cleanly.
- Brush XY projection uses `ed.groundAt` for world X/Z; Y is the configured depth below y=0. It is NOT cave-wall triangle ray-picking or a full volumetric scene edit gizmo. Native/editor viewport still needs actual cave geometry selection and brush preview.
- MapDoc extra persists authored inputs only; runtime loading, per-chunk + halo dirty classification, STRATA/GeometryScene mesh refresh, Box3D/SpatialWorld/NavMesh updates and game replay/replication are NOT hooked up.
- Stage19 `CaveSphereStamp` supports only spherical carve/fill. No spline tunnels, smoothing, erosion, brush pressure, flood fill or placement lock.
- SourceRevision is persisted; host must reparse/canonicalize, bump when any generator or entrance data changes and send the same immutable snapshot to neighbor rebuild workers.
- ID choice uses max(live ID, current sourceRevision)+1; this preserves usual monotonic histories, but arbitrary imported IDs and undo history can still need a dedicated persistent next-ID allocator for collaborative editing.
- The existing shared `Workspace::call` is expected to surface the new operation to HTML5/MCP, but connector catalog synchronization and actual web UI behavior have not been verified.

## Tests actually performed
- All listed GitHub source/skill commits were acknowledged.
- **Not performed:** compilation of Stage19 code, native editor linking, CMake/CTest, UI mouse tests, HTML5/MCP end-to-end operation tests, MapDoc full project save/load in actual app, GPU/STRATA/Box3D/NavMesh tests.
- Prior stage2 local mesh test with 3252 triangles is NOT evidence of the new Stage19 changes.
- In an AIVERSE checkout, add a CMake target with editor/Workspace linkage and run `tests/terraweave_caverns_editor_test.cpp` alongside `terraweave_sparse_editor`; inspect Scene input ownership and Undo after repeated drag strokes.

## Next stage
1. Build/compile Stage1–19 and repair errors before merge, then validate generated scene and editor commands.
2. Implement true 3D ray-picking against cave surfaces and a visible sphere brush gizmo; support editing in arbitrary XYZ and hover-lock constraints.
3. Wire `Change::Map` Caverns updates to sourceSnapshot versions, affected chunk + full influence halo invalidation, async workers and the seam gate. Connect safely to GeometryScene/STRATA and Box3D/SpatialWorld/NavMesh via a proven owner commit barrier.
4. Add persistent next-ID counter and explicit save format migration/fingerprinting, plus spline/soft sculpt brushes and editor hotkeys.
5. Keep skill and independent dated handoff after EVERY stage. Never claim tests passed without execution.
