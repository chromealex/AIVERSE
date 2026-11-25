# AIVERSE buildings — Stage 23T.0: shared authored PBR surface generation

Date: 2026-11-24
Branch: feature/aiv20261123_building-grammar
Draft PR: https://github.com/chromealex/AIVERSE/pull/114

## Goal and architecture

User explicitly requested twelve new building visual settings, each with architectural grammar, authored PBR materials, actual textures, decorations and registered agent skills. Generator must remain genre-independent. Examined exact current portable BuildingStyle JSON/parser, default engine/assets/building-styles/modern.json + scifi.json, runtime CPU model material binding, GLB writer, existing PNG encoder, and skills/catalog.json. The existing engine style parser uses strict allowed keys and material slots. Imported texture GUIDs remain the authoritative project asset references; new settings should work without inventing GUIDs to non-existent images.

## Implemented Stage 23T.0

1. NEW libraries/include/aiverselib/systems/BuildingSurfacePattern.h: portable CPU RGBA8 PBR surface texture generator, deterministic seeded pattern, 16 surface types (stone, brick, wood, metal, concrete, ceramic, fabric, organic, rust, coral, glass, soil, paint, circuit, ice, basalt). Generates tileable 128x128 albedo modulation, glTF metallic/roughness channels and normal maps with distinct patterns. No filesystem/engine/GPU dependency.
2. libraries/include/aiverselib/systems/BuildingStyle.h: StyleMaterial gains surfacePattern and surfaceSeed, strict whitelist/range parser, preserving backward compatibility of existing style JSON.
3. engine/buildings/BuildingMaterialAssets.h: generated RGBA surface images become real render::Model images; imported GUID references always take precedence per PBR channel, and each image is cached per material/pattern/channel.
4. engine/buildings/BuildingGlbWriter.h: same generated surface RGBA is encoded with the existing proper PNG encoder and embedded in GLB image bufferViews/textures. No fake MIME types or invented asset GUIDs.
5. This separate handoff is created immediately before the first asset content stage.

## Actual validation and limitations

No C++ compilation, glTF parser, runtime GPU test, PNG verification, TypeScript or CTest performed (user explicitly requests features before tests). Texture generator is a 128x128 procedural starting point, not equivalent to scanned AAA material texture quality. External GUID assets override it; the renderer still owns filtering, mips and upload. Pattern textures are not committed binary files; they are real deterministically generated image bytes when building the GLB or runtime Model. Material shading uses existing Renderer3D shader and glTF channels. Emission maps and AO are not fabricated by the surface generator; they retain authored PBR factors or explicit GUID textures. Surface patterns have no gameplay implications.

## Next stage

Create the twelve strict-schema style JSON files in engine/assets/building-styles, each with unique facade grammar, four functional-looking but mechanically neutral structures, materials, palettes, major forms, details/props and surfacePattern PBR image backing; add a separate agent skill for each setting and register all in skills/catalog.json. Add a separate handoff after each batch of settings. Do not edit FPS/RTS modules. Keep PR Draft and tests deferred.
