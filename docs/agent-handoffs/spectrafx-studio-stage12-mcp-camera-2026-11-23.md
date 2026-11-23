# SpectraStudio Stage 12 — MCP live preview and dedicated FX camera

Date: 2026-11-23
Repository: chromealex/AIVERSE
Branch: feature/spectrafx-html5-data-interfaces-20261123
PR: #115, draft stacked on PR #113
Code source SHA before this handoff: b0766505bbe5c6e9903eca23723a7891e985679d (additional map-isolation fix: 2b7a8ff40cbc89b56e5a5d647392afa4df560df0)
This handoff advances the branch SHA; obtain exact HEAD from GitHub before QA.
Main and other agent branches untouched.

## Goal and feature complete at source level

Use existing AIVERSE `aiversemcp` tooling (NOT a second MCP server) to
control the **same real native GPU/CPU particle preview** as HTML5 Studio,
including a dedicated FX stage camera.

## Shared tools and ABI

The Workspace operations registered in
`apps/editor/core/SpectraGraphOps.cpp` already expose:
`fx_graph_list`, `fx_graph_get`, `fx_graph_validate`,
`fx_graph_node_catalog`, `fx_graph_edit`, `fx_graph_set`,
`fx_graph_draft_list`, `fx_graph_draft_get`,
`fx_graph_draft_set`, `fx_graph_draft_publish`,
`fx_graph_delete`, `fx_graph_bake`.
The HTML5 Curve/Gradient inspector uses the same native Graph v2 schema.
No extra, incompatible graph or file-authoring backend was introduced.

Added `fx_graph_preview`, always discoverable via the existing
`Workspace::toolList()` used by `tools/mcp/main.cpp`:

- `{"action":"status"}`
- `{"action":"load","graphName":"preview_fx"}` or `{"action":"load","graphName":"preview_fx","draft":true}` or
  `{"action":"load","graph":{...}}`
- `{"action":"play"}`, `{"action":"pause"}`,
  `{"action":"step"}`, `{"action":"restart","play":false}`,
  `{"action":"seek","tick":120}`, `{"action":"stop"}`
- `{"action":"parameter","name":"wind_lift","value":3}`
- `{"action":"cameraReset"}`,
  `{"action":"cameraOrbit","dx":12,"dy":-4}`,
  `{"action":"cameraZoom","amount":15}`.

A preview `load` with `graphName` resolves ONLY in the active
Workspace project under the existing validated
`fx/graphs/<name>.vfx.json` or `fx/graphs/_drafts/<name>.vfx.json`;
rejects traversal, ambiguous simultaneous `graph`/`graphName`,
invalid method and invalid source. Graph `set` and `draft_set` still
require their preexisting native compiler/validation rules and explicit
author requests. Play does not modify ECS or game state.
`fx_graph_preview` does not send raw RHI handles over JSON.

`Workspace.h` has optional `setSpectraPreviewHandler(OpFn)`, bound by
`StudioHost` on construction and cleared at shutdown.
`StudioHost::runSpectraPreview` marshals the actual command to
`StudioScene::spectraPreviewCommand` on the native render-owner thread.
The HTML5 `studio.spectraPreview` API and the MCP tool use exactly the
same `runSpectraPreview` method. When `aiversemcp --no-editor` is run,
graph authoring tools remain operational; `fx_graph_preview` is
discoverable but returns the explicit error
`SpectraFX live preview requires an open native Studio GPU viewport`
instead of creating fake GPU frames.

## Dedicated FX camera — next feature after embedding

- HTML5 `SpectraNativePreview.tsx/.css` has Zoom In, Zoom Out, Reset
  Camera, plus the existing native viewport drag/orbit/zoom controls.
- `StudioScene::setSpectraDockMode` swaps to a dedicated, stable
  particle-stage camera and restores the authored map camera on exit;
  `resetSpectraDockCamera` resets the isolated preview only.
- `StudioHost::sceneMouse`, `sceneKey`, `viewportInput`,
  `placeAsset`, `placeType` ignore/reject destructive Scene authoring
  operations while FX owns the original native viewport.
- Native `cameraOrbit` and `cameraZoom` take strictly bounded
  finite deltas; `cameraReset` is only allowed in SpectraFX dock mode.
- `StudioTools.h` allows the registered preview op in the Graph tools
  group and recognizes `_preview` as read-only wrt authored documents.

## Files changed (Stage 12)

- `apps/editor/core/Workspace.h`
- `apps/editor/core/SpectraGraphOps.cpp`
- `apps/studio-native/StudioHost.h/.cpp`
- `apps/studio-native/StudioScene.h`
- `apps/studio-native/StudioTools.h`
- `apps/studio-web/src/panels/SpectraNativePreview.tsx/.css`
- `apps/studio-web/src/types.ts`
- `tests/vfx_graph_ops_test.cpp`

The dedicated Graph viewport implementation and its tests are documented
separately under
`docs/agent-handoffs/spectrafx-studio-stage11-embedded-preview-2026-11-23.md`.

## Test evidence and limitations

- Source edits are committed and a new MCP test case was ADDED to
  `tests/vfx_graph_ops_test.cpp`:
  `tools/list` discovery, no-GPU error for headless mode, optional native
  callback pass-through, safe by-name graph loading, invalid/path traversal
  rejection and callback removal at shutdown.
- New HTML5 pure clipping tests were added in Stage 11.
- Native CAMetalLayer fixture was extended to enter embedded FX mode and
  check Scene camera restoration.
- **NOT EXECUTED on current exact source:** C++/AppleClang build, updated
  `vfx_graph_ops` test, full Node22 `test:graphs` and Vite,
  native embedded FX Metal CTest, WKWebView screenshot and Windows WebView2.
  GitHub Actions remained queued at last check.
- Historical Mac 2/2 PASS for `studio_image_codecs` and
  `studio_spectrafx_preview` applies to the earlier Scene-only version;
  it is NOT evidence that this embedded and MCP version passes.
- The Scene and Graph editor intentionally share a single native GPU
  surface. They cannot display independent simultaneous frames with this
  implementation. A true dual-surface editor requires a distinct native
  offscreen resource/fence contract in a later stage.

## Next steps

1. Obtain exact branch HEAD and run on Mac:
   `cd apps/studio-web && npm run typecheck && npm run test:graphs && npm run build`
   followed by CMake target `aiverse_studio`,
   `aiverse_studio_spectrafx_preview_tests`, `aiverse_vfx_graph_ops_tests`
   (check actual target names in CMake), and CTest filters
   `studio_spectrafx_preview` / `vfx_graph_ops` / `studio_image_codecs`.
   Fix actual errors, do not report PASS without logs.
2. With two visible docks verify FX preview takes focus, Scene returns on
   FX close, and no map selection/painting occurs inside FX viewport.
3. Test direct MCP handshake via running native Studio agent link:
   `tools/list` finds `fx_graph_preview`, `tools/call` load/play/
   seek/parameter returns status from actual render thread, and closing
   Studio makes live preview unavailable. Standalone MCP still supports
   normal graph CRUD.
4. Next after that: independent thumbnail/frame capture through a native
   fence-safe handoff and GPU/CPU pixel-delta verification, then per-node
   wire dragging and undo stack.
5. A separate handoff after every stage, with test results, exact SHA,
   changed files, limitations and next action.
