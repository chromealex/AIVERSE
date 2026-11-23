# SpectraStudio Stage 15 — native FX stage settings and playback clock

Date: 2026-11-23
Repo: chromealex/AIVERSE
Branch: feature/spectrafx-html5-data-interfaces-20261123
Draft PR: #115, stacked on draft PR #113
Code HEAD immediately before handoff: eb5a6cd0206aab3bea8a0fae7ab2366982ba0417.
This document commit advances branch HEAD. Main was not modified.

## Goal

Expand actual native SpectraFX Graph preview authoring features while
explicitly DEFERRING testing until the user is ready. Stage 13/14 already
added independent Scene/FX GPU drawables; these commands work on the FX
Scene render-owner thread, not authoritative gameplay/ECS.

## Source changes

- `apps/studio-native/StudioSpectraPreview.h`:
  new `playback` command accepts explicit
  `{"action":"playback","speed":0.25|0.5|1|2|4,"loop":true|false}`,
  even before loading a graph. The renderer still executes fixed
  `1/60`-second simulation steps; speed controls only the bounded
  number of complete ticks per native frame. Quarter-tick integer
  accumulator allows 0.25× and 0.5× without variable delta time.
  Fast modes execute at most four simulation ticks/frame. At the
  existing 3600-tick preview limit, `loop:true` schedules a restart
  during the *next safe frame*, never inside an active render pass.
  Pause, Step, Seek, Stop and Replay reset fractional accumulator.
  Status includes `speed` and `loop` fields.
- `apps/studio-native/StudioScene.h/.cpp`:
  new `stage` command supports only named background presets
  `dark`, `neutral`, `light`, `black`, plus boolean `grid`
  and `axes`. The FX-only native renderer paints the selected real
  clear color and optionally draws grid/XYZ guides. No graph JSON,
  project assets or map entities are modified. New `cameraPreset`
  command with `perspective|front|side|top`; non-perspective views
  are orthographic. The camera is the independent FX view camera,
  not the project Scene camera. Stage settings are native, bounded
  and kept until the Studio Scene instance ends.
  Every status/load/playback response includes background/grid/axes
  alongside the existing GraphRuntime status.
- `apps/studio-web/src/types.ts`:
  strictly typed Stage/Camera/Playback IPC command payload and
  native state fields.
- `apps/studio-web/src/panels/SpectraNativePreview.tsx/.css`:
  HTML5 Studio dropdowns for background/camera/speed and checkboxes
  for grid, axes, looping. Controls are positioned *outside* the
  native CAMetalLayer/Vulkan rectangle so the native child view
  does not swallow their pointer events.
- `apps/editor/core/SpectraGraphOps.cpp`:
  the existing `aiversemcp` `fx_graph_preview` op accepts
  `playback`, `stage` and `cameraPreset` with typed input
  schema and the same native host routing; no second MCP service.

## Example MCP operations

```json
{"action":"playback","speed":0.5,"loop":true}
{"action":"stage","background":"black","grid":false,"axes":false}
{"action":"cameraPreset","preset":"front"}
{"action":"status"}
```

The browser uses `studio.spectraPreview` for the same actions.

## Tests and limitations

- **TESTS DELIBERATELY DEFERRED by the user** for this feature phase.
  No new Node, TypeScript/Vite, AppleClang/Metal or Windows/Vulkan
  build/CTest outcomes are claimed for this Stage 15 source.
- Playback is fixed-tick *preview-time*, not simulation time
  authoritative for gameplay or server physics. The UI frame
  scheduler drives increments and is not guaranteed to equal wall
  clock at low frame rates. The default preview boundary remains
  60 seconds per loop.
- No HDR color picker, arbitrary file uploads, full curve animation
  or actual GPU/CPU pixel parity included in this stage.
- Potential compatibility failures on exact current SHA are unknown
  until the deferred Mac and Windows compiler/GUI validations run.

## Next

Continue functionality (Stage 16: opt-in safe Graph v2 hot-reload),
add a separate handoff, then tackle event/burst preview controls.
Only once user requests testing, run `npm run typecheck`,
`npm run test:graphs`, `npm run build`, real
`aiverse_studio`/native Metal test and
`vfx_graph_ops` MCP tests on exact commit SHA.
