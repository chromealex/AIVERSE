# AIVERSE buildings — Stage 23T.2: Cyberpunk, Sci-Fi / Space and Alien Civilization

Date: 2026-11-24
Branch: feature/aiv20261123_building-grammar
PR: https://github.com/chromealex/AIVERSE/pull/114 (Draft)

## Implemented
- `engine/assets/building-styles/cyberpunk.json`: neon skyscraper architecture, spires and stacked bays, concrete/metals/circuit procedural PBR, emissive glass/signage, pylons/gantries/roof tech and dense antenna props.
- `engine/assets/building-styles/scifi-space.json`: modular sealed space colony structures, painted metal/ceramic/circuit textures, chamfered station bases, observation domes, factory gantries, space equipment with dishes/tanks and blue illuminated windows.
- `engine/assets/building-styles/alien-civilization.json`: alien bioarchitecture with basalt, coral and organic surface textures, swept hull and dome features, bio-luminous trims, monolith silhouettes, sculptural ribs and asymmetric spires.
- Each strict-schema data style contains 19 authored PBR materials including 128×128 deterministic albedo, metallic/roughness and normal maps via the shared Stage 23T.0 surface generator, 3 seeded palettes, 4 neutral structure silhouettes (residence/hall/workshop/watchtower), functional facade grammar and decorative props.
- Per-setting skills added in `skills/world/aiverse-building-{cyberpunk,scifi-space,alien-civilization}/SKILL.md`. All three registered in `skills/catalog.json`, preserving the shared `AIVERSE::lib_building_style` target, building-style dependency, and neutral ECS/Renderer3D ownership.

## Commits
- a5f2c4769925710e1da9aa4e8e417065cba04840 cyberpunk
- e4c9f08f7f6baca84eefd11c1d6cf15821150a41 Sci-Fi / Space
- 75454e0332cbea91be02861e4c3bdf4a743e112e alien
- 336de2c312b7d0b51e2052c635925f72faf8ba3d cyberpunk skill
- 618fcd9e4643c6a1ced53dd4fe18ea0ff49617df space skill
- f7a3da3de0d702085999ee4594d6c9334ab1a9be alien skill
- fac84e177bd7a042c24209628c544c2621e19297 catalog
- This file is the separate Stage 23T.2 handoff.

## Validation and limitations
No native strict-style parser execution, C++ compilation, GLB image parse, GPU, lighting screenshot or CTest; user deferred tests. Architectures and texture outputs are authored source, not verified renders. No new gameplay logic or genre-specific modules. A PBR `surfacePattern` is an actual CPU-generated image map when the shared producer runs, NOT a packaged GUID texture or scanned high-resolution asset. Authored project texture GUIDs always override pattern channels.

## Next
Stage 23T.3: Post-Apocalypse, Modern, Ancient Civilizations; ensure Modern retains original `modern` ID and backwards-compatible lookup while gaining procedural PBR + neutral structural presets. Update individual skills and catalog and create handoff immediately.
