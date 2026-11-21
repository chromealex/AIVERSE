# AIVERSE HTML5 Studio — Stage 59: cross-agent UI style onboarding

Date: 2026-11-20
Branch: feature/studio-html5
Parent commit: 82994ba7c933c7f3dd9e1fb810c5fe2ef4df10ae
Stage commit: see branch history.

## Implemented
- Added the mandatory Studio UI v1 contract to the root `AGENTS.md`, so any AI agent working on AIVERSE editor code discovers and reuses the same UI library without relying on current chat context.
- Added reusable skill `skills/editor/studio-ui/SKILL.md`: imports, generation command, visual/state design rules, stable engine/Metal/bridge boundaries, responsive guidance, acceptance checks and handoff requirement.
- Extended `tools/studio/test_design_system.py` to check root guidance and skill discovery.
- This stage is documentation/agent onboarding only, no engine behavior change.

## Files
AGENTS.md
skills/editor/studio-ui/SKILL.md
tools/studio/test_design_system.py
docs/handoff/studio-html5/59-agent-style-skill.md

## Actual tests
- Source commit authored. UI Python contract tests registered but complete repo unittest run NOT EXECUTED in this runtime.
- GitHub CI pending at last check; full Vite, macOS WebKit/Metal and visual QA still require user Mac.

## Next
Run Python+Vite tests and visually compare the updated shell across 240px dock panes. Continue redesign of existing Lighting, Materials, Shader Graph, hierarchy and inspector as separate stages, reusing the shared components. Keep ImGui and main unchanged until editor parity.
