# AIVERSE Studio HTML5 — Stage 5: AI-first UI component kit

Date: 2026-11-20
Branch: feature/studio-html5
Parent commit: 47f051edc624456b620dcc4aa387c1955cebfce2
Stage commit: see branch history.

## Completed
- Extracted PanelTitle, PropertyField, ViewportPlaceholder from monolithic App.tsx.
- Added semantic theme/tokens.css; key Studio colors now use CSS custom properties.
- Reformatted the previous compressed styles.css for easier AI/human editing.
- Added apps/studio-web/AGENTS.md: one-file-per-panel convention, DTO and bridge workflow, loading/errors/accessibility rules, explicit no-fake-GPU-viewport rule.
- Updated README and handoff markdown code formatting. Native app/ImGui editor untouched.

## Files changed
apps/studio-web/src/App.tsx
apps/studio-web/src/styles.css
apps/studio-web/src/main.tsx
apps/studio-web/src/components/PanelTitle.tsx
apps/studio-web/src/components/PropertyField.tsx
apps/studio-web/src/components/ViewportPlaceholder.tsx
apps/studio-web/src/theme/tokens.css
apps/studio-web/AGENTS.md
apps/studio-web/README.md
docs/handoff/studio-html5/01-web-shell.md
docs/handoff/studio-html5/02-workspace-adapter.md
docs/handoff/studio-html5/03-macos-webview.md
docs/handoff/studio-html5/04-save-history.md
docs/handoff/studio-html5/05-ai-component-kit.md

## Validation
Code committed to GitHub. npm install / npm run typecheck / npm run build NOT RUN here because npm deps and a local checkout are unavailable.
Native C++ and GPU tests NOT RUN.
Required next checks: npm run typecheck && npm run build, then CMake build and CTest on macOS, and manual UI screenshot/docking/keyboard tests.

## Limitations / next stage
- Components extracted, but there is not yet a proper docking library or node graph integration.
- Native GPU viewport missing; Windows/Linux native hosts not implemented.
- Continue migrating remaining editor windows; keep feature parity checklist and a handoff for each step.
- Ensure app/legacy editor builds on Mac before any plan to remove ImGui from production.
