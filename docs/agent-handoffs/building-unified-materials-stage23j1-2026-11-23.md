# AIVERSE building generator — Stage 23J.1: unified PBR materials + textures
Date: 2026-11-23
Repo: chromealex/AIVERSE
Branch: feature/aiv20261123_building-grammar
PR: https://github.com/chromealex/AIVERSE/pull/114 (Draft)

## User change
User explicitly clarified that materials and textures must be handled together. Stage 23J previously offered a separately specified render::Material and texture GUID list. Stage 23J.1 adds one authored material resource for both, applied across static shell and moving apertures.

## Files/API
- NEW engine/buildings/BuildingMaterialAssets.h: authoredBuildingMaterial(StyleMaterial) translates scalar colour/metal/rough/emissive factors; bindBuildingStyleMaterials(Model&, authoredMaterials, resolveImageByGuid, error) constructs and validates all material factors + texture images in a single staged operation, shares duplicate GUID images across materials, and installs model-local texture indices only after success.
- runtimeTexturedBuildingModel(StyledStructure, resolveImageByGuid, name, error) constructs a CPU Model for the entire generated shell, partitions, trim and roof and binds its unified materials+textures.
- BuildingAperturePresentation::bindStyleMaterial(socket, StyleMaterial, resolver) assigns an entire authored PBR leaf material and its textures at once.
- BuildingApertureGameBridge::bindStyleMaterial(socket, style, materialIndex, resolver) exposes the same single step to game resource owners.
- BuildingRuntime.h exports the shared binding.
- skills/world/aiverse-building-style/SKILL.md: Stage 23J.1 workflow and ownership.

## Data and resource ownership
BuildingStyle materials.<id> holds scalar PBR factors and the optional textures GUID group (baseColor, metallicRoughness, normal, occlusion, emissive) parsed by Stage 23J. The existing StudioAssetCatalog resolves .meta GUIDs via byGuid; a caller-provided existing image importer performs decoding and enforces sRGB/linear channels, compression/mipmaps and secure asset paths. Renderer3D still owns texture GPU uploads and leases. The new binding is CPU-only, and should be called before upload. No duplicate material/texture database and no renderer calls on ECS tick.

## Exact source commits
28b9b98b9b76489f0a9857c7ea4d2f4e2fbd15eb new unified binder
5649d6c9d476e2228a010db08f5320a113a96d56 explicit portable type aliases
6861d8e5c79ce1c3c960308cf0a9d9c886f6d683 single leaf material
9681efc04bbd0ca3651a1497207e5dc54cc32a79 game bridge API
91fc162124fd0d25c427c80f8ee00e5ecb5592b6 runtime export
1ac9a38149f631749048b9af128c63b54b2cafcb skill update
This handoff is committed separately.

## Validation/limitations/next
Tests remain deferred by user request. No compiler/CMake/CTest, import pipeline, GPU or Studio UI tests were run. Existing texture importer callback, Studio material picker UI, asset reimport invalidation and renderer upload/retirement must be wired in their actual owners. In a future stage bind the unified material system to Studio inspector with asset picker, drag-and-drop and GUID conflict resolution, plus real game render submission. Then run the combined full native test suite and reconcile Draft PR with main.