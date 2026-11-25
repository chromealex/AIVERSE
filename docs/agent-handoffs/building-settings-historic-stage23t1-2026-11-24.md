# AIVERSE buildings — Stage 23T.1: Medieval Fantasy, Dark Fantasy, Steampunk

Date: 2026-11-24
Branch: feature/aiv20261123_building-grammar
Draft PR: https://github.com/chromealex/AIVERSE/pull/114

## Source inspection and architecture

Used the real modern.json and scifi.json as parser-compatible grammar references, current BuildingStyle.h strict schema, shared surface system from Stage 23T.0, and the existing skills/catalog.json. This is data-only appearance and not a genre mechanic; structures have neutral role IDs (residence, hall, workshop, watchtower), while runtime rooms/doors/PHYSIS/navmesh remain shared systems.

## Implemented authored content

- Medieval Fantasy: engine/assets/building-styles/medieval-fantasy.json, limestone/wood/brick PBR materials, 3 seedable palettes, bastion+spire+terrace silhouette combinations, arch/pylon/gantry/dome structure forms, buttresses, masonry corners, parapets, timber paneling, lanterns and roofs.
- Dark Fantasy: engine/assets/building-styles/dark-fantasy.json, basalt/stone/rust patterns and gloomy colour palettes, taller roof/crown and spire silhouettes, narrow frames, heavy buttresses, arches and dark beacons.
- Steampunk: engine/assets/building-styles/steampunk.json, brick/riveted metal/rust surface maps, metal PBR factors, twin-bay factories and silos/gantries/pylons, pipes with brackets, mechanical tank/stack roof props, industrial window/wall detailing.
- All three reuse supported BuildingStyle appearance keys, have 19 authored named material slots with actual generated PBR albedo/metal-rough/normal maps (external project GUIDs take precedence), three deterministic palettes, four neutral structural silhouette variants, and facades/door/window props that also apply to functional building recipes.
- New individual skills: skills/world/aiverse-building-{medieval-fantasy,dark-fantasy,steampunk}/SKILL.md, with architectural/material guidelines, API, ownership, deferred verification note.
- Registered all three skills in the existing skills/catalog.json (correct id/path/dependencies, target AIVERSE::lib_building_style).

## Commit provenance

- ffca755e74c885915411d2a54863849b75946fa2 — medieval style.
- 5a2b2052472d7f623f56157543d2458ca406cba1 — dark fantasy style.
- b2886007ad979b628923bc7468c83f4ac5ae3abf — steampunk style.
- f69b5f88c31e584489ba17ed436d9493ad93f351 — medieval skill.
- 650b356a261dec21457016d4210661a68e54bfc6 — dark fantasy skill.
- 7b99cd06c5b4e9da1b46a9df9ded9c376cf22567 — steampunk skill.
- d5ceef301a89f0ca77763eb6baab38097ec3664d — skill catalog update.
- This is the mandatory separate handoff for Stage 23T.1.

## Actual tests / boundaries

NONE, by user request. No C++ compile, JSON strict parser execution, GLB validator, image inspection, style geometry or renderer captures. Claims of visual quality and compiled readiness are not verified. Generated PBR maps are 128x128 CPU procedural textures, not scanned detail material assets. Do not claim real texture image assets or screenshot evidence in devlog. The catalogue's human-readable entry should be updated after real test/capture when user lifts deferral.

## Next step

Stage 23T.2: Cyberpunk, Sci-Fi / Space, Alien Civilization, plus three per-setting skills and new catalog entries, separate handoff. Preserve genre independence, external texture GUID authority, four structural presets and actual PBR surfacePattern backing.
