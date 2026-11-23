# AIVERSE buildings — Stage 23T.5: complete 12-setting catalog and Studio selection

Date: 2026-11-24
Repository: chromealex/AIVERSE
Branch: feature/aiv20261123_building-grammar
Draft PR: https://github.com/chromealex/AIVERSE/pull/114

## User request / scope

Implement the requested 12 building generator visual settings, not just propose them. Each must have architectural grammar, authored PBR material factors, real textures, details/decorations and its own registered agent skill. Preserve neutral ECS/PHYSIS/Renderer3D functional mechanics. Continue on same branch, make a handoff after each stage. User deferred executable tests.

The 12 authored setting IDs:
1. medieval-fantasy
2. dark-fantasy
3. steampunk
4. cyberpunk
5. scifi-space
6. alien-civilization
7. post-apocalypse
8. modern (original ID preserved)
9. ancient-civilizations
10. asian-historical
11. solarpunk
12. underwater

## Stage structure and completed artifacts

- Stage 23T.0: `libraries/include/aiverselib/systems/BuildingSurfacePattern.h` portable deterministic PBR surface maps, strict `StyleMaterial.surfacePattern/surfaceSeed` parser in BuildingStyle.h, and both native `BuildingMaterialAssets.h` and GLB `BuildingGlbWriter.h` bindings to actual generated 128x128 albedo/MR/normal images. Imported real GUID textures always take priority. Handoff: `building-settings-core-stage23t0-2026-11-24.md`.
- Stage 23T.1: Medieval Fantasy / Dark Fantasy / Steampunk, three dedicated strict-schema style JSONs, three matching skills, 3 catalog entries. Handoff `building-settings-historic-stage23t1-2026-11-24.md`.
- Stage 23T.2: Cyberpunk / Sci-Fi Space / Alien Civilization, style JSONs/skills/catalog. Handoff `building-settings-future-stage23t2-2026-11-24.md`.
- Stage 23T.3: Post-Apocalypse / Modern / Ancient Civilizations. Existing modern.json enhanced in place; ID modern stays unchanged. Handoff `building-settings-civil-stage23t3-2026-11-24.md`.
- Stage 23T.4: Asian Historical / Solarpunk / Underwater. Style JSONs/skills/catalog. Handoff `building-settings-nature-stage23t4-2026-11-24.md`.
- Stage 23T.5: `apps/studio-native/StudioBuildingGeneration.h` returns full stable setting names for these 12, instead of conflating similarly prefixed names. Legacy scifi/modern grouping still works. `apps/studio-web/src/panels/generated/BuildingGeneratorPanel.tsx` exposes human-readable label for each new setting and explicitly explains PBR textures vs common functional generation. Parent skill `skills/world/aiverse-building-style/SKILL.md` now documents all 12, engine-agnostic PBR map generation, interaction with GUID overrides, mandatory agent workflow and handoffs. `docs/building-settings.md` is the complete reference table and authoring API. `skills/catalog.json` now has **12 separate setting skill entries**, each under `skills/world/aiverse-building-<setting-id>/SKILL.md` plus parent `aiverse-building-style`.

## Final stage source commits

- 189bab11e42901451f18062663df27a2455af052 — native Studio full setting-group IDs.
- 7ec2f131663549412b9d4181d3b6fe2711d152b7 — HTML5 Studio labels and updated runtime explanation.
- da098dbe3c03c59baacc7da75bc69cf50a8d1553 — comprehensive `docs/building-settings.md`.
- 84f8e416670b3881e1db09cfee901c49c27894fa — parent building skill instructions.
- 52e7b8fb04d3426af2f0e13e190338177dbfa7e3 — catalog parent summary + 12 child registrations.
- This handoff is written and committed as a distinct last stage artifact.

## Engineering contract

Only the data in `engine/assets/building-styles/*.json` defines visual setting. No setting logic belongs in the universal fixed-tick ECS/Simulation, PHYSIS colliders, Renderer3D scene owners, navigation graphs or legacy FPS/RTS modules. All selected styles use the same `readBuildingStyle`, `dressBuilding`, `dressStructure` and `generateFunctionalBuilding` APIs with four neutral aesthetic silhouettes (`residence`, `hall`, `workshop`, `watchtower`).

PBR texture pipeline generates 16 portable pattern classes at 128×128 RGBA8 resolution, three channels per material (albedo modulation, packed metallic/roughness, tangent-space normal) with seeded deterministic detail; converts to actual runtime `render::Image` resources and true PNG images embedded in GLB via the previously implemented PNG encoder. Style numerical metallic/roughness and linear baseColor factors still apply. If an existing project has a real imported texture GUID, it overrides the corresponding generated channel, preventing fake assets or accidental loss of authored materials. AO/emission images still require explicit imported texture GUID; only their scalar/factor channels are automatically handled.

## Actual tests / current limitations

**No executable QA**: user explicitly postponed C++ compiler/CMake, CTest, exact strict JSON parser test across all styles, GLB image validator, Studio web TypeScript and real GPU Metal/Vulkan/GLES screenshots. Only GitHub source changes and catalog entry presence have been confirmed; actual gameplay and rendered appearance are not established. Pattern images are modest-size procedurally synthesized textures, NOT artist-quality scanned 4K PBR assets. No UI changes for real-time 3D model preview were included in this *settings* request. Stage 23T older editor recipe-opening and semantic socket authoring work remains separate; this stage implemented the specific 12 visual settings request.

AGENTS.md also requires real verified engine screenshots before successful devlog entries are published. No shots were made: do not fabricate media or mark website roadmap feature done; final site publishing remains the integrator's job after valid tests and merge.

## Next concrete stage

User-deferred Stage 23U: run a full real parser and model generation pass for ALL twelve style JSONs (both facade and four structure modes), C++/web builds, glTF binary checks for generated PBR PNG channels, Renderer3D GPU material and lighting acceptance, platform Metal/Vulkan/GLES tests, and fix genuine failures. Assess texture fidelity and author upgraded Assets GUID material packs where necessary. Complete optional live Studio preview and recipe editing separately if requested. Keep PR #114 Draft until reviewed, tested and reconciled against current main. Continue making handoff documentation for every further stage.
