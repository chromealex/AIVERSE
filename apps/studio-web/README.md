# AIVERSE Studio Web

**Status:** experimental HTML5 editor with native macOS WKWebView, a working Metal Renderer3D Scene surface and partial authoring panels. This is the **editor**, not the engine's in-game UI. Keep the existing ImGui editor available until full feature parity.

## Run
```sh
cd apps/studio-web
npm install
npm run typecheck
npm run dev
# open http://127.0.0.1:4173/
```

On the owner’s Mac, run `bash tools/studio/verify_mac.sh` from the repository root.
It requires mounted writable KINGSTON and writes native/frontend output and caches under
`/Volumes/KINGSTON/AIVERSE-builds/studio-html5-verify`. Do not use local build output.
For custom build directories, pass the built frontend directory with
`-DAIVERSE_STUDIO_WEB_DIST=/absolute/path/to/web/dist` to CMake.
Assets use **relative** paths and are refreshed on every `aiverse_studio` build,
including when native sources have not changed.

Full migration status and remaining workflows: [parity matrix](../../docs/studio-html5-parity.md).

## AI-first screen authoring
- Build independent React panels in `src/components/<Name>.tsx`; keep `src/App.tsx` as the host/composition layer.
- Change global palette/spacing in `src/theme/tokens.css`, not by changing every color manually.
- Read `AGENTS.md` before generating a new screen or component.
- Use the reusable `src/ui` design system; classes are `au-*`, tokens are `--au-*`. See `docs/studio-design-system.md`.
- Reuse `StudioPanel`, `StudioButton`, `StudioField`, `StudioSection`, `StudioEmptyState`; keep typed snapshots and bridge methods.
- Never call the engine from React components directly; use `src/bridge.ts`.
- Ask AI to make a new screen, run Vite HMR and inspect in a browser without recompiling C++.
- Keep UI data in TypeScript DTOs; engine state/undo belongs to native Workspace.

## IPC v1
WKWebView handler: `window.webkit.messageHandlers.aiverse.postMessage({id, method, params})`.
Native replies via `window.__aiverseReceive({id, result})` or `{id,error}`.
Events via `window.__aiverseReceive({event:'workspace.changed',payload:{...}})`.

The current browser preview uses a **DEMO-only** in-memory adapter. Do not treat its edits as project edits.

## Migration rules
1. No genre-specific code in the UI infrastructure.
2. No ImGui dependencies in new files.
3. Renderer remains native (Metal/Vulkan/GLES); no bitmap streaming every frame.
4. Security: only trusted locally packaged page allowed to invoke native editor operations.
5. Keep old editor functional until verified feature parity.
6. Each stage must add a handoff markdown in `docs/handoff/studio-html5/`.

## Create a new screen with AI
From the AIVERSE repository root:

```bash
python3 tools/studio/new_panel.py --id world_stats --title "World Statistics" --area right
```

The panel appears in the HTML5 Panels menu after Vite reload, with its TSX/CSS in `apps/studio-web/src/panels/generated/`. No C++ compilation is needed for the UI layout, but operations that mutate the engine still require explicit validated native bridge APIs.

## Shared UI kit for agents
```tsx
import { StudioPanel, StudioBadge, StudioEmptyState } from '../../ui';

export function ExamplePanel() {
  return <StudioPanel title="Example" eyebrow="TOOLS" actions={<StudioBadge>Ready</StudioBadge>}>
    <StudioEmptyState title="Choose an asset" description="Open a project and select an asset."/>
  </StudioPanel>;
}
```
Create a new screen with `python3 tools/studio/new_panel.py --id scene_stats --title "Scene Statistics" --area right`. Its generated TSX already uses the shared styles.
