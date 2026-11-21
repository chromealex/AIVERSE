# AIVERSE Studio HTML5 — Stage 8: browser-to-native viewport layout protocol

Date: 2026-11-20
Branch: feature/studio-html5
Parent: 900dcacd2f2eae9c62d618f8945b2f45bccb5fe7
Commit: see this branch history.

## Done
- Added typed JS operation studio.viewportRect with scene ID, browser CSS rectangle, pixel ratio and visibility.
- HTML5 viewport measures getBoundingClientRect via ResizeObserver, viewport/tab state, window resize/scroll and document visibility. The protocol is event-driven (no per-frame JS bitmap uploads).
- Browser preview mock acknowledges geometry but has no fake GPU frame.
- Added independent pure C++ layout helper with finite/bounds checks, viewport clipping and top-left web -> bottom-left Cocoa coordinate conversion.
- Added layout contract tests for retina scale, clipping, hidden/invalid and offscreen cases.

## Files
apps/studio-web/src/types.ts
apps/studio-web/src/mock.ts
apps/studio-web/src/App.tsx
apps/studio-web/src/styles.css
apps/studio-web/src/components/ViewportPlaceholder.tsx
apps/studio-native/StudioViewportLayout.h
apps/studio-native/tests/StudioViewportLayoutTests.cpp
docs/handoff/studio-html5/08-viewport-contract.md

## Validation
Source/commit created in GitHub; UI typecheck, CMake build, XCTest and Metal runtime NOT RUN.
The native WKWebView does not yet handle studio.viewportRect: the next stage must connect this method and a GPU-backed native NSView.

## API
Request (trusted WKWebView only):
{"id":N,"method":"studio.viewportRect","params":{"id":"scene","x":250,"y":100,"width":800,"height":500,"dpr":2,"visible":true}}
Result: {"id":N,"result":{"ok":true}}
Never forward arbitrary method names or file URLs to Workspace.

## Next
- Wire native WKWebView handler to viewportPlacement and a new CAMetalLayer NSView.
- Render with AIVERSE gfx Device and Renderer3D from actual Workspace scene data, not CPU image transfer. Handle resize, input, hidden views and lifecycle.
- Add headless C++ test target for viewportPlacement and run on macOS/Linux.
- Document simplified preview limitations honestly; full legacy Scene renderer parity remains future work.
