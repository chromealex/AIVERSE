# STRATA debug view — handoff (2026-11-21)

Branch `feature/strata-debug-view` (worktree `.worktrees/strata-debug-view`),
based on main `d84e958b1`. Implementation commit
`7d0654223714bfc0db70ed83350ee75688be2fb7`.

## Code changes
- New `engine/render/StrataDebugView.{h,cpp}` (linked into
  `aiverse_strata_portable`): `DebugViewOptions`, `DebugColorMode`,
  `DebugRoute`, `SelectedCluster`, `DebugGeometry`, `DebugDrawStats`,
  `DebugSink`, `emitDebugBox`, `emitSelectedClusters`, colour helpers.
- `PortableSource::setDebugCaptureEnabled/debugSelection/debugAsset`:
  publishes the selected frontier only on successful `prepareModel`.
- `GeometryScene::setStrataDebugView`, `setStrataDebugGeometryCaptureEnabled`
  (before load), `strataDebugStats`, `strataDebugGeometryBytes`; overlay is
  emitted in `drawInstance` via a Renderer3D line/triangle sink.
- Test `tests/render/strata_debug_view.cpp`, CTest `strata_debug_view`.
- Docs: `docs/strata.md` section "Debug visualisation…", skill note.

## Checks actually run
Linux VM (x86_64, g++ 11.4, Release, Ninja), build dir /tmp/build-sdv
(Mac KINGSTON not reachable from this VM):
- built all `aiverse_strata_*_tests` and `GeometryScene.cpp.o` — OK
- `ctest -R strata`: 15 passed, `strata_vulkan_kernel` skipped (no
  device), `strata_lod_benchmark` and `strata_editor` NOT RUN (not built)
- `python3 tools/skills/catalog.py check` — OK

## NOT RUN
- macOS/Metal build and on-screen pixels on any backend.
- Full `aiverse_runtime`/editor link; GeometryScene-level test with a real
  Renderer3D (no existing headless fixture enables portable STRATA).

## Next step
Expose a toggle in a host: e.g. an editor/MCP op `strata_debug_view_set`
once a viewport renders a `GeometryScene` with portable STRATA, or an env
switch in `tests/render/strata_metal_portable_parity.cpp` to capture an
overlay screenshot on KINGSTON builds.

## Publication to main
Merged origin/main `2d034dd6b` into the branch (clean, no conflicts),
rebuilt all `aiverse_strata_*_tests` and `GeometryScene.cpp.o` on the
merged tree (Linux VM, Release): `ctest -R ^strata_` 15 passed,
`strata_vulkan_kernel` skipped (no device); skills catalog check OK.
Fast-forward pushed to origin/main at the user's request.
