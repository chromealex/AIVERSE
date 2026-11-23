---
name: aiverse-building-ancient-civilizations
description: Ancient Civilizations architecture style, portable AIVERSE building forms and physically based material surfaces.
---

# Ancient Civilizations — building setting

Style: `engine/assets/building-styles/ancient-civilizations.json`.
Parser: `libraries/include/aiverselib/systems/BuildingStyle.h`.
Portable surface textures: `BuildingSurfacePattern.h`.
All construction gameplay, collision/PHYSIS, door movement and graph systems remain in the common engine independent of this setting.

## Building grammar

- **Primary surfaces:** limestone, sandstone, basalt, terracotta. Each named PBR material contains colour, metallic/roughness, deterministic `surfacePattern` and per-channel texture GUID override support. Procedural albedo/metallic-roughness/normal RGBA maps are generated on CPU for both runtime and GLB.
- **Massing:** classical monumental massing, stacked platforms, stone colonnades, temple arches and terraced sanctuaries; four neutral structural silhouette IDs: `residence`, `hall`, `workshop`, `watchtower`.
- **Decoration:** carved archways, thick pillars, cornices, monumental roof rims, sacrificial hall courtyards. Reuse parser-supported `panels`, `corners`, `ribs`, `crown`, `roofBlocks`, `props`, `door`, `window`, `sign` and `structures[].forms`.
- **Variation:** 3 seed-dependent palettes, bounded props/roof modules and geometric dimension constraints, while functional room/door/lift recipes remain independent.

## Agent workflow

Read `skills/world/aiverse-building-style/SKILL.md`, then this style JSON. Change appearance only using strict schema keys. Keep valid referenced materials and neutral structure names. Generate through portable runtime or existing HTML5 Studio `studio.buildingInspect` / `studio.buildingGenerate`; compare geometry and real PBR material output when testing is resumed. Do not hardcode setting IDs in ECS, Renderer3D, PHYSIS, FPS or RTS modules. Document every stage in a distinct handoff, and keep this file registered in `skills/catalog.json`.

QA deferred by user. No native/GLB/GPU captures have been run for this setting.
