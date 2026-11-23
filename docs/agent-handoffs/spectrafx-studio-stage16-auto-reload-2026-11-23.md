# SpectraStudio Stage 16 — opt-in live Graph v2 recompile

Date: 2026-11-23
Repo: chromealex/AIVERSE
Branch: feature/spectrafx-html5-data-interfaces-20261123
PR: #115, stacked draft on #113
Source code SHA at this handoff: eb5a6cd0206aab3bea8a0fae7ab2366982ba0417
Current branch HEAD may be later due to Stage 17; always query exact HEAD.
Main and unrelated agent branches were not modified.

## Goal

Add live authoring preview for changes to Graph v2 without altering
published assets. User explicitly postponed builds and tests until
functional development is done.

## Implementation

`apps/studio-web/src/panels/SpectraNativePreview.tsx`:

- Adds **Auto-reload graph** (opt-in, OFF by default).
- After a successful explicit Load graph, remembers its immutable
  authored document JSON signature. Graph edits from the node canvas,
  typed Curve/Gradient editor, parameters or Data Interface inspector
  change this signature. They schedule a 650ms debounce, avoiding
  recompilation on every pointer update.
- The native `studio.spectraPreview {action:"load",graph}` path still
  performs strict Graph v2 compiler validation on the Studio render
  thread before touching an existing runtime. Invalid compiler input
  does **not** replace the last-good preview. Load returns actual
  native backend and diagnostics.
- If the graph was playing and the recompilation succeeds, the native
  `play` command resumes it. This is a reset/restart of preview time
  (not an incremental live patch of GPU shader structures).
- Runtime preview edits are in-memory only, never `fx_graph_set`,
  `fx_graph_draft_set` or Publish; the user controls persistence.
- Uses a last-loaded signature, a serialized reload-in-flight guard,
  an opt-in toggle and mount cleanup. Later graph edits during an
  in-flight compile trigger another evaluation after the native call
  resolves. Errors stay visible, never silently claim the new graph
  was accepted.
- Unmount/project switching resets preview editor state and cancels
  queued debounce. The native effect lifecycle remains independently
  owned by StudioScene and its gfx::Device.

## Test status and limitations

- **TESTING DEFERRED per explicit user request.** No Node, TS, Vite,
  native C++/Metal, Windows/Vulkan, browser interaction or MCP
  smoke was performed on this Stage 16 source.
- Source was committed through GitHub connector; this is not a
  claim of running UI acceptance.
- Only compiler-invalid graphs guarantee last-good preservation.
  An otherwise valid graph whose new GPU backend cannot be initialized
  may lose the prior runtime because the existing GraphRuntime load
  currently shuts down the former owner before attempting init.
  Fixing this requires a separate candidate runtime + GPU resource
  retirement lifecycle, not an unsafe browser-side swap.
- Auto-reload does not compile a new effect until an explicit first
  Load graph succeeds and the toggle is ON.
- Effects with required Data Interface providers still require a
  valid native provider/instance snapshot. Auto-reload never fabricates
  provider values or gameplay events.

## Next action

Continue Stage 17 — bounded on-demand Trigger Burst and Clear Particles.
Maintain a separate handoff. Once features are declared finished, run
exact-SHA `npm run typecheck && npm run test:graphs && npm run build`,
native Studio and Metal CTests, then interactive WKWebView visual QA.
