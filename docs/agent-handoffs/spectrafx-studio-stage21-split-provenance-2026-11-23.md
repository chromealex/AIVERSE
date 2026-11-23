# SpectraFX Studio Stage 21 — interactive A/B split and provenance (2026-11-23)

## Provenance
Repository chromealex/AIVERSE. Branch feature/spectrafx-html5-data-interfaces-20261123.
Implementation commit 5632551991ba6c14b989be2bdee16a318f91b4c3. Parent Stage 20 c41eb96ae99b1f6e7be4bef925fed26d51a6ab89.
Not merged into main.

## Implemented
- apps/studio-web/src/panels/SpectraNativePreview.tsx: replaces the experimental 50/50 CSS with one aligned full-width B image and clipped A image, plus 0..100% split position slider and visual separator.
- Stores per-capture browser-side provenance: graph JSON signature, project path, UI-observed tick, backend, requested snapshot max side and wall-clock capture request timestamp. These are observations at capture request time, NOT authoritative GPU frame metadata; camera pose is NOT recorded.
- Warns when graph revision, tick, backend or requested size differ. Exports A/B provenance with the existing JSON difference report.
- Keeps native GPU capture protocol, one-shot callback, and game ECS unchanged.

## Tests actually executed
NONE. No exact-source npm typecheck/Vite, Metal capture, UI screenshot or CTest. This is source implementation only, not verified production functionality.

## Limitations and risks
- Comparison assumes same actual camera pose and dimensions. Browser metadata cannot guarantee synchronized captures or identical camera state.
- JPEG comparison is lossy display-referred sRGB; not HDR/alpha or GPU/CPU parity.
- The split overlay assumes matching image dimensions; mismatched captures are rejected by the pixel comparator, but split display still needs an explicit dimension guard and interactive QA.
- No native camera snapshot serialization or deterministic A/B capture transaction. No new MCP compare operations.
- Preserve the Stage 18 native capture ownership and stacked PR #115/#113.

## Next steps
1. Compile/typecheck latest branch and fix any JSX/TS errors; test screenshot split at 0,50,100 with real captures, unequal dimensions, project switch and cancellation.
2. Add native capture metadata (exact rendered tick, camera transform, graph revision) and deterministic re-render from a saved preset if needed.
3. Add drag-on-image split interaction with pointer capture and keyboard accessibility; guard unequal dimensions.
4. Expand MCP with bounded compare-report operations only if headless native GPU viewport exists.
5. Update AGENTS/skill/roadmap and verified devlog screenshot after native QA.
