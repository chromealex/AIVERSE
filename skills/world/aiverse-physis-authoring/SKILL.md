---
name: aiverse-physis-authoring
description: Configure and validate authored PHYSIS weather, wind, storm and gravity fields through the AIVERSE MCP server with revision checks and Undo/Redo. Use for map authoring, not live server administration or visual fog rendering.
---

# PHYSIS map authoring through MCP

Use the existing `aiversemcp` executable (CMake target `aiversemcp`), which exposes the same Workspace operations as the native editor. Do not create a second MCP server. Source: `apps/editor/core/PhysisOps.cpp`; authoring contract: `apps/editor/PhysisAuthoring.h`.

## Connect and inspect

Launch the built server with `--engine <checkout>` and the intended editor's `--port <port>`. Read its actual tools/list response before using commands; an old executable may not expose new operations. Use the existing project/map open tools to select the intended document. The server's `--no-editor` disables auto-launch but may still connect to an already running editor at that port; choose an unused dedicated port for isolated headless work.

1. Call `physis_get` with `{}`. It returns `config`, `defaultConfig`, `revision`, and `liveApplied:false`. These are authored document values, not live ECS diagnostics.
2. Preserve the returned config, including unknown host-owned keys. For a new map start from `defaultConfig`. Edit grid/climate, enable flags and optional windZones/storms/gravityFields using the existing field schema in `engine/physis/PhysisFieldAssets.h`. Moving tornadoes go in `tornadoes` (schema in `engine/physis/PhysisTornadoAssets.h`, docs/physis.md "Tornadoes"); a non-empty array needs no enable flag. Fire whirl / waterspout / ground scour are the tornado's `fire`, `surface`, `erosion` objects; scene fires are `ignitions` (need `enableDisasters`). When NEREID owns water, select `floodMode: "ExternalAuthority"`.
3. Call `physis_validate` with `{"config": <complete candidate>}`. Validation does not change the document. Read any `error`; do not treat a transport-success envelope as successful validation.
4. Call `physis_set` with `{"config": <complete candidate>, "expectedRevision": <revision from step 1>}`. This replaces the complete PHYSIS block atomically, preserves other map data and records ordinary MapDoc Undo. Omitted `config` fails; explicit null removes the block. The current document revision is required even for removal.
5. On revision mismatch, read again and reconcile the requested changes against current content before retrying. Never retry a stale full replacement blindly. Read back with `physis_get`; save through the normal map-save operation only when requested by the task. No-op sets return `changed:false`.

Use `physis_zone` with the revision from `physis_get` to add a wind, storm or gravity zone. The response includes its stable ID. Send `edit` with that ID and a property patch, or `duplicate` or `remove` with the ID. Each change uses map Undo history.

Use `physis_zone_group` with a revision and ordered zone IDs for a single Undo transaction. Supply XYZ `offset` for `move`, or `yaw` (degrees), uniform `scale` and `pivot` for `transform`. The active pivot uses the first selected ID. The other pivot choices are mean, bounds, origin and custom XYZ point. Duplicate returns the new IDs; remove clears the selection.

## Authority and verification

These operations do not tick, install, or modify a live simulation, remote server, Box3D body, fog volume or game visibility. Host integration uses `skills/world/aiverse-physis/SKILL.md`. Visual clouds/god rays and local volumetric fog use `skills/visuals/aiverse-prisma/SKILL.md` and its atmosphere reference.

Validate native operation behavior with the existing `physis_authoring` CTest via `run-tests.sh`; it covers rejection without mutation, stale revisions, no-op, removal, Undo/Redo and FPS document routing. Check actual MCP tools/list and tools/call too. For this workspace, builds and compiler temporary/module-cache files belong on KINGSTON as requested by the owner. A diagnostic demo or headless test does not prove host integration or visual quality.

## Inspect the isolated diagnostic demo

Use `physis_demo` with `{"action":"reset"}` to create a private seeded ECS world. Sample it with `{"action":"view","layer":8,"palette":2}` for wind speed with the ocean palette. Actions `step` and `step60` advance one or sixty committed ticks. The response includes sample coordinates, tick, valid values, palette colors and Wind/Storm/Gravity outlines. Empty cells return null. For wind and gravity XYZ at the cell center, call `{"action":"probe","cellX":0,"cellZ":0}` and compare the returned tick with the raster tick. The demo belongs to the Workspace project lifetime and does not edit the map or advance the game.

Workflow-only skill; no separate runtime library. Uses `aiverse_editor_core` through `aiversemcp`.
