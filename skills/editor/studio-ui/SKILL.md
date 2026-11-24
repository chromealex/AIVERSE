---
name: studio-ui
description: Author HTML5 Studio editor windows using shared Studio UI v1 components and the typed native bridge.
---

# AIVERSE Studio UI — Skill for AI window authors

Use this skill whenever an AI agent **creates, redesigns, or migrates** an AIVERSE HTML5 editor window.

## Approved design, mandatory reference
**First Cinematic Concept (approved by user 2026-11-20)** — dark deep-navy workstation with a dominant real 3D viewport, polished softly raised panels, rich blue highlights, restrained violet secondary accents, clear Hierarchy and ECS Inspector, bottom asset thumbnails, small professional toolbars.

**Not approved:** later, overly flat/strict/minimal mockup. Do not confuse the first concept with a screenshot of functioning game assets.

Read in order:
1. `docs/studio-visual-direction.md` — **what it must look like**, proportions, token reference, window-specific details.
2. `docs/studio-ui-agent-guide.md` — **where it is implemented** and exact generation/testing workflow.
3. `docs/studio-design-system.md` — component API and usable TSX example.
4. `apps/studio-web/AGENTS.md` — required frontend and engine boundaries.
5. Current `docs/handoff/studio-html5/` stage before editing.

## File map
- Theme and spacing: `apps/studio-web/src/theme/tokens.css` (`--au-*` semantic tokens; preserve legacy `--studio-*` aliases).
- Shared CSS components: `apps/studio-web/src/ui/studio-ui.css` (`.au-*` classes).
- Shared React component exports: `apps/studio-web/src/ui/index.ts` and `StudioUI.tsx`.
- Compatibility styling for current toolbar/dock/Inspector/old windows: `src/ui/editor-shell.css`.
- Panel registry and docking: `src/panels/StudioPanelRenderer.tsx` and `src/nestedDock.ts`.
- Typed native operations: `src/bridge.ts`, `src/types.ts`, and `apps/studio-native/StudioSession.cpp`.
- New panel generator: `tools/studio/new_panel.py`; outputs registered TSX+CSS using Studio UI.

## Generate a new window
```bash
python3 tools/studio/new_panel.py \
  --id environment_tools \
  --title "Environment Tools" \
  --area right --dry-run
python3 tools/studio/new_panel.py \
  --id environment_tools \
  --title "Environment Tools" \
  --area right
```

## Always use the shared library
```tsx
import { RefreshCw } from 'lucide-react';
import {
  StudioPanel, StudioButton, StudioIconButton, StudioSection,
  StudioField, StudioInput, StudioBadge, StudioAlert, StudioEmptyState
} from '../ui';
```

Use `StudioPanel` as the outer shell. Use `.au-stack`, `.au-row`, `.au-grid` for layout; `StudioButton` and `StudioField` for controls; `StudioBadge`, `StudioSkeleton`, `StudioAlert`, `StudioEmptyState` for statuses. Icon-only actions must have an accessible `aria-label`.

Use only `--au-*` tokens for new colors, spacing, radii, typography, focus and hover. New semantic values belong in `tokens.css`, NOT in panel-local CSS. Local CSS is for feature-specific geometry only. Every pane must work at ~240px width and 1440×900 desktop.

## Keep engine behavior truthful
- The native C++ Renderer3D owns Metal Scene pixels. Never use a static image, canvas trick or per-frame bitmap streaming to mimic the conceptual screenshot.
- Do not alter `.viewport`, `.viewport-native`, `html.native-composite` or native GPU view rect as a styling shortcut.
- ECS components and scene object data shown in Inspector must come from real native editor/runtime sources. Unknown/unimplemented UI displays a truthful state instead of invented components.
- Preserve safe typed bridge APIs and existing actions. No browser-side direct project-file writes, arbitrary shell calls, unsafe JSON writes or new ImGui UI.

## World Persistence panel (W1.26b)

Panel ID `world_persistence`, implementation `apps/studio-web/src/panels/WorldPersistencePanel.tsx`, uses the shared UI kit and typed `StudioWorldOps` DTOs via `studio.op`. Check actual `studio.ops` discovery; do not fabricate saves in browser mocks. Backend dependency: Open World branch feature/aiv20261123_open_world_audit (W1.26a, 72560879). Integrate both branches before native acceptance.

Offline maintenance is exclusive, project-scoped and never implicitly stops a server. Every world_save request carries expectedProject and native dispatch checks it against the currently open project. Preserve exact decimal strings for IDs/revisions/generations. Retention deletion requires a fresh preview and explicit confirmation of the exact list. Region map is a projection of occupied ledger cells, not collision residency or 3D rendering. The capacity bar represents ledger records, not RAM. W1.26c runs maintenance via world_job_start/status/cancel/release. Poll actual checkpoint bytes, WAL frames and asset reads; checkpoint progress is not total operation progress. Indeterminate phases remain labelled as such. Cancel only if cancelAllowed. On panel remount discover the retained job slot; never automatically resubmit an ambiguous operation. Native dispatch binds job start to expectedProject; polling/releasing an old project job is allowed by ID.

## Live World Explorer (S1)
Panel ID `world_explorer` uses the hosted Game surface's read-only `studio.gameRegions` method with expectedProject, not an offline maintenance job or a separately simulated world. Refresh explicitly: ledger projection rebuilds stable references and scans live records, so do not add frame polling without a measured bounded native owner. Keep 64-bit counters as strings. Slice regions by Y before the X/Z projection, select by full XYZ, disclose the native row limit and missing data. Materialized counts do not prove physics/nav residency. Simulation LOD diagnostics are actual last decision counters, not per-cell tiers, time, CPU cost, or durable ticks. The legacy editor MCP `world_regions_status` reads its own preview/play simulation through the same shared ledger diagnostics; do not claim it observes the separate hosted Game instance.

## Simulation Profiler (S2)
Panel `simulation_profiler`, typed `studio.gameProfile` with expectedProject and explicit start/status/stop. Uses the existing Simulation timing owner through a bounded capture (1–120 completed steps); does not overwrite an active headless clock. Tick timings include command application and ECS flush, exclude readiness check and rendering; per-system timings include their flush. Results are wall time, not CPU thread utilization. Poll only the bounded result, never infer per-system entity counts or simulation load from LOD. Paused matches do not advance the sample; stop retains partial results. Captures are local, unsaved, and lost with the Simulation. No network profiling or remote MCP claim.

## Entity Debugger (S3)
Extend the existing `runtime` panel and `StudioRuntimeInspect`; no competing inspector. `studio.gameEntities` supports exact `sid:<decimal>`, `ref:<decimal>`, `cell:<x,y,z>` alongside the existing shared query grammar. Stable IDs and ownership revisions are decimal strings; cells come from the live ledger, not scene position. Only rows in the existing runtime model are searchable; absence is not a retired/unmaterialized classification. Ownership markers are diagnostic, not proof of full identity uniqueness or collision/nav residency. Preserve legacy numeric $entity and prefer additive $entityRef string when navigating; reject unsafe numeric fallback. Bind requests to expectedProject and selection epoch, clear on project change. Native component readers/schemas stay authoritative; list component counts refer to the existing row catalogue, selected inspector includes extensions. Read only; no runtime editing or persistence mutation.

## Runtime Debugger (S4)
The existing Runtime panel exposes only owner-approved Unit HP (strictly positive, no resurrection/death shortcut) and finite configured Energy, via `studio.gameEdit`. Native requires editor-paused controllable local play, exact expectedProject/epoch/ref/tick/previous fixed raw, current domain bounds, live eligible non-dormant on-map entity; decimal input is bounded before fixed-point conversion. Shared UnitVitals setters also serve existing scripts. No arbitrary component patches, transform/ownership edits, network/MCP mutation, scene changes or deterministic replay command. Other schemas remain read-only until owners supply explicit adapters. Session is marked debug-modified. Stop/restart discards temporary session data; ordinary Keep Play Changes remains a separate explicit authoring workflow.

## Play Mode review (S5)
Keep the existing HostedClient and independent scene document. Play requires saved project/map; Restart revalidates the saved launch mode. Review is paused local saved-map only, checks project/path/revision and actual match map index. Native preview calculates exact category counts without editing; native one-shot token binds captured entity states, epoch, tick and selected options. Apply rechecks the native snapshot and source, then commits the reviewed categories as one MapDoc History step. No browser-supplied rows, no direct title-bar keep shortcut, no automatic keep on Stop. Delete is explicitly opt-in. A successful change invalidates the launch revision; save/restart for another review. No transfer of runtime-spawned entities, HP/Energy, arbitrary components or remote worlds.

## Map history transactions (S6a)
Use the existing MapDoc owner, not a second undo stack. Synchronous compound operations use MapDoc::CompoundEdit; every nested beginEdit marks the transaction but does not push/trim history. Commit publishes one before-image, failure restores the original map/dirty state and preserves previous redo. History labels bind process-unique native entry IDs, not depth. History navigation sends expectedVersion and the panel describes the oldest retained entry, not necessarily the opened map. This is map-scoped, not project/rules/asset filesystem history. Cross-frame brush grouping is still a separate ownership concern; do not claim global transaction parity.

## Cross-frame stroke history (S6b)
One CompoundEdit is owned by the Scene stroke from press to release. No history entry until commit; invalid dab, explicit cancellation, document switch and hidden/detached Scene cancel and restore the before-image. Other authoring requests and linked agent calls are refused during the stroke rather than folded into it. Agent history links resolve retained native IDs, send the current version, and Undo all requires an uninterrupted retained agent-only suffix. This is still map history; never claim reversibility of file or rules mutations.

## Saved project document history (S6c.2)
Project owns a separate bounded history (64 entries / 16 MiB of retained plan payload). Only explicit rules_set/merge/delete and compatible set_manifest saves are labelled; other changed owner saves clear the stack, and rules-source/module changes require reopening. No-op saves preserve redo. Use project_history_status/project_history_step or native studio.projectHistory/studio.projectHistoryStep with direction and exact expectedVersion. Target bytes are composed and validated using the owner loaders before applying the reversible file plan. MapDoc history is not navigated. Asset changes, topology changes, crash durability and cross-scope agent Undo all remain outside coverage. UI integration must provide explicit scope and use one-step navigation, not simulate a multi-file jump by issuing unreviewed commands.

## HTML5 project history (S6c.3)
Use the existing HistoryPanel with explicit Map / Project scope. Map rows keep native map jumps; project rows are non-interactive and navigation uses one owner-authorized step with exact expectedVersion. Never change global map Undo/Redo shortcuts to select a project scope implicitly. Use shared StudioButton/Alert/EmptyState/Skeleton, show native blocked/conflict reasons and retained payload counters. Demo mode is explicitly unavailable for project writes. Snapshot project.historyVersion invalidates saved Rules and Build views; other snapshot-driven panels refresh via document.changed. Native host sends document.changed on stroke begin/end; native stroke guard remains authoritative. Do not claim asset history, a unified timeline, durable recovery or agent cross-scope Undo all.

## Asset importer metadata history (S6c.4a)
Use StudioAssetCatalog::patchSettings and native owner-generated exact .meta plans. GUID, importer, other identity fields and physical path must remain unchanged; reindex existing sidecars without generating unrelated metadata, refuse duplicate GUID or stale bytes. Restore through the catalog owner with index reconciliation and rollback on failure. HistoryPanel has a separate Assets scope (64 edits/16 MiB retained payload), native studio.assetHistory/Step with exact expectedVersion, and assets.changed refreshes Assets and pinned Import Settings. No asset payload or import/move undo yet; transfers invalidate this stack before applying. Do not expose arbitrary paths or merge the three timelines. Agent map Undo all remains conservative.

## Bounded internal asset move history (S6c.4b)
Use StudioTransferPlan::apply with optional native MovePlan capture; record actual final paths (including keep-both), exact bytes for regular files and their existing sidecars, and materials.json before/after. Undo/redo reuses StudioMoveJournal and the catalog owner, validates exact source bytes, GUID/path/importer, vacant destinations, matching material baseline and known read-only/current map/rules path references. No request-supplied move plans. Folder, oversized, unsupported topology or failed transfers are barriers, not silently undoable. No unified history or new standalone MCP endpoint. Do not claim complete reference graph, arbitrary rename UI, import/delete recovery or durable recovery beyond the existing move journal.

## Folder and large-file move history (S6c.4b.2)
Reuse pack::Sha256 for streaming regular-file / deterministic folder tree digests (relative paths, entry type, size, file bytes; includes empty directories and child sidecars). Retain exact root sidecar/material bytes, not payload copies. Rename uses existing preview/decision/commit route with one validated filename; file extension unchanged. Native map/rules reference fence includes current map, manifest-listed maps and bounded saved JSON maps under maps/; refusal rather than rewriting unsupported dependencies. Keep folder/payload limits and known script/encoded/binary dependency gaps explicit. Do not treat SHA-256 as a lock against concurrent external writers.

## Asset soft deletion (S6c.4c.1)
Use native studio.assetDelete with current project, exact asset history version, path/GUID and explicit confirmation. The owner moves an asset and its root sidecar to a unique .aiverse-cache/studio-asset-trash record using journal v2 restricted to content/trash pairs. Folder trees use streaming digests. Undo/redo uses the same owner and refuses conflicts; deletion/redo checks known map/rules/material references. Trash payloads intentionally survive history reset/eviction/reopen; no permanent purge or automatic cleanup. Import/replace and trash recovery UI remain separate work.

## Staged imports and folder creation (S6c.4c.2)
Use the existing transfer preview and native picker. For non-replacing imports with history, stage into private project-local recovery slots outside live content; generate metadata with the existing catalog at the final filename, retain preserved sidecars only after GUID validation, sync payloads before publication, and publish the batch through journal v2. History created=true reverses quarantine direction: Undo removes, Redo restores staged bytes and exact sidecars without external source. Folder creation uses the same owner. Undo refuses known map/rules/material references. Explicit replacement remains a history barrier. Staged orphan records are retained for recovery; no automatic purge.

## Reversible asset replacement (S6c.4c.3)
Replace only a reviewed external regular-file name conflict. Preview binds source/target SHA-256, sidecar hash and target GUID; commit refuses changed identity. Stage incoming payload into a project recovery slot; journal v3 records paired live-to-outgoing and incoming-to-live moves with digests. Restore validates all chains and occupancy phases before any rollback. Preserve exact live sidecar and target GUID, never swap metadata. Asset history replacement steps toggle old/new payload without original picker source. Replacement-only batches are supported; mixed new-import/replacement selection must be split (UI and native refusal). Do not purge inactive payloads or promise cross-session undo.

## Persistent asset recovery browser (S6c.4c.4)
Assets toolbar Trash opens AssetTrashDialog. Native assetTrashList returns paged persisted records with retained/invalid status, assetTrashReview validates the selected payload and produces an action-bound identity token, assetTrashApply requires exact project/history version, token and explicit confirmation. Restore deleted/created content only at its original vacant path/GUID through quarantine replay; restore a previous replacement version only at its matching live GUID/path, preserving current metadata and saving outgoing payload as a new recovery record. Purge only the reviewed single recovery slot, never live content; pending journals block mutations, unexpected slot contents refuse. Both operations invalidate session-local asset history; no automatic purge. Legacy records without metadata digest are identity-checked but lack original sidecar hash; new records include it. Do not claim persistent undo stack, disk quota enforcement or full recovery of invalid records.

## Durable project document transactions (S6c.5)
All existing-file ProjectFileEdits operations use one project-local journal in .aiverse-cache/project-document-journal with a stable advisory lock, bounded immutable before/after snapshots and SHA-256 manifest. Publish only after pending marker and snapshots are flushed; commit marker selects the new bytes. Recovery validates every dependency, path, permissions and snapshot before rollback, refuses unknown external bytes, and retains the pending journal for inspection. Project::load recovers before parsing; Studio resolves the inner document journal before outer asset move recovery. Project/asset history version tokens include a per-owner epoch; do not reuse stale tokens after restart. POSIX fsync/F_FULLFSYNC is used; Windows file flush/write-through does not establish equivalent directory durability without testing. Existing files only, not a global project save transaction or a lock against non-cooperating external writers.

## Agent owner history (S6d.1)
Record linked agent actions by native map ID or ProjectHistory entry ID, scope and source document. Project/asset entries have stable epoch-qualified IDs distinct from ordinal positions. Resolve current boundary and version on every navigation; studio.agentHistoryStep verifies membership, scope, direction and version before calling the same native owner. studio.agentUndoAll rechecks map-only agent suffix, never navigates a mixed project/assets run. Agent panel displays untracked/evicted edits as unavailable, not undoable. Four explicit linked Studio asset adapters are discoverable through MCP; local fallback fails closed. No arbitrary Studio RPC forwarding, destructive asset/purge exposure or cross-scope atomic undo. See docs/mcp-agent-link.md.

## Agent MCP capability audit and internal moves (S6d.2)
Use StudioAgentHistoryPolicy.h explicit policy, not suffix-based StudioToolReadOnly for agent history. studio_history_capabilities enumerates the entire live registry; unverified operations can execute but fence map-only bulk undo and disclose untracked effects even if a partial owner link exists. Agent internal move/rename requires project and asset version, existing GUIDs, independent preview token and explicit decisions. Native transfer owner revalidates dependencies and preserves GUID. No destructive/internal replace, external import or arbitrary Studio RPC. Do not claim the entire registry is audited for reversible side effects; unverified is an intentional fail-closed classification.

## Persistent Project history (S6e.1)
ProjectHistoryPersistence serializes bounded document-only steps and cursor into .aiverse-cache/project-history.json with a checksum, restricted owner paths and exact current baseline. Project::applyHistoryTransaction stages the checkpoint alongside Rules/Manifest in the existing document journal; no-op edits do not change the checkpoint. Reopen revalidates dependency sets, continuous before/after bytes and cursor, creates a fresh epoch, and reports persistenceWarning if recovery is unavailable. Do not reuse old tokens or claim Map/Assets timelines persist yet. No cross-owner atomic navigation.

## Persistent Assets history (S6e.2)
Native StudioAssetHistoryPublication holds a per-project advisory lock and writes a durable dirty marker before tracked asset mutations. After the existing asset/file journal commits, serialize typed metadata and move/quarantine/create/replacement plans and cursor into .aiverse-cache/asset-history.json. Validate checksum, bounded plan shape, current full content tree/material digest and referenced recovery-slot trees on reopen; renew owner epoch. Rejected operations can retire the dirty marker only if exact checkpoint and filesystem state still match. Interrupted checkpoint publication intentionally disables cross-session replay rather than attempting to reconstruct an uncertain cursor. Payloads remain in Trash. This is not one atomic transaction with the asset journal: a crash in the post-commit checkpoint window can discard saved history. Hashing is synchronous and bounded to 20k entries per tree. No background storage cleanup.

## Persistent Map history (S6e.3)
MapDoc persists its undo and redo snapshots and native labels at successful map save boundaries, not on each brush dab. MapHistoryPersistence uses an editor-only exact MapData codec (little-endian hex layers, float bit patterns, player/entity/road state, six-face planet layers, extras) with a checksummed bounded checkpoint per map in the map directory's .aiverse-cache. The baseline includes saved map bytes and decoded terrain sidecars. A per-map advisory lock and durable dirty marker fence interrupted saves; restore only when the exact saved baseline matches, with fresh history IDs and epoch. Save As starts an independent timeline and clears body sidecar names. Invalid checkpoint leaves the saved map open and reports a warning. Unsaved edits are deliberately not restored; legacy map and sidecar writes are not an atomic multi-file transaction. Keep this distinction explicit.

## Coordinated history (S6f.1)
StudioHistoryCoordinator validates exact ordered owner boundaries (max 64) and merges consecutive existing-file byte transitions. Agent batch preview binds all three history versions and step identities; intervening changes, untracked calls, failed potential mutations, partial application and unsupported move plans refuse. Apply stages MapDoc and Project copies, validates metadata identity and all file baselines, publishes project documents, project checkpoint and metadata in ONE ProjectDocumentJournal transaction, then swaps staged live owners. Asset checkpoint uses existing post-commit marker; map edits remain unsaved. Do not call this a durable global transaction across saved map/asset timeline or claim support for file moves, imports, deletes, replacements. Keep legacy map-only undo and individual history actions separate.

## Coordinated asset relocations (S6f.2)
Native replay owners expose a staging-only path for internal moves, quarantine (create/import/delete) and replacement. Agent coordinator accepts disjoint relocation groups and validates their references against live and target map/project state; overlapping endpoints or document paths refuse before publication. studio-history-batch journal stores exact existing-file before/after snapshots and typed relocation digests, checks a unique prefix state and reverses pending moves and documents after recovering the nested document journal. The outer commit marker is authoritative. Recovery is called under the cooperating asset-history lock at Studio open; legacy Project::load refuses pending outer journal. Asset checkpoint still follows commit under its dirty marker; map changes remain unsaved. Do not claim arbitrary dependent path chains, all MCP asset operations, or atomic saved map/asset cursor recovery.

## Map reference discovery (S6g.1)
Use studioCollectProjectAssetReferences to extend the same native material/rules graph with read-only exact map path candidates and escaped JSON pointers from the current map and bounded saved maps. The shared saved-map visitor excludes history caches; explicit missing content paths have no GUID. Move preview (including linked MCP preview) displays blocking references before commit; commit retains its independent conservative reference fence. No arbitrary script/binary analysis or automatic map repair. The native graph query indexes existing sidecars only.

## Verification and mandatory handoff
```bash
cd apps/studio-web
npm run typecheck && npm run test:web && npm run build
cd ../..
python3 -m unittest discover -s tools/studio -p 'test_*.py' -v
# Only when native integration changes, on macOS:
bash tools/studio/verify_mac.sh
```

Inspect real HTML5/WKWebView screenshots in desktop and narrow docks; check visible hover, focus, disabled, loading, empty and error states. Document actual results, commit SHA, touched files and next action in a separate stage file in `docs/handoff/studio-html5/` (named like `01-web-shell.md`) after each stage. Work on `feature/studio-html5`, not `main`; keep legacy ImGui until verified parity.

## Caverns authoring panel (Stage54a)

Panel `caverns` uses `CavernsPanel.tsx` and the existing Workspace operation `terraweave_caverns_paint`. Discover it through `studio.ops`; standalone Studio without the Caverns engine branch must report unavailable. All requests bind expectedProject and expectedMap; mutations also bind exact decimal expectedRevision, and erase uses stampIdText. A coordinate dab is one MapDoc undo step. Explicit refresh after map/history changes; no browser-side SDF, mesh generation or invented telemetry. Generator and entrance records are read-only in this panel. Native 3D brush raycast and GeometryScene preview, paging controls/diagnostics, generator/entrance mutation and combined branch verification are still outstanding. See docs/handoff/studio-html5/caverns-stage54a-2026-11-24.md.

## Caverns Scene brush (Stage54b)

Caverns panel can arm existing studio.sceneBrush with kind=caverns, caveMode carve/fill, caveSurface, caveY (metres), radius (0.25–64 m), expectedProject and expectedMap. The actual camera supplies the ray; native Workspace terraweave_caverns_pick evaluates cached canonical SDF. The explicit depth plane is independent of SDF hits. StudioHost resolves XYZ, draws a three-circle volume cursor, and feeds the existing grouped History stroke. No hit is not a selection or a fallback to an unrelated surface. Alt remains camera orbit. Backend discovery is mandatory; standalone Studio lacks the Caverns engine ops until integration. Native preview geometry, generator/entrance mutation, telemetry, full agent-stroke isolation and same-path document fencing remain outstanding. See docs/handoff/studio-html5/caverns-stage54b-2026-11-24.md; no native or frontend validation performed.
