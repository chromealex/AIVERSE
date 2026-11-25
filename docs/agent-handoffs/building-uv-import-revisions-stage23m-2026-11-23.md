# AIVERSE Building Generator — Stage 23M: UV transforms and material import revisions

Date: 2026-11-23
Repository: chromealex/AIVERSE
Branch: feature/aiv20261123_building-grammar
Draft PR: https://github.com/chromealex/AIVERSE/pull/114

## Objective and inspected existing owners

Continues Stage 23L where HTML5 Studio binds shared library material names to architectural slots and embeds PNG/JPEG texture bytes in GLB files. Examined current-branch `BuildingStyle.h`, `BuildingGlbWriter.h`, `BuildingMaterialAssets.h`, `StudioBuildingGeneration.h`, `engine/render/Model.h`, `StudioAssetCatalog.h`. The existing render::Material already has uvScale, uvOffset, uvRotation, normalScale and occlusionStrength, while Studio sidecar .meta files already own importer settings and stable asset GUIDs. Do not create another material, asset or renderer backend.

## Changes committed

1. `libraries/include/aiverselib/systems/BuildingStyle.h`: StyleMaterial now holds optional uvScale, uvOffset, uvRotation, normalScale and occlusionStrength, with backwards-compatible defaults (scale 1,1, offset 0,0, rotation 0, normalScale 1, occlusionStrength 1). Strict finite/range validation of style JSON fields.
2. `engine/buildings/BuildingMaterialAssets.h`: portable authoredBuildingMaterial now transfers those settings into the actual existing Renderer3D render::Material fields. Old untextured styles preserve defaults.
3. `engine/buildings/BuildingGlbWriter.h`: writes glTF KHR_texture_transform extension for any PBR texture slot when UV transform is nondefault, advertises the extension in extensionsUsed only when needed, and serializes normalTexture.scale and occlusionTexture.strength from StyleMaterial. Geometry UV0 stays as originally generated; per-material texture transform applies to each channel.
4. `apps/studio-native/StudioBuildingGeneration.h`: applying a shared materials.json override now also takes validated uvScale/uvOffset, normalScale, occlusionStrength fields. For every PNG/JPEG source image used in the GLB, the existing guarded AssetCatalog GUID resolution records its bytes, FNV-1a 64-bit content fingerprint, importer and .meta settings in generated recipe JSON `textureDependencies`. Each GUID is recorded once and ordering is deterministic.
5. `skills/world/aiverse-building-style/SKILL.md`: added Stage 23M API, ownership and limitations.

## Source provenance

- `36cb7f18f6491b6a810b48ae7ac66e2b8c79d2ae`: StyleMaterial UV/normal/AO schema.
- `5f2079740f116156ffc0d4877298708f64b692b4`: Renderer3D material conversion.
- `16b9a671a11549121dcf65b06c0c8b6a881e8565`: glTF UV texture transform and PBR channel strengths.
- `0c433c50ab9832d0328103eb4c5869e469479a22`: shared material override UV and channel strengths.
- `554821fdda07a9325a40a417b604cbd29abcf2c0`: asset dependency content fingerprints/settings in recipe.
- `bf42fccdb45db6414ed5d4e472c51e5f11ab4ede`: skill instructions.
- This handoff has its own final documentation commit.

## Verification and limitations

USER DEFERRED TESTS. No Clang/GCC compile, CMake/CTest, web TypeScript typecheck, GLB validator, image decoder, texture import/reimport, Renderer3D Metal/Vulkan/GLES screenshot or live game tick was executed. These are implemented source changes, not confirmed working graphics.

The GLB path still embeds only directly supported PNG/JPEG. KTX2, DDS, TGA, WebP, HDR and EXR source textures must be transcoded by a real existing importer callback or supported glTF extension; no fake MIME conversion was made. The stored textureDependencies manifest makes detecting changed source inputs feasible but no file watcher, asynchronous reimport service or GPU material reupload was registered. RuntimeTexturedBuildingModel remains a CPU-only material+image resolver, not a live Renderer3D insertion. Existing Studio UI material library edits remain project-document changes; the UV scale/offset is supported by native material mapping when supplied in the document, but an explicit visual UV editor control is still outstanding. Some draw paths may use uv textures differently; real renderer verification is required.

## Next step

Stage 23N: connect a real project texture importer to a safe PNG/JPEG/KTX2/DDS etc. transcoding/baking pipeline as appropriate, implement a native dependency checker comparing recipe textureDependencies against current catalog/import settings, and invalidate/rebuild GPU model materials through existing Renderer3D resource generation/fences. Add UV controls in the shared Material Editor using the native studio.materialPatch validation contract (do not add unsupported fields to UI). Wire dual-physical-leaf world session to a real game fixed tick and render owner, then carry out the previously deferred full compiler/CTest/glTF validation and Metal/Vulkan/GLES acceptance. Keep PR #114 Draft until verified and reconciled with divergent main. Continue skill+handoff after every stage.
