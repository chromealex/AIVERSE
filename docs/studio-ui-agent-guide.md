> **Latest approved shell revision (2026-11-20):** the user approved the compact lamp-editor mockup from this conversation. It supersedes the older two-row application/workspace toolbar composition below. Use ONE 44px global header with real macOS controls, the actual engine mark at 21px, AIVERSE without STUDIO, project name, Undo/Redo immediately after the project, then Play/Pause/Stop, and right-side project/Panel/settings actions. No separator between traffic lights and logo. Panel tabs belong to local docks. Inspector object header contains only activity checkbox + name; no Tag/Layer/Static, no component icon beside its checkbox. Never invent missing native entity fields or rendered assets. Implementation: `src/ui/compact-shell.css`, App.tsx and native Main.mm. The remaining cinematic palette guidance still applies.

# AIVERSE Studio — AI Agent Implementation Guide

**Required reading for any AI agent building or redesigning editor windows.**
**Approved design:** `docs/studio-visual-direction.md` — **First Cinematic Concept**, chosen by the user on 2026-11-20. Do not use the later strict/minimal mockup instead.
**Working branch:** `feature/studio-html5`. Never push unverified editor replacement changes to `main`.

## Where everything lives

| Path | Purpose | How an agent uses it |
|---|---|---|
| `docs/studio-visual-direction.md` | **What the editor should look like** | Visual goal, screen composition, colors, design do/don't and acceptance checklist |
| `docs/studio-design-system.md` | **Shared UI library/API** | Components, variants, CSS utilities, code example |
| `apps/studio-web/AGENTS.md` | Rules for agents working in this app | Read before editing any HTML5 Studio code |
| `skills/editor/studio-ui/SKILL.md` | Reusable UI authoring workflow | Checklist from new panel to tests/handoff |
| `apps/studio-web/src/theme/tokens.css` | Central theme/source-of-truth variables | Change all semantic colors, spaces, fonts, shadows, radii here |
| `apps/studio-web/src/ui/StudioUI.tsx` | **16 shared React primitives** | Import and compose standard panels/controls |
| `apps/studio-web/src/ui/index.ts` | Public UI kit entrypoint | `import { StudioPanel, ... } from '../ui'` |
| `apps/studio-web/src/ui/studio-ui.css` | Shared `.au-*` CSS | Don't duplicate controls in every panel |
| `apps/studio-web/src/ui/editor-shell.css` | Compatibility skin for current editor shell | Central visual overrides for existing windows, tabs and inspector |
| `apps/studio-web/src/styles.css` | Legacy current shell styles | Gradually migrate; don't fork this per panel |
| `apps/studio-web/src/main.tsx` | CSS import order and app mount | Tokens → shared UI CSS → old CSS → shell adapter |
| `apps/studio-web/src/App.tsx` | Shell, project toolbar and bridge state | Don't add one-off window JSX here |
| `apps/studio-web/src/components/NestedDockWorkspace.tsx` | Dock area renderer | Preserve docking behavior; style in scoped CSS |
| `apps/studio-web/src/nestedDock.ts` | Panel IDs/titles/default docks and persisted tree | Register new panels; do not invent ad-hoc dock models |
| `apps/studio-web/src/panels/StudioPanelRenderer.tsx` | Typed panel render registry | Generated new panel registers here |
| `apps/studio-web/src/panels/` | Existing panel implementations (Lighting, Materials, ShaderGraph etc.) | Migrate to shared UI kit incrementally |
| `apps/studio-web/src/components/PropertyField.tsx` | Validated inspector field control | Use for numeric/string/bool project properties where available |
| `apps/studio-web/src/propertyLogic.ts` | Pure field validation | No empty-number-to-zero or unvalidated writes |
| `apps/studio-web/src/bridge.ts` + `src/types.ts` | Typed browser→native method contract | UI requests only known operations |
| `apps/studio-native/StudioSession.cpp` | Whitelisted native Workspace bridge | Only safe C++ operations modify engine/project data |
| `apps/studio-native/mac/` | WKWebView host and real Metal Renderer3D | Never fake GPU frame in CSS; don't change for ordinary UI |
| `tools/studio/new_panel.py` | Agent panel generator | Creates registered TSX + scoped CSS in the approved library |
| `tools/studio/test_design_system.py` | Library contract tests | Extend when adding shared UI primitives |
| `docs/handoff/studio-html5/` | Stage-by-stage handoffs | Mandatory dated record of change, actual tests and next task |

## Create a new editor window (recommended)

From the repo root:

```bash
git switch feature/studio-html5
python3 tools/studio/new_panel.py \
  --id environment_tools \
  --title "Environment Tools" \
  --area right --dry-run
python3 tools/studio/new_panel.py \
  --id environment_tools \
  --title "Environment Tools" \
  --area right
```

The generator adds `src/panels/generated/EnvironmentToolsPanel.tsx`, its scoped CSS, the typed panel catalog in `nestedDock.ts`, and an entry in `StudioPanelRenderer.tsx`. Do not register the same panel a second time. It creates **UI scaffolding only**, not engine APIs or fictitious assets.

## Correct React composition

File example: `apps/studio-web/src/panels/EnvironmentToolsPanel.tsx` (import path `../ui` because it is directly inside `src/panels`; generated files inside `src/panels/generated` use `../../ui`):

```tsx
import { RefreshCw, FolderOpen } from 'lucide-react';
import {
  StudioPanel, StudioSection, StudioButton, StudioIconButton,
  StudioBadge, StudioField, StudioInput, StudioEmptyState,
  StudioAlert
} from '../ui';

export function EnvironmentToolsPanel({ loaded, onRefresh }: {
  loaded: boolean;
  onRefresh: () => void;
}) {
  return <StudioPanel
    title="Environment Tools"
    eyebrow="WORLD / ENVIRONMENT"
    actions={<StudioIconButton aria-label="Refresh environment"
      icon={<RefreshCw size={15}/>} onClick={onRefresh}/>}
    footer={<StudioBadge tone={loaded ? 'success' : 'neutral'}>
      {loaded ? 'Ready' : 'No project'}
    </StudioBadge>}>
    <div className="au-stack">
      <StudioSection title="Search">
        <StudioField label="Object name" htmlFor="environment-search">
          <StudioInput id="environment-search" placeholder="Find objects…"/>
        </StudioField>
      </StudioSection>
      {!loaded && <StudioEmptyState
        title="Open a project"
        description="Open a project to inspect its environment."
        icon={<FolderOpen size={24}/>}
      />}
      <StudioAlert>Use the native Workspace bridge for real data.</StudioAlert>
      <StudioButton variant="primary" onClick={onRefresh}>Refresh</StudioButton>
    </div>
  </StudioPanel>;
}
```

This is a component example, not a fake project loader. The actual handler must call the typed bridge operation or refresh already authoritative state.

## UI component catalogue

**Containers:** `StudioPanel`, `StudioSection`, `.au-stack`, `.au-row`, `.au-grid`, `.au-card`, `.au-scroll`.

**Buttons:** `StudioButton` (secondary/primary/ghost/danger; sm/md/lg; loading and disabled), `StudioIconButton` (required accessible label).

**Forms:** `StudioField` with matching `htmlFor` and input `id`, `StudioInput`, `StudioSelect`, `StudioTextarea`, `StudioCheckbox`, plus `PropertyField` for native property schemas.

**Navigation:** `StudioTabs`, `StudioListRow`. **Feedback:** `StudioBadge`, `StudioAlert`, `StudioEmptyState`, `StudioSkeleton`, `StudioProgress`.

**Icons:** `lucide-react`, ~14–18px in dense toolbars; do not mix multiple icon families.

## Existing-window migration

1. Inspect current TSX, CSS, native bridge calls and the last handoff. Determine what is genuinely implemented.
2. Match the **First Cinematic Concept** through semantic tokens and existing `StudioPanel` components, not bespoke color values.
3. Keep `Scene` as a native Metal surface; never overlay a generated mock screenshot or change its DOM rect to create more padding.
4. Preserve all current working actions: open project, select entity, valid Inspector edits, Lighting, Materials drafts, Shader Graph, dock persistence.
5. For views with missing backend (e.g., ECS components), show a clearly labeled incomplete or read-only state, never fabricated components.
6. Verify narrow panel and full desktop; write a new handoff.

## Test and screenshot gate

```bash
cd apps/studio-web
npm run typecheck
npm run test:web
npm run build
cd ../..
python3 -m unittest discover -s tools/studio -p 'test_*.py' -v
# Mac host, when native integration changed:
bash tools/studio/verify_mac.sh
```

Test the real WKWebView at ~1440×900 plus narrow docks. Capture UI screenshots *after* the build. Record failures honestly. A queueing/cancelled CI workflow is NOT a successful build.

## Required handoff after EACH stage
Commit changes to `feature/studio-html5` and create a new `docs/handoff/studio-html5/NN-*.md` documenting: what was implemented, public components/tokens added or changed, exact file list, compatibility, actual build/test commands and results, screenshot status, limitations, branch/SHA and next concrete step. Do not merge to main or remove legacy ImGui solely because the design looks finished.

## Project content directory / Assets (Stages 67–68)
The standard resource root in the opened **game project folder** is exactly `content/` (lower-case). `apps/editor/core/Projects.cpp::createProject` creates it for every new project (including all templates and genres), while `StudioSession` creates it on opening older projects if missing. UI calls `studio.assets({})` and receives only paths **relative to `content/`**; it must never ask to scan the whole project. The file scanner ignores symlinks. The content browser uses this root for breadcrumbs, folder navigation and search. `game.json`, `rules.json`, `maps/` and `.aiverse` stay outside it.

Example:
```text
MyGame/
  game.json
  maps/
  rules.json
  content/
    Models/Ship.glb
    Textures/Metal.png
    Audio/Engine.ogg
```

Existing projects are NOT migrated: data outside content remains where it was, and the user consciously moves assets and updates references if needed. The UI reads real file names and sizes only; it does not fake thumbnails or execute file operations in JavaScript.

## Building Generator panel

New dock id: `building_generator` (dock-right), implemented at
`src/panels/generated/BuildingGeneratorPanel.tsx`. Approved Studio UI kit only.
Native bridge calls are `studio.buildingCatalog` (read-only styles),
`studio.buildingInspect` (CPU candidate geometry stats), and
`studio.buildingGenerate` (real GLB in project/content with GUID).
Native ownership: `apps/studio-native/StudioBuildingGeneration.h`,
allowlisted in `StudioSession.cpp`. No browser filesystem access.
Full 3D native Renderer3D preview is not yet wired.
