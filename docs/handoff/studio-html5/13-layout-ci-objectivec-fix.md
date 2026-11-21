# AIVERSE Studio HTML5 — Stage 13: headless viewport CI and ObjC ivar fix

Date: 2026-11-20
Branch: feature/studio-html5
Parent: adc1d98df18611e728199e7e0bc118f0abcde1d0
Stage commit: see branch history.

## Changed
- Native Objective-C++ ivar readyEmitted_ is now plain BOOL without an in-class initializer. Cocoa zeroes ivars on allocation.
- Existing GitHub Actions boundary job now compiles and runs the pure, genre-neutral viewportPlacement C++17 contract independently of the full engine/Apple frameworks.
- Test enforces -Wall -Wextra -Werror to catch portability regressions.

## Files
apps/studio-native/mac/Main.mm
.github/workflows/studio-html5-web.yml
docs/handoff/studio-html5/13-layout-ci-objectivec-fix.md

## Tests
Committed GitHub sources. CI jobs are queued/pending at the time of writing; no PASS claimed.
No direct macOS SDK / GPU validation performed. Run Stage 3 handoff commands before merging.

## Next
Check GitHub Actions outcome for Stage 13, fix any headless/TypeScript issues. Build macOS app, inspect native viewport rendering/selection/popover overlap in real window, update handoff with evidence. Then replace MapDoc primitives with full AIVERSE scene renderer pipeline and port legacy ImGui windows.
