# AIVERSE HTML5 Studio — Stage 60: fix generic component export contract check

Date: 2026-11-20
Branch: feature/studio-html5
Parent: 36e68b464657046beb320484cc21a39e2ebbee91
Commit: see current HEAD.

## Root cause
The design-system test searched for exact `export function Name(`, but the public `StudioTabs<T extends string>(...)` function is generic. The test incorrectly rejected a valid TypeScript/TSX component.

## Fix
- Updated `tools/studio/test_design_system.py` to match both ordinary exported functions and exported functions with a generic parameter clause. Tests still enumerate the same 16 required UI components.
- Source UI library, styling, native GPU bridge, and generator output unchanged.

## Files
tools/studio/test_design_system.py
docs/handoff/studio-html5/60-generic-ui-contract-fix.md

## Verification
Static inspection of committed components identified this test-only mismatch and the new regex was verified against a StudioTabs<T extends string> source declaration. Full Python unittest discovery, npm typecheck/build, macOS screenshots: NOT RUN on the entire repository. No claim of full visual parity.

## Next
Run the combined design-system contract tests in a complete Mac worktree, TypeScript/Vite build and screenshots. If tests reveal any genuine compiler issues, patch them with a new handoff; maintain feature/studio-html5 and do not merge main.
