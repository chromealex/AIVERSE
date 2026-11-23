# AIVERSE Building Generator — Stage 23L: project material bindings and embedded GLB textures

Date: 2026-11-23
Repo: chromealex/AIVERSE
Branch: feature/aiv20261123_building-grammar
Draft PR: https://github.com/chromealex/AIVERSE/pull/114

## Request, inspected ownership

User continued Stage 23L following Stage 23K unified Studio material+texture editor. Examined current-branch `apps/studio-native/StudioBuildingGeneration.h` (actual building catalog, inspect and generation entry points), `engine/buildings/BuildingGlbWriter.h`, `apps/editor/core/MaterialOps.cpp` (project library is `materials.json` -> `materials` object), `apps/studio-web/src/types.ts`, `BuildingGeneratorPanel.tsx`, `BuildingMaterialEditor.tsx`, and `StudioAssetCatalog` (GUID from project/content sidecars). Did not introduce a new RPC, asset chooser, material database, or rendering backend.

## Implemented

1. Native building catalog `studioBuildingCatalog` now includes actual named architectural material slots in `styles[].materials`.
2. `StudioBuildingRequest.materialBindings?: Record<string,string>` maps an architecture style material ID to an existing shared project material name. The generator panel exposes a select control per style slot using `studio.materials` real library contents. Changing style clears stale bindings. Both functional and preset mode requests support bindings.
3. `StudioBuildingGeneration.h` parses and bounds `materialBindings`, validates slot/library identities, reads the project-owned `materials.json` (not a browser-specified path), validates the numeric PBR factors, and overlays its color, metallic, roughness, emission, and texture GUID fields onto the generated mesh's corresponding `StyleMaterial` before export. Unknown material slot or library material fails explicitly.
4. The native exporter `BuildingGlbWriter.h` now emits proper glTF 2.0 baseColorTexture, metallicRoughnessTexture, normalTexture, occlusionTexture and emissiveTexture references. PNG/JPEG bytes are obtained from `StudioAssetCatalog::byGuid` in the native building generate operation and embedded in the same GLB binary chunk using image bufferViews and textures. Duplicate GUIDs reuse one image reference; files are bounded at 16 MiB each and checked inside project/content without symbolic-link path components.
5. `studioBuildingGenerate` records materialBindings in adjacent `.glb.recipe.json` for reproducibility/agent continuation. The building CPU mesh still has the standard generator geometry/physics/seed; material overrides affect only presentation.
6. `skills/world/aiverse-building-style/SKILL.md` documents source, format and owner boundaries.

## Source commits

- `50c42e3777701b2c4ab76e2a2eca52073977c06d`: GLB PBR image/textures and embedded PNG/JPEG serialization.
- `7180f04509ef4c7441eb52fe4c4aaad649eb0180`: project material overrides and native GLB texture asset lookup.
- `1ead1a1aaf2b3b79b37774898b04d8946f441a9b`: catalog style material slot enumeration.
- `d11cd744d207c44b45c241f69fbf7cec3a64b733`: Studio TypeScript request/catalog declarations.
- `dbc227d8c0aed77bbfe9070e867fdb8c2012515d`: UI per-slot shared library material selectors.
- `23498ffa1a77f73cac2524e80056ef709ccd0c13`: skill update.
- This handoff is saved in its own documentation commit.

## Operational ownership and integration

The Native Studio generator resolves project materials once per generated asset, preserving style geometry and material slot identity. Existing Studio material editor still edits `materials.json` through `studio.materialPatch`. Assets search/drag/drop still uses existing `AssetReferencePicker`, and textures are saved as GUIDs in library. Users can now select a library entry for each generated style slot; generate emits a self-contained GLB with embedded supported image bytes. `studio.buildingInspect` remains geometry-only. GPU upload and actual material shader sampling belong to existing glTF/Renderer3D importers and graphics owners.

## Limitations and actual testing

Stage 23L code was committed only. Following user's explicit policy, **no compiler, TypeScript/Vite build, CTest, glTF validator, in-game Renderer3D screenshot, Metal/Vulkan/GLES render verification or gameplay test was executed**. Do not claim exported assets actually render correctly until tested. The GLB path supports PNG/JPEG only; KTX2/DDS/WebP/EXR/TGA need importer-mediated transcoding or a glTF extension. Only five standard glTF PBR texture channels and color/metal/roughness/emission factors are exported; additional material property set (e.g. normalScale, occlusionStrength, material inheritance, UV transform, asset reimport detection, texture compression pipeline) remains outside current exporter contract. Project material paths (instead of GUID) are deliberately refused in this binding. Existing runtime `runtimeTexturedBuildingModel` is an explicit API and not yet wired to the generated Studio model scene's GPU publication.

## Next step

Stage 23M: validate the GLB with a real glTF parser and renderer; support importer-backed conversion of non-PNG/JPEG texture formats, channel color-space handling, UV tiling and material reimport revisions. Wire live runtime/Studio preview to the existing Renderer3D CPU material/image owner using current resource leases, and complete graph/physical dual-leaf scene registration. Perform the deferred full feature QA and reconcile divergent main before merging Draft PR #114. Add a distinct handoff after every stage.
