# Lighting main stage 39 — world / TerraWeave geometry feeds (2026-11-21)

Branch `feature/aiv20261121_gi_stage38` (continues stage 38). Item 2 of the
step-2 plan in `main-integration-gi-2026-11-21.md`.

## What changed

- `engine/render/GiGeometryScene.h` (new, rewritten for main): immutable
  chunk scene with per-chunk bounds and dirty regions. `publish(target)`
  sends a full snapshot first (or after >128 edits), later only the union of
  changed old/new bounds. A refused publication commits nothing and is
  retried. Targets are main's owners: `GiMainGpuBridge` / `GiMainCpuBridge`.
  The S62 version targeted `GiDynamicRuntime` (not in main) and kept a
  retired-snapshot list; main's bridges own the snapshot lifetime.
- `GiMainCpuBridge::publishGeometryRegion` and
  `GiMainGpuBridge::publishGeometryRegion`: only probes that can see the
  changed bounds (trace distance + widest light) are cleared / re-traced.
- Ported from `feature/gi-bvh-precision-stage62-20261120@082ae5538`:
  `engine/world/GiWorldGeometryBridge.h` (WorldScene collision, TerraWeave
  patches/procedural chunks, vegetation proxies into stable chunk ids, dirty
  rects) and `engine/world/GiTerraWeaveResidentBridge.h` (publishes a
  TerraWeave `TerrainPatchResident` patch only after it is committed; keeps it
  staged and the renderer unbound while GI refuses). Changes: publish through
  `GiGeometryScene::publish`, `guardRenderer` is a template.

Not ported: `GiWorldPresentationSync.h` (built on S62 `GiLightingController`,
which main replaced with `DynamicGiHost`/`RenderLightingRuntime`) and
`gi_regional_geometry` (tests S62 `GiDynamicRuntime`; regional behaviour is
covered by the tests below).

## Tests

CPU fixtures (Lighting loop): `lighting_gi_geometry_scene` (CPU bridge:
refused retry, full then regional publish, far probes keep their light),
`lighting_gi_world_geometry_bridge` (S62 test against the new publish
protocol), `lighting_gi_terraweave_resident_bridge` (new: uncommitted
revision ignored, refusal keeps it staged + unbinds the renderer, retry after
the notification is gone, unload). `lighting_gi_gpu_residency_planner` also
checks GPU regional publication (far edit dirties nothing, near edit does).

Run here (g++ 14, single files): all pass. CMake/CTest/Metal: Mac run.

## Not done

No in-tree host publishes TerraWeave patches into `DynamicGiHost` yet (it
takes one static geometry at initialize plus `replaceGeometry`). Wiring a
native TerraWeave scene to `GiTerraWeaveResidentBridge` + Metal acceptance is
open.

## Next

Item 3: GI cascades / clipmaps — needs the texture-slot plan first.
