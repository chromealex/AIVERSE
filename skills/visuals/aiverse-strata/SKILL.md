---
name: aiverse-strata
description: Analyze STRATA virtual geometry assets through AIVERSE MCP, integrate native staging and routing, or verify STRATA LOD, streaming and batching changes.
---

# STRATA

Read `docs/strata.md` for current implementation and verification coverage; numbered stages are historical. Read the latest applicable `docs/agent-handoffs/strata-*.md` before editing. Keep a separate handoff commit after each implementation stage, recording exact SHA and actual checks.

## Asset authoring through MCP

Use the existing AIVERSE editor connection (`tools/mcp/main.cpp`), with a rebuilt editor and server. Discover tools before calling them. Call `strata_capabilities` first: builder availability is a compile-time property, not device capability or GPU acceptance. Open the intended game using `open_game`, then call `strata_model_analyze` with a project-relative model path, for example `{"model":"models/scenery.glb"}`.

Analysis loads real assets and calls the shared model adapter with conservative default options. Inspect eligible primitives, skipped reasons and seam results. Limits are 250000 vertices, 750000 indices and 64 MiB cumulative input reads; external model dependencies must remain inside the project. Decoded asset memory can exceed file size. No persistent archive, scene setting or GPU resource is created. Unsupported assets stay on the legacy route; do not delete them or force opt-ins to hide a rejection.

## Debug visualisation

To see chunks (selected clusters) and geometry, use `engine/render/StrataDebugView.h` through `GeometryScene::setStrataDebugView` (runtime) and `setStrataDebugGeometryCaptureEnabled` (before load, needed for wireframe/fill). It draws the frontier the portable selector already published; it is not a selection or parity tool. Read the "Debug visualisation" section of `docs/strata.md` for colour modes, route colours, caps and what is not shown.

## Native integration

Use `engine/render/StrataModelAdapter.h`, `engine/render/StrataView.h`, `engine/render/StrataRenderRouting.h` and `engine/render/GeometryScene.h`; reuse existing ownership and fallback paths. STRATA is presentation data, never authoritative ECS state. Do not duplicate genre engines or claim that staging enables rendering. Preserve animated/skinned/morph/material exclusions and source-wide seam checks.

Before batching or renderer edits read `docs/metal-batching-agent-handoff.md`. Keep experimental flags and GPU parity gates default OFF. CPU hierarchy/seam validation is distinct from backend execution, visual parity and same-scene CPU/GPU A/B/A measurements. Vulkan compile-only success is not GPU execution; unavailable runtime is SKIP, strict device acceptance must fail when unavailable.

For this workspace, place builds, temporary files, caches and evidence on KINGSTON under /Volumes/KINGSTON/AIVERSE-builds. Run focused STRATA CTests and skill catalog/audit checks; run real backend fixtures only where devices are available. Report architecture, backend, skips and NOT RUN coverage explicitly. Do not infer speedups from fewer nodes, commands or copied bytes.
