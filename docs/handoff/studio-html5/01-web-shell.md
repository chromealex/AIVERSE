# AIVERSE Studio Web — Stage 1 handoff

Date: 2026-11-20
Branch: feature/studio-html5
Base: main @ e4129c5062a6ab47e7c70f3fdd483392fde1f679

## Goal
Replace the AIVERSE **editor UI** (not game HUD) with HTML5/CSS/TypeScript. No genre-specific UI. Keep renderer/ECS/Workspace in native C++. Do not remove ImGui until the web editor reaches verified feature parity.

## Implemented
- Isolated React + TypeScript + Vite frontend under apps/studio-web (does not touch CMake or legacy editor).
- A clean, reusable, design-token-driven Studio shell: project header, hierarchy/search, tabbed work area, scene placeholder, inspector with editable fields, bottom console, compact status bar.
- Typed native IPC adapter for WKWebView message handler "aiverse", request IDs, response timeouts, error propagation, event subscriptions.
- Clearly marked DEMO mode with in-memory mock transport; no false claim of native renderer or project writeback.
- Responsive layout and accessible inputs. Viewport has **no GPU contents yet**.
- README documents an AI-first workflow (edit React components + CSS tokens, hot reload).

## API contract (version 1)
Web -> native: `{id:number, method:string, params?:unknown}`
Native -> web through `window.__aiverseReceive({...})`:
- response: `{id:number, result?:unknown, error?:string}`
- event: `{event:string, payload?:unknown}`
Required methods: `studio.snapshot`, `studio.select`, `studio.setProperty`.
Native bridge MUST validate origin and method allowlist. Never expose MCP's unguarded localhost port to arbitrary pages.

## Files
apps/studio-web/package.json
apps/studio-web/index.html
apps/studio-web/tsconfig.json
apps/studio-web/vite.config.ts
apps/studio-web/src/types.ts
apps/studio-web/src/bridge.ts
apps/studio-web/src/mock.ts
apps/studio-web/src/App.tsx
apps/studio-web/src/main.tsx
apps/studio-web/src/styles.css
apps/studio-web/README.md

## Checks
Repository commit/file creation via GitHub API: verified after commit.
Local npm/Vite/TypeScript compile: NOT RUN in this environment (dependencies not installed).
Native integration tests and GPU parity: NOT RUN (native host not implemented).

## Known limitations
- The new frontend currently runs as an independent browser preview with mock project data.
- ImGui remains active in the production editor. There is no native WebView host or 3D texture compositing yet.
- Dock rearrangement, native build window and workspace operations beyond the three methods are still TODO.
- No automatic synchronization of Workspace / selection yet; events protocol is defined only.

## Next stage
1. Add a macOS WKWebView native host in a new independent/opt-in target with strict local content origin and handler.
2. Implement `studio.snapshot`, `studio.select`, `studio.setProperty` through real C++ Workspace/Editor logic, preserving undo validation.
3. Integrate native Scene Render Target (not a JS frame-upload), keyboard focus, DPI, resize, and clipping; measure GPU frame cost.
4. Add unit tests for IPC schema, timeouts, update notifications; run npm build and macOS app compile.
5. Write a **new handoff after each stage** with branch, exact commits, changed files, tests actually executed, limitations, next step.

Commit: see branch HEAD (this handoff is included in the Stage 1 commit).
