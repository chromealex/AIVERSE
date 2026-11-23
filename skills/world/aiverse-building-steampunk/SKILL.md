---
name: aiverse-building-steampunk
description: Author Steampunk buildings using data-only architectural grammar, deterministic PBR surfaces and common ECS runtime.
---

# Steampunk building setting

Canonical style asset: `engine/assets/building-styles/steampunk.json`.
Shared parser and geometry: `libraries/include/aiverselib/systems/BuildingStyle.h`, `BuildingStructure.h`, `BuildingFeatureGenerator.h`.
Shared surface maps: `BuildingSurfacePattern.h`. Existing asset GUIDs override procedural surfaces per PBR channel. All shapes, materials and assets are **visual**; commands, physics, rooms, NavMesh, doors and lift semantics remain the common building systems.

## Architectural grammar

- Material family: riveted metal, red brick, copper patina. Each authored material supplies linear PBR colour, metallic, roughness, `surfacePattern` and deterministic seed; 128×128 generated albedo, metallic/roughness and normal maps are included both in GLB and native Renderer3D model paths.
- Massing: industrial twin-bay massing, steam gantries, silos, bolted plates, banded factory roofs and pipe corridors.
- Structural silhouettes: residence, hall, workshop, watchtower. The four structure IDs are neutral silhouettes, not hardcoded RTS or FPS classes; do not invent a new genre-specific structure type.
- Details and decorations: boilers, tanks, chimneys, radial masts, pipe brackets and lit fittings. Use the existing `panels`, `band`, `corners`, `crown`, `ribs`, `buttresses`, `roofBlocks`, `pipes`, `window`, `door`, `props` and `structures[].forms` rules. Only use supported forms/patterns from the portable parser.
- Preserve seeded variants in `palettes`, wall openings and entrance anchors. Keep navigable door gaps and real static/collision surfaces consistent with generated structure graph.

## Agent workflow

1. Read `skills/world/aiverse-building-style/SKILL.md` and this style JSON. Modify architecture via valid schema, not engine-specific switches.
2. Create or adapt neutral building recipe; style may dress different rooms/floors and uses common `generateFunctionalBuilding`. Preview via existing Studio building panel and `studio.buildingInspect`; generate GLB via `studio.buildingGenerate`.
3. Keep project GUID image textures authoritative when supplied; generated surface patterns supply real fallback PBR images. Never write fake texture GUIDs or reference absent files. Export uses glTF 2.0 PBR channels.
4. If altering this setting, update its material keys, props, palettes and all structure references together. Update the catalog and write a separate handoff after every stage.

**Testing:** Native parser, real GLB texture bytes and GPU output have not been verified for this stage; do not claim live results until the deferred QA phase.
