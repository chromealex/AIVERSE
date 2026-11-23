# SpectraFX Stage 39 — graph asset GUID authoring primitive (2026-11-23)

Repository chromealex/AIVERSE; branch feature/spectrafx-html5-data-interfaces-20261123; not merged to main.
Commits e368deca017a98d39a83727060b0e96b9c869589, ef0753736cae5bb58cb097dc8d08443a69a056c5.

## Implemented
- apps/studio-web/src/panels/spectraAssetBinding.ts: immutable node property binding to an Assets-index GUID with optional project-relative path, conservative GUID/path/property validation, and readback helper. Unknown graph metadata and unrelated node properties remain unchanged. Errors preserve original graph.
- skills/visuals/aiverse-spectrafx/SKILL.md: documents how agents use the helper and warns that it is not wired into the asset picker or compiler.

## Tests actually run
NONE, per user's request to finish functionality before testing. No TS typecheck, C++ compilation, UI interaction or native schema verification.

## Critical limitations
- This is a pure authoring helper, NOT a functioning Studio Assets picker, drag/drop integration, GUID database lookup, or a proven native SpectraFX graph schema. Do not persist GUID objects to native graph assets without confirming the native compiler's expected property type.
- Existing native graph compiler/validation remains the source of truth.
- GPU Shader RNG and exact capture fence remain unsupported.

## Next
Inspect native graph schema for asset-bearing node properties, Studio Assets picker APIs and GUID lookup, then connect the helper through a verified schema adapter. Implement drag/drop and selection-lock semantics in Graph Editor. Finish remaining features before testing.
