---
name: aiverse-lighting
description: Configure and integrate AIVERSE Light entities, probe GI and Luma direct lighting using native MCP and the shared renderer runtime; validate actual backend behavior.
---

# AIVERSE lighting

Read `AGENTS.md`, `docs/studio-light-entities.md`, `docs/dynamic-gi.md`,
`docs/luma-native-lighting.md` and the latest lighting handoff. Use PRISMA's
existing image pipeline for post-processing; do not build a second renderer.

## Authoring via MCP

Use `open_game`, then `scene_entity_list`/`scene_entity_get` to discover actual
entities and components. Lights have Transform and Light components, with one
canonical document payload and persisted entityId. `scene_light_create` creates
one entity; `scene_component_set` patches its declared component. Example arguments:

```json
{"name":"Key light","position":[4,2,4],"light":{"type":"point","radius":12,"intensity":6,"color":[1,0.8,0.6]}}
```

`scene_render_lighting_get` reads map renderLighting v1;
`scene_render_lighting_set` accepts a patch, for example:

```json
{"patch":{"indirect":"auto","direct":"luma","quality":"high"}}
```

Indirect modes: off/cpu/auto/gpu. Direct modes: legacy/luma. Use native validation
for probe counts, layers, spacing, rays and budget, then `map_save` for persistence.
The settings getter reports authored settings, not the actual GPU backend.
Use Studio's actual lighting status or renderbench reports to verify fallback.

## Native integration and evidence

Use `engine/render/RenderLightingRuntime.h`, `DynamicGiHost.h`, `LumaHost.h` and
`GiModelGeometry.h`. Keep geometry complete: bounded GPU overflow falls back or
rejects explicitly; never silently drop casters. Retire GPU resources with the
existing fence/idle contract. Studio currently previews terrain/document proxies.
Animated benchmark poses wait for convergence; that is not realtime gameplay
performance. Opaque snapshots do not model transparency or shader displacement.

Build and store all artifacts on KINGSTON. Use the existing Metal lighting tests
and `tools/renderbench/lighting_release_acceptance.py` for fixed-camera Release
arm64 OFF/ON/OFF measurement. Separate load/convergence from submitted CPU/GPU
frames; background builds invalidate isolated performance claims. Experimental
ICB stays OFF by default. Android/Vulkan hardware parity is unverified until an
actual device run; shader compilation is not hardware acceptance.
