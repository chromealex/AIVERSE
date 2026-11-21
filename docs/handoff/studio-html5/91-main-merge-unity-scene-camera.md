# Stage 91 — main merge + Unity-style Scene camera and axis gizmo (2026-11-21)

Branch: `feature/studio-html5`.

## 1. main merged into the branch (`e61a56c75`)

`main` (572 commits ahead, incl. lighting/GI/Luma, light entities, LENS,
STRATA, PHYSIS, engine MCP) merged into the branch. 24 conflicted files were
resolved by hand; the important decisions:

- **Document ids** — `StudioDocumentIds.h` has one canonical
  `studioDocumentId(map, ref)`: `entity:@<id>` for authored map entities,
  `light:<entityId>` for light entities (main), ordinal fallback otherwise.
  The old `(ref, map)` overload forwards to it.
- **Inspector** — `StudioSession::propertiesFor` now also builds the flat light
  field list; `snapshot()` splits it into main's `components` /
  `entityProperties` for light entities. Pinned Inspector keeps the flat list
  (component sections are shown only when unpinned).
- **StudioMetalScene** — main's PRISMA HDR + `RenderLightingRuntime` path and
  `StudioFrameScope` combined with the branch's document models, NEREID water,
  `terrain.render` / entity terrain switch and LOD diagnostics. With a lit
  preview model (`lightingModel_`) the model replaces terrain + proxy cubes;
  GLB document models are still drawn on top (possible duplicate proxy geometry
  inside the lit model — see limits).
- **RtsTerrainRender** — main moved drawing into `drawInternal`; the branch's
  `look_.render && mapSurfaceEnabled_` guard was moved there and into both
  water draws.
- `apps/studio-native/CMakeLists.txt` keeps the always-refresh web asset target
  and adds main's `studio_metal_lighting` / `studio_render_lighting` tests
  (with catalog/transfer sources + SQLite).
- Shaders: SSS transmission (branch) and dynamic GI (main) both kept.
- Two stale source contracts in `tools/studio/test_reference_pin_zoom.py`
  (already failing before the merge) updated to the current code.
- Working copy note: ~1780 tracked files had CRLF endings in the working tree
  while HEAD is LF (hidden by git's stat cache). They were restored from HEAD
  (content-identical apart from `\r`) before the merge. A local uncommitted
  `AGENTS.md` entity rule (identical to main's) was stashed as
  `stash@{0}` "local AGENTS.md entity rule (already in main)".

## 2. Unity-style Scene navigation

| Input | Action |
| --- | --- |
| LMB click | select (pick) |
| RMB hold + mouse | flythrough look |
| RMB + W/A/S/D, Q/E | fly forward/left/back/right, down/up; accelerates 1→3× over 2 s |
| RMB + Shift | 3× faster |
| RMB + wheel | change camera speed (0.05…20, overlay "Camera speed ×N") |
| Alt+LMB drag | orbit around pivot |
| MMB drag, Alt+Cmd+LMB drag | pan |
| Wheel / trackpad pinch | zoom to pivot; at min distance it dollies forward through the pivot |
| Alt+RMB drag | zoom |
| Arrow keys | move without flythrough |
| F | frame selection (animated) |
| Home | reset view (perspective) |

- Mapping is pure and unit-tested: `apps/studio-web/src/panels/sceneCamera.ts`,
  `tests/sceneCamera.test.mjs` (7 tests, wired into `npm run test:scene`).
- HTML composite path: `src/components/ViewportPlaceholder.tsx`. WASD no
  longer moves the camera without RMB (frees W/E/R for future tool hotkeys).
- Native NSView path (`mac/Main.mm`, non-composite mode) uses the same mapping.

## 3. Orientation gizmo (top-right corner)

`src/components/SceneAxisGizmo.tsx/.css`, tokens `--au-axis-*` in
`theme/tokens.css`.

- Driven by real native camera state: `Main.mm -publishSceneCamera` sends
  `viewport.camera {yaw,pitch,distance,ortho}` after a frame when it changed.
  JS validates it (`isSceneCameraState`) — no JS-side camera guessing.
- Click a cone → animated snap to that axis view (eye on that side: Right/Left,
  Top/Bottom, Front/Back) and switch to Iso; orbiting away returns to Persp
  (Unity behaviour). Centre square or caption toggles Persp/Iso.
- Keyboard accessible (`role=button`, Enter/Space), hover/focus states,
  pointer events do not reach viewport navigation.

New native API (`StudioMetalScene`): `setOrientation(yaw,pitch)` (eased 0.24 s
tween, cancelled by any direct input; focus uses the same tween),
`setOrthographic`, `cameraState()`, `viewCamera()` — the camera actually
rendered and picked: clip planes follow distance (near 0.05…0.5, far ≥1000);
ortho keeps framing from `distance` (`orthoHalfHeightOverride`) while the eye
stays within 400 units so the fixed ortho depth range does not clip.
New typed bridge kinds: `studio.viewportInput {kind:'view',yaw,pitch}` and
`{kind:'projection',ortho}`, validated in `Main.mm`.

### Default (non-composite) host — native gizmo

The app runs WITHOUT `--html-composite` by default: the Metal NSView covers the
WebView, so HTML overlays over the Scene are invisible there. Therefore
`StudioMetalScene::drawOrientationGizmo` draws the same gizmo with Renderer3D
overlay triangles (flat screen-space layout unprojected just beyond the near
plane, X/Y/Z stroke letters, Iso-highlighted centre) whenever
`setOrientationGizmo(!htmlComposite_)`; the NSView hit-tests it
(`orientationGizmoHit` / `activateOrientationGizmo`, auto Iso + return to Persp on
orbit/look). Composite mode keeps the HTML gizmo.
Also fixed (pre-existing): in the default host every `studio.viewportInput`
was rejected, so toolbar Focus/Reset did nothing; toolbar kinds
(focus/reset/view/projection) are now accepted in both modes.

Interactive QA on the Mac (default host, project `qa-lua-lab`): gizmo visible,
Y → Top/Iso, centre → Persp, X → Right/Iso, plain LMB drag does not orbit, wheel
zooms in Iso, toolbar Focus frames the selected entity with animation.
Not exercised (automation cannot hold modifiers / RMB): RMB fly, Alt orbit,
Alt+RMB zoom, MMB pan.

## Checks actually run

- `tsc --noEmit` (studio-web): OK.
- `npm run test:web`: 96/96 pass (incl. new sceneCamera tests).
- `python3 -m unittest discover -s tools/studio -p 'test_*.py'`: 41/41 OK
  (navigation contract updated for the new mapping and gizmo).
- `clang++ 18 -std=c++20 -fsyntax-only` on `StudioMetalScene.cpp` and
  `StudioSession.cpp` (Linux, after merge + camera changes): no errors.
- Gizmo rendered in headless Chrome in 6 orientations (default, Front, Top,
  Right, low angle, disabled); click → `viewForAxis` and toggle callbacks fired.

- **macOS (Mac Studio, 2026-11-21, commit `4cfbca5aa`):**
  `bash tools/studio/verify_mac.sh` → EXIT 0: npm typecheck/test:web, vite
  build, Python contracts, CMake build of `AiverseStudioHTML5.app` + all studio
  test targets (incl. main's `studio_metal_lighting`, `studio_render_lighting`
  now listed in the script), `ctest -R '^studio_'` 25/25 passed.
  Log: `logs/studio-verify.log`. One warning (unused `nodeId` overload) fixed
  afterwards.

**Not run yet:** interactive WKWebView QA of the new navigation and gizmo
(open the app with `--project`, check RMB fly, Alt orbit, axis snap, Iso).

## Known limits / next

- LMB drag does nothing yet (Unity: rectangle selection) — next candidate.
- Lit preview model + GLB document models may double-draw proxy geometry.
- Snapped top/bottom views use pitch ±90°; the next orbit clamps to ±1.52 rad
  (small jump).
- Next: Mac build + screenshots of navigation/gizmo, then translate/rotate
  gizmos (W/E/R) on the freed keys.
