# AIVERSE buildings — Stage 23T.3: Post-Apocalypse, Modern, Ancient Civilizations

Date: 2026-11-24
Branch: feature/aiv20261123_building-grammar
Draft PR: https://github.com/chromealex/AIVERSE/pull/114

## Source architecture and implementation

Built on the parser-compatible architecture style assets inspected in Stage 23T.0. No mechanics or genre modules modified.

- `engine/assets/building-styles/post-apocalypse.json`: new salvage settlement style, rusty metal/scrap paint/concrete/wood material textures, irregular terraced shelters, repurposed industrial masses, patched panels, storage tanks/industrial stacks and degraded roofing.
- `engine/assets/building-styles/modern.json`: UPDATED existing style IN PLACE, retains historic ID `modern` and original facade compatibility. Adds neutral `residence/hall/workshop/watchtower` structures, 19 PBR-authored material slots with deterministic surface patterns and three seeded palettes, commercial/office silhouettes and contemporary roof/service/trim details. Original modern lookup remains intact.
- `engine/assets/building-styles/ancient-civilizations.json`: new monumental masonry style with limestone/sandstone, terracotta, basalt, colonnade-like pylons/arches/temple massing, broad heavy corners, platform silhouettes and limited luminous props.
- Each style has distinct materials (authored factors and generated albedo/MR/normal maps from portable BuildingSurfacePattern), structure massing/form variants, seeded palettes, walls/windows/doors and decorative props. Mechanics still come from a shared functional recipe irrespective of style.
- Three individual skills added at `skills/world/aiverse-building-{post-apocalypse,modern,ancient-civilizations}/SKILL.md` and registered in the existing `skills/catalog.json`.

## Provenance

2aac885f6cd4e5497f577a4ec941e2b4109e9f9e post-apocalypse
186802d0ff3f88b1be16afd35b3980caa562982e modern (original id preserved)
6b005f6c2f01daa090dc6f7a52cc39ca97a86919 ancient
71e55ceb4d4eeac1a5075b3d95178e4f730b7d95 post-apocalypse skill
4567a9966f5726ffb06cc052896ed1d0730729f2 modern skill
19a647124371b39bb02cfe842be3a972d9778c4b ancient skill
6b1032440e9bbdc12fbd13872787236ac5308e8c catalog
This file is the mandatory separate Stage 23T.3 handoff commit.

## QA, limitations, next step

No C++/JSON strict parser execution, GLB validation, snapshot, native Studio render, GPU capture or CTest performed per user's feature-first policy. Textures are real deterministic CPU PBR maps when generated, not binary packed image assets and not source-authored 4K scans. Production lighting/perceived style quality remains unverified. Do not add genre-specific mechanics or reinterpret material factors as collision.

Next Stage 23T.4: Asian Historical, Solarpunk and Underwater. Create strict-schema assets, per-setting skills and catalog entries, then handoff. After all twelve settings, update general building skill and Studio catalog descriptions to show the explicit 12 setting groups without erroneous parser assumptions; check schema contracts by code inspection only, with execution tests deferred.
