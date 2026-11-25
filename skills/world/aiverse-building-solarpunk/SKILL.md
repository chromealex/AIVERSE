---
name: aiverse-building-solarpunk
description: Solarpunk buildings via reusable AIVERSE architectural grammar and generated PBR texture channels.
---

# Solarpunk — architectural setting

Canonical style: `engine/assets/building-styles/solarpunk.json`. Common architecture, PBR and runtime owners: `BuildingStyle.h`, `BuildingSurfacePattern.h`, `BuildingFeatureGenerator.h`, `BuildingMaterialAssets.h`, common ECS/PHYSIS and Renderer3D. No gameplay/genre conditionals.

## Grammar and detailing

- **Materials:** light eco concrete, vegetation-like organic composite, timber, glass; deterministic per-material `surfacePattern` and `surfaceSeed` generate baseColour, metallicRoughness and normal texture maps (128×128) for runtime and GLB. Real imported Assets texture GUIDs override corresponding generated maps.
- **Shapes:** terraced green roofs, organic greenhouse domes, energy canopies, soft rounded building massing. Neutral structure roles `residence`, `hall`, `workshop`, `watchtower`; each has authored massing, forms, modules, tower, roof and door parameters. Use no RTS/FPS roles.
- **Decor:** greenhouse structures, water tanks, solar collector dishes, vegetated-looking cladding, soft lighting. All uses supported style grammar fields: `panels`, `base`, `crown`, `ribs`, `pipes`, `corners`, `buttresses`, `roofBlocks`, `props`, `window`, `door`, `structures[].forms`.
- **Seeded variants:** three named material palettes. Preserve the generated structure graph for actual rooms, stairs, doors, lifts and PHYSIS; visual ornaments never become implied gameplay permission.

## Authoring procedure

Read the parent `skills/world/aiverse-building-style/SKILL.md` before edits. Use `readBuildingStyle` strict JSON grammar for authored shape and PBR fields; never write nonexistent texture GUIDs, filename-only fake textures or genre-specific systems. Save a handoff after each stage. Preview with native `studio.buildingInspect` and generate actual GLB via `studio.buildingGenerate` when Studio is available. Verify the CPU-generated PBR maps and the renderer output when deferred tests are enabled.

**Unverified:** real strict parser, GLB decoding, material shading and platform GPU acceptance are deferred per user request.
