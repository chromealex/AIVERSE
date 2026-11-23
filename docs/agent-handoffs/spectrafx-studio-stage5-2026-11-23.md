# SpectraFX Stage 5 — HTML5 Studio Graph v2 Data Interfaces editor

**Date:** 2026-11-23
**Repository:** chromealex/AIVERSE
**Head branch:** `feature/spectrafx-html5-data-interfaces-20261123` (draft stacked PR #115)
**Parent branch:** `feature/spectrafx-significance-data-20261123` (draft PR #113)
**Created from parent SHA:** `e81a0c6f74d5855e0671ab530a9e2e9f675f2714`.
**Source HEAD before handoff:** `26d5e51a927dcfa6a4276db097b662f3443ca03e` (refresh after doc commit).
**Main:** untouched; do not merge either branch until tests pass.

## Actual implementation

1. **Native Workspace-backed SpectraFX editor.**
   `apps/studio-web/src/panels/SpectraDataInterfacesPanel.tsx` replaces ONLY the `fx_graph` generic ToolPanel render case in `StudioPanelRenderer.tsx`. No changes to other tool panels or the Scene viewport. Existing `StudioSession::studio.op` and whitelisted `fx_graph_list`, `fx_graph_draft_list`, `fx_graph_get`, `fx_graph_draft_get`, `fx_graph_edit`, `fx_graph_validate`, `fx_graph_set`, `fx_graph_draft_set` handle all file IO and compilation. No browser file writes or unapproved native methods. Project-relative draft and published paths remain separate.
2. **Graph selection and draft editing.**
   Published/draft asset list, filter and drag-to-open. Open Graph v2 only (legacy v1 needs the normal native upgrade operation). New graph through native `fx_graph_edit action:new`. Explicit Validate, Save Draft, Publish, Discard with disabled/busy/error states and a confirmation before discarding unsaved edits to switch assets. Validation result `ok:false` is never treated as success; a published asset is never overwritten until native graph compilation succeeds.
3. **Typed Data Interface authoring.**
   `spectraDataInterfaceModel.ts` mirrors the conservative C++ graph-v2 binding constraints: 8 unique scalar instance slots, 8 bindings, safe provider/channel IDs, finite bounds, fallback ranges and nonmutating failures. Parameter create/update/remove, provider/channel/scale/offset/fallback/required form, reusable providers with local search and native-browser drag/drop. Add/remove DataInterface node and block deleting a binding while its node still references it. Provider names denote **runtime host callbacks**, never a file path; the panel does not falsely claim to enumerate live PHYSIS providers.
4. **Real Assets references.**
   The pane reads `studio.assets` (native, content-relative path catalog), filters real PNG/JPEG/TGA assets, lists/searches and accepts drag/drop only for an existing `SpriteOutput` node. `putSpriteTexture` uses ONLY paths in the native Assets list, rejects traversal/backslashes/unsupported extensions, and creates the project-relative `content/<asset>` reference expected by `TextureCache.resolve(name, ..., projectRoot + "/")`. No asset UUIDs are invented; native `fx_graph_validate` is still the authority before Publish.
5. **Approved Studio UI.**
   All new components use `StudioPanel`, `StudioSection`, `StudioField`, `StudioInput`, `StudioSelect`, `StudioButton`, `StudioCheckbox`, `StudioBadge`, `StudioAlert`, `StudioEmptyState`. CSS in `SpectraDataInterfacesPanel.css` uses shared semantic `--au-*` design tokens and responsive dock widths. No fake preview image or replacement viewport.
6. **Tests and CI.**
   `apps/studio-web/tests/spectraDataInterfaces.test.mjs` tests schema/slot bounds, invalid IDs, atomic edits, referenced-node deletion blocking, provider search, and native Asset membership and path validation. `test:graphs` in `apps/studio-web/package.json` now runs these tests. `.github/workflows/spectrafx-studio-ui.yml` runs Node 22 TypeScript, `npm run test:graphs`, and Vite build on Ubuntu. Existing Studio CI pull_request workflow also applies.

## Tests actually executed

- Code changes uploaded via GitHub connector; GitHub compare against parent shows 7 changed paths, no behind commits at source HEAD.
- Native Studio UI, npm typecheck, Node tests, Vite build, macOS WKWebView, Windows WebView2, and hardware GPU preview **have not been observed as passing in this environment**. The GitHub workflow was queued/pending when last checked. Do NOT label GUI acceptance complete until logs and tested SHA exist.
- Parent PR #113 has earlier Ubuntu-focused SpectraFX contracts 2/2 PASS for a **pre-field/pre-data-interface** SHA. That result does not prove Stage 5 or Stage 4.
- Parent broad AIVERSE CI previously failed on independent `engine/voxel` layer violations, GameCore `override` warnings and missing Android plugin staging files. Check exact new logs instead of assuming the failures carry forward.

## Known limitations / cross-team contracts

- This is **typed Data Interfaces + asset reference authoring**, not a complete Niagara-style visual wiring canvas. For arbitrary graph wiring use `fx_graph_edit` via the existing tool API until native/Studio canvas integration is ready.
- No **live GPU particle render preview**. The native Studio host does not yet offer a verified GPU preview frame contract for SpectraFX, and `fx_graph_preview` is legacy GUI-only; validation diagnostics are not a visual preview. Do not fake a particle view or run RHI on the frontend.
- Graph v2 Data Interfaces are **symbolic scalar read-only host callbacks**, sampled at burst time in the parent implementation. PHYSIS/TerraWeave/NEREID must expose immutable presentation snapshots from native adapters; do not add direct `engine/fx -> physis` references.
- Open Graph v1 is intentionally rejected by the new Data Interface pane rather than auto-modified; provide a deliberate Upgrade v1→v2 tool in a future stage.
- Native Assets catalogs are from `content/`, not graph source `fx/graphs/`; these are different asset kinds. Graph selection uses `fx_graph_list`, sprite texture selection uses `studio.assets`.
- Source branch `feature/studio-html5` is already behind/included in current main as of read; this stage operates on a **stacked feature branch** and must not rewrite the other agent's branch.
- Responsive layout needs actual 240px-dock/WebView screenshots; CSS alone is not visual QA. Browser drag/drop + macOS native bridge need interaction testing. Native backend tests should ensure `content/<path>` resolves with the actual host's project root and image decode.

## Exact acceptance steps for next agent

1. Inspect current HEAD of PR #115; `cd apps/studio-web && npm install --no-audit --no-fund && npm run typecheck && npm run test:graphs && npm run build`. Fix all diagnostics; do not claim PASS from queued jobs.
2. Run `npm run test:web` and `tools/studio/verify_headless.sh`; inspect asset list, new graph and draft/publish with a native StudioSession project.
3. Capture macOS WKWebView screens in wide (1440×900) and narrow (240px dock) layouts; check error, disabled, empty/loading, drag/drop, pin, undo/discard and no unwanted graph overwrite.
4. Write StudioSession-backed tests for `fx_graph_set`, `fx_graph_draft_set`, `fx_graph_validate` via studio.op, including invalid provider and texture references.
5. Implement graph-v2 visual node canvas and native GPU preview **as a new stage** with safe RHI resource lifetime ownership. Include Metal/CPU twin parity and a distinct new handoff.
6. Merge parent PR #113 only after full engine acceptance. Then rebase PR #115 with lease verification; do not merge child ahead of unverified parent.

## Agent handoff discipline

After each next stage create a separate dated `docs/agent-handoffs` document, listing branch/SHA, actual test outcomes, APIs, changed files, limitations and exact next action. Update `docs/agents/spectrafx.md`, `skills/visuals/aiverse-spectrafx/SKILL.md` and Studio agent guide accordingly.
