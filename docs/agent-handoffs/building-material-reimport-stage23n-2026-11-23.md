# AIVERSE Building Generator — Stage 23N: UV authoring, material path compatibility and texture dependency inspection

Date: 2026-11-23
Repository: chromealex/AIVERSE
Branch: feature/aiv20261123_building-grammar
Draft PR: https://github.com/chromealex/AIVERSE/pull/114

## User request and exact verified source context

User approved Stage 23N after Stage 23M. Investigated native StudioMaterialValidation, StudioSession, existing Studio materialSchema, BuildingMaterialEditor, BuildingGeneratorPanel, StudioBuildingGeneration, StudioAssetCatalog and BuildingGlbWriter. Critical discovered mismatch: HTML5 AssetReferencePicker emits project content GUIDs, but the existing StudioSession studio.materialPatch persists assets as `content/...` project-relative paths in materials.json; Stage 23L expected only GUIDs. This stage fixes that native project path/GUID boundary, not by replacing existing material storage.

## Implemented changes

1. `apps/studio-native/StudioMaterialValidation.h`: new `parseStudioUvPair` accepts a strict pair of finite numeric UV components (-1024..1024).
2. `apps/studio-native/StudioSession.cpp`: native `studio.materialPatch` maps validated `uvScale`/`uvOffset` comma-pair strings to JSON arrays before calling existing material_set; existing material storage/validation stays authoritative. Added native `studio.buildingStaleMaterials` read-only handler.
3. `apps/studio-web/src/panels/materialSchema.ts`: adds UV Scale and UV Offset to the existing shared PBR material edit schema and handles native two-element array display.
4. `apps/studio-web/src/panels/generated/BuildingMaterialEditor.tsx`: adds two UV fields alongside metallic/roughness/normal/AO controls and shared AssetReferencePicker. Explicit save validation checks UV bounds. On loading/saving the native shared material library it resolves existing content-relative image paths back to GUIDs through `studio.assets`. Prevents subsequent patch operations from re-sending stored content paths to an API which expects picker GUIDs. Emits onSaved to refresh generator library choices.
5. `apps/studio-native/StudioBuildingGeneration.h`: when binding a project library material to a building's architectural StyleMaterial, accepts either valid 32-character GUID or canonical existing `content/...` material reference, resolves the latter through the existing StudioAssetCatalog::byPath, validates importer=`texture`, and uses actual GUID for GLB writer. Rejects arbitrary or unsafe paths instead of guessing.
6. NEW `apps/studio-native/StudioBuildingDependencies.h`: bounded read-only `studioCheckBuildingDependencies` comparing each saved embedded texture dependency GUID, .meta importer settings and FNV-1a source byte fingerprint to the current StudioAssetCatalog and actual file. `studioBuildingStaleMaterials(project)` scans at most 2000 GeneratedBuildings directory entries for .glb.recipe.json manifests and produces stale reasons. Never overwrites any generated asset.
7. `apps/studio-web/src/types.ts`: adds `studio.buildingStaleMaterials` request/result typing.
8. `apps/studio-web/src/panels/generated/BuildingGeneratorPanel.tsx`: manual Check generated material dependencies button and stale list, refreshes shared material choices after saving and clears stale view.
9. `skills/world/aiverse-building-style/SKILL.md`: documents exact APIs, ownership, limitations and next step. Existing catalog registration retained.

## Source commit provenance

- `4eaa1c2eb7798534617a0f2c6f54f7849a7d77c0` — UV parser
- `383e5c3d87032ab055f6a275da1bf48f5229cf2a` — native material patch UV arrays
- `93281dd07a0e0d0ec96fbb7c9b3c81000c6a1aaf` — browser material schema
- `34a86ed96d2314fcf83ec8ef4cc3d32f75a86ca4` — browser UV edit fields
- `5ca79ceba041857281156c8977491aff43ff2dcc` — native material content paths to GUIDs
- `e42db9a5bff1782c9d8acecb48c4fdc5e46cebf8` — dependency checker
- `0aef2becfe068d12421986c75b8bdaba2709df12` — stale generated materials enumeration
- `16be55d26651aec86d7880b7b2fe6fc1d63e0253` — native readonly bridge
- `293a7e35fbc306bd0040252cc6fcbf0c4b90e318` — TS request/result types
- `7e7b125a020a1494b43db31f6dc3c503b82c577f` — Studio generator stale dependency UI
- `83ad5611f89fcb244556d3109cf31561df5a6e92` — persisted material path->picker GUID reconciliation
- `09ab45d8dc77ba925e09de5572308ddbad228aaa` — editor saved callback
- `aadbec8fc8a9f9a0c6fc13d9214406341737b9b7` — refresh generator bindings
- `af0fde976f8ab3ff9989d6eb6e251133f1ddea59` — skill
- This handoff is a separate documentation commit.

## Explicit limitations

The checker is a manual READ-ONLY reimport detection tool. No automatic regeneration or replacement of existing GLBs, no live GPU resource invalidation, no Material cache lease/revision update, and no filesystem watcher are claimed. Existing GLBs keep GUID identity and are not overwritten. Full automatic change handling requires existing renderer/asset owner lifetime and locking logic, and explicit save/conflict policy.

The current GLB exporter supports direct embedded PNG/JPEG inputs only. Existing engine decodeImage paths can accept some other formats, but this does NOT establish a safe resulting image encoder or cross-backend compatibility; KTX2/DDS/TGA/WebP/EXR conversion remains future work. The new UV controls are two-component scale/offset strings parsed to arrays on native save; the existing schema does not yet expose UV rotation editing. The renderer still uses the Stage 23M native material UV settings after texture binding.

Tests explicitly deferred by user. No compiler/CMake/CTest, web TypeScript typecheck, native material patch execution, glTF validation, model renderer GPU upload or Metal/Vulkan/GLES screenshots were run. Source commits alone are NOT proof of working UI or reimport pipeline. Keep PR Draft and do not merge until actual build and compatibility checks run.

## Next concrete stage

Stage 23O: integrate the existing importer's validated source image decoding with proper PNG/JPEG output or supported glTF compression extension, respecting per-channel sRGB/linear and mip/compression settings; add real asset change callbacks which enqueue stale generated building revisions, permissioned explicit regeneration and safe GPU resource retirement/reload; wire dynamic physical dual leaves and graph ownership to an actual simulation host. Run all deferred C++/web/glTF/graphics tests once features close, reconcile main branch divergence, and write next handoff after every stage.
