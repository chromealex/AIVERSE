# AIVERSE buildings — Stage 23T.3: Post-Apocalypse, Modern / Contemporary, Ancient Civilizations

Date: 2026-11-24
Branch: feature/aiv20261123_building-grammar
PR: https://github.com/chromealex/AIVERSE/pull/114 (Draft)

## What changed
- Post-Apocalypse: new engine/assets/building-styles/post-apocalypse.json: rust, reclaimed metal, concrete, salvage timber; asymmetrical stepped shelters, ruined gantries, water tanks, exhaust stacks and patchwork plates; three palettes, four neutral forms and deterministic generated PBR textures.
- Modern: UPDATED existing engine/assets/building-styles/modern.json (do not rename the ID). Keeps compatibility with existing `modern` lookups but adds 19 named material slots with procedural PBR surface patterns; four neutral structure presets (residence/hall/workshop/watchtower), updated glazing, concrete/metal/ceramic facade grammar, rooftop machinery and commercial signage. Existing project explicit asset GUID overrides still win.
- Ancient Civilizations: new engine/assets/building-styles/ancient-civilizations.json: sandstone/limestone/basalt/terracotta appearance, stepped temple/bastion and tower silhouettes, colonnade-like pylons, arches, monumental pillars, cornices and temple roof modules.
- Registered per-setting skills in skills/world/aiverse-building-{post-apocalypse,modern,ancient-civilizations}/SKILL.md and skills/catalog.json. All three are appearance-only and use common ECS/PHYSIS/NavMesh and Renderer3D without genre branches.
- Corrected generatedTexture C++ glTF JSON escaping in engine/buildings/BuildingGlbWriter.h (commit 045f4b246ab68c611af3f07e46620505d868d579) after source inspection, without running compilation.

## Source provenance
Styles: 2aac885f6cd4e5497f577a4ec941e2b4109e9f9e, 186802d0ff3f88b1be16afd35b3980caa562982e, 6b005f6c2f01daa090dc6f7a52cc39ca97a86919.
Individual skills and catalog were published in this branch before this handoff (verify Git history for exact SHA).
No other genre system modified.

## Actual tests and limitations
Tests intentionally postponed: no parser execution, glTF parser, thumbnail capture, image visual sampling, CMake, TypeScript or GPU QA. Material generator emits genuine deterministic RGBA8 images (128x128) only when invoked by runtime or GLB path; do not call these files imported high-resolution artwork. The style JSONs are authored schema-based source and their actual rendered quality remains unverified. Modern keeps its original asset ID but is visually expanded.

## Next
Stage 23T.4: Asian Historical, Solarpunk and Underwater (last three of twelve), each strict-schema visual grammar + real procedural PBR patterns + structure styles + separate skill/catalog entries. Add separate stage handoff. Then update master style skill/setting catalog, check Stage 23T Studio selection naming and record deferred test limitations.
