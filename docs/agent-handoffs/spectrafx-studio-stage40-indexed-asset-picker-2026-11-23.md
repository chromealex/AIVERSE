# SpectraFX Stage 40 — reusable indexed Assets picker (2026-11-23)

Repo chromealex/AIVERSE; branch feature/spectrafx-html5-data-interfaces-20261123. Not merged into main.
Commits ef11d080f83e5da558d5ef7735257a8de232e0dd, 8898b46ae9c288794569a9cc2cad0330689c8457, d2fc8f88b5cfe8d297d62cb355463b54581a6cbf.

## Implemented
- apps/studio-web/src/panels/SpectraAssetPicker.tsx: reusable searchable React asset selector taking indexed GUID-backed assets; filters by name/path/kind/GUID and limits visible list to 200; supports drag/drop using application/x-aiverse-asset with mandatory lookup in supplied project index; optional kind filter, disabled state, controlled lock toggle, and missing-asset feedback.
- skills/visuals/aiverse-spectrafx/SKILL.md: integration instructions and explicit limitations.

## Tests actually run
NONE per user instruction. No TypeScript compilation, UI interaction, C++/GPU validation or MCP acceptance.

## Limitations
- Picker is a reusable component, NOT yet mounted in the actual Graph Editor inspector.
- The parent must pass a real Assets GUID index and honor lock by ignoring external selection changes. The picker does not own global selection.
- No native compiler property schema validation or GUID-to-runtime path resolution. The Stage 39 authoring helper stores GUID objects, but native compiler compatibility is unverified.
- External filesystem drag does not auto-import; only known indexed GUIDs are accepted.

## Next
Locate the actual Studio VFX graph inspector and existing Assets index provider; mount the picker through schema-specific adapters and wire selection lock. Complete drag/import workflow after GUID resolution. Tests remain deferred until feature completion.
