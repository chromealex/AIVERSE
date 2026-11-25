# Studio HTML5 — World Persistence default dock

Code SHA `7750dd3e2` on `codex/open-world-acceptance-20261124`. The registered `world_persistence`, `catalog` and `terraweave_generator` panels are now present in the default right dock. `apps/studio-web/src/nestedDock.ts` owns the layout; `tests/nested-dock.mjs` checks registration, movement and close/reopen behavior. No panel implementation, native bridge or visual style changed.

Mac KINGSTON source-copy checks: `npm run typecheck`, `npm run build`, `npm run test:web` PASS; dock subset 40/40 PASS. Native WebView screenshot and hands-on panel interaction NOT RUN. Detailed evidence and limitations: `docs/agent-handoffs/open-world-stage37-studio-default-dock-2026-11-24.md`.
