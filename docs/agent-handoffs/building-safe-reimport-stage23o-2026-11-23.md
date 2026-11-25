# AIVERSE Building Generator — Stage 23O: safe PBR texture and material reimport

Date: 2026-11-23
Repository: chromealex/AIVERSE
Branch: feature/aiv20261123_building-grammar
Draft PR: https://github.com/chromealex/AIVERSE/pull/114

## Goal and verified existing APIs

Continues user-approved Stage 23O: auto-detect changed source images/materials, regenerate safe new asset versions and hand updated models back to existing Studio renderer owner.

Inspected actual feature-branch code before editing: StudioBuildingGeneration.h, StudioBuildingDependencies.h, StudioSession.cpp, StudioScene.h/.cpp, StudioHost.h, StudioAssetCatalog, StudioBuildingGeneratorPanel, StudioWeb types/mock and existing StudioSession::setMeshRenderer. StudioScene already checks authored model file timestamps, invalidates renderer model caches before release, and uses its existing render::prepareModelUpload / render::uploadModel / render::releaseModel lifecycle. The Studio Session existing `studio.setProperty` has a MeshRenderer asset route with MapDoc edit/Undo and native asset validation. No new Renderer3D cache or blind file overwrite was justified.

## Changes actually implemented

1. NEW `apps/studio-native/StudioBuildingRegeneration.h`:
   - `studioBuildingRecipeFile(project,relative)` validates a project/content-relative `GeneratedBuildings/*.glb.recipe.json` input: bounded length, direct child, exact suffix, regular file under 256 KiB, no relative escapes and no symlinks.
   - `studioBuildingRegenerate(engine,project,recipe)` reads canonical recipe schema 1, reconstructs the request using the original native `buildingStudioDetail::request` validator, then calls the existing `studioBuildingGenerate` path to produce a new GLB+recipe with a new sidecar GUID. Old assets remain intact. Response includes sourceRecipe and createdNewAsset.
2. `apps/studio-native/StudioSession.cpp`: native `studio.buildingRegenerate` endpoint wired to above guarded API. No WebView filesystem IO or privileged arbitrary path input.
3. `apps/studio-web/src/types.ts` and `apps/studio-web/src/mock.ts`: typed new endpoint; DEMO refuses to claim filesystem regeneration.
4. `apps/studio-native/StudioBuildingDependencies.h`:
   - `studioBuildingMaterialFingerprint(Json)` hashes canonical `materials.json` material definitions.
   - Existing dependency checker now verifies optional materialDependencies and reports changed/removed PBR parameter definitions, including material-only assets with no textures.
   - Existing texture GUID/metadata/original-byte fingerprints remain in use.
5. `apps/studio-native/StudioBuildingGeneration.h`: saves an ordered, deduplicated `materialDependencies` fingerprint manifest in each generated recipe for selected library material overrides.
6. `apps/studio-web/src/panels/generated/BuildingGeneratorPanel.tsx`:
   - "Create updated copy" button per stale original recipe.
   - Opt-in "Auto-create updated copies when Assets change" responding to existing Studio asset/document events; source recipe de-dup within the mounted project session so a stale immutable original does not generate an unbounded stream of copies.
   - Refreshes stale dependency listings and asset browser event when a regenerated asset is created.
   - "Use on Selected Object" explicitly applies the regenerated/new GLB to the selected authored MeshRenderer entity via existing `studio.setProperty(componentId=MeshRenderer,key=asset,value=assetGuid)`, with a user confirmation and existing document Undo. StudioScene is the owner of upload, replacement, and GPU retirement.
   - Resets dedup cache on changing project.
7. `skills/world/aiverse-building-style/SKILL.md`: documented boundaries, commands, lifetime and limitations.

## Ownership semantics and known limitations

Automatic detection and opt-in regeneration occur only while the relevant Studio panel is mounted and the client receives native Assets/document events. This is NOT an always-running file watcher, not an automation daemon, and not guaranteed to react to external filesystem changes that do not emit events until a manual scan runs. One stale original recipe remains stale because it and its original GLB intentionally remain immutable. The opt-in auto path generates at most one new copy per stale source recipe for this mounted project session. The user can manually create another version when desired.

Renderer integration uses existing authorized Studio MapDoc and StudioScene ownership, not a new GPU resource manager. A new copy becomes visible/loaded when placed or explicitly assigned to an authored scene entity. This DOES NOT automatically swap all live references to the old GUID or synchronize a shipped multiplayer game's entities. Existing StudioScene manages upload/caches/resource release on scene changes; this stage does not itself call renderer methods.

A new GLB creation can fail if a referenced source material no longer exists, the source recipe is invalid, a GUID has gone missing, or the image cannot be decoded/encoded. The operation is create-only and does not damage the previous source asset. The old recipe remains authoritative for the original copy. Existing `StudioAssetCatalog` owns .meta GUID index registration, and the previous Stage 23M/23N GLB PNG/JPEG / convertible-image rules still apply. Material fingerprints are FNV-1a fingerprints of JSON documents, for change tracking not security integrity.

Potential follow-up: tighter automatic update coalescing across multiple Studio windows, persistent scene-aware replacement decisions, project journal integration for generated asset creation, robust output transactions if sidecar registration fails, and shipping game owner GPU lease/fence staging.

## Stage commit provenance

- `5feb4bb46ee81992b8673a04cece42f738671fda` — guarded create-only regeneration helper.
- `f64cb1846ba351655c0b026b3eb9e8c6dc2fd9d2` — native session RPC.
- `4611d0691da3d685c70e779a040e5ec897a0bf43` — browser RPC types.
- `c03a1a9543ba9f867664e539823ba666034259e1` — Studio UI stale update and opt-in auto-create.
- `c24df13bc2bbcd53db82c579653c36bbf28ac833` — strict mock behavior.
- `26b9e6288d77d23629932baa997134e0d2c8d62b` — authored PBR material fingerprint checks.
- `7ecf53196ac9cef98c5ddd2c1527e7d7861a84d8` — material-only dependency checker fix.
- `24105dc06b7f50407fd6e0ccad99533a5b48bf9c` — generated PBR material manifests.
- `e821819fa62f5ed1612833f3f93e8a2ac7795787` — per-project auto-mode dedup reset.
- `437a02af5c4f7286e837f82c96670fb0686070a9` — existing MeshRenderer scene owner explicit application.
- `c62ef9ba884c3f5d59345b9276d30f637959853d` — skill.
- This separate handoff commit concludes the stage.

## Actual test evidence

User asked to defer tests. NO C++ compiler, CMake/CTest, browser TypeScript/Vite, GLB parser/validator, GPU Model loading or Metal/Vulkan/GLES screenshots, asset overwrite/rollback simulation, PHYSIS collision or network tests were run for Stage 23O. Do not claim production readiness based on committed source. PR remains Draft and branch is divergent from main.

## Next concrete step

Stage 23P: complete actual project/game runtime owner integration for dual-physical-leaf fixed-tick updates and revisioned Renderer3D Model staging; improve cross-window asset event coalescing and independent scene asset references. Then compile and run the deferred native/Web/GLB/GPU/PHYSIS suite, fix discovered issues before merging the Draft PR, reconcile current main branch carefully. Continue maintaining a distinct agent handoff after every stage.
