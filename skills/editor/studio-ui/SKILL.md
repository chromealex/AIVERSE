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
