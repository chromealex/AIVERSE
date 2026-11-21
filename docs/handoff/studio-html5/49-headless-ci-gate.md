# AIVERSE HTML5 Studio — Stage 49: dependency-light headless CI

Date: 2026-11-20
Branch: feature/studio-html5
Parent commit: 53361c73bd7e2c4875ecd7063e471be0efd54727
Stage commit: see branch HEAD.

## Implemented
- tools/studio/verify_headless.sh runs ImGui boundary, Python agent/scaffolder tests, Node 22 pure TypeScript domain tests and optional standalone tsc strict checks.
- Compiles and executes six independent C++17 tests with warnings as errors: viewport placement, lighting/material policy, graph name and FPS light contract, daylight math.
- The GitHub Actions boundary job now runs this script on Node 22/Python 3.12 without needing React/Vite installation.
- Existing full frontend and macOS native jobs remain separate.

## Files
tools/studio/verify_headless.sh
.github/workflows/studio-html5-web.yml
docs/handoff/studio-html5/49-headless-ci-gate.md

## Tests actually performed
- Verified the exact GitHub blob of shaderGraphLayout.ts (SHA a488cdc2112ae305ad44f56dbe4cb46f20f9d22b) matches the locally tested source.
- Node 22.16 standalone Shader Graph layout/create/delete unit tests: 8/8 PASS.
- Standalone TypeScript strict type check for that source: PASS.
- Independent Shader Graph name-validator C++17 test: PASS.
- The combined script has not yet been run on a complete repository checkout. GitHub Actions/macOS CI still queued, not PASS.

## Limitations / next
- Headless PASS does not establish React/Vite, WKWebView, Metal GPU or complete editor correctness.
- CI must actually schedule runners before reporting verified results.
- Run bash tools/studio/verify_headless.sh in a full worktree, then bash tools/studio/verify_mac.sh on Mac and fix any compiler/runtime errors.
- Retain old ImGui editor until parity. Every subsequent stage needs a separate handoff.
