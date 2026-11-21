# AIVERSE Studio HTML5 — Stage 16: one-pass macOS validation

Date: 2026-11-20
Branch: feature/studio-html5
Parent commit: aefeac2617a2148fe028eb1fbb681897a648320e
Stage commit: see branch HEAD.

## Implemented
- tools/studio/verify_mac.sh performs frontend npm install/typecheck/build, opt-in macOS CMake configure, native host and contract test compilation, and CTest.
- Exits on missing tools, build errors, test failures or non-macOS host.
- Uses an isolated build/studio-html5-verify directory.
- Leaves production ImGui editor untouched.

## Files
tools/studio/verify_mac.sh
docs/handoff/studio-html5/16-one-pass-mac-verification.md

## Run
bash tools/studio/verify_mac.sh

## Test results
Script committed, NOT RUN: current tool environment is Linux without Apple's native SDK.
GitHub Actions was queued at previous checks; no passing status claimed.
After running, record exact exit status, compiler errors and manual visual screenshots in a new handoff.

## Remaining acceptance
- WKWebView bundle loads; Metal native surface visible at correct DOM rect and Retina scale.
- Tab switching, resize and hiding do not leak GPU devices or overdraw HTML.
- Native click picking updates Inspector; orbit/zoom, save/undo/redo and project picker work.
- Native popovers cannot yet overlay the Metal sibling NSView; compositor refactor required.
- Existing Scene renderer features (mesh/material/terrain/water/lighting/VFX) are not at parity.
- Cross-platform WebView2/Vulkan and WebKitGTK/GLES still need implementation.

## Next
Run script on an actual Mac and fix errors; then integrate full AIVERSE scene render path and migrate editor windows. Handoff each stage. Only remove ImGui after the functional parity matrix passes.
