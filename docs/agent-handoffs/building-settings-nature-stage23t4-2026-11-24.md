# AIVERSE buildings — Stage 23T.4: Asian Historical, Solarpunk, Underwater

Date: 2026-11-24
Branch: feature/aiv20261123_building-grammar
PR #114: https://github.com/chromealex/AIVERSE/pull/114 (Draft)

## Implemented

- `engine/assets/building-styles/asian-historical.json`: timber, ceramic tile and lacquer PBR, terraced pagoda crowns, timber panels, red-gold palette variants, ceremonial entrances, layered eaves, masts/lantern details.
- `engine/assets/building-styles/solarpunk.json`: eco-concrete, organic wood/plant-like cladding, glazed light components, greenhouse dome and swept-hull structures, terraced roof silhouettes, water tanks and collector-dish props, plant-toned palette variants.
- `engine/assets/building-styles/underwater.json`: pressure-resistant metallic and ceramic material patterns, coral accents, sealed dome and swept hull forms, reinforced heavy wall ribs, pressure windows, blue-green emissive lights and sonar/tank/dish roof details.
- For each, the authored strict-schema style includes 19 named PBR material slots with deterministic real CPU-generated albedo/MR/normal image maps (portable shared pattern generator), 3 seedable palettes, facade/door/window/roof grammar and four neutral silhouettes (residence, hall, workshop, watchtower). User-owned Assets GUID textures supersede procedural maps where provided; no fake external texture file IDs.
- Skills: `skills/world/aiverse-building-{asian-historical,solarpunk,underwater}/SKILL.md`; all three registered in `skills/catalog.json` with AIVERSE::lib_building_style target and parent building-style dependency.

## Source commits
- 1e1dc90ac06d5840526edadcd590d850d107a6c0 Asian Historical.
- ff6ab202102c0f1dc7dda553479bb1fe1642d91d Solarpunk.
- 541f946fd2c460d19a5618b355779ce1d92a86e6 Underwater.
- 22ff55bf85b983524707cc284f1734bf2163eed5 Asian skill.
- 0441655f96efc95bb50aadcde700ddcb711ec3ac Solarpunk skill.
- 64af781a6dcafaa10a08860ce4cba95674943dd8 Underwater skill.
- 9281a6212492fd448c6f387df4a2a5f4c097a1fb catalog registration.
- This file is the separate Stage 23T.4 handoff commit.

## Actual tests and limitations
No C++ build, parser fixture, GLB validation, generated texture PNG inspection, visual screenshot or native Studio test was run; user explicitly deferred testing. There is no physical water pressure or vegetation simulation coded into these styles; those remain common environment/PHYSIS systems. 'Underwater' styling is not a new fluid solver, and 'Solarpunk' styling is not an ecology solver. The PBR surfaces are deterministic 128×128 procedural textures, not artist-authored high-resolution assets or actual screenshot evidence.

## Next stage
Stage 23T.5: finish parent building-style skill, Studio human-readable setting grouping/labels and catalogue documentation for 12 named settings, with metadata in style selection. Audit 12 JSON + skill paths without executing deferred tests. Record an individual stage handoff. If image quality needs production scans, reuse existing GUID material channels, do not invent paths. Then Stage 23U when user authorizes tests: native parser/generator/CMake, real GLB PBR textures and cross-platform GPU validation and fix all findings. Do NOT merge PR before tested.
