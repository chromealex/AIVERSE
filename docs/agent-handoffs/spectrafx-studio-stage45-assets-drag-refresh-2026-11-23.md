# SpectraFX Stage 45 — Assets Browser drag and project refresh (2026-11-23)

Repo chromealex/AIVERSE; branch feature/spectrafx-html5-data-interfaces-20261123. Not merged.
Commits 59c66515d7fdf9bae1edad0e81f6d95181269fd0, 1406873d9e3f5f1bc805717b5c7a7db7c8a99eb4, 36468ba8fb6cb990e73ca8e73aec3ce73305afac, e14b71c9133ad075ee318e424f0f6ca2ea4610d7.

## Implementation
- apps/studio-web/src/panels/SpectraAssetPicker.tsx: supports the actual AssetsPanel MIME application/x-aiverse-content-path, resolving its path through the native indexed asset list to a known GUID before selection. Also supports the previous application/x-aiverse-asset JSON GUID MIME. Rejects unknown paths/IDs.
- apps/studio-web/src/panels/SpectraIndexedGraphAssetInspector.tsx: refreshes index on projectPath change, workspace.changed and document.changed events, plus explicit Refresh button.
- apps/studio-web/src/panels/SpectraDataInterfacesPanel.tsx: passes projectPath to the mounted inspector.
- skills/visuals/aiverse-spectrafx/SKILL.md: documents actual Assets Browser drag contract.

## Tests actually executed
NONE, user deferred tests. No TS compilation, UI drag, native catalog or GPU QA.

## Limitations
- Drag from external Finder/Explorer is handled by the Assets Browser import flow, not directly by this VFX picker.
- Indexed path resolution does not prove runtime texture loading; native validate/publish still authoritative.
- The event-driven index refresh may need debounce under frequent document.changed events.
- No exact GPU capture fence, no GPU shader RNG.

## Next
Add a bounded refresh debounce or generation guard, inspect other asset-bearing VFX node properties and wire their pickers with native schema. Finish all features before tests.
