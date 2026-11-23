# AIVERSE mandatory studio-contract rules

Extracted verbatim from the previous root AGENTS.md; original section numbering and repository-root-relative paths remain intentional. Follow cross-references via the root routing table. These rules are binding, not optional guidance.

## HTML5 Studio UI design system (all future editor windows)
**Approved appearance (user decision 2026-11-20): FIRST Cinematic Concept, not the second stricter/minimalist mockup.** The portable visual specification is [docs/studio-visual-direction.md](docs/studio-visual-direction.md), and the exact path/API workflow is [docs/studio-ui-agent-guide.md](docs/studio-ui-agent-guide.md). Future agents MUST read both before creating editor UI; use the existing `src/ui` kit. A pretty reference screenshot is not evidence of implemented scene assets or ECS features.

For any work under \`apps/studio-web\` or any new editor window, use the shared, genre-independent
**Studio UI v1** style library, not a panel-specific visual language:
- Handbook: \`docs/studio-design-system.md\`; implementation: \`apps/studio-web/src/ui/\`.
- Colors, spacing, typography, control states: \`apps/studio-web/src/theme/tokens.css\` (\`--au-*\` tokens).
- React components: \`StudioPanel\`, \`StudioSection\`, \`StudioButton\`, \`StudioIconButton\`,
  \`StudioField\`, \`StudioInput\`, \`StudioSelect\`, \`StudioBadge\`, \`StudioAlert\`,
  \`StudioEmptyState\`, \`StudioTabs\`, \`StudioListRow\`, etc. Import from \`src/ui\`.
- Generate new panels via \`python3 tools/studio/new_panel.py --id world_stats --title "World Statistics" --area right\`; generated windows already use the UI library.
- Agent skill: \`skills/editor/studio-ui/SKILL.md\`. Every change needs a new stage handoff under \`docs/handoff/studio-html5/\`.
- No raw hard-coded palette/button/input styles in new panels, no ImGui additions and no browser-side project file writes. Preserve the native Renderer3D Metal viewport and \`src/bridge.ts\` allowlist.
- Work on \`feature/studio-html5\` for this migration, not \`main\`; verify \`npm run typecheck && npm run build\`, Python UI contracts, and macOS screenshots before claiming full parity.

