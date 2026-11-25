---
name: aiverse-building-cyberpunk
description: Design Cyberpunk architecture in portable AIVERSE style JSON with authored grammar and generated PBR maps.
---

# Cyberpunk setting

**Asset:** `engine/assets/building-styles/cyberpunk.json`.
**Owner:** `aiverse::lib::systems::BuildingStyle`, `BuildingStructure`, `BuildingSurfacePattern`, shared world ECS/PHYSIS and Renderer3D. No FPS, RTS or game genre dependency.

## Appearance grammar

- Material family: concrete, anodized alloy, circuit overlays, neon glass; every material stores linear colour factors and a deterministic PBR `surfacePattern`. The portable generator creates 128×128 RGBA albedo, metallic/roughness and normal data; an actual GUID image from project Assets supersedes that pattern per slot.
- Massing: vertical spires, dense tall modules, slatted window blocks, edge-lit roofs, signage and gantry bridges. Four stable neutral structure IDs are `residence`, `hall`, `workshop`, `watchtower`.
- Decorations: luminous signs, crowded mast clusters, facade light strips, antennas, industrial vents. Use `panels`, `crown`, `ribs`, `roofBlocks`, `pipes`, `window`, `door`, `props` and `structures[].forms` only within the established strict style schema. Palette variants are seeded.
- Floor and aperture mechanics are separate functional recipes. Do not fake collision or use decorative panels as NavMesh authoring authority.

## Agent steps

1. Read `skills/world/aiverse-building-style/SKILL.md`, then this setting JSON. Do not add hardcoded setting checks to ECS, Renderer3D, PHYSIS, or game modules.
2. Make changes to the style's PBR and architectural grammar only. Maintain valid material identifiers, structural references, bounded prop counts, and preserved visual variation by seed.
3. Preview three geometric variants through `studio.buildingInspect` and export via the shared `studio.buildingGenerate` when native Studio is available. Actual images come from the shared pattern generator or existing imported GUID textures.
4. Update `skills/catalog.json` when adding a new skill; create an independent agent handoff for each stage. Never assert passing tests unless real outputs were verified.

Deferred: strict C++ parser checks, glTF PNG validation, Renderer3D GPU screenshots and style visual acceptance.
