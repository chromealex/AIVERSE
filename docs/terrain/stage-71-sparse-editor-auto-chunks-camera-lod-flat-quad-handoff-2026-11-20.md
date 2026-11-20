# TerraWeave Stage71 — sparse open-world editor chunks, adaptive terrain LOD, flat-quad optimization

Date: 2026-11-20
Repo: `chromealex/AIVERSE`
Working branch: `integration/terraweave-physis-stage70-20261120`
Draft integration PR: https://github.com/chromealex/AIVERSE/pull/53
Stage input/base: `25959ce1af20f3c20c748a600c72e96f4c6208f1`

## User requirements (2026-11-20)

1. Editor terrain creation and painting **anywhere the cursor points**,
   including outside the original fixed RTS map rectangle.
2. A click on empty terrain must **automatically create a chunk**.
   A stroke across neighbor cells must create all intersected missing
   chunks and stitch them with **one shared height source**, not separate
   per-chunk border authoring.
3. Visual terrain mesh must use **camera-distance LOD**, not permanently
   fixed grid density.
4. **Every truly flat terrain chunk must be one quad** at any distance
   (4 indexed vertices, 2 triangles), and return to detailed geometry
   as soon as sculpting makes its heights non-flat.
5. The **authoritative collision/navmesh and physics grid must stay
   independent of camera-distance visual optimization**.

## Implemented new architecture

**Shared CPU world API:**
`engine/world/TerraWeaveSparseTerrainAuthoring.h`.
- Sparse `std::set<world::Cell>` occupancy independent of map width/height.
  A click creates only its missing signed X/Z chunk, with collision-world
  bounds and 65,536 authored tile limit. Adjacent chunks use one
  `EarthworksLayer` height ledger; there is no duplicated border data.
- `paint(EarthworkBrush)` stages all intersecting occupied tiles, then
  calls the production atomic `EarthworksLayer::sculpt`; on error neither
  chunk manifest nor original Earthworks terrain is mutated.
- `restore(span<Cell>)` restores canonical sorted occupancy with
  exact duplicate/extent validation, no implicit world-generation side
  effects or duplicate saved heights.
- `SparseTerrainLod` contains nested resolutions 33/17/9/5 and camera
  thresholds 320/960/2560m by default. Near/far selection uses
  absolute-world signed centimetres. Adjacent non-flat tiers differ
  by at most one after bounded propagation.
- `flat(cell)` checks a 33x33 camera-INDEPENDENT sample lattice plus
  all sparse overrides touching that tile; cache keyed by authoritative
  Earthworks revision. **Effective flat resolution is 2 for all camera
  positions**, even when neighboring tiles have higher LOD.
- `bakeSparseTerrainVisual` morphs extra high-density border heights
  onto the coarser neighbor's piecewise-linear edge before rebuilding
  GPU-only triangle topology. It sets `presentationOnly=true`.
  It MUST NOT be passed to the collision publisher.
- `view(...,maxDistanceCm)` optionally filters distant occupancy
  BEFORE expensive flatness sampling. Editor uses 2km streamed radius.

**Render and collision separation:**
- `engine/world/TerraWeaveTerrainPatch.h` adds a **trailing**
  `presentationOnly` field for source compatibility, and
  `TerrainCollisionPublisher` rejects such patches.
- `engine/render/TerraWeaveTerrainPatchRender.h` handles a flat
  2x2 sample patch with **exactly 4 vertices, 6 indices (2 triangles)**,
  never a subdivided grid or six duplicated vertices. It has opt-in
  visual-only model swaps guarded by `Device::waitForResourceIdle()`,
  preserving fixed-resolution physical triangles.
- `engine/world/TerraWeaveTerrainRuntime.h` has a backward-compatible
  trailing `std::span<const SparseTerrainView> adaptive={}` argument
  to `TerrainMutationCoordinator::publishVisible`. When a host passes
  an adaptive plan, the display model changes with the camera while
  the normal fixed-resolution `TerrainPatch` remains canonical for
  collision/navmesh. A pure LOD change skips expensive collider edits
  and authoritative dirty notifications.

**Editor and shared agent operations:**
- `apps/editor/core/TerraWeaveSparseOps.h` implements the single
  `Workspace::call("terraweave_sparse_paint", ...)` operation with
  `create`, `raise`, `lower` modes, signed world centimetres,
  optional chunk size, radius and depth, validation and undo.
- `apps/editor/core/TerraWeaveOps.h` registers it alongside existing
  terrain operations, so MCP, native editor and a future HTML5 frontend
  can all use the SAME operation without separate terrain logic.
- MapDoc `extra["terraweaveSparse"]` saves v1 occupancy, stable
  height overrides and one Earthworks revision. There is no
  implicit resizing of the original RTS rectangular map.
- `apps/editor/core/MapDoc.cpp` preserves this one optional extra
  field in FPS JSON, whose previous early return bypassed all extras.
- `apps/editor/windows/TerrainWindow.cpp` adds the
  **Sparse / open world** mode, Create/Raise/Lower, auto-activation,
  chunk-size control, live chunk count and a button to create
  a chunk at the scene camera target. Scene mouse strokes use
  `Editor::groundAt` (world ray/ground-plane fallback outside
  old map) then invoke the shared Workspace op with one undo
  snapshot per stroke.
- `apps/editor/TerraWeaveSparsePreview.h`, `Editor.h` and
  `Editor.cpp` render sparse maps into the existing Scene
  `Renderer3D` before scene resolve. Preview geometry uses
  `bakeSparseTerrainVisual`, uploads bounded to 4 tiles/frame,
  keeps at most 64 nearby tile models and retires old models
  via the Editor's existing `NativePresentationRetirement`
  GPU-fence queue. Editor shutdown releases resident models
  only after its real device-idle barrier.
  The legacy RTS grid may still be underneath this optional
  overlay; Scene preview offsets the visual surface by 2cm
  to avoid z-fighting. This is NOT a physics displacement.
- The new source is genre-neutral. FPS maps retain their
  authored terrain in FPS JSON, but the user's native module
  still decides whether to opt into this terrain system.

## Tests authored and CMake targets

- `tests/terraweave_sparse_authoring_lod_test.cpp`
  CTest: `terraweave_sparse_authoring_lod`
  executable `aiverse_terraweave_sparse_authoring_lod_test`.
  Covers negative/far-world clicking, duplicate-free tiles, cross-boundary
  auto-creation, terrain sculpting, dynamic camera distance resolution,
  flat 4-vertex / 2-triangle mesh near AND far, welded LOD edges,
  persistence of occupancy and atomic invalid brush rejection.
- `tests/terraweave_sparse_editor_test.cpp`
  CTest: `terraweave_sparse_editor`
  executable `aiverse_terraweave_sparse_editor_test`.
  Covers shared Workspace op, very distant click without legacy map
  resize, adjacent chunk creation, persistent JSON heights,
  Undo/Redo, failed-validation rollback and FPS map round-trip.
- Both tests are registered in `CMakeLists.txt`; editor test
  requires `aiverse_editor_core`. Earlier generic TerraWeave tests
  remain registered and unchanged.

## Actual verification status — be strict

**DONE:** inspected authoritative Earthworks/SpatialWorld/
TerrainPatchResident APIs, TerrainWindow/SceneWindow/Editor lifecycle,
MapDoc FPS serialization and existing GPU preview retirement owner
through connected GitHub. All source changes above were committed
to the integration branch, and read-back confirmed new key APIs,
CMake registration, two tests, editor ops and Scene preview.

**NOT RUN:** AppleClang/Clang/GCC C++ compile of the NEW source,
new CTests, Editor Scene application interactions or screenshots,
hardware LOD/flat-mesh pixel readback, native runtime adaptive
collision parity, any fresh full CTest, Android Vulkan/GLES3,
and sustained four-owner PHYSIS/NEREID/TerraWeave/Box3D erosion.

The older 271/271 CTest PASS and real Metal forest 1/1 PASS remain
valid for their earlier EXACT SHAs, not a validation of this work.
DO NOT claim Stage71 acceptance or merge PR #53 before native checks.

## Mac Studio minimal validation (no full expensive CTest yet)

```bash
cd ~/Projects/AIVERSE-terraweave-stage70-local-qa
git status --short
git fetch origin main integration/terraweave-physis-stage70-20261120
git switch --detach origin/integration/terraweave-physis-stage70-20261120
git rev-parse HEAD
cmake --build build/terraweave-main-acceptance --parallel 4 \
  --target aiverse_terraweave_sparse_authoring_lod_test \
           aiverse_terraweave_sparse_editor_test aiverse_editor
ctest --test-dir build/terraweave-main-acceptance \
  -R '^terraweave_sparse_(authoring_lod|editor)$' \
  --no-tests=error --output-on-failure
```

If build cache was configured without editor tests, first reconfigure
using `tools/terraweave-main-integration-acceptance.sh`'s
existing CMake flags; do not clean previous GPU reports.
Record the FIRST actual native compiler failure, fix narrowly and
append a new handoff. Once source compiles, inspect Scene:
create a sparse chunk beyond old map, drag raised terrain across its
boundary, compare near/far triangle counts and check Undo/Redo.
Test FPS and RTS map save/reopen.

## Known limitations / next agent action

1. **High priority:** real Mac build and two new CTests have not run.
   Fix compile/link issues and record exact tested SHA before adding
   more source. Run a real Scene preview and inspect flat-quad
   draw calls, seams and GPU-retirement events.
2. Sparse editor brush currently exposes only Create/Raise/Lower.
   Add Smooth/Flatten/Noise/Material and richer brush visualization
   through shared `Workspace` ops rather than duplicating CPU
   algorithms in Scene/HTML5. UI needs explicit sparse LOD settings,
   drag preview and editable chunk list.
3. Sparse map persistence is an editor document format for now.
   Production clients must explicitly consume
   `terraweaveSparse` and pass `authoring.view(...)` to
   `TerrainMutationCoordinator::publishVisible`; legacy games
   will not automatically opt in. Collision continues to use
   fixed resolution and normal material/asset owner policies.
4. A camera-independent 33x33 exact-height sample (plus explicit
   sparse overrides) proves flatness for the represented grid,
   not an arbitrary pathological procedural function with
   sub-grid invisible spikes; hosts with such a generator
   must increase probe resolution or supply an exact flatness
   certificate. Distances/resolutions are configurable.
5. With a legacy RTS rectangle still visible, preview uses a
   +2cm VISUAL lift to avoid z-fighting; eventual sparse-first
   world layout should suppress the old ground draw where
   replacement owns the same cells, while preserving units.
6. Other feature owners: HTML5 editor agent should consume
   `terraweave_sparse_paint` and sparse document, not build
   another incompatible terrain manager; NEREID remains water
   authority, PHYSIS weather, Box3D physics.
7. Separate prior Stage70 tasks remain: Linux Vulkan/GLES GPU CI
   (previously queued), long AUTHENTIC four-owner PHYSIS/NEREID/
   TerraWeave/Box3D storm/erosion/fall rollback integration,
   and PR #53 final merge/release review. Do not infer these
   from the sparse editor test results.

Every later bounded stage MUST have its own standalone committed
handoff with changed files, source SHA, actual tests, blockers,
architectural contracts and next action.
