# AIVERSE — procedural building architecture settings

**Branch:** feature/aiv20261123_building-grammar
**Scope:** appearance data and portable PBR textures; functionality is independent of visual setting.

All settings are authored JSON files under `engine/assets/building-styles/`.
They use the **same** `readBuildingStyle`, `dressStructure`,
`dressBuilding`, `generateFunctionalBuilding`,
`RuntimeBuildingGenerator`, shared ECS/PHYSIS, graph routing and
Renderer3D model/image owners. A style never adds a new building gameplay
type or its own collision/navigation solver. Four structural silhouettes
(`residence`, `hall`, `workshop`, `watchtower`) are aesthetic
presets, not mandatory gameplay roles.

## Twelve settings

| Setting | Style ID | Material language | Architectural silhouettes | Decorative grammar |
|---|---|---|---|---|
| Medieval Fantasy | `medieval-fantasy` | stone, timber, brick | bastions, arches, courtyard halls | battlements, timber panels, stone braces |
| Dark Fantasy | `dark-fantasy` | basalt, stone, oxidation | gothic spires, citadels | thick buttresses, dark ribs, lanterns |
| Steampunk | `steampunk` | copper/metal, brick, rust | factories, silos, gantries | chimneys, tanks, pipes |
| Cyberpunk | `cyberpunk` | concrete, metal, circuit patterns | tall stacked spires | neon signage, antennas, lit trims |
| Sci-Fi / Space | `scifi-space` | painted alloy, ceramic, circuit surfaces | modular bases, station towers, domes | dishes, pressure panels, tanks |
| Alien Civilization | `alien-civilization` | organic, basalt, coral | monoliths, swept hulls, domes | biomorphic ribs, glowing windows |
| Post-Apocalypse | `post-apocalypse` | rust, salvage, painted metal | improvised terraces and rebuilt bays | patchwork cladding, storage tanks |
| Modern / Contemporary | `modern` | concrete, glass, aluminium | office towers, residences, workshops | window grids, parapets, signs |
| Ancient Civilizations | `ancient-civilizations` | limestone, basalt, ceramic | monumental halls, temple arches, pylons | colonnade-inspired ribs, heavy cornices |
| Asian Historical | `asian-historical` | timber, glazed tile, lacquer | pagodas, halls, layered roofs | curved-looking stacked eaves, masts |
| Solarpunk | `solarpunk` | eco concrete, wood, organic surface | green terraces, greenhouse domes | collector dishes, cladding, tanks |
| Underwater | `underwater` | pressure alloy, ceramic, coral | reinforced modules and domes | pressure windows, sonar, blue lights |

The original `scifi` style is preserved as an additional legacy
asset for existing projects. The original `modern` **ID** and
facade settings are retained and extended with structural silhouettes
and generated PBR surfaces.

## Shared architectural grammar

A style chooses all visual characteristics through the strict
`BuildingStyle.h` parser, including:

- **Massing:** bastion, spire, twin-bay and terraced. Individual
  structures combine the generic `pod`, `silo`, `pylon`, `gantry`,
  `arch`, `dome` and `swept-hull` forms.
- **Facade and envelope:** panels, accents, bands, corners, ribs,
  buttresses, crown, parapets, roof blocks and cladding depth.
- **Openings:** windows, lit glass, frames, door canopy and hazard
  lights. Functional aperture locations/collision must come from the
  shared structure graph and its actual physical owner.
- **Detail props:** generic `box`, `mast`, `tank`, `stack`,
  `dish` with generated counts, size ranges, colors and light tips.
- **Variation:** three authored palettes per setting, plus the
  standard deterministic seed/placement selection.
- **Material maps:** named linear PBR base colour factors,
  metallic, roughness, emission, UV scale/offset/rotation,
  normal and occlusion strength. Project-assigned texture GUIDs
  override surface texture generation.

## Real generated PBR textures

`libraries/include/aiverselib/systems/BuildingSurfacePattern.h`
implements 16 generic procedural surface types: `stone`, `brick`,
`wood`, `metal`, `concrete`, `ceramic`, `fabric`,
`organic`, `rust`, `coral`, `glass`, `soil`,
`paint`, `circuit`, `ice`, `basalt`.

Use `materials.<slot>.surfacePattern` with an optional
`surfaceSeed`. The shared deterministic CPU texture generator
produces three **actual RGBA8 128×128 maps** per authored material:
neutral sRGB albedo modulation, packed metallic/roughness (G roughness,
B metalness), and tangent-space normal. These are built into
`render::Model::images` for Renderer3D and are encoded into proper
PNG bytes for the glTF/GLB exporter with its existing PNG encoder.
The style's linear base colour, metallic and roughness factors
remain authoritative and multiply those texture channels.

For higher-resolution authored textures, assign the existing
per-channel imported Assets GUIDs in `materials.<slot>.textures`.
The shared asset resolver and import pipeline will use real existing
content; neither JSON nor the engine creates fictitious GUIDs.
An imported channel takes precedence over the procedural channel,
allowing gradual material upgrades. `normalScale` and UV transforms
work through existing Renderer3D/glTF PBR metadata. Optional AO and
emissive image maps require actual imported GUIDs; no fake AO/emission
maps are synthesized.

Example:

```json
{
  "hull": {
    "color": [0.42, 0.39, 0.31],
    "metallic": 0.06,
    "roughness": 0.82,
    "surfacePattern": "stone",
    "surfaceSeed": 188,
    "uvScale": [2, 2]
  }
}
```

## Editor and agents

In the existing HTML5 Studio Building Generator, choose one of the
twelve explicitly named settings, select an architectural style,
choose a neutral structure silhouette or functional recipe, and
assign shared project materials if desired. Other GUI panels continue
to use the same Assets / GUID and PBR editing pipeline.

All twelve setting skills are registered in
`skills/catalog.json` under
`skills/world/aiverse-building-<setting-id>/SKILL.md`.
The shared parent is
`skills/world/aiverse-building-style/SKILL.md`.
Each setting skill names its visual grammar, material family, usable
decorations, neutral structures, and the common functional API.
All agents should read the shared parent first, preserve the strict
style schema, and write one handoff after every feature stage.

## Scope and verification

These are authored source assets and CPU material-generation code.
The user explicitly deferred testing: no C++ compiler, strict parser
execution for all 12, GLB image validation, native Studio build or
Metal/Vulkan/GLES visual comparison has been run for this batch.
Do not publish screenshots or a completed status in the devblog
without real engine evidence.

Potential post-test improvements: high-resolution scanned artist
materials, special window/vegetation shaders, procedural texture
mipmap authoring, visual blending between settings, and new generic
form primitives. None of those is claimed as implemented here.
