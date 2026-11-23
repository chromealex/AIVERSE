# SpectraFX Studio Stage 20 — native JPEG A/B pixel diff (2026-11-23)

## Provenance
Repository chromealex/AIVERSE; branch `feature/spectrafx-html5-data-interfaces-20261123`.
Implementation commits: `989864fa5f7d9b6068f066bdfa48b700c6631d7d`, `99d208f31c6ad77dba94d7a9629df76d7d1be8ea`.
Not merged into main. Stage 19 is prerequisite.

## Implemented and API
- `apps/studio-web/src/panels/spectraPixelDiff.ts`: `compareSpectraCaptures(a,b,threshold):Promise<SpectraPixelDiff>`; decodes two real native bounded JPEG data URIs using Image and 2D canvas, requires equal dimensions <=768x768, compares absolute RGB values and creates PNG heatmap. `changed` counts pixels whose max channel delta exceeds threshold. `meanAbsoluteError` is mean absolute RGB channel error /255 scale. Canvas data are 8-bit display-referred, not HDR or alpha.
- `apps/studio-web/src/panels/SpectraNativePreview.tsx`: reactive comparison after A/B updates, threshold slider 0..255, changed ratio, MAE, heatmap download, machine-readable JSON report download. Stale async comparison results are ignored on capture/threshold change or unmount. Browser-only UI; no gameplay writes or duplicate GPU readback.
- Report contains numeric summary, dimensions and caveat; JPEG A/B downloaded independently. It intentionally does not embed images or claim GPU backend parity.

## Tests actually executed
NONE: feature-first work, per prior user instruction. No npm typecheck, build, native Metal/Vulkan, UI screenshot, or MCP acceptance on these commits. Results are not verified.

## Limitations / risks
- Native JPEG lossy compression and browser sRGB conversion influence metrics. A/B camera, stage, frame tick and capture dimensions must be aligned by the user; unequal dimensions produce an explicit error rather than silently resampling.
- Difference image uses PNG and may consume memory up to 768x768; not a GPU compute heatmap.
- Existing Stage 19 Split CSS is experimental and requires QA.
- A/B state is not exposed as dedicated MCP compare operations. No pixel-level GPU/CPU parity proof.
- Browser Image decode, blob download, and UI layout require WKWebView/WebView2 interactive validation.

## Next steps
1. Run `cd apps/studio-web && npm run typecheck && npm run test:graphs && npm run build` and fix any exact-SHA errors; verify native Studio focused CTests and actual Metal capture.
2. Add robust aligned split slider with identical-dimension guard and visual QA.
3. Add A/B capture metadata (graph revision, tick, camera pose, backend) to exported report and show mismatch warnings.
4. Consider MCP comparison operation using safe host ownership and bounded output; do not invent browser pixel data on headless MCP.
5. Update system skill/roadmap and site devlog after verified editor screenshots. Preserve other agents' branch changes.
