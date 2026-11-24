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

## Direct asset dependencies (S6g.2)
The same native graph reads glTF buffers[].uri and images[].uri as local relative resource edges; embedded data, remote URLs and unsupported URIs are not resolved. Project scan is bounded and never opens the referenced target. Existing studio.assetReferences accepts an optional indexed content-relative path and returns matching incoming/outgoing edges, including folder descendants. Assets Find References shows direct dependencies separately from heuristic text mentions. Known glTF resource edges are read-only: move preview and the shared history/delete fence conservatively refuse moving either endpoint, even if a coordinated folder move could preserve the URI. No automatic repair, GLB dependency parser or completeness guarantee.

## Multi-hop dependency inspection (S6g.3)
Optional studio.assetReferences {path,depth:1..16} returns incoming/outgoing edge rows with shortest hop and cycle-closing edge marks; omitted depth retains direct/full graph contract. Assets supports folder queries, direction tabs, 50-row display pages and indexed endpoint navigation. Native transfer owner checks actual final paths (including keep-both) and individual undo/redo: known glTF URI resolution must remain unchanged and no previously unresolved destination path may be adopted. Preview proves only a single conflict-free relocation; batch history/deletion stay conservative. No URI rewriting or complete dependency analysis.

## Recovery storage budget (S6h.1)
Asset Trash lists logical bytes of the full recovery store (including unlisted slots), asset checkpoint, asset journals and available filesystem bytes. Project-local versioned studio-recovery-policy.json sets quotaMiB (0 unlimited, default compatible); explicit Studio save compares prior quota under asset-history lock. Positive net retained growth on quarantine/replacement and coordinated history is refused above a configured quota. Net-reducing restore remains available; no automatic purge. Staging peak, other caches, record-only overhead and noncooperating writers are not hard limited. Pending coordinated journal blocks manual recovery actions. Keep explicit single-record digest/token/confirmation purge semantics.

## History-protected cleanup (S6h.2)
Trash rows report logical payload and complete slot bytes (folders included). Batch review accepts 1–50 distinct slot IDs and refuses every slot mentioned by retained Undo/Redo, including redo tail. It binds canonical project, history epoch/version, sorted IDs, exact reviewed record and payload digest, and byte totals. Confirmed apply revalidates the full set and each slot, reports removed IDs and first failure; irreversible cleanup is not an atomic transaction. Single-slot restore/purge retains its existing explicit history-invalidating behavior. Staging checks available logical source bytes plus 16 MiB reserve before each native staged import/replacement; not a physical reservation or full peak-disk guarantee.

## Linked dependency and recovery tools (S6i.1)
Use authenticated Studio MCP studio_asset_history to obtain project/version, catalog for GUIDs, studio_asset_dependencies for paged known edges (optional depth 1–16). Recovery storage and Trash list are read-only. studio_asset_delete is reversible and requires existing GUID, current version and confirmed=true; native dependency checks remain authoritative. Restore uses studio_asset_trash_restore_review then studio_asset_trash_restore with exact id/token/version and confirmed=true; this clears history. Quota setter requires expectedMiB. Cleanup review is read-only; permanent purge stays in native UI. Do not expose external picker paths, arbitrary browser RPC or claim untracked restore/quota changes are undoable.

## Linked conversation history navigation (S6i.2)
Registry exposes studioLinkOnly independently of guiOnly. Use studio_agent_history_list to inspect actual retained conversation links and exact owner versions; studio_agent_history_step requires scope/id/direction/version/confirmation. Batch preview/apply reuse the same owner coordinator and token. Hosted agent must be stopped; external authenticated client may navigate, no arbitrary owner jump or new edit row is synthesized. Successful navigation preserves original links for redo; failures that change owners fence batch. Standalone audit checks explicit link-only metadata and actual fallback refusal for listed Studio tools. No claim of complete runtime parity without built tests.

## Model format coverage (S6g.4)
The existing model resource graph now scans GLB v2 JSON chunks without reading BIN chunks, and Wavefront OBJ mtllib + supported MTL map slots using the engine's OBJ-relative resolution. Opaque non-GLB placeholders remain outside coverage. Model-resource preview rows identify final native validation rather than treating every multi-selection as an unconditional blocker. Native owner still rejects any changed supported URI resolution before journal publication; no source URI rewrite or arbitrary format coverage is promised.

## Dependency coverage and paging (S6g.5)
Existing XML parser and shared BOM conversion discover COLLADA library_images init_from/ref file candidates (conservative even if unused). Native studio.assetDependencyPage returns at most 100 selected rows, direction counts, coverage diagnostics and graph token. The token hashes known selected edges and scan counts; pass it when changing pages, refresh on mismatch. Existing array endpoint is retained. Linked studio_asset_dependencies supports expectedToken and direction with depth, retains direct mode without depth. Dynamic references, opaque/unsupported formats and skipped URIs are not complete coverage; no URI repair in this stage.

## Transactional stationary glTF URI repair (S6g.6.1)
Internal Studio/MCP moves can repair exact buffers/images URI slots in stationary textual .gltf models when targets relocate. Native plan validates complete choices, budgets exact before/after bytes, and uses move journal v4 plus existing document journal; individual Undo/Redo validates typed resolution and restores exact source bytes. Moving model owners requiring edits, GLB/OBJ/DAE rewrites, coordinated repaired history and unknown URI adoption remain refused. No arbitrary JSON rewriting. Legacy direct transfer callers must opt in and provide history.

## Moved glTF owner repair (S6g.6.2)
A textual glTF owner can move directly or under a moved directory while URI targets are reprojected. Exact original document paths are retained and translated on replay. Move file/tree digests have a precomputed optional after digest. Journal v5 locates repaired files at either endpoint before reversing relocation; v4 remains readable. Individual history checks both directional digests. GLB/OBJ/DAE edits and coordinated repaired replay remain separate work.

## Binary GLB repair (S6g.6.3)
Native URI repair supports bounded .glb snapshots: validate GLB v2 headers and chunk table, rewrite only JSON, preserve exact following chunk headers/payload/padding. Snapshot history and v6 move journal encode binary bytes as hex and restore exact originals; legacy v1–v5 journals remain readable. Existing 4 MiB input / 8 MiB paired repair budget deliberately rejects large models needing rewrites; no streaming repair or external backup storage claim. UI/MCP reuse existing internal transfer path.

## Wavefront and COLLADA text repairs (S6g.6.4)
OBJ mtllib and renderer-supported MTL texture slots are repaired with exact line spans, preserving options, whitespace and line endings. MTL paths resolve relative to each consuming OBJ; incompatible shared-library demands refuse the entire move. DAE library_images init_from/ref leaf text is patched using existing XML parser source offsets, preserving unrelated XML. Round-trippable UTF-16 LE/BE is preserved, including BOM. Mixed-markup leaf rewrites remain refused. Original bytes and path-aware digests use journal v7 and existing history; other binary/dynamic formats remain outside repair coverage.

## Streaming GLB repair (S6g.6.5)
Binary GLB above 1 MiB uses exact before/after JSON prefixes, tail length and SHA-256. Stream tail in 64 KiB blocks; validate chunk structure, baseline and staged bytes before atomic replacement. Journal v8 retains compact snapshots and rolls back at the actual resource path. Existing small/full history stays readable. Aggregate 4 MiB prefix/text per direction, 32 documents and GLB uint32 total length remain limits. Disk staging requires a temporary full output file (no retained geometry copies); check free space before rename and publication, no hard reservation. Undo refuses changed tail. Generic project file journal refuses compact edits.

## Ordinary move-chain coordination (S6f.3.1)
An agent batch made of ordinary asset moves can now compose exact-endpoint chains (A→B→C) from retained file/sidecar identities before mutation. Stationary resource snapshots are joined by exact intermediate bytes and checked against the net projected graph; compact GLB tails remain verified. Coordinated journal v2 serializes binary-safe snapshots and streams compact resources during publication/rollback. Existing v1 reading remains. Nested endpoints, ambiguous cycles, moved-owner byte repairs, mixed overlapping Trash/replacement/metadata and interleaved map/rules changes remain fenced. No new MCP action; linked batch preview/apply use the same native path.

## Moving-owner chain coordination (S6f.3.2)
Ordinary asset chains now simulate each recorded directional digest using virtual repair overlays on initial file/tree identities. Resource edits follow physical identity across exact-endpoint folder/file moves and compose exact intermediate bytes. Validate the net projected graph before publication. Journal v3 relocates initial bytes, publishes final stationary and moved resources once, and verifies normalized original payload identities during recovery before reversing moves. Includes compact GLB. Nested relocation endpoints, ambiguous cycles, mixed dependent Trash/replacement/metadata, and interleaved map/rules changes remain fenced.

## Mixed file-chain coordination (S6f.3.3)
File-only mixed chains of texture/audio/.bin assets support import, ordinary move, metadata settings, replacement and reversible deletion/restoration. Virtual file/sidecar identity and GUID occupancy are validated in order; final metadata edits follow exact content or Trash sidecar endpoints, journal v3 retains those snapshots. Exact Trash asset.meta targets are allowed by the existing-document journal, not general cache writes. Quota arithmetic follows virtual source sizes. Model topology, model URI repairs in mixed chains, nested directory endpoints and ambiguous repeated states remain fenced; disjoint legacy operations retain their previous path.

## Mixed directory and embedded model chains (S6f.3.4a)
Mixed exact-endpoint directory moves/quarantine and self-contained glTF/GLB versions use virtual tree digests and nested GUID/importer checks. Both live and recovery payloads must contain only supported textures/audio/BIN or glTF 2.0 models with embedded resources and no declared extensions. Incoming known model references remain fenced. Nested relocation endpoints, nested Trash metadata edits, external-resource model transitions and mixed URI repairs remain unsupported. No new journal version or MCP action.

## Stable external model versions (S6f.3.4b.1)
Mixed replacement Undo/Redo accepts glTF/GLB local buffer/image URIs only if their logical content target is a safe indexed regular file with a live GUID and no overlapping relocation endpoint anywhere in the chain. Inspect both versions, including recovery bytes. External model relocations, dependency changes across relocation endpoints, extension topology and mixed URI repair remain unsupported. Do not bypass this fence via the existing ordinary repair planner.

## Projected local model dependencies (S6f.3.4b.2a)
Supersedes the stable-only version boundary above: resolve local glTF/GLB resource slots at every complete logical step using virtual live GUIDs and physical payload occupancy. Same model bytes must keep the same target GUID as their owner and resources relocate. Unmanaged incoming references, unsupported model formats, extensions, nested endpoints and mixed URI rewrites remain fenced. Existing journal v3 is unchanged.

## Mixed typed URI replay (S6f.3.4b.2b)
Supersedes the previous mixed URI fence for recorded glTF/GLB repairs: resolve physical owners before relocating; compose exact byte snapshots and verify directional digests and unchanged non-URI JSON. Streamed GLB tails retain exact digest/length. Per-slot target GUIDs must remain unchanged for repaired owners. Other repair formats, unknown extensions, nested endpoints and model edits crossing physical Trash boundaries are still fenced. Journal v3 and existing MCP action are reused.

## Coordinated existing-document save (S6f.4a)
Native studio.save with dirty Rules/Manifest prepares detached MapDoc and Project copies, merges exact existing-file snapshots and uses a single ProjectDocumentJournal commit. Existing map/terrain outputs only; new outputs refuse before publication. Live owners swap after commit; map history retains its separate dirty marker/checkpoint and may be fenced on interruption. Save As and standalone Workspace map_save still use their legacy paths. Do not claim global saved cursor atomicity or support for newly created sidecars. No new MCP action.

## Creation-aware Studio save (S6f.4b.1)
Native studio.save and native Save As share detached owner staging and ProjectDocumentJournal v2. Explicit createOnly output is distinct from an empty existing file, restricted to .json/.r16/.splat under maps/ with existing safe parent directories; no-clobber publish and exact-byte rollback are required. Save As refuses occupied new identities/sidecars and includes manifest registration in the same transaction. v1 recovery remains readable. Existing map history dirty marker/checkpoint is still post-commit, asset cursor checkpoint separate; standalone Workspace map_save is unchanged. Creation is save-only, not a reversible history operation. No automatic directory creation or arbitrary file deletion.

## Window menu categories
Maintain one exhaustive panel-to-category mapping in `apps/studio-web/src/windowGroups.ts` whenever registering a new StudioPanel. Window uses the shared nested context menu with alphabetic children and native open-state check marks; do not duplicate open panels into a separate flat list. Preserve workspace switching, docking, floating and Command Palette search. UI-facing labels are English, including Wind and its panel controls. The shell test enforces complete, unique registry coverage.

## Atomic Map checkpoint (S6f.4b.2)
Studio Save/Save As now holds the per-map advisory lock and stages the Map history checkpoint v2 in the same ProjectDocumentJournal transaction as map/terrain and Project documents. v2 uses the exact file hash set; v1 remains readable. Loaded baselines must match before publication, including removed sidecar dependencies. New checkpoint targets are restricted to the exact map-history-<sha256>.json cache filename. Legacy dirty markers fence atomic save. Standalone map_save and Asset history checkpoint are still separate. No full power-loss/OS validation yet.

## Atomic Asset history navigation (S6f.4b.3a)
Individual and coordinated Asset Undo/Redo use outer studio-history-batch journal v4, with a verified existing Asset checkpoint, retained before image, and transaction-bound after image published before the outer commit. Metadata-only navigation uses a zero-relocation v4 batch. Hold the cooperating asset-history lock and refuse stale checkpoint or dirty marker. Recovery restores the checkpoint and payload; v1–v3 journals remain readable. New asset mutations still use legacy dirty-marker recording; do not claim full atomicity of imports/delete/move recording. No native or fault-injection test execution yet.

## Atomic recording of staged Asset mutations (S6f.4b.3b.1)
Metadata patch, folder creation, reversible deletion, external import and replacement prepare detached history and payload operations, then publish via paired-cursor outer journal v4/v5. v5 distinguishes absent checkpoint from empty existing bytes; recovery may remove a first checkpoint only after a valid readiness marker and exact after bytes. Do not bypass the owner. Internal move/rename with material repair and explicit Trash operations retain the legacy dirty-marker path. Private staged recovery records may survive rejection; never auto-purge. No new MCP endpoint.

## Atomic internal Asset transfer (S6f.4b.3b.2)
Use staged StudioTransferPlan::apply with no live commit callback for internal Move/Rename as well as external transfers. Resolve actual conflict decisions first; prepare typed URI edits, exact material snapshots (preserve unrelated document fields), GUID and tree digests through the existing ordinary chain planner. Recheck actual endpoints against map/rules before journal v4/v5 publication with the detached Asset cursor. Refresh disposable index and material view only after commit. Oversized/unrecordable internal moves refuse before mutation; legacy direct callers retain their prior path. Explicit Trash restore/purge and batch cleanup remain separate. No new MCP endpoint.

## Transactional Trash restore and protected cleanup (S6h.3a)
Explicit restore uses staged native quarantine/replacement relocation and clears the previous Asset cursor in the same outer journal v4/v5 transaction. Single and batch purge both refuse retained Undo/Redo slots and validate the persistent checkpoint under the asset lock; unprotected purge preserves history. UI disables protected purge and displays the native recoveryReason when available. Purge remains irreversible, with partial failure possible. Legacy dirty markers are fenced, not automatically discarded. No new MCP endpoint or full native verification.

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
