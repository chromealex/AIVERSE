---
name: aiverse-building-modern
description: Modern / Contemporary architecture style, portable AIVERSE building forms and physically based material surfaces.
---

# Modern / Contemporary — building setting

Style: `engine/assets/building-styles/modern.json`.
Parser: `libraries/include/aiverselib/systems/BuildingStyle.h`.
Portable surface textures: `BuildingSurfacePattern.h`.
All construction gameplay, collision/PHYSIS, door movement and graph systems remain in the common engine independent of this setting.

## Building grammar

- **Primary surfaces:** reinforced concrete, aluminium curtain walls, glazing, painted signage. Each named PBR material contains colour, metallic/roughness, deterministic `surfacePattern` and per-channel texture GUID override support. Procedural albedo/metallic-roughness/normal RGBA maps are generated on CPU for both runtime and GLB.
- **Massing:** modern regular facade grids, commercial towers, terrace roofs, repeating panels and glazed entrances; four neutral structural silhouette IDs: `residence`, `hall`, `workshop`, `watchtower`.
- **Decoration:** AC units, rooftop service equipment, antennas, lit commercial signs, parapets. Reuse parser-supported `panels`, `corners`, `ribs`, `crown`, `roofBlocks`, `props`, `door`, `window`, `sign` and `structures[].forms`.
- **Variation:** 3 seed-dependent palettes, bounded props/roof modules and geometric dimension constraints, while functional room/door/lift recipes remain independent.

## Agent workflow

Read `skills/world/aiverse-building-style/SKILL.md`, then this style JSON. Change appearance only using strict schema keys. Keep valid referenced materials and neutral structure names. Generate through portable runtime or existing HTML5 Studio `studio.buildingInspect` / `studio.buildingGenerate`; compare geometry and real PBR material output when testing is resumed. Do not hardcode setting IDs in ECS, Renderer3D, PHYSIS, FPS or RTS modules. Document every stage in a distinct handoff, and keep this file registered in `skills/catalog.json`.

QA deferred by user. No native/GLB/GPU captures have been run for this setting.
