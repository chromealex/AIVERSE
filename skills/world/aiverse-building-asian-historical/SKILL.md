---
name: aiverse-building-asian-historical
description: Asian Historical buildings via reusable AIVERSE architectural grammar and generated PBR texture channels.
---

# Asian Historical — architectural setting

Canonical style: `engine/assets/building-styles/asian-historical.json`. Common architecture, PBR and runtime owners: `BuildingStyle.h`, `BuildingSurfacePattern.h`, `BuildingFeatureGenerator.h`, `BuildingMaterialAssets.h`, common ECS/PHYSIS and Renderer3D. No gameplay/genre conditionals.

## Grammar and detailing

- **Materials:** aged timber, ceramic roof tiles, lacquer, stone bases; deterministic per-material `surfacePattern` and `surfaceSeed` generate baseColour, metallicRoughness and normal texture maps (128×128) for runtime and GLB. Real imported Assets texture GUIDs override corresponding generated maps.
- **Shapes:** stacked pagoda roofs, courtyard annexes, tiered tower silhouettes, repeating timber lattice panels. Neutral structure roles `residence`, `hall`, `workshop`, `watchtower`; each has authored massing, forms, modules, tower, roof and door parameters. Use no RTS/FPS roles.
- **Decor:** wide eaves, tile bands, lantern masts, layered crowns, courtyard braces. All uses supported style grammar fields: `panels`, `base`, `crown`, `ribs`, `pipes`, `corners`, `buttresses`, `roofBlocks`, `props`, `window`, `door`, `structures[].forms`.
- **Seeded variants:** three named material palettes. Preserve the generated structure graph for actual rooms, stairs, doors, lifts and PHYSIS; visual ornaments never become implied gameplay permission.

## Authoring procedure

Read the parent `skills/world/aiverse-building-style/SKILL.md` before edits. Use `readBuildingStyle` strict JSON grammar for authored shape and PBR fields; never write nonexistent texture GUIDs, filename-only fake textures or genre-specific systems. Save a handoff after each stage. Preview with native `studio.buildingInspect` and generate actual GLB via `studio.buildingGenerate` when Studio is available. Verify the CPU-generated PBR maps and the renderer output when deferred tests are enabled.

**Unverified:** real strict parser, GLB decoding, material shading and platform GPU acceptance are deferred per user request.
