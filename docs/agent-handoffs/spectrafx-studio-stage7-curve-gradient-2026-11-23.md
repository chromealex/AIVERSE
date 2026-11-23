## Follow-up changes and exact CI provenance (2026-11-23)

- Additional code/test commits after the initial Stage 7 handoff:
  `f6ea2047e102e8cffe260ccab4f60ff65030c085` fixes CSS gradient preview for zero/one authored stop (CSS requires a valid two-stop fallback);
  `3692d809852be6ee6703e086eee77f3eb10bc5fa` adds the corresponding pure-model regression tests.
- Source HEAD immediately before updating this handoff:
  `3692d809852be6ee6703e086eee77f3eb10bc5fa`.
  The documentation commit advances that SHA; run acceptance on the *new*
  exact branch head before claiming tests pass.
- GitHub run IDs for that source SHA: general CI `45216592408` (queued),
  HTML5 Studio `45216592324` (pending), focused SpectraFX editor
  `45216592291` (queued). No current-source test result confirmed.
- Draft PR #115 updated to include Curve/Gradient scope.
  Parent PR #113 and main were not modified in Stage 7.

# SpectraFX Stage 7 — HTML5 Curve and Gradient key authoring

Date: 2026-11-23
Repository: `chromealex/AIVERSE`
Branch: `feature/spectrafx-html5-data-interfaces-20261123` (draft stacked PR #115)
Parent: `feature/spectrafx-significance-data-20261123` (draft PR #113, not merged)
Code HEAD before this documentation: `f989e3d975a071bcea7afe772ca5a53d41585c7d`.
Check `git rev-parse HEAD` and CI SHA before assigning test results; this documentation commit advances the branch.

## Implemented

1. `apps/studio-web/src/panels/spectraCurveModel.ts` — bounded, immutable Graph v2 `Curve` and `Gradient` authoring helpers. Existing canonical graph representation only:
   - Curve: `properties.value = [[normalizedTime,number], ...]`.
   - Gradient: `properties.value = [[normalizedTime,"#rrggbbaa"], ...]`.
   - Accept 1..32 keys, finite normalized time [0,1], finite scalar value within ±1e7, distinct key timestamps (gap ≥ 0.0001). Reject malformed arrays without mutation. Adding a key uses the widest available interval, sorted by timestamp. Delete cannot remove the final key. Preserve unknown graph fields, unrelated nodes, other node properties and stage layout.
   - Sanitize gradient colors using six/eight-digit RGB/RGBA hex; alpha conversion is explicit. No arbitrary CSS injection; generated CSS gradient is for a **static authoring preview only**, never a game GPU frame.
2. `apps/studio-web/src/panels/SpectraCurveEditor.tsx` — editor for existing Curve and Gradient nodes using approved Studio UI controls: choose node, select/add/remove keys, edit normalized time and value; color chooser + opacity for gradients; static SVG scalar curve chart and CSS gradient strip. Slider restricts crossing neighboring stops. Error state is sent to the parent graph pane; edits stay local to React draft.
3. `apps/studio-web/src/panels/SpectraCurveEditor.css` — responsive, semantic Studio `--au-*` tokens, no new palette or hardcoded fake particle effects.
4. `apps/studio-web/src/panels/SpectraDataInterfacesPanel.tsx` — embeds typed editor alongside Stage 5 Data Interfaces and Stage 6 canvas in existing `fx_graph` dock tab. On editing, clears stale compiler diagnostics; normal Validate / Save Draft / Publish flow remains authoritative.
5. `apps/studio-web/tests/spectraCurveModel.test.mjs` — tests parsing, sorted keys, duplicate/NaN/out-of-range rejection, RGBA alpha/hex safety, immutable edits, widest-gap insertion, removal and invalid authored arrays. Added to `npm run test:graphs` in `apps/studio-web/package.json`.

## Lifecycle and compiler contract

This editor does NOT add a new SpectraFX authoring format or renderer. Existing `SpectraGraphV2` supports Curve and Gradient literal values. Native Workspace `fx_graph_validate` and `fx_graph_set` remain the only publication authority; the browser cannot write graph files or mutate game ECS. `fx_graph_draft_set` intentionally allows incomplete drafts without touching published assets. Gradients are simple color-stop arrays, not a new HDR LUT/pipeline. Rendering and postprocessing stay with SpectraFX and PRISMA respectively.

## Tests actually run and results

- Source changes committed through GitHub connector; no local Node/Vite execution in this container. **Typecheck, `npm run test:graphs`, Vite, full Studio CI, macOS WKWebView and GPU frame checks are UNVERIFIED for Stage 7 HEAD.** At documentation time GitHub run IDs for code SHA `f989e3d...`: CI `45215622773`, Studio `45215622662`, focused SpectraFX UI `45215622951`, all queued.
- Prior Stage 5 Ubuntu focused Node 22 UI workflow job `137820714704` completed success: 14/14 `test:graphs` tests plus Vite build. **That result predates Stage 6 and Stage 7.** Parent PR #113 Ubuntu focused CTest run `45207729395` previously passed 4/4 native VFX tests; not proof of this UI change.
- Future CI may fail: report exact job, log and SHA, update source and handoff before any merge. Do not mark green based solely on presence of test files.

## Known limitations

- The static SVG scalar chart maps a ±100 display range for readability; true curve values are edited numerically and are not clamped to the chart. It is not a particle preview.
- Curve/gradient nodes are edited using their existing `properties.value` arrays. No direct pin drag drawing, Bézier tangents, color space conversion, HDR gradient illumination, undo history, or external animations in this stage.
- Full real VFX preview timeline cannot be provided safely by React alone; native Studio renderer must own its graphics device, compute, GPU fences, retirement and readback. Do not reuse an ImGui-owned rendering context from the wrong thread or fake preview frames.
- No changes to `main` or other agents' Studio branches. The feature branch is stacked, so parent PR #113 must first complete native acceptance before child PR #115 merges.

## Exact next step

1. Run latest exact-sha `npm run typecheck && npm run test:graphs && npm run build` (Node 22); fix any CI diagnostics, then `npm run test:web` and browser UI interactions.
2. Check active Windows/macOS Studio CI and parent PR #113 C++/Metal CPU-GPU parity. Require actual hardware evidence before calling any GPU behavior done.
3. Review C++ V2 Curve/Gradient compile against edited JSON via `fx_graph_validate` with real native StudioSession, including malformed values, alpha and undo/discard; preserve published graph on failure.
4. Implement native render-owned SpectraFX preview bridge in a later isolated stage, with frozen snapshots, safe commands and checked resource retirement. Then add Play/Pause/Step/timeline controls over that approved bridge.
5. Create another dated handoff and update agent docs at each new stage. Never leave a stage without its tested SHA and explicit next action.
