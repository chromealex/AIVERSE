---
name: aiverse-prisma
description: PRISMA — universal AIVERSE post-processing, HDR, tone mapping, bloom, temporal/screen-space effects and camera volumes; migration and agent handoff.
---

# PRISMA — AIVERSE universal image post-processing

**Read first:** `AGENTS.md` (mandatory PRISMA vs SpectraFX ownership matrix), `docs/prisma.md`, `docs/postfx.md`, `docs/hdr.md`, the latest `docs/handoffs/prisma-stage-XX.md`.

## Choose the right owner

- PRISMA owns **image-space processing**: HDR scene resolve, tonemapping, color grading, LUTs, bloom, DoF, motion blur, auto exposure, TAA/FXAA/upscaling, screen-space GI/AO/SSR, camera volumes and full-screen `postfx` Shader Graph domain. `PrismaScreenLighting` is part of PRISMA.
- SpectraFX owns **simulation/presentation of 3D VFX**: emitter graphs, sprite/mesh/ribbon particles, smoke/fire/explosions/trails, ECS FX events. It adds linear HDR pixels to the scene BEFORE PRISMA processes the result.
- Shared Renderer3D/RHI, generic Shader Graph compiler and ECS transport adapters are engine infrastructure, not a second postprocess implementation. **Do not duplicate features or couple them to genres.**

## Native C++ integration

```cpp
#include "engine/render/Prisma.h"
#include "engine/render/PrismaScreenLighting.h"

aiverse::render::Prisma prisma;
prisma.init(device);
prisma.configure(gameManifest["postfx"], mapDocument["postfx"]);
prisma.setQuality(aiverse::render::PrismaQuality::High);

// Optional per-camera blend; currently host-controlled (not auto-wired to ECS):
aiverse::render::PrismaVolumeStack cameraVolumes;
prisma.applyCameraVolumes(cameraVolumes, camera.eye());

// Existing Renderer3D flow: scene, particle emissions, resolve, then UI.
renderer.render(pass, &prisma);
drawSceneVfx();
renderer.resolvePrisma();
// Draw HUD/UI after the image resolve.
```

Use `aiverse::render::PrismaSettings`, `PrismaEffect`, `PrismaType`, `parsePrisma`, `prismaCatalog`, `prismaCostReport` for GPU-free data workflows. Link `AIVERSE::prisma_data` (alias of `aiverse_postfx_data`) for pure data/quality logic; renderer integration uses `aiverse_runtime` as before.

## Serialized camera Post Process Volumes (Stage 08)

Use `engine/render/PrismaVolumeData.h` and `parsePrismaVolumes(game["prismaVolumes"], map["prismaVolumes"], &issues)`. GameClient now applies the merged stack from `game.json` and map data to its current camera every frame. The editor applies it per viewport without rewriting project assets. Volume shapes: global/sphere/axis-aligned box, position, radius/halfExtents, priorities, weight, blendDistance, camera layerMask and `profile` with existing `postfx` effect schema. A matching `id` in the map replaces the game definition; map `replace:true` clears game volumes. Only explicitly authored `hdr`/`enabled` override the base camera's settings. **Per-effect parameters are also selectively blended:** un-authored contrast/saturation/quality/textures remain unchanged when the volume only sets e.g. exposure; see `PrismaVolumeEffectMask` (Stage 09). Native C++ volumes without a mask preserve full-profile semantics. Read `docs/prisma.md` for JSON and known blending restrictions. Tests in `tests/render/prisma_volume.cpp` (CTest `prisma_volume`) must be run before shipping.

## Editor volume authoring (Stage 10)

Open **PRISMA — Постобработка**, choose **Вся игра** or **Эта карта**, then use **PRISMA Volumes** to add a global, sphere or AABB box volume (optionally at Scene camera eye). The panel edits volume transforms, blend/priority/quality masks and nested postfx effects. Checkboxes next to effect parameters control **explicit override vs inherited** values; disabling an override removes the JSON key and preserves global camera style. The shared `makePrismaVolumeJson` factory validates new authored volumes. A native editor/Metal validation is mandatory; currently this UI is committed **but not compiled/run**. See Stage 10 handoff.

## Editor and automation

Existing external identifiers are stable by design: `postfx` JSON key, `postfx_catalog`, `postfx_get`, `postfx_add`, `postfx_set`, `postfx_preset`, `postfx_estimate`, `view_postfx` MCP operations and `postfx` saved panel id. The implementation now lives in `apps/editor/core/PrismaOps.{h,cpp}` and `apps/editor/windows/PrismaWindow.cpp` (title PRISMA). Do not rename persisted schemas or duplicate editor registration.

Common workflow: inspect `postfx_catalog` → choose preset or add effects → check device-tier budget from `postfx_estimate` → compare `view_postfx` tiers and `view_screenshot`. The `postfx` Shader Graph domain is post-processing, not the SpectraFX particle graph.

## GPU-free RenderGraph dependency planning (Stage 07)

Include `engine/render/PrismaRenderGraph.h` from native projects/tooling to declare typed resources and passes, compile a stable topological order, detect cycles, duplicate writers, read/write feedback and invalid resources. The returned `PrismaGraphPlan` describes first/last use and aliasable transient slots; it is **NOT an executable RHI render graph**. External input/output/temporal history are managed by the caller. See `docs/prisma.md` and `tests/render/prisma_graph.cpp`. Do not replace the existing `Prisma::resolve()` sequence until actual backend resource allocation/execution is integrated and GPU-verified.

## Source tree and compatibility

- Canonical core: `engine/render/Prisma.{h,cpp}`, `PrismaData.{h,cpp}`, `PrismaShaders.h`, `PrismaVolumes.h`.
- Temporal/screen-space: `PrismaScreenLighting.{h,cpp}`, `PrismaScreenLightingShaders.h`.
- Legacy `PostFx*.h`, `ScreenLighting.h` and old ops header are compatibility-only; legacy .cpp shims have no definitions. New engine changes MUST go in PRISMA files.
- PRISMA's volume stack is currently opt-in and GPU-free. **RenderGraph, auto exposure, physical depth of field, motion vectors, generalized motion blur and temporal upscaling are not yet implemented**. Never label these as working until code, build and rendering tests prove it.

## Mandatory validation and handoff

Run `./run-tests.sh --quick` plus targeted HDR policy/GPU pixel tests once a native checkout is available; on macOS build `aiverse_hdr_policy`, `aiverse_hdr_scene`, `aiverse_hdr_gl`, editor/client and check the Metal path. Report actual commands and PASS/FAIL/NOT RUN, never infer success from source files or queued CI.

After **each** bounded code stage, commit `docs/handoffs/prisma-stage-XX.md` on the same branch: exact SHA, architecture and API, all changed paths, actual tests/results, limitations, and the next actionable step. Keep PRISMA handoffs separate from `docs/agent-handoffs/universal-niagara-vfx-2026-11-18.md` maintained by SpectraFX.
