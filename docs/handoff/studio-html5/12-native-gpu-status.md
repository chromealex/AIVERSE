# AIVERSE Studio HTML5 — Stage 12: GPU readiness and failure feedback

Date: 2026-11-20
Branch: feature/studio-html5
Parent: 5dfcf66e45f3ac6ee8b1020b3af48f6f5cf6f860
Commit: see branch HEAD.

## Implemented
- Native host emits viewport.ready only after first Renderer3D frame returns successfully.
- Renderer creation/submit errors produce viewport.error with serialized JSON payload, not interpolated raw JS/HTML.
- React viewport displays startup/failure state; only hides placeholder after readiness confirmed.
- Native selection.changed shares the same serialized event helper.
- Hidden view resets ready state and still releases GPU at app shutdown.
- No fake frame content, no JS CPU readback or per-frame bridge messaging.

## Files
apps/studio-native/mac/Main.mm
apps/studio-web/src/components/ViewportPlaceholder.tsx
docs/handoff/studio-html5/12-native-gpu-status.md

## Verification
Files committed and branch HEAD verified through GitHub API.
Not built or executed on macOS; runtime behavior not confirmed.
CI was queued when checked. No tests can be reported passed.

## Limits and follow-up
- GPU ready signal is emitted after C++ submission returns, which does not guarantee the frame is visible on screen or GPU work completed; use screenshot/visual validation.
- More sophisticated device-loss handling, backing-scale changes and on-screen compositing still needed.
- Native Scene still uses simplified MapDoc mesh proxies; migrate actual scene geometry/render graph next.
- Subsequent handoff must report compile/test results from Mac and any fixes.
