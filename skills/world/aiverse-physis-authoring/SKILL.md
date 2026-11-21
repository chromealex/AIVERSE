---
name: aiverse-physis-authoring
description: Configure and validate authored PHYSIS weather, wind, storm and gravity fields through the AIVERSE MCP server with revision checks and Undo/Redo. Use for map authoring, not live server administration or visual fog rendering.
---

# PHYSIS map authoring through MCP

Use the existing `aiversemcp` executable (CMake target `aiversemcp`), which exposes the same Workspace operations as the native editor. Do not create a second MCP server. Source: `apps/editor/core/PhysisOps.cpp`; authoring contract: `apps/editor/PhysisAuthoring.h`.

## Connect and inspect

Launch the built server with `--engine <checkout>` and the intended editor's `--port <port>`. Read its actual tools/list response before using commands; an old executable may not expose new operations. Use the existing project/map open tools to select the intended document. The server's `--no-editor` disables auto-launch but may still connect to an already running editor at that port; choose an unused dedicated port for isolated headless work.

1. Call `physis_get` with `{}`. It returns `config`, `defaultConfig`, `revision`, and `liveApplied:false`. These are authored document values, not live ECS diagnostics.
2. Preserve the returned config, including unknown host-owned keys. For a new map start from `defaultConfig`. Edit grid/climate, enable flags and optional windZones/storms/gravityFields using the existing field schema in `engine/physis/PhysisFieldAssets.h`. When NEREID owns water, select `floodMode: "ExternalAuthority"`.
3. Call `physis_validate` with `{"config": <complete candidate>}`. Validation does not change the document. Read any `error`; do not treat a transport-success envelope as successful validation.
4. Call `physis_set` with `{"config": <complete candidate>, "expectedRevision": <revision from step 1>}`. This replaces the complete PHYSIS block atomically, preserves other map data and records ordinary MapDoc Undo. Omitted `config` fails; explicit null removes the block. The current document revision is required even for removal.
5. On revision mismatch, read again and reconcile the requested changes against current content before retrying. Never retry a stale full replacement blindly. Read back with `physis_get`; save through the normal map-save operation only when requested by the task. No-op sets return `changed:false`.

## Authority and verification

These operations do not tick, install, or modify a live simulation, remote server, Box3D body, fog volume or game visibility. Host integration uses `skills/world/aiverse-physis/SKILL.md`. Visual clouds/god rays and local volumetric fog use `skills/visuals/aiverse-prisma/SKILL.md` and its atmosphere reference.

Validate native operation behavior with the existing `physis_authoring` CTest via `run-tests.sh`; it covers rejection without mutation, stale revisions, no-op, removal, Undo/Redo and FPS document routing. Check actual MCP tools/list and tools/call too. For this workspace, builds and compiler temporary/module-cache files belong on KINGSTON as requested by the owner. A diagnostic demo or headless test does not prove host integration or visual quality.

Workflow-only skill; no separate runtime library. Uses `aiverse_editor_core` through `aiversemcp`.
