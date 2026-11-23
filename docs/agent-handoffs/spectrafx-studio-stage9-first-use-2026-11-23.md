# SpectraStudio Stage 9 — discoverable first-use Graph v2 flow (2026-11-23)

## Provenance
- Repo: `chromealex/AIVERSE`
- Branch: `feature/spectrafx-html5-data-interfaces-20261123`, draft PR #115, stacked on draft PR #113.
- Most recent source SHA before handoff: `38e0df4ee3528835bb92195dbc66ed0a4ed830d2`; the handoff commit advances HEAD.
- Do not touch `main` or other agents' Studio branches.

## User evidence and problem
Tester launched native AIVERSE Studio on a Mac and supplied a 1773×829 screenshot showing:
- Scene viewport displayed the actual native 3D map, objects and camera gizmo;
- `SpectraFX Graph` was docked narrowly (~420 px) to the left;
- the panel said `No matching graphs` / `Choose a Graph v2`, with no graph loaded;
- the `New Graph` creation form was hidden BELOW the independently scrolling `Graph Assets` section due to the max-height 230 px responsive rule. Nothing could be previewed without a graph.
This is a **visibility/first-use issue**, not proof of a VFX renderer failure. The same user's earlier Metal CTest `studio_spectrafx_preview` finished 1/1 PASS in 8.19s, but the screenshot does not show particle pixels.

## Changes implemented
1. `apps/studio-web/src/panels/SpectraDataInterfacesPanel.tsx`
   - Surfaced **Starter FX** in always-visible panel actions and in a centered no-graph welcome state.
   - Moved the New Graph form ABOVE the Graph Assets list in the left library.
   - Added **Blank V2** for advanced graph authors.
   - Prevents silent overwrite if a published graph or draft already has the chosen name.
   - Preserves the existing Save Draft/Publish validation ownership and unsaved-change guard.
   - When creating a starter, calls native `fx_graph_edit {action:"new"}`, then **native `fx_graph_validate`**; only successful compiler output is shown as ready.
   - Clears search after creation and offers a distinct next unused name.
2. `apps/studio-web/src/panels/spectraStarterModel.ts`
   - **Native schema is authoritative:** `spectra::authoring::newGraphV2()` ALREADY emits a working `Spawn -> Initialize -> SpriteOutput` graph. Browser never tries to duplicate or replace that format. The first local draft uses the original native result.
   - `createBlankGraph` returns an immutable copy with empty node/link/output arrays, retaining future-compatible other metadata.
   - `nextUnusedGraphName` generates deterministic, bounded, safe names with no collision.
3. `apps/studio-web/src/panels/SpectraDataInterfacesPanel.css`
   - Responsive first-use actions and clean welcome state; at <=570 px dock widths, top library gets 300 px max height, asset list is capped to 110 px, and creation stays above the list.
4. `apps/studio-web/tests/spectraStarterModel.test.mjs` and `apps/studio-web/package.json`
   - New contract tests for native graph pass-through, blank-v2 isolation/immutability and unique names.
   - Included in `npm run test:graphs`.

## Actual tests/results
- Source changes committed via GitHub connector; static inspection confirmed the native `newGraphV2` factory returns a playable 3-node graph.
- **NOT EXECUTED:** current-source TypeScript typecheck, Node22 test:graphs, Vite build, interactive WKWebView UI visual check or real graph playback after the new first-run change.
- The earlier user's macOS `studio_spectrafx_preview` PASS 1/1 is from the prior revision, not Stage 9. Do not label Stage 9 QA green yet.
- No direct file writes, GPU resource ownership or gameplay/ECS state mutation added to the browser.

## Next exact step
1. Check exact HEAD of draft PR #115, inspect queued GitHub Actions:
   - `cd apps/studio-web && npm run typecheck && npm run test:graphs && npm run build`
   - Fix any exact-sha CI failures; don't infer pass without logs.
2. On Mac:
   ```sh
   cd ~/Projects/AIVERSE-spectrafx-test
   git status --short
   git fetch origin
   git switch --detach origin/feature/spectrafx-html5-data-interfaces-20261123
   cd apps/studio-web
   npm install --no-audit --no-fund
   npm run typecheck && npm run test:graphs && npm run build
   cd ../..
   cmake --build build/spectrafx-preview --target aiverse_studio -j6
   ```
3. Open Studio and verify **Starter FX visible without scrolling** with SpectraFX Graph dock at 420 px; click Starter FX, verify visible nodes, native compiler validation, Open Scene -> Load graph -> Play. Screenshot Scene with actual particles, backend indicator and dock panel. Then Save Draft and reopen to verify persistence.
4. Validate on Windows WebView2/Vulkan and responsive resize. Treat the screenshot as real UI acceptance only when captured, not as a generated mockup.
5. Create a separate handoff for any further stage, including new SHA, changed files, real tests and next action.
