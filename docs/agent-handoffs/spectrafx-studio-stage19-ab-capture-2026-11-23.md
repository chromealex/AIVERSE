# SpectraFX Studio Stage 19 — A/B native capture slots (2026-11-23)

## Provenance
Repository: chromealex/AIVERSE
Branch: feature/spectrafx-html5-data-interfaces-20261123
Implementation commit: a5e19faa14de25f6449a42b4043e5abb8d4394c7
Parent work: Stage 18 native bounded one-shot capture; PR #115 stacked on #113.
No merge to main.

## Implemented
- Modified apps/studio-web/src/panels/SpectraNativePreview.tsx.
- Added independent A and B in-memory snapshot slots. Both reuse the existing studio.spectraPreview capture/captureStatus protocol and native GPU JPEG readback.
- Capture completion is routed to the requested slot. On project switch, slots and capture intent reset.
- Side-by-side and experimental 50/50 split viewing, per-slot JPEG download and Clear A/B.
- No new RHI capture path, ECS/gameplay writes, or fake browser particles.

## Tests actually executed
NONE. User previously deferred testing until feature work is complete. No TypeScript/Vite build, Mac Metal screenshot, runtime/UI check or MCP test performed on this SHA. In particular the split view layout and async capture slot lifecycle require interactive QA. Do not claim full Stage 19 completion.

## Deferred / limitations
- Pixel difference metrics, tolerance controls, report export, and dedicated MCP compare operations are NOT implemented.
- Existing MCP fx_graph_preview capture and captureStatus remain available; new A/B slots are browser UI state, not MCP server state.
- JPEG is lossy; no HDR/alpha or CPU/GPU parity claim.
- Confirm project-switch and failed-capture cancellation during native callback; test split CSS with both equal and unequal capture dimensions.
- A/B snapshots consume two data-URI copies; maintain bounded dimensions and never persist project files implicitly.

## Next steps
1. Run npm typecheck/test:graphs/build and native Studio focused CTest at exact SHA; fix compile/UI issues.
2. Replace experimental split CSS with dimension-aware aligned overlay and draggable divider.
3. Add pixel difference calculation using decoded actual JPEGs, handling differing dimensions and color-space limitations; expose percentage and threshold.
4. Export report, add MCP compare commands if server-side ownership is needed.
5. Update skill and system docs, add real editor screenshot and devlog only after visual validation.
