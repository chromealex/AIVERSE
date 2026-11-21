# AIVERSE Studio HTML5 — Stage 7: lightweight CI

Date: 2026-11-20
Branch: feature/studio-html5
Parent commit: f70acc12e038444741eb64bdca75e5831d48fc1a
Stage commit: see branch history.

## Completed
- Added .github/workflows/studio-html5-web.yml.
- On changes to Studio web/native/audit, GitHub Actions runs Node 22 npm install, TypeScript typecheck, Vite build.
- Independent Python job checks the new native Studio for ImGui dependency regressions and runs audit unit tests.
- Existing heavyweight editor CI not modified.

## Changes
.github/workflows/studio-html5-web.yml
docs/handoff/studio-html5/07-lightweight-ci.md

## Verification / caveats
Workflow file committed. GitHub Actions execution status must be independently inspected: creating YAML is NOT proof of test success.
No macOS WKWebView/C++ binary test in this lightweight workflow. See Stage 3 handoff for macOS commands.
The dependency lockfile is not yet committed; npm install may choose updated compatible package versions. Commit a generated npm lockfile after a successful install.

## Next
- Inspect run results and fix failures.
- Test macOS binary and C++ StudioSession with real project.
- Next major implementation: native 3D Scene viewport in HTML5 UI, followed by per-window ImGui migrations.
