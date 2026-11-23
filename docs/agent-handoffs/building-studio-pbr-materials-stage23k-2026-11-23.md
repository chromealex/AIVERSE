# AIVERSE Building Generator Stage 23K — unified Studio PBR materials + textures

Date: 2026-11-23
Repository: chromealex/AIVERSE
Branch: feature/aiv20261123_building-grammar
PR: https://github.com/chromealex/AIVERSE/pull/114 (Draft)

## User request and investigation

Following Stage 23J.1 unified CPU material+texture binding, user approved continuing with HTML5 Studio. Inspected the current branch's real `apps/studio-web/src/components/AssetReferencePicker.tsx`, `apps/studio-web/src/panels/generated/BuildingGeneratorPanel.tsx`, `apps/studio-web/src/panels/materialSchema.ts`, `apps/studio-web/src/types.ts` and `bridge.ts`.
The shared AssetReferencePicker already supports search by name/path/GUID, drag/drop from Studio Assets, image-only asset filtering, choosing GUID identity, and clearing selections. The existing native bridge already implements `studio.materials` and `studio.materialPatch` for a project materials.json entry. Do not invent a second asset chooser, GUID database or save format.

## Changes published

- NEW `apps/studio-web/src/panels/generated/BuildingMaterialEditor.tsx`:
  - Loads the actual shared material library through `studio.materials`.
  - Presents a material selector and a unified scalar+texture edit surface.
  - Numeric factors: metallic, roughness, emissive strength, normal strength, occlusion strength.
  - Texture GUID slots: Albedo/Base Color, Normal, Metallic/Roughness, Occlusion, Emissive.
  - Each asset slot uses the existing `AssetReferencePicker kinds={['image']}` with project-verified GUID, popup search and drag-and-drop.
  - Tracks baseline/dirty state, prompts before discarding on selection, has Discard/Save, handles native errors, does not display a fake browser save.
  - One `studio.materialPatch({name, patch:draft})` persists numerical factors and texture asset references together in materials.json using existing native backend semantics.
- NEW `apps/studio-web/src/panels/generated/BuildingMaterialEditor.css`: compact responsive grid consistent with the shared Studio UI, no new UI framework.
- `apps/studio-web/src/panels/generated/BuildingGeneratorPanel.tsx`: embedded the editor as `PBR materials & textures` section.
- `skills/world/aiverse-building-style/SKILL.md`: workflow and owner boundaries.

## Architecture and limitations

This stage is a real editor for existing shared Studio material library entries, **NOT** yet a material-override picker for individual architectural BuildingStyle slots. Portable `BuildingStyle` and `RuntimeBuildingGenerator` use the bundled architecture style JSON, whereas current `studio.materialPatch` persists `materials.json`. The UI explicitly states this distinction; the newly saved material does NOT automatically change the generated building GLB or Renderer3D GPU render. Binding this library entry to the style (by stable name/GUID or an explicit override) and wiring texture resource resolution into the native generator/exporter remain outstanding.

No new native RPC was invented. No project file is edited directly by browser JavaScript. Only the existing native material patch method saves. Browser preview/non-native mode disables Save. Existing Asset picker checks authoritative Studio catalog on drop and never writes a filename as GUID.

## Provenance

- `547f80e330ba14259cde3ce2a9f1ef56c5126db6`: new unified editor.
- `805ee991539cd270e663defadbc05e01415672ab`: style sheet.
- `ce7e1a396488a8ac97239ef547191e69f6e037d2`: embedded in BuildingGeneratorPanel.
- `6118dd6ca3419dec12e614181bc5f9745efeeee4`: skill.
- This handoff is a separate documentation commit.

## Testing and next step

Tests DEFERRED per user. No TypeScript typecheck/Vite build, Studio native save invocation, C++ compile/CTest, real asset reimport/GLB rendering, Metal/Vulkan/GLES test was executed. Source commits are not proof of running UI or actual generated textured building.

Stage 23L: introduce an explicit existing-owner-compatible native building style material override API (asset/catalog persistence, atomic conflict handling, style panel selection), connect chosen material factors + GUID textures to the generated runtime building and GLB/Renderer3D pipeline, and verify texture color-space/importer settings. Do not overwrite ongoing `studio-html5` development; reconcile branches before merging. Keep PR Draft until proper build, test and render evidence. Continue writing a separate handoff after every stage.
